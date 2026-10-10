/**
 *  帖子群窗口右侧的群成员面板，见 ThreadMemberPanel.h。
 *  balloonwj@qq.com   2026-10-08
 */
#include "stdafx.h"
#include "ThreadMemberPanel.h"

#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiEdit.h"
#include "../../../third-party/balloonui/balloonui/Controls/Feedback/DuiToolTip.h"   // 关闭按钮的悬停提示
#include "GroupMemberList.h"
#include "GroupChatWindowLayout.h"   // kGIdMemberSearch / kGIdMemberViewToggle / kGIdMemberList / ApplyMemberViewToggle
#include "ChatIconButton.h"          // 「平铺 / 按部门」切换按钮
#include "ChatIcons.h"               // kChatIconOrgTree
#include "GroupFollowTarget.h"       // FMG_MSG_QUERY_GROUP_FOLLOW 的负载
#include "../../Session/UserSessionData.h"   // FMG_MSG_QUERY_STARRED / FMG_MSG_QUERY_GROUP_FOLLOW
#include "../../net/GroupProtocolClient.h"   // net::GroupMemberItem / net::DepartmentItem
#include "../../Common/Log/IULog.h"
#include "../../Common/I18n/I18n.h"         // TR / TRF

using balloonwjui::DuiButton;
using balloonwjui::DuiControl;
using balloonwjui::DuiEdit;
using balloonwjui::DuiHBox;
using balloonwjui::DuiLabel;
using balloonwjui::DuiToolTipMgr;
using balloonwjui::DuiLayout;
using balloonwjui::DuiVBox;

