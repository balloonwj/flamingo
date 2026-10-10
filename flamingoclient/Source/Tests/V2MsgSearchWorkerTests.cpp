/**
 *  本地消息库后台查询封装的单元测试（Suite W），V2MsgSearchWorkerTests.cpp
 *
 *  覆盖 balloonhistory::CV2MsgSearchWorker 与 V2SearchQueue：
 *    W1～W3  「会话汇总」「文件消息」「管理器跨会话搜索（带会话范围）」三种请求，结果按请求序号
 *            投回目标窗口，内容与同步执行完全一致；
 *    W4      同一窗口、同一种查询连续放入队列时，未执行的旧请求被替换且保持原位置（纯逻辑）；
 *    W5      不同窗口或不同种类的请求互不替换（纯逻辑）；
 *    W6      目标窗口已销毁时结果由后台线程释放；已投进消息队列、窗口来不及处理的结果可用
 *            DrainPendingResponses 取出释放；所有结果最终都被释放（存活计数回到原值）；
 *    W7      Stop 之后不再执行新请求（Submit 返回 0），可以再次 Start，序号不重复；
 *    W8      本地库没打开时结果标记为失败；
 *    W9（隐藏，标签 [.][globalsearchbench]）：20 万条记录下，界面线程提交请求的耗时、查询进行中
 *            界面线程写入一条消息被阻塞的时间、从提交到结果回来的总耗时，只输出数字、不做断言；
 *    W10     库有文件路径时后台查询走自己另开的连接，内存库与没打开的库退回共用连接；
 *            CanOpenSecondConnection 对空串、内存库、URI 路径返回 false；
 *    W11     本地库以 WAL 模式打开；另一条连接开着读事务时，写库立即成功、不等待；
 *    W12     正在执行的查询被同一窗口、同一种查询的新请求取代时被中断、不投递结果，新请求的结果
 *            照常投递；Stop 时正在执行的查询同样被中断；所有结果最终都被释放。
 *  用临时 db 文件跑真实 SQLite；结果用一个只收消息的窗口（HWND_MESSAGE）接收。框架 Catch2 v3。
 *
 *  balloonwj@qq.com   2026-10-03
 */
#include "catch_amalgamated.hpp"
#include "../UI/BalloonHistory/V2MsgSearchWorker.h"
#include "../UI/BalloonHistory/V2MsgStore.h"
#include "../UI/BalloonHistory/V2MsgQuery.h"

#include <stdio.h>   // W9 输出耗时
#include "sqlite3.h"  // W11 另开一条连接持有读事务、查日志模式
#include <string>
#include <vector>
#include <windows.h>

using namespace balloonhistory;

namespace {

//会话类型（与 net::conversation_type 一致）
const int kSingle = 1;
const int kGroup  = 2;
//消息类型（与 net/Protocol.h 的 message_type 一致）
const int kTypeText = 1;
const int kTypeFile = 3;
//测试窗口接收结果用的消息号（窗口私有，任取一个 WM_APP 段的值）
const UINT kMsgResult = WM_APP + 0x7E1;
//等待结果的最长时间（毫秒）
const DWORD kWaitTimeoutMs = 10000;
//等待时每轮睡眠的时间（毫秒）
const DWORD kPollMs = 5;

//测试窗口收到的结果（按到达顺序），用完由用例自己 delete
std::vector<V2SearchResponse*> g_received;

LRESULT CALLBACK ResultWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == kMsgResult)
    {
        V2SearchResponse* resp = (V2SearchResponse*)lParam;
        //wParam 必须与结果里的序号一致
        if (resp != NULL && (uint32_t)wParam == resp->m_seq)
        {
            g_received.push_back(resp);
        }
        else
        {
            delete resp;
        }
        return 0;
    }
    return ::DefWindowProc(hwnd, msg, wParam, lParam);
}

//建一个只收消息的窗口作为结果的投递目标
HWND CreateResultWindow()
{
    static bool s_registered = false;
    const wchar_t* const kClass = L"V2MsgSearchWorkerTestWnd";
    if (!s_registered)
    {
        WNDCLASSEXW wc;
        ::ZeroMemory(&wc, sizeof(wc));
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = ResultWndProc;
        wc.hInstance     = ::GetModuleHandle(NULL);
        wc.lpszClassName = kClass;
        ::RegisterClassExW(&wc);
        s_registered = true;
    }
    return ::CreateWindowExW(0, kClass, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL,
                             ::GetModuleHandle(NULL), NULL);
}

