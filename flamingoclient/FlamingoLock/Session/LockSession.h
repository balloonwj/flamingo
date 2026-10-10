/**
 *  一次锁屏：从盖住屏幕到输对密码解锁。在锁屏线程上运行。
 *
 *  负责：每块显示器建一个锁屏窗口并播放动画；安装低级键盘、鼠标钩子与前台变化的事件钩子；保持锁屏窗口在最上层；
 *  防止休眠、关屏与屏保；推进锁屏界面的状态机；比对密码；执行关机、重启；轮流播放时到点换动画；
 *  显示器变化时重建窗口。方案见 docs/锁屏方案.md 第 4.2 ~ 4.8 节。
 *
 *  典型用法（锁屏线程，见 CLockThread）：
 *      CLockSession s;
 *      if (s.Begin(owner, &verifier, choice)) { 每帧 s.RenderFrame(); 直到 !s.IsActive(); }
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "LockWindow.h"
#include "../Core/LockPassword.h"
#include "../Core/LockUiState.h"
#include "../Core/LockAnimationChoice.h"
#include "../../Source/UI/BalloonMain/KeepAwakeSession.h"

namespace flamingolock {

/**
 *  锁屏会话结束时通知谁。由锁屏线程实现。
 */
class ILockSessionOwner
{
public:
    /**
     *  密码正确，锁屏已经结束（窗口已销毁、钩子已摘除）。在锁屏线程上调用。
     */
    virtual void OnSessionUnlocked() = 0;

protected:
    ~ILockSessionOwner()
    {
    }
};

/**
 *  一次锁屏。非线程安全，所有方法都在锁屏线程上调用；同一时刻进程里最多一个处于进行中的会话。
 */
class CLockSession : public ILockWindowSink
{
public:
    CLockSession();
    ~CLockSession();

    CLockSession(const CLockSession&) = delete;
    CLockSession& operator=(const CLockSession&) = delete;

    /**
     *  开始锁屏：建锁屏窗口、装钩子、申请防休眠。
     *  @param owner    结束通知的接收者（借用）
     *  @param verifier 已设置好临时密码的校验器（借用，须在会话结束前一直有效；解锁后会被清除）
     *  @param choice   锁屏画面的选择
     *  @return 至少建出一块显示器的锁屏窗口返回 true；失败时已清理干净，返回 false
     */
    bool Begin(ILockSessionOwner* owner, CLockPasswordVerifier* verifier, const LockAnimationChoice& choice);

    /**
     *  推进并画一帧。处理推迟到帧开始时做的事（解锁、重建窗口、重新置顶），以及定时的事（无操作淡出、
     *  轮流播放换动画、防屏保）。锁屏线程的消息循环每轮调用一次。
     */
    void RenderFrame();

    /**
     *  结束锁屏并清理（摘钩子、销毁窗口、释放防休眠、清除密码）。重复调用安全；不通知 owner。
     */
    void End();

    /**
     *  是否处于锁屏中。
     */
    bool IsActive() const;

    // ---- ILockWindowSink ----
    void OnLockMouseDown(int index, POINT pt) override;
    void OnLockMouseMove(int index, POINT pt) override;
    void OnLockMouseLeave(int index) override;
    void OnLockChar(int index, wchar_t ch) override;
    void OnLockKeyDown(int index, UINT vk) override;
    void OnLockDisplayChange() override;
    void OnLockEndSession() override;
    void OnLockReassert() override;

private:
    // 摘除钩子、释放防休眠的电源请求（不销毁窗口，可在窗口的消息处理函数里调用）
    void ReleaseSystemResources();
    // 按当前显示器布局建锁屏窗口；返回建出的窗口数
    int CreateWindows();
    // 销毁全部锁屏窗口
    void DestroyWindows();
    // 让全部窗口播放指定的动画
    void ApplyAnimation(LockAnimation anim);
    // 窗口的场景公共对象（缩放比例按窗口所在显示器）
    SceneEnv EnvFor(const CLockWindow& w) const;
    // 比对密码框里的内容
    void TryUnlock();
    // 把锁屏窗口重新置顶，前台不是锁屏窗口时取回前台
    void Reassert();
    // 让某个锁屏窗口取得前台与键盘焦点
    void FocusWindow(int index);
    // 句柄是否为本会话的锁屏窗口
    bool IsLockWindow(HWND hwnd) const;
    // 按收到的按键事件维护修饰键状态（键盘钩子里调用）
    void TrackModifier(DWORD vk, bool keyUp);
    // 密码框下方此刻要显示的提示（冷却中显示倒计时）
    std::wstring CurrentMessage(uint64_t nowMs) const;

    static LRESULT CALLBACK KeyboardProc(int code, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK MouseProc(int code, WPARAM wParam, LPARAM lParam);
    static void CALLBACK WinEventProc(HWINEVENTHOOK hook, DWORD event, HWND hwnd, LONG idObject, LONG idChild,
                                      DWORD eventThread, DWORD eventTime);

private:
    ILockSessionOwner*                         m_owner;           // 结束通知的接收者（借用）
    CLockPasswordVerifier*                     m_verifier;        // 临时密码校验器（借用）
    LockAnimationChoice                        m_choice;          // 锁屏画面的选择
    LockAnimation                              m_anim;            // 当前播放的动画
    std::vector<std::unique_ptr<CLockWindow>>  m_windows;         // 每块显示器一个锁屏窗口
    CComPtr<ID2D1Factory>                      m_d2d;             // Direct2D 工厂（单线程型，只在锁屏线程上用）
    CComPtr<IDWriteFactory>                    m_dwrite;          // DirectWrite 工厂
    CLockUiState                               m_ui;              // 界面状态机
    CLockPasswordBox                           m_box;             // 密码框内容
    std::wstring                               m_message;         // 密码框下方的提示（输错等）；冷却倒计时另算
    balloonmain::CKeepAwakeSession             m_keepAwake;       // 防休眠、防关屏的电源请求
    HHOOK                                      m_keyboardHook;    // 低级键盘钩子
    HHOOK                                      m_mouseHook;       // 低级鼠标钩子（拦截合成的点击）
    HWINEVENTHOOK                              m_foregroundHook;  // 前台窗口变化的事件钩子
    HANDLE                                     m_oldDpiContext;   // 锁屏前线程的 DPI 上下文，结束时恢复；NULL 表示未切换
    bool                                       m_active;          // 是否处于锁屏中
    bool                                       m_unlockPending;   // 密码已比对通过，待下一帧开始时结束锁屏
    bool                                       m_rebuildPending;  // 显示器变化，待下一帧开始时重建窗口
    bool                                       m_reassertPending; // 前台被别的窗口夺走，待下一帧开始时重新置顶
    bool                                       m_ctrlDown;        // 钩子维护的 Ctrl 状态
    bool                                       m_altDown;         // 钩子维护的 Alt 状态
    bool                                       m_winDown;         // 钩子维护的 Win 状态
    uint64_t                                   m_lastFrameMs;     // 上一帧的时刻（单调毫秒）
    uint64_t                                   m_lastNudgeMs;     // 上次合成输入防屏保的时刻
    uint64_t                                   m_lastReassertMs;  // 上次重新置顶的时刻
    uint64_t                                   m_animStartMs;     // 当前动画开始播放的时刻（轮流播放用）
};

} // namespace flamingolock
