/**
 *  MemoWindowLayout.cpp —— 本人备忘窗口的客户区控件树，见 MemoWindowLayout.h。
 *
 *  本 .cpp 不 include Flamingo 的 stdafx.h —— 它要被 Flamingo.vcxproj 与 FlamingoUnit.vcxproj 共用
 *  （布局单测按设计尺寸排版）。在 Flamingo.vcxproj 里需标记 PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-10-03
 */

// ---- ATL / WTL 头文件要求的前置依赖，与 RelayComposeLayout.cpp 相同 ----
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#define WINVER          0x0601      // 目标系统下限 = Windows 7
#define _WIN32_WINNT    0x0601
#define _WIN32_IE       0x0601
#define _RICHEDIT_VER   0x0300      // RichEdit 3.0，WTL 10 要求的下限
#define _WTL_NO_CSTRING
#define _WTL_NO_WTYPES

#include <atlbase.h>
#include <atlstr.h>
#include <atltypes.h>
#include <atlapp.h>

extern CAppModule _Module;

#include <atlwin.h>
#include <atlcrack.h>
#include <atlmisc.h>

#include <memory>

#include "MemoWindowLayout.h"
#include "MemoCalendarControls.h"
#include "MemoCardList.h"   // 右侧当天的备忘卡片
#include "MemoViewModel.h"   // kCalendarRows / kCalendarCols
#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiSeparator.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiComboBox.h"
#include "../../Common/I18n/I18n.h"   // TR：界面文字的多语言查找

using namespace balloonwjui;

