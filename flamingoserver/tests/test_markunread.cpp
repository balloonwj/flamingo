/**
 *  会话「标为未读」的单元测试（Suite MU-S），test_markunread.cpp
 *
 *  「标为未读」是本人对整个会话手动打的标记，存在 t_conversation.f_unread_mark，随会话列表
 *  （cmd_sync_conversations）下发，经 cmd_set_conversation 的可选字段 unreadmark 设置与清除，
 *  改完经 cmd_conversation_changed_notify 推给本人全部在线设备；本人上报已读（cmd_mark_read）
 *  时服务端顺带清除。方案见 docs/会话标为未读方案.md。
 *
 *    MU-S1  协议与存储：1513 带 unreadmark 才改这一列，1501 与 1514 都带出当前值；
 *           不带这个字段时（只改置顶）原值不变
 *    MU-S2  已读上报清除标记，已读位点没有前进时也清除
 *    MU-S3  结构：新建的 SQLite 库有 f_unread_mark 与 t_group_follow。MySQL 侧由 flamingo_tests_mysql
 *           构建跑 MU-S1 / MU-S2 时一并覆盖
 *
 *  典型用法：Tests.exe --test-case="MU-S*"
 *
 *  balloonwj@qq.com   2026-10-06
 */
#include <stdio.h>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

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

//MU-S1 / MU-S2 的临时库：SQLite 文件名与 MySQL 库名（会被整个 DROP 重建）
const char* const kMuDbPath    = "flamingo_sqlite_markunread_test.db";
const char* const kMuMysqlDb   = "flamingo_test_markunread";

//MU-S3 结构用例专用的临时库文件
const char* const kMuSchemaDbPath = "flamingo_sqlite_markunread_schema_test.db";

//本组的主角用户与另一个用户（只用来确认不会串到别人的行上）
const int32_t kMuUser  = 100001;
const int32_t kMuOther = 100002;

//主角的会话对端：一个联系人、一个群
const int64_t kMuPeer  = 200001;
const int64_t kMuGroup = 268435601LL;

//群里发消息的成员（不是主角）
const int64_t kMuSender = 200099;

//两条消息的 msgid：单聊一条、群聊一条
const int64_t kMuSingleMsgId = 11;
const int64_t kMuGroupMsgId  = 12;

//消息时间戳的基准与写入会话状态时的更新时间（Unix 毫秒），固定取值保证结果可复现
const int64_t kMuBaseTime   = 1700000000000LL;
const int64_t kMuUpdateTime = 1700000500000LL;

//消息内容类型：本组只用文本
const int kMuMsgTypeText = 1;

//把 JSON 文本解析成 Json::Value，失败返回 false
bool ParseJson(const std::string& text, Json::Value& root)
{
    Json::CharReaderBuilder builder;
    std::string errs;
    std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
    return reader->parse(text.c_str(), text.c_str() + text.size(), &root, &errs) && root.isObject();
}

//插一条消息：from 发给 to
void InsertMsg(int64_t msgid, int ctype, int64_t from, int64_t to)
{
    std::unique_ptr<IDatabase> pConn(DbFactory::createConnection());
    REQUIRE(pConn);
    std::ostringstream os;
    os << "INSERT INTO t_chatmsg "
          "(f_msgid,f_clientmsgid,f_ctype,f_from,f_to,f_msgtype,f_content,f_recalled,f_create_time) VALUES ("
       << msgid << ",'mu" << msgid << "'," << ctype << "," << from << "," << to << "," << kMuMsgTypeText
       << ",'{\"content\":{\"text\":\"t\"},\"mentions\":[],\"mentionall\":false}',0,"
       << (kMuBaseTime + msgid) << ")";
    REQUIRE(pConn->execute(os.str().c_str()));
}

//重建测试库，并给主角准备一个单聊、一个群聊各一条别人发来的消息
void ResetMuDb()
{
    testdb::resetDb(kMuDbPath, kMuMysqlDb);
    InsertMsg(kMuSingleMsgId, conversation_single, kMuPeer, kMuUser);
    InsertMsg(kMuGroupMsgId, conversation_group, kMuSender, kMuGroup);
}

//只改「标为未读」，并要求成功
void SetUnreadMark(int32_t userid, int ctype, int64_t cid, bool mark)
{
    SetConversationRequest req;
    req.ctype          = ctype;
    req.cid            = cid;
    req.has_unreadmark = true;
    req.unreadmark     = mark;
    REQUIRE(Singleton<MessageManager>::Instance().setConversation(userid, req, kMuUpdateTime));
}

