/**
 *  远程桌面新旧画面格式的协商规则，见 RdVideoNegotiation.h。
 *  balloonwj@qq.com   2026-10-15
 */
#include "RdVideoNegotiation.h"

#include <string.h>   // memcmp / memcpy：RdStoreFrameIfChanged
#include <chrono>     // steady_clock：RdStreamClockMs

namespace remotedesktop {

//BGRA32 每像素的字节数
static const size_t kRdBgraBytesPerPixel = 4;

RdFormatChooser::RdFormatChooser(bool canEncodeH264)
    : m_canEncode(canEncodeH264)
    , m_format(rd_format_undecided)
    , m_startMs(0)
    , m_controllerMax(kRdDefaultControllerMaxLongSide)
{
}

void RdFormatChooser::Start(int64_t nowMs)
{
    m_format = rd_format_undecided;
    m_startMs = nowMs;
    m_controllerMax = kRdDefaultControllerMaxLongSide;
}

bool RdFormatChooser::OnKeyframeRequest(const RdKeyframeRequestInfo& info, int64_t nowMs)
{
    (void)nowMs;
    const int before = m_format;
    if (info.m_hasCaps)
        m_controllerMax = info.m_maxLongSide;
    if (info.m_canDecodeH264 && m_canEncode)
    {
        //控制端能解、本机能编：H.264（未定或旧格式期间都切过去）
        m_format = rd_format_h264;
    }
    else if (m_format == rd_format_undecided)
    {
        //旧控制端的 1 字节请求、控制端不能解、或本机不能编：旧格式
        m_format = rd_format_legacy;
    }
    return m_format != before;
}

int RdFormatChooser::Current(int64_t nowMs)
{
    //等满时间还没有请求：先按旧格式发，之后收到带能力位的请求再切换
    if (m_format == rd_format_undecided && nowMs - m_startMs >= kRdFormatWaitMs)
        m_format = rd_format_legacy;
    //本机不能编时不会停在「未定」等请求：直接旧格式
    if (m_format == rd_format_undecided && !m_canEncode)
        m_format = rd_format_legacy;
    return m_format;
}

void RdFormatChooser::OnEncoderFailed()
{
    m_canEncode = false;
    m_format = rd_format_legacy;
}

int RdFormatChooser::ControllerMaxLongSide() const
{
    return m_controllerMax;
}

RdCapsResendPacer::RdCapsResendPacer(bool canDecodeH264)
    : m_canDecode(canDecodeH264)
    , m_sent(false)
    , m_lastSentMs(0)
{
}

bool RdCapsResendPacer::OnCapsFrame(uint8_t caps, bool receivingH264, int64_t nowMs)
{
    if (!m_canDecode || receivingH264 || (caps & kRdCapH264) == 0)
        return false;
    if (m_sent && nowMs - m_lastSentMs < kRdCapsResendIntervalMs)
        return false;
    m_sent = true;
    m_lastSentMs = nowMs;
    return true;
}

void RdCapsResendPacer::OnRequestSent(int64_t nowMs)
{
    m_sent = true;
    m_lastSentMs = nowMs;
}

RdKeyRequestLimiter::RdKeyRequestLimiter()
    : m_sent(false)
    , m_lastSentMs(0)
{
}

bool RdKeyRequestLimiter::Allow(int64_t nowMs)
{
    if (m_sent && nowMs - m_lastSentMs < kRdKeyRequestMinIntervalMs)
        return false;
    m_sent = true;
    m_lastSentMs = nowMs;
    return true;
}

int64_t RdVideoRoundWaitMs(int64_t elapsedMs, bool hasFrame, bool newFrame, bool cursorMoved)
{
    //抓到了新画面：不额外等待，DXGI 在画面变化时每来一帧就返回，帧率由发帧节奏控制
    if (hasFrame && newFrame)
        return 0;
    //画面没变而指针在动：缩短到 kRdVideoCursorRoundMs，下一轮更早读取指针位置；DXGI 在指针移动时也会立即返回，
    //所以指针连续移动时每轮约 16 毫秒。没有画面时抓取不可用，指针动不动都按 kRdVideoNoFrameRoundMs
    int64_t roundMs = kRdVideoNoFrameRoundMs;
    if (hasFrame)
        roundMs = cursorMoved ? kRdVideoCursorRoundMs : kRdVideoIdleRoundMs;
    const int64_t used = (elapsedMs > 0) ? elapsedMs : 0;
    return (used < roundMs) ? (roundMs - used) : 0;
}

bool RdVideoPacerHasNewFrame(bool hasFrame, bool captureNew, bool needKeyframe)
{
    if (!hasFrame)
        return false;
    //需要关键帧时手里的画面就可以编：发帧节奏从没编过码时不理会关键帧请求，只看有没有新画面
    return captureNew || needKeyframe;
}

bool RdStoreFrameIfChanged(const FrameView& src, std::vector<BYTE>& dst, int& dstW, int& dstH)
{
    const size_t rowBytes = static_cast<size_t>(src.m_width) * kRdBgraBytesPerPixel;
    const size_t frameBytes = rowBytes * src.m_height;
    //尺寸变了或原来没有画面：整帧都算变了
    bool changed = (dstW != src.m_width || dstH != src.m_height || dst.size() != frameBytes);
    if (changed)
    {
        dst.resize(frameBytes);
        dstW = src.m_width;
        dstH = src.m_height;
    }
    for (int y = 0; y < src.m_height; ++y)
    {
        const BYTE* srcRow = src.m_data + static_cast<size_t>(y) * src.m_stride;
        BYTE* dstRow = &dst[rowBytes * y];
        //还没发现不同：先比较，这一行相同就不必复制
        if (!changed)
        {
            if (memcmp(dstRow, srcRow, rowBytes) == 0)
                continue;
            changed = true;
        }
        memcpy(dstRow, srcRow, rowBytes);
    }
    return changed;
}

int64_t RdStreamClockMs()
{
    //steady_clock 在 Windows 上基于 QueryPerformanceCounter，精度远高于 1 毫秒；原先用的 GetTickCount64 精度约 15.6 毫秒，
    //发帧节奏 33 毫秒的最短间隔会被读成 31 毫秒而推迟到下一帧，画面每秒变化 60 次时只编出约 20 帧（RDV15）
    return static_cast<int64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

} // namespace remotedesktop
