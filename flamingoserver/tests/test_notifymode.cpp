/**
 *  会话消息提醒模式的单元测试（Suite NM），test_notifymode.cpp
 *
 *  消息提醒模式（t_conversation.f_notify_mode）取代了原来的免打扰开关 f_mute，
 *  并把原「特别关注」并入其中，成为五种互斥的模式，见 docs/消息提醒模式与消息盒子方案.md。
 *  本文件按方案的分期组织用例：
 *    - NMS：建表（第 1a 步）；
 *    - NMP：协议的解析与构造（第 1b 步）；
 *    - NMM：MessageManager 对模式的读写（第 1c 步）。
 *
 *  NMS 组会真的打开一个临时 .db 文件建表，文件名与其它套件分开，避免互相干扰。
 *
 *  典型用法：Tests.exe --test-case="NM*"
 *
 *  balloonwj@qq.com   2026-10-03
 */
#include <stdio.h>
#include <sstream>
#include <string>
#include <vector>

#include <memory>

#include "doctest.h"
#include "test_dbbackend.h"
#include "../chatserversrc/MessageManager.h"
#include "../chatserversrc/MessageProtocol.h"
#include "../chatserversrc/Protocol.h"
#include "../base/Singleton.h"
#include "../dbapi/DbFactory.h"
#include "../dbapi/IDatabase.h"
#include "../jsoncpp1.9.0/json.h"
#include "../sqliteapi/DatabaseSqlite.h"
#include "../sqliteapi/SqliteManager.h"

