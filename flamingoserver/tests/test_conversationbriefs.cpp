/**
 *  会话摘要产出内容的正确性用例（Suite CVE），test_conversationbriefs.cpp
 *
 *  这一套是为「会话列表拉取性能优化」第二步（逐会话查改批量查）做的<u>参照物</u>：
 *  第二步是纯重构 —— 把每个会话一条 SQL 改成整批两条，**产出必须逐字不变**。
 *  故先把现在这条路吐出来的每一个字段钉死，写在动代码之前，且<u>改动前就应当全绿</u>。
 *
 *  与 CVU 套的分工：CVU 盯的是"什么算未读"这一条语义；本套盯的是整条会话摘要的
 *  全部字段与集合行为 —— 最后一条消息取的对不对、置顶免打扰有没有带出来、
 *  已删除的会话该不该出现、多个会话时会不会串行。
 *
 *  这些用例会真的打开一个临时 .db 文件跑 SQL。数据量都很小（几十条），
 *  进默认测试集没有负担；真正耗时的规模测试在 CVB 套。
 *
 *  balloonwj@qq.com   2026-08-12
 */
#include <stdio.h>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "doctest.h"
#include "test_dbbackend.h"
#include "../chatserversrc/MessageManager.h"
#include "../chatserversrc/Protocol.h"
#include "../dbapi/DbConfig.h"
#include "../dbapi/IDatabase.h"
#include "../dbapi/DbFactory.h"
#include "../sqliteapi/SqliteManager.h"
#include "../base/Singleton.h"

//本套用例专用的临时数据库文件，与别的套用的文件分开，避免互相干扰
static const char* const CVE_DB_PATH = "flamingo_sqlite_convbriefs_test.db";

//灌数据用的基准时间戳（Unix 毫秒）。固定取值而不取当前时间，保证结果可复现。
static const int64_t CVE_BASE_TIME = 1700000000000LL;

static const int32_t CVE_USER_ID = 100001;

//三个对端与三个群，用来盯"多个会话时互不串行"。
static const int64_t CVE_PEER_A = 200001;
static const int64_t CVE_PEER_B = 200002;
static const int64_t CVE_PEER_C = 200003;
static const int64_t CVE_GROUP_A = 900001;
static const int64_t CVE_GROUP_B = 900002;
static const int64_t CVE_GROUP_C = 900003;

//消息内容类型：本套只用文本与图片两种，够区分 lastmsgtype 是否取对。
static const int CVE_MSGTYPE_TEXT  = 1;
static const int CVE_MSGTYPE_IMAGE = 2;

//拉历史时一次最多取的条数（RBY1 用），比用例里灌的消息数多即可
static const int CVE_HISTORY_LIMIT = 20;

//MySQL 模式下用的测试库名。会被整个 DROP 重建，故用测试专用名字。
static const char* const CVE_MYSQL_DB = "flamingo_test_cve";

//重建一个干净的测试库，并把 CDbConfig 指过去。按环境变量选后端
//（见 test_dbbackend.h）：默认 SQLite；设了 FLAMINGO_TEST_MYSQL=1 则跑 MySQL。
static void resetCveDb()
{
    testdb::resetDb(CVE_DB_PATH, CVE_MYSQL_DB);
}

//收尾清理。SQLite 删临时库文件；MySQL 留着库便于事后排查。
static void removeCveDbFiles()
{
    testdb::cleanupDb(CVE_DB_PATH);
}

static IDatabase* openConn()
{
    IDatabase* pConn = DbFactory::createConnection();
    REQUIRE(pConn != NULL);
    return pConn;
}

/**
 *  造一条落库用的 f_content。
 *
 *  格式不能随便写：MessageProtocol::buildStoredContent 打包成
 *  {"content":{...},"mentions":[],"mentionall":false}，而读回时
 *  parseStoredContent 只接受 content 是<u>对象</u>的情形，否则解出空串。
 *  故内层必须再套一层对象，这里统一用 {"text":"..."}。
 */
static std::string storedContent(const char* text)
{
    std::ostringstream os;
    os << "{\"content\":{\"text\":\"" << text << "\"},\"mentions\":[],\"mentionall\":false}";
    return os.str();
}

//读回时 lastcontent 是 content 那一层的紧凑 JSON 文本，据此拼出期望值。
static std::string expectContent(const char* text)
{
    std::ostringstream os;
    os << "{\"text\":\"" << text << "\"}";
    return os.str();
}

