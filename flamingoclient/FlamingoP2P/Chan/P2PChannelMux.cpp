/**
 *  通道复用的实现，见 P2PChannelMux.h。
 *  balloonwj@qq.com   2026-10-04
 */
#include "P2PChannelMux.h"

#include "../Core/P2PPacket.h"   //kP2PMaxPlainBytes / kP2PRelayMaxPlainBytes

namespace p2p {

namespace {

//一个字节的位数，通道号按网络字节序存放
const int kBitsPerByte = 8;

//取一个字节的掩码
const uint16_t kByteMask = 0xFF;

//通道头里通道类型的偏移（字节）
const size_t kOffsetKind = 2;

//写通道头
void PutChannelHeader(uint16_t channelId, int kind, std::string& out)
{
    out.push_back((char)((channelId >> kBitsPerByte) & kByteMask));
    out.push_back((char)(channelId & kByteMask));
    out.push_back((char)kind);
}

} // namespace

P2PChannelMux::P2PChannelMux(IP2PMuxOutput& out)
    : m_out(out)
    , m_path(0)
    , m_bytesSent(0)
    , m_bytesReceived(0)
    , m_earlyBytes(0)
{
}

P2PChannelMux::~P2PChannelMux()
{
    for (std::map<uint16_t, Channel*>::iterator it = m_channels.begin(); it != m_channels.end(); ++it)
    {
        delete it->second->m_reliable;
        delete it->second;
    }
    m_channels.clear();
}

void P2PChannelMux::SetPath(int path)
{
    m_path = path;
}

int P2PChannelMux::Open(uint16_t channelId, int kind, IP2PChannelSink* sink)
{
    if (sink == NULL || (kind != p2p_channel_datagram && kind != p2p_channel_reliable) || m_channels.count(channelId) != 0)
        return p2p_send_bad_channel;
    Channel* ch = new Channel();
    ch->m_kind = kind;
    ch->m_sink = sink;
    //可靠通道：UDP 路径经 KCP，中转路径直接透传
    if (kind == p2p_channel_reliable)
        ch->m_reliable = new P2PReliableChannel(channelId, m_out, m_path != p2p_link_path_relay);
    m_channels[channelId] = ch;
    //对方先发来的报文等下一次 OnTimer 补交：本函数在业务方的线程上调用，事件只能由 IO 线程分发
    if (m_early.count(channelId) != 0)
        m_replayChannels.insert(channelId);
    return p2p_send_ok;
}

int P2PChannelMux::Send(uint16_t channelId, const uint8_t* data, uint32_t len, int64_t nowMs)
{
    std::map<uint16_t, Channel*>::iterator it = m_channels.find(channelId);
    if (it == m_channels.end())
        return p2p_send_bad_channel;
    if (data == NULL && len > 0)
        return p2p_send_too_large;
    if (it->second->m_reliable != NULL)
    {
        const int result = it->second->m_reliable->Send(data, len, nowMs);
        if (result == p2p_send_ok)
            m_bytesSent += len;
        return result;
    }
    return SendDatagram(channelId, *it->second, data, len, nowMs);
}

void P2PChannelMux::OnPacket(const std::string& plain, int64_t nowMs, std::vector<P2PChannelEvent>& events)
{
    if (plain.size() < kP2PChannelHeaderBytes)
        return;
    const uint8_t* p = reinterpret_cast<const uint8_t*>(plain.data());
    const uint16_t channelId = (uint16_t)((p[0] << kBitsPerByte) | p[1]);
    const int kind = p[kOffsetKind];
    std::map<uint16_t, Channel*>::iterator it = m_channels.find(channelId);
    //本方还没打开这条通道：先暂存，打开之后补交；已打开但暂存的还没补交：排在暂存的后面，保持到达顺序
    if (it == m_channels.end() || m_replayChannels.count(channelId) != 0)
    {
        KeepEarly(channelId, plain);
        return;
    }
    //两端对通道类型的约定不一致：丢弃
    if (it->second->m_kind != kind)
        return;
    Channel& ch = *it->second;
    std::string message;
    if (ch.m_reliable != NULL)
    {
        ch.m_reliable->OnPayload(p + kP2PChannelHeaderBytes, plain.size() - kP2PChannelHeaderBytes, nowMs, message);
        //收到确认后发送队列可能降了下来
        AppendWritable(channelId, ch, events);
        if (message.empty())
            return;
    }
    else if (!ch.m_reassembler.Add(p + kP2PChannelHeaderBytes, plain.size() - kP2PChannelHeaderBytes, nowMs, message))
    {
        return;
    }
    m_bytesReceived += message.size();
    P2PChannelEvent ev;
    ev.m_type = p2p_channel_event_data;
    ev.m_channelId = channelId;
    ev.m_sink = ch.m_sink;
    ev.m_data.swap(message);
    events.push_back(ev);
}

void P2PChannelMux::OnTimer(int64_t nowMs, std::vector<P2PChannelEvent>& events)
{
    ReplayEarly(nowMs, events);
    for (std::map<uint16_t, Channel*>::iterator it = m_channels.begin(); it != m_channels.end(); ++it)
    {
        Channel& ch = *it->second;
        if (ch.m_reliable == NULL || ch.m_reliable->NextTimerMs() > nowMs)
            continue;
        ch.m_reliable->OnTimer(nowMs);
        AppendWritable(it->first, ch, events);
    }
}

int64_t P2PChannelMux::NextTimerMs() const
{
    //有待补交的暂存报文：立即
    if (!m_replayChannels.empty())
        return 0;
    int64_t next = INT64_MAX;
    for (std::map<uint16_t, Channel*>::const_iterator it = m_channels.begin(); it != m_channels.end(); ++it)
    {
        if (it->second->m_reliable == NULL)
            continue;
        const int64_t due = it->second->m_reliable->NextTimerMs();
        if (due < next)
            next = due;
    }
    return next;
}

void P2PChannelMux::CheckWritable(std::vector<P2PChannelEvent>& events)
{
    for (std::map<uint16_t, Channel*>::iterator it = m_channels.begin(); it != m_channels.end(); ++it)
        AppendWritable(it->first, *it->second, events);
}

void P2PChannelMux::KeepEarly(uint16_t channelId, const std::string& plain)
{
    //超出上限的报文丢弃：UDP 路径上的可靠通道之后还会重传，中转路径上该通道的字节流从此缺一段，由业务方按通道
    //出错处理（业务方的帧长度校验会发现错位）
    if (m_earlyBytes + plain.size() > kP2PEarlyDataLimitBytes)
        return;
    m_early[channelId].push_back(plain);
    m_earlyBytes += plain.size();
}

void P2PChannelMux::ReplayEarly(int64_t nowMs, std::vector<P2PChannelEvent>& events)
{
    if (m_replayChannels.empty())
        return;
    std::set<uint16_t> channels;
    channels.swap(m_replayChannels);
    for (std::set<uint16_t>::iterator id = channels.begin(); id != channels.end(); ++id)
    {
        std::map<uint16_t, std::deque<std::string> >::iterator it = m_early.find(*id);
        if (it == m_early.end())
            continue;
        std::deque<std::string> packets;
        packets.swap(it->second);
        m_early.erase(it);
        //通道此时已经打开，OnPacket 照常按通道类型校验并交出
        for (std::deque<std::string>::iterator p = packets.begin(); p != packets.end(); ++p)
        {
            m_earlyBytes -= p->size();
            OnPacket(*p, nowMs, events);
        }
    }
}

bool P2PChannelMux::IsReliable(uint16_t channelId) const
{
    std::map<uint16_t, Channel*>::const_iterator it = m_channels.find(channelId);
    return it != m_channels.end() && it->second->m_reliable != NULL;
}

size_t P2PChannelMux::QueuedBytes() const
{
    size_t total = m_out.QueuedBytes();
    for (std::map<uint16_t, Channel*>::const_iterator it = m_channels.begin(); it != m_channels.end(); ++it)
    {
        //中转路径的可靠通道报告的就是发送路径上的积压，上面已经算过
        if (it->second->m_reliable != NULL && m_path != p2p_link_path_relay)
            total += it->second->m_reliable->QueuedBytes();
    }
    return total;
}

uint32_t P2PChannelMux::SmoothedRttMs() const
{
    uint32_t rtt = 0;
    for (std::map<uint16_t, Channel*>::const_iterator it = m_channels.begin(); it != m_channels.end(); ++it)
    {
        if (it->second->m_reliable != NULL && it->second->m_reliable->SmoothedRttMs() > rtt)
            rtt = it->second->m_reliable->SmoothedRttMs();
    }
    return rtt;
}

uint64_t P2PChannelMux::RetransmittedSegments() const
{
    uint64_t total = 0;
    for (std::map<uint16_t, Channel*>::const_iterator it = m_channels.begin(); it != m_channels.end(); ++it)
    {
        if (it->second->m_reliable != NULL)
            total += it->second->m_reliable->RetransmittedSegments();
    }
    return total;
}

uint64_t P2PChannelMux::SentSegments() const
{
    uint64_t total = 0;
    for (std::map<uint16_t, Channel*>::const_iterator it = m_channels.begin(); it != m_channels.end(); ++it)
    {
        if (it->second->m_reliable != NULL)
            total += it->second->m_reliable->SentSegments();
    }
    return total;
}

void P2PChannelMux::AppendWritable(uint16_t channelId, Channel& ch, std::vector<P2PChannelEvent>& events)
{
    if (ch.m_reliable == NULL || !ch.m_reliable->TakeWritable())
        return;
    P2PChannelEvent ev;
    ev.m_type = p2p_channel_event_writable;
    ev.m_channelId = channelId;
    ev.m_sink = ch.m_sink;
    events.push_back(ev);
}

size_t P2PChannelMux::MaxDatagramMessageBytes() const
{
    return P2PDatagramFragmenter::MaxMessageBytes(MaxPlain() - kP2PChannelHeaderBytes - kP2PDatagramFragHeaderBytes);
}

size_t P2PChannelMux::MaxPlain() const
{
    return (m_path == p2p_link_path_relay) ? kP2PRelayMaxPlainBytes : kP2PMaxPlainBytes;
}

int P2PChannelMux::SendDatagram(uint16_t channelId, Channel& ch, const uint8_t* data, uint32_t len, int64_t nowMs)
{
    const size_t maxFragPayload = MaxPlain() - kP2PChannelHeaderBytes - kP2PDatagramFragHeaderBytes;
    std::vector<std::string> frags;
    if (!P2PDatagramFragmenter::Split(ch.m_nextMsgId, data, len, maxFragPayload, frags))
        return p2p_send_too_large;
    ++ch.m_nextMsgId;
    for (size_t i = 0; i < frags.size(); ++i)
    {
        std::string plain;
        plain.reserve(kP2PChannelHeaderBytes + frags[i].size());
        PutChannelHeader(channelId, p2p_channel_datagram, plain);
        plain += frags[i];
        m_out.SendDataPacket(plain, nowMs);
    }
    m_bytesSent += len;
    return p2p_send_ok;
}

} // namespace p2p
