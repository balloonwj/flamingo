/**
 *  BalloonMainPanelThread.cpp —— 主面板一侧的帖子群接入（docs/帖子群方案.md 7.2）。
 *
 *  帖子群宿主 CThreadGroupHost 经 IThreadGroupDirectory 向本面板要主程序的能力，本文件实现这些方法：
 *    · 显示名、头像：与接龙详情窗、文件共享成员行同一口径；
 *    · 群概况：群名、成员数、本人角色取群信息缓存，「此刻不能发言」按仅通知模式与本人角色判断；
 *    · 打开群资料对话框、群管理窗口（没有「群公告」页）与联系人资料窗、把帖子群标为已读；
 *    · 点帖子正文里的 @某人弹出成员菜单（docs/帖子群方案.md §15.1 第 4 条）：与群成员面板的右键菜单相同；
 *    · 群类型查询 GroupTypeOf：打开群会话时（OpenGroupChat / EnsureGroupWindow）据此分流，帖子群不建群聊窗；
 *    · 帖子群窗口右侧的群成员面板（docs/帖子群方案.md 14.3）：按群号持有面板、显示时拉名单、名单到达时交给面板，
 *      并作为面板的所有者提供「列表 / 按部门」的账号配置。
 *
 *  balloonwj@qq.com   2026-10-07
 */
#include "stdafx.h"
#include "BalloonMainPanel.h"

#include "../../Common/Log/IULog.h"
#include "../../Feature/ThreadGroup/ThreadGroupFilter.h"   // 帖子群不可用时过滤群列表与会话列表
#include "../../Feature/ThreadGroup/ThreadCardSync.h"     // ApplyThreadCardToStore / ThreadCardDigestOf：卡片改写后同步到本机
#include "../../Feature/ThreadGroup/ThreadGroupGate.h"
#include "../../Feature/ThreadGroup/ThreadGroupHost.h"
#include "../BalloonChatWnd/GroupNotifyOnly.h"   // CanSpeakInGroup：仅通知模式下本人能不能发言
#include "../../net/MessageProtocolClient.h"   // parseThreadCardContent：从卡片 content 取帖子 ID
#include "ConversationListOps.h"   // FindConversation
#include "GroupFollowModel.h"      // FollowDigestPrefix：「[关注] 」摘要标记
#include "MsgBoxModel.h"           // AtMeDigestPrefix：「[有人@我] 」摘要标记
#include "../../Common/I18n/I18n.h"   // TR / TRF：界面文字的多语言查找
#include "../../net/Protocol.h"                  // conversation_group / group_type / group_member_role
#include "../../net/IUSocket.h"                  // RequestGroupMembers：群成员面板显示时拉名单
#include "../../App/FlamingoClient.h"            // CFlamingoClient::m_UserConfig：群成员面板的「列表 / 按部门」配置
#include "../BalloonChatWnd/GroupMemberItem.h"   // kMemberPresenceUnknown；TrackMemberMenu：弹成员菜单
#include "../BalloonChatWnd/GroupMentionMenu.h"   // BuildThreadMentionMenuModel：点 @某人 弹出的成员菜单
#include "../BalloonChatWnd/GroupMemberPanelKit.h"   // ExecuteGroupMemberAction：执行成员菜单选中的一项
#include "../../../FlamingoThreadGroup/ThreadGroupApi.h"   // kMemberRoleLeft / kMemberRoleUnknown

//插件接口里 targetRole 的两个特殊取值与成员菜单模型里的常量必须相同（两边各写一份，避免纯模型文件依赖插件接口）
static_assert(threadgroup::kMemberRoleLeft == balloonchatwnd::kThreadTargetRoleLeft,
              "kMemberRoleLeft must equal kThreadTargetRoleLeft");
static_assert(threadgroup::kMemberRoleUnknown == balloonchatwnd::kThreadTargetRoleUnknown,
              "kMemberRoleUnknown must equal kThreadTargetRoleUnknown");

std::wstring CBalloonMainPanel::GetThreadUserDisplayName(int64_t userId, int64_t groupId)
{
    return std::wstring((LPCWSTR)ResolveRelayMemberName(groupId, userId));
}

