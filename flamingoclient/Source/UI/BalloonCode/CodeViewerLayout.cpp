/**
 *  CodeViewerLayout.cpp —— 代码查看窗的控件树、行号栏与正文编辑框，见 CodeViewerLayout.h。
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

#include <richedit.h>   // EM_POSFROMCHAR 等

#include <string>

#include "CodeViewerLayout.h"
#include "CodeViewerModel.h"   // LineOfPosition、kCodeViewerFontHalfPoints
#include "CodeNotesPane.h"     // CodeNotesSplitter / CodeNotesPane：右侧只读的批注栏
#include "../../../third-party/balloonui/balloonui/DuiHost.h"
#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiComboBox.h"
#include "../../Common/I18n/I18n.h"   // TR：界面文字的多语言查找
#include "../../Common/I18n/I18nLayout.h"   // DefaultButtonWidthForText：「批注 (N)」按译文加宽

using namespace balloonwjui;

namespace ballooncode {

namespace {

// ---- 颜色 ----
const COLORREF kColorWhite        = RGB(0xFF, 0xFF, 0xFF);
const COLORREF kColorCaption      = RGB(0x8A, 0x8A, 0x8A);   //「语言」小标题
const COLORREF kGutterBg          = RGB(0xF7, 0xF7, 0xF5);   //行号栏底色
const COLORREF kGutterLine        = RGB(0xE3, 0xE1, 0xDC);   //行号栏右侧的分隔线
const COLORREF kGutterText        = RGB(0xA8, 0xA6, 0xA2);   //行号
const COLORREF kGutterMark        = RGB(0xF0, 0x9A, 0x3E);   //批注所在行的竖条标记
const COLORREF kAnnotateStatusText = RGB(0x8A, 0x8A, 0x8A);  //底部一行的状态文字

// ---- 版式（逻辑像素）----
const int kPadH          = 12;    //客户区左右内边距
const int kPadTop        = 4;     //客户区上内边距（标题栏下方）
const int kPadBottom     = 12;    //客户区下内边距
const int kRootGap       = 8;     //工具条与正文之间的间距
const int kToolRowH      = 30;    //工具条高
const int kToolGap       = 8;     //工具条里各控件之间的间距
const int kCaptionW      = 40;    //「语言」小标题宽（英文译文较长，按「只增不减」留出余量）
const int kLanguageW     = 150;   //语言下拉框宽
const int kWrapW         = 100;   //「自动换行」勾选框宽
const int kCopyW         = 96;    //「复制全部」宽
const int kSaveW         = 96;    //「另存为…」宽
const int kGutterPadLeft  = 8;    //行号左侧留白
const int kGutterPadRight = 8;    //行号与分隔线之间的留白
const int kHalfPointsPerInch = 144;   //一英寸有 144 个半磅（72 磅 × 2）
const int kDefaultDpi    = 96;    //取不到窗口 DPI 时按 96 计
const wchar_t* const kGutterFontName = L"Consolas";   //行号字体，与正文相同
const int kGutterMarkW    = 3;     //批注竖条的宽度（像素），贴着行号栏左边
const int kNotesButtonW   = 88;    //工具条「批注 (N)」按钮宽（按「批注 (50)」的译文加宽）
const int kAnnotateW      = 88;    //工具条「添加批注」按钮宽（按译文加宽）
const int kAnnotateBarH   = 30;    //底部一行（状态文字与「发送」）的高，与工具条同高
const int kAnnotateSendW  = 80;    //底部「发送」按钮宽（按译文加宽）
const unsigned int kNotesLabelWidest = 50;   //「批注 (N)」按 N 为两位数时的文字宽度留宽（批注最多 50 条）

// 行高未设置时的估算：13 像素字号的等宽字体，行高约为字号的 1.2 倍（分子 / 分母）
const int kMarkFallbackFontPx = 13;
const int kMarkLineHeightNum  = 6;
const int kMarkLineHeightDen  = 5;

//按字号建行号字体
HFONT CreateGutterFont(int fontPx)
{
    return ::CreateFontW(-fontPx, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_TT_PRECIS,
                         CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, kGutterFontName);
}

//一个正整数的十进制位数
int DecimalDigits(int value)
{
    const int kBase = 10;
    int digits = 1;
    while (value >= kBase)
    {
        value /= kBase;
        ++digits;
    }
    return digits;
}

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

} // anonymous namespace

CodeLineGutter::CodeLineGutter()
    : m_edit(NULL)
    , m_followEngine(false)
    , m_fontPx(0)
{
}

void CodeLineGutter::SetFollowEngineLines(bool follow)
{
    m_followEngine = follow;
    Invalidate();
}

void CodeLineGutter::SetFontPx(int fontPx)
{
    m_fontPx = fontPx;
    Invalidate();
}

void CodeLineGutter::SetMarkedRanges(const std::vector<std::pair<int, int> >& ranges)
{
    m_marked = ranges;
    Invalidate();
}

bool CodeLineGutter::IsLineMarked(int line) const
{
    for (size_t i = 0; i < m_marked.size(); ++i)
    {
        if (line >= m_marked[i].first && line <= m_marked[i].second)
            return true;
    }
    return false;
}

long CodeLineGutter::LineStartCp(int line) const
{
    if (m_followEngine)
        return (long)m_edit->SendMessageToEngine(EM_LINEINDEX, (WPARAM)line, 0);
    return m_lineStarts[(size_t)line];
}

void CodeLineGutter::SetEdit(DuiRichEdit* edit)
{
    m_edit = edit;
}

void CodeLineGutter::SetLineStarts(const std::vector<long>& starts)
{
    m_lineStarts = starts;
    Invalidate();
}

int CodeLineGutter::FontPxForDpi(int dpi)
{
    if (dpi <= 0)
        dpi = kDefaultDpi;
    return ::MulDiv(kCodeViewerFontHalfPoints, dpi, kHalfPointsPerInch);
}

int CodeLineGutter::WidthForLines(int lines, int fontPx)
{
    int digitW = fontPx / 2;   //量不到时的估计：等宽字体的数字约为字号的一半宽
    HDC hdc = ::GetDC(NULL);
    if (hdc != NULL)
    {
        HFONT font = CreateGutterFont(fontPx);
        HGDIOBJ old = ::SelectObject(hdc, font);
        SIZE size = { 0, 0 };
        if (::GetTextExtentPoint32W(hdc, L"0", 1, &size) && size.cx > 0)
            digitW = size.cx;
        ::SelectObject(hdc, old);
        ::DeleteObject(font);
        ::ReleaseDC(NULL, hdc);
    }
    return kGutterPadLeft + DecimalDigits(lines > 0 ? lines : 1) * digitW + kGutterPadRight;
}

void CodeLineGutter::OnPaint(HDC hdc, const RECT& rcDirty)
{
    (void)rcDirty;
    const RECT& rc = GetRect();
    HBRUSH bg = ::CreateSolidBrush(kGutterBg);
    ::FillRect(hdc, &rc, bg);
    ::DeleteObject(bg);
    HBRUSH line = ::CreateSolidBrush(kGutterLine);
    RECT sep = { rc.right - 1, rc.top, rc.right, rc.bottom };
    ::FillRect(hdc, &sep, line);
    ::DeleteObject(line);
    if (m_edit == NULL || (!m_followEngine && m_lineStarts.empty()))
        return;

    const int dpi = (GetHost() != NULL) ? GetHost()->GetDpi() : kDefaultDpi;
    HFONT font = CreateGutterFont(m_fontPx > 0 ? m_fontPx : FontPxForDpi(dpi));
    const int saved = ::SaveDC(hdc);
    ::IntersectClipRect(hdc, rc.left, rc.top, rc.right, rc.bottom);
    ::SelectObject(hdc, font);
    ::SetBkMode(hdc, TRANSPARENT);
    ::SetTextColor(hdc, kGutterText);
    TEXTMETRICW tm;
    ::ZeroMemory(&tm, sizeof(tm));
    ::GetTextMetricsW(hdc, &tm);

    //从第一个可见的视觉行所在的逻辑行开始，逐行向引擎查询起点的纵坐标（宿主客户区坐标），超出行号栏底边即停。
    //按视觉行编号时视觉行就是逻辑行；否则由第一个可见视觉行的起点反查逻辑行
    const LRESULT firstVisible = m_edit->SendMessageToEngine(EM_GETFIRSTVISIBLELINE, 0, 0);
    const int lineCount = m_followEngine ? (int)m_edit->SendMessageToEngine(EM_GETLINECOUNT, 0, 0)
                                         : (int)m_lineStarts.size();
    int first = (int)firstVisible;
    if (!m_followEngine)
    {
        const LRESULT firstCp = m_edit->SendMessageToEngine(EM_LINEINDEX, (WPARAM)firstVisible, 0);
        first = LineOfPosition(m_lineStarts, (long)firstCp);
    }
    HBRUSH mark = m_marked.empty() ? NULL : ::CreateSolidBrush(kGutterMark);
    for (int i = (first > 0 ? first : 0); i < lineCount; ++i)
    {
        POINTL pos = { 0, 0 };
        m_edit->SendMessageToEngine(EM_POSFROMCHAR, (WPARAM)&pos, (LPARAM)LineStartCp(i));
        if (pos.y > rc.bottom)
            break;
        //这一行的下边：下一行的起点；最后一行按行号字体的行高估计
        long nextY = pos.y + tm.tmHeight;
        if (i + 1 < lineCount)
        {
            POINTL next = { 0, 0 };
            m_edit->SendMessageToEngine(EM_POSFROMCHAR, (WPARAM)&next, (LPARAM)LineStartCp(i + 1));
            if (next.y > pos.y)
                nextY = next.y;
        }
        if (nextY < rc.top)
            continue;
        if (mark != NULL && IsLineMarked(i))
        {
            RECT bar = { rc.left, pos.y, rc.left + kGutterMarkW, nextY };
            ::FillRect(hdc, &bar, mark);
        }
        const std::wstring number = std::to_wstring(static_cast<unsigned long long>(i + 1));
        RECT r = { rc.left, pos.y, rc.right - kGutterPadRight, pos.y + tm.tmHeight };
        ::DrawTextW(hdc, number.c_str(), -1, &r, DT_RIGHT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);
    }
    if (mark != NULL)
        ::DeleteObject(mark);
    ::RestoreDC(hdc, saved);
    ::DeleteObject(font);
}

CodeMarkedEdit::CodeMarkedEdit()
    : m_lineHeight(0)
    , m_markPass(false)
    , m_selChangeMsg(0)
    , m_selChangeParam(0)
{
}

void CodeMarkedEdit::SetSelChangeMessage(UINT msg, WPARAM wParam)
{
    m_selChangeMsg = msg;
    m_selChangeParam = wParam;
}

HRESULT CodeMarkedEdit::TxSiteNotify(DWORD iNotify, void* pv)
{
    //选区变化：这是在引擎的回调栈里，不能当场查询选区，投递消息让窗口稍后读取
    if (iNotify == EN_SELCHANGE && m_selChangeMsg != 0 && GetHost() != NULL && GetHost()->IsWindow())
        ::PostMessage(GetHost()->m_hWnd, m_selChangeMsg, m_selChangeParam, 0);
    return DuiRichEdit::TxSiteNotify(iNotify, pv);
}

int CodeFontLineHeight(int fontPx)
{
    HDC dc = ::GetDC(NULL);
    if (dc == NULL)
        return 0;
    HFONT font = CreateGutterFont(fontPx);
    HGDIOBJ old = ::SelectObject(dc, font);
    TEXTMETRICW tm;
    ::ZeroMemory(&tm, sizeof(tm));
    const BOOL ok = ::GetTextMetricsW(dc, &tm);
    ::SelectObject(dc, old);
    ::DeleteObject(font);
    ::ReleaseDC(NULL, dc);
    return ok ? (int)tm.tmHeight : 0;
}

void CodeMarkedEdit::SetMarkedRanges(const std::vector<std::pair<int, int> >& ranges)
{
    if (ranges == m_marked)
        return;
    m_marked = ranges;
    Invalidate();
}

void CodeMarkedEdit::SetMarkLineStarts(const std::vector<long>& starts)
{
    m_lineStarts = starts;
    Invalidate();
}

void CodeMarkedEdit::SetLineHeight(int px)
{
    m_lineHeight = px;
}

long CodeMarkedEdit::MarkLineStart(int line) const
{
    if (!m_lineStarts.empty())
        return m_lineStarts[(size_t)line];
    //const 方法里不能调非 const 的 SendMessageToEngine：经 const_cast 查询，只读不改
    return (long)const_cast<CodeMarkedEdit*>(this)->SendMessageToEngine(EM_LINEINDEX, (WPARAM)line, 0);
}

std::vector<RECT> CodeMarkedEdit::MarkedBands()
{
    std::vector<RECT> bands;
    if (m_marked.empty() || IsShowingPlaceholder())
        return bands;
    const RECT& rc = GetRect();
    const int visualLines = (int)SendMessageToEngine(EM_GETLINECOUNT, 0, 0);
    const int lineCount = m_lineStarts.empty() ? visualLines : (int)m_lineStarts.size();
    const int lineHeight = (m_lineHeight > 0) ? m_lineHeight
                                              : kMarkFallbackFontPx * kMarkLineHeightNum / kMarkLineHeightDen;
    for (size_t k = 0; k < m_marked.size(); ++k)
    {
        const int from = m_marked[k].first;
        const int to = m_marked[k].second;
        if (from < 0 || to < from || from >= lineCount)
            continue;
        //上边：起始行的起点；下边：结束行下一行的起点。结束行是最后一行时，取最后一个视觉行的顶边再加一个行高
        POINTL top = { 0, 0 };
        SendMessageToEngine(EM_POSFROMCHAR, (WPARAM)&top, (LPARAM)MarkLineStart(from));
        long bottom = 0;
        if (to + 1 < lineCount)
        {
            POINTL next = { 0, 0 };
            SendMessageToEngine(EM_POSFROMCHAR, (WPARAM)&next, (LPARAM)MarkLineStart(to + 1));
            bottom = next.y;
        }
        else
        {
            const LRESULT lastStart = SendMessageToEngine(EM_LINEINDEX, (WPARAM)(visualLines > 0 ? visualLines - 1 : 0), 0);
            POINTL last = { 0, 0 };
            SendMessageToEngine(EM_POSFROMCHAR, (WPARAM)&last, lastStart);
            bottom = last.y + lineHeight;
        }
        RECT band = { rc.left, (LONG)top.y, rc.right, (LONG)bottom };
        RECT visible;
        if (band.bottom > band.top && ::IntersectRect(&visible, &band, &rc))
            bands.push_back(visible);
    }
    return bands;
}

void CodeMarkedEdit::OnPaint(HDC hdc, const RECT& rcDirty)
{
    DuiRichEdit::OnPaint(hdc, rcDirty);
    if (!IsVisible() || m_marked.empty())
        return;
    //被批注的行：裁剪到这些行的范围内，以浅黄底色再画一遍（底色、文字、光标、滚动条依次重画，次序与常规绘制相同）
    const std::vector<RECT> bands = MarkedBands();
    for (size_t i = 0; i < bands.size(); ++i)
    {
        RECT dirty;
        if (!::IntersectRect(&dirty, &bands[i], &rcDirty))
            continue;
        const int saved = ::SaveDC(hdc);
        ::IntersectClipRect(hdc, dirty.left, dirty.top, dirty.right, dirty.bottom);
        m_markPass = true;
        DuiRichEdit::OnPaint(hdc, dirty);
        m_markPass = false;
        ::RestoreDC(hdc, saved);
    }
}

COLORREF CodeMarkedEdit::FillColor() const
{
    if (m_markPass)
        return kCodeNoteLineBg;
    return DuiRichEdit::FillColor();
}

CodeViewerEdit::CodeViewerEdit()
    : m_gutter(NULL)
    , m_addNoteEnabled(true)
{
}

void CodeViewerEdit::SetGutter(CodeLineGutter* gutter)
{
    m_gutter = gutter;
}

void CodeViewerEdit::SetAddNoteEnabled(bool enabled)
{
    m_addNoteEnabled = enabled;
}

void CodeViewerEdit::OnBuildContextMenu(std::vector<DuiRichEditMenuItem>& items)
{
    DuiRichEdit::OnBuildContextMenu(items);
    for (size_t i = 0; i < items.size(); ++i)
    {
        if (items[i].m_id == kCodeViewerMenuAddNote)
            items[i].m_enabled = m_addNoteEnabled;
    }
}

void CodeViewerEdit::TxSiteScrollInfoChanged(bool bVertical)
{
    DuiRichEdit::TxSiteScrollInfoChanged(bVertical);
    if (m_gutter != NULL)
        m_gutter->Invalidate();
}

std::unique_ptr<DuiControl> BuildCodeViewerBody(int gutterWidth)
{
    typedef DuiLayout::Hint Hint;

    std::unique_ptr<DuiVBox> root(new DuiVBox());
    root->SetBgColor(kColorWhite);
    root->SetPadding(kPadH, kPadTop, kPadH, kPadBottom);
    root->SetGap(kRootGap);

    //工具条：语言、自动换行，右侧复制全部、另存为
    std::unique_ptr<DuiHBox> tools(new DuiHBox());
    tools->SetGap(kToolGap);
    std::unique_ptr<DuiLabel> caption(new DuiLabel());
    caption->SetCtrlId(kCodeViewerIdLanguageCaption);
    caption->SetText(TR(L"语言"));
    caption->SetTextColor(kColorCaption);
    caption->SetTextAlign(DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    tools->AddChild(std::unique_ptr<DuiControl>(caption.release()), Hint().Fixed(kCaptionW));
    std::unique_ptr<DuiComboBox> language(new DuiComboBox());
    language->SetCtrlId(kCodeViewerIdLanguage);
    language->SetEditable(false);
    tools->AddChild(std::unique_ptr<DuiControl>(language.release()), Hint().Fixed(kLanguageW));
    std::unique_ptr<DuiButton> wrap(new DuiButton());
    wrap->SetButtonType(DuiButton::StyleCheckbox);
    wrap->SetCtrlId(kCodeViewerIdWrap);
    wrap->SetText(TR(L"自动换行"));
    wrap->SetCheck(false, false);
    tools->AddChild(std::unique_ptr<DuiControl>(wrap.release()), Hint().Fixed(kWrapW));
    tools->AddChild(std::unique_ptr<DuiControl>(new DuiLabel()), Hint().Weight(1));
    //「批注 (N)」：展开或收起右侧的批注栏；没有批注时窗口把它隐藏（docs/代码片段方案.md 第 10.5 节）
    const std::wstring widestNotes = TRF(L"批注 ({0})", kNotesLabelWidest);
    std::unique_ptr<DuiControl> notes = MakeButton(kCodeViewerIdNotes, TR(L"批注"), DuiButton::Variant::Default);
    notes->SetVisible(false);
    tools->AddChild(std::move(notes), Hint().Fixed(i18n::DefaultButtonWidthForText(widestNotes.c_str(), kNotesButtonW)));
    //「添加批注」：给这条代码片段追加批注；只在单聊窗、群聊窗打开时由窗口显示（docs/代码片段方案.md 第 11.3 节）
    std::unique_ptr<DuiControl> annotate = MakeButton(kCodeViewerIdAnnotate, TR(L"添加批注"), DuiButton::Variant::Default);
    annotate->SetVisible(false);
    tools->AddChild(std::move(annotate), Hint().Fixed(i18n::DefaultButtonWidthForText(TR(L"添加批注"), kAnnotateW)));
    tools->AddChild(MakeButton(kCodeViewerIdCopy, TR(L"复制全部"), DuiButton::Variant::Primary), Hint().Fixed(kCopyW));
    tools->AddChild(MakeButton(kCodeViewerIdSave, TR(L"另存为…"), DuiButton::Variant::Default), Hint().Fixed(kSaveW));
    root->AddChild(std::unique_ptr<DuiControl>(tools.release()), Hint().Fixed(kToolRowH));

    //正文：左侧行号栏，右侧只读编辑框（富文本模式，颜色与制表位来自 RTF）
    std::unique_ptr<DuiHBox> body(new DuiHBox());
    std::unique_ptr<CodeLineGutter> gutter(new CodeLineGutter());
    gutter->SetCtrlId(kCodeViewerIdGutter);
    std::unique_ptr<CodeViewerEdit> edit(new CodeViewerEdit());
    edit->SetCtrlId(kCodeViewerIdEdit);
    edit->SetMultiLine(true);
    edit->SetWordWrap(false);
    edit->SetReadOnly(true);
    edit->SetShowBorder(false);
    edit->SetHScrollPolicy(DuiRichEdit::kScrollBarAuto);
    edit->SetVScrollPolicy(DuiRichEdit::kScrollBarAuto);
    gutter->SetEdit(edit.get());
    edit->SetGutter(gutter.get());
    body->AddChild(std::unique_ptr<DuiControl>(gutter.release()), Hint().Fixed(gutterWidth));
    body->AddChild(std::unique_ptr<DuiControl>(edit.release()), Hint().Weight(1));

    //正文与右侧只读的批注栏左右分栏；没有批注时收起，窗口载入批注后再设
    std::unique_ptr<CodeNotesSplitter> split(new CodeNotesSplitter());
    split->SetCtrlId(kCodeViewerIdSplit);
    split->SetPane(0, std::unique_ptr<DuiControl>(body.release()));
    std::unique_ptr<CodeNotesPane> pane(new CodeNotesPane());
    pane->SetCtrlId(kCodeViewerIdNotesPane);
    pane->SetReadOnly(true);
    pane->SetNotes(std::vector<codesnippet::CodeNote>());
    split->SetPane(1, std::unique_ptr<DuiControl>(pane.release()));
    split->SetCollapsed(true);
    root->AddChild(std::unique_ptr<DuiControl>(split.release()), Hint().Weight(1));

    //底部一行：状态文字与「发送」（docs/代码片段方案.md 第 12 节）；默认隐藏，能追加批注时窗口显示它
    std::unique_ptr<DuiHBox> bar(new DuiHBox());
    bar->SetCtrlId(kCodeViewerIdAnnotateBar);
    bar->SetGap(kToolGap);
    std::unique_ptr<DuiLabel> status(new DuiLabel());
    status->SetCtrlId(kCodeViewerIdAnnotateStatus);
    status->SetTextColor(kAnnotateStatusText);
    status->SetTextAlign(DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    bar->AddChild(std::unique_ptr<DuiControl>(status.release()), Hint().Weight(1));
    bar->AddChild(MakeButton(kCodeViewerIdAnnotateSend, TR(L"发送"), DuiButton::Variant::Primary),
                  Hint().Fixed(i18n::DefaultButtonWidthForText(TR(L"发送"), kAnnotateSendW)));
    bar->SetVisible(false);
    root->AddChild(std::unique_ptr<DuiControl>(bar.release()), Hint().Fixed(kAnnotateBarH));

    return std::unique_ptr<DuiControl>(root.release());
}

} // namespace ballooncode
