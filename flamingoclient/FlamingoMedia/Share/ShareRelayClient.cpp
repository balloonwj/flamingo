/**
 *  屏幕共享房间中转客户端实现，ShareRelayClient.cpp
 *  职责与线程约定见 ShareRelayClient.h；连接、握手、收帧的写法与 Call/CallRelayClient.cpp 一致。
 *
 *  balloonwj@qq.com   2026-10-02
 */
#include "ShareRelayClient.h"

#include <ws2tcpip.h>
#include <string.h>
#include "ShareProtocolClient.h"
#include "../../Source/net/Protocol.h"          // cmd_screenshare_relay_* / MAX_BODY_FILESERVER
#include "../../Source/net/FrameCodec.h"        // net::encodeFrame / decodeFrame
#include "../../Source/net/FramePayload.h"      // encodeFramePayload / decodeFramePayload
#include "../../Source/net/HandshakeRunner.h"   // performClientHandshake / IHandshakeChannel
#include "../../Source/Common/Log/IULog.h"      // LOG_INFO / LOG_ERROR

#pragma comment(lib, "ws2_32.lib")

namespace screenshare {

namespace {

//接收线程单次阻塞 recv 的超时，单位毫秒：周期性返回以检查主动关闭标志
const int kRecvTimeoutMs = 500;
//发送超时，单位毫秒：单帧发不出去超过此时长即判连接异常
const int kSendTimeoutMs = 5000;
//接收线程单次读取的块大小，单位字节
const int kRecvChunk = 64 * 1024;
//秒到毫秒的换算系数
const int kMsPerSecond = 1000;
//报到阶段单次 recv 的缓冲大小，单位字节（应答只有几十字节）
const int kHelloRecvChunk = 4096;

} // namespace

/**
 *  握手期间的字节收发适配器：把 net::performClientHandshake 需要的收发转接到本连接的 socket 上。
 *  只在 Connect 里使用，那时接收线程还没启动，不涉及并发。
 */
class ShareRelayClient::HandshakeChannel : public net::IHandshakeChannel
{
public:
    explicit HandshakeChannel(ShareRelayClient* owner)
        : m_owner(owner)
    {
    }

    virtual bool SendAll(const char* data, size_t len)
    {
        return m_owner->SendAll(data, len);
    }

    //收满 len 个字节；socket 已设了 SO_RCVTIMEO，超时会让 recv 返回错误，据此判失败
    virtual bool RecvAll(char* buf, size_t len)
    {
        size_t got = 0;
        while (got < len)
        {
            const int n = ::recv(m_owner->m_sock, buf + got, static_cast<int>(len - got), 0);
            if (n <= 0)
                return false;
            got += static_cast<size_t>(n);
        }
        return true;
    }

private:
    ShareRelayClient* m_owner;   //宿主连接，不拥有
};

ShareRelayClient::ShareRelayClient()
    : m_sock(INVALID_SOCKET)
    , m_connected(false)
    , m_stopping(false)
    , m_handler(NULL)
    , m_encrypted(false)
    , m_lastHelloCode(-1)
{
}

ShareRelayClient::~ShareRelayClient()
{
    Close();
}

bool ShareRelayClient::DoConnect(const std::string& ip, int port, int timeoutSec)
{
    m_sock = ::socket(AF_INET, SOCK_STREAM, 0);
    if (m_sock == INVALID_SOCKET)
        return false;

    //画面帧是一串连续的分片，关掉 Nagle 避免小分片被攒着不发
    long noDelay = 1;
    ::setsockopt(m_sock, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&noDelay), sizeof(long));

    //先用非阻塞模式做带超时的 connect
    unsigned long on = 1;
    if (::ioctlsocket(m_sock, FIONBIO, &on) == SOCKET_ERROR)
        return false;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<u_short>(port));
    addr.sin_addr.s_addr = ::inet_addr(ip.c_str());
    if (addr.sin_addr.s_addr == INADDR_NONE)
    {
        struct hostent* he = ::gethostbyname(ip.c_str());
        if (he == NULL)
            return false;
        addr.sin_addr.s_addr = *reinterpret_cast<unsigned long*>(he->h_addr);
    }

    int ret = ::connect(m_sock, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));
    if (ret == SOCKET_ERROR && ::WSAGetLastError() != WSAEWOULDBLOCK)
        return false;
    if (ret != 0)
    {
        fd_set writeset;
        FD_ZERO(&writeset);
        FD_SET(m_sock, &writeset);
        struct timeval tv;
        tv.tv_sec = timeoutSec;
        tv.tv_usec = 0;
        if (::select(static_cast<int>(m_sock) + 1, NULL, &writeset, NULL, &tv) != 1)
            return false;
    }

    //连上后切回阻塞模式，并设收发超时：接收线程据 SO_RCVTIMEO 周期返回以检查停止标志
    unsigned long off = 0;
    ::ioctlsocket(m_sock, FIONBIO, &off);
    int tmRecv = kRecvTimeoutMs;
    int tmSend = kSendTimeoutMs;
    ::setsockopt(m_sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&tmRecv), sizeof(int));
    ::setsockopt(m_sock, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&tmSend), sizeof(int));
    return true;
}

