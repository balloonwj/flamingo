/**
 *  FlamingoLink.dll —— 到管理端口的一条连接的实现，说明见 LinkConnection.h。
 *
 *  balloonwj@qq.com   2026-10-04
 */
#include "stdafx.h"
#include "FlamingoLink/Net/LinkConnection.h"

#include <ws2tcpip.h>
#include <vector>

#include "../../../flamingoclient/Source/net/FrameCodec.h"
#include "../../../flamingoclient/Source/net/FramePayload.h"

namespace flamingolink {

namespace {

//握手期间单次收发的超时（毫秒）：三帧都很小，服务端正常时几十毫秒内应答
const int kHandshakeIoTimeoutMs = 5000;

//连接等待的检查间隔（毫秒）：每隔这么久看一次是否被要求停止
const int kConnectPollSliceMs = 200;

//一次 recv 的缓冲大小（字节）
const int kRecvChunkBytes = 64 * 1024;

//一毫秒折合的微秒数（select 的超时以微秒计）
const long kMicrosecondsPerMs = 1000;

//一秒折合的毫秒数
const long kMsPerSecond = 1000;

//把毫秒数换成 select 用的 timeval
timeval MakeTimeval(int ms)
{
    timeval tv;
    tv.tv_sec = static_cast<long>(ms / kMsPerSecond);
    tv.tv_usec = static_cast<long>((ms % kMsPerSecond) * kMicrosecondsPerMs);
    return tv;
}

//把 WSA 错误码拼进说明文字
std::string DescribeWsa(const char* what)
{
    char buf[128] = { 0 };
    _snprintf_s(buf, sizeof(buf), _TRUNCATE, "%s failed, wsa error %d", what, ::WSAGetLastError());
    return std::string(buf);
}

} // anonymous namespace

LinkConnection::LinkConnection()
    : m_sock(INVALID_SOCKET)
    , m_encrypted(false)
{
}

LinkConnection::~LinkConnection()
{
    Close();
}

bool LinkConnection::Connect(const std::wstring& host, int port, int timeoutMs,
                             const std::atomic<bool>& stop, std::string& err)
{
    err.clear();
    Close();

    //解析主机名（只取 IPv4，与服务端 InetAddress 一致）
    wchar_t portText[16] = { 0 };
    _itow_s(port, portText, 10);
    ADDRINFOW hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    ADDRINFOW* result = NULL;
    if (::GetAddrInfoW(host.c_str(), portText, &hints, &result) != 0 || result == NULL)
    {
        err = DescribeWsa("resolve host");
        return false;
    }

    m_sock = ::socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (m_sock == INVALID_SOCKET)
    {
        ::FreeAddrInfoW(result);
        err = DescribeWsa("socket");
        return false;
    }

    //非阻塞方式发起连接，再分片等待，期间可以响应停止请求
    u_long nonBlocking = 1;
    ::ioctlsocket(m_sock, FIONBIO, &nonBlocking);
    const int rc = ::connect(m_sock, result->ai_addr, static_cast<int>(result->ai_addrlen));
    ::FreeAddrInfoW(result);
    if (rc == SOCKET_ERROR && ::WSAGetLastError() != WSAEWOULDBLOCK)
    {
        err = DescribeWsa("connect");
        Close();
        return false;
    }

    bool connected = (rc == 0);
    int waited = 0;
    while (!connected)
    {
        if (stop.load())
        {
            err = "stopped while connecting";
            Close();
            return false;
        }
        if (waited >= timeoutMs)
        {
            err = "connect timeout";
            Close();
            return false;
        }

        fd_set writeSet;
        fd_set errorSet;
        FD_ZERO(&writeSet);
        FD_ZERO(&errorSet);
        FD_SET(m_sock, &writeSet);
        FD_SET(m_sock, &errorSet);
        timeval tv = MakeTimeval(kConnectPollSliceMs);
        const int n = ::select(0, NULL, &writeSet, &errorSet, &tv);
        if (n == SOCKET_ERROR)
        {
            err = DescribeWsa("select on connect");
            Close();
            return false;
        }
        if (n > 0)
        {
            if (FD_ISSET(m_sock, &errorSet))
            {
                int soErr = 0;
                int soLen = sizeof(soErr);
                ::getsockopt(m_sock, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&soErr), &soLen);
                char buf[64] = { 0 };
                _snprintf_s(buf, sizeof(buf), _TRUNCATE, "connect refused, socket error %d", soErr);
                err = buf;
                Close();
                return false;
            }
            connected = FD_ISSET(m_sock, &writeSet) != 0;
        }
        waited += kConnectPollSliceMs;
    }

