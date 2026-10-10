/**
 *  录制管理窗口的录像列表，见 RecordManagerList.h。
 *  balloonwj@qq.com   2026-10-05
 */
#include "stdafx.h"

#include "RecordManagerList.h"

#include <memory>
#include "CloudRecordManagerModel.h"   // CloudTrailingRect：状态列的矩形
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiEdit.h"
#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiScrollBar.h"

namespace screenrecord {

namespace {

//行的三种底色：与 DuiListBox 的默认值相同，构造时显式设给基类，状态列铺底时用同样的颜色
const COLORREF kRowBgColor = RGB(255, 255, 255);
const COLORREF kRowHoverBgColor = RGB(232, 240, 252);
const COLORREF kRowSelBgColor = RGB(180, 210, 245);
//进度条：高度、与文字之间的间距（像素）与底色
const int kProgressBarH = 4;
const int kProgressTextGap = 3;
const COLORREF kProgressTrackColor = RGB(220, 224, 230);
//进度的上限（百分比）
const int kProgressFull = 100;
//状态列文字与列右边之间的空白（像素）
const int kTrailingRightPad = 6;

} // namespace

CRecordManagerList::Trailing::Trailing()
    : m_color(RGB(0, 0, 0))
    , m_progress(-1)
{
}

CRecordManagerList::CRecordManagerList()
    : m_edit(NULL)
    , m_editing(false)
    , m_editScrollPos(0)
    , m_interruptSent(false)
    , m_trailingW(0)
    , m_hoverRow(-1)
{
    SetBgColor(kRowBgColor);
    SetHoverBgColor(kRowHoverBgColor);
    SetSelBgColor(kRowSelBgColor);
}

CRecordManagerList::~CRecordManagerList()
{
}

balloonwjui::DuiEdit* CRecordManagerList::BeginEdit(const RECT& rc, const std::wstring& text, UINT editCtrlId)
{
    if (m_edit == NULL)
    {
        std::unique_ptr<balloonwjui::DuiEdit> edit(new balloonwjui::DuiEdit());
        edit->SetVerticalCenter(true);
        edit->SetShowBorder(true);
        m_edit = edit.get();
        AddChild(std::move(edit));
    }
    m_edit->SetCtrlId(editCtrlId);
    m_edit->SetTextNoNotify(text.c_str());
    m_edit->SetVisible(true);
    //输入框不在任何布局容器里，按给定矩形直接排版（矩形不变时 SetRect 不会重排，所以用 ForceLayout）
    m_edit->ForceLayout(rc);
    m_editing = true;
    m_editScrollPos = GetScrollPos();
    m_interruptSent = false;
    m_edit->SetFocus();
    m_edit->SetSel(0, -1);
    Invalidate();
    return m_edit;
}

void CRecordManagerList::EndEdit()
{
    if (!m_editing)
        return;
    m_editing = false;
    if (m_edit != NULL)
    {
        //隐藏之前把焦点交还给列表：否则焦点留在看不见的输入框上，之后的按键都会输进它里面
        const bool hadFocus = m_edit->IsFocused();
        m_edit->SetVisible(false);
        if (hadFocus)
            SetFocus();
    }
    Invalidate();
}

bool CRecordManagerList::IsEditing() const
{
    return m_editing;
}

std::wstring CRecordManagerList::EditText() const
{
    if (!m_editing || m_edit == NULL)
        return std::wstring();
    return std::wstring(m_edit->GetText().GetString());
}

int CRecordManagerList::RowFromPoint(POINT pt) const
{
    const RECT rc = GetRect();
    const int rowH = GetItemHeight();
    if (rowH <= 0 || !::PtInRect(&rc, pt))
        return -1;
    const int row = (pt.y - rc.top + GetScrollPos()) / rowH;
    if (row < 0 || row >= GetItemCount())
        return -1;
    return row;
}

balloonwjui::DuiControl* CRecordManagerList::HitTest(POINT ptHostClient)
{
    if (m_editing && m_edit != NULL && m_edit->IsVisible() && ::PtInRect(&m_edit->GetRect(), ptHostClient))
        return m_edit->HitTest(ptHostClient);
    return balloonwjui::DuiListBox::HitTest(ptHostClient);
}

void CRecordManagerList::SetTrailingWidth(int px)
{
    m_trailingW = (px > 0) ? px : 0;
    Invalidate();
}

void CRecordManagerList::SetTrailing(int index, const std::wstring& text, COLORREF color, int progress)
{
    if (index < 0)
        return;
    Trailing t;
    t.m_text = text;
    t.m_color = color;
    t.m_progress = (progress >= 0 && progress <= kProgressFull) ? progress : -1;
    m_trailing[index] = t;
    Invalidate();
}

void CRecordManagerList::ClearTrailing()
{
    m_trailing.clear();
    Invalidate();
}

std::wstring CRecordManagerList::TrailingText(int index) const
{
    std::map<int, Trailing>::const_iterator it = m_trailing.find(index);
    return (it != m_trailing.end()) ? it->second.m_text : std::wstring();
}

bool CRecordManagerList::OnMouseMove(POINT pt, UINT mkFlags)
{
    m_hoverRow = RowFromPoint(pt);
    return balloonwjui::DuiListBox::OnMouseMove(pt, mkFlags);
}

bool CRecordManagerList::OnMouseLeave()
{
    m_hoverRow = -1;
    return balloonwjui::DuiListBox::OnMouseLeave();
}

void CRecordManagerList::paintTrailing(HDC hdc, const RECT& rcDirty)
{
    if (m_trailingW <= 0 || m_trailing.empty())
        return;
    const RECT listRc = GetRect();
    const int saved = ::SaveDC(hdc);
    //只画在列表之内（边框以内）
    ::IntersectClipRect(hdc, listRc.left + 1, listRc.top + 1, listRc.right - 1, listRc.bottom - 1);
    ::SelectObject(hdc, GetDefaultFont());
    ::SetBkMode(hdc, TRANSPARENT);
    const int curSel = GetCurSel();
    for (std::map<int, Trailing>::const_iterator it = m_trailing.begin(); it != m_trailing.end(); ++it)
    {
        const int row = it->first;
        if (row >= GetItemCount())
            continue;
        RECT rc;
        if (!CloudTrailingRect(listRc, row, GetItemHeight(), GetScrollPos(), m_trailingW,
                               balloonwjui::DuiScrollBar::kOverlayBandPx, rc))
        {
            continue;
        }
        RECT inter;
        if (!::IntersectRect(&inter, &rc, &rcDirty))
            continue;
        //按行当前的底色铺底，盖住 DuiListBox 画到这里的长文字
        const COLORREF bg = (row == curSel) ? kRowSelBgColor : (row == m_hoverRow ? kRowHoverBgColor : kRowBgColor);
        HBRUSH bgBrush = ::CreateSolidBrush(bg);
        ::FillRect(hdc, &rc, bgBrush);
        ::DeleteObject(bgBrush);

        const Trailing& t = it->second;
        RECT textRc = rc;
        textRc.right -= kTrailingRightPad;
        if (t.m_progress >= 0)
        {
            //有进度条：文字在上、进度条在下，两者合起来在行里垂直居中
            SIZE ts = { 0, 0 };
            ::GetTextExtentPoint32W(hdc, L"A", 1, &ts);
            const int blockH = ts.cy + kProgressTextGap + kProgressBarH;
            const int blockTop = (rc.top + rc.bottom - blockH) / 2;
            textRc.top = blockTop;
            textRc.bottom = blockTop + ts.cy;
            ::SetTextColor(hdc, t.m_color);
            ::DrawTextW(hdc, t.m_text.c_str(), -1, &textRc, DT_RIGHT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
            RECT track = { rc.left, textRc.bottom + kProgressTextGap, textRc.right,
                           textRc.bottom + kProgressTextGap + kProgressBarH };
            HBRUSH trackBrush = ::CreateSolidBrush(kProgressTrackColor);
            ::FillRect(hdc, &track, trackBrush);
            ::DeleteObject(trackBrush);
            RECT fill = track;
            fill.right = track.left + (track.right - track.left) * t.m_progress / kProgressFull;
            if (fill.right > fill.left)
            {
                HBRUSH fillBrush = ::CreateSolidBrush(t.m_color);
                ::FillRect(hdc, &fill, fillBrush);
                ::DeleteObject(fillBrush);
            }
        }
        else
        {
            ::SetTextColor(hdc, t.m_color);
            ::DrawTextW(hdc, t.m_text.c_str(), -1, &textRc, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
        }
    }
    ::RestoreDC(hdc, saved);
}

void CRecordManagerList::OnPaint(HDC hdc, const RECT& rcDirty)
{
    balloonwjui::DuiListBox::OnPaint(hdc, rcDirty);
    paintTrailing(hdc, rcDirty);
    if (!m_editing || m_edit == NULL)
        return;
    //滚动后输入框与行已对不上：只通知一次，由宿主延后提交（这里在绘制中途，不能改动控件树）
    if (GetScrollPos() != m_editScrollPos && !m_interruptSent)
    {
        m_interruptSent = true;
        NotifyParent(kNotifyEditInterrupted);
        return;
    }
    RECT inter;
    if (::IntersectRect(&inter, &m_edit->GetRect(), &rcDirty))
        m_edit->OnPaint(hdc, inter);
}

bool CRecordManagerList::OnLButtonDown(POINT pt, UINT mkFlags)
{
    //单击列表不会让输入框失去焦点（DuiListBox 不取焦点），所以由这里通知宿主提交编辑
    if (m_editing && !m_interruptSent)
    {
        m_interruptSent = true;
        NotifyParent(kNotifyEditInterrupted);
    }
    return balloonwjui::DuiListBox::OnLButtonDown(pt, mkFlags);
}

bool CRecordManagerList::OnRButtonDown(POINT pt, UINT mkFlags)
{
    (void)mkFlags;
    const int row = RowFromPoint(pt);
    if (row < 0)
        return true;
    //先选中这一行（发 DUIN_VALUECHANGED，宿主据此刷新按钮），再通知宿主弹菜单
    SetCurSel(row, true);
    NotifyParent(kNotifyContextMenu, static_cast<LPARAM>(row));
    return true;
}

} // namespace screenrecord
