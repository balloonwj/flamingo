/**
 *  GroupManageWindowData.cpp
 *
 *  CBalloonGroupManageWindow 的"数据面"实现：向服务端拉取群资料 / 成员 /
 *  待审批申请，接收各类应答与推送，把结果映射进界面模型并触发重建。
 *
 *  与 BalloonGroupManageWindow.cpp 的分工：那边负责"界面长什么样"（构造控件树、
 *  切页、绘制标题栏），这边负责"界面显示什么"（协议收发与模型填充）。两个文件
 *  实现的是同一个类的不同成员函数。
 *
 *  balloonwj@qq.com   2026-07-31
 */

#include "stdafx.h"
#include "BalloonGroupManageWindow.h"

#include <time.h>

#include "../../Common/Log/IULog.h"                  // LOG_INFO / LOG_ERROR
#include "../../Common/Util/EncodeUtil.h"              // UnicodeToUtf8：界面文本转协议侧 UTF-8
#include "../../Session/UserSessionData.h"         // FMG_MSG_OPEN_CHAT / FMG_MSG_OPEN_CONTACT_PROFILE
#include "../BalloonMain/PopupMenuPlacement.h"   // ClampPopupMenuToWorkArea
#include "../../../third-party/balloonui/balloonui/Controls/List/DuiMenu.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiEdit.h"
#include "../../App/FlamingoClient.h"          // 取本人 userid
#include "../../net/IUSocket.h"            // 各 Send / Request 方法
#include "../../net/GroupProtocolClient.h" // 应答与推送的解析结构
#include "GroupManageLayout.h"          // GmPageId
#include "GroupManageMapping.h"         // Apply* / IsPageVisibleForRole / DescribeGroupOpFailure
#include "../BalloonMain/CreateGroupDlg.h"      // CCreateGroupDlg::Purpose（选人对话框的用途枚举）
#include "../BalloonMain/GroupExitFlow.h"       // BuildDismissConfirmText（与群资料窗口、群列表右键同一份文案）
#include "MemberPickerRequest.h"        // 打开选人对话框的请求载荷
#include "GroupAvatarDlgRequest.h"      // 打开修改群头像对话框的请求载荷
#include "../../Session/GroupRoleRules.h"          // CanChangeGroupAvatar：谁能改群头像
#include "../BalloonMain/AvatarSpec.h"  // avatar_kind_image：判断群头像是不是自定义图
#include "../../net/ImageTransfer.h"
#include "../../net/CurrentUserAccount.h"   // net::CurrentUserAccount（本地缓存目录按登录账号分）       // 群头像真图的本地缓存路径与下载
#include <memory>

using namespace balloongroupmanage;

// 当前时间，Unix毫秒。禁言剩余时长与"N 分钟前"都按这个口径算。
static int64_t NowMilliseconds()
{
    const int64_t kMillisPerSecond = 1000;
    return static_cast<int64_t>(time(NULL)) * kMillisPerSecond;
}

// 按页号向服务端拉一页公告，并记下本次发出去的分页参数 —— 应答回来时要拿它认领
// 属于自己的那一份（见 m_announceReqOffset 的说明）。
//   page：页号（0 起）；负数按 0 处理。
void CBalloonGroupManageWindow::RequestAnnouncementPage(int page)
{
    if (m_groupId <= 0)
    {
        return;
    }
    if (page < 0)
    {
        page = 0;
    }

    m_announceReqOffset = page * kAnnouncePageSize;
    m_announceReqCount  = kAnnouncePageSize;
    CIUSocket::GetInstance().SendGetGroupAnnouncements(
        m_groupId, m_announceReqOffset, m_announceReqCount);
}

// ====================================================================
// 拉取与刷新
// ====================================================================

int64_t CBalloonGroupManageWindow::GetMyUserId() const
{
    if (m_lpFMGClient == NULL)
    {
        return 0;
    }
    return static_cast<int64_t>(m_lpFMGClient->m_UserMgr.m_UserInfo.m_uUserID);
}

void CBalloonGroupManageWindow::RequestGroupData()
{
    //未绑定真实群（调试入口 --groupmanage 直接弹出的假数据窗口）时什么都不做
    if (m_groupId <= 0)
    {
        LOG_INFO("Group manage window: no real group bound, using mock data, no request sent.");
        return;
    }

    CIUSocket& socket = CIUSocket::GetInstance();

    //三条请求并发发出，应答各自到达、各填模型的一部分，谁都不清掉别人的结果
    socket.RequestGroupInfo(m_groupId);
    socket.RequestGroupMembers(m_groupId);

    //待审批列表只有群主 / 管理员有权拉。这里先按已知角色判断一次，免得普通成员
    //每次开窗都白挨一个 error_code_not_group_admin；真实角色要等群资料应答才确定，
    //那时若发现自己其实是管理员，OnGroupInfoArrived 会补拉。
    if (m_model.m_myRole == kRoleOwner || m_model.m_myRole == kRoleAdmin)
    {
        socket.SendGetGroupJoinRequests(m_groupId);
    }

    //公告先拉第一页。之所以开窗就拉、不等切到那一页：应答里的 total 还要用来
    //算左侧导航「群公告」那个计数徽章。
    RequestAnnouncementPage(0);

    LOG_INFO("Group manage window: requesting group data, groupid=%lld.", (long long)m_groupId);
}

void CBalloonGroupManageWindow::RefreshNavAndPage()
{
    //整体重建会新建全部控件。重建前先记下三样东西，重建后还给用户，否则后台任何一次数据
    //返回或推送都会清空正在写的公告、打断正在进行的输入、把列表滚回顶部：
    //输入框里尚未提交的文字（CaptureInputDrafts，切页时同样处理）、焦点所在的输入框及其
    //选区、滚动位置。
    const int pageBefore = m_currentPage;
    const int scrollBefore = (m_pContentScroll != NULL) ? m_pContentScroll->GetScrollPos() : 0;
    int  focusId  = 0;
    long selStart = 0;
    long selEnd   = 0;
    balloonwjui::DuiEdit* pFocusEdit = dynamic_cast<balloonwjui::DuiEdit*>(GetDuiFocus());
    if (pFocusEdit != NULL && IsDraftInput((int)pFocusEdit->GetCtrlId()))
    {
        focusId = (int)pFocusEdit->GetCtrlId();
        pFocusEdit->GetSel(selStart, selEnd);
    }
    CaptureInputDrafts();

    //整体重建客户区。InitUi 内部走的是 SetClientContent，它会先摘掉旧的客户区
    //再挂新的，反复调用是安全的；调完停在成员管理页，故随后再切回原来那一页。
    //InitUi 与 SwitchToPage 建好页面后都会调用 RestoreInputDrafts 填回草稿。
    const int wanted = PickVisiblePage(m_currentPage);
    InitUi();
    if (wanted != m_currentPage)
    {
        SwitchToPage(wanted);
    }

    //滚动位置要在布局完成、内容高度算好之后再恢复：InitUi 的 SetClientContent 与
    //SwitchToPage 都已完成布局。越界的位置由 SetScrollPos 收回到新内容的范围内
    const int scrollPos = ScrollPosAfterRebuild(pageBefore, m_currentPage, scrollBefore);
    if (m_pContentScroll != NULL && scrollPos > 0)
    {
        m_pContentScroll->SetScrollPos(scrollPos);
    }

    //焦点只在两种条件同时满足时还原：仍停在原来那一页（换了页说明原来的输入框已不可见），
    //且本窗口是当前活动窗口。给输入框设焦点时 DuiHost 会调用 ::SetFocus 拿 Win32 焦点，
    //本窗口在后台时这样做会把键盘焦点从用户正在使用的窗口（例如聊天窗）抢过来
    if (focusId != 0 && m_currentPage == pageBefore && ::GetActiveWindow() == m_hWnd)
    {
        balloonwjui::DuiControl* content = GetClientContent();
        balloonwjui::DuiEdit* pEdit = (content != NULL)
            ? dynamic_cast<balloonwjui::DuiEdit*>(content->FindCtrlById(focusId))
            : NULL;
        if (pEdit != NULL)
        {
            pEdit->SetFocus();
            pEdit->SetSel(selStart, selEnd);
        }
    }
}

