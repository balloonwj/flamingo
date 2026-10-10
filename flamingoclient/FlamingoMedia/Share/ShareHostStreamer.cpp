/**
 *  屏幕共享发起人一侧的推流管线，ShareHostStreamer.cpp
 *  流程与线程约定见 ShareHostStreamer.h。
 *
 *  balloonwj@qq.com   2026-10-02
 */
#include "ShareHostStreamer.h"
#include "ShareAudioSession.h"
#include "ShareCameraSender.h"
#include "ShareCameraReceiver.h"
#include "ShareAnnotation.h"
#include "../Caption/ShareCaptionFrame.h"   // ParseShareCaptionFrame、ShareCaptionSink（语音字幕）
#include "ShareRemoteControl.h"   // ShareRemoteInput::ParseInputFrame、ShareInputSink（第 18 期控制键鼠）

#include <objbase.h>
#include <mfapi.h>
#include <chrono>
#include <string.h>
#include "ShareSources.h"       // EnumerateShareMonitors（第 23 期 MonitorSourceForWindow）
#include "ShareVideoPolicy.h"
#include "../../Source/net/Protocol.h"
#include "../../Source/Common/Log/IULog.h"

#pragma comment(lib, "mfplat.lib")

namespace screenshare {

namespace {

//连接房间中转的超时，单位秒
const int kRelayConnectTimeoutSec = 5;
//暂停、窗口已关闭等不取帧时每轮的等待，单位毫秒
const DWORD kIdleWaitMs = 50;
//采集器失效后重建之前的等待，单位毫秒
const DWORD kCapturerRetryWaitMs = 500;
//每像素字节数
const int kBytesPerPixel = 4;
//公钥字节数
const size_t kPubKeyBytes = 32;

int64_t steadyMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch()).count();
}

} // namespace

ShareSourceSpec::ShareSourceSpec()
    : m_kind(share_source_none)
    , m_adapter(-1)
    , m_output(-1)
    , m_hwnd(NULL)
{
    memset(&m_monitorRect, 0, sizeof(m_monitorRect));
}

bool MonitorSourceForWindow(HWND hwnd, ShareSourceSpec& out)
{
    if (hwnd == NULL || !::IsWindow(hwnd))
        return false;
    const HMONITOR target = ::MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    std::vector<ShareMonitor> monitors;
    if (!EnumerateShareMonitors(monitors))
        return false;
    for (size_t i = 0; i < monitors.size(); ++i)
    {
        if (monitors[i].m_monitor != target)
            continue;
        out = ShareSourceSpec();
        out.m_kind = share_source_monitor;
        out.m_adapter = monitors[i].m_adapter;
        out.m_output = monitors[i].m_output;
        out.m_monitorRect = monitors[i].m_rect;
        return true;
    }
    return false;
}

ShareHostStreamer::ShareHostStreamer()
    : m_hasPubKey(false)
    , m_listener(NULL)
    , m_running(false)
    , m_stop(false)
    , m_paused(false)
    , m_keyRequested(false)
    , m_keyRequestCount(0)
    , m_bitrate(kShareBitrateBps, kShareMinBitrateBps)
    , m_relayLost(false)
    , m_switchPending(false)
    , m_lastW(0)
    , m_lastH(0)
    , m_sourceState(screenshare_source_sharing)
    , m_sentFrames(0)
    , m_sentKeyFrames(0)
    , m_audio(NULL)
    , m_cameraSender(NULL)
    , m_cameraReceiver(NULL)
    , m_annotSink(NULL)
    , m_captionSink(NULL)
    , m_inputSink(NULL)
    , m_recordTap(false)
    , m_recordSeed(false)
    , m_recordW(0)
    , m_recordH(0)
    , m_recordSerial(0)
{
    memset(m_pubKey, 0, sizeof(m_pubKey));
    memset(&m_sourceRect, 0, sizeof(m_sourceRect));
}

ShareHostStreamer::~ShareHostStreamer()
{
    Stop();
}

