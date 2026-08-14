/**
 *  单聊「发送文件夹」发送端状态机的实现，FolderSendSession.cpp
 *
 *  本文件只承担"碰网络与碰磁盘"的那一半：建通道、发帧、读盘、算 MD5、线程编排。另一半
 *  纯逻辑（待传文件队列的分派、续传起点的应用、流控窗口的推进）在 FolderSendSession.h
 *  里以内联类给出，由 Source/Tests/FolderSendSessionTests.cpp 覆盖。
 *
 *  线程与锁的约定（改动本文件前务必先读，顺序写反了就会死锁）：
 *    · m_stateMtx 保护失败原因与当前文件名，m_manifestMtx 保护目录就绪回执，
 *      每条通道另有各自的 LaneContext::m_mtx 保护该通道的窗口与文件结果；
 *    · Fail() 会依次取 m_stateMtx、m_manifestMtx 与<u>全部</u>通道锁，故<u>任何时候都不得
 *      在持有通道锁的情况下调用 Fail() / ReportProgress()</u>，必须先把锁放掉；
 *    · 反向的加锁顺序（先通道锁后 m_stateMtx）在本文件里一处都没有，新增代码也不要引入。
 *
 *  balloonwj@qq.com   2026-08-14
 */

//本头文件<u>必须排在最前</u>：它把用到的标准库头文件都引全了，之后 FolderRelayClient.h
//才把 <winsock2.h> 连同 <windows.h> 的 min / max 宏带进来。顺序反过来的话，标准库里
//形如 std::numeric_limits<T>::min() 的写法会被当成宏调用，编译期报一串 C4003。
#include "FolderSendSession.h"

#include "FolderRelayClient.h"      // FolderRelayClient / kFolderRelayMaxAppFrameBytes / kFolderMaxDataLanes
#include "../net/Protocol.h"        // enum error_code
#include "../MD5Core.h"             // MD5Init / MD5Update / MD5Final（与上传路径复用同一份实现）
#include "../IULog.h"               // LOG_INFO / LOG_WARNING / LOG_ERROR

#include <fstream>
#include <stdio.h>                  // sprintf_s（MD5 十六进制串）

namespace foldertransfer {

//MD5 摘要的字节数与其十六进制文本的字符数。MD5 定长 128 位，故为 16 字节 / 32 个字符。
static const int kMd5DigestBytes = 16;
static const int kMd5HexChars = 32;
//sprintf_s 每次写两个十六进制字符外加一个结尾 '\0'，故缓冲区按 3 字节给。
static const int kMd5HexPerByteBufBytes = 3;

//控制通道在 m_lanes 里的下标。控制通道的 lane 序号固定为 0（kFolderControlLane），
//两者取值相同，故下标即 lane 序号。
static const size_t kControlLaneSlot = 0;

//LaneContext::m_fileIndex 的空闲取值：该通道当前没有在传任何文件。
static const int64_t kLaneIdleFileIndex = -1;

//文件校验结果里表示成功的结果码，与服务端 / 接收端的 error_code_ok 一致。
static const int kFileResultCodeOk = 0;

// ======================== 文件读取的小工具 ========================

/**
 *  取一个已打开文件流的字节数。
 *  @param in 已打开的二进制流
 *  @return 字节数；取不到返回 -1
 */
static int64_t streamFileSize(std::ifstream& in)
{
    in.clear();
    in.seekg(0, std::ios::end);
    if (!in)
        return -1;
    const std::streamoff pos = in.tellg();
    if (pos < 0)
        return -1;
    return static_cast<int64_t>(pos);
}

/**
 *  从已打开的文件流里按偏移读满一片。
 *  读不满即判失败 —— 清单里记着这个文件有多长，读不满说明它在扫描之后被改动过，继续发
 *  下去只会让接收端校验失败，不如立刻停下。
 *  @param in     已打开的二进制流
 *  @param offset 起始偏移
 *  @param buf    接收缓冲区，容量须 >= len
 *  @param len    本片字节数
 *  @return 读满返回 true
 */
static bool readFileChunkAt(std::ifstream& in, int64_t offset, char* buf, size_t len)
{
    in.clear();
    in.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    if (!in)
        return false;
    in.read(buf, static_cast<std::streamsize>(len));
    if (in.gcount() != static_cast<std::streamsize>(len))
        return false;
    return true;
}

// ======================== LaneContext ========================

/**
 *  一条中转通道的上下文：连接本身、回调适配，以及该通道的传输状态。
 *
 *  控制通道（下标 0）只用到 m_client 与帧回调；数据通道（下标 1..N）另外用 m_window
 *  做流控、用 m_resultXxx 承接对端的文件校验结果。两种通道共用一个类是因为差异只在
 *  "哪些成员会被用到"，另拆一个类反而要在收帧路径上多一层分支。
 *
 *  m_window / m_fileIndex / m_resultXxx 由本通道的工作线程与接收线程共同访问，一律在
 *  m_mtx 之内读写；m_cv 是工作线程等回执与等文件结果的唤醒点。
 */
class FolderSendSession::LaneContext : public FolderRelayClientHandler
{
public:
    LaneContext(FolderSendSession* owner, size_t slot)
        : m_owner(owner)
        , m_slot(slot)
        , m_fileIndex(kLaneIdleFileIndex)
        , m_resultReady(false)
        , m_resultCode(kFileResultCodeOk)
    {
    }

    //收到一个应用帧（在本通道的接收线程上被调用）。
    virtual void OnFolderRelayAppFrame(const unsigned char* data, size_t len)
    {
        m_owner->OnLaneFrame(m_slot, data, len);
    }

