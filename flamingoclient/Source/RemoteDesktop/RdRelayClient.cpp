/**
 *  远程桌面中转客户端实现，RdRelayClient.cpp
 *  balloonwj@qq.com   2026-06-09
 */
#include "RdRelayClient.h"

#include <ws2tcpip.h>
#include <string.h>
#include "../net/Protocol.h"                      // cmd_rd_relay_*
#include "../net/RemoteDesktopProtocolClient.h"   // parseRelayHelloAck / RDRelayHelloAck
#include "../net/FrameCodec.h"                    // encodeFrame（接入加密后本文件要自己套帧头）
#include "../net/FramePayload.h"                  // encodeFramePayload / decodeFramePayload
#include "../net/HandshakeRunner.h"               // performClientHandshake / IHandshakeChannel
#include "../net/IUSocket.h"                      // CIUSocket::GetServerPublicKey
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

//relay 报到帧的序列号。与 relayframing::BuildHelloFrame 里用的那个保持一致——
//接入协议加密之后本文件要自己组这一帧（那个函数产出的是明文帧），序列号参与
//附加认证数据，两处必须相同。
static const uint32_t kRelayHelloSeq = 1;

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

/**
 *  握手期间的字节收发适配器：把 net::performClientHandshake 需要的收发转接到
 *  本连接的 socket 上。只在 Connect() 里活着，那时接收线程还没启动，不涉及并发。
 */
class RdRelayClient::HandshakeChannel : public net::IHandshakeChannel
{
public:
    //  owner：宿主连接，生命周期覆盖本对象；timeoutSec：收取超时，单位秒
    HandshakeChannel(RdRelayClient* owner, int timeoutSec)
        : m_owner(owner), m_timeoutSec(timeoutSec)
    {
    }

    virtual bool SendAll(const char* data, size_t len)
    {
        return m_owner->SendAll(data, len);
    }

    //收满 len 个字节。socket 此刻已被 DoConnect 切回阻塞模式并设了 SO_RCVTIMEO，
    //所以这里直接循环 recv 即可；超时会让 recv 返回错误，据此判失败。
    virtual bool RecvAll(char* buf, size_t len)
    {
        (void)m_timeoutSec;

        size_t got = 0;
        while (got < len)
        {
            const int n = ::recv(m_owner->m_sock, buf + got, (int)(len - got), 0);
            if (n > 0)
            {
                got += (size_t)n;
                continue;
            }
            //阻塞模式下超时会返回 WSAETIMEDOUT；握手是一来一回的短交互，
            //超时即视为对端没有按协议应答，不再续等。
            return false;
        }
        return true;
    }

private:
    RdRelayClient* m_owner;       //宿主连接，不拥有
    int            m_timeoutSec;  //收取超时，单位秒（socket 上已设，故仅作记录）
};