//读回某会话的「标为未读」（没有状态行时为 false）
bool GetUnreadMark(int32_t userid, int ctype, int64_t cid)
{
    bool pinned = false;
    int mode = notify_mode_allow;
    bool deleted = false;
    bool unreadmark = false;
    REQUIRE(Singleton<MessageManager>::Instance().getConversationState(userid, ctype, cid,
                                                                       pinned, mode, deleted, unreadmark));
    return unreadmark;
}

//取主角单聊会话摘要里与 kMuPeer 的那一条
ConversationBrief SingleBrief()
{
    std::vector<ConversationBrief> briefs;
    REQUIRE(Singleton<MessageManager>::Instance().getConversationBriefs(kMuUser, briefs));
    for (size_t i = 0; i < briefs.size(); ++i)
    {
        if (briefs[i].cid == kMuPeer)
            return briefs[i];
    }
    FAIL("single brief not found");
    return ConversationBrief();
}

//取主角在 kMuGroup 的群会话摘要
ConversationBrief GroupBrief()
{
    std::vector<int64_t> groupids(1, kMuGroup);
    std::vector<ConversationBrief> briefs;
    REQUIRE(Singleton<MessageManager>::Instance().getGroupConversationBriefs(kMuUser, groupids, briefs));
    REQUIRE(briefs.size() == 1);
    return briefs[0];
}

//删除 MU-S3 的测试库文件，连同 WAL 模式的两个附属文件
void RemoveSchemaDbFiles()
{
    std::string basePath(kMuSchemaDbPath);
    remove(basePath.c_str());
    remove((basePath + "-wal").c_str());
    remove((basePath + "-shm").c_str());
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

} // anonymous namespace

TEST_CASE("MU-S1a 协议：1513 的 unreadmark 只认布尔值，1514 与 1501 带出当前值")
{
    //带 true / false 时 has_unreadmark 为真
    SetConversationRequest req;
    REQUIRE(MessageProtocol::parseSetConversationRequest("{\"ctype\":1,\"cid\":\"200001\",\"unreadmark\":true}", req));
    CHECK(req.has_unreadmark);
    CHECK(req.unreadmark);
    CHECK_FALSE(req.has_pinned);

    SetConversationRequest req2;
    REQUIRE(MessageProtocol::parseSetConversationRequest("{\"ctype\":2,\"cid\":\"268435601\",\"unreadmark\":false}", req2));
    CHECK(req2.has_unreadmark);
    CHECK_FALSE(req2.unreadmark);

    //不带或类型不对时视为本次不改
    SetConversationRequest req3;
    REQUIRE(MessageProtocol::parseSetConversationRequest("{\"ctype\":1,\"cid\":\"200001\",\"pinned\":true}", req3));
    CHECK_FALSE(req3.has_unreadmark);
    SetConversationRequest req4;
    REQUIRE(MessageProtocol::parseSetConversationRequest("{\"ctype\":1,\"cid\":\"200001\",\"unreadmark\":1}", req4));
    CHECK_FALSE(req4.has_unreadmark);

    //1514 带 unreadmark
    Json::Value notify;
    REQUIRE(ParseJson(MessageProtocol::buildConversationChangedNotify(conversation_single, kMuPeer, false,
                                                                      notify_mode_allow, false, true), notify));
    CHECK(notify["unreadmark"].asBool());

    //1501 每个会话带 unreadmark（0 / 1）
    std::vector<ConversationBrief> items(2);
    items[0].cid        = kMuPeer;
    items[0].unreadmark = true;
    items[1].ctype      = conversation_group;
    items[1].cid        = kMuGroup;
    Json::Value sync;
    REQUIRE(ParseJson(MessageProtocol::buildSyncConversationsResponse(0, "ok", kMuUpdateTime, items), sync));
    REQUIRE(sync["conversations"].size() == 2);
    CHECK(sync["conversations"][0]["unreadmark"].asInt() == 1);
    CHECK(sync["conversations"][1]["unreadmark"].asInt() == 0);
}

