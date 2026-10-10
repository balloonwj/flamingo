/**
 *  屏幕共享房间中转帧的封装、分片与重组，ShareFraming.cpp
 *  格式与约定见 ShareFraming.h。
 *
 *  balloonwj@qq.com   2026-10-02
 */
#include "ShareFraming.h"

#include "../../Source/net/Protocol.h"

namespace screenshare {

namespace {

//帧头各字段在包体里的偏移，单位字节
const size_t kTrackOffset = 0;
const size_t kSeatOffset = 1;
const size_t kFlagsOffset = 2;
const size_t kTimestampOffset = 3;
//时间戳的字节数与每字节的位数
const size_t kTimestampBytes = 8;
const int kBitsPerByte = 8;
//一个字节能表示的最大值，用于把整数截成 1 字节
const int kByteMask = 0xFF;

void writeBe32(std::string& out, uint32_t v)
{
    for (int i = (int)kShareFrameLengthPrefixSize - 1; i >= 0; --i)
        out.push_back(static_cast<char>((v >> (i * kBitsPerByte)) & kByteMask));
}

uint32_t readBe32(const char* p)
{
    uint32_t v = 0;
    for (size_t i = 0; i < kShareFrameLengthPrefixSize; ++i)
        v = (v << kBitsPerByte) | static_cast<unsigned char>(p[i]);
    return v;
}

} // namespace

ShareFrameHeader::ShareFrameHeader()
    : m_track(0)
    , m_senderSeat(0)
    , m_flags(0)
    , m_timestamp(0)
{
}

ShareMediaFrame::ShareMediaFrame()
    : m_track(0)
    , m_senderSeat(0)
    , m_keyframe(false)
    , m_timestamp(0)
{
}

std::string ShareFraming::encodeBody(const ShareFrameHeader& header, const char* payload, size_t len)
{
    std::string body;
    body.reserve(kShareFrameHeaderSize + len);
    body.push_back(static_cast<char>(header.m_track & kByteMask));
    body.push_back(static_cast<char>(header.m_senderSeat & kByteMask));
    body.push_back(static_cast<char>(header.m_flags & kByteMask));
    //时间戳按大端写入：最高字节在前
    for (int i = (int)kTimestampBytes - 1; i >= 0; --i)
        body.push_back(static_cast<char>((header.m_timestamp >> (i * kBitsPerByte)) & kByteMask));
    if (payload != NULL && len > 0)
        body.append(payload, len);
    return body;
}

bool ShareFraming::decodeBody(const char* body, size_t len, ShareFrameHeader& header, const char*& payload,
                              size_t& payloadLen)
{
    payload = NULL;
    payloadLen = 0;
    if (body == NULL || len < kShareFrameHeaderSize)
        return false;
    const unsigned char* p = reinterpret_cast<const unsigned char*>(body);
    const int track = p[kTrackOffset];
    if (track < screenshare_track_screen || track > screenshare_track_control)
        return false;

    header.m_track = track;
    header.m_senderSeat = p[kSeatOffset];
    header.m_flags = p[kFlagsOffset];
    uint64_t timestamp = 0;
    for (size_t i = 0; i < kTimestampBytes; ++i)
        timestamp = (timestamp << kBitsPerByte) | p[kTimestampOffset + i];
    header.m_timestamp = timestamp;
    payload = body + kShareFrameHeaderSize;
    payloadLen = len - kShareFrameHeaderSize;
    return true;
}

void ShareFraming::fragmentFrame(int track, int senderSeat, bool keyframe, uint64_t timestamp, const char* data,
                                 size_t len, size_t maxPayload, std::vector<std::string>& outBodies)
{
    outBodies.clear();
    if (data == NULL)
        len = 0;
    if (maxPayload == 0)
        maxPayload = kShareFragmentPayloadBytes;

    ShareFrameHeader header;
    header.m_track = track;
    header.m_senderSeat = senderSeat;
    header.m_timestamp = timestamp;
    const int keyFlag = keyframe ? screenshare_flag_keyframe : 0;

    //非视频轨：整帧一个包体，不带长度前缀
    if (!isVideoTrack(track))
    {
        header.m_flags = keyFlag | screenshare_flag_first_frag | screenshare_flag_last_frag;
        outBodies.push_back(encodeBody(header, data, len));
        return;
    }

    //视频轨：首分片载荷 = 4 字节整帧长度 + 数据；之后每片最多 maxPayload 字节数据
    size_t offset = 0;
    bool first = true;
    while (first || offset < len)
    {
        std::string payload;
        size_t room = maxPayload;
        if (first)
        {
            writeBe32(payload, static_cast<uint32_t>(len));
            room = (maxPayload > kShareFrameLengthPrefixSize) ? maxPayload - kShareFrameLengthPrefixSize : 1;
        }
        const size_t take = (len - offset < room) ? len - offset : room;
        payload.append(data + offset, take);
        offset += take;

        header.m_flags = keyFlag;
        if (first)
            header.m_flags |= screenshare_flag_first_frag;
        if (offset >= len)
            header.m_flags |= screenshare_flag_last_frag;
        outBodies.push_back(encodeBody(header, payload.data(), payload.size()));
        first = false;
    }
}

bool ShareFraming::parseKeyFrameRequest(const ShareMediaFrame& frame, int& videoTrack)
{
    //载荷两字节：[控制类型][视频轨]
    const size_t kKeyFrameRequestPayloadBytes = 2;
    if (frame.m_track != screenshare_track_control || frame.m_senderSeat != screenshare_seat_server ||
        frame.m_data.size() != kKeyFrameRequestPayloadBytes)
        return false;
    if (static_cast<unsigned char>(frame.m_data[0]) != screenshare_control_keyframe_request)
        return false;
    const int track = static_cast<unsigned char>(frame.m_data[1]);
    if (!isVideoTrack(track))
        return false;
    videoTrack = track;
    return true;
}

bool ShareFraming::isVideoTrack(int track)
{
    return track == screenshare_track_screen || track == screenshare_track_camera;
}

std::string ShareFraming::buildSourceStateBody(int senderSeat, int state)
{
    ShareFrameHeader header;
    header.m_track = screenshare_track_control;
    header.m_senderSeat = senderSeat;
    header.m_flags = screenshare_flag_first_frag | screenshare_flag_last_frag;
    header.m_timestamp = 0;
    const char payload[2] = { static_cast<char>(screenshare_control_source_state), static_cast<char>(state) };
    return encodeBody(header, payload, sizeof(payload));
}

bool ShareFraming::parseSourceState(const ShareMediaFrame& frame, int& state)
{
    //载荷两字节：[控制类型][状态]
    const size_t kSourceStatePayloadBytes = 2;
    if (frame.m_track != screenshare_track_control || frame.m_data.size() != kSourceStatePayloadBytes)
        return false;
    if (static_cast<unsigned char>(frame.m_data[0]) != screenshare_control_source_state)
        return false;
    const int value = static_cast<unsigned char>(frame.m_data[1]);
    if (value < screenshare_source_sharing || value > screenshare_source_closed)
        return false;
    state = value;
    return true;
}

std::string ShareFraming::buildVideoPayload(int width, int height, const char* h264, size_t len)
{
    //宽高各占 2 字节，取值 1~65535
    const int kMaxDimension = 0xFFFF;
    if (width <= 0 || height <= 0 || width > kMaxDimension || height > kMaxDimension)
        return std::string();
    std::string payload;
    payload.reserve(kShareVideoPayloadHeaderSize + len);
    payload.push_back(static_cast<char>((width >> kBitsPerByte) & kByteMask));
    payload.push_back(static_cast<char>(width & kByteMask));
    payload.push_back(static_cast<char>((height >> kBitsPerByte) & kByteMask));
    payload.push_back(static_cast<char>(height & kByteMask));
    if (h264 != NULL && len > 0)
        payload.append(h264, len);
    return payload;
}

bool ShareFraming::parseVideoPayload(const std::string& payload, int& width, int& height, const char*& h264,
                                     size_t& len)
{
    h264 = NULL;
    len = 0;
    if (payload.size() < kShareVideoPayloadHeaderSize)
        return false;
    const unsigned char* p = reinterpret_cast<const unsigned char*>(payload.data());
    const int w = (p[0] << kBitsPerByte) | p[1];
    const int h = (p[2] << kBitsPerByte) | p[3];
    if (w <= 0 || h <= 0)
        return false;
    width = w;
    height = h;
    h264 = payload.data() + kShareVideoPayloadHeaderSize;
    len = payload.size() - kShareVideoPayloadHeaderSize;
    return true;
}

ShareFrameReassembler::Partial::Partial()
    : m_expectedLen(0)
{
}

ShareFrameReassembler::ShareFrameReassembler()
    : m_droppedFrames(0)
{
}

bool ShareFrameReassembler::push(const char* body, size_t len, ShareMediaFrame& out)
{
    ShareFrameHeader header;
    const char* payload = NULL;
    size_t payloadLen = 0;
    if (!ShareFraming::decodeBody(body, len, header, payload, payloadLen))
        return false;

    const bool first = (header.m_flags & screenshare_flag_first_frag) != 0;
    const bool last = (header.m_flags & screenshare_flag_last_frag) != 0;

    //非视频轨：整帧到达即交出，不带长度前缀
    if (!ShareFraming::isVideoTrack(header.m_track))
    {
        out.m_track = header.m_track;
        out.m_senderSeat = header.m_senderSeat;
        out.m_keyframe = (header.m_flags & screenshare_flag_keyframe) != 0;
        out.m_timestamp = header.m_timestamp;
        out.m_data.assign(payload, payloadLen);
        return true;
    }

    const StreamKey key(header.m_senderSeat, header.m_track);
    std::map<StreamKey, Partial>::iterator it = m_partials.find(key);

    if (first)
    {
        //累积中途又来了新的首分片：上一帧不完整，丢弃
        if (it != m_partials.end())
        {
            ++m_droppedFrames;
            m_partials.erase(it);
        }
        if (payloadLen < kShareFrameLengthPrefixSize)
        {
            ++m_droppedFrames;
            return false;
        }
        Partial partial;
        partial.m_header = header;
        partial.m_expectedLen = readBe32(payload);
        partial.m_data.assign(payload + kShareFrameLengthPrefixSize, payloadLen - kShareFrameLengthPrefixSize);
        it = m_partials.insert(std::make_pair(key, partial)).first;
    }
    else
    {
        //没有首分片的后续分片：无法还原，丢弃
        if (it == m_partials.end())
        {
            if (last)
                ++m_droppedFrames;
            return false;
        }
        //时间戳与首分片不一致：中间混进了别的帧，整帧丢弃
        if (it->second.m_header.m_timestamp != header.m_timestamp)
        {
            ++m_droppedFrames;
            m_partials.erase(it);
            return false;
        }
        it->second.m_data.append(payload, payloadLen);
    }

    if (!last)
        return false;

    //尾分片：核对累计长度，不符说明中间缺片
    Partial done = it->second;
    m_partials.erase(it);
    if (done.m_data.size() != done.m_expectedLen)
    {
        ++m_droppedFrames;
        return false;
    }
    out.m_track = done.m_header.m_track;
    out.m_senderSeat = done.m_header.m_senderSeat;
    out.m_keyframe = (done.m_header.m_flags & screenshare_flag_keyframe) != 0;
    out.m_timestamp = done.m_header.m_timestamp;
    out.m_data.swap(done.m_data);
    return true;
}

void ShareFrameReassembler::resetSender(int senderSeat)
{
    for (std::map<StreamKey, Partial>::iterator it = m_partials.begin(); it != m_partials.end();)
    {
        if (it->first.first == senderSeat)
            m_partials.erase(it++);
        else
            ++it;
    }
}

uint64_t ShareFrameReassembler::droppedFrames() const
{
    return m_droppedFrames;
}

} // namespace screenshare