namespace {

//NMS 组专用的临时数据库文件
const char* const kNmsDbPath = "flamingo_sqlite_notifymode_schema_test.db";

//删除测试库文件，连同 WAL 模式产生的两个附属文件一起删掉
void RemoveNmsDbFiles()
{
    std::string basePath(kNmsDbPath);
    remove(basePath.c_str());
    remove((basePath + "-wal").c_str());
    remove((basePath + "-shm").c_str());
}

//执行一条只返回单个数值的查询，失败或无结果时返回 -1
int64_t QueryScalar(CDatabaseSqlite& db, const std::string& sql)
{
    QueryResult* pResult = db.query(sql.c_str());
    if (pResult == NULL)
        return -1;

    Field* pRow = pResult->fetch();
    int64_t value = -1;
    if (pRow != NULL)
    {
        value = static_cast<int64_t>(pRow[0].getUInt64());
    }

    delete pResult;
    return value;
}

//判断某张表里是否存在某一列
bool HasColumn(CDatabaseSqlite& db, const char* table, const char* column)
{
    std::string sql = std::string("PRAGMA table_info(") + table + ")";
    QueryResult* pResult = db.query(sql.c_str());
    if (pResult == NULL)
        return false;

    bool found = false;
    while (true)
    {
        Field* pRow = pResult->fetch();
        if (pRow == NULL)
            break;

        //PRAGMA table_info 每行依次是 cid、name、type、notnull、dflt_value、pk
        if (pRow[1].getString() == column)
        {
            found = true;
            break;
        }

        if (!pResult->nextRow())
            break;
    }

    delete pResult;
    return found;
}

//NMP 组用到的会话：一个单聊对端、一个群。取值只需能被原样回读。
const int64_t kNmpPeerId = 100007;
const int64_t kNmpGroupId = 268435463LL;

//越出模式取值范围的两个值：比最小的小 1、比最大的大 1
const int kNmpModeBelowRange = notify_mode_allow - 1;
const int kNmpModeAboveRange = notify_mode_mention_follow + 1;

//不存在的会话类型（服务号会话 3 也不支持设置提醒模式）
const int kNmpUnknownCtype = 9;

//把 JSON 文本解析成 Json::Value，失败返回 false
bool ParseJson(const std::string& text, Json::Value& root)
{
    Json::CharReaderBuilder builder;
    JSONCPP_STRING errs;
    Json::CharReader* reader = builder.newCharReader();
    bool ok = reader->parse(text.c_str(), text.c_str() + text.size(), &root, &errs);
    delete reader;
    return ok && errs.empty();
}

//拼一个带 count 条合法条目的 cmd_set_notify_modes 请求，条目都是群、模式都是收进消息盒子
std::string BuildBatchRequest(size_t count)
{
    std::ostringstream os;
    os << "{\"items\":[";
    for (size_t i = 0; i < count; ++i)
    {
        if (i > 0)
            os << ",";
        os << "{\"ctype\":" << conversation_group << ",\"cid\":\"" << (kNmpGroupId + (int64_t)i)
           << "\",\"mode\":" << notify_mode_msgbox << "}";
    }
    os << "]}";
    return os.str();
}

// ---- NMM 组：MessageManager 读写用的临时库与数据 ----

//NMM 组专用的临时数据库文件，以及 MySQL 模式下的测试库名（会被整个 DROP 重建）
const char* const kNmmDbPath = "flamingo_sqlite_notifymode_test.db";
const char* const kNmmMysqlDb = "flamingo_test_nmm";

//本组的主角用户、另一个用户（只用来确认不会串到别人的行上）
const int32_t kNmmUserA = 100001;
const int32_t kNmmUserB = 100002;

//A 的会话对端：两个联系人、三个群。群 id 取高位 id 空间里的值，只需互不相同。
const int64_t kNmmPeer1 = 200001;
const int64_t kNmmPeer2 = 200002;
const int64_t kNmmGroup1 = 268435501LL;
const int64_t kNmmGroup2 = 268435502LL;
const int64_t kNmmGroup3 = 268435503LL;

//群里发消息的那位成员（不是 A，这样消息才计入 A 的未读）
const int64_t kNmmSender = 200099;

//NMM8 / NMM9 预先灌满特别关注时用的联系人 id 起点：kNmmStarredBase + 0 ~ 49
const int64_t kNmmStarredBase = 300000;

//消息时间戳的基准（Unix 毫秒），固定取值保证结果可复现
const int64_t kNmmBaseTime = 1700000000000LL;

//写入会话状态时用的更新时间
const int64_t kNmmUpdateTime = 1700000500000LL;

//消息内容类型：本组只用文本
const int kNmmMsgTypeText = 1;

void ResetNmmDb()
{
    testdb::resetDb(kNmmDbPath, kNmmMysqlDb);
}

void CleanupNmmDb()
{
    testdb::cleanupDb(kNmmDbPath);
}

IDatabase* OpenNmmConn()
{
    IDatabase* pConn = DbFactory::createConnection();
    REQUIRE(pConn != NULL);
    return pConn;
}

//插一条群消息：kNmmSender 发到群 groupid
void InsertGroupMsg(int64_t msgid, int64_t groupid)
{
    std::unique_ptr<IDatabase> pConn(OpenNmmConn());
    std::ostringstream os;
    os << "INSERT INTO t_chatmsg "
          "(f_msgid,f_clientmsgid,f_ctype,f_from,f_to,f_msgtype,f_content,f_recalled,f_create_time) VALUES ("
       << msgid << ",'nmm" << msgid << "'," << conversation_group << "," << kNmmSender << "," << groupid
       << "," << kNmmMsgTypeText
       << ",'{\"content\":{\"text\":\"t\"},\"mentions\":[],\"mentionall\":false}',0,"
       << (kNmmBaseTime + msgid) << ")";
    REQUIRE(pConn->execute(os.str().c_str()));
}

//直接往 t_conversation 写一行特别关注的单聊会话，用来快速灌满上限
void InsertStarredRow(int32_t userid, int64_t peer)
{
    std::unique_ptr<IDatabase> pConn(OpenNmmConn());
    std::ostringstream os;
    os << "INSERT INTO t_conversation(f_userid, f_ctype, f_cid, f_notify_mode, f_update_time) VALUES("
       << userid << "," << conversation_single << "," << peer << "," << notify_mode_starred << ","
       << kNmmUpdateTime << ")";
    REQUIRE(pConn->execute(os.str().c_str()));
}

//只改提醒模式；返回 setConversation 的结果，失败原因写进 errorCode
bool SetMode(int32_t userid, int ctype, int64_t cid, int mode, int& errorCode)
{
    SetConversationRequest req;
    req.ctype          = ctype;
    req.cid            = cid;
    req.has_notifymode = true;
    req.notifymode     = mode;
    return Singleton<MessageManager>::Instance().setConversation(userid, req, kNmmUpdateTime, errorCode);
}

//只改提醒模式，并要求成功
void SetModeOk(int32_t userid, int ctype, int64_t cid, int mode)
{
    int errorCode = error_code_ok;
    REQUIRE(SetMode(userid, ctype, cid, mode, errorCode));
    CHECK(errorCode == error_code_ok);
}

//读回某会话的提醒模式（没有状态行时为 0）
int GetMode(int32_t userid, int ctype, int64_t cid)
{
    bool pinned = false;
    int mode = notify_mode_allow;
    bool deleted = false;
    REQUIRE(Singleton<MessageManager>::Instance().getConversationState(userid, ctype, cid,
                                                                       pinned, mode, deleted));
    return mode;
}

//读回某会话的已读位点
int64_t GetReadPos(int32_t userid, int ctype, int64_t cid)
{
    int64_t readmsgid = -1;
    REQUIRE(Singleton<MessageManager>::Instance().getReadMsgid(userid, ctype, cid, readmsgid));
    return readmsgid;
}

//取 A 在某个群的未读数（经会话摘要算出，与 cmd_sync_conversations 同一口径）
int GroupUnread(int64_t groupid)
{
    std::vector<int64_t> groupids;
    groupids.push_back(groupid);
    std::vector<ConversationBrief> briefs;
    REQUIRE(Singleton<MessageManager>::Instance().getGroupConversationBriefs(kNmmUserA, groupids, briefs));
    REQUIRE(briefs.size() == 1);
    return briefs[0].unread;
}

//拼一条批量条目
NotifyModeItem MakeItem(int ctype, int64_t cid, int mode)
{
    NotifyModeItem item;
    item.ctype = ctype;
    item.cid   = cid;
    item.mode  = mode;
    return item;
}

} // anonymous namespace

