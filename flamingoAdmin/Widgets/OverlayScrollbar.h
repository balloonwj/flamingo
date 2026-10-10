/**
 *  flamingoAdmin —— 悬浮式 auto-hide 滚动条控件 OverlayScrollbar。
 *
 *  供「手写自绘的可滚动区域」（ComposeModal 投放目标选择器、AuditScreen
 *  表格 / 详情区等，见开发约定文档「UI 约定」）复用的滚动条子控件。
 *
 *  2026-10-04 起是 balloonui DuiScrollBar 的薄子类，外观与行为同 PC 客户端
 *  统一：悬浮于内容右缘、不占内容宽度；只画一根 5 像素宽的半透明灰色圆角
 *  滑块（离本控件右缘 2 像素），不画轨道；内容溢出时才出现，滚动或鼠标移进
 *  命中带时淡入，停止操作约 800ms 后淡出，悬停或拖动时保持可见；滑块可拖动，
 *  点击轨道空白处时滑块中心跳到点击处。此前本类自己绘制 6 像素宽的方形滑块，
 *  并按「淡入→驻留→淡出」的一次性动画显示。
 *
 *  用法（宿主为一个自绘 DuiControl）：
 *    1. 持有一个 OverlayScrollbar* 子控件并 AddChild；
 *    2. 宿主 Layout 里把它 ForceLayout 到滚动区右缘的竖条：宽度取
 *       kSuggestedWidth、高度 = 可视区高度 viewH，右缘与滚动区右缘对齐；
 *    3. 内容总高 / 滚动量变化时调 SetMetrics(contentH, scroll)；
 *    4. 宿主 OnMouseWheel 滚动后调 Flash()；
 *    5. 注册 SetOnScroll(&Host::OnSbScroll, this)（继承自 DuiScrollBar）：
 *       拖动滑块或点击轨道时本控件回调宿主，由宿主更新其滚动量并重绘。
 *
 *  滚动量由宿主持有，本控件只是其「视图」；拖动经回调写回宿主。可视区
 *  高度取自本控件自身矩形（宿主须把它摆成与可视区等高）。滚轮不在本控件
 *  处理，交给宿主（见 OnMouseWheel）。
 *
 *  balloonwj@qq.com   2026-05-22
 */
#pragma once

#include "Controls/Window/DuiScrollBar.h"

namespace flamingoadmin {

// 拖动滚动回调原型（与 DuiScrollBar::OnScrollFn 相同）。
//   ctx：SetOnScroll 传入的上下文（通常为宿主 this）。
//   newScroll：拖动产生的新滚动偏移（已夹取到合法区间）。
typedef balloonwjui::DuiScrollBar::OnScrollFn OverlayScrollbarScrollFn;

// 悬浮式 auto-hide 滚动条子控件（balloonui DuiScrollBar 的子类）。
class OverlayScrollbar : public balloonwjui::DuiScrollBar
{
public:
    // 建议的滚动条宽度（像素）：即命中带宽度，与 balloonui 容器的默认值相同。
    // 宿主 Layout 时按此宽度摆放本控件；画出来的滑块只有 5 像素宽。
    static const int kSuggestedWidth = balloonwjui::DuiScrollBar::kOverlayBandPx;

    OverlayScrollbar();

    // 设置内容总高与当前滚动偏移（宿主在两者任一变化时调用）。
    //   contentH：可滚动内容的总高度（像素）。
    //   scroll：当前滚动偏移（像素），内部夹取到 [0, contentH - 可视高]。
    // 副作用：范围、页大小或位置确有变化时更新并重绘本控件，未变化时什么都不做；不回调宿主。
    // 可以在宿主的 OnPaint 里调用。
    void SetMetrics(int contentH, int scroll);

    // 触发滚动条淡入，并在空闲约 800ms 后自动淡出（宿主在滚轮滚动 / 需要提示时调用）。
    // 内容放得下时本调用为空操作。
    void Flash();

    // DuiScrollBar 覆写：先记下新矩形，再按新的可视区高度（矩形高度）重算滚动范围。
    //   rcAvail：宿主分给本控件的矩形（host-client 坐标）。
    void Layout(const RECT& rcAvail) override;

    // DuiScrollBar 覆写：内容放得下时不命中（不拦截宿主的鼠标事件）；
    // 内容溢出时整条命中带都归本控件，即使滚动条当前处于淡出隐藏态。
    //   pt：host-client 坐标。返回命中的控件（本控件）或 nullptr。
    balloonwjui::DuiControl* HitTest(POINT pt) override;

    // DuiScrollBar 覆写：滚轮一律不在本控件处理，返回 false 让事件上冒给宿主，
    // 由宿主按自己的步长滚动后调 SetMetrics 与 Flash。
    //   pt / zDelta / mkFlags：同 DuiControl::OnMouseWheel，均不使用。
    bool OnMouseWheel(POINT pt, short zDelta, UINT mkFlags) override;

private:
    // 可视区高度 = 本控件自身矩形高度。
    int ViewH() const;

    // 按 m_contentH、m_scroll 与当前可视区高度设置基类的范围、页大小与位置（不回调宿主）。
    // 数值未变化时不做任何设置，因而不会使本控件失效（宿主可以在自己的 OnPaint 里调 SetMetrics）。
    void ApplyMetrics();

    int   m_contentH = 0;        // 内容总高（像素），由宿主经 SetMetrics 给出
    int   m_scroll   = 0;        // 宿主最近一次给出的滚动偏移（像素）
};

} // namespace flamingoadmin
