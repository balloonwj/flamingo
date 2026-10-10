/**
 *  GroupMemberList.cpp
 *
 *  成员列表容器的实现。固定行高堆叠子控件；绘制时把子控件裁剪到本控件
 *  矩形内（DuiControl::OnPaint 不会按父矩形裁剪子控件）。
 *
 *  本 .cpp 不 include Flamingo 的 stdafx.h —— 它要被 Flamingo.vcxproj 与
 *  FlamingoUnit.vcxproj 共用。在 Flamingo.vcxproj 里需标记
 *  PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-05-22
 */

// ---- balloonui 头文件要求的 ATL/WTL 前置依赖（等价 NewChatDemo/stdafx.h）----
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#define WINVER          0x0601      // 目标系统下限 = Windows 7
#define _WIN32_WINNT    0x0601
#define _WIN32_IE       0x0601
#define _RICHEDIT_VER   0x0300      // RichEdit 3.0，WTL 10 要求的下限（窗口类仍为 RichEdit20W）
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

#include <gdiplus.h>

#include <string>

#include "GroupMemberList.h"
#include "GroupMemberItem.h"   // SetMyNetRoleForAll 逐行转发给 GroupMemberItem::SetMyNetRole
#include "GroupDeptHeaderItem.h"   // 部门树视图的部门标题行
#include "ChatScrollThumb.h"   // 与聊天消息区同一种细滚动条：几何计算与滑块绘制
#include "../BalloonSearch/ContactMatch.h"   // 成员搜索与群管理窗成员页共用的匹配规则
#include "../../Common/I18n/I18n.h"   // TR / TR_NOOP：界面文字的多语言查找