balloonthread::ThreadUserAvatar CBalloonMainPanel::GetThreadUserAvatar(int64_t userId)
{
    //与文件共享成员行同一份算法，只是换成帖子群宿主的结构体
    const balloonfileshare::ShareUserAvatar share = GetShareUserAvatar(userId);
    balloonthread::ThreadUserAvatar avatar;
    avatar.m_glyph = share.m_glyph;
    avatar.m_color = share.m_color;
    avatar.m_imagePath = share.m_imagePath;
    return avatar;
}

bool CBalloonMainPanel::GetThreadGroupBrief(int64_t groupId, balloonthread::ThreadGroupBriefInfo& out)
{
    std::map<int64_t, GroupBrief>::const_iterator it = m_groupInfo.find(groupId);
    if (it == m_groupInfo.end())
        return false;
    out.m_name = std::wstring((LPCWSTR)GroupDisplayName(groupId));
    out.m_memberCount = (it->second.m_membercount > 0) ? static_cast<uint32_t>(it->second.m_membercount) : 0;
    //群信息缓存里有这个群就说明本人是成员；角色还不知道时 MyRoleInGroup 按普通成员回答
    out.m_myRole = MyRoleInGroup(groupId);
    out.m_mutedForMe = !balloonchatwnd::CanSpeakInGroup(it->second.m_muteall, out.m_myRole);
    out.m_atAllScope = it->second.m_atallscope;
    return true;
}

void CBalloonMainPanel::OpenThreadGroupInfo(HWND owner, int64_t groupId)
{
    (void)owner;
    LOG_INFO("thread group asks to open group info, groupid=%lld.", static_cast<long long>(groupId));
    OpenGroupInfo(groupId);
}

void CBalloonMainPanel::OpenThreadGroupManage(HWND owner, int64_t groupId)
{
    (void)owner;
    LOG_INFO("thread group asks to open group manage, groupid=%lld.", static_cast<long long>(groupId));
    //角色门控与「同群已开则前置」都在 OpenGroupManage 里；帖子群的标记也在那里交给窗口
    OpenGroupManage(groupId);
}

void CBalloonMainPanel::ShowThreadMemberMenu(HWND owner, int64_t groupId, int64_t userId, int targetRole,
                                             const POINT& screenPt)
{
    if (groupId <= 0 || userId <= 0 || !::IsWindow(owner))
        return;
    const int64_t selfId = (m_lpFMGClient != NULL)
        ? static_cast<int64_t>(m_lpFMGClient->m_UserMgr.m_UserInfo.m_uUserID) : 0;
    //「特别关注」「关注 TA 在本群的发言」的文案正反：与群成员面板经消息问到的是同一份名单
    const bool isStarred = IsContactStarred(userId);
    const bool isFollowed = m_groupFollows.IsFollowed(groupId, userId);
    const std::vector<balloonchatwnd::MentionMenuEntry> model = balloonchatwnd::BuildThreadMentionMenuModel(
        userId, selfId, MyRoleInGroup(groupId), targetRole, isStarred, isFollowed);
    LOG_INFO("thread mention menu, groupid=%lld, userid=%lld, target role=%d, items=%u.",
             static_cast<long long>(groupId), static_cast<long long>(userId), targetRole,
             static_cast<unsigned>(model.size()));
    const int cmd = balloonchatwnd::GroupMemberItem::TrackMemberMenu(owner, model, screenPt);
    if (cmd == 0)
        return;
    //与群成员面板选中菜单项之后走同一个函数：发消息、资料、特别关注等经主面板执行，管理类直接发协议
    balloonchatwnd::ExecuteGroupMemberAction(cmd, groupId, userId, ResolveRelayMemberName(groupId, userId), owner,
                                             m_hWnd);
}

void CBalloonMainPanel::OpenThreadContactCard(HWND owner, int64_t userId)
{
    (void)owner;
    if (userId <= 0)
        return;
    OpenContactProfile(static_cast<UINT>(userId));
}

void CBalloonMainPanel::MarkThreadGroupRead(int64_t groupId)
{
    MarkConversationRead(conversation_group, groupId);
}

