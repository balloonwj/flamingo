/**
 *  会话列表拉取耗时的计时用例（Suite CVB），test_conversationbench.cpp
 *
 *  用来回答一个具体问题：一个加了很多群、群里积了很多消息的用户登录时，服务端
 *  `onSyncConversations` 这一趟要花多久。
 *
 *  背景（都是代码事实，非推测）：
 *    · `MessageManager::getConversationBriefs` 每次调用<u>新建一个数据库连接</u>
 *      （`DbFactory::createConnection()`），随后从 t_chatmsg 派生该用户的全部单聊
 *      会话，<u>没有时间窗、没有条数上限</u>，再逐个会话补最后一条消息与未读数；
 *    · `MessageManager::getGroupConversationBriefs` <u>逐群两条 SQL</u>：一条取本群
 *      最后一条消息，一条 `SELECT COUNT(*)` 数未读；
 *    · 这两个函数是在网络 I/O 线程上同步调用的（ChatServer 起 6 条 I/O 线程，
 *      会话读回调直接在其上执行，中途没有交给数据库线程池），所以这段耗时期间，
 *      同一条线程上的其他连接都在等。
 *
 *  故本用例按不同规模量三件事：群数量、每群消息条数、单聊会话数量各自的影响。
 *
 *  <u>本套用例不参与默认测试集</u>：它要真建库、真灌几万行数据，跑一次几十秒，
 *  混进常规回归里会把每次跑测试的成本抬上去。三个用例都挂了 doctest::skip()，
 *  默认只列名不执行；要跑必须<u>同时</u>给上 --no-skip 与用例名过滤：
 *      Tests.exe --no-skip --test-case="CVB*"
 *  只给 --test-case 是不够的 —— doctest 的名字过滤不会解除 skip 标记。
 *  数字用 MESSAGE 输出（doctest 的 MESSAGE 无论成败都打印）。用例只断言"跑完了、
 *  条数对得上"，不断言具体快慢 —— 写死时间阈值必然变成一条时不时误报的用例。
 *
 *  balloonwj@qq.com   2026-08-12
 */
#include <stdio.h>
#include <memory>
#include <sstream>
#include <string>
#include <vector>
#include <chrono>

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
static const char* const CVB_DB_PATH = "flamingo_sqlite_convbench_test.db";

//灌数据用的基准时间戳（Unix 毫秒）。固定取值而不取当前时间，保证结果可复现。
static const int64_t CVB_BASE_TIME = 1700000000000LL;

//被测用户的 userid。取一个不与灌进去的群 id / 对端 id 撞车的值。
static const int32_t CVB_USER_ID = 100001;

//群 id 的起始值。与单聊对端 id 分开号段，便于看日志时一眼区分。
static const int64_t CVB_GROUP_ID_BASE = 900001;

//单聊对端 userid 的起始值。
static const int64_t CVB_PEER_ID_BASE = 200001;

//每条消息的正文长度取一条中等长度的聊天文本 —— 太短会把真实的行宽掩盖掉。
static const char* const CVB_CONTENT =
    "{\"text\":\"这是一条用于计时的群聊消息，长度取中等水平，便于反映真实行宽。\"}";

//MySQL 模式下用的测试库名。会被整个 DROP 重建，故用测试专用名字。
static const char* const CVB_MYSQL_DB = "flamingo_test_cvb";

//重建一个干净的测试库，并把 CDbConfig 指过去。按环境变量选后端
//（见 test_dbbackend.h）：默认 SQLite；设了 FLAMINGO_TEST_MYSQL=1 则跑 MySQL。
static void resetCvbDb()
{
    testdb::resetDb(CVB_DB_PATH, CVB_MYSQL_DB);
}

//收尾清理。SQLite 删临时库文件；MySQL 留着库便于事后排查。
static void removeCvbDbFiles()
{
    testdb::cleanupDb(CVB_DB_PATH);
}

//毫秒计时。steady_clock 不受系统时间调整影响，测耗时用它而不是 system_clock。
static double nowMs()
{
    const std::chrono::steady_clock::time_point t = std::chrono::steady_clock::now();
    return static_cast<double>(
               std::chrono::duration_cast<std::chrono::microseconds>(
                   t.time_since_epoch()).count()) / 1000.0;
}

/**
 *  往 t_chatmsg 里灌数据。
 *
 *  灌数据本身不是被测对象，故整批包在一个事务里 —— 不然光造数据就要等上几分钟。
 *
 *    groupCount：   群数量；每个群灌 msgsPerGroup 条群聊消息（f_ctype=2）。
 *    msgsPerGroup： 每群消息条数。
 *    peerCount：    单聊对端数量；每个对端灌 msgsPerPeer 条单聊消息（f_ctype=1）。
 *    msgsPerPeer：  每个单聊对端的消息条数。
 */
