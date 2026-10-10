/**
 *  主面板的网络应答与推送处理器。
 *
 *  这里的函数都是 CBalloonMainPanel 的成员，与 BalloonMainPanel.cpp 里的实现同属
 *  一个类，只是分开放在两个文件里 —— 主面板的界面代码本身已经很长，把「收到服务端
 *  应答 / 推送之后做什么」这一类单独成文，两边都好读。
 *
 *  网络线程把应答
 *  投给它，它再逐条转调主面板。主面板成为真正的主窗口之后，这一层中转没有存在的必要，
 *  于是把处理器逐批搬了过来。搬迁期间 BalloonMainPanel.h 的消息映射末尾有一条转发器，
 *  全部由本面板处理。
 *
 *  函数体的写法保持原样：把 lParam 还原成堆上的 net:: 结构体、判空、调用主面板对应的
 *  业务方法、最后 delete。消息与负载类型的对应关系写在 BalloonMainPanel.h 的消息映射上。
 *
 *  balloonwj@qq.com   2026-08-18
 */

#include "stdafx.h"
#include "BalloonMainPanel.h"
#include "../../Feature/FileShare/FileShareHost.h"   // 群成员列表失败时也转给文件共享宿主
#include "../../Feature/Caption/CaptionHost.h"       // 1013：语音字幕开关交给字幕宿主
#include "../../Feature/SecretChat/SecretChatHost.h" // 1013：单聊密聊开关交给密聊宿主
#include "../../Feature/ThreadGroup/ThreadGroupHost.h" // 1013：帖子群开关交给帖子群宿主
#include <memory>                                       // std::unique_ptr（发起通话时托管对端显示名）

#include "../../Common/Log/IULog.h"                    // LOG_INFO / LOG_ERROR
#include "../../Common/Util/EncodeUtil.h"              // UTF-8 转 Unicode（服务端文本为 UTF-8）
#include "../../Session/UserSessionData.h"             // FMG_MSG_* 消息号
#include "../../net/MediaSignalPolicy.h"              // net::MediaSignalPacket（音视频类信令转交载体）
#include "../../../FlamingoMedia/Api/FlamingoMediaApi.h"   // flamingomedia::IMediaModule::OnSignal
#include "../../net/ContactsProtocolClient.h"          // net::ContactsResult
#include "../../net/ConversationsProtocolClient.h"     // net::ConversationsResult / ConversationChangedNotify
#include "../../net/GroupProtocolClient.h"             // net::CreateGroupResponse / GroupInfoResult / GroupMembersResult 等
#include "../../net/GroupFileProtocolClient.h"         // net::GetGroupFilesResult / GroupFilesChangedNotify
#include "../../net/GlobalSearchProtocolClient.h"      // net::GlobalSearchResult（全局搜索应答）
#include "../../net/FileTransfer.h"                    // net::GroupFileUploadResult / GroupFileDownloadResult
#include "../../net/ImageTransfer.h"                   // net::AvatarDownloadResult
#include "../../net/FriendProtocolClient.h"            // net::SetFriendRemarkResult
#include "../../net/ContactTagProtocolClient.h"        // net::SetContactTagsResult
#include "../../net/AccountProtocolClient.h"           // net::ChangePasswordResponse
#include "../../net/MessageProtocolClient.h"           // net::MessageNotify / SendMessageResponse / RecallMessageResponse / TypingNotify 等
#include "../../net/CardProtocolClient.h"              // net::RelayChangedNotify / RelayOpResponse
#include "../../net/LoginProtocolClient.h"             // net::FeaturesNotify / isFeatureOn（功能开关推送）
#include "../../net/BotProtocolClient.h"               // net::GroupBotsResult / AutoReplyResult（机器人应答）
#include "AutoReplySync.h"                             // 自动回复设置与服务端之间的换算（docs/机器人方案.md §11 Q2）
#include "../../Feature/Bot/BotUiBridge.h"              // 群机器人变化时通知机器人插件
#include "../BalloonChatWnd/ChatRelayCard.h"           // balloonchatwnd::RelayOpErrorText
#include "RelayNotifyApply.h"                          // balloonmain::ApplyRelayNotifyToStore
#include "ScreenShareInfoApply.h"                      // balloonmain::ParseScreenShareInfoUpdate / ApplyScreenShareInfoToStore
#include "GroupInviteStateApply.h"                     // balloonmain::GroupInvitePeerId / ApplyGroupInviteStateToStore
#include "../../App/FlamingoClient.h"                  // CFlamingoClient::m_UserMgr（1013 推送更新「邀请入群需被邀请人同意」开关）
#include "../BalloonChatWnd/ChatGroupInviteCard.h"     // balloonchatwnd::GroupInviteStateForAcceptError / GroupInviteAcceptErrorText
#include "../../Feature/ScreenShare/ScreenShareHub.h"  // 1734：按新标题刷新屏幕共享状态缓存
#include "NotifyModePolicy.h"                          // balloonmain::ShouldClearTrayOnModeChange（提醒模式推送到达时停掉托盘闪烁）
#include "../../net/IUSocket.h"                        // CIUSocket::RequestConversations（提醒模式设置失败时重拉会话列表）
#include "../../net/AnnounceProtocolClient.h"          // net::AnnouncementsResult / AnnouncementNotify / AnnounceMarkReadResult
#include "../../net/FolderProtocolClient.h"            // net::FolderInviteResponse / FolderStateNotify / FolderRelayInfo 等
#include "../../net/Protocol.h"                        // error_code_* —— 撤回 / Pin 失败时按错误码给具体原因
#include "../../Feature/FolderTransfer/FolderTransferManager.h" // foldertransfer::FolderTransferManager（文件夹传输会话生命周期）
#include "../../net/ProfileProtocolClient.h"           // net::UserInfoChangedNotify / OrgStructureChangedNotify
#include "../../Common/I18n/I18n.h"   // TR：界面文字的多语言查找
#include "GroupLinkFlow.h"                             // 加群链接：下一步判定、确认文字与失败原因
#include "../BalloonGroupManage/BalloonGroupManageWindow.h"   // CBalloonGroupManageWindow::OnGroupLinksArrived


//======================== 联系人、会话与个人资料 ========================

// 群列表需要重新加载：重新填充"群"页。wParam / lParam 均不使用。
LRESULT CBalloonMainPanel::OnUpdateGroupList(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/)
{
    RefreshGroups();

    return 0;
}

// 会话列表需要重新加载：重新填充"消息"页。wParam / lParam 均不使用。
LRESULT CBalloonMainPanel::OnUpdateRecentList(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/)
{
    RefreshMessages();

    return 0;
}

// v2 通讯录应答：填充"联系人"页
LRESULT CBalloonMainPanel::OnV2Contacts(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //lParam 为 IUSocket 接收线程堆分配的 net::ContactsResult，处理后须 delete。
    net::ContactsResult* pResult = (net::ContactsResult*)lParam;
    if (pResult == NULL)
        return 0;
    if (pResult->m_code == 0)
        OnContactsArrived(*pResult);
    else
        LOG_ERROR("v2 contacts response error, code=%d, msg=%s.",
                  pResult->m_code, pResult->m_msg.c_str());
    delete pResult;
    return 0;
}

// v2 最近会话列表应答：填充"消息"页
LRESULT CBalloonMainPanel::OnV2Conversations(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //lParam 为 IUSocket 接收线程堆分配的 net::ConversationsResult，处理后须 delete。
    net::ConversationsResult* pResult = (net::ConversationsResult*)lParam;
    if (pResult == NULL)
        return 0;
    if (pResult->m_code == 0)
        OnConversationsArrived(*pResult);
    else
        LOG_ERROR("v2 conversations response error, code=%d, msg=%s.",
                  pResult->m_code, pResult->m_msg.c_str());
    delete pResult;
    return 0;
}

// v2 头像图片下载结果：按 fileid 刷新对应联系人 / 会话节点头像
LRESULT CBalloonMainPanel::OnV2AvatarDownloadResult(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //lParam 为 ImageTransfer 工作线程堆分配的 net::AvatarDownloadResult，处理后须 delete。
    net::AvatarDownloadResult* pResult = (net::AvatarDownloadResult*)lParam;
    if (pResult == NULL)
        return 0;
    OnAvatarDownloadResult(*pResult);
    delete pResult;
    return 0;
}

// v2 联系人资料变更推送：更新通讯录缓存里该 userid 的头像并实时重画
LRESULT CBalloonMainPanel::OnV2UserInfoChanged(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //lParam 为 IUSocket 接收线程堆分配的 net::UserInfoChangedNotify，处理后须 delete。
    net::UserInfoChangedNotify* pNotify = (net::UserInfoChangedNotify*)lParam;
    if (pNotify == NULL)
        return 0;
    OnUserInfoChanged(*pNotify);
    delete pNotify;
    return 0;
}

// v2 组织架构变更推送：重新拉一次通讯录。企业里新入职 / 新注册的同事就是
// 靠这条推送在已登录的客户端上"冒"出来的 —— 通讯录本身只在登录时拉过一次。
LRESULT CBalloonMainPanel::OnV2OrgStructureChanged(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //lParam 为 IUSocket 接收线程堆分配的 net::OrgStructureChangedNotify，处理后须 delete。
    net::OrgStructureChangedNotify* pNotify = (net::OrgStructureChangedNotify*)lParam;
    if (pNotify == NULL)
        return 0;
    OnOrgStructureChanged(*pNotify);
    delete pNotify;
    return 0;
}

// v2 会话属性多端同步推送：按新状态刷新会话列表（置顶 / 免打扰 / 删除）
LRESULT CBalloonMainPanel::OnV2ConversationChanged(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //lParam 为 IUSocket 接收线程堆分配的 net::ConversationChangedNotify，处理后须 delete。
    net::ConversationChangedNotify* pNotify = (net::ConversationChangedNotify*)lParam;
    if (pNotify == NULL)
        return 0;
    OnConversationChanged(*pNotify);
    delete pNotify;
    return 0;
}

LRESULT CBalloonMainPanel::OnV2SetNotifyModesResult(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //lParam 为 IUSocket 接收线程堆分配的 net::SetNotifyModesResult，处理后须 delete。
    //成功的条目以 FMG_MSG_V2_NOTIFY_MODES_CHANGED 推送为准，这里只关心失败的条目。
    net::SetNotifyModesResult* pResult = (net::SetNotifyModesResult*)lParam;
    if (pResult == NULL)
        return 0;

    int failed = 0;
    int starredLimited = 0;
    for (size_t i = 0; i < pResult->m_results.size(); ++i)
    {
        const net::NotifyModeResult& r = pResult->m_results[i];
        if (r.m_code != 0)
        {
            ++failed;
            if (r.m_code == error_code_starred_limit)
                ++starredLimited;
            LOG_WARNING("set notify mode item failed: ctype=%d, cid=%lld, code=%d.",
                        r.m_ctype, (long long)r.m_cid, r.m_code);
        }
    }
    LOG_INFO("set notify modes result: code=%d, items=%u, failed=%d, starredLimited=%d.",
             pResult->m_code, (unsigned)pResult->m_results.size(), failed, starredLimited);
    const bool wholeFailed = (pResult->m_code != 0);
    //批量管理窗口开着时，失败的行标红并保留勾选，成功的取消勾选
    m_notifyManageWindow.OnApplyResult(*pResult);
    delete pResult;

    if (!wholeFailed && failed == 0)
        return 0;

    //有条目没设成：本端可能已按请求做过乐观更新，重拉一次会话列表，用应答里的 notifymodes
    //覆盖模式表，界面以服务端为准
    CIUSocket::GetInstance().RequestConversations();

    CString tip;
    if (starredLimited > 0)
        tip.Format(TR(L"有 %d 个会话没能设为特别关注：特别关注最多 %d 个。"), starredLimited,
                   balloonmain::kNotifyModeStarredMax);
    else
        tip = TR(L"部分会话的消息提醒设置没有成功，请稍后重试。");
    ::MessageBox(m_hWnd, tip, g_strAppTitle.c_str(), MB_OK | MB_ICONWARNING);
    return 0;
}

LRESULT CBalloonMainPanel::OnV2GroupFollowChanged(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //lParam 为 IUSocket 接收线程堆分配的 net::GroupFollowEntry，处理后须 delete。
    //推送带的是整份名单，直接覆盖；名单只影响之后到达的消息与成员菜单的文案，不必刷新列表
    net::GroupFollowEntry* pNotify = (net::GroupFollowEntry*)lParam;
    if (pNotify == NULL)
        return 0;
    m_groupFollows.ApplyGroup(*pNotify);
    LOG_INFO("group follows changed: group=%lld, count=%u.",
             (long long)pNotify->m_groupid, (unsigned)pNotify->m_targets.size());
    delete pNotify;
    return 0;
}