int CBalloonGroupManageWindow::PickVisiblePage(int preferredPageId)
{
    if (IsPageVisibleForRole(preferredPageId, m_model.m_myRole))
    {
        return preferredPageId;
    }

    //成员管理页对所有角色都可见，是天然的兜底。走到这里说明本人角色刚被降过
    //（如被撤销管理员），原来停留的页已经不该看得见了。
    return kPageMembers;
}

// ====================================================================
// 应答落点
// ====================================================================

void CBalloonGroupManageWindow::OnGroupInfoArrived(const net::GroupInfoResult& info)
{
    if (info.m_code != 0)
    {
        LOG_ERROR("Group manage window: get group info failed, code=%d.", info.m_code);
        //规则同 OnGroupMembersArrived：从未成功过时「基本信息」「群权限」两页改为显示失败
        //提示，否则模型里的默认值会被当成真实设置显示；已经成功过时保留上一次的真实数据
        const LoadState before = m_model.m_groupInfoState;
        m_model.m_groupInfoState = LoadStateAfterFailure(before);
        if (m_model.m_groupInfoState != before)
        {
            RefreshNavAndPage();
        }
        return;
    }

    const int oldRole = m_model.m_myRole;
    ApplyGroupInfo(info, m_model);
    ResolveGroupAvatarImage();
    RefreshNavAndPage();

    //本人角色是从这条应答里才确定的。开窗时若按旧角色跳过了待审批列表，
    //这里发现自己其实是群主 / 管理员，就补拉一次。
    const bool nowCanApprove = (m_model.m_myRole == kRoleOwner || m_model.m_myRole == kRoleAdmin);
    const bool couldApproveBefore = (oldRole == kRoleOwner || oldRole == kRoleAdmin);
    if (nowCanApprove && !couldApproveBefore && m_groupId > 0)
    {
        CIUSocket::GetInstance().SendGetGroupJoinRequests(m_groupId);
    }

    LOG_INFO("Group manage window: group info arrived, groupid=%lld, membercount=%d, myRole=%d.",
             (long long)info.m_groupid, info.m_membercount, (int)m_model.m_myRole);
}

void CBalloonGroupManageWindow::OnGroupMembersArrived(const net::GroupMembersResult& members)
{
    if (members.m_code != 0)
    {
        LOG_ERROR("Group manage window: get group members failed, code=%d.", members.m_code);
        //从未成功过时改为显示失败提示，否则「正在加载」会一直挂着；已经成功过
        //（成员变动推送触发的重新拉取失败）时保留上一次的真实列表，界面不必重建
        const LoadState before = m_model.m_membersState;
        m_model.m_membersState = LoadStateAfterFailure(before);
        if (m_model.m_membersState != before)
        {
            RefreshNavAndPage();
        }
        return;
    }

    ApplyGroupMembers(members, NowMilliseconds(), m_model);
    RefreshNavAndPage();

    LOG_INFO("Group manage window: group members arrived, groupid=%lld, members=%u, muted=%u.",
             (long long)members.m_groupid,
             (unsigned)m_model.m_members.size(), (unsigned)m_model.m_muted.size());
}

void CBalloonGroupManageWindow::OnJoinRequestsArrived(const net::GroupJoinRequestsResult& requests)
{
    if (requests.m_code != 0)
    {
        //普通成员拉这条会被服务端拒（error_code_not_group_admin），属预期内，
        //记 INFO 不记 ERROR，免得日志里一堆吓人的红字
        LOG_INFO("Group manage window: get join requests rejected, code=%d (expected when not owner/admin).",
                 requests.m_code);
        //规则同 OnGroupMembersArrived：从未成功过才改为显示失败提示。普通成员看不到
        //「加群审批」页，状态变为失败对界面没有影响
        const LoadState before = m_model.m_requestsState;
        m_model.m_requestsState = LoadStateAfterFailure(before);
        if (m_model.m_requestsState != before)
        {
            RefreshNavAndPage();
        }
        return;
    }

    ApplyJoinRequests(requests, NowMilliseconds(), m_model);
    RefreshNavAndPage();

    LOG_INFO("Group manage window: join requests arrived, groupid=%lld, pending=%u.",
             (long long)requests.m_groupid, (unsigned)m_model.m_requests.size());
}

void CBalloonGroupManageWindow::OnSensitiveWordsArrived(const net::SensitiveWordsResult& words)
{
    if (words.m_code != 0)
    {
        LOG_INFO("Group manage window: get sensitive words rejected, code=%d.", words.m_code);
        //拉失败也要把"加载中"落下去，否则界面会一直停在那句话上
        m_model.m_sensitiveWordsLoaded = true;
        if (m_currentPage == kPageSensitive)
        {
            RefreshNavAndPage();
        }
        return;
    }

    ApplySensitiveWords(words, NowMilliseconds(), m_model);

    //只有正停在管理词库页时才重建 —— 别的页不显示词库，白重建一次没有意义
    if (m_currentPage == kPageSensitive)
    {
        RefreshNavAndPage();
    }

    LOG_INFO("Group manage window: sensitive words arrived, groupid=%lld, words=%u.",
             (long long)words.m_groupid, (unsigned)words.m_words.size());
}

