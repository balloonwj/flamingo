/**
 *  GroupQaEditLayout.cpp —— 「新建问答」「修改问答」对话框的客户区控件树，见 GroupQaEditLayout.h。
 *
 *  本 .cpp 不 include Flamingo 的 stdafx.h —— 它要被 Flamingo.vcxproj 与 FlamingoUnit.vcxproj 共用
 *  （布局单测按设计尺寸排版）。在 Flamingo.vcxproj 里需标记 PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-10-07
 */

// ---- ATL / WTL 头文件要求的前置依赖，与 MemoEditLayout.cpp 相同 ----
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

#include "GroupQaEditLayout.h"
#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiEdit.h"
#include "../../Common/I18n/I18n.h"   // TR：界面文字的多语言查找

using namespace balloonwjui;

namespace balloonchatwnd {

namespace {

// ---- 颜色 ----
const COLORREF kColorWhite = RGB(0xFF, 0xFF, 0xFF);
const COLORREF kColorLabel = RGB(0x8A, 0x8A, 0x8A);   // 小标题
const COLORREF kColorError = RGB(0xE5, 0x39, 0x35);   // 提示行

// ---- 版式（逻辑像素）----
const int kPadH       = 20;   // 客户区左右内边距
const int kPadTop     = 12;   // 客户区上内边距
const int kPadBottom  = 16;   // 客户区下内边距
const int kRootGap    = 6;    // 各行之间的间距
const int kLabelH     = 20;   // 小标题的高
const int kEditH      = 32;   // 问题输入框的高
const int kErrorH     = 20;   // 提示行的高
const int kButtonH    = 34;   // 按钮行的高
const int kButtonW    = 80;   // 「取消」「保存」各自的宽
const int kButtonGap  = 10;   // 按钮之间的间距

// 问题与答案的字数上限（与服务端、GroupQaModel 的 kQaQuestionMaxChars / kQaAnswerMaxChars 一致）。
// 输入框按 UTF-16 码元限长，代理对占两个码元，限长略宽松，最终以 CheckQaInput 按字符计数的结果为准
const int kQuestionMaxLen = 128;
const int kAnswerMaxLen   = 2048;

std::unique_ptr<DuiControl> MakeLabel(UINT id, LPCTSTR text, COLORREF color)
{
    std::unique_ptr<DuiLabel> label(new DuiLabel());
    label->SetCtrlId(id);
    label->SetText(text);
    label->SetTextColor(color);
    label->SetTextAlign(DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    return std::unique_ptr<DuiControl>(label.release());
}

std::unique_ptr<DuiControl> MakeButton(UINT id, LPCTSTR text, DuiButton::Variant variant)
{
    std::unique_ptr<DuiButton> button(new DuiButton());
    button->SetCtrlId(id);
    button->SetText(text);
    button->SetButtonType(DuiButton::StylePushButton);
    button->SetVariant(variant);
    return std::unique_ptr<DuiControl>(button.release());
}

} // anonymous namespace

std::unique_ptr<DuiControl> BuildQaEditBody()
{
    typedef DuiLayout::Hint Hint;
    std::unique_ptr<DuiVBox> root(new DuiVBox());
    root->SetBgColor(kColorWhite);
    root->SetPadding(kPadH, kPadTop, kPadH, kPadBottom);
    root->SetGap(kRootGap);

    root->AddChild(MakeLabel(0, TR(L"问题"), kColorLabel), Hint().Fixed(kLabelH));
    std::unique_ptr<DuiEdit> question(new DuiEdit());
    question->SetCtrlId(kQaEditIdQuestion);
    question->SetPlaceholder(TR(L"例如：报销流程怎么走？"));
    question->SetBgColor(kColorWhite);
    question->SetMaxLength(kQuestionMaxLen);
    root->AddChild(std::unique_ptr<DuiControl>(question.release()), Hint().Fixed(kEditH));

    root->AddChild(MakeLabel(0, TR(L"答案（可以写网址，群成员点了用浏览器打开）"), kColorLabel), Hint().Fixed(kLabelH));
    std::unique_ptr<DuiEdit> answer(new DuiEdit());
    answer->SetCtrlId(kQaEditIdAnswer);
    answer->SetBgColor(kColorWhite);
    answer->SetMaxLength(kAnswerMaxLen);
    answer->SetMultiLine(true);
    answer->SetWordWrap(true);
    root->AddChild(std::unique_ptr<DuiControl>(answer.release()), Hint().Weight(1));

    root->AddChild(MakeLabel(kQaEditIdError, _T(""), kColorError), Hint().Fixed(kErrorH));

    std::unique_ptr<DuiHBox> buttons(new DuiHBox());
    buttons->SetCtrlId(kQaEditIdButtonRow);
    buttons->SetGap(kButtonGap);
    buttons->AddChild(std::unique_ptr<DuiControl>(new DuiLabel()), Hint().Weight(1));
    buttons->AddChild(MakeButton(kQaEditIdCancel, TR(L"取消"), DuiButton::Variant::Default), Hint().Fixed(kButtonW));
    buttons->AddChild(MakeButton(kQaEditIdSave, TR(L"保存"), DuiButton::Variant::Primary), Hint().Fixed(kButtonW));
    root->AddChild(std::unique_ptr<DuiControl>(buttons.release()), Hint().Fixed(kButtonH));
    return std::unique_ptr<DuiControl>(root.release());
}

} // namespace balloonchatwnd
