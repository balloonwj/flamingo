/**
 *  同一人连续消息的合组判定，见 ChatBubbleGroup.h。
 *
 *  balloonwj@qq.com   2026-10-14
 */
#include "stdafx.h"
#include "ChatBubbleGroup.h"
#include "ChatBubbleStyle.h"   // kGroupMaxGapMs

namespace balloonchatwnd {

namespace {

// 两条消息是否出自同一发送者：同为本人；或同为对方，且 userid 相同（两边都不为 0），或 userid 都为 0 而名字相同。
// 单聊里对方的消息 userid 与名字可能都为空，两条之间同样视为同一人
bool SameSender(const ChatMessage& a, const ChatMessage& b)
{
    if (a.fromMe != b.fromMe)
    {
        return false;
    }
    if (a.fromMe)
    {
        return true;
    }
    if (a.senderUserId != 0 || b.senderUserId != 0)
    {
        return a.senderUserId == b.senderUserId;
    }
    return a.senderName == b.senderName;
}

} // namespace

bool ChatBubblesGroupable(const ChatMessage& prev, const ChatMessage& cur)
{
    //提示行（时间分隔、系统提示、撤回提示）自成一行，前后都断开
    if (prev.kind == kMsgTime || cur.kind == kMsgTime)
    {
        return false;
    }
    if (!SameSender(prev, cur))
    {
        return false;
    }
    //时间未知（还没有回执的回显等）时不合组，宁可多画一次头像
    if (prev.timestampMs <= 0 || cur.timestampMs <= 0)
    {
        return false;
    }
    const long long gap = cur.timestampMs - prev.timestampMs;
    return gap >= 0 && gap <= bubblestyle::kGroupMaxGapMs;
}

} // namespace balloonchatwnd
