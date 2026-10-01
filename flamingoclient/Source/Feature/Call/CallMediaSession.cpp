/**
 *  音视频通话中转会话实现，CallMediaSession.cpp
 *  balloonwj@qq.com   2026-06-11（2026-06-12 接入真实音频；2026-06-16 接入视频多路复用）
 */
#include "CallMediaSession.h"

#include <mfapi.h>               //MFStartup / MFShutdown（视频解码线程初始化 MF 平台）
#include <utility>               //std::move（视频包入队 / 出队转移所有权）

#include "../../Common/Log/IULog.h"            //LOG_INFO / LOG_ERROR
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

CallMediaSession::CallMediaSession()
    : m_displaySink(NULL)
    , m_videoDecodeStop(false)
    , m_clockStartMs(0)
    , m_hwndNotify(NULL)
    , m_notifyMsg(0)
    , m_role(0)
    , m_enableVideo(false)
    , m_running(false)
{
}

CallMediaSession::~CallMediaSession()
{
    Stop();
}

void CallMediaSession::Start(const std::string& ip, int port, const std::string& token,
                             int role, const std::string& sessionId, HWND hwndNotify, UINT notifyMsg,
                             bool enableVideo)
{
    //幂等：清掉上一会话（若有），再起新会话
    Stop();

    m_hwndNotify = hwndNotify;
    m_notifyMsg = notifyMsg;
    m_role = role;
    m_sessionId = sessionId;
    m_enableVideo = enableVideo;
    m_demux.Reset();
    //会话时钟起点：之后音 / 视频包都打「当前 - 此起点」毫秒戳，作 A/V 同步的统一时间轴
    m_clockStartMs = ::GetTickCount64();
    m_running.store(true);

    LOG_INFO("CallMediaSession: start relay session, session=%s, role=%d, relay %s:%d, video=%d.",
             m_sessionId.c_str(), m_role, ip.c_str(), port, enableVideo ? 1 : 0);

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

    //收尾顺序很重要：
    //1) 先关中转连接——join 接收线程后再无 OnRelayData，不再把音频喂引擎解码、不再把视频入队。
    //   若连接线程仍阻塞在 Connect，Close 也会促其返回。
    m_relay.Close();
    //2) 再 join 后台连接线程——其中可能正在执行引擎 Start()，join 后该 Start 已完成，
    //   引擎不再被连接线程并发触碰。
    if (m_connectThread && m_connectThread->joinable())
        m_connectThread->join();
    m_connectThread.reset();
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
        //先起解码线程再起引擎，与 Start 里的次序一致：确保对端早到的视频帧不被丢。
        m_videoDecodeStop.store(false);
        m_videoDecodeThread.reset(new std::thread(&CallMediaSession::VideoDecodeProc, this));

        if (!m_videoEngine.Start(this, kVideoFps, kVideoBitrate))
        {
            //摄像头开不起来（无设备 / 被占用）：不回滚解码线程——对端可能已在推流，
            //本端仍要能收看。只是没有本端画面可发，把原因投给 UI 让通话窗提示。
            int reason = m_videoEngine.CameraFailReason();
            LOG_ERROR("CallMediaSession: EnableVideo(true) camera failed, session=%s, reason=%d.",
                      m_sessionId.c_str(), reason);
            PostEvent(call_relay_camera_unavailable, static_cast<unsigned int>(reason));
            m_enableVideo = true;   //视频链路（收）已建立，只是本端没在发
            return false;
        }

        m_enableVideo = true;
        LOG_INFO("CallMediaSession: video enabled, session=%s.", m_sessionId.c_str());
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
    m_videoEngine.Stop();

    m_enableVideo = false;
    LOG_INFO("CallMediaSession: video disabled, session=%s.", m_sessionId.c_str());
    return true;
}

