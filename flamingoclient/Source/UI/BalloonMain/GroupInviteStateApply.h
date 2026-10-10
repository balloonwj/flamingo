/**
 *  GroupInviteStateApply.h
 *
 *  入群邀请卡片的状态变化推送（cmd_group_invite_changed_notify=1328，docs/邀请入群方案.md §4.5）推给邀请人与被邀请人的
 *  全部在线设备。主面板收到后要做三件事：定位卡片所在的单聊会话、把本地消息库里那张卡片的 content 列换成新状态、刷新已打开
 *  的聊天窗里那张卡片。本文件提供前两件事用到的函数。
 *
 *  与接龙卡片的做法一致（见 RelayNotifyApply.h）：只改 content 列，原始JSON保持服务端发来的原样，从本地库还原时
 *  以 content 列为准（见 V2MsgQuery.h 的 GroupInviteContentFromRecord）。
 *
 *  从主面板里单独拿出来，是为了让这串步骤能被 FlamingoUnit 对着真的 SQLite 库测试（Tests/GroupInviteCardTests.cpp）；
 *  主面板本身只负责调用它、再刷新界面。
 *
 *  典型用法（主面板收到 1328 时）：
 *      const int64_t peerId = balloonmain::GroupInvitePeerId(notify.m_inviterid, notify.m_inviteeid, selfId);
 *      bool hit = false;
 *      if (peerId > 0)
 *          balloonmain::ApplyGroupInviteStateToStore(m_msgStore, peerId, notify.m_msgid, notify.m_state, hit);
 *
 *  本头文件只依赖标准库与前向声明，可以被任何文件包含。
 *
 *  balloonwj@qq.com   2026-10-04
 */
#pragma once

#include <stdint.h>
#include <string>

namespace balloonhistory { class CV2MsgStore; }

namespace balloonmain {

/**
 *  卡片所在单聊会话的对端：本人是邀请人时为被邀请人，本人是被邀请人时为邀请人。
 *  @param inviterId 邀请人 userid
 *  @param inviteeId 被邀请人 userid
 *  @param selfId    本人 userid
 *  @return 对端 userid；本人两者都不是（推送发错了人）或 id 非正时返回 0
 */
int64_t GroupInvitePeerId(int64_t inviterId, int64_t inviteeId, int64_t selfId);

/**
 *  把卡片的新状态写进本地消息库里那张卡片的 content 列（原始JSON不动）。
 *  @param store  本地消息库；须已打开。调用期间须有效，本函数不保存它
 *  @param peerId 单聊会话的对端 userid（GroupInvitePeerId 的结果），须为正
 *  @param msgid  卡片消息的 msgid，须为正
 *  @param state  新的显示状态，取值见 group_invite_state
 *  @param outHit [出参] 是否命中了本地的行；本地没有这张卡片（本机没同步过这段历史）时为 false
 *  @return 本地没有这张卡片也返回 true（属正常情况，下次同步会带回服务端的最新状态）；
 *          读库失败、卡片内容取不到或写库失败时返回 false
 */
bool ApplyGroupInviteStateToStore(balloonhistory::CV2MsgStore& store, int64_t peerId, int64_t msgid, int state,
                                  bool& outHit);

} // namespace balloonmain