//一边处理本线程的消息一边等，直到收到 count 个结果或超时；返回是否等到
bool PumpUntilReceived(size_t count)
{
    const DWORD start = ::GetTickCount();
    while (g_received.size() < count)
    {
        MSG m;
        while (::PeekMessage(&m, NULL, 0, 0, PM_REMOVE))
        {
            ::TranslateMessage(&m);
            ::DispatchMessage(&m);
        }
        if (g_received.size() >= count)
        {
            break;
        }
        if (::GetTickCount() - start > kWaitTimeoutMs)
        {
            return false;
        }
        ::Sleep(kPollMs);
    }
    return true;
}

//释放并清空已收到的结果
void ClearReceived()
{
    for (size_t i = 0; i < g_received.size(); ++i)
    {
        delete g_received[i];
    }
    g_received.clear();
}

//取一个临时目录下的 db 全路径，并先删掉同名旧文件（文件名带进程号，两份测试进程不会共用）
std::wstring FreshWorkerDb(const wchar_t* name)
{
    wchar_t dir[MAX_PATH] = { 0 };
    ::GetTempPathW(MAX_PATH, dir);
    wchar_t prefix[32] = { 0 };
    ::swprintf_s(prefix, L"%u_", static_cast<unsigned int>(::GetCurrentProcessId()));
    std::wstring path = dir;
    path += prefix;
    path += name;
    ::DeleteFileW(path.c_str());
    //本地库是 WAL 模式：上次异常退出残留的日志文件会被套用到同名新库上，一并删掉
    ::DeleteFileW((path + L"-wal").c_str());
    ::DeleteFileW((path + L"-shm").c_str());
    return path;
}

//构造一条入库记录
HistoryRecord MakeWorkerRecord(int64_t msgid, int ctype, int64_t cid, int msgtype, int64_t timestamp,
                               const std::string& searchText)
{
    HistoryRecord rec;
    rec.m_msgid      = msgid;
    rec.m_ctype      = ctype;
    rec.m_cid        = cid;
    rec.m_fromid     = cid;
    rec.m_msgtype    = msgtype;
    rec.m_timestamp  = timestamp;
    rec.m_searchText = searchText;
    return rec;
}

//往库里放一组固定数据：三个会话里有命中「设计稿」的文字消息与文件消息
void SeedWorkerStore(CV2MsgStore& store)
{
    REQUIRE(store.Upsert(MakeWorkerRecord(1, kSingle, 10, kTypeText, 1000, "周五交设计稿")));
    REQUIRE(store.Upsert(MakeWorkerRecord(2, kSingle, 10, kTypeText, 2000, "设计稿改好了")));
    REQUIRE(store.Upsert(MakeWorkerRecord(3, kGroup, 20, kTypeText, 3000, "群里的设计稿")));
    REQUIRE(store.Upsert(MakeWorkerRecord(4, kGroup, 30, kTypeFile, 4000, "设计稿.pdf")));
    REQUIRE(store.Upsert(MakeWorkerRecord(5, kGroup, 30, kTypeText, 5000, "午饭吃什么")));
}

//构造一条请求
V2SearchRequest MakeRequest(int kind, const std::string& keyword, int limit, HWND target)
{
    V2SearchRequest req;
    req.m_kind        = kind;
    req.m_keywordUtf8 = keyword;
    req.m_limit       = limit;
    req.m_target      = target;
    req.m_msg         = kMsgResult;
    return req;
}

//比较两份会话汇总是否逐项相同
void CheckSameConversations(const std::vector<ConversationHit>& a, const std::vector<ConversationHit>& b)
{
    REQUIRE(a.size() == b.size());
    for (size_t i = 0; i < a.size(); ++i)
    {
        CHECK(a[i].m_ctype == b[i].m_ctype);
        CHECK(a[i].m_cid == b[i].m_cid);
        CHECK(a[i].m_hitCount == b[i].m_hitCount);
        CHECK(a[i].m_latestMsgid == b[i].m_latestMsgid);
        CHECK(a[i].m_latestText == b[i].m_latestText);
    }
}

} // namespace