LRESULT CBalloonMainPanel::OnV2SetGroupFollowResult(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/)
{
    //只有失败的应答才投到这里；名单没变，本地缓存也不用动
    const int code = (int)wParam;
    LOG_WARNING("set group follow failed: code=%d.", code);
    const wchar_t* tip = (code == error_code_not_group_member)
                             ? TR(L"你已不在这个群里，无法设置关注的成员。")
                             : TR(L"设置关注的成员没有成功，请稍后重试。");
    ::MessageBox(m_hWnd, tip, g_strAppTitle.c_str(), MB_OK | MB_ICONWARNING);
    return 0;
}

LRESULT CBalloonMainPanel::OnV2NotifyModesChanged(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //lParam 为 IUSocket 接收线程堆分配的 net::NotifyModesChangedNotify，处理后须 delete。
    net::NotifyModesChangedNotify* pNotify = (net::NotifyModesChangedNotify*)lParam;
    if (pNotify == NULL)
        return 0;

    //逐条覆盖模式表；改成三种「不提醒」的模式的会话，若正在托盘闪烁一并停掉。其它设备解除了
    //屏蔽的会话：服务端已推进已读位点，本地未读清零，最后重拉一次会话列表取最新摘要
    bool changed = false;
    bool anyLeftBlock = false;
    for (size_t i = 0; i < pNotify->m_items.size(); ++i)
    {
        const net::NotifyModeEntry& e = pNotify->m_items[i];
        if (balloonmain::IsLeavingBlock(m_notifyModes.GetMode(e.m_ctype, e.m_cid), e.m_mode))
        {
            ClearUnreadAfterUnblock(e.m_ctype, e.m_cid);
            anyLeftBlock = true;
            changed = true;
        }
        if (m_notifyModes.SetMode(e.m_ctype, e.m_cid, e.m_mode))
        {
            changed = true;
            if (balloonmain::ShouldClearTrayOnModeChange(e.m_mode))
                PostTrayClear(e.m_ctype, e.m_cid);
        }
    }
    LOG_INFO("notify modes changed notify: items=%u, changed=%d.",
             (unsigned)pNotify->m_items.size(), changed ? 1 : 0);
    delete pNotify;

    if (changed)
    {
        FillConversationsTree();
        RefreshNotifyModeMarks();
    }
    if (anyLeftBlock)
    {
        CIUSocket::GetInstance().RequestConversations();
    }
    return 0;
}

LRESULT CBalloonMainPanel::OnV2SetFavoriteResult(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    net::SetFavoriteResult* pResult = (net::SetFavoriteResult*)lParam;
    if (pResult == NULL)
    {
        return 0;
    }

    LOG_INFO("MainPanel::OnV2SetFavoriteResult: code=%d, ctype=%d, cid=%lld, favorite=%d.",
        pResult->m_code, pResult->m_ctype, (long long)pResult->m_cid, pResult->m_favorite);

    //成功时不必动界面（乐观更新已经生效），失败要回滚并提示。
    OnSetFavoriteResult(*pResult);

    delete pResult;
    return 0;
}

LRESULT CBalloonMainPanel::OnV2SetRemarkResult(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    net::SetFriendRemarkResult* pResult = (net::SetFriendRemarkResult*)lParam;
    if (pResult == NULL)
    {
        return 0;
    }

    //备注名可能含用户隐私，日志只记结果码与目标，不记内容。
    LOG_INFO("MainPanel::OnV2SetRemarkResult: code=%d, userid=%lld.",
        pResult->m_code, (long long)pResult->m_userid);

    //成功时不必动界面（乐观更新已经生效），失败要回滚并提示。
    OnSetFriendRemarkResult(*pResult);

    delete pResult;
    return 0;
}

LRESULT CBalloonMainPanel::OnV2SetTagsResult(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    net::SetContactTagsResult* pResult = (net::SetContactTagsResult*)lParam;
    if (pResult == NULL)
    {
        return 0;
    }

    //标签可能含用户隐私，日志只记结果码、目标与条数，不记内容。
    LOG_INFO("MainPanel::OnV2SetTagsResult: code=%d, userid=%lld, tagcount=%d.",
        pResult->m_code, (long long)pResult->m_userid, (int)pResult->m_tags.size());

    //拿应答回带的列表覆盖本地缓存并刷新资料窗。
    OnSetContactTagsResult(*pResult);

    delete pResult;
    return 0;
}


//======================== 群资料、群成员、群文件与群管理 ========================

// v2 建群应答：提示建群结果（成功关闭对话框并提示；失败提示错误）
LRESULT CBalloonMainPanel::OnV2CreateGroupResult(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //lParam 为 IUSocket 接收线程堆分配的 net::CreateGroupResponse，处理后须 delete。
    net::CreateGroupResponse* pResp = (net::CreateGroupResponse*)lParam;
    if (pResp == NULL)
        return 0;
    OnCreateGroupResult(*pResp);
    delete pResp;
    return 0;
}

// v2 我的群列表应答：填"群"Tab（含无消息空群）
LRESULT CBalloonMainPanel::OnV2UserGroups(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //lParam 为 IUSocket 接收线程堆分配的 net::UserGroupsResult，处理后须 delete。
    net::UserGroupsResult* pResult = (net::UserGroupsResult*)lParam;
    if (pResult == NULL)
        return 0;
    if (pResult->m_code == 0)
        OnUserGroupsArrived(*pResult);
    else
        LOG_ERROR("v2 user-groups response error, code=%d, msg=%s.",
                  pResult->m_code, pResult->m_msg.c_str());
    delete pResult;
    return 0;
}

// v2 群资料应答：写群信息缓存并刷新群名
LRESULT CBalloonMainPanel::OnV2GroupInfo(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //lParam 为 IUSocket 接收线程堆分配的 net::GroupInfoResult，处理后须 delete。
    net::GroupInfoResult* pResult = (net::GroupInfoResult*)lParam;
    if (pResult == NULL)
        return 0;
    if (pResult->m_code == 0)
        OnGroupInfoArrived(*pResult);
    else
        LOG_ERROR("v2 group-info response error, code=%d, msg=%s.",
                  pResult->m_code, pResult->m_msg.c_str());
    delete pResult;
    return 0;
}

// v2 群成员应答：路由到对应群聊窗填成员侧栏
LRESULT CBalloonMainPanel::OnV2GroupMembers(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //lParam 为 IUSocket 接收线程堆分配的 net::GroupMembersResult，处理后须 delete。
    net::GroupMembersResult* pResult = (net::GroupMembersResult*)lParam;
    if (pResult == NULL)
        return 0;
    if (pResult->m_code == 0)
    {
        OnGroupMembersArrived(*pResult);
    }
    else
    {
        LOG_ERROR("v2 group-members response error, code=%d, msg=%s.",
                  pResult->m_code, pResult->m_msg.c_str());
        // 失败也要告诉文件共享宿主，发起共享对话框才不会一直停在「正在获取成员」
        balloonfileshare::CFileShareHost::GetInstance().OnGroupMembersArrived(*pResult);
        // 帖子群宿主同理：「@ 成员」的列表据此结束等待
        balloonthread::CThreadGroupHost::GetInstance().OnGroupMembersArrived(*pResult);
    }
    delete pResult;
    return 0;
}

// v2 群写操作通用应答（置顶/免打扰/改群名片/退群）：wParam=code，lParam=堆上 std::string* msg。
// 改群名片(SET_MEMBER_CARD)交群资料对话框处理（成功无动作；失败从快照回退我的群名片 +
// 弹提示，这才是用户要的"修改失败再还原"）。其余两个（置顶/免打扰、退群）乐观更新已在
// 发起端生效，这里只记日志、失败给通用提示（应答不带 id，无法精确回滚，下次同步会纠正）。
LRESULT CBalloonMainPanel::OnV2GroupWriteResult(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    int code = (int)wParam;
    std::string* pMsg = (std::string*)lParam;
    std::string msg = (pMsg != NULL) ? *pMsg : std::string();
    delete pMsg;

    // 改群名片：转交群资料对话框做"成功保持 / 失败回退"。
    if (uMsg == FMG_MSG_V2_SET_MEMBER_CARD_RESULT)
    {
        CString wmsg;
        if (!msg.empty())
        {
            int n = ::MultiByteToWideChar(CP_UTF8, 0, msg.c_str(), -1, NULL, 0);
            if (n > 0)
            {
                ::MultiByteToWideChar(CP_UTF8, 0, msg.c_str(), -1, wmsg.GetBuffer(n), n);
                wmsg.ReleaseBuffer();
            }
        }
        OnGroupCardSaveResult(code, wmsg);
        return 0;
    }

    // 退群：对上本面板登记的那次请求，成功时提示并清理，失败时按错误码提示。对不上的
    // （理论上不会出现）落到下面的通用处理
    if (uMsg == FMG_MSG_V2_LEAVE_GROUP_RESULT)
    {
        const balloonmain::GroupOpOutcome outcome =
            m_groupExitTracker.OnResult(balloonmain::kGroupExitOpLeave, code);
        if (outcome.m_matched)
        {
            ApplyGroupExitOutcome(balloonmain::kGroupExitOpLeave, outcome);
            return 0;
        }
    }

    if (code == 0)
    {
        LOG_INFO("v2 group write-op ok, uiMsg=%u.", uMsg);
        return 0;
    }

    LOG_ERROR("v2 group write-op failed, uiMsg=%u, code=%d, msg=%s.", uMsg, code, msg.c_str());

    //会话设置（置顶 / 消息提醒模式 / 删除会话）失败：应答不带是哪个会话，无法精确回滚本端的
    //乐观更新，重拉一次会话列表，用服务端的状态（含 notifymodes）覆盖本地
    if (uMsg == FMG_MSG_V2_SET_CONVERSATION_RESULT)
    {
        CIUSocket::GetInstance().RequestConversations();
        if (code == error_code_starred_limit)
        {
            CString tip;
            tip.Format(TR(L"特别关注最多 %d 个，请先取消一些再设置。"), balloonmain::kNotifyModeStarredMax);
            ::MessageBox(m_hWnd, tip, g_strAppTitle.c_str(), MB_OK | MB_ICONINFORMATION);
            return 0;
        }
    }
    ::MessageBox(m_hWnd, TR(L"操作未能完成，请稍后重试。"), g_strAppTitle.c_str(),
                 MB_OK | MB_ICONWARNING);
    return 0;
}

LRESULT CBalloonMainPanel::OnV2SearchGroupFiles(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //lParam 为 IUSocket 接收线程堆分配的 net::SearchGroupFilesResult，处理后须 delete。
    //转交给搜索下拉，由它按回显的关键字决定采纳还是丢弃（用户可能已经接着输入了）。
    net::SearchGroupFilesResult* pResult = (net::SearchGroupFilesResult*)lParam;
    if (pResult == NULL)
        return 0;
    LOG_INFO("Main panel got group-file search result, code=%d, count=%u, hasmore=%d.",
             pResult->m_code, (uint32_t)pResult->m_files.size(), pResult->m_hasmore ? 1 : 0);
    m_searchDropdown.OnGroupFileSearchResult(*pResult);
    delete pResult;
    return 0;
}

LRESULT CBalloonMainPanel::OnV2GlobalSearch(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //lParam 为 IUSocket 接收线程堆分配的 net::GlobalSearchResult，处理后须 delete。
    //转交给搜索下拉，由它按回显的关键字决定采纳还是丢弃（用户可能已经接着输入了）。
    net::GlobalSearchResult* pResult = (net::GlobalSearchResult*)lParam;
    if (pResult == NULL)
        return 0;
    LOG_INFO("Main panel got global search result, code=%d, conversations=%u, announcements=%u.",
             pResult->m_code, (uint32_t)pResult->m_conversations.size(), (uint32_t)pResult->m_announcements.size());
    m_searchDropdown.OnServerSearchResult(*pResult);
    delete pResult;
    return 0;
}

LRESULT CBalloonMainPanel::OnV2GroupFiles(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //lParam 为 IUSocket 接收线程堆分配的 net::GetGroupFilesResult，处理后须 delete。
    //无论成功失败都按 groupid 路由到群聊窗（失败时群聊窗据 code 收尾本次拉取）。
    net::GetGroupFilesResult* pResult = (net::GetGroupFilesResult*)lParam;
    if (pResult == NULL)
        return 0;
    OnGroupFilesArrived(*pResult);
    delete pResult;
    return 0;
}

LRESULT CBalloonMainPanel::OnV2GroupFilesChanged(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //lParam 为 IUSocket 接收线程堆分配的 net::GroupFilesChangedNotify，处理后须 delete。
    net::GroupFilesChangedNotify* pNotify = (net::GroupFilesChangedNotify*)lParam;
    if (pNotify == NULL)
        return 0;
    OnGroupFilesChanged(*pNotify);
    delete pNotify;
    return 0;
}

