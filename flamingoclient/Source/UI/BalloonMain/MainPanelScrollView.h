/**
 *  MainPanelScrollView.h
 *
 *  主面板三个列表面板（以及公告中心、消息盒子、消息提醒管理、消息记录窗等）用的滚动视口 —— 继承 balloonui 的
 *  DuiScrollView。
 *
 *  滚动策略（2026-10-04 起）：直接使用基类 DuiScrollView 的滚动条 —— balloonui 已统一为悬浮式细滑块（不占内容
 *  宽度，半透明灰色圆角，滚动或鼠标移进命中带时淡入、停止操作约 800ms 后淡出，悬停或拖动时保持可见，可拖动，
 *  点轨道空白处滑块中心跳到点击处），对应开发约定文档 "UI 约定"第 1 条。此前本类把基类滚动条宽度置 0、自己画
 *  一条同样的细滑块，并由宿主窗口的 WM_TIMER 推进淡入淡出；现在淡入淡出由 balloonui 的 DuiAnimMgr 推进，宿主
 *  不再需要定时器。
 *
 *  本类只额外做一件事：给窗口缩放边框让位。列表面板铺满窗口宽度，滚动条若贴着视口右缘，会整条落在
 *  DuiFrameWindow 的 WM_NCHITTEST 缩放边框里，鼠标只会变成左右双向箭头、滑块根本抓不住；故排版之后把滚动条
 *  命中带向左挪 SetEdgeInsetPx 设的宽度（取值见 OverlayScrollBarGeometry 的 ComputeResizeBorderInsetPx）。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL。
 *
 *  balloonwj@qq.com   2026-05-21
 */

#pragma once

#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiScrollBar.h"
#include "OverlayScrollBarGeometry.h"

namespace balloonmain {

/**
 *  MainPanelScrollView —— 滚动条为 balloonui 统一悬浮式细滑块、并给窗口缩放边框让位的列表滚动视口。
 */
class MainPanelScrollView : public balloonwjui::DuiScrollView
{
public:
    MainPanelScrollView();

    /**
     *  立即隐藏滚动条（跳过淡出动画）。切换 Tab 一类"面板整体换人"的场景用，免得隐藏的面板还在慢慢淡出。
     *  之后再滚动或鼠标移进命中带时照常淡入。
     */
    void HideScrollBarNow();

    /**
     *  设置滚动条右侧要给窗口缩放边框让出的宽度（物理像素，>= 0）。由宿主窗口在建好界面以及窗口尺寸 / DPI
     *  变化后调用，取值来自 ComputeResizeBorderInsetPx。默认 0（不让位）。
     *  副作用：宽度确有变化时按当前矩形重新排版并重绘。
     */
    void SetEdgeInsetPx(int px);

    // 取当前的让位宽度（物理像素）。
    int GetEdgeInsetPx() const { return m_edgeInsetPx; }

    // DuiScrollView 覆写：先交给基类排版，再把滚动条命中带向左挪 m_edgeInsetPx，给窗口缩放边框让位。
    //   rcAvail：分到的矩形（宿主客户区坐标）
    void Layout(const RECT& rcAvail) override;

private:
    int  m_edgeInsetPx;      // 右侧给窗口缩放边框让出的宽度（物理像素）；0 表示不让位
};

} // namespace balloonmain