TEST_CASE("W1 会话汇总请求的结果按序号投回，内容与同步执行一致", "[globalsearch]")
{
    std::wstring path = FreshWorkerDb(L"flamingo_w1.db");
    CV2MsgStore store;
    REQUIRE(store.Open(path.c_str()));
    SeedWorkerStore(store);

    HWND wnd = CreateResultWindow();
    REQUIRE(wnd != NULL);
    CV2MsgSearchWorker worker;
    REQUIRE(worker.Start(&store));

    const V2SearchRequest req = MakeRequest(kV2SearchConversations, "设计稿", 10, wnd);
    const uint32_t seq = worker.Submit(req);
    REQUIRE(seq != 0);
    REQUIRE(PumpUntilReceived(1));
    V2SearchResponse* resp = g_received[0];
    CHECK(resp->m_seq == seq);
    CHECK(resp->m_kind == kV2SearchConversations);
    CHECK(resp->m_keywordUtf8 == "设计稿");
    CHECK(resp->m_ok);

    //与同步执行同一个请求的结果逐项比较
    V2SearchResponse* sync = CV2MsgSearchWorker::Execute(&store, req);
    CHECK(sync->m_ok);
    CheckSameConversations(resp->m_conversations, sync->m_conversations);
    CHECK(resp->m_conversations.size() == 2);   //群 20 与单聊 10；群 30 里只有文件消息命中，不计
    delete sync;

    ClearReceived();
    worker.Stop();
    ::DestroyWindow(wnd);
    store.Close();
    ::DeleteFileW(path.c_str());
}

TEST_CASE("W2 文件消息请求的结果按序号投回，内容与同步执行一致", "[globalsearch]")
{
    std::wstring path = FreshWorkerDb(L"flamingo_w2.db");
    CV2MsgStore store;
    REQUIRE(store.Open(path.c_str()));
    SeedWorkerStore(store);

    HWND wnd = CreateResultWindow();
    REQUIRE(wnd != NULL);
    CV2MsgSearchWorker worker;
    REQUIRE(worker.Start(&store));

    const V2SearchRequest req = MakeRequest(kV2SearchFileMessages, "设计稿", 10, wnd);
    const uint32_t seq = worker.Submit(req);
    REQUIRE(seq != 0);
    REQUIRE(PumpUntilReceived(1));
    V2SearchResponse* resp = g_received[0];
    CHECK(resp->m_seq == seq);
    CHECK(resp->m_ok);
    REQUIRE(resp->m_records.size() == 1);
    CHECK(resp->m_records[0].m_msgid == 4);
    CHECK(resp->m_records[0].m_searchText == "设计稿.pdf");

    V2SearchResponse* sync = CV2MsgSearchWorker::Execute(&store, req);
    REQUIRE(sync->m_records.size() == resp->m_records.size());
    CHECK(sync->m_records[0].m_msgid == resp->m_records[0].m_msgid);
    delete sync;

    ClearReceived();
    worker.Stop();
    ::DestroyWindow(wnd);
    store.Close();
    ::DeleteFileW(path.c_str());
}

TEST_CASE("W3 管理器跨会话搜索带会话范围，结果与同步执行一致", "[globalsearch]")
{
    std::wstring path = FreshWorkerDb(L"flamingo_w3.db");
    CV2MsgStore store;
    REQUIRE(store.Open(path.c_str()));
    SeedWorkerStore(store);

    HWND wnd = CreateResultWindow();
    REQUIRE(wnd != NULL);
    CV2MsgSearchWorker worker;
    REQUIRE(worker.Start(&store));

    //只在单聊 10 与群 30 里搜：群 20 不在范围内；管理器口径下文件消息也计入
    V2SearchRequest req = MakeRequest(kV2SearchManager, "设计稿", 10, wnd);
    req.m_scope.push_back(HistoryConversationKey(kSingle, 10));
    req.m_scope.push_back(HistoryConversationKey(kGroup, 30));
    const uint32_t seq = worker.Submit(req);
    REQUIRE(seq != 0);
    REQUIRE(PumpUntilReceived(1));
    V2SearchResponse* resp = g_received[0];
    CHECK(resp->m_seq == seq);
    CHECK(resp->m_kind == kV2SearchManager);
    CHECK(resp->m_ok);

    V2SearchResponse* sync = CV2MsgSearchWorker::Execute(&store, req);
    CheckSameConversations(resp->m_conversations, sync->m_conversations);
    for (size_t i = 0; i < resp->m_conversations.size(); ++i)
    {
        CHECK_FALSE((resp->m_conversations[i].m_ctype == kGroup && resp->m_conversations[i].m_cid == 20));
    }
    delete sync;

    ClearReceived();
    worker.Stop();
    ::DestroyWindow(wnd);
    store.Close();
    ::DeleteFileW(path.c_str());
}

