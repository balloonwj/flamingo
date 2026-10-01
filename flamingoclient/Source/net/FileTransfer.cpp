/**
 *  聊天文件传输管理器（客户端侧，收发双向）实现，FileTransfer.cpp
 *  balloonwj@qq.com   2026-06-05
 */
#include "stdafx.h"
#include "FileTransfer.h"
#include "FileServerConn.h"   //独立 fileserver 连接（原内联在本文件，已抽出共用）

#include "IUSocket.h"
#include "TransferOwnerRegistry.h"  // transfer_owner_file
#include "ImageUploadDriver.h"      // 上传分片驱动（按"图片/文件"通用设计，文件复用）
#include "ImageDownloadDriver.h"    // 下载分片驱动（同上）
#include "FrameCodec.h"             // encodeFrame / encodeAttachmentBody / decodeFrame（内含 Protocol.h）
#include "FramePayload.h"           // decodeFramePayload（收包时的解密与解压）
#include "../Session/UserSessionData.h"     // FMG_MSG_V2_FILE_*
#include "../Common/Log/IULog.h"               // LOG_INFO / LOG_ERROR
#include "../Common/Util/MD5Core.h"   // MD5Init / MD5Update / MD5Final（纯 C 接口）
#include "../Common/Util/EncodeUtil.h"          // UnicodeToUtf8 / Utf8ToUnicode
#include "UserCachePath.h"          // UserCacheDir（Users\<登录账号>\ 的统一口径）

#include <fstream>
#include <chrono>
#include <vector>     // 流式读盘 / 上传的分片缓冲

namespace net {

//聊天文件（scene=1）与群文件（scene=2）均不设大小上限，与服务端 ChatSession::
//onRequestUpload 的口径一致，故此处无上限常量。MD5 与上传全程流式（分片读盘、
//读一片传一片，只占一个分片内存），文件多大都不会把 32 位客户端进程内存撑爆。

//申请上传 / 下载信令应答的等待超时：15 秒。正常往返为毫秒级，留足余量兜底网络抖动。
static const int kSignalTimeoutSec = 15;

//注：字节流分片收发的单次超时 kChunkIoTimeoutSec 与卡死判定轮数 kMaxStreamStallRounds
//已随流式收发实现一并移到 FileStreamTransfer.h，本文件经该头文件取用。

//流式读盘缓冲大小：算 MD5 时每次从磁盘读这么多字节做 MD5Update。整个过程只占这一块
//内存，不把整文件读入。
static const size_t kFileReadBufBytes = 256 * 1024;

//只取磁盘上文件的字节大小、不读内容（打开并 seek 到末尾取位置）；打开失败 / 空文件
//返回 -1。用于在把整个文件读入内存之前先按大小上限拦截超大文件——否则超大文件在
//readWholeFile 的 out.resize(文件大小) 处会在 32 位进程触发 std::bad_alloc→abort 崩溃。
static int64_t getFileSizeOnDisk(const std::wstring& path)
{
    std::ifstream in(path.c_str(), std::ios::binary | std::ios::ate);
    if (!in)
        return -1;
    std::streamoff sz = in.tellg();
    if (sz <= 0)
        return -1;
    return static_cast<int64_t>(sz);
}

//流式计算文件内容的 MD5（小写十六进制 32 位串），分片读盘、不把整文件读入内存。
//成功返回 true 并填 md5out；打开失败 / 读盘出错返回 false。上传申请（cmd_request_upload）
//需要先把 md5 报给服务端做秒传判定与校验，故上传前先流式扫一遍算出 md5。
static bool computeFileMd5Streaming(const std::wstring& path, std::string& md5out)
{
    std::ifstream in(path.c_str(), std::ios::binary);
    if (!in)
        return false;

    MD5_CTX ctx;
    MD5Init(&ctx);

    std::vector<char> buf(kFileReadBufBytes);
    while (in)
    {
        in.read(buf.data(), static_cast<std::streamsize>(buf.size()));
        std::streamsize got = in.gcount();
        if (got > 0)
            MD5Update(&ctx, reinterpret_cast<const unsigned char*>(buf.data()),
                      static_cast<unsigned int>(got));
    }
    //非 EOF 的读盘失败：判失败（EOF 是正常结束，eofbit 不算 bad）
    if (in.bad())
        return false;

    unsigned char digest[16] = { 0 };
    MD5Final(digest, &ctx);
    char hex[33] = { 0 };
    for (int i = 0; i < 16; ++i)
        sprintf_s(hex + i * 2, 3, "%02x", digest[i]);
    md5out.assign(hex, 32);
    return true;
}

//从路径取扩展名（不含点），转小写。扩展名为 ASCII，直接窄化即可。
static std::string extractExtLower(const std::wstring& path)
{
    size_t dot = path.find_last_of(L'.');
    size_t sep = path.find_last_of(L"\\/");
    //没有点、或点在最后一个路径分隔符之前（即不是文件名里的扩展名）：无扩展名
    if (dot == std::wstring::npos || (sep != std::wstring::npos && dot < sep))
        return std::string();

    std::string ext;
    for (size_t i = dot + 1; i < path.size(); ++i)
    {
        wchar_t c = path[i];
        if (c >= L'A' && c <= L'Z')
            c = static_cast<wchar_t>(c - L'A' + L'a');
        //仅保留 ASCII 字符，异常字符直接丢弃
        if (c < 128)
            ext.push_back(static_cast<char>(c));
    }
    return ext;
}

//取宽字符路径的文件名部分（最后一个 \ 或 / 之后），不含目录。
static std::wstring extractFileNameW(const std::wstring& path)
{
    size_t sep = path.find_last_of(L"\\/");
    if (sep == std::wstring::npos)
        return path;
    return path.substr(sep + 1);
}

//净化文件名：仅取最后一个路径分隔符之后的基名，剔除可能的目录穿越（如收到伪造
//的 "..\\..\\x"）。结果为空时回退为 "file"。入参 / 出参均为 UTF-8。
static std::string sanitizeFileName(const std::string& name)
{
    size_t sep = name.find_last_of("\\/");
    std::string base = (sep == std::string::npos) ? name : name.substr(sep + 1);
    if (base.empty() || base == "." || base == "..")
        return std::string("file");
    return base;
}

std::wstring FileTransfer::CacheFilePathFor(const std::wstring& account, const std::string& md5,
                                            const std::string& filename)
{
    if (md5.empty())
        return std::wstring();

    //缓存按登录账号分隔、按内容 MD5 分目录、按原始文件名命名：
    //<g_szDataRoot>Users\<账号>\ChatFile\<md5>\<filename>。目录那一截统一由
    //UserCacheDir 拼（Users 下的命名口径只此一处），账号为空时它返回空串，
    //本方法据此一并返回空串。md5 为 ASCII 十六进制，逐字符宽化；filename 为 UTF-8，
    //经 EncodeUtil 转宽字符。本方法只拼路径、不创建目录、不判断存在。
    std::wstring path = net::UserCacheDir(g_szDataRoot, account, L"ChatFile");
    if (path.empty())
        return std::wstring();

    for (size_t i = 0; i < md5.size(); ++i)
        path.push_back(static_cast<wchar_t>(md5[i]));
    path += L"\\";
    path += EncodeUtil::Utf8ToUnicode(sanitizeFileName(filename));
    return path;
}

//构造下载落地路径，并按需逐级创建 Users、Users\<账号>、ChatFile、ChatFile\<md5>
//四级目录。落地写文件前调用。
static std::wstring buildFileCachePath(const std::wstring& account, const std::string& md5,
                                       const std::string& filename)
{
    //先拿到 <主目录>Users\<账号>\ChatFile\，再逐级建目录。账号为空时拼不出路径，
    //直接返回空串——调用方会把它当成落地失败，而不是往当前工作目录乱写。
    const std::wstring chatFileDir = net::UserCacheDir(g_szDataRoot, account, L"ChatFile");
    if (chatFileDir.empty())
        return std::wstring();

    //CreateDirectoryW 只建最后一级，故从 Users 往下逐级调用；已存在会返回失败，忽略即可。
    const std::wstring userDir = net::UserCacheDir(g_szDataRoot, account, NULL);
    std::wstring usersRoot = g_szDataRoot;
    usersRoot += L"Users\\";
    ::CreateDirectoryW(usersRoot.c_str(), NULL);
    ::CreateDirectoryW(userDir.c_str(), NULL);
    ::CreateDirectoryW(chatFileDir.c_str(), NULL);

    std::wstring md5Dir = chatFileDir;
    for (size_t i = 0; i < md5.size(); ++i)
        md5Dir.push_back(static_cast<wchar_t>(md5[i]));
    md5Dir += L"\\";
    ::CreateDirectoryW(md5Dir.c_str(), NULL);

    return FileTransfer::CacheFilePathFor(account, md5, filename);
}

//注：文件上传已改为 streamUploadFile 流式实现（分片从磁盘读、内联组帧发送），不再经
//ImageUploadDriver + 内存版传输类驱动，故原 FileSocketUploadTransport 已移除。图片上传
//仍走 ImageTransfer 的内存版驱动（图片小，无需流式）。

//注：IFileFrameChannel 的定义已上移到 FileTransfer.h —— 流式收发改为公开的静态方法后，
//其参数类型须在头文件里完整可见（单元测试要自备假通道驱动它们）。

//共享 socket 通道：包 CIUSocket 那条全局 file socket（m_hFileSocket）。单聊收发文件
//走这条，行为与改造前完全一致——只是把原先直接调 SendOnFilePort / RecvOneFileFrame
//的两处套进了通道接口。
class MainFileChannel : public IFileFrameChannel
{
public:
    explicit MainFileChannel(FileTransfer* owner)
        : m_owner(owner)
    {
    }

