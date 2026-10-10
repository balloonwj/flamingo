/**
 *  云端录制的原始帧文件（.frec）格式，说明见 CloudRecordFile.h。
 *  balloonwj@qq.com   2026-10-06
 */
#include "CloudRecordFile.h"

#include <stdio.h>
#include "../chatserversrc/Protocol.h"   //screenshare_track_*

namespace cloudrecord {

namespace {

//各字段的字节数
const size_t kU8Bytes = 1;
const size_t kU16Bytes = 2;
const size_t kU32Bytes = 4;
const size_t kU64Bytes = 8;
//一字节的位数，小端拼接时用
const int kBitsPerByte = 8;
//记录头：类型 u8 + 相对毫秒 u32 + 负载长度 u32
const size_t kRecordHeadBytes = kU8Bytes + kU32Bytes + kU32Bytes;
//座位负载：座位号 u8 + userid u64
const size_t kSeatPayloadBytes = kU8Bytes + kU64Bytes;
//文件名缓冲区长度
const size_t kFileNameChars = 64;

void putLe(std::string& out, uint64_t v, size_t bytes)
{
    for (size_t i = 0; i < bytes; ++i)
        out.push_back(static_cast<char>((v >> (i * kBitsPerByte)) & 0xFF));
}

uint64_t getLe(const std::string& data, size_t pos, size_t bytes)
{
    uint64_t v = 0;
    for (size_t i = 0; i < bytes; ++i)
        v |= static_cast<uint64_t>(static_cast<unsigned char>(data[pos + i])) << (i * kBitsPerByte);
    return v;
}

} // namespace

FrecHeader::FrecHeader()
    : m_version(kFrecVersion)
    , m_flags(0)
    , m_recordId(0)
    , m_startTimeMs(0)
{
}

std::string EncodeHeader(const FrecHeader& header)
{
    std::string out(kFrecMagic, kFrecMagicBytes);
    putLe(out, header.m_version, kU16Bytes);
    putLe(out, header.m_flags, kU16Bytes);
    putLe(out, header.m_recordId, kU64Bytes);
    putLe(out, static_cast<uint64_t>(header.m_startTimeMs), kU64Bytes);
    const std::string roomId = header.m_roomId.size() > kFrecMaxRoomIdBytes ? header.m_roomId.substr(0, kFrecMaxRoomIdBytes)
                                                                           : header.m_roomId;
    putLe(out, roomId.size(), kU16Bytes);
    out += roomId;
    return out;
}

std::string EncodeRecord(uint8_t type, uint32_t relMs, const std::string& payload)
{
    std::string out;
    out.reserve(kRecordHeadBytes + payload.size());
    putLe(out, type, kU8Bytes);
    putLe(out, relMs, kU32Bytes);
    putLe(out, payload.size(), kU32Bytes);
    out += payload;
    return out;
}

std::string EncodeSeatPayload(int seat, int64_t userId)
{
    std::string out;
    putLe(out, static_cast<uint64_t>(seat), kU8Bytes);
    putLe(out, static_cast<uint64_t>(userId), kU64Bytes);
    return out;
}

bool DecodeHeader(const std::string& data, size_t& pos, FrecHeader& out)
{
    const size_t fixed = kFrecMagicBytes + kU16Bytes + kU16Bytes + kU64Bytes + kU64Bytes + kU16Bytes;
    if (pos + fixed > data.size())
        return false;
    if (data.compare(pos, kFrecMagicBytes, kFrecMagic, kFrecMagicBytes) != 0)
        return false;
    size_t p = pos + kFrecMagicBytes;
    const uint16_t version = static_cast<uint16_t>(getLe(data, p, kU16Bytes));
    p += kU16Bytes;
    //只认识当前版本；以后加了新字段再按版本分别处理
    if (version != kFrecVersion)
        return false;
    out.m_version = version;
    out.m_flags = static_cast<uint16_t>(getLe(data, p, kU16Bytes));
    p += kU16Bytes;
    out.m_recordId = getLe(data, p, kU64Bytes);
    p += kU64Bytes;
    out.m_startTimeMs = static_cast<int64_t>(getLe(data, p, kU64Bytes));
    p += kU64Bytes;
    const size_t roomLen = static_cast<size_t>(getLe(data, p, kU16Bytes));
    p += kU16Bytes;
    if (roomLen > kFrecMaxRoomIdBytes || p + roomLen > data.size())
        return false;
    out.m_roomId = data.substr(p, roomLen);
    pos = p + roomLen;
    return true;
}

bool DecodeRecord(const std::string& data, size_t& pos, uint8_t& type, uint32_t& relMs, std::string& payload)
{
    if (pos + kRecordHeadBytes > data.size())
        return false;
    size_t p = pos;
    const uint8_t t = static_cast<uint8_t>(getLe(data, p, kU8Bytes));
    p += kU8Bytes;
    const uint32_t ms = static_cast<uint32_t>(getLe(data, p, kU32Bytes));
    p += kU32Bytes;
    const uint32_t len = static_cast<uint32_t>(getLe(data, p, kU32Bytes));
    p += kU32Bytes;
    if (len > kFrecMaxPayloadBytes || p + len > data.size())
        return false;
    type = t;
    relMs = ms;
    payload = data.substr(p, len);
    pos = p + len;
    return true;
}

bool DecodeSeatPayload(const std::string& payload, int& seat, int64_t& userId)
{
    if (payload.size() != kSeatPayloadBytes)
        return false;
    seat = static_cast<int>(getLe(payload, 0, kU8Bytes));
    userId = static_cast<int64_t>(getLe(payload, kU8Bytes, kU64Bytes));
    return true;
}

bool ShouldRecordTrack(int track, bool audioOnly)
{
    if (audioOnly)
        return track == screenshare_track_mic || track == screenshare_track_system_audio;
    return track >= screenshare_track_screen && track <= screenshare_track_control;
}

std::string FrecFileName(uint64_t recordId)
{
    char buf[kFileNameChars] = { 0 };
    snprintf(buf, sizeof(buf), "rec_%llu.frec", static_cast<unsigned long long>(recordId));
    return std::string(buf);
}

} // namespace cloudrecord
