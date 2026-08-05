/**
 *  音视频通话中转客户端实现，CallRelayClient.cpp
 *  balloonwj@qq.com   2026-06-11
 */
#include "CallRelayClient.h"

#include <ws2tcpip.h>
#include <string.h>
#include "../net/Protocol.h"            // cmd_av_relay_* / MAX_BODY_FILESERVER
#include "../net/FrameCodec.h"          // net::encodeFrame / decodeFrame
#include "../net/CallProtocolClient.h"  // buildRelayHello / parseRelayHelloAck / CallRelayHelloAck
#include "../IULog.h"                   // LOG_INFO / LOG_ERROR

#pragma comment(lib, "ws2_32.lib")

namespace call {

//接收线程单次阻塞 recv 的超时（毫秒）：周期性返回以检查主动关闭标志，避免 Close 时久等。
static const int kRecvTimeoutMs = 500;
//发送超时（毫秒）：单帧发不出去超过此时长即判连接异常。
static const int kSendTimeoutMs = 5000;
//接收线程单次读取块大小（字节）。
static const int kRecvChunk = 64 * 1024;

CallRelayClient::CallRelayClient()
    : m_sock(INVALID_SOCKET)
    , m_connected(false)
    , m_stopping(false)
    , m_handler(NULL)
{
}

CallRelayClient::~CallRelayClient()
{
    Close();
}

bool CallRelayClient::DoConnect(const std::string& ip, int port, int timeoutSec)
{
    m_sock = ::socket(AF_INET, SOCK_STREAM, 0);
    if (m_sock == INVALID_SOCKET)
        return false;

    long noDelay = 1;
    ::setsockopt(m_sock, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&noDelay), sizeof(long));

