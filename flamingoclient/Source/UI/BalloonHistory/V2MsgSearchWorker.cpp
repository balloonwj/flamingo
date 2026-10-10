/**
 *  本地消息库后台查询封装的实现。整体说明见 V2MsgSearchWorker.h。
 *
 *  本 .cpp 不 include Flamingo 的 stdafx.h —— 它要被 Flamingo.vcxproj 与
 *  FlamingoUnit.vcxproj 共用。在 Flamingo.vcxproj 里需标记 PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-10-03
 */
#include "V2MsgSearchWorker.h"

#include "V2MsgStore.h"
#include "../../Common/Log/IULog.h"

namespace balloonhistory {

namespace {

// 两次查询之间后台线程让出的时间（毫秒）。只在退回共用连接时有意义：本地库的互斥锁不保证
// 先等先得，后台线程查完一个、马上又去查下一个时，会抢在正在等锁的界面线程（例如要写入一条
// 新消息）前面重新拿到锁，界面线程就要等两次查询（据实证：单测 W9 测到界面线程写库等了约两次
// 查询的时间之和）。查完一个先睡这一下，等锁的线程就能先拿到锁。
const DWORD kYieldBetweenQueriesMs = 1;

// 全文索引每批回填多少个 rowid。一批在一个写事务里完成，期间界面线程写库要等它（WAL 下写与写
// 互斥）。据实证（单测 FTSB，Debug 版，20 万条）：每批 2000 行时最慢的一批 173 毫秒，取 1000 行
// 把最坏的等待减半；全部回填完多花一倍的批数，在后台进行，用户感觉不到。
const int kBackfillRowsPerStep = 1000;
// 两批回填之间停顿多久（毫秒），把 CPU 与写锁让给别的线程。
const DWORD kBackfillPauseMs = 10;

// 当前时刻（毫秒，带小数），基于高精度计数器，用于统计查询耗时。
double NowMs()
{
    LARGE_INTEGER freq;
    LARGE_INTEGER now;
    ::QueryPerformanceFrequency(&freq);
    ::QueryPerformanceCounter(&now);
    return (double)now.QuadPart * 1000.0 / (double)freq.QuadPart;
}

} // namespace

volatile LONG V2SearchResponseCounter::s_live = 0;

// ============================== V2SearchQueue ==============================

bool V2SearchQueue::Push(const V2SearchRequest& req)
{
    for (size_t i = 0; i < m_items.size(); ++i)
    {
        if (m_items[i].m_target == req.m_target && m_items[i].m_kind == req.m_kind)
        {
            m_items[i] = req;
            return true;
        }
    }
    m_items.push_back(req);
    return false;
}

bool V2SearchQueue::Pop(V2SearchRequest& out)
{
    if (m_items.empty())
    {
        return false;
    }
    out = m_items.front();
    m_items.pop_front();
    return true;
}

size_t V2SearchQueue::Size() const
{
    return m_items.size();
}

void V2SearchQueue::Clear()
{
    m_items.clear();
}

// ============================ CV2MsgSearchWorker ============================

CV2MsgSearchWorker::CV2MsgSearchWorker()
    : m_store(NULL)
    , m_running(false)
    , m_stopping(false)
    , m_nextSeq(1)
    , m_executingSeq(0)
    , m_executingTarget(NULL)
    , m_executingKind(0)
    , m_executingInterrupted(false)
    , m_backfillPending(false)
{
}

bool CV2MsgSearchWorker::CanOpenSecondConnection(const std::wstring& dbPath)
{
    if (dbPath.empty() || dbPath == L":memory:")
    {
        return false;
    }
    //URI 形式的路径可能指向内存库（file::memory:、mode=memory），一律不另开
    const std::wstring kUriPrefix = L"file:";
    if (dbPath.compare(0, kUriPrefix.size(), kUriPrefix) == 0)
    {
        return false;
    }
    return true;
}

CV2MsgSearchWorker::~CV2MsgSearchWorker()
{
    Stop();
}

bool CV2MsgSearchWorker::Start(CV2MsgStore* store)
{
    if (store == NULL)
    {
        return false;
    }
    std::lock_guard<std::mutex> guard(m_mutex);
    if (m_running)
    {
        return true;
    }
    m_store    = store;
    m_stopping = false;
    m_queue.Clear();
    m_executingSeq = 0;
    m_executingInterrupted = false;

    //按同一个库文件另开一条连接给后台查询用（见文件头「读库方式」）。打开很快（建表、建索引
    //语句对已有的库都是空操作），在锁内完成即可。
    m_ownStore.reset();
    const std::wstring path = store->DbPath();
    if (CanOpenSecondConnection(path))
    {
        std::unique_ptr<CV2MsgStore> own(new CV2MsgStore());
        if (own->Open(path.c_str()))
        {
            m_ownStore.swap(own);
        }
        else
        {
            LOG_WARNING("search worker: failed to open a second connection, queries share the UI connection.");
        }
    }
    try
    {
        m_thread = std::thread(&CV2MsgSearchWorker::ThreadProc, this);
    }
    catch (...)
    {
        LOG_ERROR("search worker: failed to create thread.");
        m_store = NULL;
        m_ownStore.reset();
        return false;
    }
    m_running = true;
    //全文索引还没回填完（存量库第一次升级）时，空闲时间里分批回填
    CV2MsgStore* queryStore = m_ownStore ? m_ownStore.get() : m_store;
    m_backfillPending = queryStore->IsOpen() && !queryStore->IsFullTextReady();
    LOG_INFO("search worker: started, %s connection, full-text index %s.",
             m_ownStore ? "own" : "shared", m_backfillPending ? "needs backfill" : "ready or unavailable");
    return true;
}

bool CV2MsgSearchWorker::UsesOwnConnection() const
{
    std::lock_guard<std::mutex> guard(m_mutex);
    return m_running && m_ownStore;
}

uint32_t CV2MsgSearchWorker::ExecutingSeq() const
{
    std::lock_guard<std::mutex> guard(m_mutex);
    return m_executingSeq;
}

void CV2MsgSearchWorker::Stop()
{
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        if (!m_running)
        {
            return;
        }
        m_stopping = true;
        const size_t dropped = m_queue.Size();
        m_queue.Clear();
        if (dropped > 0)
        {
            LOG_INFO("search worker: stopping, %u pending requests dropped.", (unsigned)dropped);
        }
        //正在执行的查询走的是自己那条连接时直接中断，线程不必等它查完
        if (m_executingSeq != 0 && m_ownStore)
        {
            m_executingInterrupted = true;
            m_ownStore->Interrupt();
        }
    }
    m_cv.notify_all();

