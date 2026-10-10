/**
 *  P2P 回落中转连接的实现，见 P2PRelayConn.h。
 *  balloonwj@qq.com   2026-10-04
 */
#include <winsock2.h>
#include <ws2tcpip.h>

#include "P2PRelayConn.h"

#include <string.h>
#include <chrono>

#include "../../Source/Common/Log/IULog.h"
#include "../../Source/net/FrameCodec.h"        //encodeFrame / decodeFrame
#include "../../Source/net/FramePayload.h"      //encodeFramePayload / decodeFramePayload
#include "../../Source/net/HandshakeRunner.h"   //performClientHandshake / IHandshakeChannel
#include "../../Source/net/Protocol.h"          //cmd_p2p_relay_hello / cmd_p2p_relay_data / MAX_BODY_FILESERVER
#include "../../Source/third-party/jsoncpp-1.9.0/json.h"
#include "P2PAdapters.h"                        //ResolveIpv4

#pragma comment(lib, "ws2_32.lib")

namespace p2p {

namespace {

//连接线程等待 connect 完成时每次 select 的时长（毫秒）：分段等待，期间检查关闭标志
const int kConnectPollMs = 100;

//socket 的接收超时（毫秒）：握手与报到阶段的 recv 按此周期返回，以便检查关闭标志与总时限；就绪之后只在 select
//报告可读时才 recv，不会等这么久
const int kRecvTimeoutMs = 200;

//socket 的发送超时（毫秒）：一帧写不出去超过此时长即判连接异常
const int kSendTimeoutMs = 5000;

//IO 线程一次读取的字节数
const int kRecvChunkBytes = 64 * 1024;

//报到阶段一次读取的字节数：报到应答是一条很短的 JSON
const int kHelloRecvChunkBytes = 4096;

//报到帧与数据帧的序号：中转连接上不做请求与应答的配对，固定取 0（参与逐跳加密的附加认证数据，两端按帧头重建）
const uint32_t kRelayFrameSeq = 0;

//报到应答里表示成功的结果码
const int kHelloAckOk = 0;

//单帧包体上限：imgserver 的中转端口与 fileserver 同为 2 MiB；链路报文远小于此
const uint32_t kRelayMaxBodyBytes = MAX_BODY_FILESERVER;

//毫秒与微秒的换算
const int kMicrosPerMilli = 1000;

//单调时钟的当前时间（毫秒）
int64_t NowMs()
{
    return (int64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

} // namespace

/**
 *  握手期间的收发适配器：把 net::performClientHandshake 需要的收发转到本连接的 socket 上。
 *  只在连接线程的握手期间存在。
 */
class P2PRelayConn::HandshakeChannel : public net::IHandshakeChannel
{
public:
    /**
     *  @param owner      所属连接（借用）
     *  @param deadlineMs 收发的最晚时刻（单调时钟毫秒）
     */
    HandshakeChannel(P2PRelayConn* owner, int64_t deadlineMs)
        : m_owner(owner)
        , m_deadlineMs(deadlineMs)
    {
    }

    //把 len 个字节全部发出
    virtual bool SendAll(const char* data, size_t len) override
    {
        return m_owner->SendAll(data, len);
    }

    //收满 len 个字节；超过最晚时刻或连接被关闭时返回 false
    virtual bool RecvAll(char* buf, size_t len) override
    {
        return m_owner->RecvAll(buf, len, m_deadlineMs);
    }

private:
    P2PRelayConn* m_owner;        //所属连接（借用）
    int64_t       m_deadlineMs;   //收发的最晚时刻（毫秒）
};

P2PRelayConn::P2PRelayConn()
    : m_port(0)
    , m_hasServerPubKey(false)
    , m_waker(NULL)
    , m_socket((uintptr_t)INVALID_SOCKET)
    , m_state(p2p_relay_idle)
    , m_stopping(false)
    , m_outFrontOffset(0)
    , m_outBytes(0)
{
    memset(m_serverPubKey, 0, sizeof(m_serverPubKey));
}

P2PRelayConn::~P2PRelayConn()
{
    Close();
}

bool P2PRelayConn::Start(const std::string& host, uint16_t port, const std::string& token,
                         const unsigned char* serverPubKey, IP2PRelayWaker* waker)
{
    if (m_state != p2p_relay_idle)
        return false;
    m_host = host;
    m_port = port;
    m_token = token;
    m_waker = waker;
    //公钥复制一份：调用方给的指针只保证在本次调用期间有效
    m_hasServerPubKey = (serverPubKey != NULL);
    if (m_hasServerPubKey)
        memcpy(m_serverPubKey, serverPubKey, sizeof(m_serverPubKey));
    m_state = p2p_relay_connecting;
    try
    {
        m_thread = std::thread(&P2PRelayConn::ConnectProc, this);
    }
    catch (...)
    {
        LOG_ERROR("p2p relay: cannot start connect thread for %s:%u.", host.c_str(), (unsigned)port);
        m_state = p2p_relay_failed;
        return false;
    }
    return true;
}

int P2PRelayConn::State() const
{
    return m_state;
}

uintptr_t P2PRelayConn::Handle() const
{
    return (m_state == p2p_relay_ready) ? m_socket : (uintptr_t)INVALID_SOCKET;
}

void P2PRelayConn::ConnectProc()
{
    const int64_t deadline = NowMs() + kP2PRelayConnectTimeoutMs;
    LOG_INFO("p2p relay: connecting %s:%u, encrypted %d.", m_host.c_str(), (unsigned)m_port, m_hasServerPubKey ? 1 : 0);
    bool ok = DoConnect(deadline);
    if (!ok)
    {
        LOG_WARNING("p2p relay: connect %s:%u failed.", m_host.c_str(), (unsigned)m_port);
    }
    //协议加密握手：必须在报到之前，报到帧带着中转令牌
    if (ok && m_hasServerPubKey)
    {
        HandshakeChannel channel(this, deadline);
        std::string err;
        ok = net::performClientHandshake(channel, m_serverPubKey, m_handshake, err);
        if (!ok)
            LOG_WARNING("p2p relay: handshake with %s:%u failed: %s", m_host.c_str(), (unsigned)m_port, err.c_str());
    }
    if (ok)
        ok = SendHelloAndWaitAck(deadline);
    if (m_stopping)
        ok = false;
    //报到之后改为非阻塞：发送不在业务方或 IO 线程上阻塞，收包只在 select 报告可读之后进行
    if (ok)
    {
        u_long nonBlocking = 1;
        ok = (::ioctlsocket((SOCKET)m_socket, FIONBIO, &nonBlocking) == 0);
    }

    m_state = ok ? p2p_relay_ready : p2p_relay_failed;
    LOG_INFO("p2p relay: %s:%u %s.", m_host.c_str(), (unsigned)m_port, ok ? "ready" : "failed");
    if (m_waker != NULL)
        m_waker->WakeIo();
}

bool P2PRelayConn::DoConnect(int64_t deadlineMs)
{
    uint32_t ip = 0;
    if (!ResolveIpv4(m_host, ip))
    {
        LOG_WARNING("p2p relay: cannot resolve %s.", m_host.c_str());
        return false;
    }
    SOCKET s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET)
        return false;
    m_socket = (uintptr_t)s;

    BOOL noDelay = TRUE;
    ::setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&noDelay), sizeof(noDelay));
    u_long nonBlocking = 1;
    if (::ioctlsocket(s, FIONBIO, &nonBlocking) != 0)
        return false;

    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(ip);
    addr.sin_port = htons(m_port);
    if (::connect(s, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) != 0
        && ::WSAGetLastError() != WSAEWOULDBLOCK)
        return false;