// =====================================================================
// NMS —— 建表
// =====================================================================

TEST_CASE("NMS1 空库冷启动：t_conversation 有 f_notify_mode，没有 f_mute，也不建 t_starred")
{
    RemoveNmsDbFiles();

    CSqliteManager manager;
    REQUIRE(manager.init(kNmsDbPath));

    CDatabaseSqlite db;
    REQUIRE(db.initialize(kNmsDbPath));

    CHECK(HasColumn(db, "t_conversation", "f_notify_mode"));
    CHECK_FALSE(HasColumn(db, "t_conversation", "f_mute"));

    //特别关注并入提醒模式后不再建 t_starred
    CHECK(QueryScalar(db, "SELECT COUNT(*) FROM sqlite_master WHERE type = 'table' AND name = 't_starred'") == 0);

    //新列对新插入的行取默认值 0（允许消息提醒）
    REQUIRE(db.execute("INSERT INTO t_conversation(f_userid, f_ctype, f_cid) VALUES(1, 1, 2)"));
    CHECK(QueryScalar(db, "SELECT f_notify_mode FROM t_conversation WHERE f_userid = 1") == 0);
}

// =====================================================================
// NMP —— 协议的解析与构造
// =====================================================================

TEST_CASE("NMP1 cmd_set_conversation 的 notifymode：整数才认，缺省或类型不对视为本次不改")
{
    SetConversationRequest req;
    REQUIRE(MessageProtocol::parseSetConversationRequest(
        "{\"ctype\":2,\"cid\":\"268435463\",\"notifymode\":2}", req));
    CHECK(req.has_notifymode);
    CHECK(req.notifymode == notify_mode_msgbox);

    //不带这个字段
    SetConversationRequest absent;
    REQUIRE(MessageProtocol::parseSetConversationRequest(
        "{\"ctype\":2,\"cid\":\"268435463\",\"pinned\":true}", absent));
    CHECK_FALSE(absent.has_notifymode);

    //字符串、布尔都不算
    SetConversationRequest asString;
    REQUIRE(MessageProtocol::parseSetConversationRequest(
        "{\"ctype\":2,\"cid\":\"268435463\",\"notifymode\":\"2\"}", asString));
    CHECK_FALSE(asString.has_notifymode);

    SetConversationRequest asBool;
    REQUIRE(MessageProtocol::parseSetConversationRequest(
        "{\"ctype\":2,\"cid\":\"268435463\",\"notifymode\":true}", asBool));
    CHECK_FALSE(asBool.has_notifymode);

    //越界的值照样解析出来，由 isValidNotifyMode 判断合不合法
    SetConversationRequest outOfRange;
    REQUIRE(MessageProtocol::parseSetConversationRequest(
        "{\"ctype\":2,\"cid\":\"268435463\",\"notifymode\":6}", outOfRange));
    CHECK(outOfRange.has_notifymode);
    CHECK(outOfRange.notifymode == kNmpModeAboveRange);
}

TEST_CASE("NMP2 老的布尔字段 mute 不再识别")
{
    SetConversationRequest req;
    REQUIRE(MessageProtocol::parseSetConversationRequest(
        "{\"ctype\":1,\"cid\":\"100007\",\"mute\":true}", req));
    CHECK_FALSE(req.has_notifymode);
    CHECK_FALSE(req.has_pinned);
    CHECK_FALSE(req.has_deleted);
}

