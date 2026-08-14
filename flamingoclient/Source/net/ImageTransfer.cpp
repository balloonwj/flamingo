/**
 *  聊天图片传输管理器（客户端侧，发送方向）实现，ImageTransfer.cpp
 *  balloonwj@qq.com   2026-06-03
 */
#include "stdafx.h"
#include "ImageTransfer.h"

#include "IUSocket.h"
#include "ImageUploadDriver.h"
#include "ImageDownloadDriver.h"    // 下载分片驱动
#include "FrameCodec.h"             // encodeFrame / encodeAttachmentBody / decodeFrame（内含 Protocol.h）
#include "FramePayload.h"           // encodeFramePayload / decodeFramePayload（压缩与加密管线）
#include "../UserSessionData.h"     // FMG_MSG_V2_IMAGE_SEND_*
#include "../IULog.h"               // LOG_INFO / LOG_ERROR
#include "../MD5Sum.h"              // MD5Init / MD5Update / MD5Final
#include "../EncodeUtil.h"          // UnicodeToUtf8（日志里打登录账号用）
#include "UserCachePath.h"          // UserCacheDir（Users\<登录账号>\ 的统一口径）

#include <fstream>
#include <chrono>
#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")

namespace net {

//图片消息文件大小上限：20 MiB，与服务端 FileProtocol.h 的 FILE_MAX_SIZE_IMAGE_MSG
//一致。本地先行校验，避免把超大文件白传一趟再被服务端拒绝。
static const int64_t kMaxImageBytes = 20LL * 1024 * 1024;

//申请上传信令应答的等待超时：15 秒。正常往返为毫秒级，留足余量兜底网络抖动。
static const int kUploadSignalTimeoutSec = 15;

//字节流分片收发的单次超时：30 秒。单片 512KiB，30 秒足够，超时即判连接异常。
static const int kChunkIoTimeoutSec = 30;

//把整个文件读入内存（图片 ≤20MiB，整文件常驻可接受）；失败返回 false。
static bool readWholeFile(const std::wstring& path, std::string& out)
{
    std::ifstream in(path.c_str(), std::ios::binary | std::ios::ate);
    if (!in)
        return false;

    std::streamoff sz = in.tellg();
    if (sz <= 0)
        return false;

    in.seekg(0, std::ios::beg);
    out.resize(static_cast<size_t>(sz));
    in.read(&out[0], static_cast<std::streamsize>(sz));
    return in.good() || in.gcount() == static_cast<std::streamsize>(sz);
}

//对内存中的字节计算 MD5，返回小写十六进制 32 位串（与服务端比较口径一致）。
static std::string computeMd5Lower(const std::string& bytes)
{
    MD5_CTX ctx;
    MD5Init(&ctx);
    MD5Update(&ctx, reinterpret_cast<const unsigned char*>(bytes.data()),
              static_cast<unsigned int>(bytes.size()));
    unsigned char digest[16] = { 0 };
    MD5Final(digest, &ctx);

    char hex[33] = { 0 };
    for (int i = 0; i < 16; ++i)
        sprintf_s(hex + i * 2, 3, "%02x", digest[i]);
    return std::string(hex, 32);
}

//用 GDI+ 读取图片像素宽高；非图片 / 解码失败返回 false。
static bool readImageSize(const std::wstring& path, int& width, int& height)
{
    Gdiplus::Image img(path.c_str());
    if (img.GetLastStatus() != Gdiplus::Ok)
        return false;
    width  = static_cast<int>(img.GetWidth());
    height = static_cast<int>(img.GetHeight());
    return width > 0 && height > 0;
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

//把内存字节整块写入指定路径（覆盖）；失败返回 false。
static bool writeWholeFile(const std::wstring& path, const std::string& bytes)
{
    std::ofstream out(path.c_str(), std::ios::binary | std::ios::trunc);
    if (!out)
        return false;
    if (!bytes.empty())
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    return out.good();
}

//构造图片下载缓存文件的完整路径：<g_szHomePath>Users\<登录账号>\ChatImage\<md5>，
//并按需逐级创建 Users、Users\<账号>、Users\<账号>\ChatImage 三级目录。落地
//写文件前调用。账号为空（未登录）时返回空串，调用方按落地失败处理。
static std::wstring buildImageCachePath(const std::wstring& account, const std::string& md5)
{
    const std::wstring imageDir = net::UserCacheDir(g_szDataRoot, account, L"ChatImage");
    if (imageDir.empty())
        return std::wstring();

    //CreateDirectoryW 只建最后一级，故从 Users 往下逐级调用；已存在返回失败，忽略即可。
    std::wstring usersRoot = g_szDataRoot;   //约定以反斜杠结尾（见 IUSocket 对其用法）
    usersRoot += L"Users\\";
    ::CreateDirectoryW(usersRoot.c_str(), NULL);
    ::CreateDirectoryW(net::UserCacheDir(g_szDataRoot, account, NULL).c_str(), NULL);
    ::CreateDirectoryW(imageDir.c_str(), NULL);
    return ImageTransfer::CacheFilePathForMd5(account, md5);
}

//构造头像缓存文件完整路径：<g_szHomePath>Users\<登录账号>\Avatar\<fileid>，并按需
//逐级创建 Users、Users\<账号>、Users\<账号>\Avatar 三级目录。落地写文件前调用。
//账号为空（未登录）时返回空串，调用方按落地失败处理。
static std::wstring buildAvatarCachePath(const std::wstring& account, const std::string& fileid)
{
    const std::wstring avatarDir = net::UserCacheDir(g_szHomePath, account, L"Avatar");
    if (avatarDir.empty())
        return std::wstring();

    std::wstring usersRoot = g_szDataRoot;   //约定以反斜杠结尾
    usersRoot += L"Users\\";
    ::CreateDirectoryW(usersRoot.c_str(), NULL);
    ::CreateDirectoryW(net::UserCacheDir(g_szDataRoot, account, NULL).c_str(), NULL);
    ::CreateDirectoryW(avatarDir.c_str(), NULL);
    return ImageTransfer::AvatarCacheFilePathForFileId(account, fileid);
}

//上传分片的真实传输实现：组 cmd_file_upload(1851) 附件帧、经 img socket 发出、
//收回执帧并解析。供 ImageUploadDriver 驱动调用。
class ImgSocketUploadTransport : public IUploadChunkTransport
{
public:
    ImgSocketUploadTransport(const std::string& uploadToken, ImageTransfer* owner)
        : m_token(uploadToken)
        , m_owner(owner)
        , m_seq(1)
    {
    }

    bool uploadChunk(int64_t offset, const char* data, int64_t len, UploadChunkAck& ack) override
    {
        //描述符仅需 uploadtoken + offset（服务端据令牌取 md5/大小）
        std::string descriptor = FileProtocolClient::buildUploadChunkDescriptor(m_token, offset);

        //附件包体：4 字节大端 desclen + 描述符 + 本片二进制
        std::string attBody;
        if (!net::encodeAttachmentBody(descriptor, data + offset,
                                       static_cast<uint32_t>(len), attBody))
            return false;

        //包体先过 FramePayload 这条管线（握手完成后即为加密），再套帧头，置 has_attachment。
        //压缩策略取 never：图片本身就是压缩格式，再走一遍 zlib 压不动、只白费 CPU，
        //这也与接入加密之前的行为一致。
        const uint32_t uploadSeq = m_seq++;
        std::string wire;
        uint8_t     flags = 0;
        uint32_t    originlen = 0;
        if (!net::encodeFramePayload(cmd_file_upload, uploadSeq, attBody,
                                     CIUSocket::GetInstance().GetImgCipher(),
                                     frame_flag_has_attachment, net::payload_compress_never,
                                     wire, flags, originlen))
            return false;

        std::string frame;
        if (!net::encodeFrame(cmd_file_upload, uploadSeq, flags,
                              wire.data(), static_cast<uint32_t>(wire.size()),
                              originlen, frame))
            return false;

        if (!CIUSocket::GetInstance().SendOnImgPort(frame.data(),
                                                    static_cast<int64_t>(frame.size()),
                                                    kChunkIoTimeoutSec))
            return false;

        //收回执帧（普通 JSON 包体，无附件）
        std::string ackBody;
        if (!m_owner->RecvOneImgFrame(ackBody, kChunkIoTimeoutSec))
            return false;

        return FileProtocolClient::parseUploadChunkAck(ackBody, ack);
    }

private:
    std::string    m_token;   //上传令牌
    ImageTransfer* m_owner;    //回主类收帧
    uint32_t       m_seq;      //img 连接上分片帧的本地序号（同步请求/应答，仅作帧序号）
};

//把上传进度回投到 UI 的进度 sink。
class PostingProgressSink : public IUploadProgressSink
{
public:
    PostingProgressSink(HWND hUI, const std::string& clientMsgId)
        : m_hUI(hUI)
        , m_clientmsgid(clientMsgId)
    {
    }

    void onProgress(int64_t sent, int64_t total) override
    {
        if (m_hUI == NULL)
            return;
        ImageSendProgress* p = new ImageSendProgress();
        p->m_clientmsgid = m_clientmsgid;
        p->m_sent        = sent;
        p->m_total       = total;
        ::PostMessage(m_hUI, FMG_MSG_V2_IMAGE_SEND_PROGRESS, 0, (LPARAM)p);
    }

private:
    HWND        m_hUI;
    std::string m_clientmsgid;
};

//下载分片的真实传输实现：组 cmd_file_download(1852) 请求帧（普通 JSON 包体）、
//经 img socket 发出、收回应答帧（HAS_ATTACHMENT：描述符 + 二进制）并拆解。
//供 ImageDownloadDriver 驱动调用。
class ImgSocketDownloadTransport : public IDownloadChunkTransport
{
public:
    ImgSocketDownloadTransport(const std::string& downloadToken, ImageTransfer* owner)
        : m_token(downloadToken)
        , m_owner(owner)
        , m_seq(1)
    {
    }

    bool downloadChunk(int64_t offset, DownloadChunkInfo& info, std::string& chunk) override
    {
        //请求帧：普通 JSON 包体 { downloadtoken, offset }，无附件、不压缩。
        //握手完成后包体由 FramePayload 加密，未握手时它自然产出明文。
        std::string descriptor = FileProtocolClient::buildDownloadChunkDescriptor(m_token, offset);
        const uint32_t downloadSeq = m_seq++;
        std::string wire;
        uint8_t     flags = 0;
        uint32_t    originlen = 0;
        if (!net::encodeFramePayload(cmd_file_download, downloadSeq, descriptor,
                                     CIUSocket::GetInstance().GetImgCipher(), 0,
                                     net::payload_compress_never, wire, flags, originlen))
            return false;

        std::string frame;
        if (!net::encodeFrame(cmd_file_download, downloadSeq, flags,
                              wire.data(), static_cast<uint32_t>(wire.size()),
                              originlen, frame))
            return false;
        if (!CIUSocket::GetInstance().SendOnImgPort(frame.data(),
                                                    static_cast<int64_t>(frame.size()),
                                                    kChunkIoTimeoutSec))
            return false;

        //应答帧：HAS_ATTACHMENT，包体 = 4字节desclen + 描述符 + 本片二进制
        std::string body;
        if (!m_owner->RecvOneImgFrame(body, kChunkIoTimeoutSec))
            return false;

        std::string respDesc;
        size_t binoff = 0;
        size_t binlen = 0;
        if (!net::decodeAttachmentBody(body.data(), body.size(), respDesc, binoff, binlen))
            return false;
        if (!FileProtocolClient::parseDownloadChunkInfo(respDesc, info))
            return false;

        chunk.assign(body.data() + binoff, binlen);
        return true;
    }

private:
    std::string    m_token;   //下载令牌
    ImageTransfer* m_owner;    //回主类收帧
    uint32_t       m_seq;      //img 连接上分片帧的本地序号
};

ImageTransfer::ImageTransfer()
    : m_stop(false)
    , m_started(false)
{
}

ImageTransfer::~ImageTransfer()
{
    Stop();
}

ImageTransfer& ImageTransfer::GetInstance()
{
    static ImageTransfer instance;
    return instance;
}

std::wstring ImageTransfer::CacheFilePathForMd5(const std::wstring& account, const std::string& md5)
{
    if (md5.empty())
        return std::wstring();
    //缓存按登录账号分隔、按内容 MD5 命名：<g_szHomePath>Users\<账号>\ChatImage\<md5>。
    //目录那一截统一由 UserCacheDir 拼（Users 下的命名口径只此一处），账号为空时它
    //返回空串。md5 为 ASCII 十六进制，逐字符宽化即可。本方法只拼路径、不创建目录、
    //不判断存在。
    std::wstring path = net::UserCacheDir(g_szDataRoot, account, L"ChatImage");
    if (path.empty())
        return std::wstring();

    for (size_t i = 0; i < md5.size(); ++i)
        path.push_back(static_cast<wchar_t>(md5[i]));
    return path;
}

std::wstring ImageTransfer::AvatarCacheFilePathForFileId(const std::wstring& account,
                                                         const std::string& fileid)
{
    if (fileid.empty())
        return std::wstring();
    //缓存按登录账号分隔、按 fileid 命名：<g_szHomePath>Users\<账号>\Avatar\<fileid>。
    //fileid 为字符串编码的数字（ASCII），逐字符宽化即可；只拼路径、不建目录、不判存在。
    std::wstring path = net::UserCacheDir(g_szHomePath, account, L"Avatar");
    if (path.empty())
        return std::wstring();

    for (size_t i = 0; i < fileid.size(); ++i)
        path.push_back(static_cast<wchar_t>(fileid[i]));
    return path;
}

void ImageTransfer::Start()
{
    if (m_started)
        return;
    m_stop    = false;
    m_started = true;
    m_worker.reset(new std::thread(&ImageTransfer::WorkerProc, this));
    LOG_INFO("ImageTransfer worker started.");
}

void ImageTransfer::Stop()
{
    if (!m_started)
        return;

    m_stop = true;
    m_queueCv.notify_all();
    //关掉 img 连接，唤醒可能阻塞在分片收发上的 worker，加速退出
    CIUSocket::GetInstance().CloseImgServerConnection();

    if (m_worker && m_worker->joinable())
        m_worker->join();
    m_worker.reset();
    m_started = false;
    LOG_INFO("ImageTransfer worker stopped.");
}

void ImageTransfer::EnqueueSend(int ctype, int64_t cid, const std::string& clientMsgId,
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
        m_queue.push_back(task);
    }
    m_queueCv.notify_one();
    LOG_INFO("ImageTransfer enqueue send, ctype=%d, cid=%lld, clientmsgid=%s.",
             ctype, (long long)cid, clientMsgId.c_str());
}

void ImageTransfer::EnqueueDownload(int64_t msgid, const std::string& fileid,
                                    const std::string& expectMd5, int64_t expectSize,
                                    const std::wstring& selfAccount)
{
    Task task;
    task.m_kind        = task_download;
    task.m_msgid       = msgid;
    task.m_fileid      = fileid;
    task.m_expectMd5   = expectMd5;
    task.m_expectSize  = expectSize;
    task.m_selfAccount = selfAccount;

    {
        std::lock_guard<std::mutex> guard(m_queueMtx);
        //去重：该 msgid 已在下载中（如 live 追加已触发、历史渲染又触发）则跳过——
        //在途那份下载完成后会按 msgid 回投，更新当前那条气泡，无需重复下载。
        if (m_inflightDownloads.find(msgid) != m_inflightDownloads.end())
        {
            LOG_INFO("ImageTransfer skip duplicate download (in-flight), msgid=%lld.",
                     (long long)msgid);
            return;
        }
        m_inflightDownloads.insert(msgid);
        m_queue.push_back(task);
    }
    m_queueCv.notify_one();
    LOG_INFO("ImageTransfer enqueue download, msgid=%lld, fileid=%s.",
             (long long)msgid, fileid.c_str());
}

void ImageTransfer::EnqueueAvatarUpload(const std::wstring& localPath,
                                        const std::wstring& selfAccount)
{
    Task task;
    task.m_kind        = task_avatar_upload;
    task.m_localpath   = localPath;
    task.m_selfAccount = selfAccount;

    {
        std::lock_guard<std::mutex> guard(m_queueMtx);
        m_queue.push_back(task);
    }
    m_queueCv.notify_one();
    LOG_INFO("ImageTransfer enqueue avatar upload, account=%s.",
             EncodeUtil::UnicodeToUtf8(selfAccount).c_str());
}

void ImageTransfer::EnqueueAvatarDownload(const std::string& fileid,
                                          const std::wstring& selfAccount)
{
    if (fileid.empty() || selfAccount.empty())
    {
        return;
    }

    //已缓存则无需下载：直接回投成功，UI 据本地路径渲染。
    std::wstring cached = AvatarCacheFilePathForFileId(selfAccount, fileid);
    if (!cached.empty() && ::GetFileAttributesW(cached.c_str()) != INVALID_FILE_ATTRIBUTES)
    {
        PostAvatarDownloadResult(fileid, true, cached);
        return;
    }

    Task task;
    task.m_kind        = task_avatar_download;
    task.m_fileid      = fileid;
    task.m_selfAccount = selfAccount;

    {
        std::lock_guard<std::mutex> guard(m_queueMtx);
        //按 fileid 去重：多个节点引用同一头像只下一次。
        if (m_inflightAvatarDownloads.find(fileid) != m_inflightAvatarDownloads.end())
        {
            return;
        }
        m_inflightAvatarDownloads.insert(fileid);
        m_queue.push_back(task);
    }
    m_queueCv.notify_one();
    LOG_INFO("ImageTransfer enqueue avatar download, fileid=%s.", fileid.c_str());
}

void ImageTransfer::WorkerProc()
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
        else if (task.m_kind == task_avatar_upload)
            ProcessAvatarUpload(task);
        else if (task.m_kind == task_avatar_download)
            ProcessAvatarDownload(task);
        else
            ProcessSend(task);
    }
}

