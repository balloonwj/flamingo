/**
 *  远程桌面中转客户端实现，RdRelayClient.cpp
 *  balloonwj@qq.com   2026-06-09
 */
#include "RdRelayClient.h"

#include <ws2tcpip.h>
#include <string.h>
#include "../net/Protocol.h"                      // cmd_rd_relay_*
#include "../net/RemoteDesktopProtocolClient.h"   // parseRelayHelloAck / RDRelayHelloAck
#include "../IULog.h"                             // LOG_INFO / LOG_ERROR

#pragma comment(lib, "ws2_32.lib")

namespace remotedesktop {

//接收线程单次阻塞 recv 的超时（毫秒）：周期性返回以检查主动关闭标志，避免 Close 时久等。
static const int kRecvTimeoutMs = 500;
//发送超时（毫秒）：单帧发不出去超过此时长即判连接异常。
static const int kSendTimeoutMs = 5000;
//接收线程单次读取块大小（字节）。
static const int kRecvChunk = 64 * 1024;
//发送引擎帧时的分片大小（字节）：单个 rd_relay_data 帧最多承载这么多引擎帧字节，
//超过则切成多个分片，规避中转单帧上限(2 MiB)。512 KiB 与 fileserver 分片一致。
static const size_t kRelayChunkSize = 512 * 1024;

RdRelayClient::RdRelayClient()
    : m_sock(INVALID_SOCKET)
    , m_connected(false)
    , m_stopping(false)
    , m_handler(NULL)
{
}

RdRelayClient::~RdRelayClient()
{
    Close();
}

bool RdRelayClient::DoConnect(const std::string& ip, int port, int timeoutSec)
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

bool RdRelayClient::SendAll(const char* data, size_t len)
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

bool RdRelayClient::Connect(const std::string& ip, int port, const std::string& token,
                            RdRelayClientHandler* handler, int timeoutSec)
{
    if (handler == NULL)
        return false;
    if (m_connected.load())
        return true;

    m_handler = handler;
    m_stopping.store(false);

    if (!DoConnect(ip, port, timeoutSec))
    {
        LOG_ERROR("RdRelayClient: connect relay %s:%d failed.", ip.c_str(), port);
        if (m_sock != INVALID_SOCKET)
        {
            ::closesocket(m_sock);
            m_sock = INVALID_SOCKET;
        }
        return false;
    }

    //发握手帧
    std::string hello;
    relayframing::BuildHelloFrame(token, hello);
    if (!SendAll(hello.data(), hello.size()))
    {
        LOG_ERROR("RdRelayClient: send hello failed, relay %s:%d.", ip.c_str(), port);
        ::closesocket(m_sock);
        m_sock = INVALID_SOCKET;
        return false;
    }

    //阻塞收握手应答（一帧 cmd_rd_relay_hello），最多等 timeoutSec
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
            std::vector<RdRelayInboundFrame> frames;
            bool err = false;
            size_t used = relayframing::DrainFrames(
                reinterpret_cast<const unsigned char*>(acc.data()), acc.size(), frames, err);
            acc.erase(0, used);
            if (err)
                break;
            for (size_t i = 0; i < frames.size(); ++i)
            {
                if (frames[i].m_cmd != cmd_rd_relay_hello)
                    continue;   //握手阶段忽略非握手帧

                std::string body(reinterpret_cast<const char*>(frames[i].m_body.data()),
                                 frames[i].m_body.size());
                net::RDRelayHelloAck ackobj;
                if (net::RemoteDesktopProtocolClient::parseRelayHelloAck(body, ackobj))
                    code = ackobj.m_code;
                gotAck = true;
                break;
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
        LOG_ERROR("RdRelayClient: relay handshake failed, code=%d, relay %s:%d.",
                  code, ip.c_str(), port);
        ::closesocket(m_sock);
        m_sock = INVALID_SOCKET;
        return false;
    }

    //握手成功：把握手阶段可能多收的剩余字节（理论上没有）转入接收缓冲，重置分片重组器，再起接收线程
    m_recvBuf = acc;
    m_reassembler.Reset();
    m_connected.store(true);
    m_recvThread.reset(new std::thread(&RdRelayClient::RecvProc, this));
    LOG_INFO("RdRelayClient: relay handshake ok, relay %s:%d.", ip.c_str(), port);
    return true;
}

bool RdRelayClient::SendEngineFrame(const unsigned char* data, size_t len)
{
    if (!m_connected.load())
        return false;

    //按分片大小切成一组 rd_relay_data 帧（小帧为单个 whole 帧）
    std::vector<std::string> frames;
    if (!relayframing::BuildEngineFrames(data, len, kRelayChunkSize, frames))
    {
        LOG_ERROR("RdRelayClient: build engine frames failed, len=%u.", static_cast<unsigned>(len));
        return false;
    }

    //一个引擎帧的所有分片须持锁连续发出，保证对端按序重组、不与其它引擎帧的分片交错
    std::lock_guard<std::mutex> guard(m_sendMtx);
    if (!m_connected.load())
        return false;
    for (size_t i = 0; i < frames.size(); ++i)
    {
        if (!SendAll(frames[i].data(), frames[i].size()))
        {
            LOG_ERROR("RdRelayClient: send engine chunk %u/%u failed.",
                      static_cast<unsigned>(i + 1), static_cast<unsigned>(frames.size()));
            return false;
        }
    }
    return true;
}

void RdRelayClient::RecvProc()
{
    while (!m_stopping.load())
    {
        char buf[kRecvChunk];
        int n = ::recv(m_sock, buf, sizeof(buf), 0);
        if (n > 0)
        {
            m_recvBuf.append(buf, static_cast<size_t>(n));
            std::vector<RdRelayInboundFrame> frames;
            bool err = false;
            size_t used = relayframing::DrainFrames(
                reinterpret_cast<const unsigned char*>(m_recvBuf.data()), m_recvBuf.size(), frames, err);
            m_recvBuf.erase(0, used);

            for (size_t i = 0; i < frames.size(); ++i)
            {
                //中转通道上业务期只会来 data 帧；data 帧包体为「分片标志 + 分片数据」，
                //喂入重组器，凑齐一个完整引擎帧后再交给上层
                if (frames[i].m_cmd != cmd_rd_relay_data)
                    continue;

                std::vector<unsigned char> engineFrame;
                if (m_reassembler.Feed(frames[i].m_body.data(), frames[i].m_body.size(), engineFrame)
                    && m_handler != NULL)
                    m_handler->OnRelayEngineFrame(engineFrame.data(), engineFrame.size());
            }

            if (err)
            {
                LOG_ERROR("RdRelayClient: recv frame protocol error, closing.");
                break;
            }
        }
        else if (n == 0)
        {
            LOG_INFO("RdRelayClient: relay peer closed connection.");
            break;
        }
        else
        {
            int e = ::WSAGetLastError();
            if (e == WSAETIMEDOUT || e == WSAEWOULDBLOCK)
                continue;   //周期性超时，回去检查停止标志

            LOG_ERROR("RdRelayClient: recv error %d, closing.", e);
            break;
        }
    }

    //异常断开（非主动 Close）时回调一次 OnRelayClosed，便于上层收尾 / 提示
    bool wasStopping = m_stopping.load();
    m_connected.store(false);
    if (!wasStopping && m_handler != NULL)
        m_handler->OnRelayClosed();
}

void RdRelayClient::Close()
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

bool RdRelayClient::IsConnected() const
{
    return m_connected.load();
}

} // namespace remotedesktop
