/**
 *  云端录像文件的上传与下载（exe 一侧），见 CloudTransferRunner.h。
 *  balloonwj@qq.com   2026-10-06
 */
#include "stdafx.h"
#include "CloudTransferRunner.h"

#include "../../Common/Log/IULog.h"
#include "../../net/FileServerConn.h"
#include "../../net/FileStreamTransfer.h"
#include "../../net/ImageDownloadDriver.h"   //net::IDownloadProgressSink
#include "../../net/ImageUploadDriver.h"     //net::IUploadProgressSink

namespace ballooncloud {

namespace {

//消息窗口的类名
const wchar_t* const kWindowClass = L"FlamingoCloudTransferRunner";

//进度消息的负载（工作线程 new、UI 线程 delete）
struct ProgressPayload
{
    int64_t m_done;    //已传完的字节数
    int64_t m_total;   //总字节数
};

//把分片进度投回 UI 线程
class PostingProgressSink : public net::IUploadProgressSink, public net::IDownloadProgressSink
{
public:
    PostingProgressSink(HWND hWnd, uint32_t id)
        : m_hWnd(hWnd)
        , m_id(id)
    {
    }

    void post(int64_t done, int64_t total)
    {
        ProgressPayload* payload = new ProgressPayload();
        payload->m_done = done;
        payload->m_total = total;
        if (!::PostMessageW(m_hWnd, CCloudTransferRunner::kMsgProgress, static_cast<WPARAM>(m_id),
                            reinterpret_cast<LPARAM>(payload)))
            delete payload;
    }

    //上传：已确认的字节数
    virtual void onProgress(int64_t sent, int64_t total) override
    {
        post(sent, total);
    }

private:
    HWND     m_hWnd;   //消息窗口
    uint32_t m_id;     //传输 id
};

//下载用的进度回报：IDownloadProgressSink 与 IUploadProgressSink 的 onProgress 同名同参，分成两个类以免二义
class PostingDownloadSink : public net::IDownloadProgressSink
{
public:
    explicit PostingDownloadSink(PostingProgressSink& inner)
        : m_inner(inner)
    {
    }

    virtual void onProgress(int64_t received, int64_t total) override
    {
        m_inner.post(received, total);
    }

private:
    PostingProgressSink& m_inner;   //实际投递的对象（不拥有）
};

} // namespace

CCloudTransferRunner::Task::Task()
    : m_id(0)
    , m_direction(0)
    , m_port(0)
    , m_bytes(0)
    , m_cancel(false)
    , m_conn(NULL)
{
}

CCloudTransferRunner& CCloudTransferRunner::GetInstance()
{
    static CCloudTransferRunner instance;
    return instance;
}

CCloudTransferRunner::CCloudTransferRunner()
    : m_hWnd(NULL)
    , m_module(NULL)
    , m_nextId(1)
{
}

CCloudTransferRunner::~CCloudTransferRunner()
{
}

void CCloudTransferRunner::SetMediaModule(flamingomedia::IMediaModule* module)
{
    m_module = module;
}

size_t CCloudTransferRunner::ActiveCount() const
{
    std::lock_guard<std::mutex> guard(m_mutex);
    return m_tasks.size();
}

bool CCloudTransferRunner::ensureWindow()
{
    if (m_hWnd != NULL)
        return true;
    WNDCLASSEXW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &CCloudTransferRunner::WndProc;
    wc.hInstance = ::GetModuleHandleW(NULL);
    wc.lpszClassName = kWindowClass;
    ::RegisterClassExW(&wc);
    m_hWnd = ::CreateWindowExW(0, kWindowClass, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, wc.hInstance, NULL);
    if (m_hWnd == NULL)
    {
        LOG_ERROR("cloud transfer: create message window failed, error %lu.", ::GetLastError());
        return false;
    }
    return true;
}

LRESULT CALLBACK CCloudTransferRunner::WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    CCloudTransferRunner& self = GetInstance();
    if (msg == kMsgProgress)
    {
        ProgressPayload* payload = reinterpret_cast<ProgressPayload*>(lParam);
        if (payload != NULL)
        {
            if (self.m_module != NULL)
                self.m_module->OnCloudTransferProgress(static_cast<uint32_t>(wParam), payload->m_done, payload->m_total);
            delete payload;
        }
        return 0;
    }
    if (msg == kMsgFinished)
    {
        self.onFinished(static_cast<uint32_t>(wParam), static_cast<int>(lParam));
        return 0;
    }
    return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}

