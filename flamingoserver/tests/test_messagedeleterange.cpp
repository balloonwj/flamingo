/**
 *  按时间范围清理聊天记录的单元测试（Suite MDR），test_messagedeleterange.cpp
 *
 *  覆盖 docs/清理聊天记录方案.md §14.1 的 1c 部分：
 *    - MDR1~MDR3：区间的校验与截断、区间合并（纯函数，见 chatserversrc/MessageDeleteRange.h）；
 *    - MDR4~MDR11：区间写入 t_message_deleted_range 之后，同步 / 搜索 / 会话列表 / Pin 列表
 *      都按区间过滤，且对方与其他群成员不受影响（真的打开临时库跑 SQL，默认 SQLite，
 *      设 FLAMINGO_TEST_MYSQL=1 时跑 MySQL，见 test_dbbackend.h）；
 *    - MDB1：规模基准（挂 doctest::skip()，默认不跑）。
 *    - CLR0~CLR6：按会话本人视角清空聊天记录（cmd_clear_conversation）：锚点写入
 *      t_conversation.f_cleared_msgid 之后，同步 / 搜索 / 会话列表 / Pin 列表 / 转发取源都排除
 *      锚点之前的消息，对方与其他群成员不受影响，锚点只前进不后退。
 *
 *  典型用法：Tests.exe --test-case="MDR*"
 *
 *  balloonwj@qq.com   2026-09-30
 */
#include <stdint.h>
#include <stdio.h>
#include <chrono>     //MDB1 计时
#include <memory>
#include <sstream>    //MDB1 拼接逐条写入的 SQL
#include <string>
#include <vector>

#include "doctest.h"
#include "test_dbbackend.h"
#include "../chatserversrc/MessageDeleteRange.h"
#include "../chatserversrc/MessageManager.h"
#include "../chatserversrc/MessagePinManager.h"   //MDR8 / CLR5：Pin 列表同样按区间与清空锚点过滤
#include "../chatserversrc/MessageProtocol.h"     //CLR3：SetConversationRequest
#include "../chatserversrc/Protocol.h"      //conversation_single / conversation_group
#include "../dbapi/IDatabase.h"
#include "../dbapi/DbFactory.h"
#include "../base/Singleton.h"

using msgdelete::DeleteRange;

// =====================================================================
// MDR1~MDR3 —— 纯函数
// =====================================================================

namespace {

//MDR1/MDR2 使用的"服务端当前时刻"：某一秒过了 700 毫秒。取非整秒，才能区分
//"截断到当前毫秒"与"截断到当前这一秒开始前 1 毫秒"两种写法。
const int64_t kNowMs = 1754300000700LL;

//kNowMs 所在这一秒的开始时刻，以及截断后允许的最晚终点（这一秒开始前 1 毫秒）。
const int64_t kNowSecondStartMs = 1754300000000LL;
const int64_t kLatestEndMs = kNowSecondStartMs - 1;

//一分钟与五分钟的毫秒数，用来构造"比服务端当前时刻早 / 晚一段"的时间点。
const int64_t kOneMinuteMs = 60LL * 1000;
const int64_t kFiveMinutesMs = 5 * kOneMinuteMs;

//按"开始、结束"逐段比对两组区间；不相等时把实际结果打进失败信息，便于定位。
void CheckRanges(const std::vector<DeleteRange>& actual,
                 const std::vector<DeleteRange>& expected)
{
    REQUIRE(actual.size() == expected.size());
    for (size_t i = 0; i < expected.size(); ++i)
    {
        INFO("range #" << i << ": [" << actual[i].begintime << ", " << actual[i].endtime << "]");
        CHECK(actual[i].begintime == expected[i].begintime);
        CHECK(actual[i].endtime == expected[i].endtime);
    }
}

} // anonymous namespace

TEST_CASE("MDR1 终点晚于服务端当前时刻时截断到当前这一秒开始前 1 毫秒，早于时原样保留")
{
    DeleteRange out;

    //客户端时钟快了 5 分钟：终点落在服务端的"未来"，截断
    REQUIRE(msgdelete::NormalizeDeleteRange(0, kNowMs + kFiveMinutesMs, kNowMs, out));
    CHECK(out.begintime == 0);
    CHECK(out.endtime == kLatestEndMs);

    //终点恰好等于服务端当前毫秒：同一秒内稍后到达的消息时间会被取整到这一秒开头，
    //只截断到当前毫秒会把它们算进区间，所以仍要截断到这一秒开始之前
    REQUIRE(msgdelete::NormalizeDeleteRange(0, kNowMs, kNowMs, out));
    CHECK(out.endtime == kLatestEndMs);

    //终点恰好等于允许的最晚终点：不动
    REQUIRE(msgdelete::NormalizeDeleteRange(0, kLatestEndMs, kNowMs, out));
    CHECK(out.endtime == kLatestEndMs);

    //终点早于当前时刻：开始、结束都原样保留
    const int64_t begin = kNowMs - 2 * kFiveMinutesMs;
    const int64_t end = kNowMs - kFiveMinutesMs;
    REQUIRE(msgdelete::NormalizeDeleteRange(begin, end, kNowMs, out));
    CHECK(out.begintime == begin);
    CHECK(out.endtime == end);

    //服务端当前时刻恰好在整秒上：同样截断到这一秒开始前 1 毫秒
    REQUIRE(msgdelete::NormalizeDeleteRange(0, kNowSecondStartMs + kOneMinuteMs,
                                            kNowSecondStartMs, out));
    CHECK(out.endtime == kLatestEndMs);
}

