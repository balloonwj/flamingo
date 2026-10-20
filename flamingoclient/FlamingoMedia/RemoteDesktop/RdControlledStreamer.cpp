/**
 *  远程桌面被控端推流器实现，RdControlledStreamer.cpp
 *  2026-10-15 起另有 H.264 格式（docs/远程桌面H264与共享画质提升方案.md 4.4～4.6），见 RdControlledStreamer.h。
 *  balloonwj@qq.com   2026-06-09
 */
#include "RdControlledStreamer.h"

#include "RdScreenCapturer.h"     // RdScreenCapturer（含 windows.h）
#include "RdEngineDef.h"          // rd_frame_request_keyframe（控制端请求关键帧标志）、rd_ctl_video_ack
#include "RdKeyframePolicy.h"     // RdShouldSendKeyframe：何时发关键帧（2026-10-12 起分辨率变化时也发）
#include "RdInputInjector.h"      // RdInputInjector::inject（含 RdInputDef.h：rd_input_mouse/key）
#include "RdInputEncoder.h"       // RdInputEncoder：收回控制时把补发的松开命令编码成字节再注入
#include "RdMessages.h"           // kMsgRdStreamerConnectFailed
#include "../Share/ShareVideoPolicy.h"   // RdScreenVideoParams、ShareFramePacer（2026-10-15）
#include "../Call/MfH264Codec.h"         // 试建 H.264 编码器，判断本机能否编（2026-10-15）
#include "../../Source/Common/Log/IULog.h"             // LOG_INFO / LOG_ERROR

#include <objbase.h>
#include <mfapi.h>
#include <mmsystem.h>   // timeBeginPeriod / timeEndPeriod：推流期间提高系统定时器精度（2026-10-17）
#include <vector>
#include <chrono>

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "winmm.lib")

