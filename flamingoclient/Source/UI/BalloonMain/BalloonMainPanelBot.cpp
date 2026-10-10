/**
 *  BalloonMainPanelBot.cpp
 *
 *  主面板的机器人部分（docs/机器人方案.md §8）：机器人企业开关变化后的处理，系统菜单「我的机器人」是否出现，
 *  登录与登出时对机器人插件宿主（Feature/Bot/BotHost.h）的调用，以及宿主经回调向主面板要的数据：各群最近一次的
 *  2801 结果、重拉 2801、用户的显示名、选择联系人对话框。
 *  2801 / 2820 / 2810 / 2811 的网络应答处理在 BalloonMainPanelNetHandlers.cpp；群聊窗里的显示在 BalloonGroupChatWindow.cpp。
 *
 *  balloonwj@qq.com   2026-10-08
 */
#include "stdafx.h"
#include "BalloonMainPanel.h"

#include "../../App/FlamingoClient.h"          // CFlamingoClient::m_UserMgr（机器人的四项开关、本人资料）
#include "../../Common/Log/IULog.h"            // LOG_INFO
#include "../../Feature/Bot/BotUiBridge.h"     // botui::IsAvailable：FlamingoBot.dll 是否就绪
#include "../../Feature/Bot/BotHost.h"         // balloonbot::CBotHost：机器人管理界面模块的宿主
#include "DeptPickModel.h"                     // balloonmain::DeptNameOf：按部门ID取名称
#include "../../net/IUSocket.h"                // CIUSocket::SendGetGroupBots
#include "../BalloonGroupManage/BalloonGroupManageWindow.h"   // 群管理窗口的「群机器人」页
#include "../BalloonGroupManage/GroupManageModel.h"           // balloongroupmanage::LoadState

void CBalloonMainPanel::OnBotFeaturesChanged()
{
    if (m_lpFMGClient == NULL)
    {
        return;
    }
    const CUserMgr& um = m_lpFMGClient->m_UserMgr;
    //插件宿主随开关加载、停用或刷新；宿主登记的回调见 Feature/Bot/BotUiBridge.h
    botui::NotifyFeatures(um.m_bBotFeature, um.m_bBotGroupFeature, um.m_bBotPersonalFeature, um.m_bBotGroupExternalFeature);
    LOG_INFO("Bot features changed: bot=%d, group=%d, personal=%d, external=%d, module=%d.", um.m_bBotFeature ? 1 : 0,
             um.m_bBotGroupFeature ? 1 : 0, um.m_bBotPersonalFeature ? 1 : 0, um.m_bBotGroupExternalFeature ? 1 : 0,
             botui::IsAvailable() ? 1 : 0);
    //群管理窗口的「群机器人」页随开关出现或消失
    RefreshGroupManageBots(0, true);
}

bool CBalloonMainPanel::IsMyBotsEntryVisible() const
{
    if (m_lpFMGClient == NULL)
    {
        return false;
    }
    const CUserMgr& um = m_lpFMGClient->m_UserMgr;
    return um.m_bBotFeature && um.m_bBotPersonalFeature && botui::IsAvailable();
}

void CBalloonMainPanel::OnBotLoginSucceeded()
{
    if (m_lpFMGClient == NULL)
    {
        return;
    }
    balloonbot::BotHostCallbacks cb;
    cb.m_ctx = this;
    cb.m_queryGroupBots = &CBalloonMainPanel::BotQueryGroupBots;
    cb.m_refreshGroupBots = &CBalloonMainPanel::BotRefreshGroupBots;
    cb.m_userName = &CBalloonMainPanel::BotUserName;
    cb.m_pickContacts = &CBalloonMainPanel::BotPickContacts;
    cb.m_deptName = &CBalloonMainPanel::BotDeptName;
    cb.m_pickDepartments = &CBalloonMainPanel::BotPickDepartments;

    //第一次取宿主时它会登记 botui 的开关变化与群机器人变化回调，此后 1013 经 OnBotFeaturesChanged 直接转给它
    balloonbot::CBotHost& host = balloonbot::CBotHost::GetInstance();
    host.SetCallbacks(cb);
    const CUserMgr& um = m_lpFMGClient->m_UserMgr;
    host.SetFeatures(um.m_bBotFeature, um.m_bBotGroupFeature, um.m_bBotPersonalFeature, um.m_bBotGroupExternalFeature);
    const std::wstring myName = um.m_UserInfo.m_strNickName;
    host.OnLoginSucceeded(static_cast<int64_t>(um.m_UserInfo.m_uUserID), myName);
    LOG_INFO("Bot host notified of login: bot=%d, module=%d.", um.m_bBotFeature ? 1 : 0, host.IsAvailable() ? 1 : 0);
}

void CBalloonMainPanel::OnBotLoggingOut()
{
    balloonbot::CBotHost::GetInstance().OnLogout();
    //选人窗口的结果交给宿主，宿主已经关掉了规则编辑窗；窗口还开着时一并关掉
    if (m_botSenderPickDlg.IsWindow())
    {
        m_botSenderPickDlg.DestroyWindow();
    }
    m_botDeptPickDlg.CloseDialog();
    m_groupBotsCache.clear();
}