TEST_CASE("W4 同一窗口同一种查询的未执行请求被替换，且保持原位置", "[globalsearch]")
{
    //只用作队列里的键，不需要是真窗口
    HWND a = (HWND)(UINT_PTR)0x1000;
    HWND b = (HWND)(UINT_PTR)0x2000;

    V2SearchQueue q;
    CHECK_FALSE(q.Push(MakeRequest(kV2SearchConversations, "设", 10, a)));
    CHECK_FALSE(q.Push(MakeRequest(kV2SearchConversations, "设计", 10, b)));
    //窗口 a 接着输入：替换掉队首那条，位置不变
    CHECK(q.Push(MakeRequest(kV2SearchConversations, "设计稿", 10, a)));
    CHECK(q.Size() == 2);

    V2SearchRequest out;
    REQUIRE(q.Pop(out));
    CHECK(out.m_target == a);
    CHECK(out.m_keywordUtf8 == "设计稿");
    REQUIRE(q.Pop(out));
    CHECK(out.m_target == b);
    CHECK(out.m_keywordUtf8 == "设计");
    CHECK_FALSE(q.Pop(out));
}

TEST_CASE("W5 不同窗口或不同种类的请求互不替换", "[globalsearch]")
{
    HWND a = (HWND)(UINT_PTR)0x1000;
    HWND b = (HWND)(UINT_PTR)0x2000;

    V2SearchQueue q;
    CHECK_FALSE(q.Push(MakeRequest(kV2SearchConversations, "x", 10, a)));
    CHECK_FALSE(q.Push(MakeRequest(kV2SearchFileMessages, "x", 10, a)));
    CHECK_FALSE(q.Push(MakeRequest(kV2SearchConversations, "x", 10, b)));
    CHECK_FALSE(q.Push(MakeRequest(kV2SearchManager, "x", 10, b)));
    CHECK(q.Size() == 4);

    q.Clear();
    CHECK(q.Size() == 0);
}

TEST_CASE("W6 目标窗口不在了或来不及处理时，结果都会被释放", "[globalsearch]")
{
    std::wstring path = FreshWorkerDb(L"flamingo_w6.db");
    CV2MsgStore store;
    REQUIRE(store.Open(path.c_str()));
    SeedWorkerStore(store);

    const long baseline = V2SearchResponseCounter::Live();
    CV2MsgSearchWorker worker;
    REQUIRE(worker.Start(&store));

    //活着的窗口先建好：若在销毁 gone 之后才建，系统可能把同一个句柄值分给它，
    //发给 gone 的结果就会被它收到
    HWND live = CreateResultWindow();
    REQUIRE(live != NULL);

    //一、目标窗口在投递之前就销毁了：后台线程自己释放结果
    HWND gone = CreateResultWindow();
    REQUIRE(gone != NULL);
    ::DestroyWindow(gone);
    REQUIRE(worker.Submit(MakeRequest(kV2SearchConversations, "设计稿", 10, gone)) != 0);

    //随后提交给活着的窗口：先进先出，等它的结果到了，说明上一条已经处理完
    REQUIRE(worker.Submit(MakeRequest(kV2SearchFileMessages, "设计稿", 10, live)) != 0);
    REQUIRE(PumpUntilReceived(1));
    ClearReceived();
    CHECK(V2SearchResponseCounter::Live() == baseline);

    //二、结果已经投进消息队列、窗口还没处理：DrainPendingResponses 取出释放
    REQUIRE(worker.Submit(MakeRequest(kV2SearchConversations, "设计稿", 10, live)) != 0);
    const DWORD start = ::GetTickCount();
    MSG m;
    bool queued = false;
    while (::GetTickCount() - start < kWaitTimeoutMs)
    {
        if (::PeekMessage(&m, live, kMsgResult, kMsgResult, PM_NOREMOVE))
        {
            queued = true;
            break;
        }
        ::Sleep(kPollMs);
    }
    REQUIRE(queued);
    CHECK(V2SearchResponseCounter::Live() == baseline + 1);
    CHECK(CV2MsgSearchWorker::DrainPendingResponses(live, kMsgResult) == 1);
    CHECK(V2SearchResponseCounter::Live() == baseline);

    worker.Stop();
    ::DestroyWindow(live);
    store.Close();
    ::DeleteFileW(path.c_str());
}

