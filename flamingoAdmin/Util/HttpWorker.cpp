/**
 *  flamingoAdmin —— 异步 HTTP 客户端实现。
 *
 *  balloonwj@qq.com   2026-05-21
 */
#include "stdafx.h"
#include "Util/HttpWorker.h"
#include "Util/HttpClient.h"
#include "Util/DebugConsole.h"

#include <utility>

namespace flamingoadmin {

HttpWorker::HttpWorker()
    : m_running(false), m_stop(false)
{
}

HttpWorker::~HttpWorker()
{
    // 兜底：即便 WinMain 漏调 Stop()，单例析构时也确保线程已收回。
    Stop();
}

HttpWorker& HttpWorker::Instance()
{
    static HttpWorker s_instance;
    return s_instance;
}

void HttpWorker::Start()
{
    std::lock_guard<std::mutex> guard(m_mutex);
    if (m_running)
        return;
    m_stop    = false;
    m_running = true;
    m_thread  = std::thread(&HttpWorker::ThreadMain, this);
}

void HttpWorker::Stop()
{
    // 第一步：在锁内置停止标志。未启动则直接返回。
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        if (!m_running)
            return;
        m_stop = true;
    }

    // 第二步：唤醒可能正阻塞在队列上的工作线程，再 join 等它退出。
    m_cv.notify_all();
    if (m_thread.joinable())
        m_thread.join();

    // 第三步：线程已退出，复位状态并丢弃队列中未处理的请求。
    std::lock_guard<std::mutex> guard(m_mutex);
    m_running = false;
    m_queue.clear();
}

void HttpWorker::Post(const HttpRequest& request)
{
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        m_queue.push_back(request);
    }
    // 通知工作线程有新请求；在锁外通知，减少线程被唤醒后又立即抢锁的开销。
    m_cv.notify_one();
}

void HttpWorker::ThreadMain()
{
    for (;;)
    {
        HttpRequest request;

        // 取一个请求：队列空且未停止时阻塞等待。
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            while (m_queue.empty() && !m_stop)
                m_cv.wait(lock);

            if (m_stop)
                break;   // 收到停止：丢弃剩余请求，退出线程

            request = m_queue.front();
            m_queue.pop_front();
        }

        // 在调试控制台登记本次请求：记录目标地址 / 路径 / 请求体，拿到一个
        // 序号 dbgSeq，等应答回来后再用它补全结果。调试控制台用于在界面上
        // 实时查看所有 HTTP 往返流量，便于联调与排障。
        unsigned long long dbgSeq = DebugConsole::Instance().BeginEntry(
            CString(request.m_host.c_str()), request.m_port, request.m_useTls,
            CString(request.m_path.c_str()), request.m_bodyUtf8);

        // 记录往返起始时刻，用于计算耗时（GetTickCount64 不受系统时间调整影响）。
        ULONGLONG t0 = ::GetTickCount64();

        // 在锁外执行阻塞的 HTTP 往返（带 WinHTTP 超时，最多阻塞 m_timeoutMs）。
        HttpResult result = HttpPostJson(CString(request.m_host.c_str()),
                                         request.m_port, request.m_useTls,
                                         CString(request.m_path.c_str()),
                                         request.m_bodyUtf8,
                                         request.m_timeoutMs);

        // 计算本次往返耗时（毫秒）。
        unsigned elapsedMs = static_cast<unsigned>(::GetTickCount64() - t0);

        // 把应答结果补全到调试控制台对应的记录上。
        // 注意：本函数运行在 HTTP 工作线程，而 Logger（FA_LOGI 等，见
        // Util/Logger.h）是单线程 / 仅 UI 线程使用、非线程安全的，故此处
        // 严禁调用 FA_LOGI 等日志宏。本路径的「日志轨迹」由 DebugConsole
        // 的这条记录承担 —— 后来者请勿在 ThreadMain 里「补」日志调用。
        DebugConsole::Instance().EndEntry(dbgSeq, result.m_transportOk,
                                          result.m_statusCode, result.m_body,
                                          result.m_error, elapsedMs);

        // 把结果回传给调用方。m_replyMsg 为 0 的请求（如登出）是尽力而为，
        // 结果直接丢弃、不回传。
        if (request.m_replyMsg != 0 && request.m_replyHwnd != nullptr)
        {
            // 堆上 new 一份结果，指针经 lParam 转交，由消息处理函数 delete。
            HttpResult* heap = new HttpResult(std::move(result));
            ::PostMessage(request.m_replyHwnd, request.m_replyMsg,
                          static_cast<WPARAM>(request.m_tag),
                          reinterpret_cast<LPARAM>(heap));
        }
    }
}

} // namespace flamingoadmin