void CBalloonMainPanel::OnThreadWatchChanged(int64_t groupId, bool watching)
{
    LOG_INFO("thread group watch changed, groupid=%lld, watching=%d.", static_cast<long long>(groupId), watching ? 1 : 0);
}

int CBalloonMainPanel::GroupTypeOf(int64_t groupId) const
{
    std::map<int64_t, GroupBrief>::const_iterator it = m_groupInfo.find(groupId);
    if (it == m_groupInfo.end())
        return group_type_normal;
    return balloonthread::IsThreadGroupType(it->second.m_grouptype) ? group_type_thread : group_type_normal;
}

std::set<int64_t> CBalloonMainPanel::HiddenGroupIds() const
{
    const bool available = balloonthread::CThreadGroupHost::GetInstance().IsAvailable();
    //功能可用时什么都不隐藏，不必再收集群类型
    if (available)
        return std::set<int64_t>();
    std::map<int64_t, int> types;
    for (std::map<int64_t, GroupBrief>::const_iterator it = m_groupInfo.begin(); it != m_groupInfo.end(); ++it)
        types[it->first] = it->second.m_grouptype;
    //群信息缓存里还没有的群（刚拉到群列表、资料没到）：用群列表里带的群类型
    for (size_t i = 0; i < m_myGroups.size(); ++i)
    {
        if (types.find(m_myGroups[i].m_groupid) == types.end())
            types[m_myGroups[i].m_groupid] = m_myGroups[i].m_grouptype;
    }
    return balloonthread::HiddenThreadGroupIds(types, available);
}

void CBalloonMainPanel::OnThreadGroupAvailabilityChanged()
{
    const std::set<int64_t> hidden = HiddenGroupIds();
    LOG_INFO("thread group availability changed, hidden groups=%u.", static_cast<unsigned>(hidden.size()));
    FillGroupsTree();
    FillConversationsTree();
    RebuildFavoritesSection();
    FillMsgBoxWindowList();
    RefreshMessagesTabBadge();
    RefreshHeaderMsgBoxBadge();
    ScheduleSearchWarmUp();
    //变为不可用：托盘浮窗与闪烁里不再留帖子群
    for (std::set<int64_t>::const_iterator it = hidden.begin(); it != hidden.end(); ++it)
        PostTrayClear(conversation_group, *it);
}

void CBalloonMainPanel::OpenGroupConversation(int64_t groupId)
{
    const balloonthread::CThreadGroupHost& host = balloonthread::CThreadGroupHost::GetInstance();
    if (balloonthread::IsThreadGroupType(GroupTypeOf(groupId)) && host.IsAvailable())
    {
        //会话的最后一条是帖子卡片或动态卡片：定位到那条帖子（帖子已被删除时窗口会显示「这条帖子已被删除」）
        const net::ConversationItem* conv = balloonmain::FindConversation(m_conversations, conversation_group, groupId);
        const int64_t postId = (conv != NULL) ? conv->m_threadPostId : 0;
        LOG_INFO("open thread group conversation, groupid=%lld, postid=%lld.", (long long)groupId, (long long)postId);
        balloonthread::CThreadGroupHost::GetInstance().OpenThreadWindow(groupId, postId);
        return;
    }
    OpenGroupChat(groupId);
}

void CBalloonMainPanel::RememberThreadCardPost(int64_t groupId, int msgtype, const std::string& cardContent)
{
    if (msgtype != message_type_thread_post && msgtype != message_type_thread_reply)
        return;
    std::string arg;
    int kind = 0;
    int64_t postId = 0;
    bool deleted = false;
    if (!net::MessageProtocolClient::parseThreadCardContent(msgtype, cardContent, arg, kind, postId, deleted) || postId <= 0)
        return;
    for (size_t i = 0; i < m_conversations.size(); ++i)
    {
        if (m_conversations[i].m_ctype == conversation_group && m_conversations[i].m_cid == groupId)
        {
            m_conversations[i].m_threadPostId = postId;
            return;
        }
    }
}

