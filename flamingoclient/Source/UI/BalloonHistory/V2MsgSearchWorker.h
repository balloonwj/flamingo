/**
 *  本地消息库的后台查询封装，V2MsgSearchWorker.h
 *
 *  跨会话的关键字搜索（search_text LIKE '%词%'）用不上索引，只能整表扫描：本地库 20 万条记录时
 *  一次要几百毫秒。放在界面线程上，用户停止输入后界面会卡住这么久。本类开
 *  一个后台线程，按请求序号排队执行这些查询，结果投递回发起查询的窗口，界面线程只负责提交与
 *  显示。主面板搜索下拉与聊天记录管理器左栏共用同一个实例（主面板持有）。
 *
 *  读库方式：本地库以 WAL 模式打开（CV2MsgStore::Open），Start 时按主面板那个库的路径另开一条
 *  连接（本类自己持有的 CV2MsgStore），后台查询都走这条连接。WAL 下读与写互不阻塞，查询进行
 *  期间界面线程照常写库（收到新消息、打开聊天窗回填），不必等查询结束。库没有文件路径（内存库、
 *  尚未打开）或另开连接失败时，退回直接使用传入的那个 CV2MsgStore，靠它内部的互斥锁与界面线程
 *  排队（2026-10-03 第一版的做法）。
 *
 *  请求合并：同一个目标窗口、同一种查询，还没开始执行的旧请求会被新请求替换，只执行最新的
 *  一个（用户连续输入时，中间那些关键字不必再查）。已经开始执行的那一个若也是同一目标窗口、
 *  同一种查询，就用 sqlite3_interrupt 中断它（只在使用自己那条连接时这样做：退回共用连接时，
 *  中断可能落到界面线程刚开始执行的写库语句上），被中断的查询不投递结果。没被中断、照常执行
 *  完的旧查询结果仍会投递，接收方按序号或回显的关键字丢弃过期结果。
 *
 *  全文索引回填：存量库第一次打开时全文索引是空的（见 V2MsgStore.h），本线程在没有查询请求时
 *  每次回填一批（CV2MsgStore::BackfillFullTextStep），两批之间稍作停顿，直到回填完毕。查询请求
 *  优先：有请求排队时先执行请求，回填只在空闲时进行。
 *
 *  结果投递：结果是堆上的 V2SearchResponse，经 PostMessage(目标窗口, 请求里的消息号,
 *  请求序号, 结果指针) 投回，接收方处理完负责 delete。投递时目标窗口已经不存在、或 PostMessage
 *  失败，由后台线程自己释放。接收窗口在 WM_DESTROY 里应调用 DrainPendingResponses，把已投进
 *  消息队列、还没来得及处理的结果取出并释放。
 *
 *  典型用法：
 *      //主面板：登录后、本地库打开后
 *      m_searchWorker.Start(&m_msgStore);
 *      //搜索下拉：停止输入 250 毫秒后
 *      balloonhistory::V2SearchRequest req;
 *      req.m_kind        = balloonhistory::kV2SearchConversations;
 *      req.m_keywordUtf8 = keyword;
 *      req.m_limit       = 50;
 *      req.m_target      = m_hWnd;
 *      req.m_msg         = kMsgLocalSearchDone;
 *      m_lastSeq = worker->Submit(req);
 *      //消息处理函数里
 *      V2SearchResponse* resp = (V2SearchResponse*)lParam;   //用完 delete
 *      //主面板：登出、本地库关闭之前
 *      m_searchWorker.Stop();
 *
 *  本文件不依赖 ATL / WTL，只需要 <windows.h>。
 *
 *  balloonwj@qq.com   2026-10-03
 */
#pragma once

#include <windows.h>

#include <stdint.h>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <memory>
#include "V2MsgQuery.h"   // ConversationHit / HistoryRecord / HistoryConversationKey
#include "HistoryStorageStats.h"   // 本机存储占用页的统计与清理