    //连上之后切回阻塞模式：握手的三帧按「收满 / 发完」同步进行，之后的收取先 select 再 recv，不会卡住
    u_long blocking = 0;
    ::ioctlsocket(m_sock, FIONBIO, &blocking);
    const DWORD ioTimeout = static_cast<DWORD>(kHandshakeIoTimeoutMs);
    ::setsockopt(m_sock, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&ioTimeout), sizeof(ioTimeout));
    ::setsockopt(m_sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&ioTimeout), sizeof(ioTimeout));
    BOOL noDelay = TRUE;
    ::setsockopt(m_sock, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&noDelay), sizeof(noDelay));
    return true;
}

bool LinkConnection::Handshake(const unsigned char* serverPubKey, std::string& err)
{
    err.clear();
    if (m_sock == INVALID_SOCKET)
    {
        err = "not connected";
        return false;
    }

    if (!net::performClientHandshake(*this, serverPubKey, m_hs, err))
        return false;

    m_encrypted = true;
    return true;
}

bool LinkConnection::SendFrame(uint16_t cmd, uint32_t seq, const std::string& body, std::string& err)
{
    err.clear();
    if (m_sock == INVALID_SOCKET)
    {
        err = "not connected";
        return false;
    }

    //先压缩（管理端口的请求都很小，一律不压缩）、后加密
    std::string wire;
    uint8_t flags = 0;
    uint32_t originlen = 0;
    net::FrameCipher* cipher = m_encrypted ? &m_hs.cipher() : NULL;
    if (!net::encodeFramePayload(cmd, seq, body, cipher, 0, net::payload_compress_never, wire, flags, originlen))
    {
        err = "encode frame payload failed";
        return false;
    }

    std::string frame;
    if (!net::encodeFrame(cmd, seq, flags, wire.data(), static_cast<uint32_t>(wire.size()), originlen, frame))
    {
        err = "encode frame failed";
        return false;
    }

    if (!SendAll(frame.data(), frame.size()))
    {
        err = DescribeWsa("send");
        return false;
    }
    return true;
}

int LinkConnection::PollRecv(int timeoutMs, std::string& err)
{
    err.clear();
    if (m_sock == INVALID_SOCKET)
    {
        err = "not connected";
        return -1;
    }

    fd_set readSet;
    FD_ZERO(&readSet);
    FD_SET(m_sock, &readSet);
    timeval tv = MakeTimeval(timeoutMs);
    const int n = ::select(0, &readSet, NULL, NULL, &tv);
    if (n == SOCKET_ERROR)
    {
        err = DescribeWsa("select on recv");
        return -1;
    }
    if (n == 0)
        return 0;

    std::vector<char> chunk(kRecvChunkBytes);
    const int got = ::recv(m_sock, &chunk[0], kRecvChunkBytes, 0);
    if (got == 0)
    {
        err = "connection closed by server";
        return -1;
    }
    if (got < 0)
    {
        err = DescribeWsa("recv");
        return -1;
    }

    m_inbuf.append(&chunk[0], static_cast<size_t>(got));
    return 1;
}

int LinkConnection::NextFrame(uint16_t& cmd, uint32_t& seq, std::string& body, std::string& err)
{
    err.clear();
    body.clear();
    if (m_inbuf.empty())
        return 0;

    frame_header header;
    std::string wire;
    size_t consumed = 0;
    const net::frame_decode_result r = net::decodeFrame(m_inbuf.data(), m_inbuf.size(), header, wire, consumed);
    if (r == net::frame_decode_need_more)
        return 0;
    if (r == net::frame_decode_error)
    {
        err = "decode frame failed";
        return -1;
    }
    m_inbuf.erase(0, consumed);

    net::FrameCipher* cipher = m_encrypted ? &m_hs.cipher() : NULL;
    if (!net::decodeFramePayload(header.cmd, header.seq, header.flags, header.originlen, wire, cipher, body, err))
        return -1;

    cmd = header.cmd;
    seq = header.seq;
    return 1;
}

void LinkConnection::Close()
{
    if (m_sock != INVALID_SOCKET)
    {
        ::shutdown(m_sock, SD_BOTH);
        ::closesocket(m_sock);
        m_sock = INVALID_SOCKET;
    }
}

bool LinkConnection::SendAll(const char* data, size_t len)
{
    size_t sent = 0;
    while (sent < len)
    {
        const int n = ::send(m_sock, data + sent, static_cast<int>(len - sent), 0);
        if (n <= 0)
            return false;
        sent += static_cast<size_t>(n);
    }
    return true;
}

bool LinkConnection::RecvAll(char* buf, size_t len)
{
    size_t got = 0;
    while (got < len)
    {
        const int n = ::recv(m_sock, buf + got, static_cast<int>(len - got), 0);
        if (n <= 0)
            return false;
        got += static_cast<size_t>(n);
    }
    return true;
}

} // namespace flamingolink
