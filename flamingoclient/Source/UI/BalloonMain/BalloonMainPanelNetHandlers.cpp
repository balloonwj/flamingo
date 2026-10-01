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

#include "../../Common/Log/IULog.h"                    // LOG_INFO / LOG_ERROR
#include "../../Common/Util/EncodeUtil.h"              // UTF-8 转 Unicode（服务端文本为 UTF-8）
#include "../../Session/UserSessionData.h"             // FMG_MSG_* 消息号
#include "../../net/ContactsProtocolClient.h"          // net::ContactsResult
#include "../../net/ConversationsProtocolClient.h"     // net::ConversationsResult / ConversationChangedNotify
#include "../../net/GroupProtocolClient.h"             // net::CreateGroupResponse / GroupInfoResult / GroupMembersResult 等
#include "../../net/GroupFileProtocolClient.h"         // net::GetGroupFilesResult / GroupFilesChangedNotify
#include "../../net/FileTransfer.h"                    // net::GroupFileUploadResult / GroupFileDownloadResult
#include "../../net/ImageTransfer.h"                   // net::AvatarDownloadResult
#include "../../net/FriendProtocolClient.h"            // net::SetFriendRemarkResult
#include "../../net/ContactTagProtocolClient.h"        // net::SetContactTagsResult
#include "../../net/StarredProtocolClient.h"           // net::SetStarredResult
#include "../../net/AccountProtocolClient.h"           // net::ChangePasswordResponse
#include "../../net/MessageProtocolClient.h"           // net::MessageNotify / SendMessageResponse / RecallMessageResponse / TypingNotify 等
#include "../../net/AnnounceProtocolClient.h"          // net::AnnouncementsResult / AnnouncementNotify / AnnounceMarkReadResult
#include "../../net/FolderProtocolClient.h"            // net::FolderInviteResponse / FolderStateNotify / FolderRelayInfo 等
#include "../../net/Protocol.h"                        // error_code_* —— 撤回 / Pin 失败时按错误码给具体原因
#include "../../Feature/FolderTransfer/FolderTransferManager.h" // foldertransfer::FolderTransferManager（文件夹传输会话生命周期）
#include "../../net/ProfileProtocolClient.h"           // net::UserInfoChangedNotify / OrgStructureChangedNotify


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

LRESULT CBalloonMainPanel::OnV2SetStarredResult(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    net::SetStarredResult* pResult = (net::SetStarredResult*)lParam;
    if (pResult == NULL)
    {
        return 0;
    }

    LOG_INFO("MainPanel::OnV2SetStarredResult: code=%d, target=%lld, starred=%d.",
        pResult->m_code, (long long)pResult->m_target, pResult->m_starred);

    //成功时不必动界面（乐观更新已经生效），失败要回滚并提示。
    OnSetStarredResult(*pResult);

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
        OnGroupMembersArrived(*pResult);
    else
        LOG_ERROR("v2 group-members response error, code=%d, msg=%s.",
                  pResult->m_code, pResult->m_msg.c_str());
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

    if (code == 0)
    {
        LOG_INFO("v2 group write-op ok, uiMsg=%u.", uMsg);
        return 0;
    }

    LOG_ERROR("v2 group write-op failed, uiMsg=%u, code=%d, msg=%s.", uMsg, code, msg.c_str());
    ::MessageBox(m_hWnd, _T("操作未能完成，请稍后重试。"), _T("FlamingoIM"),
                 MB_OK | MB_ICONWARNING);
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

// v2 群敏感词库应答：填进群权限页的词库管理
LRESULT CBalloonMainPanel::OnV2SensitiveWords(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    net::SensitiveWordsResult* pResult = (net::SensitiveWordsResult*)lParam;
    if (pResult == NULL)
        return 0;
    OnSensitiveWords(*pResult);
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
        LPCTSTR pszTip = _T("撤回失败，请稍后重试。");
        if (pResp->m_code == error_code_recall_timeout)
            pszTip = _T("该消息发出已超过 2 分钟，无法撤回。");
        else if (pResp->m_code == error_code_recall_not_allowed)
            pszTip = _T("你没有权限撤回这条消息。");
        else if (pResp->m_code == error_code_message_not_exist)
            pszTip = _T("该消息不存在或已被撤回。");

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
        LPCTSTR pszTip = _T("删除失败，请稍后重试。");
        if (pResp->m_code == error_code_message_not_exist)
            pszTip = _T("该消息不存在或已被删除。");

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

// v2 本人删除了消息的推送（单条或区间）：发起删除的这台设备自己也会收到，本机与其他
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
        LPCTSTR pszTip = bPinning ? _T("Pin 失败，请稍后重试。") : _T("取消 Pin 失败，请稍后重试。");
        if (pResp->m_code == error_code_pin_not_allowed)
            pszTip = _T("只有钉住这条消息的人和群主 / 管理员才能取消它。");
        else if (pResp->m_code == error_code_message_not_exist)
            pszTip = _T("该消息不存在或已被撤回。");
        else if (pResp->m_code == error_code_not_group_member)
            pszTip = _T("你已不在该群聊中。");

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


//======================== 平台公告（消息盒子） ========================

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

// v2 平台公告新发布 / 被撤回推送：落本地库、刷铃铛角标、刷消息盒子窗。
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
