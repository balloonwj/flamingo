/**
 *  未知发送方触发通讯录刷新的节流策略实现，UnknownSenderRefreshPolicy.cpp
 *
 *  纯逻辑，只依赖 STL，不触及 UI 与网络。与 FlamingoUnit 测试工程共用，
 *  因此不 include stdafx.h，在 Flamingo.vcxproj 里 PCH 须设 NotUsing。
 *
 *  balloonwj@qq.com   2026-08-11
 */

#include "UnknownSenderRefreshPolicy.h"

namespace balloonmain {

UnknownSenderRefreshPolicy::UnknownSenderRefreshPolicy()
    : m_lastRefreshMs(0)
{
}

bool UnknownSenderRefreshPolicy::ShouldRefreshContacts(int64_t userid, int64_t nowMs)
{
    //发送方ID都拿不到的消息，刷新通讯录也补不出名字来。
    if (userid <= 0)
    {
        return false;
    }

    //这个人已经让本端拉过一次了。拉完还是不认识，说明他压根不在本企业的组织架构里
    //（典型情形：账号已被管理台删除，但历史消息还在），再拉多少次结果都一样。
    if (m_askedUserIds.find(userid) != m_askedUserIds.end())
    {
        return false;
    }

    //时钟倒退：系统时间被改过，或者调用方用的是会回绕的 32 位 tick。
    //此时算出来的间隔是负数，冷却判断会失效（甚至永远冷却不完），直接把上次刷新
    //时刻清零重新开始。
    if (nowMs < m_lastRefreshMs)
    {
        m_lastRefreshMs = 0;
    }

    //冷却期内不再刷。上一次刷新的应答会把这段时间里进组织架构的人一并带回来，
    //所以这里"漏掉"的陌生人，多半在应答到货时就已经认识了。
    if (m_lastRefreshMs != 0 && nowMs - m_lastRefreshMs < kContactsRefreshCooldownMs)
    {
        return false;
    }

    //记账：这一次确实要刷。调用方据返回值真的去发请求，两边才对得上。
    m_lastRefreshMs = nowMs;
    m_askedUserIds.insert(userid);
    return true;
}

void UnknownSenderRefreshPolicy::Reset()
{
    m_lastRefreshMs = 0;
    m_askedUserIds.clear();
}

} // namespace balloonmain