    bool SendBytes(const char* data, int64_t size, int timeoutSec) override
    {
        return CIUSocket::GetInstance().SendOnFilePort(data, size, timeoutSec);
    }

    bool RecvFrame(std::string& body, int timeoutSec) override
    {
        return m_owner->RecvOneFileFrame(body, timeoutSec);
    }

    //共享文件连接的加解密器由 CIUSocket 持有（socket 也在它那儿），
    //本通道只是转发壳、每笔传输新建一个，不能自己持有加密器
    net::FrameCipher* GetCipher() override
    {
        return CIUSocket::GetInstance().GetFileCipher();
    }

private:
    FileTransfer* m_owner;   //回主类收一整帧（共享 socket）
};

//独立 fileserver 连接类 FileServerConn 已移到 net/FileServerConn.h：
//升级包下载（Update/UpdateManager）也要用它，再抄一份 socket 代码不划算。

//把上传进度回投到 UI 的进度 sink。
class PostingUploadProgressSink : public IUploadProgressSink
{
public:
    PostingUploadProgressSink(HWND hUI, const std::string& clientMsgId)
        : m_hUI(hUI)
        , m_clientmsgid(clientMsgId)
    {
    }

    void onProgress(int64_t sent, int64_t total) override
    {
        if (!::IsWindow(m_hUI))
            return;
        FileSendProgress* p = new FileSendProgress();
        p->m_clientmsgid = m_clientmsgid;
        p->m_sent        = sent;
        p->m_total       = total;
        ::PostMessage(m_hUI, FMG_MSG_V2_FILE_SEND_PROGRESS, 0, (LPARAM)p);
    }

private:
    HWND        m_hUI;
    std::string m_clientmsgid;
};

//把下载进度回投到 UI 的进度 sink。
class PostingDownloadProgressSink : public IDownloadProgressSink
{
public:
    PostingDownloadProgressSink(HWND hUI, int64_t msgid)
        : m_hUI(hUI)
        , m_msgid(msgid)
    {
    }

    void onProgress(int64_t received, int64_t total) override
    {
        if (!::IsWindow(m_hUI))
            return;
        FileDownloadProgress* p = new FileDownloadProgress();
        p->m_msgid    = m_msgid;
        p->m_received = received;
        p->m_total    = total;
        ::PostMessage(m_hUI, FMG_MSG_V2_FILE_DOWNLOAD_PROGRESS, 0, (LPARAM)p);
    }

private:
    HWND    m_hUI;
    int64_t m_msgid;
};

//把群文件上传进度回投到 UI（面板级进度条）。整数百分比未变则不投，避免大文件每片都
//投（GB 级文件几千片）刷屏 / 挤占 WM_PAINT。
class PostingGroupFileUploadProgressSink : public IUploadProgressSink
{
public:
    PostingGroupFileUploadProgressSink(HWND hUI, int64_t groupId, int uploadId)
        : m_hUI(hUI)
        , m_groupid(groupId)
        , m_uploadid(uploadId)
        , m_lastPercent(-1)
    {
    }

    void onProgress(int64_t sent, int64_t total) override
    {
        if (!::IsWindow(m_hUI))
            return;
        int pct = (total > 0) ? static_cast<int>((sent * 100) / total) : 0;
        if (pct == m_lastPercent)
            return;
        m_lastPercent = pct;

        GroupFileUploadProgress* p = new GroupFileUploadProgress();
        p->m_groupid  = m_groupid;
        p->m_uploadid = m_uploadid;
        p->m_sent     = sent;
        p->m_total    = total;
        ::PostMessage(m_hUI, FMG_MSG_V2_GROUP_FILE_UPLOAD_PROGRESS, 0, (LPARAM)p);
    }

private:
    HWND    m_hUI;
    int64_t m_groupid;
    int     m_uploadid;      //本笔上传的客户端上传ID（UI 据此定位那一行）
    int     m_lastPercent;   //上次已投的整数百分比（-1=未投过）
};

//把群文件下载进度回投到 UI（按 entryid 更新面板里那一行的进度）。同样按整数百分比节流。
class PostingGroupFileDownloadProgressSink : public IDownloadProgressSink
{
public:
    PostingGroupFileDownloadProgressSink(HWND hUI, int64_t groupId, const std::string& entryid)
        : m_hUI(hUI)
        , m_groupid(groupId)
        , m_entryid(entryid)
        , m_lastPercent(-1)
    {
    }