TEST_CASE("NMP3 isValidNotifyMode：群聊 0~5 合法，单聊不能屏蔽，越界与未知会话类型不合法")
{
    //群聊五种都合法
    CHECK(MessageProtocol::isValidNotifyMode(conversation_group, notify_mode_allow));
    CHECK(MessageProtocol::isValidNotifyMode(conversation_group, notify_mode_silent));
    CHECK(MessageProtocol::isValidNotifyMode(conversation_group, notify_mode_msgbox));
    CHECK(MessageProtocol::isValidNotifyMode(conversation_group, notify_mode_block));
    CHECK(MessageProtocol::isValidNotifyMode(conversation_group, notify_mode_starred));

    //单聊除了屏蔽都合法
    CHECK(MessageProtocol::isValidNotifyMode(conversation_single, notify_mode_allow));
    CHECK(MessageProtocol::isValidNotifyMode(conversation_single, notify_mode_silent));
    CHECK(MessageProtocol::isValidNotifyMode(conversation_single, notify_mode_msgbox));
    CHECK_FALSE(MessageProtocol::isValidNotifyMode(conversation_single, notify_mode_block));
    CHECK(MessageProtocol::isValidNotifyMode(conversation_single, notify_mode_starred));

    //越界
    CHECK_FALSE(MessageProtocol::isValidNotifyMode(conversation_group, kNmpModeBelowRange));
    CHECK_FALSE(MessageProtocol::isValidNotifyMode(conversation_group, kNmpModeAboveRange));
    CHECK_FALSE(MessageProtocol::isValidNotifyMode(conversation_single, kNmpModeAboveRange));

    //未知会话类型、服务号会话
    CHECK_FALSE(MessageProtocol::isValidNotifyMode(kNmpUnknownCtype, notify_mode_allow));
    CHECK_FALSE(MessageProtocol::isValidNotifyMode(conversation_service, notify_mode_silent));
}

TEST_CASE("NMP4 cmd_sync_conversations 应答：会话带整数 notifymode，顶层带 notifymodes 数组")
{
    ConversationBrief brief;
    brief.ctype      = conversation_group;
    brief.cid        = kNmpGroupId;
    brief.notifymode = notify_mode_block;
    brief.lastmsgid  = 1;
    std::vector<ConversationBrief> briefs;
    briefs.push_back(brief);

    //notifymodes 里包括一个不在 conversations 里的联系人（还没聊过、但设成了特别关注）
    std::vector<NotifyModeItem> modes;
    NotifyModeItem groupMode;
    groupMode.ctype = conversation_group;
    groupMode.cid   = kNmpGroupId;
    groupMode.mode  = notify_mode_block;
    modes.push_back(groupMode);
    NotifyModeItem peerMode;
    peerMode.ctype = conversation_single;
    peerMode.cid   = kNmpPeerId;
    peerMode.mode  = notify_mode_starred;
    modes.push_back(peerMode);

    Json::Value root;
    REQUIRE(ParseJson(MessageProtocol::buildSyncConversationsResponse(0, "ok", 0, briefs, modes), root));
    REQUIRE(root["conversations"].size() == 1);
    CHECK(root["conversations"][0]["notifymode"].asInt() == notify_mode_block);
    CHECK_FALSE(root["conversations"][0].isMember("mute"));

    REQUIRE(root["notifymodes"].isArray());
    REQUIRE(root["notifymodes"].size() == 2);
    CHECK(root["notifymodes"][0]["ctype"].asInt() == conversation_group);
    CHECK(root["notifymodes"][0]["cid"].asString() == std::to_string(kNmpGroupId));
    CHECK(root["notifymodes"][0]["mode"].asInt() == notify_mode_block);
    CHECK(root["notifymodes"][1]["ctype"].asInt() == conversation_single);
    CHECK(root["notifymodes"][1]["cid"].asString() == std::to_string(kNmpPeerId));
    CHECK(root["notifymodes"][1]["mode"].asInt() == notify_mode_starred);

    //没有非默认模式时 notifymodes 是空数组，而不是缺失
    Json::Value emptyRoot;
    REQUIRE(ParseJson(MessageProtocol::buildSyncConversationsResponse(0, "ok", 0, briefs), emptyRoot));
    CHECK(emptyRoot["notifymodes"].isArray());
    CHECK(emptyRoot["notifymodes"].size() == 0);
}

TEST_CASE("NMP5 cmd_conversation_changed_notify：带 notifymode，不再带 mute")
{
    Json::Value root;
    REQUIRE(ParseJson(MessageProtocol::buildConversationChangedNotify(
        conversation_single, kNmpPeerId, false, notify_mode_starred, false, false), root));
    CHECK(root["ctype"].asInt() == conversation_single);
    CHECK(root["cid"].asString() == std::to_string(kNmpPeerId));
    CHECK(root["notifymode"].asInt() == notify_mode_starred);
    CHECK_FALSE(root.isMember("mute"));
}

