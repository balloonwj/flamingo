/**
 *  云端录制对中转转发的开销：计时用例（Suite CRB，docs/屏幕共享与录制方案.md 第 26a 步、第 14.6 节），test_cloudrecordbench.cpp
 *
 *  要回答的问题：imgserver 在转发房间帧的同时另写一份到磁盘（CloudRecorder），转发线程多花多少时间、整个进程多用多少 CPU、
 *  写线程跟不跟得上、8 MiB 的积压上限会不会丢帧。做法是用模拟推流在进程内直接驱动录制器，不连真实服务端、不录真实屏幕：
 *    - 每个房间按屏幕共享的典型码率推帧：屏幕轨每秒 15 帧、约 2 Mbps（每帧 16 667 字节），麦克风轨每 20 毫秒一帧（160 字节）；
 *    - CRB1 不限速：把 20 秒的帧一口气推完，量转发线程上每次 onFrame 的耗时（平均、最大）、写线程把文件写完的总时长与吞吐、
 *           积压时丢掉的帧数；
 *    - CRB2 按真实节奏：30 个房间同时录 10 秒，量整个进程的 CPU 占用与丢帧。
 *  转发本身的开销（每帧按观看人数各加密一次）见第 3c 步的压测 SSSTRESS1，这里只量录制多出来的那一份。
 *
 *  本套用例不参与默认测试集：要写几百 MB 的临时文件，跑一次几十秒。两个用例都挂了 doctest::skip()，要跑必须同时给上
 *  --no-skip 与用例名过滤：flamingo_tests --no-skip --test-case="CRB*"。数字用 MESSAGE 输出；用例只断言帧数对得上，
 *  不断言具体快慢。临时文件写在 Linux 的 /tmp 或 Windows 的当前目录，用完删除。
 *
 *  balloonwj@qq.com   2026-10-07
 */
#include <stdio.h>
#include <stdint.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#ifdef WIN32
#include <windows.h>
#else
#include <sys/resource.h>
#endif

#include "doctest.h"
#include "../chatserversrc/Protocol.h"
#include "../imgserversrc/CloudRecorder.h"