void CBalloonGroupManageWindow::OnAnnouncementsArrived(const net::GroupAnnouncementsResult& result)
{
    //先认领：1323 应答由主面板同时投给本窗口与群聊窗的公告边栏，而两边的页大小
    //不一样（这里每页 kAnnouncePageSize 条，边栏一次只要 1 条）。不比对就会把
    //对方那份当成自己的收下 —— 表现为「共 3 条」但列表里只剩 1 条。
    if (!result.MatchesRequest(m_announceReqOffset, m_announceReqCount))
    {
        return;
    }

    if (result.m_code != 0)
    {
        LOG_INFO("Group manage window: get announcements rejected, code=%d.", result.m_code);
        //拉失败也要把"加载中"落下去，否则界面会一直停在那句话上
        m_model.m_announcementsLoaded = true;
        if (m_currentPage == kPageAnnouncement)
        {
            RefreshNavAndPage();
        }
        return;
    }

    ApplyGroupAnnouncements(result, NowMilliseconds(), m_model);

    //公告条数会改左侧导航的计数徽章，故无论当前停在哪一页都要重建一次
    RefreshNavAndPage();

    LOG_INFO("Group manage window: announcements arrived, groupid=%lld, pageItems=%u, total=%d.",
             (long long)result.m_groupid,
             (unsigned)m_model.m_announcements.size(), result.m_total);
}

void CBalloonGroupManageWindow::OnAnnouncementChanged(const net::GroupAnnouncementNotify& notify)
{
    if (m_groupId <= 0)
    {
        return;
    }

    //重拉当前这一页：推送只带变动的这一条，而列表要跟着变顺序、变总数、变页数，
    //本地维护这些增量的分支比重拉一次多得多，公告又是低频操作，不在热路径上。
    //
    //推送的 changetype 取值在此复制一份。本文件另抄而不 include net/Protocol.h：
    //原先是因为协议头与 v1 的 net/Msg.h 有同名枚举冲突（v1 协议头已于 2026-08-14
    //删除，该限制不复存在），现在的理由与本文件 kCmdOperateSensitiveWord 处相同。
    const int kAnnounceChangePublish = 1;   //有新公告发布

    //新公告发布时回到第一页 —— 它排在最前，用户多半就是想看它；删除则留在原页，
    //免得刚删一条就被弹回第一页、还得再翻回来。
    if (notify.m_changetype == kAnnounceChangePublish)
    {
        m_model.m_announcementPage = 0;
    }
    RequestAnnouncementPage(m_model.m_announcementPage);

    LOG_INFO("Group manage window: announcement change notify, groupid=%lld, changetype=%d, reloading page %d.",
             (long long)notify.m_groupid, notify.m_changetype,
             m_model.m_announcementPage + 1);
}

void CBalloonGroupManageWindow::OnGroupOpResult(int cmd, int code, const std::string& msgUtf8)
{
    //成功什么都不做：服务端写成功后会向全体成员推 1317 / 1318，本端也在推送
    //范围内，等推送统一刷新。若这里也改一遍，同一次操作界面会重排两次。
    //
    //<u>唯一的例外是敏感词增删</u>：词库变更不在 1317 / 1318 的推送范围里
    //（它不是群资料、也不是成员变动），没有推送可等，只能靠这条应答触发重拉。
    if (code == 0)
    {
        const int kCmdOperateSensitiveWord = 1322;
        if (cmd == kCmdOperateSensitiveWord && m_groupId > 0)
        {
            CIUSocket::GetInstance().SendGetSensitiveWords(m_groupId);
        }
        LOG_INFO("Group manage operation succeeded, cmd=%d, waiting for notify to refresh UI.", cmd);
        return;
    }

    //失败没有推送，只能靠这条应答给反馈
    const CString tip = DescribeGroupOpFailure(cmd, code, msgUtf8);
    LOG_ERROR("Group manage operation failed, cmd=%d, code=%d.", cmd, code);
    ::MessageBox(m_hWnd, (LPCTSTR)tip, _T("提示"), MB_OK | MB_ICONWARNING);

    //失败后本地可能已被乐观改过（如开关拨到一半），重拉一次拉回真实状态
    RequestGroupData();
}

// ====================================================================
// 推送落点
// ====================================================================

void CBalloonGroupManageWindow::OnMemberChanged(const net::GroupMemberChangedNotify& notify)
{
    //推送只带变动的 userid，不带这些人的昵称 / 群名片 / 角色 —— 本地要么没有
    //（新加入的成员），要么未必是最新的。重拉一次的代价是一个来回，换来列表
    //必定与服务端一致，比拼命维护本地增量靠谱。
    if (m_groupId > 0)
    {
        CIUSocket::GetInstance().RequestGroupMembers(m_groupId);

        //角色变更会影响本人能看到哪些页，群资料也要跟着重拉一次
        const int kMemberChangeRole = 4;
        if (notify.m_changetype == kMemberChangeRole)
        {
            CIUSocket::GetInstance().RequestGroupInfo(m_groupId);
        }
    }

    LOG_INFO("Group manage window: member change notify, groupid=%lld, changetype=%d, reloading members.",
             (long long)notify.m_groupid, notify.m_changetype);
}

bool CBalloonGroupManageWindow::OnInfoChanged(const net::GroupInfoChangedNotify& notify)
{
    //changetype=2 表示群已被解散。本窗口管的就是这个群，群没了留着没有意义。
    const int kInfoChangeDismissed = 2;
    if (notify.m_changetype == kInfoChangeDismissed)
    {
        LOG_INFO("Group manage window: group dismissed notify, groupid=%lld, closing window.",
                 (long long)notify.m_info.m_groupid);
        ::MessageBox(m_hWnd, _T("该群聊已被解散。"), _T("提示"), MB_OK | MB_ICONINFORMATION);
        if (IsWindow())
        {
            DestroyWindow();
        }
        return true;
    }

    //推送体带的是完整的新群资料，照着覆盖即可，不用再拉一次
    ApplyGroupInfo(notify.m_info, m_model);
    ResolveGroupAvatarImage();
    RefreshNavAndPage();

    LOG_INFO("Group manage window: group info change notify, groupid=%lld.",
             (long long)notify.m_info.m_groupid);
    return false;
}

void CBalloonGroupManageWindow::OnJoinRequestNotify()
{
    if (m_groupId <= 0)
    {
        return;
    }

    //推送只带一条申请，而审批页要显示全部待处理的，重拉一次
    CIUSocket::GetInstance().SendGetGroupJoinRequests(m_groupId);
    LOG_INFO("Group manage window: join request notify, reloading pending requests.");
}

// ====================================================================
// 交互：成员右键菜单与各页按钮
// ====================================================================

bool CBalloonGroupManageWindow::ConfirmAction(LPCTSTR title, LPCTSTR message)
{
    return ::MessageBox(m_hWnd, message, title,
                        MB_OKCANCEL | MB_ICONWARNING | MB_DEFBUTTON2) == IDOK;
}