bool ShareRelayClient::SendAll(const char* data, size_t len)
{
    size_t sent = 0;
    while (sent < len)
    {
        const int n = ::send(m_sock, data + sent, static_cast<int>(len - sent), 0);
        if (n > 0)
        {
            sent += static_cast<size_t>(n);
            continue;
        }
        //阻塞模式下 WSAEWOULDBLOCK 罕见，出现则重试；其余按断开处理
        if (n == SOCKET_ERROR && ::WSAGetLastError() == WSAEWOULDBLOCK)
            continue;
        return false;
    }
    return true;
}

bool ShareRelayClient::PerformHandshake(const unsigned char* serverPubKey)
{
    m_encrypted = (serverPubKey != NULL);
    if (!m_encrypted)
        return true;

    HandshakeChannel channel(this);
    std::string err;
    if (!net::performClientHandshake(channel, serverPubKey, m_handshake, err))
    {
        LOG_ERROR("ShareRelayClient: handshake failed: %s.", err.c_str());
        return false;
    }
    return true;
}

bool ShareRelayClient::DoHello(const std::string& token, int timeoutSec)
{
    //报到包体带的是中转令牌，必须在加密握手之后发
    const std::string helloBody = ShareProtocolClient::buildRelayHello(token);
    std::string wire;
    uint8_t flags = 0;
    uint32_t originlen = 0;
    if (!net::encodeFramePayload(cmd_screenshare_relay_hello, 0, helloBody, &m_handshake.cipher(), 0,
                                 net::payload_compress_never, wire, flags, originlen))
        return false;
    std::string hello;
    if (!net::encodeFrame(cmd_screenshare_relay_hello, 0, flags, wire.data(), static_cast<uint32_t>(wire.size()),
                          originlen, hello))
        return false;
    if (!SendAll(hello.data(), hello.size()))
        return false;

    //阻塞收报到应答，最多等 timeoutSec
    std::string acc;
    int waitedMs = 0;
    while (waitedMs < timeoutSec * kMsPerSecond)
    {
        char buf[kHelloRecvChunk];
        const int n = ::recv(m_sock, buf, sizeof(buf), 0);
        if (n == 0)
            return false;
        if (n < 0)
        {
            const int e = ::WSAGetLastError();
            if (e == WSAETIMEDOUT || e == WSAEWOULDBLOCK)
            {
                waitedMs += kRecvTimeoutMs;
                continue;
            }
            return false;
        }
        acc.append(buf, static_cast<size_t>(n));
        while (true)
        {
            frame_header h;
            std::string body;
            size_t consumed = 0;
            const net::frame_decode_result r =
                net::decodeFrame(acc.data(), acc.size(), h, body, consumed, MAX_BODY_FILESERVER);
            if (r == net::frame_decode_need_more)
                break;
            if (r == net::frame_decode_error)
                return false;
            acc.erase(0, consumed);
            if (h.cmd != cmd_screenshare_relay_hello)
                continue;

            std::string plain;
            std::string err;
            if (!net::decodeFramePayload(h.cmd, h.seq, h.flags, h.originlen, body, &m_handshake.cipher(), plain,
                                         err))
            {
                LOG_ERROR("ShareRelayClient: decrypt hello ack failed: %s.", err.c_str());
                return false;
            }
            int code = -1;
            if (!ShareProtocolClient::parseRelayHelloAck(plain, code))
                return false;
            m_lastHelloCode = code;
            //握手阶段多收的字节留给接收线程
            m_recvBuf = acc;
            return code == 0;
        }
    }
    return false;
}

bool ShareRelayClient::Connect(const std::string& ip, int port, const std::string& token,
                               const unsigned char* serverPubKey, ShareRelayClientHandler* handler, int timeoutSec)
{
    if (handler == NULL)
        return false;
    if (m_connected.load())
        return true;

    m_handler = handler;
    m_stopping.store(false);
    m_lastHelloCode = -1;
    m_recvBuf.clear();

    if (!DoConnect(ip, port, timeoutSec))
    {
        LOG_ERROR("ShareRelayClient: connect relay %s:%d failed.", ip.c_str(), port);
        CloseSocket();
        return false;
    }
    if (!PerformHandshake(serverPubKey))
    {
        CloseSocket();
        return false;
    }
    if (!DoHello(token, timeoutSec))
    {
        LOG_ERROR("ShareRelayClient: relay hello failed, code=%d, relay %s:%d.", m_lastHelloCode, ip.c_str(),
                  port);
        CloseSocket();
        return false;
    }

    m_connected.store(true);
    m_recvThread.reset(new std::thread(&ShareRelayClient::RecvProc, this));
    LOG_INFO("ShareRelayClient: joined relay room, relay %s:%d, encrypted=%d.", ip.c_str(), port,
             m_encrypted ? 1 : 0);
    return true;
}