/**
 *  插一条消息。text 直接写进 f_content 的 JSON 里，供核对 lastcontent 是否取对。
 */
static void insertMsg(int64_t msgid, int ctype, int64_t from, int64_t to,
                      int msgtype, const char* text)
{
    std::unique_ptr<IDatabase> pConn(openConn());
    std::ostringstream os;
    os << "INSERT INTO t_chatmsg "
          "(f_msgid,f_clientmsgid,f_ctype,f_from,f_to,f_msgtype,f_content,"
          "f_recalled,f_create_time) VALUES ("
       << msgid << ",'c" << msgid << "'," << ctype << "," << from << "," << to
       << "," << msgtype << ",'" << storedContent(text) << "',0,"
       << (CVE_BASE_TIME + msgid) << ")";
    REQUIRE(pConn->execute(os.str().c_str()));
}

//写一行会话状态（置顶 / 消息提醒模式 / 已读位点 / 删除标记与锚点）。
static void setConvState(int ctype, int64_t cid, int pinned, int notifymode,
                         int64_t readmsgid, int deleted, int64_t deletedmsgid)
{
    std::unique_ptr<IDatabase> pConn(openConn());
    std::ostringstream os;
    os << "REPLACE INTO t_conversation "
          "(f_userid,f_ctype,f_cid,f_pinned,f_notify_mode,f_readmsgid,f_deleted,"
          "f_deleted_msgid,f_update_time) VALUES ("
       << CVE_USER_ID << "," << ctype << "," << cid << "," << pinned << "," << notifymode
       << "," << readmsgid << "," << deleted << "," << deletedmsgid << ","
       << CVE_BASE_TIME << ")";
    REQUIRE(pConn->execute(os.str().c_str()));
}

//在结果集里按 cid 找一条；找不到返回 NULL。
static const ConversationBrief* findBrief(const std::vector<ConversationBrief>& list,
                                          int64_t cid)
{
    for (size_t i = 0; i < list.size(); ++i)
    {
        if (list[i].cid == cid)
            return &list[i];
    }
    return NULL;
}

static std::vector<ConversationBrief> singleBriefs()
{
    std::vector<ConversationBrief> out;
    REQUIRE(Singleton<MessageManager>::Instance().getConversationBriefs(CVE_USER_ID, out));
    return out;
}

static std::vector<ConversationBrief> groupBriefs(const std::vector<int64_t>& groupids)
{
    std::vector<ConversationBrief> out;
    REQUIRE(Singleton<MessageManager>::Instance()
                .getGroupConversationBriefs(CVE_USER_ID, groupids, out));
    return out;
}

TEST_CASE("CVE1 单聊：每个会话的最后一条消息各取各的，不串行")
{
    resetCveDb();

    //三个对端交错发消息，msgid 顺序刻意打乱到不同会话上
    insertMsg(1, conversation_single, CVE_PEER_A, CVE_USER_ID, CVE_MSGTYPE_TEXT,  "A1");
    insertMsg(2, conversation_single, CVE_PEER_B, CVE_USER_ID, CVE_MSGTYPE_TEXT,  "B1");
    insertMsg(3, conversation_single, CVE_PEER_A, CVE_USER_ID, CVE_MSGTYPE_IMAGE, "A2");
    insertMsg(4, conversation_single, CVE_USER_ID, CVE_PEER_C, CVE_MSGTYPE_TEXT,  "C1");
    insertMsg(5, conversation_single, CVE_PEER_B, CVE_USER_ID, CVE_MSGTYPE_TEXT,  "B2");

    const std::vector<ConversationBrief> list = singleBriefs();
    REQUIRE(list.size() == 3);

    const ConversationBrief* a = findBrief(list, CVE_PEER_A);
    REQUIRE(a != NULL);
    CHECK(a->ctype == conversation_single);
    CHECK(a->lastmsgid == 3);
    CHECK(a->lastfrom == CVE_PEER_A);
    CHECK(a->lastmsgtype == CVE_MSGTYPE_IMAGE);
    CHECK(a->lastcontent == expectContent("A2"));
    CHECK(a->lasttimestamp == CVE_BASE_TIME + 3);

    const ConversationBrief* b = findBrief(list, CVE_PEER_B);
    REQUIRE(b != NULL);
    CHECK(b->lastmsgid == 5);
    CHECK(b->lastcontent == expectContent("B2"));

    //自己发出去的那条同样构成会话，最后一条的发送方是自己
    const ConversationBrief* c = findBrief(list, CVE_PEER_C);
    REQUIRE(c != NULL);
    CHECK(c->lastmsgid == 4);
    CHECK(c->lastfrom == CVE_USER_ID);
    CHECK(c->lastcontent == expectContent("C1"));

    removeCveDbFiles();
}

