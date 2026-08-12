/**
 *  客户端自动更新管理器实现，UpdateManager.cpp
 *  balloonwj@qq.com   2026-08-12
 */
#include "stdafx.h"
#include "UpdateManager.h"

#include <shlobj.h>

#include "../ClientVersion.h"
#include "../IULog.h"                     // LOG_INFO / LOG_ERROR
#include "../UserSessionData.h"           // g_szHomePath
#include "../EncodeUtil.h"                // Utf8ToUnicode
#include "../net/IUSocket.h"
#include "../net/Protocol.h"              // platform_pc
#include "../net/FileServerConn.h"        // 独立 fileserver 连接
#include "../net/FileStreamTransfer.h"    // FileStreamTransfer::downloadFile
//IDownloadProgressSink 只在 FileStreamTransfer.h 里前置声明，完整定义在这个头文件里。
//要继承它就必须拿到完整定义，否则报"未定义基类"。
#include "../net/ImageDownloadDriver.h"   // net::IDownloadProgressSink

namespace update {

namespace {

//等待 1905 应答的超时（毫秒）。检查更新是登录流程附带的一步，等待过久没有意义：
//登录路径正等它决定是否阻断登录，超时即按"本次未查到更新"处理并放行。
const int kCheckAckTimeoutMs = 8000;

//连 fileserver 的超时（秒）。与群文件下载取同一口径。
const int kConnectTimeoutSec = 5;

//升级包在本地的存放子目录（相对程序主目录）。与旧版升级流程沿用同一个名字，
//iUpdateAuto 解压时也认它。
const wchar_t* const kUpdateSubDir = L"update";

//把 UTF-8 窄串转成宽串，供路径拼接用。
std::wstring WideFromUtf8(const std::string& s)
{
    if (s.empty())
    {
        return std::wstring();
    }
    //按最坏情况给足缓冲：UTF-8 一个字符至多 4 字节，转成 UTF-16 至多 2 个 wchar_t
    std::wstring w;
    w.resize(s.size() + 1);
    EncodeUtil::Utf8ToUnicode(s.c_str(), &w[0], static_cast<int>(w.size()));
    w.resize(wcslen(w.c_str()));
    return w;
}

/**
 *  下载进度 sink —— 把 FileStreamTransfer 每片落盘后的进度回报转成一条投给升级窗的
 *  窗口消息。本仓库代码风格不用 lambda，故写成一个小类（与 net::IDownloadProgressSink
 *  的其它实现同做法）。
 *
 *  它在<u>工作线程</u>上被回调，因此只做 PostMessage，绝不碰任何控件。
 */
class DownloadProgressRelay : public net::IDownloadProgressSink
{
public:
    //  owner：转发目标管理器，生命周期覆盖本对象（本对象是 DoDownload 的栈上局部量）
    explicit DownloadProgressRelay(UpdateManager* owner)
        : m_owner(owner)
    {
    }