uint32_t CCloudTransferRunner::Start(const flamingomedia::MediaCloudTransfer& req)
{
    if (req.m_cbSize < sizeof(flamingomedia::MediaCloudTransfer) || req.m_token == NULL || req.m_host == NULL ||
        req.m_localPath == NULL || req.m_port <= 0 || req.m_bytes <= 0 ||
        (req.m_direction != flamingomedia::media_cloud_download && req.m_direction != flamingomedia::media_cloud_upload))
    {
        LOG_WARNING("cloud transfer: bad request, direction %d, port %d.", req.m_direction, req.m_port);
        return 0;
    }
    if (!ensureWindow())
        return 0;
    std::shared_ptr<Task> task(new Task());
    task->m_direction = req.m_direction;
    task->m_token = req.m_token;
    task->m_host = req.m_host;
    task->m_port = req.m_port;
    task->m_localPath = req.m_localPath;
    task->m_bytes = req.m_bytes;
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        task->m_id = m_nextId++;
        m_tasks[task->m_id] = task;
    }
    task->m_thread = std::thread(&CCloudTransferRunner::run, this, task);
    LOG_INFO("cloud transfer: started, id %u, direction %d, bytes %lld, host %s:%d.", task->m_id, task->m_direction,
             static_cast<long long>(task->m_bytes), task->m_host.c_str(), task->m_port);
    return task->m_id;
}

void CCloudTransferRunner::run(std::shared_ptr<Task> task)
{
    int result = 0;
    net::FileServerConn conn;
    if (!conn.Connect(task->m_host, static_cast<short>(task->m_port), kConnectTimeoutSec))
    {
        LOG_ERROR("cloud transfer: connect imgserver %s:%d failed, id %u.", task->m_host.c_str(), task->m_port,
                  task->m_id);
        result = error_code_server_internal;
    }
    else
    {
        {
            std::lock_guard<std::mutex> guard(m_mutex);
            task->m_conn = &conn;
        }
        PostingProgressSink sink(m_hWnd, task->m_id);
        int errcode = 0;
        std::string errmsg;
        bool ok = false;
        if (!task->m_cancel.load())
        {
            if (task->m_direction == flamingomedia::media_cloud_upload)
            {
                //分片大小传 0：按协议默认的 512 KiB
                ok = net::FileStreamTransfer::uploadFile(conn, task->m_localPath, task->m_bytes, 0, task->m_token, &sink,
                                                         &task->m_cancel, errcode, errmsg);
            }
            else
            {
                PostingDownloadSink downloadSink(sink);
                std::wstring localPath;
                //云端录像的令牌里没有内容 MD5，不做整文件校验（与 imgserver 的 scene 6 一致）
                ok = net::FileStreamTransfer::downloadFile(conn, task->m_token, std::string(), task->m_bytes,
                                                           task->m_localPath, &downloadSink, &task->m_cancel, localPath,
                                                           errcode, errmsg);
            }
        }
        {
            std::lock_guard<std::mutex> guard(m_mutex);
            task->m_conn = NULL;
        }
        conn.Close();
        if (task->m_cancel.load())
            result = kCloudTransferCancelled;
        else if (!ok)
            result = errcode != 0 ? errcode : error_code_server_internal;
        if (result != 0)
            LOG_ERROR("cloud transfer: id %u failed, result %d, error %d, %s.", task->m_id, result, errcode,
                      errmsg.c_str());
    }
    ::PostMessageW(m_hWnd, kMsgFinished, static_cast<WPARAM>(task->m_id), static_cast<LPARAM>(result));
}

void CCloudTransferRunner::onFinished(uint32_t id, int result)
{
    std::shared_ptr<Task> task;
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        std::map<uint32_t, std::shared_ptr<Task> >::iterator it = m_tasks.find(id);
        if (it == m_tasks.end())
            return;
        task = it->second;
        m_tasks.erase(it);
    }
    //工作线程投完这条消息就结束了，这里等它退出
    if (task->m_thread.joinable())
        task->m_thread.join();
    LOG_INFO("cloud transfer: finished, id %u, result %d.", id, result);
    if (m_module != NULL)
        m_module->OnCloudTransferFinished(id, result);
}

void CCloudTransferRunner::Cancel(uint32_t id)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    std::map<uint32_t, std::shared_ptr<Task> >::iterator it = m_tasks.find(id);
    if (it == m_tasks.end())
        return;
    it->second->m_cancel = true;
    //关掉连接：阻塞在收发上的工作线程立即返回（与群文件并发池停止时的做法相同）
    if (it->second->m_conn != NULL)
        it->second->m_conn->Close();
    LOG_INFO("cloud transfer: cancel requested, id %u.", id);
}

void CCloudTransferRunner::Shutdown()
{
    std::map<uint32_t, std::shared_ptr<Task> > tasks;
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        for (std::map<uint32_t, std::shared_ptr<Task> >::iterator it = m_tasks.begin(); it != m_tasks.end(); ++it)
        {
            it->second->m_cancel = true;
            if (it->second->m_conn != NULL)
                it->second->m_conn->Close();
        }
        tasks.swap(m_tasks);
    }
    for (std::map<uint32_t, std::shared_ptr<Task> >::iterator it = tasks.begin(); it != tasks.end(); ++it)
    {
        if (it->second->m_thread.joinable())
            it->second->m_thread.join();
    }
    m_module = NULL;
    if (m_hWnd != NULL)
    {
        ::DestroyWindow(m_hWnd);
        m_hWnd = NULL;
    }
    if (!tasks.empty())
        LOG_INFO("cloud transfer: shutdown, %u transfers cancelled.", static_cast<unsigned>(tasks.size()));
}

} // namespace ballooncloud
