/**
 *  数据报通道分片与重组的实现，见 P2PDatagramFrag.h。
 *  balloonwj@qq.com   2026-10-04
 */
#include "P2PDatagramFrag.h"

namespace p2p {

namespace {

//一个字节的位数，多字节字段按网络字节序拼装
const int kBitsPerByte = 8;

//取一个字节的掩码
const uint32_t kByteMask = 0xFF;

//分片头里各字段的偏移（字节）
const size_t kOffsetIndex = 4;
const size_t kOffsetCount = 6;

//按网络字节序写 4 字节与 2 字节
void PutU32(uint32_t v, std::string& out)
{
    out.push_back((char)((v >> (3 * kBitsPerByte)) & kByteMask));
    out.push_back((char)((v >> (2 * kBitsPerByte)) & kByteMask));
    out.push_back((char)((v >> kBitsPerByte) & kByteMask));
    out.push_back((char)(v & kByteMask));
}

void PutU16(uint16_t v, std::string& out)
{
    out.push_back((char)((v >> kBitsPerByte) & kByteMask));
    out.push_back((char)(v & kByteMask));
}

//按网络字节序读 4 字节与 2 字节
uint32_t GetU32(const uint8_t* p)
{
    return ((uint32_t)p[0] << (3 * kBitsPerByte)) | ((uint32_t)p[1] << (2 * kBitsPerByte))
         | ((uint32_t)p[2] << kBitsPerByte) | (uint32_t)p[3];
}

uint16_t GetU16(const uint8_t* p)
{
    return (uint16_t)(((uint16_t)p[0] << kBitsPerByte) | (uint16_t)p[1]);
}

} // namespace

bool P2PDatagramFragmenter::Split(uint32_t msgId, const uint8_t* data, size_t len, size_t maxFragPayload,
                                  std::vector<std::string>& out)
{
    out.clear();
    if (maxFragPayload == 0 || (data == NULL && len > 0))
        return false;
    //空消息也发一片
    const size_t count = (len == 0) ? 1 : (len + maxFragPayload - 1) / maxFragPayload;
    if (count > kP2PMaxDatagramFragments)
        return false;
    out.reserve(count);
    for (size_t i = 0; i < count; ++i)
    {
        const size_t offset = i * maxFragPayload;
        const size_t n = (len - offset < maxFragPayload) ? len - offset : maxFragPayload;
        std::string frag;
        frag.reserve(kP2PDatagramFragHeaderBytes + n);
        PutU32(msgId, frag);
        PutU16((uint16_t)i, frag);
        PutU16((uint16_t)count, frag);
        if (n > 0)
            frag.append(reinterpret_cast<const char*>(data) + offset, n);
        out.push_back(frag);
    }
    return true;
}

size_t P2PDatagramFragmenter::MaxMessageBytes(size_t maxFragPayload)
{
    return maxFragPayload * kP2PMaxDatagramFragments;
}

P2PDatagramReassembler::P2PDatagramReassembler()
    : m_dropped(0)
{
}

bool P2PDatagramReassembler::Add(const uint8_t* frag, size_t len, int64_t nowMs, std::string& message)
{
    Expire(nowMs);
    if (frag == NULL || len < kP2PDatagramFragHeaderBytes)
        return false;
    const uint32_t msgId = GetU32(frag);
    const uint16_t index = GetU16(frag + kOffsetIndex);
    const uint16_t count = GetU16(frag + kOffsetCount);
    if (count == 0 || count > kP2PMaxDatagramFragments || index >= count)
        return false;
    const uint8_t* payload = frag + kP2PDatagramFragHeaderBytes;
    const size_t payloadLen = len - kP2PDatagramFragHeaderBytes;

    //只有一片的消息不进表，直接交出
    if (count == 1)
    {
        message.assign(reinterpret_cast<const char*>(payload), payloadLen);
        return true;
    }

    std::map<uint32_t, Pending>::iterator it = m_pending.find(msgId);
    if (it == m_pending.end())
    {
        if (m_pending.size() >= kP2PMaxPendingDatagrams)
            EvictOldest();
        Pending p;
        p.m_firstMs = nowMs;
        p.m_count = count;
        p.m_received = 0;
        p.m_frags.resize(count);
        p.m_have.assign(count, false);
        it = m_pending.insert(std::make_pair(msgId, p)).first;
    }
    Pending& p = it->second;
    //片数与已记录的不一致：发送端不会这样发，按非法丢弃整条消息
    if (p.m_count != count)
    {
        m_pending.erase(it);
        ++m_dropped;
        return false;
    }
    if (p.m_have[index])
        return false;
    p.m_have[index] = true;
    p.m_frags[index].assign(reinterpret_cast<const char*>(payload), payloadLen);
    ++p.m_received;
    if (p.m_received < p.m_count)
        return false;

    message.clear();
    for (size_t i = 0; i < p.m_frags.size(); ++i)
        message += p.m_frags[i];
    m_pending.erase(it);
    return true;
}

size_t P2PDatagramReassembler::PendingCount() const
{
    return m_pending.size();
}

uint64_t P2PDatagramReassembler::DroppedCount() const
{
    return m_dropped;
}

void P2PDatagramReassembler::Expire(int64_t nowMs)
{
    for (std::map<uint32_t, Pending>::iterator it = m_pending.begin(); it != m_pending.end();)
    {
        if (nowMs - it->second.m_firstMs >= kP2PDatagramReassemblyMs)
        {
            it = m_pending.erase(it);
            ++m_dropped;
        }
        else
        {
            ++it;
        }
    }
}

void P2PDatagramReassembler::EvictOldest()
{
    std::map<uint32_t, Pending>::iterator oldest = m_pending.end();
    for (std::map<uint32_t, Pending>::iterator it = m_pending.begin(); it != m_pending.end(); ++it)
    {
        if (oldest == m_pending.end() || it->second.m_firstMs < oldest->second.m_firstMs)
            oldest = it;
    }
    if (oldest != m_pending.end())
    {
        m_pending.erase(oldest);
        ++m_dropped;
    }
}

} // namespace p2p
