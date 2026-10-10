/**
 *  ChatGroupInviteCard.h
 *
 *  入群邀请卡片（msgtype=21，「邀请入群需被邀请人同意」开启时服务端在邀请人与被邀请人的单聊里生成）在聊天窗里显示为一张
 *  卡片：标题行（被邀请人一侧「「张三」邀请你加入群聊」，邀请人一侧「你邀请「李四」加入群聊」）、群名（待加入时带成员数）、状态行，
 *  被邀请人一侧在待加入时多一个「加入群聊」按钮。本文件把 content 原文解析成卡片要显示的内容并套用到一条气泡数据上，
 *  状态变化时重填状态与按钮。纯逻辑，不碰绘制，可被 FlamingoUnit 直接测试。卡片的绘制在 ChatBubble 里，与屏幕共享卡片
 *  同一套几何，设计见 docs/邀请入群方案.md §3.3。
 *
 *  典型用法（单聊窗收到或翻到一张入群邀请卡片时）：
 *      balloonchatwnd::ChatMessage msg = MakeRealTextMessage(fromMe, _T("[入群邀请]"), ...);
 *      balloonchatwnd::ApplyGroupInviteCard(msg, contentJson);
 *      list->AppendMessage(msg);
 *  收到 1328 推送时：
 *      balloonchatwnd::ApplyGroupInviteState(msg, notify.m_state);
 *
 *  balloonwj@qq.com   2026-10-04
 */
#pragma once

#include <string>

#include "ChatMockData.h"   // ChatMessage / kMsgGroupInvite

namespace balloonchatwnd {

/**
 *  从卡片 content 里取邀请 id。
 *  @param contentJson content 原文（UTF-8 JSON）
 *  @return 邀请 id；解析失败或没有该字段时返回 0
 */
long long GroupInviteIdOf(const std::string& contentJson);

/**
 *  状态行文字（已按界面语言翻译）。
 *  @param state     显示状态，取值见 group_invite_state
 *  @param isInvitee 是否被邀请人一侧（对方发来的卡片）；邀请人一侧为 false
 *  @return 状态文字；认不出的状态返回空串
 */
CString GroupInviteStatusText(int state, bool isInvitee);

/**
 *  是否显示「加入群聊」按钮：只有被邀请人一侧、且为待加入时显示。
 *  @param state     显示状态，取值见 group_invite_state
 *  @param isInvitee 是否被邀请人一侧
 *  @return 显示返回 true
 */
bool GroupInviteHasButton(int state, bool isInvitee);

/**
 *  按钮文字（已按界面语言翻译）。
 *  @param joining 是否已点过、正等服务端应答
 *  @return 「加入群聊」或「正在加入…」
 */
CString GroupInviteButtonText(bool joining);

/**
 *  把一张入群邀请卡片套用到气泡数据上：kind 改为 kMsgGroupInvite，填标题、副标题、状态与按钮，text 填摘要
 *  （「[入群邀请] 群名」，引用回复、复制等按 text 取摘要的地方用到它）。被邀请人一侧还是邀请人一侧按 msg.fromMe 判断：
 *  卡片的发送者是邀请人。content 解析不了时 inviteId 为 0，标题写「入群邀请」，不显示按钮。
 *  @param msg         [入出参] 气泡数据，fromMe 须已填好
 *  @param contentJson content 原文（服务端下发时已合并状态、成员数与群的当前资料）
 */
void ApplyGroupInviteCard(ChatMessage& msg, const std::string& contentJson);

/**
 *  卡片状态变化（1328 推送）：重填状态与按钮，并清掉「正在加入」。content 原文里的 state 一并改掉，
 *  之后重新套用或落本地库时用的是新状态。
 *  @param msg   [入出参] 气泡数据（kind 须为 kMsgGroupInvite）
 *  @param state 新的显示状态，取值见 group_invite_state
 */
void ApplyGroupInviteState(ChatMessage& msg, int state);

/**
 *  被邀请人点了「加入群聊」、或服务端应答回来：设置 / 清除「正在加入」。
 *  @param msg     [入出参] 气泡数据（kind 须为 kMsgGroupInvite）
 *  @param joining 是否正等服务端应答
 */
void SetGroupInviteJoining(ChatMessage& msg, bool joining);

/**
 *  把 content 原文里的 state 改成新值（本地库保存卡片时用，保证重新打开聊天窗显示的是最新状态）。
 *  @param contentJson content 原文
 *  @param state       新的显示状态
 *  @return 改好的 content；原文解析失败时原样返回
 */
std::string SetGroupInviteContentState(const std::string& contentJson, int state);

//GroupInviteStateForAcceptError 的返回值：这个错误码不改变卡片状态，只恢复按钮
const int kGroupInviteStateUnchanged = -1;

/**
 *  点「加入群聊」失败（1327 应答 code 非 0）后卡片该换成的状态。服务端对已过期、已失效两种情况不推 1328
 *  （状态是查询时算出来的），客户端据应答自己把卡片换成对应状态；已在群中服务端会另推 1328（已加入），这里不改。
 *  @param code 1327 应答的 code
 *  @return 313 返回 group_invite_expired，314 返回 group_invite_invalid，其余返回 kGroupInviteStateUnchanged
 */
int GroupInviteStateForAcceptError(int code);

/**
 *  点「加入群聊」失败时弹给用户的提示（已按界面语言翻译）。
 *  @param code 1327 应答的 code
 *  @return 提示文字；305（已在群中）返回空串，此时不提示、直接打开群聊窗
 */
CString GroupInviteAcceptErrorText(int code);

} // namespace balloonchatwnd