CString CBalloonMainPanel::DecorateThreadCardDigest(const net::ConversationItem& conv, const CString& digest) const
{
    if (conv.m_ctype != conversation_group || conv.m_lastRecalled || conv.m_lastFrom <= 0
        || (conv.m_lastMsgType != message_type_thread_post && conv.m_lastMsgType != message_type_thread_reply))
    {
        return digest;
    }
    //名字与帖子群窗口里同一口径；本人发的写「你」
    const UINT selfId = SelfUserId();
    const CString name = ((UINT)conv.m_lastFrom == selfId) ? CString(TR(L"你"))
                                                           : ResolveRelayMemberName(conv.m_cid, conv.m_lastFrom);
    //收到时加在摘要最前面的「[有人@我] 」「[关注] 」要留在名字之前（docs/帖子群方案.md 14.1 的 P2）：
    //普通群的摘要不带名字，这两个标记就在最前，帖子卡片也保持同样的位置
    std::vector<std::wstring> tags;
    tags.push_back(std::wstring(CA2W(balloonmain::AtMeDigestPrefix().c_str(), CP_UTF8)));
    tags.push_back(std::wstring(CA2W(balloonmain::FollowDigestPrefix().c_str(), CP_UTF8)));
    const std::wstring text(digest.GetString());
    const size_t tagLen = balloonthread::DigestTagLength(text, tags);
    const CString rest(text.substr(tagLen).c_str());
    return CString(text.substr(0, tagLen).c_str()) + CString(TRF(L"{0} {1}", name, rest).c_str());
}

void CBalloonMainPanel::OnThreadCardsChanged(int64_t groupId,
                                              const std::vector<balloonthread::ThreadCardUpdate>& cards)
{
    if (cards.empty() || !EnsureMsgStoreOpen())
        return;
    size_t hits = 0;
    for (size_t i = 0; i < cards.size(); ++i)
    {
        bool hit = false;
        if (!balloonthread::ApplyThreadCardToStore(m_msgStore, groupId, cards[i], hit))
        {
            LOG_WARNING("thread card: local store rewrite failed, group=%lld, msgid=%lld.",
                        static_cast<long long>(groupId), static_cast<long long>(cards[i].m_msgid));
        }
        if (hit)
            ++hits;
    }
    //会话列表的最后一条正是被改写的卡片：摘要换成新内容（如「帖子已删除」），显示时仍在前面加发送者名字
    bool digestChanged = false;
    for (size_t i = 0; i < m_conversations.size(); ++i)
    {
        net::ConversationItem& conv = m_conversations[i];
        if (conv.m_ctype != conversation_group || conv.m_cid != groupId)
            continue;
        for (size_t k = 0; k < cards.size(); ++k)
        {
            if (cards[k].m_msgid != conv.m_lastmsgid)
                continue;
            const std::string digest = balloonthread::ThreadCardDigestOf(cards[k].m_msgtype, cards[k].m_content);
            if (!digest.empty() && digest != conv.m_digest)
            {
                conv.m_digest = digest;
                digestChanged = true;
            }
        }
        break;
    }
    if (digestChanged)
        FillConversationsTree();
    //消息记录窗口（聊天记录管理器）正显示这个帖子群时重查本地库，被改写的卡片立即显示为已删除（14.6 的「立即」；
    //2026-10-08 实机发现打开着的窗口要手动刷新才变）。本地没有命中任何一张时没有可变的行，不刷新
    if (hits > 0)
        m_historyWindow.RefreshIfConversation(conversation_group, groupId);
    LOG_INFO("thread cards applied, group=%lld, cards=%u, local hits=%u, digest changed=%d.",
             static_cast<long long>(groupId), static_cast<unsigned>(cards.size()), static_cast<unsigned>(hits),
             digestChanged ? 1 : 0);
}

bool CBalloonMainPanel::ShowThreadMemberPanel(HWND parent, int64_t groupId, const RECT& rc)
{
    std::unique_ptr<balloonchatwnd::CThreadMemberPanel>& panel = m_threadMemberPanels[groupId];
    if (!panel)
    {
        panel.reset(new balloonchatwnd::CThreadMemberPanel(groupId, this, m_hWnd));
        panel->SetDepartments(m_departments);
    }
    if (!panel->Show(parent, rc))
    {
        LOG_ERROR("thread member panel: show failed, groupid=%lld.", static_cast<long long>(groupId));
        return false;
    }
    //每次显示都重新拉一次名单：面板收起期间成员可能变过（入群、退群、改群名片、改角色），应答经
    //OnGroupMembersArrived → RouteThreadMemberPanelMembers 交给面板
    CIUSocket::GetInstance().RequestGroupMembers(groupId);
    LOG_INFO("thread member panel shown, members requested, groupid=%lld.", static_cast<long long>(groupId));
    return true;
}

