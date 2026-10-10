/**
 *  NotificationSoundPlayer.cpp
 *
 *  通知提示音<u>实际出声</u>的实现（PlayNotifySound）。带 Win32（::sndPlaySound）与
 *  程序路径（Hootina::CPath::GetAppPath）依赖，<u>只编进 Flamingo 主工程</u>，不进
 *  FlamingoUnit 测试工程 —— 测试工程只引用 NotificationSound.cpp 里的纯逻辑。
 *
 *  播放走 ::sndPlaySound(..., SND_ASYNC)，由系统在后台线程出声，<u>不阻塞</u>调用线程
 *  （UI 线程）；故收消息 / 好友上线的 UI 处理函数里可直接调用，不会卡界面。再叠加
 *  SND_NODEFAULT，wav 缺失时保持安静、不退回 Windows 默认提示音。
 *
 *  本文件不 include stdafx.h，PCH 设 NotUsing。
 *
 *  balloonwj@qq.com   2026-06-05
 */

#include <windows.h>
#include <tchar.h>
#include <mmsystem.h>          // sndPlaySound / SND_ASYNC / SND_NODEFAULT
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>

#include "NotificationSound.h"
#include "../../Common/Util/Path.h"           // Hootina::CPath::GetAppPath（程序所在目录）
#include "../../Common/Log/IULog.h"          // LOG_INFO（记录一次提示音播放，便于"为何没响"排查）

#pragma comment(lib, "winmm.lib")   // sndPlaySound 所在库；自带链接、不依赖工程全局设置

