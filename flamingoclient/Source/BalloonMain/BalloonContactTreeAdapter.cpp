/**
 *  BalloonContactTreeAdapter.cpp
 *
 *  CUserMgr 真实数据 → DuiTreeView 节点的映射实现。
 *
 *  balloonwj@qq.com   2026-05-26
 */

#include "stdafx.h"
#include "BalloonContactTreeAdapter.h"

#include "../UserMgr.h"                  // CUserMgr / CBuddyList / CGroupList / CRecentList
#include "../UserSessionData.h"          // CBuddyInfo / CBuddyTeamInfo / CGroupInfo / CRecentInfo
#include "../net/IUProtocolData.h"       // STATUS_ONLINE / STATUS_OFFLINE / ...

namespace balloonmain {

namespace {

// 把 UINT64 ms-since-epoch (实际取决于 CRecentInfo::m_MsgTime 单位,
// 见 RecvMsgThread 写入处) 简单格式化成 "HH:MM"。若解析失败返回空串。
//
// 当前实现走 SystemTime 路径:把 m_MsgTime 当成 100ns since 1601 (FILETIME)
// 处理。如果服务器实际单位是 unix epoch 秒,这里会显示成天文级时间;到时
// 再按真实单位改。本切片先把流程跑通,时间格式日后再校准。
CString FormatRecentTime(UINT64 msgTime)
{
    if (msgTime == 0)
    {
        return CString();
    }
    FILETIME ft;
    ft.dwLowDateTime  = static_cast<DWORD>(msgTime & 0xFFFFFFFFULL);
    ft.dwHighDateTime = static_cast<DWORD>((msgTime >> 32) & 0xFFFFFFFFULL);
    SYSTEMTIME st;
    if (!::FileTimeToSystemTime(&ft, &st))
    {
        return CString();
    }
    SYSTEMTIME lt;
    if (!::SystemTimeToTzSpecificLocalTime(NULL, &st, &lt))
    {
        lt = st;
    }
    CString out;
    out.Format(_T("%02d:%02d"), (int)lt.wHour, (int)lt.wMinute);
    return out;
}

} // anonymous namespace


COLORREF BalloonContactTreeAdapter::BuddyStatusColor(long status)
{
    // 颜色与 MainPanelMockData::MockStatusColor 保持一致,让 mock 占位
    // 与真实数据视觉无缝切换。
    switch (status)
    {
    case STATUS_ONLINE:
        return RGB(0x22, 0xC5, 0x5E);   // 在线 绿
    case STATUS_AWAY:
        return RGB(0xF5, 0x9E, 0x0B);   // 离开 橙
    case STATUS_BUSY:
        return RGB(0xEF, 0x44, 0x44);   // 忙碌 红
    case STATUS_INVISIBLE:
        return RGB(0x94, 0xA3, 0xB8);   // 隐身 灰
    case STATUS_OFFLINE:
    default:
        return RGB(0x94, 0xA3, 0xB8);   // 离线 灰
    }
}

void BalloonContactTreeAdapter::FillContacts(balloonwjui::DuiTreeView* tv,
                                             CUserMgr* mgr)
{
    if (tv == NULL)
    {
        return;
    }
    // 真实数据为空时不 Clear —— 保留 InitUi 阶段 FillContactsTree 填的
    // mock 占位,避免登录后(BuddyList 还未到 / 账号无好友)一刷就变空白。
    // 服务器后续真正推送 buddy list 时这里会拿到非空数据,正常 Clear+刷。
    if (mgr == NULL || mgr->m_BuddyList.GetBuddyTeamCount() == 0)
    {
        return;
    }
    tv->Clear();

    CBuddyList& bl = mgr->m_BuddyList;
    int teamCount = bl.GetBuddyTeamCount();
    for (int t = 0; t < teamCount; ++t)
    {
        CBuddyTeamInfo* team = bl.GetBuddyTeamByIndex(t);
        if (team == NULL)
        {
            continue;
        }

        // 根节点:"分组名  在线 / 总"
        CString rootLabel;
        rootLabel.Format(_T("%s  %d / %d"),
                         team->m_strName.c_str(),
                         team->GetOnlineBuddyCount(),
                         team->GetBuddyCount());
        int rootId = tv->AddRoot(rootLabel);
        tv->SetExpanded(rootId, true);

        int buddyCount = team->GetBuddyCount();
        for (int i = 0; i < buddyCount; ++i)
        {
            CBuddyInfo* b = team->GetBuddy(i);
            if (b == NULL)
            {
                continue;
            }

            // 主行:有备注用备注,没备注用昵称,再没有用账号。
            CString display;
            if (!b->m_strMarkName.empty())
            {
                display = b->m_strMarkName.c_str();
            }
            else if (!b->m_strNickName.empty())
            {
                display = b->m_strNickName.c_str();
            }
            else
            {
                display = b->m_strAccount.c_str();
            }
            int id = tv->AddChild(rootId, display);

            // 副行:个性签名 (无则不显示副行)。
            if (!b->m_strSign.empty())
            {
                tv->SetItemSubLabel(id, b->m_strSign.c_str());
            }

            // 右侧状态点。
            tv->SetItemStatusColor(id, BuddyStatusColor(b->m_nStatus));
        }
    }
}

void BalloonContactTreeAdapter::FillGroups(balloonwjui::DuiTreeView* tv,
                                           CUserMgr* mgr)
{
    if (tv == NULL)
    {
        return;
    }
    // 同 FillContacts —— 群数据为空时保留 mock 占位。
    if (mgr == NULL || mgr->m_GroupList.GetGroupCount() == 0)
    {
        return;
    }
    tv->Clear();

    CGroupList& gl = mgr->m_GroupList;
    int groupCount = gl.GetGroupCount();

    // 单根 "我的群聊  N"。
    CString rootLabel;
    rootLabel.Format(_T("我的群聊  %d"), groupCount);
    int rootId = tv->AddRoot(rootLabel);
    tv->SetExpanded(rootId, true);

    for (int i = 0; i < groupCount; ++i)
    {
        CGroupInfo* g = gl.GetGroup(i);
        if (g == NULL)
        {
            continue;
        }
        int id = tv->AddChild(rootId, g->m_strName.c_str());

        // 副行:成员数 "M 人"。
        CString memberText;
        memberText.Format(_T("%d 人"), g->GetMemberCount());
        tv->SetItemSubLabel(id, memberText);
    }
}

void BalloonContactTreeAdapter::FillMessages(balloonwjui::DuiTreeView* tv,
                                             CUserMgr* mgr)
{
    if (tv == NULL)
    {
        return;
    }
    // 同 FillContacts —— 最近会话为空时保留 mock 占位。
    if (mgr == NULL || mgr->m_RecentList.GetRecentCount() == 0)
    {
        return;
    }
    tv->Clear();

    CRecentList& rl = mgr->m_RecentList;
    int recentCount = rl.GetRecentCount();

    for (int i = 0; i < recentCount; ++i)
    {
        CRecentInfo* r = rl.GetRecent(i);
        if (r == NULL)
        {
            continue;
        }

        // 扁平结构 —— 每条会话作为顶层 AddRoot (无子节点)。
        int id = tv->AddRoot(r->m_szName);

        // 副行:末条消息预览。
        if (r->m_szLastMsgText[0] != _T('\0'))
        {
            tv->SetItemSubLabel(id, r->m_szLastMsgText);
        }

        // 右侧时间。
        CString timeText = FormatRecentTime(r->m_MsgTime);
        if (!timeText.IsEmpty())
        {
            tv->SetItemRightText(id, timeText);
        }

        // 单聊带在线状态点;群 / 讨论组不带。CRecentInfo 没存对方状态,
        // 真实场景要回查 m_BuddyList.GetBuddy(uUserID)->m_nStatus。
        // m_nType == 1 视为好友单聊(与服务端协议约定一致),其余为群类。
        if (r->m_nType == 1 && mgr != NULL)
        {
            CBuddyInfo* b = mgr->m_BuddyList.GetBuddy(r->m_uUserID);
            long status = (b != NULL) ? b->m_nStatus : STATUS_OFFLINE;
            tv->SetItemStatusColor(id, BuddyStatusColor(status));
        }
    }
}

} // namespace balloonmain