static void seedMessages(int groupCount, int msgsPerGroup, int peerCount, int msgsPerPeer)
{
    std::unique_ptr<IDatabase> pConn;
    pConn.reset(DbFactory::createConnection());
    REQUIRE(pConn);

    REQUIRE(pConn->execute("BEGIN"));

    int64_t msgid = 1;
    //群聊消息：f_to 取群 id，f_from 轮流取几个不同的发送人（更接近真实群聊）。
    for (int g = 0; g < groupCount; ++g)
    {
        const int64_t groupid = CVB_GROUP_ID_BASE + g;
        for (int m = 0; m < msgsPerGroup; ++m)
        {
            std::ostringstream os;
            os << "INSERT INTO t_chatmsg "
                  "(f_msgid,f_clientmsgid,f_ctype,f_from,f_to,f_msgtype,f_content,"
                  "f_recalled,f_create_time) VALUES ("
               << msgid << ",'c" << msgid << "'," << conversation_group << ","
               << (CVB_PEER_ID_BASE + (m % 10)) << "," << groupid << ",1,'"
               << CVB_CONTENT << "',0," << (CVB_BASE_TIME + msgid) << ")";
            pConn->execute(os.str().c_str());
            ++msgid;
        }
    }

    //单聊消息：一半本人发出、一半对端发来，与真实会话的双向形态一致。
    for (int p = 0; p < peerCount; ++p)
    {
        const int64_t peer = CVB_PEER_ID_BASE + 1000 + p;
        for (int m = 0; m < msgsPerPeer; ++m)
        {
            const bool fromMe = ((m % 2) == 0);
            std::ostringstream os;
            os << "INSERT INTO t_chatmsg "
                  "(f_msgid,f_clientmsgid,f_ctype,f_from,f_to,f_msgtype,f_content,"
                  "f_recalled,f_create_time) VALUES ("
               << msgid << ",'c" << msgid << "'," << conversation_single << ","
               << (fromMe ? (int64_t)CVB_USER_ID : peer) << ","
               << (fromMe ? peer : (int64_t)CVB_USER_ID) << ",1,'"
               << CVB_CONTENT << "',0," << (CVB_BASE_TIME + msgid) << ")";
            pConn->execute(os.str().c_str());
            ++msgid;
        }
    }

    REQUIRE(pConn->execute("COMMIT"));
}

//造一份群 id 列表，模拟 GroupManager::getUserGroups 的返回。
static std::vector<int64_t> makeGroupIds(int groupCount)
{
    std::vector<int64_t> ids;
    for (int g = 0; g < groupCount; ++g)
    {
        ids.push_back(CVB_GROUP_ID_BASE + g);
    }
    return ids;
}

/**
 *  跑一轮：按给定规模灌数据，然后量一次"拉会话列表"的耗时（单聊 + 群聊两段分开计）。
 *  与 ChatSession::onSyncConversations 的调用顺序一致。
 */
static void runOneScale(int groupCount, int msgsPerGroup, int peerCount, int msgsPerPeer)
{
    resetCvbDb();
    seedMessages(groupCount, msgsPerGroup, peerCount, msgsPerPeer);

    MessageManager& mgr = Singleton<MessageManager>::Instance();

    //单聊那一段
    std::vector<ConversationBrief> singleBriefs;
    const double t0 = nowMs();
    const bool okSingle = mgr.getConversationBriefs(CVB_USER_ID, singleBriefs);
    const double costSingle = nowMs() - t0;

    //群聊那一段
    const std::vector<int64_t> groupids = makeGroupIds(groupCount);
    std::vector<ConversationBrief> groupBriefs;
    const double t1 = nowMs();
    const bool okGroup = mgr.getGroupConversationBriefs(CVB_USER_ID, groupids, groupBriefs);
    const double costGroup = nowMs() - t1;

    CHECK(okSingle);
    CHECK(okGroup);
    //条数对得上才说明量的是真活儿，而不是一条空查询
    CHECK(singleBriefs.size() == (size_t)peerCount);
    CHECK(groupBriefs.size() == (size_t)groupCount);

    std::ostringstream os;
    os << "群 " << groupCount << " 个 × 每群 " << msgsPerGroup << " 条"
       << "，单聊 " << peerCount << " 个 × 每个 " << msgsPerPeer << " 条"
       << "（消息共 " << (groupCount * msgsPerGroup + peerCount * msgsPerPeer) << " 条）"
       << "\n    单聊段 getConversationBriefs      = " << costSingle << " ms"
       << "\n    群聊段 getGroupConversationBriefs = " << costGroup << " ms"
       << "\n    合计（登录时阻塞 I/O 线程的时长） = " << (costSingle + costGroup) << " ms";
    MESSAGE(os.str());

    removeCvbDbFiles();
}

TEST_CASE("CVB1 群数量的影响（每群消息条数固定）" * doctest::skip())
{
    //只变群数量，看逐群两条 SQL 的累加效应
    runOneScale(10,  200, 10, 50);
    runOneScale(50,  200, 10, 50);
    runOneScale(200, 200, 10, 50);
}

TEST_CASE("CVB2 每群消息条数的影响（群数量固定）" * doctest::skip())
{
    //只变每群消息条数，看未读数那条 COUNT(*) 随消息表变大的走势
    runOneScale(50, 100,  10, 50);
    runOneScale(50, 1000, 10, 50);
    runOneScale(50, 5000, 10, 50);
}

TEST_CASE("CVB3 单聊会话数量的影响" * doctest::skip())
{
    //只变单聊对端数量，看单聊那一段逐会话补 lastmsg / 未读的累加效应
    runOneScale(10, 200, 20,  50);
    runOneScale(10, 200, 100, 50);
    runOneScale(10, 200, 300, 50);
}