namespace {

//模拟推流的参数：屏幕轨每帧字节数（2 Mbps ÷ 15 帧 ÷ 8）、帧间隔（毫秒，约每秒 15 帧）；麦克风轨每帧字节数与间隔
const size_t kCrbScreenBytes = 16667;
const int kCrbScreenIntervalMs = 67;
const size_t kCrbMicBytes = 160;
const int kCrbMicIntervalMs = 20;
//模拟时间的步长（毫秒）
const int kCrbTickMs = 1;
//CRB1 推的时长（模拟秒数）、CRB2 按真实节奏录的时长（秒）
const int kCrbBurstSeconds = 20;
const int kCrbRealtimeSeconds = 10;
//CRB2 的房间数
const int kCrbRealtimeRooms = 30;
//开始时刻（Unix 毫秒）与录像 id 的起点
const int64_t kCrbT0 = 1790000000000LL;
const uint64_t kCrbIdBase = 960000;
//「磁盘剩余空间」：总是足够
const uint64_t kCrbPlenty = 100ULL * 1024 * 1024 * 1024;
const uint64_t kCrbMinFree = 1024ULL * 1024 * 1024;
//等写线程收尾的上限（毫秒）
const int kCrbWaitMs = 120000;
//换算
const double kCrbNsPerUs = 1000.0;
const double kCrbUsPerMs = 1000.0;
const double kCrbMsPerSecond = 1000.0;
const double kCrbPercent = 100.0;
const double kCrbBytesPerMb = 1024.0 * 1024.0;
const int kCrbMillisPerSecond = 1000;

//模拟的主持人 userid 起点（每个房间一个）
const int64_t kCrbUserBase = 1000;
//帧头里时间戳的字节数
const size_t kCrbTimestampBytes = 8;
//录像目录：Linux 上写 /tmp（与正式部署一样是本机磁盘；开发机上 build-linux 在 /mnt/e，经 WSL 的文件共享写入，
//速度不代表服务器），Windows 上写当前目录
#ifdef WIN32
const char* const kCrbDir = "./";
#else
const char* const kCrbDir = "/tmp/";
#endif

uint64_t crbFreeSpace(const std::string& /*dir*/)
{
    return kCrbPlenty;
}

//一帧房间中转帧的明文包体：11 字节帧头（轨道、座位、标志、8 字节时间戳）加数据
std::string crbFrame(int track, int seat, size_t dataBytes)
{
    std::string body;
    body.push_back(static_cast<char>(track));
    body.push_back(static_cast<char>(seat));
    body.push_back(0);
    body.append(kCrbTimestampBytes, '\0');
    body.append(dataBytes, 'x');
    return body;
}

//整个进程到目前为止用掉的 CPU 时间（用户态加内核态，秒），含所有线程
double crbProcessCpuSeconds()
{
#ifdef WIN32
    FILETIME created;
    FILETIME exited;
    FILETIME kernel;
    FILETIME user;
    if (!::GetProcessTimes(::GetCurrentProcess(), &created, &exited, &kernel, &user))
        return 0.0;
    ULARGE_INTEGER k;
    ULARGE_INTEGER u;
    k.LowPart = kernel.dwLowDateTime;
    k.HighPart = kernel.dwHighDateTime;
    u.LowPart = user.dwLowDateTime;
    u.HighPart = user.dwHighDateTime;
    //FILETIME 的单位是 100 纳秒
    const double kHundredNsPerSecond = 10000000.0;
    return static_cast<double>(k.QuadPart + u.QuadPart) / kHundredNsPerSecond;
#else
    struct rusage ru;
    if (getrusage(RUSAGE_SELF, &ru) != 0)
        return 0.0;
    const double kUsPerSecond = 1000000.0;
    return ru.ru_utime.tv_sec + ru.ru_stime.tv_sec + (ru.ru_utime.tv_usec + ru.ru_stime.tv_usec) / kUsPerSecond;
#endif
}

//收录制结束回报
class CrbListener : public ICloudRecorderListener
{
public:
    CrbListener()
        : m_count(0)
    {
    }

    virtual void onCloudRecordFinished(const CloudRecordResult& result) override
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_results.push_back(result);
        ++m_count;
        m_cv.notify_all();
    }

    bool waitFor(int count)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        const std::chrono::steady_clock::time_point deadline =
            std::chrono::steady_clock::now() + std::chrono::milliseconds(kCrbWaitMs);
        while (m_count < count)
        {
            if (m_cv.wait_until(lock, deadline) == std::cv_status::timeout)
                break;
        }
        return m_count >= count;
    }

    std::mutex                     m_mutex;     //保护下面两项
    std::condition_variable        m_cv;        //回报到达的通知
    std::vector<CloudRecordResult> m_results;   //收到的回报
    int                            m_count;     //收到的回报条数
};

//转发线程一侧的计时
struct CrbPushStats
{
    uint64_t m_calls;      //onFrame 调用次数
    uint64_t m_bytes;      //推进去的字节数
    double   m_sumNs;      //onFrame 耗时之和（纳秒）
    double   m_maxNs;      //onFrame 单次最大耗时（纳秒）

    CrbPushStats()
        : m_calls(0)
        , m_bytes(0)
        , m_sumNs(0.0)
        , m_maxNs(0.0)
    {
    }
};

//推一帧并计时
void crbPush(CloudRecorder& rec, const std::string& roomId, int track, const std::string& body, int64_t nowMs,
             CrbPushStats& stats)
{
    const std::chrono::steady_clock::time_point a = std::chrono::steady_clock::now();
    rec.onFrame(roomId, track, body, nowMs);
    const double ns = static_cast<double>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - a).count());
    stats.m_sumNs += ns;
    if (ns > stats.m_maxNs)
        stats.m_maxNs = ns;
    ++stats.m_calls;
    stats.m_bytes += body.size();
}

