/**
 *  帖子群不可用时从各处列表里隐藏它（纯函数，docs/帖子群方案.md 4.7、7.2，用例 TGE3）：
 *    - 先按群类型表与「功能是否可用」算出这一刻要隐藏的群号集合（功能可用时为空集）；
 *    - 再用这个集合过滤我的群列表、会话列表，或判断单个会话要不要隐藏。
 *  主面板的群列表、会话列表与消息盒子、未读角标、托盘闪烁、转发对象、全局搜索、收藏、提醒批量管理、聊天记录管理器
 *  都用这里过滤后的结果，原始数据（m_myGroups、m_conversations）不动：已读、置顶、群文件等仍要用到它们，
 *  功能重新可用时帖子群也要原样回来。
 *
 *  典型用法：
 *      const std::set<int64_t> hidden = balloonthread::HiddenThreadGroupIds(groupTypes, host.IsAvailable());
 *      std::vector<net::ConversationItem> rows = balloonthread::FilterConversations(m_conversations, hidden);
 *
 *  balloonwj@qq.com   2026-10-07
 */
#pragma once

#include <stdint.h>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "../../net/ConversationsProtocolClient.h"   // net::ConversationItem
#include "../../net/GroupProtocolClient.h"           // net::UserGroupItem

namespace balloonthread {

/**
 *  这一刻要隐藏的群号。
 *  @param groupTypes 群号 → 群类型（主面板的群信息缓存里记下的，取值见 group_type）
 *  @param available  帖子群功能是否可用（CThreadGroupHost::IsAvailable）
 *  @return 功能不可用时为全部帖子群的群号；可用时为空集
 */
std::set<int64_t> HiddenThreadGroupIds(const std::map<int64_t, int>& groupTypes, bool available);

/**
 *  过滤我的群列表：去掉要隐藏的群，其余保持原顺序。
 *  @param groups 群列表
 *  @param hidden 要隐藏的群号（HiddenThreadGroupIds 的结果）
 */
std::vector<net::UserGroupItem> FilterGroups(const std::vector<net::UserGroupItem>& groups, const std::set<int64_t>& hidden);

/**
 *  一个会话要不要隐藏：只有群会话且群号在隐藏集合里时才隐藏，单聊总是显示。
 *  @param ctype  会话类型，取值见 conversation_type
 *  @param cid    会话 ID（群会话为群号）
 *  @param hidden 要隐藏的群号
 */
bool IsHiddenConversation(int ctype, int64_t cid, const std::set<int64_t>& hidden);

/**
 *  过滤会话列表：去掉要隐藏的群会话，其余保持原顺序。
 *  @param conversations 会话列表
 *  @param hidden        要隐藏的群号
 */
std::vector<net::ConversationItem> FilterConversations(const std::vector<net::ConversationItem>& conversations,
                                                       const std::set<int64_t>& hidden);

/**
 *  帖子群里 @ 一个人时用的显示名（docs/帖子群方案.md 14.1 的 P1）：群名片，没有时为昵称，与普通群群聊窗的成员列表、
 *  @ 浮层同一口径（BalloonGroupChatWindow.cpp 的 OnGroupMembersArrived）。不含本人给对方起的备注：@ 的文字会随正文
 *  发给全群，备注只是本人看的记号。
 *  @param cardUtf8     群名片（UTF-8），可为空
 *  @param nicknameUtf8 账号昵称（UTF-8），可为空
 *  @return 显示名；两者都为空时返回空串，由调用方兜底
 */
std::wstring ThreadMentionName(const std::string& cardUtf8, const std::string& nicknameUtf8);

/**
 *  会话摘要开头的标记有多长（docs/帖子群方案.md 14.1 的 P2）：摘要以 tags 里的某一个开头时返回它的长度，否则返回 0。
 *  帖子卡片的摘要在显示时要在前面加发送者名字，「[有人@我] 」「[关注] 」这类标记须留在名字之前，主面板据此把摘要
 *  拆成「标记 + 其余」再拼名字。tags 里有互为前缀的标记时取最长的。
 *  @param digest 会话摘要
 *  @param tags   可能出现的标记（含结尾空格），按界面语言翻译好的
 */
size_t DigestTagLength(const std::wstring& digest, const std::vector<std::wstring>& tags);

} // namespace balloonthread
