/**
 *  可靠有序通道的实现，见 P2PReliableChannel.h。
 *  balloonwj@qq.com   2026-10-04
 */
#include "P2PReliableChannel.h"

#include <string.h>

#include "../Core/P2PPacket.h"           //kP2PMaxPlainBytes / kP2PRelayMaxPlainBytes
#include "../third-party/kcp/ikcp.h"
#include "P2PBbrCc.h"
#include "P2PChannelMux.h"              //IP2PMuxOutput / kP2PChannelHeaderBytes

namespace p2p {

namespace {

//一个字节的位数，通道号按网络字节序存放
const int kBitsPerByte = 8;

//取一个字节的掩码
const uint16_t kByteMask = 0xFF;

//KCP 的 MTU（字节）：一个链路报文的明文减去通道头，使满载的 KCP 输出恰好装满一个链路报文
const int kKcpMtu = (int)(kP2PMaxPlainBytes - kP2PChannelHeaderBytes);

//ikcp_nodelay 的参数：开启 nodelay（最小重传超时降到 30 毫秒、超时重传时 RTO 按 1.5 倍退避）
const int kKcpNodelayOn = 1;

//ikcp_nodelay 的参数：不关闭窗口控制。拥塞控制经 ikcp_setcc 换成 P2PBbrCc（第 4c 步起），在途数据上限由它给出
const int kKcpWindowControlOn = 0;

//流模式：按字节流使用，小块数据会并进同一个分段
const int kKcpStreamMode = 1;

//ikcp_recv 一次取出的上限（字节）：流模式下一次最多取出接收队列里的全部数据
const int kKcpRecvChunkBytes = 256 * 1024;

//中转路径一块数据的上限（字节）：中转单个报文的明文减去通道头
const size_t kRelayChunkBytes = kP2PRelayMaxPlainBytes - kP2PChannelHeaderBytes;

//写通道头
void PutHeader(uint16_t channelId, std::string& out)
{
    out.push_back((char)((channelId >> kBitsPerByte) & kByteMask));
    out.push_back((char)(channelId & kByteMask));
    out.push_back((char)p2p_channel_reliable);
}

} // namespace

P2PReliableChannel::P2PReliableChannel(uint16_t channelId, IP2PMuxOutput& out, bool overKcp)
    : m_channelId(channelId)
    , m_out(out)
    , m_overKcp(overKcp)
    , m_kcp(NULL)
    , m_lastNowMs(0)
    , m_outputNowMs(0)
    , m_blocked(false)
    , m_sentSegments(0)
{
    if (!m_overKcp)
        return;
    m_kcp = ikcp_create(channelId, this);
    ikcp_setoutput(m_kcp, &P2PReliableChannel::KcpOutput);
    ikcp_setmtu(m_kcp, kKcpMtu);
    ikcp_nodelay(m_kcp, kKcpNodelayOn, kP2PKcpIntervalMs, kP2PKcpFastResend, kKcpWindowControlOn);
    ikcp_wndsize(m_kcp, kP2PKcpWindow, kP2PKcpWindow);
    //ikcp_nodelay 把刷新间隔限制在 10 毫秒以上，直接写字段绕开
    m_kcp->interval = kP2PKcpIntervalMs;
    m_kcp->stream = kKcpStreamMode;
    //挂入 BBR 类拥塞控制，取代 KCP 自带的拥塞控制
    ikcp_setcc(m_kcp, P2PBbrCc::Ops());
}

P2PReliableChannel::~P2PReliableChannel()
{
    if (m_kcp != NULL)
    {
        ikcp_release(m_kcp);
        m_kcp = NULL;
    }
}

int P2PReliableChannel::Send(const uint8_t* data, uint32_t len, int64_t nowMs)
{
    if (data == NULL && len > 0)
        return p2p_send_too_large;
    if (QueuedBytes() >= kP2PReliableQueueLimitBytes)
    {
        m_blocked = true;
        return p2p_send_would_block;
    }
    if (len == 0)
        return p2p_send_ok;
    if (!m_overKcp)
    {
        SendDirect(data, len, nowMs);
        return p2p_send_ok;
    }
    KcpNow(nowMs);
    for (uint32_t offset = 0; offset < len;)
    {
        const uint32_t n = (len - offset < kP2PKcpSendChunkBytes) ? len - offset : (uint32_t)kP2PKcpSendChunkBytes;
        ikcp_send(m_kcp, reinterpret_cast<const char*>(data) + offset, (int)n);
        offset += n;
    }
    return p2p_send_ok;
}

void P2PReliableChannel::OnPayload(const uint8_t* payload, size_t len, int64_t nowMs, std::string& delivered)
{
    if (!m_overKcp)
    {
        delivered.append(reinterpret_cast<const char*>(payload), len);
        return;
    }
    const uint32_t now = KcpNow(nowMs);
    //ikcp_input 内部不读 current，但收到确认后的拥塞控制回调会读：先同步时间
    m_kcp->current = now;
    ikcp_input(m_kcp, reinterpret_cast<const char*>(payload), (long)len);
    if (m_recvBuf.size() < (size_t)kKcpRecvChunkBytes)
        m_recvBuf.resize(kKcpRecvChunkBytes);
    while (true)
    {
        const int n = ikcp_recv(m_kcp, &m_recvBuf[0], (int)m_recvBuf.size());
        if (n <= 0)
            break;
        delivered.append(&m_recvBuf[0], (size_t)n);
    }
}

void P2PReliableChannel::OnTimer(int64_t nowMs)
{
    if (!m_overKcp)
        return;
    m_outputNowMs = nowMs;
    ikcp_update(m_kcp, KcpNow(nowMs));
}

int64_t P2PReliableChannel::NextTimerMs() const
{
    if (!m_overKcp)
        return INT64_MAX;
    //没有待发、待确认的数据，也没有待回的确认与窗口探测：不需要定时
    if (ikcp_waitsnd(m_kcp) == 0 && m_kcp->ackcount == 0 && m_kcp->probe == 0 && m_kcp->updated != 0)
        return INT64_MAX;
    //ikcp_update 只在到达下一个刷新时刻（ts_flush）时才刷新，在那之前调用什么也不会发生。ikcp_check 在有分段到了
    //重传时刻却因发送额度不够没发出去时会一直返回「现在」，按它定时会让 IO 线程在两次刷新之间空转，所以按 ts_flush 定时
    if (m_kcp->updated == 0)
        return m_lastNowMs;
    const int32_t untilFlush = (int32_t)(m_kcp->ts_flush - (uint32_t)m_lastNowMs);
    return m_lastNowMs + ((untilFlush > 0) ? untilFlush : 0);
}

size_t P2PReliableChannel::QueuedBytes() const
{
    if (!m_overKcp)
        return m_out.QueuedBytes();
    return (size_t)ikcp_waitsnd(m_kcp) * (size_t)m_kcp->mss;
}

bool P2PReliableChannel::TakeWritable()
{
    if (!m_blocked || QueuedBytes() >= kP2PReliableWritableBytes)
        return false;
    m_blocked = false;
    return true;
}

uint32_t P2PReliableChannel::SmoothedRttMs() const
{
    if (!m_overKcp || m_kcp->rx_srtt <= 0)
        return 0;
    return (uint32_t)m_kcp->rx_srtt;
}

uint64_t P2PReliableChannel::RetransmittedSegments() const
{
    if (!m_overKcp)
        return 0;
    return m_kcp->xmit;
}

uint64_t P2PReliableChannel::SentSegments() const
{
    return m_sentSegments;
}

int P2PReliableChannel::KcpOutput(const char* buf, int len, IKCPCB* kcp, void* user)
{
    (void)kcp;
    P2PReliableChannel* self = static_cast<P2PReliableChannel*>(user);
    std::string plain;
    plain.reserve(kP2PChannelHeaderBytes + (size_t)len);
    PutHeader(self->m_channelId, plain);
    plain.append(buf, (size_t)len);
    self->m_out.SendDataPacket(plain, self->m_outputNowMs);
    ++self->m_sentSegments;
    return 0;
}

void P2PReliableChannel::SendDirect(const uint8_t* data, uint32_t len, int64_t nowMs)
{
    for (uint32_t offset = 0; offset < len;)
    {
        const uint32_t n = (len - offset < kRelayChunkBytes) ? len - offset : (uint32_t)kRelayChunkBytes;
        std::string plain;
        plain.reserve(kP2PChannelHeaderBytes + n);
        PutHeader(m_channelId, plain);
        plain.append(reinterpret_cast<const char*>(data) + offset, n);
        m_out.SendDataPacket(plain, nowMs);
        offset += n;
    }
}

uint32_t P2PReliableChannel::KcpNow(int64_t nowMs)
{
    m_lastNowMs = nowMs;
    m_outputNowMs = nowMs;
    return (uint32_t)nowMs;
}

} // namespace p2p