LRESULT CBalloonMainPanel::OnV2GroupFileUploadResult(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //lParam 为 FileTransfer 工作线程堆分配的 net::GroupFileUploadResult，处理后须 delete。
    net::GroupFileUploadResult* pResult = (net::GroupFileUploadResult*)lParam;
    if (pResult == NULL)
        return 0;
    OnGroupFileUploadResult(*pResult);
    delete pResult;
    return 0;
}

LRESULT CBalloonMainPanel::OnV2GroupFileDownloadResult(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //lParam 为 FileTransfer 工作线程堆分配的 net::GroupFileDownloadResult，处理后须 delete。
    net::GroupFileDownloadResult* pResult = (net::GroupFileDownloadResult*)lParam;
    if (pResult == NULL)
        return 0;
    OnGroupFileDownloadResult(*pResult);
    delete pResult;
    return 0;
}

LRESULT CBalloonMainPanel::OnV2GroupFileUploadProgress(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //lParam 为 FileTransfer 工作线程堆分配的 net::GroupFileUploadProgress，处理后须 delete。
    net::GroupFileUploadProgress* pProg = (net::GroupFileUploadProgress*)lParam;
    if (pProg == NULL)
        return 0;
    OnGroupFileUploadProgress(*pProg);
    delete pProg;
    return 0;
}

LRESULT CBalloonMainPanel::OnV2GroupFileDownloadProgress(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //lParam 为 FileTransfer 工作线程堆分配的 net::GroupFileDownloadProgress，处理后须 delete。
    net::GroupFileDownloadProgress* pProg = (net::GroupFileDownloadProgress*)lParam;
    if (pProg == NULL)
        return 0;
    OnGroupFileDownloadProgress(*pProg);
    delete pProg;
    return 0;
}

// v2 群管理写操作应答：按 wParam 里的协议号决定失败提示文案
LRESULT CBalloonMainPanel::OnV2GroupOpResult(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam)
{
    //lParam 为 IUSocket 接收线程堆分配的 net::GroupOpResult，处理后须 delete。
    net::GroupOpResult* pResult = (net::GroupOpResult*)lParam;
    if (pResult == NULL)
        return 0;
    OnGroupOpResult((int)wParam, *pResult);
    delete pResult;
    return 0;
}

// v2 待审批加群申请列表应答：填进群管理窗口的审批页
LRESULT CBalloonMainPanel::OnV2GroupJoinRequests(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    net::GroupJoinRequestsResult* pResult = (net::GroupJoinRequestsResult*)lParam;
    if (pResult == NULL)
        return 0;
    OnGroupJoinRequests(*pResult);
    delete pResult;
    return 0;
}

// v2 群公告列表应答：分给群管理窗的公告页与群聊窗的公告边栏
LRESULT CBalloonMainPanel::OnV2GroupAnnouncements(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    net::GroupAnnouncementsResult* pResult = (net::GroupAnnouncementsResult*)lParam;
    if (pResult == NULL)
        return 0;
    OnGroupAnnouncements(*pResult);
    delete pResult;
    return 0;
}

// v2 群公告变更推送：刷新公告界面；新发布时还要弹一次提示
LRESULT CBalloonMainPanel::OnV2AnnouncementChanged(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    net::GroupAnnouncementNotify* pNotify = (net::GroupAnnouncementNotify*)lParam;
    if (pNotify == NULL)
        return 0;
    OnAnnouncementChanged(*pNotify);
    delete pNotify;
    return 0;
}

// v2 群成员变动推送：刷新群管理窗口、群聊窗成员边栏与会话列表
LRESULT CBalloonMainPanel::OnV2GroupMemberChanged(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    net::GroupMemberChangedNotify* pNotify = (net::GroupMemberChangedNotify*)lParam;
    if (pNotify == NULL)
        return 0;
    OnGroupMemberChanged(*pNotify);
    delete pNotify;
    return 0;
}

// v2 群资料变更推送：刷新群名与各页；群已解散时关掉相关窗口
LRESULT CBalloonMainPanel::OnV2GroupInfoChanged(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    net::GroupInfoChangedNotify* pNotify = (net::GroupInfoChangedNotify*)lParam;
    if (pNotify == NULL)
        return 0;
    OnGroupInfoChanged(*pNotify);
    delete pNotify;
    return 0;
}

// v2 有人申请加群推送：让审批入口出现待处理红点
LRESULT CBalloonMainPanel::OnV2JoinGroupRequest(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    net::GroupJoinRequestItem* pNotify = (net::GroupJoinRequestItem*)lParam;
    if (pNotify == NULL)
        return 0;
    OnJoinGroupRequestNotify(*pNotify);
    delete pNotify;
    return 0;
}


//======================== 单聊消息、历史消息、撤回与 Pin ========================

LRESULT CBalloonMainPanel::OnV2RecvChatMsg(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //lParam 为堆分配的 net::MessageNotify，路由到对应单聊窗后须 delete。
    net::MessageNotify* pNotify = (net::MessageNotify*)lParam;
    if (pNotify == NULL)
        return 0;
    OnRecvChatMessage(*pNotify);
    delete pNotify;
    return 0;
}

LRESULT CBalloonMainPanel::OnV2SendChatAck(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //lParam 为堆分配的 net::SendMessageResponse，处理后须 delete。
    net::SendMessageResponse* pResp = (net::SendMessageResponse*)lParam;
    if (pResp == NULL)
        return 0;
    OnSendChatAck(*pResp);
    delete pResp;
    return 0;
}

// v2 历史消息应答：按 (ctype,cid) 路由到对应聊天窗渲染历史
LRESULT CBalloonMainPanel::OnV2SyncMessages(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //lParam 为 IUSocket 接收线程堆分配的 net::SyncMessagesResult，处理后须 delete。
    net::SyncMessagesResult* pResult = (net::SyncMessagesResult*)lParam;
    if (pResult == NULL)
        return 0;
    if (pResult->m_code == 0)
        OnSyncMessagesArrived(*pResult);
    else
        LOG_ERROR("v2 sync-messages response error, code=%d.", pResult->m_code);
    delete pResult;
    return 0;
}

LRESULT CBalloonMainPanel::OnV2TypingNotify(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //lParam 为堆分配的 net::TypingNotify，路由后须 delete。
    net::TypingNotify* pNotify = (net::TypingNotify*)lParam;
    if (pNotify == NULL)
        return 0;
    //与文件收取状态同理：这一跳留一条日志，出问题时能判断消息断在哪一层。
    LOG_INFO("路由对端正在输入状态: cid=%lld, state=%d.",
             (long long)pNotify->m_cid, pNotify->m_state);
    OnTypingNotify(*pNotify);
    delete pNotify;
    return 0;
}

// v2 撤回消息应答：只处理失败分支。撤回成功时界面改动统一等服务端随后推来的
// cmd_message_recalled_notify 再做（见 OnV2MessageRecalled），这样本端、对端与多端
// 走的是同一条路径，结果必然一致，也避免应答与推送各改一次造成重复处理。
LRESULT CBalloonMainPanel::OnV2RecallResult(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //lParam 为 IUSocket 接收线程堆分配的 net::RecallMessageResponse，处理后须 delete。
    net::RecallMessageResponse* pResp = (net::RecallMessageResponse*)lParam;
    if (pResp == NULL)
        return 0;

    if (pResp->m_code != 0)
    {
        //按服务端返回的 error_code 给出具体原因，别让用户面对一句笼统的"撤回失败"。
        LPCTSTR pszTip = TR(L"撤回失败，请稍后重试。");
        if (pResp->m_code == error_code_recall_timeout)
            pszTip = TR(L"该消息发出已超过 2 分钟，无法撤回。");
        else if (pResp->m_code == error_code_recall_not_allowed)
            pszTip = TR(L"你没有权限撤回这条消息。");
        else if (pResp->m_code == error_code_message_not_exist)
            pszTip = TR(L"该消息不存在或已被撤回。");

        LOG_ERROR("v2 recall message failed, code=%d, msg=%s.", pResp->m_code, pResp->m_msg.c_str());

        ::MessageBox(m_hWnd, pszTip, g_strAppTitle.c_str(), MB_OK | MB_ICONINFORMATION);
    }

    delete pResp;
    return 0;
}

// v2 某条消息被撤回的推送：统一处理（把对应气泡改成提示行、刷新会话列表
// 预览、把本地消息库该条标记为已撤回）。撤回者本人也会收到本推送。
LRESULT CBalloonMainPanel::OnV2MessageRecalled(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //lParam 为 IUSocket 接收线程堆分配的 net::MessageRecalledNotify，处理后须 delete。
    net::MessageRecalledNotify* pNotify = (net::MessageRecalledNotify*)lParam;
    if (pNotify == NULL)
        return 0;

    OnMessageRecalled(*pNotify);
    delete pNotify;
    return 0;
}

//主连接把白名单内的音视频类推送 / 应答原样转交过来：交给 FlamingoMedia.dll 的模块处理。
//模块未加载或不处理这条信令时记一条警告，便于排查「网络通了、界面不动」。
LRESULT CBalloonMainPanel::OnMediaSignal(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    net::MediaSignalPacket* pPacket = (net::MediaSignalPacket*)lParam;
    if (pPacket == NULL)
        return 0;

    //共享标题与描述变了（第 15 期）：本地库与聊天窗里的卡片、群聊横幅由主程序刷新，之后照常交给 DLL
    if (pPacket->m_cmd == cmd_screenshare_info_notify)
        ApplyScreenShareInfoSignal(pPacket->m_body);

    if (m_pMediaModule == NULL)
    {
        LOG_WARNING("Drop media signal cmd=%u: media module is not loaded.", static_cast<unsigned>(pPacket->m_cmd));
    }
    else if (!m_pMediaModule->OnSignal(pPacket->m_cmd, pPacket->m_body.data(),
                                       static_cast<uint32_t>(pPacket->m_body.size())))
    {
        LOG_WARNING("Media module did not handle signal cmd=%u, len=%u.", static_cast<unsigned>(pPacket->m_cmd),
                    static_cast<unsigned>(pPacket->m_body.size()));
    }
    delete pPacket;
    return 0;
}

void CBalloonMainPanel::ApplyScreenShareInfoSignal(const std::string& body)
{
    balloonmain::ScreenShareInfoUpdate update;
    if (!balloonmain::ParseScreenShareInfoUpdate(body, update))
    {
        LOG_WARNING("screen share info notify ignored: bad body, len=%u.", static_cast<unsigned>(body.size()));
        return;
    }

    //一、改写本地库里那张卡片。本地没有（本机没同步过这段历史）不是错误；推送不带卡片时跳过
    if (update.m_msgid > 0)
    {
        if (!EnsureMsgStoreOpen())
        {
            LOG_ERROR("screen share info notify: local message store not ready, msgid=%lld.", (long long)update.m_msgid);
        }
        else
        {
            bool hit = false;
            if (!balloonmain::ApplyScreenShareInfoToStore(m_msgStore, update, hit))
                LOG_ERROR("screen share info notify: update local card failed, ctype=%d, cid=%lld, msgid=%lld.",
                          update.m_ctype, (long long)update.m_cid, (long long)update.m_msgid);
            else
                LOG_INFO("screen share info notify: local card updated, ctype=%d, cid=%lld, msgid=%lld, hit=%d.",
                         update.m_ctype, (long long)update.m_cid, (long long)update.m_msgid, hit ? 1 : 0);
        }
        //二、消息记录窗正显示本会话时刷新它
        m_historyWindow.RefreshIfConversation(update.m_ctype, update.m_cid);
        //三、已打开的聊天窗里的那张卡片
        if (update.m_ctype == conversation_single)
        {
            std::map<UINT, CBalloonChatWindow*>::iterator it = m_chatWindows.find((UINT)update.m_cid);
            if (it != m_chatWindows.end() && it->second != NULL && it->second->IsWindow())
                it->second->OnScreenShareInfoChanged(update.m_msgid, update.m_content);
        }
        else if (update.m_ctype == conversation_group)
        {
            std::map<int64_t, CBalloonGroupChatWindow*>::iterator it = m_groupChatWindows.find(update.m_cid);
            if (it != m_groupChatWindows.end() && it->second != NULL && it->second->IsWindow())
                it->second->OnScreenShareInfoChanged(update.m_msgid, update.m_content);
        }
    }

    //四、状态缓存里这一场共享的标题，移交主持人后另有新主持人：群聊横幅据此显示。
    //只在缓存记着的正是这一场时更新，其余字段沿用（规则见 ScreenShareStatusCache::ApplyInfo）
    const std::wstring title = EncodeUtil::Utf8ToUnicode(update.m_title);
    const std::wstring hostName = EncodeUtil::Utf8ToUnicode(update.m_hostName);
    if (balloonscreenshare::CScreenShareHub::GetInstance().ApplyInfo(update.m_ctype, update.m_cid, update.m_roomId,
                                                                     title, update.m_hostId, hostName))
    {
        LOG_INFO("screen share info notify: status updated, ctype=%d, cid=%lld, host=%lld.", update.m_ctype,
                 (long long)update.m_cid, (long long)update.m_hostId);
        OnScreenShareStatusChanged(update.m_ctype, update.m_cid);
    }
}