TEST_CASE("W7 Stop 之后不再执行新请求，可以再次 Start，序号不重复", "[globalsearch]")
{
    std::wstring path = FreshWorkerDb(L"flamingo_w7.db");
    CV2MsgStore store;
    REQUIRE(store.Open(path.c_str()));
    SeedWorkerStore(store);

    HWND wnd = CreateResultWindow();
    REQUIRE(wnd != NULL);
    CV2MsgSearchWorker worker;

    //没启动时提交无效
    CHECK_FALSE(worker.IsRunning());
    CHECK(worker.Submit(MakeRequest(kV2SearchConversations, "设计稿", 10, wnd)) == 0);

    REQUIRE(worker.Start(&store));
    CHECK(worker.IsRunning());
    const uint32_t first = worker.Submit(MakeRequest(kV2SearchConversations, "设计稿", 10, wnd));
    REQUIRE(first != 0);
    REQUIRE(PumpUntilReceived(1));
    ClearReceived();

    worker.Stop();
    CHECK_FALSE(worker.IsRunning());
    CHECK(worker.Submit(MakeRequest(kV2SearchConversations, "设计稿", 10, wnd)) == 0);
    //Stop 可重复调用
    worker.Stop();

    //再次启动后照常工作，序号接着往上走
    REQUIRE(worker.Start(&store));
    const uint32_t second = worker.Submit(MakeRequest(kV2SearchConversations, "设计稿", 10, wnd));
    CHECK(second > first);
    REQUIRE(PumpUntilReceived(1));
    CHECK(g_received[0]->m_seq == second);
    ClearReceived();

    //请求无效（没有目标窗口或消息号）时不执行
    V2SearchRequest bad = MakeRequest(kV2SearchConversations, "设计稿", 10, NULL);
    CHECK(worker.Submit(bad) == 0);

    worker.Stop();
    ::DestroyWindow(wnd);
    store.Close();
    ::DeleteFileW(path.c_str());
}

TEST_CASE("W8 本地库没打开时结果标记为失败", "[globalsearch]")
{
    CV2MsgStore store;   //不打开
    HWND wnd = CreateResultWindow();
    REQUIRE(wnd != NULL);
    CV2MsgSearchWorker worker;
    REQUIRE(worker.Start(&store));

    REQUIRE(worker.Submit(MakeRequest(kV2SearchConversations, "设计稿", 10, wnd)) != 0);
    REQUIRE(PumpUntilReceived(1));
    CHECK_FALSE(g_received[0]->m_ok);
    CHECK(g_received[0]->m_conversations.empty());
    ClearReceived();

    //NULL 库的同步执行同样返回失败结果
    V2SearchResponse* resp = CV2MsgSearchWorker::Execute(NULL, MakeRequest(kV2SearchFileMessages, "x", 10, wnd));
    CHECK_FALSE(resp->m_ok);
    delete resp;

    worker.Stop();
    ::DestroyWindow(wnd);
}

namespace {

//W9 基准的规模与 GlobalSearchStoreTests.cpp 的 GSM9 相同：400 个会话 × 每个 500 条
const int kW9Conversations = 400;
const int kW9PerConversation = 500;
//每多少条放一条文件消息 / 一条命中消息
const int kW9FileEvery = 50;
const int kW9HitEvery = 100;
//每批写入多少条
const size_t kW9Batch = 5000;

//当前时刻（毫秒，带小数）
double W9NowMs()
{
    LARGE_INTEGER freq;
    LARGE_INTEGER now;
    ::QueryPerformanceFrequency(&freq);
    ::QueryPerformanceCounter(&now);
    return (double)now.QuadPart * 1000.0 / (double)freq.QuadPart;
}

} // namespace