    //在锁外等线程退出：正在执行的那次查询结束后，线程看到 m_stopping 就会返回。
    if (m_thread.joinable())
    {
        m_thread.join();
    }

    std::lock_guard<std::mutex> guard(m_mutex);
    m_running  = false;
    m_stopping = false;
    m_store    = NULL;
    //线程已退出，自己那条连接不会再被使用，关闭它
    m_ownStore.reset();
    LOG_INFO("search worker: stopped.");
}

bool CV2MsgSearchWorker::IsRunning() const
{
    std::lock_guard<std::mutex> guard(m_mutex);
    return m_running && !m_stopping;
}

uint32_t CV2MsgSearchWorker::Submit(const V2SearchRequest& req)
{
    if (req.m_target == NULL || req.m_msg == 0)
    {
        return 0;
    }
    uint32_t seq = 0;
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        if (!m_running || m_stopping)
        {
            return 0;
        }
        V2SearchRequest copy = req;
        seq = m_nextSeq++;
        copy.m_seq = seq;
        //序号转一圈回到 0 时跳过 0：0 留给"没有提交成功"。
        if (m_nextSeq == 0)
        {
            m_nextSeq = 1;
        }
        m_queue.Push(copy);
        //正在执行的是同一窗口、同一种查询：它的结果已经没人要了，中断它。只在走自己那条连接时
        //这样做 —— 共用连接上，中断可能落到界面线程刚开始执行的写库语句上。
        if (m_executingSeq != 0 && m_ownStore && !m_executingInterrupted
            && m_executingTarget == copy.m_target && m_executingKind == copy.m_kind)
        {
            m_executingInterrupted = true;
            m_ownStore->Interrupt();
        }
    }
    m_cv.notify_one();
    return seq;
}

