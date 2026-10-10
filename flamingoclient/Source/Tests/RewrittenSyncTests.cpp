/**
 *  RewrittenSyncTests.cpp
 *
 *  消息改写离线同步的客户端测试，标签 [rewrittensync]。方案见 docs/消息改写离线同步方案.md 第 6 节：
 *  服务端改写已发出的消息或撤回消息时分配一个全局递增的改写序号，客户端登录后用 cmd_sync_rewritten_messages（1526）
 *  按本地水位逐页拉取离线期间被改写过的消息，只覆盖本地库已有的行。
 *
 *  覆盖：
 *    RWC1  请求编码：since 以字符串编码，limit 原样写入
 *    RWC2  应答解析：条目复用 cmd_sync_messages 的消息体解析，另取 rev；应答的 rev / more / currev 正确；
 *          缺字段时取 0 / false；code 非 0 时只填 code；缺 msgid 的脏条目跳过
 *    RWC3  应用一页：本地已有的行整行覆盖（content、原始 JSON、搜索文本、撤回状态都更新，置顶保留），本地没有的不插入
 *    RWC4  水位：整页成功才推进到应答的 rev；落库失败或应答 code 非 0 时不动；只增不减
 *    RWC5  首次同步：水位为 0 且本地库为空时直接设成 currev、不再续拉；本地库不空时照常逐页拉取
 *    RWC6  水位按账号隔离：存在各自账号的本地库里，互不影响
 *    RWC7  控制器：开始时按水位发第一页，more 时以新水位续拉，没有更多时结束；重新开始（断线重连）从存下的水位接着来；
 *          没有进行中的一轮时忽略应答；服务端回错误码时本轮结束
 *    RWC8  受影响的会话开着窗口时刷新、没开的不刷新；一页里同一会话多条只刷新一次
 *
 *  用 Catch2 v3（amalgamated）作框架。全局 _Module 由 BalloonChatLayoutTests.cpp 统一定义，本文件只引用。
 *
 *  balloonwj@qq.com   2026-10-08
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <string>
#include <vector>

#include "../Feature/RewrittenSync/RewrittenSync.h"
#include "../net/MessageProtocolClient.h"
#include "../net/Protocol.h"
#include "../UI/BalloonHistory/V2MsgQuery.h"   // HistoryRecord / RecordFromNotify
#include "../UI/BalloonHistory/V2MsgStore.h"   // CV2MsgStore
#include "../third-party/jsoncpp-1.9.0/json.h"

using namespace net;
using namespace rewrittensync;

namespace {

//测试用的改写序号与 limit，取值无特殊含义；序号故意超过 32 位，确认按 64 位读写
const int64_t kSince = 4294967301LL;
const int kLimit = 150;

//解析 JSON 对象，失败或不是对象返回 false
bool ParseObject(const std::string& text, Json::Value& out)
{
    Json::CharReaderBuilder b;
    JSONCPP_STRING errs;
    Json::CharReader* reader = b.newCharReader();
    const bool ok = reader->parse(text.c_str(), text.c_str() + text.size(), &out, &errs);
    delete reader;
    return ok && out.isObject();
}

//本人、单聊对方与另一个单聊对方的 userid，以及一个群的 id；取值无特殊含义
const int64_t kSelf = 100001;
const int64_t kPeerB = 100002;
const int64_t kPeerC = 100003;
const int64_t kGroupG = 268435457LL;
//消息时间（Unix 毫秒），固定取值
const int64_t kMsgTime = 1785000000000LL;
//置顶时间（Unix 毫秒），固定取值
const int64_t kPinTime = 1785000001000LL;

//本进程专用的临时本地库路径（先删掉同名旧库与 WAL 残留）
std::wstring FreshTempDb(const wchar_t* name)
{
    wchar_t dir[MAX_PATH] = { 0 };
    ::GetTempPathW(MAX_PATH, dir);
    wchar_t prefix[32] = { 0 };
    ::swprintf_s(prefix, L"%u_", static_cast<unsigned int>(::GetCurrentProcessId()));
    std::wstring path = dir;
    path += prefix;
    path += name;
    ::DeleteFileW(path.c_str());
    ::DeleteFileW((path + L"-wal").c_str());
    ::DeleteFileW((path + L"-shm").c_str());
    return path;
}

//删掉临时本地库与 WAL 残留
void RemoveTempDb(const std::wstring& path)
{
    ::DeleteFileW(path.c_str());
    ::DeleteFileW((path + L"-wal").c_str());
    ::DeleteFileW((path + L"-shm").c_str());
}

//一条文字消息的 JSON（cmd_sync_messages 的条目格式）；rev 大于 0 时带上 rev
std::string MessageJson(int64_t msgid, int ctype, int64_t cid, int64_t from, const char* text, bool recalled,
                        int64_t rev)
{
    Json::Value m;
    m["msgid"] = std::to_string(msgid);
    m["clientmsgid"] = "c" + std::to_string(msgid);
    m["ctype"] = ctype;
    m["cid"] = std::to_string(cid);
    m["from"] = std::to_string(from);
    m["to"] = std::to_string(ctype == conversation_group ? cid : kSelf);
    m["msgtype"] = message_type_text;
    Json::Value content;
    content["text"] = text;
    m["content"] = content;
    m["mentions"] = Json::Value(Json::arrayValue);
    m["mentionall"] = false;
    m["timestamp"] = static_cast<Json::Int64>(kMsgTime + msgid);
    m["recalled"] = recalled;
    if (rev > 0)
        m["rev"] = std::to_string(rev);
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    return Json::writeString(wb, m);
}

//把一条文字消息写进本地库（与同步历史落库同一个转换）
void StoreText(balloonhistory::CV2MsgStore& store, int64_t msgid, int ctype, int64_t cid, int64_t from,
               const char* text)
{
    MessageNotify notify;
    REQUIRE(MessageProtocolClient::parseMessageNotify(MessageJson(msgid, ctype, cid, from, text, false, 0), notify));
    REQUIRE(store.Upsert(balloonhistory::RecordFromNotify(notify, ctype, cid, kSelf)));
}

//组一页应答并解析成 SyncRewrittenResult；items 为各条消息的 JSON，用逗号连接
SyncRewrittenResult MakePage(const std::string& itemsJson, int64_t rev, bool more, int64_t currev, int code = 0)
{
    std::string body = "{\"code\":" + std::to_string(code) + ",\"msg\":\"ok\",\"items\":[" + itemsJson
                       + "],\"rev\":\"" + std::to_string(rev) + "\",\"more\":" + (more ? "true" : "false")
                       + ",\"currev\":\"" + std::to_string(currev) + "\"}";
    SyncRewrittenResult page;
    REQUIRE(MessageProtocolClient::parseSyncRewrittenResponse(body, page));
    return page;
}

//本地库里的水位
int64_t StoredWatermark(balloonhistory::CV2MsgStore& store)
{
    return ReadWatermark(store);
}

//记录控制器调用的假宿主
class FakeRewrittenHost : public IRewrittenSyncHost
{
public:
    //发出的请求：since 序列
    std::vector<int64_t> m_requests;
    //发出请求时带的 limit（最后一次）
    int m_lastLimit;
    //开着窗口的会话：每项为（会话类型，会话 id）
    std::vector<std::pair<int, int64_t> > m_shown;
    //被刷新的会话，按调用先后
    std::vector<std::pair<int, int64_t> > m_refreshed;
    //每次刷新带的 msgid，与 m_refreshed 一一对应
    std::vector<std::vector<int64_t> > m_refreshedMsgids;

    FakeRewrittenHost()
        : m_lastLimit(0)
    {
    }

    virtual void SendRewrittenRequest(int64_t since, int limit)
    {
        m_requests.push_back(since);
        m_lastLimit = limit;
    }

    virtual bool IsConversationShown(int ctype, int64_t cid)
    {
        for (size_t i = 0; i < m_shown.size(); ++i)
        {
            if (m_shown[i].first == ctype && m_shown[i].second == cid)
                return true;
        }
        return false;
    }

    virtual void RefreshConversation(int ctype, int64_t cid, const std::vector<int64_t>& msgids)
    {
        m_refreshed.push_back(std::make_pair(ctype, cid));
        m_refreshedMsgids.push_back(msgids);
    }
};

} // anonymous namespace

TEST_CASE("RWC1 请求编码：since 以字符串编码，limit 原样写入", "[rewrittensync]")
{
    Json::Value root;
    REQUIRE(ParseObject(MessageProtocolClient::buildSyncRewrittenRequest(kSince, kLimit), root));
    REQUIRE(root["since"].isString());
    CHECK(root["since"].asString() == "4294967301");
    REQUIRE(root["limit"].isInt());
    CHECK(root["limit"].asInt() == kLimit);

    //首次同步：since 为 "0"
    REQUIRE(ParseObject(MessageProtocolClient::buildSyncRewrittenRequest(0, kLimit), root));
    CHECK(root["since"].asString() == "0");

    CHECK(cmd_sync_rewritten_messages == 1526);
}

TEST_CASE("RWC2 应答解析：条目与 1502 同口径，rev / more / currev 正确，缺字段取默认值", "[rewrittensync]")
{
    SECTION("成功应答：两条，一条单聊一条群聊，第二条已撤回")
    {
        const std::string body =
            "{\"code\":0,\"msg\":\"ok\",\"rev\":\"4294967310\",\"more\":true,\"currev\":\"4294967400\","
            "\"items\":["
            "{\"msgid\":\"501\",\"clientmsgid\":\"c1\",\"ctype\":1,\"cid\":\"100002\",\"from\":\"100002\","
            "\"to\":\"100001\",\"msgtype\":22,\"content\":{\"title\":\"t\",\"deleted\":true},\"mentions\":[],"
            "\"mentionall\":false,\"timestamp\":1785000000000,\"recalled\":false,\"rev\":\"4294967309\"},"
            "{\"msgid\":\"502\",\"clientmsgid\":\"c2\",\"ctype\":2,\"cid\":\"268435457\",\"from\":\"100003\","
            "\"to\":\"268435457\",\"msgtype\":1,\"content\":{\"text\":\"x\"},\"mentions\":[],\"mentionall\":false,"
            "\"timestamp\":1785000000001,\"recalled\":true,\"recalledby\":\"100003\",\"rev\":\"4294967310\"}"
            "]}";
        SyncRewrittenResult r;
        REQUIRE(MessageProtocolClient::parseSyncRewrittenResponse(body, r));
        CHECK(r.m_code == 0);
        CHECK(r.m_rev == 4294967310LL);
        CHECK(r.m_more);
        CHECK(r.m_currev == 4294967400LL);
        REQUIRE(r.m_items.size() == 2);

        CHECK(r.m_items[0].m_rev == 4294967309LL);
        CHECK(r.m_items[0].m_msg.m_msgid == 501);
        CHECK(r.m_items[0].m_msg.m_ctype == 1);
        CHECK(r.m_items[0].m_msg.m_cid == 100002);
        CHECK(r.m_items[0].m_msg.m_msgtype == 22);

        CHECK(r.m_items[1].m_rev == 4294967310LL);
        CHECK(r.m_items[1].m_msg.m_msgid == 502);
        CHECK(r.m_items[1].m_msg.m_ctype == 2);
        CHECK(r.m_items[1].m_msg.m_cid == 268435457LL);
        CHECK(r.m_items[1].m_msg.m_recalled);
    }

    SECTION("缺字段：rev / currev 取 0，more 取 false，没有 items 时为空")
    {
        SyncRewrittenResult r;
        REQUIRE(MessageProtocolClient::parseSyncRewrittenResponse("{\"code\":0}", r));
        CHECK(r.m_code == 0);
        CHECK(r.m_rev == 0);
        CHECK_FALSE(r.m_more);
        CHECK(r.m_currev == 0);
        CHECK(r.m_items.empty());
    }

    SECTION("缺 msgid 的脏条目跳过，其余照常")
    {
        const std::string body =
            "{\"code\":0,\"rev\":\"9\",\"more\":false,\"currev\":\"9\",\"items\":["
            "{\"from\":\"100002\",\"msgtype\":1,\"content\":{\"text\":\"bad\"},\"rev\":\"8\"},"
            "{\"msgid\":\"600\",\"from\":\"100002\",\"ctype\":1,\"cid\":\"100002\",\"msgtype\":1,"
            "\"content\":{\"text\":\"ok\"},\"rev\":\"9\"}]}";
        SyncRewrittenResult r;
        REQUIRE(MessageProtocolClient::parseSyncRewrittenResponse(body, r));
        REQUIRE(r.m_items.size() == 1);
        CHECK(r.m_items[0].m_msg.m_msgid == 600);
        CHECK(r.m_items[0].m_rev == 9);
    }

    SECTION("失败应答：只填 code")
    {
        SyncRewrittenResult r;
        REQUIRE(MessageProtocolClient::parseSyncRewrittenResponse("{\"code\":3,\"msg\":\"bad request\"}", r));
        CHECK(r.m_code == 3);
        CHECK(r.m_items.empty());
    }

    SECTION("不是 JSON 对象、或没有整型 code：解析失败")
    {
        SyncRewrittenResult r;
        CHECK_FALSE(MessageProtocolClient::parseSyncRewrittenResponse("not json", r));
        CHECK_FALSE(MessageProtocolClient::parseSyncRewrittenResponse("{\"msg\":\"ok\"}", r));
    }
}

TEST_CASE("RWC3 应用一页：本地已有的行整行覆盖，置顶保留；本地没有的不插入", "[rewrittensync]")
{
    const std::wstring path = FreshTempDb(L"flamingo_rwc3.db");
    {
        balloonhistory::CV2MsgStore store;
        REQUIRE(store.Open(path.c_str()));
        const int64_t kOld = 701;
        const int64_t kMissing = 702;
        StoreText(store, kOld, conversation_single, kPeerB, kPeerB, "old-body");
        bool pinHit = false;
        REQUIRE(store.SetPinned(conversation_single, kPeerB, kOld, true, kPinTime, pinHit));
        REQUIRE(pinHit);

        const SyncRewrittenResult page = MakePage(
            MessageJson(kOld, conversation_single, kPeerB, kPeerB, "new-body", true, 5) + ","
                + MessageJson(kMissing, conversation_single, kPeerB, kPeerB, "not-local", false, 6),
            6, false, 6);
        RewrittenPageOutcome outcome;
        REQUIRE(ApplyRewrittenPage(store, page, kSelf, false, outcome));
        CHECK(outcome.m_applied == 1);
        CHECK(outcome.m_skipped == 1);
        CHECK_FALSE(outcome.m_jumped);
        REQUIRE(outcome.m_affected.size() == 1);
        CHECK(outcome.m_affected[0].m_ctype == conversation_single);
        CHECK(outcome.m_affected[0].m_cid == kPeerB);

        balloonhistory::HistoryRecord row;
        bool found = false;
        REQUIRE(store.QueryRecord(conversation_single, kPeerB, kOld, row, found));
        REQUIRE(found);
        //正文用纯 ASCII：jsoncpp 1.9.0 把非 ASCII 字符一律写成 \uXXXX，content 列与原始 JSON 里按中文字面查不到
        CHECK(row.m_content.find("new-body") != std::string::npos);
        CHECK(row.m_content.find("old-body") == std::string::npos);
        CHECK(row.m_raw.find("new-body") != std::string::npos);
        CHECK(row.m_raw.find("old-body") == std::string::npos);
        CHECK(row.m_searchText.find("new-body") != std::string::npos);
        CHECK(row.m_searchText.find("old-body") == std::string::npos);
        CHECK(row.m_recalled);
        CHECK(row.m_pinned);

        REQUIRE(store.QueryRecord(conversation_single, kPeerB, kMissing, row, found));
        CHECK_FALSE(found);
        store.Close();
    }
    RemoveTempDb(path);
}

TEST_CASE("RWC4 水位：整页成功才推进，失败或错误码时不动，只增不减", "[rewrittensync]")
{
    const std::wstring path = FreshTempDb(L"flamingo_rwc4.db");
    {
        balloonhistory::CV2MsgStore store;
        REQUIRE(store.Open(path.c_str()));
        CHECK(StoredWatermark(store) == 0);
        StoreText(store, 801, conversation_single, kPeerB, kPeerB, "old");

        //成功：推进到应答的 rev
        RewrittenPageOutcome outcome;
        REQUIRE(ApplyRewrittenPage(store, MakePage(MessageJson(801, conversation_single, kPeerB, kPeerB, "new", false, 10),
                                                   10, false, 10),
                                   kSelf, false, outcome));
        CHECK(outcome.m_watermark == 10);
        CHECK(StoredWatermark(store) == 10);

        //应答 code 非 0：返回失败，水位不动
        CHECK_FALSE(ApplyRewrittenPage(store, MakePage("", 20, false, 20, 3), kSelf, false, outcome));
        CHECK(StoredWatermark(store) == 10);

        //只增不减：比现有水位小的 rev 不改水位
        REQUIRE(ApplyRewrittenPage(store, MakePage("", 7, false, 10), kSelf, false, outcome));
        CHECK(StoredWatermark(store) == 10);

        //落库失败（库已关闭）：返回失败；重新打开后水位仍是 10
        store.Close();
        CHECK_FALSE(ApplyRewrittenPage(store, MakePage(MessageJson(801, conversation_single, kPeerB, kPeerB, "x", false, 30),
                                                       30, false, 30),
                                       kSelf, false, outcome));
        REQUIRE(store.Open(path.c_str()));
        CHECK(StoredWatermark(store) == 10);
        store.Close();
    }
    RemoveTempDb(path);
}

TEST_CASE("RWC5 首次同步：本地库为空时直接跳到 currev，不空时逐页拉取", "[rewrittensync]")
{
    SECTION("本地库为空：第一页直接把水位设成 currev，不再续拉")
    {
        const std::wstring path = FreshTempDb(L"flamingo_rwc5a.db");
        {
            balloonhistory::CV2MsgStore store;
            REQUIRE(store.Open(path.c_str()));
            FakeRewrittenHost host;
            CRewrittenSyncController ctrl(&host);
            ctrl.Start(store);
            REQUIRE(host.m_requests.size() == 1);
            CHECK(host.m_requests[0] == 0);

            //服务端说后面还有，但本地库为空，没有必要继续
            ctrl.OnPage(store, MakePage(MessageJson(901, conversation_single, kPeerB, kPeerB, "x", false, 3), 3, true, 500),
                        kSelf);
            CHECK(StoredWatermark(store) == 500);
            CHECK(host.m_requests.size() == 1);
            CHECK_FALSE(ctrl.IsRunning());
            store.Close();
        }
        RemoveTempDb(path);
    }

    SECTION("本地库不空：照常逐页拉取")
    {
        const std::wstring path = FreshTempDb(L"flamingo_rwc5b.db");
        {
            balloonhistory::CV2MsgStore store;
            REQUIRE(store.Open(path.c_str()));
            StoreText(store, 902, conversation_single, kPeerB, kPeerB, "old");
            FakeRewrittenHost host;
            CRewrittenSyncController ctrl(&host);
            ctrl.Start(store);
            REQUIRE(host.m_requests.size() == 1);

            ctrl.OnPage(store, MakePage(MessageJson(902, conversation_single, kPeerB, kPeerB, "new", false, 3), 3, true, 500),
                        kSelf);
            CHECK(StoredWatermark(store) == 3);
            REQUIRE(host.m_requests.size() == 2);
            CHECK(host.m_requests[1] == 3);
            CHECK(ctrl.IsRunning());
            store.Close();
        }
        RemoveTempDb(path);
    }
}

TEST_CASE("RWC6 水位按账号隔离：存在各自账号的本地库里", "[rewrittensync]")
{
    const std::wstring pathA = FreshTempDb(L"flamingo_rwc6_a.db");
    const std::wstring pathB = FreshTempDb(L"flamingo_rwc6_b.db");
    {
        balloonhistory::CV2MsgStore storeA;
        balloonhistory::CV2MsgStore storeB;
        REQUIRE(storeA.Open(pathA.c_str()));
        REQUIRE(storeB.Open(pathB.c_str()));
        StoreText(storeA, 1001, conversation_single, kPeerB, kPeerB, "a");
        RewrittenPageOutcome outcome;
        REQUIRE(ApplyRewrittenPage(storeA, MakePage("", 42, false, 42), kSelf, false, outcome));
        CHECK(StoredWatermark(storeA) == 42);
        CHECK(StoredWatermark(storeB) == 0);
        storeA.Close();
        storeB.Close();
    }
    RemoveTempDb(pathA);
    RemoveTempDb(pathB);
}

TEST_CASE("RWC7 控制器：按水位开始，more 时续拉，结束后重新开始从存下的水位接着来", "[rewrittensync]")
{
    const std::wstring path = FreshTempDb(L"flamingo_rwc7.db");
    {
        balloonhistory::CV2MsgStore store;
        REQUIRE(store.Open(path.c_str()));
        StoreText(store, 1101, conversation_single, kPeerB, kPeerB, "a");
        REQUIRE(store.WriteSyncValue(kWatermarkKey, 7));

        FakeRewrittenHost host;
        CRewrittenSyncController ctrl(&host);

        //没有进行中的一轮：应答被忽略，不发请求、不改水位
        ctrl.OnPage(store, MakePage("", 99, true, 99), kSelf);
        CHECK(host.m_requests.empty());
        CHECK(StoredWatermark(store) == 7);

        //登录：从水位 7 开始，每页取上限条数
        ctrl.Start(store);
        REQUIRE(host.m_requests.size() == 1);
        CHECK(host.m_requests[0] == 7);
        CHECK(host.m_lastLimit == kPageLimit);

        //第一页带 more：以新水位 9 续拉
        ctrl.OnPage(store, MakePage(MessageJson(1101, conversation_single, kPeerB, kPeerB, "b", false, 9), 9, true, 11),
                    kSelf);
        REQUIRE(host.m_requests.size() == 2);
        CHECK(host.m_requests[1] == 9);

        //第二页没有更多：本轮结束，水位 11
        ctrl.OnPage(store, MakePage(MessageJson(1101, conversation_single, kPeerB, kPeerB, "c", false, 11), 11, false, 11),
                    kSelf);
        CHECK(host.m_requests.size() == 2);
        CHECK_FALSE(ctrl.IsRunning());
        CHECK(StoredWatermark(store) == 11);

        //断线重连后重新开始：从存下的水位 11 接着来
        ctrl.Start(store);
        REQUIRE(host.m_requests.size() == 3);
        CHECK(host.m_requests[2] == 11);

        //服务端回错误码：本轮结束，不续拉，水位不动
        ctrl.OnPage(store, MakePage("", 0, true, 0, 3), kSelf);
        CHECK_FALSE(ctrl.IsRunning());
        CHECK(host.m_requests.size() == 3);
        CHECK(StoredWatermark(store) == 11);

        //应答说还有、水位却没有前进：本轮结束，不反复请求同一页
        ctrl.Start(store);
        REQUIRE(host.m_requests.size() == 4);
        ctrl.OnPage(store, MakePage("", 11, true, 20), kSelf);
        CHECK_FALSE(ctrl.IsRunning());
        CHECK(host.m_requests.size() == 4);

        //登出：Reset 之后的应答被忽略
        ctrl.Start(store);
        ctrl.Reset();
        const size_t before = host.m_requests.size();
        ctrl.OnPage(store, MakePage("", 50, true, 50), kSelf);
        CHECK(host.m_requests.size() == before);
        CHECK(StoredWatermark(store) == 11);
        store.Close();
    }
    RemoveTempDb(path);
}

TEST_CASE("RWC8 受影响且开着的会话才刷新，一页里同一会话只刷新一次", "[rewrittensync]")
{
    const std::wstring path = FreshTempDb(L"flamingo_rwc8.db");
    {
        balloonhistory::CV2MsgStore store;
        REQUIRE(store.Open(path.c_str()));
        //单聊 B 两条、群 G 一条在本地库里；单聊 C 那条本地没有
        StoreText(store, 1201, conversation_single, kPeerB, kPeerB, "b1");
        StoreText(store, 1202, conversation_single, kPeerB, kSelf, "b2");
        StoreText(store, 1203, conversation_group, kGroupG, kPeerC, "g1");

        FakeRewrittenHost host;
        //B 与 C 的聊天窗开着，G 没开
        host.m_shown.push_back(std::make_pair(static_cast<int>(conversation_single), kPeerB));
        host.m_shown.push_back(std::make_pair(static_cast<int>(conversation_single), kPeerC));
        CRewrittenSyncController ctrl(&host);
        ctrl.Start(store);

        const std::string items = MessageJson(1201, conversation_single, kPeerB, kPeerB, "b1-new", false, 21) + ","
                                  + MessageJson(1203, conversation_group, kGroupG, kPeerC, "g1-new", false, 22) + ","
                                  + MessageJson(1202, conversation_single, kPeerB, kSelf, "b2-new", true, 23) + ","
                                  + MessageJson(1204, conversation_single, kPeerC, kPeerC, "c-missing", false, 24);
        ctrl.OnPage(store, MakePage(items, 24, false, 24), kSelf);

        //只刷新了 B 一次：G 没开窗口，C 那条本地没有、不算受影响
        REQUIRE(host.m_refreshed.size() == 1);
        CHECK(host.m_refreshed[0].first == conversation_single);
        CHECK(host.m_refreshed[0].second == kPeerB);
        //带上 B 这一页里被改写的两条，按出现先后
        REQUIRE(host.m_refreshedMsgids[0].size() == 2);
        CHECK(host.m_refreshedMsgids[0][0] == 1201);
        CHECK(host.m_refreshedMsgids[0][1] == 1202);
        store.Close();
    }
    RemoveTempDb(path);
}