namespace balloonchatwnd {

namespace {

//控件 id：搜索框、切换按钮与成员列表沿用群聊窗成员页签的 id（ApplyMemberViewToggle 按 id 找切换按钮）；其余为本面板自用
const UINT kIdPanelTitle = 9601;    //标题「群成员（N）」
const UINT kIdPanelClose = 9602;    //关闭按钮

//尺寸，单位：像素
const int kTitleRowH = 40;          //标题行高
const int kSearchRowH = 32;         //搜索行高
const int kRowPadX = 10;            //标题行与搜索行的左右内边距
const int kSearchGap = 6;           //搜索框与切换按钮之间的距离
const int kToggleWidth = 32;        //切换按钮宽
const int kCloseSize = 28;          //关闭按钮的边长
const int kTitlePointSize = 10;     //标题的字号（磅），加粗

//颜色
const COLORREF kPanelBg = RGB(0xFF, 0xFF, 0xFF);       //面板底色
const COLORREF kBorderColor = RGB(0xE5, 0xE5, 0xE5);   //面板四周的描边（盖在帖子群窗口上时与详情区分开）
const COLORREF kTitleColor = RGB(0x1A, 0x1A, 0x1A);    //标题文字

//滑入动画：每一步的间隔（毫秒）与总步数
const UINT kSlideTimerId = 1;
const UINT kSlideIntervalMs = 15;
const int kSlideSteps = 10;

//界面文字
const wchar_t* const kTitlePattern = TR_NOOP(L"群成员（{0}）");
const wchar_t* const kTitleLoading = TR_NOOP(L"群成员");
const wchar_t* const kSearchPlaceholder = TR_NOOP(L"搜索成员");
const wchar_t* const kCloseTip = TR_NOOP(L"关闭");
const wchar_t* const kCloseGlyph = L"✕";   //关闭按钮上的「✕」

} // namespace

//======================== CThreadMemberPanelHost ========================

CThreadMemberPanelHost::CThreadMemberPanelHost()
    : m_actionFn(NULL)
    , m_escapeFn(NULL)
    , m_user(NULL)
{
}

void CThreadMemberPanelHost::SetHandlers(ActionFn action, EscapeFn escape, void* user)
{
    m_actionFn = action;
    m_escapeFn = escape;
    m_user = user;
}

LRESULT CThreadMemberPanelHost::OnMentionAction(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& /*bHandled*/)
{
    //lParam 为成员行在堆上建的负载，无论处理与否都在这里释放
    std::unique_ptr<MentionActionData> data(reinterpret_cast<MentionActionData*>(lParam));
    if (data && m_actionFn != NULL)
        m_actionFn(m_user, *data);
    return 0;
}

LRESULT CThreadMemberPanelHost::OnKeyDown(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& bHandled)
{
    if (wParam == VK_ESCAPE && m_escapeFn != NULL)
    {
        m_escapeFn(m_user);
        return 0;
    }
    bHandled = FALSE;
    return 0;
}

//======================== CThreadMemberPanel ========================

CThreadMemberPanel::CThreadMemberPanel(int64_t groupId, IThreadMemberPanelOwner* owner, HWND hostPanel)
    : m_groupId(groupId)
    , m_owner(owner)
    , m_hostPanel(hostPanel)
    , m_list(NULL)
    , m_title(NULL)
    , m_selfId(0)
    , m_myRole(0)
    , m_membersLoaded(false)
    , m_slideX(0)
{
    ::SetRectEmpty(&m_targetRect);
    m_host.SetHandlers(&CThreadMemberPanel::ActionThunk, &CThreadMemberPanel::EscapeThunk, this);
}

CThreadMemberPanel::~CThreadMemberPanel()
{
    if (IsWindow())
        DestroyWindow();
}

int64_t CThreadMemberPanel::GroupId() const
{
    return m_groupId;
}

bool CThreadMemberPanel::Show(HWND parent, const RECT& rc)
{
    //帖子群窗口重建过（关掉又打开）：旧的子窗口已随父窗口销毁，重新建
    if (IsWindow() && GetParent() != parent)
        DestroyWindow();
    if (!IsWindow())
    {
        if (!CreateOnce(parent, rc))
            return false;
        //子窗口是新建的：已经到达的名单立即挂上（名单要等 1304 应答，第一次打开时这里还没有）
        if (m_membersLoaded)
            RebuildRows();
    }
    m_targetRect = rc;
    if (IsShown())
    {
        Move(rc);
        return true;
    }
    //从右边缘滑入：先放在目标位置右边一整个面板宽的地方，定时器每步往左挪
    m_slideX = rc.right;
    SetWindowPos(HWND_TOP, m_slideX, rc.top, rc.right - rc.left, rc.bottom - rc.top, SWP_SHOWWINDOW | SWP_NOACTIVATE);
    SetTimer(kSlideTimerId, kSlideIntervalMs);
    LOG_INFO("thread member panel shown, groupid=%lld.", static_cast<long long>(m_groupId));
    return true;
}

void CThreadMemberPanel::Move(const RECT& rc)
{
    m_targetRect = rc;
    if (!IsWindow())
        return;
    //滑入途中改了尺寸：停掉动画直接到位
    KillTimer(kSlideTimerId);
    m_slideX = rc.left;
    SetWindowPos(HWND_TOP, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top, SWP_NOACTIVATE);
}

void CThreadMemberPanel::HidePanel()
{
    if (!IsWindow())
        return;
    KillTimer(kSlideTimerId);
    ShowWindow(SW_HIDE);
}

bool CThreadMemberPanel::IsShown() const
{
    return IsWindow() && IsWindowVisible() != FALSE;
}

void CThreadMemberPanel::SetMembers(const std::vector<net::GroupMemberItem>& members, int64_t selfId, int myRole)
{
    m_members = members;
    m_selfId = selfId;
    m_myRole = myRole;
    m_membersLoaded = true;
    m_rowInfo = BuildGroupMemberRowInfo(members, selfId);
    RebuildRows();
}

void CThreadMemberPanel::SetDepartments(const std::vector<net::DepartmentItem>& depts)
{
    m_depts = depts;
    //部门只影响按部门显示
    if (m_membersLoaded && m_owner != NULL && m_owner->IsMemberTreeView())
        RebuildRows();
}

void CThreadMemberPanel::SetMemberPresence(int64_t userId, int presence)
{
    m_presence[userId] = presence;
    if (m_list != NULL && m_list->SetMemberPresence(userId, presence))
        m_list->Invalidate();
}

void CThreadMemberPanel::RefreshAvatars(const std::string& fileid)
{
    if (!m_membersLoaded)
        return;
    //只有用到这张照片的成员才需要换头像：没有这样的成员时不重建，避免每张头像下载完都重建整张列表
    if (!fileid.empty())
    {
        bool used = false;
        for (size_t i = 0; i < m_members.size() && !used; ++i)
            used = (m_members[i].m_customface == fileid);
        if (!used)
            return;
    }
    m_rowInfo = BuildGroupMemberRowInfo(m_members, m_selfId);
    RebuildRows();
}

bool CThreadMemberPanel::CreateOnce(HWND parent, const RECT& rc)
{
    //子窗口：裁剪自己的子窗口（balloonui 宿主），不抢父窗口的激活
    if (Create(parent, const_cast<RECT&>(rc), L"", WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, 0) == NULL)
    {
        LOG_ERROR("thread member panel: create failed, groupid=%lld, error=%lu.", static_cast<long long>(m_groupId),
                  ::GetLastError());
        return false;
    }
    RECT client;
    GetClientRect(&client);
    if (m_host.Create(m_hWnd, client, NULL, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN) == NULL)
    {
        LOG_ERROR("thread member panel: host create failed, groupid=%lld.", static_cast<long long>(m_groupId));
        DestroyWindow();
        return false;
    }
    //成员行的双击（打开单聊）要宿主合成双击才会收到，与群聊窗相同
    m_host.EnableDoubleClick(true);

    typedef DuiLayout::Hint Hint;
    //根：白底、四周一圈描边（DuiVBox 的卡片样式），里面自上而下是标题行、搜索行与成员列表
    std::unique_ptr<DuiVBox> content(new DuiVBox());
    content->SetBgColor(kPanelBg);
    content->SetBorderColor(kBorderColor);

    //标题行：「群成员（N）」与关闭按钮
    std::unique_ptr<DuiHBox> titleRow(new DuiHBox());
    titleRow->SetPadding(kRowPadX, 0, kRowPadX, 0);
    std::unique_ptr<DuiLabel> title(new DuiLabel());
    title->SetCtrlId(kIdPanelTitle);
    title->SetText(TR(kTitleLoading));
    title->SetTextColor(kTitleColor);
    title->SetTextPointSize(kTitlePointSize, true);
    m_title = title.get();
    titleRow->AddChild(std::unique_ptr<DuiControl>(title.release()), Hint().Weight(1));
    std::unique_ptr<DuiButton> close(new DuiButton());
    close->SetCtrlId(kIdPanelClose);
    close->SetText(kCloseGlyph);
    close->SetVariant(DuiButton::Variant::Ghost);
    //控件析构时 DuiControl 会自动注销提示
    DuiToolTipMgr::Inst().Register(close.get(), TR(kCloseTip));
    titleRow->AddChild(std::unique_ptr<DuiControl>(close.release()),
                       Hint().Fixed(kCloseSize, kCloseSize).AlignC(DuiLayout::AlignCenter));
    content->AddChild(std::unique_ptr<DuiControl>(titleRow.release()), Hint().Fixed(kTitleRowH));

    //搜索行：搜索框与「平铺 / 按部门」切换按钮，与群聊窗成员页签相同
    std::unique_ptr<DuiHBox> searchRow(new DuiHBox());
    searchRow->SetPadding(kRowPadX, 0, kRowPadX, 0);
    searchRow->SetGap(kSearchGap);
    std::unique_ptr<DuiEdit> search(new DuiEdit());
    search->SetCtrlId(kGIdMemberSearch);
    search->SetPlaceholder(TR(kSearchPlaceholder));
    search->SetDragDropEnabled(false);
    searchRow->AddChild(std::unique_ptr<DuiControl>(search.release()), Hint().Weight(1));
    std::unique_ptr<ChatIconButton> toggle(new ChatIconButton());
    toggle->SetCtrlId(kGIdMemberViewToggle);
    toggle->SetIconKind(kChatIconOrgTree);
    searchRow->AddChild(std::unique_ptr<DuiControl>(toggle.release()), Hint().Fixed(kToggleWidth));
    content->AddChild(std::unique_ptr<DuiControl>(searchRow.release()), Hint().Fixed(kSearchRowH));

    //成员列表
    std::unique_ptr<GroupMemberList> list(new GroupMemberList());
    list->SetCtrlId(kGIdMemberList);
    m_list = list.get();
    content->AddChild(std::unique_ptr<DuiControl>(list.release()), Hint().Weight(1));
    m_host.SetRoot(std::unique_ptr<DuiControl>(content.release()));

    const bool treeMode = (m_owner != NULL) && m_owner->IsMemberTreeView();
    ApplyMemberViewToggle(m_host.GetRoot(), treeMode);
    LOG_INFO("thread member panel created, groupid=%lld, tree=%d.", static_cast<long long>(m_groupId), treeMode ? 1 : 0);
    return true;
}

GroupMemberRowContext CThreadMemberPanel::RowContext()
{
    GroupMemberRowContext ctx;
    ctx.m_selfId = m_selfId;
    ctx.m_myRole = m_myRole;
    ctx.m_presence = &m_presence;
    ctx.m_starredFn = &CThreadMemberPanel::StarredThunk;
    ctx.m_starredUser = this;
    ctx.m_followFn = &CThreadMemberPanel::GroupFollowThunk;
    ctx.m_followUser = this;
    //帖子群没有聊天输入框，右键菜单里不出现「@TA」
    ctx.m_allowMention = false;
    return ctx;
}

void CThreadMemberPanel::RebuildRows()
{
    if (m_list == NULL)
        return;
    const bool treeMode = (m_owner != NULL) && m_owner->IsMemberTreeView();
    const size_t rows = FillGroupMemberList(m_list, m_members, m_rowInfo, treeMode, m_depts, RowContext());
    if (m_title != NULL)
    {
        DuiLabel* title = static_cast<DuiLabel*>(m_title);
        title->SetText(TRF(kTitlePattern, static_cast<int>(m_members.size())).c_str());
    }
    m_list->Invalidate();
    LOG_INFO("thread member panel rebuilt, groupid=%lld, members=%u, rows=%u, tree=%d.",
             static_cast<long long>(m_groupId), static_cast<unsigned>(m_members.size()), static_cast<unsigned>(rows),
             treeMode ? 1 : 0);
}

void CThreadMemberPanel::CloseByUser()
{
    HidePanel();
    LOG_INFO("thread member panel closed by user, groupid=%lld.", static_cast<long long>(m_groupId));
    if (m_owner != NULL)
        m_owner->OnMemberPanelClosed(m_groupId);
}

LRESULT CThreadMemberPanel::OnDuiNotify(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& /*bHandled*/)
{
    const balloonwjui::DuiNotify* pNotify = reinterpret_cast<const balloonwjui::DuiNotify*>(lParam);
    if (pNotify == NULL)
        return 0;
    //搜索框的文字变了（每按一个键一次）：按关键字过滤，没有命中时列表自己画「没有找到匹配的成员」
    if (pNotify->code == balloonwjui::DUIN_VALUECHANGED && pNotify->ctrlId == kGIdMemberSearch)
    {
        DuiEdit* search = dynamic_cast<DuiEdit*>(static_cast<DuiControl*>(pNotify->pCtrl));
        if (search != NULL && m_list != NULL)
        {
            m_list->SetFilterKeyword(search->GetText());
            m_list->Invalidate();
        }
        return 0;
    }
    //「平铺 / 按部门」切换：写进配置后按新视图重建（切换按钮不在列表里，不会被重建，可以就地处理）
    if (pNotify->code == balloonwjui::DUIN_CLICK && pNotify->ctrlId == kGIdMemberViewToggle)
    {
        const bool treeMode = (m_owner != NULL) && !m_owner->IsMemberTreeView();
        if (m_owner != NULL)
            m_owner->SetMemberTreeView(treeMode);
        ApplyMemberViewToggle(m_host.GetRoot(), treeMode);
        RebuildRows();
        return 0;
    }
    //关闭按钮：收起面板并通知所有者（隐藏不销毁控件，可以就地处理）
    if (pNotify->code == balloonwjui::DUIN_CLICK && pNotify->ctrlId == kIdPanelClose)
    {
        CloseByUser();
        return 0;
    }
    return 0;
}

LRESULT CThreadMemberPanel::OnSize(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& /*bHandled*/)
{
    if (m_host.IsWindow())
        m_host.SetWindowPos(NULL, 0, 0, LOWORD(lParam), HIWORD(lParam), SWP_NOZORDER | SWP_NOACTIVATE);
    return 0;
}

LRESULT CThreadMemberPanel::OnTimer(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& bHandled)
{
    if (wParam != kSlideTimerId)
    {
        bHandled = FALSE;
        return 0;
    }
    const int width = m_targetRect.right - m_targetRect.left;
    const int step = (width + kSlideSteps - 1) / kSlideSteps;
    m_slideX -= step;
    if (m_slideX <= m_targetRect.left)
    {
        m_slideX = m_targetRect.left;
        KillTimer(kSlideTimerId);
    }
    SetWindowPos(NULL, m_slideX, m_targetRect.top, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    return 0;
}

LRESULT CThreadMemberPanel::OnEraseBkgnd(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    return 1;
}

LRESULT CThreadMemberPanel::OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled)
{
    bHandled = FALSE;
    KillTimer(kSlideTimerId);
    m_list = NULL;
    m_title = NULL;
    LOG_INFO("thread member panel window destroyed, groupid=%lld.", static_cast<long long>(m_groupId));
    return 0;
}

void CThreadMemberPanel::ActionThunk(void* user, const MentionActionData& data)
{
    CThreadMemberPanel* self = static_cast<CThreadMemberPanel*>(user);
    if (self == NULL)
        return;
    //确认框的所有者用帖子群窗口（面板是它的子窗口）
    const HWND owner = self->IsWindow() ? self->GetParent() : NULL;
    ExecuteGroupMemberAction(data.id, self->m_groupId, data.uid, CString(data.displayName.c_str()), owner,
                             self->m_hostPanel);
}

void CThreadMemberPanel::EscapeThunk(void* user)
{
    CThreadMemberPanel* self = static_cast<CThreadMemberPanel*>(user);
    if (self != NULL)
        self->CloseByUser();
}

bool CThreadMemberPanel::StarredThunk(void* userdata, long long uid)
{
    CThreadMemberPanel* self = static_cast<CThreadMemberPanel*>(userdata);
    if (self == NULL || self->m_hostPanel == NULL || !::IsWindow(self->m_hostPanel))
        return false;   //拿不到主面板：按「未关注」处理
    //同步问主面板（两者都在界面线程上，负载放在栈上即可）
    const int64_t target = static_cast<int64_t>(uid);
    return ::SendMessage(self->m_hostPanel, FMG_MSG_QUERY_STARRED, 0, reinterpret_cast<LPARAM>(&target)) != 0;
}

bool CThreadMemberPanel::GroupFollowThunk(void* userdata, long long uid)
{
    CThreadMemberPanel* self = static_cast<CThreadMemberPanel*>(userdata);
    if (self == NULL || self->m_hostPanel == NULL || !::IsWindow(self->m_hostPanel))
        return false;
    GroupFollowTarget target;
    target.m_groupId = self->m_groupId;
    target.m_userId = static_cast<int64_t>(uid);
    return ::SendMessage(self->m_hostPanel, FMG_MSG_QUERY_GROUP_FOLLOW, 0, reinterpret_cast<LPARAM>(&target)) != 0;
}

} // namespace balloonchatwnd
