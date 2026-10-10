/**
 *  数据库连接句柄池 DbHandlePool 的单元测试（Suite PL），test_dbhandlepool.cpp
 *
 *  这套用例只测池本身的规则：取用顺序、按连接目标区分、容量上限、取用前校验、
 *  保活、收缩、清空，以及多线程并发取用与归还。句柄用 FakeHandle 代替真实连接，
 *  时钟用可控的假时钟，所以不需要任何数据库，Windows 与 Linux 的默认测试目标都能跑。
 *  连真实 MySQL 的用例在 test_mysqlpool.cpp（Suite PM）。
 *
 *  balloonwj@qq.com   2026-10-04
 */
#include <atomic>
#include <string>
#include <thread>
#include <vector>

#include "doctest.h"
#include "../dbapi/DbHandlePool.h"

namespace {

//代替真实连接的假句柄
struct FakeHandle
{
    bool             m_alive;        //被 ping 时返回的结果：true 表示连接仍然可用
    int              m_pingCount;    //被 ping 的次数。只在主线程里读写（PL9 不 ping）
    int              m_closeCount;   //被关闭的次数，正常情况下最多为 1。只在主线程里读写（PL9 在各线程结束后才清空池）
    std::atomic<int> m_inUse;        //当前有几处在使用它，PL9 用来检查同一个句柄是否同时交给了两处

    FakeHandle()
        : m_alive(true), m_pingCount(0), m_closeCount(0), m_inUse(0)
    {
    }
};

//假时钟的当前值，单位毫秒，由用例直接改写
int64_t g_fakeNowMs = 0;

int64_t fakeNow()
{
    return g_fakeNowMs;
}

void fakeClose(void* handle)
{
    static_cast<FakeHandle*>(handle)->m_closeCount++;
}

bool fakePing(void* handle)
{
    FakeHandle* h = static_cast<FakeHandle*>(handle);
    h->m_pingCount++;
    return h->m_alive;
}

//本套用例共用的连接目标
const char* const PL_KEY_A = "mysql://127.0.0.1/root/flamingo";
const char* const PL_KEY_B = "mysql://127.0.0.1/root/flamingo_other";

//构造一组阈值：除 PL4 外容量都给足，三条时间规则默认全部关闭，由各用例按需打开
DbHandlePool::Options makeOptions()
{
    DbHandlePool::Options opt;
    opt.m_maxIdle = 16;
    opt.m_validateAfterMs = -1;
    opt.m_keepAliveAfterMs = -1;
    opt.m_maxIdleMs = -1;
    return opt;
}

//PL9：并发线程数与每条线程取用、归还的轮数
const int PL9_THREADS = 8;
const int PL9_ROUNDS = 2000;

//PL9 共享的状态
struct Pl9Shared
{
    DbHandlePool*    m_pool;          //被测的池
    FakeHandle*      m_handles;       //预先分配好的假句柄数组，新建连接时按顺序取用
    int              m_capacity;      //m_handles 的长度
    std::atomic<int> m_created;       //已经「新建」了几个句柄
    std::atomic<int> m_violations;    //发现同一个句柄同时被两处使用的次数
    std::atomic<int> m_overflow;      //需要新建句柄但预分配的数组已经用完的次数
};

//PL9 的线程函数：反复取用一个句柄（取不到就新建）、确认只有自己在用、再归还
void pl9Worker(Pl9Shared* shared)
{
    for (int round = 0; round < PL9_ROUNDS; ++round)
    {
        FakeHandle* h = static_cast<FakeHandle*>(shared->m_pool->acquire(PL_KEY_A));
        if (h == NULL)
        {
            int index = shared->m_created.fetch_add(1);
            if (index >= shared->m_capacity)
            {
                shared->m_overflow++;
                continue;
            }
            h = &shared->m_handles[index];
        }

        if (h->m_inUse.fetch_add(1) != 0)
            shared->m_violations++;
        std::this_thread::yield();
        h->m_inUse.fetch_sub(1);

        shared->m_pool->release(PL_KEY_A, h);
    }
}

} // anonymous namespace