TEST_CASE("CVE2 单聊：置顶 / 消息提醒模式 / 未读随各自的会话行走")
{
    resetCveDb();

    insertMsg(1, conversation_single, CVE_PEER_A, CVE_USER_ID, CVE_MSGTYPE_TEXT, "A1");
    insertMsg(2, conversation_single, CVE_PEER_A, CVE_USER_ID, CVE_MSGTYPE_TEXT, "A2");
    insertMsg(3, conversation_single, CVE_PEER_B, CVE_USER_ID, CVE_MSGTYPE_TEXT, "B1");

    //A 置顶、允许提醒、已读到 1；B 不置顶、收进消息盒子、一条没读
    setConvState(conversation_single, CVE_PEER_A, 1, notify_mode_allow, 1, 0, 0);
    setConvState(conversation_single, CVE_PEER_B, 0, notify_mode_msgbox, 0, 0, 0);

    const std::vector<ConversationBrief> list = singleBriefs();
    REQUIRE(list.size() == 2);

    const ConversationBrief* a = findBrief(list, CVE_PEER_A);
    REQUIRE(a != NULL);
    CHECK(a->pinned);
    CHECK(a->notifymode == notify_mode_allow);
    CHECK(a->unread == 1);

    const ConversationBrief* b = findBrief(list, CVE_PEER_B);
    REQUIRE(b != NULL);
    CHECK_FALSE(b->pinned);
    CHECK(b->notifymode == notify_mode_msgbox);
    CHECK(b->unread == 1);

    removeCveDbFiles();
}

TEST_CASE("CVE3 单聊：已删除的会话不出现，但来了新消息就重现")
{
    resetCveDb();

    insertMsg(1, conversation_single, CVE_PEER_A, CVE_USER_ID, CVE_MSGTYPE_TEXT, "A1");
    insertMsg(2, conversation_single, CVE_PEER_B, CVE_USER_ID, CVE_MSGTYPE_TEXT, "B1");

    //A 被删除，锚点停在它当时的最后一条（msgid=1）
    setConvState(conversation_single, CVE_PEER_A, 0, 0, 0, 1, 1);

    {
        const std::vector<ConversationBrief> list = singleBriefs();
        REQUIRE(list.size() == 1);
        CHECK(list[0].cid == CVE_PEER_B);
    }

    //删除之后又来了一条新消息：会话重现，且最后一条取的是新那条
    insertMsg(3, conversation_single, CVE_PEER_A, CVE_USER_ID, CVE_MSGTYPE_TEXT, "A2");
    {
        const std::vector<ConversationBrief> list = singleBriefs();
        REQUIRE(list.size() == 2);
        const ConversationBrief* a = findBrief(list, CVE_PEER_A);
        REQUIRE(a != NULL);
        CHECK(a->lastmsgid == 3);
        CHECK(a->lastcontent == expectContent("A2"));
    }

    removeCveDbFiles();
}