TEST_CASE("MDR2 开始或结束为负、截断后结束早于开始时判为非法")
{
    DeleteRange out;
    CHECK_FALSE(msgdelete::NormalizeDeleteRange(-1, kLatestEndMs, kNowMs, out));
    CHECK_FALSE(msgdelete::NormalizeDeleteRange(0, -1, kNowMs, out));

    //本身就是结束早于开始
    CHECK_FALSE(msgdelete::NormalizeDeleteRange(kNowMs - kOneMinuteMs,
                                                kNowMs - kFiveMinutesMs, kNowMs, out));

    //开始落在当前这一秒之内：截断后结束早于开始
    CHECK_FALSE(msgdelete::NormalizeDeleteRange(kNowMs, kNowMs + kOneMinuteMs, kNowMs, out));

    //开始等于结束（只清理某一毫秒）是合法的
    REQUIRE(msgdelete::NormalizeDeleteRange(kLatestEndMs, kLatestEndMs, kNowMs, out));
    CHECK(out.begintime == kLatestEndMs);
    CHECK(out.endtime == kLatestEndMs);
}

TEST_CASE("MDR3 区间合并：不相交保留、重叠合并、首尾相接合并、盖住多段合并为一段")
{
    std::vector<DeleteRange> none;

    //原先没有区间：结果就是新区间
    {
        std::vector<DeleteRange> expected;
        expected.push_back(DeleteRange(100, 200));
        CheckRanges(msgdelete::MergeDeleteRanges(none, DeleteRange(100, 200)), expected);
    }

    //不相交：各自保留，按开始时间升序（新区间在前面也要排好）
    {
        std::vector<DeleteRange> existing;
        existing.push_back(DeleteRange(300, 400));
        std::vector<DeleteRange> expected;
        expected.push_back(DeleteRange(100, 200));
        expected.push_back(DeleteRange(300, 400));
        CheckRanges(msgdelete::MergeDeleteRanges(existing, DeleteRange(100, 200)), expected);
    }

    //重叠：合并成一段
    {
        std::vector<DeleteRange> existing;
        existing.push_back(DeleteRange(100, 200));
        std::vector<DeleteRange> expected;
        expected.push_back(DeleteRange(100, 300));
        CheckRanges(msgdelete::MergeDeleteRanges(existing, DeleteRange(150, 300)), expected);
    }

    //首尾相接（200 之后紧接 201）：两端都包含，中间没有漏掉的毫秒，合并
    {
        std::vector<DeleteRange> existing;
        existing.push_back(DeleteRange(100, 200));
        std::vector<DeleteRange> expected;
        expected.push_back(DeleteRange(100, 300));
        CheckRanges(msgdelete::MergeDeleteRanges(existing, DeleteRange(201, 300)), expected);
    }

    //中间差 1 毫秒（201 这一毫秒不在任何区间里）：不合并
    {
        std::vector<DeleteRange> existing;
        existing.push_back(DeleteRange(100, 200));
        std::vector<DeleteRange> expected;
        expected.push_back(DeleteRange(100, 200));
        expected.push_back(DeleteRange(202, 300));
        CheckRanges(msgdelete::MergeDeleteRanges(existing, DeleteRange(202, 300)), expected);
    }

    //新区间盖住多段：合并为一段
    {
        std::vector<DeleteRange> existing;
        existing.push_back(DeleteRange(100, 200));
        existing.push_back(DeleteRange(300, 400));
        existing.push_back(DeleteRange(500, 600));
        std::vector<DeleteRange> expected;
        expected.push_back(DeleteRange(100, 600));
        CheckRanges(msgdelete::MergeDeleteRanges(existing, DeleteRange(150, 550)), expected);
    }

    //新区间落在已有区间内部：结果不变
    {
        std::vector<DeleteRange> existing;
        existing.push_back(DeleteRange(100, 600));
        std::vector<DeleteRange> expected;
        expected.push_back(DeleteRange(100, 600));
        CheckRanges(msgdelete::MergeDeleteRanges(existing, DeleteRange(200, 300)), expected);
    }

    //已有区间顺序是乱的、彼此还有重叠（库里的脏数据）：照样整理成有序、互不相交
    {
        std::vector<DeleteRange> existing;
        existing.push_back(DeleteRange(1000, 1100));
        existing.push_back(DeleteRange(200, 400));
        existing.push_back(DeleteRange(100, 300));
        std::vector<DeleteRange> expected;
        expected.push_back(DeleteRange(100, 400));
        expected.push_back(DeleteRange(700, 800));
        expected.push_back(DeleteRange(1000, 1100));
        CheckRanges(msgdelete::MergeDeleteRanges(existing, DeleteRange(700, 800)), expected);
    }
}

// =====================================================================
// MDR4~MDR11 —— 写入 t_message_deleted_range 与读取过滤（真的打开临时库跑 SQL）
// =====================================================================

namespace {

//本套数据库用例专用的临时库文件与 MySQL 测试库名，与别的套分开，避免互相干扰。
//MySQL 模式下该库会被整个 DROP 重建，故用测试专用名字。
const char* const kMdrDbPath = "flamingo_sqlite_msgdelrange_test.db";
const char* const kMdrMysqlDb = "flamingo_test_mdr";

//三个用户：A 是执行清理的人，B 是 A 的单聊对象，C 是与 A 同群的另一名成员。
const int32_t kUserA = 100001;
const int32_t kUserB = 100002;
const int32_t kUserC = 100003;

//重建一个干净的测试库。按环境变量选后端（见 test_dbbackend.h）：默认 SQLite，
//设了 FLAMINGO_TEST_MYSQL=1 则跑 MySQL。
void ResetMdrDb()
{
    testdb::resetDb(kMdrDbPath, kMdrMysqlDb);
}

//收尾清理：SQLite 删临时库文件，MySQL 留着库便于事后排查。
void CleanupMdrDb()
{
    testdb::cleanupDb(kMdrDbPath);
}

//读出某用户当前的全部清理区间。
std::vector<DeleteRange> RangesOf(int32_t userid)
{
    std::vector<DeleteRange> out;
    REQUIRE(Singleton<MessageManager>::Instance().getDeletedRanges(userid, out));
    return out;
}

//以 A 的身份清理一段区间（写入时间取固定值，保证结果可复现）。
const int64_t kWriteTimeMs = 1754400000000LL;
void AddRangeFor(int32_t userid, int64_t beginMs, int64_t endMs)
{
    REQUIRE(Singleton<MessageManager>::Instance().addDeletedRange(
        userid, DeleteRange(beginMs, endMs), kWriteTimeMs));
}

} // anonymous namespace