    void onProgress(int64_t received, int64_t total) override
    {
        if (!::IsWindow(m_hUI))
            return;
        int pct = (total > 0) ? static_cast<int>((received * 100) / total) : 0;
        if (pct == m_lastPercent)
            return;
        m_lastPercent = pct;

        GroupFileDownloadProgress* p = new GroupFileDownloadProgress();
        p->m_groupid  = m_groupid;
        p->m_entryid  = m_entryid;
        p->m_received = received;
        p->m_total    = total;
        ::PostMessage(m_hUI, FMG_MSG_V2_GROUP_FILE_DOWNLOAD_PROGRESS, 0, (LPARAM)p);
    }

private:
    HWND        m_hUI;
    int64_t     m_groupid;
    std::string m_entryid;
    int         m_lastPercent;
};

FileTransfer::FileTransfer()
    : m_stop(false)
    , m_started(false)
{
}

FileTransfer::~FileTransfer()
{
    Stop();
}

FileTransfer& FileTransfer::GetInstance()
{
    static FileTransfer instance;
    return instance;
}

void FileTransfer::Start()
{
    if (m_started)
        return;
    m_stop    = false;
    m_started = true;
    //串行 worker（单聊收发文件）
    m_worker.reset(new std::thread(&FileTransfer::WorkerProc, this));
    //群文件并发池：起 kMaxConcurrentGroupTransfers 个 worker，各自一条独立连接。
    for (int i = 0; i < kMaxConcurrentGroupTransfers; ++i)
    {
        m_poolWorkers.push_back(std::unique_ptr<std::thread>(
            new std::thread(&FileTransfer::PoolWorkerProc, this)));
    }
    LOG_INFO("FileTransfer started: 1 serial worker + %d group-transfer pool workers.",
             kMaxConcurrentGroupTransfers);
}

void FileTransfer::Stop()
{
    if (!m_started)
        return;

    m_stop = true;
    m_queueCv.notify_all();
    m_poolCv.notify_all();
    //关掉共享 file 连接，唤醒可能阻塞在分片收发上的串行 worker，加速退出。
    CIUSocket::GetInstance().CloseFileServerConnection();
    //关掉并发池里所有在传的独立连接，唤醒阻塞在 socket 收发上的池线程。
    {
        std::lock_guard<std::mutex> guard(m_poolMtx);
        for (std::set<FileServerConn*>::iterator it = m_activeConns.begin();
             it != m_activeConns.end(); ++it)
        {
            (*it)->Close();
        }
    }

    if (m_worker && m_worker->joinable())
        m_worker->join();
    m_worker.reset();

    for (size_t i = 0; i < m_poolWorkers.size(); ++i)
    {
        if (m_poolWorkers[i] && m_poolWorkers[i]->joinable())
            m_poolWorkers[i]->join();
    }
    m_poolWorkers.clear();

    m_started = false;
    LOG_INFO("FileTransfer stopped (serial worker + pool).");
}

void FileTransfer::EnqueueSend(int ctype, int64_t cid, const std::string& clientMsgId,
                               const std::wstring& localPath)
{
    Task task;
    task.m_kind        = task_send;
    task.m_ctype       = ctype;
    task.m_cid         = cid;
    task.m_clientmsgid = clientMsgId;
    task.m_localpath   = localPath;

    {
        std::lock_guard<std::mutex> guard(m_queueMtx);
        //登记取消标志后随任务入队：用户在排队阶段就点取消时，worker 取出即跳过。
        task.m_cancelFlag = m_cancelReg.RegisterSend(clientMsgId);
        m_queue.push_back(task);
    }
    m_queueCv.notify_one();
    LOG_INFO("FileTransfer enqueue send, ctype=%d, cid=%lld, clientmsgid=%s.",
             ctype, (long long)cid, clientMsgId.c_str());
}

void FileTransfer::EnqueueDownload(int64_t msgid, const std::string& fileid,
                                   const std::string& filename, const std::string& expectMd5,
                                   int64_t expectSize, const std::wstring& selfAccount)
{
    Task task;
    task.m_kind        = task_download;
    task.m_msgid       = msgid;
    task.m_fileid      = fileid;
    task.m_filename    = filename;
    task.m_expectMd5   = expectMd5;
    task.m_expectSize  = expectSize;
    task.m_selfAccount = selfAccount;

    {
        std::lock_guard<std::mutex> guard(m_queueMtx);
        //去重：该 msgid 已在下载中（如 live 追加已触发、历史渲染又触发）则跳过——
        //在途那份下载完成后会按 msgid 回投，更新当前那条气泡，无需重复下载。
        if (m_inflightDownloads.find(msgid) != m_inflightDownloads.end())
        {
            LOG_INFO("FileTransfer skip duplicate download (in-flight), msgid=%lld.",
                     (long long)msgid);
            return;
        }
        m_inflightDownloads.insert(msgid);
        //登记取消标志后随任务入队，理由同 EnqueueSend。
        task.m_cancelFlag = m_cancelReg.RegisterDownload(msgid);
        m_queue.push_back(task);
    }
    m_queueCv.notify_one();
    LOG_INFO("FileTransfer enqueue download, msgid=%lld, fileid=%s.",
             (long long)msgid, fileid.c_str());
}

int FileTransfer::EnqueueGroupFileUpload(int64_t groupId, const std::wstring& localPath,
                                         int64_t selfUserId)
{
    int uploadId = 0;
    {
        std::lock_guard<std::mutex> guard(m_poolMtx);
        uploadId = m_poolQueue.EnqueueUpload(groupId, localPath, selfUserId);
    }
    m_poolCv.notify_one();
    LOG_INFO("FileTransfer enqueue group-file upload, group=%lld, uploadId=%d.",
             (long long)groupId, uploadId);
    return uploadId;
}

void FileTransfer::CancelSend(const std::string& clientMsgId)
{
    bool found = false;
    {
        std::lock_guard<std::mutex> guard(m_queueMtx);
        found = m_cancelReg.CancelSend(clientMsgId);
    }
    //标志已置位：还在排队的会被 worker 取出时跳过，正在传的会在下一个分片边界中止。
    //两种情形都由 worker 统一回投 m_cancelled=true 的结果，这里不直接回投，避免与
    //worker 的回投重复（UI 会收到两条结果）。
    LOG_INFO("FileTransfer cancel send, clientmsgid=%s, found=%d.",
             clientMsgId.c_str(), found ? 1 : 0);
}

void FileTransfer::CancelDownload(int64_t msgid)
{
    bool found = false;
    {
        std::lock_guard<std::mutex> guard(m_queueMtx);
        found = m_cancelReg.CancelDownload(msgid);
    }
    LOG_INFO("FileTransfer cancel download, msgid=%lld, found=%d.",
             (long long)msgid, found ? 1 : 0);
}

void FileTransfer::CancelGroupFileUpload(int uploadId)
{
    {
        std::lock_guard<std::mutex> guard(m_poolMtx);
        m_poolQueue.CancelUpload(uploadId);
    }
    //若被取消的那笔仍在队列里（未轮到），上面已把它移除——无需唤醒（少一笔活而已）。
    //若它正在传，取消标志已置位，传输 worker 会在下一个分片边界自行中止。
    LOG_INFO("FileTransfer cancel group-file upload, uploadId=%d.", uploadId);
}

void FileTransfer::CancelGroupFileDownload(const std::string& entryid)
{
    {
        std::lock_guard<std::mutex> guard(m_poolMtx);
        m_poolQueue.CancelDownload(entryid);
    }
    LOG_INFO("FileTransfer cancel group-file download, entryid=%s.", entryid.c_str());
}

void FileTransfer::EnqueueGroupFileDownload(int64_t groupId, const std::string& entryid,
                                            const std::string& fileid, const std::string& filename,
                                            const std::string& expectMd5, int64_t expectSize,
                                            int64_t selfUserId, const std::wstring& destPath)
{
    bool accepted = false;
    {
        std::lock_guard<std::mutex> guard(m_poolMtx);
        accepted = m_poolQueue.EnqueueDownload(groupId, entryid, fileid, filename,
                                               expectMd5, expectSize, selfUserId, destPath);
    }
    if (!accepted)
    {
        //同一 entryid 已在队列 / 在传：去重，跳过（在途那份完成后会回投，更新面板该行）。
        LOG_INFO("FileTransfer skip duplicate group-file download (in-flight), entryid=%s.",
                 entryid.c_str());
        return;
    }
    m_poolCv.notify_one();
    LOG_INFO("FileTransfer enqueue group-file download, group=%lld, entryid=%s, fileid=%s.",
             (long long)groupId, entryid.c_str(), fileid.c_str());
}

void FileTransfer::WorkerProc()
{
    while (true)
    {
        Task task;
        {
            std::unique_lock<std::mutex> lk(m_queueMtx);
            while (!m_stop && m_queue.empty())
                m_queueCv.wait(lk);
            if (m_stop && m_queue.empty())
                return;
            task = m_queue.front();
            m_queue.pop_front();
        }
        if (task.m_kind == task_download)
            ProcessDownload(task);
        else
            ProcessSend(task);
    }
}

void FileTransfer::PoolWorkerProc()
{
    while (true)
    {
        GroupTransferItem item;
        {
            std::unique_lock<std::mutex> lk(m_poolMtx);
            while (!m_stop && m_poolQueue.PopNext(item) == false)
                m_poolCv.wait(lk);
            if (m_stop)
                return;
            //PopNext 在上面循环条件里已取出一笔（登记为在传）。
        }

        if (item.m_isUpload)
            ProcessPooledUpload(item);
        else
            ProcessPooledDownload(item);

        //收尾：清理该笔的在传簿记（取消标志 / entryid 去重位）。
        {
            std::lock_guard<std::mutex> lk(m_poolMtx);
            if (item.m_isUpload)
                m_poolQueue.CompleteUpload(item.m_uploadId);
            else
                m_poolQueue.CompleteDownload(item.m_entryId);
        }
    }
}

void FileTransfer::RegisterActiveConn(FileServerConn* conn)
{
    std::lock_guard<std::mutex> guard(m_poolMtx);
    m_activeConns.insert(conn);
}

void FileTransfer::UnregisterActiveConn(FileServerConn* conn)
{
    std::lock_guard<std::mutex> guard(m_poolMtx);
    m_activeConns.erase(conn);
}

void FileTransfer::ProcessSend(const Task& task)
{
    std::atomic<bool>* cancel = task.m_cancelFlag.get();

    //排队期间就被取消：还没开始传就直接回投，不必读盘、不必联网。
    if (cancel != NULL && cancel->load())
    {
        LOG_INFO("FileTransfer: send cancelled before start, clientmsgid=%s.",
                 task.m_clientmsgid.c_str());
        PostSendResult(task, false, error_code_upload_fail, "cancelled by user", "", "", "", 0);
        return;
    }

    //0. 取文件实际大小（流式上传的总长 + 拦掉空 / 打不开的文件）。聊天文件不限大小：
    //   下面的 MD5 与上传都是流式（分片读盘、读一片传一片，全程只占一个分片内存），
    //   不把整文件读入，故任意大小都不会撑爆 32 位进程的内存。
    int64_t fsizeOnDisk = getFileSizeOnDisk(task.m_localpath);
    if (fsizeOnDisk <= 0)
    {
        LOG_ERROR("FileTransfer: open/size file failed, clientmsgid=%s.", task.m_clientmsgid.c_str());
        PostSendResult(task, false, error_code_upload_fail, "open file failed or empty", "", "", "", 0);
        return;
    }

    int64_t size = fsizeOnDisk;

    //1. 流式算 MD5（分片读盘、不把整文件读入内存）。上传申请需先把 md5 报给服务端。
    std::string md5;
    if (!computeFileMd5Streaming(task.m_localpath, md5))
    {
        LOG_ERROR("FileTransfer: read file failed, clientmsgid=%s.", task.m_clientmsgid.c_str());
        PostSendResult(task, false, error_code_upload_fail, "read file failed", "", "", "", 0);
        return;
    }

    //2. 原始文件名（UTF-8，随消息发出并作上传申请的 name）+ 扩展名
    std::string filename = EncodeUtil::UnicodeToUtf8(extractFileNameW(task.m_localpath));
    std::string ext      = extractExtLower(task.m_localpath);

    //3. 申请上传（异步信令，阻塞等应答）。归属标记为文件传输，使应答回路由到本类。
    uint32_t seq = CIUSocket::GetInstance().RequestUpload(
        file_scene_chat_file, md5, size, filename, ext, transfer_owner_file);
    RequestUploadResult ru;
    if (!WaitUploadSignal(seq, ru, kSignalTimeoutSec))
    {
        LOG_ERROR("FileTransfer: request-upload timeout, seq=%u, clientmsgid=%s.",
                  seq, task.m_clientmsgid.c_str());
        PostSendResult(task, false, error_code_upload_fail, "request upload timeout", "", "", "", 0);
        return;
    }
    if (ru.m_code != error_code_ok)
    {
        LOG_ERROR("FileTransfer: request-upload rejected, code=%d, clientmsgid=%s.",
                  ru.m_code, task.m_clientmsgid.c_str());
        PostSendResult(task, false, ru.m_code, ru.m_msg, "", "", "", 0);
        return;
    }

    //4. 非秒传则流式上传字节（分片从磁盘读、读一片传一片；进度回报到聊天气泡进度条）
    if (!ru.m_exists)
    {
        if (CIUSocket::GetInstance().IsFileServerClosed())
        {
            if (!CIUSocket::GetInstance().ConnectToFileServer(5))
            {
                LOG_ERROR("FileTransfer: connect fileserver failed, clientmsgid=%s.", task.m_clientmsgid.c_str());
                PostSendResult(task, false, error_code_upload_fail, "connect fileserver failed", "", "", "", 0);
                return;
            }
        }

        PostingUploadProgressSink sink(CIUSocket::GetInstance().GetUIProxyWnd(), task.m_clientmsgid);
        int errcode = 0;
        std::string errmsg;
        //单聊发文件走共享 socket 通道；传入本笔的取消标志，用户点「取消」后在分片边界中止。
        MainFileChannel channel(this);
        bool ok = FileStreamTransfer::uploadFile(channel, task.m_localpath, size, ru.m_chunksize, ru.m_uploadtoken,
                                   &sink, cancel, errcode, errmsg);

        //一个文件传完即断开 file 连接，避免会话状态残留（下次发文件重连）。中途取消时
        //尤其必须断——共享通道上停在半截，服务端仍可能回下一片的回执，不断开会让下一笔
        //传输收到上一笔的遗留帧而串包。
        CIUSocket::GetInstance().CloseFileServerConnection();

        if (!ok)
        {
            LOG_ERROR("FileTransfer: upload bytes failed, errcode=%d, errmsg=%s, clientmsgid=%s.",
                      errcode, errmsg.c_str(), task.m_clientmsgid.c_str());
            PostSendResult(task, false, errcode, errmsg, "", "", "", 0);
            return;
        }
    }
    else
    {
        LOG_INFO("FileTransfer: instant-transfer hit, fileid=%s, clientmsgid=%s.",
                 ru.m_fileid.c_str(), task.m_clientmsgid.c_str());
    }

    //6. 发文件消息（发送回执沿用 FMG_MSG_V2_SEND_CHATMSG_ACK）
    CIUSocket::GetInstance().SendChatFileMessage(task.m_ctype, task.m_cid, task.m_clientmsgid,
                                                 ru.m_fileid, filename, size, md5);

    //7. 成功回投
    LOG_INFO("FileTransfer: file sent, fileid=%s, size=%lld, clientmsgid=%s.",
             ru.m_fileid.c_str(), (long long)size, task.m_clientmsgid.c_str());
    PostSendResult(task, true, 0, "", ru.m_fileid, md5, filename, size);
}

void FileTransfer::PostSendResult(const Task& task, bool ok, int errcode, const std::string& errmsg,
                                  const std::string& fileid, const std::string& md5,
                                  const std::string& filename, int64_t size)
{
    //本笔发送结束（成功 / 失败 / 被取消）：把它移出取消簿记。worker 手上的 shared_ptr
    //让标志对象继续存活，下面读取仍安全。
    {
        std::lock_guard<std::mutex> guard(m_queueMtx);
        m_cancelReg.CompleteSend(task.m_clientmsgid);
    }

    HWND hUI = CIUSocket::GetInstance().GetUIProxyWnd();
    FileSendResult* r = new FileSendResult();
    //失败 + 该笔取消标志已置位 → 判定为用户主动取消。所有失败路径都汇到这里，故取消
    //判定只需写这一处，无需在每个失败分支各传一个标记。
    r->m_cancelled = (!ok && task.m_cancelFlag && task.m_cancelFlag->load());
    r->m_clientmsgid = task.m_clientmsgid;
    r->m_ctype       = task.m_ctype;
    r->m_cid         = task.m_cid;
    r->m_ok          = ok;
    r->m_errcode     = errcode;
    r->m_errmsg      = errmsg;
    r->m_fileid      = fileid;
    r->m_md5         = md5;
    r->m_filename    = filename;
    r->m_size        = size;
    r->m_localpath   = task.m_localpath;   //成功时供 UI 登记本地落点

    if (::IsWindow(hUI))
        ::PostMessage(hUI, FMG_MSG_V2_FILE_SEND_RESULT, 0, (LPARAM)r);
    else
        delete r;   //UI 未就绪，丢弃以免泄漏
}

bool FileTransfer::WaitUploadSignal(uint32_t seq, RequestUploadResult& out, int timeoutSec)
{
    UploadWaiter* w = new UploadWaiter();

    //注册等待槽前先看是否已有"早到"的应答（接收线程可能抢在注册前到达）
    {
        std::lock_guard<std::mutex> guard(m_waiterMtx);
        std::map<uint32_t, RequestUploadResult>::iterator it = m_earlyResponses.find(seq);
        if (it != m_earlyResponses.end())
        {
            out = it->second;
            m_earlyResponses.erase(it);
            delete w;
            return true;
        }
        m_waiters[seq] = w;
    }

    //分片等待并周期性检查停止标志，使关停时 worker 能尽快退出
    bool responded = false;
    {
        std::chrono::steady_clock::time_point deadline =
            std::chrono::steady_clock::now() + std::chrono::seconds(timeoutSec);
        std::unique_lock<std::mutex> lk(w->m_mtx);
        while (!w->m_responded)
        {
            if (m_stop)
                break;
            std::chrono::steady_clock::time_point slice =
                std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
            if (slice > deadline)
                slice = deadline;
            w->m_cv.wait_until(lk, slice);
            if (!w->m_responded && std::chrono::steady_clock::now() >= deadline)
                break;
        }
        responded = w->m_responded;
        if (responded)
            out = w->m_result;
    }

    {
        std::lock_guard<std::mutex> guard(m_waiterMtx);
        m_waiters.erase(seq);
    }
    delete w;
    return responded;
}

void FileTransfer::OnRequestUploadResponse(uint32_t seq, const RequestUploadResult& r)
{
    std::lock_guard<std::mutex> guard(m_waiterMtx);
    std::map<uint32_t, UploadWaiter*>::iterator it = m_waiters.find(seq);
    if (it != m_waiters.end())
    {
        UploadWaiter* w = it->second;
        std::lock_guard<std::mutex> wlk(w->m_mtx);
        w->m_result    = r;
        w->m_responded = true;
        w->m_cv.notify_one();
        return;
    }
    //worker 尚未注册等待槽：暂存，待其注册时立即取用（防竞态）
    m_earlyResponses[seq] = r;
}

void FileTransfer::ProcessDownload(const Task& task)
{
    std::atomic<bool>* cancel = task.m_cancelFlag.get();

    //排队期间就被取消：还没开始下就直接回投，不必联网。
    if (cancel != NULL && cancel->load())
    {
        LOG_INFO("FileTransfer: download cancelled before start, msgid=%lld.",
                 (long long)task.m_msgid);
        PostDownloadResult(task.m_msgid, false, error_code_server_internal, std::wstring(),
                           task.m_expectMd5, task.m_filename);
        return;
    }

    //1. 申请下载（异步信令，阻塞等应答）。归属标记为文件传输。
    uint32_t seq = CIUSocket::GetInstance().RequestDownload(task.m_fileid, transfer_owner_file);
    RequestDownloadResult rd;
    if (!WaitDownloadSignal(seq, rd, kSignalTimeoutSec))
    {
        LOG_ERROR("FileTransfer: request-download timeout, seq=%u, msgid=%lld.",
                  seq, (long long)task.m_msgid);
        PostDownloadResult(task.m_msgid, false, error_code_server_internal, std::wstring(),
                           task.m_expectMd5, task.m_filename);
        return;
    }
    if (rd.m_code != error_code_ok)
    {
        LOG_ERROR("FileTransfer: request-download rejected, code=%d, msgid=%lld.",
                  rd.m_code, (long long)task.m_msgid);
        PostDownloadResult(task.m_msgid, false, rd.m_code, std::wstring(),
                           task.m_expectMd5, task.m_filename);
        return;
    }

    //2. 连 fileserver 跑字节流下载（带进度回投）
    if (CIUSocket::GetInstance().IsFileServerClosed())
    {
        if (!CIUSocket::GetInstance().ConnectToFileServer(5))
        {
            LOG_ERROR("FileTransfer: connect fileserver failed (download), msgid=%lld.",
                      (long long)task.m_msgid);
            PostDownloadResult(task.m_msgid, false, error_code_server_internal, std::wstring(),
                           task.m_expectMd5, task.m_filename);
            return;
        }
    }

    //3. 流式下载：收一片写一片、增量算 MD5，只占一个分片的内存。聊天文件已取消大小
    //   上限，若仍像早期那样把整文件累积进内存再落盘，超大文件会在 32 位客户端进程
    //   bad_alloc；同时流式实现自带取消检查，用户点「取消」即在分片边界中止。
    //   落地路径按内容 MD5 分目录、按原始文件名命名，目录由 buildFileCachePath 逐级建好。
    //   MD5 取消息 content 里的期望值，为空则用下载应答里的——它同时决定落地目录名，
    //   故必须在下载前就定下来（流式实现无法等下完再按实际 MD5 选目录）。
    std::string expect = !task.m_expectMd5.empty() ? task.m_expectMd5 : rd.m_md5;
    for (size_t i = 0; i < expect.size(); ++i)
    {
        if (expect[i] >= 'A' && expect[i] <= 'F')
            expect[i] = static_cast<char>(expect[i] - 'A' + 'a');
    }
    if (expect.empty())
    {
        LOG_ERROR("FileTransfer: download without md5 (neither msg nor response), msgid=%lld.",
                  (long long)task.m_msgid);
        CIUSocket::GetInstance().CloseFileServerConnection();
        PostDownloadResult(task.m_msgid, false, error_code_server_internal, std::wstring(),
                           task.m_expectMd5, task.m_filename);
        return;
    }

    std::wstring cachePath = buildFileCachePath(task.m_selfAccount, expect, task.m_filename);

    MainFileChannel channel(this);
    PostingDownloadProgressSink sink(CIUSocket::GetInstance().GetUIProxyWnd(), task.m_msgid);
    std::wstring outLocalPath;
    int          errcode = 0;
    std::string  errmsg;
    //streamDownloadFile 内部完成：分片收取 → 写 .downloading 临时文件 → 增量 MD5 →
    //校验 → 原子 rename 到 cachePath。失败 / 取消时它会自行删掉临时文件。
    bool ok = FileStreamTransfer::downloadFile(channel, rd.m_downloadtoken, expect, task.m_expectSize,
                                 cachePath, &sink, cancel, outLocalPath, errcode, errmsg);

    //一个文件下完即断开 file 连接，避免会话状态残留（下次重连）。中途取消时尤其必须断，
    //理由同 ProcessSend：共享通道停在半截，遗留帧会串到下一笔传输。
    CIUSocket::GetInstance().CloseFileServerConnection();

    if (!ok)
    {
        LOG_ERROR("FileTransfer: download failed, errcode=%d, errmsg=%s, msgid=%lld.",
                  errcode, errmsg.c_str(), (long long)task.m_msgid);
        PostDownloadResult(task.m_msgid, false, errcode, std::wstring(),
                           task.m_expectMd5, task.m_filename);
        return;
    }

    //路径先转 UTF-8 再以 %s 记日志：narrow 日志器对 %ls + 含中文宽字符串会因默认 "C"
    //locale 转多字节失败而算错长度，进而缓冲溢出触发断言（同 BalloonGroupChatWindow
    //里几处已修复的日志）。下载缓存路径必然含用户名 / 原始文件名，中文极常见。
    std::string pathUtf8 = EncodeUtil::UnicodeToUtf8(outLocalPath);
    LOG_INFO("FileTransfer: file downloaded, msgid=%lld, path=%s.",
             (long long)task.m_msgid, pathUtf8.c_str());
    PostDownloadResult(task.m_msgid, true, 0, outLocalPath, expect, task.m_filename);
}

void FileTransfer::PostDownloadResult(int64_t msgid, bool ok, int errcode,
                                      const std::wstring& localPath,
                                      const std::string& md5, const std::string& filename)
{
    //本条下载结束（成功 / 失败 / 被取消）：从在途集合移除放开后续下载，并把它移出取消
    //簿记。移出前先取一次取消状态——所有失败路径都汇到这里，取消判定只需写这一处。
    bool cancelled = false;
    {
        std::lock_guard<std::mutex> guard(m_queueMtx);
        cancelled = !ok && m_cancelReg.IsDownloadCancelled(msgid);
        m_cancelReg.CompleteDownload(msgid);
        m_inflightDownloads.erase(msgid);
    }

    HWND hUI = CIUSocket::GetInstance().GetUIProxyWnd();
    FileDownloadResult* r = new FileDownloadResult();
    r->m_msgid     = msgid;
    r->m_ok        = ok;
    r->m_errcode   = errcode;
    r->m_localpath = localPath;
    r->m_cancelled = cancelled;
    r->m_md5       = md5;        //成功时供 UI 按 (md5, 文件名) 登记本地落点
    r->m_filename  = filename;

    if (::IsWindow(hUI))
        ::PostMessage(hUI, FMG_MSG_V2_FILE_DOWNLOAD_RESULT, 0, (LPARAM)r);
    else
        delete r;
}


void FileTransfer::ProcessPooledUpload(const GroupTransferItem& item)
{
    int64_t  groupId  = item.m_groupId;
    int      uploadId = item.m_uploadId;
    std::atomic<bool>* cancel = item.m_cancelFlag.get();

    //并发取消：若在真正开始前用户已点 ✕，直接回投失败（UI 那行已被移除、会静默忽略）。
    if (cancel != NULL && cancel->load())
    {
        LOG_INFO("FileTransfer: group-file upload cancelled before start, uploadId=%d.", uploadId);
        PostGroupFileUploadResult(groupId, uploadId, false, error_code_upload_fail, "cancelled", "", "", 0);
        return;
    }

    //0. 取文件实际大小（流式上传的总长 + 拦掉空 / 打不开的文件）。群文件不限大小：下面的
    //   MD5 与上传都是流式（分片读盘、读一片传一片，全程只占一个分片内存），任意大小都不爆。
    int64_t fsizeOnDisk = getFileSizeOnDisk(item.m_localPath);
    if (fsizeOnDisk <= 0)
    {
        LOG_ERROR("FileTransfer: group-file open/size failed, group=%lld, uploadId=%d.",
                  (long long)groupId, uploadId);
        PostGroupFileUploadResult(groupId, uploadId, false, error_code_upload_fail,
                                  "open file failed or empty", "", "", 0);
        return;
    }
    int64_t size = fsizeOnDisk;

    //1. 流式算 MD5（分片读盘、不把整文件读入内存）。上传申请需先把 md5 报给服务端。
    std::string md5;
    if (!computeFileMd5Streaming(item.m_localPath, md5))
    {
        LOG_ERROR("FileTransfer: group-file read/md5 failed, group=%lld, uploadId=%d.",
                  (long long)groupId, uploadId);
        PostGroupFileUploadResult(groupId, uploadId, false, error_code_upload_fail, "read file failed", "", "", 0);
        return;
    }

    //2. 原始文件名（UTF-8）+ 扩展名
    std::string filename = EncodeUtil::UnicodeToUtf8(extractFileNameW(item.m_localPath));
    std::string ext      = extractExtLower(item.m_localPath);

    //3. 申请上传（scene=群文件；归属标文件传输；seq 唯一，应答经 m_waiters 按 seq 回到本笔）
    uint32_t seq = CIUSocket::GetInstance().RequestUpload(
        file_scene_group_file, md5, size, filename, ext, transfer_owner_file);
    RequestUploadResult ru;
    if (!WaitUploadSignal(seq, ru, kSignalTimeoutSec))
    {
        LOG_ERROR("FileTransfer: group-file request-upload timeout, seq=%u, group=%lld, uploadId=%d.",
                  seq, (long long)groupId, uploadId);
        PostGroupFileUploadResult(groupId, uploadId, false, error_code_upload_fail,
                                  "request upload timeout", "", "", 0);
        return;
    }
    if (ru.m_code != error_code_ok)
    {
        LOG_ERROR("FileTransfer: group-file request-upload rejected, code=%d, group=%lld, uploadId=%d.",
                  ru.m_code, (long long)groupId, uploadId);
        PostGroupFileUploadResult(groupId, uploadId, false, ru.m_code, ru.m_msg, "", "", 0);
        return;
    }

    //4. 非秒传则在一条独立连接上流式上传字节（传完即断）。秒传则跳过字节流。
    if (!ru.m_exists)
    {
        FileServerConn conn;
        if (!conn.Connect(CIUSocket::GetInstance().GetFileServerAddr(),
                          CIUSocket::GetInstance().GetFilePort(), 5))
        {
            LOG_ERROR("FileTransfer: group-file pool connect fileserver failed, group=%lld, uploadId=%d.",
                      (long long)groupId, uploadId);
            PostGroupFileUploadResult(groupId, uploadId, false, error_code_upload_fail,
                                      "connect fileserver failed", "", "", 0);
            return;
        }
        RegisterActiveConn(&conn);   //登记，Stop 时可关掉以解阻塞

        int errcode = 0;
        std::string errmsg;
        //群文件上传进度按 groupid + uploadId 回投到对应群聊窗那一行「上传中」。
        PostingGroupFileUploadProgressSink sink(CIUSocket::GetInstance().GetUIProxyWnd(),
                                                groupId, uploadId);
        bool ok = FileStreamTransfer::uploadFile(conn, item.m_localPath, size, ru.m_chunksize, ru.m_uploadtoken,
                                   &sink, cancel, errcode, errmsg);

        UnregisterActiveConn(&conn);
        conn.Close();   //一笔传完即断

        if (!ok)
        {
            LOG_ERROR("FileTransfer: group-file pool upload bytes failed, errcode=%d, group=%lld, uploadId=%d.",
                      errcode, (long long)groupId, uploadId);
            PostGroupFileUploadResult(groupId, uploadId, false, errcode, errmsg, "", "", 0);
            return;
        }
    }
    else
    {
        LOG_INFO("FileTransfer: group-file instant-transfer hit, fileid=%s, group=%lld, uploadId=%d.",
                 ru.m_fileid.c_str(), (long long)groupId, uploadId);
    }

    //6. 成功回投（不发聊天消息；群聊窗据 fileid 发 cmd_add_group_file 登记进群文件库）
    LOG_INFO("FileTransfer: group-file uploaded, fileid=%s, size=%lld, group=%lld, uploadId=%d.",
             ru.m_fileid.c_str(), (long long)size, (long long)groupId, uploadId);
    PostGroupFileUploadResult(groupId, uploadId, true, 0, "", ru.m_fileid, filename, size);
}

void FileTransfer::PostGroupFileUploadResult(int64_t groupId, int uploadId, bool ok, int errcode,
                                             const std::string& errmsg, const std::string& fileid,
                                             const std::string& filename, int64_t size)
{
    HWND hUI = CIUSocket::GetInstance().GetUIProxyWnd();
    GroupFileUploadResult* r = new GroupFileUploadResult();
    r->m_groupid  = groupId;
    r->m_uploadid = uploadId;
    r->m_ok       = ok;
    r->m_errcode  = errcode;
    r->m_errmsg   = errmsg;
    r->m_fileid   = fileid;
    r->m_filename = filename;
    r->m_size     = size;
    if (::IsWindow(hUI))
        ::PostMessage(hUI, FMG_MSG_V2_GROUP_FILE_UPLOAD_RESULT, 0, (LPARAM)r);
    else
        delete r;
}


void FileTransfer::ProcessPooledDownload(const GroupTransferItem& item)
{
    int64_t            groupId = item.m_groupId;
    const std::string& entryId = item.m_entryId;
    std::atomic<bool>* cancel  = item.m_cancelFlag.get();

    //并发取消：若在真正开始前用户已点 ✕，直接回投失败（下载失败本就静默，UI 那行已恢复）。
    if (cancel != NULL && cancel->load())
    {
        LOG_INFO("FileTransfer: group-file download cancelled before start, entryid=%s.", entryId.c_str());
        PostGroupFileDownloadResult(groupId, entryId, false, error_code_server_internal, std::wstring());
        return;
    }

    //1. 申请下载（归属标文件传输；seq 唯一，应答经 m_dlWaiters 按 seq 回到本笔）
    uint32_t seq = CIUSocket::GetInstance().RequestDownload(item.m_fileId, transfer_owner_file);
    RequestDownloadResult rd;
    if (!WaitDownloadSignal(seq, rd, kSignalTimeoutSec))
    {
        LOG_ERROR("FileTransfer: group-file request-download timeout, seq=%u, entryid=%s.",
                  seq, entryId.c_str());
        PostGroupFileDownloadResult(groupId, entryId, false, error_code_server_internal, std::wstring());
        return;
    }
    if (rd.m_code != error_code_ok)
    {
        LOG_ERROR("FileTransfer: group-file request-download rejected, code=%d, entryid=%s.",
                  rd.m_code, entryId.c_str());
        PostGroupFileDownloadResult(groupId, entryId, false, rd.m_code, std::wstring());
        return;
    }

    //2. 建一条独立连接（不碰共享 socket，传完即断）
    FileServerConn conn;
    if (!conn.Connect(CIUSocket::GetInstance().GetFileServerAddr(),
                      CIUSocket::GetInstance().GetFilePort(), 5))
    {
        LOG_ERROR("FileTransfer: group-file pool connect fileserver failed (download), entryid=%s.",
                  entryId.c_str());
        PostGroupFileDownloadResult(groupId, entryId, false, error_code_server_internal, std::wstring());
        return;
    }
    RegisterActiveConn(&conn);

    //3. 流式下载（边收边写盘、增量校验，全程只占一个分片内存）。期望 md5 优先用条目带的。
    std::string expect = !item.m_expectMd5.empty() ? item.m_expectMd5 : rd.m_md5;
    std::wstring localPath;
    int errcode = 0;
    std::string errmsg;
    //下载进度按 groupid + entryid 回投，在面板里那一行就地显示。
    PostingGroupFileDownloadProgressSink sink(CIUSocket::GetInstance().GetUIProxyWnd(),
                                              groupId, entryId);
    bool ok = FileStreamTransfer::downloadFile(conn, rd.m_downloadtoken, expect, rd.m_size, item.m_destPath,
                                 &sink, item.m_cancelFlag.get(),
                                 localPath, errcode, errmsg);

    UnregisterActiveConn(&conn);
    conn.Close();   //一笔下完即断

    if (!ok)
    {
        LOG_ERROR("FileTransfer: group-file pool download failed, errcode=%d, entryid=%s, msg=%s.",
                  errcode, entryId.c_str(), errmsg.c_str());
        PostGroupFileDownloadResult(groupId, entryId, false, errcode, std::wstring());
        return;
    }

    LOG_INFO("FileTransfer: group-file downloaded, entryid=%s.", entryId.c_str());
    PostGroupFileDownloadResult(groupId, entryId, true, 0, localPath);
}

void FileTransfer::PostGroupFileDownloadResult(int64_t groupId, const std::string& entryid,
                                               bool ok, int errcode, const std::wstring& localPath)
{
    HWND hUI = CIUSocket::GetInstance().GetUIProxyWnd();
    GroupFileDownloadResult* r = new GroupFileDownloadResult();
    r->m_groupid   = groupId;
    r->m_entryid   = entryid;
    r->m_ok        = ok;
    r->m_errcode   = errcode;
    r->m_localpath = localPath;
    if (::IsWindow(hUI))
        ::PostMessage(hUI, FMG_MSG_V2_GROUP_FILE_DOWNLOAD_RESULT, 0, (LPARAM)r);
    else
        delete r;
}

bool FileTransfer::WaitDownloadSignal(uint32_t seq, RequestDownloadResult& out, int timeoutSec)
{
    DownloadWaiter* w = new DownloadWaiter();

    //注册等待槽前先看是否已有"早到"的应答（接收线程可能抢在注册前到达）
    {
        std::lock_guard<std::mutex> guard(m_waiterMtx);
        std::map<uint32_t, RequestDownloadResult>::iterator it = m_dlEarlyResponses.find(seq);
        if (it != m_dlEarlyResponses.end())
        {
            out = it->second;
            m_dlEarlyResponses.erase(it);
            delete w;
            return true;
        }
        m_dlWaiters[seq] = w;
    }

    bool responded = false;
    {
        std::chrono::steady_clock::time_point deadline =
            std::chrono::steady_clock::now() + std::chrono::seconds(timeoutSec);
        std::unique_lock<std::mutex> lk(w->m_mtx);
        while (!w->m_responded)
        {
            if (m_stop)
                break;
            std::chrono::steady_clock::time_point slice =
                std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
            if (slice > deadline)
                slice = deadline;
            w->m_cv.wait_until(lk, slice);
            if (!w->m_responded && std::chrono::steady_clock::now() >= deadline)
                break;
        }
        responded = w->m_responded;
        if (responded)
            out = w->m_result;
    }

    {
        std::lock_guard<std::mutex> guard(m_waiterMtx);
        m_dlWaiters.erase(seq);
    }
    delete w;
    return responded;
}

void FileTransfer::OnRequestDownloadResponse(uint32_t seq, const RequestDownloadResult& r)
{
    std::lock_guard<std::mutex> guard(m_waiterMtx);
    std::map<uint32_t, DownloadWaiter*>::iterator it = m_dlWaiters.find(seq);
    if (it != m_dlWaiters.end())
    {
        DownloadWaiter* w = it->second;
        std::lock_guard<std::mutex> wlk(w->m_mtx);
        w->m_result    = r;
        w->m_responded = true;
        w->m_cv.notify_one();
        return;
    }
    //worker 尚未注册等待槽：暂存，待其注册时立即取用（防竞态）
    m_dlEarlyResponses[seq] = r;
}

bool FileTransfer::RecvOneFileFrame(std::string& body, int timeoutSec)
{
    //先收 24 字节帧头
    char hdr[FRAME_HEADER_SIZE] = { 0 };
    if (!CIUSocket::GetInstance().RecvOnFilePort(hdr, FRAME_HEADER_SIZE, timeoutSec))
        return false;

    //bodylen 为大端 uint32，位于帧头偏移 12（magic2+ver1+flags1+cmd2+reserved2+seq4）
    uint32_t bodylen = (static_cast<uint32_t>(static_cast<unsigned char>(hdr[12])) << 24)
                     | (static_cast<uint32_t>(static_cast<unsigned char>(hdr[13])) << 16)
                     | (static_cast<uint32_t>(static_cast<unsigned char>(hdr[14])) << 8)
                     |  static_cast<uint32_t>(static_cast<unsigned char>(hdr[15]));
    if (bodylen > MAX_BODY_FILESERVER)
        return false;

    //拼成完整帧再交给 decodeFrame 做魔数 / 版本 / 长度 / CRC 校验
    std::string buf;
    buf.resize(FRAME_HEADER_SIZE + bodylen);
    memcpy(&buf[0], hdr, FRAME_HEADER_SIZE);
    if (bodylen > 0)
    {
        if (!CIUSocket::GetInstance().RecvOnFilePort(&buf[FRAME_HEADER_SIZE],
                                                     static_cast<int64_t>(bodylen), timeoutSec))
            return false;
    }

    //frame_header 在 Protocol.h 中是全局类型（非 net 命名空间），decodeFrame /
    //frame_decode_ok 才在 net 下，故此处不加 net:: 前缀。
    frame_header header;
    std::string decoded;
    size_t consumed = 0;
    if (net::decodeFrame(buf.data(), buf.size(), header, decoded, consumed,
                         MAX_BODY_FILESERVER) != net::frame_decode_ok)
        return false;

    //还原包体：握手完成后服务端发来的包体是加密的，须在此解出明文再交给业务层。
    //解压也一并由 FramePayload 处理（本链路的帧目前都不压缩，但按标志位走才不会
    //在服务端哪天开了压缩之后悄悄解错）。
    std::string err;
    if (!net::decodeFramePayload(header.cmd, header.seq, header.flags, header.originlen,
                                 decoded, CIUSocket::GetInstance().GetFileCipher(),
                                 body, err))
    {
        LOG_ERROR("RecvOneFileFrame: restore body failed: %s, cmd=%u, bodylen=%u.",
                  err.c_str(), header.cmd, (uint32_t)decoded.size());
        //解密失败意味着这个方向的流已不可恢复，连接不能再用。关掉它，
        //下一笔传输会重新建连并重新握手。
        CIUSocket::GetInstance().CloseFileServerConnection();
        return false;
    }

    return true;
}

} // namespace net
