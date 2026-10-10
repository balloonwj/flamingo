/**
 *  P2P 会话状态机的实现，见 P2PLinkSession.h。
 *  balloonwj@qq.com   2026-10-04
 */
#include "P2PLinkSession.h"

#include <stdint.h>
#include <string.h>

#include "../../Source/net/Protocol.h"   //cmd_p2p_* / p2p_close_reason

namespace p2p {

namespace {

//地址对文本里本方地址未知时的写法
const char* const kUnknownLocal = "0.0.0.0";

//中转路径的地址对文本前缀（2304 的 pair 只用于服务端日志）
const char* const kRelayPairPrefix = "relay ";

//丢包率按千分比报告
const uint64_t kPermille = 1000;

} // namespace

P2PLinkSession::P2PLinkSession(const P2PSessionInfo& info, IP2PDatagramSender& udp, IP2PSessionObserver& observer)
    : m_info(info)
    , m_udp(udp)
    , m_observer(observer)
    , m_prober(udp)
    , m_checker(udp, m_cipher, info.IsControlling(), info.m_connectTimeoutMs)
    , m_state(p2p_session_idle)
    , m_peerSignalReceived(false)
    , m_beginMs(0)
    , m_signalDeadlineMs(0)
    , m_lastSendMs(0)
    , m_lastRecvMs(0)
    , m_path(0)
    , m_forceRelay(false)
    , m_relay(NULL)
    , m_relayDeadlineMs(0)
    , m_nextNominateMs(0)
    , m_mux(*this)
    , m_batchDepth(0)
    , m_dispatchDepth(0)
    , m_closedByBusiness(false)
{
}

void P2PLinkSession::SetForceRelay(bool force)
{
    std::lock_guard<std::recursive_mutex> guard(m_mutex);
    m_forceRelay = force;
}

void P2PLinkSession::Begin(const std::vector<P2PCandidate>& localHosts, const std::vector<P2PProbeServer>& probes,
                           int64_t nowMs)
{
    EventFlush flush(*this);
    std::lock_guard<std::recursive_mutex> guard(m_mutex);
    if (m_state != p2p_session_idle)
        return;
    m_localHosts = localHosts;
    m_beginMs = nowMs;
    //对方的 2303 必须在这个时刻之前到：地址探测的预留时间 + 建链超时 + 应答方的额外等待
    m_signalDeadlineMs = nowMs + kP2PGatherBudgetMs + m_info.m_connectTimeoutMs + kP2PControlledExtraMs;
    m_keys.Generate();
    if (probes.empty())
    {
        SendOurSignal(nowMs);
        return;
    }
    m_state = p2p_session_gathering;
    m_prober.Start(probes, nowMs);
}

void P2PLinkSession::OnPeerSignal(const std::string& publicKey, const std::vector<P2PCandidate>& candidates, int64_t nowMs)
{
    EventFlush flush(*this);
    std::lock_guard<std::recursive_mutex> guard(m_mutex);
    //尚未开始、已在打洞、已在走中转或已结束：重复或迟到的 2303 忽略
    if (m_state == p2p_session_idle || m_state == p2p_session_checking || m_state == p2p_session_relaying
        || m_state == p2p_session_connected || IsFinished())
        return;
    uint8_t peerKey[kP2PPublicKeyBytes];
    if (!P2PKeyPair::DecodePublicKey(publicKey, peerKey) || !m_cipher.Init(m_info.IsControlling(), m_keys, peerKey))
    {
        FailConnect(error_code_bad_request, nowMs);
        return;
    }
    m_peerCandidates = candidates;
    m_peerSignalReceived = true;
    //本方的 2303 已经发出：立即开始打洞；否则等探测完成发出 2303 时再开始
    if (m_state == p2p_session_signaling)
        StartChecking(nowMs);
}

void P2PLinkSession::OnDatagram(uint32_t fromIp, uint16_t fromPort, const uint8_t* data, size_t len, int64_t nowMs)
{
    EventFlush flush(*this);
    std::lock_guard<std::recursive_mutex> guard(m_mutex);
    BatchScope batch(*this);
    if (IsFinished())
        return;
    const int kind = ClassifyDatagram(data, len);
    if (kind == p2p_datagram_stun)
    {
        if (m_state == p2p_session_gathering)
        {
            m_prober.OnStunDatagram(data, len, nowMs);
            if (m_prober.IsDone())
                SendOurSignal(nowMs);
        }
        return;
    }
    if (kind != p2p_datagram_link || !m_cipher.IsReady())
        return;
    //正在走中转或已经经中转就绪：UDP 上迟到的链路报文一律忽略（路径选定后不切换）
    if (m_state == p2p_session_relaying || m_path == p2p_link_path_relay)
        return;
    uint8_t type = 0;
    std::string plain;
    if (m_cipher.Open(data, len, type, plain) != p2p_open_ok)
        return;

    if (m_state == p2p_session_checking)
    {
        m_checker.OnPacket(fromIp, fromPort, type, plain, nowMs);
        if (m_checker.State() == p2p_check_connected)
            MarkReady(nowMs);
        return;
    }
    if (m_state != p2p_session_connected)
        return;

    m_lastRecvMs = nowMs;
    //连通性检查的报文：对方可能没收到选定确认而重发选定，交给检查状态机照常应答
    if (type != p2p_packet_data && type != p2p_packet_keepalive && type != p2p_packet_close)
    {
        m_checker.OnPacket(fromIp, fromPort, type, plain, nowMs);
        return;
    }
    HandleConnectedPacket(type, plain, nowMs);
}

void P2PLinkSession::OnRelayConnected(IP2PDatagramSender& relay, int64_t nowMs)
{
    EventFlush flush(*this);
    std::lock_guard<std::recursive_mutex> guard(m_mutex);
    if (m_state != p2p_session_relaying || m_relay != NULL)
        return;
    m_relay = &relay;
    //发起方立即发出第一个「选定」，之后按间隔重发，直到收到「选定确认」；应答方等「选定」
    if (m_info.IsControlling())
    {
        SendRelayPacket(p2p_packet_nominate, nowMs);
        m_nextNominateMs = nowMs + kP2PRelayNominateIntervalMs;
    }
}

void P2PLinkSession::OnRelayFailed(int64_t nowMs)
{
    EventFlush flush(*this);
    std::lock_guard<std::recursive_mutex> guard(m_mutex);
    if (m_state != p2p_session_relaying)
        return;
    FailConnect(0, nowMs);
}

void P2PLinkSession::OnRelayPacket(const uint8_t* data, size_t len, int64_t nowMs)
{
    EventFlush flush(*this);
    std::lock_guard<std::recursive_mutex> guard(m_mutex);
    if (IsFinished() || m_relay == NULL || !m_cipher.IsReady())
        return;
    if (ClassifyDatagram(data, len) != p2p_datagram_link)
        return;
    uint8_t type = 0;
    std::string plain;
    if (m_cipher.Open(data, len, type, plain) != p2p_open_ok)
        return;

    if (m_state == p2p_session_relaying)
    {
        //应答方收到「选定」：回「选定确认」，就绪
        if (!m_info.IsControlling() && type == p2p_packet_nominate)
        {
            SendRelayPacket(p2p_packet_nominate_ack, nowMs);
            MarkRelayReady(nowMs);
        }
        //发起方收到「选定确认」：就绪
        else if (m_info.IsControlling() && type == p2p_packet_nominate_ack)
        {
            MarkRelayReady(nowMs);
        }
        return;
    }
    if (m_state != p2p_session_connected || m_path != p2p_link_path_relay)
        return;

    m_lastRecvMs = nowMs;
    //应答方又收到「选定」：发起方在确认到达之前多发了几个，照样回确认
    if (type == p2p_packet_nominate)
    {
        if (!m_info.IsControlling())
            SendRelayPacket(p2p_packet_nominate_ack, nowMs);
        return;
    }
    HandleConnectedPacket(type, plain, nowMs);
}

void P2PLinkSession::OnRelayBroken(int64_t nowMs)
{
    EventFlush flush(*this);
    std::lock_guard<std::recursive_mutex> guard(m_mutex);
    if (m_state == p2p_session_relaying)
    {
        FailConnect(0, nowMs);
        return;
    }
    if (m_state == p2p_session_connected && m_path == p2p_link_path_relay)
        FinishClosed(p2p_close_link_timeout, true);
}

bool P2PLinkSession::CanRelay() const
{
    std::lock_guard<std::recursive_mutex> guard(m_mutex);
    return !m_info.m_relay.m_host.empty() && m_info.m_relay.m_port != 0 && !m_info.m_token.empty();
}

void P2PLinkSession::OnTimer(int64_t nowMs)
{
    EventFlush flush(*this);
    std::lock_guard<std::recursive_mutex> guard(m_mutex);
    BatchScope batch(*this);
    if (IsFinished() || m_state == p2p_session_idle)
        return;

    if (m_state == p2p_session_gathering)
    {
        m_prober.OnTimer(nowMs);
        if (m_prober.IsDone())
            SendOurSignal(nowMs);
    }
    if (m_state == p2p_session_gathering || m_state == p2p_session_signaling)
    {
        if (nowMs >= m_signalDeadlineMs)
            FailConnect(0, nowMs);
        return;
    }
    if (m_state == p2p_session_checking)
    {
        m_checker.OnTimer(nowMs);
        if (m_checker.State() == p2p_check_connected)
            MarkReady(nowMs);
        else if (m_checker.State() == p2p_check_failed)
            FallbackToRelay(nowMs);
        return;
    }
    if (m_state == p2p_session_relaying)
    {
        if (nowMs >= m_relayDeadlineMs)
        {
            FailConnect(0, nowMs);
            return;
        }
        if (m_relay != NULL && m_info.IsControlling() && nowMs >= m_nextNominateMs)
        {
            SendRelayPacket(p2p_packet_nominate, nowMs);
            m_nextNominateMs = nowMs + kP2PRelayNominateIntervalMs;
        }
        return;
    }
    //就绪之后：断开判定、通道的定时工作与保活
    if (nowMs - m_lastRecvMs >= m_info.m_linkTimeoutMs)
    {
        FinishClosed(p2p_close_link_timeout, true);
        return;
    }
    std::vector<P2PChannelEvent> events;
    m_mux.OnTimer(nowMs, events);
    QueueChannelEvents(events);
    if (nowMs - m_lastSendMs >= m_info.m_keepaliveMs)
        SendLinkPacket(p2p_packet_keepalive, std::string(), nowMs);
}

int64_t P2PLinkSession::NextTimerMs() const
{
    std::lock_guard<std::recursive_mutex> guard(m_mutex);
    switch (m_state)
    {
    //正在取公网映射地址：探测的重发时刻与信令时限，取早的
    case p2p_session_gathering:
    {
        const int64_t probe = m_prober.NextTimerMs();
        return (probe < m_signalDeadlineMs) ? probe : m_signalDeadlineMs;
    }

    //等对方的 2303：只有信令时限
    case p2p_session_signaling:
        return m_signalDeadlineMs;

    //正在打洞：由检查状态机决定（其中已含建链超时）
    case p2p_session_checking:
        return m_checker.NextTimerMs();

    //正在走中转：中转时限；发起方连上中转后还有「选定」的重发时刻，取早的
    case p2p_session_relaying:
        if (m_relay != NULL && m_info.IsControlling() && m_nextNominateMs < m_relayDeadlineMs)
            return m_nextNominateMs;
        return m_relayDeadlineMs;

    //就绪：断开判定、保活与通道的定时工作，取最早的
    case p2p_session_connected:
    {
        int64_t next = m_lastRecvMs + m_info.m_linkTimeoutMs;
        const int64_t keepalive = m_lastSendMs + m_info.m_keepaliveMs;
        if (keepalive < next)
            next = keepalive;
        const int64_t channel = m_mux.NextTimerMs();
        if (channel < next)
            next = channel;
        return next;
    }

    //尚未开始或已结束：没有定时工作
    default:
        return INT64_MAX;
    }
}

void P2PLinkSession::OnServerClose(int reason, int errCode, int64_t nowMs)
{
    (void)nowMs;
    EventFlush flush(*this);
    std::lock_guard<std::recursive_mutex> guard(m_mutex);
    if (IsFinished())
        return;
    if (m_state == p2p_session_connected)
    {
        m_state = p2p_session_closed;
        QueueEvent(session_event_closed, reason);
        return;
    }
    m_state = p2p_session_failed;
    QueueEvent(session_event_failed, errCode);
}

void P2PLinkSession::Close(int reason, int64_t nowMs)
{
    std::unique_lock<std::recursive_mutex> lock(m_mutex);
    m_closedByBusiness = true;
    //IO 线程上正在分发事件、而本调用不在那个线程上：等这一轮分发结束，Close 返回之后不再有任何回调在进行
    while (m_dispatchDepth > 0 && m_dispatchThread != std::this_thread::get_id())
        m_dispatchCv.wait(lock);
    if (IsFinished())
        return;
    //就绪时先直接告诉对方（比经 chatserver 转 2305 快），再发 2305 结束服务端的会话
    if (m_state == p2p_session_connected)
        SendLinkPacket(p2p_packet_close, std::string(1, (char)reason), nowMs);
    m_state = p2p_session_closed;
    m_observer.SendSignal(cmd_p2p_close, P2PSignalCodec::BuildClose(m_info.m_sessionId, reason, 0));
}

int P2PLinkSession::OpenChannel(uint16_t channelId, int kind, IP2PChannelSink* sink)
{
    std::lock_guard<std::recursive_mutex> guard(m_mutex);
    if (IsFinished())
        return p2p_send_closed;
    const int result = m_mux.Open(channelId, kind, sink);
    //对方先发来的数据已经暂存着：唤醒 IO 线程，由它在下一次定时里补交（事件只在 IO 线程上分发）
    if (result == p2p_send_ok && m_mux.HasEarlyReplay())
        m_observer.RequestWake();
    return result;
}

int P2PLinkSession::SendData(uint16_t channelId, const uint8_t* data, uint32_t len, int64_t nowMs)
{
    std::lock_guard<std::recursive_mutex> guard(m_mutex);
    BatchScope batch(*this);
    if (m_state != p2p_session_connected)
        return p2p_send_closed;
    const int result = m_mux.Send(channelId, data, len, nowMs);
    //UDP 路径的可靠通道：数据进了 KCP 的发送队列，要由 IO 线程在下一个刷新间隔发出
    if (result == p2p_send_ok && len > 0 && m_path != p2p_link_path_relay && m_mux.IsReliable(channelId))
        m_observer.RequestWake();
    return result;
}

void P2PLinkSession::OnTransportDrained(int64_t nowMs)
{
    (void)nowMs;
    EventFlush flush(*this);
    std::lock_guard<std::recursive_mutex> guard(m_mutex);
    if (m_state != p2p_session_connected)
        return;
    std::vector<P2PChannelEvent> events;
    m_mux.CheckWritable(events);
    QueueChannelEvents(events);
}

void P2PLinkSession::GetStats(P2PLinkStats* out) const
{
    std::lock_guard<std::recursive_mutex> guard(m_mutex);
    if (out == NULL || out->m_cbSize < sizeof(P2PLinkStats))
        return;
    out->m_path = m_path;
    out->m_rttMs = m_mux.SmoothedRttMs();
    const uint64_t sent = m_mux.SentSegments();
    out->m_lossPermille = (sent == 0) ? 0 : (uint32_t)(m_mux.RetransmittedSegments() * kPermille / sent);
    out->m_queuedBytes = m_mux.QueuedBytes();
    out->m_bytesSent = m_mux.BytesSent();
    out->m_bytesReceived = m_mux.BytesReceived();
}

size_t P2PLinkSession::MaxDatagramMessageBytes() const
{
    std::lock_guard<std::recursive_mutex> guard(m_mutex);
    return m_mux.MaxDatagramMessageBytes();
}

void P2PLinkSession::DispatchEvents()
{
    std::vector<SessionEvent> events;
    {
        std::lock_guard<std::recursive_mutex> guard(m_mutex);
        events.swap(m_events);
        if (events.empty() || m_closedByBusiness)
            return;
        ++m_dispatchDepth;
        m_dispatchThread = std::this_thread::get_id();
    }
    for (size_t i = 0; i < events.size(); ++i)
    {
        {
            //业务方在本轮分发期间（在回调里）Close 了：剩下的事件不再分发
            std::lock_guard<std::recursive_mutex> guard(m_mutex);
            if (m_closedByBusiness)
                break;
        }
        const SessionEvent& ev = events[i];
        switch (ev.m_type)
        {
        //链路就绪
        case session_event_ready:
            m_observer.OnSessionReady(this);
            break;

        //建链失败
        case session_event_failed:
            m_observer.OnSessionFailed(this, ev.m_value);
            break;

        //就绪后断开
        case session_event_closed:
            m_observer.OnSessionClosed(this, ev.m_value);
            break;

        //通道事件：收到数据或可以继续发送
        case session_event_channel:
            if (ev.m_channel.m_sink == NULL)
                break;
            if (ev.m_channel.m_type == p2p_channel_event_data)
                ev.m_channel.m_sink->OnData(ev.m_channel.m_channelId,
                                            reinterpret_cast<const uint8_t*>(ev.m_channel.m_data.data()),
                                            (uint32_t)ev.m_channel.m_data.size());
            else if (ev.m_channel.m_type == p2p_channel_event_writable)
                ev.m_channel.m_sink->OnWritable(ev.m_channel.m_channelId);
            break;

        //不会出现的种类
        default:
            break;
        }
    }
    {
        std::lock_guard<std::recursive_mutex> guard(m_mutex);
        --m_dispatchDepth;
    }
    m_dispatchCv.notify_all();
}

void P2PLinkSession::QueueEvent(int type, int value)
{
    SessionEvent ev;
    ev.m_type = type;
    ev.m_value = value;
    m_events.push_back(ev);
}

void P2PLinkSession::QueueChannelEvents(std::vector<P2PChannelEvent>& events)
{
    for (size_t i = 0; i < events.size(); ++i)
    {
        SessionEvent ev;
        ev.m_type = session_event_channel;
        ev.m_channel.m_type = events[i].m_type;
        ev.m_channel.m_channelId = events[i].m_channelId;
        ev.m_channel.m_sink = events[i].m_sink;
        ev.m_channel.m_data.swap(events[i].m_data);
        m_events.push_back(ev);
    }
    events.clear();
}

bool P2PLinkSession::SendDataPacket(const std::string& plain, int64_t nowMs)
{
    SendLinkPacket(p2p_packet_data, plain, nowMs);
    return true;
}

void P2PLinkSession::FlushUdpBatch()
{
    if (m_udpBatch.empty())
        return;
    const size_t segment = m_udp.BatchSegmentBytes();
    const uint32_t ip = m_checker.SelectedIp();
    const uint16_t port = m_checker.SelectedPort();
    std::string group;
    size_t count = 0;
    for (size_t i = 0; i < m_udpBatch.size(); ++i)
    {
        const std::string& p = m_udpBatch[i];
        //满载报文：并进当前组，组满即发
        if (segment != 0 && p.size() == segment)
        {
            group += p;
            if (++count == kP2PMaxBatchSegments)
            {
                m_udp.SendBatch(ip, port, reinterpret_cast<const uint8_t*>(group.data()), group.size());
                group.clear();
                count = 0;
            }
            continue;
        }
        //短报文：作为当前组的最后一段一起发出；当前组为空时单独发
        group += p;
        m_udp.SendBatch(ip, port, reinterpret_cast<const uint8_t*>(group.data()), group.size());
        group.clear();
        count = 0;
    }
    if (!group.empty())
        m_udp.SendBatch(ip, port, reinterpret_cast<const uint8_t*>(group.data()), group.size());
    m_udpBatch.clear();
}

size_t P2PLinkSession::QueuedBytes() const
{
    if (m_path == p2p_link_path_relay && m_relay != NULL)
        return m_relay->QueuedBytes();
    return 0;
}

int P2PLinkSession::State() const
{
    std::lock_guard<std::recursive_mutex> guard(m_mutex);
    return m_state;
}

bool P2PLinkSession::IsFinished() const
{
    std::lock_guard<std::recursive_mutex> guard(m_mutex);
    return m_state == p2p_session_failed || m_state == p2p_session_closed;
}

int P2PLinkSession::Path() const
{
    std::lock_guard<std::recursive_mutex> guard(m_mutex);
    return m_path;
}

int P2PLinkSession::NatMapping() const
{
    std::lock_guard<std::recursive_mutex> guard(m_mutex);
    return m_prober.NatMapping();
}

void P2PLinkSession::SendOurSignal(int64_t nowMs)
{
    //公网映射地址放在前面：对方最多读 kP2PMaxCandidates 个，本机地址多时宁可少发几个本机地址
    std::vector<P2PCandidate> candidates = m_prober.SrflxCandidates(kP2PLocalPrefPhysical);
    candidates.insert(candidates.end(), m_localHosts.begin(), m_localHosts.end());
    if (candidates.size() > kP2PMaxCandidates)
        candidates.resize(kP2PMaxCandidates);
    m_observer.SendSignal(cmd_p2p_signal,
                          P2PSignalCodec::BuildSignal(m_info.m_sessionId, m_keys.PublicKeyBase64(), candidates));
    m_state = p2p_session_signaling;
    if (m_peerSignalReceived)
        StartChecking(nowMs);
}

void P2PLinkSession::StartChecking(int64_t nowMs)
{
    if (m_forceRelay && CanRelay())
    {
        FallbackToRelay(nowMs);
        return;
    }
    m_checker.Start(m_localHosts, m_peerCandidates, nowMs);
    m_state = p2p_session_checking;
}

void P2PLinkSession::FallbackToRelay(int64_t nowMs)
{
    if (!CanRelay())
    {
        FailConnect(0, nowMs);
        return;
    }
    //先改状态再回调：观察者只登记请求，中转的结果由 IO 线程稍后经 OnRelayConnected / OnRelayFailed 交回
    m_state = p2p_session_relaying;
    m_relayDeadlineMs = nowMs + kP2PRelayBudgetMs;
    m_observer.StartRelay(this);
}

void P2PLinkSession::MarkRelayReady(int64_t nowMs)
{
    m_state = p2p_session_connected;
    m_path = p2p_link_path_relay;
    m_lastSendMs = nowMs;
    m_lastRecvMs = nowMs;
    const std::string pair = std::string(kRelayPairPrefix) + m_info.m_relay.m_host + ":"
                           + std::to_string(m_info.m_relay.m_port);
    m_observer.SendSignal(cmd_p2p_result, P2PSignalCodec::BuildResult(m_info.m_sessionId, true, m_path, nowMs - m_beginMs,
                                                                      pair, m_prober.NatMapping()));
    m_mux.SetPath(m_path);
    QueueEvent(session_event_ready, 0);
}

void P2PLinkSession::SendRelayPacket(uint8_t type, int64_t nowMs)
{
    if (m_relay == NULL)
        return;
    std::string wire;
    if (!m_cipher.Seal(type, NULL, 0, wire))
        return;
    m_relay->SendTo(0, 0, reinterpret_cast<const uint8_t*>(wire.data()), wire.size());
    m_lastSendMs = nowMs;
}

void P2PLinkSession::HandleConnectedPacket(uint8_t type, const std::string& plain, int64_t nowMs)
{
    switch (type)
    {
    //业务数据：交给通道复用层
    case p2p_packet_data:
    {
        std::vector<P2PChannelEvent> events;
        m_mux.OnPacket(plain, nowMs, events);
        QueueChannelEvents(events);
        break;
    }

    //保活：收到即说明链路还在，调用方已刷新最近收到的时刻
    case p2p_packet_keepalive:
        break;

    //对方关闭了链路：明文第一个字节是关闭原因；对方会自己发 2305，这里不再发
    case p2p_packet_close:
        FinishClosed(plain.empty() ? p2p_close_business_end : (int)(uint8_t)plain[0], false);
        break;

    //其它类型（就绪之后不该再出现）：忽略
    default:
        break;
    }
}

void P2PLinkSession::MarkReady(int64_t nowMs)
{
    m_state = p2p_session_connected;
    const int origin = m_checker.SelectedOrigin();
    m_path = (origin == p2p_check_origin_same_subnet || origin == p2p_check_origin_host) ? p2p_link_path_lan
                                                                                       : p2p_link_path_punched;
    m_lastSendMs = nowMs;
    m_lastRecvMs = nowMs;
    //地址对文本只用于日志：本方地址取第一个本机候选（socket 绑定的是 0.0.0.0，实际出口由系统决定）
    const std::string local = m_localHosts.empty() ? std::string(kUnknownLocal) : IpToString(m_localHosts[0].m_ip);
    const uint16_t localPort = m_localHosts.empty() ? 0 : m_localHosts[0].m_port;
    const std::string pair = local + ":" + std::to_string(localPort) + " -> " + IpToString(m_checker.SelectedIp()) + ":"
                           + std::to_string(m_checker.SelectedPort());
    m_observer.SendSignal(cmd_p2p_result, P2PSignalCodec::BuildResult(m_info.m_sessionId, true, m_path, nowMs - m_beginMs,
                                                                      pair, m_prober.NatMapping()));
    m_mux.SetPath(m_path);
    QueueEvent(session_event_ready, 0);
}

void P2PLinkSession::FailConnect(int code, int64_t nowMs)
{
    if (IsFinished())
        return;
    m_state = p2p_session_failed;
    m_observer.SendSignal(cmd_p2p_result, P2PSignalCodec::BuildResult(m_info.m_sessionId, false, 0, nowMs - m_beginMs,
                                                                      std::string(), m_prober.NatMapping()));
    m_observer.SendSignal(cmd_p2p_close, P2PSignalCodec::BuildClose(m_info.m_sessionId, p2p_close_connect_fail, code));
    QueueEvent(session_event_failed, code);
}

void P2PLinkSession::FinishClosed(int reason, bool sendClose)
{
    m_state = p2p_session_closed;
    if (sendClose)
        m_observer.SendSignal(cmd_p2p_close, P2PSignalCodec::BuildClose(m_info.m_sessionId, reason, 0));
    QueueEvent(session_event_closed, reason);
}

void P2PLinkSession::SendLinkPacket(uint8_t type, const std::string& plain, int64_t nowMs)
{
    std::string wire;
    const size_t maxPlain = (m_path == p2p_link_path_relay) ? kP2PRelayMaxPlainBytes : kP2PMaxPlainBytes;
    if (!m_cipher.Seal(type, reinterpret_cast<const uint8_t*>(plain.data()), plain.size(), wire, maxPlain))
        return;
    //经中转就绪：走中转连接；UDP 路径在攒批作用域内且支持成批发送时先攒起来；否则直接发往打洞选定的地址
    if (m_path == p2p_link_path_relay && m_relay != NULL)
        m_relay->SendTo(0, 0, reinterpret_cast<const uint8_t*>(wire.data()), wire.size());
    else if (m_batchDepth > 0 && m_udp.BatchSegmentBytes() > 0)
        m_udpBatch.push_back(wire);
    else
        m_udp.SendTo(m_checker.SelectedIp(), m_checker.SelectedPort(), reinterpret_cast<const uint8_t*>(wire.data()),
                     wire.size());
    m_lastSendMs = nowMs;
}

} // namespace p2p
