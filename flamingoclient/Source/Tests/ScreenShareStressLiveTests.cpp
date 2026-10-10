/**
 *  屏幕共享房间中转压测（Suite SSSTRESS，标签 [.stress][sharestress]），ScreenShareStressLiveTests.cpp
 *
 *  对应 docs/屏幕共享与录制方案.md 第 3 期 3c「服务端压测」：一个房间里一名发起人、30 名观看者，发起人以每秒 15 帧、
 *  约 2 Mbps 的码率（每 4 秒一个约 60 KB 的关键帧，其余每帧约 16 KB）连续发 20 秒，观看者全部正常读取。统计：
 *    - 每名观看者收到的帧数（最少、平均），断言最少也收到了发出帧数的 95%，且每人都收到了关键帧；
 *    - imgserver 进程在发送期间的 CPU 占用（相对一个核）与峰值工作集，以 WARN 输出，记入方案的进度表。
 *  中转令牌在测试里自己签，直接连 imgserver（辅助函数见 RoomRelayTestKit），不经过 chatserver。
 *
 *  默认不运行，也不会被「[screenshare]」选中：跑之前先起 tmp/calltest 的测试服务端，然后
 *
 *      FlamingoUnit.exe "[sharestress]"
 *
 *  balloonwj@qq.com   2026-10-03
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <string>
#include <thread>
#include <vector>
#include <memory>
#include <tlhelp32.h>
#include <psapi.h>

#include "RoomRelayTestKit.h"
#include "../net/Protocol.h"
#include "../../../flamingoserver/utils/RelayToken.h"

#pragma comment(lib, "psapi.lib")

using namespace roomrelaytest;

namespace {

//观看者人数、帧率、持续秒数
const int kStressViewers = 30;
const int kStressFps = 15;
const int kStressSeconds = 20;
//普通帧与关键帧的载荷大小（字节）：16 KB × 15 帧 ≈ 2 Mbps；关键帧单帧不超过一个分片（64 KiB）
const size_t kStressFrameBytes = 16 * 1024;
const size_t kStressKeyFrameBytes = 60 * 1024;
//每隔多少帧一个关键帧（4 秒）
const int kStressKeyFrameEvery = 4 * kStressFps;
//每名观看者至少要收到的帧数比例（百分比），以及百分比的基数
const int kStressMinReceivedPercent = 95;
const int kPercentBase = 100;
//毫秒与秒、100 纳秒单位与毫秒的换算
const ULONGLONG kMsPerSecondU = 1000;
const ULONGLONG kFileTimeUnitsPerMs = 10000;
//百分比、字节到兆位的换算
const double kPercent = 100.0;
const double kBitsPerByteD = 8.0;
const double kBitsPerMegabit = 1000000.0;
const double kBytesPerMegabyte = 1024.0 * 1024.0;

//一名观看者的收帧统计（收帧线程写、主线程在 join 之后读）
struct ViewerStats
{
    RoomPeer* m_peer;        //该观看者的连接（不拥有）
    int       m_frames;      //收到的发起人画面帧数
    int       m_keyFrames;   //其中的关键帧数
    long long m_bytes;       //收到的载荷字节数

    ViewerStats()
        : m_peer(NULL)
        , m_frames(0)
        , m_keyFrames(0)
        , m_bytes(0)
    {
    }
};

//观看者的收帧线程：一直收到超时（发起人停发 3 秒后）或断开
void stressReceive(ViewerStats* s)
{
    RoomFrame f;
    while (recvRoomFrame(*s->m_peer, f))
    {
        if (f.m_track != screenshare_track_screen || f.m_seat != 0)
            continue;
        ++s->m_frames;
        if ((f.m_flags & screenshare_flag_keyframe) != 0)
            ++s->m_keyFrames;
        s->m_bytes += static_cast<long long>(f.m_payload.size());
    }
}

//找 tmp/calltest 测试服务端的 imgserver 进程并打开（查询 CPU 时间与内存）；找不到返回 NULL
HANDLE openCalltestImgserver()
{
    HANDLE snap = ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE)
        return NULL;
    PROCESSENTRY32W pe;
    memset(&pe, 0, sizeof(pe));
    pe.dwSize = sizeof(pe);
    HANDLE found = NULL;
    for (BOOL ok = ::Process32FirstW(snap, &pe); ok && found == NULL; ok = ::Process32NextW(snap, &pe))
    {
        if (_wcsicmp(pe.szExeFile, L"imgserver.exe") != 0)
            continue;
        HANDLE h = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE, pe.th32ProcessID);
        if (h == NULL)
            continue;
        wchar_t path[MAX_PATH] = { 0 };
        DWORD len = MAX_PATH;
        if (::QueryFullProcessImageNameW(h, 0, path, &len) && wcsstr(path, L"\\tmp\\calltest\\") != NULL)
            found = h;
        else
            ::CloseHandle(h);
    }
    ::CloseHandle(snap);
    return found;
}

//进程累计 CPU 时间（内核 + 用户），单位毫秒
ULONGLONG processCpuMs(HANDLE h)
{
    FILETIME created;
    FILETIME exited;
    FILETIME kernel;
    FILETIME user;
    if (!::GetProcessTimes(h, &created, &exited, &kernel, &user))
        return 0;
    ULARGE_INTEGER k;
    ULARGE_INTEGER u;
    k.LowPart = kernel.dwLowDateTime;
    k.HighPart = kernel.dwHighDateTime;
    u.LowPart = user.dwLowDateTime;
    u.HighPart = user.dwHighDateTime;
    return (k.QuadPart + u.QuadPart) / kFileTimeUnitsPerMs;
}

} // namespace

TEST_CASE("SSSTRESS1 一个房间 30 名观看者，每秒 15 帧约 2 Mbps 持续 20 秒", "[.stress][sharestress]")
{
    const std::string room = makeRoomId();
    RoomPeer host;
    int code = -1;
    std::string reason;
    REQUIRE(joinRoom(host, room, RelayToken::role_screenshare_host, 0, kRoomTokenKey, code, reason));
    REQUIRE(code == 0);

    std::vector<std::unique_ptr<RoomPeer> > viewers;
    std::vector<ViewerStats> stats(kStressViewers);
    for (int i = 0; i < kStressViewers; ++i)
    {
        viewers.push_back(std::unique_ptr<RoomPeer>(new RoomPeer()));
        INFO("viewer seat " << (i + 1) << ": " << reason);
        REQUIRE(joinRoom(*viewers.back(), room, RelayToken::role_screenshare_member, i + 1, kRoomTokenKey, code,
                         reason));
        REQUIRE(code == 0);
        stats[i].m_peer = viewers.back().get();
    }
    std::vector<std::thread> threads;
    for (int i = 0; i < kStressViewers; ++i)
        threads.push_back(std::thread(stressReceive, &stats[i]));

    HANDLE img = openCalltestImgserver();
    const ULONGLONG cpuStart = (img != NULL) ? processCpuMs(img) : 0;
    const ULONGLONG wallStart = ::GetTickCount64();
    const std::string frame = makePayload(kStressFrameBytes, 3);
    const std::string keyFrame = makePayload(kStressKeyFrameBytes, 5);
    const int totalFrames = kStressFps * kStressSeconds;
    long long sentBytes = 0;
    for (int n = 0; n < totalFrames; ++n)
    {
        //按绝对时间排程，发送耗时不会累积成漂移
        const ULONGLONG due = wallStart + static_cast<ULONGLONG>(n) * kMsPerSecondU / kStressFps;
        const ULONGLONG now = ::GetTickCount64();
        if (due > now)
            ::Sleep(static_cast<DWORD>(due - now));
        const bool key = (n % kStressKeyFrameEvery) == 0;
        const std::string& payload = key ? keyFrame : frame;
        REQUIRE(sendRoomFrame(host, screenshare_track_screen, 0, key ? kWholeKey : kWhole, static_cast<uint64_t>(n),
                              payload));
        sentBytes += static_cast<long long>(payload.size());
    }
    const ULONGLONG wallEnd = ::GetTickCount64();
    const ULONGLONG cpuEnd = (img != NULL) ? processCpuMs(img) : 0;
    PROCESS_MEMORY_COUNTERS mem;
    memset(&mem, 0, sizeof(mem));
    mem.cb = sizeof(mem);
    const bool gotMem = (img != NULL) && ::GetProcessMemoryInfo(img, &mem, sizeof(mem));

    for (size_t i = 0; i < threads.size(); ++i)
        threads[i].join();
    if (img != NULL)
        ::CloseHandle(img);

    int minFrames = totalFrames;
    long long sumFrames = 0;
    long long sumBytes = 0;
    for (int i = 0; i < kStressViewers; ++i)
    {
        INFO("viewer seat " << (i + 1) << " frames " << stats[i].m_frames << ", key frames " << stats[i].m_keyFrames);
        CHECK(stats[i].m_keyFrames > 0);
        if (stats[i].m_frames < minFrames)
            minFrames = stats[i].m_frames;
        sumFrames += stats[i].m_frames;
        sumBytes += stats[i].m_bytes;
    }
    const double wallSec = static_cast<double>(wallEnd - wallStart) / static_cast<double>(kMsPerSecondU);
    const double inMbps = static_cast<double>(sentBytes) * kBitsPerByteD / kBitsPerMegabit / wallSec;
    const double outMbps = static_cast<double>(sumBytes) * kBitsPerByteD / kBitsPerMegabit / wallSec;
    const double cpuPercent = (img != NULL && wallEnd > wallStart)
                              ? static_cast<double>(cpuEnd - cpuStart) * kPercent / static_cast<double>(wallEnd - wallStart)
                              : -1.0;
    WARN("SSSTRESS1: viewers=" << kStressViewers << ", sent frames=" << totalFrames << " in " << wallSec
         << " s, min frames=" << minFrames << ", avg frames=" << (static_cast<double>(sumFrames) / kStressViewers)
         << ", host in " << inMbps << " Mbps, relay out " << outMbps << " Mbps, imgserver cpu " << cpuPercent
         << "% of one core, imgserver peak working set "
         << (gotMem ? static_cast<double>(mem.PeakWorkingSetSize) / kBytesPerMegabyte : -1.0) << " MB");
    CHECK(minFrames * kPercentBase >= totalFrames * kStressMinReceivedPercent);
}
