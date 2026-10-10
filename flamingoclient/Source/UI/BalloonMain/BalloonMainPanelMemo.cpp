/**
 *  主面板里与「备忘与提醒」相关的实现：作为 balloonmemo::IMemoHost，向备忘中枢提供群名、本人在群里的
 *  角色、主面板是否可见与群备忘创建人的名字。备忘的网络应答与推送由 BalloonMainPanelNetHandlers.cpp 转给备忘中枢。
 *
 *  这里的函数都是 CBalloonMainPanel 的成员，与 BalloonMainPanel.cpp 同属一个类，只是单独成文，
 *  与 BalloonMainPanelAutoReply.cpp 同一做法。方案见 docs/备忘与提醒方案.md。
 *
 *  balloonwj@qq.com   2026-10-03
 */

#include "stdafx.h"
#include "BalloonMainPanel.h"

#include "../../App/FlamingoClient.h"                  // CFlamingoClient::m_UserMgr（本人 userid）
#include "../../Common/Util/EncodeUtil.h"              // 群名由 UTF-8 转 Unicode
#include "../../net/Protocol.h"                        // group_role_owner
#include "../BalloonChatWnd/BalloonGroupChatWindow.h"  // 已打开的群聊窗里记录的本人角色

std::wstring CBalloonMainPanel::MemoGroupName(int64_t groupid)
{
    for (size_t i = 0; i < m_myGroups.size(); ++i)
    {
        if (m_myGroups[i].m_groupid == groupid)
        {
            return EncodeUtil::Utf8ToUnicode(m_myGroups[i].m_name);
        }
    }
    return std::wstring();
}

int CBalloonMainPanel::MemoGroupRole(int64_t groupid)
{
    const int64_t selfId = (m_lpFMGClient != NULL) ? (int64_t)m_lpFMGClient->m_UserMgr.m_UserInfo.m_uUserID : 0;
    for (size_t i = 0; i < m_myGroups.size(); ++i)
    {
        if (m_myGroups[i].m_groupid == groupid && selfId != 0 && m_myGroups[i].m_ownerid == selfId)
        {
            return group_role_owner;
        }
    }
    //管理员不在群列表里，只有打开过群聊窗、拉过成员列表才知道
    std::map<int64_t, CBalloonGroupChatWindow*>::const_iterator it = m_groupChatWindows.find(groupid);
    if (it != m_groupChatWindows.end() && it->second != NULL && it->second->IsWindow())
    {
        return it->second->GetMyRole();
    }
    return -1;
}

bool CBalloonMainPanel::MemoPanelVisible()
{
    return IsWindow() && IsWindowVisible() && !IsIconic();
}

std::wstring CBalloonMainPanel::MemoUserName(int64_t groupid, int64_t userid)
{
    //该群的聊天窗开着时按它的成员表（群名片优先），否则按通讯录；都查不到时为「用户<id>」
    return std::wstring(ResolveRelayMemberName(groupid, userid).GetString());
}