bool ShareHostStreamer::Start(const ShareRelayInfo& relay, const unsigned char* serverPubKey,
                              const ShareSourceSpec& source, IShareCapturer* injected,
                              ShareHostStreamerListener* listener)
{
    if (m_running.load() || (source.m_kind == share_source_none && injected == NULL))
    {
        delete injected;
        return false;
    }
    m_relay = relay;
    m_hasPubKey = (serverPubKey != NULL);
    if (m_hasPubKey)
        memcpy(m_pubKey, serverPubKey, kPubKeyBytes);
    m_listener = listener;
    m_source = source;
    m_injected.reset(injected);
    m_stop.store(false);
    m_paused.store(false);
    m_keyRequested.store(false);
    m_relayLost.store(false);
    m_sentFrames.store(0);
    m_sentKeyFrames.store(0);
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        m_switchPending = false;
        m_sourceRect = source.m_monitorRect;
    }
    m_running.store(true);
    m_thread.reset(new std::thread(&ShareHostStreamer::WorkerProc, this));
    return true;
}

/**
 *  一名 P2P 观看者：链路槽、帧通道与状态。帧通道的回调把观看者发来的包体交回推流管线、把断开记下。
 *  线程：TryOpen / Expired 在工作线程上；Send / QueuedBytes 在发送的线程上；Close 在任意线程上；回调在转交线程上。
 */
class ShareHostStreamer::P2PViewerLink : public balloonp2p::IP2PStreamHandler
{
public:
    //观看者链路的状态
    enum link_state
    {
        link_waiting = 0,   //等链路结果
        link_open    = 1,   //帧通道已打开，经它发送
        link_closed  = 2,   //链路断开、放弃或已关闭：回到经房间中转发送
    };

    P2PViewerLink(ShareHostStreamer* owner, int seat, const std::shared_ptr<balloonp2p::P2PLinkSlot>& slot)
        : m_owner(owner)
        , m_seat(seat)
        , m_slot(slot)
        , m_createdMs(::GetTickCount64())
        , m_state(link_waiting)
    {
    }

    ~P2PViewerLink()
    {
        Close();
    }

    //座位号
    int Seat() const { return m_seat; }

    //当前状态，取值见 link_state
    int State() const { return m_state.load(); }

    //帧通道打开后链路的实际路径（供「P2P」徽章）；还没打开时为 0
    int LinkPath() const { return balloonp2p::LinkPathWhenUsing(m_state.load() == link_open, m_slot); }

    /**
     *  链路就绪时打开帧通道（工作线程上）。
     *  @return 本次刚打开返回 true
     */
    bool TryOpen()
    {
        if (m_state.load() != link_waiting || m_slot->State() != balloonp2p::P2PLinkSlot::slot_ready)
            return false;
        m_channel.reset(new balloonp2p::P2PStreamChannel(m_slot, kShareP2PMaxBodyBytes, kShareP2PSendWaitMs));
        if (!m_channel->Open(kShareP2PChannelId, this))
        {
            LOG_WARNING("screenshare host: open p2p channel to seat %d failed, staying on the relay.", m_seat);
            m_state.store(link_closed);
            return false;
        }
        m_state.store(link_open);
        return true;
    }

    //还在等链路，但链路已失败、断开，或等了太久：放弃 P2P（观看者一侧同样会改连房间中转）
    bool Expired() const
    {
        if (m_state.load() != link_waiting)
            return false;
        const int s = m_slot->State();
        if (s == balloonp2p::P2PLinkSlot::slot_failed || s == balloonp2p::P2PLinkSlot::slot_closed)
            return true;
        return ::GetTickCount64() - m_createdMs >= (unsigned long long)kShareP2PLinkWaitMs;
    }

    //标记为已放弃
    void GiveUp()
    {
        m_state.store(link_closed);
    }

    //经帧通道发一个包体
    bool Send(const std::string& body)
    {
        if (m_state.load() != link_open)
            return false;
        return m_channel->SendFrame(reinterpret_cast<const unsigned char*>(body.data()), body.size());
    }

    //链路当前的发送积压（字节）；取不到时为 0
    uint64_t QueuedBytes() const
    {
        p2p::IP2PLink* link = m_slot->Link();
        if (link == NULL)
            return 0;
        p2p::P2PLinkStats stats;
        memset(&stats, 0, sizeof(stats));
        stats.m_cbSize = sizeof(stats);
        link->GetStats(&stats);
        return stats.m_queuedBytes;
    }

