/**
 *  界面线程无响应检测的实现，见 HangWatchdog.h。
 *  balloonwj@qq.com   2026-10-07
 */
#include "stdafx.h"
#include "HangWatchdog.h"

#include <stdio.h>
#include <DbgHelp.h>

#include "../Common/Log/IULog.h"

#pragma comment(lib, "DbgHelp")

namespace balloonapp {

namespace {

//转储文件名的前缀：与崩溃转储的 "Flamingo_" 区分开，一眼看出是无响应时写的
const wchar_t* const kHangDumpPrefix = L"Flamingo_hang_";

//转储类型：普通小转储加线程信息、已卸载模块与句柄。带全部线程的调用栈，足以看出谁在等谁；不带堆内存，体积小
const MINIDUMP_TYPE kHangDumpType =
    static_cast<MINIDUMP_TYPE>(MiniDumpNormal | MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules |
                               MiniDumpWithHandleData);

//当前单调时刻（毫秒）
int64_t hangNowMs()
{
    return static_cast<int64_t>(::GetTickCount64());
}

//宽字符路径里取文件名部分（最后一个反斜杠之后）
std::wstring hangFileNameOf(const std::wstring& path)
{
    const size_t pos = path.find_last_of(L"\\/");
    return (pos == std::wstring::npos) ? path : path.substr(pos + 1);
}

//只含 ASCII 的宽字符串转成窄字符串（用于日志；非 ASCII 字符写成 '?'）
std::string hangAsciiForLog(const std::wstring& text)
{
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i)
        out.push_back(text[i] < 0x80 ? static_cast<char>(text[i]) : '?');
    return out;
}

} // namespace

HangWatchConfig::HangWatchConfig()
    : m_probeIntervalMs(kHangProbeIntervalMs)
    , m_probeTimeoutMs(kHangProbeTimeoutMs)
    , m_dumpAfterMs(kHangDumpAfterMs)
    , m_minFailedProbes(kHangMinFailedProbes)
    , m_maxDumps(kHangMaxDumps)
{
}

HangWatchPolicy::HangWatchPolicy(const HangWatchConfig& config)
    : m_config(config)
    , m_hangStartMs(-1)
    , m_failedProbes(0)
    , m_dumpedThisHang(false)
    , m_dumps(0)
    , m_lastHangMs(0)
{
}

int HangWatchPolicy::OnProbe(bool responded, int64_t probeStartMs, int64_t probeEndMs)
{
    if (responded)
    {
        int action = hang_action_none;
        //失败次数够了才算一次确认的无响应，恢复时记日志；不够的（如休眠唤醒那一次）悄悄清掉
        if (m_hangStartMs >= 0 && m_failedProbes >= m_config.m_minFailedProbes)
        {
            m_lastHangMs = probeStartMs - m_hangStartMs;
            action = hang_action_log_recovered;
        }
        m_hangStartMs = -1;
        m_failedProbes = 0;
        m_dumpedThisHang = false;
        return action;
    }
    if (m_hangStartMs < 0)
        m_hangStartMs = probeStartMs;
    ++m_failedProbes;
    if (m_dumpedThisHang || m_dumps >= m_config.m_maxDumps)
        return hang_action_none;
    if (m_failedProbes < m_config.m_minFailedProbes || probeEndMs - m_hangStartMs < m_config.m_dumpAfterMs)
        return hang_action_none;
    m_dumpedThisHang = true;
    ++m_dumps;
    return hang_action_write_dump;
}

int64_t HangWatchPolicy::HangDurationMs(int64_t nowMs) const
{
    return (m_hangStartMs < 0) ? 0 : nowMs - m_hangStartMs;
}

int64_t HangWatchPolicy::LastHangMs() const
{
    return m_lastHangMs;
}

int HangWatchPolicy::DumpsWritten() const
{
    return m_dumps;
}

CHangWatchdog::CHangWatchdog()
    : m_target(NULL)
    , m_stopEvent(::CreateEventW(NULL, TRUE, FALSE, NULL))
    , m_dumps(0)
{
}

CHangWatchdog::~CHangWatchdog()
{
    Stop();
    if (m_stopEvent != NULL)
        ::CloseHandle(m_stopEvent);
}