TEST_CASE("NMP6 cmd_set_notify_modes 请求解析：整单失败的几种情形与逐条非法")
{
    std::vector<SetNotifyModeEntry> entries;

    //正常三条：两个群、一个单聊
    REQUIRE(MessageProtocol::parseSetNotifyModesRequest(
        "{\"items\":[{\"ctype\":2,\"cid\":\"268435463\",\"mode\":2},"
        "{\"ctype\":2,\"cid\":\"268435464\",\"mode\":3},"
        "{\"ctype\":1,\"cid\":\"100007\",\"mode\":4}]}", entries));
    REQUIRE(entries.size() == 3);
    CHECK(entries[0].wellformed);
    CHECK(entries[0].item.ctype == conversation_group);
    CHECK(entries[0].item.cid == kNmpGroupId);
    CHECK(entries[0].item.mode == notify_mode_msgbox);
    CHECK(entries[1].item.mode == notify_mode_block);
    CHECK(entries[2].item.ctype == conversation_single);
    CHECK(entries[2].item.cid == kNmpPeerId);
    CHECK(entries[2].item.mode == notify_mode_starred);

    //整单失败：items 缺失、不是数组、为空、JSON 非法
    CHECK_FALSE(MessageProtocol::parseSetNotifyModesRequest("{}", entries));
    CHECK_FALSE(MessageProtocol::parseSetNotifyModesRequest("{\"items\":{}}", entries));
    CHECK_FALSE(MessageProtocol::parseSetNotifyModesRequest("{\"items\":[]}", entries));
    CHECK_FALSE(MessageProtocol::parseSetNotifyModesRequest("not json", entries));

    //条数上限：正好 NOTIFY_MODE_BATCH_MAX 条通过，多一条整单失败
    REQUIRE(MessageProtocol::parseSetNotifyModesRequest(BuildBatchRequest(NOTIFY_MODE_BATCH_MAX), entries));
    CHECK(entries.size() == NOTIFY_MODE_BATCH_MAX);
    CHECK_FALSE(MessageProtocol::parseSetNotifyModesRequest(BuildBatchRequest(NOTIFY_MODE_BATCH_MAX + 1),
                                                            entries));

    //某几条有问题：只这几条判为非法，其余照常，能解析出的 ctype / cid 原样保留供回带
    REQUIRE(MessageProtocol::parseSetNotifyModesRequest(
        "{\"items\":[{\"ctype\":2,\"cid\":\"268435463\"},"          //缺 mode
        "{\"ctype\":2,\"cid\":\"0\",\"mode\":1},"                    //cid 非正
        "{\"ctype\":\"2\",\"cid\":\"268435463\",\"mode\":1},"        //ctype 不是整数
        "\"oops\","                                                  //不是对象
        "{\"ctype\":1,\"cid\":\"100007\",\"mode\":1}]}", entries));
    REQUIRE(entries.size() == 5);
    CHECK_FALSE(entries[0].wellformed);
    CHECK(entries[0].item.ctype == conversation_group);
    CHECK(entries[0].item.cid == kNmpGroupId);
    CHECK_FALSE(entries[1].wellformed);
    CHECK_FALSE(entries[2].wellformed);
    CHECK(entries[2].item.cid == kNmpGroupId);
    CHECK_FALSE(entries[3].wellformed);
    CHECK(entries[4].wellformed);
    CHECK(entries[4].item.mode == notify_mode_silent);
}

TEST_CASE("NMP7 cmd_set_notify_modes 应答：results 逐条带 ctype、字符串 cid 与结果码")
{
    std::vector<NotifyModeResult> results;
    NotifyModeResult ok;
    ok.ctype = conversation_group;
    ok.cid   = kNmpGroupId;
    ok.code  = error_code_ok;
    results.push_back(ok);
    NotifyModeResult limit;
    limit.ctype = conversation_single;
    limit.cid   = kNmpPeerId;
    limit.code  = error_code_starred_limit;
    results.push_back(limit);

    Json::Value root;
    REQUIRE(ParseJson(MessageProtocol::buildSetNotifyModesResponse(error_code_ok, "ok", results), root));
    CHECK(root["code"].asInt() == error_code_ok);
    REQUIRE(root["results"].size() == 2);
    CHECK(root["results"][0]["ctype"].asInt() == conversation_group);
    CHECK(root["results"][0]["cid"].asString() == std::to_string(kNmpGroupId));
    CHECK(root["results"][0]["code"].asInt() == error_code_ok);
    CHECK(root["results"][1]["cid"].asString() == std::to_string(kNmpPeerId));
    CHECK(root["results"][1]["code"].asInt() == error_code_starred_limit);

    //整单失败时 results 为空数组
    Json::Value failRoot;
    REQUIRE(ParseJson(MessageProtocol::buildSetNotifyModesResponse(
        error_code_bad_request, "bad request", std::vector<NotifyModeResult>()), failRoot));
    CHECK(failRoot["code"].asInt() == error_code_bad_request);
    CHECK(failRoot["results"].isArray());
    CHECK(failRoot["results"].size() == 0);
}

