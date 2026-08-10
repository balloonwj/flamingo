/**
 *  flamingoAdmin —— 异步 HTTP 客户端（单工作线程 + 请求队列）。
 *
 *  admin 控制台所有接口都是「POST JSON」的短连接调用。HTTP 往返是阻塞操作，
 *  不能放在 UI 线程。本文件提供进程级单例 HttpWorker：整个进程只有【一个】
 *  常驻 HTTP 工作线程，所有接口共用它 —— 不再为每个请求新建线程。
 *
 *  分层：
 *    · HttpClient（HttpPostJson）—— 同步传输原语，只被本工作线程调用。
 *    · HttpWorker（本文件）       —— 持有线程与队列，泛型，不认识具体业务。
 *    · AdminApi                  —— 业务层，拼请求 / 解析应答（纯函数）。
 *
 *  用法：
 *    · WinMain 早期 Start() 一次，退出前 Stop() 一次。
 *    · UI 线程构造 HttpRequest、调 Post() 入队后立即返回（不阻塞界面）。
 *    · 工作线程串行处理，完成后把【堆上的 HttpResult*】经 PostMessage 回传到
 *      请求里指定的窗口 / 消息码；结果接收方（消息处理函数）负责 delete 它。
 *
 *  balloonwj@qq.com   2026-05-21
 */
#pragma once

#include <atlstr.h>
#include <string>
#include <deque>
#include <thread>
#include <mutex>
#include <condition_variable>

namespace flamingoadmin {

// 一个待发送的 HTTP 请求。由 UI 线程构造并入队，工作线程取出执行。
// 字符串字段一律用 std::wstring / std::string（不用 CString）：标准库字符串
// 不带写时复制引用计数，跨线程拷贝天然安全。
struct HttpRequest
{
    std::wstring m_host;       // 服务器主机名 / IP
    int          m_port;      // 服务器端口（取值 1 - 65535）
    bool         m_useTls;    // true 走 https，false 走明文 http
    std::wstring m_path;      // 请求路径，以 '/' 开头，如 L"/admin/login.do"
    std::string  m_bodyUtf8;  // 请求体，UTF-8 编码的 JSON 字节
    int          m_timeoutMs; // 各网络阶段的超时（毫秒）

    HWND         m_replyHwnd; // 结果回传的目标窗口；nullptr 表示不回传
    UINT         m_replyMsg;  // 结果回传的消息码；0 表示不需要结果（尽力而为）
    UINT_PTR     m_tag;       // 调用方自定义标签，随结果原样回传（作 WPARAM）

    HttpRequest()
        : m_port(0), m_useTls(false), m_timeoutMs(0)
        , m_replyHwnd(nullptr), m_replyMsg(0), m_tag(0)
    {
    }
};

// 异步 HTTP 客户端：进程级单例，持有唯一一个常驻 HTTP 工作线程与请求队列。
class HttpWorker
{
public:
    // 取全局唯一实例。
    static HttpWorker& Instance();

    // 启动工作线程。须由 WinMain 调用一次；重复调用安全（已启动则忽略）。
    void Start();

    // 停止工作线程并 join。须由 WinMain 在退出前调用一次；重复调用安全。
    // 队列中尚未开始处理的请求一律丢弃；若有一个请求正处于网络往返中，
    // 本函数最多等待它这一次的超时返回（有界，不会无限挂起）。
    void Stop();

    // 提交一个请求入队，立即返回（不阻塞调用线程）。
    //   request：要发送的请求，见 HttpRequest。函数内部复制一份入队。
    void Post(const HttpRequest& request);

private:
    HttpWorker();
    ~HttpWorker();
    HttpWorker(const HttpWorker&) = delete;
    HttpWorker& operator=(const HttpWorker&) = delete;

    // 工作线程主循环：阻塞等待队列 → 取请求 → 发送 → 回传结果。
    void ThreadMain();

    std::thread             m_thread;   // 唯一的 HTTP 工作线程
    std::mutex              m_mutex;    // 保护 m_queue / m_running / m_stop
    std::condition_variable m_cv;       // 「队列空 ↔ 有新请求 / 停止」的等待唤醒
    std::deque<HttpRequest> m_queue;    // 待处理请求队列（FIFO）
    bool                    m_running;  // 工作线程是否已启动
    bool                    m_stop;     // 是否已请求停止
};

} // namespace flamingoadmin