std::string crbRoomId(int index)
{
    std::ostringstream os;
    os << "room-crb-" << index;
    return os.str();
}

//开始录 rooms 个房间
void crbStartRooms(CloudRecorder& rec, int rooms)
{
    for (int i = 0; i < rooms; ++i)
    {
        std::vector<std::pair<int, int64_t> > seats;
        seats.push_back(std::make_pair(0, kCrbUserBase + i));
        REQUIRE(rec.start(kCrbIdBase + static_cast<uint64_t>(i), crbRoomId(i), kCrbT0, false, seats) ==
                cloudrec_start_ok);
    }
}

//停掉 rooms 个房间、等全部回报，返回写进文件的帧数与丢掉的帧数之和；删除录像文件
void crbStopRooms(CloudRecorder& rec, CrbListener& listener, int rooms, uint64_t& frames, uint64_t& dropped,
                  uint64_t& bytes)
{
    for (int i = 0; i < rooms; ++i)
        CHECK(rec.stop(kCrbIdBase + static_cast<uint64_t>(i), cloudrec_stop_requested));
    REQUIRE(listener.waitFor(rooms));
    frames = 0;
    dropped = 0;
    bytes = 0;
    for (size_t i = 0; i < listener.m_results.size(); ++i)
    {
        frames += listener.m_results[i].m_frames;
        dropped += listener.m_results[i].m_dropped;
        bytes += listener.m_results[i].m_bytes;
        rec.deleteFile(listener.m_results[i].m_fileName);
    }
}

//CRB1 的一轮：rooms 个房间各推 kCrbBurstSeconds 秒的帧，不限速
void crbBurst(int rooms)
{
    CrbListener listener;
    CloudRecorder rec;
    rec.configure(kCrbDir, kCrbMinFree, &crbFreeSpace);
    rec.setListener(&listener);
    crbStartRooms(rec, rooms);
    const std::string screen = crbFrame(screenshare_track_screen, 0, kCrbScreenBytes);
    const std::string mic = crbFrame(screenshare_track_mic, 0, kCrbMicBytes);
    CrbPushStats stats;
    const double cpu0 = crbProcessCpuSeconds();
    const std::chrono::steady_clock::time_point wall0 = std::chrono::steady_clock::now();
    const int totalMs = kCrbBurstSeconds * kCrbMillisPerSecond;
    for (int t = 0; t < totalMs; t += kCrbTickMs)
    {
        for (int r = 0; r < rooms; ++r)
        {
            if (t % kCrbScreenIntervalMs == 0)
                crbPush(rec, crbRoomId(r), screenshare_track_screen, screen, kCrbT0 + t, stats);
            if (t % kCrbMicIntervalMs == 0)
                crbPush(rec, crbRoomId(r), screenshare_track_mic, mic, kCrbT0 + t, stats);
        }
    }
    const double pushMs = static_cast<double>(
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - wall0).count()) /
        kCrbUsPerMs;
    uint64_t frames = 0;
    uint64_t dropped = 0;
    uint64_t bytes = 0;
    crbStopRooms(rec, listener, rooms, frames, dropped, bytes);
    const double totalWallMs = static_cast<double>(
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - wall0).count()) /
        kCrbUsPerMs;
    const double cpu = crbProcessCpuSeconds() - cpu0;
    CHECK(frames + dropped == stats.m_calls);

    std::ostringstream os;
    os << "CRB1 " << rooms << " 个房间、各推 " << kCrbBurstSeconds << " 秒的帧（不限速）："
       << "\n    onFrame " << stats.m_calls << " 次，平均 " << (stats.m_sumNs / stats.m_calls / kCrbNsPerUs)
       << " 微秒，最大 " << (stats.m_maxNs / kCrbNsPerUs) << " 微秒"
       << "\n    推完用时 " << pushMs << " 毫秒，写完用时 " << totalWallMs << " 毫秒，文件共 "
       << (bytes / kCrbBytesPerMb) << " MB，写入吞吐 " << (bytes / kCrbBytesPerMb) / (totalWallMs / kCrbMsPerSecond)
       << " MB/s"
       << "\n    写进文件 " << frames << " 帧，积压丢掉 " << dropped << " 帧；进程 CPU " << cpu << " 秒";
    MESSAGE(os.str());
}

} // namespace