namespace remotedesktop {

//旧格式的抓屏推流周期（毫秒）：5 帧 / 秒，与引擎自测一致，画质 / 带宽平衡，一期足够。
static const int kCaptureIntervalMs = 200;
//抓屏位深：一期固定 16 位色。
static const int kCaptureBitCount = 16;
//关键帧重发间隔（帧数）：每隔这么多增量帧重发一次关键帧（BITMAPINFO + 首屏），
//即每约 2 秒（10 × 200ms），让晚连 / 丢帧的控制端能在下个周期重新对齐就绪。
static const int kFramesPerKeyframe = 10;
//推流统计日志的间隔（毫秒）：每隔这么久记一次已发的帧数、关键帧数、字节数与 P2P 链路统计，
//控制端迟迟没有画面时与它的「no picture yet」日志对照，判断数据停在哪一段
static const ULONGLONG kStreamStatsLogIntervalMs = 10000;
//H.264 循环每轮抓取等新画面的最长时间（毫秒）：屏幕不变时也能按约 30 次每秒更新指针（2026-10-15）
static const UINT kVideoCaptureWaitMs = 33;
//旧格式每轮抓取等新画面的最长时间（毫秒）：与 RdScreenCapturer 的默认值相同（切回旧格式时恢复）
static const UINT kLegacyCaptureWaitMs = 120;
//还没定格式（等控制端的请求）时每轮等待的时间（毫秒）
static const int kUndecidedWaitMs = 33;
//试建 H.264 编码器判断本机能否编时用的尺寸、帧率与码率（只建不编）
static const int kProbeEncodeW = 640;
static const int kProbeEncodeH = 360;
static const int kProbeEncodeFps = 30;
static const int kProbeEncodeBps = 1000000;
//微秒与毫秒的换算
static const uint64_t kUsPerMs = 1000;
//推流期间要求的系统定时器精度（毫秒，2026-10-17）。Windows 默认约 15.6 毫秒，推流循环每轮补足时长用的 sleep_for 按这个
//粒度醒来：补足 16 毫秒实际睡约 31 毫秒、补足 33 毫秒实际睡约 47 毫秒（tmp/rdlatency/sleep_granularity.py 实测），
//指针在动时每秒只能更新二三十次。走 P2P 时 FlamingoP2P 的 IO 线程忙时会提高精度，走中转时没有，这里自己提高；
//Win10 2004 起只影响本进程
static const UINT kRdTimerResolutionMs = 1;

//DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 的哨兵值（避免依赖较新 SDK 头），
//与 FlamingoScreenshot/Capture/ScreenCaptureOverlay.cpp 取值一致。
static const HANDLE kPmv2Context = reinterpret_cast<HANDLE>(static_cast<INT_PTR>(-4));

//SetThreadDpiAwarenessContext 的函数指针类型（Win10 1607+）。
typedef HANDLE (WINAPI* FN_SetThreadCtx)(HANDLE);

//取 SetThreadDpiAwarenessContext；老系统无此 API 返回 NULL，调用方退回原行为（逻辑像素）。
static FN_SetThreadCtx getSetThreadCtxFn()
{
    HMODULE u = ::GetModuleHandleW(L"user32.dll");
    if (u == NULL)
        return NULL;
    return reinterpret_cast<FN_SetThreadCtx>(::GetProcAddress(u, "SetThreadDpiAwarenessContext"));
}

//单调时钟的毫秒数（推流线程的计时与视频帧的发送时刻共用）。2026-10-17 起取 RdStreamClockMs（steady_clock），
//原先的 GetTickCount64 精度约 15.6 毫秒，画面持续变化时只编出约 20 fps
static int64_t steadyNowMs()
{
    return RdStreamClockMs();
}

//试建一个系统软件编码器：建得起来说明本机能编 H.264（调用线程须已初始化 COM 与 Media Foundation）
static bool probeH264Encoder()
{
    call::MfH264Encoder probe;
    const bool ok = probe.Init(kProbeEncodeW, kProbeEncodeH, kProbeEncodeFps, kProbeEncodeBps);
    probe.Close();
    return ok;
}

RdControlledStreamer::RdControlledStreamer()
    : m_notifyWnd(NULL)
    , m_running(false)
    , m_stopping(false)
    , m_keyframeRequested(false)
    , m_transportReady(false)
    , m_startSeq(0)
    , m_droppedInputs(0)
    , m_ackPending(false)
    , m_ackEchoMs(0)
    , m_capturer(NULL)
    , m_state(NULL)
    , m_bitrate(screenshare::kRdVideoBitrateBps, screenshare::kShareMinBitrateBps)
    , m_pacer(screenshare::kRdVideoMaxFps, screenshare::kShareKeyFrameIntervalMs)
{
    //控制者的输入落在本进程（Flamingo 自己）的窗口上时不注入
    m_controlGate.SetOwnWindowProbe(&RdControlGate::IsOnOwnProcessWindow);
}

RdControlledStreamer::~RdControlledStreamer()
{
    Stop();
}

bool RdControlledStreamer::Start(const RdTransportParams& params, HWND notifyWnd)
{
    if (m_running.load())
        return true;
    //上一次的推流线程可能已经因断开而结束（m_running 为 false），先回收它
    Stop();

    m_stopping.store(false);
    m_keyframeRequested.store(false);
    m_ackPending.store(false);
    {
        std::lock_guard<std::mutex> guard(m_requestMutex);
        m_request = RdKeyframeRequestInfo();
    }
    //每次会话都从「只能查看」开始，被控者同意或直接允许之后才打开
    m_controlGate.Reset();
    m_droppedInputs.store(0);
    m_params = params;
    m_notifyWnd = notifyWnd;
    ++m_startSeq;
    m_running.store(true);
    m_pushThread.reset(new std::thread(&RdControlledStreamer::PushProc, this));
    LOG_INFO("RdControlledStreamer: started, session=%s, relay %s:%d, p2p offered=%d.", params.m_sessionId.c_str(),
             params.m_relayIp.c_str(), params.m_relayPort, params.m_slot ? 1 : 0);
    return true;
}

void RdControlledStreamer::Stop()
{
    m_stopping.store(true);
    m_running.store(false);
    //会话结束：关闭把关并补发松开，丢弃计数只在本次会话确有丢弃时打一条日志
    SetControlAllowed(false);
    const unsigned int dropped = m_droppedInputs.exchange(0);
    if (dropped > 0)
        LOG_INFO("RdControlledStreamer: %u input commands dropped by the control gate in this session.", dropped);

    //已连上时先关传输：促使推流线程阻塞中的 SendEngineFrame 失败、循环退出。还在连接时不能关（RdTransport
    //不允许与 Connect 并发关闭），推流线程等链路时每 100 毫秒检查一次停止标志，连中转时最多等连接超时
    {
        std::lock_guard<std::mutex> guard(m_connectMutex);
        if (m_transportReady)
            m_transport.Close();
    }

    if (m_pushThread && m_pushThread->joinable())
    {
        if (std::this_thread::get_id() != m_pushThread->get_id())
            m_pushThread->join();
        else
            m_pushThread->detach();
    }
    m_pushThread.reset();

    //推流线程已经结束：此时关闭总是安全的（包括连接刚成功、还没来得及置位的情况）
    m_transport.Close();
    std::lock_guard<std::mutex> guard(m_connectMutex);
    m_transportReady = false;
}

bool RdControlledStreamer::IsRunning() const
{
    return m_running.load();
}

void RdControlledStreamer::OnRelayEngineFrame(const unsigned char* data, size_t len)
{
    if (len < 1)
        return;

    //控制端发来的帧按首字节类型分发：请求关键帧 / 视频回执 / 鼠标命令 / 键盘命令
    unsigned char type = data[0];
    if (type == static_cast<unsigned char>(rd_frame_request_keyframe))
    {
        //控制端连上中转 / 缺关键帧时发出：置标志让推流线程下一轮立即补发关键帧。2026-10-15 起新控制端附带能力位，
        //推流线程据此选格式（旧控制端只发 1 字节，视为不能解 H.264）
        RdKeyframeRequestInfo info;
        RdVideoFrames::ParseKeyframeRequest(data, len, info);
        {
            std::lock_guard<std::mutex> guard(m_requestMutex);
            m_request = info;
        }
        m_keyframeRequested.store(true);
        return;
    }
    if (type == static_cast<unsigned char>(rd_ctl_video_ack))
    {
        //视频回执（2026-10-15）：记下回送的发送时刻，推流线程据此算延迟交给码率自适应
        uint32_t echoed = 0;
        if (RdVideoFrames::ParseVideoAck(data, len, echoed))
        {
            m_ackEchoMs.store(echoed);
            m_ackPending.store(true);
        }
        return;
    }
    if (type == static_cast<unsigned char>(rd_input_mouse)
        || type == static_cast<unsigned char>(rd_input_key))
    {
        //鼠标 / 键盘控制命令：先经控制把关（未授权、落在本进程窗口上的都丢弃），通过的才注入本机
        RdInputCommand cmd;
        if (!RdInputInjector::parse(data, len, cmd))
            return;
        if (!m_controlGate.Filter(cmd))
        {
            //只在第一条被丢弃时记日志，避免控制者在未授权时移动鼠标刷满日志
            if (m_droppedInputs.fetch_add(1) == 0)
                LOG_INFO("RdControlledStreamer: input dropped by the control gate, allowed=%d, type=%d, message=0x%x.",
                         m_controlGate.IsAllowed() ? 1 : 0, cmd.type, cmd.message);
            return;
        }
        RdInputInjector::inject(data, len);
    }
}

bool RdControlledStreamer::takeKeyframeRequest(RdKeyframeRequestInfo& info)
{
    if (!m_keyframeRequested.exchange(false))
        return false;
    std::lock_guard<std::mutex> guard(m_requestMutex);
    info = m_request;
    return true;
}

void RdControlledStreamer::SetControlAllowed(bool allowed)
{
    POINT cursor = { 0, 0 };
    ::GetCursorPos(&cursor);
    std::vector<RdInputCommand> releases;
    const bool wasAllowed = m_controlGate.IsAllowed();
    m_controlGate.SetAllowed(allowed, cursor, releases);
    //关闭时补发控制者按下、尚未松开的键与鼠标按钮，避免被控者的电脑上留下按住的键
    for (size_t i = 0; i < releases.size(); ++i)
    {
        std::vector<BYTE> bytes;
        if (releases[i].type == rd_input_mouse)
            RdInputEncoder::encodeMouse(bytes, releases[i].message, releases[i].x, releases[i].y, 0);
        else
            RdInputEncoder::encodeKey(bytes, releases[i].message, releases[i].vkey);
        RdInputInjector::inject(bytes.data(), bytes.size());
    }
    if (wasAllowed != allowed || !releases.empty())
        LOG_INFO("RdControlledStreamer: control %s, released %u inputs.", allowed ? "allowed" : "disallowed",
                 static_cast<unsigned>(releases.size()));
}

void RdControlledStreamer::OnRelayClosed()
{
    //中转断开：标记停止；会话级 UI 收尾由协调器据 cmd_rd_state_notify 处理
    m_running.store(false);
}

void RdControlledStreamer::PushProc()
{
    const bool connected = m_transport.Connect(m_params, this, &m_stopping);
    {
        std::lock_guard<std::mutex> guard(m_connectMutex);
        m_transportReady = connected;
    }
    if (!connected)
    {
        m_running.store(false);
        //主动停止时不算失败；否则请控制器提示用户并结束会话（wParam 带序号，控制器据此丢弃迟到的通知）
        if (!m_stopping.load())
        {
            LOG_ERROR("RdControlledStreamer: connect failed, session=%s.", m_params.m_sessionId.c_str());
            if (m_notifyWnd != NULL && ::IsWindow(m_notifyWnd))
                ::PostMessage(m_notifyWnd, kMsgRdStreamerConnectFailed, (WPARAM)m_startSeq.load(), 0);
        }
        return;
    }
    LOG_INFO("RdControlledStreamer: connected, session=%s, path=%d.", m_params.m_sessionId.c_str(),
             m_transport.Path());
    //H.264 编码要求本线程已初始化 COM（多线程套间）与 Media Foundation（2026-10-15）
    const HRESULT comHr = ::CoInitializeEx(NULL, COINIT_MULTITHREADED);
    ::MFStartup(MF_VERSION);
    //推流期间提高系统定时器精度，每轮补足的时长才能睡准（见 kRdTimerResolutionMs），推流结束时恢复
    const bool hiRes = (::timeBeginPeriod(kRdTimerResolutionMs) == TIMERR_NOERROR);
    LOG_INFO("RdControlledStreamer: high resolution timer %s.", hiRes ? "on" : "unavailable");
    PushLoop();
    if (hiRes)
        ::timeEndPeriod(kRdTimerResolutionMs);
    m_videoEncoder.Close();
    ::MFShutdown();
    if (SUCCEEDED(comHr))
        ::CoUninitialize();
}

bool RdControlledStreamer::sendFrame(const std::vector<unsigned char>& frame, PushState& st)
{
    if (frame.empty())
        return true;
    if (!m_transport.SendEngineFrame(frame.data(), frame.size()))
    {
        st.m_sendFailed = true;
        return false;
    }
    ++st.m_framesSent;
    st.m_bytesSent += frame.size();
    return true;
}

bool RdControlledStreamer::sendCaps(PushState& st)
{
    std::vector<unsigned char> caps;
    RdVideoFrames::BuildCaps(kRdCapH264, caps);
    return sendFrame(caps, st);
}

void RdControlledStreamer::PushLoop()
{
    //把本线程切到 Per-Monitor-V2：使抓屏在物理像素（Flamingo 是 DPI-unaware 进程，
    //否则 GetSystemMetrics / BitBlt 落在被 DWM 拉伸的逻辑像素、画面模糊放大）。推流
    //线程独立，切换只影响本线程、不污染主 UI 线程，故线程退出即可、无需显式恢复。
    FN_SetThreadCtx setCtx = getSetThreadCtxFn();
    if (setCtx != NULL)
        setCtx(kPmv2Context);

    RdScreenCapturer capturer(kCaptureBitCount);
    PushState st;
    //关键帧（BITMAPINFO + 首屏全量）重发间隔：每约 2 秒一次（kFramesPerKeyframe 帧 ×
    //kCaptureIntervalMs）。控制端可能晚于被控端连上中转、错过最初的关键帧（被中转丢弃，
    //且从分片中段开始无法重组）；周期重发让晚连 / 丢帧的控制端能在下个周期重新对齐并就绪
    //——BITMAPINFO 是 whole 帧，会触发对端分片重组器 Reset 清掉错位残片，随后首屏的分片
    //就能从干净状态重组。buildKeyframe 同时把编码器的 DIFF 基准设为这一屏，之后的
    //buildNext 即以此为基准，与控制端收到首屏后重置的画面保持一致。
    //走 P2P 时不定时重发（RdTransport::NeedsPeriodicKeyframes）：帧通道可靠有序，控制端连上后会请求一次关键帧，
    //定时重发 4 MB 的首屏只会白占带宽、让其后的增量帧晚到 1 秒多（见 docs/P2P通信方案.md 第 14.26 节）
    st.m_periodicKeyframes = RdTransport::NeedsPeriodicKeyframes(m_transport.Path());
    st.m_framesSinceKeyframe = 0;
    st.m_needKeyframe = true;   //首轮必发关键帧
    st.m_sentW = 0;
    st.m_sentH = 0;
    st.m_framesSent = 0;
    st.m_keyframesSent = 0;
    st.m_bytesSent = 0;
    st.m_sendFailed = false;
    st.m_startMs = steadyNowMs();
    st.m_firstVideoSent = false;
    st.m_firstVideoMs = 0;
    st.m_cursorPos.x = 0;
    st.m_cursorPos.y = 0;
    st.m_cursorType = 0;
    st.m_cursorSent = false;
    st.m_srcW = 0;
    st.m_srcH = 0;
    st.m_canEncode = false;
    m_capturer = &capturer;
    m_state = &st;

    //选格式（2026-10-15）：本机能否编 H.264 先试建一次；之后按控制端的请求决定
    const bool canEncode = probeH264Encoder();
    st.m_canEncode = canEncode;
    RdFormatChooser chooser(canEncode);
    chooser.Start(st.m_startMs);
    m_pacer = screenshare::ShareFramePacer(screenshare::kRdVideoMaxFps, screenshare::kShareKeyFrameIntervalMs);
    m_bitrate = screenshare::ShareBitrateController(screenshare::kRdVideoBitrateBps, screenshare::kShareMinBitrateBps);
    m_videoEncoder.Close();
    int lastFormat = rd_format_undecided;
    LOG_INFO("RdControlledStreamer: push loop started, path=%d, periodic keyframes=%d, can encode h264=%d.",
             m_transport.Path(), st.m_periodicKeyframes ? 1 : 0, canEncode ? 1 : 0);
    ULONGLONG lastStatsTick = ::GetTickCount64();

    while (!m_stopping.load())
    {
        const ULONGLONG nowTick = ::GetTickCount64();
        if (nowTick - lastStatsTick >= kStreamStatsLogIntervalMs)
        {
            lastStatsTick = nowTick;
            p2p::P2PLinkStats stats;
            const bool onP2P = m_transport.GetP2PStats(stats);
            LOG_INFO("RdControlledStreamer: stats, format=%d, frames=%d, keyframes=%d, bytes=%llu, bitrate=%d, path=%d, "
                     "p2p=%d, rtt=%u ms, loss=%u permille, queued=%llu, sent=%llu, received=%llu, captured=%u, "
                     "identical=%u.", lastFormat, st.m_framesSent, st.m_keyframesSent, st.m_bytesSent,
                     m_bitrate.CurrentBps(), m_transport.Path(), onP2P ? 1 : 0, stats.m_rttMs, stats.m_lossPermille,
                     (unsigned long long)stats.m_queuedBytes, (unsigned long long)stats.m_bytesSent,
                     (unsigned long long)stats.m_bytesReceived, capturer.capturedFrames(), capturer.identicalFrames());
        }

        const int64_t now = steadyNowMs();
        //控制端请求了关键帧（连上中转 / 缺关键帧时发来）：本轮立即补发关键帧；请求里的能力位决定格式
        RdKeyframeRequestInfo request;
        if (takeKeyframeRequest(request))
        {
            st.m_needKeyframe = true;
            if (chooser.OnKeyframeRequest(request, now))
                LOG_INFO("RdControlledStreamer: keyframe request changed format, caps=%d, h264=%d, max long side=%d.",
                         request.m_hasCaps ? 1 : 0, request.m_canDecodeH264 ? 1 : 0, request.m_maxLongSide);
        }
        //视频回执：往返加排队的延迟交给码率自适应（只在发过视频帧之后有意义）
        if (m_ackPending.exchange(false) && st.m_firstVideoSent)
        {
            int64_t delay = 0;
            if (RdAckDelayMs(static_cast<uint32_t>(now), m_ackEchoMs.load(), st.m_firstVideoMs, delay))
                m_bitrate.OnFeedbackDelay(delay, now);
        }

        const int format = chooser.Current(now);
        if (format == rd_format_undecided)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(kUndecidedWaitMs));
            continue;
        }
        if (format != lastFormat)
        {
            LOG_INFO("RdControlledStreamer: format %d -> %d.", lastFormat, format);
            lastFormat = format;
            st.m_needKeyframe = true;
            if (format == rd_format_h264)
            {
                //编码长边：远程桌面参数的上限与控制端能解的最大长边中较小的一个
                screenshare::ScreenVideoParams params = screenshare::RdScreenVideoParams();
                params.m_maxLongSide = RdChooseEncodeLongSide(params.m_maxLongSide, chooser.ControllerMaxLongSide());
                m_videoEncoder.SetParams(params);
                capturer.setCaptureTimeout(kVideoCaptureWaitMs);
                st.m_cursorSent = false;
            }
            else
            {
                capturer.setCaptureTimeout(kLegacyCaptureWaitMs);
            }
        }