    //本通道异常断开（在本通道的接收线程上被调用）。
    virtual void OnFolderRelayClosed()
    {
        m_owner->OnLaneClosed(m_slot);
    }

    FolderSendSession*      m_owner;        // 所属会话（不拥有；生命周期必然长于本对象）
    size_t                  m_slot;         // 本通道在 m_lanes 里的下标，同时就是 lane 序号
    FolderRelayClient       m_client;       // 本通道的中转连接

    std::mutex              m_mtx;          // 保护以下四个成员（工作线程与接收线程共同访问）
    std::condition_variable m_cv;           // 工作线程等分片回执 / 等文件校验结果的唤醒点
    FolderSendFlowWindow    m_window;       // 本通道的流控窗口，每开始一个文件重置一次
    int64_t                 m_fileIndex;    // 本通道当前在传的文件序号；kLaneIdleFileIndex 表示空闲
    bool                    m_resultReady;  // 是否已收到当前文件的 ft_file_result
    int                     m_resultCode;   // 当前文件的校验结果码，0 为通过
    std::string             m_resultMsg;    // 当前文件的校验结果描述
};

// ======================== 构造 / 析构 ========================

FolderSendSession::FolderSendSession()
    : m_handler(NULL)
    , m_dataLaneCount(kFolderSendMinDataLanes)
    , m_started(false)
    , m_running(false)
    , m_stop(false)
    , m_failed(false)
    , m_errcode(0)
    , m_manifestAckReady(false)
    , m_sentBytes(0)
    , m_finishedFiles(0)
    , m_lastProgressAt(std::chrono::steady_clock::now())
{
}

FolderSendSession::~FolderSendSession()
{
    Stop();
}

// ======================== 公开接口 ========================

bool FolderSendSession::Start(const FolderSendSessionParams& params, IFolderSendSessionHandler* handler)
{
    if (handler == NULL)
    {
        LOG_ERROR("FolderSendSession: start rejected, handler is null.");
        return false;
    }
    if (params.m_relayIp.empty() || params.m_relayPort <= 0 || params.m_relayToken.empty())
    {
        LOG_ERROR("FolderSendSession: start rejected, bad relay params, port=%d.", params.m_relayPort);
        return false;
    }
    if (params.m_rootPath.empty())
    {
        LOG_ERROR("FolderSendSession: start rejected, root path is empty.");
        return false;
    }
    //一个实例只跑一次传输：重试续传要拿新下发的中转参数另建实例，复用已结束的实例会
    //把上一次的失败状态带进新一次传输
    if (m_started.exchange(true))
    {
        LOG_ERROR("FolderSendSession: start rejected, this session has already been started.");
        return false;
    }

    m_params = params;
    m_handler = handler;

    //数据通道条数由服务端下发，这里只做下限与硬上限的修正
    m_dataLaneCount = params.m_dataLanes;
    if (m_dataLaneCount < kFolderSendMinDataLanes)
    {
        LOG_WARNING("FolderSendSession: lanes=%d from server is too small, use %d.",
                    params.m_dataLanes, kFolderSendMinDataLanes);
        m_dataLaneCount = kFolderSendMinDataLanes;
    }
    if (m_dataLaneCount > kFolderMaxDataLanes)
    {
        LOG_WARNING("FolderSendSession: lanes=%d from server exceeds hard limit, use %d.",
                    params.m_dataLanes, kFolderMaxDataLanes);
        m_dataLaneCount = kFolderMaxDataLanes;
    }

    //清单的文件列表转成协议结构，下标即文件序号，此后发帧与队列都用这一份
    m_files.clear();
    m_files.reserve(m_params.m_manifest.m_files.size());
    for (size_t i = 0; i < m_params.m_manifest.m_files.size(); ++i)
    {
        FolderRelayFileEntry entry;
        entry.m_relpath = m_params.m_manifest.m_files[i].m_relativePath;
        entry.m_size = m_params.m_manifest.m_files[i].m_sizeBytes;
        m_files.push_back(entry);
    }
    m_queue.Reset(m_files);

    //<u>通道对象在这里一次性建齐</u>，而不是等驱动线程去建：Cancel / Stop 与各通道的接收
    //线程都会遍历 m_lanes，容器在传输过程中再被写入就是数据竞争。此处仍在调用方线程上、
    //驱动线程尚未启动，是唯一可以安全写它的时机
    const int laneTotal = m_dataLaneCount + 1;
    m_lanes.reserve(static_cast<size_t>(laneTotal));
    for (int i = 0; i < laneTotal; ++i)
        m_lanes.push_back(std::unique_ptr<LaneContext>(new LaneContext(this, static_cast<size_t>(i))));

    m_running.store(true);
    m_driver.reset(new std::thread(&FolderSendSession::RunProc, this));
    m_driverId = m_driver->get_id();
    return true;
}

void FolderSendSession::Cancel()
{
    LOG_INFO("FolderSendSession: cancel requested by local user.");
    Fail(error_code_folder_canceled, "canceled by local user");
}

void FolderSendSession::Stop()
{
    //传输还在进行就先记一个"本端主动停止"的原因，让驱动线程走正常的失败收尾路径（发
    //ft_abort、关通道、回报失败）。Fail 只记第一个原因，故已经因别的原因失败时不会被盖掉
    if (m_running.load())
        Fail(error_code_folder_canceled, "session stopped by local side");

    m_stop.store(true);
    WakeAllLanes();
    {
        //目录就绪回执的等待点也要叫醒，否则驱动线程会一直等到超时才发现该收工了
        std::lock_guard<std::mutex> guard(m_manifestMtx);
        m_manifestCv.notify_all();
    }

    if (!m_driver)
        return;
    //在回调线程里调 Stop 等于让驱动线程 join 自己，这里识别出来并跳过；线程仍会自行走完
    //收尾流程，只是本次调用不等它
    if (std::this_thread::get_id() == m_driverId)
    {
        LOG_WARNING("FolderSendSession: Stop() called from the session thread, skip join.");
        return;
    }
    if (m_driver->joinable())
        m_driver->join();
    m_driver.reset();
}

bool FolderSendSession::IsRunning() const
{
    return m_running.load();
}

// ======================== 驱动线程 ========================

void FolderSendSession::RunProc()
{
    LOG_INFO("FolderSendSession: session start, relay=%s:%d, data lanes=%d, files=%u, bytes=%lld.",
             m_params.m_relayIp.c_str(), m_params.m_relayPort, m_dataLaneCount,
             static_cast<unsigned int>(m_files.size()),
             static_cast<long long>(m_queue.TotalBytes()));

    //建通道 → 发清单 → 等目录就绪 → 并行传文件，前一步成功才做下一步
    if (OpenLanes() && SendManifest() && WaitManifestAck())
    {
        for (int i = 1; i <= m_dataLaneCount; ++i)
        {
            m_workers.push_back(std::unique_ptr<std::thread>(
                new std::thread(&FolderSendSession::WorkerProc, this, static_cast<size_t>(i))));
        }
        for (size_t i = 0; i < m_workers.size(); ++i)
        {
            if (m_workers[i]->joinable())
                m_workers[i]->join();
        }
        m_workers.clear();
    }

    bool failed = false;
    int errcode = 0;
    std::string errmsg;
    {
        std::lock_guard<std::mutex> guard(m_stateMtx);
        failed = m_failed;
        errcode = m_errcode;
        errmsg = m_errmsg;
    }

    //全部文件都传完了：在控制通道上发完成帧
    if (!failed)
    {
        FolderRelayComplete summary;
        summary.m_totalfiles = static_cast<int64_t>(m_queue.TotalCount());
        summary.m_totalsize = m_queue.TotalBytes();

        std::string frame;
        if (!relayframing::BuildCompleteFrame(summary, frame))
        {
            Fail(error_code_bad_request, "build ft_complete failed");
        }
        else if (!m_lanes[kControlLaneSlot]->m_client.SendAppFrame(
                     reinterpret_cast<const unsigned char*>(frame.data()), frame.size()))
        {
            Fail(error_code_folder_relay_broken, "send ft_complete failed");
        }
        else
        {
            LOG_INFO("FolderSendSession: ft_complete sent, files=%lld, bytes=%lld.",
                     static_cast<long long>(summary.m_totalfiles),
                     static_cast<long long>(summary.m_totalsize));
        }

        std::lock_guard<std::mutex> guard(m_stateMtx);
        failed = m_failed;
        errcode = m_errcode;
        errmsg = m_errmsg;
    }

    //失败时把原因经控制通道直达对端，再把全部通道停掉
    if (failed)
        SendAbort();
    CloseAllLanes();

    m_running.store(false);

    if (failed)
    {
        LOG_ERROR("FolderSendSession: session finished with failure, errcode=%d, errmsg=%s.",
                  errcode, errmsg.c_str());
        m_handler->OnFolderSendFailed(errcode, errmsg);
    }
    else
    {
        LOG_INFO("FolderSendSession: session finished successfully, files=%u, bytes=%lld.",
                 static_cast<unsigned int>(m_queue.TotalCount()),
                 static_cast<long long>(m_queue.TotalBytes()));
        ReportProgress(true, "");
        m_handler->OnFolderSendCompleted(static_cast<int64_t>(m_queue.TotalCount()), m_queue.TotalBytes());
    }
}

bool FolderSendSession::OpenLanes()
{
    //通道对象已在 Start 里建齐（见那里的说明），这里只负责逐条连接
    const int total = static_cast<int>(m_lanes.size());
    const int timeoutSec = m_params.m_connectTimeoutSec > 0
                           ? m_params.m_connectTimeoutSec
                           : kFolderRelayConnectTimeoutSec;

    for (int i = 0; i < total; ++i)
    {
        if (ShouldStop())
        {
            LOG_WARNING("FolderSendSession: stop requested while opening lanes, lane=%d.", i);
            return false;
        }
        if (!m_lanes[static_cast<size_t>(i)]->m_client.Connect(m_params.m_relayIp, m_params.m_relayPort,
                                                              m_params.m_relayToken, i,
                                                              m_lanes[static_cast<size_t>(i)].get(),
                                                              timeoutSec))
        {
            LOG_ERROR("FolderSendSession: open relay lane failed, lane=%d, relay=%s:%d.",
                      i, m_params.m_relayIp.c_str(), m_params.m_relayPort);
            Fail(error_code_folder_relay_broken, "open relay lane failed");
            return false;
        }
        LOG_INFO("FolderSendSession: relay lane ready, lane=%d.", i);
    }

    LOG_INFO("FolderSendSession: all relay lanes ready, total=%d (1 control + %d data).",
             total, m_dataLaneCount);
    return true;
}

bool FolderSendSession::SendManifest()
{
    std::vector<std::string> frames;
    if (!relayframing::BuildManifestFrames(m_params.m_manifest.m_folderName,
                                           m_params.m_manifest.m_dirs,
                                           m_files,
                                           kFolderRelayManifestSegmentBytes,
                                           frames))
    {
        LOG_ERROR("FolderSendSession: build manifest frames failed, dirs=%u, files=%u.",
                  static_cast<unsigned int>(m_params.m_manifest.m_dirs.size()),
                  static_cast<unsigned int>(m_files.size()));
        Fail(error_code_bad_request, "build manifest frames failed");
        return false;
    }

    //封帧按加密前的 2 MiB 判上限，而通道按加密后的大小判上限，两者差 net::STREAM_OVERHEAD_BYTES
    //字节。压线的那一段会"构造得出来却发不出去"，故发之前先逐帧核对一遍
    size_t badIndex = 0;
    if (!CheckManifestFramesFitLimit(frames, kFolderRelayMaxAppFrameBytes, badIndex))
    {
        LOG_ERROR("FolderSendSession: manifest segment %u is %u bytes, exceeds relay limit %u bytes.",
                  static_cast<unsigned int>(badIndex),
                  static_cast<unsigned int>(frames[badIndex].size()),
                  static_cast<unsigned int>(kFolderRelayMaxAppFrameBytes));
        Fail(error_code_bad_request, "manifest frame exceeds relay limit");
        return false;
    }

    for (size_t i = 0; i < frames.size(); ++i)
    {
        if (ShouldStop())
            return false;
        if (!m_lanes[kControlLaneSlot]->m_client.SendAppFrame(
                reinterpret_cast<const unsigned char*>(frames[i].data()), frames[i].size()))
        {
            LOG_ERROR("FolderSendSession: send manifest segment %u/%u failed.",
                      static_cast<unsigned int>(i), static_cast<unsigned int>(frames.size()));
            Fail(error_code_folder_relay_broken, "send ft_manifest failed");
            return false;
        }
    }

    LOG_INFO("FolderSendSession: manifest sent, segments=%u, dirs=%u, files=%u, bytes=%lld.",
             static_cast<unsigned int>(frames.size()),
             static_cast<unsigned int>(m_params.m_manifest.m_dirs.size()),
             static_cast<unsigned int>(m_files.size()),
             static_cast<long long>(m_queue.TotalBytes()));
    return true;
}

bool FolderSendSession::WaitManifestAck()
{
    LOG_INFO("FolderSendSession: waiting receiver to create directories, timeout=%d sec.",
             kFolderSendManifestAckTimeoutSec);

    FolderRelayManifestAck ack;
    bool got = false;
    {
        std::unique_lock<std::mutex> lock(m_manifestMtx);
        while (!m_manifestAckReady && !m_stop.load())
        {
            const std::cv_status st = m_manifestCv.wait_for(
                lock, std::chrono::seconds(kFolderSendManifestAckTimeoutSec));
            //超时且回执仍未到：不再等下去，按失败收场
            if (st == std::cv_status::timeout && !m_manifestAckReady)
                break;
        }
        got = m_manifestAckReady;
        if (got)
            ack = m_manifestAck;
    }

    if (!got)
    {
        //已经在停了（失败或取消）：原因早已记下，不要用超时把它盖掉
        if (ShouldStop())
            return false;
        LOG_ERROR("FolderSendSession: manifest ack not received in %d sec.",
                  kFolderSendManifestAckTimeoutSec);
        //归入"通道不工作"这一类：等不到回执时无从判断是对端建目录卡住还是链路不通，
        //而接收端真正建目录失败时会主动发 ft_abort 带上 error_code_folder_mkdir_fail
        Fail(error_code_folder_relay_broken, "manifest ack timeout");
        return false;
    }

    if (!m_queue.ApplyResumePoints(ack))
    {
        LOG_ERROR("FolderSendSession: manifest ack invalid, totalfiles=%lld, points=%u, local files=%u.",
                  static_cast<long long>(ack.m_totalfiles),
                  static_cast<unsigned int>(ack.m_points.size()),
                  static_cast<unsigned int>(m_files.size()));
        Fail(error_code_bad_request, "manifest ack resume points invalid");
        return false;
    }

    m_sentBytes.store(m_queue.ResumedBytes());
    m_finishedFiles.store(static_cast<int64_t>(m_queue.SkippedCount()));

    LOG_INFO("FolderSendSession: directories ready on receiver, resumed bytes=%lld, skipped files=%u, pending files=%u.",
             static_cast<long long>(m_queue.ResumedBytes()),
             static_cast<unsigned int>(m_queue.SkippedCount()),
             static_cast<unsigned int>(m_queue.PendingCount()));
    ReportProgress(true, "");
    return true;
}

// ======================== 数据通道工作线程 ========================

void FolderSendSession::WorkerProc(size_t laneSlot)
{
    LOG_INFO("FolderSendSession: data lane worker started, lane=%u.",
             static_cast<unsigned int>(laneSlot));

    FolderSendFileTask task;
    while (!ShouldStop() && m_queue.TakeNext(task))
    {
        if (!SendOneFile(laneSlot, task))
            break;

        m_finishedFiles.fetch_add(1);
        LOG_INFO("FolderSendSession: file done, lane=%u, index=%lld, path=%s.",
                 static_cast<unsigned int>(laneSlot),
                 static_cast<long long>(task.m_index), task.m_relpath.c_str());
        ReportProgress(true, "");
    }

    LOG_INFO("FolderSendSession: data lane worker exit, lane=%u.",
             static_cast<unsigned int>(laneSlot));
}

bool FolderSendSession::SendOneFile(size_t laneSlot, const FolderSendFileTask& task)
{
    LaneContext& ctx = *m_lanes[laneSlot];
    const std::wstring localPath = MakeLocalPath(task.m_relpath);

    LOG_INFO("FolderSendSession: file start, lane=%u, index=%lld, size=%lld, offset=%lld, path=%s.",
             static_cast<unsigned int>(laneSlot), static_cast<long long>(task.m_index),
             static_cast<long long>(task.m_size), static_cast<long long>(task.m_startOffset),
             task.m_relpath.c_str());
    ReportProgress(false, task.m_relpath);

    //1. 先流式算一遍整文件 MD5（一次额外读盘），随 ft_file_begin 下发给接收端
    std::string md5;
    if (!ComputeFileMd5(localPath, md5))
    {
        if (ShouldStop())
            return false;
        LOG_ERROR("FolderSendSession: compute md5 failed, lane=%u, path=%s.",
                  static_cast<unsigned int>(laneSlot), task.m_relpath.c_str());
        Fail(error_code_folder_read_fail, "compute source file md5 failed");
        return false;
    }

    //2. 打开源文件并核对长度：扫描之后被改动过的文件继续发下去只会让对端校验失败
    std::ifstream in(localPath.c_str(), std::ios::binary);
    if (!in)
    {
        if (ShouldStop())
            return false;
        LOG_ERROR("FolderSendSession: open source file failed, lane=%u, path=%s.",
                  static_cast<unsigned int>(laneSlot), task.m_relpath.c_str());
        Fail(error_code_folder_read_fail, "open source file failed");
        return false;
    }
    const int64_t actualSize = streamFileSize(in);
    if (actualSize != task.m_size)
    {
        LOG_ERROR("FolderSendSession: source file size changed, lane=%u, path=%s, manifest=%lld, actual=%lld.",
                  static_cast<unsigned int>(laneSlot), task.m_relpath.c_str(),
                  static_cast<long long>(task.m_size), static_cast<long long>(actualSize));
        Fail(error_code_folder_read_fail, "source file size changed after scan");
        return false;
    }

    //3. 重置本通道的窗口与文件结果，再发 ft_file_begin
    {
        std::lock_guard<std::mutex> guard(ctx.m_mtx);
        ctx.m_window.Begin(task.m_size, task.m_startOffset,
                           kFolderRelayChunkBytes, kFolderSendMaxInflightChunks);
        ctx.m_fileIndex = task.m_index;
        ctx.m_resultReady = false;
        ctx.m_resultCode = kFileResultCodeOk;
        ctx.m_resultMsg.clear();
    }

    FolderRelayFileBegin begin;
    begin.m_index = task.m_index;
    begin.m_size = task.m_size;
    begin.m_md5 = md5;
    begin.m_offset = task.m_startOffset;

    std::string frame;
    if (!relayframing::BuildFileBeginFrame(begin, frame))
    {
        Fail(error_code_bad_request, "build ft_file_begin failed");
        return false;
    }
    if (!ctx.m_client.SendAppFrame(reinterpret_cast<const unsigned char*>(frame.data()), frame.size()))
    {
        Fail(error_code_folder_relay_broken, "send ft_file_begin failed");
        return false;
    }

    //4. 分片循环：窗口允许就读一片发一片，窗口满了就等回执
    std::vector<char> buf(kFolderRelayChunkBytes);
    for (;;)
    {
        if (ShouldStop())
            return false;

        int64_t offset = 0;
        size_t  len = 0;
        bool    drained = false;
        bool    ackTimeout = false;
        int64_t ackedAtTimeout = 0;
        {
            std::unique_lock<std::mutex> lock(ctx.m_mtx);
            if (ctx.m_window.IsDrained())
            {
                drained = true;
            }
            else if (!ctx.m_window.CanSendMore())
            {
                //窗口已满，或字节已全发完而回执尚未收齐：就地等回执
                const int64_t beforeAcked = ctx.m_window.AckedOffset();
                const std::cv_status st = ctx.m_cv.wait_for(
                    lock, std::chrono::seconds(kFolderSendChunkAckTimeoutSec));
                //整段时间里位点一点没动，判定链路或对端已经不工作了
                if (st == std::cv_status::timeout && ctx.m_window.AckedOffset() == beforeAcked)
                {
                    ackTimeout = true;
                    //位点要在锁内取好带出去：出了锁再读它就与接收线程撞上了
                    ackedAtTimeout = beforeAcked;
                }
            }
            else
            {
                offset = ctx.m_window.NextSendOffset();
                len = ctx.m_window.NextChunkBytes();
            }
        }

        if (drained)
            break;
        if (ackTimeout)
        {
            if (ShouldStop())
                return false;
            LOG_ERROR("FolderSendSession: chunk ack timeout, lane=%u, index=%lld, acked=%lld.",
                      static_cast<unsigned int>(laneSlot), static_cast<long long>(task.m_index),
                      static_cast<long long>(ackedAtTimeout));
            Fail(error_code_folder_relay_broken, "chunk ack timeout");
            return false;
        }
        //本轮只是被唤醒后重新判断，没有取到要发的片
        if (len == 0)
            continue;

        //读盘不持锁：一片 512 KiB 的读取不该把本通道的回执处理挡在门外
        if (!readFileChunkAt(in, offset, &buf[0], len))
        {
            if (ShouldStop())
                return false;
            LOG_ERROR("FolderSendSession: read source file failed, lane=%u, index=%lld, offset=%lld, len=%u.",
                      static_cast<unsigned int>(laneSlot), static_cast<long long>(task.m_index),
                      static_cast<long long>(offset), static_cast<unsigned int>(len));
            Fail(error_code_folder_read_fail, "read source file failed");
            return false;
        }

        std::string chunkFrame;
        if (!relayframing::BuildFileChunkFrame(task.m_index, offset,
                                               reinterpret_cast<const unsigned char*>(&buf[0]),
                                               len, chunkFrame))
        {
            Fail(error_code_bad_request, "build ft_file_chunk failed");
            return false;
        }

        //记账必须排在真正写 socket 之前，否则这一片的回执可能先到，窗口里却查不到它的
        //在途记录，会被误判成"对方回退位点"
        bool stale = false;
        {
            std::lock_guard<std::mutex> guard(ctx.m_mtx);
            if (ctx.m_window.NextSendOffset() != offset)
                stale = true;
            else
                ctx.m_window.OnChunkSent(len);
        }
        //读盘期间对方回退了位点：这一片读到的内容已经作废，回到循环重新取
        if (stale)
            continue;

        if (!ctx.m_client.SendAppFrame(reinterpret_cast<const unsigned char*>(chunkFrame.data()),
                                       chunkFrame.size()))
        {
            LOG_ERROR("FolderSendSession: send ft_file_chunk failed, lane=%u, index=%lld, offset=%lld.",
                      static_cast<unsigned int>(laneSlot), static_cast<long long>(task.m_index),
                      static_cast<long long>(offset));
            Fail(error_code_folder_relay_broken, "send ft_file_chunk failed");
            return false;
        }
    }

    //5. 字节全部被确认：发 ft_file_end，告诉接收端可以做校验了
    std::string endFrame;
    if (!relayframing::BuildFileEndFrame(task.m_index, endFrame))
    {
        Fail(error_code_bad_request, "build ft_file_end failed");
        return false;
    }
    if (!ctx.m_client.SendAppFrame(reinterpret_cast<const unsigned char*>(endFrame.data()),
                                   endFrame.size()))
    {
        Fail(error_code_folder_relay_broken, "send ft_file_end failed");
        return false;
    }

    //6. 等接收端的校验结果
    bool ready = false;
    int resultCode = kFileResultCodeOk;
    std::string resultMsg;
    {
        std::unique_lock<std::mutex> lock(ctx.m_mtx);
        while (!ctx.m_resultReady && !m_stop.load())
        {
            const std::cv_status st = ctx.m_cv.wait_for(
                lock, std::chrono::seconds(kFolderSendFileResultTimeoutSec));
            if (st == std::cv_status::timeout && !ctx.m_resultReady)
                break;
        }
        ready = ctx.m_resultReady;
        resultCode = ctx.m_resultCode;
        resultMsg = ctx.m_resultMsg;
        ctx.m_fileIndex = kLaneIdleFileIndex;
    }

    if (!ready)
    {
        if (ShouldStop())
            return false;
        LOG_ERROR("FolderSendSession: file result timeout, lane=%u, index=%lld.",
                  static_cast<unsigned int>(laneSlot), static_cast<long long>(task.m_index));
        Fail(error_code_folder_relay_broken, "file result timeout");
        return false;
    }
    if (resultCode != kFileResultCodeOk)
    {
        LOG_ERROR("FolderSendSession: receiver rejected file, lane=%u, index=%lld, code=%d, msg=%s.",
                  static_cast<unsigned int>(laneSlot), static_cast<long long>(task.m_index),
                  resultCode, resultMsg.c_str());
        //失败原因以接收端给的为准，它比发送端更清楚是写盘失败还是内容校验不符
        Fail(resultCode, resultMsg.empty() ? std::string("receiver rejected file") : resultMsg);
        return false;
    }
    return true;
}

bool FolderSendSession::ComputeFileMd5(const std::wstring& path, std::string& md5out)
{
    md5out.clear();

    std::ifstream in(path.c_str(), std::ios::binary);
    if (!in)
        return false;

    MD5_CTX ctx;
    MD5Init(&ctx);

    std::vector<char> buf(kFolderSendMd5ReadBufBytes);
    while (in)
    {
        //取消是在分片边界生效的，超大文件的 MD5 一次要扫好几分钟，这里也得给取消留出口
        if (ShouldStop())
            return false;

        in.read(&buf[0], static_cast<std::streamsize>(buf.size()));
        const std::streamsize got = in.gcount();
        if (got > 0)
        {
            MD5Update(&ctx, reinterpret_cast<const unsigned char*>(&buf[0]),
                      static_cast<unsigned int>(got));
        }
    }
    //读到文件尾是正常结束（eofbit 不算 bad），只有 badbit 才是真的读盘出错
    if (in.bad())
        return false;

    unsigned char digest[kMd5DigestBytes] = { 0 };
    MD5Final(digest, &ctx);

    char hex[kMd5HexChars + 1] = { 0 };
    for (int i = 0; i < kMd5DigestBytes; ++i)
        sprintf_s(hex + i * 2, kMd5HexPerByteBufBytes, "%02x", digest[i]);
    md5out.assign(hex, kMd5HexChars);
    return true;
}

// ======================== 收帧路径（在各通道的接收线程上） ========================

void FolderSendSession::OnLaneFrame(size_t laneSlot, const unsigned char* data, size_t len)
{
    FolderRelayInboundFrame frame;
    if (!relayframing::DecodeFrame(data, len, frame))
    {
        LOG_ERROR("FolderSendSession: decode app frame failed, lane=%u, len=%u.",
                  static_cast<unsigned int>(laneSlot), static_cast<unsigned int>(len));
        Fail(error_code_bad_request, "decode app frame failed");
        return;
    }

    switch (frame.m_type)
    {
    //目录就绪回执：接收端把整棵目录树建好之后才回，回执里带每个文件的续传起点
    case ft_manifest_ack:
        OnManifestAckFrame(frame.m_manifestAck);
        break;

    //分片回执：既是流控信号，也是权威续传位点
    case ft_chunk_ack:
        OnChunkAckFrame(laneSlot, frame.m_chunkAck);
        break;

    //某个文件的落盘与校验结果，收到才算这个文件真的传成功
    case ft_file_result:
        OnFileResultFrame(laneSlot, frame.m_fileResult);
        break;

    //对端中止：整个文件夹传输失败，原因以对端给的为准
    case ft_abort:
        OnAbortFrame(frame.m_abort);
        break;

    //以下几种都是发送端自己该发的帧，收到说明对端实现有误。记一条日志后忽略，不因此
    //中止传输 —— 真出了错对端会另发 ft_abort
    case ft_manifest:
    case ft_file_begin:
    case ft_file_chunk:
    case ft_file_end:
    case ft_complete:
        LOG_WARNING("FolderSendSession: unexpected frame type %d from receiver, lane=%u.",
                    static_cast<int>(frame.m_type), static_cast<unsigned int>(laneSlot));
        break;

    //帧类型未知：解码器已经保证不会走到这里，留一条日志兜底
    default:
        LOG_WARNING("FolderSendSession: unknown frame type %d, lane=%u.",
                    static_cast<int>(frame.m_type), static_cast<unsigned int>(laneSlot));
        break;
    }
}

void FolderSendSession::OnManifestAckFrame(const FolderRelayManifestAck& ack)
{
    LOG_INFO("FolderSendSession: ft_manifest_ack received, totalfiles=%lld, resume points=%u.",
             static_cast<long long>(ack.m_totalfiles),
             static_cast<unsigned int>(ack.m_points.size()));

    std::lock_guard<std::mutex> guard(m_manifestMtx);
    //重复的回执一律忽略：续传位点只认第一份，否则已经开传的文件会被改掉起点
    if (m_manifestAckReady)
    {
        LOG_WARNING("FolderSendSession: duplicated ft_manifest_ack ignored.");
        return;
    }
    m_manifestAck = ack;
    m_manifestAckReady = true;
    m_manifestCv.notify_all();
}

void FolderSendSession::OnChunkAckFrame(size_t laneSlot, const FolderRelayChunkAck& ack)
{
    if (laneSlot >= m_lanes.size())
        return;
    LaneContext& ctx = *m_lanes[laneSlot];

    bool stale = false;
    bool invalid = false;
    bool repositioned = false;
    int64_t delta = 0;
    {
        std::lock_guard<std::mutex> guard(ctx.m_mtx);
        //序号对不上：多半是上一个文件的迟到回执，丢弃即可，不能拿它去动当前文件的窗口
        if (ctx.m_fileIndex != ack.m_index)
        {
            stale = true;
        }
        else
        {
            const int64_t before = ctx.m_window.AckedOffset();
            const FolderSendAckResult result = ctx.m_window.OnChunkAck(ack.m_nextoffset);
            if (result == kFolderSendAckInvalid)
            {
                invalid = true;
            }
            else
            {
                repositioned = (result == kFolderSendAckRepositioned);
                delta = ctx.m_window.AckedOffset() - before;
            }
        }
    }
    //唤醒本通道等窗口的工作线程。放在锁外通知，避免它醒来后又立刻卡在这把锁上
    ctx.m_cv.notify_all();

    if (stale)
    {
        LOG_WARNING("FolderSendSession: stale chunk ack ignored, lane=%u, index=%lld.",
                    static_cast<unsigned int>(laneSlot), static_cast<long long>(ack.m_index));
        return;
    }
    if (invalid)
    {
        LOG_ERROR("FolderSendSession: chunk ack offset out of range, lane=%u, index=%lld, nextoffset=%lld.",
                  static_cast<unsigned int>(laneSlot), static_cast<long long>(ack.m_index),
                  static_cast<long long>(ack.m_nextoffset));
        Fail(error_code_bad_request, "chunk ack offset out of range");
        return;
    }
    if (repositioned)
    {
        LOG_WARNING("FolderSendSession: receiver repositioned, lane=%u, index=%lld, resend from %lld.",
                    static_cast<unsigned int>(laneSlot), static_cast<long long>(ack.m_index),
                    static_cast<long long>(ack.m_nextoffset));
    }

    if (delta != 0)
    {
        m_sentBytes.fetch_add(delta);
        ReportProgress(false, "");
    }
}

void FolderSendSession::OnFileResultFrame(size_t laneSlot, const FolderRelayFileResult& result)
{
    if (laneSlot >= m_lanes.size())
        return;
    LaneContext& ctx = *m_lanes[laneSlot];

    bool stale = false;
    {
        std::lock_guard<std::mutex> guard(ctx.m_mtx);
        //序号对不上：同样是迟到的结果，丢弃，不能让它把当前文件的等待点提前唤醒
        if (ctx.m_fileIndex != result.m_index)
        {
            stale = true;
        }
        else
        {
            ctx.m_resultCode = result.m_code;
            ctx.m_resultMsg = result.m_msg;
            ctx.m_resultReady = true;
        }
    }
    ctx.m_cv.notify_all();

    if (stale)
    {
        LOG_WARNING("FolderSendSession: stale file result ignored, lane=%u, index=%lld.",
                    static_cast<unsigned int>(laneSlot), static_cast<long long>(result.m_index));
        return;
    }
    LOG_INFO("FolderSendSession: ft_file_result received, lane=%u, index=%lld, code=%d.",
             static_cast<unsigned int>(laneSlot), static_cast<long long>(result.m_index),
             result.m_code);
}

void FolderSendSession::OnAbortFrame(const FolderRelayAbort& abort)
{
    LOG_ERROR("FolderSendSession: ft_abort received from receiver, errcode=%d, errmsg=%s.",
              abort.m_errcode, abort.m_errmsg.c_str());
    //对端没给出错误码时归入"通道不工作"，否则界面上会显示一个含义为 0 的码
    const int errcode = (abort.m_errcode != 0) ? abort.m_errcode : error_code_folder_relay_broken;
    Fail(errcode, abort.m_errmsg.empty() ? std::string("aborted by receiver") : abort.m_errmsg);
}

void FolderSendSession::OnLaneClosed(size_t laneSlot)
{
    LOG_ERROR("FolderSendSession: relay lane closed unexpectedly, lane=%u.",
              static_cast<unsigned int>(laneSlot));
    //按方案定稿不做自动重连：任一条通道断开即整个文件夹传输失败，由用户决定是否手动重试
    Fail(error_code_folder_relay_broken, "relay lane closed");
}

// ======================== 失败、收尾与进度 ========================

void FolderSendSession::Fail(int errcode, const std::string& errmsg)
{
    {
        std::lock_guard<std::mutex> guard(m_stateMtx);
        //只记第一个原因：后面那些多半是第一个原因引发的连锁反应，报给用户没有意义
        if (m_failed)
            return;
        m_failed = true;
        m_errcode = errcode;
        m_errmsg = errmsg;
    }

    m_stop.store(true);
    LOG_ERROR("FolderSendSession: fail, errcode=%d, errmsg=%s.", errcode, errmsg.c_str());

    //两个等待点都要叫醒。持锁通知是刻意的：不持锁的话，恰好在"判完标志、还没进入等待"
    //那一瞬间发出的通知会丢失，等待方要一直睡到超时才发现该收工了
    {
        std::lock_guard<std::mutex> guard(m_manifestMtx);
        m_manifestCv.notify_all();
    }
    WakeAllLanes();
}

void FolderSendSession::SendAbort()
{
    if (m_lanes.empty())
        return;
    if (!m_lanes[kControlLaneSlot]->m_client.IsConnected())
    {
        LOG_WARNING("FolderSendSession: control lane is down, ft_abort not sent (signaling path will cover it).");
        return;
    }

    FolderRelayAbort abort;
    {
        std::lock_guard<std::mutex> guard(m_stateMtx);
        abort.m_errcode = m_errcode;
        abort.m_errmsg = m_errmsg;
    }

    std::string frame;
    if (!relayframing::BuildAbortFrame(abort, frame))
    {
        LOG_ERROR("FolderSendSession: build ft_abort failed, errcode=%d.", abort.m_errcode);
        return;
    }
    if (!m_lanes[kControlLaneSlot]->m_client.SendAppFrame(
            reinterpret_cast<const unsigned char*>(frame.data()), frame.size()))
    {
        LOG_ERROR("FolderSendSession: send ft_abort failed, errcode=%d.", abort.m_errcode);
        return;
    }
    LOG_INFO("FolderSendSession: ft_abort sent, errcode=%d, errmsg=%s.",
             abort.m_errcode, abort.m_errmsg.c_str());
}

void FolderSendSession::CloseAllLanes()
{
    for (size_t i = 0; i < m_lanes.size(); ++i)
        m_lanes[i]->m_client.Close();
    LOG_INFO("FolderSendSession: all relay lanes closed, total=%u.",
             static_cast<unsigned int>(m_lanes.size()));
}

void FolderSendSession::WakeAllLanes()
{
    for (size_t i = 0; i < m_lanes.size(); ++i)
    {
        std::lock_guard<std::mutex> guard(m_lanes[i]->m_mtx);
        m_lanes[i]->m_cv.notify_all();
    }
}

void FolderSendSession::ReportProgress(bool force, const std::string& currentFile)
{
    if (m_handler == NULL)
        return;

    {
        std::lock_guard<std::mutex> guard(m_stateMtx);
        if (!currentFile.empty())
            m_currentFile = currentFile;
    }

    {
        std::lock_guard<std::mutex> guard(m_progressMtx);
        const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
        if (!force)
        {
            const long long elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - m_lastProgressAt).count();
            //回调太密会把 UI 线程的消息队列塞满，而人眼也分辨不出更密的刷新
            if (elapsedMs < kFolderSendProgressIntervalMs)
                return;
        }
        m_lastProgressAt = now;
    }