TEST_CASE("MDR4 同一用户多次清理只留合并后的区间，不同用户互不影响")
{
    ResetMdrDb();

    //从没清理过：没有任何区间
    CHECK(RangesOf(kUserA).empty());

    //A 先后清理三次，第三次把前两段连成一段
    AddRangeFor(kUserA, 100, 200);
    AddRangeFor(kUserA, 300, 400);
    {
        std::vector<DeleteRange> expected;
        expected.push_back(DeleteRange(100, 200));
        expected.push_back(DeleteRange(300, 400));
        CheckRanges(RangesOf(kUserA), expected);
    }
    AddRangeFor(kUserA, 150, 350);
    {
        std::vector<DeleteRange> expected;
        expected.push_back(DeleteRange(100, 400));
        CheckRanges(RangesOf(kUserA), expected);
    }

    //B 清理的区间只属于 B，A 的区间不变
    AddRangeFor(kUserB, 1000, 2000);
    {
        std::vector<DeleteRange> expectedA;
        expectedA.push_back(DeleteRange(100, 400));
        CheckRanges(RangesOf(kUserA), expectedA);

        std::vector<DeleteRange> expectedB;
        expectedB.push_back(DeleteRange(1000, 2000));
        CheckRanges(RangesOf(kUserB), expectedB);
    }

    //C 没有清理过
    CHECK(RangesOf(kUserC).empty());

    CleanupMdrDb();
}

namespace {

//MDR5~MDR11 使用的另一名单聊对象，以及两个群。A 与 C 都在群里。
const int32_t kUserD = 100004;
const int64_t kGroupG = 900001;
const int64_t kGroupG2 = 900002;

//消息时间轴：以 kT0 为起点每隔一秒一条。固定取值，保证结果可复现。
const int64_t kT0 = 1700000000000LL;
const int64_t kT1 = kT0 + 1000;
const int64_t kT2 = kT0 + 2000;
const int64_t kT3 = kT0 + 3000;
const int64_t kT4 = kT0 + 4000;

//一次同步 / 搜索最多取的条数，远大于本套用例里任何一个会话的消息数
const int kFetchLimit = 100;

/**
 *  插一条消息。f_content 的格式须与 MessageProtocol::buildStoredContent 打包出的一致
 *  （content 必须是对象），否则读回时解不出内容。
 *    msgid：消息ID；ctype：会话类型；from：发送方；to：单聊为对端、群聊为群ID；
 *    createTime：消息时间（Unix毫秒）；text：正文，搜索用例靠它命中关键字。
 */
void InsertMsg(int64_t msgid, int ctype, int64_t from, int64_t to,
               int64_t createTime, const char* text)
{
    std::unique_ptr<IDatabase> pConn(DbFactory::createConnection());
    REQUIRE(pConn);
    char sql[512] = { 0 };
    ::snprintf(sql, sizeof(sql),
               "INSERT INTO t_chatmsg(f_msgid, f_clientmsgid, f_ctype, f_from, f_to, "
               "f_msgtype, f_content, f_recalled, f_create_time) VALUES(%lld, 'c%lld', %d, "
               "%lld, %lld, 1, '{\"content\":{\"text\":\"%s\"},\"mentions\":[],"
               "\"mentionall\":false}', 0, %lld)",
               (long long)msgid, (long long)msgid, ctype, (long long)from, (long long)to,
               text, (long long)createTime);
    REQUIRE(pConn->execute(sql));
}

//以 userid 的身份同步某会话的全部消息，返回 msgid 列表（升序）。
std::vector<int64_t> SyncIds(int32_t userid, int ctype, int64_t cid)
{
    SyncMessagesRequest req;
    req.ctype = ctype;
    req.cid = cid;
    req.beforemsgid = 0;
    req.limit = kFetchLimit;
    std::vector<MessageRecord> records;
    bool hasMore = false;
    REQUIRE(Singleton<MessageManager>::Instance().getConversationMessages(
        userid, req, records, hasMore));

    std::vector<int64_t> ids;
    for (size_t i = 0; i < records.size(); ++i)
        ids.push_back(records[i].msgid);
    return ids;
}

//以 userid 的身份在某会话里按关键字搜索，返回命中的 msgid 列表（升序）。
std::vector<int64_t> SearchIds(int32_t userid, int ctype, int64_t cid, const char* keyword)
{
    SearchMessagesRequest req;
    req.ctype = ctype;
    req.cid = cid;
    req.keyword = keyword;
    req.beforemsgid = 0;
    req.limit = kFetchLimit;
    std::vector<MessageRecord> records;
    bool hasMore = false;
    REQUIRE(Singleton<MessageManager>::Instance().searchMessages(userid, req, records, hasMore));

    std::vector<int64_t> ids;
    for (size_t i = 0; i < records.size(); ++i)
        ids.push_back(records[i].msgid);
    return ids;
}

//由若干 msgid 拼出期望列表，便于写成一行断言。count 为个数，ids 为首元素指针。
std::vector<int64_t> Ids(const int64_t* ids, size_t count)
{
    return std::vector<int64_t>(ids, ids + count);
}

//在会话摘要列表里按 cid 找一条；找不到返回 NULL。
const ConversationBrief* FindBrief(const std::vector<ConversationBrief>& list, int64_t cid)
{
    for (size_t i = 0; i < list.size(); ++i)
    {
        if (list[i].cid == cid)
            return &list[i];
    }
    return NULL;
}

//A 的单聊会话摘要
std::vector<ConversationBrief> SingleBriefsOfA()
{
    std::vector<ConversationBrief> out;
    REQUIRE(Singleton<MessageManager>::Instance().getConversationBriefs(kUserA, out));
    return out;
}

//A 在两个群里的会话摘要
std::vector<ConversationBrief> GroupBriefsOfA()
{
    std::vector<int64_t> groupids;
    groupids.push_back(kGroupG);
    groupids.push_back(kGroupG2);
    std::vector<ConversationBrief> out;
    REQUIRE(Singleton<MessageManager>::Instance().getGroupConversationBriefs(kUserA, groupids, out));
    return out;
}

//MDR5 / MDR9 共用的数据：A 与 B 的单聊四条，群 G 里三条（A、C 各有发言）。
void InsertSingleAndGroupMessages()
{
    InsertMsg(1, conversation_single, kUserA, kUserB, kT1, "s1");
    InsertMsg(2, conversation_single, kUserB, kUserA, kT2, "s2");
    InsertMsg(3, conversation_single, kUserA, kUserB, kT3, "s3");
    InsertMsg(4, conversation_single, kUserB, kUserA, kT4, "s4");
    InsertMsg(11, conversation_group, kUserC, kGroupG, kT1, "g1");
    InsertMsg(12, conversation_group, kUserA, kGroupG, kT2, "g2");
    InsertMsg(13, conversation_group, kUserC, kGroupG, kT3, "g3");
}

} // anonymous namespace

