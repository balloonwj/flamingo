/**
 *  音视频通话中转客户端实现，CallRelayClient.cpp
 *  balloonwj@qq.com   2026-06-11
 */
#include "CallRelayClient.h"

#include <ws2tcpip.h>
#include <string.h>
#include "../../Source/net/Protocol.h"            // cmd_av_relay_* / MAX_BODY_FILESERVER
#include "../../Source/net/FrameCodec.h"          // net::encodeFrame / decodeFrame
#include "CallProtocolClient.h"  // buildRelayHello / parseRelayHelloAck / CallRelayHelloAck
#include "../../Source/net/FramePayload.h"        // encodeFramePayload / decodeFramePayload
#include "../../Source/net/HandshakeRunner.h"     // performClientHandshake / IHandshakeChannel
#include "../../Source/Common/Log/IULog.h"                   // LOG_INFO / LOG_ERROR

#pragma comment(lib, "ws2_32.lib")

namespace call {

//接收线程单次阻塞 recv 的超时（毫秒）：周期性返回以检查主动关闭标志，避免 Close 时久等。
static const int kRecvTimeoutMs = 500;
//发送超时（毫秒）：单帧发不出去超过此时长即判连接异常。
static const int kSendTimeoutMs = 5000;
//接收线程单次读取块大小（字节）。
static const int kRecvChunk = 64 * 1024;
//每秒的毫秒数：把握手超时（秒）换算成截止时刻（毫秒）
static const ULONGLONG kMsPerSecond = 1000;

CallRelayClient::CallRelayClient()
    : m_sock(INVALID_SOCKET)
    , m_connected(false)
    , m_stopping(false)
    , m_handler(NULL)
    , m_hasServerPubKey(false)
{
    memset(m_serverPubKey, 0, sizeof(m_serverPubKey));
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

/**
 *  握手期间的字节收发适配器：把 net::performClientHandshake 需要的收发转接到
 *  本连接的 socket 上。只在 Connect() 里活着，那时接收线程还没启动，不涉及并发。
 */
class CallRelayClient::HandshakeChannel : public net::IHandshakeChannel
{
public:
    //  owner：宿主连接，生命周期覆盖本对象；timeoutSec：握手允许的总时长，单位秒
    HandshakeChannel(CallRelayClient* owner, int timeoutSec)
        : m_owner(owner)
        , m_deadlineMs(::GetTickCount64() + static_cast<ULONGLONG>(timeoutSec) * kMsPerSecond)
    {
    }

    virtual bool SendAll(const char* data, size_t len)
    {
        return m_owner->SendAll(data, len);
    }

    //收满 len 个字节。socket 此刻已被 DoConnect 切回阻塞模式并设了 SO_RCVTIMEO（kRecvTimeoutMs）。
    //2026-10-06 修正：那个 500 毫秒的接收超时是给接收线程定期检查停止标志用的，比握手允许的时长短得多。原先这里
    //第一次超时就判失败，握手往返超过 500 毫秒（网络很差、跨境线路）时通话连不上中转，双方都没有声音
    //（弱网校准用例 CRL2 在注入约 500 毫秒往返时实测复现，bugs.md BUG-109）。现在超时后只要总时限未到、
    //也没有被要求停止就继续等；连接关闭或出错仍立即失败。
    virtual bool RecvAll(char* buf, size_t len)
    {
        size_t got = 0;
        while (got < len)
        {
            const int n = ::recv(m_owner->m_sock, buf + got, (int)(len - got), 0);
            if (n > 0)
            {
                got += (size_t)n;
                continue;
            }
            const bool timedOut = (n < 0 && ::WSAGetLastError() == WSAETIMEDOUT);
            if (timedOut && !m_owner->m_stopping.load() && ::GetTickCount64() < m_deadlineMs)
                continue;
            return false;
        }
        return true;
    }

private:
    CallRelayClient* m_owner;        //宿主连接，不拥有
    ULONGLONG        m_deadlineMs;   //握手的截止时刻（GetTickCount64，毫秒），超过即判失败
};

bool CallRelayClient::PerformHandshake(int timeoutSec)
{
    const unsigned char* pServerPubKey = m_hasServerPubKey ? m_serverPubKey : NULL;
    if (pServerPubKey == NULL)
    {
        //没配服务端公钥就是不加密，本连接全程明文
        return true;
    }

    HandshakeChannel channel(this, timeoutSec);

    std::string err;
    if (!net::performClientHandshake(channel, pServerPubKey, m_handshake, err))
    {
        LOG_ERROR("CallRelayClient: handshake failed: %s.", err.c_str());
        return false;
    }

    LOG_INFO("CallRelayClient: handshake done, relay traffic is now encrypted.");
    return true;
}

bool CallRelayClient::Connect(const std::string& ip, int port, const std::string& token,
                              const unsigned char* serverPubKey, CallRelayClientHandler* handler, int timeoutSec)
{
    if (handler == NULL)
        return false;
    if (m_connected.load())
        return true;

    //复制服务端公钥：调用方的指针只保证在本次调用期间有效
    m_hasServerPubKey = (serverPubKey != NULL);
    if (m_hasServerPubKey)
        memcpy(m_serverPubKey, serverPubKey, sizeof(m_serverPubKey));
    else
        memset(m_serverPubKey, 0, sizeof(m_serverPubKey));

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

    //协议加密握手：必须排在下面的 relay 报到帧之前——报到帧里带的是中转令牌，
    //明文发出去等于把凭据白送。此刻接收线程还没启动，同步收发不会与它抢数据。
    if (!PerformHandshake(timeoutSec))
    {
        ::closesocket(m_sock);
        m_sock = INVALID_SOCKET;
        return false;
    }

    //发报到帧（cmd_av_relay_hello，包体 {"token":"..."}）。包体走加密。
    const std::string helloBody = net::CallProtocolClient::buildRelayHello(token);
    std::string helloWire;
    uint8_t     helloFlags = 0;
    uint32_t    helloOriginLen = 0;
    if (!net::encodeFramePayload(cmd_av_relay_hello, 0, helloBody, &m_handshake.cipher(),
                                 0, net::payload_compress_never,
                                 helloWire, helloFlags, helloOriginLen))
    {
        LOG_ERROR("CallRelayClient: encrypt hello failed, relay %s:%d.", ip.c_str(), port);
        ::closesocket(m_sock);
        m_sock = INVALID_SOCKET;
        return false;
    }

    std::string hello;
    if (!net::encodeFrame(cmd_av_relay_hello, 0, helloFlags, helloWire.data(),
                          static_cast<uint32_t>(helloWire.size()), helloOriginLen, hello))
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
                    //报到应答的包体同样是加密的，先还原成明文再解析
                    std::string plain;
                    std::string decErr;
                    if (!net::decodeFramePayload(h.cmd, h.seq, h.flags, h.originlen, body,
                                                 &m_handshake.cipher(), plain, decErr))
                    {
                        LOG_ERROR("CallRelayClient: decrypt hello ack failed: %s.",
                                  decErr.c_str());
                        gotAck = true;   //跳出等待循环，下面会因 code != 0 判失败
                        code = -1;
                        break;
                    }

                    net::CallRelayHelloAck ackobj;
                    if (net::CallProtocolClient::parseRelayHelloAck(plain, ackobj))
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

    const std::string body(reinterpret_cast<const char*>(data), len);

    //<u>加密必须在发送锁之内做</u>：本类的发送方有两条线程（音频采集线程与视频采集
    //线程，见 CallMediaSession 的 OnEncodedAudio / OnEncodedVideo），流加密按调用
    //顺序推进内部计数器，若在锁外加密、锁内发送，写到 socket 的先后就可能与加密的
    //先后相反，服务端按流顺序解密必然失败并断开连接。FrameCipher 内部那把锁只保证
    //单次加密是原子的，保证不了这个顺序。
    std::lock_guard<std::mutex> guard(m_sendMtx);
    if (!m_connected.load())
        return false;

    //中转数据帧不压缩：音视频帧本身已由编码器压过，再走一遍 zlib 压不动。
    //服务端也不解压，压缩标志与 originlen 原样透传到对端客户端。
    std::string wire;
    uint8_t     flags = 0;
    uint32_t    originlen = 0;
    if (!net::encodeFramePayload(cmd_av_relay_data, 0, body, &m_handshake.cipher(), 0,
                                 net::payload_compress_never, wire, flags, originlen))
    {
        LOG_ERROR("CallRelayClient: encrypt data frame failed, len=%u.",
                  static_cast<unsigned>(len));
        return false;
    }

    //封成单个 cmd_av_relay_data 帧（音频帧小，不分片）
    std::string frame;
    if (!net::encodeFrame(cmd_av_relay_data, 0, flags, wire.data(),
                          static_cast<uint32_t>(wire.size()), originlen, frame))
    {
        LOG_ERROR("CallRelayClient: encode data frame failed, len=%u.", static_cast<unsigned>(len));
        return false;
    }

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
                if (h.cmd == cmd_av_relay_data)
                {
                    //先把线上包体还原成明文再交给上层。中转通道上服务端会用各自的
                    //密钥重新加密，所以这里解的是「服务端到本端」这个方向的流。
                    std::string plain;
                    std::string decErr;
                    if (!net::decodeFramePayload(h.cmd, h.seq, h.flags, h.originlen, body,
                                                 &m_handshake.cipher(), plain, decErr))
                    {
                        //解密失败意味着这个方向的流已不可恢复，只能断开重来，
                        //不能跳过本帧继续解下一帧
                        LOG_ERROR("CallRelayClient: decrypt relay data failed: %s, closing.",
                                  decErr.c_str());
                        protoErr = true;
                        break;
                    }

                    if (m_handler != NULL)
                        m_handler->OnRelayData(
                            reinterpret_cast<const unsigned char*>(plain.data()), plain.size());
                }
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