    //分段等待连接完成：可写表示连上，异常集合表示连接被拒
    bool connected = false;
    while (!m_stopping && NowMs() < deadlineMs)
    {
        fd_set writable;
        fd_set failed;
        FD_ZERO(&writable);
        FD_ZERO(&failed);
        FD_SET(s, &writable);
        FD_SET(s, &failed);
        timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = kConnectPollMs * kMicrosPerMilli;
        const int n = ::select(0, NULL, &writable, &failed, &tv);
        if (n < 0 || FD_ISSET(s, &failed))
            return false;
        if (FD_ISSET(s, &writable))
        {
            connected = true;
            break;
        }
    }
    if (!connected)
        return false;

    //连上后切回阻塞模式并设收发超时：握手与报到按周期返回以检查关闭标志；就绪之后 IO 线程只在可读时 recv
    u_long blocking = 0;
    ::ioctlsocket(s, FIONBIO, &blocking);
    DWORD recvTimeout = kRecvTimeoutMs;
    DWORD sendTimeout = kSendTimeoutMs;
    ::setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&recvTimeout), sizeof(recvTimeout));
    ::setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&sendTimeout), sizeof(sendTimeout));
    return true;
}

bool P2PRelayConn::SendAll(const char* data, size_t len)
{
    size_t sent = 0;
    while (sent < len)
    {
        const int n = ::send((SOCKET)m_socket, data + sent, (int)(len - sent), 0);
        if (n <= 0)
            return false;
        sent += (size_t)n;
    }
    return true;
}

