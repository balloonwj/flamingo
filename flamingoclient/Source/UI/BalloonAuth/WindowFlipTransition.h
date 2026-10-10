/**
 *  登录窗与注册窗之间的「翻转」过渡（2026-10-07）：一个临时的分层弹出窗口盖在两个窗口的位置上，逐帧画出
 *  「起始窗口绕竖直中线压扁到 0、再展开成目标窗口」，约 350 毫秒（每一帧的几何与像素见 WindowFlipFrame.h）。
 *
 *  切换的顺序保证任一时刻至少有一个窗口看得见、前台不落到别的程序上：
 *      1. 截取起始窗口与目标窗口（目标窗口事先在目标位置隐藏着建好、排好版）的整窗画面；
 *      2. 显示过渡窗口并激活，第一帧与起始窗口一模一样，盖在它上面；
 *      3. 隐藏起始窗口（前台留在过渡窗口上）；
 *      4. 播放翻转；
 *      5. 显示并激活目标窗口；
 *      6. 销毁过渡窗口。
 *  系统关闭了「窗口内的动画」（SPI_GETCLIENTAREAANIMATION 为 FALSE）、起始窗口不可见或已最小化、截图或建过渡窗口
 *  失败时退回直接切换：先显示并激活目标窗口，再隐藏起始窗口。
 *
 *  截图：DuiHost 不处理 WM_PRINTCLIENT，隐藏窗口也没有屏幕内容可截。这里对窗口直接发一条 WM_PAINT，DuiHost::OnPaint
 *  在系统给出的待重画区域为空（隐藏窗口）或为整个客户区时都把整个客户区画进自己的后台缓冲，随后从
 *  DuiHost::GetBackBufferDC 拷出画面。这一做法依赖 balloonui 当前的绘制实现，由单元测试 WF6 看住；
 *  取不到后台缓冲时截图失败，切换退回直接切换，不影响功能。
 *
 *  过渡窗口不进任务栏（WS_EX_TOOLWINDOW）。登录窗、注册窗都有所有者窗口（登录窗归隐藏的 g_hwndOwner，注册窗归登录窗），
 *  本来就不在任务栏上显示，所以隐藏登录窗不会让任务栏按钮闪烁；托盘图标与它们无关。
 *
 *  典型用法（注册窗打开与返回时）：
 *      registerWnd 在登录窗上边缘对齐、水平居中处隐藏建好；
 *      SwitchDuiWindows(loginWnd, registerWnd, kFlipForward, IsFlipAnimationEnabled(), NULL);
 *
 *  balloonwj@qq.com   2026-10-07
 */
#pragma once

#include <windows.h>

#include "WindowFlipFrame.h"

namespace balloonwjui { class DuiHost; }

namespace balloonauth {

/**
 *  切换过程中的各个步骤，按发生顺序排列（观察者据此核对「谁可见」）。
 */
enum FlipStep
{
    kFlipStepCovered = 0,      // 过渡窗口已显示并激活，画着起始窗口的画面，盖在起始窗口上（只有翻转时才有）
    kFlipStepSourceHidden,     // 起始窗口已隐藏
    kFlipStepTargetShown,      // 目标窗口已显示并激活
    kFlipStepFinished,         // 过渡窗口已销毁（直接切换时没有过渡窗口），切换完成
};

/**
 *  切换过程的观察者：单元测试用它核对每一步之后窗口的可见状态，并取一帧翻转画面存图核对。生产代码传 NULL。
 */
class IFlipObserver
{
public:
    /**
     *  某一步刚完成。
     *  @param step 见 FlipStep
     */
    virtual void OnFlipStep(FlipStep step) = 0;

    /**
     *  过渡窗口刚换上一帧画面。
     *  @param t      线性进度 0~1
     *  @param canvas 这一帧的整块画面（借用，仅在回调期间有效）
     */
    virtual void OnFlipFrame(float t, const FlipBitmap& canvas) = 0;

protected:
    ~IFlipObserver()
    {
    }
};

/**
 *  系统是否允许窗口内的动画（「设置 → 轻松使用 → 在 Windows 中显示动画」，即 SPI_GETCLIENTAREAANIMATION）。
 *  @return 允许返回 true；查询失败时按允许处理
 */
bool IsFlipAnimationEnabled();

/**
 *  截取 DuiHost 窗口当前的整窗画面（DuiFrameWindow 的客户区就是整个窗口）。窗口可见或隐藏都可以。
 *  @param host 窗口（须已创建）
 *  @param out  接收画面（预乘 BGRA，alpha 一律为 255）
 *  @return 成功返回 true；窗口无效、尺寸为 0、取不到后台缓冲或 GDI 资源不足时返回 false
 */
bool CaptureDuiHostImage(balloonwjui::DuiHost& host, FlipBitmap& out);

/**
 *  一次翻转过渡：一个分层弹出窗口与它的逐帧播放。非线程安全，在界面线程上使用。
 */
class CWindowFlipTransition
{
public:
    CWindowFlipTransition();

    // 销毁过渡窗口（如果还在）
    ~CWindowFlipTransition();

