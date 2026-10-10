/**
 *  屏幕共享房间中转客户端，ShareRelayClient.h
 *
 *  管理一条到 imgserver 中转端口（与远程桌面、通话共用 relaylistenport）的独立 TCP 连接：连上后做三帧协议
 *  加密握手，再用 cmd_screenshare_relay_hello（1859）携 relay 令牌报到，服务端按令牌里的房间号与座位号把本连接
 *  登记进房间；之后在该连接上双向收发 cmd_screenshare_relay_data（1860）。1860 的包体由 ShareFraming 封装
 *  （帧头 + 载荷，大帧分片），本类只负责加密、组帧与收发，不解析包体；服务端要读帧头，所以 1860 一律不压缩。
 *
 *  接收线程阻塞收字节、拆帧、解密，经回调把每个 1860 包体交给上层（上层用 ShareFrameReassembler 重组）。
 *  发送可在任意线程调用，内部一把发送锁保证「加密的先后」与「写到线上的先后」一致（理由同 CallRelayClient）。
 *  一个视频帧的若干分片用 SendBodies 一次发出，中间不会插进别的线程的帧。
 *
 *  socket 收发不进自动化单测，由联调用例验证，见 flamingoclient/Source/Tests/ShareRelayClientLiveTests.cpp。
 *
 *  典型用法：
 *      screenshare::ShareRelayClient relay;
 *      if (relay.Connect(info.m_relayIp, info.m_relayPort, info.m_token, serverPubKey, &handler, 5))
 *          relay.SendBodies(fragments);
 *      ...
 *      relay.Close();
 *
 *  balloonwj@qq.com   2026-10-02
 */
#pragma once

#include <winsock2.h>
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "../../Source/net/FrameCrypto.h"   // net::ClientHandshake（本连接自己的会话密钥）

namespace screenshare {

//中转连接事件回调接口。回调在 ShareRelayClient 的接收线程上触发，实现方须自行保证线程安全，
//不可在回调里做耗时阻塞操作，也不可在回调里调用本连接的 Close（会等待接收线程自身结束）。
class ShareRelayClientHandler
{
public:
    virtual ~ShareRelayClientHandler()
    {
    }

    /**
     *  收到一帧 1860 的包体（已解密，帧头 + 载荷）。
     *  @param body 包体起始地址，只在本次回调期间有效
     *  @param len  包体字节数
     */
    virtual void OnRelayBody(const char* body, size_t len) = 0;

    //连接异常断开（对端关闭、收发出错、解密失败）；主动 Close 不回调
    virtual void OnRelayClosed() = 0;

    //接收线程刚启动、还没收任何帧时调用一次（观看端在这里初始化本线程的 COM 与 Media Foundation，好在本线程解码）
    virtual void OnRelayThreadStart()
    {
    }

    //接收线程即将退出时调用一次（无论主动关闭还是异常断开），与 OnRelayThreadStart 配对
    virtual void OnRelayThreadExit()
    {
    }
};

//屏幕共享房间中转客户端；一个房间成员持有一个实例
class ShareRelayClient
{
public:
    ShareRelayClient();
    ~ShareRelayClient();

    ShareRelayClient(const ShareRelayClient& rhs) = delete;
    ShareRelayClient& operator=(const ShareRelayClient& rhs) = delete;

    /**
     *  连接中转端口、握手、报到；成功后启动接收线程。阻塞调用，最长约 timeoutSec 秒。
     *  @param ip           中转地址（1726 的 relayip）
     *  @param port         中转端口（1726 的 relayport）
     *  @param token        relay 令牌（1726 的 token）
     *  @param serverPubKey 服务端 32 字节公钥；为 NULL 时不做加密握手（服务端未启用加密的环境）。只在本次调用期间读取
     *  @param handler      事件回调，不转移所有权，须在 Close 返回之前一直有效；不可为 NULL
     *  @param timeoutSec   连接与报到的超时，单位秒
     *  @return 报到成功返回 true；失败时 LastHelloCode 给出服务端回的错误码（未收到应答时为 -1）
     */
    bool Connect(const std::string& ip, int port, const std::string& token, const unsigned char* serverPubKey,
                 ShareRelayClientHandler* handler, int timeoutSec);

    /**
     *  发送一帧 1860。可在任意线程调用。
     *  @param body 包体（ShareFraming 封装好的帧头 + 载荷）
     *  @return 已完整写入 socket 返回 true；未连接或发送失败返回 false
     */
    bool SendBody(const std::string& body);

    /**
     *  依次发送多帧 1860（一个视频帧的全部分片），整个过程持有发送锁，中间不会插入其它线程的帧。
     *  @return 全部写入返回 true；中途失败返回 false（之后的帧不再发送）
     */
    bool SendBodies(const std::vector<std::string>& bodies);

    //主动关闭连接并等待接收线程结束；未连接时什么都不做，可重复调用。不可在回调里调用
    void Close();

    //是否已报到成功且连接仍在
    bool IsConnected() const;

    //最近一次 Connect 收到的报到应答错误码；未收到应答时为 -1
    int LastHelloCode() const;

private:
    class HandshakeChannel;

    //带超时的 TCP 连接；成功后 socket 为阻塞模式并设好收发超时
    bool DoConnect(const std::string& ip, int port, int timeoutSec);
    //把一段字节完整写入 socket
    bool SendAll(const char* data, size_t len);
    //三帧协议加密握手
    bool PerformHandshake(const unsigned char* serverPubKey);
    //发 1859 报到并等应答；成功时把握手阶段多收的字节留在 m_recvBuf
    bool DoHello(const std::string& token, int timeoutSec);
    //加密并组好一帧 1860；调用方须已持有发送锁
    bool EncodeBodyLocked(const std::string& body, std::string& frame);
    //接收线程主体
    void RecvProc();
    //关闭 socket（不等线程）
    void CloseSocket();

    SOCKET                       m_sock;           //中转连接的 socket
    std::atomic<bool>            m_connected;      //是否已报到成功且连接仍在
    std::atomic<bool>            m_stopping;       //是否正在主动关闭（接收线程据此退出且不回调 OnRelayClosed）
    ShareRelayClientHandler*     m_handler;        //事件回调（不拥有）
    net::ClientHandshake         m_handshake;      //本连接的握手器，握手后其 cipher() 用于加解密
    bool                         m_encrypted;      //是否做了加密握手
    std::mutex                   m_sendMtx;        //发送锁：加密与写 socket 一起在锁内完成
    std::string                  m_recvBuf;        //接收累积缓冲（只在 Connect 与接收线程里使用，两者不并发）
    std::unique_ptr<std::thread> m_recvThread;     //接收线程
    int                          m_lastHelloCode;  //最近一次报到应答的错误码
};

} // namespace screenshare
