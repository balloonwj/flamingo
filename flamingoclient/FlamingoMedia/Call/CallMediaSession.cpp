/**
 *  音视频通话中转会话实现，CallMediaSession.cpp
 *  balloonwj@qq.com   2026-06-11（2026-06-12 接入真实音频；2026-06-16 接入视频多路复用；2026-10-06 共享电脑声音）
 */
#include "CallMediaSession.h"

#include <mfapi.h>               //MFStartup / MFShutdown（视频解码线程初始化 MF 平台）
#include <utility>               //std::move（视频包入队 / 出队转移所有权）
#include <stdlib.h>              //getenv_s（调试版「不用声音设备」开关）
#include <string.h>              //strcmp
#include <chrono>                //统计线程按固定周期醒来

#include "../../Source/Common/Log/IULog.h"            //LOG_INFO / LOG_ERROR
//事件消息 id（FMG_MSG_CALL_RELAY_EVENT）由 Start 的 notifyMsg 参数传入，故本 .cpp
//无需 include MFC 重头文件 UserSessionData.h（它依赖 CString / _T，非 PCH 文件包含会编译失败）。

#pragma comment(lib, "mfplat.lib")

namespace call {

//连接 + 握手总超时（秒）：接通后中转一般在同机 / 局域网，握手应近乎瞬时；留 5 秒兜底。
static const int kConnectTimeoutSec = 5;

//视频编码参数：帧率与目标码率。720p 实时通话用 30fps / 2Mbps，兼顾清晰度与带宽。
static const int kVideoFps     = 30;
static const int kVideoBitrate = 2000000;

//视频帧切片大小（字节）：单个中转 body 的负载上限。中转单帧上限 MAX_BODY_FILESERVER=2MiB，
//取 512KiB 远低于上限（加 2 字节封装头后仍 << 2MiB），H.264 关键帧超此值时切成多片重组。
static const size_t kVideoChunkSize = 512 * 1024;

//1 毫秒的 100ns 计数（会话时间戳是毫秒，喂解码器要 100ns，故 ×此值换算）。
static const int64_t kHnsPerMs = 10000LL;
//一个音频帧的时长（毫秒）：音频引擎按 48 kHz、每帧 960 个采样编码。发送端的媒体时间戳 = 采集帧序号 × 本值
static const uint64_t kCallAudioFrameMs = 20;
//每毫秒的采样数（48 kHz）：接收端把毫秒时间戳换算成抖动缓冲用的采样时间戳
static const uint64_t kCallSamplesPerMs = 48;

#ifdef _DEBUG
//调试版「通话不用声音设备」开关的环境变量名：值为 1 时音频引擎不打开麦克风与扬声器，编解码与收发照常。
//用于在开发机上联调通话（同一台机器上两个客户端互相呼叫），不碰真实的麦克风、不从扬声器放声音。
static const char* const kNoAudioDeviceEnv = "FLAMINGO_CALL_NO_AUDIO";

//是否打开了「通话不用声音设备」开关
static bool UseNoAudioDevice()
{
    char value[8] = { 0 };
    size_t len = 0;
    if (getenv_s(&len, value, sizeof(value), kNoAudioDeviceEnv) != 0 || len == 0)
        return false;
    return strcmp(value, "1") == 0;
}
#endif

CallMediaSession::CallMediaSession()
    : m_displaySink(NULL)
    , m_videoDecodeStop(false)
    , m_clockStartMs(0)
    , m_hwndNotify(NULL)
    , m_notifyMsg(0)
    , m_role(0)
    , m_enableVideo(false)
    , m_cameraOn(false)
    , m_running(false)
    , m_micWanted(true)
    , m_path(call_media_path_none)
    , m_audioSeq(0)
    , m_videoSeq(0)
    , m_videoGuardReset(false)
    , m_p2pSendFailures(0)
    , m_videoSent(0)
    , m_videoRecv(0)
    , m_statsStop(true)
    , m_lastAudioSent(0)
    , m_lastVideoSent(0)
    , m_lastVideoRecv(0)
    , m_lastPlayRequested(0)
    , m_lastPlayConcealed(0)
    , m_reportSeq(0)
    , m_tickPlayRequested(0)
    , m_tickPlayConcealed(0)
    , m_logRecv(0)
    , m_logLost(0)
    , m_logLate(0)
    , m_myGrade(kCallGradeUnknown)
    , m_hasMyWindow(false)
    , m_myLossPermille(0)
    , m_myJitterMs(0)
    , m_myStallPermille(0)
    , m_myBufferMs(0)
    , m_sysOn(false)
    , m_sysExcludingSelf(false)
    , m_sysSeq(0)
    , m_sysSent(0)
    , m_sysRecv(0)
    , m_lastSysSent(0)
    , m_lastSysRecv(0)
    , m_peerSysSharing(false)
{
}

CallMediaSession::~CallMediaSession()
{
    Stop();
}

void CallMediaSession::Start(const std::string& ip, int port, const std::string& token, const unsigned char* serverPubKey,
                             int role, const std::string& sessionId, HWND hwndNotify, UINT notifyMsg,
                             bool enableVideo, const std::shared_ptr<balloonp2p::P2PLinkSlot>& p2pSlot)
{
    //幂等：清掉上一会话（若有），再起新会话
    Stop();

    //上一会话的数据报通道此时已关闭、没有线程再用它，可以销毁；P2P 的各项状态从头开始
    m_datagram.reset();
    m_p2pSlot = p2pSlot;
    m_path.store(call_media_path_none);
    m_audioSeq = 0;
    m_videoSeq = 0;
    m_videoGuard.Reset();
    m_keyLimiter.Reset();
    m_videoGuardReset.store(false);
    m_p2pSendFailures.store(0);
    //每通电话从「麦克风打开」开始（Stop 已 join 连接线程，此刻没有别的线程读它）
    m_micWanted.store(true);

    //复制服务端公钥：连接线程稍后才用到它，调用方的指针只保证在本次调用期间有效
    if (serverPubKey != NULL)
        m_serverPubKey.assign(serverPubKey, serverPubKey + net::IDENTITY_PUBLICKEY_BYTES);
    else
        m_serverPubKey.clear();

    m_hwndNotify = hwndNotify;
    m_notifyMsg = notifyMsg;
    m_role = role;
    m_sessionId = sessionId;
    m_enableVideo = enableVideo;
    m_demux.Reset();
    //会话时钟起点：之后音 / 视频包都打「当前 - 此起点」毫秒戳，作 A/V 同步的统一时间轴
    m_clockStartMs = ::GetTickCount64();
    m_running.store(true);

    LOG_INFO("CallMediaSession: start relay session, session=%s, role=%d, relay %s:%d, video=%d, p2p offered=%d.",
             m_sessionId.c_str(), m_role, ip.c_str(), port, enableVideo ? 1 : 0, m_p2pSlot ? 1 : 0);

    //通话统计从头开始（上一会话的统计线程已在开头的 Stop 里停掉，此刻没有别的线程访问这些计数）
    {
        std::lock_guard<std::mutex> guard(m_recvStatsMtx);
        m_recvStats.Reset(static_cast<int64_t>(::GetTickCount64()));
        m_reportX.Reset();
        m_myGrade = kCallGradeUnknown;
        m_hasMyWindow = false;
        m_myLossPermille = 0;
        m_myJitterMs = 0;
        m_myStallPermille = 0;
        m_myBufferMs = 0;
        m_peerSysSharing = false;
    }
    m_gradeSmoother.Reset();
    //共享电脑声音的计数从头开始（开头的 Stop 已关掉上一通电话的共享）
    m_sysSeq = 0;
    m_sysSent.store(0);
    m_sysRecv.store(0);
    m_lastSysSent = 0;
    m_lastSysRecv = 0;
    m_tickStreamStats = AudioStreamStats();
    m_logStreamStats = AudioStreamStats();
    m_reportSeq = 0;
    m_tickPlayRequested = 0;
    m_tickPlayConcealed = 0;
    m_logRecv = 0;
    m_logLost = 0;
    m_logLate = 0;
    m_videoSent.store(0);
    m_videoRecv.store(0);
    m_lastAudioSent = 0;
    m_lastVideoSent = 0;
    m_lastVideoRecv = 0;
    m_lastPlayRequested = 0;
    m_lastPlayConcealed = 0;
    {
        std::lock_guard<std::mutex> guard(m_statsMtx);
        m_statsStop = false;
    }
    m_statsThread.reset(new std::thread(&CallMediaSession::StatsProc, this));

    //在后台线程做阻塞连接 + 握手，避免卡住 UI 线程（Start 由 UI 线程调用）
    m_connectThread.reset(new std::thread(&CallMediaSession::ConnectProc, this, ip, port, token));
}

void CallMediaSession::SetVideoDisplaySink(CallVideoDisplaySink* sink)
{
    //仅指针赋值，调用方保证 sink 生命周期；解码线程读 m_displaySink 是非原子的，但显示 sink
    //的注入约定在 Start 前（通话窗准备阶段）完成、Stop 后才解绑，故无并发写读冲突。
    m_displaySink = sink;
}

void CallMediaSession::Stop()
{
    //幂等：仅首次（running:true→false）真正执行收尾
    if (!m_running.exchange(false))
        return;

    //0) 先停统计线程：它只读计数与引擎状态，停在最前面，之后的收尾不必考虑它
    StopStatsThread();
    //0b) 再停共享电脑声音：join 回环采集线程后不再有电脑声音发往中转或 P2P
    stopSystemAudio();

    //收尾顺序很重要：
    //1) 先关中转连接——join 接收线程后再无 OnRelayData，不再把音频喂引擎解码、不再把视频入队。
    //   若连接线程仍阻塞在 Connect，Close 也会促其返回。
    m_relay.Close();
    //2) 再 join 后台连接线程——其中可能正在执行引擎 Start()，join 后该 Start 已完成，
    //   引擎不再被连接线程并发触碰。
    if (m_connectThread && m_connectThread->joinable())
        m_connectThread->join();
    m_connectThread.reset();
    //2b) 再关 P2P 数据报通道——join 它的转交线程后再无 OnDatagram；随后以「业务结束」关闭链路（服务端随之结束
    //    P2P 会话）。通道对象留到下次 Start 再销毁：采集线程可能正停在 Send 里
    m_path.store(call_media_path_none);
    if (m_datagram)
        m_datagram->Close();
    if (m_p2pSlot)
        m_p2pSlot->CloseLink();
    //3) 停并 join 视频解码线程——之后视频解码器不再被任何线程访问。
    if (m_videoDecodeThread)
    {
        {
            std::lock_guard<std::mutex> guard(m_videoQueueMtx);
            m_videoDecodeStop.store(true);
        }
        m_videoQueueCv.notify_all();
        if (m_videoDecodeThread->joinable())
            m_videoDecodeThread->join();
        m_videoDecodeThread.reset();
    }
    //清空残留待解码视频包
    {
        std::lock_guard<std::mutex> guard(m_videoQueueMtx);
        m_videoInQueue.clear();
    }
    //4) 最后停音视频引擎——停采集 / 播放、释放 codec。此时无任何线程并发访问引擎，安全。
    m_audioEngine.Stop();
    m_videoEngine.Stop();
    m_cameraOn.store(false);

    LOG_INFO("CallMediaSession: stopped relay session, session=%s.", m_sessionId.c_str());
}

bool CallMediaSession::EnableVideo(bool on)
{
    //幂等：与当前状态相同直接返回，避免重复起 / 停线程。
    if (m_enableVideo == on)
    {
        return true;
    }

    if (on)
    {
        //P2P 路径：重新开始收视频时先等关键帧（转交线程据标志重置丢帧状态）
        m_videoGuardReset.store(true);
        //先起解码线程再让引擎开始接收，与 Start 里的次序一致：确保对端早到的视频帧不被丢。
        m_videoDecodeStop.store(false);
        m_videoDecodeThread.reset(new std::thread(&CallMediaSession::VideoDecodeProc, this));
        m_videoEngine.StartReceive(this);

        //本端摄像头不在这里打开（由调用方按需调 SetCameraEnabled）：切到视频时只有发起方开摄像头
        m_enableVideo = true;
        LOG_INFO("CallMediaSession: video receive enabled, session=%s.", m_sessionId.c_str());
        return true;
    }

    //关闭：次序与 Stop() 里的视频段完全一致 —— 先停并 join 解码线程（之后解码器不再被
    //任何线程访问），清空残留待解码包，最后停视频引擎。中转连接与音频引擎原样不动。
    if (m_videoDecodeThread)
    {
        {
            std::lock_guard<std::mutex> guard(m_videoQueueMtx);
            m_videoDecodeStop.store(true);
        }
        m_videoQueueCv.notify_all();
        if (m_videoDecodeThread->joinable())
            m_videoDecodeThread->join();
        m_videoDecodeThread.reset();
    }
    {
        std::lock_guard<std::mutex> guard(m_videoQueueMtx);
        m_videoInQueue.clear();
    }
    SetCameraEnabled(false);
    m_videoEngine.StopReceive();

    m_enableVideo = false;
    LOG_INFO("CallMediaSession: video disabled, session=%s.", m_sessionId.c_str());
    return true;
}

bool CallMediaSession::SetCameraEnabled(bool on)
{
    std::lock_guard<std::mutex> guard(m_cameraMtx);
    if (!on)
    {
        const bool wasOn = m_cameraOn.exchange(false);
        m_videoEngine.StopCamera();
        if (wasOn)
            LOG_INFO("CallMediaSession: local camera off, session=%s.", m_sessionId.c_str());
        return true;
    }
    if (!m_enableVideo)
    {
        LOG_WARNING("CallMediaSession: camera on requested before video enabled, ignored, session=%s.",
                    m_sessionId.c_str());
        return false;
    }
    if (m_cameraOn.load())
        return true;
    if (!m_videoEngine.StartCamera(kVideoFps, kVideoBitrate))
    {
        //摄像头开不起来（无设备 / 被占用）：接收不受影响，仍能看对方画面；把原因投给 UI 让通话窗提示
        const int reason = m_videoEngine.CameraFailReason();
        LOG_ERROR("CallMediaSession: open local camera failed, session=%s, reason=%d.", m_sessionId.c_str(), reason);
        PostEvent(call_relay_camera_unavailable, static_cast<unsigned int>(reason));
        return false;
    }
    m_cameraOn.store(true);
    LOG_INFO("CallMediaSession: local camera on, session=%s.", m_sessionId.c_str());
    return true;
}

bool CallMediaSession::SetMicEnabled(bool on)
{
    std::lock_guard<std::mutex> guard(m_audioCtlMtx);
    m_micWanted.store(on);
    //引擎还没启动（正在连接）或已停止：只记下状态，连接线程启动引擎时按它决定
    if (!m_audioEngine.IsRunning())
    {
        LOG_INFO("CallMediaSession: microphone %s requested before the audio engine runs, session=%s.",
                 on ? "on" : "off", m_sessionId.c_str());
        return true;
    }
    const bool ok = m_audioEngine.SetCaptureEnabled(on);
    if (!ok)
    {
        //打开失败（设备被拔掉、被独占）：麦克风保持关闭，界面据返回值显示关闭态
        m_micWanted.store(false);
        LOG_ERROR("CallMediaSession: turn microphone on failed, session=%s.", m_sessionId.c_str());
        return false;
    }
    LOG_INFO("CallMediaSession: microphone %s by user, session=%s.", on ? "on" : "off", m_sessionId.c_str());
    return true;
}

void CallMediaSession::ConnectProc(std::string ip, int port, std::string token)
{
    //有链路槽：先等 P2P 链路（多半在响铃期间已经打通），开数据报通道
    bool p2pOk = false;
    if (m_p2pSlot)
    {
        bool stopped = false;
        p2pOk = ConnectP2P(stopped);
        if (stopped)
            return;
    }

    if (p2pOk)
    {
        m_path.store(call_media_path_p2p);
        LOG_INFO("CallMediaSession: p2p connected, session=%s, role=%d.", m_sessionId.c_str(), m_role);
    }
    else
    {
        //阻塞连中转并握手；本类作为 handler 接收后续数据 / 断开回调
        const unsigned char* serverPubKey = m_serverPubKey.empty() ? NULL : &m_serverPubKey[0];
        bool ok = m_relay.Connect(ip, port, token, serverPubKey, this, kConnectTimeoutSec);
        if (!ok)
        {
            LOG_ERROR("CallMediaSession: relay connect/handshake failed, session=%s.", m_sessionId.c_str());
            PostEvent(call_relay_failed, 0);
            return;
        }
        m_path.store(call_media_path_relay);
        LOG_INFO("CallMediaSession: relay connected, session=%s, role=%d, p2p offered=%d.", m_sessionId.c_str(), m_role,
                 m_p2pSlot ? 1 : 0);
    }
    //路径已定：接收统计从此刻起算（等 P2P 链路、连中转的这段时间对方本来就没有消息，不算断流）
    {
        std::lock_guard<std::mutex> guard(m_recvStatsMtx);
        m_recvStats.Reset(static_cast<int64_t>(::GetTickCount64()));
    }
    PostEvent(call_relay_connected, 0);

    //起音频引擎：麦克风采集 + Opus 编码经 OnEncodedAudio 发中转；对端来的包经 OnRelayData
    //分流后喂引擎解码、抖动缓冲后播放。引擎启动失败（无设备 / 采集率非 Opus 支持）不影响信令
    //通话，仅记错误、本端无声。
    //真实通话启用 AEC + 降噪：避免扬声器放出的对端声音被麦克风拾回、再发回去形成回授啸叫。
    //麦克风按用户想要的状态打开（接通前后点过关麦的，这里就不打开采集）；与 SetMicEnabled 持同一把锁，
    //引擎启动期间 UI 线程的开关请求等它启动完再执行
    bool audioStarted = false;
    {
        std::lock_guard<std::mutex> guard(m_audioCtlMtx);
        const bool micOn = m_micWanted.load();
#ifdef _DEBUG
        if (UseNoAudioDevice())
        {
            //调试开关：不打开麦克风与扬声器，双方都听不到声音。记 WARNING 并提示界面，避免被误认为通话故障
            LOG_WARNING("CallMediaSession: FLAMINGO_CALL_NO_AUDIO=1, audio engine runs without devices; "
                        "neither side will hear anything, session=%s.", m_sessionId.c_str());
            audioStarted = m_audioEngine.StartWithoutDevices(this, true, micOn);
            if (audioStarted)
                PostEvent(call_relay_no_audio_devices, 0);
        }
        else
#endif
        {
            audioStarted = m_audioEngine.Start(this, true, micOn);
        }
        if (!micOn)
            LOG_INFO("CallMediaSession: microphone was turned off before the audio engine started, session=%s.",
                     m_sessionId.c_str());
    }
    if (!audioStarted)
        LOG_ERROR("CallMediaSession: audio engine start failed, session=%s; call will have no audio.",
                  m_sessionId.c_str());

    //视频通话：先起视频解码线程（接收线程把整帧入队、由它解码）并开始接收，再打开本端摄像头（采集 +
    //H.264 编码经 OnEncodedVideo 发中转）。先起解码线程可确保对端早到的视频帧不被丢。
    //摄像头开不起来（没有设备 / 被占用）时通话继续（音频照常、也仍能收看对端画面），SetCameraEnabled
    //会把失败原因投给 UI，让通话窗告诉用户，而不是只留一块黑屏。
    if (m_enableVideo)
    {
        m_videoDecodeStop.store(false);
        m_videoDecodeThread.reset(new std::thread(&CallMediaSession::VideoDecodeProc, this));
        m_videoEngine.StartReceive(this);
        SetCameraEnabled(true);
    }
}

bool CallMediaSession::ConnectP2P(bool& stopped)
{
    //分段等链路结果，每段之间检查会话是否已停止（Stop 在等本线程结束）
    stopped = false;
    const unsigned long long start = ::GetTickCount64();
    int state = balloonp2p::P2PLinkSlot::slot_waiting;
    while (true)
    {
        if (!m_running.load())
        {
            stopped = true;
            return false;
        }
        state = m_p2pSlot->Wait(kCallP2PWaitSliceMs);
        if (state != balloonp2p::P2PLinkSlot::slot_waiting)
            break;
        if (::GetTickCount64() - start >= (unsigned long long)kCallP2PLinkWaitMs)
            break;
    }
    const unsigned long long waited = ::GetTickCount64() - start;
    if (state != balloonp2p::P2PLinkSlot::slot_ready)
    {
        LOG_INFO("CallMediaSession: p2p link not available, session=%s, slot state=%d, waited=%llu ms, using relay.",
                 m_sessionId.c_str(), state, waited);
        return false;
    }
    m_datagram.reset(new balloonp2p::P2PDatagramChannel(m_p2pSlot, kCallDatagramQueueLimit));
    if (!m_datagram->Open(kCallP2PChannelId, this))
    {
        LOG_WARNING("CallMediaSession: open p2p datagram channel failed, session=%s, using relay.", m_sessionId.c_str());
        m_datagram->Close();
        return false;
    }
    LOG_INFO("CallMediaSession: path decided, session=%s, path=p2p, waited=%llu ms.", m_sessionId.c_str(), waited);
    return true;
}

void CallMediaSession::SendP2P(int kind, uint8_t flags, uint32_t seq, uint64_t tsMs, const uint8_t* data, size_t len)
{
    //发送失败只记日志：前几次各记一条，之后每隔这么多次记一条，避免刷屏
    static const uint64_t kFailLogEvery = 100;
    static const uint64_t kFailLogFirst = 5;
    std::vector<uint8_t> msg;
    BuildCallP2PMessage(kind, flags, seq, tsMs, data, len, msg);
    const int r = m_datagram->Send(msg.data(), msg.size());
    if (r == p2p::p2p_send_ok)
        return;
    const uint64_t n = ++m_p2pSendFailures;
    if (n <= kFailLogFirst || n % kFailLogEvery == 0)
        LOG_WARNING("CallMediaSession: p2p send failed, session=%s, kind=%d, len=%u, result=%d, failures=%llu.",
                    m_sessionId.c_str(), kind, (unsigned)msg.size(), r, (unsigned long long)n);
}

void CallMediaSession::EnqueueRemoteVideo(std::vector<uint8_t>& frame, uint64_t tsMs)
{
    VideoInPacket pkt;
    pkt.m_data = std::move(frame);
    pkt.m_tsMs = tsMs;
    std::lock_guard<std::mutex> guard(m_videoQueueMtx);
    m_videoInQueue.push_back(std::move(pkt));
    m_videoQueueCv.notify_one();
}

void CallMediaSession::OnDatagram(const unsigned char* data, size_t len)
{
    //数据报转交线程上下文：对端来的一条 P2P 消息
    CallP2PMessage m;
    if (!ParseCallP2PMessage(data, len, m))
    {
        LOG_WARNING("CallMediaSession: bad p2p message, len=%u, session=%s.", (unsigned)len, m_sessionId.c_str());
        return;
    }
    const int64_t now = (int64_t)::GetTickCount64();
    if (m_videoGuardReset.exchange(false))
        m_videoGuard.Reset();
    //通话统计：音频包按序号统计丢包与抖动（迟到、重复的包也交给它，由它计入迟到数）；其它消息只刷新断流计时
    if (m.m_kind == call_p2p_kind_audio)
        NoteRecvAudio(true, m.m_seq, m.m_tsMs, now);
    else
        NoteRecvOther(now);
    switch (m.m_kind)
    {
    //音频：按媒体时间戳交给播放端的抖动缓冲（2026-10-06 起，docs/音视频通话方案.md 第 9.9 节）。乱序、缺失与
    //丢包补偿都由抖动缓冲处理，这里不再为序号缺口补帧，也不丢弃晚到的包（抖动缓冲范围内的晚到包仍能按序播放）
    case call_p2p_kind_audio:
        m_audioEngine.OnEncodedPacket(m.m_payload, m.m_payloadLen,
                                      static_cast<uint32_t>(m.m_tsMs * kCallSamplesPerMs));
        break;

    //对方共享的电脑声音（2026-10-06）：进音频引擎的第 kSystemAudioStreamId 路，与说话声各自排队后混音；
    //不计入说话声的丢包与抖动统计
    case call_p2p_kind_system_audio:
        ++m_sysRecv;
        m_audioEngine.OnEncodedPacketFrom(kSystemAudioStreamId, m.m_payload, m.m_payloadLen,
                                          static_cast<uint32_t>(m.m_tsMs * kCallSamplesPerMs));
        break;

    //视频：按丢帧规则交给解码线程或丢弃；断档后请对方补关键帧
    case call_p2p_kind_video:
    {
        ++m_videoRecv;
        bool request = false;
        const bool keyframe = (m.m_flags & kCallP2PFlagKeyframe) != 0;
        if (m_videoGuard.OnFrame(m.m_seq, keyframe, now, request) == CallVideoLossGuard::video_decode)
        {
            std::vector<uint8_t> frame(m.m_payload, m.m_payload + m.m_payloadLen);
            EnqueueRemoteVideo(frame, m.m_tsMs);
        }
        if (request)
        {
            LOG_INFO("CallMediaSession: video frames lost, request key frame, session=%s, seq=%u, dropped=%llu.",
                     m_sessionId.c_str(), m.m_seq, (unsigned long long)m_videoGuard.DroppedFrames());
            SendP2P(call_p2p_kind_keyframe_request, 0, 0, 0, NULL, 0);
        }
        break;
    }

    //对端丢了视频帧，请本端出一个关键帧（限频）
    case call_p2p_kind_keyframe_request:
        if (m_keyLimiter.Allow(now))
        {
            LOG_INFO("CallMediaSession: peer requested a key frame, session=%s.", m_sessionId.c_str());
            m_videoEngine.RequestKeyFrame();
        }
        break;

    //对方的质量报告（网络状况标识第 2 期）：记下来并算往返时延；断流计时已在上面刷新
    case call_p2p_kind_stats:
        OnPeerReportBytes(m.m_payload, m.m_payloadLen, m.m_tsMs);
        break;

    //其它类型：解析时已过滤，不会走到这里
    default:
        break;
    }
    //仍在等关键帧时按间隔重发请求（请求本身也可能丢）；音频包每 20 毫秒一个，足以驱动这项检查
    if (m_videoGuard.PollRequest(now))
        SendP2P(call_p2p_kind_keyframe_request, 0, 0, 0, NULL, 0);
}

void CallMediaSession::OnDatagramClosed()
{
    //数据报转交线程上下文：P2P 链路断开（本端 Stop 关闭时不回调）
    LOG_INFO("CallMediaSession: p2p link closed unexpectedly, session=%s.", m_sessionId.c_str());
    PostEvent(call_relay_closed, 0);
}

void CallMediaSession::OnRelayData(const unsigned char* data, size_t len)
{
    //中转接收线程上下文：对端来的一个中转 body → 经 demux 分流。
    std::vector<uint8_t> payload;
    uint64_t tsMs = 0;
    CallMediaDemux::Result r = m_demux.Feed(data, len, payload, tsMs);
    const int64_t now = static_cast<int64_t>(::GetTickCount64());
    switch (r)
    {
    //音频包：在接收线程内联喂引擎解码（Opus 解码很快，不会拖累接收）。引擎未运行时内部丢弃。
    //中转路径走 TCP、音频包不带序号，通话统计只算到达数与抖动
    case CallMediaDemux::call_demux_audio:
        NoteRecvAudio(false, 0, tsMs, now);
        m_audioEngine.OnEncodedPacket(payload.data(), payload.size(), static_cast<uint32_t>(tsMs * kCallSamplesPerMs));
        break;
    //完整视频帧：连同时间戳入队交视频解码线程解码（H.264 解码偏慢，剥离出接收线程避免拖累音频）。
    case CallMediaDemux::call_demux_video:
        NoteRecvOther(now);
        ++m_videoRecv;
        EnqueueRemoteVideo(payload, tsMs);
        break;
    //视频分片累积中，未构成完整帧：只刷新断流计时（大的关键帧要好几片才收齐）
    case CallMediaDemux::call_demux_none:
        NoteRecvOther(now);
        break;
    //对方的质量报告（网络状况标识第 2 期）：刷新断流计时，记下报告并算往返时延
    case CallMediaDemux::call_demux_stats:
        NoteRecvOther(now);
        OnPeerReportBytes(payload.data(), payload.size(), tsMs);
        break;
    //对方共享的电脑声音（2026-10-06）：刷新断流计时，进音频引擎的第 kSystemAudioStreamId 路，不计入说话声的统计
    case CallMediaDemux::call_demux_system_audio:
        NoteRecvOther(now);
        ++m_sysRecv;
        m_audioEngine.OnEncodedPacketFrom(kSystemAudioStreamId, payload.data(), payload.size(),
                                          static_cast<uint32_t>(tsMs * kCallSamplesPerMs));
        break;
    //非法 body：demux 已重置视频重组状态，仅记日志
    case CallMediaDemux::call_demux_error:
    default:
        //非法 body：demux 已重置视频重组状态，仅记日志
        LOG_ERROR("CallMediaSession: demux error on relay body, len=%u, session=%s.",
                  static_cast<unsigned>(len), m_sessionId.c_str());
        break;
    }
}

void CallMediaSession::OnEncodedAudio(const unsigned char* data, size_t len, uint64_t frameIndex)
{
    //采集线程上下文：本端音频包 → 打「音频 / whole」标记 + 媒体时间戳封成中转 body → 发给对端。
    if (!m_running.load())
        return;
    //媒体时间戳（2026-10-06，docs/音视频通话方案.md 第 9.9.1 节）：采集帧序号 × 20 毫秒，相邻两帧恰好差 20。
    //原先取 GetTickCount64() − 起点，精度约 16 毫秒、与 20 毫秒的帧间隔对不齐，接收端的抖动缓冲无法据此排队
    const uint64_t tsMs = frameIndex * kCallAudioFrameMs;
    //P2P：一个音频包一条数据报，带序号供对端发现丢包
    if (m_path.load() == call_media_path_p2p)
    {
        SendP2P(call_p2p_kind_audio, 0, m_audioSeq++, tsMs, data, len);
        return;
    }
    std::vector<uint8_t> body;
    BuildAudioBody(data, len, tsMs, body);
    m_relay.SendData(body.data(), body.size());
}

void CallMediaSession::OnEncodedVideo(const uint8_t* data, size_t len, bool keyframe, int64_t /*ptsHns*/)
{
    //采集线程上下文：本端 H.264 帧 → 打「视频」标记并按 kVideoChunkSize 切片 → 逐片发给对端。
    //关键帧可能较大、超过中转单帧上限，故须切片；接收端按 whole/more/last 重组成整帧。
    if (!m_running.load())
        return;
    uint64_t tsMs = ::GetTickCount64() - m_clockStartMs;
    ++m_videoSent;
    //P2P：一整帧一条数据报（DLL 按路径分片），带序号与关键帧标志供对端判断丢帧
    if (m_path.load() == call_media_path_p2p)
    {
        SendP2P(call_p2p_kind_video, keyframe ? kCallP2PFlagKeyframe : 0, m_videoSeq++, tsMs, data, len);
        return;
    }
    std::vector<std::vector<uint8_t> > bodies;
    BuildVideoBodies(data, len, kVideoChunkSize, tsMs, bodies);
    for (size_t i = 0; i < bodies.size(); ++i)
    {
        if (!m_relay.SendData(bodies[i].data(), bodies[i].size()))
            break;   //发送失败（连接断 / 超限）：放弃本帧剩余分片，等下一帧
    }
}

void CallMediaSession::OnLocalVideoFrame(const uint8_t* nv12, int width, int height, int stride)
{
    //采集线程上下文：本端采集帧（编码前）→ 转投显示 sink 作本地预览。未注入 sink 时丢弃。
    if (m_displaySink != NULL)
        m_displaySink->OnLocalVideoFrame(nv12, width, height, stride);
}

void CallMediaSession::OnRemoteVideoFrame(const uint8_t* nv12, int width, int height, int stride)
{
    //视频解码线程上下文：远端解码帧 → 转投显示 sink（通话窗）。未注入 sink 时丢弃（如尚无 UI）。
    if (m_displaySink != NULL)
        m_displaySink->OnRemoteVideoFrame(nv12, width, height, stride);
}

void CallMediaSession::OnLocalVideoUnavailable(int reason)
{
    //采集线程上下文：本端摄像头中途没了。通话不中断，只把原因投给 UI 线程提示用户。
    LOG_ERROR("CallMediaSession: local camera unavailable, session=%s, reason=%d.",
              m_sessionId.c_str(), reason);
    PostEvent(call_relay_camera_unavailable, static_cast<unsigned int>(reason));
}

void CallMediaSession::VideoDecodeProc()
{
    //视频解码线程：H.264 解码器（MfH264Decoder）MFT 须在使用它的线程上创建 / 使用，故本线程
    //先初始化 COM（MTA）+ MF 平台，再循环取队列里的远端帧喂引擎解码（引擎在首帧时于本线程懒
    //创建解码器）。退出前 MFShutdown + CoUninitialize 与之配平。
    HRESULT hrCo = ::CoInitializeEx(NULL, COINIT_MULTITHREADED);
    bool comInited = SUCCEEDED(hrCo);
    HRESULT hrMf = ::MFStartup(MF_VERSION, MFSTARTUP_LITE);
    bool mfInited = SUCCEEDED(hrMf);
    if (!mfInited)
        LOG_ERROR("CallMediaSession: video decode thread MFStartup failed, hr=0x%08X.", static_cast<unsigned>(hrMf));

    while (true)
    {
        VideoInPacket pkt;
        {
            std::unique_lock<std::mutex> lock(m_videoQueueMtx);
            //手写谓词等待（不用 lambda）：被唤醒后须复查条件，防虚假唤醒
            while (!m_videoDecodeStop.load() && m_videoInQueue.empty())
                m_videoQueueCv.wait(lock);
            if (m_videoDecodeStop.load() && m_videoInQueue.empty())
                break;
            pkt = std::move(m_videoInQueue.front());
            m_videoInQueue.pop_front();
        }

        //用帧自带的会话时间戳（毫秒→100ns）喂解码器：既是合法单调 pts，又把同步时间轴带到
        //解码侧，供后续 A/V 同步呈现使用（解码出帧的 pts 即此值）。
        int64_t pts = static_cast<int64_t>(pkt.m_tsMs) * kHnsPerMs;
        m_videoEngine.OnRemoteEncodedVideo(pkt.m_data.data(), pkt.m_data.size(), pts);
    }

    if (mfInited)
        ::MFShutdown();
    if (comInited)
        ::CoUninitialize();
}

void CallMediaSession::OnRelayClosed()
{
    //中转接收线程上下文：异常断开（CallRelayClient 仅在非主动关闭时回调本方法）
    LOG_INFO("CallMediaSession: relay closed unexpectedly, session=%s.", m_sessionId.c_str());
    PostEvent(call_relay_closed, 0);
}

void CallMediaSession::PostEvent(int eventCode, unsigned int arg)
{
    //已 Stop 则不再投递，避免给正在收尾的 UI 投滞后事件
    if (::IsWindow(m_hwndNotify) && m_notifyMsg != 0 && m_running.load())
        ::PostMessage(m_hwndNotify, m_notifyMsg,
                      static_cast<WPARAM>(eventCode), static_cast<LPARAM>(arg));
}

// ======================== 通话统计（网络状况标识第 1 期）========================

namespace {

//千分比的分母
const uint64_t kStatsPermille = 1000;
//质量报告里 16 位字段的上限
const uint32_t kStatsU16Max = 0xFFFF;

//路径的日志名
const char* PathName(int path)
{
    switch (path)
    {
    //P2P 链路上的数据报通道
    case call_media_path_p2p:
        return "p2p";
    //imgserver 中转
    case call_media_path_relay:
        return "relay";
    //还没连上或已停止
    default:
        return "none";
    }
}

//两次累计值之差；累计值被清零过（引擎重新启动、混音器重建这一路）时以当前值为增量
uint64_t CounterDelta(uint64_t current, uint64_t last)
{
    return (current >= last) ? (current - last) : current;
}

} // namespace

CallNetSnapshot CallMediaSession::GetNetSnapshot() const
{
    CallNetSnapshot s;
    std::lock_guard<std::mutex> guard(m_recvStatsMtx);
    s.m_hasRtt = m_reportX.HasRtt();
    s.m_rttMs = m_reportX.RttMs();
    s.m_peerReports = m_reportX.ReportsReceived();
    s.m_peerReport = m_reportX.LastPeerReport();
    s.m_myGrade = m_myGrade;
    s.m_hasMine = m_hasMyWindow;
    s.m_mineLossPermille = m_myLossPermille;
    s.m_mineJitterMs = m_myJitterMs;
    s.m_mineStallPermille = m_myStallPermille;
    s.m_mineBufferMs = m_myBufferMs;
    return s;
}

void CallMediaSession::NoteRecvAudio(bool hasSeq, uint32_t seq, uint64_t tsMs, int64_t nowMs)
{
    std::lock_guard<std::mutex> guard(m_recvStatsMtx);
    m_recvStats.OnAudioPacket(hasSeq, seq, tsMs, nowMs);
}

void CallMediaSession::NoteRecvOther(int64_t nowMs)
{
    std::lock_guard<std::mutex> guard(m_recvStatsMtx);
    m_recvStats.OnAnyMessage(nowMs);
}

void CallMediaSession::StopStatsThread()
{
    {
        std::lock_guard<std::mutex> guard(m_statsMtx);
        m_statsStop = true;
    }
    m_statsCv.notify_all();
    if (m_statsThread && m_statsThread->joinable())
        m_statsThread->join();
    m_statsThread.reset();
}

void CallMediaSession::StatsProc()
{
    int ticks = 0;
    bool silent = false;
    std::unique_lock<std::mutex> lock(m_statsMtx);
    while (!m_statsStop)
    {
        //等一个周期；被 Stop 唤醒时立即退出。手写等待循环（不用带谓词的 lambda），醒来后复查条件防虚假唤醒
        const std::chrono::steady_clock::time_point deadline =
            std::chrono::steady_clock::now() + std::chrono::milliseconds(kCallStatsTickMs);
        while (!m_statsStop && std::chrono::steady_clock::now() < deadline)
            m_statsCv.wait_until(lock, deadline);
        if (m_statsStop)
            break;
        lock.unlock();

        ++ticks;
        //只在路径确定之后判断断流：连接阶段对方本来就没有消息
        bool nowSilent = false;
        if (m_path.load() != call_media_path_none)
        {
            std::lock_guard<std::mutex> guard(m_recvStatsMtx);
            nowSilent = m_recvStats.IsSilent(static_cast<int64_t>(::GetTickCount64()));
        }
        if (nowSilent != silent)
        {
            silent = nowSilent;
            if (silent)
                LOG_WARNING("CallMediaSession: no message from peer for %lld ms, session=%s, path=%s.",
                            static_cast<long long>(kCallSilentMs), m_sessionId.c_str(), PathName(m_path.load()));
            else
                LOG_INFO("CallMediaSession: peer messages resumed, session=%s, path=%s.", m_sessionId.c_str(),
                         PathName(m_path.load()));
        }
        //每秒取一个接收窗口、发一份质量报告（第 2 期）；路径确定之前没有可发的通道
        if (m_path.load() != call_media_path_none)
            TickReport(silent);
        if (ticks % kCallStatsLogEveryTicks == 0)
            LogStats(silent);

        lock.lock();
    }
}

void CallMediaSession::TickReport(bool peerSilent)
{
    //取这一秒的接收窗口，累加给 5 秒一条的统计日志
    CallRecvWindow w;
    {
        std::lock_guard<std::mutex> guard(m_recvStatsMtx);
        w = m_recvStats.TakeWindow();
    }
    m_logRecv += w.m_received;
    m_logLost += w.m_lost;
    m_logLate += w.m_late;

    //这一秒播放端补静音的比例
    uint64_t requested = 0;
    uint64_t concealed = 0;
    uint32_t stallPermille = 0;
    if (m_audioEngine.GetPlayoutCounters(CallAudioEngine::kDefaultStreamId, requested, concealed))
    {
        const uint64_t dReq = CounterDelta(requested, m_tickPlayRequested);
        const uint64_t dCon = CounterDelta(concealed, m_tickPlayConcealed);
        m_tickPlayRequested = requested;
        m_tickPlayConcealed = concealed;
        stallPermille = (dReq == 0) ? 0 : static_cast<uint32_t>(dCon * kStatsPermille / dReq);
    }
    //这一秒没有应收的音频包（对方关了麦克风）时，播放端整秒补静音是正常现象，不算卡顿：
    //否则对方的悬停详情会显示「卡顿 100%」。日志里的 playout concealed 另行统计，仍记原始比例
    const uint32_t audioPackets = w.m_received + w.m_lost;
    if (audioPackets == 0)
        stallPermille = 0;

    //这一秒正常播放时的平均缓冲延迟（2026-10-06 B4）：抖动缓冲每取到一个包量一次缓冲深度，这里取累计量的差值相除
    uint32_t bufferMs = 0;
    AudioStreamStats ss;
    if (m_audioEngine.GetStreamStats(CallAudioEngine::kDefaultStreamId, ss))
    {
        const uint64_t dSum = CounterDelta(ss.m_jb.m_bufferedSum, m_tickStreamStats.m_jb.m_bufferedSum);
        const uint64_t dCount = CounterDelta(ss.m_jb.m_bufferedCount, m_tickStreamStats.m_jb.m_bufferedCount);
        m_tickStreamStats = ss;
        if (dCount > 0)
            bufferMs = static_cast<uint32_t>(dSum / dCount / kCallSamplesPerMs);
    }

    //填报告：超出 16 位的数值按上限填，档位在下面加锁后评出（要用往返时延）
    CallQualityReport r;
    r.m_lossPermille = static_cast<uint16_t>((w.m_lossPermille > kStatsU16Max) ? kStatsU16Max : w.m_lossPermille);
    r.m_jitterMs = static_cast<uint16_t>((w.m_jitterMs > kStatsU16Max) ? kStatsU16Max : w.m_jitterMs);
    r.m_stallPermille = static_cast<uint16_t>((stallPermille > kStatsU16Max) ? kStatsU16Max : stallPermille);
    r.m_bufferMs = static_cast<uint16_t>((bufferMs > kStatsU16Max) ? kStatsU16Max : bufferMs);
    //标志位（2026-10-06）：本端恒能接收电脑声音；正在共享时再带上「正在共享」，对方据此显示提示
    r.m_flags = kCallReportFlagRecvSystemAudio;
    if (m_sysOn.load())
        r.m_flags |= kCallReportFlagSharingSystemAudio;
    const uint64_t nowSessionMs = ::GetTickCount64() - m_clockStartMs;
    {
        std::lock_guard<std::mutex> guard(m_recvStatsMtx);
        //评出本端「我收对方」的档位（第 3 期）：这一秒的接收窗口加上最近的往返时延，经防抖后填进报告、留给快照
        CallNetMetrics m;
        m.m_silent = peerSilent;
        m.m_hasRtt = m_reportX.HasRtt();
        m.m_rttMs = m_reportX.RttMs();
        m.m_audioPackets = audioPackets;
        m.m_lossPermille = w.m_lossPermille;
        m.m_jitterMs = w.m_jitterMs;
        m.m_stallPermille = stallPermille;
        m.m_bufferMs = bufferMs;
        const int grade = m_gradeSmoother.Feed(GradeForMetrics(m));
        if (grade != m_myGrade)
            LOG_INFO("CallMediaSession: my recv grade %d -> %d, session=%s, rtt=%d ms, audio=%u, loss=%u permille, "
                     "jitter=%u ms, stall=%u permille, buffer=%u ms, silent=%d.",
                     m_myGrade, grade, m_sessionId.c_str(), m.m_hasRtt ? static_cast<int>(m.m_rttMs) : -1,
                     audioPackets, w.m_lossPermille, w.m_jitterMs, stallPermille, bufferMs, peerSilent ? 1 : 0);
        m_myGrade = grade;
        m_hasMyWindow = true;
        m_myLossPermille = w.m_lossPermille;
        m_myJitterMs = w.m_jitterMs;
        m_myStallPermille = stallPermille;
        m_myBufferMs = bufferMs;
        r.m_grade = static_cast<uint8_t>(grade);
        m_reportX.PrepareReport(static_cast<int64_t>(nowSessionMs), r);
    }
    std::vector<uint8_t> bytes;
    BuildCallQualityReport(r, bytes);
    //走当前路径发出：P2P 一份报告一条数据报（序号为报告序号）；中转封成 [stats][whole][ts]
    if (m_path.load() == call_media_path_p2p)
    {
        SendP2P(call_p2p_kind_stats, 0, m_reportSeq++, nowSessionMs, bytes.data(), bytes.size());
        return;
    }
    std::vector<uint8_t> body;
    BuildStatsBody(bytes.data(), bytes.size(), nowSessionMs, body);
    m_relay.SendData(body.data(), body.size());
}

void CallMediaSession::OnPeerReportBytes(const uint8_t* data, size_t len, uint64_t peerTsMs)
{
    CallQualityReport r;
    if (!ParseCallQualityReport(data, len, r))
    {
        LOG_WARNING("CallMediaSession: bad quality report from peer, len=%u, session=%s.", static_cast<unsigned>(len),
                    m_sessionId.c_str());
        return;
    }
    const int64_t nowSessionMs = static_cast<int64_t>(::GetTickCount64() - m_clockStartMs);
    bool first = false;
    //对方是否正在共享电脑声音（2026-10-06）：与上一份报告相比有变化时记日志
    const bool peerSharing = (r.m_flags & kCallReportFlagSharingSystemAudio) != 0;
    bool sharingChanged = false;
    {
        std::lock_guard<std::mutex> guard(m_recvStatsMtx);
        first = !m_reportX.HasPeerReport();
        m_reportX.OnPeerReport(r, peerTsMs, nowSessionMs);
        sharingChanged = (peerSharing != m_peerSysSharing);
        m_peerSysSharing = peerSharing;
    }
    if (first)
        LOG_INFO("CallMediaSession: first quality report from peer, version=%u, flags=0x%02x, session=%s.",
                 static_cast<unsigned>(r.m_version), static_cast<unsigned>(r.m_flags), m_sessionId.c_str());
    if (sharingChanged)
    {
        LOG_INFO("CallMediaSession: peer system audio sharing %d -> %d, session=%s.", peerSharing ? 0 : 1,
                 peerSharing ? 1 : 0, m_sessionId.c_str());
        //对方停止共享：删掉那一路，释放解码器；下次共享的时间戳从 0 重新开始，用新的一路排队
        if (!peerSharing)
            m_audioEngine.RemoveStream(kSystemAudioStreamId);
    }
}

void CallMediaSession::LogStats(bool peerSilent)
{
    //这 5 秒（5 个一秒窗口）的接收统计；路径确定之前 TickReport 不跑，累加值为 0
    const uint64_t expected = static_cast<uint64_t>(m_logRecv) + m_logLost;
    const uint32_t lossPermille = (expected == 0) ? 0 : static_cast<uint32_t>(m_logLost * kStatsPermille / expected);
    uint32_t jitterMs = 0;
    bool hasRtt = false;
    uint32_t rttMs = 0;
    uint64_t peerReports = 0;
    CallQualityReport peer;
    int myGrade = kCallGradeUnknown;
    {
        std::lock_guard<std::mutex> guard(m_recvStatsMtx);
        myGrade = m_myGrade;
        jitterMs = m_recvStats.JitterMs();
        hasRtt = m_reportX.HasRtt();
        rttMs = m_reportX.RttMs();
        peerReports = m_reportX.ReportsReceived();
        peer = m_reportX.LastPeerReport();
    }
    const uint32_t logRecv = m_logRecv;
    const uint32_t logLost = m_logLost;
    const uint32_t logLate = m_logLate;
    m_logRecv = 0;
    m_logLost = 0;
    m_logLate = 0;

    //这 5 秒的增量：发出的音频包、视频帧，收到的视频帧
    const uint64_t audioSent = m_audioEngine.EncodedPacketCount();
    const uint64_t videoSent = m_videoSent.load();
    const uint64_t videoRecv = m_videoRecv.load();
    const uint64_t dAudioSent = CounterDelta(audioSent, m_lastAudioSent);
    const uint64_t dVideoSent = CounterDelta(videoSent, m_lastVideoSent);
    const uint64_t dVideoRecv = CounterDelta(videoRecv, m_lastVideoRecv);
    m_lastAudioSent = audioSent;
    m_lastVideoSent = videoSent;
    m_lastVideoRecv = videoRecv;

    //播放端补静音的比例（卡顿率）：对方关麦或断流时会接近 1000，须与收到的音频包数一起看
    uint64_t requested = 0;
    uint64_t concealed = 0;
    uint32_t concealedPermille = 0;
    if (m_audioEngine.GetPlayoutCounters(CallAudioEngine::kDefaultStreamId, requested, concealed))
    {
        const uint64_t dReq = CounterDelta(requested, m_lastPlayRequested);
        const uint64_t dCon = CounterDelta(concealed, m_lastPlayConcealed);
        m_lastPlayRequested = requested;
        m_lastPlayConcealed = concealed;
        concealedPermille = (dReq == 0) ? 0 : static_cast<uint32_t>(dCon * kStatsPermille / dReq);
    }

    //抖动缓冲这 5 秒的情况（2026-10-06 B4）：正常播放时的平均缓冲延迟，缺失、插入、迟到丢弃、时间线重置的次数，
    //丢包补偿与补静音的帧数
    AudioStreamStats ss;
    m_audioEngine.GetStreamStats(CallAudioEngine::kDefaultStreamId, ss);
    const uint64_t dBufSum = CounterDelta(ss.m_jb.m_bufferedSum, m_logStreamStats.m_jb.m_bufferedSum);
    const uint64_t dBufCount = CounterDelta(ss.m_jb.m_bufferedCount, m_logStreamStats.m_jb.m_bufferedCount);
    const unsigned bufferMs = (dBufCount == 0) ? 0u : static_cast<unsigned>(dBufSum / dBufCount / kCallSamplesPerMs);
    const unsigned long long jbMissing = CounterDelta(ss.m_jb.m_missing, m_logStreamStats.m_jb.m_missing);
    const unsigned long long jbInserts = CounterDelta(ss.m_jb.m_inserts, m_logStreamStats.m_jb.m_inserts);
    const unsigned long long jbLate = CounterDelta(ss.m_jb.m_lateDropped, m_logStreamStats.m_jb.m_lateDropped);
    const unsigned long long jbResets = CounterDelta(ss.m_jb.m_resets, m_logStreamStats.m_jb.m_resets);
    const unsigned long long plcFrames = CounterDelta(ss.m_plcFrames, m_logStreamStats.m_plcFrames);
    const unsigned long long silenceFrames = CounterDelta(ss.m_silenceFrames, m_logStreamStats.m_silenceFrames);
    m_logStreamStats = ss;

    //共享电脑声音这 5 秒的收发包数（2026-10-06）
    const uint64_t sysSent = m_sysSent.load();
    const uint64_t sysRecv = m_sysRecv.load();
    const unsigned long long dSysSent = CounterDelta(sysSent, m_lastSysSent);
    const unsigned long long dSysRecv = CounterDelta(sysRecv, m_lastSysRecv);
    m_lastSysSent = sysSent;
    m_lastSysRecv = sysRecv;
    bool peerSysSharing = false;
    {
        std::lock_guard<std::mutex> guard(m_recvStatsMtx);
        peerSysSharing = m_peerSysSharing;
    }

    //音频引擎的状态：没有运行（启动失败或尚未启动）、无设备模式、正常使用声音设备
    const char* audioMode = !m_audioEngine.IsRunning() ? "stopped"
                            : (m_audioEngine.IsWithoutDevices() ? "no-devices" : "devices");
    //往返时延与对方报告（第 2 期）：rtt 为 -1 表示还没有样本；peer reports 是累计收到的对方报告份数
    //档位（第 3 期）：my grade 为本端「我收对方」防抖后的档位，peer grade 取最近一份对方报告；255 表示未评级
    const int rttLog = hasRtt ? static_cast<int>(rttMs) : -1;
    LOG_INFO("call stats: session=%s, path=%s, link path=%d, audio=%s, mic=%s, volume=%d%%, "
             "sent audio=%llu video=%llu, p2p send failures=%llu, "
             "recv audio=%u lost=%u (%u permille) late=%u video=%llu, jitter=%u ms, "
             "playout concealed=%u permille, capture rms=%d, peer silent=%d, rtt=%d ms, "
             "peer reports=%llu, peer recv loss=%u permille jitter=%u ms stall=%u permille, "
             "my grade=%d, peer grade=%d, playout buffer=%u ms, jb missing=%llu inserts=%llu late=%llu resets=%llu, "
             "plc frames=%llu, silence frames=%llu, peer buffer=%u ms, "
             "system audio=%s sent=%llu recv=%llu, peer system audio=%d.",
             m_sessionId.c_str(), PathName(m_path.load()), LinkPath(), audioMode, m_micWanted.load() ? "on" : "off",
             m_audioEngine.PlaybackVolume(), static_cast<unsigned long long>(dAudioSent),
             static_cast<unsigned long long>(dVideoSent), static_cast<unsigned long long>(m_p2pSendFailures.load()),
             logRecv, logLost, lossPermille, logLate, static_cast<unsigned long long>(dVideoRecv), jitterMs,
             concealedPermille, m_audioEngine.CaptureLevel(), peerSilent ? 1 : 0, rttLog,
             static_cast<unsigned long long>(peerReports),
             static_cast<unsigned>(peer.m_lossPermille), static_cast<unsigned>(peer.m_jitterMs),
             static_cast<unsigned>(peer.m_stallPermille), myGrade, static_cast<int>(peer.m_grade), bufferMs, jbMissing,
             jbInserts, jbLate, jbResets, plcFrames, silenceFrames, static_cast<unsigned>(peer.m_bufferMs),
             m_sysOn.load() ? "on" : "off", dSysSent, dSysRecv, peerSysSharing ? 1 : 0);
}


int CallMediaSession::SetSystemAudioEnabled(bool on)
{
    if (!on)
    {
        stopSystemAudio();
        return call_sysaudio_ok;
    }
    if (m_sysOn.load())
        return call_sysaudio_ok;

    //打开前的检查：通话已接通（路径已定、音频引擎在运行），且对方的报告表明能接收电脑声音
    uint32_t peerReports = 0;
    uint8_t peerFlags = 0;
    {
        std::lock_guard<std::mutex> guard(m_recvStatsMtx);
        peerReports = static_cast<uint32_t>(m_reportX.ReportsReceived());
        peerFlags = m_reportX.LastPeerReport().m_flags;
    }
    const bool connected = m_running.load() && m_audioEngine.IsRunning() && m_path.load() != call_media_path_none;
    const int check = CallSystemAudioPrecheck(connected, peerReports, peerFlags);
    if (check != call_sysaudio_ok)
    {
        LOG_INFO("CallMediaSession: system audio sharing refused, result=%d, connected=%d, peer reports=%u, "
                 "peer flags=0x%02x, session=%s.", check, connected ? 1 : 0, peerReports,
                 static_cast<unsigned>(peerFlags), m_sessionId.c_str());
        return check;
    }

    //先备好编码，再打开回环采集：采集一打开就会有回调
    if (!m_sysSender.Start(this))
    {
        LOG_ERROR("CallMediaSession: system audio encoder init failed, session=%s.", m_sessionId.c_str());
        return call_sysaudio_capture_failed;
    }
    m_sysSeq = 0;
    m_sysExcludingSelf.store(false);
    m_sysOn.store(true);

    //无设备模式：不打开真实的回环，由测试经 FeedTestSystemAudio 喂入，按「排除本进程树」处理
    if (m_audioEngine.IsWithoutDevices())
    {
        m_sysExcludingSelf.store(true);
        LOG_INFO("CallMediaSession: system audio sharing on (no devices, fed by tests), session=%s.",
                 m_sessionId.c_str());
        return call_sysaudio_ok;
    }
    if (!m_sysCapture.Start(this))
    {
        m_sysOn.store(false);
        m_sysSender.Stop();
        LOG_ERROR("CallMediaSession: system audio loopback capture failed to start, session=%s.", m_sessionId.c_str());
        return call_sysaudio_capture_failed;
    }
    m_sysExcludingSelf.store(m_sysCapture.IsExcludingSelf());
    LOG_INFO("CallMediaSession: system audio sharing on, excluding self=%d, path=%s, session=%s.",
             m_sysExcludingSelf.load() ? 1 : 0, PathName(m_path.load()), m_sessionId.c_str());
    return call_sysaudio_ok;
}

bool CallMediaSession::IsPeerSharingSystemAudio() const
{
    std::lock_guard<std::mutex> guard(m_recvStatsMtx);
    return m_peerSysSharing;
}

void CallMediaSession::FeedTestSystemAudio(const int16_t* mono, int samples)
{
    if (!m_audioEngine.IsWithoutDevices())
        return;
    OnSystemAudio(mono, samples);
}

void CallMediaSession::stopSystemAudio()
{
    if (!m_sysOn.exchange(false))
        return;
    //先停回环采集（join 采集线程），之后不再有 OnSystemAudio；再释放编码器、丢掉没用掉的回声参考
    m_sysCapture.Stop();
    const unsigned long long frames = static_cast<unsigned long long>(m_sysSender.FramesSent());
    m_sysSender.Stop();
    m_audioEngine.ClearSystemAudioReference();
    LOG_INFO("CallMediaSession: system audio sharing off, frames=%llu, session=%s.", frames, m_sessionId.c_str());
}

void CallMediaSession::OnSystemAudio(const int16_t* mono, int samples)
{
    //回环采集线程上下文（无设备模式下为测试线程）
    if (!m_sysOn.load() || mono == NULL || samples <= 0)
        return;
    const bool excludingSelf = m_sysExcludingSelf.load();
    //回声参考：只在排除了本进程树时押入原样的电脑声音（扬声器放出来的就是它，闪避只影响发给对方的那一份）。
    //退回普通回环时，采到的声音里已含本端播放的对方说话声，再押进去会与播放混音那一路重复，故不押
    if (excludingSelf)
        m_audioEngine.PushSystemAudioReference(mono, samples);
    int peerLevel = 0;
    m_audioEngine.GetStreamLevel(CallAudioEngine::kDefaultStreamId, peerLevel);
    m_sysSender.Feed(mono, samples, CallSystemAudioGainPercent(excludingSelf, peerLevel));
}

void CallMediaSession::OnSystemAudioPacket(const uint8_t* data, size_t len, uint64_t tsMs)
{
    //回环采集线程上下文：电脑声音的一个包 → 按当前路径发出
    if (!m_running.load())
        return;
    ++m_sysSent;
    if (m_path.load() == call_media_path_p2p)
    {
        SendP2P(call_p2p_kind_system_audio, 0, m_sysSeq++, tsMs, data, len);
        return;
    }
    std::vector<uint8_t> body;
    BuildSystemAudioBody(data, len, tsMs, body);
    m_relay.SendData(body.data(), body.size());
}

} // namespace call