//单聊窗 / 联系人窗 / 联系人右键菜单的「语音通话」：交给 FlamingoMedia.dll 发起呼叫。
//wParam=对端 userid，lParam=堆 std::wstring* 对端显示名（由 StartCallViaMediaModule 释放）。
LRESULT CBalloonMainPanel::OnCallStartInvite(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam)
{
    StartCallViaMediaModule((UINT)wParam, (std::wstring*)lParam, false);
    return 0;
}

//「视频通话」：同上，通话类型为视频。
LRESULT CBalloonMainPanel::OnCallStartVideoInvite(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam)
{
    StartCallViaMediaModule((UINT)wParam, (std::wstring*)lParam, true);
    return 0;
}

void CBalloonMainPanel::StartCallViaMediaModule(UINT peerUserId, std::wstring* pPeerName, bool video)
{
    //对端显示名由发起方 new 出来，无论能否发起都在这里释放
    std::unique_ptr<std::wstring> name(pPeerName);
    if (peerUserId == 0)
        return;

    if (m_pMediaModule == NULL)
    {
        LOG_ERROR("Start call failed: media module is not loaded, userid=%u, video=%d.", peerUserId, video ? 1 : 0);
        ::MessageBox(m_hWnd, TR(L"音视频组件未能加载，暂时无法发起通话。"), g_strAppTitle.c_str(), MB_OK | MB_ICONWARNING);
        return;
    }
    const wchar_t* peerName = (name.get() != NULL) ? name->c_str() : L"";
    const int callType = video ? flamingomedia::media_call_video : flamingomedia::media_call_audio;
    if (!m_pMediaModule->StartCall((int64_t)peerUserId, peerName, callType))
        LOG_ERROR("Start call failed: media module rejected, userid=%u, video=%d.", peerUserId, video ? 1 : 0);
}

//单聊窗「远程桌面」：交给 FlamingoMedia.dll 发起远程桌面（本端为控制端）。
//wParam=对端 userid，lParam=堆 std::wstring* 对端显示名（这里释放）。模块未加载时提示用户。
LRESULT CBalloonMainPanel::OnRdStartInvite(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam)
{
    //对端显示名由发起方 new 出来，无论能否发起都在这里释放
    std::unique_ptr<std::wstring> name((std::wstring*)lParam);
    UINT peerUserId = (UINT)wParam;
    if (peerUserId == 0)
        return 0;

    if (m_pMediaModule == NULL)
    {
        LOG_ERROR("Start remote desktop failed: media module is not loaded, userid=%u.", peerUserId);
        ::MessageBox(m_hWnd, TR(L"音视频组件未能加载，暂时无法发起远程桌面。"), g_strAppTitle.c_str(), MB_OK | MB_ICONWARNING);
        return 0;
    }
    const wchar_t* peerName = (name.get() != NULL) ? name->c_str() : L"";
    if (!m_pMediaModule->StartRemoteDesktop((int64_t)peerUserId, peerName))
        LOG_ERROR("Start remote desktop failed: media module rejected, userid=%u.", peerUserId);
    return 0;
}

// v2 转发消息的应答。wParam 是请求时 CIUSocket::ForwardMessage 返回的帧 seq，
// 交给 OnForwardResponse（BalloonMainPanelForward.cpp），由转发调度器据它对回是哪一次转发、
// 哪一个目标。
LRESULT CBalloonMainPanel::OnV2ForwardResult(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam)
{
    //lParam 为 IUSocket 接收线程堆分配的 net::ForwardMessageResponse，处理后须 delete。
    net::ForwardMessageResponse* pResp = (net::ForwardMessageResponse*)lParam;
    if (pResp == NULL)
        return 0;

    LOG_INFO("v2 forward result, seq=%u, code=%d, msg=%s, created=%u, bodies=%u.",
             (unsigned)wParam, pResp->m_code, pResp->m_msg.c_str(),
             (unsigned)pResp->m_msgids.size(), (unsigned)pResp->m_messages.size());

    OnForwardResponse((uint32_t)wParam, *pResp);
    delete pResp;
    return 0;
}

// v2 删除单条消息的应答：成功时不动界面（本地删行、移除气泡等删除推送到达后统一做），
// 失败时按错误码提示原因。
LRESULT CBalloonMainPanel::OnV2DeleteMessageResult(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //lParam 为 IUSocket 接收线程堆分配的 net::DeleteMessageResponse，处理后须 delete。
    net::DeleteMessageResponse* pResp = (net::DeleteMessageResponse*)lParam;
    if (pResp == NULL)
        return 0;

    if (pResp->m_code != 0)
    {
        //消息不存在是最常见的失败：它已在别的设备上被删掉、或被对方撤回后又被清理
        LPCTSTR pszTip = TR(L"删除失败，请稍后重试。");
        if (pResp->m_code == error_code_message_not_exist)
            pszTip = TR(L"该消息不存在或已被删除。");

        LOG_ERROR("v2 delete message failed, code=%d, msg=%s.", pResp->m_code, pResp->m_msg.c_str());
        ::MessageBox(m_hWnd, pszTip, g_strAppTitle.c_str(), MB_OK | MB_ICONINFORMATION);
    }

    delete pResp;
    return 0;
}

// v2 按时间范围清理的应答：转交系统设置 → 清理聊天记录对话框提示结果（对话框已关闭时
// 只记日志）。
LRESULT CBalloonMainPanel::OnV2DeleteByTimeResult(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //lParam 为 IUSocket 接收线程堆分配的 net::DeleteMessagesByTimeResponse，处理后须 delete。
    net::DeleteMessagesByTimeResponse* pResp = (net::DeleteMessagesByTimeResponse*)lParam;
    if (pResp == NULL)
        return 0;

    LOG_INFO("v2 delete messages by time result, code=%d, msg=%s, range=[%lld, %lld].",
             pResp->m_code, pResp->m_msg.c_str(),
             (long long)pResp->m_begintime, (long long)pResp->m_endtime);

    //结果提示交给系统设置 → 清理聊天记录对话框；本机记录的删除等 1522 区间推送，与此无关
    m_systemSettingDlg.OnChatCleanResult(pResp->m_code, pResp->m_begintime, pResp->m_endtime);

    delete pResp;
    return 0;
}

// v2 按会话清空聊天记录的应答：成功时什么都不做（本地删除与界面刷新等清空形式的 1522 推送，
// 本机与本人其他设备走同一条路径）；失败时按 seq 对回是哪个会话，把提示弹在发起的消息记录窗上，
// 主面板右键菜单发起的弹在该会话的聊天窗上（见 ChooseClearFailureOwner）。
LRESULT CBalloonMainPanel::OnV2ClearConversationResult(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam)
{
    //lParam 为 IUSocket 接收线程堆分配的 net::ClearConversationResponse，处理后须 delete。
    net::ClearConversationResponse* pResp = (net::ClearConversationResponse*)lParam;
    if (pResp == NULL)
        return 0;

    const uint32_t seq = (uint32_t)wParam;
    const balloonmain::ClearHistoryOutcome outcome = m_clearHistoryTracker.OnResult(seq, pResp->m_code);
    LOG_INFO("v2 clear conversation result, seq=%u, code=%d, msg=%s, matched=%d, ctype=%d, cid=%lld, clearedmsgid=%lld.",
             seq, pResp->m_code, pResp->m_msg.c_str(), outcome.m_matched ? 1 : 0,
             outcome.m_op.m_ctype, (long long)outcome.m_op.m_cid, (long long)pResp->m_clearedMsgid);

    //没对上（重连前发出的旧请求等）只记日志；对上且失败才提示
    if (outcome.m_matched && !outcome.m_succeeded)
    {
        //消息记录窗发起的弹在消息记录窗上（它还开着时），其余弹在该会话的聊天窗上
        const HWND requester = outcome.m_op.m_ownerHwnd;
        const HWND hOwner = balloonmain::ChooseClearFailureOwner(
            requester, ::IsWindow(requester) != FALSE, m_hWnd,
            ConversationDialogOwner(outcome.m_op.m_ctype, outcome.m_op.m_cid));
        ::MessageBox(hOwner, outcome.m_errorText.c_str(), TR(L"清空聊天记录"), MB_OK | MB_ICONWARNING);
    }

    delete pResp;
    return 0;
}

// v2 本人删除了消息的推送（单条、区间或清空）：发起删除的这台设备自己也会收到，本机与其他
// 设备统一在这里更新本地库与界面。
LRESULT CBalloonMainPanel::OnV2MessagesDeleted(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //lParam 为 IUSocket 接收线程堆分配的 net::MessagesDeletedNotify，处理后须 delete。
    net::MessagesDeletedNotify* pNotify = (net::MessagesDeletedNotify*)lParam;
    if (pNotify == NULL)
        return 0;

    OnMessagesDeleted(*pNotify);
    delete pNotify;
    return 0;
}

