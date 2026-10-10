/**
 *  SearchFilterTab.cpp —— 搜索分类筛选文字标签实现。
 *  balloonwj@qq.com   2026-06-01
 */
#include "stdafx.h"
#include "SearchFilterTab.h"
#include "../../Common/I18n/I18n.h"   // i18n::UiFontName：当前界面语言的界面字体

namespace balloonsearch {

namespace {

// 文字字号（逻辑像素）。
const int kTabTextPx = 13;
// 选中态橙色（与设计稿主色一致）。
const COLORREF kActiveColor = RGB(0xFF, 0x5A, 0x44);
// 普通态文字色。
const COLORREF kNormalColor = RGB(0x4A, 0x4A, 0x4A);
// 置灰态文字色。
const COLORREF kGrayedColor = RGB(0xC0, 0xC0, 0xC0);
// 下划线高度（逻辑像素）。
const int kUnderlineH = 2;
// 按文字排版时文字左右各留的空白（像素）。与宿主的标签间距合起来就是相邻两段文字的间隔；
// 取 4 是按主面板默认宽度 338 算的：五个标签都带两位数计数时（约 282 像素）加上左内距与
// 间距仍放得下。
const int kTabPadX = 4;
// 标签的最小宽度（像素）：两个字的标签不带计数时也不至于窄到不好点。
const int kTabMinWidth = 40;

// 用与绘制相同的字体（选中态的半粗体，比常规体略宽，按它量能保证切换选中时不必重排）
// 量一段文字的宽度（像素）。取不到字体时返回 0。
int MeasureTabText(const CString& text)
{
    if (text.IsEmpty())
    {
        return 0;
    }
    HDC hdc = ::CreateCompatibleDC(NULL);
    if (hdc == NULL)
    {
        return 0;
    }
    HFONT hFont = ::CreateFontW(
        -kTabTextPx, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, i18n::UiFontName());
    SIZE sz = { 0, 0 };
    if (hFont != NULL)
    {
        HFONT hOld = (HFONT)::SelectObject(hdc, hFont);
        ::GetTextExtentPoint32W(hdc, (LPCWSTR)text, text.GetLength(), &sz);
        ::SelectObject(hdc, hOld);
        ::DeleteObject(hFont);
    }
    ::DeleteDC(hdc);
    return sz.cx;
}

// 用 GDI 绘制单行文字（YaHei + ClearType），与主面板各控件口径一致。
void DrawTabText(HDC hdc, LPCWSTR text, const RECT& rc, int px, bool bold,
                 COLORREF color, UINT dtFlags)
{
    if (text == NULL || text[0] == L'\0')
    {
        return;
    }
    HFONT hFont = ::CreateFontW(
        -px, 0, 0, 0, bold ? FW_SEMIBOLD : FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, i18n::UiFontName());
    if (hFont == NULL)
    {
        return;
    }
    HFONT    hOld   = (HFONT)::SelectObject(hdc, hFont);
    int      oldBk  = ::SetBkMode(hdc, TRANSPARENT);
    COLORREF oldClr = ::SetTextColor(hdc, color);
    RECT r = rc;
    ::DrawTextW(hdc, text, -1, &r, dtFlags);
    ::SetTextColor(hdc, oldClr);
    ::SetBkMode(hdc, oldBk);
    ::SelectObject(hdc, hOld);
    ::DeleteObject(hFont);
}

} // anonymous namespace

SearchFilterTab::SearchFilterTab()
    : m_active(false)
    , m_showCount(false)
    , m_count(0)
    , m_grayed(false)
    , m_desiredWidth(kTabMinWidth)
{
}

void SearchFilterTab::SetLabel(LPCTSTR text)
{
    m_label = (text != NULL) ? text : _T("");
    RecomputeDesiredWidth();
    Invalidate();
}

SIZE SearchFilterTab::GetDesiredSize() const
{
    SIZE sz = { m_desiredWidth, 0 };
    return sz;
}

CString SearchFilterTab::DisplayText() const
{
    CString text = m_label;
    if (m_showCount)
    {
        CString cnt;
        cnt.Format(_T(" %d"), m_count);
        text += cnt;
    }
    return text;
}

void SearchFilterTab::RecomputeDesiredWidth()
{
    int w = MeasureTabText(DisplayText()) + 2 * kTabPadX;
    if (w < kTabMinWidth)
    {
        w = kTabMinWidth;
    }
    m_desiredWidth = w;
}

void SearchFilterTab::SetActive(bool active)
{
    m_active = active;
    Invalidate();
}

void SearchFilterTab::SetCount(bool show, int count)
{
    m_showCount = show;
    m_count     = (count >= 0) ? count : 0;
    RecomputeDesiredWidth();
    Invalidate();
}

void SearchFilterTab::SetGrayed(bool grayed)
{
    m_grayed = grayed;
    Invalidate();
}

void SearchFilterTab::OnPaint(HDC hdc, const RECT& /*rcDirty*/)
{
    const int x = m_rcItem.left;
    const int y = m_rcItem.top;
    const int w = m_rcItem.right - m_rcItem.left;
    const int h = m_rcItem.bottom - m_rcItem.top;
    if (w <= 0 || h <= 0)
    {
        return;
    }

    // 组装显示文字："标签" 或 "标签 N"。
    const CString text = DisplayText();

    // 文字色：置灰 > 选中 > 普通。
    COLORREF color = kNormalColor;
    if (m_grayed)
    {
        color = kGrayedColor;
    }
    else if (m_active)
    {
        color = kActiveColor;
    }

    RECT textRc = { x, y, x + w, y + h - kUnderlineH };
    DrawTabText(hdc, (LPCWSTR)text, textRc, kTabTextPx, m_active,
                color, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    // 选中态：底部居中橙色下划线（不超过文字宽度，留点内缩更精致）。
    if (m_active && !m_grayed)
    {
        SIZE sz = { 0, 0 };
        HFONT hFont = ::CreateFontW(
            -kTabTextPx, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, i18n::UiFontName());
        int underlineW = w / 2;
        if (hFont != NULL)
        {
            HFONT hOld = (HFONT)::SelectObject(hdc, hFont);
            ::GetTextExtentPoint32W(hdc, (LPCWSTR)text, text.GetLength(), &sz);
            ::SelectObject(hdc, hOld);
            ::DeleteObject(hFont);
            if (sz.cx > 0 && sz.cx < w)
            {
                underlineW = sz.cx;
            }
        }
        int ulLeft  = x + (w - underlineW) / 2;
        RECT ul = { ulLeft, y + h - kUnderlineH, ulLeft + underlineW, y + h };
        HBRUSH hb = ::CreateSolidBrush(kActiveColor);
        ::FillRect(hdc, &ul, hb);
        ::DeleteObject(hb);
    }
}

} // namespace balloonsearch
