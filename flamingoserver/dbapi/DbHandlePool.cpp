/**
 *  DbHandlePool 的实现，详见 DbHandlePool.h。
 *  balloonwj@qq.com   2026-10-04
 */
#include "DbHandlePool.h"

#include <chrono>

//空闲句柄总数的默认上限。chatserver 会访问数据库的线程约十来条（客户端 I/O 6 条、
//HTTP 4 条、监控 1 条，另有主线程），同一时刻在用的连接数大致不超过线程数的两倍，
//取 32 足以让高峰期归还的句柄都能留下
static const size_t  kDefaultMaxIdle = 32;

//取用前校验的默认阈值，单位毫秒。连续请求之间间隔很短的句柄不必 ping；
//空闲超过 5 秒的才花一次往来（局域网内不到 1 毫秒）确认它仍然可用
static const int64_t kDefaultValidateAfterMs = 5 * 1000;

//保活的默认阈值，单位毫秒。远小于 MySQL wait_timeout 的默认值 8 小时，
//也小于常见防火墙、NAT 设备回收空闲 TCP 连接的时长（通常为数分钟到一小时）
static const int64_t kDefaultKeepAliveAfterMs = 60 * 1000;

//收缩的默认阈值，单位毫秒。10 分钟没有被业务使用过的句柄说明负载已经降下来，关闭它
static const int64_t kDefaultMaxIdleMs = 10 * 60 * 1000;

DbHandlePool::Options::Options()
{
    m_maxIdle = kDefaultMaxIdle;
    m_validateAfterMs = kDefaultValidateAfterMs;
    m_keepAliveAfterMs = kDefaultKeepAliveAfterMs;
    m_maxIdleMs = kDefaultMaxIdleMs;
}

DbHandlePool::Stats::Stats()
{
    m_acquireHits = 0;
    m_acquireMisses = 0;
    m_validatePings = 0;
    m_keepAlivePings = 0;
    m_pingFailures = 0;
    m_closedOverflow = 0;
    m_closedIdle = 0;
}

DbHandlePool::DbHandlePool(CloseHandleFn closeFn, PingHandleFn pingFn, const Options& options, NowMsFn nowFn)
{
    m_closeFn = closeFn;
    m_pingFn = pingFn;
    m_nowFn = nowFn;
    m_options = options;
    m_bEnabled = false;
}

DbHandlePool::~DbHandlePool()
{
    clear();
}

int64_t DbHandlePool::nowMs() const
{
    if (m_nowFn != NULL)
        return m_nowFn();

    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

void DbHandlePool::setEnabled(bool enabled)
{
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        m_bEnabled = enabled;
    }

    //关闭本池时，已在池中的句柄也不再保留
    if (!enabled)
        clear();
}

bool DbHandlePool::isEnabled() const
{
    std::lock_guard<std::mutex> guard(m_mutex);
    return m_bEnabled;
}

void DbHandlePool::setOptions(const Options& options)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    m_options = options;
}

DbHandlePool::Options DbHandlePool::getOptions() const
{
    std::lock_guard<std::mutex> guard(m_mutex);
    return m_options;
}

void* DbHandlePool::acquire(const std::string& key)
{
    //每轮从池中取出一个候选句柄；它若需要校验且 ping 不通，就关闭它再取下一个
    for (;;)
    {
        Entry candidate;
        {
            std::lock_guard<std::mutex> guard(m_mutex);
            if (!m_bEnabled)
                return NULL;

            //从尾部往前找：尾部是最近归还的句柄，其连接最可能仍然可用
            int found = -1;
            for (int i = (int)m_idle.size() - 1; i >= 0; --i)
            {
                if (m_idle[i].m_key == key)
                {
                    found = i;
                    break;
                }
            }

            if (found < 0)
            {
                ++m_stats.m_acquireMisses;
                return NULL;
            }

            candidate = m_idle[found];
            m_idle.erase(m_idle.begin() + found);

            bool needValidate = (m_pingFn != NULL
                                 && m_options.m_validateAfterMs >= 0
                                 && nowMs() - candidate.m_lastActiveMs >= m_options.m_validateAfterMs);
            if (!needValidate)
            {
                ++m_stats.m_acquireHits;
                return candidate.m_handle;
            }

            ++m_stats.m_validatePings;
        }

        //ping 是一次网络往来，在锁外进行。候选句柄已经移出池，其他线程取不到它
        if (m_pingFn(candidate.m_handle))
        {
            std::lock_guard<std::mutex> guard(m_mutex);
            ++m_stats.m_acquireHits;
            return candidate.m_handle;
        }

        {
            std::lock_guard<std::mutex> guard(m_mutex);
            ++m_stats.m_pingFailures;
        }
        m_closeFn(candidate.m_handle);
    }
}

