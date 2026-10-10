/**
 *  MemoEditLayout.cpp —— 备忘编辑对话框的客户区控件树，见 MemoEditLayout.h。
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

#include "MemoEditLayout.h"
#include "MemoViewModel.h"   // 标题、备注长度上限，次数上限
#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiEdit.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiSpinBox.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiComboBox.h"
#include "../../Common/I18n/I18n.h"   // TR：界面文字的多语言查找

using namespace balloonwjui;

namespace balloonmemo {

//「提醒」下拉各项的提前分钟数：到点、5、10、15、30 分钟、1 小时、2 小时、1 天
const int kMemoAdvanceChoices[kMemoAdvanceChoiceCount] = { 0, 5, 10, 15, 30, 60, 120, 1440 };

namespace {

// ---- 颜色 ----
const COLORREF kColorWhite = RGB(0xFF, 0xFF, 0xFF);
const COLORREF kColorLabel = RGB(0x8A, 0x8A, 0x8A);   // 小标题与各行行首的文字
const COLORREF kColorError = RGB(0xE5, 0x39, 0x35);   // 提示行
const COLORREF kColorText  = RGB(0x55, 0x55, 0x55);   // 已选几人

// ---- 版式（逻辑像素）----
const int kPadH        = 20;   // 客户区左右内边距
const int kPadTop      = 4;    // 客户区上内边距（标题栏下方）
const int kPadBottom   = 16;   // 客户区下内边距
const int kRootGap     = 6;    // 各行之间的间距
const int kLabelH      = 18;   // 小标题行高
const int kEditH       = 34;   // 单行输入框高
const int kNoteH       = 72;   // 备注输入框高（多行）
const int kRowH        = 28;   // 微调框、下拉框、单选、勾选各行的高
const int kLeadW       = 64;   // 各行行首文字（「提醒」「结束」等）的宽
//时间的五个微调框统一宽，「年 / 月 / 日」与「时 / 分」两行按列对齐（2026-10-03 用户反馈原先宽窄不一、错位）。
//68 放得下四位年份与右侧的上下箭头，与「发起接龙」窗口的截止时间微调框同宽
const int kTimeSpinW   = 68;
const int kUnitW       = 18;   // 「年」「月」「日」「时」「分」文字宽
const int kSpinGap     = 4;    // 同一行里控件之间的间距
const int kComboW      = 130;  // 下拉框宽
const int kComboGap    = 16;   // 「提醒」与「重复」两组之间的间距
const int kWeekdayW    = 44;   // 每个星期勾选框宽
const int kRadioW      = 104;  // 单选按钮宽
const int kCountW      = 64;   // 次数微调框宽
const int kPickBtnW    = 88;   // 「选择成员」宽
const int kPreviewBtnW = 80;   // 「预览效果」文字按钮宽
const int kErrorH      = 20;   // 提示行高
const int kButtonRowH  = 34;   // 底部按钮行高
const int kButtonGap   = 10;   // 底部按钮之间的间距
const int kCancelW     = 80;   // 「取消」宽
const int kSaveW       = 88;   // 「保存」宽
const int kComboMaxVisible = 8;  // 下拉一次最多显示几项

// ---- 微调框的取值范围。年的范围由对话框打开时按当前年份重设 ----
const int kYearMin   = 2000;
const int kYearMax   = 2099;
const int kMonthMax  = 12;
const int kDayMax    = 31;
const int kHourMax   = 23;
const int kMinuteMax = 59;

// ---- 单选按钮的互斥组（同一个父容器里组号相同的单选互斥）----
const int kRadioGroupEnd    = 1;   // 结束
const int kRadioGroupStyle  = 2;   // 提醒方式
const int kRadioGroupTarget = 3;   // 提醒对象

typedef DuiLayout::Hint Hint;

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

std::unique_ptr<DuiControl> MakeSpin(UINT id, int minV, int maxV)
{
    std::unique_ptr<DuiSpinBox> spin(new DuiSpinBox());
    spin->SetCtrlId(id);
    spin->SetRange(minV, maxV);
    return std::unique_ptr<DuiControl>(spin.release());
}

std::unique_ptr<DuiControl> MakeCombo(UINT id, const LPCTSTR* items, int count)
{
    std::unique_ptr<DuiComboBox> combo(new DuiComboBox());
    combo->SetCtrlId(id);
    for (int i = 0; i < count; ++i)
        combo->AddString(items[i]);
    combo->SetMaxVisibleItems(kComboMaxVisible);
    combo->SetCurSel(0, false);
    return std::unique_ptr<DuiControl>(combo.release());
}

std::unique_ptr<DuiControl> MakeCheck(UINT id, LPCTSTR text, DuiButton::Style style, int radioGroup)
{
    std::unique_ptr<DuiButton> button(new DuiButton());
    button->SetButtonType(style);
    button->SetCtrlId(id);
    button->SetText(text);
    if (radioGroup != 0)
        button->SetRadioGroup(radioGroup);
    return std::unique_ptr<DuiControl>(button.release());
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

//行首文字 + 一组控件，右侧留空
std::unique_ptr<DuiHBox> MakeRow(LPCTSTR lead)
{
    std::unique_ptr<DuiHBox> row(new DuiHBox());
    row->SetGap(kSpinGap);
    row->AddChild(MakeLabel(0, lead, kColorLabel, DT_LEFT | DT_VCENTER), Hint().Fixed(kLeadW));
    return row;
}

void FinishRow(DuiVBox& root, std::unique_ptr<DuiHBox> row)
{
    row->AddChild(std::unique_ptr<DuiControl>(new DuiLabel()), Hint().Weight(1));
    root.AddChild(std::unique_ptr<DuiControl>(row.release()), Hint().Fixed(kRowH));
}

} // anonymous namespace

std::unique_ptr<DuiControl> BuildMemoEditBody(bool groupMode)
{
    std::unique_ptr<DuiVBox> root(new DuiVBox());
    root->SetBgColor(kColorWhite);
    root->SetPadding(kPadH, kPadTop, kPadH, kPadBottom);
    root->SetGap(kRootGap);

    //标题
    root->AddChild(MakeLabel(0, TR(L"标题"), kColorLabel, DT_LEFT | DT_VCENTER), Hint().Fixed(kLabelH));
    std::unique_ptr<DuiEdit> title(new DuiEdit());
    title->SetCtrlId(kMemoEditIdTitle);
    title->SetPlaceholder(TR(L"例如：周会、交周报"));
    title->SetBgColor(kColorWhite);
    title->SetMaxLength((int)kMemoTitleMaxChars);
    root->AddChild(std::unique_ptr<DuiControl>(title.release()), Hint().Fixed(kEditH));

    //开始时间：[年]年 [月]月 [日]日 [时]时 [分]分
    {
        std::unique_ptr<DuiHBox> row = MakeRow(TR(L"时间"));
        row->AddChild(MakeSpin(kMemoEditIdYear, kYearMin, kYearMax), Hint().Fixed(kTimeSpinW));
        row->AddChild(MakeLabel(0, TR(L"年"), kColorText, DT_CENTER | DT_VCENTER), Hint().Fixed(kUnitW));
        row->AddChild(MakeSpin(kMemoEditIdMonth, 1, kMonthMax), Hint().Fixed(kTimeSpinW));
        row->AddChild(MakeLabel(0, TR(L"月"), kColorText, DT_CENTER | DT_VCENTER), Hint().Fixed(kUnitW));
        row->AddChild(MakeSpin(kMemoEditIdDay, 1, kDayMax), Hint().Fixed(kTimeSpinW));
        row->AddChild(MakeLabel(0, TR(L"日"), kColorText, DT_CENTER | DT_VCENTER), Hint().Fixed(kUnitW));
        FinishRow(*root, std::move(row));
    }
    {
        std::unique_ptr<DuiHBox> row = MakeRow(_T(""));
        row->AddChild(MakeSpin(kMemoEditIdHour, 0, kHourMax), Hint().Fixed(kTimeSpinW));
        row->AddChild(MakeLabel(0, TR(L"时"), kColorText, DT_CENTER | DT_VCENTER), Hint().Fixed(kUnitW));
        row->AddChild(MakeSpin(kMemoEditIdMinute, 0, kMinuteMax), Hint().Fixed(kTimeSpinW));
        row->AddChild(MakeLabel(0, TR(L"分"), kColorText, DT_CENTER | DT_VCENTER), Hint().Fixed(kUnitW));
        FinishRow(*root, std::move(row));
    }

    //提醒（提前量）与重复
    {
        static const LPCTSTR kAdvanceItems[kMemoAdvanceChoiceCount] = {
            TR(L"到点提醒"), TR(L"提前 5 分钟"), TR(L"提前 10 分钟"), TR(L"提前 15 分钟"),
            TR(L"提前 30 分钟"), TR(L"提前 1 小时"), TR(L"提前 2 小时"), TR(L"提前 1 天") };
        //序号与 memo_repeat_type 的取值一致
        const int kRepeatItemCount = 6;
        static const LPCTSTR kRepeatItems[kRepeatItemCount] = {
            TR(L"不重复"), TR(L"每天"), TR(L"每个工作日"), TR(L"每周"), TR(L"每月"), TR(L"每年") };
        std::unique_ptr<DuiHBox> row = MakeRow(TR(L"提醒"));
        row->AddChild(MakeCombo(kMemoEditIdAdvance, kAdvanceItems, kMemoAdvanceChoiceCount), Hint().Fixed(kComboW));
        row->AddChild(std::unique_ptr<DuiControl>(new DuiLabel()), Hint().Fixed(kComboGap));
        row->AddChild(MakeLabel(0, TR(L"重复"), kColorLabel, DT_LEFT | DT_VCENTER), Hint().Fixed(kLeadW / 2 + kUnitW));
        row->AddChild(MakeCombo(kMemoEditIdRepeat, kRepeatItems, kRepeatItemCount), Hint().Fixed(kComboW));
        FinishRow(*root, std::move(row));
    }

    //每周重复时选星期几。其它重复方式下行内控件隐藏、行本身仍占位，下面各行不上下移动（2026-10-03 与用户确认）
    {
        static const LPCTSTR kWeekdayItems[memo::kDaysPerWeek] = {
            TR(L"一"), TR(L"二"), TR(L"三"), TR(L"四"), TR(L"五"), TR(L"六"), TR(L"日") };
        std::unique_ptr<DuiHBox> row = MakeRow(TR(L"每周"));
        row->SetCtrlId(kMemoEditIdWeekdayRow);
        for (int i = 0; i < memo::kDaysPerWeek; ++i)
        {
            row->AddChild(MakeCheck(kMemoEditIdWeekdayFirst + i, kWeekdayItems[i], DuiButton::StyleCheckbox, 0),
                          Hint().Fixed(kWeekdayW));
        }
        FinishRow(*root, std::move(row));
    }

    //结束：永不结束 / 提醒 [N] 次（不重复时整行置灰）
    {
        std::unique_ptr<DuiHBox> row = MakeRow(TR(L"结束"));
        row->AddChild(MakeCheck(kMemoEditIdEndNever, TR(L"永不结束"), DuiButton::StyleRadio, kRadioGroupEnd),
                      Hint().Fixed(kRadioW));
        row->AddChild(MakeCheck(kMemoEditIdEndCount, TR(L"提醒"), DuiButton::StyleRadio, kRadioGroupEnd),
                      Hint().Fixed(kLeadW));
        row->AddChild(MakeSpin(kMemoEditIdCount, 1, kMemoCountMax), Hint().Fixed(kCountW));
        row->AddChild(MakeLabel(0, TR(L"次"), kColorText, DT_CENTER | DT_VCENTER), Hint().Fixed(kUnitW));
        FinishRow(*root, std::move(row));
    }

    //提醒方式
    {
        std::unique_ptr<DuiHBox> row = MakeRow(TR(L"方式"));
        row->AddChild(MakeCheck(kMemoEditIdStylePopup, TR(L"右下角弹窗"), DuiButton::StyleRadio, kRadioGroupStyle),
                      Hint().Fixed(kRadioW));
        row->AddChild(MakeCheck(kMemoEditIdStyleFull, TR(L"全屏动画"), DuiButton::StyleRadio, kRadioGroupStyle),
                      Hint().Fixed(kRadioW));
        //「预览效果」：按当前填写的内容播放一遍全屏提醒，不影响真实提醒（2026-10-04 起）
        row->AddChild(MakeButton(kMemoEditIdPreviewFull, TR(L"预览效果"), DuiButton::Variant::Text),
                      Hint().Fixed(kPreviewBtnW));
        FinishRow(*root, std::move(row));
    }

    //提醒对象（仅群备忘）
    if (groupMode)
    {
        std::unique_ptr<DuiHBox> row = MakeRow(TR(L"提醒谁"));
        row->AddChild(MakeCheck(kMemoEditIdTargetAll, TR(L"全体成员"), DuiButton::StyleRadio, kRadioGroupTarget),
                      Hint().Fixed(kRadioW - kUnitW));
        row->AddChild(MakeCheck(kMemoEditIdTargetPick, TR(L"指定成员"), DuiButton::StyleRadio, kRadioGroupTarget),
                      Hint().Fixed(kRadioW - kUnitW));
        row->AddChild(MakeButton(kMemoEditIdPickButton, TR(L"选择成员"), DuiButton::Variant::Default), Hint().Fixed(kPickBtnW));
        row->AddChild(MakeLabel(kMemoEditIdTargetText, _T(""), kColorText, DT_LEFT | DT_VCENTER | DT_END_ELLIPSIS),
                      Hint().Weight(1));
        root->AddChild(std::unique_ptr<DuiControl>(row.release()), Hint().Fixed(kRowH));
    }

    //备注
    root->AddChild(MakeLabel(0, TR(L"备注（选填）"), kColorLabel, DT_LEFT | DT_VCENTER), Hint().Fixed(kLabelH));
    std::unique_ptr<DuiEdit> note(new DuiEdit());
    note->SetCtrlId(kMemoEditIdNote);
    note->SetBgColor(kColorWhite);
    note->SetMaxLength((int)kMemoNoteMaxChars);
    note->SetMultiLine(true);
    note->SetWordWrap(true);
    root->AddChild(std::unique_ptr<DuiControl>(note.release()), Hint().Fixed(kNoteH));

    root->AddChild(MakeLabel(kMemoEditIdError, _T(""), kColorError, DT_LEFT | DT_VCENTER | DT_END_ELLIPSIS),
                   Hint().Fixed(kErrorH));

    //余下的高度留空，按钮行贴底
    root->AddChild(std::unique_ptr<DuiControl>(new DuiLabel()), Hint().Weight(1));

    std::unique_ptr<DuiHBox> buttons(new DuiHBox());
    buttons->SetCtrlId(kMemoEditIdButtonRow);
    buttons->SetGap(kButtonGap);
    //左下角「删除」：只在修改已有的一条时由对话框显示出来
    buttons->AddChild(MakeButton(kMemoEditIdDelete, TR(L"删除"), DuiButton::Variant::Danger), Hint().Fixed(kCancelW));
    buttons->AddChild(std::unique_ptr<DuiControl>(new DuiLabel()), Hint().Weight(1));
    buttons->AddChild(MakeButton(kMemoEditIdCancel, TR(L"取消"), DuiButton::Variant::Default), Hint().Fixed(kCancelW));
    buttons->AddChild(MakeButton(kMemoEditIdSave, TR(L"保存"), DuiButton::Variant::Primary), Hint().Fixed(kSaveW));
    root->AddChild(std::unique_ptr<DuiControl>(buttons.release()), Hint().Fixed(kButtonRowH));

    return std::unique_ptr<DuiControl>(root.release());
}

} // namespace balloonmemo