    //关闭帧通道并以「业务结束」关闭链路（幂等）
    void Close()
    {
        m_state.store(link_closed);
        if (m_channel)
            m_channel->Close();
        m_slot->CloseLink();
    }

    //观看者经 P2P 发来的一个包体：交回推流管线
    virtual void OnStreamFrame(const unsigned char* data, size_t len) override
    {
        m_owner->onP2PBody(m_seat, data, len);
    }

    //链路中途断开：回到经房间中转发送（观看者一侧会改连中转，服务端随即请求关键帧）
    virtual void OnStreamClosed() override
    {
        LOG_WARNING("screenshare host: p2p link to seat %d lost, back to the relay.", m_seat);
        m_state.store(link_closed);
    }

private:
    ShareHostStreamer*                             m_owner;      //所属推流管线（借用）
    int                                            m_seat;       //观看者的座位号
    std::shared_ptr<balloonp2p::P2PLinkSlot>       m_slot;       //链路槽（共享所有权）
    std::unique_ptr<balloonp2p::P2PStreamChannel>  m_channel;    //帧通道；TryOpen 之前为空
    unsigned long long                             m_createdMs;  //登记时刻（GetTickCount64）
    std::atomic<int>                               m_state;      //状态，取值见 link_state
};

void ShareHostStreamer::SetP2PViewer(int seat, const std::shared_ptr<balloonp2p::P2PLinkSlot>& slot)
{
    std::shared_ptr<P2PViewerLink> old;
    {
        std::lock_guard<std::mutex> guard(m_p2pMutex);
        old.swap(m_p2pViewer);
        if (slot)
            m_p2pViewer.reset(new P2PViewerLink(this, seat, slot));
    }
    //被替换的观看者在锁外关闭（关闭帧通道要等它的转交线程结束）
    if (old)
        old->Close();
    LOG_INFO("screenshare host: p2p viewer seat %d registered, waiting for the link.", seat);
}

void ShareHostStreamer::ClearP2PViewer(int seat)
{
    std::shared_ptr<P2PViewerLink> old;
    {
        std::lock_guard<std::mutex> guard(m_p2pMutex);
        if (!m_p2pViewer || m_p2pViewer->Seat() != seat)
            return;
        old.swap(m_p2pViewer);
    }
    old->Close();
    LOG_INFO("screenshare host: p2p viewer seat %d removed.", seat);
}

bool ShareHostStreamer::IsP2PViewerOpen() const
{
    return openP2PViewer() ? true : false;
}

int ShareHostStreamer::P2PViewerLinkPath() const
{
    std::shared_ptr<P2PViewerLink> viewer = openP2PViewer();
    return viewer ? viewer->LinkPath() : 0;
}

std::shared_ptr<ShareHostStreamer::P2PViewerLink> ShareHostStreamer::openP2PViewer() const
{
    std::lock_guard<std::mutex> guard(m_p2pMutex);
    if (m_p2pViewer && m_p2pViewer->State() == P2PViewerLink::link_open)
        return m_p2pViewer;
    return std::shared_ptr<P2PViewerLink>();
}

void ShareHostStreamer::pollP2PViewer()
{
    std::shared_ptr<P2PViewerLink> viewer;
    {
        std::lock_guard<std::mutex> guard(m_p2pMutex);
        viewer = m_p2pViewer;
    }
    if (!viewer)
        return;
    if (viewer->TryOpen())
    {
        //观看者经 P2P 连上了：像服务端在新成员报到时那样，立即出关键帧、补发标注、让摄像头出关键帧
        LOG_INFO("screenshare host: p2p channel to seat %d open, media now goes over p2p.", viewer->Seat());
        m_p2pGate.Reset();
        m_keyRequested.store(true);
        notify(share_host_key_requested, 0);
        ShareCameraSender* camera = m_cameraSender.load();
        if (camera != NULL)
            camera->RequestKeyFrame();
        return;
    }
    if (viewer->Expired())
    {
        LOG_INFO("screenshare host: p2p link to seat %d not available, staying on the relay.", viewer->Seat());
        viewer->GiveUp();
    }
}

