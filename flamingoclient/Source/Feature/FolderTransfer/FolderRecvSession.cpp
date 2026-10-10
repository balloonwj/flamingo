/**
 *  单聊「接收文件夹」接收端状态机的实现，FolderRecvSession.cpp
 *
 *  设计、时序、线程模型与流控口径见 FolderRecvSession.h 的文件头注释，本文件只补充实现层面
 *  的三点取舍：
 *    1. 文件读写一律走 Win32 的 CreateFileW / WriteFile / SetEndOfFile，不用 std::ofstream。
 *       原因有二：续传时要把 .part 文件<u>截断</u>到权威位点（上一次可能多写了一段没来得及计
 *       入记录的字节，留着它会让最终文件比清单大），标准库没有截断接口；以及全部路径都要先
 *       过 MakeLongPath 加 "\\?\" 前缀，Win32 API 才肯受理超过 260 字符的路径。
 *    2. 增量 MD5 复用 MD5Core.h 的 MD5Init / MD5Update / MD5Final，与文件下载路径
 *       （net/FileStreamTransfer.cpp）同一份实现，不另找一份。
 *    3. 中转通道的接收线程只把应用帧拷进本通道的队列，磁盘 I/O 一律在通道自己的工作线程上
 *       做。队列满时入队方阻塞，TCP 接收窗口随之收紧，发送端被自然压住 —— 这就是接收端的流控。
 *
 *  balloonwj@qq.com   2026-08-14
 */

#include "FolderRecvSession.h"

#include <windows.h>
#include <stdio.h>                 // sprintf_s（MD5 十六进制串）
#include <chrono>                  // 队列等待的超时
#include <set>                     // 建目录时的去重

#include "../../net/Protocol.h"       // error_code_ok / error_code_folder_*
#include "../../Common/Log/IULog.h"              // LOG_INFO / LOG_WARNING / LOG_ERROR

