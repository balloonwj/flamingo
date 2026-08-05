/**
 *  消息 Pin 存储管理器单元测试（Suite MPM），test_messagepinmanager.cpp
 *
 *  覆盖 MessagePinManager 的 Pin / 取消 Pin 幂等性、按会话列举被 Pin 的消息
 *  （群聊 / 单聊双向 / 不串会话 / 排除已撤回与本端已删除）、单次返回条数截断
 *  与 total 的真实性，以及撤回和群解散两处清理。对应方案见
 *  docs/消息Pin接入方案.md 第 9 节第 9~17 条。
 *
 *  这些用例会真的打开一个临时 .db 文件跑 SQL —— MessagePinManager 经 DbFactory
 *  取连接，而 DbFactory 读的是全局 CDbConfig，故每个用例先把 CDbConfig 指到本套
 *  专用的库文件上。SQLite 是嵌入式的，不需要任何外部服务，放进默认测试集没有负担。
 *
 *  balloonwj@qq.com   2026-08-04
 */
#include <stdio.h>
#include <memory>
#include <string>
#include <vector>

#include "doctest.h"
#include "../chatserversrc/MessagePinManager.h"
#include "../chatserversrc/Protocol.h"       //conversation_type
#include "../dbapi/DbConfig.h"
#include "../dbapi/IDatabase.h"
#include "../dbapi/DbFactory.h"
#include "../sqliteapi/SqliteManager.h"
#include "../base/Singleton.h"

//本套用例专用的临时数据库文件，与 SQD / SQM 两套用的文件分开，避免互相干扰
static const char* const PIN_DB_PATH = "flamingo_sqlite_pin_test.db";

//删除测试库文件，连同 WAL 模式产生的两个附属文件一起清掉
static void removePinDbFiles()
{
    std::string basePath(PIN_DB_PATH);
    remove(basePath.c_str());
    remove((basePath + "-wal").c_str());
    remove((basePath + "-shm").c_str());
}

//把测试库重建成一个干净的完整结构库，并把全局 CDbConfig 指到它上面。
//每个用例开头都调一次，保证从确定的初始状态开始 —— MessagePinManager 是单例，
//用例之间共享，唯一能隔离的就是底下这个库文件。
static void resetPinDb()
{
    removePinDbFiles();

    //CSqliteManager::init 会把空文件建成完整结构（建表 + 补列 + 建索引与触发器）
    REQUIRE(Singleton<CSqliteManager>::Instance().init(PIN_DB_PATH));

    //DbFactory 按 CDbConfig 造连接；MySQL 那四个参数在 SQLite 模式下不参与连接
    Singleton<CDbConfig>::Instance().init(DB_TYPE_NAME_SQLITE, NULL, NULL, NULL,
                                          NULL, PIN_DB_PATH);
}

//往 t_chatmsg 插一条消息。内容取最简形态，本套用例只关心消息的归属与时间，
//不关心正文渲染。
//    msgid       指定的消息ID（本套用例显式给定，便于断言）
//    ctype       会话类型（conversation_single / conversation_group）
//    from        发送方userid
//    to          接收方：单聊为对端userid，群聊为群id
//    createTime  消息时间，Unix毫秒
//    recalled    是否已撤回
static void insertMessage(int64_t msgid, int ctype, int64_t from, int64_t to,
                          int64_t createTime, bool recalled = false)
{
    std::unique_ptr<IDatabase> pConn(DbFactory::createConnection());
    REQUIRE(pConn);

    char sql[512] = { 0 };
    ::snprintf(sql, sizeof(sql),
               "INSERT INTO t_chatmsg(f_msgid, f_clientmsgid, f_ctype, f_from, f_to, "
               "f_msgtype, f_content, f_recalled, f_create_time) VALUES(%lld, '', %d, "
               "%lld, %lld, 1, '{\"content\":{\"text\":\"hi\"}}', %d, %lld)",
               (long long)msgid, ctype, (long long)from, (long long)to,
               recalled ? 1 : 0, (long long)createTime);
    REQUIRE(pConn->execute(sql));
}

//往 t_message_deleted 插一行，表示 userid 从自己视角删掉了 msgid 这条消息
static void insertDeletedMark(int64_t userid, int64_t msgid)
{
    std::unique_ptr<IDatabase> pConn(DbFactory::createConnection());
    REQUIRE(pConn);

    char sql[256] = { 0 };
    ::snprintf(sql, sizeof(sql),
               "INSERT INTO t_message_deleted(f_userid, f_msgid) VALUES(%lld, %lld)",
               (long long)userid, (long long)msgid);
    REQUIRE(pConn->execute(sql));
}