V2SearchResponse* CV2MsgSearchWorker::Execute(CV2MsgStore* store, const V2SearchRequest& req)
{
    V2SearchResponse* resp = new V2SearchResponse();
    resp->m_seq         = req.m_seq;
    resp->m_kind        = req.m_kind;
    resp->m_keywordUtf8 = req.m_keywordUtf8;
    resp->m_wildcard    = req.m_wildcard;
    if (store == NULL || !store->IsOpen())
    {
        resp->m_ok = false;
        return resp;
    }

    const double t0 = NowMs();
    switch (req.m_kind)
    {
    //主面板搜索下拉「消息」标签：按会话汇总
    case kV2SearchConversations:
        resp->m_ok = store->SearchConversations(req.m_keywordUtf8, req.m_limit, resp->m_conversations);
        break;
    //主面板搜索下拉「文件」标签的本地部分：文件、文件夹传输记录、文件共享卡片
    case kV2SearchFileMessages:
        resp->m_ok = store->SearchFileMessages(req.m_keywordUtf8, req.m_limit, resp->m_records);
        break;
    //聊天记录管理器左栏：可限定会话范围的跨会话搜索
    case kV2SearchManager:
        resp->m_ok = store->SearchConversationsForManager(req.m_keywordUtf8, req.m_limit, req.m_scope,
                                                          resp->m_conversations, req.m_wildcard,
                                                          req.m_filter.m_syntaxFromIds);
        break;
    //聊天记录页时间轴模式：各月条数按不含关键字的条件统计，各月命中数与命中位置按含关键字的条件统计
    case kV2SearchTimelineStats:
    {
        HistoryFilter list = req.m_filter;
        list.m_keyword.clear();
        bool ok = store->CountByLocalMonth(list, req.m_nowMs, resp->m_monthCounts);
        if (ok && HasKeywordFilter(req.m_filter))
        {
            ok = store->CountByLocalMonth(req.m_filter, req.m_nowMs, resp->m_monthHits)
              && store->QueryPositions(req.m_filter, req.m_nowMs, req.m_limit, resp->m_positions);
        }
        resp->m_ok = ok;
        break;
    }
    //本机存储占用页的统计：读出缓存引用，再到磁盘上量大小
    case kV2SearchStorageStats:
        resp->m_ok = store->QueryCacheRefs(resp->m_cacheRefs);
        if (resp->m_ok)
        {
            resp->m_storage = ComputeStorageStats(resp->m_cacheRefs, req.m_storageDirs);
        }
        break;
    //本机存储占用页的清理：逐个删除，删除前做边界检查
    case kV2SearchStorageClean:
        for (size_t i = 0; i < req.m_cleanPaths.size(); ++i)
        {
            if (DeleteStoragePath(req.m_cleanPaths[i], req.m_storageDirs))
            {
                ++resp->m_cleaned;
            }
            else
            {
                ++resp->m_cleanFailed;
            }
        }
        resp->m_ok = true;
        break;
    //不认识的种类：按失败返回
    default:
        resp->m_ok = false;
        break;
    }
    resp->m_elapsedMs = NowMs() - t0;
    return resp;
}

void CV2MsgSearchWorker::Deliver(const V2SearchRequest& req, V2SearchResponse* resp)
{
    //目标窗口已经不存在、或投递失败（消息队列满等）：结果无人接收，在这里释放。
    if (!::IsWindow(req.m_target)
        || !::PostMessage(req.m_target, req.m_msg, (WPARAM)req.m_seq, (LPARAM)resp))
    {
        LOG_INFO("search worker: target window gone, result of seq %u dropped.", req.m_seq);
        delete resp;
    }
}