bool ShareHostStreamer::routeBodies(const std::vector<std::string>& bodies, bool isVideo, bool keyframe)
{
    std::shared_ptr<P2PViewerLink> viewer = openP2PViewer();
    if (!viewer)
        return m_relayClient.SendBodies(bodies);
    if (isVideo)
    {
        bool requestKey = false;
        if (!m_p2pGate.OnVideoFrame(keyframe, viewer->QueuedBytes(), steadyMs(), requestKey))
        {
            //链路积压：这一帧不发，请求关键帧（计入码率自适应，与服务端的关键帧请求同样处理）
            if (requestKey)
            {
                m_keyRequested.store(true);
                m_keyRequestCount.fetch_add(1);
                LOG_INFO("screenshare host: p2p backlog to seat %d, dropping frames until a key frame.", viewer->Seat());
            }
            return true;
        }
    }
    for (size_t i = 0; i < bodies.size(); ++i)
    {
        if (!viewer->Send(bodies[i]))
            return false;
    }
    return true;
}

void ShareHostStreamer::onP2PBody(int seat, const unsigned char* data, size_t len)
{
    //与服务端一样，把发送者座位号改写为这名观看者的座位，客户端填错或冒充都无效
    std::string body(reinterpret_cast<const char*>(data), len);
    if (!ShareRewriteSenderSeat(body, seat))
        return;
    handleRoomBody(body.data(), body.size(), m_p2pReassembler);
}

void ShareHostStreamer::Stop()
{
    m_stop.store(true);
    //先关中转：工作线程若正阻塞在发送上，会因连接关闭立即返回
    m_relayClient.Close();
    if (m_thread && m_thread->joinable())
    {
        if (std::this_thread::get_id() != m_thread->get_id())
            m_thread->join();
        else
            m_thread->detach();
    }
    m_thread.reset();
    m_running.store(false);
    //P2P 观看者：关闭帧通道与链路
    std::shared_ptr<P2PViewerLink> viewer;
    {
        std::lock_guard<std::mutex> guard(m_p2pMutex);
        viewer.swap(m_p2pViewer);
    }
    if (viewer)
        viewer->Close();
}

bool ShareHostStreamer::IsRunning() const
{
    return m_running.load();
}

void ShareHostStreamer::SwitchSource(const ShareSourceSpec& source)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    m_pendingSource = source;
    m_switchPending = true;
}

void ShareHostStreamer::SetPaused(bool paused)
{
    m_paused.store(paused);
}

void ShareHostStreamer::RequestKeyFrame()
{
    m_keyRequested.store(true);
}

RECT ShareHostStreamer::CurrentSourceRect() const
{
    std::lock_guard<std::mutex> guard(m_mutex);
    return m_sourceRect;
}

uint64_t ShareHostStreamer::SentFrames() const
{
    return m_sentFrames.load();
}

uint64_t ShareHostStreamer::SentKeyFrames() const
{
    return m_sentKeyFrames.load();
}

IShareCapturer* ShareHostStreamer::createCapturer(const ShareSourceSpec& source)
{
    if (source.m_kind == share_source_monitor)
        return CreateMonitorCapturer(source.m_adapter, source.m_output, source.m_monitorRect);
    if (source.m_kind == share_source_window)
        return CreateWindowCapturer(source.m_hwnd);
    return NULL;
}

void ShareHostStreamer::notify(int event, int value)
{
    if (m_listener != NULL)
        m_listener->OnHostStreamEvent(event, value);
}

void ShareHostStreamer::sendSourceState(int state)
{
    std::vector<std::string> bodies(1, ShareFraming::buildSourceStateBody(m_relay.m_seat, state));
    routeBodies(bodies, false, false);
}

void ShareHostStreamer::copyLastFrame(const ShareCaptureFrame& f)
{
    m_lastW = f.m_width;
    m_lastH = f.m_height;
    const size_t rowBytes = static_cast<size_t>(f.m_width) * kBytesPerPixel;
    m_lastFrame.resize(rowBytes * f.m_height);
    for (int y = 0; y < f.m_height; ++y)
        memcpy(&m_lastFrame[rowBytes * y], f.m_data + static_cast<size_t>(y) * f.m_stride, rowBytes);
}