TEST_CASE("CVE4 群聊：每个群的最后一条各取各的，不串行")
{
    resetCveDb();

    std::vector<int64_t> groupids;
    groupids.push_back(CVE_GROUP_A);
    groupids.push_back(CVE_GROUP_B);
    groupids.push_back(CVE_GROUP_C);

    //三个群交错发消息
    insertMsg(1, conversation_group, CVE_PEER_A, CVE_GROUP_A, CVE_MSGTYPE_TEXT,  "GA1");
    insertMsg(2, conversation_group, CVE_PEER_B, CVE_GROUP_B, CVE_MSGTYPE_TEXT,  "GB1");
    insertMsg(3, conversation_group, CVE_PEER_C, CVE_GROUP_A, CVE_MSGTYPE_IMAGE, "GA2");
    insertMsg(4, conversation_group, CVE_USER_ID, CVE_GROUP_C, CVE_MSGTYPE_TEXT, "GC1");

    const std::vector<ConversationBrief> list = groupBriefs(groupids);
    REQUIRE(list.size() == 3);

    const ConversationBrief* a = findBrief(list, CVE_GROUP_A);
    REQUIRE(a != NULL);
    CHECK(a->ctype == conversation_group);
    CHECK(a->lastmsgid == 3);
    CHECK(a->lastfrom == CVE_PEER_C);
    CHECK(a->lastmsgtype == CVE_MSGTYPE_IMAGE);
    CHECK(a->lastcontent == expectContent("GA2"));
    CHECK(a->lasttimestamp == CVE_BASE_TIME + 3);
    CHECK(a->unread == 2);   //两条都是别人发的

    const ConversationBrief* b = findBrief(list, CVE_GROUP_B);
    REQUIRE(b != NULL);
    CHECK(b->lastmsgid == 2);
    CHECK(b->unread == 1);

    //只有自己发过消息的群：最后一条是自己的，未读为 0
    const ConversationBrief* c = findBrief(list, CVE_GROUP_C);
    REQUIRE(c != NULL);
    CHECK(c->lastmsgid == 4);
    CHECK(c->lastfrom == CVE_USER_ID);
    CHECK(c->unread == 0);

    removeCveDbFiles();
}

TEST_CASE("CVE5 群聊：一条消息都没有的群不进会话列表")
{
    resetCveDb();

    std::vector<int64_t> groupids;
    groupids.push_back(CVE_GROUP_A);
    groupids.push_back(CVE_GROUP_B);   //这个群一条消息都没有

    insertMsg(1, conversation_group, CVE_PEER_A, CVE_GROUP_A, CVE_MSGTYPE_TEXT, "GA1");

    const std::vector<ConversationBrief> list = groupBriefs(groupids);
    REQUIRE(list.size() == 1);
    CHECK(list[0].cid == CVE_GROUP_A);

    removeCveDbFiles();
}

TEST_CASE("CVE6 群聊：置顶 / 消息提醒模式 / 已删除随各自的会话行走")
{
    resetCveDb();

    std::vector<int64_t> groupids;
    groupids.push_back(CVE_GROUP_A);
    groupids.push_back(CVE_GROUP_B);
    groupids.push_back(CVE_GROUP_C);

    insertMsg(1, conversation_group, CVE_PEER_A, CVE_GROUP_A, CVE_MSGTYPE_TEXT, "GA1");
    insertMsg(2, conversation_group, CVE_PEER_A, CVE_GROUP_B, CVE_MSGTYPE_TEXT, "GB1");
    insertMsg(3, conversation_group, CVE_PEER_A, CVE_GROUP_C, CVE_MSGTYPE_TEXT, "GC1");

    setConvState(conversation_group, CVE_GROUP_A, 1, notify_mode_allow, 0, 0, 0);   //置顶
    setConvState(conversation_group, CVE_GROUP_B, 0, notify_mode_block, 0, 0, 0);   //屏蔽
    setConvState(conversation_group, CVE_GROUP_C, 0, 0, 0, 1, 3);   //已删除且无新消息

    const std::vector<ConversationBrief> list = groupBriefs(groupids);
    REQUIRE(list.size() == 2);   //C 被隐藏

    const ConversationBrief* a = findBrief(list, CVE_GROUP_A);
    REQUIRE(a != NULL);
    CHECK(a->pinned);
    CHECK(a->notifymode == notify_mode_allow);

    const ConversationBrief* b = findBrief(list, CVE_GROUP_B);
    REQUIRE(b != NULL);
    CHECK_FALSE(b->pinned);
    CHECK(b->notifymode == notify_mode_block);

    CHECK(findBrief(list, CVE_GROUP_C) == NULL);

    removeCveDbFiles();
}

