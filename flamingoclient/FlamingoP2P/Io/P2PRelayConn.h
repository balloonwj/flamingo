/**
 *  P2P 回落中转的一条 TCP 连接（docs/P2P通信方案.md 6.4 节、docs/协议设计-v2.md 18.4 节，第 3 期 3e）。
 *
 *  - Start 起一个连接线程：解析地址、带超时连接 imgserver 的中转端口，做三帧协议加密握手（有服务端公钥时），再发
 *    cmd_p2p_relay_hello(1861) { "token" } 报到并等应答 { "code" }；全部完成后状态变为就绪，失败则变为失败，
 *    两种情况都经 IP2PRelayWaker 唤醒 IO 线程；连接、握手与报到合计最多 kP2PRelayConnectTimeoutMs；
 *  - 就绪之后由 IO 线程收包：select 报告可读时调用 ReadPackets，拆出 cmd_p2p_relay_data(1862) 帧并逐跳解密，
 *    包体就是对方发来的链路报文；
 *  - 发送（IP2PDatagramSender::SendTo，地址参数被忽略）把一个链路报文装进 1862 帧，追加到发送缓冲后立即尽量写进
 *    socket（就绪之后 socket 为非阻塞），写不完的部分由 IO 线程在 socket 可写时调用 Flush 继续写（第 4b 步起）；
 *    可以在任意线程上调用，加密与入缓冲都在发送锁之内（流加密按调用顺序推进计数器，写出的先后必须与加密的先后一致）。
 *    缓冲超过 kP2PRelayOutHardLimitBytes 时 SendTo 返回 false（丢弃这个报文）；业务方的流控按 QueuedBytes 在更低的
 *    水位就会停下；
 *  - Close 先 shutdown 使阻塞中的收发立即返回，再等连接线程结束，最后关闭 socket。
 *
 *  结构照 Source/Feature/FolderTransfer/FolderRelayClient（连接、握手、报到的流程相同），区别是就绪之后不起接收线程，
 *  收包交给 P2P 引擎的 IO 线程，与 UDP socket 一起 select。
 *
 *  典型用法（P2PEngine）：
 *      relay.Start(host, port, token, serverPubKey, &waker);
 *      ...IO 线程被唤醒后：relay.State() 为 p2p_relay_ready 时把 relay 交给会话（OnRelayConnected）；
 *      ...select 报告 relay.Handle() 可读：relay.ReadPackets(packets)，逐个交给会话（OnRelayPacket）；
 *      relay.Close();
 *
 *  balloonwj@qq.com   2026-10-04
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <atomic>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "../../Source/net/FrameCrypto.h"   //net::ClientHandshake / net::IDENTITY_PUBLICKEY_BYTES
#include "../Ice/P2PAddressProber.h"        //IP2PDatagramSender

namespace p2p {

//中转连接的状态
enum p2p_relay_state
{
    p2p_relay_idle       = 0,   //尚未 Start
    p2p_relay_connecting = 1,   //连接线程正在连接、握手或报到
    p2p_relay_ready      = 2,   //报到成功，可以收发 1862
    p2p_relay_failed     = 3,   //连接、握手或报到失败（终态）
    p2p_relay_closed     = 4,   //已关闭，或就绪之后连接断开（终态）
};

//连接、握手与报到合计的超时（毫秒）：与 FolderRelayClient 等现有中转客户端的 5 秒一致
const int kP2PRelayConnectTimeoutMs = 5000;

//发送缓冲的硬上限（字节）：超过时 SendTo 丢弃报文。可靠通道在 4 MiB 处就会让业务方停下，这里只防异常情况无限增长
const size_t kP2PRelayOutHardLimitBytes = 64 * 1024 * 1024;

/**
 *  连接线程完成（就绪或失败）后唤醒 IO 线程的接口，由引擎实现。
 *  析构函数受保护：连接只借用。
 */
class IP2PRelayWaker
{
public:
    //唤醒 IO 线程（在连接线程上调用，实现须线程安全）
    virtual void WakeIo() = 0;

protected:
    ~IP2PRelayWaker()
    {
    }
};

/**
 *  一条到 imgserver 中转端口的连接。
 */
class P2PRelayConn : public IP2PDatagramSender
{
public:
    P2PRelayConn();
    ~P2PRelayConn();