void ShareHostStreamer::SetMouseEffects(const ShareMouseEffectSettings& settings)
{
    m_mouseFx.SetSettings(settings);
    LOG_INFO("screenshare host: mouse effects halo=%d click=%d trail=%d halocolor=%d trailcolor=%d.",
             settings.m_halo ? 1 : 0, settings.m_click ? 1 : 0, settings.m_trail ? 1 : 0, settings.m_haloColor,
             settings.m_trailColor);
}

ShareMouseEffectSettings ShareHostStreamer::MouseEffects() const
{
    return m_mouseFx.Settings();
}

void ShareHostStreamer::WorkerProc()
{
    //WGC 的激活工厂与自由线程帧池、Media Foundation 的编码器都要求本线程初始化 COM 与 MF
    ::CoInitializeEx(NULL, COINIT_MULTITHREADED);
    ::MFStartup(MF_VERSION);

    if (!m_relayClient.Connect(m_relay.m_relayIp, m_relay.m_relayPort, m_relay.m_token,
                               m_hasPubKey ? m_pubKey : NULL, this, kRelayConnectTimeoutSec))
    {
        LOG_ERROR("screenshare host: relay connect failed, room=%s, code=%d.", m_relay.m_roomId.c_str(),
                  m_relayClient.LastHelloCode());
        notify(share_host_relay_failed, 0);
        ::MFShutdown();
        ::CoUninitialize();
        return;
    }
    notify(share_host_relay_connected, 0);
    LOG_INFO("screenshare host: streaming room %s as seat %d.", m_relay.m_roomId.c_str(), m_relay.m_seat);

    std::unique_ptr<IShareCapturer> capturer(m_injected.release());
    if (!capturer)
        capturer.reset(createCapturer(m_source));
    bool capturerOk = capturer && capturer->Start();
    if (!capturerOk)
        notify(share_host_capture_failed, 0);

    ShareFramePacer pacer(kShareMaxFps, kShareKeyFrameIntervalMs);
    //码率自适应从最高码率重新开始（上一次推流降下来的码率不沿用）
    m_bitrate = ShareBitrateController(kShareBitrateBps, kShareMinBitrateBps);
    m_keyRequestCount.store(0);
    m_encoder.SetBitrate(kShareBitrateBps);
    //鼠标效果从头开始（上一次推流留下的轨迹、点击圈不沿用，设置保留）
    m_mouseFx.Reset();
    const int64_t startMs = steadyMs();
    bool wasPaused = false;
    while (!m_stop.load() && !m_relayLost.load())
    {
        //P2P 观看者：链路就绪就打开帧通道，等太久或失败就放弃
        pollP2PViewer();

        //切换共享内容：换采集器，编码器按新尺寸重建（下一帧必为关键帧）
        bool switchNow = false;
        ShareSourceSpec next;
        {
            std::lock_guard<std::mutex> guard(m_mutex);
            if (m_switchPending)
            {
                switchNow = true;
                next = m_pendingSource;
                m_switchPending = false;
            }
        }
        if (switchNow)
        {
            if (capturer)
                capturer->Stop();
            m_source = next;
            capturer.reset(createCapturer(m_source));
            capturerOk = capturer && capturer->Start();
            m_lastFrame.clear();
            m_encoder.Close();
            m_mouseFx.Reset();
            m_keyRequested.store(true);
            if (!capturerOk)
                notify(share_host_capture_failed, 0);
            LOG_INFO("screenshare host: switched source, kind=%d, ok=%d.", m_source.m_kind, capturerOk ? 1 : 0);
        }

        //暂停：不取新画面；刚进入暂停时通知观看端
        const bool paused = m_paused.load();
        if (paused != wasPaused)
        {
            wasPaused = paused;
            m_sourceState = paused ? screenshare_source_paused : screenshare_source_sharing;
            sendSourceState(m_sourceState);
            notify(share_host_source_state, m_sourceState);
        }

        bool newFrame = false;
        if (!paused && capturerOk && m_sourceState != screenshare_source_closed)
        {
            ShareCaptureFrame frame;
            const int r = capturer->Capture(static_cast<UINT>(pacer.minIntervalMs()), frame);
            if (r == share_capture_ok)
            {
                copyLastFrame(frame);
                newFrame = true;
            }
            else if (r == share_capture_closed)
            {
                m_sourceState = screenshare_source_closed;
                sendSourceState(m_sourceState);
                notify(share_host_source_state, m_sourceState);
            }
            else if (r == share_capture_lost || r == share_capture_fatal)
            {
                //锁屏、分辨率变化等：稍等后重建采集器
                capturer->Stop();
                ::Sleep(kCapturerRetryWaitMs);
                capturerOk = capturer->Start();
            }

            //最小化状态变化：通知观看端
            const int state = capturer->IsMinimized() ? screenshare_source_minimized : screenshare_source_sharing;
            if (m_sourceState != screenshare_source_closed && state != m_sourceState)
            {
                m_sourceState = state;
                sendSourceState(m_sourceState);
                notify(share_host_source_state, m_sourceState);
            }
            std::lock_guard<std::mutex> guard(m_mutex);
            m_sourceRect = capturer->SourceRect();
        }
        else
        {
            ::Sleep(kIdleWaitMs);
        }
        //刚打开录制旁路：把最近一帧补给录制
        if (m_recordSeed.load() && !m_lastFrame.empty() && m_recordSeed.exchange(false))
        {
            std::lock_guard<std::mutex> guard(m_recordMutex);
            m_recordFrame = m_lastFrame;
            m_recordW = m_lastW;
            m_recordH = m_lastH;
            ++m_recordSerial;
        }

        const bool keyRequested = m_keyRequested.exchange(false);
        const int64_t now = steadyMs();
        //鼠标效果（第 16 期）：光标移动、效果淡出时画面内容可能没变，由效果一环要求多编一帧；暂停时不画
        bool fxNeedsFrame = false;
        if (!paused && !m_lastFrame.empty())
        {
            POINT cursor;
            bool leftDown = false;
            bool rightDown = false;
            m_cursorSampler.Sample(cursor, leftDown, rightDown, now);
            RECT sourceRect;
            {
                std::lock_guard<std::mutex> guard(m_mutex);
                sourceRect = m_sourceRect;
            }
            fxNeedsFrame = m_mouseFx.Tick(cursor, leftDown, rightDown, sourceRect, m_lastW, m_lastH, now);
        }
        const ShareFramePacer::Decision d =
            pacer.decide(now, newFrame || fxNeedsFrame, keyRequested, !m_lastFrame.empty());
        if (!d.m_encode || m_lastFrame.empty())
        {
            //关键帧请求暂时编不了（还没有画面）：留到下一轮
            if (keyRequested && m_lastFrame.empty())
                m_keyRequested.store(true);
            continue;
        }
        //把鼠标效果画进画面副本（没有可见效果时就是原始画面）；暂停时编的是暂停前的画面，不画效果
        int stride = m_lastW * kBytesPerPixel;
        const uint8_t* pixels = &m_lastFrame[0];
        if (!paused)
            pixels = m_mouseFx.Compose(&m_lastFrame[0], m_lastW, m_lastH, stride, now, stride);
        ShareCaptureFrame last;
        last.m_data = pixels;
        last.m_stride = stride;
        last.m_width = m_lastW;
        last.m_height = m_lastH;
        //录制旁路：留一份编码用的画面，录下的内容与观看者看到的一致（带鼠标效果，第 16 期起在这里取）
        if (m_recordTap.load())
        {
            std::lock_guard<std::mutex> guard(m_recordMutex);
            m_recordFrame.assign(pixels, pixels + static_cast<size_t>(stride) * m_lastH);
            m_recordW = m_lastW;
            m_recordH = m_lastH;
            ++m_recordSerial;
        }
        const uint64_t tsUs = static_cast<uint64_t>(now - startMs) * 1000;
        //码率自适应：把这段时间的关键帧请求计入，每个评估周期最多调整一次码率
        m_bitrate.OnKeyFrameRequests(m_keyRequestCount.exchange(0));
        int bps = 0;
        if (m_bitrate.Evaluate(now, bps))
        {
            m_encoder.SetBitrate(bps);
            LOG_INFO("screenshare host: bitrate -> %d bps.", bps);
        }
        m_encoder.Encode(last, d.m_keyframe, tsUs, this);
        pacer.onEncoded(now, d.m_keyframe || m_encoder.LastWasRebuild());
        if (!paused)
            m_mouseFx.OnEncoded();
    }

    if (capturer)
        capturer->Stop();
    capturer.reset();
    m_encoder.Close();
    const bool lost = m_relayLost.load() && !m_stop.load();
    m_relayClient.Close();
    if (lost)
        notify(share_host_relay_lost, 0);
    LOG_INFO("screenshare host: worker exit, frames=%llu keyframes=%llu.",
             static_cast<unsigned long long>(m_sentFrames.load()),
             static_cast<unsigned long long>(m_sentKeyFrames.load()));
    ::MFShutdown();
    ::CoUninitialize();
}

