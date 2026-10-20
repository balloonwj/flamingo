/**
 *  远程桌面控制端的 H.264 画面，见 RdVideoPicture.h。
 *  balloonwj@qq.com   2026-10-15
 */
#include "RdVideoPicture.h"

#include <objbase.h>
#include <mfapi.h>
#include <string.h>
#include "RdVideoFrames.h"
#include "../Call/VideoFrameConvert.h"
#include "../../Source/Common/Log/IULog.h"

#pragma comment(lib, "mfplat.lib")

namespace remotedesktop {

namespace {

//每像素字节数（BGRA）
const int kBytesPerPixel = 4;
//解码线程等新帧的最长时间（毫秒）：醒来检查是否要求退出
const int kDecodeWaitMs = 200;
//没有指针类型时的索引（与 CCursorInfo 取不到类型时一致，画布画普通箭头）
const BYTE kUnknownCursorType = 255;
//试建解码器的基准尺寸（像素）：1080p 建得起来才算能解 H.264
const int kProbeBaseWidth = 1920;
const int kProbeBaseHeight = 1080;
//试建大尺寸时按 16:9 由长边求高
const int kProbeAspectW = 16;
const int kProbeAspectH = 9;

} // namespace

RdVideoPicture::RdVideoPicture()
    : m_queue(kRdVideoQueueMaxFrames)
    , m_stop(false)
    , m_notifyWnd(NULL)
    , m_msgFrame(0)
    , m_msgKeyRequest(0)
    , m_hasSendMs(false)
    , m_lastSendMs(0)
    , m_lastRecvMs(0)
    , m_decoded(0)
    , m_curSrcW(0)
    , m_curSrcH(0)
    , m_curEncW(0)
    , m_curEncH(0)
    , m_srcW(0)
    , m_srcH(0)
    , m_encW(0)
    , m_encH(0)
    , m_cursorType(kUnknownCursorType)
{
    m_cursor.x = 0;
    m_cursor.y = 0;
}

RdVideoPicture::~RdVideoPicture()
{
    Stop();
}

void RdVideoPicture::Start(HWND notifyWnd, UINT msgFrame, UINT msgKeyRequest)
{
    Stop();
    m_notifyWnd = notifyWnd;
    m_msgFrame = msgFrame;
    m_msgKeyRequest = msgKeyRequest;
    {
        std::lock_guard<std::mutex> guard(m_ackMutex);
        m_hasSendMs = false;
    }
    m_decoded.store(0);
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        m_latest.clear();
        m_srcW = 0;
        m_srcH = 0;
        m_encW = 0;
        m_encH = 0;
        m_cursor.x = 0;
        m_cursor.y = 0;
        m_cursorType = kUnknownCursorType;
    }
    m_queue.Clear();
    m_stop.store(false);
    m_thread.reset(new std::thread(&RdVideoPicture::DecodeProc, this));
    LOG_INFO("RdVideoPicture: decode thread started.");
}

void RdVideoPicture::Stop()
{
    if (!m_thread)
        return;
    m_stop.store(true);
    {
        std::lock_guard<std::mutex> guard(m_wakeMutex);
        m_wake.notify_all();
    }
    if (m_thread->joinable())
        m_thread->join();
    m_thread.reset();
    m_queue.Clear();
    LOG_INFO("RdVideoPicture: decode thread stopped, decoded=%llu.", (unsigned long long)m_decoded.load());
}