// v2 对方已读推送：记下对方的已读位点，并转给已打开的单聊窗把对应气泡标为已读。
// v2 功能开关推送（1013，第 3 期第 1 步）：管理员改了本企业的开关。交给文件共享宿主按新开关激活或收起模块
// （收起时模块关闭全部窗口、结束会话，DLL 不卸载）；入口显隐有变化时刷新已打开聊天窗的「文件共享」按钮。
// 系统菜单每次弹出时现建，不用在这里处理；卡片状态随后由服务端推来的 2203 刷新。
LRESULT CBalloonMainPanel::OnV2FeaturesNotify(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //lParam 为 IUSocket 接收线程堆分配的 net::FeaturesNotify，处理后须 delete。
    net::FeaturesNotify* pNotify = (net::FeaturesNotify*)lParam;
    if (pNotify == NULL)
        return 0;

    const bool fileShareOn = net::LoginProtocolClient::isFeatureOn(pNotify->m_features, net::kFeatureFileShare);
    //「邀请入群需被邀请人同意」：只记下新值，下次打开邀请对话框时生效
    const bool inviteConfirmOn = net::LoginProtocolClient::isFeatureOn(pNotify->m_features, net::kFeatureInviteConfirm);
    if (m_lpFMGClient != NULL)
        m_lpFMGClient->m_UserMgr.m_bInviteConfirmFeature = inviteConfirmOn;
    LOG_INFO("Features notify: inviteconfirm=%d.", inviteConfirmOn ? 1 : 0);
    //群语音 / 群视频通话：记下新值，已打开的群聊窗按它显隐「语音通话」「视频通话」按钮（进行中的通话不受影响）
    const bool groupCallOn = net::LoginProtocolClient::isFeatureOn(pNotify->m_features, net::kFeatureGroupCall);
    const bool groupCallChanged = (m_lpFMGClient != NULL && m_lpFMGClient->m_UserMgr.m_bGroupCallFeature != groupCallOn);
    if (m_lpFMGClient != NULL)
        m_lpFMGClient->m_UserMgr.m_bGroupCallFeature = groupCallOn;
    LOG_INFO("Features notify: groupcall=%d, changed=%d.", groupCallOn ? 1 : 0, groupCallChanged ? 1 : 0);
    //语音字幕：交给字幕宿主，可用状态变化时由它通知 FlamingoMedia 停止识别、显隐字幕入口（docs/语音字幕方案.md D5）
    const bool captionOn = net::LoginProtocolClient::isFeatureOn(pNotify->m_features, net::kFeatureCaption);
    if (m_lpFMGClient != NULL)
        m_lpFMGClient->m_UserMgr.m_bCaptionFeature = captionOn;
    ballooncaption::CCaptionHost::GetInstance().OnFeaturesChanged(captionOn);
    //云端录制：记下新值，FlamingoMedia 下次打开菜单或选择窗时据此显隐入口；进行中的云端录制不受影响
    const bool cloudRecordOn = net::LoginProtocolClient::isFeatureOn(pNotify->m_features, net::kFeatureCloudRecord);
    if (m_lpFMGClient != NULL)
        m_lpFMGClient->m_UserMgr.m_bCloudRecordFeature = cloudRecordOn;
    LOG_INFO("Features notify: cloudrecord=%d.", cloudRecordOn ? 1 : 0);
    //单聊密聊：交给密聊宿主。由关变开时补加载并报到，报到应答到达后（OnSecretRegistered）再刷新「密聊」按钮；
    //由开变关时进行中的密聊由服务端发 2605 结束，这里只刷新按钮
    const bool secretOn = net::LoginProtocolClient::isFeatureOn(pNotify->m_features, net::kFeatureSecretChat);
    if (m_lpFMGClient != NULL)
        m_lpFMGClient->m_UserMgr.m_bSecretChatFeature = secretOn;
    if (balloonsecret::CSecretChatHost::GetInstance().OnFeaturesChanged(secretOn))
        RefreshSecretEntries();
    LOG_INFO("Features notify: secretchat=%d.", secretOn ? 1 : 0);
    //帖子群：交给帖子群宿主。由关变开时补加载并让模块进入已登录状态，由开变关时模块关闭全部帖子群窗口；
    //可用状态变了就刷新各处列表，帖子群随之出现或隐藏（docs/帖子群方案.md 4.7）
    const bool threadOn = net::LoginProtocolClient::isFeatureOn(pNotify->m_features, net::kFeatureThreadGroup);
    if (m_lpFMGClient != NULL)
        m_lpFMGClient->m_UserMgr.m_bThreadGroupFeature = threadOn;
    if (balloonthread::CThreadGroupHost::GetInstance().OnFeaturesChanged(threadOn))
        OnThreadGroupAvailabilityChanged();
    LOG_INFO("Features notify: threadgroup=%d.", threadOn ? 1 : 0);
    //机器人：四项开关写回用户管理器，打开着的群聊窗重拉本群机器人（关闭时机器人区域随之隐藏）
    {
        const bool botOn = net::LoginProtocolClient::isFeatureOn(pNotify->m_features, net::kFeatureBot);
        const bool botGroupOn = net::LoginProtocolClient::isFeatureOn(pNotify->m_features, net::kFeatureBotGroup);
        const bool botPersonalOn = net::LoginProtocolClient::isFeatureOn(pNotify->m_features, net::kFeatureBotPersonal);
        const bool botExternalOn =
            net::LoginProtocolClient::isFeatureOn(pNotify->m_features, net::kFeatureBotGroupExternal);
        bool botChanged = false;
        if (m_lpFMGClient != NULL)
        {
            CUserMgr& um = m_lpFMGClient->m_UserMgr;
            botChanged = um.m_bBotFeature != botOn || um.m_bBotGroupFeature != botGroupOn
                         || um.m_bBotPersonalFeature != botPersonalOn || um.m_bBotGroupExternalFeature != botExternalOn;
            um.m_bBotFeature = botOn;
            um.m_bBotGroupFeature = botGroupOn;
            um.m_bBotPersonalFeature = botPersonalOn;
            um.m_bBotGroupExternalFeature = botExternalOn;
        }
        if (botChanged)
        {
            for (std::map<int64_t, CBalloonGroupChatWindow*>::iterator it = m_groupChatWindows.begin();
                 it != m_groupChatWindows.end(); ++it)
            {
                if (it->second != NULL && it->second->IsWindow())
                    it->second->OnGroupBotsChanged();
            }
            OnBotFeaturesChanged();
        }
        LOG_INFO("Features notify: bot=%d, botgroup=%d, botpersonal=%d, botgroupexternal=%d, changed=%d.", botOn ? 1 : 0,
                 botGroupOn ? 1 : 0, botPersonalOn ? 1 : 0, botExternalOn ? 1 : 0, botChanged ? 1 : 0);
    }
    if (groupCallChanged)
    {
        for (std::map<int64_t, CBalloonGroupChatWindow*>::iterator it = m_groupChatWindows.begin();
             it != m_groupChatWindows.end(); ++it)
        {
            if (it->second != NULL && it->second->IsWindow())
                it->second->ApplyGroupCallButtonVisibility();
        }
    }
    delete pNotify;
    const bool changed = balloonfileshare::CFileShareHost::GetInstance().OnFeaturesChanged(fileShareOn);
    LOG_INFO("Features notify handled: fileshare=%d, entries changed=%d.", fileShareOn ? 1 : 0, changed ? 1 : 0);
    if (!changed)
        return 0;
    // 底部主操作按钮在「文件共享」与「查找」之间切换
    RefreshBottomAction();
    for (std::map<UINT, CBalloonChatWindow*>::iterator it = m_chatWindows.begin(); it != m_chatWindows.end(); ++it)
    {
        if (it->second != NULL && it->second->IsWindow())
            it->second->ApplyShareButtonVisibility();
    }
    for (std::map<int64_t, CBalloonGroupChatWindow*>::iterator it = m_groupChatWindows.begin(); it != m_groupChatWindows.end(); ++it)
    {
        if (it->second != NULL && it->second->IsWindow())
            it->second->ApplyShareButtonVisibility();
    }
    return 0;
}

LRESULT CBalloonMainPanel::OnV2MessageReadNotify(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //lParam 为 IUSocket 接收线程堆分配的 net::MessageReadNotify，处理后须 delete。
    net::MessageReadNotify* pNotify = (net::MessageReadNotify*)lParam;
    if (pNotify == NULL)
        return 0;

    OnMessageReadNotify(*pNotify);
    delete pNotify;
    return 0;
}

// v2 Pin 消息应答：与撤回同口径 —— 成功时什么都不做（界面改动等 cmd_message_pinned_notify
// 推送），只在失败时据 code 给出具体原因。
LRESULT CBalloonMainPanel::OnV2PinResult(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //lParam 为 IUSocket 接收线程堆分配的 net::PinMessageResponse，处理后须 delete。
    net::PinMessageResponse* pResp = (net::PinMessageResponse*)lParam;
    if (pResp == NULL)
        return 0;

    if (pResp->m_code != 0)
    {
        //按服务端返回的 error_code 给出具体原因，别让用户面对一句笼统的"操作失败"。
        //文案要分清"钉"与"取消"两个方向 —— 同一个失败码在两个方向下读起来完全不同。
        const bool bPinning = (pResp->m_pin == 1);
        LPCTSTR pszTip = bPinning ? TR(L"Pin 失败，请稍后重试。") : TR(L"取消 Pin 失败，请稍后重试。");
        if (pResp->m_code == error_code_pin_not_allowed)
            pszTip = TR(L"只有钉住这条消息的人和群主 / 管理员才能取消它。");
        else if (pResp->m_code == error_code_message_not_exist)
            pszTip = TR(L"该消息不存在或已被撤回。");
        else if (pResp->m_code == error_code_not_group_member)
            pszTip = TR(L"你已不在该群聊中。");

        LOG_ERROR("v2 pin message failed, code=%d, msg=%s, ctype=%d, cid=%lld, msgid=%lld, pin=%d.",
                  pResp->m_code, pResp->m_msg.c_str(), pResp->m_ctype,
                  (long long)pResp->m_cid, (long long)pResp->m_msgid, pResp->m_pin);

        ::MessageBox(m_hWnd, pszTip, g_strAppTitle.c_str(), MB_OK | MB_ICONINFORMATION);
    }

    delete pResp;
    return 0;
}

// v2 某条消息被 Pin / 取消 Pin 的推送：统一处理（落本地消息库标记、刷新
// 消息记录窗；本地没有这条消息时再向服务端补拉一次消息体）。操作者本人也会收到本推送。
LRESULT CBalloonMainPanel::OnV2MessagePinned(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //lParam 为 IUSocket 接收线程堆分配的 net::MessagePinnedNotify，处理后须 delete。
    net::MessagePinnedNotify* pNotify = (net::MessagePinnedNotify*)lParam;
    if (pNotify == NULL)
        return 0;

    OnMessagePinned(*pNotify);
    delete pNotify;
    return 0;
}

// v2 接龙名单或状态变化的推送：合并进本地库里那张卡片的内容，刷新消息记录窗。
// 群聊窗里卡片的实时刷新在卡片气泡接入后补上（docs/群接龙方案.md 3a）。
LRESULT CBalloonMainPanel::OnV2RelayChanged(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //lParam 为 IUSocket 接收线程堆分配的 net::RelayChangedNotify，处理后须 delete。
    net::RelayChangedNotify* pNotify = (net::RelayChangedNotify*)lParam;
    if (pNotify == NULL)
        return 0;

    if (!EnsureMsgStoreOpen())
    {
        LOG_ERROR("v2 relay changed notify dropped: local message store not ready, msgid=%lld.",
                  (long long)pNotify->m_msgid);
        delete pNotify;
        return 0;
    }

    //一、合并进本地库。本地没有这张卡片（本机没同步过这段历史）不是错误，下次同步会带回最新内容
    std::string mergedJson;
    bool bHit = false;
    if (!balloonmain::ApplyRelayNotifyToStore(m_msgStore, *pNotify, mergedJson, bHit))
    {
        LOG_ERROR("v2 relay changed notify: merge into local store failed, ctype=%d, cid=%lld, msgid=%lld.",
                  pNotify->m_ctype, (long long)pNotify->m_cid, (long long)pNotify->m_msgid);
    }
    else
    {
        LOG_INFO("v2 relay changed notify merged, ctype=%d, cid=%lld, msgid=%lld, items=%u, state=%d, hit=%d.",
                 pNotify->m_ctype, (long long)pNotify->m_cid, (long long)pNotify->m_msgid,
                 (unsigned)pNotify->m_items.size(), pNotify->m_state, bHit ? 1 : 0);
    }

    //二、若消息记录窗正显示本会话，刷新它
    m_historyWindow.RefreshIfConversation(pNotify->m_ctype, pNotify->m_cid);

    //三、若该群的聊天窗开着，刷新窗里的那张卡片（卡片不在当前视图里时窗口自己忽略）。
    //接龙只在群里发起（方案 §1），单聊卡片的推送只更新本地库
    if (pNotify->m_ctype == conversation_group)
    {
        std::map<int64_t, CBalloonGroupChatWindow*>::iterator it = m_groupChatWindows.find(pNotify->m_cid);
        if (it != m_groupChatWindows.end() && it->second != NULL && it->second->IsWindow())
        {
            it->second->OnRelayChanged(*pNotify);
        }
    }

    //四、若这张卡片的详情窗开着，刷新它
    CRelayDetailWindow* detail = FindRelayDetailWindow(pNotify->m_msgid);
    if (detail != NULL)
    {
        detail->OnRelayChanged(*pNotify);
    }

    delete pNotify;
    return 0;
}

// v2 接龙操作（参与 / 撤回本人那条 / 提前结束）的应答：只在失败时据 code 提示原因，
// 成功一律等 cmd_relay_changed_notify 推送再改界面。提示弹在对应的聊天窗上（没有打开时弹在主面板上）。
LRESULT CBalloonMainPanel::OnV2RelayOpResult(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam)
{
    //lParam 为 IUSocket 接收线程堆分配的 net::RelayOpResponse，处理后须 delete；wParam 为命令号
    net::RelayOpResponse* pResp = (net::RelayOpResponse*)lParam;
    if (pResp == NULL)
        return 0;

    const int cmd = (int)wParam;
    if (pResp->m_code != 0)
    {
        LOG_ERROR("v2 relay op failed, cmd=%d, code=%d, msg=%s, ctype=%d, cid=%lld, msgid=%lld.",
                  cmd, pResp->m_code, pResp->m_msg.c_str(), pResp->m_ctype,
                  (long long)pResp->m_cid, (long long)pResp->m_msgid);
    }

    //请求是从详情窗发出的（参与、修改、撤回、结束都在详情窗里）：结果交给它，失败原因写在窗口里；
    //详情窗已经关掉了、或请求来自发起窗口的「我的接龙」，失败时才弹提示框
    CRelayDetailWindow* detail = FindRelayDetailWindow(pResp->m_msgid);
    if (detail != NULL)
    {
        detail->OnOpResult(cmd, pResp->m_code);
    }
    else if (pResp->m_code != 0)
    {
        const CString tip = balloonchatwnd::RelayOpErrorText(cmd, pResp->m_code);
        ::MessageBox(ConversationDialogOwner(pResp->m_ctype, pResp->m_cid), tip, g_strAppTitle.c_str(),
                     MB_OK | MB_ICONINFORMATION);
    }

    delete pResp;
    return 0;
}