void CallMediaSession::ConnectProc(std::string ip, int port, std::string token)
{
    //阻塞连中转并握手；本类作为 handler 接收后续数据 / 断开回调
    bool ok = m_relay.Connect(ip, port, token, this, kConnectTimeoutSec);
    if (!ok)
    {
        LOG_ERROR("CallMediaSession: relay connect/handshake failed, session=%s.", m_sessionId.c_str());
        PostEvent(call_relay_failed, 0);
        return;
    }

    LOG_INFO("CallMediaSession: relay connected, session=%s, role=%d.", m_sessionId.c_str(), m_role);
    PostEvent(call_relay_connected, 0);

    //起音频引擎：麦克风采集 + Opus 编码经 OnEncodedAudio 发中转；对端来的包经 OnRelayData
    //分流后喂引擎解码、抖动缓冲后播放。引擎启动失败（无设备 / 采集率非 Opus 支持）不影响信令
    //通话，仅记错误、本端无声。
    //真实通话启用 AEC + 降噪：避免扬声器放出的对端声音被麦克风拾回、再发回去形成回授啸叫。
    if (!m_audioEngine.Start(this, true))
        LOG_ERROR("CallMediaSession: audio engine start failed, session=%s; call will have no audio.",
                  m_sessionId.c_str());

    //视频通话：先起视频解码线程（接收线程把整帧入队、由它解码），再起视频引擎（摄像头采集 +
    //H.264 编码经 OnEncodedVideo 发中转）。先起解码线程可确保对端早到的视频帧不被丢。
    if (m_enableVideo)
    {
        m_videoDecodeStop.store(false);
        m_videoDecodeThread.reset(new std::thread(&CallMediaSession::VideoDecodeProc, this));
        if (!m_videoEngine.Start(this, kVideoFps, kVideoBitrate))
        {
            //摄像头开不起来（没有设备 / 被占用）：通话继续（音频照常、也仍能收看对端画面），
            //只是本端没有画面可发。把失败原因投给 UI，让通话窗告诉用户，而不是只留一块黑屏。
            int reason = m_videoEngine.CameraFailReason();
            LOG_ERROR("CallMediaSession: video engine start failed, session=%s, reason=%d; call will have no local video.",
                      m_sessionId.c_str(), reason);
            PostEvent(call_relay_camera_unavailable, static_cast<unsigned int>(reason));
        }
    }
}

void CallMediaSession::OnRelayData(const unsigned char* data, size_t len)
{
    //中转接收线程上下文：对端来的一个中转 body → 经 demux 分流。
    std::vector<uint8_t> payload;
    uint64_t tsMs = 0;
    CallMediaDemux::Result r = m_demux.Feed(data, len, payload, tsMs);
    switch (r)
    {
    case CallMediaDemux::call_demux_audio:
        //音频包：在接收线程内联喂引擎解码（Opus 解码很快，不会拖累接收）。引擎未运行时内部丢弃。
        m_audioEngine.OnEncodedPacket(payload.data(), payload.size());
        break;
    case CallMediaDemux::call_demux_video:
    {
        //完整视频帧：连同时间戳入队交视频解码线程解码（H.264 解码偏慢，剥离出接收线程避免拖累音频）。
        VideoInPacket pkt;
        pkt.m_data = std::move(payload);
        pkt.m_tsMs = tsMs;
        std::lock_guard<std::mutex> guard(m_videoQueueMtx);
        m_videoInQueue.push_back(std::move(pkt));
        m_videoQueueCv.notify_one();
        break;
    }
    case CallMediaDemux::call_demux_none:
        //视频分片累积中，未构成完整帧：无需处理
        break;
    case CallMediaDemux::call_demux_error:
    default:
        //非法 body：demux 已重置视频重组状态，仅记日志
        LOG_ERROR("CallMediaSession: demux error on relay body, len=%u, session=%s.",
                  static_cast<unsigned>(len), m_sessionId.c_str());
        break;
    }
}

void CallMediaSession::OnEncodedAudio(const unsigned char* data, size_t len)
{
    //采集线程上下文：本端音频包 → 打「音频 / whole」标记 + 会话时间戳封成中转 body → 发给对端。
    if (!m_running.load())
        return;
    uint64_t tsMs = ::GetTickCount64() - m_clockStartMs;
    std::vector<uint8_t> body;
    BuildAudioBody(data, len, tsMs, body);
    m_relay.SendData(body.data(), body.size());
}

void CallMediaSession::OnEncodedVideo(const uint8_t* data, size_t len, bool /*keyframe*/, int64_t /*ptsHns*/)
{
    //采集线程上下文：本端 H.264 帧 → 打「视频」标记并按 kVideoChunkSize 切片 → 逐片发给对端。
    //关键帧可能较大、超过中转单帧上限，故须切片；接收端按 whole/more/last 重组成整帧。
    if (!m_running.load())
        return;
    uint64_t tsMs = ::GetTickCount64() - m_clockStartMs;
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

} // namespace call