void ShareHostStreamer::OnShareEncoded(const uint8_t* data, size_t len, bool keyframe, uint64_t timestampUs)
{
    const std::string payload = ShareFraming::buildVideoPayload(m_encoder.EncodeWidth(), m_encoder.EncodeHeight(),
                                                                reinterpret_cast<const char*>(data), len);
    std::vector<std::string> bodies;
    ShareFraming::fragmentFrame(screenshare_track_screen, m_relay.m_seat, keyframe, timestampUs, payload.data(),
                                payload.size(), kShareFragmentPayloadBytes, bodies);
    //发送阻塞的时长交给码率自适应：发送缓冲区满了 send 才会阻塞，说明上行带宽跟不上（本回调在工作线程上）
    const int64_t sendStartMs = steadyMs();
    const bool sent = routeBodies(bodies, true, keyframe);
    m_bitrate.OnFrameSent(steadyMs() - sendStartMs);
    if (!sent)
        return;
    m_sentFrames.fetch_add(1);
    if (keyframe)
    {
        m_sentKeyFrames.fetch_add(1);
        //每个关键帧后补发一次状态：后加入的观看者据此得知当前状态
        sendSourceState(m_sourceState);
    }
}

void ShareHostStreamer::SetAudioSession(ShareAudioSession* audio)
{
    m_audio.store(audio);
}

