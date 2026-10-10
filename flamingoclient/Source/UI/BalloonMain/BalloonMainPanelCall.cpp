/**
 *  主面板与音视频通话相关的部分（docs/音视频通话优化方案.md）：给 FlamingoMedia.dll 取群头像、记下本人的通话状态并在会话、
 *  通讯录（含「我的收藏」分区）、群列表里画「通话中」徽章、主菜单「发起语音 / 视频通话」。从 BalloonMainPanel.cpp
 *  拆出来单独成文件，与 BalloonMainPanelForward.cpp 同样的做法。
 *
 *  balloonwj@qq.com   2026-10-05
 */
#include "stdafx.h"
#include "BalloonMainPanel.h"

#include <utility>
#include <vector>

#include "../../App/FlamingoClient.h"          // CFlamingoClient::m_UserMgr（群通话功能开关）
#include "../../Common/Log/IULog.h"            // LOG_INFO
#include "../../Common/I18n/I18n.h"            // TR：界面文字的多语言查找
#include "../../Feature/GroupCall/GroupCallHub.h"   // 群通话：已有通话时加入、选好成员后发起
#include "../../net/IUSocket.h"                // CIUSocket::RequestGroupMembers
#include "../../net/Protocol.h"                // conversation_single / conversation_group
#include "AvatarSpec.h"                        // DecodeAvatar：群头像与群列表同一口径
#include "BalloonMainLayout.h"                 // kIdMessagesTree / kIdContactsTree / kIdGroupsTree
#include "ForwardRequest.h"                    // kForwardCallVoice / kForwardCallVideo
#include "GroupExitFlow.h"                     // BuildTransferCandidates：群成员去掉本人后转成选人候选
#include "MainPanelTreeView.h"                 // SetItemInCall：「通话中」徽章

namespace {

//群通话一次最多呼叫几个人：服务端人数上限默认 16（含发起人，docs/协议设计-v2.md 21.7），与群聊窗同一个数
const size_t kCallPickMaxInvitees = 15;

//UTF-8 → CString（群名等资料为 UTF-8）
CString Utf8ToCString(const std::string& utf8)
{
    if (utf8.empty())
    {
        return CString();
    }
    const int n = ::MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(), NULL, 0);
    if (n <= 0)
    {
        return CString();
    }
    CString out;
    ::MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(), out.GetBuffer(n), n);
    out.ReleaseBuffer(n);
    return out;
}

} // namespace

bool CBalloonMainPanel::GetGroupAvatarForMedia(int64_t groupId, balloonfileshare::ShareUserAvatar& out)
{
    for (size_t i = 0; i < m_myGroups.size(); ++i)
    {
        const net::UserGroupItem& g = m_myGroups[i];
        if (g.m_groupid != groupId)
        {
            continue;
        }
        //与群列表、转发选择窗的群行同一口径：群设置的字形或照片，没设置过时群名首字
        const balloonmain::AvatarSpec spec =
            balloonmain::DecodeAvatar(g.m_facetype, g.m_customface, Utf8ToCString(g.m_name), groupId);
        out.m_glyph = spec.m_glyph.GetString();
        out.m_color = spec.m_color;
        out.m_imagePath.clear();
        if (spec.m_kind == balloonmain::avatar_kind_image)
        {
            //本地没有时 ResolveLocalAvatarImage 安排下载，完成后经 OnAvatarsUpdated 通知媒体模块
            out.m_imagePath = ResolveLocalAvatarImage(g.m_customface);
        }
        return true;
    }
    return false;
}

void CBalloonMainPanel::OnCallPresence(int ctype, int64_t cid, int media, bool inCall)
{
    if (cid <= 0 || (ctype != conversation_single && ctype != conversation_group))
    {
        return;
    }
    const std::pair<int, int64_t> key(ctype, cid);
    if (inCall)
    {
        m_inCallConversations[key] = (media == balloonmain::kForwardCallVideo) ? balloonmain::kForwardCallVideo
                                                                               : balloonmain::kForwardCallVoice;
    }
    else
    {
        m_inCallConversations.erase(key);
    }
    LOG_INFO("call presence applied to panel: ctype=%d, cid=%lld, media=%d, incall=%d, total=%u.", ctype,
             (long long)cid, media, inCall ? 1 : 0, (unsigned)m_inCallConversations.size());
    ApplyInCallMarks();
    //已打开的单聊窗里还没结束的通话卡片随之写「通话中」或恢复「正在呼叫」
    if (ctype == conversation_single)
    {
        std::map<UINT, CBalloonChatWindow*>::iterator it = m_chatWindows.find((UINT)cid);
        if (it != m_chatWindows.end() && it->second != NULL && it->second->IsWindow())
        {
            it->second->SetInCallMedia(InCallMediaOf(conversation_single, cid));
        }
    }
}