//数一数 t_message_pin 里现在有几行，用于"重复 Pin 不产生第二行"这类断言
static int64_t countPinRows()
{
    std::unique_ptr<IDatabase> pConn(DbFactory::createConnection());
    if (!pConn)
        return -1;

    QueryResult* pResult = pConn->query("SELECT COUNT(*) FROM t_message_pin");
    if (pResult == NULL)
        return -1;

    int64_t count = -1;
    Field* pRow = pResult->fetch();
    if (pRow != NULL)
    {
        count = static_cast<int64_t>(pRow[0].getUInt64());
    }
    pResult->endQuery();
    delete pResult;
    return count;
}

// ---------------------------------------------------------------------------
// MPM1-MPM2  Pin / 取消 Pin 的幂等性
// ---------------------------------------------------------------------------

TEST_CASE("MPM1 Pin 后回读得到该行；重复 Pin 幂等且不刷新 operator 与 pintime")
{
    resetPinDb();
    MessagePinManager& mgr = Singleton<MessagePinManager>::Instance();

    insertMessage(1001, conversation_group, 5001, 900001, 1700000000000LL);

    REQUIRE(mgr.pinMessage(1001, 5001, 1700000001000LL));

    PinRecord rec;
    REQUIRE(mgr.getPinRecord(1001, rec));
    CHECK(rec.msgid == 1001);
    CHECK(rec.operatorId == 5001);
    CHECK(rec.pinTime == 1700000001000LL);
    CHECK(countPinRows() == 1);

    //换个人、换个时间再钉一次：仍然成功，但既不产生第二行，也不把这条的归属
    //改到后来这个人头上 —— 否则取消 Pin 的权限会悄悄易主
    REQUIRE(mgr.pinMessage(1001, 5002, 1700000009000LL));
    CHECK(countPinRows() == 1);

    PinRecord again;
    REQUIRE(mgr.getPinRecord(1001, again));
    CHECK(again.operatorId == 5001);
    CHECK(again.pinTime == 1700000001000LL);
}

TEST_CASE("MPM2 取消 Pin 后回读为空；取消一条没 Pin 的消息不算失败")
{
    resetPinDb();
    MessagePinManager& mgr = Singleton<MessagePinManager>::Instance();

    insertMessage(1002, conversation_group, 5001, 900001, 1700000000000LL);
    REQUIRE(mgr.pinMessage(1002, 5001, 1700000001000LL));

    REQUIRE(mgr.unpinMessage(1002));
    PinRecord rec;
    CHECK_FALSE(mgr.getPinRecord(1002, rec));
    CHECK(countPinRows() == 0);

    //再取消一次、以及取消一条压根没钉过的：都算成功，客户端连点两下不该报错
    CHECK(mgr.unpinMessage(1002));
    CHECK(mgr.unpinMessage(1003));

    //msgid 非正是参数错误，与"没钉住"要区分开
    CHECK_FALSE(mgr.pinMessage(0, 5001, 1700000001000LL));
    CHECK_FALSE(mgr.unpinMessage(-5));
}

// ---------------------------------------------------------------------------
// MPM3-MPM5  按会话列举被 Pin 的消息
// ---------------------------------------------------------------------------

TEST_CASE("MPM3 群聊：只返回本群的，且排除已撤回与本端已删除的")
{
    resetPinDb();
    MessagePinManager& mgr = Singleton<MessagePinManager>::Instance();

    const int64_t groupA = 900001;
    const int64_t groupB = 900002;
    const int64_t self   = 5001;

    insertMessage(2001, conversation_group, 5001, groupA, 1700000001000LL);
    insertMessage(2002, conversation_group, 5002, groupA, 1700000002000LL);
    insertMessage(2003, conversation_group, 5002, groupA, 1700000003000LL, true);  //已撤回
    insertMessage(2004, conversation_group, 5002, groupA, 1700000004000LL);        //本端已删除
    insertMessage(2005, conversation_group, 5003, groupB, 1700000005000LL);        //别的群

    insertDeletedMark(self, 2004);

    REQUIRE(mgr.pinMessage(2001, 5001, 1700000010000LL));
    REQUIRE(mgr.pinMessage(2002, 5002, 1700000011000LL));
    REQUIRE(mgr.pinMessage(2003, 5002, 1700000012000LL));
    REQUIRE(mgr.pinMessage(2004, 5002, 1700000013000LL));
    REQUIRE(mgr.pinMessage(2005, 5003, 1700000014000LL));

    std::vector<MessageRecord> out;
    std::vector<PinRecord> pins;
    int64_t total = 0;
    REQUIRE(mgr.getPinnedMessages(conversation_group, groupA, self,
                                  PIN_FETCH_LIMIT, out, pins, total));

    //只剩 2001 与 2002；按消息时间倒序，2002 在前
    REQUIRE(out.size() == 2);
    CHECK(out[0].msgid == 2002);
    CHECK(out[1].msgid == 2001);
    CHECK(total == 2);
}

