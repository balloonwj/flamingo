/**
 *  会话列表的行位置同步：会话排序变化后（收到新消息、置顶、新增会话），算出应当把哪一行
 *  挪到哪一行之前，使会话树的行顺序与排好序的会话列表一致。主面板据此调用
 *  DuiTreeView::MoveRoot 就地挪动那一行，不必整表重建。
 *
 *  2026-10-01 之前每收到一条消息都要整表重建会话树（3000 个会话时约 0.4 ~ 4 秒，
 *  随其它优化逐步下降），挪动一行只要几毫秒。
 *
 *  纯逻辑，只依赖 STL 与 net::ConversationItem，不依赖 balloonui 与 stdafx.h，与
 *  FlamingoUnit 测试工程共用同一份源码（见 Tests/ConversationRowSyncTests.cpp）。
 *
 *  典型用法：
 *      balloonmain::SortConversations(m_conversations);
 *      int node = -1;
 *      int before = -1;
 *      if (balloonmain::FindConversationMoveTarget(m_conversations, m_convNodeId,
 *                                                  ctype, cid, node, before))
 *      {
 *          tv->MoveRoot(node, before);
 *      }
 *      else
 *      {
 *          FillConversationsTree();   // 映射不全等异常情况：退回整表重建
 *      }
 *
 *  balloonwj@qq.com   2026-10-01
 */

#pragma once

#include <stdint.h>
#include <map>
#include <utility>
#include <vector>

#include "../../net/ConversationsProtocolClient.h"   // net::ConversationItem

namespace balloonmain {

// 会话在"会话 → 行节点"映射里的键：(ctype, cid)。
typedef std::pair<int, int64_t> ConvNodeKey;

/**
 *  算出会话 (ctype, cid) 的行应当挪到哪一行之前。
 *    sorted：        已按 ConversationOrderLess 排好序的会话列表（置顶在前、时间新者在前）。
 *    nodeOf：        会话 → 会话树里该会话那一行的节点 id。
 *    ctype / cid：   要调整位置的会话。
 *    outNode：       [出参] 该会话那一行的节点 id。
 *    outBeforeNode： [出参] 应当挪到其前面的那一行的节点 id（排在该会话之后的那个会话的行）；
 *                    该会话排在最后时为 -1，表示挪到末尾。
 *  返回：true 表示两个出参有效，调用方据此 MoveRoot 即可；false 表示该会话不在 sorted
 *        里、映射里缺它的行、或缺排在它后面那个会话的行 —— 此时出参不可用，调用方应退回
 *        整表重建。
 */
bool FindConversationMoveTarget(const std::vector<net::ConversationItem>& sorted,
                                const std::map<ConvNodeKey, int>& nodeOf,
                                int ctype, int64_t cid,
                                int& outNode, int& outBeforeNode);

} // namespace balloonmain