bool P2PRelayConn::RecvAll(char* buf, size_t len, int64_t deadlineMs)
{
    size_t got = 0;
    while (got < len)
    {
        if (m_stopping || NowMs() >= deadlineMs)
            return false;
        const int n = ::recv((SOCKET)m_socket, buf + got, (int)(len - got), 0);
        if (n > 0)
        {
            got += (size_t)n;
            continue;
        }
        if (n == 0)
            return false;
        //接收超时只是周期返回，回到循环开头检查关闭标志与总时限
        if (::WSAGetLastError() != WSAETIMEDOUT)
            return false;
    }
    return true;
}

bool P2PRelayConn::SendHelloAndWaitAck(int64_t deadlineMs)
{
    //报到帧经逐跳加密发出（握手完成时；没有公钥时 cipher 未就绪，包体为明文）
    Json::Value hello;
    hello["token"] = m_token;
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    const std::string body = Json::writeString(wb, hello);
    std::string wire;
    uint8_t flags = 0;
    uint32_t originLen = 0;
    if (!net::encodeFramePayload(cmd_p2p_relay_hello, kRelayFrameSeq, body, &m_handshake.cipher(), 0,
                                 net::payload_compress_never, wire, flags, originLen))
        return false;
    std::string frame;
    if (!net::encodeFrame(cmd_p2p_relay_hello, kRelayFrameSeq, flags, wire.data(), (uint32_t)wire.size(), originLen, frame)
        || !SendAll(frame.data(), frame.size()))
    {
        LOG_WARNING("p2p relay: send hello to %s:%u failed.", m_host.c_str(), (unsigned)m_port);
        return false;
    }

    while (!m_stopping && NowMs() < deadlineMs)
    {
        char buf[kHelloRecvChunkBytes];
        const int n = ::recv((SOCKET)m_socket, buf, sizeof(buf), 0);
        if (n == 0)
        {
            LOG_WARNING("p2p relay: %s:%u closed the connection before hello ack.", m_host.c_str(), (unsigned)m_port);
            return false;
        }
        if (n < 0)
        {
            if (::WSAGetLastError() == WSAETIMEDOUT)
                continue;
            return false;
        }
        m_recvBuf.append(buf, (size_t)n);
        frame_header header;
        std::string ackWire;
        size_t consumed = 0;
        const net::frame_decode_result r = net::decodeFrame(m_recvBuf.data(), m_recvBuf.size(), header, ackWire, consumed,
                                                            kRelayMaxBodyBytes);
        if (r == net::frame_decode_need_more)
            continue;
        if (r == net::frame_decode_error || header.cmd != cmd_p2p_relay_hello)
        {
            LOG_WARNING("p2p relay: unexpected frame (cmd %u) while waiting hello ack.", (unsigned)header.cmd);
            return false;
        }
        m_recvBuf.erase(0, consumed);
        std::string plain;
        std::string err;
        if (!net::decodeFramePayload(header.cmd, header.seq, header.flags, header.originlen, ackWire, &m_handshake.cipher(),
                                     plain, err))
        {
            LOG_WARNING("p2p relay: decrypt hello ack failed: %s", err.c_str());
            return false;
        }
        Json::CharReaderBuilder rb;
        Json::Value ack;
        JSONCPP_STRING errs;
        Json::CharReader* reader = rb.newCharReader();
        const bool parsed = reader->parse(plain.data(), plain.data() + plain.size(), &ack, &errs);
        delete reader;
        const int code = (parsed && ack.isObject() && ack["code"].isInt()) ? ack["code"].asInt() : -1;
        if (code != kHelloAckOk)
        {
            LOG_WARNING("p2p relay: hello rejected by %s:%u, code %d.", m_host.c_str(), (unsigned)m_port, code);
            return false;
        }
        return true;
    }
    LOG_WARNING("p2p relay: no hello ack from %s:%u in time.", m_host.c_str(), (unsigned)m_port);
    return false;
}