TEST_CASE("CVE7 群聊：本人已删除的那条消息不能当作最后一条")
{
    resetCveDb();

    std::vector<int64_t> groupids;
    groupids.push_back(CVE_GROUP_A);

    insertMsg(1, conversation_group, CVE_PEER_A, CVE_GROUP_A, CVE_MSGTYPE_TEXT, "GA1");
    insertMsg(2, conversation_group, CVE_PEER_A, CVE_GROUP_A, CVE_MSGTYPE_TEXT, "GA2");

    //把最新那条标记成"本人已删除"：会话预览应当回退到上一条
    {
        std::unique_ptr<IDatabase> pConn(openConn());
        std::ostringstream os;
        os << "REPLACE INTO t_message_deleted (f_userid,f_msgid) VALUES ("
           << CVE_USER_ID << ",2)";
        REQUIRE(pConn->execute(os.str().c_str()));
    }

    const std::vector<ConversationBrief> list = groupBriefs(groupids);
    REQUIRE(list.size() == 1);
    CHECK(list[0].lastmsgid == 1);
    CHECK(list[0].lastcontent == expectContent("GA1"));
    CHECK(list[0].unread == 1);   //删掉的那条也不该再计入未读

    removeCveDbFiles();
}

//把一条消息标成已撤回（与 MessageManager::recallMessage 写的是同一列）
static void markRecalled(int64_t msgid)
{
    std::unique_ptr<IDatabase> pConn(openConn());
    std::ostringstream os;
    os << "UPDATE t_chatmsg SET f_recalled = 1 WHERE f_msgid = " << msgid;
    REQUIRE(pConn->execute(os.str().c_str()));
}

TEST_CASE("CVE8 单聊：最后一条已撤回时摘要带上撤回标志，没撤回的会话不受影响")
{
    resetCveDb();

    insertMsg(1, conversation_single, CVE_PEER_A, CVE_USER_ID, CVE_MSGTYPE_TEXT, "A1");
    insertMsg(2, conversation_single, CVE_PEER_A, CVE_USER_ID, CVE_MSGTYPE_TEXT, "A2");
    insertMsg(3, conversation_single, CVE_PEER_B, CVE_USER_ID, CVE_MSGTYPE_TEXT, "B1");
    markRecalled(2);

    const std::vector<ConversationBrief> list = singleBriefs();
    const ConversationBrief* a = findBrief(list, CVE_PEER_A);
    const ConversationBrief* b = findBrief(list, CVE_PEER_B);
    REQUIRE(a != NULL);
    REQUIRE(b != NULL);
    //最后一条仍是被撤回的那条（会话列表的位置与时间照旧），只是带上标志，由应答组装时隐去原文
    CHECK(a->lastmsgid == 2);
    CHECK(a->lastfrom == CVE_PEER_A);
    CHECK(a->lastrecalled);
    CHECK_FALSE(b->lastrecalled);

    removeCveDbFiles();
}

TEST_CASE("CVE9 群聊：最后一条已撤回时摘要带上撤回标志")
{
    resetCveDb();

    std::vector<int64_t> groupids;
    groupids.push_back(CVE_GROUP_A);
    groupids.push_back(CVE_GROUP_B);
    insertMsg(1, conversation_group, CVE_PEER_A, CVE_GROUP_A, CVE_MSGTYPE_TEXT, "GA1");
    insertMsg(2, conversation_group, CVE_USER_ID, CVE_GROUP_A, CVE_MSGTYPE_TEXT, "GA2");
    insertMsg(3, conversation_group, CVE_PEER_B, CVE_GROUP_B, CVE_MSGTYPE_TEXT, "GB1");
    markRecalled(2);

    const std::vector<ConversationBrief> list = groupBriefs(groupids);
    const ConversationBrief* ga = findBrief(list, CVE_GROUP_A);
    const ConversationBrief* gb = findBrief(list, CVE_GROUP_B);
    REQUIRE(ga != NULL);
    REQUIRE(gb != NULL);
    CHECK(ga->lastmsgid == 2);
    CHECK(ga->lastfrom == CVE_USER_ID);
    CHECK(ga->lastrecalled);
    CHECK_FALSE(gb->lastrecalled);

    removeCveDbFiles();
}