TEST_CASE("CRB1 recording cost on the forwarding thread, unthrottled" * doctest::skip())
{
    crbBurst(1);
    crbBurst(10);
    crbBurst(30);
}

TEST_CASE("CRB2 thirty rooms recording at the real pace" * doctest::skip())
{
    CrbListener listener;
    CloudRecorder rec;
    rec.configure(kCrbDir, kCrbMinFree, &crbFreeSpace);
    rec.setListener(&listener);
    crbStartRooms(rec, kCrbRealtimeRooms);
    const std::string screen = crbFrame(screenshare_track_screen, 0, kCrbScreenBytes);
    const std::string mic = crbFrame(screenshare_track_mic, 0, kCrbMicBytes);
    CrbPushStats stats;
    //空转一段时间的 CPU 作为基线：只睡眠、不推帧
    const double idle0 = crbProcessCpuSeconds();
    std::this_thread::sleep_for(std::chrono::milliseconds(kCrbMillisPerSecond));
    const double idleCpu = crbProcessCpuSeconds() - idle0;
    const double cpu0 = crbProcessCpuSeconds();
    const std::chrono::steady_clock::time_point wall0 = std::chrono::steady_clock::now();
    const int totalMs = kCrbRealtimeSeconds * kCrbMillisPerSecond;
    for (int t = 0; t < totalMs; t += kCrbTickMs)
    {
        //按真实时间推进：还没到这一毫秒就先睡到它
        const std::chrono::steady_clock::time_point due = wall0 + std::chrono::milliseconds(t);
        std::this_thread::sleep_until(due);
        for (int r = 0; r < kCrbRealtimeRooms; ++r)
        {
            if (t % kCrbScreenIntervalMs == 0)
                crbPush(rec, crbRoomId(r), screenshare_track_screen, screen, kCrbT0 + t, stats);
            if (t % kCrbMicIntervalMs == 0)
                crbPush(rec, crbRoomId(r), screenshare_track_mic, mic, kCrbT0 + t, stats);
        }
    }
    const double cpu = crbProcessCpuSeconds() - cpu0;
    uint64_t frames = 0;
    uint64_t dropped = 0;
    uint64_t bytes = 0;
    crbStopRooms(rec, listener, kCrbRealtimeRooms, frames, dropped, bytes);
    CHECK(frames + dropped == stats.m_calls);
    CHECK(dropped == 0);

    std::ostringstream os;
    os << "CRB2 " << kCrbRealtimeRooms << " 个房间按真实节奏录 " << kCrbRealtimeSeconds << " 秒："
       << "\n    onFrame " << stats.m_calls << " 次，平均 " << (stats.m_sumNs / stats.m_calls / kCrbNsPerUs)
       << " 微秒，最大 " << (stats.m_maxNs / kCrbNsPerUs) << " 微秒"
       << "\n    进程 CPU " << cpu << " 秒，占一个核的 " << (cpu / kCrbRealtimeSeconds * kCrbPercent)
       << "%（空转基线每秒 " << idleCpu << " 秒，含推帧线程的定时开销）"
       << "\n    文件共 " << (bytes / kCrbBytesPerMb) << " MB，写进文件 " << frames << " 帧，积压丢掉 " << dropped << " 帧";
    MESSAGE(os.str());
}