TEST_CASE("W9 后台查询时界面线程的耗时（基准，不参与常规运行）", "[.][globalsearchbench]")
{
    std::wstring path = FreshWorkerDb(L"flamingo_w9.db");
    CV2MsgStore store;
    REQUIRE(store.Open(path.c_str()));

    //造 20 万条记录（与 GSM9 同样的数据形态）
    std::vector<HistoryRecord> batch;
    batch.reserve(kW9Batch);
    int64_t msgid = 1;
    for (int c = 0; c < kW9Conversations; ++c)
    {
        const int ctype = (c % 2 == 0) ? kSingle : kGroup;
        const int64_t cid = 1000 + c;
        for (int i = 0; i < kW9PerConversation; ++i)
        {
            const int64_t ts = 1700000000000LL + msgid * 1000;
            if (i % kW9FileEvery == 0)
            {
                batch.push_back(MakeWorkerRecord(msgid, ctype, cid, kTypeFile, ts, "季度报告-附件.pdf"));
            }
            else if (i % kW9HitEvery == 1)
            {
                batch.push_back(MakeWorkerRecord(msgid, ctype, cid, kTypeText, ts, "设计稿已经放到共享盘了"));
            }
            else
            {
                batch.push_back(MakeWorkerRecord(msgid, ctype, cid, kTypeText, ts, "收到，我晚点把材料整理好发到群里"));
            }
            ++msgid;
            if (batch.size() >= kW9Batch)
            {
                REQUIRE(store.UpsertBatch(batch));
                batch.clear();
            }
        }
    }
    if (!batch.empty())
    {
        REQUIRE(store.UpsertBatch(batch));
    }

    HWND wnd = CreateResultWindow();
    REQUIRE(wnd != NULL);
    CV2MsgSearchWorker worker;
    REQUIRE(worker.Start(&store));

    //界面线程提交两种查询的耗时
    const double t0 = W9NowMs();
    REQUIRE(worker.Submit(MakeRequest(kV2SearchConversations, "设计稿", 50, wnd)) != 0);
    REQUIRE(worker.Submit(MakeRequest(kV2SearchFileMessages, "报告", 50, wnd)) != 0);
    const double submitMs = W9NowMs() - t0;

    //查询进行中界面线程写入一条新消息：要等后台查询释放本地库的锁
    ::Sleep(kPollMs);
    const double t1 = W9NowMs();
    REQUIRE(store.Upsert(MakeWorkerRecord(msgid, kSingle, 1000, kTypeText, 1800000000000LL, "新消息")));
    const double upsertMs = W9NowMs() - t1;

    //从提交到两份结果都回来的总耗时
    REQUIRE(PumpUntilReceived(2));
    const double totalMs = W9NowMs() - t0;
    std::printf("W9 submit %.3f ms, upsert during search %.1f ms, both results after %.1f ms "
                "(background: conversations %.1f ms, files %.1f ms)\n",
                submitMs, upsertMs, totalMs, g_received[0]->m_elapsedMs, g_received[1]->m_elapsedMs);
    std::fflush(stdout);

    ClearReceived();
    worker.Stop();
    ::DestroyWindow(wnd);
    store.Close();
    ::DeleteFileW(path.c_str());
}

TEST_CASE("W10 有文件路径的库走自己另开的连接，内存库与没打开的库退回共用连接", "[globalsearch]")
{
    CHECK_FALSE(CV2MsgSearchWorker::CanOpenSecondConnection(L""));
    CHECK_FALSE(CV2MsgSearchWorker::CanOpenSecondConnection(L":memory:"));
    CHECK_FALSE(CV2MsgSearchWorker::CanOpenSecondConnection(L"file::memory:"));
    CHECK(CV2MsgSearchWorker::CanOpenSecondConnection(L"C:\\Users\\a\\ChatLog\\msgv2.db"));

    HWND wnd = CreateResultWindow();
    REQUIRE(wnd != NULL);

    //文件库：另开连接，查询结果与同步执行一致
    {
        std::wstring path = FreshWorkerDb(L"flamingo_w10.db");
        CV2MsgStore store;
        REQUIRE(store.Open(path.c_str()));
        CHECK(store.DbPath() == path);
        SeedWorkerStore(store);

        CV2MsgSearchWorker worker;
        CHECK_FALSE(worker.UsesOwnConnection());
        REQUIRE(worker.Start(&store));
        CHECK(worker.UsesOwnConnection());
        REQUIRE(worker.Submit(MakeRequest(kV2SearchConversations, "设计稿", 10, wnd)) != 0);
        REQUIRE(PumpUntilReceived(1));
        V2SearchResponse* sync = CV2MsgSearchWorker::Execute(&store, MakeRequest(kV2SearchConversations, "设计稿", 10, wnd));
        CHECK(g_received[0]->m_ok);
        CheckSameConversations(g_received[0]->m_conversations, sync->m_conversations);
        delete sync;
        ClearReceived();

        worker.Stop();
        CHECK_FALSE(worker.UsesOwnConnection());
        store.Close();
        CHECK(store.DbPath().empty());
        ::DeleteFileW(path.c_str());
    }

    //内存库：另开连接只会得到另一个空库，退回共用连接，照样查得到数据
    {
        CV2MsgStore store;
        REQUIRE(store.Open(L":memory:"));
        SeedWorkerStore(store);
        CV2MsgSearchWorker worker;
        REQUIRE(worker.Start(&store));
        CHECK_FALSE(worker.UsesOwnConnection());
        REQUIRE(worker.Submit(MakeRequest(kV2SearchConversations, "设计稿", 10, wnd)) != 0);
        REQUIRE(PumpUntilReceived(1));
        CHECK(g_received[0]->m_ok);
        //「设计稿」命中三个会话，其中 (2,30) 那条是文件消息，归「文件」标签，不计入会话汇总
        CHECK(g_received[0]->m_conversations.size() == 2);
        ClearReceived();
        worker.Stop();
    }

    //没打开的库：退回共用连接（查询得到失败结果，见 W8）
    {
        CV2MsgStore store;
        CV2MsgSearchWorker worker;
        REQUIRE(worker.Start(&store));
        CHECK_FALSE(worker.UsesOwnConnection());
        worker.Stop();
    }

    ::DestroyWindow(wnd);
}

