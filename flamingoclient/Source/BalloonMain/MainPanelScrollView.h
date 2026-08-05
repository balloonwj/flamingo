/**
 *  MainPanelScrollView.h
 *
 *  主面板三个列表面板用的滚动视口 —— 继承 balloonui 的 DuiScrollView。
 *
 *  滚动策略：内置滚动条宽度设为 0（不占布局、内容用满宽度），改为在
 *  OnPaint 里自绘一条悬浮式（overlay）细滚动条 —— 平时不画，滚轮滚动或
 *  鼠标移到滚动条附近时淡入出现；停止操作 kScrollbarIdleHideMs 毫秒后淡出
 *  消失。滑块可以用鼠标按住拖动。对应开发约定文档 "UI 约定"第 1 条。
 *
 *  滑块与命中带的坐标由 OverlayScrollBarGeometry 计算，其中包含"给窗口缩放
 *  边框让位"的处理 —— 不让位的话整条滚动条都落在 DuiFrameWindow 的
 *  WM_NCHITTEST 缩放边框里，鼠标只会变成左右双向箭头、滑块根本抓不住。
 *
 *  淡入淡出与自动隐藏由宿主窗口（CBalloonMainPanel）的 WM_TIMER 驱动：本类
 *  在需要动画时用 kScrollbarAnimTimerId 在宿主 HWND 上起一个 pulse 定时器，
 *  宿主每次收到 WM_TIMER 就把三个面板各推进一帧（TickScrollBarFade），三个
 *  都不再需要动画时由宿主 KillTimer。之所以不放在本类里收尾，是因为一个
 *  HWND 上同 id 的定时器只有一份，三个面板共用，只能由宿主统一管。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL。
 *
 *  balloonwj@qq.com   2026-05-21
 */

#pragma once

#include "../../balloonui/balloonui/Controls/Window/DuiScrollBar.h"
#include "OverlayScrollBarGeometry.h"

namespace balloonmain {

// 滚动条淡入淡出的 pulse 定时器 id（挂在主面板 HWND 上，WM_TIMER）。
const UINT_PTR kScrollbarAnimTimerId = 0xB001;

// pulse 定时器间隔（毫秒）。约 60Hz，与 balloonui DuiAnimMgr 同一档位。
const UINT kScrollbarAnimIntervalMs = 16;

// 停止滚动 / 移开鼠标后多久开始淡出（毫秒）。与 balloonui DuiScrollBar 的
// auto-hide 时序对齐（约 800ms 空闲后淡出）。
const int kScrollbarIdleHideMs = 800;

// 淡入 / 淡出一次的时长（毫秒）。同样对齐 DuiScrollBar 的 200ms。
const int kScrollbarFadeMs = 200;

// 滑块完全显现时的不透明度（0-255）。半透明深灰，叠在内容上方。
const int kScrollbarThumbAlpha = 140;

/**
 *  MainPanelScrollView —— 带自动隐藏、可拖动的悬浮式滚动条的列表滚动视口。
 */
class MainPanelScrollView : public balloonwjui::DuiScrollView
{
public:
    MainPanelScrollView();

    /**
     *  立即隐藏滚动条（跳过淡出动画）。切换 Tab 一类"面板整体换人"的场景用，
     *  免得隐藏的面板还在后台占着 pulse 定时器慢慢淡。
     */
    void HideScrollBarNow();

    /**
     *  推进一帧空闲计时与淡入 / 淡出动画。由宿主窗口的 WM_TIMER 调用。
     *    elapsedMs：距上一帧过去的毫秒数（宿主按定时器间隔传入）。
     *  返回：true 表示还需要继续收到下一帧（动画未结束、或空闲计时还在跑）；
     *        false 表示本面板已经稳定，宿主可据此决定是否 KillTimer。
     *  副作用：透明度变化时调用 Invalidate 重绘本视口。
     */
    bool TickScrollBarFade(int elapsedMs);

