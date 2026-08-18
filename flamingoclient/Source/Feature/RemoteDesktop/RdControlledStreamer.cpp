/**
 *  远程桌面被控端推流器实现，RdControlledStreamer.cpp
 *  balloonwj@qq.com   2026-06-09
 */
#include "RdControlledStreamer.h"

#include "RdScreenCapturer.h"     // RdScreenCapturer（含 windows.h）
#include "RdEngineDef.h"          // rd_frame_request_keyframe（控制端请求关键帧标志）
#include "RdInputInjector.h"      // RdInputInjector::inject（含 RdInputDef.h：rd_input_mouse/key）
#include "../../Common/Log/IULog.h"             // LOG_INFO / LOG_ERROR

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

//DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 的哨兵值（避免依赖较新 SDK 头），
//与 BalloonScreenshot/ScreenCaptureOverlay.cpp 取值一致。
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
    : m_running(false)
    , m_stopping(false)
    , m_keyframeRequested(false)
{
}

RdControlledStreamer::~RdControlledStreamer()
{
    Stop();
}

bool RdControlledStreamer::Start(const std::string& ip, int port, const std::string& token)
{
    if (m_running.load())
        return true;

    m_stopping.store(false);

    if (!m_relay.Connect(ip, port, token, this))
    {
        LOG_ERROR("RdControlledStreamer: connect relay %s:%d failed.", ip.c_str(), port);
        return false;
    }

    m_running.store(true);
    m_pushThread.reset(new std::thread(&RdControlledStreamer::PushProc, this));
    LOG_INFO("RdControlledStreamer: started, relay %s:%d.", ip.c_str(), port);
    return true;
}

void RdControlledStreamer::Stop()
{
    m_stopping.store(true);
    m_running.store(false);

    //关连接：促使推流线程的 SendEngineFrame 失败、循环退出
    m_relay.Close();

    if (m_pushThread && m_pushThread->joinable())
    {
        if (std::this_thread::get_id() != m_pushThread->get_id())
            m_pushThread->join();
        else
            m_pushThread->detach();
    }
    m_pushThread.reset();
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
    int  framesSinceKeyframe = 0;
    bool needKeyframe = true;   //首轮必发关键帧

    while (!m_stopping.load())
    {
        //控制端请求了关键帧（连上中转 / 缺关键帧时发来）：本轮立即补发一帧关键帧
        if (m_keyframeRequested.exchange(false))
            needKeyframe = true;

        if (needKeyframe || framesSinceKeyframe >= kFramesPerKeyframe)
        {
            //先把 BITMAPINFO 与首屏都抓好，再连续发出：captureFirst 抓全屏较耗时，若先发
            //BITMAPINFO 再抓首屏，BITMAPINFO 会比首屏早几十毫秒发出、可能赶在控制端连上中转
            //之前被丢弃（控制端就因此只收到首屏、缺 BITMAPINFO 而无法就绪）。抓完再发让两者
            //发送时刻紧挨，控制端只要在抓首屏期间连上即可同时收到。
            std::vector<unsigned char> bmiBuf;
            capturer.captureBitmapInfo(bmiBuf);
            capturer.captureFirst(buf);
            if (!m_relay.SendEngineFrame(bmiBuf.data(), bmiBuf.size()))
            {
                LOG_INFO("RdControlledStreamer: send bitmapinfo failed, stop pushing.");
                break;
            }
            if (!m_relay.SendEngineFrame(buf.data(), buf.size()))
            {
                LOG_INFO("RdControlledStreamer: send first(full) frame failed, stop pushing.");
                break;
            }
            framesSinceKeyframe = 0;
            needKeyframe = false;
            LOG_INFO("RdControlledStreamer: sent keyframe (bitmapinfo + first), screen=%dx%d.",
                     capturer.screenWidth(), capturer.screenHeight());
        }
        else
        {
            capturer.captureNext(buf);
            if (!m_relay.SendEngineFrame(buf.data(), buf.size()))
            {
                LOG_INFO("RdControlledStreamer: send next frame failed, stop pushing.");
                break;
            }
            ++framesSinceKeyframe;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(kCaptureIntervalMs));
    }

    m_running.store(false);
    LOG_INFO("RdControlledStreamer: push loop exited.");
}

} // namespace remotedesktop
