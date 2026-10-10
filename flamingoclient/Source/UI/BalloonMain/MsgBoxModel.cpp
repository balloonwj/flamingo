/**
 *  MsgBoxModel.cpp
 *
 *  会话列表里「消息盒子」一行的汇总、行序与挪行定位的实现。纯逻辑，Flamingo.vcxproj 与
 *  FlamingoUnit.vcxproj 两个工程逐字共用；在 Flamingo.vcxproj 里本文件需标记
 *  PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-10-03
 */

#include "MsgBoxModel.h"
#include "../../net/Protocol.h"   // notify_mode_msgbox
#include "../../Common/I18n/I18n.h"   // TR8：「[有人@我] 」前缀的多语言查找

namespace balloonmain {

std::string AtMeDigestPrefix()
{
    return TR8(u8"[有人@我] ");
}

namespace {

// 会话 a 是否比 b 更"新"：时间大者新，时间相同时 cid 小者优先，保证汇总结果稳定
bool IsNewer(const net::ConversationItem& a, const net::ConversationItem& b)
{
    if (a.m_timestamp != b.m_timestamp)
        return a.m_timestamp > b.m_timestamp;
    return a.m_cid < b.m_cid;
}

// 会话是否被收进消息盒子
bool InMsgBox(const net::ConversationItem& conv, const NotifyModeModel& modes)
{
    return modes.GetMode(conv.m_ctype, conv.m_cid) == notify_mode_msgbox;
}

} // anonymous namespace

bool IsMsgBoxRowKey(const ConvNodeKey& key)
{
    return key.first == kMsgBoxRowCtype && key.second == kMsgBoxRowCid;
}

bool StripAtMePrefix(const std::string& digest, std::string& stripped)
{
    const std::string prefix = AtMeDigestPrefix();
    if (digest.compare(0, prefix.size(), prefix) == 0)
    {
        stripped = digest.substr(prefix.size());
        return true;
    }
    stripped = digest;
    return false;
}

MsgBoxSummary ComputeMsgBoxSummary(const std::vector<net::ConversationItem>& convs,
                                   const NotifyModeModel& modes)
{
    MsgBoxSummary sum;
    const net::ConversationItem* newest = NULL;       // 时间最新的被收进会话
    const net::ConversationItem* newestAtMe = NULL;   // 带未读 @ 我的会话里时间最新的
    for (size_t i = 0; i < convs.size(); ++i)
    {
        const net::ConversationItem& c = convs[i];
        if (!InMsgBox(c, modes))
            continue;

        ++sum.m_count;
        if (c.m_unread > 0)
            sum.m_unread += c.m_unread;
        if (c.m_unreadMark)
            sum.m_unreadMarked = true;
        if (newest == NULL || IsNewer(c, *newest))
            newest = &c;

        //摘要带着「[有人@我]」前缀、且还有未读，才算有人 @ 我还没看：点开过（未读清零）的不再突出
        std::string stripped;
        if (c.m_unread > 0 && StripAtMePrefix(c.m_digest, stripped))
        {
            if (newestAtMe == NULL || IsNewer(c, *newestAtMe))
                newestAtMe = &c;
        }
    }
    if (newest == NULL)
        return sum;

    sum.m_timestamp = newest->m_timestamp;
    const net::ConversationItem* pick = (newestAtMe != NULL) ? newestAtMe : newest;
    sum.m_digestCtype = pick->m_ctype;
    sum.m_digestCid   = pick->m_cid;
    sum.m_atMe        = (newestAtMe != NULL);
    StripAtMePrefix(pick->m_digest, sum.m_digest);
    return sum;
}

std::vector<ConvNodeKey> BuildConversationRowOrder(const std::vector<net::ConversationItem>& sorted,
                                                   const NotifyModeModel& modes)
{
    const MsgBoxSummary sum = ComputeMsgBoxSummary(sorted, modes);

    std::vector<ConvNodeKey> order;
    order.reserve(sorted.size() + 1);
    bool boxPlaced = (sum.m_count == 0);   //没有被收进的会话时不需要这一行
    for (size_t i = 0; i < sorted.size(); ++i)
    {
        const net::ConversationItem& c = sorted[i];
        if (InMsgBox(c, modes))
            continue;

        //消息盒子行排在全部置顶会话之后，插在第一个时间早于它的非置顶会话之前
        if (!boxPlaced && c.m_pinned == 0 && c.m_timestamp < sum.m_timestamp)
        {
            order.push_back(ConvNodeKey(kMsgBoxRowCtype, kMsgBoxRowCid));
            boxPlaced = true;
        }
        order.push_back(ConvNodeKey(c.m_ctype, c.m_cid));
    }
    //其余会话都比它新（或都是置顶）：排在最后
    if (!boxPlaced)
        order.push_back(ConvNodeKey(kMsgBoxRowCtype, kMsgBoxRowCid));
    return order;
}

bool FindRowMoveTarget(const std::vector<ConvNodeKey>& order,
                       const std::map<ConvNodeKey, int>& nodeOf,
                       const ConvNodeKey& key,
                       int& outNode, int& outBeforeNode)
{
    outNode = -1;
    outBeforeNode = -1;

    size_t pos = order.size();
    for (size_t i = 0; i < order.size(); ++i)
    {
        if (order[i] == key)
        {
            pos = i;
            break;
        }
    }
    if (pos == order.size())
        return false;   //不在行序里

    std::map<ConvNodeKey, int>::const_iterator self = nodeOf.find(key);
    if (self == nodeOf.end())
        return false;   //它自己的行不存在

    //目标位置：排在它后面那一行之前；它排在最后则挪到末尾
    int before = -1;
    if (pos + 1 < order.size())
    {
        std::map<ConvNodeKey, int>::const_iterator it = nodeOf.find(order[pos + 1]);
        if (it == nodeOf.end())
            return false;   //后面那一行不存在，无从定位
        before = it->second;
    }

    outNode = self->second;
    outBeforeNode = before;
    return true;
}

std::vector<net::ConversationItem> CollectMsgBoxConversations(const std::vector<net::ConversationItem>& sorted,
                                                              const NotifyModeModel& modes)
{
    std::vector<net::ConversationItem> items;
    for (size_t i = 0; i < sorted.size(); ++i)
    {
        if (InMsgBox(sorted[i], modes))
            items.push_back(sorted[i]);
    }
    return items;
}

} // namespace balloonmain
