/**
 *  P2P 链路报文的格式（docs/P2P通信方案.md 7.1 节，第 3 期 3a）：报文类型、报文头的布局与解析、首字节分类。
 *
 *  一个链路报文（UDP 路径上就是一个 UDP 报文，中转路径上整个放进 1862 的帧体）：
 *      [类型 1B][保留 1B][会话标签 4B][计数器 8B][密文 …][认证标签 16B]
 *  - 类型、保留、会话标签、计数器这 14 字节是报文头，以明文传输，同时作为附加数据参与认证（改动任何一字节都会
 *    认证失败）；
 *  - 会话标签由发送方向的密钥派生，接收方据此在解密之前丢弃无关报文；
 *  - 计数器是发送方向上单调递增的序号，同时生成 nonce，接收方据此做重放检查（P2PReplayWindow）。
 *  加解密见 P2PCrypto.h。
 *
 *  类型的取值都不小于 0x80：STUN 报文首字节的最高两位必须为 0（RFC 5389 第 6 节），因此同一个 UDP socket 上，
 *  地址探测服务的应答与对端发来的链路报文按首字节即可区分（ClassifyDatagram）。
 *
 *  只依赖标准库，不碰 socket，可以直接编进单元测试（FlamingoUnit 的 [p2pcore]）。
 *
 *  balloonwj@qq.com   2026-10-04
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace p2p {

//链路报文的类型（报文首字节）
enum p2p_packet_type
{
    p2p_packet_probe        = 0x81,   //连通性检查的探测：在每个候选地址对上持续重发，直到收到探测应答或建链超时
    p2p_packet_probe_ack    = 0x82,   //探测应答：原样带回探测里的事务号，收到即说明这个地址对双向连通
    p2p_packet_nominate     = 0x83,   //发起方从连通的地址对里选定一对，在那个地址对上发出
    p2p_packet_nominate_ack = 0x84,   //应答方确认选定；发起方收到后链路就绪
    p2p_packet_data         = 0x85,   //业务数据：解密后为通道号、通道类型与负载（第 4 期）
    p2p_packet_keepalive    = 0x86,   //保活：链路空闲时定时发送，同时维持 NAT 映射
    p2p_packet_close        = 0x87,   //关闭链路：一方主动结束时发出，对方收到后立即收尾
};

//UDP 报文的分类结果（ClassifyDatagram 的返回值）
enum p2p_datagram_kind
{
    p2p_datagram_unknown = 0,   //既不是 STUN 也不是链路报文（或太短），丢弃
    p2p_datagram_stun    = 1,   //STUN 报文：地址探测服务的应答
    p2p_datagram_link    = 2,   //链路报文：类型是 p2p_packet_type 之一，长度不小于报文头加认证标签
};

//报文头的长度（字节）：类型 1 + 保留 1 + 会话标签 4 + 计数器 8
const size_t kP2PHeaderBytes = 14;

//认证标签的长度（字节）：ChaCha20-Poly1305 的 Poly1305 标签
const size_t kP2PAuthTagBytes = 16;

//单个 UDP 报文的上限（字节）：取 QUIC（RFC 9000 第 14 节）要求任何路径都必须支持的 1200 字节，避开 IP 分片
const size_t kP2PMaxDatagramBytes = 1200;

//一个链路报文能承载的最大明文（字节）
const size_t kP2PMaxPlainBytes = kP2PMaxDatagramBytes - kP2PHeaderBytes - kP2PAuthTagBytes;

//中转路径上一个链路报文能承载的最大明文（字节，第 4 期 4a 起）：中转是 TCP，不受 UDP 报文大小的限制；取 64 KiB，
//每字节的封装开销降到千分之一以下，单个报文在 imgserver 的转发缓冲里也占用不多
const size_t kP2PRelayMaxPlainBytes = 64 * 1024;

//报文头里各字段的偏移（字节）
const size_t kP2POffsetType = 0;
const size_t kP2POffsetReserved = 1;
const size_t kP2POffsetSessionTag = 2;
const size_t kP2POffsetCounter = 6;

//解析出来的报文头
struct P2PPacketHeader
{
    uint8_t  m_type;         //报文类型，取值见 p2p_packet_type
    uint32_t m_sessionTag;   //会话标签（报文中按网络字节序存放）
    uint64_t m_counter;      //发送计数器（报文中按网络字节序存放）

    P2PPacketHeader()
        : m_type(0)
        , m_sessionTag(0)
        , m_counter(0)
    {
    }
};

/**
 *  首字节是不是某种链路报文类型。
 *  @param b 报文首字节
 *  @return 是 p2p_packet_type 之一返回 true
 */
bool IsP2PPacketType(uint8_t b);

/**
 *  给一个收到的 UDP 报文分类。
 *  @param data 报文内容，不接管所有权；len 为 0 时可以为 NULL
 *  @param len  报文长度（字节）
 *  @return p2p_datagram_kind：首字节最高两位为 0 且长度不小于 20 字节为 STUN；首字节是链路报文类型且长度不小于
 *          报文头加认证标签为链路报文；其余为 unknown
 */
int ClassifyDatagram(const uint8_t* data, size_t len);

/**
 *  写报文头（14 字节）。
 *  @param type       报文类型
 *  @param sessionTag 会话标签
 *  @param counter    发送计数器
 *  @param out        [出参] 至少 kP2PHeaderBytes 字节的缓冲，不接管所有权
 */
void WritePacketHeader(uint8_t type, uint32_t sessionTag, uint64_t counter, uint8_t* out);

/**
 *  解析报文头。
 *  @param data 报文内容
 *  @param len  报文长度（字节）
 *  @param out  [出参] 解析结果，仅在返回 true 时有效
 *  @return 长度不小于报文头加认证标签、且类型是链路报文类型时返回 true
 */
bool ParsePacketHeader(const uint8_t* data, size_t len, P2PPacketHeader& out);

} // namespace p2p