        if (format == rd_format_h264)
        {
            const int64_t roundStart = steadyNowMs();
            bool hasFrame = false;
            bool newFrame = false;
            bool cursorMoved = false;
            if (!pushVideoRound(capturer, st, hasFrame, newFrame, cursorMoved))
            {
                if (st.m_sendFailed)
                    break;
                //编码器彻底失败：退回旧格式，下一轮发旧格式的关键帧
                LOG_WARNING("RdControlledStreamer: h264 encoder failed, fall back to legacy format.");
                chooser.OnEncoderFailed();
                continue;
            }
            //补足这一轮的最短时长（2026-10-17）：DXGI 在屏幕不变时阻塞到 kVideoCaptureWaitMs 才返回，而抓取出错（隐藏桌面、
            //锁屏与 UAC 提示期间）时立即返回，原先这里不等待，推流线程空转、占满一个核。抓到新画面时不等，帧率由 pacer 控制
            //画面没变而指针在动时只补足约 16 毫秒，远端指针每秒约更新 60 次（2026-10-17，此前与静止时一样补足 33 毫秒）
            const int64_t waitMs = RdVideoRoundWaitMs(steadyNowMs() - roundStart, hasFrame, newFrame, cursorMoved);
            if (waitMs > 0)
                std::this_thread::sleep_for(std::chrono::milliseconds(waitMs));
            continue;
        }
        if (!pushLegacyRound(capturer, st))
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(kCaptureIntervalMs));
    }

    m_capturer = NULL;
    m_state = NULL;
    m_running.store(false);
    LOG_INFO("RdControlledStreamer: push loop exited, frames=%d, keyframes=%d, bytes=%llu.", st.m_framesSent,
             st.m_keyframesSent, st.m_bytesSent);
}