namespace balloonhistory {

class CV2MsgStore;

// 后台查询的种类。
enum V2SearchKind
{
    // 按会话汇总命中的聊天记录（CV2MsgStore::SearchConversations），主面板搜索下拉「消息」标签用
    kV2SearchConversations = 1,
    // 文件、文件夹传输记录、文件共享卡片三类消息（CV2MsgStore::SearchFileMessages），
    // 主面板搜索下拉「文件」标签的本地部分用
    kV2SearchFileMessages  = 2,
    // 聊天记录管理器左栏的跨会话搜索，可限定会话范围（CV2MsgStore::SearchConversationsForManager）
    kV2SearchManager       = 3,
    // 聊天记录页时间轴模式的统计（大会话在界面线程上要几百毫秒）：各月条数（不含关键字）、各月命中数与
    // 命中位置（含关键字，关键字筛选生效时才查），条件取请求里的 m_filter
    kV2SearchTimelineStats = 4,
    // 聊天记录管理器「本机存储占用」页的统计（docs/聊天记录管理器方案.md §12.6）：读出全部缓存引用
    // （CV2MsgStore::QueryCacheRefs），再到磁盘上量各会话各类缓存的大小（ComputeStorageStats）。目录取请求里的
    // m_storageDirs
    kV2SearchStorageStats  = 5,
    // 同一页的「清理缓存」：逐个删除请求里 m_cleanPaths 列出的缓存（DeleteStoragePath，删除前做边界检查），
    // 不读本地库
    kV2SearchStorageClean  = 6,
};

// 一次后台查询的请求。
struct V2SearchRequest
{
    int                                 m_kind;         // 查询种类，取值见 V2SearchKind
    std::string                         m_keywordUtf8;  // 关键字（UTF-8，调用方已去掉首尾空白）
    int                                 m_limit;        // 最多返回几条（会话数或记录数）
    std::vector<HistoryConversationKey> m_scope;        // 限定的会话，只对 kV2SearchManager 有意义；空表示不限
    // 关键字是否按通配符解释（'*'、'?'，见 HistoryWildcard.h），只对 kV2SearchManager 有意义；默认 false
    bool                                m_wildcard;
    // 时间轴统计的完整条件（含关键字与通配符开关；页号、每页条数不参与），只对 kV2SearchTimelineStats 有意义。
    // 该种查询的 m_limit 是命中位置最多取几条。kV2SearchManager 只用其中的 m_syntaxFromIds（关键字里 from:名字
    // 解析出的 userid，见 docs/聊天记录管理器方案.md §12.3）
    HistoryFilter                       m_filter;
    int64_t                             m_nowMs;        // 当前时间（Unix 毫秒），供「近 N 天」换算；只对时间轴统计有意义
    // 当前账号的缓存目录与本地库路径，只对 kV2SearchStorageStats / kV2SearchStorageClean 有意义（目录要在界面线程上
    // 按登录账号拼好再传进来）
    HistoryStorageDirs                  m_storageDirs;
    std::vector<std::wstring>           m_cleanPaths;   // 要删除的缓存路径（StorageCleanTargets），只对 kV2SearchStorageClean 有意义
    HWND                                m_target;       // 结果投递给哪个窗口
    UINT                                m_msg;          // 投递用的消息号（目标窗口自己的私有消息）
    uint32_t                            m_seq;          // 请求序号，由 Submit 分配，调用方填什么都会被覆盖

    V2SearchRequest()
        : m_kind(kV2SearchConversations)
        , m_limit(0)
        , m_wildcard(false)
        , m_nowMs(0)
        , m_target(NULL)
        , m_msg(0)
        , m_seq(0)
    {
    }
};

/**
 *  V2SearchResponseCounter —— 统计当前存活的 V2SearchResponse 个数。作为结果结构的一个成员，
 *  随结果的构造、复制、析构增减计数。结果要跨线程投递、由接收方释放，漏释放不会有任何报错；
 *  单元测试据此确认每个结果最后都被释放了，排查内存问题时也可以在日志里打出这个数。
 */
class V2SearchResponseCounter
{
public:
    V2SearchResponseCounter()
    {
        ::InterlockedIncrement(&s_live);
    }

    V2SearchResponseCounter(const V2SearchResponseCounter& /*other*/)
    {
        ::InterlockedIncrement(&s_live);
    }

    // 赋值不改变存活个数（两边原本都在计数之内）。
    V2SearchResponseCounter& operator=(const V2SearchResponseCounter& /*other*/)
    {
        return *this;
    }

    ~V2SearchResponseCounter()
    {
        ::InterlockedDecrement(&s_live);
    }