void ShareHostStreamer::SetCameraEndpoints(ShareCameraSender* sender, ShareCameraReceiver* receiver)
{
    m_cameraSender.store(sender);
    m_cameraReceiver.store(receiver);
}

void ShareHostStreamer::SetRecordTapEnabled(bool on)
{
    m_recordTap.store(on);
    //最近一帧只在工作线程上读写：请工作线程下一轮补一份
    m_recordSeed.store(on);
    if (on)
        return;
    std::lock_guard<std::mutex> guard(m_recordMutex);
    m_recordFrame.clear();
    m_recordW = 0;
    m_recordH = 0;
}

bool ShareHostStreamer::GetRecordFrame(std::vector<uint8_t>& bgra, int& width, int& height, uint64_t& serial)
{
    std::lock_guard<std::mutex> guard(m_recordMutex);
    if (m_recordFrame.empty())
        return false;
    bgra = m_recordFrame;
    width = m_recordW;
    height = m_recordH;
    serial = m_recordSerial;
    return true;
}

void ShareHostStreamer::SetAnnotationSink(ShareAnnotationSink* sink)
{
    m_annotSink.store(sink);
}

void ShareHostStreamer::SetCaptionSink(ShareCaptionSink* sink)
{
    m_captionSink.store(sink);
}

void ShareHostStreamer::SetInputSink(ShareInputSink* sink)
{
    m_inputSink.store(sink);
}

bool ShareHostStreamer::SendRoomBody(const std::string& body)
{
    //有打开着的 P2P 观看者时经它发（声音、标注、摄像头一律照发），否则经房间中转
    if (IsP2PViewerOpen())
    {
        std::vector<std::string> bodies(1, body);
        return routeBodies(bodies, false, false);
    }
    if (!m_relayClient.IsConnected())
        return false;
    return m_relayClient.SendBody(body);
}

void ShareHostStreamer::OnRelayBody(const char* body, size_t len)
{
    handleRoomBody(body, len, m_reassembler);
}

