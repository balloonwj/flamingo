/**
 *  flamingoAdmin —— 列表底部的分页条实现，说明见 PagerBar.h。
 *
 *  balloonwj@qq.com   2026-10-02
 */
#include "stdafx.h"
#include "Widgets/PagerBar.h"
#include "Theme/AdminTheme.h"
#include "Widgets/UiHelpers.h"

namespace flamingoadmin { namespace ui {

namespace {

// ---- 分页条几何（96 dpi 逻辑像素），与原企业管理页表尾保持一致 ----
const int kDefaultSidePad  = 14;   // 默认左右内边距：与企业管理页表格的单元格内边距相同
const int kButtonWidth     = 60;   // 翻页按钮宽
const int kButtonHeight    = 24;   // 翻页按钮高
const int kPageTextWidth   = 120;  // 「第 X / 共 Y 页」文字区宽
const int kPageTextHalfH   = 10;   // 「第 X / 共 Y 页」文字区半高
const int kButtonGap       = 8;    // 按钮与页码文字之间的间距
const int kTotalTextWidth  = 240;  // 左侧「共 N 条」文字区宽
const int kButtonRadius    = 5;    // 按钮圆角半径
const float kBorderWidth   = 1.0f; // 按钮描边宽度

} // namespace

PagerBar::PagerBar()
    : m_page(1)
    , m_total(0)
    , m_pageSize(1)
    , m_sidePad(kDefaultSidePad)
    , m_onPage(nullptr)
    , m_onPageUser(nullptr)
{
    m_prevRc = RECT{ 0, 0, 0, 0 };
    m_nextRc = RECT{ 0, 0, 0, 0 };
    m_pageTextRc = RECT{ 0, 0, 0, 0 };
}

void PagerBar::SetPageInfo(int page, int total, int pageSize)
{
    const int newPage     = (page < 1) ? 1 : page;
    const int newTotal    = (total < 0) ? 0 : total;
    const int newPageSize = (pageSize > 0) ? pageSize : 1;
    // 值没有变化时不重绘：宿主可能在自己的 OnPaint 里调用本函数，无条件 Invalidate 会让
    // 宿主反复重绘。
    if (newPage == m_page && newTotal == m_total && newPageSize == m_pageSize)
    {
        return;
    }
    m_page     = newPage;
    m_total    = newTotal;
    m_pageSize = newPageSize;
    Invalidate();
}

CString PagerBar::FormatPageText(int page, int pageCount)
{
    CString text;
    text.Format(_T("第 %d / 共 %d 页"), page, pageCount);
    return text;
}

void PagerBar::SetPageCallback(PageChangeFn fn, void* user)
{
    m_onPage = fn;
    m_onPageUser = user;
}

void PagerBar::SetSidePadding(int px)
{
    m_sidePad = (px < 0) ? 0 : px;
    ComputeRects();
    Invalidate();
}

int PagerBar::PageCount() const
{
    const int n = (m_total + m_pageSize - 1) / m_pageSize;
    return (n < 1) ? 1 : n;
}

void PagerBar::Layout(const RECT& rcAvail)
{
    DuiControl::Layout(rcAvail);
    ComputeRects();
}

void PagerBar::ComputeRects()
{
    const int cy = (m_rcItem.top + m_rcItem.bottom) / 2;

    //从右往左排：[上一页] 第 X / 共 Y 页 [下一页]
    m_nextRc.right  = m_rcItem.right - m_sidePad;
    m_nextRc.left   = m_nextRc.right - kButtonWidth;
    m_nextRc.top    = cy - kButtonHeight / 2;
    m_nextRc.bottom = cy + kButtonHeight / 2;

    m_pageTextRc.right  = m_nextRc.left - kButtonGap;
    m_pageTextRc.left   = m_pageTextRc.right - kPageTextWidth;
    m_pageTextRc.top    = cy - kPageTextHalfH;
    m_pageTextRc.bottom = cy + kPageTextHalfH;

    m_prevRc.right  = m_pageTextRc.left - kButtonGap;
    m_prevRc.left   = m_prevRc.right - kButtonWidth;
    m_prevRc.top    = cy - kButtonHeight / 2;
    m_prevRc.bottom = cy + kButtonHeight / 2;
}

void PagerBar::OnPaint(HDC hdc, const RECT& /*rcDirty*/)
{
    if (!m_bVisible)
        return;

    //控件矩形可能在没有经过 Layout 的情况下被改过，绘制前按当前矩形再算一次
    ComputeRects();

    //顶部分隔线
    FillLine(hdc, m_rcItem.left, m_rcItem.top, m_rcItem.right - m_rcItem.left, 1, theme::kInk100);

    HFONT font = UiFont(theme::kFontSizeSmall, false);

    CString totalText;
    totalText.Format(_T("共 %d 条"), m_total);
    const RECT totalRc = { m_rcItem.left + m_sidePad, m_rcItem.top,
                           m_rcItem.left + m_sidePad + kTotalTextWidth, m_rcItem.bottom };
    DrawTextLine(hdc, totalText, totalRc, theme::kInk500, font,
                 DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    DrawPagerButton(hdc, m_nextRc, _T("下一页"), CanNext());

    const CString pageText = FormatPageText(m_page, PageCount());
    DrawTextLine(hdc, pageText, m_pageTextRc, theme::kInk500, font,
                 DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    DrawPagerButton(hdc, m_prevRc, _T("上一页"), CanPrev());
}

void PagerBar::DrawPagerButton(HDC hdc, const RECT& rc, LPCTSTR text, bool enabled) const
{
    FillRoundRect(hdc, rc, enabled ? theme::kInk0 : theme::kInk25, kButtonRadius);
    StrokeRoundRect(hdc, rc, theme::kInk100, kButtonRadius, kBorderWidth);
    DrawTextLine(hdc, text, rc, enabled ? theme::kInk600 : theme::kInk300,
                 UiFont(theme::kFontSizeSmall, false),
                 DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
}

bool PagerBar::OnLButtonUp(POINT pt, UINT /*mkFlags*/)
{
    if (m_onPage == nullptr)
        return true;

    //只在按钮可用时发出翻页请求；点在两个按钮以外的位置不做任何事
    if (::PtInRect(&m_prevRc, pt) && CanPrev())
        m_onPage(m_onPageUser, m_page - 1);
    else if (::PtInRect(&m_nextRc, pt) && CanNext())
        m_onPage(m_onPageUser, m_page + 1);
    return true;
}

bool PagerBar::OnSetCursor(POINT pt)
{
    //鼠标停在可用的翻页按钮上时显示手形光标，提示可以点击
    const bool overPrev = ::PtInRect(&m_prevRc, pt) && CanPrev();
    const bool overNext = ::PtInRect(&m_nextRc, pt) && CanNext();
    if (!overPrev && !overNext)
        return false;
    ::SetCursor(::LoadCursor(nullptr, IDC_HAND));
    return true;
}

}} // namespace flamingoadmin::ui
