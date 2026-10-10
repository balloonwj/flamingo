/**
 *  接龙变更推送合并进本地消息库的单元测试（Suite RNA），RelayNotifyApplyTests.cpp
 *
 *  覆盖 UI/BalloonMain/RelayNotifyApply 的 ApplyRelayNotifyToStore（docs/群接龙方案.md 2d），
 *  对着真的 SQLite 库读写：
 *    - RNA1 本地有这张卡片：合并后 content 列更新，返回合并后的完整内容；
 *    - RNA2 本地没有这张卡片：不报错、不凭空建行；
 *    - RNA3 本地那一行取不出卡片内容：返回 false，content 列不动。
 *
 *  标签 [grouprelay]。框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-02
 */
#include "catch_amalgamated.hpp"

#include <string>
#include <windows.h>

#include "../UI/BalloonMain/RelayNotifyApply.h"
#include "../UI/BalloonHistory/V2MsgQuery.h"
#include "../UI/BalloonHistory/V2MsgStore.h"
#include "../net/CardProtocolClient.h"
#include "../net/MessageProtocolClient.h"
#include "../net/Protocol.h"   // conversation_group、message_type_relay、relay_state
#include "../third-party/jsoncpp-1.9.0/json.h"

namespace {

//本组用例用到的群、卡片与参与者
const int64_t kGroupId = 600001;
const int64_t kCardMsgId = 9007199254741011LL;
const int64_t kCreator = 100001;
const int64_t kUserA = 100008;
const int64_t kUserB = 100009;
const int64_t kDeadlineMs = 1759485600000LL;

//临时库的全路径：带上进程号，两份测试进程同时跑时不会共用一个文件；先删掉同名旧文件
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
    //本地库是 WAL 模式：上次异常退出残留的日志文件会被套用到同名新库上，一并删掉
    ::DeleteFileW((path + L"-wal").c_str());
    ::DeleteFileW((path + L"-shm").c_str());
    return path;
}

//照服务端同步下发的格式构造一条接龙卡片消息并入库，名单里只有张三
void UpsertCard(balloonhistory::CV2MsgStore& store)
{
    Json::Value item;
    item["userid"] = std::to_string(kUserA);
    item["text"]   = u8"本人+1";
    Json::Value content;
    content["title"]    = u8"周五团建报名";
    content["note"]     = u8"格式：姓名";
    content["deadline"] = static_cast<Json::Int64>(kDeadlineMs);
    content["items"]    = Json::Value(Json::arrayValue);
    content["items"].append(item);
    content["state"]    = relay_state_open;
    Json::Value msg;
    msg["msgid"]     = std::to_string(kCardMsgId);
    msg["ctype"]     = conversation_group;
    msg["cid"]       = std::to_string(kGroupId);
    msg["from"]      = std::to_string(kCreator);
    msg["to"]        = std::to_string(kGroupId);
    msg["msgtype"]   = message_type_relay;
    msg["content"]   = content;
    msg["timestamp"] = static_cast<Json::Int64>(1759400000000LL);
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";

    net::MessageNotify received;
    REQUIRE(net::MessageProtocolClient::parseMessageNotify(Json::writeString(wb, msg), received));
    REQUIRE(store.Upsert(balloonhistory::RecordFromNotify(received, conversation_group, kGroupId)));
}

//一次变更推送：李四参与，发起人随即结束了接龙
net::RelayChangedNotify MakeNotify()
{
    net::RelayChangedNotify notify;
    notify.m_ctype = conversation_group;
    notify.m_cid   = kGroupId;
    notify.m_msgid = kCardMsgId;
    net::RelayItem a;
    a.m_userid = kUserA;
    a.m_text   = u8"本人+1";
    net::RelayItem b;
    b.m_userid = kUserB;
    b.m_text   = u8"不带";
    notify.m_items.push_back(a);
    notify.m_items.push_back(b);
    notify.m_state     = relay_state_closed;
    notify.m_closedBy  = kCreator;
    notify.m_closeTime = 1759401000000LL;
    return notify;
}

} // anonymous namespace

