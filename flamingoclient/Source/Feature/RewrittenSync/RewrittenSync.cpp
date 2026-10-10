/**
 *  消息改写离线同步的客户端逻辑实现，说明见 RewrittenSync.h。
 *  balloonwj@qq.com   2026-10-08
 */
#include "stdafx.h"
#include "RewrittenSync.h"

#include "../../Common/Log/IULog.h"
#include "../../net/MessageProtocolClient.h"     // SyncRewrittenResult / MessageNotify
#include "../../UI/BalloonHistory/V2MsgQuery.h"   // HistoryRecord / RecordFromNotify
#include "../../UI/BalloonHistory/V2MsgStore.h"   // CV2MsgStore

namespace rewrittensync {

const char* const kWatermarkKey = "rewritten_rev";

int64_t ReadWatermark(balloonhistory::CV2MsgStore& store)
{
    int64_t value = 0;
    bool found = false;
    if (!store.ReadSyncValue(kWatermarkKey, value, found) || !found || value < 0)
        return 0;
    return value;
}

//把水位推进到 rev（只增不减）。写失败返回 false；rev 不比现有水位大时不写，返回 true
static bool AdvanceWatermark(balloonhistory::CV2MsgStore& store, int64_t current, int64_t rev, int64_t& out)
{
    out = current;
    if (rev <= current)
        return true;
    if (!store.WriteSyncValue(kWatermarkKey, rev))
        return false;
    out = rev;
    return true;
}

bool ApplyRewrittenPage(balloonhistory::CV2MsgStore& store, const net::SyncRewrittenResult& page, int64_t selfUserId,
                        bool firstSyncOnEmptyStore, RewrittenPageOutcome& out)
{
    out = RewrittenPageOutcome();
    const int64_t current = ReadWatermark(store);
    out.m_watermark = current;
    if (page.m_code != 0)
        return false;

    //首次同步且本地库为空：本页每一条本地都没有，不必逐条查，直接跳到服务端当前的最大序号
    if (firstSyncOnEmptyStore)
    {
        out.m_jumped = true;
        return AdvanceWatermark(store, current, page.m_currev, out.m_watermark);
    }

    std::vector<balloonhistory::HistoryRecord> recs;
    for (size_t i = 0; i < page.m_items.size(); ++i)
    {
        const net::MessageNotify& m = page.m_items[i].m_msg;
        balloonhistory::HistoryRecord existing;
        bool found = false;
        if (!store.QueryRecord(m.m_ctype, m.m_cid, m.m_msgid, existing, found))
        {
            LOG_ERROR("rewritten sync: query local record failed, ctype=%d, cid=%lld, msgid=%lld.", m.m_ctype,
                      (long long)m.m_cid, (long long)m.m_msgid);
            return false;
        }
        //本地没有这一行：本机没同步过这段历史，以后拉到时服务端给的就是改写后的内容，不插入
        if (!found)
        {
            ++out.m_skipped;
            continue;
        }
        recs.push_back(balloonhistory::RecordFromNotify(m, m.m_ctype, m.m_cid, selfUserId));

        //按会话归并：同一会话只占一项，msgid 依次追加
        bool seen = false;
        for (size_t k = 0; k < out.m_affected.size(); ++k)
        {
            if (out.m_affected[k].m_ctype == m.m_ctype && out.m_affected[k].m_cid == m.m_cid)
            {
                out.m_affected[k].m_msgids.push_back(m.m_msgid);
                seen = true;
                break;
            }
        }
        if (!seen)
        {
            RewrittenConversation conv;
            conv.m_ctype = m.m_ctype;
            conv.m_cid = m.m_cid;
            conv.m_msgids.push_back(m.m_msgid);
            out.m_affected.push_back(conv);
        }
    }

    if (!recs.empty() && !store.UpsertBatch(recs))
    {
        LOG_ERROR("rewritten sync: batch upsert failed, count=%u.", (uint32_t)recs.size());
        out.m_affected.clear();
        return false;
    }
    out.m_applied = recs.size();
    return AdvanceWatermark(store, current, page.m_rev, out.m_watermark);
}

CRewrittenSyncController::CRewrittenSyncController(IRewrittenSyncHost* host)
    : m_host(host)
    , m_running(false)
    , m_since(0)
    , m_firstOnEmpty(false)
{
}

void CRewrittenSyncController::Start(balloonhistory::CV2MsgStore& store)
{
    if (m_host == NULL || !store.IsOpen())
    {
        LOG_WARNING("rewritten sync: not started, host or local store unavailable.");
        m_running = false;
        return;
    }
    m_since = ReadWatermark(store);
    bool hasAny = true;
    //查询失败时按「有消息」处理：逐页拉取总是正确的，只是慢一些
    if (!store.HasAnyMessage(hasAny))
        hasAny = true;
    m_firstOnEmpty = (m_since == 0 && !hasAny);
    m_running = true;
    LOG_INFO("rewritten sync: start, since=%lld, first on empty store=%d.", (long long)m_since,
             m_firstOnEmpty ? 1 : 0);
    m_host->SendRewrittenRequest(m_since, kPageLimit);
}

void CRewrittenSyncController::OnPage(balloonhistory::CV2MsgStore& store, const net::SyncRewrittenResult& page,
                                      int64_t selfUserId)
{
    if (!m_running || m_host == NULL)
    {
        LOG_INFO("rewritten sync: page ignored, no round in progress.");
        return;
    }
    if (page.m_code != 0)
    {
        LOG_WARNING("rewritten sync: server returned code %d, round ended.", page.m_code);
        m_running = false;
        return;
    }

    RewrittenPageOutcome outcome;
    const bool firstOnEmpty = m_firstOnEmpty;
    //捷径只用于本轮第一页
    m_firstOnEmpty = false;
    if (!ApplyRewrittenPage(store, page, selfUserId, firstOnEmpty, outcome))
    {
        LOG_ERROR("rewritten sync: apply page failed, watermark stays at %lld, round ended.",
                  (long long)outcome.m_watermark);
        m_running = false;
        return;
    }
    LOG_INFO("rewritten sync: page applied, items=%u, applied=%u, skipped=%u, jumped=%d, watermark=%lld, more=%d.",
             (uint32_t)page.m_items.size(), (uint32_t)outcome.m_applied, (uint32_t)outcome.m_skipped,
             outcome.m_jumped ? 1 : 0, (long long)outcome.m_watermark, page.m_more ? 1 : 0);

    //受影响的会话开着窗口才刷新；一页里同一会话只刷新一次（m_affected 已去重）
    for (size_t i = 0; i < outcome.m_affected.size(); ++i)
    {
        const RewrittenConversation& conv = outcome.m_affected[i];
        if (m_host->IsConversationShown(conv.m_ctype, conv.m_cid))
            m_host->RefreshConversation(conv.m_ctype, conv.m_cid, conv.m_msgids);
    }

    //走了捷径、没有更多、或水位没有前进（防止服务端异常时反复请求同一页）：本轮结束
    if (outcome.m_jumped || !page.m_more)
    {
        m_running = false;
        return;
    }
    if (outcome.m_watermark <= m_since)
    {
        LOG_WARNING("rewritten sync: more pages reported but watermark did not advance (%lld), round ended.",
                    (long long)outcome.m_watermark);
        m_running = false;
        return;
    }
    m_since = outcome.m_watermark;
    m_host->SendRewrittenRequest(m_since, kPageLimit);
}

void CRewrittenSyncController::Reset()
{
    m_running = false;
    m_since = 0;
    m_firstOnEmpty = false;
}

bool CRewrittenSyncController::IsRunning() const
{
    return m_running;
}

} // namespace rewrittensync