void CBalloonGroupManageWindow::ShowMuteDurationMenu(int64_t userId, POINT anchor)
{
    //禁言时长档位（秒）。取值与菜单项 id 一一对应，id 从 1 起（0 表示没选）。
    const int kMuteDur10Min = 10 * 60;
    const int kMuteDur1Hour = 60 * 60;
    const int kMuteDur12Hour = 12 * 60 * 60;
    const int kMuteDur1Day = 24 * 60 * 60;
    const int kMuteDurForever = 0;          //0 = 永久，与服务端约定一致

    const UINT kIdDur10Min = 1;
    const UINT kIdDur1Hour = 2;
    const UINT kIdDur12Hour = 3;
    const UINT kIdDur1Day = 4;
    const UINT kIdDurForever = 5;

    balloonwjui::DuiMenu menu;
    menu.AppendItem(kIdDur10Min, _T("禁言 10 分钟"));
    menu.AppendItem(kIdDur1Hour, _T("禁言 1 小时"));
    menu.AppendItem(kIdDur12Hour, _T("禁言 12 小时"));
    menu.AppendItem(kIdDur1Day, _T("禁言 1 天"));
    menu.AppendSeparator();
    menu.AppendItem(kIdDurForever, _T("永久禁言"));

    const POINT pos = balloonmain::ClampPopupMenuToWorkArea(anchor, menu.MeasureSize());
    const UINT picked = menu.TrackPopup(pos.x, pos.y, m_hWnd);
    if (picked == 0)
    {
        return;
    }

    int duration = kMuteDurForever;
    switch (picked)
    {
    case kIdDur10Min:
        duration = kMuteDur10Min;
        break;
    case kIdDur1Hour:
        duration = kMuteDur1Hour;
        break;
    case kIdDur12Hour:
        duration = kMuteDur12Hour;
        break;
    case kIdDur1Day:
        duration = kMuteDur1Day;
        break;
    case kIdDurForever:
        duration = kMuteDurForever;
        break;
    default:
        return;
    }

    //禁言原因走完整对话框才好填，右键这条快捷路径不填 —— 服务端允许为空。
    CIUSocket::GetInstance().SendMuteMember(m_groupId, userId, true, duration, std::string());
    LOG_INFO("Send mute request, groupid=%lld, userid=%lld, duration=%d sec.",
             (long long)m_groupId, (long long)userId, duration);
}

void CBalloonGroupManageWindow::ShowMemberContextMenu(int memberIndex)
{
    if (memberIndex < 0 || memberIndex >= (int)m_model.m_members.size())
    {
        return;
    }
    const GroupMember& member = m_model.m_members[memberIndex];

    //未绑定真实群（调试入口的假数据）时不弹菜单 —— 点了也发不出请求
    if (m_groupId <= 0)
    {
        LOG_INFO("Group manage window: no real group bound, member context menu not shown.");
        return;
    }

    MemberMenuState state;
    state.m_myRole = m_model.m_myRole;
    state.m_targetRole = member.role;
    state.m_targetMuted = member.muted;
    state.m_isSelf = (member.userid != 0 && member.userid == GetMyUserId());

    const std::vector<MemberMenuEntry> model = BuildMemberMenuModel(state);
    if (model.empty())
    {
        return;
    }

    balloonwjui::DuiMenu menu;
    for (size_t i = 0; i < model.size(); ++i)
    {
        if (model[i].m_isSeparator)
        {
            menu.AppendSeparator();
        }
        else
        {
            menu.AppendItem(model[i].m_id, model[i].m_label);
        }
    }

    //菜单落点取当前光标位置，经统一的夹取函数保证整体落在显示器工作区内
    //（开发约定文档的菜单约定：不允许把原始光标坐标直接交给 TrackPopup）。
    POINT cursor;
    ::GetCursorPos(&cursor);
    const POINT pos = balloonmain::ClampPopupMenuToWorkArea(cursor, menu.MeasureSize());
    const UINT picked = menu.TrackPopup(pos.x, pos.y, m_hWnd);
    if (picked == 0)
    {
        return;
    }

    CIUSocket& socket = CIUSocket::GetInstance();
    switch (picked)
    {
    case kMemberMenuSendMsg:
        //打开单聊窗归主面板管（窗口集合在它手里），这里发一条窗口间消息请它
        //代劳。没有宿主面板（调试入口直接弹出的窗口）或宿主面板已销毁时静默跳过。
        if (::IsWindow(m_hHostPanel))
        {
            ::PostMessage(m_hHostPanel, FMG_MSG_OPEN_CHAT, (WPARAM)member.userid, 0);
        }
        break;

    case kMemberMenuProfile:
        if (::IsWindow(m_hHostPanel))
        {
            ::PostMessage(m_hHostPanel, FMG_MSG_OPEN_CONTACT_PROFILE,
                          (WPARAM)member.userid, 0);
        }
        break;

    case kMemberMenuSetAdmin:
        socket.SendSetMemberRole(m_groupId, member.userid, true);
        break;

    case kMemberMenuCancelAdmin:
        socket.SendSetMemberRole(m_groupId, member.userid, false);
        break;

    case kMemberMenuMute:
        //二级菜单选时长
        ShowMuteDurationMenu(member.userid, cursor);
        break;

    case kMemberMenuUnmute:
        socket.SendMuteMember(m_groupId, member.userid, false, 0, std::string());
        break;

    case kMemberMenuKick:
    {
        //移出是不可撤销的，先确认
        CString tip;
        tip.Format(_T("确定把「%s」移出本群吗？\n\n移出后该成员将无法再看到本群消息。"),
                   (LPCTSTR)member.name);
        if (!ConfirmAction(_T("移出成员"), tip))
        {
            break;
        }
        std::vector<int64_t> ids;
        ids.push_back(member.userid);
        socket.SendKickGroupMembers(m_groupId, ids);
        break;
    }

    default:
        break;
    }
}

