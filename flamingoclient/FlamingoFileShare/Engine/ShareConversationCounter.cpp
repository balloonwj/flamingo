/**
 *  按会话统计可用共享的个数 CShareConversationCounter 的实现，见 ShareConversationCounter.h。
 *  balloonwj@qq.com   2026-10-03
 */
#include "stdafx.h"

#include "ShareConversationCounter.h"

#include "../../Source/net/Protocol.h"
#include "../../Source/Common/Log/IULog.h"

namespace fileshare {

CShareConversationCounter::CShareConversationCounter()
    : m_service(NULL)
    , m_sink(NULL)
{
}

CShareConversationCounter::~CShareConversationCounter()
{
    Detach();
}

void CShareConversationCounter::Attach(CShareService* service, IShareCountSink* sink)
{
    m_service = service;
    m_sink = sink;
}

void CShareConversationCounter::Detach()
{
    if (m_service != NULL)
        m_service->DetachListObserver(this);
    m_service = NULL;
    m_sink = NULL;
    m_watched.clear();
    m_requests.clear();
}

bool CShareConversationCounter::Watch(int ctype, int64_t cid)
{
    if (m_service == NULL || cid <= 0 || (ctype != conversation_single && ctype != conversation_group))
        return false;
    const ConvKey key(ctype, cid);
    Watched& watched = m_watched[key];
    const bool ok = RequestBoth(key, watched);
    LOG_INFO("share count watch, ctype: %d, cid: %lld, requested: %d.", ctype, static_cast<long long>(cid), ok ? 1 : 0);
    return ok;
}

void CShareConversationCounter::Unwatch(int ctype, int64_t cid)
{
    const ConvKey key(ctype, cid);
    if (m_watched.erase(key) == 0)
        return;
    //尚未有结果的请求留在表里，到达时按「会话已注销」丢弃（不能让业务总控不回调：观察者是按对象登记的，别的会话的请求还要回调）
    LOG_INFO("share count unwatch, ctype: %d, cid: %lld.", ctype, static_cast<long long>(cid));
}

void CShareConversationCounter::RefreshAll()
{
    for (std::map<ConvKey, Watched>::iterator it = m_watched.begin(); it != m_watched.end(); ++it)
        RequestBoth(it->first, it->second);
}

void CShareConversationCounter::Reset()
{
    if (m_service != NULL)
        m_service->DetachListObserver(this);
    m_watched.clear();
    m_requests.clear();
}

int CShareConversationCounter::GetCount(int ctype, int64_t cid) const
{
    std::map<ConvKey, Watched>::const_iterator it = m_watched.find(ConvKey(ctype, cid));
    return (it == m_watched.end()) ? -1 : it->second.m_lastCount;
}

size_t CShareConversationCounter::GetWatchedCount() const
{
    return m_watched.size();
}

bool CShareConversationCounter::RequestBoth(const ConvKey& key, Watched& watched)
{
    if (m_service == NULL)
        return false;
    //本会话还没有结果的旧请求不再等：它们的结果到达时按「不认识的请求」丢弃，以新请求的结果为准
    std::map<uint32_t, PendingRequest>::iterator it = m_requests.begin();
    while (it != m_requests.end())
    {
        if (it->second.m_conv == key)
            it = m_requests.erase(it);
        else
            ++it;
    }
    watched.m_pending = 0;

    const int scopes[] = { kShareListScopeMine, kShareListScopeSharedMe };
    bool allSent = true;
    for (size_t i = 0; i < sizeof(scopes) / sizeof(scopes[0]); ++i)
    {
        const uint32_t requestId = m_service->RequestList(scopes[i], key.first, key.second, this);
        if (requestId == 0)
        {
            allSent = false;
            continue;
        }
        PendingRequest pending;
        pending.m_conv = key;
        pending.m_scope = scopes[i];
        m_requests[requestId] = pending;
        ++watched.m_pending;
    }
    return allSent;
}

void CShareConversationCounter::OnShareListResult(uint32_t requestId, int scope, int code,
                                                  const std::vector<ShareListEntry>& shares)
{
    std::map<uint32_t, PendingRequest>::iterator req = m_requests.find(requestId);
    if (req == m_requests.end())
        return;
    const ConvKey key = req->second.m_conv;
    m_requests.erase(req);

    //会话已注销：丢弃
    std::map<ConvKey, Watched>::iterator it = m_watched.find(key);
    if (it == m_watched.end())
        return;
    Watched& watched = it->second;
    if (watched.m_pending > 0)
        --watched.m_pending;

    //失败（含没有应答）时保留上一次的列表：角标宁可停在旧值，也不要因一次失败清零
    if (code == error_code_ok)
    {
        std::set<int64_t>& target = (scope == kShareListScopeMine) ? watched.m_mine : watched.m_sharedMe;
        target.clear();
        for (size_t i = 0; i < shares.size(); ++i)
            target.insert(shares[i].m_shareId);
    }
    else
    {
        LOG_WARNING("share count list failed, ctype: %d, cid: %lld, scope: %d, code: %d.", key.first,
                    static_cast<long long>(key.second), scope, code);
    }

    //两份列表都有结果了才数：只到一份时数出来的个数是不完整的
    if (watched.m_pending == 0)
        Recount(key, watched);
}

void CShareConversationCounter::OnCardStateChanged(int64_t shareId)
{
    for (std::map<ConvKey, Watched>::iterator it = m_watched.begin(); it != m_watched.end(); ++it)
    {
        Watched& watched = it->second;
        if (watched.m_pending > 0)
            continue;
        if (watched.m_mine.count(shareId) == 0 && watched.m_sharedMe.count(shareId) == 0)
            continue;
        Recount(it->first, watched);
    }
}

void CShareConversationCounter::Recount(const ConvKey& key, Watched& watched)
{
    if (m_service == NULL)
        return;
    //两份列表按 shareid 合并去重（同一个共享不会同时出现在两份里，这里仍按集合合并以防万一）
    std::set<int64_t> all(watched.m_mine);
    all.insert(watched.m_sharedMe.begin(), watched.m_sharedMe.end());
    int count = 0;
    for (std::set<int64_t>::const_iterator id = all.begin(); id != all.end(); ++id)
    {
        ShareCardState state;
        if (m_service->GetCardState(*id, state) && state.m_state == share_state_available)
            ++count;
    }
    if (count == watched.m_lastCount)
        return;
    watched.m_lastCount = count;
    LOG_INFO("share count changed, ctype: %d, cid: %lld, available: %d.", key.first, static_cast<long long>(key.second),
             count);
    if (m_sink != NULL)
        m_sink->OnConversationShareCount(key.first, key.second, count);
}

} // namespace fileshare