namespace balloonmemo {

namespace {

// ---- 颜色 ----
const COLORREF kColorWhite   = RGB(0xFF, 0xFF, 0xFF);
const COLORREF kColorTitle   = RGB(0x2A, 0x2A, 0x2A);   // 日期标题
const COLORREF kColorWeekday = RGB(0x9A, 0x98, 0x93);   // 星期一到星期日
const COLORREF kColorHint    = RGB(0x8A, 0x8A, 0x8A);   // 条数提示
const COLORREF kColorSep     = RGB(0xEC, 0xEA, 0xE6);   // 左右两栏之间的分隔线

// ---- 版式（逻辑像素）----
const int kPadH         = 16;   // 客户区左右内边距
const int kPadTop       = 4;    // 客户区上内边距（标题栏下方）
const int kPadBottom    = 16;   // 客户区下内边距
const int kColumnGap    = 16;   // 左右两栏之间的间距
const int kCellW        = 44;   // 日期格宽
const int kCellH        = 44;   // 日期格高
const int kCalendarW    = kCellW * kCalendarCols;   // 月历栏宽
const int kRowGap       = 4;    // 月历各行之间的间距
const int kHeaderH      = 32;   // 「‹ 年 月 › 今天」一行的高
const int kNavBtnW      = 32;   // 「‹」「›」宽
const int kTodayBtnW    = 56;   // 「今天」宽
const int kHeaderGap    = 4;    // 月份行里各控件的间距
const int kYearComboW   = 92;   // 年份下拉框宽：放得下「2026年」与右侧箭头
const int kMonthComboW  = 68;   // 月份下拉框宽：放得下「12月」与右侧箭头
const int kComboVisible = 12;   // 下拉一次最多显示几项：月份正好 12 项不用滚动，年份 16 项滚动查看
const int kWeekdayH     = 24;   // 星期一到星期日那一行的高
const int kTitlePt      = 12;   // 日期标题的磅值
const int kMonthsPerYear = 12;  // 月份下拉框的项数
const int kDayTitleH    = 32;   // 右侧日期标题的高
const int kHintH        = 20;   // 右侧条数提示的高
const int kRightGap     = 6;    // 右侧各行之间的间距
const int kButtonRowH   = 34;   // 底部按钮行高
const int kAddBtnW      = 120;  // 「＋ 添加备忘」宽

std::unique_ptr<DuiControl> MakeLabel(UINT id, LPCTSTR text, COLORREF color, DWORD align)
{
    std::unique_ptr<DuiLabel> label(new DuiLabel());
    if (id != 0)
        label->SetCtrlId(id);
    label->SetText(text);
    label->SetTextColor(color);
    label->SetTextAlign(align | DT_SINGLELINE | DT_NOPREFIX);
    return std::unique_ptr<DuiControl>(label.release());
}

std::unique_ptr<DuiControl> MakeButton(UINT id, LPCTSTR text, DuiButton::Variant variant)
{
    std::unique_ptr<DuiButton> button(new DuiButton());
    button->SetButtonType(DuiButton::StylePushButton);
    button->SetVariant(variant);
    button->SetCtrlId(id);
    button->SetText(text);
    return std::unique_ptr<DuiControl>(button.release());
}

//翻月一行的按钮（「‹」「›」「今天」）：不参与 Tab 键焦点，点过之后不留虚线焦点框
std::unique_ptr<DuiControl> MakeNavButton(UINT id, LPCTSTR text, DuiButton::Variant variant)
{
    std::unique_ptr<DuiControl> button = MakeButton(id, text, variant);
    button->SetTabStop(false);
    return button;
}

//年份或月份下拉框（只读，不能手输）
std::unique_ptr<DuiComboBox> MakeCalendarCombo(UINT id)
{
    std::unique_ptr<DuiComboBox> combo(new DuiComboBox());
    combo->SetCtrlId(id);
    combo->SetMaxVisibleItems(kComboVisible);
    return combo;
}

std::unique_ptr<DuiControl> BuildCalendarColumn()
{
    typedef DuiLayout::Hint Hint;
    std::unique_ptr<DuiVBox> col(new DuiVBox());
    col->SetGap(kRowGap);

    //「‹ [2026年 ▾] [10月 ▾] › …… 今天」：年、月两个下拉框可直接选到任意年月（2026-10-04 与用户确认），
    //「‹」「›」仍按月翻
    std::unique_ptr<DuiHBox> header(new DuiHBox());
    header->SetGap(kHeaderGap);
    header->AddChild(MakeNavButton(kMemoWinIdPrev, _T("‹"), DuiButton::Variant::Ghost), Hint().Fixed(kNavBtnW));
    //年份的项由窗口按今年与当前显示的年份填入（CalendarYearRange）
    header->AddChild(std::unique_ptr<DuiControl>(MakeCalendarCombo(kMemoWinIdYear).release()), Hint().Fixed(kYearComboW));
    std::unique_ptr<DuiComboBox> month = MakeCalendarCombo(kMemoWinIdMonth);
    for (int m = 1; m <= kMonthsPerYear; ++m)
        month->AddString(TRF(L"{0}月", m).c_str());
    header->AddChild(std::unique_ptr<DuiControl>(month.release()), Hint().Fixed(kMonthComboW));
    header->AddChild(MakeNavButton(kMemoWinIdNext, _T("›"), DuiButton::Variant::Ghost), Hint().Fixed(kNavBtnW));
    header->AddChild(std::unique_ptr<DuiControl>(new DuiLabel()), Hint().Weight(1));
    header->AddChild(MakeNavButton(kMemoWinIdToday, TR(L"今天"), DuiButton::Variant::Default), Hint().Fixed(kTodayBtnW));
    col->AddChild(std::unique_ptr<DuiControl>(header.release()), Hint().Fixed(kHeaderH));

    //星期一到星期日，与日期格的列对齐
    static const LPCTSTR kWeekdayNames[kCalendarCols] = { TR(L"一"), TR(L"二"), TR(L"三"), TR(L"四"),
                                                           TR(L"五"), TR(L"六"), TR(L"日") };
    std::unique_ptr<DuiHBox> weekdays(new DuiHBox());
    for (int i = 0; i < kCalendarCols; ++i)
        weekdays->AddChild(MakeLabel(0, kWeekdayNames[i], kColorWeekday, DT_CENTER | DT_VCENTER), Hint().Weight(1));
    col->AddChild(std::unique_ptr<DuiControl>(weekdays.release()), Hint().Fixed(kWeekdayH));

    for (int r = 0; r < kCalendarRows; ++r)
    {
        std::unique_ptr<DuiHBox> row(new DuiHBox());
        for (int c = 0; c < kCalendarCols; ++c)
        {
            std::unique_ptr<CMemoDayButton> cell(new CMemoDayButton());
            cell->SetCtrlId(kMemoWinIdDayFirst + r * kCalendarCols + c);
            row->AddChild(std::unique_ptr<DuiControl>(cell.release()), Hint().Weight(1));
        }
        col->AddChild(std::unique_ptr<DuiControl>(row.release()), Hint().Fixed(kCellH));
    }
    col->AddChild(std::unique_ptr<DuiControl>(new DuiLabel()), Hint().Weight(1));
    return std::unique_ptr<DuiControl>(col.release());
}

std::unique_ptr<DuiControl> BuildDayColumn()
{
    typedef DuiLayout::Hint Hint;
    std::unique_ptr<DuiVBox> col(new DuiVBox());
    col->SetGap(kRightGap);

    std::unique_ptr<DuiLabel> dayTitle(new DuiLabel());
    dayTitle->SetCtrlId(kMemoWinIdDayTitle);
    dayTitle->SetTextColor(kColorTitle);
    dayTitle->SetTextPointSize(kTitlePt, true);
    dayTitle->SetTextAlign(DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    col->AddChild(std::unique_ptr<DuiControl>(dayTitle.release()), Hint().Fixed(kDayTitleH));
    col->AddChild(MakeLabel(kMemoWinIdDayHint, _T(""), kColorHint, DT_LEFT | DT_VCENTER), Hint().Fixed(kHintH));

    col->AddChild(std::unique_ptr<DuiControl>(CreateMemoCardScroll(kMemoWinIdList).release()), Hint().Weight(1));

    std::unique_ptr<DuiHBox> buttons(new DuiHBox());
    buttons->AddChild(std::unique_ptr<DuiControl>(new DuiLabel()), Hint().Weight(1));
    buttons->AddChild(MakeButton(kMemoWinIdAdd, TR(L"＋ 添加备忘"), DuiButton::Variant::Primary), Hint().Fixed(kAddBtnW));
    col->AddChild(std::unique_ptr<DuiControl>(buttons.release()), Hint().Fixed(kButtonRowH));
    return std::unique_ptr<DuiControl>(col.release());
}

} // anonymous namespace

std::unique_ptr<DuiControl> BuildMemoWindowBody()
{
    typedef DuiLayout::Hint Hint;
    //底色只有 DuiVBox 能设：外层一个竖直容器负责白底与内边距，里面一行左右两栏
    std::unique_ptr<DuiVBox> root(new DuiVBox());
    root->SetBgColor(kColorWhite);
    root->SetPadding(kPadH, kPadTop, kPadH, kPadBottom);

    std::unique_ptr<DuiHBox> columns(new DuiHBox());
    columns->SetGap(kColumnGap);
    columns->AddChild(BuildCalendarColumn(), Hint().Fixed(kCalendarW));

    std::unique_ptr<DuiSeparator> sep(new DuiSeparator());
    sep->SetOrientation(DuiSeparator::Vertical);
    sep->SetColor(kColorSep);
    columns->AddChild(std::unique_ptr<DuiControl>(sep.release()), Hint().Fixed(1));

    columns->AddChild(BuildDayColumn(), Hint().Weight(1));
    root->AddChild(std::unique_ptr<DuiControl>(columns.release()), Hint().Weight(1));
    return std::unique_ptr<DuiControl>(root.release());
}

} // namespace balloonmemo