void DbHandlePool::release(const std::string& key, void* handle)
{
    if (handle == NULL)
        return;

    std::vector<void*> toClose;
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        if (!m_bEnabled)
        {
            toClose.push_back(handle);
        }
        else
        {
            Entry entry;
            entry.m_key = key;
            entry.m_handle = handle;
            entry.m_lastUsedMs = nowMs();
            entry.m_lastActiveMs = entry.m_lastUsedMs;
            m_idle.push_back(entry);
            trimToCapacityLocked(toClose);
        }
    }

    closeAll(toClose);
}

void DbHandlePool::trimToCapacityLocked(std::vector<void*>& toClose)
{
    while (m_idle.size() > m_options.m_maxIdle)
    {
        toClose.push_back(m_idle.front().m_handle);
        m_idle.erase(m_idle.begin());
        ++m_stats.m_closedOverflow;
    }
}

void DbHandlePool::maintain()
{
    std::vector<void*> toClose;
    std::vector<Entry> toPing;
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        int64_t now = nowMs();

        std::vector<Entry> kept;
        kept.reserve(m_idle.size());
        for (size_t i = 0; i < m_idle.size(); ++i)
        {
            const Entry& entry = m_idle[i];

            //空闲过久：负载已经降下来，直接关闭，不再为它保活
            if (m_options.m_maxIdleMs >= 0 && now - entry.m_lastUsedMs >= m_options.m_maxIdleMs)
            {
                toClose.push_back(entry.m_handle);
                ++m_stats.m_closedIdle;
                continue;
            }

            //达到保活阈值：移出池，到锁外 ping，期间其他线程取不到它
            if (m_pingFn != NULL && m_options.m_keepAliveAfterMs >= 0
                && now - entry.m_lastActiveMs >= m_options.m_keepAliveAfterMs)
            {
                toPing.push_back(entry);
                ++m_stats.m_keepAlivePings;
                continue;
            }

            kept.push_back(entry);
        }
        m_idle.swap(kept);
    }

    closeAll(toClose);
    toClose.clear();

    //逐个 ping。成功的刷新往来时刻，失败的关闭
    std::vector<Entry> alive;
    for (size_t i = 0; i < toPing.size(); ++i)
    {
        Entry entry = toPing[i];
        if (m_pingFn(entry.m_handle))
        {
            entry.m_lastActiveMs = nowMs();
            alive.push_back(entry);
        }
        else
        {
            m_closeFn(entry.m_handle);
            std::lock_guard<std::mutex> guard(m_mutex);
            ++m_stats.m_pingFailures;
        }
    }

    if (alive.empty())
        return;

    {
        std::lock_guard<std::mutex> guard(m_mutex);
        if (!m_bEnabled)
        {
            //ping 期间本池被关闭了：这些句柄也不再保留
            for (size_t i = 0; i < alive.size(); ++i)
                toClose.push_back(alive[i].m_handle);
        }
        else
        {
            //按业务归还时刻插回原来的先后位置，使「尾部是最近归还的」这一约定继续成立
            for (size_t i = 0; i < alive.size(); ++i)
            {
                std::vector<Entry>::iterator pos = m_idle.begin();
                while (pos != m_idle.end() && pos->m_lastUsedMs <= alive[i].m_lastUsedMs)
                    ++pos;
                m_idle.insert(pos, alive[i]);
            }

            //ping 期间其他线程可能已经归还了句柄，放回之后可能超出容量
            trimToCapacityLocked(toClose);
        }
    }

    closeAll(toClose);
}

void DbHandlePool::clear()
{
    std::vector<void*> toClose;
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        for (size_t i = 0; i < m_idle.size(); ++i)
            toClose.push_back(m_idle[i].m_handle);
        m_idle.clear();
    }

    closeAll(toClose);
}

size_t DbHandlePool::idleCount() const
{
    std::lock_guard<std::mutex> guard(m_mutex);
    return m_idle.size();
}

DbHandlePool::Stats DbHandlePool::getStats() const
{
    std::lock_guard<std::mutex> guard(m_mutex);
    return m_stats;
}

void DbHandlePool::closeAll(const std::vector<void*>& handles)
{
    for (size_t i = 0; i < handles.size(); ++i)
        m_closeFn(handles[i]);
}