namespace balloonchatwnd {

namespace {

// 「没有找到匹配的成员」提示：文字、字号（与成员行的昵称同为 13 像素）、颜色、
// 文字所在横条的上沿距列表上沿的距离（像素）
const wchar_t* const kNoMatchText = TR_NOOP(L"没有找到匹配的成员");
const int            kNoMatchTextPx = 13;
const COLORREF       kNoMatchColor = RGB(0x99, 0x99, 0x99);
const int            kNoMatchTopPad = 16;

// 在 rc 里水平居中、垂直居中画一行灰色提示文字（GDI，与成员行的昵称同一种字体）。
void DrawNoMatchText(HDC hdc, const RECT& rc)
{
    HFONT hFont = ::CreateFontW(-kNoMatchTextPx, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                                CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                                i18n::UiFontName());
    if (hFont == NULL)
    {
        return;
    }
    HFONT    hOldFont  = (HFONT)::SelectObject(hdc, hFont);
    int      oldBkMode = ::SetBkMode(hdc, TRANSPARENT);
    COLORREF oldColor  = ::SetTextColor(hdc, kNoMatchColor);
    RECT r = rc;
    ::DrawTextW(hdc, TR(kNoMatchText), -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    ::SetTextColor(hdc, oldColor);
    ::SetBkMode(hdc, oldBkMode);
    ::SelectObject(hdc, hOldFont);
    ::DeleteObject(hFont);
}

} // anonymous namespace

bool GroupMemberNameMatches(const CString& name, const CString& keyword)
{
    CString k = keyword;
    k.Trim();
    if (k.IsEmpty())
    {
        return true;
    }
    //群聊窗没有成员的账号与职务，只按名字匹配；账号、地区两档传空串
    const contactsearch::MatchResult r = contactsearch::Match(
        std::wstring((LPCWSTR)k), std::wstring((LPCWSTR)name), std::wstring(), std::wstring());
    return r.m_matched;
}

GroupMemberList::GroupMemberList()
    : m_scrollY(0)
    , m_thumbDragging(false)
    , m_thumbDragOffsetY(0)
    , m_rightInset(0)
    , m_treeMode(false)
    , m_selectedUserId(0)
{
    m_scrollFade.SetOwner(this);
}

void GroupMemberList::SelectMember(int64_t userid)
{
    //部门树视图保持原样，不产生选中（2026-10-08 用户确认）；平铺视图里记着的人也不因此改变
    if (m_treeMode || userid <= 0)
    {
        return;
    }
    if (m_selectedUserId == userid)
    {
        return;
    }
    m_selectedUserId = userid;
    ApplySelectionToRows();
}

void GroupMemberList::CommitRows()
{
    //选中的人不在新名单里（离开了群）：取消选中，他之后再入群也不会自动恢复
    if (m_selectedUserId != 0)
    {
        bool found = false;
        for (size_t i = 0; i < m_children.size() && !found; ++i)
        {
            GroupMemberItem* item = dynamic_cast<GroupMemberItem*>(m_children[i].get());
            found = (item != nullptr && item->UserId() == m_selectedUserId);
        }
        if (!found)
        {
            m_selectedUserId = 0;
        }
    }
    ApplySelectionToRows();
}

void GroupMemberList::ApplySelectionToRows()
{
    //平铺视图里每个人只有一行；部门树视图一律不显示选中
    for (size_t i = 0; i < m_children.size(); ++i)
    {
        GroupMemberItem* item = dynamic_cast<GroupMemberItem*>(m_children[i].get());
        if (item != nullptr)
        {
            item->SetSelected(!m_treeMode && m_selectedUserId != 0 && item->UserId() == m_selectedUserId);
        }
    }
}

RECT GroupMemberList::ContentRect() const
{
    RECT rc = m_rcItem;
    rc.right -= m_rightInset;
    if (rc.right < rc.left)
    {
        rc.right = rc.left;
    }
    return rc;
}

void GroupMemberList::SetRightInset(int px)
{
    if (px < 0)
    {
        px = 0;
    }
    if (px == m_rightInset)
    {
        return;
    }
    m_rightInset = px;
    ForceLayout(m_rcItem);
    Invalidate();
}

int GroupMemberList::VisibleRowCount() const
{
    int count = 0;
    const std::vector<std::unique_ptr<balloonwjui::DuiControl> >& kids = Children();
    for (size_t i = 0; i < kids.size(); ++i)
    {
        if (kids[i]->IsVisible())
        {
            ++count;
        }
    }
    return count;
}

int GroupMemberList::ContentHeight() const
{
    return VisibleRowCount() * kGroupMemberRowHeight;
}

bool GroupMemberList::HasNoMatches() const
{
    if (m_filterKeyword.IsEmpty())
    {
        return false;
    }
    //只看成员行：部门树视图下部门标题行跟着命中的成员显示，没有命中的成员时它们也都隐藏了
    for (size_t i = 0; i < m_children.size(); ++i)
    {
        if (m_children[i]->IsVisible() && dynamic_cast<GroupMemberItem*>(m_children[i].get()) != nullptr)
        {
            return false;
        }
    }
    return true;
}

int GroupMemberList::MemberRowCount() const
{
    int count = 0;
    for (size_t i = 0; i < m_children.size(); ++i)
    {
        if (dynamic_cast<GroupMemberItem*>(m_children[i].get()) != nullptr)
        {
            ++count;
        }
    }
    return count;
}

void GroupMemberList::ClearRows()
{
    while (!m_children.empty())
    {
        RemoveChild(m_children[0].get());
    }
    m_rowParent.clear();
}

void GroupMemberList::AddRow(std::unique_ptr<balloonwjui::DuiControl> row, int parentRow)
{
    if (!row)
    {
        return;
    }
    //父行必须是已经挂上的行；越界的按没有父行处理
    if (parentRow < 0 || parentRow >= (int)m_children.size())
    {
        parentRow = -1;
    }
    GroupDeptHeaderItem* head = dynamic_cast<GroupDeptHeaderItem*>(row.get());
    if (head != nullptr)
    {
        head->SetCollapsed(IsDeptCollapsed(head->DeptId()));
    }
    //平铺视图下，记着的那个人的行挂上来时就显示选中（列表重建后按 userid 保留选中）
    GroupMemberItem* member = dynamic_cast<GroupMemberItem*>(row.get());
    if (member != nullptr)
    {
        member->SetSelected(!m_treeMode && m_selectedUserId != 0 && member->UserId() == m_selectedUserId);
    }
    //AddChild 之前先补齐父行记录：别处直接 AddChild 挂上的行没有记录，按没有父行处理
    m_rowParent.resize(m_children.size(), -1);
    m_rowParent.push_back(parentRow);
    AddChild(std::move(row));
}

void GroupMemberList::SetTreeMode(bool on)
{
    if (m_treeMode == on)
    {
        return;
    }
    m_treeMode = on;
    m_scrollY = 0;
    //部门树视图不显示选中；切回平铺视图时恢复记着的那个人
    ApplySelectionToRows();
}

void GroupMemberList::ToggleDeptCollapsed(int64_t deptId)
{
    const bool collapse = !IsDeptCollapsed(deptId);
    if (collapse)
    {
        m_collapsedDepts.insert(deptId);
    }
    else
    {
        m_collapsedDepts.erase(deptId);
    }
    for (size_t i = 0; i < m_children.size(); ++i)
    {
        GroupDeptHeaderItem* head = dynamic_cast<GroupDeptHeaderItem*>(m_children[i].get());
        if (head != nullptr && head->DeptId() == deptId)
        {
            head->SetCollapsed(collapse);
        }
    }
    ReapplyFilter();
}

void GroupMemberList::SetFilterKeyword(const CString& keyword)
{
    CString k = keyword;
    k.Trim();
    if (k != m_filterKeyword)
    {
        //换了关键词就是另一批结果，从顶部开始显示
        m_scrollY = 0;
    }
    m_filterKeyword = k;
    ReapplyFilter();
}

void GroupMemberList::ReapplyFilter()
{
    const size_t n = m_children.size();
    //别处直接 AddChild 挂上的行没有父行记录，按没有父行处理
    m_rowParent.resize(n, -1);
    const bool searching = !m_filterKeyword.IsEmpty();

    std::vector<bool> show(n, false);
    if (!m_treeMode || !searching)
    {
        //平铺视图：成员行按关键词过滤，其余行照常显示。部门树视图不搜索时：父部门行隐藏或折叠着的行都隐藏。
        //行按先父后子的顺序挂上，父行的结果总是先算出来
        for (size_t i = 0; i < n; ++i)
        {
            GroupMemberItem* item = dynamic_cast<GroupMemberItem*>(m_children[i].get());
            bool visible = (item == nullptr) || GroupMemberNameMatches(item->Name(), m_filterKeyword);
            const int parent = m_rowParent[i];
            if (m_treeMode && parent >= 0)
            {
                GroupDeptHeaderItem* head = dynamic_cast<GroupDeptHeaderItem*>(m_children[(size_t)parent].get());
                if (!show[(size_t)parent] || (head != nullptr && head->IsCollapsed()))
                {
                    visible = false;
                }
            }
            show[i] = visible;
        }
    }
    else
    {
        //部门树视图下搜索：命中的成员显示，并沿父行一路把所在部门及上级显示出来（自动展开）；
        //没有命中成员的部门隐藏。折叠状态不受影响，清空关键字后恢复
        for (size_t i = 0; i < n; ++i)
        {
            GroupMemberItem* item = dynamic_cast<GroupMemberItem*>(m_children[i].get());
            if (item == nullptr || !GroupMemberNameMatches(item->Name(), m_filterKeyword))
            {
                continue;
            }
            show[i] = true;
            int parent = m_rowParent[i];
            while (parent >= 0 && !show[(size_t)parent])
            {
                show[(size_t)parent] = true;
                parent = m_rowParent[(size_t)parent];
            }
        }
    }

    for (size_t i = 0; i < n; ++i)
    {
        GroupDeptHeaderItem* head = dynamic_cast<GroupDeptHeaderItem*>(m_children[i].get());
        if (head != nullptr)
        {
            head->SetSearching(searching);
        }
        m_children[i]->SetVisible(show[i]);
    }
    //SetVisible 只置标志、不重新排版（开发约定文档 UI 约定第 5 条），而本控件矩形没变时 SetRect
    //直接返回，故用 ForceLayout 按新的可见行重新摆放
    ForceLayout(m_rcItem);
    Invalidate();
}

void GroupMemberList::Layout(const RECT& rcAvail)
{
    m_rcItem = rcAvail;

    //行数或可视高度变了（重新填充成员、窗口缩放、过滤）：滚动位置收回新的合法范围。成员变少时
    //停在新的底部，否则保持原位置
    m_scrollY = ClampChatScrollY(m_scrollY, rcAvail.bottom - rcAvail.top, ContentHeight());

    // 可见的子控件按固定行高自上而下堆叠，整体上移滚动量；右侧让出缩放边框的宽度。
    // 被过滤隐藏的行给一个空矩形：不占位置，也不会被命中测试选中。
    const RECT content = ContentRect();
    const std::vector<std::unique_ptr<balloonwjui::DuiControl> >& kids = Children();
    int y = rcAvail.top - m_scrollY;
    for (size_t i = 0; i < kids.size(); ++i)
    {
        if (!kids[i]->IsVisible())
        {
            RECT empty = { content.left, rcAvail.top, content.left, rcAvail.top };
            kids[i]->SetRect(empty);
            continue;
        }
        RECT r = { content.left, y, content.right, y + kGroupMemberRowHeight };
        kids[i]->SetRect(r);
        y += kGroupMemberRowHeight;
    }
}

void GroupMemberList::OnPaint(HDC hdc, const RECT& rcDirty)
{
    // 绘制各成员行 —— 裁剪到本控件矩形内（DuiControl::OnPaint 不会按父
    // 矩形裁剪子控件；滚出可视区的行须被裁掉，不画到边栏外）。
    const int savedDC = ::SaveDC(hdc);
    ::IntersectClipRect(hdc, m_rcItem.left, m_rcItem.top,
                        m_rcItem.right, m_rcItem.bottom);
    DuiControl::OnPaint(hdc, rcDirty);

    // 搜索一个都没命中：在列表上部画一行提示，横条高度与成员行相同
    if (HasNoMatches())
    {
        const RECT content = ContentRect();
        RECT band = { content.left, m_rcItem.top + kNoMatchTopPad, content.right,
                      m_rcItem.top + kNoMatchTopPad + kGroupMemberRowHeight };
        DrawNoMatchText(hdc, band);
    }

    // 内容超出时在右侧画滑块，浮在成员行之上。滑块落在成员行右侧的内边距里，不压到角色徽标
    const RECT thumb = ThumbRect();
    if (thumb.bottom > thumb.top)
    {
        Gdiplus::Graphics g(hdc);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        PaintChatScrollThumb(g, thumb, m_scrollFade.Alpha());
    }
    ::RestoreDC(hdc, savedDC);
}

RECT GroupMemberList::ThumbRect() const
{
    return ComputeChatScrollThumb(ContentRect(), m_rcItem.bottom - m_rcItem.top, ContentHeight(),
                                  m_scrollY);
}

void GroupMemberList::SetScrollY(int scrollY)
{
    const int y = ClampChatScrollY(scrollY, m_rcItem.bottom - m_rcItem.top, ContentHeight());
    if (y == m_scrollY)
    {
        return;
    }
    m_scrollY = y;
    //矩形没变，必须用 ForceLayout 才会按新的滚动量重新摆放各行（SetRect 在矩形不变时直接返回）
    ForceLayout(m_rcItem);
    Invalidate();
}

balloonwjui::DuiControl* GroupMemberList::HitTest(POINT ptHostClient)
{
    balloonwjui::DuiControl* hit = DuiControl::HitTest(ptHostClient);
    if (hit != NULL && HitChatScrollBand(ptHostClient, ThumbRect(), ContentRect()))
    {
        return this;
    }
    return hit;
}

bool GroupMemberList::OnMouseWheel(POINT /*pt*/, short zDelta, UINT /*mkFlags*/)
{
    if (ContentHeight() <= m_rcItem.bottom - m_rcItem.top)
    {
        return false;   //内容没有超出，交给外层
    }
    //zDelta 为正表示向上滚。按 WHEEL_DELTA 折算，触控板发来的不足一格的小刻度也能按比例滚
    const int step = (int)((LONGLONG)zDelta * kGroupMemberWheelRows * kGroupMemberRowHeight
                           / WHEEL_DELTA);
    SetScrollY(m_scrollY - step);
    m_scrollFade.OnScrolled();
    return true;
}

bool GroupMemberList::OnLButtonDown(POINT pt, UINT mkFlags)
{
    //命中带的判断与 HitTest 共用 HitChatScrollBand，两处口径一致：HitTest 把这条带里的点击
    //截给了本控件，这里就必须接住
    const RECT thumb = ThumbRect();
    if (!HitChatScrollBand(pt, thumb, ContentRect()))
    {
        return DuiControl::OnLButtonDown(pt, mkFlags);
    }
    if (pt.y >= thumb.top && pt.y < thumb.bottom)
    {
        //抓在滑块上：记住抓点相对滑块上沿的偏移，拖动时滑块不跳
        m_thumbDragOffsetY = pt.y - thumb.top;
    }
    else
    {
        //点在轨道空白处：滑块中心先跳到点击处，再按住继续拖
        m_thumbDragOffsetY = (thumb.bottom - thumb.top) / 2;
        SetScrollY(ChatScrollYFromThumbTop(pt.y - m_thumbDragOffsetY, ContentRect(),
                                           m_rcItem.bottom - m_rcItem.top, ContentHeight()));
    }
    m_thumbDragging = true;
    m_scrollFade.SetDragging(true);
    Capture();
    return true;
}

bool GroupMemberList::OnMouseMove(POINT pt, UINT mkFlags)
{
    if (!m_thumbDragging)
    {
        //鼠标在命中带上时滑块淡入并保持可见，离开命中带后重新计时淡出
        m_scrollFade.SetHover(HitChatScrollBand(pt, ThumbRect(), ContentRect()));
        return DuiControl::OnMouseMove(pt, mkFlags);
    }
    SetScrollY(ChatScrollYFromThumbTop(pt.y - m_thumbDragOffsetY, ContentRect(),
                                       m_rcItem.bottom - m_rcItem.top, ContentHeight()));
    return true;
}

bool GroupMemberList::OnMouseLeave()
{
    m_scrollFade.OnOwnerMouseLeave();
    return DuiControl::OnMouseLeave();
}

bool GroupMemberList::OnLButtonUp(POINT pt, UINT mkFlags)
{
    if (!m_thumbDragging)
    {
        return DuiControl::OnLButtonUp(pt, mkFlags);
    }
    m_thumbDragging = false;
    m_scrollFade.SetDragging(false);
    ReleaseCapture();
    return true;
}

bool GroupMemberList::SetMemberPresence(int64_t userid, int presence)
{
    //部门树视图下同一个人可能有多行（兼属部门），每一行都要更新，不能找到第一行就返回
    bool found = false;
    for (size_t i = 0; i < m_children.size(); ++i)
    {
        GroupMemberItem* item = dynamic_cast<GroupMemberItem*>(m_children[i].get());
        if (item != nullptr && item->UserId() == userid)
        {
            item->SetPresence(presence);
            found = true;
        }
    }
    return found;
}

void GroupMemberList::SetMyNetRoleForAll(int netRole)
{
    for (size_t i = 0; i < m_children.size(); ++i)
    {
        GroupMemberItem* item = dynamic_cast<GroupMemberItem*>(m_children[i].get());
        if (item != nullptr)
        {
            item->SetMyNetRole(netRole);
        }
    }
}

void GroupMemberList::SetSelfConnectionStateForAll(int connUiState)
{
    for (size_t i = 0; i < m_children.size(); ++i)
    {
        GroupMemberItem* item = dynamic_cast<GroupMemberItem*>(m_children[i].get());
        if (item != nullptr)
        {
            item->SetSelfConnectionState(connUiState);
        }
    }
}

} // namespace balloonchatwnd
