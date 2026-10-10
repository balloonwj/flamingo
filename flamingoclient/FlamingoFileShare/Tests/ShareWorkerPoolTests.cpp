/**
 *  分享者一端工作线程池单元测试（Suite SWP，标签 [shareworker]），ShareWorkerPoolTests.cpp
 *  覆盖（对应 docs/文件共享方案.md 第 2 期第 3 步的 3b）：
 *    - SWP1 投递的任务在工作线程上执行（不在投递线程上），多个任务都执行完；Start 可重复调用；
 *    - SWP2 Stop：正在执行的任务执行完才返回，尚未开始的任务不执行、直接销毁；停止后 Post 失败且任务仍归调用方；
 *    - SWP3 未启动时 Post 失败，任务仍归调用方，可以自己执行。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-03
 */
#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <windows.h>
#include <atomic>
#include <memory>
#include <thread>

#include "../Engine/ShareWorkerPool.h"

using namespace fileshare;

namespace {

//SWP1 投递的任务个数
const int kTaskCount = 20;

//等任务执行完的最长时间，单位：毫秒
const DWORD kWaitMs = 5000;

//SWP2：阻塞任务的放行等待上限，单位：毫秒
const DWORD kGateWaitMs = 5000;

//计数任务：执行时累加计数并记下执行线程；析构时累加销毁计数
class CountTask : public IShareTask
{
public:
    CountTask(std::atomic<int>* runs, std::atomic<int>* destroyed, std::atomic<DWORD>* thread)
        : m_runs(runs)
        , m_destroyed(destroyed)
        , m_thread(thread)
    {
    }

    ~CountTask()
    {
        if (m_destroyed != NULL)
            ++(*m_destroyed);
    }

    virtual void Run() override
    {
        if (m_thread != NULL)
            m_thread->store(::GetCurrentThreadId());
        ++(*m_runs);
    }

private:
    std::atomic<int>*   m_runs;       //执行次数
    std::atomic<int>*   m_destroyed;  //销毁次数（可为 NULL）
    std::atomic<DWORD>* m_thread;     //最近一次执行的线程 id（可为 NULL）
};

//阻塞任务：执行后等放行事件，用来占住唯一的工作线程
class GateTask : public IShareTask
{
public:
    GateTask(HANDLE started, HANDLE release, std::atomic<int>* finished)
        : m_started(started)
        , m_release(release)
        , m_finished(finished)
    {
    }

    virtual void Run() override
    {
        ::SetEvent(m_started);
        ::WaitForSingleObject(m_release, kGateWaitMs);
        ++(*m_finished);
    }

private:
    HANDLE            m_started;   //开始执行时置位
    HANDLE            m_release;   //放行事件
    std::atomic<int>* m_finished;  //执行完的次数
};

//等计数达到 target；超时返回 false
bool WaitCount(const std::atomic<int>& count, int target)
{
    const DWORD start = ::GetTickCount();
    while (count.load() < target && ::GetTickCount() - start < kWaitMs)
        ::Sleep(1);
    return count.load() >= target;
}

} // namespace

TEST_CASE("SWP1 tasks run on worker threads", "[fileshare][shareworker]")
{
    CShareWorkerPool pool;
    REQUIRE(pool.Start(kShareWorkerThreads));
    CHECK(pool.Start(kShareWorkerThreads));
    CHECK(pool.IsRunning());

    std::atomic<int> runs(0);
    std::atomic<DWORD> thread(0);
    for (int i = 0; i < kTaskCount; ++i)
    {
        std::unique_ptr<IShareTask> task(new CountTask(&runs, NULL, &thread));
        REQUIRE(pool.Post(task));
        CHECK_FALSE(task);
    }
    CHECK(WaitCount(runs, kTaskCount));
    CHECK(thread.load() != ::GetCurrentThreadId());
    pool.Stop();
    CHECK_FALSE(pool.IsRunning());
}

TEST_CASE("SWP2 stop waits for running task and drops queued ones", "[fileshare][shareworker]")
{
    CShareWorkerPool pool;
    REQUIRE(pool.Start(1));
    HANDLE started = ::CreateEventW(NULL, TRUE, FALSE, NULL);
    HANDLE release = ::CreateEventW(NULL, TRUE, FALSE, NULL);
    std::atomic<int> gateDone(0);
    std::unique_ptr<IShareTask> gate(new GateTask(started, release, &gateDone));
    REQUIRE(pool.Post(gate));
    REQUIRE(::WaitForSingleObject(started, kWaitMs) == WAIT_OBJECT_0);

    //唯一的线程被占住：后面的任务排在队列里
    std::atomic<int> runs(0);
    std::atomic<int> destroyed(0);
    for (int i = 0; i < kTaskCount; ++i)
    {
        std::unique_ptr<IShareTask> task(new CountTask(&runs, &destroyed, NULL));
        REQUIRE(pool.Post(task));
    }
    CHECK(pool.GetQueuedCount() == static_cast<size_t>(kTaskCount));

    //在另一条线程上停止：停止先丢弃队列、再等正在执行的那个结束，所以此时排队的任务已被销毁、一个也没执行
    std::thread stopper(&CShareWorkerPool::Stop, &pool);
    const DWORD start = ::GetTickCount();
    while (pool.IsRunning() && ::GetTickCount() - start < kWaitMs)
        ::Sleep(1);
    CHECK_FALSE(pool.IsRunning());
    CHECK(WaitCount(destroyed, kTaskCount));
    CHECK(runs.load() == 0);
    CHECK(gateDone.load() == 0);
    //放行：正在执行的那个执行完，Stop 才返回
    ::SetEvent(release);
    stopper.join();
    CHECK(gateDone.load() == 1);
    CHECK(pool.GetQueuedCount() == 0);

    //停止后投递：失败，任务仍归调用方
    std::unique_ptr<IShareTask> late(new CountTask(&runs, NULL, NULL));
    CHECK_FALSE(pool.Post(late));
    REQUIRE(late);
    ::CloseHandle(started);
    ::CloseHandle(release);
}

TEST_CASE("SWP3 post before start", "[fileshare][shareworker]")
{
    CShareWorkerPool pool;
    std::atomic<int> runs(0);
    std::unique_ptr<IShareTask> task(new CountTask(&runs, NULL, NULL));
    CHECK_FALSE(pool.Post(task));
    REQUIRE(task);
    task->Run();
    CHECK(runs.load() == 1);
    CHECK_FALSE(pool.Start(0));
}
