/**
 *  BalloonContactTreeAdapter.h
 *
 *  把 CUserMgr (CBuddyList / CGroupList / CRecentList) 的真实数据
 *  映射到 balloonui DuiTreeView 节点。无状态(全 static),便于在
 *  BalloonMainPanel 各 RefreshXxx 之间复用,也便于将来用 mock 数据
 *  源做单元测试。
 *
 *  本文件遵循 balloonui 头文件约定:包含它之前调用方必须已引入
 *  ATL/WTL (CString) 与 Flamingo stdafx (tstring)。
 *
 *  balloonwj@qq.com   2026-05-26
 */

#pragma once

#include "../../balloonui/balloonui/Controls/List/DuiTreeView.h"

class CUserMgr;

namespace balloonmain {

/**
 *  BalloonContactTreeAdapter
 *
 *  Contacts / Groups / Messages 三个 Tab 的 DuiTreeView 数据装填器。
 *  每个 Fill 都先 Clear 再重新 AddRoot / AddChild,所以可以反复调用
 *  (Refresh 语义)。Adapter 不持有 TreeView 或 CUserMgr,只在 Fill
 *  调用期间引用它们。
 */
class BalloonContactTreeAdapter
{
public:
    /**
     *  【企业版下已成空实现】原先用 mgr->m_BuddyList(v1 好友列表)的分组 + 好友数据填
     *  Contacts 页 TreeView。v1 好友体系已整体删除 —— 通讯录改由服务端组织架构下发,
     *  联系人页由 CBalloonMainPanel 收到 cmd_get_contacts 应答后自行填充,不再经本适配器。
     *  函数保留是为了不改动调用点(CBalloonMainPanel::RefreshContacts),调用它不产生任何效果。
     *    tv:  目标 TreeView;当前实现不使用。
     *    mgr: 数据源;当前实现不使用。
     */
    static void FillContacts(balloonwjui::DuiTreeView* tv, CUserMgr* mgr);

    /**
     *  用 mgr->m_GroupList 的真实群数据填 Groups 页 TreeView。
     *  节点结构:单根 "我的群聊 N",下面挂每个 CGroupInfo。
     */
    static void FillGroups(balloonwjui::DuiTreeView* tv, CUserMgr* mgr);

    /**
     *  用 mgr->m_RecentList 的真实最近会话填 Messages 页 TreeView。
     *  节点结构:扁平 —— 每条会话作为一个顶层根 (无子节点)。
     */
    static void FillMessages(balloonwjui::DuiTreeView* tv, CUserMgr* mgr);

    /**
     *  把 IUProtocolData.h 里的 STATUS_* 在线状态码映射成 DuiTreeView
     *  右侧状态点的颜色 (与 mock 数据 MockStatusColor 颜色保持一致,
     *  让 mock 占位与真实数据视觉无缝切换)。
     *    status: STATUS_ONLINE / STATUS_OFFLINE / ... 见 IUProtocolData.h。
     */
    static COLORREF BuddyStatusColor(long status);
};

} // namespace balloonmain