void ShareHostStreamer::handleRoomBody(const char* body, size_t len, ShareFrameReassembler& reassembler)
{
    ShareMediaFrame frame;
    if (!reassembler.push(body, len, frame))
        return;
    //观看者的声音：交给房间语音会话
    if (frame.m_track == screenshare_track_mic || frame.m_track == screenshare_track_system_audio)
    {
        ShareAudioSession* audio = m_audio.load();
        if (audio != NULL)
            audio->OnRemoteFrame(frame);
        return;
    }
    //观看者的摄像头：交给摄像头接收端
    if (frame.m_track == screenshare_track_camera)
    {
        ShareCameraReceiver* cameras = m_cameraReceiver.load();
        if (cameras != NULL)
            cameras->OnRemoteFrame(frame);
        return;
    }
    //观看者的标注：交给标注接收方
    if (frame.m_track == screenshare_track_annotation)
    {
        ShareAnnotationSink* annot = m_annotSink.load();
        if (annot != NULL)
            annot->OnAnnotationFrame(frame.m_senderSeat, frame.m_data);
        return;
    }
    //观看者的实时字幕（语音字幕）：交给字幕接收方
    ShareCaptionText caption;
    if (ParseShareCaptionFrame(frame, caption))
    {
        ShareCaptionSink* sink = m_captionSink.load();
        if (sink != NULL)
            sink->OnCaptionFrame(frame.m_senderSeat, caption);
        return;
    }
    //控制者的键鼠输入（第 18 期）：解析后交给接收方，由它在 UI 线程上核对控制者并注入
    int inputTarget = -1;
    ShareInputEvent input;
    if (ShareRemoteInput::ParseInputFrame(frame, inputTarget, input))
    {
        ShareInputSink* sink = m_inputSink.load();
        if (sink != NULL)
            sink->OnInputFrame(frame.m_senderSeat, inputTarget, input);
        return;
    }
    //其他共享者的屏幕画面与共享内容状态（第 22 期多人共享）：按画面座位解码，主持人据此看别人的屏幕
    if (frame.m_track == screenshare_track_screen)
    {
        m_screens.OnScreenFrame(frame);
        return;
    }
    int remoteState = 0;
    if (ShareFraming::parseSourceState(frame, remoteState))
    {
        m_screens.SetSourceState(frame.m_senderSeat, remoteState);
        return;
    }
    int track = 0;
    if (ShareFraming::parseKeyFrameRequest(frame, track) && track == screenshare_track_screen)
    {
        m_keyRequested.store(true);
        m_keyRequestCount.fetch_add(1);
        LOG_INFO("screenshare host: key frame requested by relay.");
        //有新成员连上中转时服务端会请求关键帧：控制器据此把已有的标注补发一次
        notify(share_host_key_requested, 0);
    }
    //本人摄像头轨的关键帧请求（有新成员加入）
    else if (track == screenshare_track_camera)
    {
        ShareCameraSender* camera = m_cameraSender.load();
        if (camera != NULL)
            camera->RequestKeyFrame();
    }
}

void ShareHostStreamer::OnRelayThreadStart()
{
    //其他共享者的屏幕画面在接收线程上解码，本线程要初始化 COM 与 MF
    ::CoInitializeEx(NULL, COINIT_MULTITHREADED);
    ::MFStartup(MF_VERSION);
    m_screens.SetListener(this);
}

void ShareHostStreamer::OnRelayThreadExit()
{
    //解码器在本线程上建、在本线程上关（MF 对象要在 MFShutdown 之前释放）
    m_screens.Clear();
    ::MFShutdown();
    ::CoUninitialize();
}

void ShareHostStreamer::OnScreenDecoded(int seat)
{
    notify(share_host_remote_screen, seat);
}

bool ShareHostStreamer::GetScreenFrame(int seat, std::vector<uint8_t>& bgra, int& width, int& height,
                                       uint64_t& serial) const
{
    return m_screens.GetLatestFrame(seat, bgra, width, height, serial);
}

std::vector<int> ShareHostStreamer::ScreenSeats() const
{
    return m_screens.Seats();
}

int ShareHostStreamer::ScreenSourceState(int seat) const
{
    return m_screens.SourceState(seat);
}

void ShareHostStreamer::RemoveRemoteScreen(int seat)
{
    m_screens.Remove(seat);
}

void ShareHostStreamer::OnRelayClosed()
{
    LOG_WARNING("screenshare host: relay connection lost.");
    m_relayLost.store(true);
}

} // namespace screenshare
