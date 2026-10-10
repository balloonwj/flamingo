/**
 *  MsgBoxModel.h
 *
 *  会话列表里「消息盒子」一行的纯逻辑（docs/消息提醒模式与消息盒子方案.md §4.4、§7.4）：
 *    · 提醒模式为「收进消息盒子且不提醒」的会话不单独出现在会话列表里，汇总成一行「消息盒子」；
 *    · 这一行的摘要、时间、未读由被收进的各会话汇总得出（ComputeMsgBoxSummary）；
 *    · 会话列表的行序（BuildConversationRowOrder）：去掉被收进的会话，把消息盒子行按它的时间
 *      插在置顶会话之后、与普通会话按时间混排；
 *    · 收到消息后就地挪动某一行时，按行序找它该挪到哪一行之前（FindRowMoveTarget）；
 *    · 消息盒子窗口里列出的会话（CollectMsgBoxConversations）。
 *
 *  只依赖 STL、net::ConversationItem 与 NotifyModeModel，不依赖 balloonui 与 stdafx.h，可被
 *  Flamingo.vcxproj 与 FlamingoUnit.vcxproj 两个工程逐字共用；在 Flamingo.vcxproj 里本文件需
 *  标记 PrecompiledHeader=NotUsing。
 *
 *  典型用法（CBalloonMainPanel::FillConversationsTree）：
 *      std::vector<ConvNodeKey> order = BuildConversationRowOrder(m_conversations, m_notifyModes);
 *      for (size_t i = 0; i < order.size(); ++i)
 *      {
 *          if (IsMsgBoxRowKey(order[i]))
 *              AddMsgBoxNode(tv, ComputeMsgBoxSummary(m_conversations, m_notifyModes));
 *          else
 *              AddConversationNode(tv, *FindConversation(m_conversations, order[i].first, order[i].second));
 *      }
 *
 *  balloonwj@qq.com   2026-10-03
 */

#pragma once

#include <stdint.h>
#include <map>
#include <string>
#include <vector>

#include "ConversationRowSync.h"   // ConvNodeKey
#include "NotifyModeModel.h"

namespace balloonmain {

// 「消息盒子」行在会话树两张映射表（节点 → 会话、会话 → 节点）里的保留键。ctype 取 99：
// 协议里的会话类型只有 1 单聊、2 群聊，99 不会与真实会话撞上；cid 取 0。仅客户端内部使用，
// 不会出现在任何协议报文里。
const int     kMsgBoxRowCtype = 99;
const int64_t kMsgBoxRowCid   = 0;

/**
 *  群消息被 @ 时主面板给会话摘要加的前缀：「[有人@我] 」在当前界面语言下的译文（UTF-8，多语言第 5 期起经 TR8）。
 *  CBalloonMainPanel 收到群消息时用它加前缀，消息盒子行用它判断被收进的会话里有没有 @ 我的消息；两处取的是
 *  同一个函数，界面语言在一次运行里不变，所以加上去的与识别用的总是同一个串。
 *  @return 前缀（UTF-8，末尾带一个空格）
 */
std::string AtMeDigestPrefix();

/**
 *  MsgBoxSummary —— 「消息盒子」一行要显示的汇总数据。
 */
struct MsgBoxSummary
{
    int         m_count;        // 被收进消息盒子的会话数；为 0 时会话列表里不出现这一行
    int         m_unread;       // 被收进的各会话的未读之和（灰色胶囊显示，不计入页签红色总数）
    int64_t     m_timestamp;    // 被收进的会话里最新一条消息的时间（Unix 毫秒）；决定这一行的排序与右上角时间
    int         m_digestCtype;  // 摘要取自哪个会话：会话类型（1 单聊 2 群聊）；m_count 为 0 时为 0
    int64_t     m_digestCid;    // 摘要取自哪个会话：会话 ID；m_count 为 0 时为 0
    std::string m_digest;       // 该会话的摘要原文（UTF-8），已去掉「[有人@我] 」前缀；显示前由调用方拼上会话名
    bool        m_atMe;         // 摘要所取的会话是否带着未读的 @ 我：为 true 时显示前加「[有人@我]」
    bool        m_unreadMarked; // 被收进的会话里有没有被手动标为未读的（docs/会话标为未读方案.md）：
                                // 为 true 且 m_unread 为 0 时这一行画灰色小红点