namespace foldertransfer {

//==================== 实现层常量 ====================

//「本通道当前没有正在接收的文件」的文件序号取值。
static const int64_t kFolderRecvNoFileIndex = -1;

//把一个字节格式化成两位十六进制所需的缓冲字节数：2 个字符加一个结束符。
static const int kFolderRecvHexPairBufBytes = 3;

//一次 WriteFile 允许写入的最大字节数。分片本身只有 512 KiB，这里取同一量级的上限只是为了
//让「写了一部分」的循环有个明确边界，正常一次调用即可写完。
static const DWORD kFolderRecvMaxWriteBytes = 1u << 20;

//==================== 文件系统辅助（本文件私有） ====================

//路径存在性探测的生产实现：走 Win32 的 GetFileAttributesW，文件与目录都算存在。
class FolderRecvWin32PathProbe : public folderrecv::IFolderPathProbe
{
public:
    virtual bool PathExists(const std::wstring& path)
    {
        const DWORD attrs = ::GetFileAttributesW(MakeLongPath(path).c_str());
        return attrs != INVALID_FILE_ATTRIBUTES;
    }
};

//把宽字符路径转成可以直接进日志的 UTF-8 文本。日志里的路径可能含中文，这属于「变量值」，
//按仓库约定照常打印；开发者自己写的那部分文字仍然一律英文。
static std::string PathForLog(const std::wstring& path)
{
    std::string utf8;
    if (!Utf16ToUtf8(path, utf8))
        return std::string("<unprintable path>");

    return utf8;
}

//取一个已存在文件的字节数。
//@param path    完整路径（宽字符，函数内部自行加长路径前缀）
//@param outSize [出参] 文件字节数
//@return 文件存在且取到大小返回 true；不存在或它是个目录返回 false
static bool GetExistingFileSize(const std::wstring& path, int64_t& outSize)
{
    outSize = 0;

    WIN32_FILE_ATTRIBUTE_DATA data;
    memset(&data, 0, sizeof(data));
    if (!::GetFileAttributesExW(MakeLongPath(path).c_str(), GetFileExInfoStandard, &data))
        return false;

    if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
        return false;

    LARGE_INTEGER size;
    size.HighPart = static_cast<LONG>(data.nFileSizeHigh);
    size.LowPart  = data.nFileSizeLow;
    outSize = size.QuadPart;
    return true;
}

//建一个目录；已经存在（且确实是目录）也算成功。
//@param path 目录完整路径（宽字符，函数内部自行加长路径前缀）
//@return 目录已就位返回 true
static bool CreateDirectoryIfMissing(const std::wstring& path)
{
    const std::wstring longPath = MakeLongPath(path);
    if (::CreateDirectoryW(longPath.c_str(), NULL))
        return true;

    const DWORD err = ::GetLastError();
    if (err != ERROR_ALREADY_EXISTS)
        return false;

    //已存在：还得确认它是目录而不是同名文件，否则后续往里写文件必然失败
    const DWORD attrs = ::GetFileAttributesW(longPath.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES)
        return false;

    return (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

//在根目录下逐级建出一条相对路径对应的目录（每一级都建，父目录缺失时不会失败）。
//@param rootPath 落地根目录的完整路径（宽字符）
//@param relPath  相对路径（UTF-8，正斜杠分隔）；空串直接返回成功
//@param created  [出入参] 已建过的相对路径集合，用于跳过重复建目录
//@return 全部层级都已就位返回 true
static bool EnsureRelativeDirectory(const std::wstring& rootPath, const std::string& relPath,
                                    std::set<std::string>& created)
{
    if (relPath.empty())
        return true;
    if (created.find(relPath) != created.end())
        return true;

    //逐段推进：每遇到一个分隔符就把到此为止的一段建出来，父目录因而必定先于子目录建成
    std::string prefix;
    size_t begin = 0;
    while (begin <= relPath.size())
    {
        size_t pos = relPath.find(kFolderPathSeparator, begin);
        if (pos == std::string::npos)
            pos = relPath.size();

        if (pos > begin)
        {
            if (!prefix.empty())
                prefix += kFolderPathSeparator;
            prefix.append(relPath, begin, pos - begin);

            if (created.find(prefix) == created.end())
            {
                std::wstring local;
                if (folderrecv::ResolveLocalRelativePath(prefix, local) != kFolderPathOk)
                    return false;

                if (!CreateDirectoryIfMissing(folderrecv::JoinFolderPath(rootPath, local)))
                    return false;

                created.insert(prefix);
            }
        }
        begin = pos + 1;
    }
    return true;
}

//把一个增量 MD5 上下文定格成 32 位小写十六进制文本。上下文按值取副本，故调用之后原上下文
//仍可继续 MD5Update（MD5Final 会把传给它的上下文用废）。
static std::string Md5HexFromContext(const MD5_CTX& ctx)
{
    MD5_CTX copy = ctx;
    unsigned char digest[kFolderRecvMd5DigestBytes] = { 0 };
    MD5Final(digest, &copy);

    char hex[kFolderRecvMd5HexChars + 1] = { 0 };
    for (int i = 0; i < kFolderRecvMd5DigestBytes; ++i)
        sprintf_s(hex + i * 2, kFolderRecvHexPairBufBytes, "%02x", digest[i]);

    return std::string(hex, kFolderRecvMd5HexChars);
}

//把十六进制串统一成小写，供 MD5 比对（发送端可能给大写）。
static std::string ToLowerHex(const std::string& text)
{
    std::string out = text;
    for (size_t i = 0; i < out.size(); ++i)
    {
        if (out[i] >= 'A' && out[i] <= 'F')
            out[i] = static_cast<char>(out[i] - 'A' + 'a');
    }
    return out;
}

//把整段字节写进文件句柄，自行处理「只写出去一部分」。
//@return 全部写出返回 true
static bool WriteAllToHandle(HANDLE handle, const unsigned char* data, size_t len)
{
    size_t done = 0;
    while (done < len)
    {
        DWORD want = kFolderRecvMaxWriteBytes;
        if (static_cast<size_t>(want) > len - done)
            want = static_cast<DWORD>(len - done);

        DWORD written = 0;
        if (!::WriteFile(handle, data + done, want, &written, NULL))
            return false;
        if (written == 0)
            return false;

        done += written;
    }
    return true;
}

//==================== FolderRecvLane ====================

FolderRecvLane::FolderRecvLane(int laneIndex, IFolderLaneTransport* transport)
    : m_laneIndex(laneIndex)
    , m_client(transport)
    , m_closed(false)
    , m_peerClosed(false)
{
}

FolderRecvLane::~FolderRecvLane()
{
    Shutdown();
    m_client->Close();
}

IFolderLaneTransport& FolderRecvLane::GetClient()
{
    return *m_client;
}

int FolderRecvLane::GetLaneIndex() const
{
    return m_laneIndex;
}

void FolderRecvLane::OnFolderRelayAppFrame(const unsigned char* data, size_t len)
{
    std::unique_lock<std::mutex> guard(m_mtx);

    //队列满时在此阻塞：不读走 socket 里的字节，TCP 接收窗口随之收紧，发送端被压住。
    //这是接收端唯一的流控手段，故绝不能改成「满了就丢帧」。
    while (!m_closed && m_queue.size() >= kFolderRecvLaneQueueDepth)
        m_notFull.wait(guard);

    if (m_closed)
        return;

    if (data != NULL && len > 0)
        m_queue.push_back(std::string(reinterpret_cast<const char*>(data), len));
    else
        m_queue.push_back(std::string());

    m_notEmpty.notify_one();
}

void FolderRecvLane::OnFolderRelayClosed()
{
    {
        std::lock_guard<std::mutex> guard(m_mtx);
        m_peerClosed = true;
    }
    m_notEmpty.notify_all();
    m_notFull.notify_all();

    LOG_ERROR("folder recv: relay lane %d disconnected.", m_laneIndex);
}

bool FolderRecvLane::PopFrame(std::string& out, int waitMs)
{
    std::unique_lock<std::mutex> guard(m_mtx);
    if (m_queue.empty() && !m_closed)
        m_notEmpty.wait_for(guard, std::chrono::milliseconds(waitMs));

    if (m_queue.empty())
        return false;

    out.swap(m_queue.front());
    m_queue.pop_front();
    m_notFull.notify_one();
    return true;
}

void FolderRecvLane::Shutdown()
{
    {
        std::lock_guard<std::mutex> guard(m_mtx);
        m_closed = true;
        m_queue.clear();
    }
    m_notEmpty.notify_all();
    m_notFull.notify_all();
}

bool FolderRecvLane::IsPeerClosed() const
{
    std::lock_guard<std::mutex> guard(m_mtx);
    return m_peerClosed;
}

//==================== FolderRecvSession：生命周期 ====================

FolderRecvSession::FolderRecvSession()
    : m_handler(NULL)
    , m_totalBytes(0)
    , m_started(false)
    , m_stopRequested(false)
    , m_canceled(false)
    , m_failed(false)
    , m_allDone(false)
    , m_completeReceived(false)
    , m_manifestReady(false)
    , m_peerAborted(false)
    , m_finished(false)
    , m_errcode(0)
    , m_lastReportTick(0)
{
}

FolderRecvSession::~FolderRecvSession()
{
    Stop();
}

bool FolderRecvSession::Start(const FolderRecvSessionParams& params,
                              const std::shared_ptr<FolderReceiveState>& state,
                              IFolderRecvSessionHandler* handler)
{
    if (!state || handler == NULL)
    {
        LOG_ERROR("folder recv: start rejected, state or handler is null.");
        return false;
    }
    if (params.m_relayIp.empty() || params.m_relayPort <= 0 || params.m_relayToken.empty())
    {
        LOG_ERROR("folder recv: start rejected, bad relay parameters (port %d).", params.m_relayPort);
        return false;
    }
    if (params.m_dataLanes < 1 || params.m_dataLanes > kFolderMaxDataLanes)
    {
        LOG_ERROR("folder recv: start rejected, data lane count %d out of range [1, %d].",
                  params.m_dataLanes, kFolderMaxDataLanes);
        return false;
    }
    //保存位置只有首次传输才用得上；重试时根目录取自接收状态，此时允许它为空
    if (params.m_saveParentDir.empty() && !state->HasRootPath())
    {
        LOG_ERROR("folder recv: start rejected, save location is empty and no root dir recorded.");
        return false;
    }

    bool expected = false;
    if (!m_started.compare_exchange_strong(expected, true))
    {
        LOG_ERROR("folder recv: start rejected, this session object has already been started.");
        return false;
    }

    m_params  = params;
    m_state   = state;
    m_handler = handler;

    //通道对象在这里就全部建好（只是还没连），此后 m_lanes 不再增删，各线程可以放心按序号访问
    const size_t laneCount = static_cast<size_t>(params.m_dataLanes) + 1;
    m_lanes.clear();
    for (size_t i = 0; i < laneCount; ++i)
    {
        //有通道工厂（P2P）时用它建，否则按中转参数建到 fileserver 的中转连接
        IFolderLaneTransport* transport = (params.m_transportFactory != NULL)
            ? params.m_transportFactory->CreateLane(static_cast<int>(i))
            : new FolderRelayLaneTransport(params.m_relayIp, params.m_relayPort, params.m_relayToken,
                                           params.m_serverPubKey);
        m_lanes.push_back(std::unique_ptr<FolderRecvLane>(new FolderRecvLane(static_cast<int>(i), transport)));
    }

    m_laneFiles.clear();
    m_laneFiles.resize(laneCount);

    LOG_INFO("folder recv: starting session %s, relay %s:%d, lanes %d (1 control + %d data).",
             m_params.m_sessionId.c_str(), m_params.m_relayIp.c_str(), m_params.m_relayPort,
             static_cast<int>(laneCount), m_params.m_dataLanes);

    m_sessionThread.reset(new std::thread(&FolderRecvSession::SessionProc, this));
    return true;
}

void FolderRecvSession::Cancel()
{
    if (!m_started.load())
        return;

    //只置标志：真正的中止（发 ft_abort、停通道）由工作线程在下一个分片边界上做，
    //使本方法可以安全地在 UI 线程调用而不会被 socket 发送阻塞住。
    m_canceled.store(true);
    LOG_INFO("folder recv: session %s cancel requested by user.", m_params.m_sessionId.c_str());
}

void FolderRecvSession::Stop()
{
    std::lock_guard<std::mutex> guard(m_stopMtx);

    if (!m_started.load())
        return;

    m_stopRequested.store(true);

    //会话还没走到终态就被停掉，按用户取消处理，使对端也能收到一条明确的原因
    if (!m_allDone.load())
        Fail(error_code_folder_canceled, "folder receive stopped by caller");

    if (m_sessionThread && m_sessionThread->joinable())
        m_sessionThread->join();

    m_sessionThread.reset();
}

bool FolderRecvSession::IsRunning() const
{
    return m_started.load() && !m_finished.load();
}

bool FolderRecvSession::ShouldStop() const
{
    return m_stopRequested.load() || m_failed.load() || m_allDone.load();
}

//==================== FolderRecvSession：会话线程 ====================

void FolderRecvSession::SessionProc()
{
    if (ConnectAllLanes())
    {
        //数据通道各起一条工作线程；控制通道由本线程亲自处理
        for (size_t i = 1; i < m_lanes.size(); ++i)
        {
            m_laneThreads.push_back(std::unique_ptr<std::thread>(
                new std::thread(&FolderRecvSession::LaneProc, this, m_lanes[i].get())));
        }

        //扩展点的前置交换从这里开始（文件共享的访问者在此发 st_job）
        int hookCode = 0;
        if (m_params.m_hooks != NULL)
            hookCode = m_params.m_hooks->OnLanesReady(*this);
        if (hookCode != 0)
            Fail(hookCode, "lanes ready hook failed");
        else
            ControlLoop();
    }

    //收尾顺序不能颠倒：先唤醒并关掉各通道的队列，卡在入队处的中转接收线程才会退出；
    //再回收数据通道工作线程；最后才关中转连接（Close 内部要 join 中转接收线程）。
    for (size_t i = 0; i < m_lanes.size(); ++i)
        m_lanes[i]->Shutdown();

    for (size_t i = 0; i < m_laneThreads.size(); ++i)
    {
        if (m_laneThreads[i] && m_laneThreads[i]->joinable())
            m_laneThreads[i]->join();
    }
    m_laneThreads.clear();

    for (size_t i = 0; i < m_lanes.size(); ++i)
        m_lanes[i]->GetClient().Close();

    //把各通道手上还没写回的进度与 MD5 上下文存回接收状态，供期内重试续传
    for (size_t i = 0; i < m_lanes.size(); ++i)
        CloseCurrentFile(m_lanes[i].get(), true);

    //回报最终结果：完成与失败二者只触发其一，且只触发一次
    if (m_allDone.load() && !m_failed.load())
    {
        const int64_t doneFiles = m_state->GetCompletedFileCount();
        const int64_t doneBytes = m_state->GetReceivedBytes();
        LOG_INFO("folder recv: session %s completed, %lld files, %lld bytes, root %s.",
                 m_params.m_sessionId.c_str(), doneFiles, doneBytes, PathForLog(m_rootPath).c_str());
        m_handler->OnFolderRecvCompleted(m_rootPath, doneFiles, doneBytes);
    }
    else
    {
        int         errcode = 0;
        std::string errmsg;
        {
            std::lock_guard<std::mutex> guard(m_failMtx);
            errcode = m_errcode;
            errmsg  = m_errmsg;
        }
        //既没完成也没记下原因：只可能是 Start 之后立刻被 Stop，按用户取消上报
        if (errcode == 0)
        {
            errcode = error_code_folder_canceled;
            errmsg  = "folder receive ended without a recorded reason";
        }
        LOG_ERROR("folder recv: session %s failed, code %d, msg %s.",
                  m_params.m_sessionId.c_str(), errcode, errmsg.c_str());
        m_handler->OnFolderRecvFailed(errcode, errmsg);
    }

    m_finished.store(true);
}

bool FolderRecvSession::ConnectAllLanes()
{
    for (size_t i = 0; i < m_lanes.size(); ++i)
    {
        if (ShouldStop())
            return false;

        const int lane = m_lanes[i]->GetLaneIndex();
        if (!m_lanes[i]->GetClient().Connect(lane, m_lanes[i].get(), kFolderRelayConnectTimeoutSec))
        {
            LOG_ERROR("folder recv: session %s connect relay lane %d to %s:%d failed.",
                      m_params.m_sessionId.c_str(), lane,
                      m_params.m_relayIp.c_str(), m_params.m_relayPort);
            Fail(error_code_folder_relay_broken, "connect relay lane failed");
            return false;
        }
        LOG_INFO("folder recv: session %s relay lane %d connected.", m_params.m_sessionId.c_str(), lane);
    }
    return true;
}

void FolderRecvSession::ControlLoop()
{
    FolderRecvLane* lane = m_lanes[0].get();

    while (!ShouldStop())
    {
        //取消在此生效：控制通道每处理完一帧就检查一次
        if (m_canceled.load() && !m_failed.load())
        {
            Fail(error_code_folder_canceled, "folder receive canceled by user");
            break;
        }

        std::string frame;
        if (!lane->PopFrame(frame, kFolderRecvQueueWaitMs))
        {
            if (lane->IsPeerClosed())
            {
                Fail(error_code_folder_relay_broken, "control lane disconnected");
                break;
            }
            //控制通道空闲：给扩展点一个定时重发的机会
            if (m_params.m_hooks != NULL)
            {
                const int code = m_params.m_hooks->OnControlIdle(*this);
                if (code != 0)
                {
                    Fail(code, "control idle hook failed");
                    break;
                }
            }
            continue;
        }

        if (!HandleControlFrame(frame))
            break;
    }
}

void FolderRecvSession::LaneProc(FolderRecvLane* lane)
{
    while (!ShouldStop())
    {
        //取消在分片边界生效：本通道每处理完一帧（即一个分片）就检查一次
        if (m_canceled.load() && !m_failed.load())
        {
            Fail(error_code_folder_canceled, "folder receive canceled by user");
            break;
        }

        std::string frame;
        if (!lane->PopFrame(frame, kFolderRecvQueueWaitMs))
        {
            if (lane->IsPeerClosed())
            {
                //全部文件都已校验通过：发送端发完 ft_complete 就关闭了全部通道，本通道只是先看到了断开，
                //退出本通道的循环即可，整体结果由控制通道判定
                if (!folderrecv::IsDataLaneCloseFatal(m_manifestReady.load(), m_state->GetCompletedFileCount(),
                                                      static_cast<int64_t>(m_files.size())))
                {
                    LOG_INFO("folder recv: session %s data lane closed by peer after all files completed.",
                             m_params.m_sessionId.c_str());
                    break;
                }
                Fail(error_code_folder_relay_broken, "data lane disconnected");
                break;
            }
            continue;
        }

        if (!HandleDataFrame(lane, frame))
            break;
    }
}

//==================== FolderRecvSession：控制通道 ====================

bool FolderRecvSession::HandleControlFrame(const std::string& frame)
{
    const unsigned char* body = reinterpret_cast<const unsigned char*>(frame.data());

    //首字节不属于 ft_* 的帧是扩展点的自定义帧（文件共享的 st_job / st_job_ack），交给扩展点
    if (m_params.m_hooks != NULL && !frame.empty() && !IsFolderFrameTypeByte(body[0]))
    {
        const int code = m_params.m_hooks->OnControlFrame(*this, frame);
        if (code != 0)
        {
            Fail(code, "control frame hook failed");
            return false;
        }
        return true;
    }

    FolderRelayInboundFrame inbound;
    if (!relayframing::DecodeFrame(body, frame.size(), inbound))
    {
        LOG_ERROR("folder recv: session %s decode control frame failed, %u bytes.",
                  m_params.m_sessionId.c_str(), static_cast<unsigned int>(frame.size()));
        Fail(error_code_folder_relay_broken, "decode control frame failed");
        return false;
    }

    switch (inbound.m_type)
    {
    //清单（可能分多段）：逐段喂进重组器，收齐之后才开始建目录
    case ft_manifest:
        {
            const FolderRelayManifestAssembler::FeedResult fed = m_assembler.Feed(inbound.m_manifest);
            if (fed == FolderRelayManifestAssembler::folder_manifest_error)
            {
                LOG_ERROR("folder recv: session %s manifest segment %lld/%lld rejected by assembler.",
                          m_params.m_sessionId.c_str(), inbound.m_manifest.m_segindex,
                          inbound.m_manifest.m_segcount);
                Fail(error_code_folder_relay_broken, "manifest segments are inconsistent");
                return false;
            }
            if (fed == FolderRelayManifestAssembler::folder_manifest_done)
                return OnManifestReady();
            //发送端在回执到达前重发的多余一份、或开头被 fileserver 丢掉的那一份：丢弃本段，继续等
            if (fed == FolderRelayManifestAssembler::folder_manifest_ignored)
            {
                LOG_INFO("folder recv: session %s manifest segment %lld/%lld ignored (resent copy or partial copy).",
                         m_params.m_sessionId.c_str(), inbound.m_manifest.m_segindex,
                         inbound.m_manifest.m_segcount);
            }

            return true;
        }

    //发送端报告全部文件已传完：本端还要确认每个文件都已校验通过才算整体完成
    case ft_complete:
        LOG_INFO("folder recv: session %s got complete frame, %lld files / %lld bytes claimed.",
                 m_params.m_sessionId.c_str(), inbound.m_complete.m_totalfiles,
                 inbound.m_complete.m_totalsize);
        m_completeReceived.store(true);
        MaybeFinish();
        return true;

    //对端出错主动中止：原样采用它给的错误码与描述，双方界面因而显示同一条原因
    case ft_abort:
        m_peerAborted.store(true);
        LOG_ERROR("folder recv: session %s aborted by peer, code %d, msg %s.",
                  m_params.m_sessionId.c_str(), inbound.m_abort.m_errcode,
                  inbound.m_abort.m_errmsg.c_str());
        Fail(inbound.m_abort.m_errcode != 0 ? inbound.m_abort.m_errcode : error_code_folder_relay_broken,
             inbound.m_abort.m_errmsg.empty() ? std::string("peer aborted the transfer")
                                              : inbound.m_abort.m_errmsg);
        return false;

    //其余帧型都不该出现在控制通道上：对端实现有误或通道串了，按失败处理
    default:
        LOG_ERROR("folder recv: session %s unexpected frame type %d on control lane.",
                  m_params.m_sessionId.c_str(), static_cast<int>(inbound.m_type));
        Fail(error_code_folder_relay_broken, "unexpected frame type on control lane");
        return false;
    }
}

bool FolderRecvSession::OnManifestReady()
{
    m_folderName = m_assembler.GetFolderName();
    m_dirs       = m_assembler.GetDirs();
    m_files      = m_assembler.GetFiles();
    m_totalBytes = m_assembler.GetTotalSize();

    LOG_INFO("folder recv: session %s manifest ready, %u dirs, %u files, %lld bytes, folder name %s.",
             m_params.m_sessionId.c_str(), static_cast<unsigned int>(m_dirs.size()),
             static_cast<unsigned int>(m_files.size()), m_totalBytes, m_folderName.c_str());

    //根文件夹名必须是单独一段：它要直接拼到用户选定的保存位置下面，带分隔符就等于越权指定落点。
    //有扩展点时允许为空（清单没有根文件夹，顶层条目直接落在扩展点给出的根目录下）
    if ((m_folderName.empty() && m_params.m_hooks == NULL)
        || (!m_folderName.empty() && CheckFolderRelativePath(m_folderName) != kFolderPathOk)
        || m_folderName.find(kFolderPathSeparator) != std::string::npos
        || m_folderName.find('\\') != std::string::npos)
    {
        LOG_ERROR("folder recv: session %s root folder name is unsafe: %s.",
                  m_params.m_sessionId.c_str(), m_folderName.c_str());
        Fail(error_code_folder_mkdir_fail, "unsafe root folder name in manifest");
        return false;
    }

    //清单是对端发来的，落地前必须逐条校验，不能假定发送端已经校验过（防目录穿越）
    for (size_t i = 0; i < m_dirs.size(); ++i)
    {
        std::wstring local;
        const FolderPathVerdict verdict = folderrecv::ResolveLocalRelativePath(m_dirs[i], local);
        if (verdict != kFolderPathOk)
        {
            LOG_ERROR("folder recv: session %s unsafe directory path in manifest: %s (%s).",
                      m_params.m_sessionId.c_str(), m_dirs[i].c_str(), FolderPathVerdictText(verdict));
            Fail(error_code_folder_mkdir_fail, "unsafe directory path in manifest");
            return false;
        }
    }
    for (size_t i = 0; i < m_files.size(); ++i)
    {
        std::wstring local;
        const FolderPathVerdict verdict = folderrecv::ResolveLocalRelativePath(m_files[i].m_relpath, local);
        if (verdict != kFolderPathOk)
        {
            LOG_ERROR("folder recv: session %s unsafe file path in manifest: %s (%s).",
                      m_params.m_sessionId.c_str(), m_files[i].m_relpath.c_str(),
                      FolderPathVerdictText(verdict));
            Fail(error_code_folder_write_fail, "unsafe file path in manifest");
            return false;
        }
        if (m_files[i].m_size < 0)
        {
            LOG_ERROR("folder recv: session %s negative file size in manifest at index %u.",
                      m_params.m_sessionId.c_str(), static_cast<unsigned int>(i));
            Fail(error_code_folder_verify_fail, "negative file size in manifest");
            return false;
        }
    }

    //与接收状态核对：清单一致则沿用上次的进度，不一致（对端换了内容）则整体重来
    if (m_state->SyncWithManifest(m_files))
        LOG_INFO("folder recv: session %s reuses recorded progress from the previous attempt.",
                 m_params.m_sessionId.c_str());
    else
        LOG_INFO("folder recv: session %s starts with a fresh progress record.",
                 m_params.m_sessionId.c_str());

    if (!PrepareRootDirectory())
        return false;
    if (!CreateAllDirectories())
        return false;
    if (!ReconcileResumeState())
        return false;

    //目录全部就绪之后才回执，并把每个文件的续传起点一并带回去
    FolderRelayManifestAck ack;
    ack.m_totalfiles = static_cast<int64_t>(m_files.size());
    m_state->BuildResumePoints(ack.m_points);

    std::string frame;
    if (!relayframing::BuildManifestAckFrame(ack, frame))
    {
        LOG_ERROR("folder recv: session %s build manifest ack failed, %u resume points.",
                  m_params.m_sessionId.c_str(), static_cast<unsigned int>(ack.m_points.size()));
        Fail(error_code_folder_relay_broken, "build manifest ack frame failed");
        return false;
    }
    if (!SendControlFrame(frame))
    {
        Fail(error_code_folder_relay_broken, "send manifest ack failed");
        return false;
    }

    m_manifestReady.store(true);
    LOG_INFO("folder recv: session %s directories are ready, manifest ack sent with %u resume points.",
             m_params.m_sessionId.c_str(), static_cast<unsigned int>(ack.m_points.size()));

    ReportProgress(std::string(), true);

    //清单里一个文件都没有（纯目录结构）时，发送端会紧接着发 ft_complete；这里先判一次，
    //避免「完成帧已经先到、但当时清单还没就绪」的情况漏掉终结判断
    MaybeFinish();
    return true;
}

bool FolderRecvSession::PrepareRootDirectory()
{
    //扩展点决定落点（文件共享：下载落到用户选的目录、上传落到共享内的目标目录，各自的同名规则由它处理）
    if (m_params.m_hooks != NULL)
    {
        std::wstring root;
        const int code = m_params.m_hooks->PrepareRoot(m_folderName, m_dirs, m_files, root);
        if (code != 0 || root.empty())
        {
            LOG_ERROR("folder recv: session %s prepare root by hooks failed, code %d.", m_params.m_sessionId.c_str(), code);
            Fail(code != 0 ? code : error_code_folder_mkdir_fail, "prepare root directory by hooks failed");
            return false;
        }
        m_rootPath = root;
        m_state->SetRootPath(m_rootPath);
        LOG_INFO("folder recv: session %s root dir given by hooks: %s.", m_params.m_sessionId.c_str(),
                 PathForLog(m_rootPath).c_str());
        return true;
    }

    //重试：复用上次那个根目录，不再改名新建（方案 8.3）
    if (m_state->HasRootPath())
    {
        m_rootPath = m_state->GetRootPath();
        if (!CreateDirectoryIfMissing(m_rootPath))
        {
            LOG_ERROR("folder recv: session %s reuse root dir %s failed, win32 error %lu.",
                      m_params.m_sessionId.c_str(), PathForLog(m_rootPath).c_str(), ::GetLastError());
            Fail(error_code_folder_mkdir_fail, "reuse existing root directory failed");
            return false;
        }
        LOG_INFO("folder recv: session %s reuses root dir %s.",
                 m_params.m_sessionId.c_str(), PathForLog(m_rootPath).c_str());
        return true;
    }

    std::wstring folderNameW;
    if (!Utf8ToUtf16(m_folderName, folderNameW) || folderNameW.empty())
    {
        LOG_ERROR("folder recv: session %s convert root folder name to utf-16 failed.",
                  m_params.m_sessionId.c_str());
        Fail(error_code_folder_mkdir_fail, "convert root folder name failed");
        return false;
    }

    //首次：重名自动改名，依次试原名、"xxx(2)"、"xxx(3)"……
    FolderRecvWin32PathProbe probe;
    if (!folderrecv::ChooseUniqueRootFolderPath(m_params.m_saveParentDir, folderNameW, probe, m_rootPath))
    {
        LOG_ERROR("folder recv: session %s cannot find a free root dir name under %s after %d attempts.",
                  m_params.m_sessionId.c_str(), PathForLog(m_params.m_saveParentDir).c_str(),
                  kFolderRecvMaxRenameAttempts);
        Fail(error_code_folder_mkdir_fail, "no free name for the root directory");
        return false;
    }

    if (!CreateDirectoryIfMissing(m_rootPath))
    {
        LOG_ERROR("folder recv: session %s create root dir %s failed, win32 error %lu.",
                  m_params.m_sessionId.c_str(), PathForLog(m_rootPath).c_str(), ::GetLastError());
        Fail(error_code_folder_mkdir_fail, "create root directory failed");
        return false;
    }

    m_state->SetRootPath(m_rootPath);
    LOG_INFO("folder recv: session %s created root dir %s.",
             m_params.m_sessionId.c_str(), PathForLog(m_rootPath).c_str());
    return true;
}

bool FolderRecvSession::CreateAllDirectories()
{
    //已建过的相对路径，避免同一条路径被反复 CreateDirectory
    std::set<std::string> created;

    //清单里的目录已按广度优先排好（父目录先于子目录），逐条建即可；空目录也在其中
    for (size_t i = 0; i < m_dirs.size(); ++i)
    {
        if (ShouldStop())
            return false;

        if (!EnsureRelativeDirectory(m_rootPath, m_dirs[i], created))
        {
            LOG_ERROR("folder recv: session %s create dir %s under %s failed, win32 error %lu.",
                      m_params.m_sessionId.c_str(), m_dirs[i].c_str(),
                      PathForLog(m_rootPath).c_str(), ::GetLastError());
            Fail(error_code_folder_mkdir_fail, "create directory failed");
            return false;
        }
    }

    //防御性补齐：清单里的目录列表正常已覆盖每个文件所在的目录，万一漏了就在这里补上，
    //否则写文件时会因为找不到落点而失败，而那时字节已经开始传了
    for (size_t i = 0; i < m_files.size(); ++i)
    {
        if (ShouldStop())
            return false;

        const std::string parent = folderrecv::ParentOfRelativePath(m_files[i].m_relpath);
        if (parent.empty() || created.find(parent) != created.end())
            continue;

        LOG_WARNING("folder recv: session %s manifest has no dir entry for %s, creating it.",
                    m_params.m_sessionId.c_str(), parent.c_str());
        if (!EnsureRelativeDirectory(m_rootPath, parent, created))
        {
            LOG_ERROR("folder recv: session %s create missing dir %s failed, win32 error %lu.",
                      m_params.m_sessionId.c_str(), parent.c_str(), ::GetLastError());
            Fail(error_code_folder_mkdir_fail, "create missing directory failed");
            return false;
        }
    }

    LOG_INFO("folder recv: session %s created %u directories under the root dir.",
             m_params.m_sessionId.c_str(), static_cast<unsigned int>(created.size()));
    return true;
}

bool FolderRecvSession::ReconcileResumeState()
{
    //只核对「有进度」的文件。首次传输时全部文件都是全新的，一次磁盘调用都不必发，
    //上万个文件也不会因为逐个 stat 而卡住
    for (size_t i = 0; i < m_files.size(); ++i)
    {
        FolderRecvFileState record;
        if (!m_state->GetFileRecord(static_cast<int64_t>(i), record))
            continue;
        if (!record.m_completed && record.m_bytesWritten <= 0)
            continue;

        std::wstring local;
        if (folderrecv::ResolveLocalRelativePath(m_files[i].m_relpath, local) != kFolderPathOk)
        {
            Fail(error_code_folder_write_fail, "unsafe file path in manifest");
            return false;
        }
        const std::wstring finalPath = folderrecv::JoinFolderPath(m_rootPath, local);

        //已完成的文件：确认最终文件仍在且大小相符，否则把它降级成「从头再传」
        if (record.m_completed)
        {
            int64_t size = 0;
            if (!GetExistingFileSize(finalPath, size) || size != record.m_size)
            {
                LOG_WARNING("folder recv: session %s completed file %s is missing or resized, "
                            "it will be received again.",
                            m_params.m_sessionId.c_str(), m_files[i].m_relpath.c_str());
                m_state->ResetFile(static_cast<int64_t>(i));
            }
            continue;
        }

        //半成品：把内存记录与 .part 文件的实际长度对齐
        std::wstring partPath = finalPath;
        partPath += kFolderRecvPartSuffix;

        int64_t partSize = kFolderRecvNoPartFile;
        int64_t onDisk   = 0;
        if (GetExistingFileSize(partPath, onDisk))
            partSize = onDisk;

        const int64_t resume = folderrecv::ComputeFileResumeOffset(record, partSize);
        if (resume <= 0)
        {
            LOG_WARNING("folder recv: session %s partial file %s (recorded %lld bytes, on disk %lld bytes) "
                        "cannot be resumed, it will be received again.",
                        m_params.m_sessionId.c_str(), m_files[i].m_relpath.c_str(),
                        record.m_bytesWritten, partSize);
            m_state->ResetFile(static_cast<int64_t>(i));
        }
    }

    //磁盘空间：还要落地的字节数超过根目录所在卷的剩余空间就直接失败，别等写到一半再报错
    const int64_t remaining = m_totalBytes - m_state->GetReceivedBytes();
    if (remaining > 0)
    {
        ULARGE_INTEGER freeForCaller;
        ULARGE_INTEGER totalBytes;
        ULARGE_INTEGER totalFree;
        memset(&freeForCaller, 0, sizeof(freeForCaller));
        memset(&totalBytes, 0, sizeof(totalBytes));
        memset(&totalFree, 0, sizeof(totalFree));

        const std::wstring rootLong = MakeLongPath(m_rootPath);
        if (::GetDiskFreeSpaceExW(rootLong.c_str(), &freeForCaller, &totalBytes, &totalFree))
        {
            if (static_cast<int64_t>(freeForCaller.QuadPart) < remaining)
            {
                LOG_ERROR("folder recv: session %s needs %lld more bytes but only %llu bytes are free.",
                          m_params.m_sessionId.c_str(), remaining, freeForCaller.QuadPart);
                Fail(error_code_folder_disk_full, "not enough free disk space for this folder");
                return false;
            }
        }
        else
        {
            //取不到剩余空间不算致命：继续传，真写满了写盘失败时自然会报错
            LOG_WARNING("folder recv: session %s query free disk space failed, win32 error %lu.",
                        m_params.m_sessionId.c_str(), ::GetLastError());
        }
    }
    return true;
}

//==================== FolderRecvSession：数据通道 ====================

bool FolderRecvSession::HandleDataFrame(FolderRecvLane* lane, const std::string& frame)
{
    const unsigned char* body = reinterpret_cast<const unsigned char*>(frame.data());

    //分片走零拷贝解码：每片 512 KiB，能省一次整片内存拷贝
    folder_frame_type type = ft_invalid;
    if (!relayframing::PeekFrameType(body, frame.size(), type))
    {
        LOG_ERROR("folder recv: session %s peek frame type failed on lane %d.",
                  m_params.m_sessionId.c_str(), lane->GetLaneIndex());
        Fail(error_code_folder_relay_broken, "peek frame type failed on data lane");
        return false;
    }

    if (type == ft_file_chunk)
    {
        FolderRelayFileChunk info;
        size_t dataOffset = 0;
        if (!relayframing::DecodeFileChunk(body, frame.size(), info, dataOffset))
        {
            LOG_ERROR("folder recv: session %s decode chunk frame failed on lane %d.",
                      m_params.m_sessionId.c_str(), lane->GetLaneIndex());
            Fail(error_code_folder_relay_broken, "decode chunk frame failed");
            return false;
        }
        return WriteChunk(lane, info, body + dataOffset, static_cast<size_t>(info.m_datalen));
    }

    FolderRelayInboundFrame inbound;
    if (!relayframing::DecodeFrame(body, frame.size(), inbound))
    {
        LOG_ERROR("folder recv: session %s decode data frame failed on lane %d.",
                  m_params.m_sessionId.c_str(), lane->GetLaneIndex());
        Fail(error_code_folder_relay_broken, "decode data frame failed");
        return false;
    }

    switch (inbound.m_type)
    {
    //本通道开始接收一个新文件：准备好 .part 文件与增量 MD5 上下文
    case ft_file_begin:
        return BeginFile(lane, inbound.m_fileBegin);

    //本通道当前文件的字节已发完：校验字节数与 MD5，通过则去掉 .part 后缀
    case ft_file_end:
        return FinishFile(lane, inbound.m_fileEnd);

    //对端在数据通道上中止（协议规定走控制通道，这里一并受理，免得原因丢失）
    case ft_abort:
        m_peerAborted.store(true);
        LOG_ERROR("folder recv: session %s aborted by peer on lane %d, code %d, msg %s.",
                  m_params.m_sessionId.c_str(), lane->GetLaneIndex(),
                  inbound.m_abort.m_errcode, inbound.m_abort.m_errmsg.c_str());
        Fail(inbound.m_abort.m_errcode != 0 ? inbound.m_abort.m_errcode : error_code_folder_relay_broken,
             inbound.m_abort.m_errmsg.empty() ? std::string("peer aborted the transfer")
                                              : inbound.m_abort.m_errmsg);
        return false;

    //其余帧型（清单、回执之类）都不该出现在数据通道上
    default:
        LOG_ERROR("folder recv: session %s unexpected frame type %d on data lane %d.",
                  m_params.m_sessionId.c_str(), static_cast<int>(inbound.m_type), lane->GetLaneIndex());
        Fail(error_code_folder_relay_broken, "unexpected frame type on data lane");
        return false;
    }
}

bool FolderRecvSession::BeginFile(FolderRecvLane* lane, const FolderRelayFileBegin& info)
{
    //上一个文件若没有正常收尾（发送端换文件了），先把句柄关掉、进度写回
    CloseCurrentFile(lane, true);

    if (info.m_index < 0 || static_cast<size_t>(info.m_index) >= m_files.size())
    {
        LOG_ERROR("folder recv: session %s file index %lld out of range (manifest has %u files).",
                  m_params.m_sessionId.c_str(), info.m_index, static_cast<unsigned int>(m_files.size()));
        Fail(error_code_folder_verify_fail, "file index out of range");
        return false;
    }

    const FolderRelayFileEntry& entry = m_files[static_cast<size_t>(info.m_index)];
    if (info.m_size != entry.m_size)
    {
        LOG_ERROR("folder recv: session %s file %s size %lld does not match manifest size %lld.",
                  m_params.m_sessionId.c_str(), entry.m_relpath.c_str(), info.m_size, entry.m_size);
        Fail(error_code_folder_verify_fail, "file size does not match the manifest");
        return false;
    }

    FolderRecvFileState record;
    if (!m_state->GetFileRecord(info.m_index, record))
    {
        Fail(error_code_folder_verify_fail, "no receive record for this file index");
        return false;
    }

    //发送端重传一个已经完成的文件：按重新接收处理，把记录清零后照常收
    if (record.m_completed)
    {
        LOG_WARNING("folder recv: session %s peer re-sends completed file %s, receiving it again.",
                    m_params.m_sessionId.c_str(), entry.m_relpath.c_str());
        m_state->ResetFile(info.m_index);
        m_state->GetFileRecord(info.m_index, record);
    }

    std::wstring local;
    if (folderrecv::ResolveLocalRelativePath(entry.m_relpath, local) != kFolderPathOk)
    {
        Fail(error_code_folder_write_fail, "unsafe file path in manifest");
        return false;
    }

    LaneFileContext& ctx = m_laneFiles[static_cast<size_t>(lane->GetLaneIndex())];
    ctx.m_finalPath = folderrecv::JoinFolderPath(m_rootPath, local);
    ctx.m_partPath  = ctx.m_finalPath;
    ctx.m_partPath += kFolderRecvPartSuffix;

    //权威续传位点以本端记录为准。发送端理应照 ft_manifest_ack 里给的位点起传，若它给的位点
    //与本端不符，只接受「它决定从头重传」这一种；其余情形说明两端对进度的认识已经分叉
    int64_t partSize = kFolderRecvNoPartFile;
    int64_t onDisk   = 0;
    if (GetExistingFileSize(ctx.m_partPath, onDisk))
        partSize = onDisk;

    int64_t resume = folderrecv::ComputeFileResumeOffset(record, partSize);
    if (info.m_offset != resume)
    {
        if (info.m_offset == 0)
        {
            LOG_WARNING("folder recv: session %s peer restarts file %s from zero (local resume point %lld).",
                        m_params.m_sessionId.c_str(), entry.m_relpath.c_str(), resume);
            m_state->ResetFile(info.m_index);
            m_state->GetFileRecord(info.m_index, record);
            resume = 0;
        }
        else
        {
            LOG_ERROR("folder recv: session %s peer starts file %s at %lld but local resume point is %lld.",
                      m_params.m_sessionId.c_str(), entry.m_relpath.c_str(), info.m_offset, resume);
            Fail(error_code_folder_verify_fail, "file begin offset does not match the resume point");
            return false;
        }
    }

    //打开 .part 并把它截断到权威位点：上一次可能多写了一段还没计入记录的字节，
    //留着它最终文件就会比清单大
    const std::wstring longPart = MakeLongPath(ctx.m_partPath);
    HANDLE handle = ::CreateFileW(longPart.c_str(), GENERIC_WRITE, FILE_SHARE_READ, NULL,
                                  OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (handle == INVALID_HANDLE_VALUE)
    {
        LOG_ERROR("folder recv: session %s open part file %s failed, win32 error %lu.",
                  m_params.m_sessionId.c_str(), PathForLog(ctx.m_partPath).c_str(), ::GetLastError());
        Fail(error_code_folder_write_fail, "open part file failed");
        return false;
    }

    LARGE_INTEGER pos;
    pos.QuadPart = resume;
    if (!::SetFilePointerEx(handle, pos, NULL, FILE_BEGIN) || !::SetEndOfFile(handle))
    {
        LOG_ERROR("folder recv: session %s truncate part file %s to %lld failed, win32 error %lu.",
                  m_params.m_sessionId.c_str(), PathForLog(ctx.m_partPath).c_str(), resume, ::GetLastError());
        ::CloseHandle(handle);
        Fail(error_code_folder_write_fail, "truncate part file failed");
        return false;
    }

    //半成品已打开：告诉扩展点（文件共享的上传据此给它设隐藏属性）
    if (m_params.m_hooks != NULL)
        m_params.m_hooks->OnPartFileOpened(ctx.m_partPath);

    ctx.m_fileHandle = handle;
    ctx.m_index      = info.m_index;
    ctx.m_size       = entry.m_size;
    ctx.m_written    = resume;
    ctx.m_relpath    = entry.m_relpath;
    ctx.m_expectMd5  = ToLowerHex(info.m_md5);

    //续传时接着上次的 MD5 上下文算，全新则重新开始
    if (resume > 0 && record.m_hashing)
        ctx.m_md5ctx = record.m_md5ctx;
    else
        MD5Init(&ctx.m_md5ctx);

    m_state->StoreFileProgress(ctx.m_index, ctx.m_written, ctx.m_md5ctx);

    LOG_INFO("folder recv: session %s lane %d begins file %lld (%s), size %lld, from offset %lld.",
             m_params.m_sessionId.c_str(), lane->GetLaneIndex(), ctx.m_index,
             ctx.m_relpath.c_str(), ctx.m_size, ctx.m_written);

    ReportProgress(ctx.m_relpath, true);
    return true;
}

bool FolderRecvSession::WriteChunk(FolderRecvLane* lane, const FolderRelayFileChunk& info,
                                   const unsigned char* data, size_t len)
{
    LaneFileContext& ctx = m_laneFiles[static_cast<size_t>(lane->GetLaneIndex())];

    if (ctx.m_index == kFolderRecvNoFileIndex || ctx.m_index != info.m_index
        || ctx.m_fileHandle == NULL)
    {
        LOG_ERROR("folder recv: session %s got a chunk for file %lld on lane %d, "
                  "but that lane is receiving file %lld.",
                  m_params.m_sessionId.c_str(), info.m_index, lane->GetLaneIndex(), ctx.m_index);
        Fail(error_code_folder_write_fail, "chunk does not belong to the file open on this lane");
        return false;
    }

    int64_t skip = 0;
    const folderrecv::FolderChunkVerdict verdict =
        folderrecv::ClassifyFolderChunk(ctx.m_written, info.m_offset,
                                        static_cast<int64_t>(len), ctx.m_size, skip);

    switch (verdict)
    {
    //正常来料（skip 为 0）或与已落盘部分有重叠的重发（skip 为重叠字节数）：写下新的那一段
    case folderrecv::kFolderChunkAccept:
        {
            const size_t writeLen = len - static_cast<size_t>(skip);
            if (writeLen > 0)
            {
                if (!WriteAllToHandle(static_cast<HANDLE>(ctx.m_fileHandle),
                                      data + static_cast<size_t>(skip), writeLen))
                {
                    LOG_ERROR("folder recv: session %s write %u bytes at %lld into %s failed, win32 error %lu.",
                              m_params.m_sessionId.c_str(), static_cast<unsigned int>(writeLen),
                              ctx.m_written, PathForLog(ctx.m_partPath).c_str(), ::GetLastError());
                    Fail(error_code_folder_write_fail, "write chunk to part file failed");
                    return false;
                }
                MD5Update(&ctx.m_md5ctx, data + static_cast<size_t>(skip),
                          static_cast<unsigned int>(writeLen));
                ctx.m_written = info.m_offset + static_cast<int64_t>(len);
                m_state->StoreFileBytes(ctx.m_index, ctx.m_written);
            }
            break;
        }

    //整片都已落过盘：发送端的重复投递，丢弃即可，回执照发（它带的是权威位点）
    case folderrecv::kFolderChunkDuplicate:
        LOG_WARNING("folder recv: session %s dropped a duplicate chunk of file %s at offset %lld (%u bytes), "
                    "already written %lld bytes.",
                    m_params.m_sessionId.c_str(), ctx.m_relpath.c_str(), info.m_offset,
                    static_cast<unsigned int>(len), ctx.m_written);
        break;

    //起点越过已落盘字节：中间空了一段，增量 MD5 无法跳着算
    case folderrecv::kFolderChunkGap:
        LOG_ERROR("folder recv: session %s got an out-of-order chunk of file %s at offset %lld, "
                  "expected offset %lld.",
                  m_params.m_sessionId.c_str(), ctx.m_relpath.c_str(), info.m_offset, ctx.m_written);
        Fail(error_code_folder_verify_fail, "out-of-order chunk leaves a gap in the file");
        return false;

    //本片末尾越过清单里的文件大小：对端给的数据与清单不符
    case folderrecv::kFolderChunkOverrun:
        LOG_ERROR("folder recv: session %s chunk of file %s at offset %lld (%u bytes) overruns size %lld.",
                  m_params.m_sessionId.c_str(), ctx.m_relpath.c_str(), info.m_offset,
                  static_cast<unsigned int>(len), ctx.m_size);
        Fail(error_code_folder_verify_fail, "chunk overruns the file size given in the manifest");
        return false;

    //偏移或长度为负：包体被改坏或对端实现有误
    default:
        LOG_ERROR("folder recv: session %s chunk of file %s has bad offset %lld or length %u.",
                  m_params.m_sessionId.c_str(), ctx.m_relpath.c_str(), info.m_offset,
                  static_cast<unsigned int>(len));
        Fail(error_code_folder_verify_fail, "chunk descriptor is malformed");
        return false;
    }

    //每片一个回执：既是流控（发送端在途分片不超过窗口），也是权威续传位点
    FolderRelayChunkAck ack;
    ack.m_index      = ctx.m_index;
    ack.m_nextoffset = ctx.m_written;

    std::string frame;
    if (!relayframing::BuildChunkAckFrame(ack, frame))
    {
        Fail(error_code_folder_relay_broken, "build chunk ack frame failed");
        return false;
    }
    if (!lane->GetClient().SendAppFrame(reinterpret_cast<const unsigned char*>(frame.data()), frame.size()))
    {
        LOG_ERROR("folder recv: session %s send chunk ack on lane %d failed.",
                  m_params.m_sessionId.c_str(), lane->GetLaneIndex());
        Fail(error_code_folder_relay_broken, "send chunk ack failed");
        return false;
    }

    ReportProgress(ctx.m_relpath, false);
    return true;
}

bool FolderRecvSession::FinishFile(FolderRecvLane* lane, const FolderRelayFileEnd& info)
{
    LaneFileContext& ctx = m_laneFiles[static_cast<size_t>(lane->GetLaneIndex())];

    if (ctx.m_index == kFolderRecvNoFileIndex || ctx.m_index != info.m_index)
    {
        LOG_ERROR("folder recv: session %s got file end for %lld on lane %d, but that lane holds %lld.",
                  m_params.m_sessionId.c_str(), info.m_index, lane->GetLaneIndex(), ctx.m_index);
        Fail(error_code_folder_write_fail, "file end does not belong to the file open on this lane");
        return false;
    }

    //先把结果码算出来，无论成败都要回一个 ft_file_result，发送端据此定位是哪个文件出的问题
    int         resultCode = error_code_ok;
    std::string resultMsg  = "ok";

    if (ctx.m_written != ctx.m_size)
    {
        LOG_ERROR("folder recv: session %s file %s received %lld bytes but the manifest says %lld.",
                  m_params.m_sessionId.c_str(), ctx.m_relpath.c_str(), ctx.m_written, ctx.m_size);
        resultCode = error_code_folder_verify_fail;
        resultMsg  = "received size does not match the manifest";
    }

    std::string actualMd5;
    if (resultCode == error_code_ok && !ctx.m_expectMd5.empty())
    {
        actualMd5 = Md5HexFromContext(ctx.m_md5ctx);
        if (actualMd5 != ctx.m_expectMd5)
        {
            LOG_ERROR("folder recv: session %s file %s md5 mismatch, expected %s but computed %s.",
                      m_params.m_sessionId.c_str(), ctx.m_relpath.c_str(),
                      ctx.m_expectMd5.c_str(), actualMd5.c_str());
            resultCode = error_code_folder_verify_fail;
            resultMsg  = "content md5 does not match the value given by the sender";
        }
    }

    //校验通过：关句柄之后把 .part 后缀去掉，落地这一步是原子的
    if (resultCode == error_code_ok)
    {
        if (ctx.m_fileHandle != NULL)
        {
            ::CloseHandle(static_cast<HANDLE>(ctx.m_fileHandle));
            ctx.m_fileHandle = NULL;
        }

        //有扩展点时由它决定正式名已存在能否覆盖（文件共享的上传不覆盖）
        const bool replace = (m_params.m_hooks == NULL) || m_params.m_hooks->ReplaceExistingOnFinish();
        const DWORD moveFlags = MOVEFILE_WRITE_THROUGH | (replace ? MOVEFILE_REPLACE_EXISTING : 0);
        if (!::MoveFileExW(MakeLongPath(ctx.m_partPath).c_str(), MakeLongPath(ctx.m_finalPath).c_str(), moveFlags))
        {
            LOG_ERROR("folder recv: session %s rename %s to the final name failed, win32 error %lu.",
                      m_params.m_sessionId.c_str(), PathForLog(ctx.m_partPath).c_str(), ::GetLastError());
            resultCode = error_code_folder_write_fail;
            resultMsg  = "rename the part file to its final name failed";
        }
        else if (m_params.m_hooks != NULL)
        {
            //先于下面的结果帧：发送端收到完成时，正式文件已经是最终的样子
            m_params.m_hooks->OnFileFinalized(ctx.m_partPath, ctx.m_finalPath);
            //字节数与 MD5 都已校验通过：交出落地路径与这份 MD5（发送端没给 MD5 时为空）
            m_params.m_hooks->OnFileVerified(ctx.m_finalPath, actualMd5);
        }
    }

    FolderRelayFileResult result;
    result.m_index = ctx.m_index;
    result.m_code  = resultCode;
    result.m_msg   = resultMsg;

    std::string frame;
    if (relayframing::BuildFileResultFrame(result, frame))
        lane->GetClient().SendAppFrame(reinterpret_cast<const unsigned char*>(frame.data()), frame.size());

    if (resultCode != error_code_ok)
    {
        //失败：句柄与半成品都留着，进度写回接收状态，供期内重试续传
        CloseCurrentFile(lane, true);
        Fail(resultCode, resultMsg);
        return false;
    }

    const int64_t index   = ctx.m_index;
    const int64_t size    = ctx.m_size;
    const std::string rel = ctx.m_relpath;

    m_state->MarkFileCompleted(index);

    //本通道恢复空闲，等发送端派下一个文件
    ctx = LaneFileContext();

    LOG_INFO("folder recv: session %s lane %d finished file %lld (%s), %lld bytes verified.",
             m_params.m_sessionId.c_str(), lane->GetLaneIndex(), index, rel.c_str(), size);

    ReportProgress(rel, true);
    MaybeFinish();
    return true;
}

void FolderRecvSession::CloseCurrentFile(FolderRecvLane* lane, bool storeProgress)
{
    LaneFileContext& ctx = m_laneFiles[static_cast<size_t>(lane->GetLaneIndex())];

    if (ctx.m_fileHandle != NULL)
    {
        ::CloseHandle(static_cast<HANDLE>(ctx.m_fileHandle));
        ctx.m_fileHandle = NULL;
    }

    //把已落盘字节数与增量 MD5 上下文写回接收状态，重试时才能接着算，不必从头重传
    if (storeProgress && ctx.m_index != kFolderRecvNoFileIndex && m_state)
        m_state->StoreFileProgress(ctx.m_index, ctx.m_written, ctx.m_md5ctx);

    ctx = LaneFileContext();
}

//==================== FolderRecvSession：收尾与回报 ====================

void FolderRecvSession::Fail(int errcode, const std::string& errmsg)
{
    //只记第一个原因：后续通道连锁失败产生的次生错误不该盖掉真正的起因
    bool expected = false;
    if (!m_failed.compare_exchange_strong(expected, true))
        return;

    {
        std::lock_guard<std::mutex> guard(m_failMtx);
        m_errcode = errcode;
        m_errmsg  = errmsg;
    }

    LOG_ERROR("folder recv: session %s is aborting, code %d, msg %s.",
              m_params.m_sessionId.c_str(), errcode, errmsg.c_str());

    //对端已经发过中止帧就不必再回一个，它那边正在拆通道
    if (!m_peerAborted.load())
    {
        FolderRelayAbort abort;
        abort.m_errcode = errcode;
        abort.m_errmsg  = errmsg;

        std::string frame;
        if (relayframing::BuildAbortFrame(abort, frame))
            SendControlFrame(frame);
    }
}

bool FolderRecvSession::SendControl(const std::string& frame)
{
    return SendControlFrame(frame);
}

bool FolderRecvSession::SendControlFrame(const std::string& frame)
{
    if (m_lanes.empty())
        return false;

    return m_lanes[0]->GetClient().SendAppFrame(
        reinterpret_cast<const unsigned char*>(frame.data()), frame.size());
}

void FolderRecvSession::MaybeFinish()
{
    //三个条件缺一不可：清单已就绪（否则文件总数还不知道）、发送端已声明全部发完、
    //本端每个文件都已通过校验并落地
    if (!m_manifestReady.load() || !m_completeReceived.load())
        return;
    if (m_state->GetCompletedFileCount() != static_cast<int64_t>(m_files.size()))
        return;

    m_allDone.store(true);
}

void FolderRecvSession::ReportProgress(const std::string& currentFile, bool force)
{
    std::lock_guard<std::mutex> guard(m_reportMtx);

    const int64_t now = static_cast<int64_t>(::GetTickCount64());
    if (!force && m_lastReportTick != 0 && now - m_lastReportTick < kFolderRecvProgressIntervalMs)
        return;

    m_lastReportTick = now;

    FolderRecvProgress progress;
    progress.m_receivedBytes = m_state->GetReceivedBytes();
    progress.m_totalBytes    = m_totalBytes;
    progress.m_doneFiles     = m_state->GetCompletedFileCount();
    progress.m_totalFiles    = static_cast<int64_t>(m_files.size());
    progress.m_currentFile   = currentFile;

    m_handler->OnFolderRecvProgress(progress);
}

} // namespace foldertransfer