    FolderSendProgress progress;
    progress.m_sentBytes = m_sentBytes.load();
    progress.m_totalBytes = m_queue.TotalBytes();
    progress.m_finishedFiles = m_finishedFiles.load();
    progress.m_totalFiles = static_cast<int64_t>(m_queue.TotalCount());
    {
        std::lock_guard<std::mutex> guard(m_stateMtx);
        progress.m_currentFile = m_currentFile;
    }
    m_handler->OnFolderSendProgress(progress);
}

std::wstring FolderSendSession::MakeLocalPath(const std::string& relpath) const
{
    std::wstring wideRel;
    if (!Utf8ToUtf16(relpath, wideRel))
    {
        LOG_WARNING("FolderSendSession: relative path is not valid utf-8, path=%s.", relpath.c_str());
    }
    //清单里一律是正斜杠，本地路径要换回反斜杠 —— 加了长路径前缀的路径不再经系统规范化，
    //留着正斜杠会打不开
    for (size_t i = 0; i < wideRel.size(); ++i)
    {
        if (wideRel[i] == L'/')
            wideRel[i] = L'\\';
    }

    std::wstring full = m_params.m_rootPath;
    while (!full.empty() && (full[full.size() - 1] == L'\\' || full[full.size() - 1] == L'/'))
        full.erase(full.size() - 1);
    full += L"\\";
    full += wideRel;
    return MakeLongPath(full);
}

bool FolderSendSession::ShouldStop() const
{
    return m_stop.load();
}

} // namespace foldertransfer
