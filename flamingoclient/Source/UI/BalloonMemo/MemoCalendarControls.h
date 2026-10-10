/**
 *  MemoCalendarControls.h
 *
 *  备忘窗口的月历格子 CMemoDayButton。它是 balloonui DuiButton 的子类，只覆写绘制（开发约定文档 UI 约定第 2 条）：
 *  沿用 DuiButton 的点击、悬停与通知，选中的那天画实心圆、今天画描边圆、不属于当前月份的日子画浅色数字、
 *  有备忘的日子在数字下方画一个圆点。DuiButton 的 PushButton 文字色由 Variant 决定、不能单独改，也不能画圆点，
 *  所以子类化。
 *
 *  当天的备忘与群「备忘」页签原先用的列表控件 CMemoEntryList 已于 2026-10-04 换成 MemoCardList.h 的卡片。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL。
 *
 *  balloonwj@qq.com   2026-10-03
 */
#pragma once

#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "MemoViewModel.h"

namespace balloonmemo {

/**
 *  月历上的一格。点击发 DUIN_CLICK，ctrlId 由窗口按格子序号分配。
 */
class CMemoDayButton : public balloonwjui::DuiButton
{
public:
    CMemoDayButton();

    /**
     *  换成这一格的内容（几号、是否本月、今天、选中、有无备忘），并重绘。
     */
    void SetCell(const CalendarCell& cell);

    /**
     *  当前这一格的内容。
     */
    const CalendarCell& Cell() const;

    //DuiControl 覆写：按这一格的状态画选中实心圆或今天描边圆、几号与有备忘时的圆点
    void OnPaint(HDC hdc, const RECT& rcDirty) override;

private:
    CalendarCell m_cell;   //这一格的内容；SetCell 时整体替换
};

} // namespace balloonmemo