TEST_CASE("MDR5 同步：区间内的消息不再返回，区间外照常；落在区间两端的也算在内")
{
    ResetMdrDb();
    InsertSingleAndGroupMessages();

    //清理之前全都在
    {
        const int64_t single[] = { 1, 2, 3, 4 };
        const int64_t group[] = { 11, 12, 13 };
        CHECK(SyncIds(kUserA, conversation_single, kUserB) == Ids(single, 4));
        CHECK(SyncIds(kUserA, conversation_group, kGroupG) == Ids(group, 3));
    }

    //区间恰好是 [T2, T3]：时间等于两端的 2、3、12、13 都要被过滤
    AddRangeFor(kUserA, kT2, kT3);
    {
        const int64_t single[] = { 1, 4 };
        const int64_t group[] = { 11 };
        CHECK(SyncIds(kUserA, conversation_single, kUserB) == Ids(single, 2));
        CHECK(SyncIds(kUserA, conversation_group, kGroupG) == Ids(group, 1));
    }

    CleanupMdrDb();
}

TEST_CASE("MDR6 搜索：区间内的消息不再出现在搜索结果里")
{
    ResetMdrDb();
    InsertMsg(1, conversation_single, kUserA, kUserB, kT1, "plan-a");
    InsertMsg(2, conversation_single, kUserB, kUserA, kT2, "plan-b");
    InsertMsg(3, conversation_single, kUserA, kUserB, kT3, "plan-c");
    InsertMsg(4, conversation_single, kUserB, kUserA, kT4, "plan-d");

    {
        const int64_t all[] = { 1, 2, 3, 4 };
        CHECK(SearchIds(kUserA, conversation_single, kUserB, "plan") == Ids(all, 4));
    }

    AddRangeFor(kUserA, kT2, kT3);
    {
        const int64_t kept[] = { 1, 4 };
        CHECK(SearchIds(kUserA, conversation_single, kUserB, "plan") == Ids(kept, 2));
    }

    CleanupMdrDb();
}

TEST_CASE("MDR7 会话列表：最后一条与未读都跳过区间内的消息，全在区间内的会话不再出现")
{
    ResetMdrDb();
    //与 B 的单聊三条、与 D 的单聊一条，全是对方发来的（都算未读）
    InsertMsg(1, conversation_single, kUserB, kUserA, kT1, "b1");
    InsertMsg(2, conversation_single, kUserB, kUserA, kT2, "b2");
    InsertMsg(3, conversation_single, kUserB, kUserA, kT3, "b3");
    InsertMsg(5, conversation_single, kUserD, kUserA, kT2, "d1");
    //群 G 两条、群 G2 一条，都是 C 发的
    InsertMsg(11, conversation_group, kUserC, kGroupG, kT1, "g1");
    InsertMsg(12, conversation_group, kUserC, kGroupG, kT3, "g2");
    InsertMsg(21, conversation_group, kUserC, kGroupG2, kT2, "h1");

    //清理之前：四个会话都在，最后一条与未读按全部消息算
    {
        const std::vector<ConversationBrief> singles = SingleBriefsOfA();
        const ConversationBrief* b = FindBrief(singles, kUserB);
        REQUIRE(b != NULL);
        CHECK(b->lastmsgid == 3);
        CHECK(b->unread == 3);
        CHECK(FindBrief(singles, kUserD) != NULL);

        const std::vector<ConversationBrief> groups = GroupBriefsOfA();
        const ConversationBrief* g = FindBrief(groups, kGroupG);
        REQUIRE(g != NULL);
        CHECK(g->lastmsgid == 12);
        CHECK(g->unread == 2);
        CHECK(FindBrief(groups, kGroupG2) != NULL);
    }

    AddRangeFor(kUserA, kT2, kT3);
    {
        const std::vector<ConversationBrief> singles = SingleBriefsOfA();
        const ConversationBrief* b = FindBrief(singles, kUserB);
        REQUIRE(b != NULL);
        CHECK(b->lastmsgid == 1);
        CHECK(b->unread == 1);
        //与 D 的唯一一条落在区间内：该会话不再出现
        CHECK(FindBrief(singles, kUserD) == NULL);

        const std::vector<ConversationBrief> groups = GroupBriefsOfA();
        const ConversationBrief* g = FindBrief(groups, kGroupG);
        REQUIRE(g != NULL);
        CHECK(g->lastmsgid == 11);
        CHECK(g->unread == 1);
        //群 G2 的唯一一条落在区间内：该群不再出现
        CHECK(FindBrief(groups, kGroupG2) == NULL);
    }

    CleanupMdrDb();
}