bool RdRelayClient::PerformHandshake(int timeoutSec)
{
    const unsigned char* pServerPubKey = CIUSocket::GetInstance().GetServerPublicKey();
    if (pServerPubKey == NULL)
    {
        //没配服务端公钥就是不加密，本连接全程明文
        return true;
    }

    HandshakeChannel channel(this, timeoutSec);

    std::string err;
    if (!net::performClientHandshake(channel, pServerPubKey, m_handshake, err))
    {
        LOG_ERROR("RdRelayClient: handshake failed: %s.", err.c_str());
        return false;
    }

    LOG_INFO("RdRelayClient: handshake done, relay traffic is now encrypted.");
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

    //协议加密握手：必须排在下面的 relay 报到帧之前——报到帧里带的是中转令牌，
    //明文发出去等于把凭据白送。此刻接收线程还没启动，同步收发不会与它抢数据。
    if (!PerformHandshake(timeoutSec))
    {
        ::closesocket(m_sock);
        m_sock = INVALID_SOCKET;
        return false;
    }

    //发 relay 报到帧。协议加密握手已在上面完成，故这一帧的包体要走加密——
    //它带着中转令牌，正是最该保护的那一帧。这里不能再用 relayframing::BuildHelloFrame，
    //那个产出的是明文帧。
    const std::string helloBody = net::RemoteDesktopProtocolClient::buildRelayHello(token);
    std::string helloWire;
    uint8_t     helloFlags = 0;
    uint32_t    helloOriginLen = 0;
    if (!net::encodeFramePayload(cmd_rd_relay_hello, kRelayHelloSeq, helloBody,
                                 &m_handshake.cipher(), 0, net::payload_compress_never,
                                 helloWire, helloFlags, helloOriginLen))
    {
        LOG_ERROR("RdRelayClient: encrypt hello failed, relay %s:%d.", ip.c_str(), port);
        ::closesocket(m_sock);
        m_sock = INVALID_SOCKET;
        return false;
    }

    std::string hello;
    if (!net::encodeFrame(cmd_rd_relay_hello, kRelayHelloSeq, helloFlags, helloWire.data(),
                          static_cast<uint32_t>(helloWire.size()), helloOriginLen, hello))
    {
        LOG_ERROR("RdRelayClient: encode hello failed, relay %s:%d.", ip.c_str(), port);
        ::closesocket(m_sock);
        m_sock = INVALID_SOCKET;
        return false;
    }

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
                    continue;   //报到阶段忽略非报到帧

                //报到应答的包体同样是加密的，先还原成明文再解析
                const std::string wire(reinterpret_cast<const char*>(frames[i].m_body.data()),
                                       frames[i].m_body.size());
                std::string body;
                std::string decErr;
                if (!net::decodeFramePayload(frames[i].m_cmd, frames[i].m_seq, frames[i].m_flags,
                                             frames[i].m_originlen, wire, &m_handshake.cipher(),
                                             body, decErr))
                {
                    LOG_ERROR("RdRelayClient: decrypt hello ack failed: %s.", decErr.c_str());
                    gotAck = true;   //跳出等待循环，下面会因 code != 0 判失败
                    break;
                }

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

    //按分片大小切成一组数据帧的包体（小帧为单个 whole 包体）。切分本身与顺序无关，
    //可以在锁外做；加密不行，见下面的说明。
    std::vector<std::string> bodies;
    if (!relayframing::BuildEngineBodies(data, len, kRelayChunkSize, bodies))
    {
        LOG_ERROR("RdRelayClient: build engine frame bodies failed, len=%u.",
                  static_cast<unsigned>(len));
        return false;
    }

    //一个引擎帧的所有分片须持锁连续发出，保证对端按序重组、不与其它引擎帧的分片交错。
    //<u>加密也必须在这把锁之内做</u>：流加密按调用顺序推进内部计数器，本类的发送方
    //有两条线程（被控端的推流线程、控制端的 UI 线程），若在锁外加密、锁内发送，
    //写到 socket 的先后就可能与加密的先后相反，服务端按流顺序解密必然失败并断开连接。
    //FrameCipher 内部那把锁只保证单次加密是原子的，保证不了这个顺序。
    std::lock_guard<std::mutex> guard(m_sendMtx);
    if (!m_connected.load())
        return false;

    for (size_t i = 0; i < bodies.size(); ++i)
    {
        //中转数据帧不压缩：屏幕帧本身已由编码器压过，再走一遍 zlib 压不动。
        //服务端也不解压，压缩标志与 originlen 原样透传到对端客户端。
        std::string wire;
        uint8_t     flags = 0;
        uint32_t    originlen = 0;
        if (!net::encodeFramePayload(cmd_rd_relay_data, 0, bodies[i], &m_handshake.cipher(),
                                     0, net::payload_compress_never, wire, flags, originlen))
        {
            LOG_ERROR("RdRelayClient: encrypt engine chunk %u/%u failed.",
                      static_cast<unsigned>(i + 1), static_cast<unsigned>(bodies.size()));
            return false;
        }

        std::string frame;
        if (!net::encodeFrame(cmd_rd_relay_data, 0, flags, wire.data(),
                              static_cast<uint32_t>(wire.size()), originlen, frame))
        {
            LOG_ERROR("RdRelayClient: encode engine chunk %u/%u failed.",
                      static_cast<unsigned>(i + 1), static_cast<unsigned>(bodies.size()));
            return false;
        }

        if (!SendAll(frame.data(), frame.size()))
        {
            LOG_ERROR("RdRelayClient: send engine chunk %u/%u failed.",
                      static_cast<unsigned>(i + 1), static_cast<unsigned>(bodies.size()));
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

                //先把线上包体还原成明文，再交给分片重组器。中转通道上服务端会用
                //各自的密钥重新加密，所以这里解的是「服务端到本端」这个方向的流。
                const std::string wire(reinterpret_cast<const char*>(frames[i].m_body.data()),
                                       frames[i].m_body.size());
                std::string plain;
                std::string decErr;
                if (!net::decodeFramePayload(frames[i].m_cmd, frames[i].m_seq, frames[i].m_flags,
                                             frames[i].m_originlen, wire, &m_handshake.cipher(),
                                             plain, decErr))
                {
                    //解密失败意味着这个方向的流已不可恢复，只能断开重来，
                    //不能跳过本帧继续解下一帧
                    LOG_ERROR("RdRelayClient: decrypt relay data failed: %s, closing.",
                              decErr.c_str());
                    err = true;
                    break;
                }

                std::vector<unsigned char> engineFrame;
                if (m_reassembler.Feed(reinterpret_cast<const unsigned char*>(plain.data()),
                                       plain.size(), engineFrame)
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