TEST_CASE("NMP8 cmd_notify_modes_changed_notify：items 逐条带 ctype、字符串 cid 与 mode")
{
    std::vector<NotifyModeItem> items;
    NotifyModeItem item;
    item.ctype = conversation_group;
    item.cid   = kNmpGroupId;
    item.mode  = notify_mode_msgbox;
    items.push_back(item);

    Json::Value root;
    REQUIRE(ParseJson(MessageProtocol::buildNotifyModesChangedNotify(items), root));
    REQUIRE(root["items"].size() == 1);
    CHECK(root["items"][0]["ctype"].asInt() == conversation_group);
    CHECK(root["items"][0]["cid"].asString() == std::to_string(kNmpGroupId));
    CHECK(root["items"][0]["mode"].asInt() == notify_mode_msgbox);
}

// =====================================================================
// NMM —— MessageManager 对提醒模式的读写
//
// 会话摘要带出 notifymode 由 CVE2（单聊）、CVE6（群聊）覆盖，本组不重复。
// =====================================================================

TEST_CASE("NMM1 提醒模式逐个写入再读回")
{
    ResetNmmDb();

    //群聊五种
    const int groupModes[] = { notify_mode_silent, notify_mode_msgbox, notify_mode_block,
                               notify_mode_starred, notify_mode_allow };
    for (size_t i = 0; i < sizeof(groupModes) / sizeof(groupModes[0]); ++i)
    {
        SetModeOk(kNmmUserA, conversation_group, kNmmGroup1, groupModes[i]);
        CHECK(GetMode(kNmmUserA, conversation_group, kNmmGroup1) == groupModes[i]);
    }

    //单聊四种（屏蔽由协议层拒绝，不会走到这里）
    const int singleModes[] = { notify_mode_silent, notify_mode_msgbox, notify_mode_starred,
                                notify_mode_allow };
    for (size_t i = 0; i < sizeof(singleModes) / sizeof(singleModes[0]); ++i)
    {
        SetModeOk(kNmmUserA, conversation_single, kNmmPeer1, singleModes[i]);
        CHECK(GetMode(kNmmUserA, conversation_single, kNmmPeer1) == singleModes[i]);
    }

    //没有状态行的会话按允许提醒
    CHECK(GetMode(kNmmUserA, conversation_single, kNmmPeer2) == notify_mode_allow);

    CleanupNmmDb();
}

TEST_CASE("NMM2 只改模式与只改置顶互不影响，也不影响删除标记")
{
    ResetNmmDb();
    MessageManager& mgr = Singleton<MessageManager>::Instance();

    //先只改模式
    SetModeOk(kNmmUserA, conversation_group, kNmmGroup1, notify_mode_msgbox);

    //再只改置顶：模式不变
    SetConversationRequest pinReq;
    pinReq.ctype      = conversation_group;
    pinReq.cid        = kNmmGroup1;
    pinReq.has_pinned = true;
    pinReq.pinned     = true;
    REQUIRE(mgr.setConversation(kNmmUserA, pinReq, kNmmUpdateTime));

    bool pinned = false;
    int mode = notify_mode_allow;
    bool deleted = true;
    REQUIRE(mgr.getConversationState(kNmmUserA, conversation_group, kNmmGroup1, pinned, mode, deleted));
    CHECK(pinned);
    CHECK(mode == notify_mode_msgbox);
    CHECK_FALSE(deleted);

    //再只改模式：置顶不变
    SetModeOk(kNmmUserA, conversation_group, kNmmGroup1, notify_mode_silent);
    REQUIRE(mgr.getConversationState(kNmmUserA, conversation_group, kNmmGroup1, pinned, mode, deleted));
    CHECK(pinned);
    CHECK(mode == notify_mode_silent);
    CHECK_FALSE(deleted);

    CleanupNmmDb();
}