bool P2PRelayConn::ReadPackets(std::vector<std::string>& packets)
{
    if (m_state != p2p_relay_ready)
        return false;
    char buf[kRecvChunkBytes];
    const int n = ::recv((SOCKET)m_socket, buf, sizeof(buf), 0);
    if (n == 0)
    {
        LOG_INFO("p2p relay: %s:%u closed the connection.", m_host.c_str(), (unsigned)m_port);
        m_state = p2p_relay_closed;
        return false;
    }
    if (n < 0)
    {
        const int err = ::WSAGetLastError();
        if (err == WSAEWOULDBLOCK || err == WSAETIMEDOUT)
            return true;
        LOG_WARNING("p2p relay: recv from %s:%u failed, error %d.", m_host.c_str(), (unsigned)m_port, err);
        m_state = p2p_relay_closed;
        return false;
    }
    m_recvBuf.append(buf, (size_t)n);
    while (true)
    {
        frame_header header;
        std::string wire;
        size_t consumed = 0;
        const net::frame_decode_result r = net::decodeFrame(m_recvBuf.data(), m_recvBuf.size(), header, wire, consumed,
                                                            kRelayMaxBodyBytes);
        if (r == net::frame_decode_need_more)
            return true;
        if (r == net::frame_decode_error)
        {
            LOG_WARNING("p2p relay: bad frame from %s:%u, closing.", m_host.c_str(), (unsigned)m_port);
            m_state = p2p_relay_closed;
            return false;
        }
        m_recvBuf.erase(0, consumed);
        //解密失败时这个方向的流状态已经对不上，只能断开，不能跳过本帧
        std::string plain;
        std::string err;
        if (!net::decodeFramePayload(header.cmd, header.seq, header.flags, header.originlen, wire, &m_handshake.cipher(),
                                     plain, err))
        {
            LOG_WARNING("p2p relay: decrypt frame from %s:%u failed: %s", m_host.c_str(), (unsigned)m_port, err.c_str());
            m_state = p2p_relay_closed;
            return false;
        }
        //就绪之后只该收到 1862；别的命令记日志后忽略，不断开
        if (header.cmd != cmd_p2p_relay_data)
        {
            LOG_WARNING("p2p relay: unexpected cmd %u ignored.", (unsigned)header.cmd);
            continue;
        }
        packets.push_back(plain);
    }
}

