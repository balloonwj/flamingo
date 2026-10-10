/**
 *  地址探测的客户端报文（docs/P2P通信方案.md 6.2 节，第 3 期 3b）：构造 Binding 请求、解析 Binding 成功应答。
 *
 *  服务端（imgserver 的 P2PAddressProbe）只应答不短于 32 字节的请求（应答固定 32 字节，保证不放大），所以请求
 *  在 20 字节头之后带一个 8 字节值的 SOFTWARE 属性（类型 0x8022，接收方可忽略），正好 32 字节。
 *  应答按 RFC 5389 解析：逐个遍历属性（按 4 字节对齐），取 XOR-MAPPED-ADDRESS（IPv4），其余属性跳过，因此也能
 *  解析带 SOFTWARE、MESSAGE-INTEGRITY 等属性的标准服务器的应答（RFC 5769 第 2.2 节的示例）。
 *
 *  只依赖标准库。
 *
 *  balloonwj@qq.com   2026-10-04
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string>

namespace p2p {

//STUN 事务号的长度（字节）
const size_t kP2PStunTxidBytes = 12;

/**
 *  Binding 报文的构造与解析。只有静态函数。
 */
class P2PStunClient
{
public:
    //请求的长度（字节）：20 字节头 + 12 字节的 SOFTWARE 属性（属性头 4 字节、值 8 字节）
    static const size_t kRequestBytes = 32;

    /**
     *  构造一个 Binding 请求。
     *  @param txid 事务号（kP2PStunTxidBytes 字节）
     *  @return 请求报文，kRequestBytes 字节
     */
    static std::string BuildRequest(const uint8_t* txid);

    /**
     *  解析 Binding 成功应答。
     *  @param data 报文内容，不接管所有权
     *  @param len  报文长度（字节）
     *  @param txid 期望的事务号（kP2PStunTxidBytes 字节）；传 NULL 时不核对，只取出报文里的事务号
     *  @param ip   [出参] 映射地址（主机字节序），仅在返回 true 时有效
     *  @param port [出参] 映射端口（主机字节序），仅在返回 true 时有效
     *  @param outTxid [出参，可为 NULL] 报文里的事务号（kP2PStunTxidBytes 字节）
     *  @return 类型为 0x0101、魔数正确、长度字段与实际相符、事务号相符且带 IPv4 的 XOR-MAPPED-ADDRESS 时返回 true
     */
    static bool ParseResponse(const uint8_t* data, size_t len, const uint8_t* txid, uint32_t& ip, uint16_t& port,
                              uint8_t* outTxid = NULL);
};

} // namespace p2p
