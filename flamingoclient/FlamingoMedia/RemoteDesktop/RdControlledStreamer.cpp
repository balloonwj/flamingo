/**
 *  远程桌面被控端推流器实现，RdControlledStreamer.cpp
 *  balloonwj@qq.com   2026-06-09
 */
#include "RdControlledStreamer.h"

#include "RdScreenCapturer.h"     // RdScreenCapturer（含 windows.h）
#include "RdEngineDef.h"          // rd_frame_request_keyframe（控制端请求关键帧标志）
#include "RdInputInjector.h"      // RdInputInjector::inject（含 RdInputDef.h：rd_input_mouse/key）
#include "RdMessages.h"           // kMsgRdStreamerConnectFailed
#include "../../Source/Common/Log/IULog.h"             // LOG_INFO / LOG_ERROR

#include <vector>
#include <chrono>

namespace remotedesktop {

//抓屏推流周期（毫秒）：5 帧 / 秒，与引擎自测一致，画质 / 带宽平衡，一期足够。
static const int kCaptureIntervalMs = 200;
//抓屏位深：一期固定 16 位色。
static const int kCaptureBitCount = 16;
//关键帧重发间隔（帧数）：每隔这么多增量帧重发一次关键帧（BITMAPINFO + 首屏），
//即每约 2 秒（10 × 200ms），让晚连 / 丢帧的控制端能在下个周期重新对齐就绪。
static const int kFramesPerKeyframe = 10;
//推流统计日志的间隔（毫秒）：每隔这么久记一次已发的帧数、关键帧数、字节数与 P2P 链路统计，
//控制端迟迟没有画面时与它的「no picture yet」日志对照，判断数据停在哪一段
static const ULONGLONG kStreamStatsLogIntervalMs = 10000;

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

RdControlledStreamer::RdControlledStreamer()
    : m_notifyWnd(NULL)
    , m_running(false)
    , m_stopping(false)
    , m_keyframeRequested(false)
    , m_transportReady(false)
    , m_startSeq(0)
{
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

    //控制端发来的帧按首字节类型分发：请求关键帧 / 鼠标命令 / 键盘命令
    unsigned char type = data[0];
    if (type == static_cast<unsigned char>(rd_frame_request_keyframe))
    {
        //控制端连上中转 / 缺关键帧时发出：置标志让推流线程下一轮立即补发关键帧（BITMAPINFO+首屏）
        m_keyframeRequested.store(true);
        return;
    }
    if (type == static_cast<unsigned char>(rd_input_mouse)
        || type == static_cast<unsigned char>(rd_input_key))
    {
        //鼠标 / 键盘控制命令：解析并注入到本机（SetCursorPos / mouse_event / keybd_event）
        RdInputInjector::inject(data, len);
    }
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
    PushLoop();
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
    std::vector<unsigned char> buf;

    //关键帧（BITMAPINFO + 首屏全量）重发间隔：每约 2 秒一次（kFramesPerKeyframe 帧 ×
    //kCaptureIntervalMs）。控制端可能晚于被控端连上中转、错过最初的关键帧（被中转丢弃，
    //且从分片中段开始无法重组）；周期重发让晚连 / 丢帧的控制端能在下个周期重新对齐并就绪
    //——BITMAPINFO 是 whole 帧，会触发对端分片重组器 Reset 清掉错位残片，随后首屏的分片
    //就能从干净状态重组。captureFirst 同时把抓屏的 DIFF 基准重置为当前全屏，紧接的
    //captureNext 即以此为基准，与控制端收到首屏后重置的画面保持一致。
    //走 P2P 时不定时重发（RdTransport::NeedsPeriodicKeyframes）：帧通道可靠有序，控制端连上后会请求一次关键帧，
    //定时重发 4 MB 的首屏只会白占带宽、让其后的增量帧晚到 1 秒多（见 docs/P2P通信方案.md 第 14.26 节）
    const bool periodicKeyframes = RdTransport::NeedsPeriodicKeyframes(m_transport.Path());
    LOG_INFO("RdControlledStreamer: push loop started, path=%d, periodic keyframes=%d.", m_transport.Path(),
             periodicKeyframes ? 1 : 0);
    int  framesSinceKeyframe = 0;
    bool needKeyframe = true;   //首轮必发关键帧
    //推流统计（只在本线程上访问）：已发的引擎帧数、其中关键帧的轮数、已发字节数，以及上一次写统计日志的时刻
    int                framesSent = 0;
    int                keyframesSent = 0;
    unsigned long long bytesSent = 0;
    ULONGLONG          lastStatsTick = ::GetTickCount64();

    while (!m_stopping.load())
    {
        const ULONGLONG now = ::GetTickCount64();
        if (now - lastStatsTick >= kStreamStatsLogIntervalMs)
        {
            lastStatsTick = now;
            p2p::P2PLinkStats stats;
            const bool onP2P = m_transport.GetP2PStats(stats);
            LOG_INFO("RdControlledStreamer: stats, frames=%d, keyframes=%d, bytes=%llu, path=%d, p2p=%d, rtt=%u ms, "
                     "loss=%u permille, queued=%llu, sent=%llu, received=%llu.", framesSent, keyframesSent, bytesSent,
                     m_transport.Path(), onP2P ? 1 : 0, stats.m_rttMs, stats.m_lossPermille,
                     (unsigned long long)stats.m_queuedBytes, (unsigned long long)stats.m_bytesSent,
                     (unsigned long long)stats.m_bytesReceived);
        }

        //控制端请求了关键帧（连上中转 / 缺关键帧时发来）：本轮立即补发一帧关键帧
        if (m_keyframeRequested.exchange(false))
            needKeyframe = true;

        if (needKeyframe || (periodicKeyframes && framesSinceKeyframe >= kFramesPerKeyframe))
        {
            //先把 BITMAPINFO 与首屏都抓好，再连续发出：captureFirst 抓全屏较耗时，若先发
            //BITMAPINFO 再抓首屏，BITMAPINFO 会比首屏早几十毫秒发出、可能赶在控制端连上中转
            //之前被丢弃（控制端就因此只收到首屏、缺 BITMAPINFO 而无法就绪）。抓完再发让两者
            //发送时刻紧挨，控制端只要在抓首屏期间连上即可同时收到。
            std::vector<unsigned char> bmiBuf;
            capturer.captureBitmapInfo(bmiBuf);
            capturer.captureFirst(buf);
            if (!m_transport.SendEngineFrame(bmiBuf.data(), bmiBuf.size()))
            {
                LOG_INFO("RdControlledStreamer: send bitmapinfo failed, stop pushing.");
                break;
            }
            if (!m_transport.SendEngineFrame(buf.data(), buf.size()))
            {
                LOG_INFO("RdControlledStreamer: send first(full) frame failed, stop pushing.");
                break;
            }
            framesSinceKeyframe = 0;
            needKeyframe = false;
            framesSent += 2;
            ++keyframesSent;
            bytesSent += bmiBuf.size() + buf.size();
            LOG_INFO("RdControlledStreamer: sent keyframe (bitmapinfo + first), screen=%dx%d.",
                     capturer.screenWidth(), capturer.screenHeight());
        }
        else
        {
            capturer.captureNext(buf);
            if (!m_transport.SendEngineFrame(buf.data(), buf.size()))
            {
                LOG_INFO("RdControlledStreamer: send next frame failed, stop pushing.");
                break;
            }
            ++framesSinceKeyframe;
            ++framesSent;
            bytesSent += buf.size();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(kCaptureIntervalMs));
    }

    m_running.store(false);
    LOG_INFO("RdControlledStreamer: push loop exited.");
}

} // namespace remotedesktop
