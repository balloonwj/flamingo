/**
 *  MsgStoreBatchTests.cpp
 *
 *  本地消息库批量写入（CV2MsgStore::UpsertBatch）的测试（标签 [msgstorebatch]）。
 *
 *  背景：打开聊天窗时，历史应答里的一整页消息原先是逐条 Upsert 进本地库的，每条单独
 *  提交、各自刷盘，50 条在 UI 线程上要花约 300 毫秒（实测），聊天窗要等它写完才开始
 *  渲染。UpsertBatch 把一批消息放进一个事务一次提交。本组用例只验证行为与逐条写入
 *  一致，不断言耗时。
 *
 *  覆盖：
 *    MSB1 批量写入后每条都能按原字段查回
 *    MSB2 批内重复与库内已有的同一条消息都按覆盖处理，不产生重复行
 *    MSB3 空批次直接成功，不改动库
 *    MSB4 库未打开时返回失败
 *    MSB5 msgid 非正的记录被跳过，其余照常写入
 *
 *  balloonwj@qq.com   2026-09-30
 */
#include "catch_amalgamated.hpp"
#include "../UI/BalloonHistory/V2MsgStore.h"
#include <windows.h>
#include <vector>

using namespace balloonhistory;

namespace {

// 测试会话：单聊，对端 userid 取任意正数。
const int     kCtype = 1;
const int64_t kCid   = 5308;

// 批量写入的条数，取首屏历史一页的量级。
const int kBatchSize = 50;

// 查询时的一页条数，取得比写入的条数大，一次就能全部查回。
const int kQueryPageSize = 1000;

// 消息时间的起点（Unix 毫秒，任意正值）与相邻两条的间隔（毫秒）。
const int64_t kBaseTimeMs     = 1790000000000LL;
const int64_t kTimeIntervalMs = 60000;

// 取临时目录下的 db 全路径并删旧文件，保证每个用例从空库开始。文件名带进程 id，
// 原因同 HistoryModelTests.cpp 的 FreshTempDb：并发跑的两个测试进程不能共用一个库。
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

// 造第 i 条测试消息（i 从 1 起）。各字段都随 i 变化，查回时逐字段比对才有意义。
HistoryRecord MakeRecord(int i)
{
    HistoryRecord rec;
    rec.m_msgid       = 1000 + i;
    rec.m_clientmsgid = "CLIENT-" + std::to_string(i);
    rec.m_ctype       = kCtype;
    rec.m_cid         = kCid;
    rec.m_fromid      = (i % 2 == 0) ? 5127 : kCid;
    rec.m_msgtype     = 1;
    rec.m_content     = "{\"content\":{\"text\":\"message " + std::to_string(i) + "\"}}";
    rec.m_searchText  = "message " + std::to_string(i);
    rec.m_isLink      = (i % 5 == 0);
    rec.m_timestamp   = kBaseTimeMs + kTimeIntervalMs * i;
    return rec;
}

// 查出测试会话的全部记录（最新在前）。
std::vector<HistoryRecord> QueryAllRecords(CV2MsgStore& store)
{
    HistoryFilter filter;
    filter.m_ctype    = kCtype;
    filter.m_cid      = kCid;
    filter.m_page     = 1;
    filter.m_pageSize = kQueryPageSize;
    std::vector<HistoryRecord> out;
    REQUIRE(store.QueryPage(filter, kBaseTimeMs, out));
    return out;
}

// 按 msgid 在查询结果里找一条；找不到返回 NULL。
const HistoryRecord* FindByMsgid(const std::vector<HistoryRecord>& recs, int64_t msgid)
{
    for (size_t i = 0; i < recs.size(); ++i)
    {
        if (recs[i].m_msgid == msgid)
        {
            return &recs[i];
        }
    }
    return NULL;
}

} // anonymous namespace

