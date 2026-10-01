/**
 *  转发取源消息的用例（Suite FWD），test_forwardsource.cpp
 *
 *  cmd_forward_message 经 MessageManager::getMessagesByIds 按 msgid 取转发源。
 *  本套钉住它的过滤口径：转发人本人已删除的消息（单条删除记在 t_message_deleted，
 *  按时间范围清理记在 t_message_deleted_range）不得进入转发源，口径与同步、搜索、
 *  会话列表所用的 notDeletedClause 一致；删除只对本人生效，另一方照样取得到。
 *  对应 docs/聊天消息转发方案.md 的 FWD-S3（修复前服务端允许转发本人已删除的消息）。
 *
 *  用例会真的打开一个临时 .db 文件跑 SQL：MessageManager 经 DbFactory 取连接，
 *  而 DbFactory 读的是全局 CDbConfig，故每个用例先把 CDbConfig 指到本套专用的库文件上。
 *
 *  balloonwj@qq.com   2026-10-01
 */
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "doctest.h"
#include "test_dbbackend.h"
#include "../chatserversrc/MessageManager.h"
#include "../chatserversrc/MessageDeleteRange.h"
#include "../chatserversrc/Protocol.h"
#include "../dbapi/IDatabase.h"
#include "../dbapi/DbFactory.h"
#include "../base/Singleton.h"

//本套用例专用的临时数据库文件，与别的套用的文件分开，避免互相干扰
static const char* const FWD_DB_PATH = "flamingo_sqlite_forwardsource_test.db";

//MySQL 模式下用的测试库名。会被整个 DROP 重建，故用测试专用名字。
static const char* const FWD_MYSQL_DB = "flamingo_test_fwd";

//灌数据用的基准时间戳（Unix 毫秒）。每条消息的 f_create_time 取基准值加 msgid，
//固定取值而不取当前时间，保证按时间范围清理的用例结果可复现。
static const int64_t FWD_BASE_TIME = 1700000000000LL;

//转发人与单聊对端的 userid
static const int32_t FWD_USER_ID = 100001;
static const int32_t FWD_PEER_ID = 200001;

//消息正文，与过滤口径无关，取一条短文本即可
static const char* const FWD_CONTENT = "{\"text\":\"forward source\"}";

//本套用例灌入的消息条数，msgid 取 1 ~ FWD_MSG_COUNT
static const int64_t FWD_MSG_COUNT = 5;

//取一个数据库连接，调用方负责释放
static IDatabase* openFwdConn()
{
    IDatabase* pConn = DbFactory::createConnection();
    REQUIRE(pConn != NULL);
    return pConn;
}

/**
 *  插一条单聊消息，f_create_time 取 FWD_BASE_TIME + msgid。
 *    msgid：   消息 id（用例自己排号，保证顺序可控）
 *    from/to： 发送方 / 接收方 userid
 */
static void insertFwdMsg(int64_t msgid, int32_t from, int32_t to)
{
    std::unique_ptr<IDatabase> pConn(openFwdConn());
    std::ostringstream os;
    os << "INSERT INTO t_chatmsg "
          "(f_msgid,f_clientmsgid,f_ctype,f_from,f_to,f_msgtype,f_content,"
          "f_recalled,f_create_time) VALUES ("
       << msgid << ",'c" << msgid << "'," << conversation_single << "," << from << "," << to
       << ",1,'" << FWD_CONTENT << "',0," << (FWD_BASE_TIME + msgid) << ")";
    REQUIRE(pConn->execute(os.str().c_str()));
}

//灌入 1 ~ FWD_MSG_COUNT 共若干条单聊消息，收发方向交替
static void insertFwdConversation()
{
    for (int64_t i = 1; i <= FWD_MSG_COUNT; ++i)
    {
        if (i % 2 == 1)
            insertFwdMsg(i, FWD_USER_ID, FWD_PEER_ID);
        else
            insertFwdMsg(i, FWD_PEER_ID, FWD_USER_ID);
    }
}

//1 ~ FWD_MSG_COUNT 的 msgid 列表，作为转发请求里的 msgids
static std::vector<int64_t> allFwdMsgids()
{
    std::vector<int64_t> ids;
    for (int64_t i = 1; i <= FWD_MSG_COUNT; ++i)
        ids.push_back(i);
    return ids;
}

//以 viewer 的身份取转发源，返回命中消息的 msgid 列表（按返回顺序）
static std::vector<int64_t> fetchSourceIds(int32_t viewer)
{
    std::vector<MessageRecord> out;
    REQUIRE(Singleton<MessageManager>::Instance().getMessagesByIds(viewer, allFwdMsgids(), out));
    std::vector<int64_t> ids;
    for (size_t i = 0; i < out.size(); ++i)
        ids.push_back(out[i].msgid);
    return ids;
}

TEST_CASE("FWD-S3a 没有删除时按 msgid 升序全部取回")
{
    testdb::resetDb(FWD_DB_PATH, FWD_MYSQL_DB);
    insertFwdConversation();

    std::vector<int64_t> expected = allFwdMsgids();
    CHECK(fetchSourceIds(FWD_USER_ID) == expected);

    testdb::cleanupDb(FWD_DB_PATH);
}

TEST_CASE("FWD-S3b 本人单条删除的消息不进入转发源，对方不受影响")
{
    testdb::resetDb(FWD_DB_PATH, FWD_MYSQL_DB);
    insertFwdConversation();

    //转发人删掉第 2 条：只对本人生效
    REQUIRE(Singleton<MessageManager>::Instance().deleteMessageForUser(FWD_USER_ID, 2));

    std::vector<int64_t> mine;
    mine.push_back(1);
    mine.push_back(3);
    mine.push_back(4);
    mine.push_back(5);
    CHECK(fetchSourceIds(FWD_USER_ID) == mine);

    //对方没有删除，照样取得到全部
    std::vector<int64_t> peers = allFwdMsgids();
    CHECK(fetchSourceIds(FWD_PEER_ID) == peers);

    testdb::cleanupDb(FWD_DB_PATH);
}

TEST_CASE("FWD-S3c 本人按时间范围清理掉的消息不进入转发源，对方不受影响")
{
    testdb::resetDb(FWD_DB_PATH, FWD_MYSQL_DB);
    insertFwdConversation();

    //清理第 3、4 条所在的时间段（两端都包含）
    msgdelete::DeleteRange range(FWD_BASE_TIME + 3, FWD_BASE_TIME + 4);
    REQUIRE(Singleton<MessageManager>::Instance().addDeletedRange(FWD_USER_ID, range, FWD_BASE_TIME));

    std::vector<int64_t> mine;
    mine.push_back(1);
    mine.push_back(2);
    mine.push_back(5);
    CHECK(fetchSourceIds(FWD_USER_ID) == mine);

    std::vector<int64_t> peers = allFwdMsgids();
    CHECK(fetchSourceIds(FWD_PEER_ID) == peers);

    testdb::cleanupDb(FWD_DB_PATH);
}