bool RdControlledStreamer::pushLegacyRound(RdScreenCapturer& capturer, PushState& st)
{
    //先抓一帧（2026-10-14 起抓帧在判断之前）：画面尺寸变了时编码器当轮就报告需要关键帧，
    //不再先发一个只有帧头的增量帧、等下一轮才补发
    if (!capturer.captureFrame())
    {
        //抓取后端都不可用、从没抓到过画面：这一轮什么也不发，等下一轮
        return true;
    }
    const bool encoderNeedsKeyframe = capturer.needsKeyframe();
    if (RdShouldSendKeyframe(st.m_needKeyframe, st.m_periodicKeyframes, st.m_framesSinceKeyframe, kFramesPerKeyframe,
                             encoderNeedsKeyframe))
    {
        //不是请求或首轮、而是编码器因分辨率变化报告：记下新旧尺寸
        if (!st.m_needKeyframe && encoderNeedsKeyframe && st.m_sentW > 0)
            LOG_INFO("RdControlledStreamer: capture size %dx%d -> %dx%d, resend keyframe.", st.m_sentW, st.m_sentH,
                     capturer.frameWidth(), capturer.frameHeight());
        //先把 BITMAPINFO 与首屏都组好，再连续发出：两者发送时刻紧挨，控制端只要在这之前连上中转即可同时收到
        //（若 BITMAPINFO 先发、首屏晚到，BITMAPINFO 可能赶在控制端连上中转之前被丢弃，控制端缺它而无法就绪）
        std::vector<unsigned char> bmiBuf;
        std::vector<unsigned char> buf;
        capturer.buildKeyframe(bmiBuf, buf);
        //能力帧（2026-10-15）：本机能编 H.264 时在每个关键帧之前通告，最初的请求被中转丢掉的新控制端据此重发请求
        if (st.m_canEncode && !sendCaps(st))
        {
            LOG_INFO("RdControlledStreamer: send caps failed, stop pushing.");
            return false;
        }
        if (!sendFrame(bmiBuf, st))
        {
            LOG_INFO("RdControlledStreamer: send bitmapinfo failed, stop pushing.");
            return false;
        }
        if (!sendFrame(buf, st))
        {
            LOG_INFO("RdControlledStreamer: send first(full) frame failed, stop pushing.");
            return false;
        }
        st.m_framesSinceKeyframe = 0;
        st.m_needKeyframe = false;
        st.m_sentW = capturer.frameWidth();
        st.m_sentH = capturer.frameHeight();
        ++st.m_keyframesSent;
        LOG_INFO("RdControlledStreamer: sent keyframe (bitmapinfo + first), screen=%dx%d.",
                 capturer.screenWidth(), capturer.screenHeight());
        return true;
    }
    std::vector<unsigned char> buf;
    capturer.buildNext(buf);
    if (!sendFrame(buf, st))
    {
        LOG_INFO("RdControlledStreamer: send next frame failed, stop pushing.");
        return false;
    }
    ++st.m_framesSinceKeyframe;
    return true;
}