void CBalloonMainPanel::MoveThreadMemberPanel(int64_t groupId, const RECT& rc)
{
    std::map<int64_t, std::unique_ptr<balloonchatwnd::CThreadMemberPanel> >::iterator it =
        m_threadMemberPanels.find(groupId);
    if (it != m_threadMemberPanels.end() && it->second && it->second->IsShown())
        it->second->Move(rc);
}

void CBalloonMainPanel::HideThreadMemberPanel(int64_t groupId)
{
    std::map<int64_t, std::unique_ptr<balloonchatwnd::CThreadMemberPanel> >::iterator it =
        m_threadMemberPanels.find(groupId);
    if (it != m_threadMemberPanels.end() && it->second)
        it->second->HidePanel();
}

bool CBalloonMainPanel::IsMemberTreeView() const
{
    return (m_lpFMGClient != NULL) && m_lpFMGClient->m_UserConfig.IsGroupMemberTreeView();
}

void CBalloonMainPanel::SetMemberTreeView(bool treeMode)
{
    if (m_lpFMGClient == NULL)
        return;
    m_lpFMGClient->m_UserConfig.SetGroupMemberTreeView(treeMode);
    m_lpFMGClient->SaveUserConfig();
    LOG_INFO("thread member panel: member view set, tree=%d.", treeMode ? 1 : 0);
}

void CBalloonMainPanel::OnMemberPanelClosed(int64_t groupId)
{
    balloonthread::CThreadGroupHost::GetInstance().OnMemberPanelClosed(groupId);
}

void CBalloonMainPanel::RouteThreadMemberPanelMembers(const net::GroupMembersResult& members)
{
    if (members.m_code != 0)
        return;
    std::map<int64_t, std::unique_ptr<balloonchatwnd::CThreadMemberPanel> >::iterator it =
        m_threadMemberPanels.find(members.m_groupid);
    if (it == m_threadMemberPanels.end() || !it->second)
        return;
    balloonchatwnd::CThreadMemberPanel* panel = it->second.get();
    //先记下各成员的在线状态，再换名单：重建成员行时按记下的状态画徽标。本人取状态胶囊上的当前状态，其他人取
    //通讯录缓存里的 presence；不在通讯录里的成员查不到，传「未知」，不画徽标（与群聊窗成员页签相同）
    const UINT selfId = SelfUserId();
    for (size_t i = 0; i < members.m_members.size(); ++i)
    {
        const int64_t uid = members.m_members[i].m_userid;
        int presence = balloonchatwnd::kMemberPresenceUnknown;
        if (selfId != 0 && uid == static_cast<int64_t>(selfId))
        {
            presence = GetPresence();
        }
        else
        {
            const net::ContactItem* pc = FindCachedContact(static_cast<UINT>(uid));
            if (pc != NULL)
                presence = pc->m_presence;
        }
        panel->SetMemberPresence(uid, presence);
    }
    panel->SetMembers(members.m_members, static_cast<int64_t>(selfId), MyRoleInGroup(members.m_groupid));
}

bool CBalloonMainPanel::IsConversationHidden(int ctype, int64_t cid) const
{
    if (ctype != conversation_group)
        return false;
    return balloonthread::IsHiddenConversation(ctype, cid, HiddenGroupIds());
}

std::vector<net::UserGroupItem> CBalloonMainPanel::VisibleGroups() const
{
    return balloonthread::FilterGroups(m_myGroups, HiddenGroupIds());
}

const std::vector<net::ConversationItem>& CBalloonMainPanel::VisibleConversations(
    std::vector<net::ConversationItem>& storage) const
{
    const std::set<int64_t> hidden = HiddenGroupIds();
    if (hidden.empty())
        return m_conversations;
    storage = balloonthread::FilterConversations(m_conversations, hidden);
    return storage;
}