void ImageTransfer::ProcessSend(const Task& task)
{
    //1. 读文件
    std::string bytes;
    if (!readWholeFile(task.m_localpath, bytes))
    {
        LOG_ERROR("ImageTransfer: read file failed, clientmsgid=%s.", task.m_clientmsgid.c_str());
        PostSendResult(task, false, error_code_upload_fail, "read file failed", "", "", 0, 0, 0);
        return;
    }
    int64_t size = static_cast<int64_t>(bytes.size());
    if (size <= 0 || size > kMaxImageBytes)
    {
        LOG_ERROR("ImageTransfer: bad image size %lld, clientmsgid=%s.",
                  (long long)size, task.m_clientmsgid.c_str());
        PostSendResult(task, false, error_code_file_too_large, "image too large or empty", "", "", 0, 0, 0);
        return;
    }

    //2. MD5（小写 hex）
    std::string md5 = computeMd5Lower(bytes);

    //3. 宽高（同时作为"是否为可解码图片"的校验）
    int width = 0;
    int height = 0;
    if (!readImageSize(task.m_localpath, width, height))
    {
        LOG_ERROR("ImageTransfer: decode image size failed, clientmsgid=%s.", task.m_clientmsgid.c_str());
        PostSendResult(task, false, error_code_upload_fail, "not a decodable image", "", "", 0, 0, 0);
        return;
    }

    //4. 扩展名（name 留空，图片消息不靠文件名展示）
    std::string ext = extractExtLower(task.m_localpath);

    //5. 申请上传（异步信令，阻塞等应答）
    uint32_t seq = CIUSocket::GetInstance().RequestUpload(file_scene_image_msg, md5, size, "", ext);
    RequestUploadResult ru;
    if (!WaitUploadSignal(seq, ru, kUploadSignalTimeoutSec))
    {
        LOG_ERROR("ImageTransfer: request-upload timeout, seq=%u, clientmsgid=%s.",
                  seq, task.m_clientmsgid.c_str());
        PostSendResult(task, false, error_code_upload_fail, "request upload timeout", "", "", 0, 0, 0);
        return;
    }
    if (ru.m_code != error_code_ok)
    {
        LOG_ERROR("ImageTransfer: request-upload rejected, code=%d, clientmsgid=%s.",
                  ru.m_code, task.m_clientmsgid.c_str());
        PostSendResult(task, false, ru.m_code, ru.m_msg, "", "", 0, 0, 0);
        return;
    }

    //6. 非秒传则跑字节流上传
    if (!ru.m_exists)
    {
        if (CIUSocket::GetInstance().IsImgServerClosed())
        {
            if (!CIUSocket::GetInstance().ConnectToImgServer(5))
            {
                LOG_ERROR("ImageTransfer: connect imgserver failed, clientmsgid=%s.", task.m_clientmsgid.c_str());
                PostSendResult(task, false, error_code_upload_fail, "connect imgserver failed", "", "", 0, 0, 0);
                return;
            }
        }

        ImgSocketUploadTransport transport(ru.m_uploadtoken, this);
        PostingProgressSink sink(CIUSocket::GetInstance().GetUIProxyWnd(), task.m_clientmsgid);
        UploadDriveResult dr = ImageUploadDriver::run(bytes.data(), size, ru.m_chunksize, transport, &sink);

        //一张图传完即断开 img 连接，避免会话状态残留（下次发图重连）
        CIUSocket::GetInstance().CloseImgServerConnection();

        if (!dr.m_ok)
        {
            LOG_ERROR("ImageTransfer: upload bytes failed, errcode=%d, clientmsgid=%s.",
                      dr.m_errcode, task.m_clientmsgid.c_str());
            PostSendResult(task, false, dr.m_errcode, dr.m_errmsg, "", "", 0, 0, 0);
            return;
        }
    }
    else
    {
        LOG_INFO("ImageTransfer: instant-transfer hit, fileid=%s, clientmsgid=%s.",
                 ru.m_fileid.c_str(), task.m_clientmsgid.c_str());
    }

    //7. 发图片消息（发送回执沿用 FMG_MSG_V2_SEND_CHATMSG_ACK）
    CIUSocket::GetInstance().SendChatImageMessage(task.m_ctype, task.m_cid, task.m_clientmsgid,
                                                  ru.m_fileid, md5, width, height, size);

    //8. 成功回投
    LOG_INFO("ImageTransfer: image sent, fileid=%s, %dx%d, size=%lld, clientmsgid=%s.",
             ru.m_fileid.c_str(), width, height, (long long)size, task.m_clientmsgid.c_str());
    PostSendResult(task, true, 0, "", ru.m_fileid, md5, width, height, size);
}