TEST_CASE("W11 本地库以 WAL 模式打开，另一条连接开着读事务时写库不等待", "[globalsearch]")
{
    //写库允许的最长耗时（毫秒）。非 WAL 模式下写库要等读事务结束，会一直等到忙等待超时（3 秒）
    //后失败；WAL 模式下立即成功，留足余量也远小于 3 秒。
    const double kMaxWriteMs = 1000.0;

    std::wstring path = FreshWorkerDb(L"flamingo_w11.db");
    CV2MsgStore store;
    REQUIRE(store.Open(path.c_str()));
    SeedWorkerStore(store);

    //另一条连接：查日志模式，并开一个读事务停在半路
    sqlite3* reader = NULL;
    REQUIRE(sqlite3_open16(path.c_str(), &reader) == SQLITE_OK);
    sqlite3_stmt* mode = NULL;
    REQUIRE(sqlite3_prepare_v2(reader, "PRAGMA journal_mode", -1, &mode, NULL) == SQLITE_OK);
    REQUIRE(sqlite3_step(mode) == SQLITE_ROW);
    const unsigned char* modeText = sqlite3_column_text(mode, 0);
    CHECK(std::string(modeText != NULL ? (const char*)modeText : "") == "wal");
    sqlite3_finalize(mode);

    REQUIRE(sqlite3_exec(reader, "BEGIN", NULL, NULL, NULL) == SQLITE_OK);
    sqlite3_stmt* scan = NULL;
    REQUIRE(sqlite3_prepare_v2(reader, "SELECT msgid FROM t_msg", -1, &scan, NULL) == SQLITE_OK);
    REQUIRE(sqlite3_step(scan) == SQLITE_ROW);   //读事务已经开始，语句停在第一行

    //读事务进行中写入一条：立即成功
    const double t0 = W9NowMs();
    CHECK(store.Upsert(MakeWorkerRecord(100, kSingle, 10, kTypeText, 9000, "读事务进行中写入")));
    const double writeMs = W9NowMs() - t0;
    CHECK(writeMs < kMaxWriteMs);

    //读事务看到的仍是开始时的快照（共 5 条），结束之后才看得到新写入的那一条
    int rows = 1;
    while (sqlite3_step(scan) == SQLITE_ROW)
    {
        ++rows;
    }
    CHECK(rows == 5);
    sqlite3_finalize(scan);
    REQUIRE(sqlite3_exec(reader, "COMMIT", NULL, NULL, NULL) == SQLITE_OK);
    HistoryRecord rec;
    bool found = false;
    REQUIRE(store.QueryRecord(kSingle, 10, 100, rec, found));
    CHECK(found);

    sqlite3_close(reader);
    store.Close();
    ::DeleteFileW(path.c_str());
}

