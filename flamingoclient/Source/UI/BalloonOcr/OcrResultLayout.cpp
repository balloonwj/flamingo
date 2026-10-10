/**
 *  OcrResultLayout.cpp —— 「图片文字识别」结果窗口的客户区控件树与状态文字，见 OcrResultLayout.h。
 *
 *  本 .cpp 不 include Flamingo 的 stdafx.h —— 它要被 Flamingo.vcxproj 与 FlamingoUnit.vcxproj 共用
 *  （布局单测按设计尺寸排版）。在 Flamingo.vcxproj 里需标记 PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-10-05
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

#include "OcrResultLayout.h"
#include "../../Feature/Ocr/OcrRecognizer.h"   // ocr::OcrTaskCode
#include "../../../FlamingoOcr/FlamingoOcrApi.h"
#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiComboBox.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiRichEdit.h"
#include "../../Common/I18n/I18n.h"   // TR：界面文字的多语言查找

using namespace balloonwjui;

namespace balloonocr {

namespace {

// ---- 颜色 ----
const COLORREF kColorWhite   = RGB(0xFF, 0xFF, 0xFF);
const COLORREF kColorCaption = RGB(0x8A, 0x8A, 0x8A);   // 「识别语言」小标题与状态文字

// ---- 版式（逻辑像素）----
const int kPadH          = 20;    // 客户区左右内边距
const int kPadTop        = 4;     // 客户区上内边距（标题栏下方）
const int kPadBottom     = 16;    // 客户区下内边距
const int kRootGap       = 10;    // 各行之间的间距
const int kToolRowH      = 30;    // 第一行（识别语言与状态）的高
const int kToolRowGap    = 8;     // 第一行里各控件之间的间距
const int kCaptionW      = 64;    // 「识别语言」小标题的宽（英文译文较长，按「只增不减」留出余量）
const int kLanguageW     = 200;   // 识别语言下拉框的宽
const int kButtonRowH    = 34;    // 底部按钮行高
const int kButtonGap     = 10;    // 底部按钮之间的间距
const int kRetryW        = 96;    // 「重新识别」宽
const int kCopyW         = 96;    // 「复制全部」宽
const int kOpenW         = 96;    // 「打开图片」宽

// 造一个底部按钮
std::unique_ptr<DuiControl> MakeButton(UINT id, LPCTSTR text, DuiButton::Variant variant)
{
    std::unique_ptr<DuiButton> button(new DuiButton());
    button->SetButtonType(DuiButton::StylePushButton);
    button->SetVariant(variant);
    button->SetCtrlId(id);
    button->SetText(text);
    return std::unique_ptr<DuiControl>(button.release());
}

// 造一个灰色单行文字
std::unique_ptr<DuiLabel> MakeGrayLabel(UINT id, LPCTSTR text, UINT align)
{
    std::unique_ptr<DuiLabel> label(new DuiLabel());
    label->SetCtrlId(id);
    label->SetText(text);
    label->SetTextColor(kColorCaption);
    label->SetTextAlign(align | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    return label;
}

} // anonymous namespace

std::unique_ptr<DuiControl> BuildOcrResultBody()
{
    typedef DuiLayout::Hint Hint;

    std::unique_ptr<DuiVBox> root(new DuiVBox());
    root->SetBgColor(kColorWhite);
    root->SetPadding(kPadH, kPadTop, kPadH, kPadBottom);
    root->SetGap(kRootGap);

    //第一行：识别语言 + 下拉框 + 状态文字（靠右）
    std::unique_ptr<DuiHBox> tools(new DuiHBox());
    tools->SetGap(kToolRowGap);
    tools->AddChild(std::unique_ptr<DuiControl>(MakeGrayLabel(kOcrIdLanguageCaption, TR(L"识别语言"), DT_LEFT).release()),
                    Hint().Fixed(kCaptionW));
    std::unique_ptr<DuiComboBox> language(new DuiComboBox());
    language->SetCtrlId(kOcrIdLanguage);
    language->SetEditable(false);
    tools->AddChild(std::unique_ptr<DuiControl>(language.release()), Hint().Fixed(kLanguageW));
    tools->AddChild(std::unique_ptr<DuiControl>(MakeGrayLabel(kOcrIdStatus, L"", DT_RIGHT).release()), Hint().Weight(1));
    root->AddChild(std::unique_ptr<DuiControl>(tools.release()), Hint().Fixed(kToolRowH));

    //识别结果：可编辑（识别错的字可以就地改）、可选取，占满余下的高度
    std::unique_ptr<DuiRichEdit> text(new DuiRichEdit());
    text->SetCtrlId(kOcrIdText);
    text->SetMultiLine(true);
    text->SetWordWrap(true);
    text->SetShowBorder(true);
    text->SetPlaceholder(TR(L"识别出的文字会显示在这里，可以直接修改后再复制。"));
    //最小高度由窗口的最小尺寸（kOcrResultMinHeight）保证，布局单测 OCRW1 检查
    root->AddChild(std::unique_ptr<DuiControl>(text.release()), Hint().Weight(1));

    //底部按钮行：左侧「打开图片」，右侧「重新识别 / 复制全部」
    std::unique_ptr<DuiHBox> buttons(new DuiHBox());
    buttons->SetGap(kButtonGap);
    buttons->AddChild(MakeButton(kOcrIdOpen, TR(L"打开图片"), DuiButton::Variant::Default), Hint().Fixed(kOpenW));
    buttons->AddChild(std::unique_ptr<DuiControl>(new DuiLabel()), Hint().Weight(1));
    buttons->AddChild(MakeButton(kOcrIdRetry, TR(L"重新识别"), DuiButton::Variant::Default), Hint().Fixed(kRetryW));
    buttons->AddChild(MakeButton(kOcrIdCopy, TR(L"复制全部"), DuiButton::Variant::Primary), Hint().Fixed(kCopyW));
    root->AddChild(std::unique_ptr<DuiControl>(buttons.release()), Hint().Fixed(kButtonRowH));

    return std::unique_ptr<DuiControl>(root.release());
}

std::wstring OcrResultStatusText(int code, size_t lineCount)
{
    switch (code)
    {
    //识别成功：有文字时报行数，没有时说明
    case flamingoocr::ocr_ok:
        if (lineCount == 0)
            return TR(L"没有识别到文字");
        return TRF(L"识别完成，共 {0} 行", static_cast<unsigned int>(lineCount));

    //用户换了语言或关闭了窗口，上一次识别被取消
    case flamingoocr::ocr_cancelled:
        return TR(L"已取消");

    //图片文件打不开或格式不受支持
    case ocr::ocr_task_decode_failed:
        return TR(L"图片无法打开，可能已被删除或格式不受支持");

    //识别模块或引擎不可用
    case ocr::ocr_task_no_engine:
    case flamingoocr::ocr_unavailable:
        return TR(L"文字识别组件不可用");

    //所选识别语言不在本机可用列表里
    case flamingoocr::ocr_language_unsupported:
        return TR(L"所选识别语言不可用");

    //图片过大导致内存不足
    case flamingoocr::ocr_out_of_memory:
        return TR(L"图片太大，内存不足");

    //参数错误、引擎内部错误及不认识的结果码
    default:
        return TR(L"识别失败，请重试");
    }
}

std::wstring OcrRecognizingText()
{
    return TR(L"正在识别…");
}

std::wstring OcrNoImageText()
{
    return TR(L"请打开一张图片");
}

std::wstring NormalizeLineBreaks(const std::wstring& text)
{
    std::wstring out;
    out.reserve(text.size() + text.size() / 8);
    for (size_t i = 0; i < text.size(); ++i)
    {
        const wchar_t ch = text[i];
        if (ch == L'\r')
        {
            out += L"\r\n";
            if (i + 1 < text.size() && text[i + 1] == L'\n')
                ++i;   //已经是 \r\n
        }
        else if (ch == L'\n')
        {
            out += L"\r\n";
        }
        else
        {
            out += ch;
        }
    }
    return out;
}

} // namespace balloonocr
