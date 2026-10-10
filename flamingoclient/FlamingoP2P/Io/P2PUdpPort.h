/**
 *  一个 P2P 会话用的 UDP socket（docs/P2P通信方案.md 6.1、8.2 节，第 3 期 3d）。
 *
 *  - 绑定 0.0.0.0 的随机端口：第 0a 步实测，绑定固定端口会让 Windows 弹出防火墙提示框，随机端口不会（14.2 节）；
 *  - 非阻塞；关闭 SIO_UDP_CONNRESET，否则向已关闭的端口发包后收到的 ICMP 端口不可达会让此后的 recvfrom 报错；
 *  - 实现 IP2PDatagramSender，供地址探测、连通性检查与会话发包；SendTo 可以在任意线程上调用（sendto 本身线程安全），
 *    收包只在 IO 线程上进行；
 *  - 成批发送（第 4e 步起）：Open 时尝试设置 UDP_SEND_MSG_SIZE（每段 kP2PMaxDatagramBytes 字节），成功后 SendBatch
 *    一次 sendto 交出多个等长报文，由系统切成独立的 UDP 报文（第 0b 步实测，回环上约为逐个发送的 3 倍）；Win10 之前的
 *    系统不支持该选项，此时 SendBatch 退回逐个发送。该选项不影响单个不超过 1200 字节的报文。
 *
 *  头文件不包含 winsock 的头文件（socket 句柄以 uintptr_t 保存），包含它的源文件不必关心 winsock.h 与 winsock2.h 的顺序。
 *
 *  balloonwj@qq.com   2026-10-04
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "../Core/P2PPacket.h"   //kP2PMaxDatagramBytes
#include "../Ice/P2PAddressProber.h"

namespace p2p {

//一次成批发送的总长度上限（字节）：kP2PMaxBatchSegments 个满载报文
const size_t kP2PMaxBatchBytes = kP2PMaxBatchSegments * kP2PMaxDatagramBytes;

/**
 *  一个 UDP socket。
 */
class P2PUdpPort : public IP2PDatagramSender
{
public:
    P2PUdpPort();
    ~P2PUdpPort();

    /**
     *  创建并绑定到 0.0.0.0 的随机端口。已打开时先关闭。
     *  @return 成功返回 true
     */
    bool Open();

    //关闭 socket（可重复调用）；之后 SendTo 一律返回 false
    void Close();

    //是否已打开
    bool IsOpen() const;

    //绑定的端口（主机字节序）；未打开时为 0
    uint16_t Port() const { return m_port; }

    //socket 句柄（IO 线程放进 select 的集合）；未打开时为 INVALID_SOCKET 的值
    uintptr_t Handle() const { return m_socket; }

    /**
     *  收一个报文（非阻塞）。
     *  @param buf      接收缓冲
     *  @param cap      缓冲大小（字节）
     *  @param fromIp   [出参] 来源地址（主机字节序）
     *  @param fromPort [出参] 来源端口（主机字节序）
     *  @return 收到的字节数；没有数据或出错时返回 -1
     */
    int Receive(uint8_t* buf, int cap, uint32_t& fromIp, uint16_t& fromPort);

    /**
     *  发一个报文（IP2PDatagramSender 的实现）。
     *  @param ip   目的地址（主机字节序）
     *  @param port 目的端口（主机字节序）
     *  @param data 报文，只在本次调用期间有效
     *  @param len  报文长度（字节）
     *  @return 已交给系统发送返回 true
     */
    virtual bool SendTo(uint32_t ip, uint16_t port, const uint8_t* data, size_t len) override;

    //成批发送时每段的字节数（IP2PDatagramSender 的实现）：系统支持发送分段卸载时为 kP2PMaxDatagramBytes，否则为 0
    virtual size_t BatchSegmentBytes() const override;

    /**
     *  成批发送（IP2PDatagramSender 的实现）：支持发送分段卸载时一次 sendto，否则逐段发送。
     *  @param ip   目的地址（主机字节序）
     *  @param port 目的端口（主机字节序）
     *  @param data 首尾相接的报文（除最后一段外每段 BatchSegmentBytes() 字节）
     *  @param len  总长度（字节），不超过 kP2PMaxBatchBytes
     *  @return 已交给系统返回 true
     */
    virtual bool SendBatch(uint32_t ip, uint16_t port, const uint8_t* data, size_t len) override;

private:
    P2PUdpPort(const P2PUdpPort&);
    P2PUdpPort& operator=(const P2PUdpPort&);

private:
    uintptr_t m_socket;      //socket 句柄；未打开时为 INVALID_SOCKET 的值
    uint16_t  m_port;        //绑定的端口（主机字节序）
    size_t    m_usoSegment;  //发送分段卸载的每段字节数；系统不支持时为 0
};

} // namespace p2p
