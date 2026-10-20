/**
 *  flamingoAdmin —— 带行内文字操作链接的虚拟表格实现，说明见 ActionTable.h。
 *
 *  balloonwj@qq.com   2026-10-02
 */
#include "stdafx.h"
#include "Widgets/ActionTable.h"
#include "Theme/AdminTheme.h"
#include "DuiHost.h"
#include "Controls/Feedback/DuiToolTip.h"
#include "Widgets/UiHelpers.h"
#include "Controls/Window/DuiScrollBar.h"

namespace flamingoadmin { namespace ui {

namespace {

// ---- 操作链接几何（96 dpi 逻辑像素）----
const int kLinkAreaPad   = 14;   // 操作列左侧内边距：与表格单元格内边距相同
const int kLinkGap       = 14;   // 相邻两个链接之间的间距
const int kLinkHalfH     = 11;   // 链接命中区的半高（行垂直居中，上下各 11 像素，约一行文字高）
const int kLinkFontSize  = theme::kFontSizeNormal;  // 链接文字字号，与表格正文相同

// 量出一段文字在链接字体下的宽度（像素）。没有可用的 DC 时返回 0。
int MeasureLinkText(const CString& text)
{
    HDC screen = ::GetDC(nullptr);
    if (screen == nullptr)
        return 0;
    HGDIOBJ old = ::SelectObject(screen, UiFont(kLinkFontSize, false));
    SIZE size = { 0, 0 };
    ::GetTextExtentPoint32(screen, text.GetString(), text.GetLength(), &size);
    ::SelectObject(screen, old);
    ::ReleaseDC(nullptr, screen);
    return size.cx;
}

} // namespace

ActionTable::ActionTable()
    : m_rowPainter(nullptr)
    , m_rowPainterUser(nullptr)
    , m_actionAreaWidth(0)
    , m_onAction(nullptr)
    , m_onActionUser(nullptr)
    , m_actionVisible(nullptr)
    , m_actionVisibleUser(nullptr)
    , m_collapseHidden(false)
    , m_tooltipFn(nullptr)
    , m_tooltipUser(nullptr)
{
    //占用基类的绘行回调：先转给宿主画行内容，再补画链接
    DuiVirtualList::SetPaintRowCallback(&ActionTable::PaintRowThunk, this);
}

ActionTable::~ActionTable()
{
    if (!m_tooltipText.IsEmpty())
        balloonwjui::DuiToolTipMgr::Inst().Unregister(this);
}

void ActionTable::SetRowPainter(PaintRowFn fn, void* user)
{
    m_rowPainter = fn;
    m_rowPainterUser = user;
}

void ActionTable::SetActionAreaWidth(int width)
{
    m_actionAreaWidth = (width < 0) ? 0 : width;
    Invalidate();
}

void ActionTable::AddAction(const CString& label, COLORREF color)
{
    ActionLink link;
    link.m_label = label;
    link.m_color = color;
    link.m_width = MeasureLinkText(label);
    m_actions.push_back(link);
    Invalidate();
}

void ActionTable::ClearActions()
{
    m_actions.clear();
    Invalidate();
}

void ActionTable::SetActionCallback(ActionFn fn, void* user)
{
    m_onAction = fn;
    m_onActionUser = user;
}

void ActionTable::SetActionVisibility(ActionVisibleFn fn, void* user)
{
    m_actionVisible = fn;
    m_actionVisibleUser = user;
    Invalidate();
}

bool ActionTable::IsActionVisible(int rowIndex, int actionIndex) const
{
    if (m_actionVisible == nullptr)
        return true;
    return m_actionVisible(m_actionVisibleUser, rowIndex, actionIndex);
}

void ActionTable::SetCollapseHiddenActions(bool collapse)
{
    m_collapseHidden = collapse;
    Invalidate();
}

void ActionTable::SetOverlayText(const CString& text)
{
    if (m_overlayText == text)
        return;
    m_overlayText = text;
    Invalidate();
}

int ActionTable::RowFromPoint(POINT pt) const
{
    if (!::PtInRect(&m_rcItem, pt))
        return -1;
    const int rowH = GetRowHeight();
    if (rowH <= 0)
        return -1;
    //算法与 DuiVirtualList 私有的 IndexFromPoint 相同：相对控件顶边的偏移加上滚动位置，再按行高取整
    const int idx = ((pt.y - m_rcItem.top) + GetScrollPos()) / rowH;
    if (idx < 0 || idx >= GetRowCount())
        return -1;
    return idx;
}

int ActionTable::BodyRight() const
{
    //滚动条可见时占据控件最右侧一条，数据行画到它的左边为止
    balloonwjui::DuiScrollBar* sb = const_cast<ActionTable*>(this)->GetScrollBar();
    if (sb != nullptr && sb->IsVisible())
    {
        const RECT& rcSb = sb->GetRect();
        return m_rcItem.right - (rcSb.right - rcSb.left);
    }
    return m_rcItem.right;
}

void ActionTable::SetTooltipProvider(TooltipFn fn, void* user)
{
    m_tooltipFn = fn;
    m_tooltipUser = user;
    if (fn == nullptr && !m_tooltipText.IsEmpty())
    {
        m_tooltipText.Empty();
        balloonwjui::DuiToolTipMgr::Inst().Unregister(this);
    }
}

bool ActionTable::OnMouseMove(POINT pt, UINT mkFlags)
{
    const bool handled = DuiVirtualList::OnMouseMove(pt, mkFlags);
    if (m_tooltipFn == nullptr)
        return handled;

    //显示覆盖提示时表格里没有数据行，不显示悬停提示
    const CString text = m_overlayText.IsEmpty() ? m_tooltipFn(m_tooltipUser, pt) : CString();
    if (text == m_tooltipText)
        return handled;

    //DuiToolTipMgr 按控件登记，同一控件内换了位置宿主不会再报告悬停变化；Retarget 先收起本控件
    //正在计时或已显示的提示，再按新文字登记并重新开始计时；新文字为空时只取消登记
    m_tooltipText = text;
    POINT screenPt = pt;
    balloonwjui::DuiHost* host = GetHost();
    if (host != nullptr && ::IsWindow(host->m_hWnd))
        ::ClientToScreen(host->m_hWnd, &screenPt);
    balloonwjui::DuiToolTipMgr::Inst().Retarget(this, text, screenPt);
    return handled;
}

int ActionTable::ScrollBarWidth() const
{
    return m_rcItem.right - BodyRight();
}

RECT ActionTable::LinkRectInRow(const RECT& rowRect, int rowIndex, int actionIndex) const
{
    RECT rc = { 0, 0, 0, 0 };
    if (actionIndex < 0 || actionIndex >= ActionCount())
        return rc;

    //链接从操作列左边（加内边距）起依次排开；让出位置时跳过本行不显示的链接
    int left = rowRect.right - m_actionAreaWidth + kLinkAreaPad;
    for (int i = 0; i < actionIndex; ++i)
    {
        if (m_collapseHidden && !IsActionVisible(rowIndex, i))
            continue;
        left += m_actions[static_cast<size_t>(i)].m_width + kLinkGap;
    }

    const int cy = (rowRect.top + rowRect.bottom) / 2;
    rc.left   = left;
    rc.right  = left + m_actions[static_cast<size_t>(actionIndex)].m_width;
    rc.top    = cy - kLinkHalfH;
    rc.bottom = cy + kLinkHalfH;
    //不越出本行
    if (rc.top < rowRect.top)
        rc.top = rowRect.top;
    if (rc.bottom > rowRect.bottom)
        rc.bottom = rowRect.bottom;
    return rc;
}

RECT ActionTable::GetLinkRect(int rowIndex, int actionIndex) const
{
    RECT empty = { 0, 0, 0, 0 };
    if (rowIndex < 0 || rowIndex >= GetRowCount())
        return empty;
    const int rowH = GetRowHeight();
    RECT rowRect;
    rowRect.left   = m_rcItem.left;
    rowRect.right  = BodyRight();
    rowRect.top    = m_rcItem.top + rowIndex * rowH - GetScrollPos();
    rowRect.bottom = rowRect.top + rowH;
    return LinkRectInRow(rowRect, rowIndex, actionIndex);
}

int ActionTable::ActionFromPoint(POINT pt, int* rowIndex) const
{
    if (rowIndex != nullptr)
        *rowIndex = -1;
    //显示覆盖层时（加载中、请先选择企业等）链接不响应
    if (!m_overlayText.IsEmpty())
        return -1;

    const int row = RowFromPoint(pt);
    if (row < 0)
        return -1;
    for (int i = 0; i < ActionCount(); ++i)
    {
        if (!IsActionVisible(row, i))
            continue;
        const RECT rc = GetLinkRect(row, i);
        if (::PtInRect(&rc, pt))
        {
            if (rowIndex != nullptr)
                *rowIndex = row;
            return i;
        }
    }
    return -1;
}

void ActionTable::PaintRowThunk(void* user, HDC hdc, int rowIndex, const RECT& rowRect,
                                bool selected, bool hover)
{
    ActionTable* self = static_cast<ActionTable*>(user);
    if (self->m_rowPainter != nullptr)
        self->m_rowPainter(self->m_rowPainterUser, hdc, rowIndex, rowRect, selected, hover);
    self->PaintActions(hdc, rowIndex, rowRect);
}

void ActionTable::PaintActions(HDC hdc, int rowIndex, const RECT& rowRect) const
{
    HFONT font = UiFont(kLinkFontSize, false);
    for (int i = 0; i < ActionCount(); ++i)
    {
        if (!IsActionVisible(rowIndex, i))
            continue;
        const ActionLink& link = m_actions[static_cast<size_t>(i)];
        const RECT rc = LinkRectInRow(rowRect, rowIndex, i);
        DrawTextLine(hdc, link.m_label, rc, link.m_color, font,
                     DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }
}

void ActionTable::OnPaint(HDC hdc, const RECT& rcDirty)
{
    //先让基类画数据行（经 PaintRowThunk 补画链接）；行数为 0 时画的是空白
    DuiVirtualList::OnPaint(hdc, rcDirty);
    if (m_overlayText.IsEmpty())
        return;
    DrawTextLine(hdc, m_overlayText, m_rcItem, theme::kInk400, UiFont(theme::kFontSizeNormal, false),
                 DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
}

bool ActionTable::OnLButtonUp(POINT pt, UINT /*mkFlags*/)
{
    //不调用基类：基类会向宿主窗口发 DUIN_CLICK，而行内操作已经经回调交给宿主，再发一次没有意义
    int row = -1;
    const int action = ActionFromPoint(pt, &row);
    if (action >= 0 && m_onAction != nullptr)
        m_onAction(m_onActionUser, row, action);
    return true;
}

bool ActionTable::OnSetCursor(POINT pt)
{
    //鼠标停在链接上时显示手形光标，提示可以点击
    if (ActionFromPoint(pt, nullptr) < 0)
        return false;
    ::SetCursor(::LoadCursor(nullptr, IDC_HAND));
    return true;
}

}} // namespace flamingoadmin::ui