bool CBalloonGroupManageWindow::HandlePageButton(UINT ctrlId, LPARAM extra)
{
    if (m_groupId <= 0)
    {
        //假数据窗口：按钮不接真实协议
        return false;
    }

    CIUSocket& socket = CIUSocket::GetInstance();

    //---- 群权限页：六个开关 + 两个下拉，拨动即生效 ----
    if (SendPermChange(ctrlId, (int)extra))
    {
        return true;
    }

    //---- 群公告页：优先级 / 清空 / 发布 / 逐条重新发布与撤回 ----
    if (HandleAnnouncementButton(ctrlId))
    {
        return true;
    }

    //---- 管理词库页：逐条删除 ----
    if (ctrlId >= kIdSensitiveDelBase && ctrlId <= kIdSensitiveDelMax)
    {
        const size_t row = ctrlId - kIdSensitiveDelBase;
        if (row < m_model.m_sensitiveWords.size())
        {
            socket.SendDeleteSensitiveWord(m_groupId, m_model.m_sensitiveWords[row].id);
            //删除没有推送可等（敏感词库不在 1318 的范围里），本地先摘掉这一条，
            //应答成功后再重拉一次对齐。失败时重拉会把它带回来。
            m_model.m_sensitiveWords.erase(m_model.m_sensitiveWords.begin() + row);
            RefreshNavAndPage();
        }
        return true;
    }

    //---- 加群审批：逐条同意 / 拒绝 ----
    if (ctrlId >= kIdApprovalAcceptBase && ctrlId < kIdApprovalAcceptBase + 50)
    {
        const size_t row = ctrlId - kIdApprovalAcceptBase;
        if (row < m_model.m_requests.size())
        {
            socket.SendHandleJoinGroup(m_model.m_requests[row].requestid, true);
        }
        return true;
    }
    if (ctrlId >= kIdApprovalRejectBase && ctrlId < kIdApprovalRejectBase + 50)
    {
        const size_t row = ctrlId - kIdApprovalRejectBase;
        if (row < m_model.m_requests.size())
        {
            socket.SendHandleJoinGroup(m_model.m_requests[row].requestid, false);
        }
        return true;
    }

    //---- 禁言管理：逐条解除 ----
    if (ctrlId >= kIdMuteReleaseBase && ctrlId < kIdMuteReleaseBase + 50)
    {
        const size_t row = ctrlId - kIdMuteReleaseBase;
        if (row < m_model.m_muted.size())
        {
            socket.SendMuteMember(m_groupId, m_model.m_muted[row].userid,
                                  false, 0, std::string());
        }
        return true;
    }

    //---- 禁言管理：逐条延长（在原基础上再加一小时）----
    if (ctrlId >= kIdMuteExtendBase && ctrlId < kIdMuteExtendBase + 50)
    {
        const size_t row = ctrlId - kIdMuteExtendBase;
        if (row < m_model.m_muted.size())
        {
            POINT cursor;
            ::GetCursorPos(&cursor);
            ShowMuteDurationMenu(m_model.m_muted[row].userid, cursor);
        }
        return true;
    }

    switch (ctrlId)
    {
    //---- 基本信息：跳到群公告页 ----
    case kIdBtnAnnounceGoto:
        SwitchToPage(kPageAnnouncement);
        return true;

    //---- 群权限：进入管理词库二级界面 ----
    case kIdBtnSensitiveDict:
        //每次进入都重拉一次 —— 词库可能被别的管理员改过，且它不在推送范围内
        m_model.m_sensitiveWordsLoaded = false;
        m_model.m_sensitiveWords.clear();
        socket.SendGetSensitiveWords(m_groupId);
        SwitchToPage(kPageSensitive);
        return true;

    //---- 管理词库：返回群权限 ----
    case kIdBtnSensitiveBack:
        SwitchToPage(kPagePerms);
        return true;

    //---- 管理词库：添加 ----
    case kIdBtnSensitiveAdd:
        AddSensitiveWordFromInput();
        return true;

    //---- 成员管理：邀请成员 ----
    case kIdBtnInvite:
        OpenMemberPicker(CCreateGroupDlg::kPurposeInvite);
        return true;

    //---- 禁言管理：添加禁言成员 ----
    case kIdBtnAddMute:
        OpenMemberPicker(CCreateGroupDlg::kPurposeMute);
        return true;

    //---- 危险区域：转让群主 ----
    case kIdBtnTransfer:
        OpenMemberPicker(CCreateGroupDlg::kPurposeTransfer);
        return true;

    //---- 加群审批：批量 ----
    case kIdBtnApproveAll:
    case kIdBtnRejectAll:
    {
        const bool accept = (ctrlId == kIdBtnApproveAll);
        if (m_model.m_requests.empty())
        {
            return true;
        }
        CString tip;
        tip.Format(accept ? _T("确定同意全部 %d 条加群申请吗？")
                          : _T("确定拒绝全部 %d 条加群申请吗？"),
                   (int)m_model.m_requests.size());
        if (!ConfirmAction(accept ? _T("批量同意") : _T("批量拒绝"), tip))
        {
            return true;
        }
        //服务端没有批量审批接口，逐条发。每条各自回一个应答，界面靠推送统一刷新。
        for (size_t i = 0; i < m_model.m_requests.size(); ++i)
        {
            socket.SendHandleJoinGroup(m_model.m_requests[i].requestid, accept);
        }
        return true;
    }

    //---- 禁言管理：全员禁言开关 ----
    case kIdSwitchMuteAll:
        //开关控件已被用户拨到新位置，这里按"拨过之后"的目标值发请求。
        //失败时 OnGroupOpResult 会重拉数据把开关拨回去。
        socket.SendMuteAll(m_groupId, !m_model.m_muteAll);
        return true;

    //---- 基本信息：保存 ----
    case kIdBtnSave:
        SaveBasicInfo();
        return true;

    //---- 基本信息：重置（丢弃编辑，重拉一次）----
    case kIdBtnReset:
        RevertBasicInfoInputs();
        RequestGroupData();
        return true;

    //---- 基本信息：复制群号 ----
    case kIdBtnCopyGroupNo:
        CopyGroupNumberToClipboard();
        return true;

    //---- 基本信息：更换头像 ----
    case kIdBtnChangeAvatar:
        RequestOpenAvatarDlg();
        return true;

    //---- 危险区域：解散群 ----
    case kIdBtnDismiss:
    {
        const std::wstring tip = balloonmain::BuildDismissConfirmText((LPCTSTR)m_model.m_groupName);
        if (!ConfirmAction(_T("解散群聊"), tip.c_str()))
        {
            return true;
        }
        socket.SendDismissGroup(m_groupId);
        return true;
    }

    //---- 危险区域：移除全部成员 ----
    case kIdBtnRemoveAll:
    {
        //收集除群主与自己之外的全部成员
        std::vector<int64_t> ids;
        const int64_t myId = GetMyUserId();
        for (size_t i = 0; i < m_model.m_members.size(); ++i)
        {
            const GroupMember& m = m_model.m_members[i];
            if (m.role == kRoleOwner || m.userid == myId || m.userid <= 0)
                continue;
            ids.push_back(m.userid);
        }
        if (ids.empty())
        {
            ::MessageBox(m_hWnd, _T("本群没有可移除的成员。"), _T("提示"),
                         MB_OK | MB_ICONINFORMATION);
            return true;
        }

        CString tip;
        tip.Format(_T("确定移除全部 %d 名成员吗？\n\n此操作不可恢复。"), (int)ids.size());
        if (!ConfirmAction(_T("移除全部成员"), tip))
        {
            return true;
        }
        socket.SendKickGroupMembers(m_groupId, ids);
        return true;
    }

    default:
        break;
    }

    return false;
}

