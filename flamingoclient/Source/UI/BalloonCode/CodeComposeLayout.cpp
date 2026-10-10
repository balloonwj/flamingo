/**
 *  CodeComposeLayout.cpp —— 「发送代码」窗口的控件树与状态文字，见 CodeComposeLayout.h。
 *
 *  本 .cpp 不 include Flamingo 的 stdafx.h —— 它要被 Flamingo.vcxproj 与 FlamingoUnit.vcxproj 共用
 *  （布局单测按设计尺寸排版）。在 Flamingo.vcxproj 里需标记 PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-10-06
 */

// ---- ATL / WTL 头文件要求的前置依赖，与 OcrResultLayout.cpp 相同 ----
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

#include "CodeComposeLayout.h"
#include "CodeNotesPane.h"   // CodeNotesSplitter / CodeNotesPane：右侧的批注栏
#include "../../Common/Code/CodeLexer.h"
#include "../../Common/I18n/I18n.h"   // TR / TRF：界面文字的多语言查找
#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiComboBox.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiRichEdit.h"
#include "../../Common/I18n/I18nLayout.h"   // GrowFixedWidthToText：「行号」勾选框按译文加宽

using namespace balloonwjui;

namespace ballooncode {

namespace {

// ---- 颜色 ----
const COLORREF kColorWhite   = RGB(0xFF, 0xFF, 0xFF);
const COLORREF kColorCaption = RGB(0x8A, 0x8A, 0x8A);   //「语言」小标题、状态文字与底部提示
const COLORREF kColorFrame   = RGB(0xD9, 0xD9, 0xD9);   //行号栏与编辑框外框的边线

// ---- 版式（逻辑像素）----
const int kPadH       = 16;    //客户区左右内边距
const int kPadTop     = 4;     //客户区上内边距（标题栏下方）
const int kPadBottom  = 16;    //客户区下内边距
const int kRootGap    = 10;    //各行之间的间距
const int kToolRowH   = 30;    //第一行高
const int kToolGap    = 8;     //第一行里各控件之间的间距
const int kCaptionW   = 40;    //「语言」小标题宽（英文译文较长，按「只增不减」留出余量）
const int kLanguageW  = 160;   //语言下拉框宽
const int kButtonRowH = 34;    //底部按钮行高
const int kButtonGap  = 10;    //底部按钮之间的间距
const int kCancelW    = 88;    //「取消」宽
const int kSendW      = 88;    //「发送」宽
const int kLineNumbersW = 64;   //「行号」勾选框宽（含勾选框图形与文字）
const int kToolButtonW  = 72;   //工具行按钮（「格式化」）宽
const int kCheckboxPadPx = 24;  //勾选框按译文加宽时，文字之外还要留的宽度（勾选框图形加间距）
const int kMinGutterNumber = 99;   //行号栏至少按两位数留宽，免得 9 行到 10 行时宽度跳变
const unsigned int kNotesLabelWidest = 50;   //「批注 (N)」按钮按 N 为两位数时的文字宽度留宽（批注最多 50 条）
const int kUndoLinkW = 40;      //状态文字右侧「撤销」链接的宽度（按译文加宽）
const int kLinkPadPx = 4;       //链接按译文加宽时，文字之外留的宽度

//造一个按钮
std::unique_ptr<DuiControl> MakeButton(UINT id, LPCTSTR text, DuiButton::Variant variant)
{
    std::unique_ptr<DuiButton> button(new DuiButton());
    button->SetButtonType(DuiButton::StylePushButton);
    button->SetVariant(variant);
    button->SetCtrlId(id);
    button->SetText(text);
    return std::unique_ptr<DuiControl>(button.release());
}

//造一个灰色单行文字
std::unique_ptr<DuiControl> MakeGrayLabel(UINT id, LPCTSTR text)
{
    std::unique_ptr<DuiLabel> label(new DuiLabel());
    label->SetCtrlId(id);
    label->SetText(text);
    label->SetTextColor(kColorCaption);
    label->SetTextAlign(DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    return std::unique_ptr<DuiControl>(label.release());
}

} // anonymous namespace

CodeComposeEdit::CodeComposeEdit()
    : m_gutter(NULL)
    , m_addNoteEnabled(true)
    , m_formatEnabled(true)
{
}

void CodeComposeEdit::SetGutter(CodeLineGutter* gutter)
{
    m_gutter = gutter;
}

void CodeComposeEdit::SetAddNoteEnabled(bool enabled)
{
    m_addNoteEnabled = enabled;
}

void CodeComposeEdit::SetFormatEnabled(bool enabled)
{
    m_formatEnabled = enabled;
}

void CodeComposeEdit::TxSiteScrollInfoChanged(bool bVertical)
{
    DuiRichEdit::TxSiteScrollInfoChanged(bVertical);
    if (m_gutter != NULL)
        m_gutter->Invalidate();
}

HRESULT CodeComposeEdit::TxSiteNotify(DWORD iNotify, void* pv)
{
    if (iNotify == EN_CHANGE && m_gutter != NULL)
        m_gutter->Invalidate();
    return CodeMarkedEdit::TxSiteNotify(iNotify, pv);
}

void CodeComposeEdit::OnBuildContextMenu(std::vector<DuiRichEditMenuItem>& items)
{
    DuiRichEdit::OnBuildContextMenu(items);
    for (size_t i = 0; i < items.size(); ++i)
    {
        if (items[i].m_id == kCodeComposeMenuAddNote)
            items[i].m_enabled = m_addNoteEnabled;
        else if (items[i].m_id == kCodeComposeMenuFormat)
            items[i].m_enabled = m_formatEnabled;
    }
}

int CodeComposeGutterWidth(int lines, int fontPx)
{
    return CodeLineGutter::WidthForLines(lines > kMinGutterNumber ? lines : kMinGutterNumber, fontPx);
}

void ExpandSelectionToLines(const std::wstring& text, long selBegin, long selEnd, long& lineBegin, long& lineEnd)
{
    const long size = (long)text.size();
    if (selBegin < 0)
        selBegin = 0;
    if (selBegin > size)
        selBegin = size;
    if (selEnd < selBegin)
        selEnd = selBegin;
    if (selEnd > size)
        selEnd = size;
    //选区的结束位置正好在行首（前一个字符是换行）时，那一行不算在内
    long last = selEnd;
    if (selEnd > selBegin && text[(size_t)(selEnd - 1)] == L'\r')
        last = selEnd - 1;
    lineBegin = selBegin;
    while (lineBegin > 0 && text[(size_t)(lineBegin - 1)] != L'\r')
        --lineBegin;
    lineEnd = (last > selBegin) ? last : selBegin;
    while (lineEnd < size && text[(size_t)lineEnd] != L'\r')
        ++lineEnd;
}

std::unique_ptr<DuiControl> BuildCodeComposeBody()
{
    typedef DuiLayout::Hint Hint;

    std::unique_ptr<DuiVBox> root(new DuiVBox());
    root->SetBgColor(kColorWhite);
    root->SetPadding(kPadH, kPadTop, kPadH, kPadBottom);
    root->SetGap(kRootGap);

    //第一行：语言、「行号」勾选框、「格式化」按钮，剩下的宽度给状态文字
    std::unique_ptr<DuiHBox> tools(new DuiHBox());
    tools->SetGap(kToolGap);
    tools->AddChild(MakeGrayLabel(kCodeComposeIdLanguageCaption, TR(L"语言")), Hint().Fixed(kCaptionW));
    std::unique_ptr<DuiComboBox> language(new DuiComboBox());
    language->SetCtrlId(kCodeComposeIdLanguage);
    language->SetEditable(false);
    tools->AddChild(std::unique_ptr<DuiControl>(language.release()), Hint().Fixed(kLanguageW));
    //「行号」勾选框：默认勾选，窗口按用户配置再设一次
    std::unique_ptr<DuiButton> lineNumbers(new DuiButton());
    lineNumbers->SetButtonType(DuiButton::StyleCheckbox);
    lineNumbers->SetCtrlId(kCodeComposeIdLineNumbers);
    lineNumbers->SetText(TR(L"行号"));
    lineNumbers->SetCheck(true, false);
    DuiButton* lineNumbersRaw = lineNumbers.get();
    tools->AddChild(std::unique_ptr<DuiControl>(lineNumbers.release()), Hint().Fixed(kLineNumbersW));
    //译文比中文长时按文字宽度加宽（简体中文界面不变）
    i18n::GrowFixedWidthToText(lineNumbersRaw, TR(L"行号"), i18n::DefaultFontPixelHeight(), false, kCheckboxPadPx);
    //「格式化」按钮（docs/代码片段方案.md 第 10.4 节）
    tools->AddChild(MakeButton(kCodeComposeIdFormat, TR(L"格式化"), DuiButton::Variant::Default),
                    Hint().Fixed(i18n::DefaultButtonWidthForText(TR(L"格式化"), kToolButtonW)));
    //「批注」按钮（docs/代码片段方案.md 第 10.5 节）：按「批注 (50)」的宽度留宽，收起时改写成「批注 (N)」不会被截断
    const std::wstring widestNotes = TRF(L"批注 ({0})", kNotesLabelWidest);
    tools->AddChild(MakeButton(kCodeComposeIdNotes, TR(L"批注"), DuiButton::Variant::Default),
                    Hint().Fixed(i18n::DefaultButtonWidthForText(widestNotes.c_str(), kToolButtonW)));
    tools->AddChild(MakeGrayLabel(kCodeComposeIdStatus, L""), Hint().Weight(1));
    //「撤销」链接：删除批注之后才显示
    std::unique_ptr<DuiLabel> undo(new DuiLabel());
    undo->SetCtrlId(kCodeComposeIdUndoNote);
    undo->SetText(TR(L"撤销"));
    undo->SetMode(DuiLabel::ModeLink);
    undo->SetTextAlign(DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    undo->SetVisible(false);
    DuiLabel* undoRaw = undo.get();
    tools->AddChild(std::unique_ptr<DuiControl>(undo.release()), Hint().Fixed(kUndoLinkW));
    i18n::GrowFixedWidthToText(undoRaw, TR(L"撤销"), i18n::DefaultFontPixelHeight(), false, kLinkPadPx);
    root->AddChild(std::unique_ptr<DuiControl>(tools.release()), Hint().Fixed(kToolRowH));

    //编辑框：纯文本（粘贴时只取文字），不折行、可横向滚动；只能在写入文字之前切换纯文本模式。
    //左侧是行号栏（按视觉行编号：不折行时每个视觉行就是一个逻辑行），两者放在同一个带边框的框里
    std::unique_ptr<CodeComposeEdit> editor(new CodeComposeEdit());
    editor->SetCtrlId(kCodeComposeIdEditor);
    editor->SetPlainTextMode(true);
    editor->SetMultiLine(true);
    editor->SetWordWrap(false);
    editor->SetShowBorder(false);
    editor->SetHScrollPolicy(DuiRichEdit::kScrollBarAuto);
    editor->SetVScrollPolicy(DuiRichEdit::kScrollBarAuto);
    editor->SetMaxLength(0);
    editor->SetPlaceholder(TR(L"在这里输入或粘贴代码"));
    //右键菜单追加「格式化」「添加批注…」：选中后编辑框向窗口发 DUIN_RICHTEXT_MENUCOMMAND 通知
    editor->AppendContextMenuItem(kCodeComposeMenuFormat, TR(L"格式化"));
    editor->AppendContextMenuItem(kCodeComposeMenuAddNote, TR(L"添加批注…"));
    std::unique_ptr<CodeLineGutter> gutter(new CodeLineGutter());
    gutter->SetCtrlId(kCodeComposeIdGutter);
    gutter->SetFollowEngineLines(true);
    gutter->SetFontPx(kCodeComposeFontPx);
    gutter->SetEdit(editor.get());
    editor->SetGutter(gutter.get());
    std::unique_ptr<DuiHBox> code(new DuiHBox());
    code->AddChild(std::unique_ptr<DuiControl>(gutter.release()),
                   Hint().Fixed(CodeComposeGutterWidth(1, kCodeComposeFontPx)));
    code->AddChild(std::unique_ptr<DuiControl>(editor.release()), Hint().Weight(1));
    std::unique_ptr<DuiVBox> frame(new DuiVBox());
    frame->SetCtrlId(kCodeComposeIdCodeFrame);
    frame->SetBorderColor(kColorFrame);
    frame->SetBorderWidth(1.0f);
    frame->SetPadding(1, 1, 1, 1);
    frame->AddChild(std::unique_ptr<DuiControl>(code.release()), Hint().Weight(1));

    //代码区与右侧的批注栏左右分栏；没有批注时批注栏收起，窗口打开后按批注与用户配置再设
    std::unique_ptr<CodeNotesSplitter> split(new CodeNotesSplitter());
    split->SetCtrlId(kCodeComposeIdSplit);
    split->SetPane(0, std::unique_ptr<DuiControl>(frame.release()));
    std::unique_ptr<CodeNotesPane> pane(new CodeNotesPane());
    pane->SetCtrlId(kCodeComposeIdNotesPane);
    pane->SetNotes(std::vector<codesnippet::CodeNote>());
    split->SetPane(1, std::unique_ptr<DuiControl>(pane.release()));
    split->SetCollapsed(true);
    root->AddChild(std::unique_ptr<DuiControl>(split.release()), Hint().Weight(1));

    //底部：提示、取消、发送
    std::unique_ptr<DuiHBox> buttons(new DuiHBox());
    buttons->SetGap(kButtonGap);
    buttons->AddChild(MakeGrayLabel(kCodeComposeIdHint, TR(L"Ctrl+Enter 发送")), Hint().Weight(1));
    buttons->AddChild(MakeButton(kCodeComposeIdCancel, TR(L"取消"), DuiButton::Variant::Default), Hint().Fixed(kCancelW));
    buttons->AddChild(MakeButton(kCodeComposeIdSend, TR(L"发送"), DuiButton::Variant::Primary), Hint().Fixed(kSendW));
    root->AddChild(std::unique_ptr<DuiControl>(buttons.release()), Hint().Fixed(kButtonRowH));

    return std::unique_ptr<DuiControl>(root.release());
}

std::wstring CodeComposeStatusText(bool autoDetect, int lang, int lines, bool tooLarge)
{
    if (tooLarge)
        return TR(L"代码太长，超过了单条消息的上限，请删减后再发送");
    if (lines <= 0)
        return std::wstring();
    const wchar_t* name = codesnippet::CodeLanguageDisplayName(lang);
    const std::wstring display = (lang == codesnippet::code_lang_plain) ? std::wstring(TR(name)) : std::wstring(name);
    if (autoDetect)
        return TRF(L"自动识别为 {0}，共 {1} 行", display, static_cast<unsigned int>(lines));
    return TRF(L"共 {0} 行", static_cast<unsigned int>(lines));
}

std::wstring CodeAnnotateStatusText(const std::wstring& senderName, int lang, int lines, int newCount, bool tooLarge)
{
    //代码不能改，超限只可能是批注加多了
    if (tooLarge)
        return TR(L"代码与批注超过了单条消息的上限，请删减批注后再发送");
    const wchar_t* name = codesnippet::CodeLanguageDisplayName(lang);
    const std::wstring display = (lang == codesnippet::code_lang_plain) ? std::wstring(TR(name)) : std::wstring(name);
    const unsigned int lineCount = static_cast<unsigned int>(lines > 0 ? lines : 0);
    //整句模板，不把几段文字拼成一句（开发约定文档 UI 约定第 8 条）
    if (newCount <= 0)
    {
        if (senderName.empty())
            return TRF(L"{0} · {1} 行。添加或修改批注后才能发送", display, lineCount);
        return TRF(L"{0}的代码 · {1} · {2} 行。添加或修改批注后才能发送", senderName, display, lineCount);
    }
    if (senderName.empty())
        return TRF(L"{0} · {1} 行。已新增或修改 {2} 条批注", display, lineCount, static_cast<unsigned int>(newCount));
    return TRF(L"{0}的代码 · {1} · {2} 行。已新增或修改 {3} 条批注", senderName, display, lineCount,
               static_cast<unsigned int>(newCount));
}

} // namespace ballooncode