bool RdVideoPicture::PushFrame(const unsigned char* data, size_t len)
{
    RdVideoFrameHeader h;
    const unsigned char* h264 = NULL;
    size_t h264Len = 0;
    //缺头或损坏的帧（控制端在多分片引擎帧中途连上时可能收到）：丢弃
    if (!RdVideoFrames::ParseVideo(data, len, h, h264, h264Len))
        return false;
    {
        std::lock_guard<std::mutex> guard(m_ackMutex);
        m_lastSendMs = h.m_sendMs;
        m_lastRecvMs = static_cast<int64_t>(::GetTickCount64());
        m_hasSendMs = true;
    }
    if (!m_thread)
        return true;
    const std::vector<unsigned char> frame(data, data + len);
    //解码跟不上、队列溢出：丢到下一个关键帧，请窗口请求一次关键帧（窗口那边限频）
    if (m_queue.Push(frame, h.m_keyframe))
    {
        LOG_WARNING("RdVideoPicture: decode queue overflow, waiting for a keyframe.");
        notify(m_msgKeyRequest);
    }
    std::lock_guard<std::mutex> guard(m_wakeMutex);
    m_wake.notify_one();
    return true;
}

void RdVideoPicture::DecodeProc()
{
    //系统 H.264 解码器要求本线程已初始化 COM（多线程套间）与 Media Foundation
    const HRESULT comHr = ::CoInitializeEx(NULL, COINIT_MULTITHREADED);
    ::MFStartup(MF_VERSION);
    std::vector<unsigned char> frame;
    while (!m_stop.load())
    {
        if (!m_queue.Pop(frame))
        {
            std::unique_lock<std::mutex> lock(m_wakeMutex);
            m_wake.wait_for(lock, std::chrono::milliseconds(kDecodeWaitMs));
            continue;
        }
        decodeOne(frame);
    }
    m_decoder.Close();
    ::MFShutdown();
    if (SUCCEEDED(comHr))
        ::CoUninitialize();
}

void RdVideoPicture::decodeOne(const std::vector<unsigned char>& frame)
{
    RdVideoFrameHeader h;
    const unsigned char* h264 = NULL;
    size_t h264Len = 0;
    if (!RdVideoFrames::ParseVideo(frame.data(), frame.size(), h, h264, h264Len))
        return;
    m_curSrcW = h.m_srcW;
    m_curSrcH = h.m_srcH;
    m_curEncW = h.m_encW;
    m_curEncH = h.m_encH;
    //解码器按第一帧的编码尺寸建；之后码流尺寸变了由解码器自己重新协商
    if (!m_decoder.IsReady() && !m_decoder.Init(h.m_encW, h.m_encH))
    {
        LOG_ERROR("RdVideoPicture: decoder init %dx%d failed.", h.m_encW, h.m_encH);
        notify(m_msgKeyRequest);
        return;
    }
    m_decoder.DecodePacket(h264, h264Len, 0, this);
}

void RdVideoPicture::OnDecodedNv12(const uint8_t* nv12, int width, int height, int stride, int64_t ptsHns)
{
    (void)ptsHns;
    //按帧头里的编码尺寸裁掉解码补齐的部分（如 1080 解成 1088）
    const int w = (m_curEncW > 0 && m_curEncW <= width) ? m_curEncW : width;
    const int h = (m_curEncH > 0 && m_curEncH <= height) ? m_curEncH : height;
    m_staging.resize(static_cast<size_t>(w) * h * kBytesPerPixel);
    call::Nv12ToBgraCropped(nv12, height, stride, w, h, &m_staging[0], w * kBytesPerPixel);
    bool first = false;
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        first = m_latest.empty();
        m_latest.swap(m_staging);
        m_srcW = (m_curSrcW > 0) ? m_curSrcW : w;
        m_srcH = (m_curSrcH > 0) ? m_curSrcH : h;
        m_encW = w;
        m_encH = h;
    }
    m_decoded.fetch_add(1);
    if (first)
        LOG_INFO("RdVideoPicture: first frame decoded, source %dx%d, encode %dx%d.", m_curSrcW, m_curSrcH, w, h);
    notify(m_msgFrame);
}

void RdVideoPicture::notify(UINT msg) const
{
    if (m_notifyWnd != NULL && msg != 0 && ::IsWindow(m_notifyWnd))
        ::PostMessage(m_notifyWnd, msg, 0, 0);
}