bool RdControlledStreamer::pushVideoRound(RdScreenCapturer& capturer, PushState& st, bool& hasFrame, bool& newFrame,
                                          bool& cursorMoved)
{
    hasFrame = capturer.captureFrame();
    newFrame = hasFrame && capturer.lastCaptureNew();
    cursorMoved = false;
    const int64_t now = steadyNowMs();

    //指针：位置或类型变了就发 5 号帧。指针在动时每轮约 16 毫秒（RdVideoRoundWaitMs），所以最多每秒约 60 次
    POINT pos;
    BYTE type = 0;
    capturer.cursor(pos, type);
    if (!st.m_cursorSent || pos.x != st.m_cursorPos.x || pos.y != st.m_cursorPos.y || type != st.m_cursorType)
    {
        cursorMoved = true;
        std::vector<unsigned char> frame;
        RdVideoFrames::BuildCursor(pos.x, pos.y, type, frame);
        if (!sendFrame(frame, st))
            return false;
        st.m_cursorPos = pos;
        st.m_cursorType = type;
        st.m_cursorSent = true;
    }
    if (!hasFrame)
        return true;

    //发帧节奏：画面不变不编；帧率上限 30；每 4 秒兜底关键帧；收到请求、换了格式时立即编关键帧。
    //需要关键帧时手里的画面也算新画面（2026-10-17）：发帧节奏从没编过码时不理会关键帧请求，会话中途由旧格式切到 H.264
    //时屏幕若恰好静止，第一帧要等到画面变化才编得出来
    const bool pacerNew = RdVideoPacerHasNewFrame(hasFrame, newFrame, st.m_needKeyframe);
    const screenshare::ShareFramePacer::Decision d = m_pacer.decide(now, pacerNew, st.m_needKeyframe, true);
    if (!d.m_encode)
        return true;
    //码率自适应：每个评估周期最多调整一次
    int bps = 0;
    if (m_bitrate.Evaluate(now, bps))
    {
        m_videoEncoder.SetBitrate(bps);
        LOG_INFO("RdControlledStreamer: bitrate -> %d bps.", bps);
    }
    screenshare::ShareCaptureFrame frame;
    frame.m_data = capturer.frameData();
    frame.m_stride = capturer.frameStride();
    frame.m_width = capturer.frameWidth();
    frame.m_height = capturer.frameHeight();
    st.m_srcW = frame.m_width;
    st.m_srcH = frame.m_height;
    const uint64_t tsUs = static_cast<uint64_t>(now - st.m_startMs) * kUsPerMs;
    if (!m_videoEncoder.Encode(frame, d.m_keyframe, tsUs, this))
    {
        //硬件、软件编码器都建不起来：交给推流循环退回旧格式（发送失败时 m_sendFailed 已置位）
        return false;
    }
    if (st.m_sendFailed)
        return false;
    m_pacer.onEncoded(now, d.m_keyframe || m_videoEncoder.LastWasRebuild());
    if (d.m_keyframe || m_videoEncoder.LastWasRebuild())
        st.m_needKeyframe = false;
    //编码尺寸变了（源尺寸、逐档退）导致码率范围变化：码率自适应换到新范围
    if (m_videoEncoder.TakeRangeChanged())
    {
        m_bitrate.SetRange(m_videoEncoder.MaxBps(), m_videoEncoder.MinBps(), m_videoEncoder.BitrateBps());
        LOG_INFO("RdControlledStreamer: source %dx%d, encode %dx%d, bitrate %d (range %d-%d).", frame.m_width,
                 frame.m_height, m_videoEncoder.EncodeWidth(), m_videoEncoder.EncodeHeight(),
                 m_videoEncoder.BitrateBps(), m_videoEncoder.MinBps(), m_videoEncoder.MaxBps());
    }
    return true;
}