// v2 点入群邀请卡片「加入群聊」的应答（docs/邀请入群方案.md §4.4）。已入群：打开群聊窗；转为加群申请：卡片状态
// 等 1328 推送再改，这里不做事；失败：恢复卡片按钮，已过期、已失效两种情况服务端不推 1328，这里把卡片与本地库
// 改成对应状态，再提示原因。已在群中（305）服务端另推 1328，这里直接打开群聊窗。
LRESULT CBalloonMainPanel::OnV2AcceptGroupInviteResult(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //lParam 为 IUSocket 接收线程堆分配的 net::AcceptGroupInviteResult，处理后须 delete
    std::unique_ptr<net::AcceptGroupInviteResult> pResult((net::AcceptGroupInviteResult*)lParam);
    if (!pResult)
        return 0;

    //成功：已入群时打开群聊窗（群资料还没到时窗口先用默认名字，等 1303 应答补设）；转为申请时只等推送
    if (pResult->m_code == 0)
    {
        LOG_INFO("v2 accept group invite ok, inviteid=%lld, groupid=%lld, joined=%d.",
                 (long long)pResult->m_inviteid, (long long)pResult->m_groupid, pResult->m_joined ? 1 : 0);
        if (pResult->m_joined && pResult->m_groupid > 0)
            OpenGroupChat(pResult->m_groupid);
        return 0;
    }

    LOG_ERROR("v2 accept group invite failed, code=%d, msg=%s, inviteid=%lld, groupid=%lld.",
              pResult->m_code, pResult->m_msg.c_str(), (long long)pResult->m_inviteid,
              (long long)pResult->m_groupid);

    //一、在已打开的单聊窗里找到那张卡片，恢复按钮并按错误码换状态；提示框弹在那个窗口上
    const int state = balloonchatwnd::GroupInviteStateForAcceptError(pResult->m_code);
    HWND hOwner = m_hWnd;
    for (std::map<UINT, CBalloonChatWindow*>::iterator it = m_chatWindows.begin(); it != m_chatWindows.end(); ++it)
    {
        if (it->second == NULL || !it->second->IsWindow())
            continue;
        const int64_t msgid = it->second->OnGroupInviteAcceptFailed(pResult->m_inviteid, state);
        if (msgid <= 0)
            continue;
        hOwner = it->second->m_hWnd;

        //二、状态有变化时一并改写本地库，重新打开聊天窗时显示的是新状态
        bool hit = false;
        if (state != balloonchatwnd::kGroupInviteStateUnchanged && EnsureMsgStoreOpen()
            && !balloonmain::ApplyGroupInviteStateToStore(m_msgStore, (int64_t)it->first, msgid, state, hit))
        {
            LOG_ERROR("v2 accept group invite failed: update local card failed, peer=%u, msgid=%lld, state=%d.",
                      it->first, (long long)msgid, state);
        }
        m_historyWindow.RefreshIfConversation(conversation_single, (int64_t)it->first);
        break;
    }

    //三、已在群中：服务端已把卡片记为已加入并推了 1328，直接打开群聊窗；其余错误提示原因
    if (pResult->m_code == error_code_already_in_group)
    {
        if (pResult->m_groupid > 0)
            OpenGroupChat(pResult->m_groupid);
        return 0;
    }
    const CString tip = balloonchatwnd::GroupInviteAcceptErrorText(pResult->m_code);
    if (!tip.IsEmpty())
        ::MessageBox(hOwner, tip, g_strAppTitle.c_str(), MB_OK | MB_ICONINFORMATION);
    return 0;
}

// v2 查询入群必读的应答（1335，docs/群公告接入方案.md §14）：按群转给打开着的群聊窗
LRESULT CBalloonMainPanel::OnV2MustRead(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //lParam 为 IUSocket 接收线程堆分配的 net::MustReadResult，处理后须 delete
    std::unique_ptr<net::MustReadResult> pResult((net::MustReadResult*)lParam);
    if (!pResult)
        return 0;
    std::map<int64_t, CBalloonGroupChatWindow*>::iterator it = m_groupChatWindows.find(pResult->m_groupid);
    if (it != m_groupChatWindows.end() && it->second != NULL && it->second->IsWindow())
        it->second->OnMustReadArrived(*pResult);
    return 0;
}

// v2 群问答列表的应答（1336，docs/群问答方案.md）：按群转给打开着的群聊窗
LRESULT CBalloonMainPanel::OnV2GroupQa(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //lParam 为 IUSocket 接收线程堆分配的 net::GroupQaResult，处理后须 delete
    std::unique_ptr<net::GroupQaResult> pResult((net::GroupQaResult*)lParam);
    if (!pResult)
        return 0;
    std::map<int64_t, CBalloonGroupChatWindow*>::iterator it = m_groupChatWindows.find(pResult->m_groupid);
    if (it != m_groupChatWindows.end() && it->second != NULL && it->second->IsWindow())
        it->second->OnGroupQaArrived(*pResult);
    return 0;
}

// v2 群问答变了的推送（1339）：打开着的群聊窗重拉
LRESULT CBalloonMainPanel::OnV2GroupQaChanged(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //lParam 为堆上的群ID，处理后须 delete
    std::unique_ptr<int64_t> pGroupId((int64_t*)lParam);
    if (!pGroupId)
        return 0;
    std::map<int64_t, CBalloonGroupChatWindow*>::iterator it = m_groupChatWindows.find(*pGroupId);
    if (it != m_groupChatWindows.end() && it->second != NULL && it->second->IsWindow())
        it->second->OnGroupQaChanged();
    return 0;
}

// v2 本群可用机器人的应答（2801，docs/机器人方案.md §4.2）：按群转给打开着的群聊窗
LRESULT CBalloonMainPanel::OnV2GroupBots(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //lParam 为 IUSocket 接收线程堆分配的 net::GroupBotsResult，处理后须 delete
    std::unique_ptr<net::GroupBotsResult> pResult((net::GroupBotsResult*)lParam);
    if (!pResult)
        return 0;
    //先存进缓存，打开着的群机器人管理窗（FlamingoBot.dll）随后经宿主回调读取
    CacheGroupBots(*pResult);
    std::map<int64_t, CBalloonGroupChatWindow*>::iterator it = m_groupChatWindows.find(pResult->m_groupid);
    if (it != m_groupChatWindows.end() && it->second != NULL && it->second->IsWindow())
        it->second->OnGroupBotsArrived(*pResult);
    //新结果已到：打开着的群机器人管理窗刷新。只在 2820 时通知不够，那时新的 2801 还没有回来
    botui::NotifyGroupBotsChanged(pResult->m_groupid);
    return 0;
}

// v2 群机器人变化的推送（2820）：群号为 0 表示本企业全部群（企业机器人在管理台有变化），打开着的群聊窗都重拉
LRESULT CBalloonMainPanel::OnV2GroupBotsChanged(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //lParam 为堆上的群ID，处理后须 delete
    std::unique_ptr<int64_t> pGroupId((int64_t*)lParam);
    if (!pGroupId)
        return 0;
    //缓存里的结果已经过时；管理窗随后读不到结果，会经宿主回调请本面板重拉 2801
    ForgetGroupBots(*pGroupId);
    for (std::map<int64_t, CBalloonGroupChatWindow*>::iterator it = m_groupChatWindows.begin();
         it != m_groupChatWindows.end(); ++it)
    {
        if (it->second == NULL || !it->second->IsWindow())
            continue;
        if (*pGroupId == 0 || it->first == *pGroupId)
            it->second->OnGroupBotsChanged();
    }
    //打开着的群机器人管理窗（FlamingoBot.dll）读不到缓存，会请本面板重拉 2801
    botui::NotifyGroupBotsChanged(*pGroupId);
    return 0;
}

// v2 自动回复设置的应答（2810，docs/机器人方案.md §11 Q2）：服务端已有设置时覆盖本机；还没有时把本机旧设置上传一次
LRESULT CBalloonMainPanel::OnV2AutoReply(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //lParam 为 IUSocket 接收线程堆分配的 net::AutoReplyResult，处理后须 delete
    std::unique_ptr<net::AutoReplyResult> pResult((net::AutoReplyResult*)lParam);
    if (!pResult || m_lpFMGClient == NULL)
        return 0;
    if (pResult->m_code != 0)
    {
        LOG_WARNING("Auto-reply settings fetch failed, code=%d.", pResult->m_code);
        return 0;
    }
    CUserConfig& cfg = m_lpFMGClient->m_UserConfig;
    if (!pResult->m_exists)
    {
        const bool awayOn = (cfg.IsEnableAutoReplyAway() != FALSE);
        const bool busyOn = (cfg.IsEnableAutoReplyBusy() != FALSE);
        const std::wstring awayText = cfg.GetAutoReplyAwayText();
        const std::wstring busyText = cfg.GetAutoReplyBusyText();
        if (balloonmain::IsDefaultAutoReply(awayOn, awayText, busyOn, busyText))
        {
            LOG_INFO("Auto-reply settings: server has none and local is default, nothing to upload.");
            return 0;
        }
        CIUSocket::GetInstance().SendSetAutoReply(balloonmain::BuildAutoReplyWire(awayOn, awayText, busyOn, busyText));
        LOG_INFO("Auto-reply settings: local settings uploaded once, away=%d, busy=%d.", awayOn ? 1 : 0, busyOn ? 1 : 0);
        return 0;
    }
    bool awayOn = false;
    bool busyOn = false;
    std::wstring awayText;
    std::wstring busyText;
    balloonmain::ApplyServerAutoReply(pResult->m_settings, awayOn, awayText, busyOn, busyText);
    cfg.EnableAutoReplyAway(awayOn ? TRUE : FALSE);
    cfg.EnableAutoReplyBusy(busyOn ? TRUE : FALSE);
    cfg.SetAutoReplyAwayText(awayText.c_str());
    cfg.SetAutoReplyBusyText(busyText.c_str());
    m_lpFMGClient->SaveUserConfig();
    LOG_INFO("Auto-reply settings: overwritten by server, away=%d, busy=%d, allowed=%d.", awayOn ? 1 : 0,
             busyOn ? 1 : 0, pResult->m_allowed ? 1 : 0);
    return 0;
}

// v2 保存自动回复设置的结果（2811）：失败时提示原因
LRESULT CBalloonMainPanel::OnV2SetAutoReplyResult(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/)
{
    const int code = (int)wParam;
    if (code == 0)
        return 0;
    LOG_WARNING("Auto-reply settings save failed, code=%d.", code);
    ::MessageBox(m_hWnd,
                 code == error_code_bot_sensitive_word ? TR(L"自动回复的文字含有敏感词，没有保存到服务器。")
                                           : TR(L"自动回复设置没有保存到服务器，请稍后重试。"),
                 g_strAppTitle.c_str(), MB_OK | MB_ICONWARNING);
    return 0;
}

// v2 生成加群链接的应答（1340，docs/邀请入群方案.md §12）：交给「生成加群链接」窗口
LRESULT CBalloonMainPanel::OnV2GroupLinkCreated(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //lParam 为 IUSocket 接收线程堆分配的 net::CreateGroupLinkResult，处理后须 delete
    std::unique_ptr<net::CreateGroupLinkResult> pResult((net::CreateGroupLinkResult*)lParam);
    if (!pResult)
        return 0;
    m_groupLinkDlg.OnLinkCreated(*pResult);
    return 0;
}

// v2 加群链接列表的应答（1341）：按群号交给「生成加群链接」窗口（「我生成的链接」）与管着这个群的群管理窗，
// 两边看的是同一个群时都给 —— 应答里看不出是谁发的请求
LRESULT CBalloonMainPanel::OnV2GroupLinks(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //lParam 为 IUSocket 接收线程堆分配的 net::GroupLinksResult，处理后须 delete
    std::unique_ptr<net::GroupLinksResult> pResult((net::GroupLinksResult*)lParam);
    if (!pResult)
        return 0;
    CBalloonGroupManageWindow* pWin = GetLiveGroupManageWindow(pResult->m_groupid);
    const balloonmain::GroupLinksDelivery delivery = balloonmain::DecideGroupLinksDelivery(
        pResult->m_groupid, m_groupLinkDlg.GroupId(), (pWin != NULL) ? pWin->GetGroupId() : 0);
    if (delivery.m_toDialog)
        m_groupLinkDlg.OnLinksArrived(*pResult);
    if (delivery.m_toManageWindow && pWin != NULL)
        pWin->OnGroupLinksArrived(*pResult);
    return 0;
}

