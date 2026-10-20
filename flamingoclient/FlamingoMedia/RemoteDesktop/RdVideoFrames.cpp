/**
 *  远程桌面 H.264 引擎帧的编解码，见 RdVideoFrames.h。
 *  balloonwj@qq.com   2026-10-15
 */
#include "RdVideoFrames.h"

#include "RdEngineDef.h"

namespace remotedesktop {

namespace {

//一个字节的位数（拼小端整数用）与字节掩码
const int kBitsPerByte = 8;
const uint32_t kByteMask = 0xFFu;
//视频帧头里各字段的偏移：类型之后依次是源宽、源高、编码宽、编码高（各 2 字节）、标志 1 字节、发送时刻 4 字节
const size_t kOffSrcW = 1;
const size_t kOffSrcH = 3;
const size_t kOffEncW = 5;
const size_t kOffEncH = 7;
const size_t kOffFlags = 9;
const size_t kOffSendMs = 10;
//指针帧：类型之后是横坐标、纵坐标（各 4 字节）与类型 1 字节
const size_t kOffCursorX = 1;
const size_t kOffCursorY = 5;
const size_t kOffCursorType = 9;
//关键帧请求：类型之后是能力位 1 字节与最大长边 2 字节
const size_t kOffReqCaps = 1;
const size_t kOffReqLongSide = 2;
//回执：类型之后是发送时刻 4 字节
const size_t kOffAckMs = 1;
//两字节与四字节整数的长度
const size_t kU16Bytes = 2;
const size_t kU32Bytes = 4;
//两字节无符号整数的上限
const int kU16Max = 0xFFFF;

void putLe(std::vector<unsigned char>& out, uint32_t v, size_t bytes)
{
    for (size_t i = 0; i < bytes; ++i)
    {
        out.push_back(static_cast<unsigned char>(v & kByteMask));
        v >>= kBitsPerByte;
    }
}

uint32_t getLe(const unsigned char* p, size_t bytes)
{
    uint32_t v = 0;
    for (size_t i = 0; i < bytes; ++i)
        v |= static_cast<uint32_t>(p[i]) << (kBitsPerByte * i);
    return v;
}

//把尺寸夹到两字节能表示的范围（只在组帧时防御，正常尺寸远小于上限）
uint32_t clampU16(int v)
{
    if (v < 0)
        return 0;
    return static_cast<uint32_t>(v > kU16Max ? kU16Max : v);
}

bool validDimension(int v)
{
    return v > 0 && v <= kRdVideoMaxDimension;
}

} // namespace

RdVideoFrameHeader::RdVideoFrameHeader()
    : m_srcW(0)
    , m_srcH(0)
    , m_encW(0)
    , m_encH(0)
    , m_keyframe(false)
    , m_sendMs(0)
{
}

RdKeyframeRequestInfo::RdKeyframeRequestInfo()
    : m_hasCaps(false)
    , m_canDecodeH264(false)
    , m_maxLongSide(kRdDefaultControllerMaxLongSide)
{
}

void RdVideoFrames::BuildVideo(const RdVideoFrameHeader& h, const uint8_t* h264, size_t len,
                               std::vector<unsigned char>& out)
{
    out.clear();
    out.reserve(kRdVideoHeaderLen + len);
    out.push_back(static_cast<unsigned char>(rd_frame_video));
    putLe(out, clampU16(h.m_srcW), kU16Bytes);
    putLe(out, clampU16(h.m_srcH), kU16Bytes);
    putLe(out, clampU16(h.m_encW), kU16Bytes);
    putLe(out, clampU16(h.m_encH), kU16Bytes);
    out.push_back(h.m_keyframe ? kRdVideoFlagKeyframe : 0);
    putLe(out, h.m_sendMs, kU32Bytes);
    if (h264 != NULL && len > 0)
        out.insert(out.end(), h264, h264 + len);
}

bool RdVideoFrames::ParseVideo(const unsigned char* data, size_t len, RdVideoFrameHeader& h, const unsigned char*& h264,
                               size_t& h264Len)
{
    h264 = NULL;
    h264Len = 0;
    //负载不能为空：只有帧头的视频帧没有意义
    if (data == NULL || len <= kRdVideoHeaderLen || data[0] != static_cast<unsigned char>(rd_frame_video))
        return false;
    RdVideoFrameHeader parsed;
    parsed.m_srcW = static_cast<int>(getLe(data + kOffSrcW, kU16Bytes));
    parsed.m_srcH = static_cast<int>(getLe(data + kOffSrcH, kU16Bytes));
    parsed.m_encW = static_cast<int>(getLe(data + kOffEncW, kU16Bytes));
    parsed.m_encH = static_cast<int>(getLe(data + kOffEncH, kU16Bytes));
    parsed.m_keyframe = (data[kOffFlags] & kRdVideoFlagKeyframe) != 0;
    parsed.m_sendMs = getLe(data + kOffSendMs, kU32Bytes);
    if (!validDimension(parsed.m_srcW) || !validDimension(parsed.m_srcH) || !validDimension(parsed.m_encW) ||
        !validDimension(parsed.m_encH))
        return false;
    h = parsed;
    h264 = data + kRdVideoHeaderLen;
    h264Len = len - kRdVideoHeaderLen;
    return true;
}

void RdVideoFrames::BuildCursor(int32_t x, int32_t y, uint8_t type, std::vector<unsigned char>& out)
{
    out.clear();
    out.push_back(static_cast<unsigned char>(rd_frame_cursor));
    putLe(out, static_cast<uint32_t>(x), kU32Bytes);
    putLe(out, static_cast<uint32_t>(y), kU32Bytes);
    out.push_back(type);
}

bool RdVideoFrames::ParseCursor(const unsigned char* data, size_t len, int32_t& x, int32_t& y, uint8_t& type)
{
    if (data == NULL || len < kRdCursorFrameLen || data[0] != static_cast<unsigned char>(rd_frame_cursor))
        return false;
    x = static_cast<int32_t>(getLe(data + kOffCursorX, kU32Bytes));
    y = static_cast<int32_t>(getLe(data + kOffCursorY, kU32Bytes));
    type = data[kOffCursorType];
    return true;
}

void RdVideoFrames::BuildCaps(uint8_t caps, std::vector<unsigned char>& out)
{
    out.clear();
    out.push_back(static_cast<unsigned char>(rd_frame_caps));
    out.push_back(caps);
}

bool RdVideoFrames::ParseCaps(const unsigned char* data, size_t len, uint8_t& caps)
{
    if (data == NULL || len < kRdCapsFrameLen || data[0] != static_cast<unsigned char>(rd_frame_caps))
        return false;
    caps = data[1];
    return true;
}

void RdVideoFrames::BuildKeyframeRequest(bool canDecodeH264, int maxLongSide, std::vector<unsigned char>& out)
{
    out.clear();
    out.push_back(static_cast<unsigned char>(rd_frame_request_keyframe));
    out.push_back(canDecodeH264 ? kRdCapH264 : 0);
    putLe(out, clampU16(maxLongSide), kU16Bytes);
}

void RdVideoFrames::ParseKeyframeRequest(const unsigned char* data, size_t len, RdKeyframeRequestInfo& info)
{
    info = RdKeyframeRequestInfo();
    //旧控制端只发 1 字节：不带能力位，视为不能解 H.264
    if (data == NULL || len <= kOffReqCaps)
        return;
    info.m_hasCaps = true;
    info.m_canDecodeH264 = (data[kOffReqCaps] & kRdCapH264) != 0;
    if (len >= kRdKeyRequestFullLen)
    {
        const int side = static_cast<int>(getLe(data + kOffReqLongSide, kU16Bytes));
        if (side > 0)
            info.m_maxLongSide = side;
    }
}

void RdVideoFrames::BuildVideoAck(uint32_t sendMs, std::vector<unsigned char>& out)
{
    out.clear();
    out.push_back(static_cast<unsigned char>(rd_ctl_video_ack));
    putLe(out, sendMs, kU32Bytes);
}

bool RdVideoFrames::ParseVideoAck(const unsigned char* data, size_t len, uint32_t& sendMs)
{
    if (data == NULL || len < kRdVideoAckLen || data[0] != static_cast<unsigned char>(rd_ctl_video_ack))
        return false;
    sendMs = getLe(data + kOffAckMs, kU32Bytes);
    return true;
}

bool RdAckDelayMs(uint32_t nowMs, uint32_t echoedMs, uint32_t firstSentMs, int64_t& delayMs)
{
    //32 位无符号差值再按有符号解读：回绕不影响，正负表示先后
    const int32_t sinceFirst = static_cast<int32_t>(echoedMs - firstSentMs);
    const int32_t sinceEcho = static_cast<int32_t>(nowMs - echoedMs);
    if (sinceFirst < 0 || sinceEcho < 0)
        return false;
    delayMs = sinceEcho;
    return true;
}

uint32_t RdAckEchoMs(uint32_t sendMs, int64_t receivedAtMs, int64_t nowMs)
{
    const int64_t held = (nowMs > receivedAtMs) ? nowMs - receivedAtMs : 0;
    return sendMs + static_cast<uint32_t>(held);
}

int RdChooseEncodeLongSide(int paramsMaxLongSide, int controllerMaxLongSide)
{
    const int controller = (controllerMaxLongSide > 0) ? controllerMaxLongSide : kRdDefaultControllerMaxLongSide;
    return (paramsMaxLongSide < controller) ? paramsMaxLongSide : controller;
}

} // namespace remotedesktop