    // 当前存活的结果个数。
    static long Live()
    {
        return ::InterlockedCompareExchange(&s_live, 0, 0);
    }

private:
    static volatile LONG s_live;   // 存活个数，全进程共用
};

// 一次后台查询的结果。经 PostMessage 的 lParam 交给接收方，接收方负责 delete。
struct V2SearchResponse
{
    V2SearchResponseCounter      m_counter;        // 存活个数统计，见 V2SearchResponseCounter
    uint32_t                     m_seq;            // 对应请求的序号（与 wParam 相同）
    int                          m_kind;           // 查询种类，与请求相同
    std::string                  m_keywordUtf8;    // 请求里的关键字，接收方据此判断结果是否已过期
    bool                         m_wildcard;       // 请求里的「是否通配符」，与关键字一起用来判断结果是否已过期
    bool                         m_ok;             // 查询是否成功；本地库没打开或出错时为 false，结果为空
    std::vector<ConversationHit> m_conversations;  // kV2SearchConversations / kV2SearchManager 的结果
    std::vector<HistoryRecord>   m_records;        // kV2SearchFileMessages 的结果
    // kV2SearchTimelineStats 的结果：各月条数（不含关键字）、各月命中数与命中位置（关键字筛选不生效时两者为空）。
    // 命中位置最多 m_limit 条，接收方据条数是否达到上限判断有没有截断
    std::vector<HistoryPeriodCount> m_monthCounts;
    std::vector<HistoryPeriodCount> m_monthHits;
    std::vector<HistoryPosition>    m_positions;
    // kV2SearchStorageStats 的结果：统计结果与读出的全部缓存引用（接收方清理某个会话时据引用找要删的路径）
    HistoryStorageStats             m_storage;
    std::vector<HistoryCacheRef>    m_cacheRefs;
    int                          m_cleaned;        // kV2SearchStorageClean：删掉（或本来就不在）的个数
    int                          m_cleanFailed;    // kV2SearchStorageClean：没删掉的个数（被占用、边界检查不通过）
    double                       m_elapsedMs;      // 查询本身的耗时（毫秒），记日志与排查用

    V2SearchResponse()
        : m_seq(0)
        , m_kind(kV2SearchConversations)
        , m_wildcard(false)
        , m_ok(false)
        , m_cleaned(0)
        , m_cleanFailed(0)
        , m_elapsedMs(0.0)
    {
    }
};

/**
 *  V2SearchQueue —— 后台查询的请求队列（纯逻辑，不加锁，由 CV2MsgSearchWorker 在锁内使用）。
 *  先进先出；同一个目标窗口、同一种查询的请求在队列里最多一条：新请求到来时替换那条旧请求
 *  （保持旧请求在队列中的位置，免得一直输入的窗口被排到别的窗口后面）。
 */
class V2SearchQueue
{
public:
    /**
     *  放入一条请求。队列里已有同一目标窗口、同一种查询的请求时就地替换它。
     *    req：请求（按值复制进队列）。
     *  @return 替换了旧请求返回 true；新增一条返回 false。
     */
    bool Push(const V2SearchRequest& req);

    /**
     *  取出最早的一条请求。
     *    out：[出] 取出的请求。
     *  @return 队列为空时返回 false，out 不变。
     */
    bool Pop(V2SearchRequest& out);

    // 队列里的请求条数。
    size_t Size() const;

    // 清空队列（丢弃全部未执行的请求）。
    void Clear();

private:
    std::deque<V2SearchRequest> m_items;   // 未执行的请求，队首最早
};

/**
 *  CV2MsgSearchWorker —— 本地消息库的后台查询线程。线程安全：Submit / Start / Stop 可在任意
 *  线程调用（实际都在界面线程）。
 */
class CV2MsgSearchWorker
{
public:
    CV2MsgSearchWorker();

    // 析构：调用 Stop，等后台线程退出。
    ~CV2MsgSearchWorker();

    /**
     *  启动后台线程。已经在运行时什么也不做。库有文件路径时按该路径另开一条连接给后台查询用，
     *  否则（内存库、尚未打开、另开失败）直接查询 store 本身，见文件头「读库方式」。
     *    store：要查询的本地库（借用，不持有；调用方须保证在 Stop 返回之前它一直有效）。
     *           可以是尚未打开的库，查询时会得到失败结果。
     *  @return 启动成功（或已在运行）返回 true；store 为 NULL 或线程创建失败返回 false。
     */
    bool Start(CV2MsgStore* store);

    /**
     *  后台查询是否走自己另开的那条连接（而不是与界面线程共用 Start 传入的 store）。没在运行时
     *  返回 false。
     */
    bool UsesOwnConnection() const;