TEST_CASE("MSB1 批量写入后每条都能按原字段查回", "[msgstorebatch]")
{
    std::wstring path = FreshTempDb(L"flamingo_msb1.db");
    CV2MsgStore store;
    REQUIRE(store.Open(path.c_str()));

    std::vector<HistoryRecord> batch;
    for (int i = 1; i <= kBatchSize; ++i)
    {
        batch.push_back(MakeRecord(i));
    }
    REQUIRE(store.UpsertBatch(batch));

    const std::vector<HistoryRecord> recs = QueryAllRecords(store);
    REQUIRE(recs.size() == (size_t)kBatchSize);
    for (int i = 1; i <= kBatchSize; ++i)
    {
        INFO("i=" << i);
        const HistoryRecord want = MakeRecord(i);
        const HistoryRecord* got = FindByMsgid(recs, want.m_msgid);
        REQUIRE(got != NULL);
        CHECK(got->m_clientmsgid == want.m_clientmsgid);
        CHECK(got->m_fromid == want.m_fromid);
        CHECK(got->m_msgtype == want.m_msgtype);
        CHECK(got->m_content == want.m_content);
        CHECK(got->m_searchText == want.m_searchText);
        CHECK(got->m_isLink == want.m_isLink);
        CHECK(got->m_timestamp == want.m_timestamp);
    }
}

TEST_CASE("MSB2 批内重复与库内已有的同一条消息都按覆盖处理", "[msgstorebatch]")
{
    std::wstring path = FreshTempDb(L"flamingo_msb2.db");
    CV2MsgStore store;
    REQUIRE(store.Open(path.c_str()));

    //库里先有第 1 条（旧内容）
    HistoryRecord old = MakeRecord(1);
    old.m_content = "{\"content\":{\"text\":\"old\"}}";
    REQUIRE(store.Upsert(old));

    //批次里：第 1 条的新内容、第 2 条、第 2 条又出现一次（更新的内容）
    std::vector<HistoryRecord> batch;
    batch.push_back(MakeRecord(1));
    batch.push_back(MakeRecord(2));
    HistoryRecord second = MakeRecord(2);
    second.m_content = "{\"content\":{\"text\":\"second, updated\"}}";
    batch.push_back(second);
    REQUIRE(store.UpsertBatch(batch));

    const std::vector<HistoryRecord> recs = QueryAllRecords(store);
    REQUIRE(recs.size() == 2);
    const HistoryRecord* r1 = FindByMsgid(recs, MakeRecord(1).m_msgid);
    const HistoryRecord* r2 = FindByMsgid(recs, MakeRecord(2).m_msgid);
    REQUIRE(r1 != NULL);
    REQUIRE(r2 != NULL);
    CHECK(r1->m_content == MakeRecord(1).m_content);
    //批内后出现的那条覆盖先出现的，与按顺序逐条写入的结果一致
    CHECK(r2->m_content == second.m_content);
}

TEST_CASE("MSB3 空批次直接成功，不改动库", "[msgstorebatch]")
{
    std::wstring path = FreshTempDb(L"flamingo_msb3.db");
    CV2MsgStore store;
    REQUIRE(store.Open(path.c_str()));
    REQUIRE(store.Upsert(MakeRecord(1)));

    std::vector<HistoryRecord> empty;
    CHECK(store.UpsertBatch(empty));

    CHECK(QueryAllRecords(store).size() == 1);
}

TEST_CASE("MSB4 库未打开时返回失败", "[msgstorebatch]")
{
    CV2MsgStore store;
    std::vector<HistoryRecord> batch;
    batch.push_back(MakeRecord(1));
    CHECK_FALSE(store.UpsertBatch(batch));
}

TEST_CASE("MSB5 msgid 非正的记录被跳过，其余照常写入", "[msgstorebatch]")
{
    std::wstring path = FreshTempDb(L"flamingo_msb5.db");
    CV2MsgStore store;
    REQUIRE(store.Open(path.c_str()));

    std::vector<HistoryRecord> batch;
    batch.push_back(MakeRecord(1));
    HistoryRecord noId = MakeRecord(2);
    noId.m_msgid = 0;                 //服务器还没分配 msgid 的消息
    batch.push_back(noId);
    HistoryRecord negative = MakeRecord(3);
    negative.m_msgid = -1;
    batch.push_back(negative);
    batch.push_back(MakeRecord(4));
    REQUIRE(store.UpsertBatch(batch));

    const std::vector<HistoryRecord> recs = QueryAllRecords(store);
    CHECK(recs.size() == 2);
    CHECK(FindByMsgid(recs, MakeRecord(1).m_msgid) != NULL);
    CHECK(FindByMsgid(recs, MakeRecord(4).m_msgid) != NULL);
}