TEST_CASE("MPM4 单聊：我发的与对方发的都能取到")
{
    resetPinDb();
    MessagePinManager& mgr = Singleton<MessagePinManager>::Instance();

    const int64_t self = 5001;
    const int64_t peer = 5002;

    insertMessage(3001, conversation_single, self, peer, 1700000001000LL);  //我发给对方
    insertMessage(3002, conversation_single, peer, self, 1700000002000LL);  //对方发给我

    REQUIRE(mgr.pinMessage(3001, self, 1700000010000LL));
    REQUIRE(mgr.pinMessage(3002, peer, 1700000011000LL));

    std::vector<MessageRecord> out;
    std::vector<PinRecord> pins;
    int64_t total = 0;
    REQUIRE(mgr.getPinnedMessages(conversation_single, peer, self,
                                  PIN_FETCH_LIMIT, out, pins, total));

    REQUIRE(out.size() == 2);
    CHECK(out[0].msgid == 3002);
    CHECK(out[1].msgid == 3001);
    CHECK(total == 2);
}

TEST_CASE("MPM5 单聊：不串到第三个人的会话里")
{
    resetPinDb();
    MessagePinManager& mgr = Singleton<MessagePinManager>::Instance();

    const int64_t self   = 5001;
    const int64_t peer   = 5002;
    const int64_t others = 5003;

    insertMessage(3101, conversation_single, self, peer,   1700000001000LL);
    insertMessage(3102, conversation_single, self, others, 1700000002000LL);  //我与第三人
    insertMessage(3103, conversation_single, others, peer, 1700000003000LL);  //与我无关的两人

    REQUIRE(mgr.pinMessage(3101, self, 1700000010000LL));
    REQUIRE(mgr.pinMessage(3102, self, 1700000011000LL));
    REQUIRE(mgr.pinMessage(3103, others, 1700000012000LL));

    std::vector<MessageRecord> out;
    std::vector<PinRecord> pins;
    int64_t total = 0;
    REQUIRE(mgr.getPinnedMessages(conversation_single, peer, self,
                                  PIN_FETCH_LIMIT, out, pins, total));

    REQUIRE(out.size() == 1);
    CHECK(out[0].msgid == 3101);
    CHECK(total == 1);
}

// ---------------------------------------------------------------------------
// MPM6-MPM7  单次返回条数的截断与 total 的真实性
// ---------------------------------------------------------------------------

TEST_CASE("MPM6 超过 limit 时只返回最近 limit 条，total 仍是真实总数")
{
    resetPinDb();
    MessagePinManager& mgr = Singleton<MessagePinManager>::Instance();

    const int64_t group = 900010;
    const int64_t self  = 5001;

    //钉 5 条，时间依次递增；随后只取 2 条
    for (int i = 0; i < 5; ++i)
    {
        const int64_t msgid = 4001 + i;
        insertMessage(msgid, conversation_group, self, group,
                      1700000001000LL + i * 1000LL);
        REQUIRE(mgr.pinMessage(msgid, self, 1700000010000LL + i * 1000LL));
    }

    std::vector<MessageRecord> out;
    std::vector<PinRecord> pins;
    int64_t total = 0;
    REQUIRE(mgr.getPinnedMessages(conversation_group, group, self, 2, out, pins, total));

    //返回的是"最近的两条"，即时间最大的 4005 与 4004
    REQUIRE(out.size() == 2);
    CHECK(out[0].msgid == 4005);
    CHECK(out[1].msgid == 4004);

    //total 不受 limit 约束 —— 客户端就是靠它判断发生了截断，进而决定本地对账
    //的时间下界；这里若跟着返回 2，本地那些更老的 Pin 会被误清
    CHECK(total == 5);
    CHECK(total > static_cast<int64_t>(out.size()));

    //Pin 元信息与消息一一对应：条数相同、msgid 对得上，operator 与 pintime 是
    //当初钉的那一份。应答里的 pin 子对象直接取自这里，错位了客户端就会把甲钉的
    //消息显示成乙钉的
    REQUIRE(pins.size() == out.size());
    CHECK(pins[0].msgid == 4005);
    CHECK(pins[0].operatorId == self);
    CHECK(pins[0].pinTime == 1700000014000LL);
    CHECK(pins[1].msgid == 4004);
    CHECK(pins[1].pinTime == 1700000013000LL);
}