int CV2MsgSearchWorker::DrainPendingResponses(HWND hwnd, UINT msg)
{
    int count = 0;
    MSG m;
    while (::PeekMessage(&m, hwnd, msg, msg, PM_REMOVE))
    {
        delete (V2SearchResponse*)m.lParam;
        ++count;
    }
    return count;
}

void CV2MsgSearchWorker::ThreadProc()
{
    for (;;)
    {
        V2SearchRequest req;
        CV2MsgStore* store = NULL;
        bool backfill = false;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            while (!m_stopping && m_queue.Size() == 0 && !m_backfillPending)
            {
                m_cv.wait(lock);
            }
            if (m_stopping)
            {
                return;
            }
            //没有请求排队：回填一批全文索引
            if (m_queue.Size() == 0)
            {
                backfill = true;
                store = m_ownStore ? m_ownStore.get() : m_store;
            }
        }
        if (backfill)
        {
            const double t0 = NowMs();
            const int rows = store->BackfillFullTextStep(kBackfillRowsPerStep);
            const bool ready = store->IsFullTextReady();
            if (rows < 0 || ready)
            {
                std::lock_guard<std::mutex> guard(m_mutex);
                m_backfillPending = false;
            }
            if (ready)
            {
                LOG_INFO("search worker: full-text index backfill finished (last step %d rows, %.1f ms).",
                         rows, NowMs() - t0);
            }
            else if (rows < 0)
            {
                LOG_WARNING("search worker: full-text index backfill failed, searches keep using LIKE.");
            }
            ::Sleep(kBackfillPauseMs);
            continue;
        }
        {
            std::lock_guard<std::mutex> guard(m_mutex);
            if (m_stopping)
            {
                return;
            }
            if (!m_queue.Pop(req))
            {
                continue;
            }
            store = m_ownStore ? m_ownStore.get() : m_store;
            //上一次请求留下的中断标志不能作用到这一次
            if (m_ownStore)
            {
                m_ownStore->ClearInterrupt();
            }
            //先登记再执行：Submit 据此判断要不要中断它
            m_executingSeq         = req.m_seq;
            m_executingTarget      = req.m_target;
            m_executingKind        = req.m_kind;
            m_executingInterrupted = false;
        }

        //在锁外执行查询：查询期间界面线程仍可提交新请求（替换队列里的旧请求、中断这一个）。
        V2SearchResponse* resp = Execute(store, req);

        bool interrupted = false;
        bool shared = false;
        {
            std::lock_guard<std::mutex> guard(m_mutex);
            interrupted = m_executingInterrupted;
            shared = !m_ownStore;
            //中断标志只作用于刚执行完的这一次；清掉，免得空闲时的全文索引回填被它中止
            if (m_ownStore)
            {
                m_ownStore->ClearInterrupt();
            }
            m_executingSeq         = 0;
            m_executingTarget      = NULL;
            m_executingKind        = 0;
            m_executingInterrupted = false;
        }

        //关键字只记字节数，不记内容（日志里不留用户搜索过什么）。
        LOG_INFO("search worker: seq %u kind %d %s, %u conversations, %u records in %.1f ms (keyword %u bytes).",
                 req.m_seq, req.m_kind, resp->m_ok ? "ok" : (interrupted ? "interrupted" : "failed"),
                 (unsigned)resp->m_conversations.size(), (unsigned)resp->m_records.size(),
                 resp->m_elapsedMs, (unsigned)req.m_keywordUtf8.size());
        //被中断的查询结果不完整、也已经没人要了，不投递。中断请求发出时查询恰好已经查完
        //（m_ok 为 true）的，照常投递，接收方按序号自行丢弃。
        if (interrupted && !resp->m_ok)
        {
            delete resp;
        }
        else
        {
            Deliver(req, resp);
        }

        //共用连接时，让正在等本地库锁的线程（多半是界面线程要写库）先拿到锁，再查下一个。
        if (shared)
        {
            ::Sleep(kYieldBetweenQueriesMs);
        }
    }
}

} // namespace balloonhistory
