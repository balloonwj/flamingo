/**
 *  imgserver 地址探测服务的纯逻辑，P2PProbeCore.h
 *
 *  docs/P2P通信方案.md 6.2 节、第 2 期 2e-2。客户端向 imgserver 的两个 UDP 端口发 Binding 请求，服务端回一个带
 *  XOR-MAPPED-ADDRESS 的成功应答，告诉客户端「从服务端看来你的地址是什么」，客户端据此得到自己在 NAT 外的映射地址。
 *  报文兼容 RFC 5389，但只实现这一种消息和这一个属性；请求里的属性一律不解析。
 *
 *  这里只放与网络无关、可以单独测试的两部分：
 *    - P2PStunCodec：判断并解析 Binding 请求，构造成功应答。应答固定 32 字节，为保证应答不大于请求、不会被利用来
 *      放大流量，只接受长度不小于 32 字节的请求（用户 2e 时确认的「严格不放大」）。客户端用一个 8 字节值的 SOFTWARE
 *      属性（类型 0x8022，接收方可忽略）把请求补足 32 字节；只发 20 字节最小请求的现成 STUN 工具得不到应答。
 *    - P2PProbeRateLimiter：按来源 IP 限频，每个 IP 每个一秒窗口内最多应答若干个；表项数量有上限，表满时先清理过期
 *      表项，仍满则拒绝新来源（伪造大量来源 IP 时内存不会无限增长）。
 *  收发与端口管理在 P2PAddressProbe（第 2e-3 步）。
 *
 *  单元测试见 flamingoserver/tests/test_p2paddressprobe.cpp（Suite STUN、RL）。
 *
 *  balloonwj@qq.com   2026-10-03
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string>
#include <unordered_map>

//STUN 事务号：12 字节，应答原样带回，客户端据此把应答与请求对上
struct StunTransactionId
{
    uint8_t m_bytes[12];   //事务号的 12 个字节，按报文中的顺序存放

    StunTransactionId()
    {
        for (size_t i = 0; i < sizeof(m_bytes); ++i)
            m_bytes[i] = 0;
    }
};

/**
 *  Binding 报文的编解码（RFC 5389 的一个子集）。只有静态函数，不需要实例。
 */
class P2PStunCodec
{
public:
    //STUN 报文头的长度（字节）
    static const size_t kHeaderBytes = 20;
    //成功应答的长度（字节）：头 20 字节，加 XOR-MAPPED-ADDRESS 属性（属性头 4 字节、IPv4 地址值 8 字节）
    static const size_t kBindingResponseBytes = 32;
    //接受的最短请求（字节）：不小于应答长度，保证应答不大于请求
    static const size_t kMinRequestBytes = kBindingResponseBytes;

    /**
     *  判断一个 UDP 报文是不是可以应答的 Binding 请求，是则取出事务号。以下情况返回 false：
     *  不足 kMinRequestBytes 字节；首字节最高两位不为 0；类型不是 Binding 请求（0x0001）；魔数不是 0x2112A442；
     *  长度字段不是 4 的倍数或与报文实际长度不符。属性内容不解析。
     *  @param data 报文内容，不接管所有权；len 为 0 时可以为 NULL
     *  @param len  报文长度（字节）
     *  @param txid [出参] 事务号，仅在返回 true 时有效
     *  @return 可以应答返回 true
     */
    static bool parseBindingRequest(const uint8_t* data, size_t len, StunTransactionId& txid);

    /**
     *  构造 Binding 成功应答（类型 0x0101），只带一个 XOR-MAPPED-ADDRESS 属性。
     *  @param txid 请求的事务号
     *  @param ip   请求方的 IPv4 地址（主机字节序）
     *  @param port 请求方的 UDP 端口（主机字节序）
     *  @return 应答报文，固定 kBindingResponseBytes 字节
     */
    static std::string buildBindingResponse(const StunTransactionId& txid, uint32_t ip, uint16_t port);
};

/**
 *  按来源 IP 的限频：每个 IP 在一秒的固定窗口内最多放行 maxPerSecond 个请求。
 *  只在地址探测服务所在的那一个 EventLoop 线程上使用，不加锁。
 */
class P2PProbeRateLimiter
{
public:
    /**
     *  @param maxPerSecond 每个来源 IP 每秒最多放行的请求数，须大于 0
     *  @param maxEntries   同时记录的来源 IP 数上限，须大于 0
     */
    P2PProbeRateLimiter(int maxPerSecond, size_t maxEntries);

    /**
     *  判断是否放行来自 ip 的一个请求；放行时计入该 IP 当前窗口的计数。
     *  新来源遇到表满时，先清理窗口已过期的表项；清理后仍满则拒绝。
     *  @param ip    来源 IPv4 地址（主机字节序）
     *  @param nowMs 当前时间（毫秒，单调时钟即可）
     *  @return 放行返回 true
     */
    bool allow(uint32_t ip, int64_t nowMs);

    //当前记录的来源 IP 数
    size_t entryCount() const;

private:
    //一个来源 IP 的当前窗口
    struct Window
    {
        int64_t m_startMs;   //窗口开始时刻（毫秒）
        int     m_count;     //窗口内已放行的请求数
    };

    //清理窗口已过期（开始时刻距今不少于一秒）的表项
    void purgeExpired(int64_t nowMs);

private:
    int                                    m_maxPerSecond;   //每个来源 IP 每秒最多放行的请求数
    size_t                                 m_maxEntries;     //同时记录的来源 IP 数上限
    std::unordered_map<uint32_t, Window>   m_windows;        //来源 IP → 当前窗口
};
