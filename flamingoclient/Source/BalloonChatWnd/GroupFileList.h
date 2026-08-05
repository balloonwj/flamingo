/**
 *  GroupFileList.h
 *
 *  群文件面板里的文件列表堆叠容器 —— 把若干 GroupFileItem 按固定行高自上而下
 *  堆叠。本控件本身不滚动：它作为 balloonui DuiScrollView 的内容控件被装入，
 *  滚动 / 悬浮 auto-hide 滚动条由 DuiScrollView 负责（符合 UI 规约第 1 条
 *  「复用 balloonui 滚动控件」）。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL。
 *
 *  balloonwj@qq.com   2026-06-08
 */

#pragma once

#include "../../balloonui/balloonui/DuiControl.h"

namespace balloonchatwnd {

/**
 *  GroupFileList —— 文件行（GroupFileItem）的固定行高堆叠容器。
 *
 *  典型用法：作为 DuiScrollView 的内容；每次增删行后由调用方按
 *  ContentHeight() 调 DuiScrollView::SetContentHeight 同步滚动范围。
 */
class GroupFileList : public balloonwjui::DuiControl
{
public:
    GroupFileList();

    // 当前内容总高度（= 行数 × 行高），供宿主同步给 DuiScrollView 的内容高度。
    int ContentHeight() const;

    // DuiControl 覆写：把子控件（文件行）按固定行高自上而下堆叠。
    void Layout(const RECT& rcAvail) override;
};

} // namespace balloonchatwnd