TEST_CASE("NMM3 取全部非默认模式：包括没有消息的会话，不含值为 0 的行与别人的行")
{
    ResetNmmDb();
    MessageManager& mgr = Singleton<MessageManager>::Instance();

    //A：一个群屏蔽、一个联系人特别关注（都没有任何消息）；另一个群设过又改回允许
    SetModeOk(kNmmUserA, conversation_group, kNmmGroup2, notify_mode_block);
    SetModeOk(kNmmUserA, conversation_single, kNmmPeer1, notify_mode_starred);
    SetModeOk(kNmmUserA, conversation_group, kNmmGroup3, notify_mode_silent);
    SetModeOk(kNmmUserA, conversation_group, kNmmGroup3, notify_mode_allow);
    //B 的设置不应出现在 A 的结果里
    SetModeOk(kNmmUserB, conversation_group, kNmmGroup1, notify_mode_silent);

    std::vector<NotifyModeItem> modes;
    REQUIRE(mgr.getNotifyModes(kNmmUserA, modes));
    REQUIRE(modes.size() == 2);
    //按 ctype、cid 升序：单聊在前
    CHECK(modes[0].ctype == conversation_single);
    CHECK(modes[0].cid == kNmmPeer1);
    CHECK(modes[0].mode == notify_mode_starred);
    CHECK(modes[1].ctype == conversation_group);
    CHECK(modes[1].cid == kNmmGroup2);
    CHECK(modes[1].mode == notify_mode_block);

    CleanupNmmDb();
}

TEST_CASE("NMM5 群从屏蔽改成允许：已读位点推进到最新一条，之后的新消息照常计未读")
{
    ResetNmmDb();

    InsertGroupMsg(1, kNmmGroup1);
    InsertGroupMsg(2, kNmmGroup1);
    InsertGroupMsg(3, kNmmGroup1);

    SetModeOk(kNmmUserA, conversation_group, kNmmGroup1, notify_mode_block);
    //屏蔽本身不动已读位点：服务端照常算未读，由客户端按模式忽略
    CHECK(GetReadPos(kNmmUserA, conversation_group, kNmmGroup1) == 0);
    CHECK(GroupUnread(kNmmGroup1) == 3);

    SetModeOk(kNmmUserA, conversation_group, kNmmGroup1, notify_mode_allow);
    CHECK(GetReadPos(kNmmUserA, conversation_group, kNmmGroup1) == 3);
    CHECK(GroupUnread(kNmmGroup1) == 0);

    InsertGroupMsg(4, kNmmGroup1);
    CHECK(GroupUnread(kNmmGroup1) == 1);

    CleanupNmmDb();
}

TEST_CASE("NMM6 不涉及改出屏蔽的切换都不动已读位点")
{
    ResetNmmDb();

    InsertGroupMsg(1, kNmmGroup1);
    InsertGroupMsg(2, kNmmGroup1);

    //0 → 1 → 0
    SetModeOk(kNmmUserA, conversation_group, kNmmGroup1, notify_mode_silent);
    CHECK(GetReadPos(kNmmUserA, conversation_group, kNmmGroup1) == 0);
    SetModeOk(kNmmUserA, conversation_group, kNmmGroup1, notify_mode_allow);
    CHECK(GetReadPos(kNmmUserA, conversation_group, kNmmGroup1) == 0);

    //3 → 3：仍在屏蔽中，不算改出
    SetModeOk(kNmmUserA, conversation_group, kNmmGroup1, notify_mode_block);
    SetModeOk(kNmmUserA, conversation_group, kNmmGroup1, notify_mode_block);
    CHECK(GetReadPos(kNmmUserA, conversation_group, kNmmGroup1) == 0);
    CHECK(GroupUnread(kNmmGroup1) == 2);

    CleanupNmmDb();
}

TEST_CASE("NMM7 特别关注的个数单聊与群聊合计")
{
    ResetNmmDb();
    MessageManager& mgr = Singleton<MessageManager>::Instance();

    SetModeOk(kNmmUserA, conversation_single, kNmmPeer1, notify_mode_starred);
    SetModeOk(kNmmUserA, conversation_single, kNmmPeer2, notify_mode_starred);
    SetModeOk(kNmmUserA, conversation_group, kNmmGroup1, notify_mode_starred);
    SetModeOk(kNmmUserA, conversation_group, kNmmGroup2, notify_mode_silent);
    SetModeOk(kNmmUserB, conversation_group, kNmmGroup1, notify_mode_starred);

    int count = -1;
    REQUIRE(mgr.countNotifyMode(kNmmUserA, notify_mode_starred, count));
    CHECK(count == 3);
    REQUIRE(mgr.countNotifyMode(kNmmUserA, notify_mode_silent, count));
    CHECK(count == 1);

    CleanupNmmDb();
}