TEST_CASE("MDR8 Pin 列表：区间内被钉住的消息不再出现在本人的 Pin 列表里")
{
    ResetMdrDb();
    InsertMsg(11, conversation_group, kUserC, kGroupG, kT1, "g1");
    InsertMsg(12, conversation_group, kUserC, kGroupG, kT2, "g2");
    InsertMsg(13, conversation_group, kUserC, kGroupG, kT3, "g3");

    MessagePinManager& pinMgr = Singleton<MessagePinManager>::Instance();
    REQUIRE(pinMgr.pinMessage(11, kUserC, kT4));
    REQUIRE(pinMgr.pinMessage(12, kUserC, kT4));
    REQUIRE(pinMgr.pinMessage(13, kUserC, kT4));

    //A 只清理 T2 这一毫秒：12 从 A 的 Pin 列表里消失
    AddRangeFor(kUserA, kT2, kT2);

    std::vector<MessageRecord> out;
    std::vector<PinRecord> pins;
    int64_t total = 0;
    REQUIRE(pinMgr.getPinnedMessages(conversation_group, kGroupG, kUserA,
                                     PIN_FETCH_LIMIT, out, pins, total));
    //按消息时间倒序：13 在前
    REQUIRE(out.size() == 2);
    CHECK(out[0].msgid == 13);
    CHECK(out[1].msgid == 11);
    CHECK(total == 2);

    //Pin 是会话内共享的状态：C 仍看到三条
    REQUIRE(pinMgr.getPinnedMessages(conversation_group, kGroupG, kUserC,
                                     PIN_FETCH_LIMIT, out, pins, total));
    CHECK(out.size() == 3);
    CHECK(total == 3);

    CleanupMdrDb();
}

TEST_CASE("MDR9 A 清理后，B 看单聊、C 看群聊与清理前完全一致")
{
    ResetMdrDb();
    InsertSingleAndGroupMessages();

    //A 把全部时间都清理掉
    AddRangeFor(kUserA, kT1, kT4);
    CHECK(SyncIds(kUserA, conversation_single, kUserB).empty());
    CHECK(SyncIds(kUserA, conversation_group, kGroupG).empty());

    //对方与其他群成员不受影响
    const int64_t single[] = { 1, 2, 3, 4 };
    const int64_t group[] = { 11, 12, 13 };
    CHECK(SyncIds(kUserB, conversation_single, kUserA) == Ids(single, 4));
    CHECK(SyncIds(kUserC, conversation_group, kGroupG) == Ids(group, 3));

    CleanupMdrDb();
}

TEST_CASE("MDR10 清理之后才到达的新消息照常可见")
{
    ResetMdrDb();
    InsertMsg(1, conversation_single, kUserB, kUserA, kT1, "old-1");
    InsertMsg(2, conversation_single, kUserB, kUserA, kT2, "old-2");

    AddRangeFor(kUserA, kT1, kT3);
    CHECK(SyncIds(kUserA, conversation_single, kUserB).empty());

    //区间终点之后到达的一条
    InsertMsg(3, conversation_single, kUserB, kUserA, kT4, "new-1");
    const int64_t fresh[] = { 3 };
    CHECK(SyncIds(kUserA, conversation_single, kUserB) == Ids(fresh, 1));

    CleanupMdrDb();
}

TEST_CASE("MDR11 区间清理与逐条删除同时生效，互不干扰")
{
    ResetMdrDb();
    InsertMsg(1, conversation_single, kUserA, kUserB, kT1, "s1");
    InsertMsg(2, conversation_single, kUserB, kUserA, kT2, "s2");
    InsertMsg(3, conversation_single, kUserA, kUserB, kT3, "s3");
    InsertMsg(4, conversation_single, kUserB, kUserA, kT4, "s4");

    //1 在区间外，用逐条删除去掉；2、3 在区间内
    REQUIRE(Singleton<MessageManager>::Instance().deleteMessageForUser(kUserA, 1));
    AddRangeFor(kUserA, kT2, kT3);

    const int64_t kept[] = { 4 };
    CHECK(SyncIds(kUserA, conversation_single, kUserB) == Ids(kept, 1));

    //B 仍然四条都在
    const int64_t all[] = { 1, 2, 3, 4 };
    CHECK(SyncIds(kUserB, conversation_single, kUserA) == Ids(all, 4));

    CleanupMdrDb();
}

// =====================================================================
// MDB1 —— 规模基准（默认不跑）
//
//   对比"逐条写入 t_message_deleted"与"只写一条区间"两种做法：清理本身的耗时，以及清理
//   之后同步一页、拉一次会话列表的耗时。结论用来确认方案 §12 的判断，数字回填到
//   docs/清理聊天记录方案.md §15。
//
//   挂了 doctest::skip()，默认只列名不执行；要跑必须同时给上 --no-skip 与名字过滤：
//       Tests.exe --no-skip --test-case="MDB*"
//   用例只断言"跑完了、条数对得上"，不断言快慢 —— 写死时间阈值必然变成一条时不时误报的用例。
// =====================================================================

