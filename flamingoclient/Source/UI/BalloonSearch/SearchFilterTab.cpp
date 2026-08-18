/**
 *  SearchFilterTab.cpp —— 搜索分类筛选文字标签实现。
 *  balloonwj@qq.com   2026-06-01
 */
#include "stdafx.h"
#include "SearchFilterTab.h"

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
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");
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
{
}

void SearchFilterTab::SetLabel(LPCTSTR text)
{
    m_label = (text != NULL) ? text : _T("");
    Invalidate();
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
    CString text = m_label;
    if (m_showCount)
    {
        CString cnt;
        cnt.Format(_T(" %d"), m_count);
        text += cnt;
    }

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
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");
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
