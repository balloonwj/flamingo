/**
 *  设置临时密码窗口里「预览」按钮打开的全屏预览：在设置窗口所在的那块显示器上，用锁屏窗口（CLockWindow）
 *  播放下拉框里选中的动画，画面上方显示「按 Esc 或单击退出预览」。
 *
 *  与真正的锁屏（CLockSession）的区别：只盖一块显示器；不装键盘、鼠标钩子与前台钩子，不防休眠，不定时置顶，
 *  不要求密码；单击、按任意键（含 Esc）或窗口失去激活即结束；「随机」任取一种，「轮流播放」每 5 秒换一种。
 *  何时结束、何时换片、随机取哪一种由纯逻辑 CLockPreviewState 判定（可单测）。
 *
 *  帧驱动：锁屏线程的消息循环在预览进行中每轮调用一次 RenderFrame，并用 DwmFlush 对齐节拍（与锁屏相同）。
 *  结束推迟到下一帧开始时进行：结束的输入是在预览窗口自己的消息处理里收到的，那里不能销毁窗口。
 *
 *  典型用法（锁屏线程，设置窗口）：
 *      if (preview.Begin(this, m_hWnd, choice)) { ...每帧 preview.RenderFrame()... }
 *      // 结束时先回调 owner->OnPreviewEnding()（设置窗口恢复可用并取回前台），再销毁预览窗口
 *
 *  方案见 docs/锁屏方案.md 第 11 节。
 *  balloonwj@qq.com   2026-10-07
 */
#pragma once

#include <memory>
#include <string>

#include "LockWindow.h"
#include "../Core/LockPreviewState.h"

namespace flamingolock {

/**
 *  预览结束时通知谁。由设置窗口实现，在锁屏线程上调用。
 */
class ILockPreviewOwner
{
public:
    /**
     *  预览即将结束，预览窗口此刻还在。接收者在这里恢复自己可用并取回前台；随后预览会话才销毁预览窗口，
     *  这样前台不会因为预览窗口消失而落到别的程序上。
     */
    virtual void OnPreviewEnding() = 0;

protected:
    ~ILockPreviewOwner()
    {
    }
};

/**
 *  一次全屏预览。非线程安全，所有方法都在锁屏线程上调用。
 */
class CLockPreviewSession : public ILockWindowSink
{
public:
    CLockPreviewSession();
    ~CLockPreviewSession();

    CLockPreviewSession(const CLockPreviewSession&) = delete;
    CLockPreviewSession& operator=(const CLockPreviewSession&) = delete;

    /**
     *  开始预览：在 anchor 窗口所在的那块显示器上建一个全屏的预览窗口并取得前台。已在预览中时先结束旧的。
     *  @param owner  结束通知的接收者（借用，须在预览结束前一直有效）
     *  @param anchor 据以确定显示器的窗口（设置窗口）；无效时取主显示器
     *  @param choice 下拉框当前的选择
     *  @return 预览窗口建成返回 true；Direct2D 不可用或窗口建不出来时返回 false（已清理干净，不回调 owner）
     */
    bool Begin(ILockPreviewOwner* owner, HWND anchor, const LockAnimationChoice& choice);

    /**
     *  推进并画一帧；已决定结束时改为回调 owner->OnPreviewEnding() 并结束预览。锁屏线程的消息循环每轮调用一次。
     */
    void RenderFrame();

    /**
     *  立即结束预览并销毁窗口（不回调 owner）。重复调用安全。不得在预览窗口自己的消息处理里调用。
     */
    void End();

    /**
     *  是否处于预览中（窗口还在）。
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
    void OnLockSysKeyDown(int index, UINT vk) override;
    void OnLockOtherButtonDown(int index, POINT pt) override;
    void OnLockDeactivate(int index) override;

private:
    // 公共对象（缩放比例取预览窗口所在显示器的）
    SceneEnv Env() const;
    // 记录一次输入；决定结束时写日志
    void HandleInput(LockPreviewInput input, const char* what);

private:
    ILockPreviewOwner*             m_owner;        // 结束通知的接收者（借用）
    CLockPreviewState              m_state;        // 播哪一种、何时换片、何时结束
    std::unique_ptr<CLockWindow>   m_window;       // 预览窗口；不在预览时为空
    CComPtr<ID2D1Factory>          m_d2d;          // Direct2D 工厂（单线程型，只在锁屏线程上用）
    CComPtr<IDWriteFactory>        m_dwrite;       // DirectWrite 工厂
    std::wstring                   m_hint;         // 画面上方的退出提示（开始时按当前界面语言取好）
    uint64_t                       m_lastFrameMs;  // 上一帧的时刻（单调毫秒）
    uint64_t                       m_beginMs;      // 开始预览的时刻（单调毫秒），用于记录第一帧用了多久
    bool                           m_active;       // 是否处于预览中
    bool                           m_firstFramePending; // 第一帧还没画：画完后才开始计输入保护时间与轮流播放的计时
};

} // namespace flamingolock