TEST_CASE("PL1 a released handle is handed out again by acquire")
{
    g_fakeNowMs = 0;
    DbHandlePool pool(fakeClose, fakePing, makeOptions(), fakeNow);
    pool.setEnabled(true);
    FakeHandle h1;

    CHECK(pool.acquire(PL_KEY_A) == NULL);

    pool.release(PL_KEY_A, &h1);
    CHECK(pool.idleCount() == 1);
    CHECK(pool.acquire(PL_KEY_A) == &h1);
    CHECK(pool.idleCount() == 0);

    //池里只有一个句柄，交出去之后再取就没有了
    CHECK(pool.acquire(PL_KEY_A) == NULL);
    CHECK(h1.m_closeCount == 0);

    DbHandlePool::Stats stats = pool.getStats();
    CHECK(stats.m_acquireHits == 1);
    CHECK(stats.m_acquireMisses == 2);
}

TEST_CASE("PL2 handles of different keys are never mixed")
{
    g_fakeNowMs = 0;
    DbHandlePool pool(fakeClose, fakePing, makeOptions(), fakeNow);
    pool.setEnabled(true);
    FakeHandle h1;

    pool.release(PL_KEY_A, &h1);
    CHECK(pool.acquire(PL_KEY_B) == NULL);
    CHECK(pool.acquire(PL_KEY_A) == &h1);
}

TEST_CASE("PL3 the most recently released handle is handed out first")
{
    g_fakeNowMs = 0;
    DbHandlePool pool(fakeClose, fakePing, makeOptions(), fakeNow);
    pool.setEnabled(true);
    FakeHandle h1;
    FakeHandle h2;

    pool.release(PL_KEY_A, &h1);
    pool.release(PL_KEY_A, &h2);
    CHECK(pool.acquire(PL_KEY_A) == &h2);
    CHECK(pool.acquire(PL_KEY_A) == &h1);
}

TEST_CASE("PL4 the earliest released handle is closed when the pool is over capacity")
{
    g_fakeNowMs = 0;
    DbHandlePool::Options opt = makeOptions();
    opt.m_maxIdle = 2;
    DbHandlePool pool(fakeClose, fakePing, opt, fakeNow);
    pool.setEnabled(true);
    FakeHandle h1;
    FakeHandle h2;
    FakeHandle h3;

    pool.release(PL_KEY_A, &h1);
    pool.release(PL_KEY_A, &h2);
    pool.release(PL_KEY_A, &h3);

    CHECK(h1.m_closeCount == 1);
    CHECK(h2.m_closeCount == 0);
    CHECK(h3.m_closeCount == 0);
    CHECK(pool.idleCount() == 2);
    CHECK(pool.getStats().m_closedOverflow == 1);

    CHECK(pool.acquire(PL_KEY_A) == &h3);
    CHECK(pool.acquire(PL_KEY_A) == &h2);
}

TEST_CASE("PL5 a handle idle beyond the validate threshold is pinged before being handed out")
{
    g_fakeNowMs = 0;
    DbHandlePool::Options opt = makeOptions();
    opt.m_validateAfterMs = 1000;
    DbHandlePool pool(fakeClose, fakePing, opt, fakeNow);
    pool.setEnabled(true);
    FakeHandle h1;
    FakeHandle h2;

    SUBCASE("not pinged before the threshold")
    {
        pool.release(PL_KEY_A, &h1);
        g_fakeNowMs = 999;
        CHECK(pool.acquire(PL_KEY_A) == &h1);
        CHECK(h1.m_pingCount == 0);
        CHECK(pool.getStats().m_validatePings == 0);
    }

    SUBCASE("pinged at the threshold and handed out when alive")
    {
        pool.release(PL_KEY_A, &h1);
        g_fakeNowMs = 1000;
        CHECK(pool.acquire(PL_KEY_A) == &h1);
        CHECK(h1.m_pingCount == 1);
        CHECK(h1.m_closeCount == 0);
        CHECK(pool.getStats().m_validatePings == 1);
    }

    SUBCASE("a dead handle is closed and the next one is tried")
    {
        h2.m_alive = false;
        pool.release(PL_KEY_A, &h1);
        pool.release(PL_KEY_A, &h2);
        g_fakeNowMs = 5000;

        //h2 是最近归还的，先被取出、ping 失败、关闭；接着取 h1，ping 成功后交出
        CHECK(pool.acquire(PL_KEY_A) == &h1);
        CHECK(h2.m_pingCount == 1);
        CHECK(h2.m_closeCount == 1);
        CHECK(h1.m_pingCount == 1);
        CHECK(h1.m_closeCount == 0);
        CHECK(pool.getStats().m_pingFailures == 1);
        CHECK(pool.idleCount() == 0);
    }

    SUBCASE("acquire returns NULL when every candidate is dead")
    {
        h1.m_alive = false;
        pool.release(PL_KEY_A, &h1);
        g_fakeNowMs = 5000;
        CHECK(pool.acquire(PL_KEY_A) == NULL);
        CHECK(h1.m_closeCount == 1);
    }

    SUBCASE("validateAfterMs = 0 pings on every acquire")
    {
        opt.m_validateAfterMs = 0;
        pool.setOptions(opt);
        pool.release(PL_KEY_A, &h1);
        CHECK(pool.acquire(PL_KEY_A) == &h1);
        CHECK(h1.m_pingCount == 1);
    }
}

