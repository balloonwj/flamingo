/**
 *  地址探测的客户端流程（docs/P2P通信方案.md 6.1 节，第 3 期 3b）：向 2301 应答里的地址探测服务器各发 Binding 请求，
 *  收集公网映射地址（srflx 候选），并比较两个服务器看到的端口判断 NAT 映射是否与目的地址无关。
 *
 *  - 每个服务器一个事务号（随机 12 字节）；没有应答时每隔 kP2PProbeRetransmitMs 重发，最多 kP2PProbeMaxAttempts 次；
 *    最后一次发出后再等 kP2PProbeRetransmitMs 仍无应答即放弃该服务器；全部服务器都有结果（应答或放弃）即完成。
 *  - 两个服务器看到的映射端口相同：映射与目的地址无关（锥形 NAT 或没有 NAT）；不同：与目的地址有关（对称型 NAT）。
 *    这个判断只用于调整超时与记日志，不据此放弃打洞（6.1 节）。
 *  - 应答按事务号对应，迟到的、重复的、事务号不认识的应答都忽略。
 *
 *  不碰 socket 与真实时钟：发送经 IP2PDatagramSender，时间由调用方传入，便于在单测里确定性地驱动；
 *  在 IO 线程上使用（第 3d 步）。
 *
 *  典型用法：
 *      P2PAddressProber prober(sender);
 *      prober.Start(servers, now);
 *      ...收到 STUN 报文：prober.OnStunDatagram(data, len, now);
 *      ...定时：if (now >= prober.NextTimerMs()) prober.OnTimer(now);
 *      if (prober.IsDone()) { candidates = prober.SrflxCandidates(port...); mapping = prober.NatMapping(); }
 *
 *  balloonwj@qq.com   2026-10-04
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <vector>

#include "P2PCandidate.h"
#include "P2PStunClient.h"

namespace p2p {

//NAT 映射的判断（P2PAddressProber::NatMapping 的返回值，2304 上报 nat.samemapping 时据此填写）
enum p2p_nat_mapping
{
    p2p_nat_mapping_unknown     = 0,   //不足两个服务器有应答，无法判断
    p2p_nat_mapping_independent = 1,   //两个服务器看到的映射端口相同：映射与目的地址无关
    p2p_nat_mapping_dependent   = 2,   //映射端口不同：与目的地址有关（对称型 NAT）
};

//没有应答时的重发间隔（毫秒）与每个服务器最多发送的次数：最长约 1 秒完成，留出打洞超时（默认 3 秒）的大部分
const int64_t kP2PProbeRetransmitMs = 250;
const int kP2PProbeMaxAttempts = 3;

//一个地址探测服务器
struct P2PProbeServer
{
    uint32_t m_ip;     //IPv4 地址（主机字节序）
    uint16_t m_port;   //UDP 端口（主机字节序）

    P2PProbeServer()
        : m_ip(0)
        , m_port(0)
    {
    }
};

//一次成批发送最多的报文个数（第 4e 步起）：UDP_SEND_MSG_SIZE 一次能交出的总长度受系统限制（不超过 64 KiB），
//32 个满载报文约 38 KB
const size_t kP2PMaxBatchSegments = 32;

/**
 *  发送一个 UDP 报文。IO 层用链路的 UDP socket 实现，单测里用记录下来的假实现。
 *  析构函数受保护：P2PAddressProber 只借用，不负责销毁。
 */
class IP2PDatagramSender
{
public:
    /**
     *  @param ip   目的地址（主机字节序）
     *  @param port 目的端口（主机字节序）
     *  @param data 报文内容，只在本次调用期间有效
     *  @param len  报文长度（字节）
     *  @return 已交给系统发送返回 true
     */
    virtual bool SendTo(uint32_t ip, uint16_t port, const uint8_t* data, size_t len) = 0;

    /**
     *  已收下、还没写进系统的字节数（第 4b 步起）。UDP socket 直接交给系统，为 0；中转连接返回发送缓冲里的积压。
     *  @return 积压的字节数
     */
    virtual size_t QueuedBytes() const
    {
        return 0;
    }

