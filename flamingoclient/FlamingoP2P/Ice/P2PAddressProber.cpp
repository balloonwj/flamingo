/**
 *  地址探测客户端流程的实现，见 P2PAddressProber.h。
 *  balloonwj@qq.com   2026-10-04
 */
#include "P2PAddressProber.h"

#include <stdint.h>
#include <string.h>
#include <sodium.h>

namespace p2p {

P2PAddressProber::P2PAddressProber(IP2PDatagramSender& sender)
    : m_sender(sender)
{
}

void P2PAddressProber::Start(const std::vector<P2PProbeServer>& servers, int64_t nowMs)
{
    m_states.clear();
    for (size_t i = 0; i < servers.size(); ++i)
    {
        ServerState s;
        s.m_server = servers[i];
        //事务号随机，迟到或伪造的应答因此对不上
        randombytes_buf(s.m_txid, sizeof(s.m_txid));
        s.m_attempts = 0;
        s.m_lastSendMs = nowMs;
        s.m_answered = false;
        s.m_givenUp = false;
        s.m_mappedIp = 0;
        s.m_mappedPort = 0;
        m_states.push_back(s);
    }
    for (size_t i = 0; i < m_states.size(); ++i)
        SendRequest(m_states[i], nowMs);
}

bool P2PAddressProber::OnStunDatagram(const uint8_t* data, size_t len, int64_t nowMs)
{
    (void)nowMs;
    uint32_t ip = 0;
    uint16_t port = 0;
    uint8_t txid[kP2PStunTxidBytes];
    if (!P2PStunClient::ParseResponse(data, len, NULL, ip, port, txid))
        return false;
    for (size_t i = 0; i < m_states.size(); ++i)
    {
        ServerState& s = m_states[i];
        if (memcmp(s.m_txid, txid, sizeof(s.m_txid)) != 0)
            continue;
        //已经应答过、或已经放弃：重复与迟到的应答都忽略
        if (s.m_answered || s.m_givenUp)
            return false;
        s.m_answered = true;
        s.m_mappedIp = ip;
        s.m_mappedPort = port;
        return true;
    }
    return false;
}

void P2PAddressProber::OnTimer(int64_t nowMs)
{
    for (size_t i = 0; i < m_states.size(); ++i)
    {
        ServerState& s = m_states[i];
        if (s.m_answered || s.m_givenUp || nowMs < s.m_lastSendMs + kP2PProbeRetransmitMs)
            continue;
        if (s.m_attempts >= kP2PProbeMaxAttempts)
            s.m_givenUp = true;
        else
            SendRequest(s, nowMs);
    }
}

bool P2PAddressProber::IsDone() const
{
    for (size_t i = 0; i < m_states.size(); ++i)
    {
        if (!m_states[i].m_answered && !m_states[i].m_givenUp)
            return false;
    }
    return true;
}

int64_t P2PAddressProber::NextTimerMs() const
{
    int64_t next = INT64_MAX;
    for (size_t i = 0; i < m_states.size(); ++i)
    {
        const ServerState& s = m_states[i];
        if (s.m_answered || s.m_givenUp)
            continue;
        const int64_t due = s.m_lastSendMs + kP2PProbeRetransmitMs;
        if (due < next)
            next = due;
    }
    return next;
}

std::vector<P2PCandidate> P2PAddressProber::SrflxCandidates(uint16_t localPref) const
{
    std::vector<P2PCandidate> out;
    for (size_t i = 0; i < m_states.size(); ++i)
    {
        const ServerState& s = m_states[i];
        if (!s.m_answered)
            continue;
        bool duplicate = false;
        for (size_t k = 0; k < out.size(); ++k)
        {
            if (out[k].m_ip == s.m_mappedIp && out[k].m_port == s.m_mappedPort)
                duplicate = true;
        }
        if (duplicate)
            continue;
        P2PCandidate c;
        c.m_type = p2p_candidate_srflx;
        c.m_ip = s.m_mappedIp;
        c.m_port = s.m_mappedPort;
        c.m_priority = CandidatePriority(kP2PTypePrefSrflx, localPref);
        out.push_back(c);
    }
    return out;
}

int P2PAddressProber::NatMapping() const
{
    //取前两个有应答的服务器比较映射端口
    const ServerState* first = NULL;
    for (size_t i = 0; i < m_states.size(); ++i)
    {
        const ServerState& s = m_states[i];
        if (!s.m_answered)
            continue;
        if (first == NULL)
        {
            first = &s;
            continue;
        }
        return (s.m_mappedIp == first->m_mappedIp && s.m_mappedPort == first->m_mappedPort)
            ? p2p_nat_mapping_independent : p2p_nat_mapping_dependent;
    }
    return p2p_nat_mapping_unknown;
}

size_t P2PAddressProber::AnsweredCount() const
{
    size_t n = 0;
    for (size_t i = 0; i < m_states.size(); ++i)
    {
        if (m_states[i].m_answered)
            ++n;
    }
    return n;
}

void P2PAddressProber::SendRequest(ServerState& state, int64_t nowMs)
{
    const std::string request = P2PStunClient::BuildRequest(state.m_txid);
    m_sender.SendTo(state.m_server.m_ip, state.m_server.m_port, reinterpret_cast<const uint8_t*>(request.data()),
                    request.size());
    ++state.m_attempts;
    state.m_lastSendMs = nowMs;
}

} // namespace p2p
