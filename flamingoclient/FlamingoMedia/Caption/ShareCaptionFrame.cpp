/**
 *  屏幕共享的字幕控制帧，见 ShareCaptionFrame.h。
 *  balloonwj@qq.com   2026-10-06
 */
#include "ShareCaptionFrame.h"

#include "../../Source/net/Protocol.h"   // screenshare_track_control、screenshare_control_caption

namespace screenshare {

namespace {

//载荷各字段的位置与长度：[类型 1][标志 1][句子编号 4][时长 2][文字]
const size_t kTypeOffset = 0;
const size_t kFlagOffset = 1;
const size_t kIdOffset = 2;
const size_t kDurationOffset = 6;
const size_t kTextOffset = 8;
//标志的取值
const unsigned char kFlagPartial = 0;
const unsigned char kFlagFinal = 1;
//UTF-8 续字节的高两位为 10
const unsigned char kUtf8ContinuationMask = 0xC0;
const unsigned char kUtf8ContinuationBits = 0x80;
//按字节拼大端整数时的移位；句子编号的字节数
const int kBitsPerByte = 8;
const int kIdBytes = 4;

} // namespace

std::string TruncateUtf8(const std::string& text, size_t maxBytes)
{
    if (text.size() <= maxBytes)
        return text;
    size_t end = maxBytes;
    //退到一个字符的起始字节（不是续字节）为止
    while (end > 0 && (static_cast<unsigned char>(text[end]) & kUtf8ContinuationMask) == kUtf8ContinuationBits)
        --end;
    return text.substr(0, end);
}

std::string BuildShareCaptionBody(int senderSeat, const ShareCaptionText& caption)
{
    const std::string text = TruncateUtf8(caption.m_utf8, kShareCaptionMaxTextBytes);
    const uint32_t duration = caption.m_durationMs > kShareCaptionMaxDurationMs ? kShareCaptionMaxDurationMs
                                                                                : caption.m_durationMs;
    std::string payload(kTextOffset, '\0');
    payload[kTypeOffset] = static_cast<char>(screenshare_control_caption);
    payload[kFlagOffset] = static_cast<char>(caption.m_final ? kFlagFinal : kFlagPartial);
    for (int i = 0; i < kIdBytes; ++i)
        payload[kIdOffset + i] =
            static_cast<char>((caption.m_sentenceId >> ((kIdBytes - 1 - i) * kBitsPerByte)) & 0xFF);
    payload[kDurationOffset] = static_cast<char>((duration >> kBitsPerByte) & 0xFF);
    payload[kDurationOffset + 1] = static_cast<char>(duration & 0xFF);
    payload += text;
    ShareFrameHeader header;
    header.m_track = screenshare_track_control;
    header.m_senderSeat = senderSeat;
    header.m_flags = screenshare_flag_first_frag | screenshare_flag_last_frag;
    header.m_timestamp = 0;
    return ShareFraming::encodeBody(header, payload.data(), payload.size());
}

bool ParseShareCaptionFrame(const ShareMediaFrame& frame, ShareCaptionText& out)
{
    if (frame.m_track != screenshare_track_control || frame.m_data.size() < kTextOffset)
        return false;
    const std::string& d = frame.m_data;
    if (static_cast<unsigned char>(d[kTypeOffset]) != screenshare_control_caption)
        return false;
    const unsigned char flag = static_cast<unsigned char>(d[kFlagOffset]);
    if (flag != kFlagPartial && flag != kFlagFinal)
        return false;
    uint32_t id = 0;
    for (int i = 0; i < kIdBytes; ++i)
        id = (id << kBitsPerByte) | static_cast<unsigned char>(d[kIdOffset + i]);
    const uint32_t duration = (static_cast<uint32_t>(static_cast<unsigned char>(d[kDurationOffset])) << kBitsPerByte) |
                              static_cast<unsigned char>(d[kDurationOffset + 1]);
    if (d.size() - kTextOffset > kShareCaptionMaxTextBytes)
        return false;
    out.m_final = (flag == kFlagFinal);
    out.m_sentenceId = id;
    out.m_durationMs = duration;
    out.m_utf8 = d.substr(kTextOffset);
    return true;
}

} // namespace screenshare
