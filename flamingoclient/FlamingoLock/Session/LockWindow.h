/**
 *  一块显示器上的锁屏窗口：无边框、置顶、不在任务栏显示，盖住整块显示器（含任务栏）。
 *  持有自己的 Direct2D 渲染目标、动画场景与叠加层绘制器；鼠标、键盘、显示器变化与系统关机等事件
 *  转给 ILockWindowSink（锁屏会话）处理。关闭请求（WM_CLOSE、SC_CLOSE、Alt+F4）一律忽略，输入法关闭。
 *
 *  须在锁屏线程上、Per-Monitor-V2 的 DPI 上下文里创建，客户区坐标即物理像素。
 *  2026-10-07 起设置窗口的「预览」（CLockPreviewSession）也用本类在一块显示器上全屏播放动画。
 *
 *  典型用法（锁屏会话）：
 *      w.Create(index, monitorRect, scale, sink);
 *      w.SetScene(CreateLockScene(anim), env);
 *      每帧：w.Render(env, overlayFrame, dt, wallMs);
 *      w.DestroyLockWindow();
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include <memory>

#include "../stdafx.h"
#include "../Render/LockScene.h"
#include "../Render/LockOverlayRenderer.h"

namespace flamingolock {

//锁屏窗口私有消息：请锁屏会话重新把锁屏窗口置顶并取回前台（由前台变化的事件钩子投递，避免在钩子回调里直接操作窗口）
const UINT kMsgLockReassert = WM_APP + 0x100;

/**
 *  锁屏窗口把事件交给谁处理。由锁屏会话实现，所有方法都在锁屏线程上调用。
 */
class ILockWindowSink
{
public:
    // 鼠标左键按下。index：窗口序号；pt：客户区坐标
    virtual void OnLockMouseDown(int index, POINT pt) = 0;
    // 鼠标移动。index：窗口序号；pt：客户区坐标
    virtual void OnLockMouseMove(int index, POINT pt) = 0;
    // 鼠标离开窗口
    virtual void OnLockMouseLeave(int index) = 0;
    // 字符输入（WM_CHAR）
    virtual void OnLockChar(int index, wchar_t ch) = 0;
    // 按键按下（WM_KEYDOWN），用于回车、退格、Esc
    virtual void OnLockKeyDown(int index, UINT vk) = 0;
    // 显示器布局或分辨率变化（WM_DISPLAYCHANGE）
    virtual void OnLockDisplayChange() = 0;
    // 系统正在注销或关机（WM_ENDSESSION 且 wParam 为 TRUE）
    virtual void OnLockEndSession() = 0;
    // 需要重新置顶（kMsgLockReassert）
    virtual void OnLockReassert() = 0;

    //以下三项 2026-10-07 为设置窗口的「预览」加入，锁屏会话不需要，默认什么都不做

    // 系统键按下（WM_SYSKEYDOWN，如 Alt、F10、Alt+F4）。窗口照旧忽略这些键（不交给系统默认处理），只是告知接收者。
    // index：窗口序号；vk：虚拟键码
    virtual void OnLockSysKeyDown(int index, UINT vk)
    {
        (void)index;
        (void)vk;
    }
    // 鼠标右键或中键按下。index：窗口序号；pt：客户区坐标
    virtual void OnLockOtherButtonDown(int index, POINT pt)
    {
        (void)index;
        (void)pt;
    }
    // 窗口失去激活（WM_ACTIVATE 的 WA_INACTIVE）。index：窗口序号
    virtual void OnLockDeactivate(int index)
    {
        (void)index;
    }

protected:
    ~ILockWindowSink()
    {
    }
};

/**
 *  一块显示器上的锁屏窗口。
 */
class CLockWindow : public CWindowImpl<CLockWindow, CWindow, CWinTraits<WS_POPUP, WS_EX_TOPMOST | WS_EX_TOOLWINDOW>>
{
public:
    DECLARE_WND_CLASS_EX(L"FlamingoLockScreenWindow", 0, -1)

    CLockWindow();
    ~CLockWindow();

    /**
     *  创建并显示窗口，盖住给定的显示器矩形。
     *  @param index       窗口序号（0 起），事件回调时带回
     *  @param monitorRect 显示器矩形（虚拟桌面坐标，物理像素）
     *  @param scale       该显示器的缩放比例（DPI / 96）
     *  @param sink        事件接收者（借用，须在窗口销毁前一直有效）
     *  @return 成功返回 true
     */
    bool CreateLockWindow(int index, const RECT& monitorRect, float scale, ILockWindowSink* sink);

    /**
     *  销毁窗口并释放渲染目标与场景。重复调用安全。
     */
    void DestroyLockWindow();