namespace {

//W12 的数据规模：需要一次查询能持续上百毫秒，才来得及在它执行期间提交新请求。关键字用两个字，
//走 LIKE 整表扫描（全文索引只对三个字及以上的关键字生效）。
const int kW12Conversations = 200;
const int kW12PerConversation = 500;
//每次查询最多返回几个会话（少于会话数，「材料」命中全部会话时结果条数就是它）
const int kW12Limit = 50;
//等查询开始执行的最长时间（毫秒）
const DWORD kW12StartTimeoutMs = 5000;
//新请求的结果到了之后再多等一会儿（毫秒），确认被中断的那个没有迟到投递
const DWORD kW12SettleMs = 300;

//造 W12 用的数据：每个会话里每 100 条有一条命中「设计」，其余命中「材料」
void SeedW12Store(CV2MsgStore& store)
{
    std::vector<HistoryRecord> batch;
    batch.reserve(kW9Batch);
    int64_t msgid = 1;
    for (int c = 0; c < kW12Conversations; ++c)
    {
        const int64_t cid = 1000 + c;
        for (int i = 0; i < kW12PerConversation; ++i)
        {
            const int64_t ts = 1700000000000LL + msgid * 1000;
            const char* text = (i % kW9HitEvery == 1) ? "设计稿已经放到共享盘了" : "收到，我晚点把材料整理好发到群里";
            batch.push_back(MakeWorkerRecord(msgid, kSingle, cid, kTypeText, ts, text));
            ++msgid;
            if (batch.size() >= kW9Batch)
            {
                REQUIRE(store.UpsertBatch(batch));
                batch.clear();
            }
        }
    }
    if (!batch.empty())
    {
        REQUIRE(store.UpsertBatch(batch));
    }
}

//等后台线程开始执行序号为 seq 的请求；超时返回 false
bool WaitExecuting(const CV2MsgSearchWorker& worker, uint32_t seq)
{
    const DWORD start = ::GetTickCount();
    while (worker.ExecutingSeq() != seq)
    {
        if (::GetTickCount() - start > kW12StartTimeoutMs)
        {
            return false;
        }
        ::Sleep(0);
    }
    return true;
}

//一边处理消息一边等 ms 毫秒
void PumpFor(DWORD ms)
{
    const DWORD start = ::GetTickCount();
    while (::GetTickCount() - start < ms)
    {
        MSG m;
        while (::PeekMessage(&m, NULL, 0, 0, PM_REMOVE))
        {
            ::TranslateMessage(&m);
            ::DispatchMessage(&m);
        }
        ::Sleep(kPollMs);
    }
}

} // namespace

TEST_CASE("W12 被同类新请求取代的查询被中断、不投递，Stop 时同样中断", "[globalsearch]")
{
    std::wstring path = FreshWorkerDb(L"flamingo_w12.db");
    CV2MsgStore store;
    REQUIRE(store.Open(path.c_str()));
    SeedW12Store(store);

    HWND wnd = CreateResultWindow();
    REQUIRE(wnd != NULL);
    const long baseline = V2SearchResponseCounter::Live();
    CV2MsgSearchWorker worker;
    REQUIRE(worker.Start(&store));
    REQUIRE(worker.UsesOwnConnection());

    //一、A 执行期间提交同一窗口、同一种查询 B：A 被中断、不投递，只收到 B
    const uint32_t seqA = worker.Submit(MakeRequest(kV2SearchConversations, "设计", kW12Limit, wnd));
    REQUIRE(seqA != 0);
    REQUIRE(WaitExecuting(worker, seqA));
    const uint32_t seqB = worker.Submit(MakeRequest(kV2SearchConversations, "材料", kW12Limit, wnd));
    REQUIRE(seqB != 0);
    REQUIRE(PumpUntilReceived(1));
    PumpFor(kW12SettleMs);
    REQUIRE(g_received.size() == 1);
    CHECK(g_received[0]->m_seq == seqB);
    CHECK(g_received[0]->m_ok);
    CHECK(g_received[0]->m_conversations.size() == (size_t)kW12Limit);
    ClearReceived();

    //二、另一种查询（文件消息）不会中断正在执行的会话汇总：两个结果都到
    const uint32_t seqC = worker.Submit(MakeRequest(kV2SearchConversations, "设计", kW12Limit, wnd));
    REQUIRE(seqC != 0);
    REQUIRE(WaitExecuting(worker, seqC));
    REQUIRE(worker.Submit(MakeRequest(kV2SearchFileMessages, "设计", kW12Limit, wnd)) != 0);
    REQUIRE(PumpUntilReceived(2));
    CHECK(g_received[0]->m_seq == seqC);
    CHECK(g_received[0]->m_ok);
    ClearReceived();

    //三、Stop 时正在执行的查询被中断，不投递
    const uint32_t seqD = worker.Submit(MakeRequest(kV2SearchConversations, "设计", kW12Limit, wnd));
    REQUIRE(seqD != 0);
    REQUIRE(WaitExecuting(worker, seqD));
    worker.Stop();
    PumpFor(kW12SettleMs);
    CHECK(g_received.empty());
    ClearReceived();

    //所有结果都已释放
    CHECK(V2SearchResponseCounter::Live() == baseline);

    ::DestroyWindow(wnd);
    store.Close();
    ::DeleteFileW(path.c_str());
}
