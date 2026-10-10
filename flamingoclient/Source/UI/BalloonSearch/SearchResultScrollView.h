/**
 *  SearchResultScrollView.h
 *
 *  主面板搜索下拉的结果滚动区（含「最近搜索」列表）。继承 balloonui 的 DuiScrollView。
 *
 *  2026-10-04 用户要求「和聊天框的滚动条样式一样」时，本类曾把内置滚动条宽度设为 0、自己画聊天窗样式的细滑块；
 *  同日 balloonui 的滚动条统一成了同一外观（悬浮式半透明灰色圆角细滑块，不占内容宽度，滚动时淡入、停手后淡出，
 *  可拖动，点轨道空白处滑块中心跳到点击处），本类随之改为直接使用基类的滚动条，不再自绘。保留这个类名，
 *  只是为了不改动使用处与单测的入口。
 *
 *  典型用法（SearchDropdown 内）：
 *      std::unique_ptr<balloonsearch::SearchResultScrollView> scroll(new balloonsearch::SearchResultScrollView());
 *      scroll->SetAutoContentHeight(true);
 *      scroll->SetContent(std::move(content));
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL。
 *
 *  balloonwj@qq.com   2026-10-04
 */
#pragma once

#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiScrollBar.h"

namespace balloonsearch {

/**
 *  SearchResultScrollView —— 搜索下拉的结果滚动视口，滚动条即 balloonui 统一的悬浮式细滑块。
 */
class SearchResultScrollView : public balloonwjui::DuiScrollView
{
public:
    SearchResultScrollView();

    /**
     *  当前画出来的细滑块矩形（宿主客户区坐标），即基类滚动条的 ComputePaintThumbRect。内容没有超出可视区、
     *  或尚未布局时返回空矩形（全 0）。供单元测试核对。
     */
    RECT ThumbRect() const;

    /**
     *  是否正在拖动滑块（按下之后、松开之前），即基类滚动条的 IsDragging。供单元测试核对。
     */
    bool IsDraggingThumb() const;
};

} // namespace balloonsearch
