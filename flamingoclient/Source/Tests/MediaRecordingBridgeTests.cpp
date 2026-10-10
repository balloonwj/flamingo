/**
 *  IMediaHost 录像记录方法的单元测试（标签 [recordbridge]），MediaRecordingBridgeTests.cpp
 *
 *  CMediaHostAdapter 的 AddRecording / ListRecordings / UpdateRecordingPath / RemoveRecording 各自直接转调
 *  MediaRecordingBridge.h 里的同名函数（适配器本身依赖主面板，无法编进本工程），这里测这四个函数，用内存库
 *  驱动 CRecordLocalStore（docs/屏幕共享与录制方案.md 第 11.6 节 F9 的 RMH1）：
 *    RMH1a 添加后列出：字段与本地库一致，最新的在前；结构体大小不符、路径为空时拒绝；
 *    RMH1b 只取总数（capacity 为 0）、数组比记录少时只填前几条；元素大小不符的不填写；
 *    RMH1c 改路径、删除后，列出的结果与本地库一致；本地库没有打开时各函数返回失败或 0。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-05
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <cwchar>
#include <vector>
#include "../App/MediaRecordingBridge.h"
#include "../UI/BalloonHistory/RecordLocalStore.h"

using flamingomedia::MediaRecordingInfo;

namespace {

//两个录制结束时间（Unix 毫秒）：UTC 2026-10-04、2026-10-05 的 04:00
const int64_t kRmhDay4 = 1791086400000LL;
const int64_t kRmhDay5 = 1791172800000LL;

//取一个干净的内存库
void FreshBridgeStore()
{
    balloonhistory::CRecordLocalStore& store = balloonhistory::CRecordLocalStore::GetInstance();
    store.Close();
    REQUIRE(store.Open(L":memory:"));
}

MediaRecordingInfo MakeRmhInfo(const wchar_t* path, int64_t durationMs, int64_t sizeBytes, int64_t endTimeMs)
{
    MediaRecordingInfo info;
    ::memset(&info, 0, sizeof(info));
    info.m_cbSize = sizeof(MediaRecordingInfo);
    info.m_durationMs = durationMs;
    info.m_sizeBytes = sizeBytes;
    info.m_endTimeMs = endTimeMs;
    ::wcsncpy_s(info.m_path, flamingomedia::kMediaRecordingPathChars, path, _TRUNCATE);
    return info;
}

//准备一个能装 n 条记录的数组，每个元素都填好 m_cbSize
std::vector<MediaRecordingInfo> MakeRmhArray(size_t n)
{
    std::vector<MediaRecordingInfo> v(n);
    for (size_t i = 0; i < n; ++i)
    {
        ::memset(&v[i], 0, sizeof(MediaRecordingInfo));
        v[i].m_cbSize = sizeof(MediaRecordingInfo);
    }
    return v;
}

} // namespace

TEST_CASE("RMH1a 添加后列出的字段与本地库一致；结构体大小不符、路径为空时拒绝", "[recordbridge]")
{
    FreshBridgeStore();
    MediaRecordingInfo older = MakeRmhInfo(L"D:\\录像\\周会.mp4", 83000, 15555677, kRmhDay4);
    MediaRecordingInfo newer = MakeRmhInfo(L"D:\\Videos\\20261005_131807.mp4", 42000, 6100000, kRmhDay5);
    older.m_id = 999;   //m_id 被忽略
    REQUIRE(balloonapp::AddRecordingToStore(&older));
    REQUIRE(balloonapp::AddRecordingToStore(&newer));

    std::vector<MediaRecordingInfo> out = MakeRmhArray(4);
    REQUIRE(balloonapp::ListRecordingsFromStore(&out[0], (uint32_t)out.size()) == 2u);
    CHECK(std::wstring(out[0].m_path) == L"D:\\Videos\\20261005_131807.mp4");
    CHECK(out[0].m_durationMs == 42000);
    CHECK(out[0].m_sizeBytes == 6100000);
    CHECK(out[0].m_endTimeMs == kRmhDay5);
    CHECK(std::wstring(out[1].m_path) == L"D:\\录像\\周会.mp4");
    CHECK(out[1].m_id != 999);

    //与本地库直接读到的一致
    std::vector<balloonhistory::RecordLocalEntry> all;
    REQUIRE(balloonhistory::CRecordLocalStore::GetInstance().List(all));
    REQUIRE(all.size() == 2);
    CHECK(out[0].m_id == all[0].m_id);
    CHECK(out[1].m_id == all[1].m_id);

    //结构体大小不符、路径为空、空指针：拒绝，记录数不变
    MediaRecordingInfo badSize = MakeRmhInfo(L"D:\\Videos\\x.mp4", 1, 1, kRmhDay5);
    badSize.m_cbSize = sizeof(MediaRecordingInfo) - 1;
    CHECK_FALSE(balloonapp::AddRecordingToStore(&badSize));
    MediaRecordingInfo emptyPath = MakeRmhInfo(L"", 1, 1, kRmhDay5);
    CHECK_FALSE(balloonapp::AddRecordingToStore(&emptyPath));
    CHECK_FALSE(balloonapp::AddRecordingToStore(NULL));
    CHECK(balloonapp::ListRecordingsFromStore(NULL, 0) == 2u);
}

TEST_CASE("RMH1b 只取总数、数组比记录少时只填前几条；元素大小不符的不填写", "[recordbridge]")
{
    FreshBridgeStore();
    MediaRecordingInfo a = MakeRmhInfo(L"D:\\Videos\\a.mp4", 1000, 2000, kRmhDay4);
    MediaRecordingInfo b = MakeRmhInfo(L"D:\\Videos\\b.mp4", 1000, 2000, kRmhDay5);
    REQUIRE(balloonapp::AddRecordingToStore(&a));
    REQUIRE(balloonapp::AddRecordingToStore(&b));

    CHECK(balloonapp::ListRecordingsFromStore(NULL, 0) == 2u);

    std::vector<MediaRecordingInfo> one = MakeRmhArray(1);
    CHECK(balloonapp::ListRecordingsFromStore(&one[0], 1) == 2u);
    CHECK(std::wstring(one[0].m_path) == L"D:\\Videos\\b.mp4");

    //第二个元素的 m_cbSize 不对：不填写它，第一个照常填写
    std::vector<MediaRecordingInfo> two = MakeRmhArray(2);
    two[1].m_cbSize = 0;
    CHECK(balloonapp::ListRecordingsFromStore(&two[0], 2) == 2u);
    CHECK(std::wstring(two[0].m_path) == L"D:\\Videos\\b.mp4");
    CHECK(two[1].m_path[0] == L'\0');
}

TEST_CASE("RMH1c 改路径、删除后列出的结果与本地库一致；本地库没有打开时返回失败", "[recordbridge]")
{
    FreshBridgeStore();
    MediaRecordingInfo a = MakeRmhInfo(L"D:\\Videos\\a.mp4", 1000, 2000, kRmhDay4);
    MediaRecordingInfo b = MakeRmhInfo(L"D:\\Videos\\b.mp4", 1000, 2000, kRmhDay5);
    REQUIRE(balloonapp::AddRecordingToStore(&a));
    REQUIRE(balloonapp::AddRecordingToStore(&b));
    std::vector<MediaRecordingInfo> out = MakeRmhArray(2);
    REQUIRE(balloonapp::ListRecordingsFromStore(&out[0], 2) == 2u);
    const int64_t idA = out[1].m_id;
    const int64_t idB = out[0].m_id;

    REQUIRE(balloonapp::UpdateRecordingPathInStore(idA, L"D:\\Videos\\周会录像.mp4"));
    CHECK_FALSE(balloonapp::UpdateRecordingPathInStore(idA, NULL));
    CHECK_FALSE(balloonapp::UpdateRecordingPathInStore(idA, L""));
    REQUIRE(balloonapp::RemoveRecordingFromStore(idB));
    CHECK_FALSE(balloonapp::RemoveRecordingFromStore(idB));

    out = MakeRmhArray(2);
    REQUIRE(balloonapp::ListRecordingsFromStore(&out[0], 2) == 1u);
    CHECK(out[0].m_id == idA);
    CHECK(std::wstring(out[0].m_path) == L"D:\\Videos\\周会录像.mp4");

    //本地库没有打开（尚未登录）
    balloonhistory::CRecordLocalStore::GetInstance().Close();
    CHECK_FALSE(balloonapp::AddRecordingToStore(&a));
    CHECK(balloonapp::ListRecordingsFromStore(&out[0], 2) == 0u);
    CHECK_FALSE(balloonapp::UpdateRecordingPathInStore(idA, L"D:\\Videos\\c.mp4"));
    CHECK_FALSE(balloonapp::RemoveRecordingFromStore(idA));
}