void RdControlledStreamer::OnShareEncoded(const uint8_t* data, size_t len, bool keyframe, uint64_t timestampUs)
{
    (void)timestampUs;
    if (m_state == NULL || m_state->m_sendFailed)
        return;
    PushState& st = *m_state;
    const uint32_t sendMs = static_cast<uint32_t>(steadyNowMs());
    //关键帧之前先通告能力（不论格式），控制端据此确认对方能编 H.264
    if (keyframe && !sendCaps(st))
        return;
    RdVideoFrameHeader h;
    h.m_srcW = st.m_srcW;
    h.m_srcH = st.m_srcH;
    h.m_encW = m_videoEncoder.EncodeWidth();
    h.m_encH = m_videoEncoder.EncodeHeight();
    h.m_keyframe = keyframe;
    h.m_sendMs = sendMs;
    std::vector<unsigned char> frame;
    RdVideoFrames::BuildVideo(h, data, len, frame);
    //发送阻塞的时长交给码率自适应：发送缓冲区满了 send 才会阻塞，说明上行跟不上
    const int64_t sendStart = steadyNowMs();
    if (!sendFrame(frame, st))
    {
        LOG_INFO("RdControlledStreamer: send video frame failed, stop pushing.");
        return;
    }
    m_bitrate.OnFrameSent(steadyNowMs() - sendStart);
    if (!st.m_firstVideoSent)
    {
        st.m_firstVideoSent = true;
        st.m_firstVideoMs = sendMs;
    }
    if (keyframe)
    {
        ++st.m_keyframesSent;
        //每个关键帧之后补发一次指针：控制端刚切到新画面、或刚连上时就能画出指针
        std::vector<unsigned char> cursor;
        RdVideoFrames::BuildCursor(st.m_cursorPos.x, st.m_cursorPos.y, st.m_cursorType, cursor);
        sendFrame(cursor, st);
        LOG_INFO("RdControlledStreamer: sent h264 keyframe, source %dx%d, encode %dx%d, %u bytes.", h.m_srcW, h.m_srcH,
                 h.m_encW, h.m_encH, static_cast<unsigned>(frame.size()));
    }
}

} // namespace remotedesktop
