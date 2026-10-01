/**
 *  会话列表行位置同步的实现。整体说明见 ConversationRowSync.h。
 *
 *  本 .cpp 不 include Flamingo 的 stdafx.h —— 它要被 Flamingo.vcxproj 与
 *  FlamingoUnit.vcxproj 共用。在 Flamingo.vcxproj 里需标记 PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-10-01
 */

#include "ConversationRowSync.h"

namespace balloonmain {

bool FindConversationMoveTarget(const std::vector<net::ConversationItem>& sorted,
                                const std::map<ConvNodeKey, int>& nodeOf,
                                int ctype, int64_t cid,
                                int& outNode, int& outBeforeNode)
{
    outNode = -1;
    outBeforeNode = -1;

    //它在排好序的列表里排第几
    size_t pos = sorted.size();
    for (size_t i = 0; i < sorted.size(); ++i)
    {
        if (sorted[i].m_ctype == ctype && sorted[i].m_cid == cid)
        {
            pos = i;
            break;
        }
    }
    if (pos == sorted.size())
    {
        return false;   //不在列表里
    }

    std::map<ConvNodeKey, int>::const_iterator self = nodeOf.find(ConvNodeKey(ctype, cid));
    if (self == nodeOf.end())
    {
        return false;   //它自己的行不存在
    }

    //目标位置：排在它后面那个会话的行之前；它排在最后则挪到末尾
    int before = -1;
    if (pos + 1 < sorted.size())
    {
        const net::ConversationItem& next = sorted[pos + 1];
        std::map<ConvNodeKey, int>::const_iterator it = nodeOf.find(ConvNodeKey(next.m_ctype, next.m_cid));
        if (it == nodeOf.end())
        {
            return false;   //后面那一项的行不存在，无从定位
        }
        before = it->second;
    }

    outNode = self->second;
    outBeforeNode = before;
    return true;
}

} // namespace balloonmain
