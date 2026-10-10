/**
 *  屏幕共享房间中转数据帧的帧头解析与改写，RoomRelayFrame.cpp
 *  格式说明见 RoomRelayFrame.h。
 *
 *  balloonwj@qq.com   2026-10-02
 */
#include "RoomRelayFrame.h"

#include "../chatserversrc/Protocol.h"
#include "../utils/RelayToken.h"

RoomRelayFrameHeader::RoomRelayFrameHeader()
    : m_track(0)
    , m_senderSeat(0)
    , m_flags(0)
    , m_timestamp(0)
{
}

namespace {

//帧头各字段在包体里的偏移，单位字节
const size_t kTrackOffset = 0;
const size_t kSenderSeatOffset = 1;
const size_t kFlagsOffset = 2;
const size_t kTimestampOffset = 3;
//时间戳的字节数与每字节的位数
const size_t kTimestampBytes = 8;
const int kBitsPerByte = 8;
//订阅控制帧载荷里座位数所在的偏移（载荷第 1 字节是控制类型），以及一帧最多带的座位数（座位号只有 1 字节）
const size_t kSubscribeCountOffset = 1;
const size_t kSubscribeMaxSeats = 254;
//键鼠输入控制帧载荷里目标座位与输入命令所在的偏移（载荷第 1 字节是控制类型，第 18 期）
const size_t kInputTargetOffset = 1;
const size_t kInputCommandOffset = 2;

} // namespace

bool RoomRelayFrame::parseHeader(const char* data, size_t len, RoomRelayFrameHeader& out)
{
    if (data == NULL || len < kRoomRelayHeaderSize)
        return false;

    const unsigned char* p = reinterpret_cast<const unsigned char*>(data);
    const int track = p[kTrackOffset];
    if (track < screenshare_track_screen || track > screenshare_track_control)
        return false;

    out.m_track = p[kTrackOffset];
    out.m_senderSeat = p[kSenderSeatOffset];
    out.m_flags = p[kFlagsOffset];
    //时间戳按大端读取：最高字节在前
    uint64_t timestamp = 0;
    for (size_t i = 0; i < kTimestampBytes; ++i)
        timestamp = (timestamp << kBitsPerByte) | p[kTimestampOffset + i];
    out.m_timestamp = timestamp;
    return true;
}

bool RoomRelayFrame::rewriteSenderSeat(std::string& body, uint8_t seat)
{
    if (body.size() < kRoomRelayHeaderSize)
        return false;
    body[kSenderSeatOffset] = static_cast<char>(seat);
    return true;
}

bool RoomRelayFrame::isVideoTrack(int track)
{
    return track == screenshare_track_screen || track == screenshare_track_camera;
}

std::string RoomRelayFrame::buildKeyFrameRequest(int videoTrack)
{
    std::string body(kRoomRelayHeaderSize, '\0');
    body[kTrackOffset] = static_cast<char>(screenshare_track_control);
    body[kSenderSeatOffset] = static_cast<char>(screenshare_seat_server);
    body[kFlagsOffset] = static_cast<char>(screenshare_flag_first_frag | screenshare_flag_last_frag);
    //时间戳为 0：控制帧不参与音画同步
    body.push_back(static_cast<char>(screenshare_control_keyframe_request));
    body.push_back(static_cast<char>(videoTrack));
    return body;
}

bool RoomRelayFrame::isTrackAllowedForRole(int role, int track)
{
    //群通话里没有屏幕画面、电脑声音与标注，这三种轨道一律不收
    if (role == RelayToken::role_groupcall_member)
        return track == screenshare_track_camera || track == screenshare_track_mic || track == screenshare_track_control;
    //只推流的连接（第 22 期）：只推自己的屏幕画面与共享内容状态等控制帧
    if (role == RelayToken::role_screenshare_publisher)
        return track == screenshare_track_screen || track == screenshare_track_control;
    return track >= screenshare_track_screen && track <= screenshare_track_control;
}

bool RoomRelayFrame::receivesRoomFrames(int role)
{
    return role != RelayToken::role_screenshare_publisher;
}

bool RoomRelayFrame::shouldForwardTo(int receiverRole, int64_t receiverUserId, int64_t senderUserId)
{
    if (!receivesRoomFrames(receiverRole))
    {
        return false;
    }
    //发送者本人的另一条连接：不回送
    if (senderUserId != 0 && receiverUserId == senderUserId)
    {
        return false;
    }
    return true;
}

std::string RoomRelayFrame::buildSubscribe(const std::vector<int>& seats)
{
    std::string body(kRoomRelayHeaderSize, '\0');
    body[kTrackOffset] = static_cast<char>(screenshare_track_control);
    body[kFlagsOffset] = static_cast<char>(screenshare_flag_first_frag | screenshare_flag_last_frag);
    const size_t count = seats.size() < kSubscribeMaxSeats ? seats.size() : kSubscribeMaxSeats;
    body.push_back(static_cast<char>(screenshare_control_subscribe));
    body.push_back(static_cast<char>(count));
    for (size_t i = 0; i < count; ++i)
        body.push_back(static_cast<char>(seats[i]));
    return body;
}

bool RoomRelayFrame::parseSubscribe(const std::string& body, std::vector<int>& seats)
{
    seats.clear();
    RoomRelayFrameHeader header;
    if (!parseHeader(body.data(), body.size(), header) || header.m_track != screenshare_track_control)
        return false;
    const size_t payloadLen = body.size() - kRoomRelayHeaderSize;
    const unsigned char* payload = reinterpret_cast<const unsigned char*>(body.data()) + kRoomRelayHeaderSize;
    //至少要有控制类型与座位数两个字节
    if (payloadLen <= kSubscribeCountOffset || payload[0] != screenshare_control_subscribe)
        return false;
    const size_t count = payload[kSubscribeCountOffset];
    if (payloadLen != kSubscribeCountOffset + 1 + count)
        return false;
    for (size_t i = 0; i < count; ++i)
        seats.push_back(payload[kSubscribeCountOffset + 1 + i]);
    return true;
}

bool RoomRelayFrame::parseInputTarget(const std::string& body, int& targetSeat)
{
    RoomRelayFrameHeader header;
    if (!parseHeader(body.data(), body.size(), header) || header.m_track != screenshare_track_control)
        return false;
    const size_t payloadLen = body.size() - kRoomRelayHeaderSize;
    const unsigned char* payload = reinterpret_cast<const unsigned char*>(body.data()) + kRoomRelayHeaderSize;
    //控制类型、目标座位之后至少还要有一个字节的输入命令
    if (payloadLen <= kInputCommandOffset || payload[0] != screenshare_control_input)
        return false;
    const int seat = payload[kInputTargetOffset];
    //255 是服务端自己的座位号，不是任何成员
    if (seat >= screenshare_seat_server)
        return false;
    targetSeat = seat;
    return true;
}