// v2 按随机码查群的应答（1343）：已在群里就打开群聊窗，能加入时弹确认框，确认后发 1344；否则提示原因
LRESULT CBalloonMainPanel::OnV2GroupLinkInfo(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //lParam 为 IUSocket 接收线程堆分配的 net::GroupLinkInfoResult，处理后须 delete
    std::unique_ptr<net::GroupLinkInfoResult> pResult((net::GroupLinkInfoResult*)lParam);
    if (!pResult)
        return 0;
    const std::string code = m_joinLinkCode;
    const int action = balloonmain::DecideLinkInfoAction(pResult->m_code, pResult->m_member, pResult->m_joinmode);
    LOG_INFO("join-group link info, code=%d, groupid=%lld, action=%d.", pResult->m_code,
             (long long)pResult->m_groupid, action);
    switch (action)
    {
    //已在群里：直接打开群聊窗
    case balloonmain::kLinkActionOpenGroup:
        m_joinLinkCode.clear();
        OpenGroupChat(pResult->m_groupid);
        break;

    //可以加入：确认后发 1344
    case balloonmain::kLinkActionConfirmJoin:
    {
        const std::wstring name = EncodeUtil::Utf8ToUnicode(pResult->m_name);
        const std::wstring text = balloonmain::JoinLinkConfirmText(name, pResult->m_membercount, pResult->m_joinmode);
        if (code.empty() || ::MessageBox(m_hWnd, text.c_str(), TR(L"加入群聊"), MB_OKCANCEL | MB_ICONQUESTION) != IDOK)
        {
            m_joinLinkCode.clear();
            break;
        }
        CIUSocket::GetInstance().SendJoinGroupByLink(code);
        break;
    }

    //群禁止加入
    case balloonmain::kLinkActionForbidden:
        m_joinLinkCode.clear();
        ::MessageBox(m_hWnd, balloonmain::GroupLinkErrorText(error_code_permission_denied).c_str(),
                     g_strAppTitle.c_str(), MB_OK | MB_ICONINFORMATION);
        break;

    //链接无效、不是同一企业等：提示原因
    default:
        m_joinLinkCode.clear();
        ::MessageBox(m_hWnd, balloonmain::GroupLinkErrorText(pResult->m_code).c_str(),
                     g_strAppTitle.c_str(), MB_OK | MB_ICONINFORMATION);
        break;
    }
    return 0;
}

// v2 凭链接加群的应答（1344）：已入群就打开群聊窗，转为申请时提示等待审批，失败时提示原因
LRESULT CBalloonMainPanel::OnV2JoinGroupByLink(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //lParam 为 IUSocket 接收线程堆分配的 net::JoinGroupByLinkResult，处理后须 delete
    std::unique_ptr<net::JoinGroupByLinkResult> pResult((net::JoinGroupByLinkResult*)lParam);
    if (!pResult)
        return 0;
    m_joinLinkCode.clear();
    if (pResult->m_code != 0)
    {
        LOG_WARNING("join group by link failed, code=%d, msg=%s.", pResult->m_code, pResult->m_msg.c_str());
        ::MessageBox(m_hWnd, balloonmain::GroupLinkErrorText(pResult->m_code).c_str(),
                     g_strAppTitle.c_str(), MB_OK | MB_ICONINFORMATION);
        return 0;
    }
    LOG_INFO("join group by link ok, groupid=%lld, joined=%d.", (long long)pResult->m_groupid, pResult->m_joined ? 1 : 0);
    if (pResult->m_joined && pResult->m_groupid > 0)
    {
        //群列表随入群推送（1317）补拉，群聊窗先用默认名字打开，等群资料应答补设
        OpenGroupChat(pResult->m_groupid);
        return 0;
    }
    ::MessageBox(m_hWnd, balloonmain::JoinLinkPendingText().c_str(), g_strAppTitle.c_str(), MB_OK | MB_ICONINFORMATION);
    return 0;
}

// FMG_MSG_OPEN_JOIN_GROUP_LINK：点了加群链接（聊天窗里点的，或另一个客户端进程经代理窗交来的）
LRESULT CBalloonMainPanel::OnOpenJoinGroupLinkMsg(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& /*bHandled*/)
{
    //lParam 为堆上的随机码，处理后须 delete
    std::unique_ptr<std::string> pCode((std::string*)lParam);
    if (!pCode || pCode->empty())
        return 0;
    LOG_INFO("open join-group link requested.");
    //从别的进程交来时主面板可能藏在托盘里：先把它带到前台，后面的确认框挂在它上面
    if (m_sessionController.IsLoggedIn() && !IsWindowVisible())
    {
        ShowWindow(SW_SHOW);
    }
    if (IsWindowVisible())
    {
        ::SetForegroundWindow(m_hWnd);
    }
    StartJoinGroupLink(*pCode);
    return 0;
}

// v2 「加群申请已处理」推送（1329，docs/邀请入群方案.md 第 4 期）：另一位群主 / 管理员、或本人的其他设备处理了一条申请。
// 这个群的群管理窗开着时重拉待审批列表，与收到 1307 时同一个做法；没开着时什么都不做，下次打开会重新拉到
LRESULT CBalloonMainPanel::OnV2JoinRequestHandled(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //lParam 为 IUSocket 接收线程堆分配的 net::JoinRequestHandledNotify，处理后须 delete
    std::unique_ptr<net::JoinRequestHandledNotify> pNotify((net::JoinRequestHandledNotify*)lParam);
    if (!pNotify)
        return 0;

    CBalloonGroupManageWindow* pWin = GetLiveGroupManageWindow(pNotify->m_groupid);
    LOG_INFO("v2 join-request handled notify, groupid=%lld, requestid=%lld, status=%d, handler=%lld, window=%d.",
             (long long)pNotify->m_groupid, (long long)pNotify->m_requestid, pNotify->m_status,
             (long long)pNotify->m_handler, pWin != NULL ? 1 : 0);
    if (pWin != NULL)
        pWin->OnJoinRequestNotify();
    return 0;
}

// v2 入群邀请卡片状态变化推送（docs/邀请入群方案.md §4.5）：推给邀请人与被邀请人。按两个 userid 里不是本人的那一方
// 定位单聊会话，改写本地库里那张卡片的状态，再刷新消息记录窗与已打开的单聊窗。
LRESULT CBalloonMainPanel::OnV2CallCardChanged(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //lParam 为 IUSocket 接收线程堆分配的 net::CallCardNotify，处理后须 delete
    std::unique_ptr<net::CallCardNotify> pNotify((net::CallCardNotify*)lParam);
    if (!pNotify)
        return 0;

    //一、改写本地库里那张卡片：写回 content 列，格式与入库时相同（BuildContentJson）。本地没有这张卡片（本机没同步过
    //这段历史）时命中 0 行，属正常，下次同步会带回服务端改写后的内容
    if (!EnsureMsgStoreOpen())
    {
        LOG_ERROR("v2 call card notify: local message store not ready, msgid=%lld.", (long long)pNotify->m_msgid);
    }
    else
    {
        net::MessageNotify forColumn;
        forColumn.m_msgtype = message_type_rtc;
        forColumn.m_groupCallContent = pNotify->m_content;
        bool hit = false;
        if (!m_msgStore.UpdateContent(conversation_single, pNotify->m_cid, pNotify->m_msgid,
                                      balloonhistory::BuildContentJson(forColumn), hit))
        {
            LOG_ERROR("v2 call card notify: update local card failed, peer=%lld, msgid=%lld.", (long long)pNotify->m_cid,
                      (long long)pNotify->m_msgid);
        }
        else
        {
            LOG_INFO("v2 call card notify applied, peer=%lld, msgid=%lld, session=%s, hit=%d.",
                     (long long)pNotify->m_cid, (long long)pNotify->m_msgid, pNotify->m_sessionId.c_str(), hit ? 1 : 0);
        }
    }

    //二、消息记录窗正显示本会话时刷新它
    m_historyWindow.RefreshIfConversation(conversation_single, pNotify->m_cid);

    //三、已打开的单聊窗里的那张卡片（卡片不在当前视图里时窗口自己忽略）
    std::map<UINT, CBalloonChatWindow*>::iterator it = m_chatWindows.find((UINT)pNotify->m_cid);
    if (it != m_chatWindows.end() && it->second != NULL && it->second->IsWindow())
        it->second->OnCallCardChanged(pNotify->m_msgid, pNotify->m_content);
    return 0;
}

LRESULT CBalloonMainPanel::OnV2GroupInviteChanged(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //lParam 为 IUSocket 接收线程堆分配的 net::GroupInviteChangedNotify，处理后须 delete
    std::unique_ptr<net::GroupInviteChangedNotify> pNotify((net::GroupInviteChangedNotify*)lParam);
    if (!pNotify)
        return 0;

    const int64_t peerId = balloonmain::GroupInvitePeerId(pNotify->m_inviterid, pNotify->m_inviteeid,
                                                          (int64_t)SelfUserId());
    if (peerId <= 0)
    {
        LOG_ERROR("v2 group invite changed notify dropped: self is neither inviter nor invitee, inviter=%lld, "
                  "invitee=%lld, self=%u.", (long long)pNotify->m_inviterid, (long long)pNotify->m_inviteeid,
                  SelfUserId());
        return 0;
    }

    //一、改写本地库。本地没有这张卡片（本机没同步过这段历史）不是错误，下次同步会带回最新状态
    bool hit = false;
    if (!EnsureMsgStoreOpen())
    {
        LOG_ERROR("v2 group invite changed notify: local message store not ready, msgid=%lld.",
                  (long long)pNotify->m_msgid);
    }
    else if (!balloonmain::ApplyGroupInviteStateToStore(m_msgStore, peerId, pNotify->m_msgid, pNotify->m_state, hit))
    {
        LOG_ERROR("v2 group invite changed notify: update local card failed, peer=%lld, msgid=%lld, state=%d.",
                  (long long)peerId, (long long)pNotify->m_msgid, pNotify->m_state);
    }
    else
    {
        LOG_INFO("v2 group invite changed notify applied, peer=%lld, msgid=%lld, inviteid=%lld, state=%d, hit=%d.",
                 (long long)peerId, (long long)pNotify->m_msgid, (long long)pNotify->m_inviteid, pNotify->m_state,
                 hit ? 1 : 0);
    }

    //二、消息记录窗正显示本会话时刷新它
    m_historyWindow.RefreshIfConversation(conversation_single, peerId);

    //三、已打开的单聊窗里的那张卡片（卡片不在当前视图里时窗口自己忽略）
    std::map<UINT, CBalloonChatWindow*>::iterator it = m_chatWindows.find((UINT)peerId);
    if (it != m_chatWindows.end() && it->second != NULL && it->second->IsWindow())
        it->second->OnGroupInviteChanged(pNotify->m_msgid, pNotify->m_state);
    return 0;
}

// v2 被 Pin 消息全集应答：把本地库缺的消息补齐，并与这一份对账。
LRESULT CBalloonMainPanel::OnV2PinnedMessages(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //lParam 为 IUSocket 接收线程堆分配的 net::PinnedMessagesResult，处理后须 delete。
    net::PinnedMessagesResult* pResult = (net::PinnedMessagesResult*)lParam;
    if (pResult == NULL)
        return 0;

    OnPinnedMessagesArrived(*pResult);
    delete pResult;
    return 0;
}


//======================== 备忘与提醒 ========================

// v2 备忘列表应答：交给备忘中枢整体替换备忘簿、补出错过的提醒。
LRESULT CBalloonMainPanel::OnV2MemoList(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //lParam 为 IUSocket 接收线程堆分配的 net::MemoListResult，处理后须 delete。
    net::MemoListResult* pResult = (net::MemoListResult*)lParam;
    if (pResult == NULL)
        return 0;

    balloonmemo::CMemoCenter::Instance().OnListResult(*pResult);
    delete pResult;
    return 0;
}

// v2 保存备忘应答：交给备忘中枢，成功时写入备忘簿并关闭编辑对话框，失败时在对话框里写出原因。
LRESULT CBalloonMainPanel::OnV2MemoSaveResult(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    net::MemoSaveResult* pResult = (net::MemoSaveResult*)lParam;
    if (pResult == NULL)
        return 0;

    balloonmemo::CMemoCenter::Instance().OnSaveResult(*pResult);
    delete pResult;
    return 0;
}

// v2 删除备忘应答：交给备忘中枢。
LRESULT CBalloonMainPanel::OnV2MemoDeleteResult(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    net::MemoDeleteResult* pResult = (net::MemoDeleteResult*)lParam;
    if (pResult == NULL)
        return 0;

    balloonmemo::CMemoCenter::Instance().OnDeleteResult(*pResult);
    delete pResult;
    return 0;
}

// v2 备忘推送：交给备忘中枢更新备忘簿与提醒。
LRESULT CBalloonMainPanel::OnV2MemoNotify(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    net::MemoNotify* pNotify = (net::MemoNotify*)lParam;
    if (pNotify == NULL)
        return 0;

    balloonmemo::CMemoCenter::Instance().OnNotify(*pNotify);
    delete pNotify;
    return 0;
}

//======================== 平台公告（公告中心） ========================

// v2 平台公告列表应答：逐条落本地库 announce.db 并刷新铃铛角标。
// 注意与群公告的 OnV2GroupAnnouncements 不是一回事——那个属于某个群，这个属于整个平台。
LRESULT CBalloonMainPanel::OnV2Announcements(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //lParam 为 IUSocket 接收线程堆分配的 net::AnnouncementsResult，处理后须 delete。
    net::AnnouncementsResult* pResult = (net::AnnouncementsResult*)lParam;
    if (pResult == NULL)
        return 0;

    OnAnnouncementsArrived(*pResult);
    delete pResult;
    return 0;
}