void CBalloonGroupManageWindow::ResolveGroupAvatarImage()
{
    m_model.m_groupAvatarImagePath.Empty();

    //不是自定义图头像（字形 / 自动）就没有真图可画，字形那套已经由 ApplyGroupInfo
    //填好了。
    if (m_model.m_groupAvatarKind != balloonmain::avatar_kind_image
        || m_model.m_groupAvatarFileId.IsEmpty())
    {
        return;
    }

    //头像缓存按登录账号分桶，未登录（调试入口起的窗口）时不去碰它。
    const int64_t selfId = GetMyUserId();
    if (selfId <= 0)
    {
        return;
    }

    const std::string fileId =
        EncodeUtil::UnicodeToUtf8(std::wstring((LPCTSTR)m_model.m_groupAvatarFileId));
    const std::wstring path =
        net::ImageTransfer::AvatarCacheFilePathForFileId(net::CurrentUserAccount(), fileId);
    if (!path.empty() && ::GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES)
    {
        m_model.m_groupAvatarImagePath = path.c_str();
        return;
    }

    //缓存没命中：先留空（这一轮画字形占位），同时把图拉下来。下载完成后主面板
    //会把结果转回本窗口的 OnAvatarDownloaded，那时再解析一次并重建界面。
    net::ImageTransfer::GetInstance().EnqueueAvatarDownload(fileId, net::CurrentUserAccount());
    LOG_INFO("Group avatar image not in cache, download requested, groupid=%lld.", (long long)m_groupId);
}

void CBalloonGroupManageWindow::OnAvatarDownloaded(const std::string& fileId)
{
    //只关心本群那张图；别人的头像下好了与本窗口无关。
    if (fileId.empty() || m_model.m_groupAvatarFileId.IsEmpty())
    {
        return;
    }
    const std::string mine =
        EncodeUtil::UnicodeToUtf8(std::wstring((LPCTSTR)m_model.m_groupAvatarFileId));
    if (mine != fileId)
    {
        return;
    }

    ResolveGroupAvatarImage();
    RefreshNavAndPage();
    LOG_INFO("Group avatar image downloaded, group manage window refreshed, groupid=%lld.", (long long)m_groupId);
}

void CBalloonGroupManageWindow::RequestOpenAvatarDlg()
{
    if (m_groupId <= 0)
    {
        return;
    }

    //按钮已按角色置灰过，正常点不到这里；但角色可能在窗口开着时被撤（1317 推送
    //刷新界面之前有个空档），故再挡一道，免得发出去必然被服务端拒的请求。
    if (!grouprole::CanChangeGroupAvatar(UiRoleToNetRole(m_model.m_myRole)))
    {
        ::MessageBox(m_hWnd, _T("只有群主和管理员可以修改群头像。"), _T("提示"),
                     MB_OK | MB_ICONINFORMATION);
        return;
    }

    if (!::IsWindow(m_hHostPanel))
    {
        //调试入口（--groupmanage）单独起窗口时没有宿主主面板；宿主主面板已销毁时同样
        //无法代为打开对话框。
        LOG_ERROR("Open group avatar dialog failed: host panel is not set or already destroyed.");
        return;
    }

    std::unique_ptr<balloongroupmanage::GroupAvatarDlgRequest> req(
        new balloongroupmanage::GroupAvatarDlgRequest());
    req->m_groupId        = m_groupId;
    req->m_groupNameUtf8  = EncodeUtil::UnicodeToUtf8(std::wstring((LPCTSTR)m_model.m_groupName));
    //头像用协议原值，不用界面上那份解码结果 —— 对话框要靠它预选当前头像。
    req->m_facetype       = m_model.m_groupFacetype;
    req->m_customfaceUtf8 = m_model.m_groupCustomfaceUtf8;

    //投递后所有权转给主面板，由它处理完 delete
    ::PostMessage(m_hHostPanel, FMG_MSG_OPEN_GROUP_AVATAR_DLG, 0, (LPARAM)req.release());
    LOG_INFO("Request to open group avatar dialog, groupid=%lld.", (long long)m_groupId);
}

void CBalloonGroupManageWindow::SaveBasicInfo()
{
    if (m_groupId <= 0)
    {
        return;
    }

    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }

    net::UpdateGroupInfoParams params;
    params.m_groupid = m_groupId;

    //逐个输入框取值，只把"改过的"标成待更新 —— 没动过的项不发，避免多端
    //同时编辑时互相覆盖（见方案文档 §4.1 对 has_* 的说明）。
    balloonwjui::DuiEdit* pName = dynamic_cast<balloonwjui::DuiEdit*>(
        content->FindCtrlById(kIdEditGroupName));
    if (pName != NULL)
    {
        //去掉首尾空白后再比较；清空（或只剩空白）时发空串，服务端据此恢复为自动群名
        const std::string value = net::GroupProtocolClient::normalizeGroupNameInput(
            EncodeUtil::UnicodeToUtf8(std::wstring((LPCTSTR)pName->GetText())));
        const std::string current = EncodeUtil::UnicodeToUtf8(
            std::wstring((LPCTSTR)m_model.m_groupName));
        if (value != current)
        {
            params.m_hasName = true;
            params.m_name = value;
        }
    }

    //群公告刻意不在这里发：它已经是一条条带标题 / 发布人 / 优先级的记录，编辑
    //入口统一在「群公告」页（走 cmd_publish_group_announcement）。基本信息页上
    //那一行只是当前最新公告的只读摘要，没有输入框可读。
    balloonwjui::DuiEdit* pIntro = dynamic_cast<balloonwjui::DuiEdit*>(
        content->FindCtrlById(kIdEditIntro));
    if (pIntro != NULL)
    {
        const CString value = pIntro->GetText();
        if (value != m_model.m_intro)
        {
            params.m_hasIntro = true;
            params.m_intro = EncodeUtil::UnicodeToUtf8(std::wstring((LPCTSTR)value));
        }
    }

    if (!params.m_hasName && !params.m_hasIntro)
    {
        ::MessageBox(m_hWnd, _T("没有需要保存的修改。"), _T("提示"),
                     MB_OK | MB_ICONINFORMATION);
        return;
    }

    CIUSocket::GetInstance().SendUpdateGroupInfo(params);
    LOG_INFO("Send update group info request, groupid=%lld, hasName=%d, hasIntro=%d.",
             (long long)m_groupId, (int)params.m_hasName, (int)params.m_hasIntro);
}

// ====================================================================
// 群公告页
// ====================================================================