    /**
     *  换一个动画场景（锁屏开始、轮流播放到点时）。
     *  @param scene 新场景（本对象接管所有权；为 NULL 时不画动画，只画深色底）
     *  @param env   公共对象
     */
    void SetScene(ILockScene* scene, const SceneEnv& env);

    /**
     *  推进动画并画一帧。渲染目标丢失时自动重建。
     *  @param env    公共对象
     *  @param frame  叠加层状态（m_mouse 等由本窗口填）
     *  @param dt     距上一帧的秒数
     *  @param wallMs 当前时刻（自 1970-01-01 起的毫秒，UTC）
     */
    void Render(const SceneEnv& env, OverlayFrame frame, float dt, uint64_t wallMs);

    /**
     *  叠加层布局，供命中测试。
     */
    const LockOverlayLayout& Layout() const;

    /**
     *  输错密码时让密码框抖动。
     *  @param nowMs 单调时刻（毫秒）
     */
    void Shake(uint64_t nowMs);

    /**
     *  窗口序号。
     */
    int Index() const;

    /**
     *  显示器矩形（虚拟桌面坐标）。
     */
    const RECT& MonitorRect() const;

    /**
     *  窗口所在显示器的缩放比例（DPI / 96），创建时确定。
     */
    float Scale() const;

    BEGIN_MSG_MAP(CLockWindow)
        MESSAGE_HANDLER(WM_LBUTTONDOWN, OnLButtonDown)
        MESSAGE_HANDLER(WM_MOUSEMOVE, OnMouseMove)
        MESSAGE_HANDLER(WM_MOUSELEAVE, OnMouseLeave)
        MESSAGE_HANDLER(WM_CHAR, OnChar)
        MESSAGE_HANDLER(WM_KEYDOWN, OnKeyDown)
        MESSAGE_HANDLER(WM_SYSKEYDOWN, OnSysKeyDown)
        MESSAGE_HANDLER(WM_RBUTTONDOWN, OnOtherButtonDown)
        MESSAGE_HANDLER(WM_MBUTTONDOWN, OnOtherButtonDown)
        MESSAGE_HANDLER(WM_ACTIVATE, OnActivate)
        MESSAGE_HANDLER(WM_SYSCHAR, OnSwallow)
        MESSAGE_HANDLER(WM_CLOSE, OnSwallow)
        MESSAGE_HANDLER(WM_SYSCOMMAND, OnSysCommand)
        MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
        MESSAGE_HANDLER(WM_PAINT, OnPaint)
        MESSAGE_HANDLER(WM_SETCURSOR, OnSetCursor)
        MESSAGE_HANDLER(WM_MOUSEACTIVATE, OnMouseActivate)
        MESSAGE_HANDLER(WM_DISPLAYCHANGE, OnDisplayChange)
        MESSAGE_HANDLER(WM_QUERYENDSESSION, OnQueryEndSession)
        MESSAGE_HANDLER(WM_ENDSESSION, OnEndSession)
        MESSAGE_HANDLER(kMsgLockReassert, OnReassert)
    END_MSG_MAP()

private:
    LRESULT OnLButtonDown(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnMouseMove(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnMouseLeave(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnChar(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnKeyDown(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnSysKeyDown(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnOtherButtonDown(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnActivate(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnSwallow(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnSysCommand(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnEraseBkgnd(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnPaint(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnSetCursor(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnMouseActivate(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnDisplayChange(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnQueryEndSession(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnEndSession(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnReassert(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    // 确保渲染目标存在；失败返回 false
    bool EnsureTarget(const SceneEnv& env);
    // 释放渲染目标以及场景、叠加层里与它绑定的资源
    void DiscardTarget();

private:
    int                                   m_index;          // 窗口序号
    RECT                                  m_monitorRect;    // 显示器矩形（虚拟桌面坐标）
    int                                   m_width;          // 客户区宽（物理像素）
    int                                   m_height;         // 客户区高（物理像素）
    float                                 m_scale;          // 缩放比例
    ILockWindowSink*                      m_sink;           // 事件接收者（借用）
    CComPtr<ID2D1HwndRenderTarget>        m_target;         // 渲染目标；设备丢失后重建
    std::unique_ptr<ILockScene>           m_scene;          // 当前动画场景
    bool                                  m_sceneInited;    // 场景是否已按尺寸初始化
    CLockOverlayRenderer                  m_overlay;        // 叠加层绘制器
    POINT                                 m_mouse;          // 鼠标在客户区的位置；不在窗口内时为 (-1, -1)
    bool                                  m_trackingLeave;  // 是否已登记鼠标离开通知
};

} // namespace flamingolock