int CBalloonMainPanel::InCallMediaOf(int ctype, int64_t cid) const
{
    std::map<std::pair<int, int64_t>, int>::const_iterator it =
        m_inCallConversations.find(std::make_pair(ctype, cid));
    return (it != m_inCallConversations.end()) ? it->second : 0;
}

void CBalloonMainPanel::ApplyInCallMarks()
{
    //没有通话、列表里也没画着徽章：没有要改的（列表重建时走到这里，几千行的通讯录不必逐行检查）
    if (m_inCallConversations.empty() && !m_inCallMarksShown)
    {
        return;
    }
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }

    //会话树：单聊与群聊会话，按 (ctype, cid) 匹配
    balloonmain::MainPanelTreeView* mtv =
        dynamic_cast<balloonmain::MainPanelTreeView*>(content->FindCtrlById(balloonmain::kIdMessagesTree));
    if (mtv != NULL)
    {
        for (std::map<int, std::pair<int, int64_t> >::const_iterator it = m_msgNodeConv.begin();
             it != m_msgNodeConv.end(); ++it)
        {
            mtv->SetItemInCall(it->first, InCallMediaOf(it->second.first, it->second.second));
        }
    }

    //通讯录树：组织架构与「我的收藏」分区里的联系人行，以及收藏分区里的群行
    balloonmain::MainPanelTreeView* ctv =
        dynamic_cast<balloonmain::MainPanelTreeView*>(content->FindCtrlById(balloonmain::kIdContactsTree));
    if (ctv != NULL)
    {
        for (std::map<int, UINT>::const_iterator it = m_contactNodeUserId.begin(); it != m_contactNodeUserId.end(); ++it)
        {
            ctv->SetItemInCall(it->first, InCallMediaOf(conversation_single, (int64_t)it->second));
        }
        for (std::map<int, int64_t>::const_iterator it = m_favoriteGroupNodeId.begin();
             it != m_favoriteGroupNodeId.end(); ++it)
        {
            ctv->SetItemInCall(it->first, InCallMediaOf(conversation_group, it->second));
        }
    }

    //群树
    balloonmain::MainPanelTreeView* gtv =
        dynamic_cast<balloonmain::MainPanelTreeView*>(content->FindCtrlById(balloonmain::kIdGroupsTree));
    if (gtv != NULL)
    {
        for (std::map<int, int64_t>::const_iterator it = m_groupNodeId.begin(); it != m_groupNodeId.end(); ++it)
        {
            gtv->SetItemInCall(it->first, InCallMediaOf(conversation_group, it->second));
        }
    }
    m_inCallMarksShown = !m_inCallConversations.empty();
}

void CBalloonMainPanel::OpenStartCallPicker(int media)
{
    if (!CheckOnlineFor(balloonmain::kActionRealtimeCall))
    {
        return;
    }
    if (m_pMediaModule == NULL)
    {
        ::MessageBox(m_hWnd, TR(L"音视频组件未能加载，暂时无法发起通话。"), g_strAppTitle.c_str(), MB_OK | MB_ICONWARNING);
        return;
    }
    balloonmain::ForwardRequest request;
    request.m_callMedia = (media == balloonmain::kForwardCallVideo) ? balloonmain::kForwardCallVideo
                                                                    : balloonmain::kForwardCallVoice;
    LOG_INFO("start call picker opened from the system menu, media=%d.", request.m_callMedia);
    OpenForwardPicker(request);
}