void CBalloonMainPanel::CacheGroupBots(const net::GroupBotsResult& result)
{
    if (result.m_groupid <= 0)
    {
        return;
    }
    if (result.m_code != 0)
    {
        //拉取失败：打开着的群管理窗口「群机器人」页改为显示失败提示，不再显示「正在加载」
        CBalloonGroupManageWindow* wnd = GetLiveGroupManageWindow(result.m_groupid);
        if (wnd != NULL && m_lpFMGClient != NULL)
        {
            wnd->SetGroupBots(m_lpFMGClient->m_UserMgr.m_bBotFeature && botui::IsAvailable(),
                              balloongroupmanage::kLoadFailed, std::vector<net::GroupBotInfo>());
        }
        return;
    }
    m_groupBotsCache[result.m_groupid] = result;
    RefreshGroupManageBots(result.m_groupid, false);
}

void CBalloonMainPanel::ForgetGroupBots(int64_t groupId)
{
    if (groupId == 0)
    {
        m_groupBotsCache.clear();
    }
    else
    {
        m_groupBotsCache.erase(groupId);
    }
    //打开着的群管理窗口管的就是这个群（或清空全部）：先显示「正在加载」并重拉 2801
    RefreshGroupManageBots(groupId, true);
}

void CBalloonMainPanel::RefreshGroupManageBots(int64_t groupId, bool requestIfMissing)
{
    CBalloonGroupManageWindow* wnd = GetLiveGroupManageWindow(groupId);
    if (wnd == NULL || m_lpFMGClient == NULL)
    {
        return;
    }
    const int64_t gid = wnd->GetGroupId();
    const bool available = m_lpFMGClient->m_UserMgr.m_bBotFeature && botui::IsAvailable();
    std::map<int64_t, net::GroupBotsResult>::const_iterator it = m_groupBotsCache.find(gid);
    if (it != m_groupBotsCache.end())
    {
        wnd->SetGroupBots(available, balloongroupmanage::kLoadDone, it->second.m_bots);
        return;
    }
    wnd->SetGroupBots(available, balloongroupmanage::kLoadPending, std::vector<net::GroupBotInfo>());
    if (available && requestIfMissing && gid > 0)
    {
        CIUSocket::GetInstance().SendGetGroupBots(gid);
        LOG_INFO("Group bots requested for group manage window, groupid=%lld.", (long long)gid);
    }
}

bool CBalloonMainPanel::BotQueryGroupBots(void* ctx, int64_t groupId, net::GroupBotsResult& out)
{
    CBalloonMainPanel* self = static_cast<CBalloonMainPanel*>(ctx);
    if (self == NULL)
    {
        return false;
    }
    std::map<int64_t, net::GroupBotsResult>::const_iterator it = self->m_groupBotsCache.find(groupId);
    if (it == self->m_groupBotsCache.end())
    {
        return false;
    }
    out = it->second;
    return true;
}

void CBalloonMainPanel::BotRefreshGroupBots(void* ctx, int64_t groupId)
{
    (void)ctx;
    if (groupId <= 0)
    {
        return;
    }
    CIUSocket::GetInstance().SendGetGroupBots(groupId);
}

std::wstring CBalloonMainPanel::BotUserName(void* ctx, int64_t userId)
{
    CBalloonMainPanel* self = static_cast<CBalloonMainPanel*>(ctx);
    if (self == NULL || userId <= 0)
    {
        return std::wstring();
    }
    //与文件共享取显示名同一口径：通讯录显示名、昵称依次取第一个非空的
    return self->GetShareUserDisplayName(userId);
}

std::wstring CBalloonMainPanel::BotDeptName(void* ctx, int64_t deptId)
{
    CBalloonMainPanel* self = static_cast<CBalloonMainPanel*>(ctx);
    if (self == NULL || deptId <= 0)
    {
        return std::wstring();
    }
    return balloonmain::DeptNameOf(self->m_departments, deptId);
}

bool CBalloonMainPanel::BotPickDepartments(void* ctx, HWND owner, const std::vector<int64_t>& preselected,
                                           balloonmain::IDeptPickSink* sink)
{
    CBalloonMainPanel* self = static_cast<CBalloonMainPanel*>(ctx);
    if (self == NULL)
    {
        return false;
    }
    //选部门用的是通讯录缓存里的组织架构；离线或还没加载时没有可选的部门
    if (!self->CheckOnlineFor(balloonmain::kActionAccountOp) || self->m_departments.empty())
    {
        LOG_INFO("Bot department picker not opened: offline or departments not loaded.");
        return false;
    }
    return self->m_botDeptPickDlg.Open((owner != NULL && ::IsWindow(owner)) ? owner : self->m_hWnd, self->m_departments,
                                       preselected, sink);
}

bool CBalloonMainPanel::BotPickContacts(void* ctx, HWND owner, const std::vector<int64_t>& preselected,
                                        IMemberPickSink* sink)
{
    CBalloonMainPanel* self = static_cast<CBalloonMainPanel*>(ctx);
    if (self == NULL || self->m_lpFMGClient == NULL)
    {
        return false;
    }
    //选人用的是通讯录缓存；离线或通讯录还没加载时没有可选的人
    if (!self->CheckOnlineFor(balloonmain::kActionAccountOp) || self->m_contacts.empty())
    {
        LOG_INFO("Bot sender picker not opened: offline or contacts not loaded.");
        return false;
    }
    const UINT selfId = self->m_lpFMGClient->m_UserMgr.m_UserInfo.m_uUserID;
    self->m_botSenderPickDlg.SetInitialSelection(preselected);
    self->m_botSenderPickDlg.SetPickSink(sink);
    self->m_botSenderPickDlg.OpenForPick((owner != NULL && ::IsWindow(owner)) ? owner : self->m_hWnd,
                                         CCreateGroupDlg::kPurposeBotSenders, 0, selfId, self->m_contacts,
                                         self->m_departments);
    return true;
}