TEST_CASE("MU-S1b 存储：带 unreadmark 才改这一列，单聊群聊的摘要与状态读回一致，只改置顶时不变")
{
    ResetMuDb();
    MessageManager& mgr = Singleton<MessageManager>::Instance();

    //初始没有标记
    CHECK_FALSE(SingleBrief().unreadmark);
    CHECK_FALSE(GroupBrief().unreadmark);

    //标为未读：单聊、群聊各一次
    SetUnreadMark(kMuUser, conversation_single, kMuPeer, true);
    SetUnreadMark(kMuUser, conversation_group, kMuGroup, true);
    CHECK(GetUnreadMark(kMuUser, conversation_single, kMuPeer));
    CHECK(GetUnreadMark(kMuUser, conversation_group, kMuGroup));
    CHECK(SingleBrief().unreadmark);
    CHECK(GroupBrief().unreadmark);

    //只改置顶：标记不变
    SetConversationRequest pinReq;
    pinReq.ctype      = conversation_single;
    pinReq.cid        = kMuPeer;
    pinReq.has_pinned = true;
    pinReq.pinned     = true;
    REQUIRE(mgr.setConversation(kMuUser, pinReq, kMuUpdateTime));
    CHECK(GetUnreadMark(kMuUser, conversation_single, kMuPeer));
    CHECK(SingleBrief().pinned);

    //只改提醒模式：标记也不变
    SetConversationRequest modeReq;
    modeReq.ctype          = conversation_group;
    modeReq.cid            = kMuGroup;
    modeReq.has_notifymode = true;
    modeReq.notifymode     = notify_mode_silent;
    REQUIRE(mgr.setConversation(kMuUser, modeReq, kMuUpdateTime));
    CHECK(GetUnreadMark(kMuUser, conversation_group, kMuGroup));

    //清除：置顶保持
    SetUnreadMark(kMuUser, conversation_single, kMuPeer, false);
    CHECK_FALSE(GetUnreadMark(kMuUser, conversation_single, kMuPeer));
    CHECK_FALSE(SingleBrief().unreadmark);
    CHECK(SingleBrief().pinned);

    //别人的行不受影响
    CHECK_FALSE(GetUnreadMark(kMuOther, conversation_group, kMuGroup));

    testdb::cleanupDb(kMuDbPath);
}

TEST_CASE("MU-S2 已读上报清除标记，已读位点没有前进时也清除")
{
    ResetMuDb();
    MessageManager& mgr = Singleton<MessageManager>::Instance();

    //先读到最新一条，再标为未读：未读数仍为 0，只有标记
    REQUIRE(mgr.markRead(kMuUser, conversation_group, kMuGroup, kMuGroupMsgId, kMuUpdateTime));
    SetUnreadMark(kMuUser, conversation_group, kMuGroup, true);
    CHECK(GroupBrief().unread == 0);
    CHECK(GroupBrief().unreadmark);

    //位点不前进的已读上报：标记照样清除
    REQUIRE(mgr.markRead(kMuUser, conversation_group, kMuGroup, kMuGroupMsgId, kMuUpdateTime));
    CHECK_FALSE(GetUnreadMark(kMuUser, conversation_group, kMuGroup));

    //单聊：没有状态行时先标记（插入新行），再上报已读清除
    SetUnreadMark(kMuUser, conversation_single, kMuPeer, true);
    CHECK(SingleBrief().unreadmark);
    REQUIRE(mgr.markRead(kMuUser, conversation_single, kMuPeer, kMuSingleMsgId, kMuUpdateTime));
    CHECK_FALSE(SingleBrief().unreadmark);
    CHECK(SingleBrief().unread == 0);

    testdb::cleanupDb(kMuDbPath);
}

TEST_CASE("MU-S3 新库有 f_unread_mark 与 t_group_follow")
{
    //新库
    RemoveSchemaDbFiles();
    {
        CSqliteManager manager;
        REQUIRE(manager.init(kMuSchemaDbPath));
        CDatabaseSqlite db;
        REQUIRE(db.initialize(kMuSchemaDbPath));
        CHECK(HasColumn(db, "t_conversation", "f_unread_mark"));
        CHECK(HasColumn(db, "t_group_follow", "f_userid"));
        CHECK(HasColumn(db, "t_group_follow", "f_groupid"));
        CHECK(HasColumn(db, "t_group_follow", "f_target_userid"));
        CHECK(HasColumn(db, "t_group_follow", "f_create_time"));
    }
    RemoveSchemaDbFiles();
}
