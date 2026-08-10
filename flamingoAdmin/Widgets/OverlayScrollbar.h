/**
 *  flamingoAdmin —— 悬浮式 auto-hide 滚动条控件 OverlayScrollbar。
 *
 *  供「手写自绘的可滚动区域」（ComposeModal 投放目标选择器、AuditScreen
 *  表格 / 详情区等，见开发约定文档「UI 约定」）复用的滚动条子控件：内容
 *  溢出时才出现；滚动 / 拖动时淡入，停止约 1 秒后淡出；悬浮于内容右缘、
 *  不占内容宽度；滑块可拖动。
 *
 *  用法（宿主为一个自绘 DuiControl）：
 *    1. 持有一个 OverlayScrollbar* 子控件并 AddChild；
 *    2. 宿主 Layout 里把它 ForceLayout 到滚动区右缘的细矩形（宽度取
 *       kSuggestedWidth、高度 = 可视区高度 viewH）；
 *    3. 内容总高 / 滚动量变化时调 SetMetrics(contentH, scroll)；
 *    4. 宿主 OnMouseWheel 滚动后调 Flash()；
 *    5. 注册 SetOnScroll(&Host::OnSbScroll, this)：拖动滑块时本控件回调
 *       宿主、由宿主更新其滚动量并重绘。
 *
 *  滚动量由宿主持有，本控件只是其「视图」；拖动经回调写回宿主。可视区
 *  高度取自本控件自身矩形（宿主须把它摆成与可视区等高）。
 *
 *  balloonwj@qq.com   2026-05-22
 */
#pragma once

#include "DuiControl.h"

namespace flamingoadmin {

// 拖动滚动回调原型。
//   ctx：SetOnScroll 传入的上下文（通常为宿主 this）。
//   newScroll：拖动产生的新滚动偏移（已夹取到合法区间）。
typedef void (*OverlayScrollbarScrollFn)(void* ctx, int newScroll);

// 悬浮式 auto-hide 滚动条子控件。
class OverlayScrollbar : public balloonwjui::DuiControl
{
public:
    // 建议的滚动条宽度（像素）：宿主 Layout 时按此宽度摆放本控件。
    static const int kSuggestedWidth = 6;

    OverlayScrollbar();

    // 设置内容总高与当前滚动偏移（宿主在两者任一变化时调用）。
    //   contentH：可滚动内容的总高度（像素）。
    //   scroll：当前滚动偏移（像素）。
    void SetMetrics(int contentH, int scroll);

    // 触发滚动条淡入并排期自动淡出（宿主在滚轮滚动 / 需要提示时调用）。
    // 内容放得下时本调用为空操作。
    void Flash();

    // 注册拖动滚动回调。拖动滑块时本控件回调它，宿主据此更新滚动量并重绘。
    //   fn：回调函数指针；ctx：回调上下文（通常为宿主 this）。
    void SetOnScroll(OverlayScrollbarScrollFn fn, void* ctx);

    // 淡入淡出动画每帧回调（由内部动画类调用，业务代码勿直接调）。
    //   gen：发起动画时的代次；alpha：本帧不透明度 [0,1]。
    void OnFadeTick(int gen, float alpha);

    balloonwjui::DuiControl* HitTest(POINT pt) override;
    void OnPaint(HDC hdc, const RECT& rcDirty) override;
    bool OnLButtonDown(POINT pt, UINT mkFlags) override;
    bool OnMouseMove(POINT pt, UINT mkFlags) override;
    bool OnLButtonUp(POINT pt, UINT mkFlags) override;

private:
    // 当前滑块矩形（host-client 坐标）；内容不溢出时为空矩形。
    RECT ThumbRectNow() const;

    // 可视区高度 = 本控件自身矩形高度。
    int ViewH() const;

    int   m_contentH = 0;        // 内容总高（像素）
    int   m_scroll   = 0;        // 当前滚动偏移（像素）
    float m_alpha    = 0.0f;     // 当前不透明度 [0,1]，0 = 不可见
    int   m_animGen  = 0;        // 淡入淡出动画代次（新 Flash 令旧动画失效）
    bool  m_dragging = false;    // 是否正在拖动滑块
    int   m_dragOffsetY = 0;     // 起拖时鼠标相对滑块顶部的 y 偏移

    OverlayScrollbarScrollFn m_onScroll = nullptr;    // 拖动滚动回调
    void*                    m_onScrollCtx = nullptr; // 回调上下文
};

} // namespace flamingoadmin