namespace balloonmain {

namespace {

// ---- 自管理的循环播放 ------------------------------------------------------
//
// 为什么不用 PlaySound 的 SND_LOOP：SND_PURGE 这个停止标志 MSDN 明确标注对 PlaySound
// 不受支持（只对旧的 sndPlaySound 有效），一度写成 PlaySound(NULL, NULL, SND_PURGE) 结果
// 是个彻底的空操作，铃声停不掉。改为自己控制：后台线程按 wav 时长周期性重播一次性音效，
// 停止＝置标志＋唤醒线程＋join。这样"能不能停"完全取决于我们自己的代码，与 SND_LOOP 的
// 实现行为无关。
//
// 实测已验证有效（日志 20260801102108_18372.log）：起循环后 4.5 秒收到拒接推送，
// StopLoopSound 在同一毫秒内把线程停住，随后忙音正常播出，没有残留。

// 一次性音效的重播间隔取 wav 自身时长；取不到时长时用这个兜底值（毫秒）。
const unsigned int kLoopFallbackMs = 3000;

std::thread             g_loopThread;      // 循环线程；joinable 表示正在循环
std::mutex              g_loopMutex;       // 保护下面的停止标志与条件变量
std::condition_variable g_loopCv;          // 停止时立刻唤醒线程，不必等满一个周期
bool                    g_loopStop = true; // 是否请求停止（初值 true＝当前无循环）

// 读 wav 头算出播放时长（毫秒）。读失败返回 0，调用方用兜底间隔。
// 只解析标准 RIFF/WAVE 的 fmt 与 data 两个块，够用且不引入依赖。
unsigned int WavDurationMs(LPCTSTR path)
{
    HANDLE h = ::CreateFile(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return 0;

    unsigned char buf[64] = { 0 };
    DWORD got = 0;
    BOOL ok = ::ReadFile(h, buf, sizeof(buf), &got, NULL);
    ::CloseHandle(h);
    if (!ok || got < 44)
        return 0;

    // 标准 44 字节头：byteRate 在偏移 28，data 块长度在偏移 40。
    unsigned int byteRate = *(const unsigned int*)(buf + 28);
    unsigned int dataLen  = *(const unsigned int*)(buf + 40);
    if (byteRate == 0)
        return 0;
    return (unsigned int)((double)dataLen * 1000.0 / (double)byteRate);
}

// 提示音的全局拦截判定（锁屏期间不响），由 SetNotifySoundSuppressor 登记；NULL 表示没有登记
NotifySoundSuppressorFn g_soundSuppressor = NULL;

// 是否被全局拦截判定挡下
bool IsSoundSuppressed()
{
    return g_soundSuppressor != NULL && g_soundSuppressor();
}

} // anonymous namespace

void SetNotifySoundSuppressor(NotifySoundSuppressorFn fn)
{
    g_soundSuppressor = fn;
}

void PlayNotifySound(NotifySoundKind kind, bool muted)
{
    // 已"关闭所有声音"：直接静默返回，不出声。托盘菜单与系统设置写同一个静音位，
    // 两处任一关闭都会让 muted 为 true。锁屏期间同样不响（全局拦截判定）。
    if (muted || IsSoundSuppressed())
        return;

    const wchar_t* rel = NotifySoundRelativePath(kind);
    if (rel == NULL || rel[0] == L'\0')
        return;

    // 拼成绝对路径：程序目录 + "Sound\\xxx.wav"。GetAppPath 末尾带反斜杠。
    tstring strPath = Hootina::CPath::GetAppPath() + rel;

    // SND_ASYNC：异步播放、立即返回，不阻塞 UI 线程；SND_NODEFAULT：文件缺失时保持安静。
    ::sndPlaySound(strPath.c_str(), SND_ASYNC | SND_NODEFAULT);

    LOG_INFO(_T("播放通知提示音: kind=%d, 文件=%s。"), (int)kind, strPath.c_str());
}

void StartLoopSound(NotifySoundKind kind, bool muted)
{
    // 无论如何先停掉上一路：同一时刻只允许一路循环。
    StopLoopSound();

    if (muted || IsSoundSuppressed())
        return;

    // 非循环类（接通 / 结束 / 忙音等一次性音效）走 PlayNotifySound，不该进这里；
    // 误传时直接忽略，免得一次性音效被循环成永不停歇的噪音。
    if (!IsLoopSound(kind))
    {
        LOG_INFO(_T("StartLoopSound 收到非循环类音效, kind=%d, 已忽略。"), (int)kind);
        return;
    }

    const wchar_t* rel = NotifySoundRelativePath(kind);
    if (rel == NULL || rel[0] == L'\0')
        return;

    tstring strPath = Hootina::CPath::GetAppPath() + rel;

    unsigned int periodMs = WavDurationMs(strPath.c_str());
    if (periodMs == 0)
        periodMs = kLoopFallbackMs;

    {
        std::lock_guard<std::mutex> guard(g_loopMutex);
        g_loopStop = false;
    }

    // 线程按周期重播一次性音效。用条件变量等待而非 Sleep：停止时能立刻醒来，
    // 不会残留最多一个周期的滞后。
    g_loopThread = std::thread([strPath, periodMs]()
    {
        for (;;)
        {
            // 与消息提示音 PlayNotifySound 用同一套 API（sndPlaySound）：全进程只用一套，
            // 避免新旧两套接口在同一设备上互相接管、导致停止调用失效。
            ::sndPlaySound(strPath.c_str(), SND_ASYNC | SND_NODEFAULT);

            std::unique_lock<std::mutex> lk(g_loopMutex);
            if (g_loopCv.wait_for(lk, std::chrono::milliseconds(periodMs),
                                  []() { return g_loopStop; }))
            {
                break;   // 收到停止请求
            }
        }
        // 线程退出前把正在响的那一遍也掐掉（同一套 API 的停止方式）
        ::sndPlaySound(NULL, 0);
    });

    LOG_INFO(_T("开始循环播放通话音效: kind=%d, 周期=%ums, 文件=%s。"),
             (int)kind, periodMs, strPath.c_str());
}

void StopLoopSound()
{
    if (!g_loopThread.joinable())
    {
        // 当前没有循环在播：空操作。故可在各条收尾路径上都兜一次 ——
        // 铃声停不下来比多调一次糟得多。
        return;
    }

    {
        std::lock_guard<std::mutex> guard(g_loopMutex);
        g_loopStop = true;
    }
    g_loopCv.notify_all();
    g_loopThread.join();

    // 兜底：线程内已停过一次，这里再停一次，确保没有残留。
    ::sndPlaySound(NULL, 0);

    LOG_INFO(_T("停止循环通话音效。"));
}

} // namespace balloonmain