void ImageTransfer::ProcessAvatarUpload(const Task& task)
{
    //与 ProcessSend 的上传部分完全一致，区别只有两点：scene=file_scene_avatar，且
    //成功后<b>不发聊天消息</b>，而是把图落地到头像缓存、回投 fileid/md5/本地路径。
    if (task.m_selfAccount.empty())
    {
        PostAvatarUploadResult(false, error_code_upload_fail, "not logged in", "", "", L"");
        return;
    }

    //1. 读文件
    std::string bytes;
    if (!readWholeFile(task.m_localpath, bytes))
    {
        LOG_ERROR("ImageTransfer(avatar): read file failed.");
        PostAvatarUploadResult(false, error_code_upload_fail, "read file failed", "", "", L"");
        return;
    }
    int64_t size = static_cast<int64_t>(bytes.size());
    if (size <= 0 || size > kMaxImageBytes)
    {
        LOG_ERROR("ImageTransfer(avatar): bad image size %lld.", (long long)size);
        PostAvatarUploadResult(false, error_code_file_too_large, "image too large or empty", "", "", L"");
        return;
    }

    //2. MD5（小写 hex）
    std::string md5 = computeMd5Lower(bytes);

    //3. 宽高（兼作"是否可解码图片"的校验）
    int width = 0;
    int height = 0;
    if (!readImageSize(task.m_localpath, width, height))
    {
        LOG_ERROR("ImageTransfer(avatar): decode image size failed.");
        PostAvatarUploadResult(false, error_code_upload_fail, "not a decodable image", "", "", L"");
        return;
    }

    //4. 扩展名
    std::string ext = extractExtLower(task.m_localpath);

    //5. 申请上传（scene=头像；异步信令，阻塞等应答）
    uint32_t seq = CIUSocket::GetInstance().RequestUpload(file_scene_avatar, md5, size, "", ext);
    RequestUploadResult ru;
    if (!WaitUploadSignal(seq, ru, kUploadSignalTimeoutSec))
    {
        LOG_ERROR("ImageTransfer(avatar): request-upload timeout, seq=%u.", seq);
        PostAvatarUploadResult(false, error_code_upload_fail, "request upload timeout", "", "", L"");
        return;
    }
    if (ru.m_code != error_code_ok)
    {
        LOG_ERROR("ImageTransfer(avatar): request-upload rejected, code=%d.", ru.m_code);
        PostAvatarUploadResult(false, ru.m_code, ru.m_msg, "", "", L"");
        return;
    }

    //6. 非秒传则跑字节流上传（头像上传无进度回投需求，sink 传 NULL）
    if (!ru.m_exists)
    {
        if (CIUSocket::GetInstance().IsImgServerClosed())
        {
            if (!CIUSocket::GetInstance().ConnectToImgServer(5))
            {
                LOG_ERROR("ImageTransfer(avatar): connect imgserver failed.");
                PostAvatarUploadResult(false, error_code_upload_fail, "connect imgserver failed", "", "", L"");
                return;
            }
        }

        ImgSocketUploadTransport transport(ru.m_uploadtoken, this);
        UploadDriveResult dr = ImageUploadDriver::run(bytes.data(), size, ru.m_chunksize, transport, NULL);

        //一张图传完即断开 img 连接，避免会话状态残留
        CIUSocket::GetInstance().CloseImgServerConnection();

        if (!dr.m_ok)
        {
            LOG_ERROR("ImageTransfer(avatar): upload bytes failed, errcode=%d.", dr.m_errcode);
            PostAvatarUploadResult(false, dr.m_errcode, dr.m_errmsg, "", "", L"");
            return;
        }
    }
    else
    {
        LOG_INFO("ImageTransfer(avatar): instant-transfer hit, fileid=%s.", ru.m_fileid.c_str());
    }

    //7. 落地到头像缓存 Users\<userId>\Avatar\<fileid>，供本端即时渲染、后续免下载。
    std::wstring cachePath = buildAvatarCachePath(task.m_selfAccount, ru.m_fileid);
    if (cachePath.empty() || !writeWholeFile(cachePath, bytes))
    {
        //缓存落地失败不算致命：fileid 已拿到，仍回投成功，但本地路径为空，
        //本端渲染时按需再下载（第一期下载渲染未做则回退字形）。
        LOG_WARNING("ImageTransfer(avatar): cache write failed, fileid=%s.", ru.m_fileid.c_str());
        cachePath.clear();
    }

    //8. 成功回投（不发任何聊天消息）
    LOG_INFO("ImageTransfer(avatar): uploaded, fileid=%s, %dx%d, size=%lld.",
             ru.m_fileid.c_str(), width, height, (long long)size);
    PostAvatarUploadResult(true, 0, "", ru.m_fileid, md5, cachePath);
}

