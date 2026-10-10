/**
 *  锁屏线程：锁屏模块的全部界面（设置临时密码的窗口、锁屏窗口）都在这条常驻线程上运行。
 *
 *  为什么单独开线程（docs/锁屏方案.md 第 4.2 节）：低级键盘钩子的回调在安装它的线程上执行，须在系统超时内返回，
 *  exe 的界面线程偶有较长的同步操作，放在那里会漏键；动画按帧绘制，不应受 exe 界面线程卡顿影响；
 *  登录前 exe 界面线程运行的是登录框的模态循环。线程第一次请求锁屏时创建，模块关闭时结束；
 *  balloonui 的单例（提示框、动画管理器等）因此始终在同一条线程上使用。
 *
 *  典型用法（CLockModule，宿主的界面线程上）：
 *      thread.RequestLock();     // 托盘「锁屏」
 *      thread.IsLocked();        // 任意线程
 *      thread.Stop();            // 模块关闭
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include <atomic>
#include <memory>

#include "LockSession.h"
#include "LockSetupDialog.h"
#include "../FlamingoLockApi.h"

namespace flamingolock {

/**
 *  锁屏线程。RequestLock、Stop 在宿主的界面线程上调用，IsLocked 任意线程可调，其余都在锁屏线程内部。
 */
class CLockThread : public ILockSessionOwner
{
public:
    CLockThread();
    ~CLockThread();

    CLockThread(const CLockThread&) = delete;
    CLockThread& operator=(const CLockThread&) = delete;

    /**
     *  登记宿主接口（读写锁屏画面选择、通知锁屏状态）。须在 RequestLock 之前调用。
     *  @param host 宿主接口（借用，须在 Stop 返回之前一直有效）
     */
    void SetHost(ILockHost* host);

    /**
     *  请求锁屏：线程不存在时先创建，再让它打开设置窗口。
     *  @return 已开始打开设置窗口返回 true；设置窗口已开着或正在锁屏时返回 false（设置窗口会被提到前面）
     */
    bool RequestLock();

    /**
     *  当前是否处于锁屏中。任意线程可调。
     */
    bool IsLocked() const;

    /**
     *  结束线程：关闭设置窗口、结束进行中的锁屏（摘钩子、销毁窗口），并等待线程退出。重复调用安全。
     */
    void Stop();

    // ---- ILockSessionOwner ----
    void OnSessionUnlocked() override;

private:
    // 线程入口
    static unsigned __stdcall ThreadEntry(void* param);
    // 线程主体：消息循环，锁屏期间与设置窗口全屏预览期间每轮画一帧
    void Run();
    // 处理一条线程消息（hwnd 为 NULL 的消息）
    void HandleThreadMessage(const MSG& msg);
    // 打开设置窗口
    void OpenSetup();
    // 设置窗口确认：取走结果，开始锁屏
    void BeginLockFromSetup();
    // 通知宿主锁屏状态变化
    void NotifyHost(bool locked);

private:
    ILockHost*                          m_host;          // 宿主接口（借用）
    HANDLE                              m_thread;        // 线程句柄；NULL 表示线程未创建
    DWORD                               m_threadId;      // 线程 id，投递线程消息用
    HANDLE                              m_readyEvent;    // 线程建好消息队列后置位，RequestLock 据此再投递消息
    std::atomic<bool>                   m_busy;          // 设置窗口开着或正在锁屏（拒绝重复请求）
    std::atomic<bool>                   m_locked;        // 正在锁屏
    std::unique_ptr<CLockSetupDialog>   m_setup;         // 设置临时密码的窗口（锁屏线程上创建与销毁）
    std::unique_ptr<CLockSession>       m_session;       // 进行中的锁屏；不在锁屏时为空
    CLockPasswordVerifier               m_verifier;      // 本次锁屏的临时密码（只存盐与摘要）
};

} // namespace flamingolock
