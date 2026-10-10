/**
 *  PresenceTreeMarks.h
 *
 *  把某个用户的在线状态标记写到主面板树上：头像右下角的在线状态角标（离开、忙碌两种才画，
 *  2026-10-07 起取代行右端的状态徽标，见 MainPanelTreeView::SetItemPresence），以及头像是否画成灰度
 *  （离线用户头像置灰，2026-10-03）。同一个人在树上可能有多个节点 —— 通讯录里部门树一次、
 *  「我的收藏」分区一次，消息列表里与他的单聊会话一次 —— 本文件按主面板维护的
 *  「节点 → 用户」映射逐个命中并写入。
 *
 *  从 CBalloonMainPanel::OnPresenceChanged 中抽出，以便脱离主面板做单元测试
 *  （见 Tests/PresenceTreeMarksTests.cpp）。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL / WTL 与 <windows.h>。
 *
 *  典型用法：
 *      const bool gray = ShouldGrayContactAvatar(userid, presence);
 *      if (balloonmain::ApplyUserPresenceMarks(tv, m_contactNodeUserId, (UINT)userid,
 *                                              presence, gray) > 0)
 *      {
 *          tv->Invalidate();
 *      }
 *
 *  balloonwj@qq.com   2026-10-03
 */

#pragma once

#include <stdint.h>
#include <map>
#include <utility>

#include "MainPanelTreeView.h"

namespace balloonmain {

// ApplyUserPresenceMarks / ApplyConversationPresenceMarks 的 presence 参数取这个值时，只改头像灰度、不动在线状态
// 角标（本人连接态变化时用）。与 OnlineCapability.h 的 kPresenceUnknown 同值：不知道状态时本来也不该改角标。
const int kPresenceMarkKeep = -1;

/**
 *  把 userid 的在线状态标记写到通讯录树上该用户的所有节点。
 *
 *    tv：          目标树；为 NULL 时什么都不做、返回 0。
 *    nodeUserIds： 节点 id → userid 的映射（主面板的 m_contactNodeUserId，部门树与「我的收藏」
 *                  分区里的联系人行都登记在内）。只读。
 *    userid：      要更新的用户。
 *    presence：    该用户的在线状态（presence_type 的取值），写进 MainPanelTreeView::SetItemPresence，
 *                  决定头像右下角画不画角标；传 kPresenceMarkKeep 表示不改角标、只改灰彩
 *                  （本人连接态变化时只需要改灰彩）。
 *    gray：        头像是否画成灰度。
 *
 *  @return 命中的节点个数。调用方据此决定是否重绘；两个写入方法在状态变化时自己会请求重绘，
 *          命中后调用方仍调一次 Invalidate 也无妨。
 */
int ApplyUserPresenceMarks(MainPanelTreeView* tv,
                           const std::map<int, UINT>& nodeUserIds,
                           UINT userid, int presence, bool gray);

/**
 *  把 userid 的在线状态标记写到消息列表里与他的单聊会话行上。群会话行不处理（群没有在线状态）。
 *
 *    tv：      目标树；为 NULL 时什么都不做、返回 0。
 *    nodeConv：节点 id → (会话类型, 会话 id) 的映射（主面板的 m_msgNodeConv）；单聊的会话类型为 1，
 *              会话 id 即对端 userid。只读。
 *    userid：  要更新的用户。
 *    presence：该用户的在线状态，含义同 ApplyUserPresenceMarks；传 kPresenceMarkKeep 表示不改角标。
 *    gray：    头像是否画成灰度。
 *
 *  @return 命中的节点个数，用途同 ApplyUserPresenceMarks。
 */
int ApplyConversationPresenceMarks(MainPanelTreeView* tv,
                                   const std::map<int, std::pair<int, int64_t> >& nodeConv,
                                   int64_t userid, int presence, bool gray);

} // namespace balloonmain