namespace {

//造数据的规模：50 个群各 1000 条、100 个单聊各 500 条，合计 10 万条
const int kBenchGroupCount = 50;
const int kBenchMsgsPerGroup = 1000;
const int kBenchPeerCount = 100;
const int kBenchMsgsPerPeer = 500;
const int64_t kBenchTotalMsgs = static_cast<int64_t>(kBenchGroupCount) * kBenchMsgsPerGroup
                              + static_cast<int64_t>(kBenchPeerCount) * kBenchMsgsPerPeer;

//群 id 与单聊对端 userid 的起始值，分开号段
const int64_t kBenchGroupIdBase = 910001;
const int64_t kBenchPeerIdBase = 300001;

//相邻两条消息的时间间隔（毫秒）。按 msgid 递增，与真实数据"msgid 与时间同步递增"一致
const int64_t kBenchMsgIntervalMs = 1000;

//清理较早的 90%：区间终点取第 90000 条消息的时间
const int64_t kBenchClearedMsgs = kBenchTotalMsgs * 9 / 10;
const int64_t kBenchClearEndMs = kT0 + kBenchClearedMsgs * kBenchMsgIntervalMs;

//读取测量时一页的条数，与服务端 sync 的上限一致
const int kBenchPageSize = 100;

//毫秒计时。steady_clock 不受系统时间调整影响
double BenchNowMs()
{
    const std::chrono::steady_clock::time_point t = std::chrono::steady_clock::now();
    return static_cast<double>(
               std::chrono::duration_cast<std::chrono::microseconds>(
                   t.time_since_epoch()).count()) / 1000.0;
}

//第 msgid 条消息的时间
int64_t BenchTimeOf(int64_t msgid)
{
    return kT0 + msgid * kBenchMsgIntervalMs;
}

/**
 *  灌 10 万条消息。群聊与单聊交错写入（每个群、每个单聊的消息在时间上都铺满全程），
 *  这样"清理较早的 90%"会让每个会话都只剩最近的一截，贴近真实用法。
 *  灌数据本身不是被测对象，整批包在一个事务里。
 */
void SeedBenchMessages()
{
    std::unique_ptr<IDatabase> pConn(DbFactory::createConnection());
    REQUIRE(pConn);
    REQUIRE(pConn->execute("BEGIN"));

    //每一轮给每个群各写 kBenchMsgsPerGroup / kBenchMsgsPerPeer 比例的消息：
    //群每轮 2 条、单聊每轮 1 条，共 500 轮
    const int kRounds = kBenchMsgsPerPeer;
    const int kGroupMsgsPerRound = kBenchMsgsPerGroup / kRounds;
    int64_t msgid = 1;
    for (int round = 0; round < kRounds; ++round)
    {
        for (int g = 0; g < kBenchGroupCount; ++g)
        {
            for (int k = 0; k < kGroupMsgsPerRound; ++k)
            {
                char sql[512] = { 0 };
                ::snprintf(sql, sizeof(sql),
                           "INSERT INTO t_chatmsg(f_msgid,f_clientmsgid,f_ctype,f_from,f_to,"
                           "f_msgtype,f_content,f_recalled,f_create_time) VALUES(%lld,'c%lld',%d,"
                           "%lld,%lld,1,'{\"content\":{\"text\":\"bench\"},\"mentions\":[],"
                           "\"mentionall\":false}',0,%lld)",
                           (long long)msgid, (long long)msgid, conversation_group,
                           (long long)(kUserA + 1 + (msgid % 10)),
                           (long long)(kBenchGroupIdBase + g), (long long)BenchTimeOf(msgid));
                pConn->execute(sql);
                ++msgid;
            }
        }
        for (int p = 0; p < kBenchPeerCount; ++p)
        {
            const int64_t peer = kBenchPeerIdBase + p;
            const bool fromMe = ((round % 2) == 0);
            char sql[512] = { 0 };
            ::snprintf(sql, sizeof(sql),
                       "INSERT INTO t_chatmsg(f_msgid,f_clientmsgid,f_ctype,f_from,f_to,"
                       "f_msgtype,f_content,f_recalled,f_create_time) VALUES(%lld,'c%lld',%d,"
                       "%lld,%lld,1,'{\"content\":{\"text\":\"bench\"},\"mentions\":[],"
                       "\"mentionall\":false}',0,%lld)",
                       (long long)msgid, (long long)msgid, conversation_single,
                       (long long)(fromMe ? kUserA : peer), (long long)(fromMe ? peer : kUserA),
                       (long long)BenchTimeOf(msgid));
            pConn->execute(sql);
            ++msgid;
        }
    }
    REQUIRE(pConn->execute("COMMIT"));
    REQUIRE(msgid - 1 == kBenchTotalMsgs);
}

//A 所在的全部群
std::vector<int64_t> BenchGroupIds()
{
    std::vector<int64_t> ids;
    for (int g = 0; g < kBenchGroupCount; ++g)
        ids.push_back(kBenchGroupIdBase + g);
    return ids;
}

//三项读取的耗时（毫秒）
struct BenchReadCost
{
    double m_syncSingleMs;   //同步一个单聊的最新一页
    double m_syncGroupMs;    //同步一个群的最新一页
    double m_briefsMs;       //拉一次会话列表（单聊 + 群聊）
    size_t m_singlePage;     //单聊那一页实际返回的条数
    size_t m_groupPage;      //群那一页实际返回的条数
    size_t m_briefCount;     //会话列表的会话数

    BenchReadCost()
        : m_syncSingleMs(0), m_syncGroupMs(0), m_briefsMs(0)
        , m_singlePage(0), m_groupPage(0), m_briefCount(0)
    {
    }
};

//以 A 的身份测三项读取
BenchReadCost MeasureReads()
{
    MessageManager& mgr = Singleton<MessageManager>::Instance();
    BenchReadCost cost;

    SyncMessagesRequest req;
    req.ctype = conversation_single;
    req.cid = kBenchPeerIdBase;
    req.beforemsgid = 0;
    req.limit = kBenchPageSize;
    std::vector<MessageRecord> records;
    bool hasMore = false;
    double t0 = BenchNowMs();
    REQUIRE(mgr.getConversationMessages(kUserA, req, records, hasMore));
    cost.m_syncSingleMs = BenchNowMs() - t0;
    cost.m_singlePage = records.size();

    req.ctype = conversation_group;
    req.cid = kBenchGroupIdBase;
    t0 = BenchNowMs();
    REQUIRE(mgr.getConversationMessages(kUserA, req, records, hasMore));
    cost.m_syncGroupMs = BenchNowMs() - t0;
    cost.m_groupPage = records.size();

    std::vector<ConversationBrief> singles;
    std::vector<ConversationBrief> groups;
    t0 = BenchNowMs();
    REQUIRE(mgr.getConversationBriefs(kUserA, singles));
    REQUIRE(mgr.getGroupConversationBriefs(kUserA, BenchGroupIds(), groups));
    cost.m_briefsMs = BenchNowMs() - t0;
    cost.m_briefCount = singles.size() + groups.size();
    return cost;
}

//把三项读取耗时打进测试输出
void ReportReads(const char* label, const BenchReadCost& cost)
{
    //doctest 对裸的 const char* 按指针输出，须先转成 std::string
    MESSAGE(std::string(label) << ": sync single page " << cost.m_syncSingleMs << " ms ("
            << cost.m_singlePage << " rows), sync group page " << cost.m_syncGroupMs
            << " ms (" << cost.m_groupPage << " rows), conversation briefs "
            << cost.m_briefsMs << " ms (" << cost.m_briefCount << " conversations)");
}

} // anonymous namespace

