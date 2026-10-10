/**
 *  imgserver 地址探测服务的纯逻辑实现，见 P2PProbeCore.h。
 *  balloonwj@qq.com   2026-10-03
 */
#include "P2PProbeCore.h"

#include <string.h>

//类内静态常量的类外定义：g++ 在 ODR 使用（如测试里绑定到 const 引用）时需要
const size_t P2PStunCodec::kHeaderBytes;
const size_t P2PStunCodec::kBindingResponseBytes;
const size_t P2PStunCodec::kMinRequestBytes;

namespace {

//报文类型（RFC 5389 第 6 节）：Binding 请求、Binding 成功应答
const uint16_t kTypeBindingRequest = 0x0001;
const uint16_t kTypeBindingSuccess = 0x0101;

//魔数（RFC 5389 第 6 节），XOR-MAPPED-ADDRESS 用它的高 16 位异或端口、全部 32 位异或 IPv4 地址
const uint32_t kMagicCookie = 0x2112A442;
const int kCookieHighShift = 16;

//XOR-MAPPED-ADDRESS 属性（RFC 5389 第 15.2 节）：属性类型、值的长度（字节）、地址族取值 IPv4
const uint16_t kAttrXorMappedAddress = 0x0020;
const uint16_t kXorMappedAddressValueBytes = 8;
const char kFamilyIPv4 = 0x01;

//STUN 报文首字节的最高两位必须为 0，用来与其它协议的报文区分
const uint8_t kTopTwoBitsMask = 0xC0;

//长度字段必须是 4 的倍数（属性按 4 字节对齐）
const uint16_t kLengthAlignMask = 0x0003;

//报文头里各字段的偏移（字节）
const size_t kOffsetType = 0;
const size_t kOffsetLength = 2;
const size_t kOffsetCookie = 4;
const size_t kOffsetTxid = 8;

//限频窗口的长度（毫秒）
const int64_t kWindowMs = 1000;

//按网络字节序读 16 / 32 位整数
uint16_t ReadU16(const uint8_t* p)
{
    return (uint16_t)((p[0] << 8) | p[1]);
}

uint32_t ReadU32(const uint8_t* p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

//按网络字节序追加 16 / 32 位整数
void AppendU16(std::string& out, uint16_t v)
{
    out.push_back((char)(v >> 8));
    out.push_back((char)(v & 0xFF));
}

void AppendU32(std::string& out, uint32_t v)
{
    out.push_back((char)(v >> 24));
    out.push_back((char)((v >> 16) & 0xFF));
    out.push_back((char)((v >> 8) & 0xFF));
    out.push_back((char)(v & 0xFF));
}

} // namespace

bool P2PStunCodec::parseBindingRequest(const uint8_t* data, size_t len, StunTransactionId& txid)
{
    //严格不放大：比应答短的请求一律不应答（也包括不足一个报文头的）
    if (data == NULL || len < kMinRequestBytes)
        return false;
    if ((data[0] & kTopTwoBitsMask) != 0)
        return false;
    if (ReadU16(data + kOffsetType) != kTypeBindingRequest)
        return false;
    const uint16_t length = ReadU16(data + kOffsetLength);
    if ((length & kLengthAlignMask) != 0 || kHeaderBytes + length != len)
        return false;
    if (ReadU32(data + kOffsetCookie) != kMagicCookie)
        return false;
    memcpy(txid.m_bytes, data + kOffsetTxid, sizeof(txid.m_bytes));
    return true;
}

std::string P2PStunCodec::buildBindingResponse(const StunTransactionId& txid, uint32_t ip, uint16_t port)
{
    std::string out;
    out.reserve(kBindingResponseBytes);
    //报文头：类型、长度（头之后的字节数）、魔数、事务号
    AppendU16(out, kTypeBindingSuccess);
    AppendU16(out, (uint16_t)(kBindingResponseBytes - kHeaderBytes));
    AppendU32(out, kMagicCookie);
    out.append((const char*)txid.m_bytes, sizeof(txid.m_bytes));
    //XOR-MAPPED-ADDRESS：属性头，保留字节，地址族，异或后的端口与地址
    AppendU16(out, kAttrXorMappedAddress);
    AppendU16(out, kXorMappedAddressValueBytes);
    out.push_back(0);
    out.push_back(kFamilyIPv4);
    AppendU16(out, (uint16_t)(port ^ (uint16_t)(kMagicCookie >> kCookieHighShift)));
    AppendU32(out, ip ^ kMagicCookie);
    return out;
}

P2PProbeRateLimiter::P2PProbeRateLimiter(int maxPerSecond, size_t maxEntries)
    : m_maxPerSecond(maxPerSecond)
    , m_maxEntries(maxEntries)
{
}

bool P2PProbeRateLimiter::allow(uint32_t ip, int64_t nowMs)
{
    std::unordered_map<uint32_t, Window>::iterator it = m_windows.find(ip);
    if (it == m_windows.end())
    {
        //新来源：表满时先清理过期表项，仍满则拒绝，避免伪造大量来源 IP 时内存无限增长
        if (m_windows.size() >= m_maxEntries)
            purgeExpired(nowMs);
        if (m_windows.size() >= m_maxEntries || m_maxPerSecond <= 0)
            return false;
        Window w;
        w.m_startMs = nowMs;
        w.m_count = 1;
        m_windows[ip] = w;
        return true;
    }

    Window& w = it->second;
    //窗口已过一秒：开始新窗口
    if (nowMs - w.m_startMs >= kWindowMs)
    {
        w.m_startMs = nowMs;
        w.m_count = 0;
    }
    if (w.m_count >= m_maxPerSecond)
        return false;
    ++w.m_count;
    return true;
}

size_t P2PProbeRateLimiter::entryCount() const
{
    return m_windows.size();
}

void P2PProbeRateLimiter::purgeExpired(int64_t nowMs)
{
    for (std::unordered_map<uint32_t, Window>::iterator it = m_windows.begin(); it != m_windows.end();)
    {
        if (nowMs - it->second.m_startMs >= kWindowMs)
            it = m_windows.erase(it);
        else
            ++it;
    }
}
