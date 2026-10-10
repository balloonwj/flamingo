/**
 *  AnnouncementCardApply.h
 *
 *  群公告被删除时，服务端把这条公告的群公告卡片（msgtype=22）改写为已删除形态（去掉正文、带 deleted），并在
 *  cmd_group_announcement_notify（1326，changetype=2）里带上卡片的 msgid 与改写后的 content（docs/群公告接入方案.md §13.3）。
 *  主面板收到后要做两件事：把本地消息库里那张卡片换成已删除形态、刷新已打开的群聊窗里那张卡片。本文件提供前一件事。
 *
 *  与入群邀请卡片的做法不同（见 GroupInviteStateApply.h）：入群邀请卡片只改 content 列；公告卡片的正文必须从本机一并
 *  去掉（公告的删除语义是彻底消失），所以 content 列、原始JSON与搜索文本三处一起改写，其余列（置顶、已听等）原样写回。
 *
 *  从主面板里单独拿出来，是为了让这串步骤能被 FlamingoUnit 对着真的 SQLite 库测试（Tests/AnnouncementCardTests.cpp）。
 *
 *  典型用法（主面板收到 1326 删除推送时）：
 *      bool hit = false;
 *      if (notify.m_cardMsgId > 0)
 *          balloonmain::ApplyAnnouncementCardToStore(m_msgStore, notify.m_groupid, notify.m_cardMsgId,
 *                                                    notify.m_cardContent, hit);
 *
 *  本头文件只依赖标准库与前向声明，可以被任何文件包含。
 *
 *  balloonwj@qq.com   2026-10-05
 */
#pragma once

#include <stdint.h>
#include <string>

namespace balloonhistory { class CV2MsgStore; }

namespace balloonmain {

/**
 *  把改写后的群公告卡片写进本地消息库：content 列、原始JSON里的 content 与搜索文本都换成新内容，其余列不变。
 *  @param store       本地消息库；须已打开。调用期间须有效，本函数不保存它
 *  @param groupId     卡片所在群的 id，须为正
 *  @param msgid       卡片消息的 msgid，须为正
 *  @param cardContent 改写后的卡片 content（紧凑 JSON 对象，1326 推送里的 card）
 *  @param outHit      [出参] 是否命中并改写了本地的行；本地没有这张卡片（本机没同步过这段历史）时为 false
 *  @return 本地没有这张卡片也返回 true（属正常情况，下次同步会带回服务端改写后的卡片）；
 *          参数非法、cardContent 不是 JSON 对象、本地那一行不是群公告卡片、读写库失败时返回 false
 */
bool ApplyAnnouncementCardToStore(balloonhistory::CV2MsgStore& store, int64_t groupId, int64_t msgid,
                                  const std::string& cardContent, bool& outHit);

} // namespace balloonmain