TEST_CASE("CVE10 群主撤回别人发的最后一条：摘要带上撤回者，发送方不变；存量的撤回数据撤回者为 0")
{
    resetCveDb();

    std::vector<int64_t> groupids;
    groupids.push_back(CVE_GROUP_A);
    groupids.push_back(CVE_GROUP_B);
    insertMsg(1, conversation_group, CVE_PEER_A, CVE_GROUP_A, CVE_MSGTYPE_TEXT, "GA1");
    insertMsg(2, conversation_group, CVE_PEER_B, CVE_GROUP_B, CVE_MSGTYPE_TEXT, "GB1");
    //群 A：本人（群主）撤回成员 A 发的消息
    REQUIRE(Singleton<MessageManager>::Instance().recallMessage(1, CVE_USER_ID));
    //群 B：只置撤回标志，模拟 2026-10-03 之前撤回、没有记录撤回者的存量数据
    markRecalled(2);

    const std::vector<ConversationBrief> list = groupBriefs(groupids);
    const ConversationBrief* ga = findBrief(list, CVE_GROUP_A);
    const ConversationBrief* gb = findBrief(list, CVE_GROUP_B);
    REQUIRE(ga != NULL);
    REQUIRE(gb != NULL);
    CHECK(ga->lastrecalled);
    CHECK(ga->lastfrom == CVE_PEER_A);
    CHECK(ga->lastrecalledby == CVE_USER_ID);
    CHECK(gb->lastrecalled);
    CHECK(gb->lastrecalledby == 0);

    removeCveDbFiles();
}

TEST_CASE("RBY1 撤回后按 msgid 取消息、拉历史、按 id 批量取都带出撤回者，没撤回的为 0")
{
    resetCveDb();

    insertMsg(1, conversation_group, CVE_PEER_A, CVE_GROUP_A, CVE_MSGTYPE_TEXT, "G1");
    insertMsg(2, conversation_group, CVE_PEER_B, CVE_GROUP_A, CVE_MSGTYPE_TEXT, "G2");
    MessageManager& msgMgr = Singleton<MessageManager>::Instance();
    REQUIRE(msgMgr.recallMessage(1, CVE_USER_ID));

    MessageRecord rec;
    REQUIRE(msgMgr.getMessageById(1, rec));
    CHECK(rec.recalled);
    CHECK(rec.recalledby == CVE_USER_ID);
    CHECK(rec.from == CVE_PEER_A);
    REQUIRE(msgMgr.getMessageById(2, rec));
    CHECK_FALSE(rec.recalled);
    CHECK(rec.recalledby == 0);

    //拉历史：聊天窗渲染撤回提示行用的就是这条路径
    SyncMessagesRequest req;
    req.ctype = conversation_group;
    req.cid   = CVE_GROUP_A;
    req.limit = CVE_HISTORY_LIMIT;
    std::vector<MessageRecord> rows;
    bool hasMore = false;
    REQUIRE(msgMgr.getConversationMessages(CVE_USER_ID, req, rows, hasMore));
    REQUIRE(rows.size() == 2);
    CHECK(rows[0].msgid == 1);
    CHECK(rows[0].recalledby == CVE_USER_ID);
    CHECK(rows[1].msgid == 2);
    CHECK(rows[1].recalledby == 0);

    //按 id 批量取（转发、合并记录等路径）与上面共用同一份列序，一并核对
    std::vector<int64_t> msgids;
    msgids.push_back(1);
    std::vector<MessageRecord> picked;
    REQUIRE(msgMgr.getMessagesByIds(CVE_USER_ID, msgids, picked));
    REQUIRE(picked.size() == 1);
    CHECK(picked[0].recalledby == CVE_USER_ID);

    removeCveDbFiles();
}

TEST_CASE("SSTI3b 改写已存消息的内容：只改内容，发送方与撤回状态不变，内容里的引号照常转义")
{
    resetCveDb();

    insertMsg(1, conversation_group, CVE_PEER_A, CVE_GROUP_A, CVE_MSGTYPE_TEXT, "old");
    insertMsg(2, conversation_group, CVE_PEER_B, CVE_GROUP_A, CVE_MSGTYPE_TEXT, "other");
    MessageManager& msgMgr = Singleton<MessageManager>::Instance();
    REQUIRE(msgMgr.updateMessageContent(1, storedContent("it's new")));

    MessageRecord rec;
    REQUIRE(msgMgr.getMessageById(1, rec));
    CHECK(rec.content == expectContent("it's new"));
    CHECK(rec.from == CVE_PEER_A);
    CHECK_FALSE(rec.recalled);
    //别的消息不受影响
    REQUIRE(msgMgr.getMessageById(2, rec));
    CHECK(rec.content == expectContent("other"));
    //msgid 不存在：什么也不改，照样返回成功
    CHECK(msgMgr.updateMessageContent(999, storedContent("x")));

    removeCveDbFiles();
}
