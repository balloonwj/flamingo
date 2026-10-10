/**
 *  GroupListOps.h
 *
 *  「群」Tab 群列表（std::vector<net::UserGroupItem>，即主面板的 m_myGroups）的
 *  <u>纯操作函数</u>：算出置顶优先的显示顺序、改某个群的置顶状态、查某个群是否置顶。
 *
 *  群的置顶状态有两个来源：一是会话列表（群里有过消息才会有这条会话），二是群列表
 *  应答 cmd_get_user_groups 里每个群带的 pinned（空群的置顶只能从这里得知）。本模块
 *  管的是后者在内存里的那份，群 Tab 据此排序并画置顶样式。
 *
 *  不依赖 HWND、不依赖 balloonui，可被 Flamingo.vcxproj 与 FlamingoUnit.vcxproj 两个
 *  工程逐字共用（见 Tests/GroupListOpsTests.cpp）。在 Flamingo.vcxproj 里本文件需标记
 *  PrecompiledHeader=NotUsing。纯内存操作，不发协议。
 *
 *  典型用法：
 *      std::vector<size_t> order = OrderGroupsForList(m_myGroups);
 *      for (size_t i = 0; i < order.size(); ++i)
 *          AddGroupRow(m_myGroups[order[i]]);
 *
 *  balloonwj@qq.com   2026-09-30
 */

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <vector>

#include "../../net/GroupProtocolClient.h"   // net::UserGroupItem

namespace balloonmain {

/**
 *  算出群 Tab 的显示顺序：置顶的群在前，置顶群之间、非置顶群之间各自保持在 groups 里的
 *  原有顺序。只给出顺序，不改动 groups，所以取消置顶后群会回到原来的位置。
 *    groups：本人加入的群列表。
 *  返回：groups 的下标序列，长度与 groups 相同。纯函数，无副作用。
 */
std::vector<size_t> OrderGroupsForList(const std::vector<net::UserGroupItem>& groups);

/**
 *  修改群列表中某个群的置顶状态（本端切换置顶、或收到另一台设备的同步推送时调用）。
 *    groups： 本人加入的群列表，就地修改。
 *    groupId：目标群ID。
 *    pinned： 目标状态。
 *  返回：状态确实改变返回 true；群不在列表里、或本就是该状态返回 false，调用方据此
 *        决定要不要重填群 Tab。
 */
bool SetUserGroupPinned(std::vector<net::UserGroupItem>& groups, int64_t groupId, bool pinned);

/**
 *  查某个群是否置顶。
 *    groups： 本人加入的群列表。
 *    groupId：目标群ID。
 *  返回：群在列表里且已置顶返回 true，否则返回 false。
 */
bool IsUserGroupPinned(const std::vector<net::UserGroupItem>& groups, int64_t groupId);

/**
 *  把群资料应答里的人数同步进群列表（2026-10-08：「群聊」页的人数取自登录时拉的群列表，别人进出群后
 *  主面板补拉群资料，再经本函数改掉列表里的旧值）。
 *    groups：     本人加入的群列表，就地修改。
 *    groupId：    目标群ID。
 *    memberCount：群资料应答里的人数；小于 0 视为无效，不改动。
 *  返回：人数确实改变返回 true；群不在列表里、人数无效或本就相同返回 false，调用方据此决定要不要重填群 Tab。
 */
bool SetUserGroupMemberCount(std::vector<net::UserGroupItem>& groups, int64_t groupId, int memberCount);

} // namespace balloonmain