    //先用非阻塞模式做带超时的 connect（仿 CIUSocket::ConnectToImgServer）
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

bool CallRelayClient::SendAll(const char* data, size_t len)
{
    size_t sent = 0;
    while (sent < len)
    {
        int n = ::send(m_sock, data + sent, static_cast<int>(len - sent), 0);
        if (n > 0)
        {
            sent += static_cast<size_t>(n);
            continue;
        }

        //阻塞模式下 WSAEWOULDBLOCK 罕见，出现则重试；其余按断开处理
        int e = ::WSAGetLastError();
        if (n == SOCKET_ERROR && e == WSAEWOULDBLOCK)
            continue;
        return false;
    }
    return true;
}

bool CallRelayClient::Connect(const std::string& ip, int port, const std::string& token,
                              CallRelayClientHandler* handler, int timeoutSec)
{
    if (handler == NULL)
        return false;
    if (m_connected.load())
        return true;

    m_handler = handler;
    m_stopping.store(false);

    if (!DoConnect(ip, port, timeoutSec))
    {
        LOG_ERROR("CallRelayClient: connect relay %s:%d failed.", ip.c_str(), port);
        if (m_sock != INVALID_SOCKET)
        {
            ::closesocket(m_sock);
            m_sock = INVALID_SOCKET;
        }
        return false;
    }

    //发握手帧（cmd_av_relay_hello，包体 {"token":"..."}）
    std::string hello;
    if (!net::encodeFrame(cmd_av_relay_hello, 0,
                          net::CallProtocolClient::buildRelayHello(token), hello))
    {
        ::closesocket(m_sock);
        m_sock = INVALID_SOCKET;
        return false;
    }
    if (!SendAll(hello.data(), hello.size()))
    {
        LOG_ERROR("CallRelayClient: send hello failed, relay %s:%d.", ip.c_str(), port);
        ::closesocket(m_sock);
        m_sock = INVALID_SOCKET;
        return false;
    }

    //阻塞收握手应答（一帧 cmd_av_relay_hello），最多等 timeoutSec
    std::string acc;
    bool gotAck = false;
    int code = -1;
    int waitedMs = 0;
    while (!gotAck && waitedMs < timeoutSec * 1000)
    {
        char buf[4096];
        int n = ::recv(m_sock, buf, sizeof(buf), 0);
        if (n > 0)
        {
            acc.append(buf, static_cast<size_t>(n));
            //从累积缓冲循环拆帧，遇握手应答帧即取出结果码
            while (true)
            {
                frame_header h;
                std::string body;
                size_t consumed = 0;
                net::frame_decode_result r = net::decodeFrame(
                    acc.data(), acc.size(), h, body, consumed, MAX_BODY_FILESERVER);
                if (r == net::frame_decode_need_more)
                    break;
                if (r == net::frame_decode_error)
                {
                    gotAck = true;   //协议错误：以失败收场
                    code = -1;
                    break;
                }
                acc.erase(0, consumed);
                if (h.cmd == cmd_av_relay_hello)
                {
                    net::CallRelayHelloAck ackobj;
                    if (net::CallProtocolClient::parseRelayHelloAck(body, ackobj))
                        code = ackobj.m_code;
                    gotAck = true;
                    break;
                }
                //握手阶段忽略非握手帧
            }
        }
        else if (n == 0)
        {
            break;   //对端关闭
        }
        else
        {
            int e = ::WSAGetLastError();
            if (e == WSAETIMEDOUT || e == WSAEWOULDBLOCK)
            {
                waitedMs += kRecvTimeoutMs;
                continue;
            }
            break;
        }
    }

    if (!gotAck || code != 0)
    {
        LOG_ERROR("CallRelayClient: relay handshake failed, code=%d, relay %s:%d.",
                  code, ip.c_str(), port);
        ::closesocket(m_sock);
        m_sock = INVALID_SOCKET;
        return false;
    }

    //握手成功：把握手阶段可能多收的剩余字节转入接收缓冲，再起接收线程
    m_recvBuf = acc;
    m_connected.store(true);
    m_recvThread.reset(new std::thread(&CallRelayClient::RecvProc, this));
    LOG_INFO("CallRelayClient: relay handshake ok, relay %s:%d.", ip.c_str(), port);
    return true;
}

bool CallRelayClient::SendData(const unsigned char* data, size_t len)
{
    if (!m_connected.load())
        return false;

    //封成单个 cmd_av_relay_data 帧（音频帧小，不分片）
    std::string frame;
    if (!net::encodeFrame(cmd_av_relay_data, 0,
                          std::string(reinterpret_cast<const char*>(data), len), frame))
    {
        LOG_ERROR("CallRelayClient: encode data frame failed, len=%u.", static_cast<unsigned>(len));
        return false;
    }

    std::lock_guard<std::mutex> guard(m_sendMtx);
    if (!m_connected.load())
        return false;
    return SendAll(frame.data(), frame.size());
}

void CallRelayClient::RecvProc()
{
    while (!m_stopping.load())
    {
        char buf[kRecvChunk];
        int n = ::recv(m_sock, buf, sizeof(buf), 0);
        if (n > 0)
        {
            m_recvBuf.append(buf, static_cast<size_t>(n));
            bool protoErr = false;
            //从累积缓冲循环拆帧，把 data 帧包体交给上层
            while (true)
            {
                frame_header h;
                std::string body;
                size_t consumed = 0;
                net::frame_decode_result r = net::decodeFrame(
                    m_recvBuf.data(), m_recvBuf.size(), h, body, consumed, MAX_BODY_FILESERVER);
                if (r == net::frame_decode_need_more)
                    break;
                if (r == net::frame_decode_error)
                {
                    protoErr = true;
                    break;
                }
                m_recvBuf.erase(0, consumed);
                if (h.cmd == cmd_av_relay_data && m_handler != NULL)
                    m_handler->OnRelayData(
                        reinterpret_cast<const unsigned char*>(body.data()), body.size());
            }

            if (protoErr)
            {
                LOG_ERROR("CallRelayClient: recv frame protocol error, closing.");
                break;
            }
        }
        else if (n == 0)
        {
            LOG_INFO("CallRelayClient: relay peer closed connection.");
            break;
        }
        else
        {
            int e = ::WSAGetLastError();
            if (e == WSAETIMEDOUT || e == WSAEWOULDBLOCK)
                continue;   //周期性超时，回去检查停止标志

            LOG_ERROR("CallRelayClient: recv error %d, closing.", e);
            break;
        }
    }

    //异常断开（非主动 Close）时回调一次 OnRelayClosed，便于上层收尾 / 提示
    bool wasStopping = m_stopping.load();
    m_connected.store(false);
    if (!wasStopping && m_handler != NULL)
        m_handler->OnRelayClosed();
}

void CallRelayClient::Close()
{
    m_stopping.store(true);
    m_connected.store(false);

    if (m_sock != INVALID_SOCKET)
    {
        //先 shutdown 再 closesocket，促使接收线程阻塞的 recv 立即返回
        ::shutdown(m_sock, SD_BOTH);
        ::closesocket(m_sock);
        m_sock = INVALID_SOCKET;
    }

    if (m_recvThread && m_recvThread->joinable())
    {
        //不能在接收线程自身上 join 自己（如析构发生在回调里）：那种情况下分离即可
        if (std::this_thread::get_id() != m_recvThread->get_id())
            m_recvThread->join();
        else
            m_recvThread->detach();
    }
    m_recvThread.reset();
}

bool CallRelayClient::IsConnected() const
{
    return m_connected.load();
}

} // namespace call