TEST_CASE("RNA1 本地有这张卡片：合并进 content 列并返回合并后的完整内容", "[grouprelay]")
{
    std::wstring path = FreshTempDb(L"flamingo_rna1.db");
    balloonhistory::CV2MsgStore store;
    REQUIRE(store.Open(path.c_str()));
    UpsertCard(store);

    std::string mergedJson;
    bool hit = false;
    REQUIRE(balloonmain::ApplyRelayNotifyToStore(store, MakeNotify(), mergedJson, hit));
    CHECK(hit);

    //返回的内容：名单与状态是新的，标题、说明、截止时间不变
    net::RelayCardContent merged;
    REQUIRE(net::CardProtocolClient::parseRelayCardContent(mergedJson, merged));
    REQUIRE(merged.m_items.size() == 2);
    CHECK(merged.m_items[1].m_userid == kUserB);
    CHECK(merged.m_state == relay_state_closed);
    CHECK(merged.m_closedBy == kCreator);
    CHECK(merged.m_title == u8"周五团建报名");
    CHECK(merged.m_note == u8"格式：姓名");
    CHECK(merged.m_deadline == kDeadlineMs);

    //本地库里的那一行：还原出来也是新内容，原始JSON保持原样（名单里仍只有一条）
    balloonhistory::HistoryRecord row;
    bool found = false;
    REQUIRE(store.QueryRecord(conversation_group, kGroupId, kCardMsgId, row, found));
    REQUIRE(found);
    net::MessageNotify restored;
    REQUIRE(balloonhistory::NotifyFromRecord(row, restored));
    net::RelayCardContent restoredCard;
    REQUIRE(net::CardProtocolClient::parseRelayCardContent(restored.m_relayContent, restoredCard));
    CHECK(restoredCard.m_items.size() == 2);
    net::MessageNotify fromRaw;
    REQUIRE(net::MessageProtocolClient::parseMessageNotify(row.m_raw, fromRaw));
    net::RelayCardContent rawCard;
    REQUIRE(net::CardProtocolClient::parseRelayCardContent(fromRaw.m_relayContent, rawCard));
    CHECK(rawCard.m_items.size() == 1);

    store.Close();
    ::DeleteFileW(path.c_str());
}

TEST_CASE("RNA2 本地没有这张卡片：返回成功、不建行", "[grouprelay]")
{
    std::wstring path = FreshTempDb(L"flamingo_rna2.db");
    balloonhistory::CV2MsgStore store;
    REQUIRE(store.Open(path.c_str()));

    std::string mergedJson = "untouched";
    bool hit = true;
    REQUIRE(balloonmain::ApplyRelayNotifyToStore(store, MakeNotify(), mergedJson, hit));
    CHECK_FALSE(hit);
    CHECK(mergedJson == "untouched");

    balloonhistory::HistoryRecord row;
    bool found = true;
    REQUIRE(store.QueryRecord(conversation_group, kGroupId, kCardMsgId, row, found));
    CHECK_FALSE(found);

    store.Close();
    ::DeleteFileW(path.c_str());
}

TEST_CASE("RNA3 本地那一行取不出卡片内容：返回 false，content 列不动", "[grouprelay]")
{
    std::wstring path = FreshTempDb(L"flamingo_rna3.db");
    balloonhistory::CV2MsgStore store;
    REQUIRE(store.Open(path.c_str()));

    //一行没有卡片内容的记录：content 列是空对象、也没有原始JSON
    balloonhistory::HistoryRecord rec;
    rec.m_msgid     = kCardMsgId;
    rec.m_ctype     = conversation_group;
    rec.m_cid       = kGroupId;
    rec.m_fromid    = kCreator;
    rec.m_msgtype   = message_type_relay;
    rec.m_content   = "{}";
    rec.m_timestamp = 1759400000000LL;
    REQUIRE(store.Upsert(rec));

    std::string mergedJson;
    bool hit = true;
    CHECK_FALSE(balloonmain::ApplyRelayNotifyToStore(store, MakeNotify(), mergedJson, hit));
    CHECK_FALSE(hit);

    balloonhistory::HistoryRecord row;
    bool found = false;
    REQUIRE(store.QueryRecord(conversation_group, kGroupId, kCardMsgId, row, found));
    REQUIRE(found);
    CHECK(row.m_content == "{}");

    store.Close();
    ::DeleteFileW(path.c_str());
}