    /**
     *  开始连接（起连接线程后立即返回）。只能调用一次。
     *  @param host         中转地址（IP 文本或域名，取自 2302 的 relayip）
     *  @param port         中转端口（2302 的 relayport）
     *  @param token        中转令牌（2302 的 token，角色为 11 或 12）
     *  @param serverPubKey 服务端身份公钥（net::IDENTITY_PUBLICKEY_BYTES 字节，复制保存）；为 NULL 表示服务端没有
     *                      配置公钥，本连接不握手、全程明文
     *  @param waker        完成后唤醒 IO 线程（借用，须比本对象活得久）
     *  @return 已起连接线程返回 true；已经开始过或建线程失败返回 false（后者状态变为失败）
     */
    bool Start(const std::string& host, uint16_t port, const std::string& token, const unsigned char* serverPubKey,
               IP2PRelayWaker* waker);

    //当前状态，取值见 p2p_relay_state
    int State() const;

    //socket 句柄（就绪之后放进 select 的可读集合）；未就绪时为 INVALID_SOCKET 的值
    uintptr_t Handle() const;

    /**
     *  读一次 socket 并拆出完整的 1862 帧（IO 线程在 select 报告可读之后调用）。
     *  @param packets [出参] 追加收到的链路报文（1862 的包体，已逐跳解密）
     *  @return 连接仍然正常返回 true；对方关闭、出错、帧格式或解密错误返回 false（状态变为已关闭）
     */
    bool ReadPackets(std::vector<std::string>& packets);

    /**
     *  经中转发一个链路报文（IP2PDatagramSender 的实现，ip 与 port 被忽略）。可以在任意线程上调用。
     *  @param data 报文，只在本次调用期间有效
     *  @param len  报文长度（字节）
     *  @return 已全部写进 socket 返回 true；未就绪或写失败返回 false
     */
    virtual bool SendTo(uint32_t ip, uint16_t port, const uint8_t* data, size_t len) override;

    //发送缓冲里还没写进 socket 的字节数（IP2PDatagramSender 的实现，任意线程上可调用）
    virtual size_t QueuedBytes() const override;

    /**
     *  把发送缓冲尽量写进 socket（IO 线程在 select 报告可写时调用）。
     *  @return 连接仍然正常返回 true；写出错返回 false（状态变为已关闭）
     */
    bool Flush();

    //关闭：使阻塞中的收发立即返回、等连接线程结束、关闭 socket。可重复调用；不得在连接线程上调用
    void Close();

private:
    P2PRelayConn(const P2PRelayConn&);
    P2PRelayConn& operator=(const P2PRelayConn&);

    class HandshakeChannel;

    //连接线程的入口：连接、握手、报到，最后设置状态并唤醒 IO 线程
    void ConnectProc();

    //带超时连接（非阻塞 connect + 分段 select，期间检查关闭标志）；成功后切回阻塞模式并设收发超时
    bool DoConnect(int64_t deadlineMs);

    //发报到帧并等应答；code 为 0 返回 true
    bool SendHelloAndWaitAck(int64_t deadlineMs);

    //把 len 个字节全部写进 socket
    bool SendAll(const char* data, size_t len);

    //收满 len 个字节（握手用），超过 deadlineMs 或关闭时返回 false
    bool RecvAll(char* buf, size_t len, int64_t deadlineMs);

    //把发送缓冲尽量写进非阻塞 socket；调用方已持有 m_sendMutex。写出错返回 false
    bool FlushLocked();

private:
    std::string             m_host;              //中转地址
    uint16_t                m_port;              //中转端口
    std::string             m_token;             //中转令牌
    unsigned char           m_serverPubKey[net::IDENTITY_PUBLICKEY_BYTES];   //服务端身份公钥的副本
    bool                    m_hasServerPubKey;   //是否有服务端公钥（没有时不握手）
    IP2PRelayWaker*         m_waker;             //完成后唤醒 IO 线程（借用）
    uintptr_t               m_socket;            //socket 句柄；连接线程建立，Close 时关闭
    net::ClientHandshake    m_handshake;         //协议加密握手器，握手成功后其 cipher() 用于逐跳加解密
    std::atomic<int>        m_state;             //当前状态，取值见 p2p_relay_state
    std::atomic<bool>       m_stopping;          //Close 已调用：连接线程据此尽快退出
    std::thread             m_thread;            //连接线程
    std::mutex              m_sendMutex;         //保护发送：加密、入缓冲与写 socket 必须在同一把锁内
    std::deque<std::string> m_outQueue;          //发送缓冲：编好的帧，按先后排队（受 m_sendMutex 保护）
    size_t                  m_outFrontOffset;    //队首那一帧已写出的字节数（受 m_sendMutex 保护）
    std::atomic<size_t>     m_outBytes;          //发送缓冲里尚未写出的字节数
    std::string             m_recvBuf;           //尚未拆成完整帧的字节（报到阶段多收的字节也留在这里）
};

} // namespace p2p
