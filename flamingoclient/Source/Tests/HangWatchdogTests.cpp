/**
 *  界面线程无响应检测的单元测试（标签 [hangwatch]），HangWatchdogTests.cpp
 *
 *  背景：bugs.md BUG-120。客户端偶发卡死时日志里没有任何线索，只能靠人手工附加调试器取堆栈。CHangWatchdog 在后台线程上
 *  定期探测界面线程，连续无响应达到阈值时自动写一份转储。本文件覆盖：
 *    - HWD1 判定：界面线程一直有响应时什么都不做；无响应不够久、或连续失败次数不够时也不写转储；
 *    - HWD2 判定：无响应达到阈值写一次转储，同一次无响应不重复写；恢复后记一条日志并重新计时，下一次无响应可以再写；
 *    - HWD3 判定：一个进程写满 m_maxDumps 份之后不再写，恢复的日志照常记；
 *    - HWD4 实际运行：测试线程上的窗口卡住一段时间，检测线程写出转储文件（文件存在且不为空）；窗口恢复后不再多写。
 *  HWD4 在系统临时目录下建一个子目录放转储，用完删除；在调试器里运行时检测线程不写转储，本用例跳过。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-07
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <string>
#include <thread>

#include "../App/HangWatchdog.h"

using namespace balloonapp;

namespace {

//HWD1~3 用的参数：失败至少 3 次、持续至少 1000 毫秒写转储，最多 2 份
const int kHwdMinFailed = 3;
const int kHwdDumpAfterMs = 1000;
const int kHwdMaxDumps = 2;
//HWD1~3 里每次探测的耗时（毫秒），按超时计
const int64_t kHwdProbeMs = 400;

//HWD4：检测参数（毫秒）——探测间隔、探测超时、写转储的阈值
const int kHwd4IntervalMs = 50;
const int kHwd4TimeoutMs = 100;
const int kHwd4DumpAfterMs = 300;
//HWD4：测试窗口卡住的时长（毫秒）
const DWORD kHwd4HangMs = 1500;
//HWD4：等待条件成立时每次检查的间隔与最长等待（毫秒）
const DWORD kHwd4PollMs = 20;
const DWORD kHwd4WaitMaxMs = 5000;
//HWD4：测试窗口收到这条消息就卡住 kHwd4HangMs
const UINT kHwd4MsgHang = WM_APP + 1;
//HWD4：测试窗口的窗口类名
const wchar_t* const kHwd4WndClass = L"FlamingoUnit_HangWatchdogTarget";

HangWatchConfig hwdConfig()
{
    HangWatchConfig cfg;
    cfg.m_minFailedProbes = kHwdMinFailed;
    cfg.m_dumpAfterMs = kHwdDumpAfterMs;
    cfg.m_maxDumps = kHwdMaxDumps;
    return cfg;
}

/**
 *  从 start 起连续探测 count 次，每次都失败（耗时 kHwdProbeMs），返回各次的判定。
 *  @param policy 判定对象
 *  @param start  第一次探测开始的时刻（毫秒）
 *  @param count  探测次数
 *  @param end    [出参] 最后一次探测结束的时刻
 *  @param dumps  [出参] 其中判定为写转储的次数
 */
void hwdFailProbes(HangWatchPolicy& policy, int64_t start, int count, int64_t& end, int& dumps)
{
    dumps = 0;
    int64_t t = start;
    for (int i = 0; i < count; ++i)
    {
        if (policy.OnProbe(false, t, t + kHwdProbeMs) == hang_action_write_dump)
            ++dumps;
        t += kHwdProbeMs;
    }
    end = t;
}

//HWD4 测试窗口的窗口过程：收到 kHwd4MsgHang 就卡住一段时间，其余交给默认处理
LRESULT CALLBACK hwd4WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == kHwd4MsgHang)
    {
        ::Sleep(kHwd4HangMs);
        return 0;
    }
    return ::DefWindowProcW(hwnd, msg, wParam, lParam);
}

//HWD4 测试线程的共享状态
struct Hwd4Target
{
    HWND   m_hwnd;     //测试窗口（测试线程建立，建好之前为 NULL）
    HANDLE m_ready;    //测试窗口建好后置位
};

//HWD4 测试线程：建一个仅消息窗口并跑消息循环，直到窗口被销毁
void hwd4ThreadProc(Hwd4Target* target)
{
    WNDCLASSW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.lpfnWndProc = &hwd4WndProc;
    wc.hInstance = ::GetModuleHandleW(NULL);
    wc.lpszClassName = kHwd4WndClass;
    ::RegisterClassW(&wc);
    target->m_hwnd = ::CreateWindowW(kHwd4WndClass, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, wc.hInstance, NULL);
    ::SetEvent(target->m_ready);
    if (target->m_hwnd == NULL)
        return;
    MSG msg;
    while (::GetMessageW(&msg, NULL, 0, 0) > 0)
    {
        ::TranslateMessage(&msg);
        ::DispatchMessageW(&msg);
        if (!::IsWindow(target->m_hwnd))
            break;
    }
}

//HWD4：删除目录下的全部文件，再删目录本身
void hwd4RemoveDir(const std::wstring& dir)
{
    WIN32_FIND_DATAW fd;
    HANDLE h = ::FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE)
    {
        do
        {
            if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
                ::DeleteFileW((dir + L"\\" + fd.cFileName).c_str());
        } while (::FindNextFileW(h, &fd));
        ::FindClose(h);
    }
    ::RemoveDirectoryW(dir.c_str());
}

} // namespace