TEST_CASE("MDB1 10 万条消息：逐条写入删除记录与只写一条区间的清理耗时与读取耗时对比"
          * doctest::skip())
{
    //一、不清理时的读取耗时，作为参照
    ResetMdrDb();
    SeedBenchMessages();
    ReportReads("no clear", MeasureReads());

    //二、逐条写入：把区间内本人参与的消息逐条写进 t_message_deleted
    {
        std::ostringstream os;
        os << "INSERT IGNORE INTO t_message_deleted(f_userid, f_msgid) SELECT " << kUserA
           << ", f_msgid FROM t_chatmsg WHERE f_create_time BETWEEN 0 AND " << kBenchClearEndMs
           << " AND ((f_ctype = " << conversation_single << " AND (f_from = " << kUserA
           << " OR f_to = " << kUserA << ")) OR (f_ctype = " << conversation_group
           << " AND f_to IN (";
        const std::vector<int64_t> groups = BenchGroupIds();
        for (size_t i = 0; i < groups.size(); ++i)
            os << (i == 0 ? "" : ",") << groups[i];
        os << ")))";

        std::unique_ptr<IDatabase> pConn(DbFactory::createConnection());
        REQUIRE(pConn);
        const double t0 = BenchNowMs();
        REQUIRE(pConn->execute(os.str().c_str()));
        const double costMs = BenchNowMs() - t0;
        MESSAGE("per-message clear: " << costMs << " ms for about " << kBenchClearedMsgs
                << " messages");
    }
    ReportReads("after per-message clear", MeasureReads());

    //三、只写一条区间（本方案的做法），换一个干净的库
    ResetMdrDb();
    SeedBenchMessages();
    {
        const double t0 = BenchNowMs();
        AddRangeFor(kUserA, 0, kBenchClearEndMs);
        const double costMs = BenchNowMs() - t0;
        MESSAGE("range clear: " << costMs << " ms");
    }
    const BenchReadCost afterRange = MeasureReads();
    ReportReads("after range clear", afterRange);

    //每个会话都只剩最近的一截：单聊 500 条里剩最后约 50 条、群 1000 条里剩最后约 100 条
    CHECK(afterRange.m_singlePage > 0);
    CHECK(afterRange.m_groupPage > 0);
    CHECK(afterRange.m_briefCount == static_cast<size_t>(kBenchGroupCount + kBenchPeerCount));

    CleanupMdrDb();
}

// =====================================================================
// CLR 组：按会话本人视角清空聊天记录（cmd_clear_conversation，2026-10-02）
//
// 清空锚点记在本人这一行 t_conversation.f_cleared_msgid：该会话里 msgid 不大于锚点的消息对本人
// 隐藏，对方与其他群成员不受影响。读取时由 visibleToUserClause 里的 BuildNotClearedClause 过滤，
// 与按时间范围清理（MDR 组）同时生效。
// =====================================================================

namespace {

//清空时写入 f_update_time 的时刻（Unix毫秒），取值无特殊含义
const int64_t kClearTimeMs = 1754500000000LL;

//以 userid 的身份清空某会话，返回生效的锚点；失败时中止用例。
int64_t ClearFor(int32_t userid, int ctype, int64_t cid)
{
    int64_t anchor = -1;
    REQUIRE(Singleton<MessageManager>::Instance().clearConversation(userid, ctype, cid,
                                                                     kClearTimeMs, anchor));
    return anchor;
}

} // anonymous namespace

TEST_CASE("CLR0 清空过滤条件：按消息算出本人视角的会话，取该会话的清空锚点比较 msgid")
{
    const std::string clause = msgdelete::BuildNotClearedClause(kUserA, "m.");
    //列名都带上调用方给的前缀，联表时不会有歧义
    CHECK(clause.find("m.f_msgid >") != std::string::npos);
    CHECK(clause.find("t_conversation") != std::string::npos);
    CHECK(clause.find("f_cleared_msgid") != std::string::npos);
    //本人 userid 出现在子查询里
    CHECK(clause.find("100001") != std::string::npos);
    //没有清空过（查不到行）时按 0 处理
    CHECK(clause.find("COALESCE") != std::string::npos);
    //片段自带前置空格与 AND，可以直接拼在 WHERE 条件之后
    CHECK(clause.compare(0, 5, " AND ") == 0);
}

TEST_CASE("CLR1 清空群会话：本人同步、搜索、会话列表都不再带出旧消息，其他成员不受影响，新消息照常")
{
    ResetMdrDb();
    InsertSingleAndGroupMessages();

    //锚点为清空时该群的最大 msgid
    CHECK(ClearFor(kUserA, conversation_group, kGroupG) == 13);

    CHECK(SyncIds(kUserA, conversation_group, kGroupG).empty());
    CHECK(SearchIds(kUserA, conversation_group, kGroupG, "g").empty());
    //群里没有本人可见的消息：不再出现在会话列表里
    CHECK(FindBrief(GroupBriefsOfA(), kGroupG) == NULL);
    //单聊不受影响
    const int64_t single[] = { 1, 2, 3, 4 };
    CHECK(SyncIds(kUserA, conversation_single, kUserB) == Ids(single, 4));
    //其他群成员不受影响
    const int64_t group[] = { 11, 12, 13 };
    CHECK(SyncIds(kUserC, conversation_group, kGroupG) == Ids(group, 3));

    //清空之后到达的新消息照常可见，会话重新出现、未读只数新消息
    InsertMsg(14, conversation_group, kUserC, kGroupG, kT4, "g4");
    const int64_t fresh[] = { 14 };
    CHECK(SyncIds(kUserA, conversation_group, kGroupG) == Ids(fresh, 1));
    //会话列表先存进局部变量再查：FindBrief 返回的是列表元素的指针，临时列表一结束语句就析构了
    const std::vector<ConversationBrief> briefs = GroupBriefsOfA();
    const ConversationBrief* brief = FindBrief(briefs, kGroupG);
    REQUIRE(brief != NULL);
    CHECK(brief->lastmsgid == 14);
    CHECK(brief->unread == 1);

    CleanupMdrDb();
}

