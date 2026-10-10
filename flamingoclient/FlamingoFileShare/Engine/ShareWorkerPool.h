/**
 *  分享者一端的工作线程池，ShareWorkerPool.h
 *
 *  docs/文件共享方案.md 7.5：分享者执行搜索、写操作（以及后续的缩略图）不在中转通道的接收线程上做，而是交给固定条数的
 *  工作线程，一个慢请求（例如对整个磁盘的搜索）不会阻塞同一访问会话上的其它请求（列目录、取消）。
 *
 *  约定：
 *    - 任务是 IShareTask 的实现，投递后归线程池所有，执行完（或线程池停止时未执行）即销毁；
 *    - Stop 不再执行队列里尚未开始的任务（直接销毁），等正在执行的任务结束后返回。任务自己要能尽快结束
 *      （搜索任务看取消标志，写操作本身很快）；
 *    - 线程池由 CShareService 持有，在 Detach 里停止：DLL 卸载时（持有加载器锁）不能再去等线程，所以不能依赖静态对象的析构。
 *
 *  单元测试见 FlamingoFileShare/Tests/ShareWorkerPoolTests.cpp（标签 [shareworker]）。
 *
 *  典型用法：
 *      CShareWorkerPool pool;
 *      pool.Start(kShareWorkerThreads);
 *      std::unique_ptr<IShareTask> task(new SomeTask(...));
 *      if (!pool.Post(task))
 *          task->Run();
 *      ...
 *      pool.Stop();
 *
 *  balloonwj@qq.com   2026-10-03
 */
#pragma once

#include <stddef.h>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace fileshare {

//分享者一端工作线程的条数（方案 7.5）
const size_t kShareWorkerThreads = 4;

/**
 *  一个在工作线程上执行的任务。
 */
class IShareTask
{
public:
    virtual ~IShareTask()
    {
    }

    //在某条工作线程上执行一次
    virtual void Run() = 0;
};

/**
 *  CShareWorkerPool —— 固定条数的工作线程与一个先进先出的任务队列。线程安全；不可拷贝。
 */
class CShareWorkerPool
{
public:
    CShareWorkerPool();

    //析构时停止（等正在执行的任务结束）
    ~CShareWorkerPool();

    CShareWorkerPool(const CShareWorkerPool&) = delete;
    CShareWorkerPool& operator=(const CShareWorkerPool&) = delete;

    /**
     *  启动工作线程；已启动时什么也不做。
     *  @param threads 线程条数，须 > 0
     *  @return 已在运行（含本次启动成功）返回 true
     */
    bool Start(size_t threads);

    /**
     *  停止：丢弃尚未开始的任务，等正在执行的任务结束，回收全部线程。可重复调用。
     */
    void Stop();

    /**
     *  投递一个任务。
     *  @param task 任务，不可为空；排进队列时所有权转给线程池（task 变为空），失败时保持不变，调用方可以自己执行
     *  @return 已排进队列返回 true；线程池未运行时返回 false
     */
    bool Post(std::unique_ptr<IShareTask>& task);

    //是否在运行
    bool IsRunning() const;

    //队列里尚未开始的任务数
    size_t GetQueuedCount() const;

private:
    //工作线程：取任务、执行，直到停止
    void ThreadProc();

private:
    mutable std::mutex                       m_mutex;    //保护下面三项
    std::condition_variable                  m_cv;       //有任务或要停止时唤醒工作线程
    std::deque<std::unique_ptr<IShareTask> > m_queue;    //尚未开始的任务
    bool                                     m_running;  //是否在运行；Stop 时置假
    std::vector<std::thread>                 m_threads;  //工作线程；只在 Start / Stop 的调用线程上增删
};

} // namespace fileshare