TEST_CASE("PL5b a backend without ping never validates")
{
    g_fakeNowMs = 0;
    DbHandlePool::Options opt = makeOptions();
    opt.m_validateAfterMs = 0;
    opt.m_keepAliveAfterMs = 0;
    DbHandlePool pool(fakeClose, NULL, opt, fakeNow);
    pool.setEnabled(true);
    FakeHandle h1;

    pool.release(PL_KEY_A, &h1);
    g_fakeNowMs = 100000;
    pool.maintain();
    CHECK(pool.acquire(PL_KEY_A) == &h1);
    CHECK(h1.m_pingCount == 0);
    CHECK(h1.m_closeCount == 0);
}

TEST_CASE("PL6 maintain pings idle handles to keep them alive")
{
    g_fakeNowMs = 0;
    DbHandlePool::Options opt = makeOptions();
    opt.m_keepAliveAfterMs = 1000;
    opt.m_validateAfterMs = 1500;
    DbHandlePool pool(fakeClose, fakePing, opt, fakeNow);
    pool.setEnabled(true);
    FakeHandle h1;
    FakeHandle h2;
    FakeHandle h3;

    h2.m_alive = false;
    pool.release(PL_KEY_A, &h1);
    pool.release(PL_KEY_A, &h2);
    g_fakeNowMs = 100;
    pool.release(PL_KEY_A, &h3);

    //没到保活阈值：一个都不 ping
    g_fakeNowMs = 999;
    pool.maintain();
    CHECK(h1.m_pingCount == 0);
    CHECK(h2.m_pingCount == 0);

    //到了阈值：h1、h2 各 ping 一次，ping 不通的 h2 被关闭；h3 归还得晚，还没到阈值
    g_fakeNowMs = 1000;
    pool.maintain();
    CHECK(h1.m_pingCount == 1);
    CHECK(h2.m_pingCount == 1);
    CHECK(h2.m_closeCount == 1);
    CHECK(h3.m_pingCount == 0);
    CHECK(pool.idleCount() == 2);

    DbHandlePool::Stats stats = pool.getStats();
    CHECK(stats.m_keepAlivePings == 2);
    CHECK(stats.m_pingFailures == 1);

    //h1 刚被 ping 过、往来时刻已刷新为 1000，所以 2000 时距上次往来只有 1000 毫秒，
    //不到取用前校验的 1500，不必再 ping。先取出的是归还最晚的 h3（距上次往来 1900，要校验）
    g_fakeNowMs = 2000;
    CHECK(pool.acquire(PL_KEY_A) == &h3);
    CHECK(h3.m_pingCount == 1);
    CHECK(pool.acquire(PL_KEY_A) == &h1);
    CHECK(h1.m_pingCount == 1);
}