TEST_CASE("NMM8 特别关注已满：再设新的返回 210 且不改动，重复设已有的照样成功")
{
    ResetNmmDb();

    for (int i = 0; i < NOTIFY_MODE_STARRED_MAX; ++i)
    {
        InsertStarredRow(kNmmUserA, kNmmStarredBase + i);
    }

    //第 51 个：群也算在内，被拒绝，模式不变
    int errorCode = error_code_ok;
    CHECK_FALSE(SetMode(kNmmUserA, conversation_group, kNmmGroup1, notify_mode_starred, errorCode));
    CHECK(errorCode == error_code_starred_limit);
    CHECK(GetMode(kNmmUserA, conversation_group, kNmmGroup1) == notify_mode_allow);

    //已经是特别关注的再设一次：不占新名额
    SetModeOk(kNmmUserA, conversation_single, kNmmStarredBase, notify_mode_starred);

    //空出一个名额后就能设了
    SetModeOk(kNmmUserA, conversation_single, kNmmStarredBase, notify_mode_allow);
    SetModeOk(kNmmUserA, conversation_group, kNmmGroup1, notify_mode_starred);

    //别人的上限互不影响
    SetModeOk(kNmmUserB, conversation_group, kNmmGroup1, notify_mode_starred);

    CleanupNmmDb();
}

TEST_CASE("NMM9 批量写入：逐条结果、跳过预判非法的条目、上限按顺序累计、改出屏蔽推进已读")
{
    ResetNmmDb();
    MessageManager& mgr = Singleton<MessageManager>::Instance();

    //特别关注已满；群 2 处于屏蔽且有两条消息
    for (int i = 0; i < NOTIFY_MODE_STARRED_MAX; ++i)
    {
        InsertStarredRow(kNmmUserA, kNmmStarredBase + i);
    }
    InsertGroupMsg(1, kNmmGroup2);
    InsertGroupMsg(2, kNmmGroup2);
    SetModeOk(kNmmUserA, conversation_group, kNmmGroup2, notify_mode_block);

    std::vector<NotifyModeItem> items;
    items.push_back(MakeItem(conversation_group, kNmmGroup1, notify_mode_msgbox));        //0 成功
    items.push_back(MakeItem(conversation_group, kNmmGroup3, notify_mode_silent));        //1 调用方已判非法
    items.push_back(MakeItem(conversation_group, kNmmGroup2, notify_mode_allow));         //2 改出屏蔽
    items.push_back(MakeItem(conversation_single, kNmmPeer1, notify_mode_starred));       //3 已满
    items.push_back(MakeItem(conversation_single, kNmmStarredBase, notify_mode_allow));   //4 空出一个名额
    items.push_back(MakeItem(conversation_single, kNmmPeer2, notify_mode_starred));       //5 用上空出的名额

    std::vector<NotifyModeResult> results(items.size());
    for (size_t i = 0; i < items.size(); ++i)
    {
        results[i].ctype = items[i].ctype;
        results[i].cid   = items[i].cid;
    }
    results[1].code = error_code_not_group_member;

    std::vector<NotifyModeItem> changed;
    REQUIRE(mgr.setNotifyModes(kNmmUserA, items, kNmmUpdateTime, results, changed));

    CHECK(results[0].code == error_code_ok);
    CHECK(results[1].code == error_code_not_group_member);
    CHECK(results[2].code == error_code_ok);
    CHECK(results[3].code == error_code_starred_limit);
    CHECK(results[4].code == error_code_ok);
    CHECK(results[5].code == error_code_ok);

    //只有写入成功的四条进 changed，顺序与请求一致
    REQUIRE(changed.size() == 4);
    CHECK(changed[0].cid == kNmmGroup1);
    CHECK(changed[1].cid == kNmmGroup2);
    CHECK(changed[2].cid == kNmmStarredBase);
    CHECK(changed[3].cid == kNmmPeer2);

    CHECK(GetMode(kNmmUserA, conversation_group, kNmmGroup1) == notify_mode_msgbox);
    CHECK(GetMode(kNmmUserA, conversation_group, kNmmGroup3) == notify_mode_allow);
    CHECK(GetMode(kNmmUserA, conversation_group, kNmmGroup2) == notify_mode_allow);
    CHECK(GetReadPos(kNmmUserA, conversation_group, kNmmGroup2) == 2);
    CHECK(GetMode(kNmmUserA, conversation_single, kNmmPeer1) == notify_mode_allow);
    CHECK(GetMode(kNmmUserA, conversation_single, kNmmStarredBase) == notify_mode_allow);
    CHECK(GetMode(kNmmUserA, conversation_single, kNmmPeer2) == notify_mode_starred);

    int starred = -1;
    REQUIRE(mgr.countNotifyMode(kNmmUserA, notify_mode_starred, starred));
    CHECK(starred == NOTIFY_MODE_STARRED_MAX);

    CleanupNmmDb();
}