    virtual void onProgress(int64_t received, int64_t total)
    {
        if (m_owner != NULL)
        {
            m_owner->PostProgress(received, total);
        }
    }

private:
    UpdateManager* m_owner;   //不持有所有权
};

//确保 <程序目录>\update\ 存在；返回该目录（以反斜杠结尾）。建不出来返回空串。
std::wstring EnsureUpdateDir()
{
    std::wstring dir(g_szHomePath);
    if (dir.empty())
    {
        return std::wstring();
    }
    if (dir.back() != L'\\' && dir.back() != L'/')
    {
        dir += L'\\';
    }
    dir += kUpdateSubDir;

    const DWORD attr = ::GetFileAttributesW(dir.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES)
    {
        if (!::CreateDirectoryW(dir.c_str(), NULL))
        {
            LOG_ERROR("Update: create update dir failed, err=%u.", ::GetLastError());
            return std::wstring();
        }
    }
    else if ((attr & FILE_ATTRIBUTE_DIRECTORY) == 0)
    {
        //同名的普通文件占据了该路径：无法自动处理，如实报错
        LOG_ERROR("Update: update path exists but is not a directory.");
        return std::wstring();
    }

    dir += L'\\';
    return dir;
}

} // namespace

UpdateManager& UpdateManager::GetInstance()
{
    static UpdateManager s_instance;
    return s_instance;
}

UpdateManager::UpdateManager()
    : m_running(false)
    , m_ackSeq(0)
    , m_ackArrived(false)
    , m_notifyWnd(NULL)
{
}

UpdateManager::~UpdateManager()
{
    Uninit();
}

void UpdateManager::Init()
{
    if (m_running)
    {
        return;
    }
    m_running = true;
    m_worker = std::thread(&UpdateManager::Run, this);
    LOG_INFO("UpdateManager started.");
}

void UpdateManager::Uninit()
{
    if (!m_running)
    {
        return;
    }
    m_running = false;
    m_taskCv.notify_all();
    //一并唤醒可能正阻塞在等待应答处的工作线程，避免停止服务时白白等到超时
    m_ackCv.notify_all();
    if (m_worker.joinable())
    {
        m_worker.join();
    }
    LOG_INFO("UpdateManager stopped.");
}

void UpdateManager::CheckAndDownload(bool autoDownload)
{
    {
        std::lock_guard<std::mutex> guard(m_taskMtx);
        m_tasks.push_back(autoDownload);
    }
    m_taskCv.notify_one();
    LOG_INFO("Update: check task queued, autoDownload=%d.", autoDownload ? 1 : 0);
}

void UpdateManager::OnCheckUpdateResponse(uint32_t seq, const net::CheckUpdateResult& r)
{
    {
        std::lock_guard<std::mutex> guard(m_ackMtx);
        if (m_ackSeq == 0 || m_ackSeq != seq)
        {
            //超时后迟到的应答，或当前无人等待该应答：丢弃即可，但需记录日志
            LOG_INFO("Update: drop check-update ack, seq=%u, waiting=%u.", seq, m_ackSeq);
            return;
        }
        m_ackResult  = r;
        m_ackArrived = true;
    }
    m_ackCv.notify_all();
}

void UpdateManager::SetNotifyTarget(HWND hWnd)
{
    std::lock_guard<std::mutex> guard(m_notifyMtx);
    m_notifyWnd = hWnd;
    LOG_INFO("Update: notify target %s.", hWnd != NULL ? "set" : "cleared");
}

HWND UpdateManager::GetNotifyTarget()
{
    std::lock_guard<std::mutex> guard(m_notifyMtx);
    return m_notifyWnd;
}

void UpdateManager::PostProgress(int64_t received, int64_t total)
{
    const HWND hWnd = GetNotifyTarget();
    if (hWnd == NULL || !::IsWindow(hWnd))
    {
        //没有窗口登记为投递目标（例如登录时的静默下载）：直接丢弃，不必缓存
        return;
    }

    //堆上分配，UI 线程处理后 delete。PostMessage 失败（消息队列满 / 窗口正在销毁）
    //时必须在此处释放，否则这块内存不会被回收。
    UpdateProgressInfo* info = new UpdateProgressInfo();
    info->m_received = received;
    info->m_total    = total;

    if (!::PostMessage(hWnd, FMG_MSG_UPDATE_PROGRESS, 0, (LPARAM)info))
    {
        delete info;
    }
}

bool UpdateManager::TakeLastResult(UpdateTaskResult& out)
{
    std::lock_guard<std::mutex> guard(m_resultMtx);
    if (!m_lastResult.m_valid)
    {
        return false;
    }
    out = m_lastResult;
    //取走即清空：界面层轮询时不会把上一次的旧结果当成本次的
    m_lastResult = UpdateTaskResult();
    return true;
}

void UpdateManager::Run()
{
    while (m_running)
    {
        bool autoDownload = false;
        {
            std::unique_lock<std::mutex> lock(m_taskMtx);
            m_taskCv.wait(lock, [this]() { return !m_running || !m_tasks.empty(); });
            if (!m_running)
            {
                break;
            }
            autoDownload = m_tasks.front();
            m_tasks.pop_front();
        }

        DoCheck(autoDownload);
    }
}

void UpdateManager::DoCheck(bool autoDownload)
{
    UpdateTaskResult result;
    result.m_valid = true;

    //① 发请求。登记等待的 seq 必须在发出之前，否则应答可能先于登记到达。
    const std::string body = net::UpdateProtocolClient::buildCheckUpdateRequest(
        platform_pc, FLAMINGO_CLIENT_VERSION);
    uint32_t seq = 0;
    {
        std::lock_guard<std::mutex> guard(m_ackMtx);
        m_ackArrived = false;
        m_ackResult  = net::CheckUpdateResult();
        seq = CIUSocket::GetInstance().SendCheckUpdate(body);
        m_ackSeq = seq;
    }
    if (seq == 0)
    {
        result.m_errmsg = "send check-update failed (not connected)";
        LOG_ERROR("Update: %s.", result.m_errmsg.c_str());
        {
            std::lock_guard<std::mutex> guard(m_ackMtx);
            m_ackSeq = 0;
        }
        PublishResult(result);
        return;
    }
    LOG_INFO("Update: check-update sent, seq=%u, version=%s.", seq, FLAMINGO_CLIENT_VERSION);

    //② 等应答
    net::CheckUpdateResult ack;
    bool arrived = false;
    {
        std::unique_lock<std::mutex> lock(m_ackMtx);
        arrived = m_ackCv.wait_for(lock, std::chrono::milliseconds(kCheckAckTimeoutMs),
                                   [this]() { return m_ackArrived || !m_running; });
        arrived = arrived && m_ackArrived;
        if (arrived)
        {
            ack = m_ackResult;
        }
        m_ackSeq = 0;   //无论成败都撤掉等待登记，后到的应答会被丢弃
    }

    if (!arrived)
    {
        result.m_errmsg = "check-update timeout";
        LOG_ERROR("Update: check-update timeout, seq=%u.", seq);
        PublishResult(result);
        return;
    }

    result.m_transportOk  = true;
    result.m_code         = ack.m_code;
    result.m_hasUpdate    = ack.m_hasUpdate;
    result.m_forced       = ack.m_forced;
    result.m_latestVersion = ack.m_latestVersion;
    result.m_releaseNote  = ack.m_releaseNote;

    if (ack.m_code != 0)
    {
        result.m_errmsg = ack.m_msg;
        LOG_ERROR("Update: check-update failed, code=%d, msg=%s.", ack.m_code, ack.m_msg.c_str());
        PublishResult(result);
        return;
    }
    if (!ack.m_hasUpdate)
    {
        LOG_INFO("Update: already latest, local=%s, server latest=%s.",
                 FLAMINGO_CLIENT_VERSION, ack.m_latestVersion.c_str());
        PublishResult(result);
        return;
    }
    if (!ack.HasUsableUpdate())
    {
        //服务端报告有更新，但未下发完整的令牌 / MD5 / 大小：下载后也无法通过校验，故不下载。
        //m_packageUsable 保持 false，界面据此按"无更新"呈现、不给下载入口。
        result.m_errmsg = "server reported an update without a usable package";
        LOG_ERROR("Update: %s, latest=%s.", result.m_errmsg.c_str(), ack.m_latestVersion.c_str());
        PublishResult(result);
        return;
    }

    //包信息齐备，可以进下载流程；大小带给界面显示
    result.m_packageUsable = true;
    result.m_size          = ack.m_size;

    LOG_INFO("Update: new version available, local=%s, latest=%s, forced=%d, size=%lld, md5=%s.",
             FLAMINGO_CLIENT_VERSION, ack.m_latestVersion.c_str(), ack.m_forced ? 1 : 0,
             (long long)ack.m_size, ack.m_md5.c_str());

    //③ 下载
    if (autoDownload)
    {
        DoDownload(ack, result);
    }
    PublishResult(result);
}

void UpdateManager::DoDownload(const net::CheckUpdateResult& info, UpdateTaskResult& result)
{
    const std::wstring dir = EnsureUpdateDir();
    if (dir.empty())
    {
        result.m_errmsg = "prepare update dir failed";
        return;
    }

    //落地文件名用服务端给的原始包名；包名异常时退回"版本号.zip"，
    //绝不用 MD5 当文件名 —— 那是服务端那侧的存储约定，本地留个人能看懂的名字。
    std::wstring fileName = WideFromUtf8(info.m_packageName);
    if (fileName.empty() || fileName.find(L'\\') != std::wstring::npos
        || fileName.find(L'/') != std::wstring::npos)
    {
        fileName = L"Flamingo-" + WideFromUtf8(info.m_latestVersion) + L".zip";
    }
    const std::wstring destPath = dir + fileName;

    net::FileServerConn conn;
    if (!conn.Connect(info.m_host, static_cast<short>(info.m_port), kConnectTimeoutSec))
    {
        result.m_errmsg = "connect fileserver failed";
        LOG_ERROR("Update: connect fileserver %s:%d failed.", info.m_host.c_str(), info.m_port);
        return;
    }

    std::wstring localPath;
    int errcode = 0;
    std::string errmsg;
    //downloadFile 内部已做分片、增量算 MD5、校验后原子 rename，这里不必重复。
    //进度 sink 把每片落盘后的进度投递给升级窗；未登记投递目标时 PostProgress 内部直接丢弃。
    DownloadProgressRelay relay(this);
    const bool ok = net::FileStreamTransfer::downloadFile(
        conn, info.m_downloadToken, info.m_md5, info.m_size, destPath,
        &relay, NULL, localPath, errcode, errmsg);
    conn.Close();

    if (!ok)
    {
        result.m_errmsg = errmsg;
        LOG_ERROR("Update: download failed, errcode=%d, errmsg=%s.", errcode, errmsg.c_str());
        return;
    }

    result.m_downloaded = true;
    result.m_localPath  = localPath;
    LOG_INFO("Update: package downloaded and md5 verified, size=%lld, path=%s.",
             (long long)info.m_size, EncodeUtil::UnicodeToUtf8(localPath).c_str());
}

void UpdateManager::PublishResult(const UpdateTaskResult& result)
{
    {
        std::lock_guard<std::mutex> guard(m_resultMtx);
        m_lastResult = result;
    }
    LOG_INFO("Update: result ready, transportOk=%d, code=%d, hasUpdate=%d, forced=%d, "
             "usable=%d, downloaded=%d, latest=%s.",
             result.m_transportOk ? 1 : 0, result.m_code, result.m_hasUpdate ? 1 : 0,
             result.m_forced ? 1 : 0, result.m_packageUsable ? 1 : 0,
             result.m_downloaded ? 1 : 0, result.m_latestVersion.c_str());

    //已登记投递目标时投递一份副本；未登记则只保留在 m_lastResult 中，由界面主动取走 ——
    //登录时的静默检查就是这条路径：那时升级窗还没建出来。
    const HWND hWnd = GetNotifyTarget();
    if (hWnd == NULL || !::IsWindow(hWnd))
    {
        return;
    }

    UpdateTaskResult* copy = new UpdateTaskResult(result);
    if (!::PostMessage(hWnd, FMG_MSG_UPDATE_RESULT, 0, (LPARAM)copy))
    {
        delete copy;
    }
}

} // namespace update