TEST_CASE("CLR2 清空单聊：本人看不到、会话从本人列表消失，对方完全不受影响")
{
    ResetMdrDb();
    InsertSingleAndGroupMessages();
    InsertMsg(21, conversation_single, kUserD, kUserA, kT1, "d1");

    CHECK(ClearFor(kUserA, conversation_single, kUserB) == 4);

    CHECK(SyncIds(kUserA, conversation_single, kUserB).empty());
    CHECK(FindBrief(SingleBriefsOfA(), kUserB) == NULL);
    //与 D 的单聊、群 G 不受影响
    CHECK(FindBrief(SingleBriefsOfA(), kUserD) != NULL);
    const int64_t group[] = { 11, 12, 13 };
    CHECK(SyncIds(kUserA, conversation_group, kGroupG) == Ids(group, 3));

    //B 看单聊与清空前一致，B 的会话列表里仍有 A
    const int64_t single[] = { 1, 2, 3, 4 };
    CHECK(SyncIds(kUserB, conversation_single, kUserA) == Ids(single, 4));
    std::vector<ConversationBrief> briefsOfB;
    REQUIRE(Singleton<MessageManager>::Instance().getConversationBriefs(kUserB, briefsOfB));
    CHECK(FindBrief(briefsOfB, kUserA) != NULL);

    CleanupMdrDb();
}

TEST_CASE("CLR3 锚点只前进不后退；清空不改动置顶与消息提醒模式")
{
    ResetMdrDb();
    InsertSingleAndGroupMessages();

    //先把与 B 的单聊设为置顶、接收消息但不提醒
    SetConversationRequest req;
    req.ctype = conversation_single;
    req.cid = kUserB;
    req.has_pinned = true;
    req.pinned = true;
    req.has_notifymode = true;
    req.notifymode = notify_mode_silent;
    req.has_deleted = false;
    req.deleted = false;
    int errorCode = error_code_ok;
    REQUIRE(Singleton<MessageManager>::Instance().setConversation(kUserA, req, kClearTimeMs, errorCode));

    CHECK(ClearFor(kUserA, conversation_single, kUserB) == 4);
    //没有新消息时再清空一次：锚点不变
    CHECK(ClearFor(kUserA, conversation_single, kUserB) == 4);
    //来了新消息再清空：锚点前进
    InsertMsg(5, conversation_single, kUserB, kUserA, kT4, "s5");
    CHECK(ClearFor(kUserA, conversation_single, kUserB) == 5);
    CHECK(SyncIds(kUserA, conversation_single, kUserB).empty());

    bool pinned = false;
    int notifymode = notify_mode_allow;
    bool deleted = true;
    REQUIRE(Singleton<MessageManager>::Instance().getConversationState(
        kUserA, conversation_single, kUserB, pinned, notifymode, deleted));
    CHECK(pinned);
    CHECK(notifymode == notify_mode_silent);
    CHECK_FALSE(deleted);

    CleanupMdrDb();
}

TEST_CASE("CLR4 清空一个没有任何消息的会话：成功，锚点为 0，之后的消息照常可见")
{
    ResetMdrDb();
    CHECK(ClearFor(kUserA, conversation_single, kUserD) == 0);
    InsertMsg(31, conversation_single, kUserD, kUserA, kT1, "first");
    const int64_t fresh[] = { 31 };
    CHECK(SyncIds(kUserA, conversation_single, kUserD) == Ids(fresh, 1));
    CleanupMdrDb();
}

TEST_CASE("CLR5 Pin 列表与按 ID 取消息（转发取源）同样排除被清空的消息")
{
    ResetMdrDb();
    InsertMsg(11, conversation_group, kUserC, kGroupG, kT1, "g1");
    InsertMsg(12, conversation_group, kUserC, kGroupG, kT2, "g2");

    MessagePinManager& pinMgr = Singleton<MessagePinManager>::Instance();
    REQUIRE(pinMgr.pinMessage(11, kUserC, kT3));

    CHECK(ClearFor(kUserA, conversation_group, kGroupG) == 12);
    InsertMsg(13, conversation_group, kUserC, kGroupG, kT3, "g3");

    std::vector<MessageRecord> out;
    std::vector<PinRecord> pins;
    int64_t total = 0;
    REQUIRE(pinMgr.getPinnedMessages(conversation_group, kGroupG, kUserA,
                                     PIN_FETCH_LIMIT, out, pins, total));
    CHECK(out.empty());
    //C 仍看得到被钉住的那条
    REQUIRE(pinMgr.getPinnedMessages(conversation_group, kGroupG, kUserC,
                                     PIN_FETCH_LIMIT, out, pins, total));
    CHECK(out.size() == 1);

    //转发取源：A 取不到被清空的 11、12，取得到清空之后的 13
    std::vector<int64_t> wanted;
    wanted.push_back(11);
    wanted.push_back(12);
    wanted.push_back(13);
    std::vector<MessageRecord> records;
    REQUIRE(Singleton<MessageManager>::Instance().getMessagesByIds(kUserA, wanted, records));
    REQUIRE(records.size() == 1);
    CHECK(records[0].msgid == 13);

    CleanupMdrDb();
}

TEST_CASE("CLR6 按会话清空与按时间范围清理、逐条删除同时生效")
{
    ResetMdrDb();
    InsertMsg(1, conversation_single, kUserA, kUserB, kT1, "s1");
    InsertMsg(2, conversation_single, kUserB, kUserA, kT2, "s2");
    CHECK(ClearFor(kUserA, conversation_single, kUserB) == 2);

    //清空之后的两条：一条被逐条删除，一条落在时间清理区间里，都不可见；最后一条可见
    InsertMsg(3, conversation_single, kUserB, kUserA, kT3, "s3");
    InsertMsg(4, conversation_single, kUserB, kUserA, kT4, "s4");
    InsertMsg(5, conversation_single, kUserB, kUserA, kT4 + 1000, "s5");
    REQUIRE(Singleton<MessageManager>::Instance().deleteMessageForUser(kUserA, 3));
    AddRangeFor(kUserA, kT4, kT4);

    const int64_t kept[] = { 5 };
    CHECK(SyncIds(kUserA, conversation_single, kUserB) == Ids(kept, 1));
    CleanupMdrDb();
}