    /**
     *  建过渡窗口并把第一帧（起始画面，原样大小）推上去，显示并激活它。
     *  @param owner     过渡窗口的所有者（一般取起始窗口的所有者），可为 NULL
     *  @param from      起始窗口的画面（借用，须在 End 之前一直有效）
     *  @param fromRect  起始窗口的屏幕矩形
     *  @param to        目标窗口的画面（借用，须在 End 之前一直有效）
     *  @param toRect    目标窗口的屏幕矩形
     *  @param dir       翻转方向
     *  @return 过渡窗口已显示返回 true；建窗或绘制资源失败返回 false（已清理干净）
     */
    bool Begin(HWND owner, const FlipBitmap& from, const RECT& fromRect, const FlipBitmap& to, const RECT& toRect,
               FlipDirection dir);

    /**
     *  阻塞地播放整个翻转（约 durationMs 毫秒），每帧之间用 DwmFlush 对齐屏幕刷新（桌面合成不可用时改为睡眠约 15 毫秒）。
     *  播放期间不处理消息：时长很短，也避免用户在过渡中途操作两个窗口。最后一帧恰为目标画面。
     *  @param observer   观察者，可为 NULL
     *  @param durationMs 时长（毫秒）
     */
    void Run(IFlipObserver* observer, uint32_t durationMs);

    /**
     *  销毁过渡窗口并释放绘制资源。重复调用安全。
     */
    void End();

    /**
     *  过渡窗口句柄；没有建出来或已销毁时为 NULL。
     */
    HWND Hwnd() const;

private:
    // 不可复制：持有窗口与 GDI 句柄
    CWindowFlipTransition(const CWindowFlipTransition&);
    CWindowFlipTransition& operator=(const CWindowFlipTransition&);

    // 按进度 t 算出一帧、画进画布并推给过渡窗口；observer 非空时回调
    bool PushFrame(float t, IFlipObserver* observer);

private:
    HWND              m_hwnd;        // 过渡窗口；NULL 表示没有
    HDC               m_memDC;       // 选入 m_dib 的内存设备上下文，UpdateLayeredWindow 的源
    HBITMAP           m_dib;         // 32 位自上而下的 DIB，尺寸与画布相同
    HGDIOBJ           m_oldBitmap;   // m_memDC 原来选入的位图，释放前换回
    void*             m_dibBits;     // m_dib 的像素指针（随 m_dib 释放）
    const FlipBitmap* m_from;        // 起始画面（借用）
    const FlipBitmap* m_to;          // 目标画面（借用）
    RECT              m_fromRect;    // 起始窗口的屏幕矩形
    RECT              m_toRect;      // 目标窗口的屏幕矩形
    RECT              m_canvasRect;  // 过渡画布（两个矩形的并集，屏幕坐标），过渡窗口就盖在这里
    FlipDirection     m_dir;         // 翻转方向
    FlipBitmap        m_canvas;      // 当前一帧的画面（软件绘制后拷进 m_dib）
};

/**
 *  开关一个窗口的 DWM 窗口动画（打开、关闭、最小化等系统动画）的函数原型。
 *  @param hwnd     窗口
 *  @param disabled true 表示关掉，false 表示恢复系统默认
 */
typedef void (*FlipTransitionSwitchFn)(HWND hwnd, bool disabled);

/**
 *  测试用：替换 SwitchDuiWindows 开关目标窗口 DWM 窗口动画所用的函数，以便断言调用的时机。默认实现调用
 *  DwmSetWindowAttribute(DWMWA_TRANSITIONS_FORCEDISABLED)。只在界面线程调用。
 *  @param fn 新的函数；NULL 表示恢复默认实现
 *  @return 替换之前的函数（从未替换过时为默认实现）
 */
FlipTransitionSwitchFn SetFlipTransitionSwitchForTest(FlipTransitionSwitchFn fn);

/**
 *  在两个 DuiHost 窗口之间切换：起始窗口隐藏，目标窗口显示并激活。目标窗口须事先摆到目标位置（可以隐藏着）。
 *  animate 为 true、起始窗口可见且没有最小化、截图与过渡窗口都成功时播放翻转，否则直接切换。窗口的可用 / 禁用状态
 *  由调用方管理（本函数不改）。播放翻转时，从盖上过渡窗口到目标窗口显示完毕这段时间里关掉目标窗口的 DWM 窗口动画：
 *  目标窗口第一次显示时系统会播放约 300 毫秒的淡入放大动画，而过渡窗口在目标窗口显示后立即销毁，不关掉的话那块
 *  区域会先露出后面的桌面、再浮现出窗口，看上去像闪了一下（2026-10-07 逐帧截屏实证，docs/登录注册窗口翻转切换.md）。
 *  @param from     起始窗口
 *  @param to       目标窗口
 *  @param dir      翻转方向
 *  @param animate  是否允许播放翻转（调用方一般传 IsFlipAnimationEnabled()）
 *  @param observer 观察者，可为 NULL
 *  @return 播放了翻转返回 true，直接切换返回 false。无论哪种，返回时目标窗口可见并已激活、起始窗口已隐藏
 */
bool SwitchDuiWindows(balloonwjui::DuiHost& from, balloonwjui::DuiHost& to, FlipDirection dir, bool animate,
                      IFlipObserver* observer);

} // namespace balloonauth
