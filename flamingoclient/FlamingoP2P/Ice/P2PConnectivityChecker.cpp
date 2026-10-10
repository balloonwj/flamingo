/**
 *  P2P 连通性检查状态机的实现，见 P2PConnectivityChecker.h。
 *  balloonwj@qq.com   2026-10-04
 */
#include "P2PConnectivityChecker.h"

#include <stdint.h>
#include <string.h>
#include <algorithm>
#include <sodium.h>

#include "../Core/P2PPacket.h"

namespace p2p {

namespace {

//IPv4 地址的位数，判断同网段时按前缀长度移位
const int kIpv4Bits = 32;

//探测应答明文里看到的来源地址与端口的长度（字节）
const size_t kAckIpBytes = 4;
const size_t kAckPortBytes = 2;

//一个字节的位数
const int kBitsPerByte = 8;

//按网络字节序追加 bytes 个字节的无符号整数
void AppendBigEndian(std::string& s, uint32_t value, size_t bytes)
{
    for (size_t i = 0; i < bytes; ++i)
        s.push_back((char)(value >> (kBitsPerByte * (bytes - 1 - i))));
}

//两个地址在给定前缀长度下是否同网段；前缀长度为 0 或超过 32 时视为未知，返回 false
bool SameSubnet(uint32_t a, uint32_t b, uint8_t prefixLen)
{
    if (prefixLen == 0 || prefixLen > kIpv4Bits)
        return false;
    if (prefixLen == kIpv4Bits)
        return a == b;
    const int shift = kIpv4Bits - prefixLen;
    return (a >> shift) == (b >> shift);
}

} // namespace

P2PConnectivityChecker::P2PConnectivityChecker(IP2PDatagramSender& sender, P2PLinkCipher& cipher, bool controlling,
                                               int64_t connectTimeoutMs)
    : m_sender(sender)
    , m_cipher(cipher)
    , m_controlling(controlling)
    , m_connectTimeoutMs(connectTimeoutMs)
    , m_state(p2p_check_idle)
    , m_startMs(0)
    , m_deadlineMs(0)
    , m_firstSuccessMs(-1)
    , m_nominating(false)
    , m_nominatedIp(0)
    , m_nominatedPort(0)
    , m_nominatedOrigin(0)
    , m_nextNominateMs(0)
    , m_selectedIp(0)
    , m_selectedPort(0)
    , m_selectedOrigin(0)
    , m_probesSent(0)
{
}

void P2PConnectivityChecker::Start(const std::vector<P2PCandidate>& localHosts, const std::vector<P2PCandidate>& remotes,
                                   int64_t nowMs)
{
    m_state = p2p_check_running;
    m_localHosts = localHosts;
    m_checks.clear();
    m_startMs = nowMs;
    //应答方多等一段，避免发起方的「选定」还在路上时先放弃
    m_deadlineMs = nowMs + m_connectTimeoutMs + (m_controlling ? 0 : kP2PControlledExtraMs);
    m_firstSuccessMs = -1;
    m_nominating = false;
    m_selectedIp = 0;
    m_selectedPort = 0;
    m_selectedOrigin = 0;

    for (size_t i = 0; i < remotes.size(); ++i)
    {
        const P2PCandidate& r = remotes[i];
        if (r.m_ip == 0 || r.m_port == 0)
            continue;
        int origin = p2p_check_origin_srflx;
        if (r.m_type == p2p_candidate_host)
        {
            origin = p2p_check_origin_host;
            for (size_t k = 0; k < m_localHosts.size(); ++k)
            {
                if (SameSubnet(m_localHosts[k].m_ip, r.m_ip, m_localHosts[k].m_prefixLen))
                    origin = p2p_check_origin_same_subnet;
            }
        }
        //同一个地址只留一项（如公网主机的本机地址与公网映射地址相同），取更优先的来历
        Check* existing = FindCheck(r.m_ip, r.m_port);
        if (existing != NULL)
        {
            if (origin < existing->m_origin)
            {
                existing->m_origin = origin;
                existing->m_remotePriority = r.m_priority;
            }
            continue;
        }
        Check c;
        c.m_ip = r.m_ip;
        c.m_port = r.m_port;
        c.m_origin = origin;
        c.m_remotePriority = r.m_priority;
        randombytes_buf(c.m_txid, sizeof(c.m_txid));
        c.m_nextProbeMs = nowMs;
        c.m_succeeded = false;
        m_checks.push_back(c);
    }
    std::stable_sort(m_checks.begin(), m_checks.end(), CheckBefore);
    for (size_t i = 0; i < m_checks.size(); ++i)
        SendProbe(m_checks[i], nowMs);
}

void P2PConnectivityChecker::OnPacket(uint32_t fromIp, uint16_t fromPort, uint8_t type, const std::string& plain,
                                      int64_t nowMs)
{
    if (m_state == p2p_check_idle || m_state == p2p_check_failed)
        return;

    switch (type)
    {
    //对方的探测：回应答（原样带回事务号，写上看到的来源地址）；仍在检查时立即向这个地址补发一次本方的探测，
    //来源不在检查表里时作为对端反射地址加入
    case p2p_packet_probe:
    {
        if (plain.size() < kP2PCheckTxidBytes)
            return;
        std::string ack = plain.substr(0, kP2PCheckTxidBytes);
        AppendBigEndian(ack, fromIp, kAckIpBytes);
        AppendBigEndian(ack, fromPort, kAckPortBytes);
        SendControl(p2p_packet_probe_ack, ack, fromIp, fromPort);
        if (m_state != p2p_check_running)
            return;
        Check* c = FindCheck(fromIp, fromPort);
        if (c == NULL)
        {
            Check added;
            added.m_ip = fromIp;
            added.m_port = fromPort;
            added.m_origin = p2p_check_origin_prflx;
            added.m_remotePriority = 0;
            randombytes_buf(added.m_txid, sizeof(added.m_txid));
            added.m_nextProbeMs = nowMs;
            added.m_succeeded = false;
            m_checks.push_back(added);
            c = &m_checks.back();
        }
        if (!c->m_succeeded && !(m_controlling && m_nominating))
            SendProbe(*c, nowMs);
        break;
    }

    //探测应答：事务号对上的那一项双向连通；发起方在最优先的一项连通时立即选定
    case p2p_packet_probe_ack:
    {
        if (m_state != p2p_check_running || plain.size() < kP2PCheckTxidBytes)
            return;
        for (size_t i = 0; i < m_checks.size(); ++i)
        {
            if (memcmp(m_checks[i].m_txid, plain.data(), kP2PCheckTxidBytes) != 0)
                continue;
            if (!m_checks[i].m_succeeded)
            {
                m_checks[i].m_succeeded = true;
                if (m_firstSuccessMs < 0)
                    m_firstSuccessMs = nowMs;
            }
            break;
        }
        if (m_controlling && !m_nominating && !m_checks.empty() && m_checks[0].m_succeeded)
            Nominate(nowMs);
        break;
    }

    //发起方选定了一个地址：应答方锁定它并确认；已锁定后同一地址的选定照样确认，其它地址的选定不理
    case p2p_packet_nominate:
    {
        if (m_controlling)
            return;
        if (m_state == p2p_check_running)
        {
            const Check* c = FindCheck(fromIp, fromPort);
            MarkConnected(fromIp, fromPort, (c != NULL) ? c->m_origin : p2p_check_origin_prflx);
            SendControl(p2p_packet_nominate_ack, std::string(), fromIp, fromPort);
        }
        else if (fromIp == m_selectedIp && fromPort == m_selectedPort)
        {
            SendControl(p2p_packet_nominate_ack, std::string(), fromIp, fromPort);
        }
        break;
    }

    //应答方确认了选定：只认来自所选地址的确认
    case p2p_packet_nominate_ack:
    {
        if (m_controlling && m_state == p2p_check_running && m_nominating && fromIp == m_nominatedIp
            && fromPort == m_nominatedPort)
            MarkConnected(m_nominatedIp, m_nominatedPort, m_nominatedOrigin);
        break;
    }

    //数据、保活、关闭等报文由链路层处理，与连通性检查无关
    default:
        break;
    }
}

void P2PConnectivityChecker::OnTimer(int64_t nowMs)
{
    if (m_state != p2p_check_running)
        return;
    if (nowMs >= m_deadlineMs)
    {
        m_state = p2p_check_failed;
        return;
    }
    //发起方一旦选定就不再探测其它项，只重发选定
    if (!(m_controlling && m_nominating))
    {
        for (size_t i = 0; i < m_checks.size(); ++i)
        {
            if (!m_checks[i].m_succeeded && nowMs >= m_checks[i].m_nextProbeMs)
                SendProbe(m_checks[i], nowMs);
        }
    }
    if (!m_controlling)
        return;
    if (!m_nominating && m_firstSuccessMs >= 0 && nowMs >= m_firstSuccessMs + kP2PNominateGraceMs)
        Nominate(nowMs);
    else if (m_nominating && nowMs >= m_nextNominateMs)
    {
        SendControl(p2p_packet_nominate, std::string(), m_nominatedIp, m_nominatedPort);
        m_nextNominateMs = nowMs + kP2PCheckProbeIntervalMs;
    }
}

int64_t P2PConnectivityChecker::NextTimerMs() const
{
    if (m_state != p2p_check_running)
        return INT64_MAX;
    int64_t next = m_deadlineMs;
    if (!(m_controlling && m_nominating))
    {
        for (size_t i = 0; i < m_checks.size(); ++i)
        {
            if (!m_checks[i].m_succeeded && m_checks[i].m_nextProbeMs < next)
                next = m_checks[i].m_nextProbeMs;
        }
    }
    if (m_controlling)
    {
        if (!m_nominating && m_firstSuccessMs >= 0 && m_firstSuccessMs + kP2PNominateGraceMs < next)
            next = m_firstSuccessMs + kP2PNominateGraceMs;
        if (m_nominating && m_nextNominateMs < next)
            next = m_nextNominateMs;
    }
    return next;
}

bool P2PConnectivityChecker::CheckBefore(const Check& a, const Check& b)
{
    if (a.m_origin != b.m_origin)
        return a.m_origin < b.m_origin;
    return a.m_remotePriority > b.m_remotePriority;
}

P2PConnectivityChecker::Check* P2PConnectivityChecker::FindCheck(uint32_t ip, uint16_t port)
{
    for (size_t i = 0; i < m_checks.size(); ++i)
    {
        if (m_checks[i].m_ip == ip && m_checks[i].m_port == port)
            return &m_checks[i];
    }
    return NULL;
}

void P2PConnectivityChecker::SendProbe(Check& check, int64_t nowMs)
{
    SendControl(p2p_packet_probe, std::string(reinterpret_cast<const char*>(check.m_txid), sizeof(check.m_txid)),
                check.m_ip, check.m_port);
    ++m_probesSent;
    check.m_nextProbeMs = nowMs + kP2PCheckProbeIntervalMs;
}

void P2PConnectivityChecker::SendControl(uint8_t type, const std::string& plain, uint32_t ip, uint16_t port)
{
    std::string wire;
    if (!m_cipher.Seal(type, reinterpret_cast<const uint8_t*>(plain.data()), plain.size(), wire))
        return;
    m_sender.SendTo(ip, port, reinterpret_cast<const uint8_t*>(wire.data()), wire.size());
}

void P2PConnectivityChecker::Nominate(int64_t nowMs)
{
    //检查表按优先级排列：第一个已连通的项就是最好的
    for (size_t i = 0; i < m_checks.size(); ++i)
    {
        if (!m_checks[i].m_succeeded)
            continue;
        m_nominating = true;
        m_nominatedIp = m_checks[i].m_ip;
        m_nominatedPort = m_checks[i].m_port;
        m_nominatedOrigin = m_checks[i].m_origin;
        SendControl(p2p_packet_nominate, std::string(), m_nominatedIp, m_nominatedPort);
        m_nextNominateMs = nowMs + kP2PCheckProbeIntervalMs;
        return;
    }
}

void P2PConnectivityChecker::MarkConnected(uint32_t ip, uint16_t port, int origin)
{
    m_state = p2p_check_connected;
    m_selectedIp = ip;
    m_selectedPort = port;
    m_selectedOrigin = origin;
}

} // namespace p2p