void ImageTransfer::PostAvatarUploadResult(bool ok, int errcode, const std::string& errmsg,
                                           const std::string& fileid, const std::string& md5,
                                           const std::wstring& localPath)
{
    HWND hUI = CIUSocket::GetInstance().GetUIProxyWnd();
    AvatarUploadResult* r = new AvatarUploadResult();
    r->m_ok       = ok;
    r->m_errcode  = errcode;
    r->m_errmsg   = errmsg;
    r->m_fileid   = fileid;
    r->m_md5      = md5;
    r->m_localpath = localPath;

    if (hUI != NULL)
        ::PostMessage(hUI, FMG_MSG_V2_AVATAR_UPLOAD_RESULT, 0, (LPARAM)r);
    else
        delete r;   //UI 未就绪，丢弃以免泄漏
}

void ImageTransfer::ProcessAvatarDownload(const Task& task)
{
    //1. 申请下载（异步信令，阻塞等应答）
    uint32_t seq = CIUSocket::GetInstance().RequestDownload(task.m_fileid);
    RequestDownloadResult rd;
    if (!WaitDownloadSignal(seq, rd, kUploadSignalTimeoutSec))
    {
        LOG_ERROR("ImageTransfer(avatar-dl): request-download timeout, fileid=%s.", task.m_fileid.c_str());
        PostAvatarDownloadResult(task.m_fileid, false, std::wstring());
        return;
    }
    if (rd.m_code != error_code_ok)
    {
        LOG_ERROR("ImageTransfer(avatar-dl): request-download rejected, code=%d, fileid=%s.",
                  rd.m_code, task.m_fileid.c_str());
        PostAvatarDownloadResult(task.m_fileid, false, std::wstring());
        return;
    }

    //2. 连 imgserver 跑字节流下载
    if (CIUSocket::GetInstance().IsImgServerClosed())
    {
        if (!CIUSocket::GetInstance().ConnectToImgServer(5))
        {
            LOG_ERROR("ImageTransfer(avatar-dl): connect imgserver failed, fileid=%s.", task.m_fileid.c_str());
            PostAvatarDownloadResult(task.m_fileid, false, std::wstring());
            return;
        }
    }

    ImgSocketDownloadTransport transport(rd.m_downloadtoken, this);
    std::string bytes;
    DownloadDriveResult dr = ImageDownloadDriver::run(transport, bytes, NULL);
    CIUSocket::GetInstance().CloseImgServerConnection();

    if (!dr.m_ok || bytes.empty())
    {
        LOG_ERROR("ImageTransfer(avatar-dl): download bytes failed, errcode=%d, fileid=%s.",
                  dr.m_errcode, task.m_fileid.c_str());
        PostAvatarDownloadResult(task.m_fileid, false, std::wstring());
        return;
    }

    //3. 原子落地缓存 Users\<userId>\Avatar\<fileid>（先写临时再 rename）。头像下载不强制
    //   校验 md5（联系人侧未必带期望 md5）；内容由 fileid 唯一对应。
    std::wstring cachePath = buildAvatarCachePath(task.m_selfAccount, task.m_fileid);
    if (cachePath.empty())
    {
        PostAvatarDownloadResult(task.m_fileid, false, std::wstring());
        return;
    }
    std::wstring tmpPath = cachePath + L".downloading";
    if (!writeWholeFile(tmpPath, bytes))
    {
        LOG_ERROR("ImageTransfer(avatar-dl): write temp cache failed, fileid=%s.", task.m_fileid.c_str());
        PostAvatarDownloadResult(task.m_fileid, false, std::wstring());
        return;
    }
    if (!::MoveFileExW(tmpPath.c_str(), cachePath.c_str(),
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        ::DeleteFileW(tmpPath.c_str());
        PostAvatarDownloadResult(task.m_fileid, false, std::wstring());
        return;
    }

    LOG_INFO("ImageTransfer(avatar-dl): avatar downloaded, fileid=%s, size=%u.",
             task.m_fileid.c_str(), static_cast<unsigned>(bytes.size()));
    PostAvatarDownloadResult(task.m_fileid, true, cachePath);
}

void ImageTransfer::PostAvatarDownloadResult(const std::string& fileid, bool ok, const std::wstring& localPath)
{
    //本条头像下载结束：从在途集合移除，放开后续对该 fileid 的下载。
    {
        std::lock_guard<std::mutex> guard(m_queueMtx);
        m_inflightAvatarDownloads.erase(fileid);
    }

    HWND hUI = CIUSocket::GetInstance().GetUIProxyWnd();
    AvatarDownloadResult* r = new AvatarDownloadResult();
    r->m_fileid   = fileid;
    r->m_ok       = ok;
    r->m_localpath = localPath;

    if (hUI != NULL)
        ::PostMessage(hUI, FMG_MSG_V2_AVATAR_DOWNLOAD_RESULT, 0, (LPARAM)r);
    else
        delete r;
}

void ImageTransfer::PostSendResult(const Task& task, bool ok, int errcode, const std::string& errmsg,
                                   const std::string& fileid, const std::string& md5,
                                   int width, int height, int64_t size)
{
    HWND hUI = CIUSocket::GetInstance().GetUIProxyWnd();
    ImageSendResult* r = new ImageSendResult();
    r->m_clientmsgid = task.m_clientmsgid;
    r->m_ctype       = task.m_ctype;
    r->m_cid         = task.m_cid;
    r->m_ok          = ok;
    r->m_errcode     = errcode;
    r->m_errmsg      = errmsg;
    r->m_fileid      = fileid;
    r->m_md5         = md5;
    r->m_width       = width;
    r->m_height      = height;
    r->m_size        = size;

    if (hUI != NULL)
        ::PostMessage(hUI, FMG_MSG_V2_IMAGE_SEND_RESULT, 0, (LPARAM)r);
    else
        delete r;   //UI 未就绪，丢弃以免泄漏
}

bool ImageTransfer::WaitUploadSignal(uint32_t seq, RequestUploadResult& out, int timeoutSec)
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

void ImageTransfer::OnRequestUploadResponse(uint32_t seq, const RequestUploadResult& r)
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

void ImageTransfer::ProcessDownload(const Task& task)
{
    //1. 申请下载（异步信令，阻塞等应答）
    uint32_t seq = CIUSocket::GetInstance().RequestDownload(task.m_fileid);
    RequestDownloadResult rd;
    if (!WaitDownloadSignal(seq, rd, kUploadSignalTimeoutSec))
    {
        LOG_ERROR("ImageTransfer: request-download timeout, seq=%u, msgid=%lld.",
                  seq, (long long)task.m_msgid);
        PostDownloadResult(task.m_msgid, false, error_code_server_internal, std::wstring());
        return;
    }
    if (rd.m_code != error_code_ok)
    {
        LOG_ERROR("ImageTransfer: request-download rejected, code=%d, msgid=%lld.",
                  rd.m_code, (long long)task.m_msgid);
        PostDownloadResult(task.m_msgid, false, rd.m_code, std::wstring());
        return;
    }

    //2. 连 imgserver 跑字节流下载
    if (CIUSocket::GetInstance().IsImgServerClosed())
    {
        if (!CIUSocket::GetInstance().ConnectToImgServer(5))
        {
            LOG_ERROR("ImageTransfer: connect imgserver failed (download), msgid=%lld.",
                      (long long)task.m_msgid);
            PostDownloadResult(task.m_msgid, false, error_code_server_internal, std::wstring());
            return;
        }
    }

    ImgSocketDownloadTransport transport(rd.m_downloadtoken, this);
    std::string bytes;
    DownloadDriveResult dr = ImageDownloadDriver::run(transport, bytes, NULL);

    //一张图下完即断开 img 连接，避免会话状态残留（下次重连）
    CIUSocket::GetInstance().CloseImgServerConnection();

    if (!dr.m_ok)
    {
        LOG_ERROR("ImageTransfer: download bytes failed, errcode=%d, msgid=%lld.",
                  dr.m_errcode, (long long)task.m_msgid);
        PostDownloadResult(task.m_msgid, false, dr.m_errcode, std::wstring());
        return;
    }

    //3. 校验 MD5：以下载内容算出的小写 hex 与期望（消息 content / 应答）比对
    std::string actual = computeMd5Lower(bytes);
    std::string expect = !task.m_expectMd5.empty() ? task.m_expectMd5 : rd.m_md5;
    for (size_t i = 0; i < expect.size(); ++i)
    {
        if (expect[i] >= 'A' && expect[i] <= 'F')
            expect[i] = static_cast<char>(expect[i] - 'A' + 'a');
    }
    if (!expect.empty() && actual != expect)
    {
        LOG_ERROR("ImageTransfer: download md5 mismatch, expect=%s, actual=%s, msgid=%lld.",
                  expect.c_str(), actual.c_str(), (long long)task.m_msgid);
        PostDownloadResult(task.m_msgid, false, error_code_md5_mismatch, std::wstring());
        return;
    }

    //4. 原子落地缓存（按登录用户分隔目录、按内容 MD5 命名，天然去重）。
    //   先写临时文件、整文件写完再 rename 成最终 <md5> 名——保证只要 <md5>
    //   存在即为完整文件（崩在写中途只会留下临时文件，绝不留半截 <md5>），
    //   读取侧因而可只判存在即认完整。
    std::wstring cachePath = buildImageCachePath(task.m_selfAccount, actual);
    std::wstring tmpPath   = cachePath + L".downloading";
    if (!writeWholeFile(tmpPath, bytes))
    {
        LOG_ERROR("ImageTransfer: write temp cache file failed, msgid=%lld.", (long long)task.m_msgid);
        PostDownloadResult(task.m_msgid, false, error_code_server_internal, std::wstring());
        return;
    }
    //MOVEFILE_REPLACE_EXISTING：并发/重复下载时覆盖既有同名缓存（同 md5 内容一致）
    if (!::MoveFileExW(tmpPath.c_str(), cachePath.c_str(),
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        LOG_ERROR("ImageTransfer: finalize cache (rename) failed, msgid=%lld, err=%lu.",
                  (long long)task.m_msgid, ::GetLastError());
        ::DeleteFileW(tmpPath.c_str());   //改名失败，清掉临时文件避免残留
        PostDownloadResult(task.m_msgid, false, error_code_server_internal, std::wstring());
        return;
    }

    LOG_INFO("ImageTransfer: image downloaded, msgid=%lld, size=%u bytes.",
             (long long)task.m_msgid, static_cast<unsigned>(bytes.size()));
    PostDownloadResult(task.m_msgid, true, 0, cachePath);
}

void ImageTransfer::PostDownloadResult(int64_t msgid, bool ok, int errcode, const std::wstring& localPath)
{
    //本条下载结束（成功 / 失败）：从在途集合移除，放开后续对该 msgid 的下载。
    {
        std::lock_guard<std::mutex> guard(m_queueMtx);
        m_inflightDownloads.erase(msgid);
    }

    HWND hUI = CIUSocket::GetInstance().GetUIProxyWnd();
    ImageDownloadResult* r = new ImageDownloadResult();
    r->m_msgid    = msgid;
    r->m_ok       = ok;
    r->m_errcode  = errcode;
    r->m_localpath = localPath;

    if (hUI != NULL)
        ::PostMessage(hUI, FMG_MSG_V2_IMAGE_DOWNLOAD_RESULT, 0, (LPARAM)r);
    else
        delete r;
}

bool ImageTransfer::WaitDownloadSignal(uint32_t seq, RequestDownloadResult& out, int timeoutSec)
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

void ImageTransfer::OnRequestDownloadResponse(uint32_t seq, const RequestDownloadResult& r)
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

bool ImageTransfer::RecvOneImgFrame(std::string& body, int timeoutSec)
{
    //先收 24 字节帧头
    char hdr[FRAME_HEADER_SIZE] = { 0 };
    if (!CIUSocket::GetInstance().RecvOnImgPort(hdr, FRAME_HEADER_SIZE, timeoutSec))
        return false;

    //bodylen 为大端 uint32，位于帧头偏移 12（magic2+ver1+flags1+cmd2+reserved2+seq4）
    uint32_t bodylen = (static_cast<uint32_t>(static_cast<unsigned char>(hdr[12])) << 24)
                     | (static_cast<uint32_t>(static_cast<unsigned char>(hdr[13])) << 16)
                     | (static_cast<uint32_t>(static_cast<unsigned char>(hdr[14])) << 8)
                     |  static_cast<uint32_t>(static_cast<unsigned char>(hdr[15]));
    if (bodylen > MAX_BODY_FILESERVER)
        return false;

    //拼成完整帧再交给 decodeFrame 做魔数/版本/长度/CRC 校验
    std::string buf;
    buf.resize(FRAME_HEADER_SIZE + bodylen);
    memcpy(&buf[0], hdr, FRAME_HEADER_SIZE);
    if (bodylen > 0)
    {
        if (!CIUSocket::GetInstance().RecvOnImgPort(&buf[FRAME_HEADER_SIZE],
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

    //还原包体：握手完成后服务端发来的包体是加密的，须在此解出明文再交给业务层
    std::string err;
    if (!net::decodeFramePayload(header.cmd, header.seq, header.flags, header.originlen,
                                 decoded, CIUSocket::GetInstance().GetImgCipher(),
                                 body, err))
    {
        LOG_ERROR("RecvOneImgFrame: restore body failed: %s, cmd=%u, bodylen=%u.",
                  err.c_str(), header.cmd, (uint32_t)decoded.size());
        //解密失败意味着这个方向的流已不可恢复，连接不能再用。关掉它，
        //下一张图会重新建连并重新握手。
        CIUSocket::GetInstance().CloseImgServerConnection();
        return false;
    }

    return true;
}

} // namespace net
