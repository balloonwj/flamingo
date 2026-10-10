/**
 *  P2P 单测用模拟网络的实现，见 P2PSimNet.h。
 *  balloonwj@qq.com   2026-10-04
 */
#include "stdafx.h"

#include "P2PSimNet.h"

#include <stdint.h>

namespace p2psim {

namespace {

//线性同余发生器的参数（Numerical Recipes 给出的取值）与取高位时右移的位数
const uint32_t kLcgMultiplier = 1664525u;
const uint32_t kLcgIncrement = 1013904223u;
const int kRandomShift = 8;

//丢包率的刻度：千分比
const uint32_t kPermilleScale = 1000;

//STUN 报文的若干常量（RFC 5389）：头长、事务号偏移与长度、魔数、Binding 请求与成功应答的类型
const size_t kStunHeaderBytes = 20;
const size_t kStunTxidOffset = 8;
const size_t kStunTxidBytes = 12;
const uint32_t kStunMagic = 0x2112A442;
const uint8_t kStunRequestType0 = 0x00;
const uint8_t kStunRequestType1 = 0x01;
const uint16_t kStunSuccessType = 0x0101;

//XOR-MAPPED-ADDRESS 属性：类型、值长度、地址族 IPv4；属性加属性头一共 12 字节
const uint16_t kAttrXorMapped = 0x0020;
const uint16_t kXorMappedValueBytes = 8;
const uint8_t kFamilyIpv4 = 0x01;
const uint16_t kXorMappedAttrBytes = 12;

//一个字节的位数
const int kBitsPerByte = 8;

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

} // namespace

SimNetwork::SimNetwork(uint32_t seed)
    : m_seed(seed)
    , m_now(0)
    , m_seq(0)
    , m_baseMs(1)
    , m_jitterMs(0)
    , m_lossPermille(0)
    , m_sent(0)
    , m_dropped(0)
    , m_delivered(0)
{
}

int SimNetwork::AddNat(SimNatType type, uint32_t publicIp)
{
    Nat n;
    n.m_type = type;
    n.m_publicIp = publicIp;
    n.m_nextPort = kSimFirstExternalPort;
    m_nats.push_back(n);
    return (int)m_nats.size() - 1;
}

int SimNetwork::AddSocket(int natId, uint32_t ip, uint16_t port, ISimReceiver* receiver)
{
    Socket s;
    s.m_natId = natId;
    s.m_ip = ip;
    s.m_port = port;
    s.m_receiver = receiver;
    m_sockets.push_back(s);
    return (int)m_sockets.size() - 1;
}

void SimNetwork::AddStunServer(uint32_t ip, uint16_t port)
{
    m_stunServers.insert(std::make_pair(ip, port));
}

void SimNetwork::SetLinkProfile(int64_t baseMs, int64_t jitterMs, int lossPermille)
{
    m_baseMs = baseMs;
    m_jitterMs = jitterMs;
    m_lossPermille = lossPermille;
}

uint32_t SimNetwork::Random(uint32_t bound)
{
    m_seed = m_seed * kLcgMultiplier + kLcgIncrement;
    return (bound == 0) ? 0 : ((m_seed >> kRandomShift) % bound);
}

void SimNetwork::Send(int socketId, uint32_t dstIp, uint16_t dstPort, const std::string& data)
{
    ++m_sent;
    const Socket& s = m_sockets[socketId];
    Packet p;
    p.m_dstIp = dstIp;
    p.m_dstPort = dstPort;
    p.m_data = data;
    p.m_lanNatId = -1;
    if (s.m_natId < 0)
    {
        //公网主机：按自己的地址发出
        p.m_srcIp = s.m_ip;
        p.m_srcPort = s.m_port;
        Enqueue(p);
        return;
    }

    //目的地址是同一 NAT 后面的某台主机：局域网内直接投递，不经过 NAT
    for (size_t i = 0; i < m_sockets.size(); ++i)
    {
        if (m_sockets[i].m_natId == s.m_natId && m_sockets[i].m_ip == dstIp)
        {
            p.m_srcIp = s.m_ip;
            p.m_srcPort = s.m_port;
            p.m_lanNatId = s.m_natId;
            Enqueue(p);
            return;
        }
    }

    //经 NAT 出去：找（或分配）映射，并记下放行条件
    Nat& nat = m_nats[s.m_natId];
    uint16_t extPort = 0;
    for (std::map<std::pair<int, uint16_t>, Mapping>::iterator it = m_mappings.begin(); it != m_mappings.end(); ++it)
    {
        if (it->first.first != s.m_natId || it->second.m_socketId != socketId)
            continue;
        //对称型：映射还要对应同一个目的地址
        if (nat.m_type == sim_nat_symmetric && (it->second.m_dstIp != dstIp || it->second.m_dstPort != dstPort))
            continue;
        extPort = it->first.second;
        break;
    }
    if (extPort == 0)
    {
        extPort = nat.m_nextPort++;
        Mapping m;
        m.m_socketId = socketId;
        m.m_dstIp = dstIp;
        m.m_dstPort = dstPort;
        m_mappings[std::make_pair(s.m_natId, extPort)] = m;
    }
    Mapping& mapping = m_mappings[std::make_pair(s.m_natId, extPort)];
    mapping.m_allowedIps.insert(dstIp);
    mapping.m_allowedEndpoints.insert(std::make_pair(dstIp, dstPort));
    p.m_srcIp = nat.m_publicIp;
    p.m_srcPort = extPort;
    Enqueue(p);
}

void SimNetwork::Enqueue(const Packet& packet)
{
    if (m_lossPermille > 0 && Random(kPermilleScale) < (uint32_t)m_lossPermille)
    {
        ++m_dropped;
        return;
    }
    const int64_t jitter = (m_jitterMs > 0) ? (int64_t)Random((uint32_t)m_jitterMs + 1) : 0;
    m_queue[std::make_pair(m_now + m_baseMs + jitter, m_seq++)] = packet;
}

void SimNetwork::RunUntil(int64_t untilMs)
{
    while (!m_queue.empty() && m_queue.begin()->first.first <= untilMs)
    {
        std::map<std::pair<int64_t, uint64_t>, Packet>::iterator it = m_queue.begin();
        const Packet packet = it->second;
        if (it->first.first > m_now)
            m_now = it->first.first;
        m_queue.erase(it);
        Deliver(packet);
    }
    if (untilMs > m_now)
        m_now = untilMs;
}

int64_t SimNetwork::NextEventMs() const
{
    return m_queue.empty() ? INT64_MAX : m_queue.begin()->first.first;
}

void SimNetwork::Deliver(const Packet& packet)
{
    //局域网内投递
    if (packet.m_lanNatId >= 0)
    {
        const int s = FindLanSocket(packet.m_lanNatId, packet.m_dstIp, packet.m_dstPort);
        if (s < 0)
        {
            ++m_dropped;
            return;
        }
        ++m_delivered;
        m_sockets[s].m_receiver->OnSimReceive(packet.m_srcIp, packet.m_srcPort, packet.m_data, m_now);
        return;
    }

    //经公网：地址探测服务器、公网主机、NAT 的映射端口，三者之一
    if (m_stunServers.count(std::make_pair(packet.m_dstIp, packet.m_dstPort)) != 0)
    {
        ++m_delivered;
        AnswerStun(packet);
        return;
    }
    const int pub = FindPublicSocket(packet.m_dstIp, packet.m_dstPort);
    if (pub >= 0)
    {
        ++m_delivered;
        m_sockets[pub].m_receiver->OnSimReceive(packet.m_srcIp, packet.m_srcPort, packet.m_data, m_now);
        return;
    }
    const int natId = FindNatByPublicIp(packet.m_dstIp);
    std::map<std::pair<int, uint16_t>, Mapping>::iterator it = m_mappings.find(std::make_pair(natId, packet.m_dstPort));
    if (natId < 0 || it == m_mappings.end())
    {
        ++m_dropped;
        return;
    }
    const Mapping& m = it->second;
    bool allowed = false;
    switch (m_nats[natId].m_type)
    {
    //完全锥形：任何来源都放行
    case sim_nat_full_cone:
        allowed = true;
        break;

    //受限锥形：只放行发过包的来源 IP
    case sim_nat_restricted:
        allowed = (m.m_allowedIps.count(packet.m_srcIp) != 0);
        break;

    //端口受限锥形：只放行发过包的来源 IP 加端口
    case sim_nat_port_restricted:
        allowed = (m.m_allowedEndpoints.count(std::make_pair(packet.m_srcIp, packet.m_srcPort)) != 0);
        break;

    //对称型：映射本来就只对应一个目的地址，同样只放行发过包的来源 IP 加端口
    case sim_nat_symmetric:
        allowed = (m.m_allowedEndpoints.count(std::make_pair(packet.m_srcIp, packet.m_srcPort)) != 0);
        break;

    //不会出现的类型：按不放行处理
    default:
        allowed = false;
        break;
    }
    if (!allowed)
    {
        ++m_dropped;
        return;
    }
    ++m_delivered;
    m_sockets[m.m_socketId].m_receiver->OnSimReceive(packet.m_srcIp, packet.m_srcPort, packet.m_data, m_now);
}

void SimNetwork::AnswerStun(const Packet& request)
{
    const std::string& d = request.m_data;
    if (d.size() < kStunHeaderBytes || (uint8_t)d[0] != kStunRequestType0 || (uint8_t)d[1] != kStunRequestType1)
        return;
    std::string resp;
    Append16(resp, kStunSuccessType);
    Append16(resp, kXorMappedAttrBytes);
    Append32(resp, kStunMagic);
    resp.append(d, kStunTxidOffset, kStunTxidBytes);
    Append16(resp, kAttrXorMapped);
    Append16(resp, kXorMappedValueBytes);
    resp.push_back(0);
    resp.push_back((char)kFamilyIpv4);
    Append16(resp, (uint16_t)(request.m_srcPort ^ (kStunMagic >> (2 * kBitsPerByte))));
    Append32(resp, request.m_srcIp ^ kStunMagic);

    ++m_sent;
    Packet reply;
    reply.m_srcIp = request.m_dstIp;
    reply.m_srcPort = request.m_dstPort;
    reply.m_dstIp = request.m_srcIp;
    reply.m_dstPort = request.m_srcPort;
    reply.m_lanNatId = -1;
    reply.m_data = resp;
    Enqueue(reply);
}

int SimNetwork::FindLanSocket(int natId, uint32_t ip, uint16_t port) const
{
    for (size_t i = 0; i < m_sockets.size(); ++i)
    {
        if (m_sockets[i].m_natId == natId && m_sockets[i].m_ip == ip && m_sockets[i].m_port == port)
            return (int)i;
    }
    return -1;
}

int SimNetwork::FindPublicSocket(uint32_t ip, uint16_t port) const
{
    return FindLanSocket(-1, ip, port);
}

int SimNetwork::FindNatByPublicIp(uint32_t ip) const
{
    for (size_t i = 0; i < m_nats.size(); ++i)
    {
        if (m_nats[i].m_publicIp == ip)
            return (int)i;
    }
    return -1;
}

bool SimNetwork::ExternalAddressOf(int socketId, uint32_t& ip, uint16_t& port) const
{
    const Socket& s = m_sockets[socketId];
    if (s.m_natId < 0)
    {
        ip = s.m_ip;
        port = s.m_port;
        return true;
    }
    //map 按（NAT 编号，外部端口）排序：取这个 socket 端口号最小（即最先建立）的映射
    for (std::map<std::pair<int, uint16_t>, Mapping>::const_iterator it = m_mappings.begin(); it != m_mappings.end(); ++it)
    {
        if (it->first.first == s.m_natId && it->second.m_socketId == socketId)
        {
            ip = m_nats[s.m_natId].m_publicIp;
            port = it->first.second;
            return true;
        }
    }
    return false;
}

} // namespace p2psim
