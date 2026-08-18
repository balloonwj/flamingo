/**
 *  BalloonContactTreeAdapter.cpp
 *
 *  CUserMgr 真实数据 → DuiTreeView 节点的映射实现。
 *
 *  balloonwj@qq.com   2026-05-26
 */

#include "stdafx.h"
#include "BalloonContactTreeAdapter.h"

#include "../../Session/UserMgr.h"                  // CUserMgr / CBuddyList / CGroupList / CRecentList
#include "../../Session/UserSessionData.h"          // CBuddyInfo / CBuddyTeamInfo / CGroupInfo / CRecentInfo
#include "../../net/IUProtocolData.h"       // STATUS_ONLINE / STATUS_OFFLINE / ...

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
    case STATUS_OFFLINE:
    default:
        return RGB(0x94, 0xA3, 0xB8);   // 离线 灰
    }
}

void BalloonContactTreeAdapter::FillContacts(balloonwjui::DuiTreeView* tv,
                                             CUserMgr* /*mgr*/)
{
    // 企业版：数据源 CUserMgr::m_BuddyList(v1 好友列表)已整体删除 —— 通讯录改由服务端
    // 组织架构下发，联系人页由 CBalloonMainPanel::OnV2Contacts 收到 cmd_get_contacts
    // 应答后自行填充(见其 FillContactsFromResult / m_contacts)，不再经本适配器。
    //
    // 本函数保留为空实现而不是删掉：调用点在 CBalloonMainPanel::RefreshContacts 里，
    // 那个文件当前由他人维护，删函数会连带改到那里。空实现的语义与改造前一致 ——
    // 原实现在"好友分组数为 0"时就直接 return（保留界面已有内容不清空）。
    (void)tv;
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

        // 单聊带在线状态点;群 / 讨论组不带。CRecentInfo 本身没存对方状态,原先回查
        // v1 好友列表(m_BuddyList)取 m_nStatus —— 该列表已随 v1 好友体系删除,这里
        // 一律按离线灰显示。真实在线状态走 v2 推送(cmd_presence_changed_notify),
        // 由 CBalloonMainPanel 自己刷会话列表的状态点。
        // m_nType == 1 视为单聊(与服务端协议约定一致),其余为群类。
        if (r->m_nType == 1)
        {
            tv->SetItemStatusColor(id, BuddyStatusColor(STATUS_OFFLINE));
        }
    }
}

} // namespace balloonmain
