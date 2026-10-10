/**
 *  地址探测客户端报文的实现，见 P2PStunClient.h。
 *  balloonwj@qq.com   2026-10-04
 */
#include "P2PStunClient.h"

#include <string.h>

namespace p2p {

//类内静态常量的类外定义
const size_t P2PStunClient::kRequestBytes;

namespace {

//报文类型（RFC 5389 第 6 节）：Binding 请求、Binding 成功应答
const uint16_t kTypeBindingRequest = 0x0001;
const uint16_t kTypeBindingSuccess = 0x0101;

//魔数（RFC 5389 第 6 节）；XOR-MAPPED-ADDRESS 用它的高 16 位异或端口、全部 32 位异或 IPv4 地址
const uint32_t kMagicCookie = 0x2112A442;
const int kCookieHighShift = 16;

//报文头的长度与各字段的偏移（字节）
const size_t kHeaderBytes = 20;
const size_t kOffsetLength = 2;
const size_t kOffsetCookie = 4;
const size_t kOffsetTxid = 8;

//属性头的长度（字节）、属性按 4 字节对齐
const size_t kAttrHeaderBytes = 4;
const size_t kAttrAlign = 4;
const size_t kAttrOffsetLength = 2;   //属性头里长度字段的偏移（字节）

//SOFTWARE 属性：类型（接收方可忽略）、值（正好 8 字节，使请求为 32 字节）
const uint16_t kAttrSoftware = 0x8022;
const char kSoftwareValue[] = "flamingo";
const size_t kSoftwareValueBytes = 8;

//XOR-MAPPED-ADDRESS 属性：类型、IPv4 时值的长度（字节）、地址族 IPv4、值里端口与地址的偏移
const uint16_t kAttrXorMappedAddress = 0x0020;
const uint16_t kXorMappedIpv4Bytes = 8;
const uint8_t kFamilyIpv4 = 0x01;
const size_t kXorOffsetFamily = 1;
const size_t kXorOffsetPort = 2;
const size_t kXorOffsetAddress = 4;

//一个字节的位数
const int kBitsPerByte = 8;

static_assert(sizeof(kSoftwareValue) - 1 == kSoftwareValueBytes, "SOFTWARE value must be 8 bytes");
static_assert(kHeaderBytes + kAttrHeaderBytes + kSoftwareValueBytes == P2PStunClient::kRequestBytes,
              "request must be 32 bytes");

void Append16(std::string& s, uint16_t v)
{
    s.push_back((char)(v >> kBitsPerByte));
    s.push_back((char)(v & 0xFF));
}

void Append32(std::string& s, uint32_t v)
{
    Append16(s, (uint16_t)(v >> (2 * kBitsPerByte)));
    Append16(s, (uint16_t)(v & 0xFFFF));
}

uint16_t Read16(const uint8_t* p)
{
    return (uint16_t)((p[0] << kBitsPerByte) | p[1]);
}

uint32_t Read32(const uint8_t* p)
{
    return ((uint32_t)Read16(p) << (2 * kBitsPerByte)) | Read16(p + 2);
}

} // namespace

std::string P2PStunClient::BuildRequest(const uint8_t* txid)
{
    std::string s;
    s.reserve(kRequestBytes);
    Append16(s, kTypeBindingRequest);
    Append16(s, (uint16_t)(kRequestBytes - kHeaderBytes));
    Append32(s, kMagicCookie);
    s.append(reinterpret_cast<const char*>(txid), kP2PStunTxidBytes);
    Append16(s, kAttrSoftware);
    Append16(s, (uint16_t)kSoftwareValueBytes);
    s.append(kSoftwareValue, kSoftwareValueBytes);
    return s;
}

bool P2PStunClient::ParseResponse(const uint8_t* data, size_t len, const uint8_t* txid, uint32_t& ip, uint16_t& port,
                                  uint8_t* outTxid)
{
    if (data == NULL || len < kHeaderBytes)
        return false;
    if (Read16(data) != kTypeBindingSuccess || Read32(data + kOffsetCookie) != kMagicCookie)
        return false;
    const size_t bodyLen = Read16(data + kOffsetLength);
    if (kHeaderBytes + bodyLen != len)
        return false;
    if (txid != NULL && memcmp(data + kOffsetTxid, txid, kP2PStunTxidBytes) != 0)
        return false;

    //逐个遍历属性，取第一个 IPv4 的 XOR-MAPPED-ADDRESS，其余属性跳过
    size_t pos = kHeaderBytes;
    while (pos + kAttrHeaderBytes <= len)
    {
        const uint16_t type = Read16(data + pos);
        const size_t valueLen = Read16(data + pos + kAttrOffsetLength);
        const size_t valueStart = pos + kAttrHeaderBytes;
        if (valueStart + valueLen > len)
            return false;
        if (type == kAttrXorMappedAddress && valueLen == kXorMappedIpv4Bytes
            && data[valueStart + kXorOffsetFamily] == kFamilyIpv4)
        {
            port = (uint16_t)(Read16(data + valueStart + kXorOffsetPort) ^ (uint16_t)(kMagicCookie >> kCookieHighShift));
            ip = Read32(data + valueStart + kXorOffsetAddress) ^ kMagicCookie;
            if (outTxid != NULL)
                memcpy(outTxid, data + kOffsetTxid, kP2PStunTxidBytes);
            return true;
        }
        //下一个属性：值按 4 字节对齐补齐
        pos = valueStart + ((valueLen + kAttrAlign - 1) / kAttrAlign) * kAttrAlign;
    }
    return false;
}

} // namespace p2p
