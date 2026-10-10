/**
 *  P2P 链路报文格式的实现，见 P2PPacket.h。
 *  balloonwj@qq.com   2026-10-04
 */
#include "P2PPacket.h"

namespace p2p {

namespace {

//STUN 报文首字节最高两位的掩码：RFC 5389 第 6 节规定这两位必须为 0
const uint8_t kStunTopBitsMask = 0xC0;

//STUN 报文头的长度（字节），比它短的不可能是 STUN 报文
const size_t kStunHeaderBytes = 20;

//报文头里会话标签与计数器的长度（字节）
const size_t kSessionTagBytes = 4;
const size_t kCounterBytes = 8;

//一个字节的位数，按网络字节序拼装多字节整数时用
const int kBitsPerByte = 8;

//按网络字节序写 bytes 个字节的无符号整数
void WriteBigEndian(uint64_t value, size_t bytes, uint8_t* out)
{
    for (size_t i = 0; i < bytes; ++i)
        out[i] = (uint8_t)(value >> (kBitsPerByte * (bytes - 1 - i)));
}

//按网络字节序读 bytes 个字节的无符号整数
uint64_t ReadBigEndian(const uint8_t* in, size_t bytes)
{
    uint64_t value = 0;
    for (size_t i = 0; i < bytes; ++i)
        value = (value << kBitsPerByte) | in[i];
    return value;
}

} // namespace

bool IsP2PPacketType(uint8_t b)
{
    //类型取值连续：从探测到关闭
    return b >= p2p_packet_probe && b <= p2p_packet_close;
}

int ClassifyDatagram(const uint8_t* data, size_t len)
{
    if (data == NULL || len == 0)
        return p2p_datagram_unknown;
    if ((data[0] & kStunTopBitsMask) == 0)
        return (len >= kStunHeaderBytes) ? p2p_datagram_stun : p2p_datagram_unknown;
    if (IsP2PPacketType(data[0]) && len >= kP2PHeaderBytes + kP2PAuthTagBytes)
        return p2p_datagram_link;
    return p2p_datagram_unknown;
}

void WritePacketHeader(uint8_t type, uint32_t sessionTag, uint64_t counter, uint8_t* out)
{
    out[kP2POffsetType] = type;
    out[kP2POffsetReserved] = 0;
    WriteBigEndian(sessionTag, kSessionTagBytes, out + kP2POffsetSessionTag);
    WriteBigEndian(counter, kCounterBytes, out + kP2POffsetCounter);
}

bool ParsePacketHeader(const uint8_t* data, size_t len, P2PPacketHeader& out)
{
    if (data == NULL || len < kP2PHeaderBytes + kP2PAuthTagBytes || !IsP2PPacketType(data[kP2POffsetType]))
        return false;
    out.m_type = data[kP2POffsetType];
    out.m_sessionTag = (uint32_t)ReadBigEndian(data + kP2POffsetSessionTag, kSessionTagBytes);
    out.m_counter = ReadBigEndian(data + kP2POffsetCounter, kCounterBytes);
    return true;
}

} // namespace p2p