bool P2PRelayConn::SendTo(uint32_t ip, uint16_t port, const uint8_t* data, size_t len)
{
    (void)ip;
    (void)port;
    if (m_state != p2p_relay_ready || (data == NULL && len > 0))
        return false;
    const std::string body(reinterpret_cast<const char*>(data), len);
    //加密与入缓冲都在锁内：流加密按调用顺序推进计数器，写出的先后必须与加密的先后一致
    std::lock_guard<std::mutex> guard(m_sendMutex);
    if (m_outBytes + len > kP2PRelayOutHardLimitBytes)
        return false;
    std::string wire;
    uint8_t flags = 0;
    uint32_t originLen = 0;
    if (!net::encodeFramePayload(cmd_p2p_relay_data, kRelayFrameSeq, body, &m_handshake.cipher(), 0,
                                 net::payload_compress_never, wire, flags, originLen))
        return false;
    std::string frame;
    if (!net::encodeFrame(cmd_p2p_relay_data, kRelayFrameSeq, flags, wire.data(), (uint32_t)wire.size(), originLen, frame))
        return false;
    const bool wasEmpty = m_outQueue.empty();
    m_outBytes += frame.size();
    m_outQueue.push_back(std::string());
    m_outQueue.back().swap(frame);
    if (!FlushLocked())
    {
        m_state = p2p_relay_closed;
        return false;
    }
    //缓冲由空变为有积压：IO 线程此刻的 select 可能没有等这个 socket 可写，唤醒它重新计算
    if (wasEmpty && !m_outQueue.empty() && m_waker != NULL)
        m_waker->WakeIo();
    return true;
}

size_t P2PRelayConn::QueuedBytes() const
{
    return m_outBytes;
}

bool P2PRelayConn::Flush()
{
    std::lock_guard<std::mutex> guard(m_sendMutex);
    if (m_state != p2p_relay_ready)
        return false;
    if (!FlushLocked())
    {
        LOG_WARNING("p2p relay: send to %s:%u failed, error %d.", m_host.c_str(), (unsigned)m_port, ::WSAGetLastError());
        m_state = p2p_relay_closed;
        return false;
    }
    return true;
}

bool P2PRelayConn::FlushLocked()
{
    while (!m_outQueue.empty())
    {
        const std::string& front = m_outQueue.front();
        const int n = ::send((SOCKET)m_socket, front.data() + m_outFrontOffset, (int)(front.size() - m_outFrontOffset), 0);
        if (n < 0)
            return ::WSAGetLastError() == WSAEWOULDBLOCK;
        m_outFrontOffset += (size_t)n;
        m_outBytes -= (size_t)n;
        if (m_outFrontOffset == front.size())
        {
            m_outQueue.pop_front();
            m_outFrontOffset = 0;
        }
    }
    return true;
}

void P2PRelayConn::Close()
{
    m_stopping = true;
    if (m_state == p2p_relay_ready || m_state == p2p_relay_connecting)
        m_state = p2p_relay_closed;
    //shutdown 使连接线程或别的线程里阻塞中的 recv / send 立即返回；句柄要等连接线程结束后再关闭
    if ((SOCKET)m_socket != INVALID_SOCKET)
        ::shutdown((SOCKET)m_socket, SD_BOTH);
    if (m_thread.joinable())
        m_thread.join();
    //连接线程可能在上面置为已关闭之后才写下「就绪」：线程结束后再统一改成终态
    if (m_state != p2p_relay_failed && m_state != p2p_relay_idle)
        m_state = p2p_relay_closed;
    std::lock_guard<std::mutex> guard(m_sendMutex);
    if ((SOCKET)m_socket != INVALID_SOCKET)
    {
        ::closesocket((SOCKET)m_socket);
        m_socket = (uintptr_t)INVALID_SOCKET;
    }
}

} // namespace p2p