// v2 平台公告新发布 / 被撤回推送：落本地库、刷铃铛角标、刷公告中心窗。
LRESULT CBalloonMainPanel::OnV2AnnouncementNotify(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //lParam 为 IUSocket 接收线程堆分配的 net::AnnouncementNotify，处理后须 delete。
    net::AnnouncementNotify* pNotify = (net::AnnouncementNotify*)lParam;
    if (pNotify == NULL)
        return 0;

    OnAnnouncementNotify(*pNotify);
    delete pNotify;
    return 0;
}

// v2 平台公告已读上报应答：成功时界面不需要动（已读在发起时就乐观落了本地库），
// 仅在失败时留痕，便于日后排查「读过的公告在别的设备上仍显示未读」。
LRESULT CBalloonMainPanel::OnV2AnnMarkReadResult(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //lParam 为 IUSocket 接收线程堆分配的 net::AnnounceMarkReadResult，处理后须 delete。
    net::AnnounceMarkReadResult* pResult = (net::AnnounceMarkReadResult*)lParam;
    if (pResult == NULL)
        return 0;

    OnAnnounceMarkReadResult(*pResult);
    delete pResult;
    return 0;
}


//======================== 图片与文件传输 ========================

LRESULT CBalloonMainPanel::OnV2ImageSendProgress(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //lParam 为堆分配的 net::ImageSendProgress，路由后须 delete。
    net::ImageSendProgress* pProg = (net::ImageSendProgress*)lParam;
    if (pProg == NULL)
        return 0;
    OnImageSendProgress(*pProg);
    delete pProg;
    return 0;
}

LRESULT CBalloonMainPanel::OnV2ImageSendResult(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //lParam 为堆分配的 net::ImageSendResult，路由后须 delete。
    net::ImageSendResult* pResult = (net::ImageSendResult*)lParam;
    if (pResult == NULL)
        return 0;
    OnImageSendResult(*pResult);
    delete pResult;
    return 0;
}

LRESULT CBalloonMainPanel::OnV2ImageDownloadResult(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //lParam 为堆分配的 net::ImageDownloadResult，路由后须 delete。
    net::ImageDownloadResult* pResult = (net::ImageDownloadResult*)lParam;
    if (pResult == NULL)
        return 0;
    OnImageDownloadResult(*pResult);
    delete pResult;
    return 0;
}

LRESULT CBalloonMainPanel::OnV2FileSendProgress(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //lParam 为堆分配的 net::FileSendProgress，路由后须 delete。
    net::FileSendProgress* pProg = (net::FileSendProgress*)lParam;
    if (pProg == NULL)
        return 0;
    OnFileSendProgress(*pProg);
    delete pProg;
    return 0;
}

LRESULT CBalloonMainPanel::OnV2FileSendResult(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //lParam 为堆分配的 net::FileSendResult，路由后须 delete。
    net::FileSendResult* pResult = (net::FileSendResult*)lParam;
    if (pResult == NULL)
        return 0;
    OnFileSendResult(*pResult);
    delete pResult;
    return 0;
}

LRESULT CBalloonMainPanel::OnV2FileDownloadProgress(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //lParam 为堆分配的 net::FileDownloadProgress，路由后须 delete。
    net::FileDownloadProgress* pProg = (net::FileDownloadProgress*)lParam;
    if (pProg == NULL)
        return 0;
    OnFileDownloadProgress(*pProg);
    delete pProg;
    return 0;
}

LRESULT CBalloonMainPanel::OnV2FileDownloadResult(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //lParam 为堆分配的 net::FileDownloadResult，路由后须 delete。
    net::FileDownloadResult* pResult = (net::FileDownloadResult*)lParam;
    if (pResult == NULL)
        return 0;
    OnFileDownloadResult(*pResult);
    delete pResult;
    return 0;
}

LRESULT CBalloonMainPanel::OnV2VoiceSendResult(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //lParam 为堆分配的 net::VoiceSendResult，路由后须 delete。
    net::VoiceSendResult* pResult = (net::VoiceSendResult*)lParam;
    if (pResult == NULL)
        return 0;
    OnVoiceSendResult(*pResult);
    delete pResult;
    return 0;
}

LRESULT CBalloonMainPanel::OnV2VoiceDownloadResult(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //lParam 为堆分配的 net::VoiceDownloadResult，广播后须 delete。
    net::VoiceDownloadResult* pResult = (net::VoiceDownloadResult*)lParam;
    if (pResult == NULL)
        return 0;
    OnVoiceDownloadResult(*pResult);
    delete pResult;
    return 0;
}

LRESULT CBalloonMainPanel::OnV2FileTransferAck(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //lParam 为堆分配的 net::FileTransferAckNotify，路由后须 delete。
    net::FileTransferAckNotify* pNotify = (net::FileTransferAckNotify*)lParam;
    if (pNotify == NULL)
        return 0;
    //这一环原先没有日志，出问题时无法判断消息是断在窗口消息映射还是断在主面板，
    //补一条使整条链路（收帧 → 转发 → 主面板 → 聊天窗）每一跳都可追。
    LOG_INFO("路由对方收取文件状态: msgid=%lld, state=%d.",
             (long long)pNotify->m_msgid, pNotify->m_state);
    OnFileTransferAck(*pNotify);
    delete pNotify;
    return 0;
}


//======================== 文件夹传输 ========================

LRESULT CBalloonMainPanel::OnV2FolderInviteAck(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam)
{
    //wParam 为本次邀请请求所用的帧 seq，lParam 为堆分配的 net::FolderInviteResponse，
    //处理后须 delete。
    net::FolderInviteResponse* pResp = (net::FolderInviteResponse*)lParam;
    if (pResp == NULL)
        return 0;

    //管理器按帧 seq 找到发起这笔邀请的那一条记录（应答包体里没有任何可对回的字段，而本机
    //可能有多笔传输同时在等应答），据应答记下服务端分配的会话 id（成功）或就地放开名额
    //（失败），并回填一份"界面要用的定位信息"——气泡此刻还挂着本地占位 id，得靠它换成
    //真会话 id。
    foldertransfer::FolderBindNotice bind;
    foldertransfer::FolderTransferManager::GetInstance().OnInviteAck(
        (uint32_t)wParam, *pResp, bind);
    OnFolderBound(bind);

    delete pResp;
    return 0;
}

LRESULT CBalloonMainPanel::OnV2FolderInviteNotify(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //lParam 为堆分配的 net::FolderInviteNotify，处理后须 delete。
    net::FolderInviteNotify* pNotify = (net::FolderInviteNotify*)lParam;
    if (pNotify == NULL)
        return 0;

    //本机已有一笔文件夹传输在跑时管理器会就地回一个拒绝并返回 false，此时不必出气泡——
    //出了反而让用户以为还能点「接收」。
    if (foldertransfer::FolderTransferManager::GetInstance().OnInviteNotify(*pNotify))
    {
        LOG_INFO("Route folder invite to chat window, session=%s, from=%lld.",
                 pNotify->m_sessionId.c_str(), (long long)pNotify->m_fromId);
        OnFolderInviteNotify(*pNotify);
    }

    delete pNotify;
    return 0;
}

LRESULT CBalloonMainPanel::OnV2FolderSessionAck(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam)
{
    //wParam 为发出的那条命令的协议号（1812 / 1813 / 1814 / 1817），
    //lParam 为堆分配的 net::FolderSessionResponse，处理后须 delete。
    net::FolderSessionResponse* pResp = (net::FolderSessionResponse*)lParam;
    if (pResp == NULL)
        return 0;

    foldertransfer::FolderTransferManager::GetInstance().OnSessionAck((int)wParam, *pResp);

    delete pResp;
    return 0;
}

LRESULT CBalloonMainPanel::OnV2FolderStateNotify(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //lParam 为堆分配的 net::FolderStateNotify，处理后须 delete。
    net::FolderStateNotify* pNotify = (net::FolderStateNotify*)lParam;
    if (pNotify == NULL)
        return 0;

    foldertransfer::FolderTransferManager::GetInstance().OnStateNotify(*pNotify);
    OnFolderStateNotify(*pNotify);

    delete pNotify;
    return 0;
}

LRESULT CBalloonMainPanel::OnV2FolderRelayInfo(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //lParam 为堆分配的 net::FolderRelayInfo，处理后须 delete。
    net::FolderRelayInfo* pInfo = (net::FolderRelayInfo*)lParam;
    if (pInfo == NULL)
        return 0;

    //中转参数一到就建通道开传，界面上气泡随之转成「传输中」。
    foldertransfer::FolderTransferManager::GetInstance().OnRelayInfo(*pInfo);
    OnFolderTransferStarted(pInfo->m_sessionId);

    delete pInfo;
    return 0;
}

LRESULT CBalloonMainPanel::OnV2FolderScanned(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //lParam 为堆分配的 foldertransfer::FolderScanNotice，处理后须 delete。
    foldertransfer::FolderScanNotice* pNotice = (foldertransfer::FolderScanNotice*)lParam;
    if (pNotice == NULL)
        return 0;

    //扫描是本端自己的事，管理器已经在工作线程上处理完了（失败时也已放开名额），
    //这里只负责把结果画到气泡上。
    OnFolderScanned(*pNotice);

    delete pNotice;
    return 0;
}

LRESULT CBalloonMainPanel::OnV2FolderSendProgress(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //lParam 为堆分配的 foldertransfer::FolderProgressNotice，处理后须 delete。
    foldertransfer::FolderProgressNotice* pProg = (foldertransfer::FolderProgressNotice*)lParam;
    if (pProg == NULL)
        return 0;

    OnFolderProgress(*pProg);

    delete pProg;
    return 0;
}

LRESULT CBalloonMainPanel::OnV2FolderSendResult(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //lParam 为堆分配的 foldertransfer::FolderResultNotice，处理后须 delete。
    foldertransfer::FolderResultNotice* pResult = (foldertransfer::FolderResultNotice*)lParam;
    if (pResult == NULL)
        return 0;

    //收尾必须在 UI 线程做：FinishSend 内部要等状态机的工作线程退出，在状态机自己的
    //回调线程里调等于让它 join 自己（见 IFolderSendSessionHandler 的注释）。
    foldertransfer::FolderTransferManager::GetInstance().FinishSend(*pResult);
    OnFolderResult(*pResult);

    delete pResult;
    return 0;
}

LRESULT CBalloonMainPanel::OnV2FolderRecvProgress(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //lParam 为堆分配的 foldertransfer::FolderProgressNotice，处理后须 delete。
    foldertransfer::FolderProgressNotice* pProg = (foldertransfer::FolderProgressNotice*)lParam;
    if (pProg == NULL)
        return 0;

    OnFolderProgress(*pProg);

    delete pProg;
    return 0;
}

LRESULT CBalloonMainPanel::OnV2FolderRecvResult(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //lParam 为堆分配的 foldertransfer::FolderResultNotice，处理后须 delete。
    foldertransfer::FolderResultNotice* pResult = (foldertransfer::FolderResultNotice*)lParam;
    if (pResult == NULL)
        return 0;

    //理由同 OnV2FolderSendResult：FinishRecv 会等接收会话的工作线程退出。
    foldertransfer::FolderTransferManager::GetInstance().FinishRecv(*pResult);
    OnFolderResult(*pResult);

    delete pResult;
    return 0;
}


//======================== 修改密码与头像 ========================

// v2 修改密码应答：转发到主面板（再转发给改密对话框提示结果）
LRESULT CBalloonMainPanel::OnV2ChangePwdResult(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //lParam 为 IUSocket 接收线程堆分配的 net::ChangePasswordResponse，处理后须 delete。
    net::ChangePasswordResponse* pResp = (net::ChangePasswordResponse*)lParam;
    if (pResp == NULL)
        return 0;
    OnChangePwdResult(*pResp);
    delete pResp;
    return 0;
}

// v2 修改头像应答：交修改头像对话框据 code 提示成功/失败，成功则刷新本端头像
LRESULT CBalloonMainPanel::OnV2ChangeAvatarResult(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //lParam 为 IUSocket 接收线程堆分配的 net::UpdateProfileResponse（修改头像应答体为
    //通用 {code,msg}，复用此结构承载），处理后须 delete。
    net::UpdateProfileResponse* pResp = (net::UpdateProfileResponse*)lParam;
    if (pResp == NULL)
        return 0;
    OnChangeAvatarResult(*pResp);
    delete pResp;
    return 0;
}

// v2 头像图片上传结果：交修改头像对话框（成功则接着发 cmd_change_avatar；失败提示）
LRESULT CBalloonMainPanel::OnV2AvatarUploadResult(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //lParam 为 ImageTransfer 工作线程堆分配的 net::AvatarUploadResult，处理后须 delete。
    net::AvatarUploadResult* pResult = (net::AvatarUploadResult*)lParam;
    if (pResult == NULL)
        return 0;
    OnAvatarUploadResult(*pResult);
    delete pResult;
    return 0;
}