TEST_CASE("MPM7 未超 limit 时 total 恰等于返回条数")
{
    resetPinDb();
    MessagePinManager& mgr = Singleton<MessagePinManager>::Instance();

    const int64_t group = 900011;
    const int64_t self  = 5001;

    insertMessage(4101, conversation_group, self, group, 1700000001000LL);
    insertMessage(4102, conversation_group, self, group, 1700000002000LL);
    REQUIRE(mgr.pinMessage(4101, self, 1700000010000LL));
    REQUIRE(mgr.pinMessage(4102, self, 1700000011000LL));

    std::vector<MessageRecord> out;
    std::vector<PinRecord> pins;
    int64_t total = 0;
    REQUIRE(mgr.getPinnedMessages(conversation_group, group, self,
                                  PIN_FETCH_LIMIT, out, pins, total));

    CHECK(out.size() == 2);
    CHECK(total == static_cast<int64_t>(out.size()));

    //一条都没钉的会话：查询成功，结果为空，total 为 0
    std::vector<MessageRecord> empty;
    std::vector<PinRecord> emptyPins;
    int64_t emptyTotal = -1;
    REQUIRE(mgr.getPinnedMessages(conversation_group, 900012, self,
                                  PIN_FETCH_LIMIT, empty, emptyPins, emptyTotal));
    CHECK(empty.empty());
    CHECK(emptyPins.empty());
    CHECK(emptyTotal == 0);
}

// ---------------------------------------------------------------------------
// MPM8-MPM9  两处清理挂载点
// ---------------------------------------------------------------------------

TEST_CASE("MPM8 removePinForMessage 只清该条，不误伤同会话其它 Pin")
{
    resetPinDb();
    MessagePinManager& mgr = Singleton<MessagePinManager>::Instance();

    const int64_t group = 900020;
    const int64_t self  = 5001;

    insertMessage(5001, conversation_group, self, group, 1700000001000LL);
    insertMessage(5002, conversation_group, self, group, 1700000002000LL);
    REQUIRE(mgr.pinMessage(5001, self, 1700000010000LL));
    REQUIRE(mgr.pinMessage(5002, self, 1700000011000LL));

    REQUIRE(mgr.removePinForMessage(5001));

    PinRecord rec;
    CHECK_FALSE(mgr.getPinRecord(5001, rec));
    CHECK(mgr.getPinRecord(5002, rec));
    CHECK(countPinRows() == 1);

    //清一条本来就没钉住的：算成功，不是错误
    CHECK(mgr.removePinForMessage(5003));
}

TEST_CASE("MPM9 removePinsForGroup 清掉本群全部 Pin，不误伤其它群与单聊")
{
    resetPinDb();
    MessagePinManager& mgr = Singleton<MessagePinManager>::Instance();

    const int64_t groupA = 900030;
    const int64_t groupB = 900031;
    const int64_t self   = 5001;
    const int64_t peer   = 5002;

    insertMessage(6001, conversation_group, self, groupA, 1700000001000LL);
    insertMessage(6002, conversation_group, self, groupA, 1700000002000LL);
    insertMessage(6003, conversation_group, self, groupB, 1700000003000LL);
    insertMessage(6004, conversation_single, self, peer,  1700000004000LL);

    REQUIRE(mgr.pinMessage(6001, self, 1700000010000LL));
    REQUIRE(mgr.pinMessage(6002, self, 1700000011000LL));
    REQUIRE(mgr.pinMessage(6003, self, 1700000012000LL));
    REQUIRE(mgr.pinMessage(6004, self, 1700000013000LL));

    REQUIRE(mgr.removePinsForGroup(groupA));

    PinRecord rec;
    CHECK_FALSE(mgr.getPinRecord(6001, rec));
    CHECK_FALSE(mgr.getPinRecord(6002, rec));
    CHECK(mgr.getPinRecord(6003, rec));
    CHECK(mgr.getPinRecord(6004, rec));
    CHECK(countPinRows() == 2);
}
