/**
 *  分享者一端工作线程池 CShareWorkerPool 的实现，见 ShareWorkerPool.h。
 *  balloonwj@qq.com   2026-10-03
 */
#include "stdafx.h"

#include "ShareWorkerPool.h"

namespace fileshare {

CShareWorkerPool::CShareWorkerPool()
    : m_running(false)
{
}

CShareWorkerPool::~CShareWorkerPool()
{
    Stop();
}

bool CShareWorkerPool::Start(size_t threads)
{
    if (threads == 0)
        return false;
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        if (m_running)
            return true;
        m_running = true;
    }
    for (size_t i = 0; i < threads; ++i)
        m_threads.push_back(std::thread(&CShareWorkerPool::ThreadProc, this));
    return true;
}

void CShareWorkerPool::Stop()
{
    std::deque<std::unique_ptr<IShareTask> > dropped;
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        m_running = false;
        dropped.swap(m_queue);
    }
    m_cv.notify_all();
    //尚未开始的任务先销毁（在锁外：任务的析构可能做些收尾，如释放共享状态），再等正在执行的任务结束
    dropped.clear();
    for (size_t i = 0; i < m_threads.size(); ++i)
    {
        if (m_threads[i].joinable())
            m_threads[i].join();
    }
    m_threads.clear();
}

bool CShareWorkerPool::Post(std::unique_ptr<IShareTask>& task)
{
    if (!task)
        return false;
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        if (!m_running)
            return false;
        m_queue.push_back(std::move(task));
    }
    m_cv.notify_one();
    return true;
}

bool CShareWorkerPool::IsRunning() const
{
    std::lock_guard<std::mutex> guard(m_mutex);
    return m_running;
}

size_t CShareWorkerPool::GetQueuedCount() const
{
    std::lock_guard<std::mutex> guard(m_mutex);
    return m_queue.size();
}

void CShareWorkerPool::ThreadProc()
{
    while (true)
    {
        std::unique_ptr<IShareTask> task;
        {
            std::unique_lock<std::mutex> guard(m_mutex);
            while (m_running && m_queue.empty())
                m_cv.wait(guard);
            if (!m_running)
                return;
            task = std::move(m_queue.front());
            m_queue.pop_front();
        }
        task->Run();
    }
}

} // namespace fileshare
