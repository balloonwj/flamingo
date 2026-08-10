/**
 *  ConversationListOps.cpp
 *
 *  最近会话列表纯操作函数的实现。只碰传进来的 vector，不创建窗口、不发协议，
 *  故可被 Flamingo.vcxproj 与 FlamingoUnit.vcxproj 两个工程逐字共用。在
 *  Flamingo.vcxproj 里本文件需标记 PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-07-31
 */

#include "ConversationListOps.h"

#include <algorithm>

namespace balloonmain {

bool ConversationOrderLess(const net::ConversationItem& a, const net::ConversationItem& b)
{
    if (a.m_pinned != b.m_pinned)
    {
        return a.m_pinned > b.m_pinned;   // 置顶(1)在前
    }
    if (a.m_timestamp != b.m_timestamp)
    {
        return a.m_timestamp > b.m_timestamp;   // 时间新者在前
    }
    return a.m_cid < b.m_cid;
}

void SortConversations(std::vector<net::ConversationItem>& items)
{
    std::sort(items.begin(), items.end(), ConversationOrderLess);
}

net::ConversationItem* FindConversation(std::vector<net::ConversationItem>& items,
                                        int ctype, int64_t cid)
{
    for (size_t i = 0; i < items.size(); ++i)
    {
        if (items[i].m_ctype == ctype && items[i].m_cid == cid)
        {
            return &items[i];
        }
    }
    return NULL;
}

const net::ConversationItem* FindConversation(const std::vector<net::ConversationItem>& items,
                                              int ctype, int64_t cid)
{
    for (size_t i = 0; i < items.size(); ++i)
    {
        if (items[i].m_ctype == ctype && items[i].m_cid == cid)
        {
            return &items[i];
        }
    }
    return NULL;
}

size_t RemoveConversation(std::vector<net::ConversationItem>& items, int ctype, int64_t cid)
{
    size_t removed = 0;
    std::vector<net::ConversationItem>::iterator it = items.begin();
    while (it != items.end())
    {
        if (it->m_ctype == ctype && it->m_cid == cid)
        {
            it = items.erase(it);
            ++removed;
        }
        else
        {
            ++it;
        }
    }
    return removed;
}

std::vector<ConversationKey> CollectConversationKeys(const std::vector<net::ConversationItem>& items)
{
    std::vector<ConversationKey> keys;
    keys.reserve(items.size());
    for (size_t i = 0; i < items.size(); ++i)
    {
        keys.push_back(ConversationKey(items[i].m_ctype, items[i].m_cid));
    }
    return keys;
}

bool SetConversationPinned(std::vector<net::ConversationItem>& items,
                           int ctype, int64_t cid, bool pinned)
{
    net::ConversationItem* pConv = FindConversation(items, ctype, cid);
    if (pConv == NULL)
    {
        return false;
    }

    const int target = pinned ? 1 : 0;
    if (pConv->m_pinned == target)
    {
        return false;   // 本就是该状态，调用方无须重排 / 重绘
    }
    pConv->m_pinned = target;

    // 置顶直接决定排序（置顶的排最前），改完必须重排，否则列表顺序与标志不一致。
    SortConversations(items);
    return true;
}

bool SetConversationMuted(std::vector<net::ConversationItem>& items,
                          int ctype, int64_t cid, bool muted)
{
    net::ConversationItem* pConv = FindConversation(items, ctype, cid);
    if (pConv == NULL)
    {
        return false;
    }

    const int target = muted ? 1 : 0;
    if (pConv->m_mute == target)
    {
        return false;
    }
    pConv->m_mute = target;

    // 免打扰不参与排序（见 ConversationOrderLess），故这里不重排。
    return true;
}

bool IsConversationMuted(const std::vector<net::ConversationItem>& items,
                         int ctype, int64_t cid)
{
    const net::ConversationItem* pConv = FindConversation(items, ctype, cid);
    if (pConv == NULL)
    {
        return false;
    }
    return pConv->m_mute != 0;
}

bool ShouldTreatAsReadOnArrival(bool windowExists, bool isWindow, bool isVisible,
                                bool isIconic, bool isForeground)
{
    // 五项全中才算"用户正看着这个窗口"。任何一项不满足都按未读处理 —— 宁可多亮
    // 一个红点让用户自己去点掉，也不要把没看到的消息悄悄标成已读。
    if (!windowExists)
        return false;
    if (!isWindow)
        return false;
    if (!isVisible)
        return false;
    // 最小化的窗口 ::IsWindowVisible 仍返回 TRUE，故这一项不能省。
    if (isIconic)
        return false;
    if (!isForeground)
        return false;

    return true;
}

int NextUnreadCount(int oldUnread, bool countAsUnread, bool treatAsRead)
{
    // 脏数据兜底：未读数不该为负，真出现了按 0 起算，避免把负值一路传进 UI。
    const int base = (oldUnread > 0) ? oldUnread : 0;

    // 用户正看着这个会话：本条固然不算未读，之前累积的旧未读也一并清掉。
    if (treatAsRead)
        return 0;

    if (countAsUnread)
        return base + 1;

    return base;
}

} // namespace balloonmain
