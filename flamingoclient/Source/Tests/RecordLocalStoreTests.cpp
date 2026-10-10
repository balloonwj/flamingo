/**
 *  录像记录本地库读写单元测试（标签 [recordstore]），RecordLocalStoreTests.cpp
 *
 *  用内存库（:memory:）驱动 balloonhistory::CRecordLocalStore（docs/屏幕共享与录制方案.md 第 11.6 节 F9），覆盖：
 *    RLS1 添加后按录制结束时间从新到旧列出，路径（含中文）、时长、大小、结束时间原样读回；
 *    RLS2 同一路径再添加（大小写不同也算同一路径）时覆盖原记录，id 不变，不出现两条；
 *    RLS3 按 id 修改路径；id 不存在、新路径为空或已被另一条记录占用时返回失败；
 *    RLS4 按 id 删除，列表少一条；id 不存在时返回失败；
 *    RLS5 库未打开时各操作返回失败、不崩溃；路径为空时不写入。
 *
 *  注：CRecordLocalStore 是单例，各用例通过先 Close 再 Open(:memory:) 拿到干净的库。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-05
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include "../UI/BalloonHistory/RecordLocalStore.h"

using namespace balloonhistory;

namespace {

//几个录制结束时间（Unix 毫秒）：UTC 2026-10-03、2026-10-04、2026-10-05 的 04:00
const int64_t kRlsDay3 = 1791000000000LL;
const int64_t kRlsDay4 = 1791086400000LL;
const int64_t kRlsDay5 = 1791172800000LL;

//取一个干净的内存库：先关再开，确保不带上一个用例的数据
CRecordLocalStore& FreshRecordStore()
{
    CRecordLocalStore& store = CRecordLocalStore::GetInstance();
    store.Close();
    REQUIRE(store.Open(L":memory:"));
    return store;
}

RecordLocalEntry MakeRlsEntry(const wchar_t* path, int64_t durationMs, int64_t sizeBytes, int64_t endTimeMs)
{
    RecordLocalEntry e;
    e.m_path = path;
    e.m_durationMs = durationMs;
    e.m_sizeBytes = sizeBytes;
    e.m_endTimeMs = endTimeMs;
    return e;
}

} // namespace

TEST_CASE("RLS1 添加后按录制结束时间从新到旧列出，字段原样读回", "[recordstore]")
{
    CRecordLocalStore& store = FreshRecordStore();
    int64_t id3 = 0;
    int64_t id5 = 0;
    int64_t id4 = 0;
    //故意不按时间顺序添加
    REQUIRE(store.Add(MakeRlsEntry(L"D:\\录像\\会议 1003.mp4", 83000, 15555677, kRlsDay3), &id3));
    REQUIRE(store.Add(MakeRlsEntry(L"D:\\Videos\\20261005_131807.mp4", 3723000, 1048576000LL, kRlsDay5), &id5));
    REQUIRE(store.Add(MakeRlsEntry(L"D:\\Videos\\20261004_093011.mp4", 42000, 6100000, kRlsDay4), &id4));
    CHECK(id3 > 0);
    CHECK(id4 > 0);
    CHECK(id5 > 0);
    CHECK(id3 != id4);
    CHECK(id4 != id5);

    std::vector<RecordLocalEntry> all;
    REQUIRE(store.List(all));
    REQUIRE(all.size() == 3);
    CHECK(all[0].m_id == id5);
    CHECK(all[1].m_id == id4);
    CHECK(all[2].m_id == id3);
    CHECK(all[0].m_path == L"D:\\Videos\\20261005_131807.mp4");
    CHECK(all[0].m_durationMs == 3723000);
    CHECK(all[0].m_sizeBytes == 1048576000LL);
    CHECK(all[0].m_endTimeMs == kRlsDay5);
    CHECK(all[2].m_path == L"D:\\录像\\会议 1003.mp4");
    CHECK(all[2].m_durationMs == 83000);
    CHECK(all[2].m_sizeBytes == 15555677);
    CHECK(all[2].m_endTimeMs == kRlsDay3);

    //结束时间相同：id 大的（后添加的）在前
    int64_t idSame = 0;
    REQUIRE(store.Add(MakeRlsEntry(L"D:\\Videos\\same_time.mp4", 1000, 2000, kRlsDay5), &idSame));
    REQUIRE(store.List(all));
    REQUIRE(all.size() == 4);
    CHECK(all[0].m_id == idSame);
    CHECK(all[1].m_id == id5);
}

TEST_CASE("RLS2 同一路径再添加时覆盖原记录，id 不变", "[recordstore]")
{
    CRecordLocalStore& store = FreshRecordStore();
    int64_t first = 0;
    REQUIRE(store.Add(MakeRlsEntry(L"D:\\Videos\\a.mp4", 1000, 2000, kRlsDay3), &first));
    int64_t second = 0;
    //大小写不同也算同一个文件（Windows 路径不区分大小写）
    REQUIRE(store.Add(MakeRlsEntry(L"d:\\videos\\A.MP4", 5000, 9000, kRlsDay5), &second));
    CHECK(second == first);

    std::vector<RecordLocalEntry> all;
    REQUIRE(store.List(all));
    REQUIRE(all.size() == 1);
    CHECK(all[0].m_id == first);
    CHECK(all[0].m_durationMs == 5000);
    CHECK(all[0].m_sizeBytes == 9000);
    CHECK(all[0].m_endTimeMs == kRlsDay5);

    //出参可以为 NULL
    REQUIRE(store.Add(MakeRlsEntry(L"D:\\Videos\\b.mp4", 1, 1, kRlsDay4), NULL));
    REQUIRE(store.List(all));
    CHECK(all.size() == 2);
}

TEST_CASE("RLS3 按 id 修改路径", "[recordstore]")
{
    CRecordLocalStore& store = FreshRecordStore();
    int64_t idA = 0;
    int64_t idB = 0;
    REQUIRE(store.Add(MakeRlsEntry(L"D:\\Videos\\a.mp4", 1000, 2000, kRlsDay3), &idA));
    REQUIRE(store.Add(MakeRlsEntry(L"D:\\Videos\\b.mp4", 1000, 2000, kRlsDay4), &idB));

    REQUIRE(store.UpdatePath(idA, L"D:\\Videos\\周会录像.mp4"));
    std::vector<RecordLocalEntry> all;
    REQUIRE(store.List(all));
    REQUIRE(all.size() == 2);
    CHECK(all[1].m_id == idA);
    CHECK(all[1].m_path == L"D:\\Videos\\周会录像.mp4");
    //其余字段不变
    CHECK(all[1].m_durationMs == 1000);
    CHECK(all[1].m_endTimeMs == kRlsDay3);

    //id 不存在、新路径为空、新路径已被另一条记录占用：都失败，原记录不变
    CHECK_FALSE(store.UpdatePath(idA + idB + 100, L"D:\\Videos\\c.mp4"));
    CHECK_FALSE(store.UpdatePath(idA, L""));
    CHECK_FALSE(store.UpdatePath(idA, L"D:\\Videos\\B.mp4"));
    REQUIRE(store.List(all));
    CHECK(all[1].m_path == L"D:\\Videos\\周会录像.mp4");
    CHECK(all[0].m_path == L"D:\\Videos\\b.mp4");
}

TEST_CASE("RLS4 按 id 删除", "[recordstore]")
{
    CRecordLocalStore& store = FreshRecordStore();
    int64_t idA = 0;
    int64_t idB = 0;
    REQUIRE(store.Add(MakeRlsEntry(L"D:\\Videos\\a.mp4", 1000, 2000, kRlsDay3), &idA));
    REQUIRE(store.Add(MakeRlsEntry(L"D:\\Videos\\b.mp4", 1000, 2000, kRlsDay4), &idB));

    REQUIRE(store.Remove(idA));
    std::vector<RecordLocalEntry> all;
    REQUIRE(store.List(all));
    REQUIRE(all.size() == 1);
    CHECK(all[0].m_id == idB);
    //已删除或不存在的 id：失败
    CHECK_FALSE(store.Remove(idA));
    CHECK_FALSE(store.Remove(idA + idB + 100));
}

TEST_CASE("RLS5 库未打开时各操作返回失败、不崩溃；路径为空时不写入", "[recordstore]")
{
    CRecordLocalStore& store = CRecordLocalStore::GetInstance();
    store.Close();
    CHECK_FALSE(store.IsOpen());
    int64_t id = 0;
    CHECK_FALSE(store.Add(MakeRlsEntry(L"D:\\Videos\\a.mp4", 1, 1, kRlsDay3), &id));
    std::vector<RecordLocalEntry> all;
    all.push_back(RecordLocalEntry());
    CHECK_FALSE(store.List(all));
    CHECK(all.empty());
    CHECK_FALSE(store.UpdatePath(1, L"D:\\Videos\\b.mp4"));
    CHECK_FALSE(store.Remove(1));

    CRecordLocalStore& fresh = FreshRecordStore();
    CHECK(fresh.IsOpen());
    CHECK_FALSE(fresh.Add(MakeRlsEntry(L"", 1, 1, kRlsDay3), &id));
    REQUIRE(fresh.List(all));
    CHECK(all.empty());
    //同一路径重复 Open：直接成功，数据还在
    REQUIRE(fresh.Add(MakeRlsEntry(L"D:\\Videos\\a.mp4", 1, 1, kRlsDay3), &id));
    REQUIRE(fresh.Open(L":memory:"));
    REQUIRE(fresh.List(all));
    CHECK(all.size() == 1);
    fresh.Close();

    //库文件路径：个人目录下的 ChatLog\recordlocal.db；个人目录为空（尚未登录）时为空
    CHECK(CRecordLocalStore::DbPathIn(L"D:\\Bin\\Users\\MY0127\\") == L"D:\\Bin\\Users\\MY0127\\ChatLog\\recordlocal.db");
    CHECK(CRecordLocalStore::DbPathIn(L"").empty());
}