void CBalloonMainPanel::StartCallToConversation(int ctype, int64_t cid, int media)
{
    if (cid <= 0 || !CheckOnlineFor(balloonmain::kActionRealtimeCall))
    {
        return;
    }
    const bool video = (media == balloonmain::kForwardCallVideo);
    LOG_INFO("start call from the system menu: ctype=%d, cid=%lld, video=%d.", ctype, (long long)cid, video ? 1 : 0);
    if (ctype == conversation_single)
    {
        //与单聊窗、联系人右键菜单同一条路：交给 FlamingoMedia.dll 呼叫（显示名由它释放）
        StartCallViaMediaModule((UINT)cid, new std::wstring(ContactDisplayName((UINT)cid).GetString()), video);
        return;
    }
    if (ctype != conversation_group)
    {
        return;
    }

    balloongroupcall::CGroupCallHub& hub = balloongroupcall::CGroupCallHub::GetInstance();
    if (!hub.IsModuleReady() || m_lpFMGClient == NULL || !m_lpFMGClient->m_UserMgr.m_bGroupCallFeature)
    {
        ::MessageBox(m_hWnd, TR(L"本企业没有开通群语音 / 群视频通话，暂时无法在群里发起通话。"), TR(L"群通话"),
                     MB_OK | MB_ICONINFORMATION);
        return;
    }
    //本群已有进行中的群通话：一个群同时只有一场，直接加入那一场
    balloongroupcall::GroupCallActiveInfo active;
    if (hub.GetActive(cid, active) && !active.m_roomId.empty())
    {
        LOG_INFO("group call already active, join instead of start. group=%lld, room=%s.", (long long)cid,
                 active.m_roomId.c_str());
        hub.JoinCall(cid, GroupDisplayName(cid), active.m_roomId);
        return;
    }
    //主面板不缓存群成员：现拉一次，到达后在 OnCallPickMembersArrived 里弹出成员选择框
    m_callPickGroupId = cid;
    m_callPickMedia = video ? balloongroupcall::kGroupCallVideo : balloongroupcall::kGroupCallVoice;
    CIUSocket::GetInstance().RequestGroupMembers(cid);
    LOG_INFO("start group call from the system menu: fetching members, group=%lld, media=%d.", (long long)cid,
             m_callPickMedia);
}

void CBalloonMainPanel::OnCallPickMembersArrived(const net::GroupMembersResult& members)
{
    if (m_callPickGroupId == 0 || members.m_groupid != m_callPickGroupId)
    {
        return;
    }
    m_callPickGroupId = 0;
    if (members.m_code != 0)
    {
        ::MessageBox(m_hWnd, TR(L"未能获取群成员，请稍后重试。"), TR(L"群通话"), MB_OK | MB_ICONWARNING);
        return;
    }
    const std::vector<net::ContactItem> candidates =
        balloonmain::BuildTransferCandidates(members.m_members, (int64_t)SelfUserId());
    if (candidates.empty())
    {
        ::MessageBox(m_hWnd, TR(L"群里没有其他成员，无法发起群通话。"), TR(L"群通话"), MB_OK | MB_ICONINFORMATION);
        return;
    }
    m_callPickOpenGroupId = members.m_groupid;
    m_callPickDlg.SetPickSink(this);
    m_callPickDlg.OpenForPick(m_hWnd, CCreateGroupDlg::kPurposeGroupCall, members.m_groupid, SelfUserId(), candidates,
                              std::vector<net::DepartmentItem>());
    LOG_INFO("start group call from the system menu: member picker opened, group=%lld, candidates=%u.",
             (long long)members.m_groupid, (unsigned)candidates.size());
}

void CBalloonMainPanel::OnMembersPicked(const std::vector<int64_t>& userIds)
{
    const int64_t groupId = m_callPickOpenGroupId;
    m_callPickOpenGroupId = 0;
    if (groupId <= 0 || userIds.empty())
    {
        return;
    }
    if (userIds.size() > kCallPickMaxInvitees)
    {
        //人数上限含本人，所以提示里的数是可呼叫人数加一
        const std::wstring tip = TRF(L"群通话最多 {0} 人，请少选几位成员。", (int)(kCallPickMaxInvitees + 1));
        ::MessageBox(m_hWnd, tip.c_str(), TR(L"群通话"), MB_OK | MB_ICONINFORMATION);
        return;
    }
    const bool ok = balloongroupcall::CGroupCallHub::GetInstance().StartCall(groupId, GroupDisplayName(groupId),
                                                                            m_callPickMedia, userIds);
    LOG_INFO("start group call from the system menu: members picked, group=%lld, media=%d, count=%u, ok=%d.",
             (long long)groupId, m_callPickMedia, (unsigned)userIds.size(), ok ? 1 : 0);
}