TEST_CASE("PL7 maintain closes handles unused beyond maxIdleMs and pings do not count as use")
{
    g_fakeNowMs = 0;
    DbHandlePool::Options opt = makeOptions();
    opt.m_keepAliveAfterMs = 1000;
    opt.m_maxIdleMs = 3000;
    DbHandlePool pool(fakeClose, fakePing, opt, fakeNow);
    pool.setEnabled(true);
    FakeHandle h1;
    FakeHandle h2;

    pool.release(PL_KEY_A, &h1);

    g_fakeNowMs = 1000;
    pool.maintain();
    g_fakeNowMs = 2000;
    pool.maintain();
    CHECK(h1.m_pingCount == 2);
    CHECK(h1.m_closeCount == 0);

    g_fakeNowMs = 2500;
    pool.release(PL_KEY_A, &h2);

    //h1 距上次被业务归还已满 3000 毫秒，尽管期间一直被 ping，也要关闭；h2 留下
    g_fakeNowMs = 3000;
    pool.maintain();
    CHECK(h1.m_closeCount == 1);
    CHECK(h2.m_closeCount == 0);
    CHECK(pool.idleCount() == 1);
    CHECK(pool.getStats().m_closedIdle == 1);
}

TEST_CASE("PL8 clear and the disabled state close handles")
{
    g_fakeNowMs = 0;
    DbHandlePool pool(fakeClose, fakePing, makeOptions(), fakeNow);
    FakeHandle h1;
    FakeHandle h2;
    FakeHandle h3;

    SUBCASE("a new pool is disabled: release closes at once and acquire returns NULL")
    {
        CHECK_FALSE(pool.isEnabled());
        pool.release(PL_KEY_A, &h1);
        CHECK(h1.m_closeCount == 1);
        CHECK(pool.idleCount() == 0);
        CHECK(pool.acquire(PL_KEY_A) == NULL);
    }

    SUBCASE("clear closes every idle handle and keeps the counters")
    {
        pool.setEnabled(true);
        pool.release(PL_KEY_A, &h1);
        pool.release(PL_KEY_B, &h2);
        CHECK(pool.acquire(PL_KEY_A) == &h1);
        pool.release(PL_KEY_A, &h1);

        pool.clear();
        CHECK(h1.m_closeCount == 1);
        CHECK(h2.m_closeCount == 1);
        CHECK(pool.idleCount() == 0);
        CHECK(pool.isEnabled());
        CHECK(pool.getStats().m_acquireHits == 1);
    }

    SUBCASE("disabling closes the idle handles")
    {
        pool.setEnabled(true);
        pool.release(PL_KEY_A, &h1);
        pool.setEnabled(false);
        CHECK(h1.m_closeCount == 1);

        pool.release(PL_KEY_A, &h3);
        CHECK(h3.m_closeCount == 1);
        CHECK(pool.idleCount() == 0);
    }
}

TEST_CASE("PL9 concurrent acquire and release never hand one handle to two users")
{
    DbHandlePool::Options opt = makeOptions();
    opt.m_maxIdle = 64;
    //用真实时钟：多线程下假时钟没有意义，三条时间规则在本用例里都关闭
    DbHandlePool pool(fakeClose, fakePing, opt);
    pool.setEnabled(true);

    std::vector<FakeHandle> handles(PL9_THREADS);
    Pl9Shared shared;
    shared.m_pool = &pool;
    shared.m_handles = &handles[0];
    shared.m_capacity = PL9_THREADS;
    shared.m_created = 0;
    shared.m_violations = 0;
    shared.m_overflow = 0;

    std::vector<std::thread> threads;
    for (int i = 0; i < PL9_THREADS; ++i)
        threads.push_back(std::thread(pl9Worker, &shared));
    for (size_t i = 0; i < threads.size(); ++i)
        threads[i].join();

    //各线程都已结束，把原子计数读成普通整数再断言
    int violations = shared.m_violations.load();
    int overflow = shared.m_overflow.load();
    int created = shared.m_created.load();

    //同一时刻最多每条线程手里一个，所以新建的句柄数不会超过线程数
    CHECK(violations == 0);
    CHECK(overflow == 0);
    CHECK(created <= PL9_THREADS);
    CHECK(pool.idleCount() == (size_t)created);

    DbHandlePool::Stats stats = pool.getStats();
    CHECK(stats.m_acquireHits + stats.m_acquireMisses == (uint64_t)PL9_THREADS * PL9_ROUNDS);
    CHECK(stats.m_acquireMisses == (uint64_t)created);

    pool.clear();
    int closed = 0;
    for (int i = 0; i < PL9_THREADS; ++i)
        closed += handles[i].m_closeCount;
    CHECK(closed == created);
}