TEST_CASE("HWD1 no dump while responsive or not hung long enough", "[hangwatch]")
{
    HangWatchPolicy policy(hwdConfig());
    //一直有响应
    for (int i = 0; i < 10; ++i)
        CHECK(policy.OnProbe(true, i * 1000, i * 1000 + 1) == hang_action_none);
    CHECK(policy.HangDurationMs(10000) == 0);
    //失败两次（不够 3 次），即便持续时间已超过阈值也不写
    CHECK(policy.OnProbe(false, 20000, 20000 + kHwdDumpAfterMs) == hang_action_none);
    CHECK(policy.OnProbe(false, 21000, 21000 + kHwdDumpAfterMs) == hang_action_none);
    //恢复：失败次数不够，不算一次无响应，不记恢复日志
    CHECK(policy.OnProbe(true, 23000, 23001) == hang_action_none);
    //失败 3 次但总共只有 600 毫秒，不够 1000
    CHECK(policy.OnProbe(false, 30000, 30200) == hang_action_none);
    CHECK(policy.OnProbe(false, 30200, 30400) == hang_action_none);
    CHECK(policy.OnProbe(false, 30400, 30600) == hang_action_none);
    CHECK(policy.HangDurationMs(30600) == 600);
    CHECK(policy.DumpsWritten() == 0);
}

TEST_CASE("HWD2 one dump per hang, re-armed after recovery", "[hangwatch]")
{
    HangWatchPolicy policy(hwdConfig());
    int64_t end = 0;
    int dumps = 0;
    //失败 6 次、共 2400 毫秒：第 3 次时满 1200 毫秒、满 3 次，写一次；之后同一次无响应不再写
    hwdFailProbes(policy, 1000, 6, end, dumps);
    CHECK(dumps == 1);
    CHECK(policy.DumpsWritten() == 1);
    CHECK(policy.HangDurationMs(end) == 6 * kHwdProbeMs);
    //恢复：记恢复日志，记下这次无响应的时长
    CHECK(policy.OnProbe(true, end, end + 1) == hang_action_log_recovered);
    CHECK(policy.LastHangMs() == 6 * kHwdProbeMs);
    CHECK(policy.HangDurationMs(end + 1) == 0);
    //再次无响应：重新计时，可以再写一次
    hwdFailProbes(policy, end + 1000, 4, end, dumps);
    CHECK(dumps == 1);
    CHECK(policy.DumpsWritten() == 2);
}

TEST_CASE("HWD3 stops writing dumps after the limit", "[hangwatch]")
{
    HangWatchPolicy policy(hwdConfig());
    int64_t end = 0;
    int dumps = 0;
    int64_t t = 0;
    for (int round = 0; round < kHwdMaxDumps + 2; ++round)
    {
        hwdFailProbes(policy, t, 5, end, dumps);
        CHECK(dumps == (round < kHwdMaxDumps ? 1 : 0));
        //恢复的日志照常记
        CHECK(policy.OnProbe(true, end, end + 1) == hang_action_log_recovered);
        t = end + 1000;
    }
    CHECK(policy.DumpsWritten() == kHwdMaxDumps);
}

TEST_CASE("HWD4 watchdog writes a dump when a window hangs", "[hangwatch]")
{
    if (::IsDebuggerPresent())
    {
        SKIP("the watchdog does not write dumps under a debugger");
    }
    wchar_t tempDir[MAX_PATH] = { 0 };
    ::GetTempPathW(MAX_PATH, tempDir);
    const std::wstring dumpDir = std::wstring(tempDir) + L"flamingo_hwd4_test";
    hwd4RemoveDir(dumpDir);
    REQUIRE(::CreateDirectoryW(dumpDir.c_str(), NULL));

    Hwd4Target target;
    target.m_hwnd = NULL;
    target.m_ready = ::CreateEventW(NULL, TRUE, FALSE, NULL);
    std::thread uiThread(hwd4ThreadProc, &target);
    ::WaitForSingleObject(target.m_ready, kHwd4WaitMaxMs);
    ::CloseHandle(target.m_ready);
    REQUIRE(target.m_hwnd != NULL);

    HangWatchConfig cfg;
    cfg.m_probeIntervalMs = kHwd4IntervalMs;
    cfg.m_probeTimeoutMs = kHwd4TimeoutMs;
    cfg.m_dumpAfterMs = kHwd4DumpAfterMs;
    cfg.m_minFailedProbes = 2;
    cfg.m_maxDumps = 1;
    CHangWatchdog watchdog;
    REQUIRE(watchdog.Start(target.m_hwnd, dumpDir, cfg));
    //窗口正常时不写
    ::Sleep(kHwd4DumpAfterMs * 2);
    CHECK(watchdog.DumpsWritten() == 0);
    //让测试窗口卡住
    ::PostMessageW(target.m_hwnd, kHwd4MsgHang, 0, 0);
    for (DWORD waited = 0; waited < kHwd4WaitMaxMs && watchdog.DumpsWritten() == 0; waited += kHwd4PollMs)
        ::Sleep(kHwd4PollMs);
    CHECK(watchdog.DumpsWritten() == 1);
    const std::wstring path = watchdog.LastDumpPath();
    REQUIRE_FALSE(path.empty());
    WIN32_FILE_ATTRIBUTE_DATA attr;
    REQUIRE(::GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &attr));
    CHECK((attr.nFileSizeLow != 0 || attr.nFileSizeHigh != 0));
    //等窗口恢复后再停，确认没有多写
    ::Sleep(kHwd4HangMs);
    watchdog.Stop();
    CHECK(watchdog.DumpsWritten() == 1);

    ::PostMessageW(target.m_hwnd, WM_CLOSE, 0, 0);
    uiThread.join();
    ::UnregisterClassW(kHwd4WndClass, ::GetModuleHandleW(NULL));
    hwd4RemoveDir(dumpDir);
}