bool CBalloonGroupManageWindow::HandleAnnouncementButton(UINT ctrlId)
{
    if (m_groupId <= 0)
    {
        return false;
    }

    CIUSocket& socket = CIUSocket::GetInstance();

    //逐条「重新发布」。服务端会读回原件、以当前时间新发一条，历史里因此留下
    //两条记录 —— 这是刻意的，改旧那条的时间会让先前那次发布凭空消失。
    if (ctrlId >= kIdAnnounceRepublishBase && ctrlId <= kIdAnnounceRepublishMax)
    {
        const size_t row = ctrlId - kIdAnnounceRepublishBase;
        if (row < m_model.m_announcements.size())
        {
            CString tip;
            tip.Format(_T("确定重新发布「%s」吗？\n\n全体成员会再收到一次公告提醒。"),
                       (LPCTSTR)m_model.m_announcements[row].title);
            if (ConfirmAction(_T("重新发布"), tip))
            {
                socket.SendRepublishGroupAnnouncement(m_groupId, m_model.m_announcements[row].id);
            }
        }
        return true;
    }

    //逐条「删除」。真删不可恢复，必须过一道确认，提示语里把这一点说明白。
    if (ctrlId >= kIdAnnounceDeleteBase && ctrlId <= kIdAnnounceDeleteMax)
    {
        const size_t row = ctrlId - kIdAnnounceDeleteBase;
        if (row < m_model.m_announcements.size())
        {
            CString tip;
            tip.Format(_T("确定删除「%s」吗？\n\n删除后这条公告将从群公告里彻底消失，")
                       _T("成员与管理员都不再看得到，且无法恢复。"),
                       (LPCTSTR)m_model.m_announcements[row].title);
            if (ConfirmAction(_T("删除公告"), tip))
            {
                socket.SendDeleteGroupAnnouncement(m_groupId, m_model.m_announcements[row].id);
            }
        }
        return true;
    }

    switch (ctrlId)
    {
    //历史公告翻页。翻页即重新向服务端要那一页 —— 本地只留当前这一页。
    case kIdAnnouncePagePrev:
        if (m_model.m_announcementPage > 0)
        {
            --m_model.m_announcementPage;
            RequestAnnouncementPage(m_model.m_announcementPage);
        }
        return true;

    case kIdAnnouncePageNext:
        if (m_model.m_announcementPage + 1 < m_model.AnnouncementPageCount())
        {
            ++m_model.m_announcementPage;
            RequestAnnouncementPage(m_model.m_announcementPage);
        }
        return true;

    //优先级三选一。选中态靠"哪个按钮画成主色"表达，故改完要整页重建一次。
    case kIdChipSeverityNormal:
        m_model.m_draftSeverity = kSeverityNormal;
        RefreshNavAndPage();
        return true;

    case kIdChipSeverityImportant:
        m_model.m_draftSeverity = kSeverityImportant;
        RefreshNavAndPage();
        return true;

    case kIdChipSeverityUrgent:
        m_model.m_draftSeverity = kSeverityUrgent;
        RefreshNavAndPage();
        return true;

    case kIdBtnAnnounceClear:
        ClearAnnouncementDraft();
        return true;

    case kIdBtnAnnouncePublish:
        PublishAnnouncementFromInput();
        return true;

    default:
        break;
    }

    return false;
}

void CBalloonGroupManageWindow::ClearAnnouncementDraft()
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }

    balloonwjui::DuiEdit* pTitle = dynamic_cast<balloonwjui::DuiEdit*>(
        content->FindCtrlById(kIdEditAnnounceTitle));
    if (pTitle != NULL)
    {
        pTitle->SetText(_T(""));
    }

    balloonwjui::DuiEdit* pBody = dynamic_cast<balloonwjui::DuiEdit*>(
        content->FindCtrlById(kIdEditAnnounceBody));
    if (pBody != NULL)
    {
        pBody->SetText(_T(""));
    }
}

void CBalloonGroupManageWindow::RevertBasicInfoInputs()
{
    balloonwjui::DuiControl* content = GetClientContent();
    const int ids[] = { kIdEditGroupName, kIdEditIntro };
    for (size_t i = 0; i < sizeof(ids) / sizeof(ids[0]); ++i)
    {
        std::wstring baseline;
        if (!m_inputDrafts.Revert(ids[i], &baseline) || content == NULL)
        {
            continue;
        }
        balloonwjui::DuiEdit* pEdit = dynamic_cast<balloonwjui::DuiEdit*>(content->FindCtrlById(ids[i]));
        if (pEdit != NULL)
        {
            pEdit->SetText(baseline.c_str());
        }
    }
    LOG_INFO("Group manage window: basic info inputs reverted, groupid=%lld.", (long long)m_groupId);
}

void CBalloonGroupManageWindow::PublishAnnouncementFromInput()
{
    if (m_groupId <= 0)
    {
        return;
    }

    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }

    balloonwjui::DuiEdit* pTitle = dynamic_cast<balloonwjui::DuiEdit*>(
        content->FindCtrlById(kIdEditAnnounceTitle));
    if (pTitle == NULL)
    {
        return;
    }

    CString title = pTitle->GetText();
    title.Trim();
    if (title.IsEmpty())
    {
        //服务端也会拒（标题是必填项），但提前挡掉能立刻给反馈
        ::MessageBox(m_hWnd, _T("请先填写公告标题。"), _T("提示"),
                     MB_OK | MB_ICONINFORMATION);
        return;
    }

    CString body;
    balloonwjui::DuiEdit* pBody = dynamic_cast<balloonwjui::DuiEdit*>(
        content->FindCtrlById(kIdEditAnnounceBody));
    if (pBody != NULL)
    {
        body = pBody->GetText();
    }

    CIUSocket::GetInstance().SendPublishGroupAnnouncement(
        m_groupId,
        EncodeUtil::UnicodeToUtf8(std::wstring((LPCTSTR)title)),
        EncodeUtil::UnicodeToUtf8(std::wstring((LPCTSTR)body)),
        m_model.m_draftSeverity);

    //就地清空表单，让"已经发出去了"这件事在界面上看得见。发失败时
    //OnGroupOpResult 会弹提示，用户重打一遍的代价小于"发成功了却还留着草稿、
    //以为没发出去又发一遍"。
    ClearAnnouncementDraft();

    LOG_INFO("Send publish announcement request, groupid=%lld, severity=%d.",
             (long long)m_groupId, m_model.m_draftSeverity);
}

void CBalloonGroupManageWindow::CopyGroupNumberToClipboard()
{
    if (m_model.m_groupNumber.IsEmpty())
    {
        return;
    }
    if (!::OpenClipboard(m_hWnd))
    {
        return;
    }
    ::EmptyClipboard();

    const int chars = m_model.m_groupNumber.GetLength() + 1;
    HGLOBAL hMem = ::GlobalAlloc(GMEM_MOVEABLE, chars * sizeof(wchar_t));
    if (hMem != NULL)
    {
        wchar_t* p = static_cast<wchar_t*>(::GlobalLock(hMem));
        if (p != NULL)
        {
            wcscpy_s(p, chars, (LPCTSTR)m_model.m_groupNumber);
            ::GlobalUnlock(hMem);
            //SetClipboardData 成功后内存归系统所有，不能再 GlobalFree
            if (::SetClipboardData(CF_UNICODETEXT, hMem) == NULL)
            {
                ::GlobalFree(hMem);
            }
        }
        else
        {
            ::GlobalFree(hMem);
        }
    }
    ::CloseClipboard();
}

// ====================================================================
// 选人对话框的三个入口
// ====================================================================
//
// 三处共用 CCreateGroupDlg 的"选人"模式（见其 Purpose 枚举）：界面都是
// 「搜索 + 可勾选候选 + 已选区 + 底部确认」，差别只在候选来源与确认后发哪条
// 协议。候选来源这一层由本窗口负责准备 —— 那个对话框不认识"群"的概念。