    /**
     *  正在执行的那个请求的序号；没有在执行的查询时返回 0。供单元测试确认查询已经开始执行，
     *  排查问题时也可以记进日志。
     */
    uint32_t ExecutingSeq() const;

    /**
     *  一个库路径能否另开一条连接查询同一份数据：普通文件路径可以；空串、内存库（":memory:"）、
     *  以 "file:" 开头的 URI（可能指向内存库）不可以 —— 对它们另开连接得到的是另一个空库。
     *    dbPath：CV2MsgStore::DbPath 的结果。
     */
    static bool CanOpenSecondConnection(const std::wstring& dbPath);

    /**
     *  停止后台线程：丢弃全部未执行的请求，中断正在执行的那一个（走自己那条连接时；被中断的
     *  不投递结果，没能中断的照常执行完并投递），再等线程退出，然后关闭自己那条连接。之后
     *  Submit 不再执行任何请求，直到再次 Start。没在运行时什么也不做。
     *  副作用：会阻塞调用线程；能中断时很快返回，退回共用连接时最长为一次查询的耗时。
     */
    void Stop();

    // 后台线程是否在运行。
    bool IsRunning() const;

    /**
     *  提交一次查询。同一目标窗口、同一种查询还没开始执行的旧请求会被这次替换；正在执行的
     *  那一个若也是同一目标窗口、同一种查询，且走的是自己那条连接，就中断它（不投递其结果）。
     *    req：请求；m_seq 由本方法分配。m_target 为 NULL 或 m_msg 为 0 时不执行。
     *  @return 分配的请求序号（从 1 开始递增）；没在运行或请求无效时返回 0，不会有结果投递。
     */
    uint32_t Submit(const V2SearchRequest& req);

    /**
     *  直接在调用线程上执行一次查询（不经过后台线程、不投递），返回堆上的结果，调用方 delete。
     *  后台线程内部也用它；公开出来供单元测试比对「后台执行的结果与同步执行完全一致」。
     *    store：要查询的本地库，可为 NULL（得到失败结果）。
     *    req：请求。
     *  @return 结果，永不为 NULL。
     */
    static V2SearchResponse* Execute(CV2MsgStore* store, const V2SearchRequest& req);

    /**
     *  接收窗口在 WM_DESTROY 里调用：把消息队列里已投递、还没处理的结果取出并释放，免得窗口
     *  销毁后这些结果无人释放。
     *    hwnd：接收窗口。
     *    msg：它接收结果用的消息号。
     *  @return 释放的结果个数。
     */
    static int DrainPendingResponses(HWND hwnd, UINT msg);

private:
    // 后台线程主循环：等请求、逐个执行、投递结果，直到 Stop。
    void ThreadProc();

    // 把结果投递给请求里的目标窗口；投不出去时释放结果。
    static void Deliver(const V2SearchRequest& req, V2SearchResponse* resp);

private:
    mutable std::mutex      m_mutex;    // 保护以下全部成员（m_thread 除外，它只在 Start / Stop 里动）
    std::condition_variable m_cv;       // 有新请求或要停止时唤醒后台线程
    V2SearchQueue           m_queue;    // 未执行的请求
    CV2MsgStore*            m_store;    // Start 传入的本地库（借用），Start 时设置
    // 自己另开的那条连接（持有）；Start 时按 m_store 的路径打开，Stop 时线程退出后关闭。
    // 为空表示没能另开，查询直接走 m_store。
    std::unique_ptr<CV2MsgStore> m_ownStore;
    // 正在执行的请求（m_executingSeq 为 0 表示没有）：序号、目标窗口、查询种类。Submit 据此判断
    // 新请求是否取代了正在执行的那一个。
    uint32_t                m_executingSeq;
    HWND                    m_executingTarget;
    int                     m_executingKind;
    // 正在执行的那一个是否已被请求中断；执行结束后据它决定丢弃结果，然后复位。
    bool                    m_executingInterrupted;
    // 全文索引还需要回填（Start 时按库的状态设置，回填完毕或出错时清除）。为 true 时线程在没有
    // 请求的空闲时间里回填。
    bool                    m_backfillPending;
    bool                    m_running;  // 后台线程是否在运行（Start 成功后为 true，Stop 后为 false）
    bool                    m_stopping; // 正在停止：后台线程看到它就退出循环
    uint32_t                m_nextSeq;  // 下一个请求序号，从 1 开始；跨 Start / Stop 不归零，序号不重复
    std::thread             m_thread;   // 后台线程
};

} // namespace balloonhistory