bool ShareRelayClient::EncodeBodyLocked(const std::string& body, std::string& frame)
{
    //1860 不压缩：服务端要在解密后直接读帧头
    std::string wire;
    uint8_t flags = 0;
    uint32_t originlen = 0;
    if (!net::encodeFramePayload(cmd_screenshare_relay_data, 0, body, &m_handshake.cipher(), 0,
                                 net::payload_compress_never, wire, flags, originlen))
        return false;
    return net::encodeFrame(cmd_screenshare_relay_data, 0, flags, wire.data(), static_cast<uint32_t>(wire.size()),
                            originlen, frame);
}

bool ShareRelayClient::SendBody(const std::string& body)
{
    std::vector<std::string> one(1, body);
    return SendBodies(one);
}

bool ShareRelayClient::SendBodies(const std::vector<std::string>& bodies)
{
    if (!m_connected.load())
        return false;

    //加密必须在发送锁之内做：流加密按调用顺序推进内部计数器，锁外加密、锁内发送会让写到线上的先后
    //与加密的先后相反，服务端按流顺序解密必然失败
    std::lock_guard<std::mutex> guard(m_sendMtx);
    for (size_t i = 0; i < bodies.size(); ++i)
    {
        if (!m_connected.load())
            return false;
        std::string frame;
        if (!EncodeBodyLocked(bodies[i], frame))
        {
            LOG_ERROR("ShareRelayClient: encode relay data failed, len=%u.", static_cast<unsigned>(bodies[i].size()));
            return false;
        }
        if (!SendAll(frame.data(), frame.size()))
        {
            LOG_ERROR("ShareRelayClient: send relay data failed, len=%u.", static_cast<unsigned>(frame.size()));
            return false;
        }
    }
    return true;
}

void ShareRelayClient::RecvProc()
{
    if (m_handler != NULL)
        m_handler->OnRelayThreadStart();
    while (!m_stopping.load())
    {
        //先处理缓冲里已有的完整帧（报到阶段可能多收了几帧）
        bool protoErr = false;
        while (true)
        {
            frame_header h;
            std::string body;
            size_t consumed = 0;
            const net::frame_decode_result r =
                net::decodeFrame(m_recvBuf.data(), m_recvBuf.size(), h, body, consumed, MAX_BODY_FILESERVER);
            if (r == net::frame_decode_need_more)
                break;
            if (r == net::frame_decode_error)
            {
                protoErr = true;
                break;
            }
            m_recvBuf.erase(0, consumed);
            if (h.cmd != cmd_screenshare_relay_data)
                continue;

            //解密失败意味着这个方向的流已不可恢复，只能断开，不能跳过本帧继续解下一帧
            std::string plain;
            std::string err;
            if (!net::decodeFramePayload(h.cmd, h.seq, h.flags, h.originlen, body, &m_handshake.cipher(), plain,
                                         err))
            {
                LOG_ERROR("ShareRelayClient: decrypt relay data failed: %s, closing.", err.c_str());
                protoErr = true;
                break;
            }
            if (m_handler != NULL)
                m_handler->OnRelayBody(plain.data(), plain.size());
        }
        if (protoErr)
        {
            LOG_ERROR("ShareRelayClient: relay frame protocol error, closing.");
            break;
        }

        char buf[kRecvChunk];
        const int n = ::recv(m_sock, buf, sizeof(buf), 0);
        if (n > 0)
        {
            m_recvBuf.append(buf, static_cast<size_t>(n));
            continue;
        }
        if (n == 0)
        {
            LOG_INFO("ShareRelayClient: relay peer closed connection.");
            break;
        }
        const int e = ::WSAGetLastError();
        if (e == WSAETIMEDOUT || e == WSAEWOULDBLOCK)
            continue;
        if (!m_stopping.load())
            LOG_ERROR("ShareRelayClient: recv error %d, closing.", e);
        break;
    }

    //异常断开（非主动 Close）时回调一次 OnRelayClosed，便于上层收尾
    const bool wasStopping = m_stopping.load();
    m_connected.store(false);
    if (!wasStopping && m_handler != NULL)
        m_handler->OnRelayClosed();
    if (m_handler != NULL)
        m_handler->OnRelayThreadExit();
}

void ShareRelayClient::CloseSocket()
{
    if (m_sock != INVALID_SOCKET)
    {
        //先 shutdown 再 closesocket，促使接收线程阻塞的 recv 立即返回
        ::shutdown(m_sock, SD_BOTH);
        ::closesocket(m_sock);
        m_sock = INVALID_SOCKET;
    }
}

void ShareRelayClient::Close()
{
    m_stopping.store(true);
    m_connected.store(false);
    {
        //等正在进行的发送结束再关 socket，避免发送线程写一个已关闭的句柄
        std::lock_guard<std::mutex> guard(m_sendMtx);
        CloseSocket();
    }
    if (m_recvThread && m_recvThread->joinable())
    {
        //不能在接收线程自身上 join 自己：那种情况下分离即可
        if (std::this_thread::get_id() != m_recvThread->get_id())
            m_recvThread->join();
        else
            m_recvThread->detach();
    }
    m_recvThread.reset();
}

bool ShareRelayClient::IsConnected() const
{
    return m_connected.load();
}

int ShareRelayClient::LastHelloCode() const
{
    return m_lastHelloCode;
}

} // namespace screenshare