    /**
     *  成批发送时每段的字节数（第 4e 步起）。返回 0 表示不支持成批发送（默认），调用方逐个调用 SendTo。
     *  @return 每段字节数；不支持时为 0
     */
    virtual size_t BatchSegmentBytes() const
    {
        return 0;
    }

    /**
     *  成批发送若干个发往同一地址的报文（第 4e 步起）：data 是首尾相接的若干段，除最后一段外每段恰好
     *  BatchSegmentBytes() 字节，最后一段可以更短。默认实现按段逐个调用 SendTo。
     *  @param ip   目的地址（主机字节序）
     *  @param port 目的端口（主机字节序）
     *  @param data 首尾相接的报文，只在本次调用期间有效
     *  @param len  总长度（字节）
     *  @return 全部交给系统返回 true
     */
    virtual bool SendBatch(uint32_t ip, uint16_t port, const uint8_t* data, size_t len)
    {
        const size_t seg = BatchSegmentBytes();
        if (seg == 0)
            return SendTo(ip, port, data, len);
        bool ok = true;
        for (size_t offset = 0; offset < len; offset += seg)
        {
            const size_t n = (len - offset < seg) ? len - offset : seg;
            ok = SendTo(ip, port, data + offset, n) && ok;
        }
        return ok;
    }

protected:
    ~IP2PDatagramSender()
    {
    }
};

/**
 *  地址探测的客户端流程，一条链路一个。
 */
class P2PAddressProber
{
public:
    /**
     *  @param sender 发送出口（借用，须比本对象活得久）
     */
    explicit P2PAddressProber(IP2PDatagramSender& sender);

    /**
     *  开始探测：给每个服务器生成事务号并发出第一个请求。重复调用时先丢弃上一次的状态。
     *  @param servers 地址探测服务器（2301 应答的 probe）；为空时立即完成
     *  @param nowMs   当前时间（毫秒）
     */
    void Start(const std::vector<P2PProbeServer>& servers, int64_t nowMs);

    /**
     *  收到一个 STUN 报文（IO 层按首字节分来的）。事务号对得上且该服务器尚未应答时记下映射地址。
     *  @param data  报文内容，只在本次调用期间有效
     *  @param len   报文长度（字节）
     *  @param nowMs 当前时间（毫秒）
     *  @return 报文被认作某个服务器的应答返回 true
     */
    bool OnStunDatagram(const uint8_t* data, size_t len, int64_t nowMs);

    /**
     *  定时驱动：对到了重发时刻的服务器重发请求，对超过次数的服务器放弃。
     *  @param nowMs 当前时间（毫秒）
     */
    void OnTimer(int64_t nowMs);

    //是否已完成（每个服务器都已应答或已放弃）
    bool IsDone() const;

    //下一次需要调用 OnTimer 的时刻（毫秒）；已完成时为 INT64_MAX
    int64_t NextTimerMs() const;

    /**
     *  收集到的公网映射地址（去重），作为 srflx 候选。
     *  @param localPref 本地偏好（取本链路 socket 所用网卡的偏好，通常为 kP2PLocalPrefPhysical）
     *  @return srflx 候选，按服务器顺序排列
     */
    std::vector<P2PCandidate> SrflxCandidates(uint16_t localPref) const;

    //NAT 映射的判断，取值见 p2p_nat_mapping
    int NatMapping() const;

    //有应答的服务器个数
    size_t AnsweredCount() const;

private:
    //一个服务器的探测状态
    struct ServerState
    {
        P2PProbeServer m_server;                      //服务器地址
        uint8_t        m_txid[kP2PStunTxidBytes];     //事务号
        int            m_attempts;                    //已发送次数
        int64_t        m_lastSendMs;                  //最近一次发送的时刻（毫秒）
        bool           m_answered;                    //是否已收到应答
        bool           m_givenUp;                     //是否已放弃（发满次数后仍无应答）
        uint32_t       m_mappedIp;                    //应答里的映射地址（主机字节序）
        uint16_t       m_mappedPort;                  //应答里的映射端口（主机字节序）
    };

    //给一个服务器发一次请求
    void SendRequest(ServerState& state, int64_t nowMs);

private:
    IP2PDatagramSender&      m_sender;   //发送出口（借用）
    std::vector<ServerState> m_states;   //每个服务器的探测状态，顺序与 Start 传入的一致
};

} // namespace p2p
