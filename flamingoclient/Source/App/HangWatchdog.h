/**
 *  界面线程无响应检测，HangWatchdog.h
 *
 *  背景：bugs.md BUG-120。客户端偶发卡死（界面无响应），日志里没有任何线索，只能靠人在卡死时手工附加调试器取堆栈，
 *  而用户往往已经结束了进程。CHangWatchdog 在自己的后台线程上，每隔 m_probeIntervalMs 向界面线程的一个窗口发一条
 *  WM_NULL（SendMessageTimeout，最多等 m_probeTimeoutMs）。界面线程只要还在处理消息——包括弹着消息框、菜单这类
 *  嵌套消息循环——探测就会成功；只有真正卡住时才失败。连续失败至少 m_minFailedProbes 次、且持续 m_dumpAfterMs 以上时，
 *  写一份带全部线程堆栈的转储（exe 目录下 Users\dump，文件名 Flamingo_hang_年月日_时分秒.dmp），再记一条日志；
 *  界面线程恢复后记一条恢复日志。同一次无响应只写一份，一个进程最多写 m_maxDumps 份；在调试器里运行时不写。
 *  先写转储、后记日志：卡死的原因可能就在日志模块里，记日志这一步可能会卡住检测线程，而转储那时已经写好了。
 *
 *  判定逻辑在纯逻辑类 HangWatchPolicy 里，可单独测试；CHangWatchdog 负责线程、探测与写文件。
 *
 *  典型用法（Startup.cpp）：
 *      balloonapp::CHangWatchdog watchdog;
 *      watchdog.Start(g_hwndOwner, PrepareCrashDumpDirectory(), balloonapp::HangWatchConfig());
 *      ...消息循环...
 *      watchdog.Stop();
 *
 *  balloonwj@qq.com   2026-10-07
 */
#pragma once

#include <windows.h>
#include <stdint.h>
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace balloonapp {

//两次探测之间的间隔（毫秒）
const int kHangProbeIntervalMs = 1000;
//一次探测等界面线程处理 WM_NULL 的上限（毫秒）
const int kHangProbeTimeoutMs = 2000;
//界面线程连续无响应多久（毫秒）写一份转储
const int kHangDumpAfterMs = 15000;
//至少连续失败几次才算无响应：休眠唤醒等一次性的长耗时只会让一次探测失败，不应写转储
const int kHangMinFailedProbes = 3;
//一个进程最多写几份无响应转储，避免反复卡顿时占满磁盘
const int kHangMaxDumps = 3;

//一次探测之后要做的事（HangWatchPolicy::OnProbe 的返回值）
enum HangWatchAction
{
    hang_action_none          = 0,   //不用做任何事：界面线程正常；或无响应还不够久、失败次数不够；或这次无响应已写过转储
    hang_action_write_dump    = 1,   //无响应已达阈值且这次还没写过：写一份转储并记日志
    hang_action_log_recovered = 2,   //界面线程在一次确认的无响应（失败次数够了）之后恢复：记一条恢复日志
};

//检测的参数；默认值即上面的几个常量，测试可以改小
struct HangWatchConfig
{
    int m_probeIntervalMs;   //两次探测之间的间隔，毫秒
    int m_probeTimeoutMs;    //一次探测的超时，毫秒
    int m_dumpAfterMs;       //连续无响应多久写转储，毫秒
    int m_minFailedProbes;   //至少连续失败几次才算无响应
    int m_maxDumps;          //一个进程最多写几份转储

    HangWatchConfig();
};

/**
 *  探测结果驱动的判定（纯逻辑，只在检测线程上使用，不加锁）。
 *  一次「无响应」从第一次失败的探测开始，到下一次成功的探测为止。
 */
class HangWatchPolicy
{
public:
    explicit HangWatchPolicy(const HangWatchConfig& config);

    /**
     *  交一次探测的结果。
     *  @param responded    界面线程在超时内处理了探测消息
     *  @param probeStartMs 这次探测开始的时刻（毫秒，单调递增）
     *  @param probeEndMs   这次探测结束的时刻（毫秒），不小于 probeStartMs
     *  @return 要做的事，取值见 HangWatchAction；返回写转储时内部已把已写份数加一
     */
    int OnProbe(bool responded, int64_t probeStartMs, int64_t probeEndMs);

    /**
     *  当前这次无响应已经持续了多久。
     *  @param nowMs 当前时刻（毫秒）
     *  @return 毫秒；不在无响应中时为 0
     */
    int64_t HangDurationMs(int64_t nowMs) const;

    //刚结束的那次无响应持续了多久（毫秒）；OnProbe 返回 hang_action_log_recovered 之后读取才有意义
    int64_t LastHangMs() const;

    //已经判定写过几份转储
    int DumpsWritten() const;

private:
    HangWatchConfig m_config;           //检测参数
    int64_t         m_hangStartMs;      //当前这次无响应的开始时刻；不在无响应中时为 -1
    int             m_failedProbes;     //当前这次无响应里连续失败的探测次数
    bool            m_dumpedThisHang;   //当前这次无响应是否已写过转储
    int             m_dumps;            //本进程已写过的转储份数
    int64_t         m_lastHangMs;       //刚结束的那次无响应的时长，毫秒
};

/**
 *  界面线程无响应检测：后台线程定期探测，按 HangWatchPolicy 的判定写转储与日志。
 *  Start 与 Stop 在同一线程（通常是界面线程）上调用；DumpsWritten / LastDumpPath 可在任意线程读取。
 */
class CHangWatchdog
{
public:
    CHangWatchdog();

    //析构时停止检测线程
    ~CHangWatchdog();

    CHangWatchdog(const CHangWatchdog&) = delete;
    CHangWatchdog& operator=(const CHangWatchdog&) = delete;

    /**
     *  开始检测。
     *  @param target  界面线程上的一个窗口（探测消息发给它），不拥有；窗口被销毁后检测线程自行结束
     *  @param dumpDir 转储目录（须已存在，不带结尾的反斜杠）；为空时只记日志、不写转储
     *  @param config  检测参数
     *  @return 已开始返回 true；已在检测或 target 不是窗口返回 false
     */
    bool Start(HWND target, const std::wstring& dumpDir, const HangWatchConfig& config);

    //停止检测并等检测线程退出（检测线程正卡在写日志里时会一直等）；可重复调用
    void Stop();

    //已写出的转储份数（任意线程）
    int DumpsWritten() const;

    //最近一份转储的完整路径（任意线程）；还没写过时为空
    std::wstring LastDumpPath() const;

private:
    //检测线程的主体
    void threadProc();
    //写一份转储；成功时记下路径并返回 true
    bool writeDump();

    HWND                         m_target;        //探测的目标窗口（不拥有）
    std::wstring                 m_dumpDir;       //转储目录
    HangWatchConfig              m_config;        //检测参数
    std::unique_ptr<std::thread> m_thread;        //检测线程
    HANDLE                       m_stopEvent;     //通知检测线程退出的事件（手动复位），构造时创建、析构时关闭
    std::atomic<int>             m_dumps;         //已写出的转储份数
    mutable std::mutex           m_pathMutex;     //保护 m_lastDumpPath
    std::wstring                 m_lastDumpPath;  //最近一份转储的路径
};

} // namespace balloonapp