void RdVideoPicture::SetCursor(int32_t x, int32_t y, BYTE type)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    m_cursor.x = x;
    m_cursor.y = y;
    m_cursorType = type;
}

bool RdVideoPicture::AckEcho(int64_t nowMs, uint32_t& echo) const
{
    std::lock_guard<std::mutex> guard(m_ackMutex);
    if (!m_hasSendMs)
        return false;
    echo = RdAckEchoMs(m_lastSendMs, m_lastRecvMs, nowMs);
    return true;
}

bool RdVideoPicture::WaitingForKeyframe() const
{
    return m_queue.WaitingForKeyframe();
}

bool RdVideoPicture::ProbeDecoder(int wantLongSide, int& maxLongSide)
{
    maxLongSide = 0;
    const HRESULT comHr = ::CoInitializeEx(NULL, COINIT_MULTITHREADED);
    ::MFStartup(MF_VERSION);
    bool canDecode = false;
    {
        call::MfH264Decoder probe;
        canDecode = probe.Init(kProbeBaseWidth, kProbeBaseHeight);
        probe.Close();
        if (canDecode)
        {
            //想要的长边按 16:9 取高；被拒（如 Win7 的系统解码器）就报 1920
            maxLongSide = kProbeBaseWidth;
            if (wantLongSide > kProbeBaseWidth)
            {
                call::MfH264Decoder big;
                if (big.Init(wantLongSide, wantLongSide * kProbeAspectH / kProbeAspectW))
                    maxLongSide = wantLongSide;
                big.Close();
            }
        }
    }
    ::MFShutdown();
    if (SUCCEEDED(comHr))
        ::CoUninitialize();
    LOG_INFO("RdVideoPicture: probe decoder, h264=%d, max long side=%d.", canDecode ? 1 : 0, maxLongSide);
    return canDecode;
}

uint64_t RdVideoPicture::DecodedCount() const
{
    return m_decoded.load();
}

bool RdVideoPicture::ready() const
{
    std::lock_guard<std::mutex> guard(m_mutex);
    return !m_latest.empty();
}

int RdVideoPicture::width() const
{
    std::lock_guard<std::mutex> guard(m_mutex);
    return m_srcW;
}

int RdVideoPicture::height() const
{
    std::lock_guard<std::mutex> guard(m_mutex);
    return m_srcH;
}

void RdVideoPicture::drawTo(HDC hdc, const RECT& dest) const
{
    std::lock_guard<std::mutex> guard(m_mutex);
    if (m_latest.empty() || m_encW <= 0 || m_encH <= 0)
        return;
    //缩小显示（如 2560 宽的画面放进窗口）时用 SSE2 缩放后原样贴上，30 fps 下不再让 HALFTONE 占满 UI 线程；
    //放大或缩得比一半还小时绘制器仍用半色调模式
    m_drawer.Draw(hdc, dest, &m_latest[0], m_encW, m_encH, m_encW * kBytesPerPixel);
}

POINT RdVideoPicture::cursorPos() const
{
    std::lock_guard<std::mutex> guard(m_mutex);
    return m_cursor;
}

BYTE RdVideoPicture::cursorType() const
{
    std::lock_guard<std::mutex> guard(m_mutex);
    return m_cursorType;
}

void RdVideoPicture::SetFrameForTest(int srcW, int srcH, int encW, int encH, const std::vector<uint8_t>& bgra)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    m_latest = bgra;
    m_srcW = srcW;
    m_srcH = srcH;
    m_encW = encW;
    m_encH = encH;
}

bool RdVideoPicture::GetFrameForTest(std::vector<uint8_t>& bgra, int& encW, int& encH) const
{
    std::lock_guard<std::mutex> guard(m_mutex);
    if (m_latest.empty())
        return false;
    bgra = m_latest;
    encW = m_encW;
    encH = m_encH;
    return true;
}

} // namespace remotedesktop