void CBalloonGroupManageWindow::OpenMemberPicker(int purpose)
{
    if (m_groupId <= 0)
    {
        return;
    }
    if (!::IsWindow(m_hHostPanel))
    {
        //选人对话框归主面板持有（它掌握通讯录缓存，也负责对话框的生命周期）。
        //没有宿主或宿主已销毁时这几个入口不可用 —— 调试入口打开的假数据窗口即属此列。
        LOG_INFO("Group manage window has no live host panel, member picker is unavailable.");
        return;
    }

    std::unique_ptr<balloongroupmanage::MemberPickerRequest> req(
        new balloongroupmanage::MemberPickerRequest());
    req->m_purpose = purpose;
    req->m_groupId = m_groupId;

    const int64_t myId = GetMyUserId();

    if (purpose == CCreateGroupDlg::kPurposeInvite)
    {
        //邀请入群：候选来自通讯录（只有主面板有），这里只把"谁已经在群里"
        //告诉它，让它做差集 —— 已在群里的人再邀请一次没有意义，列出来只会
        //让人以为漏选了。
        for (size_t i = 0; i < m_model.m_members.size(); ++i)
        {
            if (m_model.m_members[i].userid > 0)
            {
                req->m_excludeUserIds.push_back(m_model.m_members[i].userid);
            }
        }
    }
    else
    {
        //禁言 / 转让群主：候选就是本群成员，本窗口模型里现成有。群成员未必在
        //通讯录里（可能根本不是好友），故这里现造 ContactItem —— 选人对话框
        //只认这个类型，它不需要知道这些人是从哪来的。
        for (size_t i = 0; i < m_model.m_members.size(); ++i)
        {
            const GroupMember& m = m_model.m_members[i];
            if (m.userid <= 0 || m.userid == myId)
            {
                continue;   //自己不出现在候选里（禁言自己、转让给自己都无意义）
            }
            //禁言场景还要排除群主 —— 服务端不允许禁言群主，列出来点了必然失败
            if (purpose == CCreateGroupDlg::kPurposeMute && m.role == kRoleOwner)
            {
                continue;
            }

            net::ContactItem c;
            c.m_userid = m.userid;
            //显示名直接用模型里算好的（已按"群名片优先"处理过），转成 UTF-8
            c.m_nickname = EncodeUtil::UnicodeToUtf8(std::wstring((LPCTSTR)m.name));
            req->m_candidates.push_back(c);
        }

        if (req->m_candidates.empty())
        {
            ::MessageBox(m_hWnd, _T("没有可选的成员。"), _T("提示"),
                         MB_OK | MB_ICONINFORMATION);
            return;
        }
    }

    //投递后所有权转给主面板，由它处理完 delete
    ::PostMessage(m_hHostPanel, FMG_MSG_OPEN_MEMBER_PICKER, 0, (LPARAM)req.release());
    LOG_INFO("Request to open member picker, purpose=%d, groupid=%lld.",
             purpose, (long long)m_groupId);
}

// ====================================================================
// 群权限页：逐项发送
// ====================================================================

bool CBalloonGroupManageWindow::SendPermChange(UINT ctrlId, int value)
{
    if (m_groupId <= 0)
    {
        return false;
    }

    net::UpdateGroupInfoParams params;
    params.m_groupid = m_groupId;

    switch (ctrlId)
    {
    case kIdComboJoinMethod:
        //下拉项下标与服务端 group_join_mode 同序（0自由 1需审批 2禁止），直接用
        params.m_hasJoinMode = true;
        params.m_joinMode = value;
        break;

    case kIdComboAtAllScope:
        //下拉项下标与服务端 group_at_all_scope 同序（0仅群主和管理员 1所有人）
        params.m_hasAtAllScope = true;
        params.m_atAllScope = value;
        break;

    case kIdSwitchHistory:
        params.m_hasNewSeeHistory = true;
        params.m_newSeeHistory = (value != 0) ? 1 : 0;
        break;

    case kIdSwitchInvite:
        params.m_hasMemberInvite = true;
        params.m_memberInvite = (value != 0) ? 1 : 0;
        break;

    case kIdSwitchWelcome:
        params.m_hasWelcomeOn = true;
        params.m_welcomeOn = (value != 0) ? 1 : 0;

        //开启欢迎语但正文还是空的：服务端那边"开关开着、正文为空"等于不发，
        //用户会以为没生效。这里顺手带一句默认欢迎语过去，之后可在基本信息
        //里改（欢迎语正文的编辑入口留待后续，见方案文档第 12 节）。
        if (params.m_welcomeOn != 0 && m_model.m_welcomeText.IsEmpty())
        {
            params.m_hasWelcomeText = true;
            params.m_welcomeText = EncodeUtil::UnicodeToUtf8(
                std::wstring(_T("欢迎加入本群！")));
        }
        break;

    case kIdSwitchSensitive:
        params.m_hasSensitiveOn = true;
        params.m_sensitiveOn = (value != 0) ? 1 : 0;
        break;

    default:
        return false;   //不是群权限页的控件
    }

    CIUSocket::GetInstance().SendUpdateGroupInfo(params);
    LOG_INFO("Group permission change sent, groupid=%lld, ctrlid=%u, value=%d.",
             (long long)m_groupId, ctrlId, value);
    return true;
}

// ====================================================================
// 管理词库
// ====================================================================

void CBalloonGroupManageWindow::AddSensitiveWordFromInput()
{
    if (m_groupId <= 0)
    {
        return;
    }

    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    balloonwjui::DuiEdit* pEdit = dynamic_cast<balloonwjui::DuiEdit*>(
        content->FindCtrlById(kIdEditSensitiveNew));
    if (pEdit == NULL)
    {
        return;
    }

    CString word = pEdit->GetText();
    word.Trim();
    if (word.IsEmpty())
    {
        return;   //空词不发，让用户接着输
    }

    //本地已有同一个词就不重复发 —— 服务端也会静默忽略，但提前挡掉能立刻
    //给用户反馈，而不是点了半天没动静。
    for (size_t i = 0; i < m_model.m_sensitiveWords.size(); ++i)
    {
        if (m_model.m_sensitiveWords[i].word.CompareNoCase(word) == 0)
        {
            ::MessageBox(m_hWnd, _T("这个词已经在词库里了。"), _T("提示"),
                         MB_OK | MB_ICONINFORMATION);
            return;
        }
    }

    CIUSocket::GetInstance().SendAddSensitiveWord(
        m_groupId, EncodeUtil::UnicodeToUtf8(std::wstring((LPCTSTR)word)));

    //清空输入框，方便接着加下一个
    pEdit->SetText(_T(""));

    LOG_INFO("Add sensitive word request sent, groupid=%lld.", (long long)m_groupId);
}
