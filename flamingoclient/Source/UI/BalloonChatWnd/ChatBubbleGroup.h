/**
 *  同一人连续消息的合组判定（docs/聊天气泡美化方案.md 第 1 节第 1 条、第 3 节）。纯逻辑，不碰窗口，可被 FlamingoUnit 直接调用
 *  （用例 [bubblestyle] 的 BS1）。消息列表在排版时对相邻两行调用它，把结果交给 ChatBubble::SetGroupPosition：
 *      const bool withPrev = (i > 0) && ChatBubblesGroupable(rows[i - 1], rows[i]);
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL（ChatMessage 用到 CString）。
 *
 *  balloonwj@qq.com   2026-10-14
 */
#pragma once

#include "ChatMockData.h"   // ChatMessage

namespace balloonchatwnd {

/**
 *  判断两条相邻消息能否合成一组（后一条不再画头像与名字，两条之间的间距收窄）。
 *  两条都不是提示行（kMsgTime），同为本人发出或同为同一位对方发来（对方先比 senderUserId，两边都为 0 时比 senderName；
 *  单聊里对方的消息两者都为空，即视为同一人），两条都有时间戳且后一条比前一条晚 0～3 分钟（含 3 分钟）时返回 true。
 *  卡片与文字之间照常合组。
 *  @param prev 列表里排在前面的那条消息
 *  @param cur  紧随其后的那条消息
 *  @return 能合组返回 true
 */
bool ChatBubblesGroupable(const ChatMessage& prev, const ChatMessage& cur);

} // namespace balloonchatwnd
