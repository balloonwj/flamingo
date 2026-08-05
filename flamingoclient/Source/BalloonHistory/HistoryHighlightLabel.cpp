/**
 *  带关键字高亮的文本标签实现，HistoryHighlightLabel.cpp
 *  balloonwj@qq.com   2026-08-04
 */
#include "stdafx.h"
#include "HistoryHighlightLabel.h"

#include "../../balloonui/balloonui/DuiResMgr.h"

namespace balloonhistory {

//默认高亮底色：淡黄。比正文浅得多，压不住字，又足以一眼看见。
static const COLORREF kDefaultHighlightColor = RGB(0xFF, 0xF1, 0xA8);

//高亮块在文字上下各多出的像素。只按字高画会显得局促，像贴着字的一条杠。
static const int kHighlightPadV = 2;

HistoryHighlightLabel::HistoryHighlightLabel()
    : m_color(kDefaultHighlightColor)
{
}

void HistoryHighlightLabel::SetHighlightRanges(const std::vector<HighlightRange>& ranges)
{
    m_ranges = ranges;
    Invalidate();
}

void HistoryHighlightLabel::SetHighlightColor(COLORREF color)
{
    m_color = color;
    Invalidate();
}

void HistoryHighlightLabel::OnPaint(HDC hdc, const RECT& rcDirty)
{
    //先铺底色再让基类画字：反过来的话底色会把刚画好的文字盖掉。
    PaintHighlights(hdc);
    balloonwjui::DuiLabel::OnPaint(hdc, rcDirty);
}

void HistoryHighlightLabel::PaintHighlights(HDC hdc)
{
    if (m_ranges.empty())
    {
        return;
    }
    const CString text = GetText();
    if (text.IsEmpty())
    {
        return;
    }

    //测量用的字体必须与基类绘制时用的是同一个，否则底色会与文字错位。
    //基类的取法是"没设就用 DuiResMgr 的默认字体"，这里照搬。
    HFONT useFont = GetFont();
    if (useFont == NULL)
    {
        useFont = balloonwjui::DuiResMgr::Inst().GetDefaultFont();
    }
    HFONT oldFont = (HFONT)::SelectObject(hdc, useFont);

    TEXTMETRIC tm = { 0 };
    ::GetTextMetrics(hdc, &tm);

    const RECT rc = GetRect();
    const int textLen = text.GetLength();

    //整段文字的宽度：DT_CENTER / DT_RIGHT 对齐时要靠它反推起点。
    SIZE full = { 0, 0 };
    ::GetTextExtentPoint32(hdc, (LPCTSTR)text, textLen, &full);

    const DWORD align = GetTextAlign();
    int originX = rc.left;
    if ((align & DT_CENTER) != 0)
    {
        originX = rc.left + ((rc.right - rc.left) - full.cx) / 2;
    }
    else if ((align & DT_RIGHT) != 0)
    {
        originX = rc.right - full.cx;
    }

    //纵向：本类只用于单行，故按 DT_VCENTER 的口径把字高居中放置。
    const int centerY = (rc.top + rc.bottom) / 2;
    int top    = centerY - tm.tmHeight / 2 - kHighlightPadV;
    int bottom = centerY + tm.tmHeight / 2 + kHighlightPadV;
    if (top < rc.top)
    {
        top = rc.top;
    }
    if (bottom > rc.bottom)
    {
        bottom = rc.bottom;
    }

    HBRUSH brush = ::CreateSolidBrush(m_color);
    for (size_t i = 0; i < m_ranges.size(); ++i)
    {
        const int begin = m_ranges[i].m_begin;
        const int len   = m_ranges[i].m_len;
        //越界或长度非正的区间直接跳过 —— 由本类兜住，调用方不必预先过滤。
        if (begin < 0 || len <= 0 || begin >= textLen)
        {
            continue;
        }
        int useLen = len;
        if (begin + useLen > textLen)
        {
            useLen = textLen - begin;
        }

        SIZE prefix = { 0, 0 };
        if (begin > 0)
        {
            ::GetTextExtentPoint32(hdc, (LPCTSTR)text, begin, &prefix);
        }
        SIZE seg = { 0, 0 };
        ::GetTextExtentPoint32(hdc, ((LPCTSTR)text) + begin, useLen, &seg);

        RECT hl;
        hl.left   = originX + prefix.cx;
        hl.right  = hl.left + seg.cx;
        hl.top    = top;
        hl.bottom = bottom;

        //超出本控件可视区的部分不画：预览文字过长时会被基类截断成省略号，
        //底色若照旧画出去就会糊到隔壁列上。
        if (hl.left >= rc.right)
        {
            continue;
        }
        if (hl.right > rc.right)
        {
            hl.right = rc.right;
        }
        ::FillRect(hdc, &hl, brush);
    }
    ::DeleteObject(brush);

    ::SelectObject(hdc, oldFont);
}

} // namespace balloonhistory