bool CHangWatchdog::Start(HWND target, const std::wstring& dumpDir, const HangWatchConfig& config)
{
    if (m_thread || m_stopEvent == NULL || !::IsWindow(target))
        return false;
    m_target = target;
    m_dumpDir = dumpDir;
    m_config = config;
    ::ResetEvent(m_stopEvent);
    m_thread.reset(new std::thread(&CHangWatchdog::threadProc, this));
    LOG_INFO("hang watchdog: started, probe every %d ms (timeout %d ms), dump after %d ms and %d failed probes, "
             "at most %d dumps.", config.m_probeIntervalMs, config.m_probeTimeoutMs, config.m_dumpAfterMs,
             config.m_minFailedProbes, config.m_maxDumps);
    return true;
}

void CHangWatchdog::Stop()
{
    if (!m_thread)
        return;
    ::SetEvent(m_stopEvent);
    if (m_thread->joinable())
        m_thread->join();
    m_thread.reset();
}

int CHangWatchdog::DumpsWritten() const
{
    return m_dumps.load();
}

std::wstring CHangWatchdog::LastDumpPath() const
{
    std::lock_guard<std::mutex> guard(m_pathMutex);
    return m_lastDumpPath;
}

void CHangWatchdog::threadProc()
{
    HangWatchPolicy policy(m_config);
    bool debuggerNoted = false;
    for (;;)
    {
        //每轮先等一个间隔；等到退出事件就结束
        if (::WaitForSingleObject(m_stopEvent, static_cast<DWORD>(m_config.m_probeIntervalMs)) == WAIT_OBJECT_0)
            break;
        if (!::IsWindow(m_target))
            break;
        const int64_t start = hangNowMs();
        DWORD_PTR result = 0;
        const LRESULT sent = ::SendMessageTimeoutW(m_target, WM_NULL, 0, 0, SMTO_BLOCK | SMTO_ABORTIFHUNG,
                                                   static_cast<UINT>(m_config.m_probeTimeoutMs), &result);
        const int64_t end = hangNowMs();
        const int action = policy.OnProbe(sent != 0, start, end);
        switch (action)
        {
        //无响应达到阈值：先写转储，再记日志（日志模块本身可能就是卡住的原因）
        case hang_action_write_dump:
        {
            const int64_t hangMs = policy.HangDurationMs(end);
            if (::IsDebuggerPresent())
            {
                //在调试器里断下来也表现为无响应，不写转储
                if (!debuggerNoted)
                {
                    debuggerNoted = true;
                    LOG_INFO("hang watchdog: ui thread not responding for %lld ms under a debugger, no dump written.",
                             static_cast<long long>(hangMs));
                }
                break;
            }
            const bool written = writeDump();
            if (written)
            {
                LOG_ERROR("hang watchdog: ui thread not responding for %lld ms, dump written: Users\\dump\\%s.",
                          static_cast<long long>(hangMs), hangAsciiForLog(hangFileNameOf(LastDumpPath())).c_str());
            }
            else
            {
                LOG_ERROR("hang watchdog: ui thread not responding for %lld ms, failed to write a dump (error %lu).",
                          static_cast<long long>(hangMs), ::GetLastError());
            }
            break;
        }

        //确认过的无响应结束了：记下持续时长
        case hang_action_log_recovered:
            LOG_WARNING("hang watchdog: ui thread responded again after %lld ms.",
                        static_cast<long long>(policy.LastHangMs()));
            break;

        //界面线程正常，或无响应还在累计
        default:
            break;
        }
    }
}

bool CHangWatchdog::writeDump()
{
    if (m_dumpDir.empty())
        return false;
    SYSTEMTIME st;
    ::GetLocalTime(&st);
    wchar_t name[MAX_PATH] = { 0 };
    //年月日时分秒补零成定长，文件名按名称排序即按时间排序（与崩溃转储的命名一致）
    swprintf_s(name, MAX_PATH, L"%s%04u%02u%02u_%02u%02u%02u.dmp", kHangDumpPrefix, st.wYear, st.wMonth, st.wDay,
               st.wHour, st.wMinute, st.wSecond);
    const std::wstring path = m_dumpDir + L"\\" + name;
    HANDLE file = ::CreateFileW(path.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE)
        return false;
    const BOOL ok = ::MiniDumpWriteDump(::GetCurrentProcess(), ::GetCurrentProcessId(), file, kHangDumpType, NULL, NULL,
                                        NULL);
    const DWORD err = ::GetLastError();
    ::CloseHandle(file);
    if (!ok)
    {
        ::DeleteFileW(path.c_str());
        ::SetLastError(err);
        return false;
    }
    {
        std::lock_guard<std::mutex> guard(m_pathMutex);
        m_lastDumpPath = path;
    }
    m_dumps.fetch_add(1);
    return true;
}

} // namespace balloonapp