    MsgBoxSummary()
        : m_count(0)
        , m_unread(0)
        , m_timestamp(0)
        , m_digestCtype(0)
        , m_digestCid(0)
        , m_atMe(false)
        , m_unreadMarked(false)
    {
    }
};

/**
 *  判断会话树映射表里的一个键是不是「消息盒子」行。
 */
bool IsMsgBoxRowKey(const ConvNodeKey& key);

/**
 *  摘要是否以「[有人@我] 」开头；是则在 stripped 里给出去掉前缀后的部分。
 *    digest：   会话摘要（UTF-8）。
 *    stripped： [出参] 去掉前缀后的摘要；不以前缀开头时等于 digest。
 *  返回：以前缀开头时为 true。
 */
bool StripAtMePrefix(const std::string& digest, std::string& stripped);

/**
 *  汇总被收进消息盒子的会话。纯函数。
 *    convs： 主面板的会话缓存（任意顺序）。
 *    modes： 提醒模式表。
 *  返回：汇总结果。摘要的取法：被收进的会话里有带未读的 @ 我时，取其中最新的那一个并置
 *        m_atMe；否则取时间最新的那一个。时间相同时取 cid 较小的，保证结果稳定。
 */
MsgBoxSummary ComputeMsgBoxSummary(const std::vector<net::ConversationItem>& convs,
                                   const NotifyModeModel& modes);

/**
 *  会话列表（「消息」页签）的行序。纯函数。
 *    sorted： 已按 ConversationOrderLess 排好序的会话缓存（置顶在前、时间新者在前）。
 *    modes：  提醒模式表。
 *  返回：自上而下每一行的键。被收进消息盒子的会话不在其中（置顶的也一样）；有被收进的会话时
 *        多出一个消息盒子行的键（IsMsgBoxRowKey），位置在全部置顶会话之后，排在第一个时间
 *        早于它的非置顶会话之前；时间相同时普通会话在前。
 */
std::vector<ConvNodeKey> BuildConversationRowOrder(const std::vector<net::ConversationItem>& sorted,
                                                   const NotifyModeModel& modes);

/**
 *  按行序算出某一行应当挪到哪一行之前（FindConversationMoveTarget 的通用版，行序里可以有
 *  消息盒子行）。纯函数。
 *    order：         BuildConversationRowOrder 的结果。
 *    nodeOf：        行键 → 会话树里那一行的节点 id。
 *    key：           要调整位置的行。
 *    outNode：       [出参] 该行的节点 id。
 *    outBeforeNode： [出参] 应当挪到其前面的那一行的节点 id；该行排在最后时为 -1，表示挪到末尾。
 *  返回：true 表示两个出参有效；false 表示 key 不在行序里、映射里缺它的行、或缺排在它后面
 *        那一行 —— 调用方应退回整表重建。
 */
bool FindRowMoveTarget(const std::vector<ConvNodeKey>& order,
                       const std::map<ConvNodeKey, int>& nodeOf,
                       const ConvNodeKey& key,
                       int& outNode, int& outBeforeNode);

/**
 *  消息盒子窗口里列出的会话：提醒模式为「收进消息盒子且不提醒」的会话，保持 sorted 的顺序
 *  （置顶在前，其余按时间倒序）。纯函数。
 *    sorted： 已排好序的会话缓存。
 *    modes：  提醒模式表。
 *  返回：会话的拷贝（值语义，调用方拥有）。
 */
std::vector<net::ConversationItem> CollectMsgBoxConversations(const std::vector<net::ConversationItem>& sorted,
                                                              const NotifyModeModel& modes);

} // namespace balloonmain