    /**
     *  设置滚动条右侧要给窗口缩放边框让出的宽度（物理像素，>= 0）。
     *  由宿主窗口在建好界面以及窗口尺寸 / DPI 变化后调用，取值来自
     *  ComputeResizeBorderInsetPx。默认 0（不让位）。
     *  副作用：宽度确有变化时调用 Invalidate 重绘本视口。
     */
    void SetEdgeInsetPx(int px);

    // 取当前的让位宽度（物理像素）。
    int GetEdgeInsetPx() const { return m_edgeInsetPx; }

    // ---- DuiControl / DuiScrollView 覆写 ----

    // 命中测试：落在滚动条命中带上时认领为本控件自己，否则照常下沉到内容子树。
    // 不认领的话，命中的会是铺满视口的 DuiTreeView 行，滑块永远抓不住。
    balloonwjui::DuiControl* HitTest(POINT ptHostClient) override;

    // 滚轮滚动时令滚动条淡入显现，并重置空闲计时。
    bool OnMouseWheel(POINT pt, short zDelta, UINT mkFlags) override;

    // 拖动中按鼠标位置改滚动位置；否则维护"是否悬停在滚动条上"并保持其可见。
    bool OnMouseMove(POINT pt, UINT mkFlags) override;

    // 鼠标离开本控件：清掉悬停标记，让空闲计时重新开始跑。
    bool OnMouseLeave() override;

    // 按在滑块上开始拖动（抓 capture）；按在命中带其余位置只唤出滚动条并把
    // 点击消费掉，不让它穿透到底下的列表行。
    bool OnLButtonDown(POINT pt, UINT mkFlags) override;

    // 结束拖动、释放 capture、重启空闲计时。
    bool OnLButtonUp(POINT pt, UINT mkFlags) override;

    // 先画内容（DuiScrollView 基类），再按当前透明度叠画滑块。
    void OnPaint(HDC hdc, const RECT& rcDirty) override;

private:
    // 按当前视口 / 内容高 / 滚动位置 / 让位宽度算滑块与命中带。
    OverlayScrollBarMetrics CurrentScrollBar() const;

    // 令滚动条进入"显现"态：重置空闲计时并确保 pulse 定时器在跑。
    void WakeScrollBar();

    // 在宿主 HWND 上（重新）起 pulse 定时器。宿主未就绪时静默返回。
    void StartAnimPulse();

private:
    int  m_alpha;            // 滑块当前不透明度，0 到 kScrollbarThumbAlpha
    bool m_awake;            // 目标态：true 淡入到全不透明，false 淡出到全透明
    int  m_idleMs;           // 处于显现态且无交互后累积的空闲毫秒数
    bool m_hoverBar;         // 鼠标当前是否停在滚动条命中带上（悬停期间不淡出）
    bool m_dragging;         // 是否正在拖动滑块
    int  m_dragGrabOffsetY;  // 按下点到滑块顶边的距离（像素），拖动期间保持不变
    int  m_edgeInsetPx;      // 右侧给窗口缩放边框让出的宽度（物理像素）
};

/**
 *  把滚轮事件转发给 from 的祖先链里最近的滚动视口。
 *
 *  DuiHost 的滚轮只投递给"最深命中控件"、不向上冒泡（见 DuiHost::
 *  OnMouseWheel）；而列表项 / 分组标题铺满了滚动视口，滚轮几乎总是落在
 *  它们身上。故这些控件须在自己的 OnMouseWheel 里调用本函数，把滚轮
 *  交还祖先里的滚动视口。
 *
 *    from：发起转发的控件（通常是列表项本身）。
 *  返回：祖先里的滚动视口消费了滚轮则返 true，否则 false。
 */
bool ForwardWheelToScrollView(balloonwjui::DuiControl* from,
                              POINT pt, short zDelta, UINT mkFlags);

} // namespace balloonmain
