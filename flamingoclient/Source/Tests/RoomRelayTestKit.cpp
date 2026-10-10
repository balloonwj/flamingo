/**
 *  屏幕共享房间中转联调用的测试工具，见 RoomRelayTestKit.h。
 *  balloonwj@qq.com   2026-10-03（从 ScreenShareRelayLiveTests.cpp 抽出）
 */
#include "stdafx.h"

#include "RoomRelayTestKit.h"

#include <stdio.h>
#include <time.h>

#include "../net/FrameCodec.h"
#include "../net/FrameCrypto.h"
#include "../net/FramePayload.h"
#include "../third-party/jsoncpp-1.9.0/json.h"
#include "../../../flamingoserver/utils/RelayToken.h"

using namespace net;
using namespace livetest;

namespace roomrelaytest {

namespace {

//令牌有效期，单位毫秒
const int64_t kRoomTokenTtlMs = 5 * 60 * 1000;
//秒到毫秒
const int64_t kMsPerSecond = 1000;

//房间中转帧头的长度与各字段偏移（与服务端 imgserversrc/RoomRelayFrame.h 一致）
const size_t kHeaderSize = 11;
const size_t kTrackOffset = 0;
const size_t kSeatOffset = 1;
const size_t kFlagsOffset = 2;
const size_t kTimestampOffset = 3;
const size_t kTimestampBytes = 8;
const int kBitsPerByte = 8;

//令牌里的 uid：imgserver 不校验 uid，取一个固定值即可
const int64_t kAnyUid = 100001;

bool parseAckCode(const std::string& json, int& code)
{
    Json::CharReaderBuilder b;
    JSONCPP_STRING errs;
    Json::Value root;
    Json::CharReader* reader = b.newCharReader();
    const bool ok = reader->parse(json.c_str(), json.c_str() + json.size(), &root, &errs);
    delete reader;
    if (!ok || !root.isObject() || !root["code"].isInt())
        return false;
    code = root["code"].asInt();
    return true;
}

} // namespace

std::string makeRoomId()
{
    static unsigned int s_seq = 0;
    ++s_seq;
    char buf[64] = { 0 };
    sprintf_s(buf, sizeof(buf), "ss-relaytest-%llx-%u", (unsigned long long)::GetTickCount64(), s_seq);
    return std::string(buf);
}

std::string makeToken(const std::string& roomId, int role, int seat, const char* key)
{
    RelayTokenPayload payload;
    payload.sessionid = roomId;
    payload.role = role;
    payload.uid = kAnyUid;
    payload.peeruid = seat;
    payload.expire = static_cast<int64_t>(time(NULL)) * kMsPerSecond + kRoomTokenTtlMs;
    return RelayToken::sign(payload, key);
}

bool joinRoom(RoomPeer& p, const std::string& roomId, int role, int seat, const char* key, int& code,
              std::string& reason)
{
    code = -1;
    if (!p.m_conn.Connect(kRoomRelayPort))
    {
        reason = "connect relay port failed";
        return false;
    }
    if (!DoHandshake(p.m_conn, p.m_hs, reason))
        return false;

    const std::string body = "{\"token\":\"" + makeToken(roomId, role, seat, key) + "\"}";
    std::string wire;
    uint8_t flags = 0;
    uint32_t originlen = 0;
    ++p.m_seq;
    if (!encodeFramePayload(cmd_screenshare_relay_hello, p.m_seq, body, &p.m_hs.cipher(), 0, payload_compress_never,
                            wire, flags, originlen))
    {
        reason = "encode hello failed";
        return false;
    }
    std::string frame;
    if (!encodeFrame(cmd_screenshare_relay_hello, p.m_seq, flags, wire.data(), (uint32_t)wire.size(), originlen,
                     frame) ||
        !p.m_conn.SendAll(frame))
    {
        reason = "send hello failed";
        return false;
    }

    frame_header header;
    std::string respWire;
    if (!p.m_conn.RecvFrame(header, respWire, MAX_BODY_FILESERVER))
    {
        reason = "no hello ack (server closed the connection?)";
        return false;
    }
    std::string plain;
    if (!decodeFramePayload(header.cmd, header.seq, header.flags, header.originlen, respWire, &p.m_hs.cipher(),
                            plain, reason))
        return false;
    if (header.cmd != cmd_screenshare_relay_hello || !parseAckCode(plain, code))
    {
        reason = "unexpected hello ack: " + plain;
        return false;
    }
    return true;
}

bool sendRoomFrame(RoomPeer& p, int track, int seat, int flags, uint64_t timestamp, const std::string& payload,
                   bool markCompressed)
{
    std::string body;
    body.push_back(static_cast<char>(track));
    body.push_back(static_cast<char>(seat));
    body.push_back(static_cast<char>(flags));
    for (int i = (int)kTimestampBytes - 1; i >= 0; --i)
        body.push_back(static_cast<char>((timestamp >> (i * kBitsPerByte)) & 0xFF));
    body += payload;

    ++p.m_seq;
    std::string wire;
    uint8_t outFlags = 0;
    const uint8_t srcFlags = markCompressed ? (uint8_t)frame_flag_compressed : (uint8_t)0;
    const uint32_t originlen = (uint32_t)body.size();
    if (!encryptFramePayload(cmd_screenshare_relay_data, p.m_seq, srcFlags, originlen, body, &p.m_hs.cipher(), wire,
                             outFlags))
        return false;
    std::string frame;
    if (!encodeFrame(cmd_screenshare_relay_data, p.m_seq, outFlags, wire.data(), (uint32_t)wire.size(), originlen,
                     frame))
        return false;
    return p.m_conn.SendAll(frame);
}

bool recvRoomFrame(RoomPeer& p, RoomFrame& out)
{
    frame_header header;
    std::string wire;
    if (!p.m_conn.RecvFrame(header, wire, MAX_BODY_FILESERVER))
        return false;
    std::string plain;
    std::string err;
    if (!decryptFramePayload(header.cmd, header.seq, header.flags, header.originlen, wire, &p.m_hs.cipher(), plain,
                             err))
        return false;
    if (header.cmd != cmd_screenshare_relay_data || plain.size() < kHeaderSize)
        return false;
    const unsigned char* b = reinterpret_cast<const unsigned char*>(plain.data());
    out.m_track = b[kTrackOffset];
    out.m_seat = b[kSeatOffset];
    out.m_flags = b[kFlagsOffset];
    out.m_timestamp = 0;
    for (size_t i = 0; i < kTimestampBytes; ++i)
        out.m_timestamp = (out.m_timestamp << kBitsPerByte) | b[kTimestampOffset + i];
    out.m_payload = plain.substr(kHeaderSize);
    return true;
}

bool isKeyFrameRequest(const RoomFrame& f, int videoTrack)
{
    return f.m_track == screenshare_track_control && f.m_seat == screenshare_seat_server &&
           f.m_payload.size() == 2 &&
           static_cast<unsigned char>(f.m_payload[0]) == screenshare_control_keyframe_request &&
           static_cast<unsigned char>(f.m_payload[1]) == videoTrack;
}

std::string makePayload(size_t len, int salt)
{
    std::string s(len, '\0');
    for (size_t i = 0; i < len; ++i)
        s[i] = static_cast<char>((i + salt) & 0xFF);
    return s;
}

} // namespace roomrelaytest
