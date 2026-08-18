/**
 *  文件夹传输中转通道客户端实现，FolderRelayClient.cpp
 *
 *  实现结构照 RemoteDesktop/RdRelayClient.cpp 与 Call/CallRelayClient.cpp 而来，三者
 *  的连接、加密握手、报到、接收线程与关闭流程一致，本文件只在"报到帧多带一个 lane
 *  序号"和"数据帧用文件夹传输的 cmd"两处不同。
 *
 *  balloonwj@qq.com   2026-08-13
 */
#include "FolderRelayClient.h"

#include <ws2tcpip.h>
#include <string.h>
#include "../../net/Protocol.h"                      // cmd_folder_relay_* / MAX_BODY_FILESERVER
#include "../../net/FrameCodec.h"                    // encodeFrame / decodeFrame
#include "../../net/FramePayload.h"                  // encodeFramePayload / decodeFramePayload
#include "../../net/HandshakeRunner.h"               // performClientHandshake / IHandshakeChannel
#include "../../net/RemoteDesktopProtocolClient.h"   // parseRelayHelloAck（报到应答与远程桌面同结构）
#include "../../net/IUSocket.h"                      // CIUSocket::GetServerPublicKey
#include "../../Common/Log/IULog.h"                             // LOG_INFO / LOG_WARNING / LOG_ERROR

#pragma comment(lib, "ws2_32.lib")

namespace foldertransfer {

//接收线程单次阻塞 recv 的超时（毫秒）：周期性返回以检查主动关闭标志，避免 Close 时久等。
static const int kRecvTimeoutMs = 500;
//发送超时（毫秒）：单帧发不出去超过此时长即判连接异常。
static const int kSendTimeoutMs = 5000;
//接收线程单次读取块大小（字节）。
static const int kRecvChunk = 64 * 1024;
//报到阶段单次读取块大小（字节）：报到应答是一条很短的 JSON，不需要大缓冲。
static const int kHelloRecvChunk = 4096;
//一秒的毫秒数，用于把调用方给的超时秒数换算成毫秒计数。
static const int kMillisPerSecond = 1000;

//报到帧的序列号。一条中转连接上只报到一次，不存在"请求与应答按序列号配对"的需要，
//故固定取 0（与 CallRelayClient 一致）。该值参与加密的附加认证数据，两端按同一帧头
//字段重建，故取什么值都可以，但必须固定。
static const uint32_t kHelloFrameSeq = 0;
//数据帧的序列号。中转数据帧同样不做请求应答配对，固定取 0。
static const uint32_t kDataFrameSeq = 0;
//报到应答里表示成功的结果码，与服务端 error_code_ok 一致。
static const int kHelloAckCodeOk = 0;
//报到结果码的初值：取 -1 表示"尚未解析出有效结果码"，避免未收到应答时被误判为成功。
static const int kHelloAckCodeUnset = -1;

FolderRelayClient::FolderRelayClient()
    : m_sock(INVALID_SOCKET)
    , m_lane(kFolderControlLane)
    , m_connected(false)
    , m_stopping(false)
    , m_handler(NULL)
{
}

FolderRelayClient::~FolderRelayClient()
{
    Close();
}

bool FolderRelayClient::DoConnect(const std::string& ip, int port, int timeoutSec)
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
        //不是点分十进制，按主机名解析
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

bool FolderRelayClient::SendAll(const char* data, size_t len)
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
 *  握手期间的字节收发适配器：把 net::performClientHandshake 需要的收发转接到本连接的
 *  socket 上。只在 Connect() 里活着，那时接收线程还没启动，不涉及并发。
 */
class FolderRelayClient::HandshakeChannel : public net::IHandshakeChannel
{
public:
    //  owner：宿主连接，生命周期覆盖本对象；timeoutSec：收取超时，单位秒
    HandshakeChannel(FolderRelayClient* owner, int timeoutSec)
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
            const int n = ::recv(m_owner->m_sock, buf + got, static_cast<int>(len - got), 0);
            if (n > 0)
            {
                got += static_cast<size_t>(n);
                continue;
            }
            //握手是一来一回的短交互，超时即视为对端没有按协议应答，不再续等
            return false;
        }
        return true;
    }

private:
    FolderRelayClient* m_owner;       //宿主连接，不拥有
    int                m_timeoutSec;  //收取超时，单位秒（socket 上已设，故仅作记录）
};

bool FolderRelayClient::PerformHandshake(int timeoutSec)
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
        LOG_ERROR("FolderRelayClient: handshake failed: %s, lane=%d.", err.c_str(), m_lane.load());
        return false;
    }

    LOG_INFO("FolderRelayClient: handshake done, relay traffic is now encrypted, lane=%d.",
             m_lane.load());
    return true;
}

bool FolderRelayClient::SendHelloAndWaitAck(const std::string& token, int lane,
                                            const std::string& ip, int port, int timeoutSec)
{
    //报到帧包体走加密：它带着中转令牌，正是最该保护的那一帧。协议加密握手已在调用本
    //函数之前完成，此刻 m_handshake.cipher() 已就绪（未配公钥时它未就绪，包体走明文）。
    const std::string helloBody = folderrelay::BuildRelayHelloBody(token, lane);
    std::string helloWire;
    uint8_t     helloFlags = 0;
    uint32_t    helloOriginLen = 0;
    if (!net::encodeFramePayload(cmd_folder_relay_hello, kHelloFrameSeq, helloBody,
                                 &m_handshake.cipher(), 0, net::payload_compress_never,
                                 helloWire, helloFlags, helloOriginLen))
    {
        LOG_ERROR("FolderRelayClient: encrypt hello failed, relay %s:%d, lane=%d.",
                  ip.c_str(), port, lane);
        return false;
    }

    std::string hello;
    if (!net::encodeFrame(cmd_folder_relay_hello, kHelloFrameSeq, helloFlags, helloWire.data(),
                          static_cast<uint32_t>(helloWire.size()), helloOriginLen, hello))
    {
        LOG_ERROR("FolderRelayClient: encode hello failed, relay %s:%d, lane=%d.",
                  ip.c_str(), port, lane);
        return false;
    }

    if (!SendAll(hello.data(), hello.size()))
    {
        LOG_ERROR("FolderRelayClient: send hello failed, relay %s:%d, lane=%d.",
                  ip.c_str(), port, lane);
        return false;
    }

    //阻塞收报到应答（一帧 cmd_folder_relay_hello），最多等 timeoutSec
    std::string acc;
    bool gotAck = false;
    int  code = kHelloAckCodeUnset;
    int  waitedMs = 0;
    while (!gotAck && waitedMs < timeoutSec * kMillisPerSecond)
    {
        char buf[kHelloRecvChunk];
        int n = ::recv(m_sock, buf, sizeof(buf), 0);
        if (n > 0)
        {
            acc.append(buf, static_cast<size_t>(n));
            //从累积缓冲循环拆帧，遇报到应答帧即取出结果码
            while (true)
            {
                frame_header h;
                std::string  body;
                size_t       consumed = 0;
                net::frame_decode_result r = net::decodeFrame(
                    acc.data(), acc.size(), h, body, consumed, MAX_BODY_FILESERVER);
                if (r == net::frame_decode_need_more)
                    break;
                if (r == net::frame_decode_error)
                {
                    //协议错误：本条连接已不可信，以失败收场
                    LOG_ERROR("FolderRelayClient: bad frame while waiting hello ack, relay %s:%d, lane=%d.",
                              ip.c_str(), port, lane);
                    gotAck = true;
                    code = kHelloAckCodeUnset;
                    break;
                }

                acc.erase(0, consumed);
                if (h.cmd != cmd_folder_relay_hello)
                {
                    //报到阶段忽略非报到帧，继续拆下一帧
                    LOG_WARNING("FolderRelayClient: ignore cmd %u while waiting hello ack, lane=%d.",
                                static_cast<unsigned>(h.cmd), lane);
                    continue;
                }

                //报到应答的包体同样是加密的，先还原成明文再解析
                std::string plain;
                std::string decErr;
                if (!net::decodeFramePayload(h.cmd, h.seq, h.flags, h.originlen, body,
                                             &m_handshake.cipher(), plain, decErr))
                {
                    LOG_ERROR("FolderRelayClient: decrypt hello ack failed: %s, lane=%d.",
                              decErr.c_str(), lane);
                    gotAck = true;   //跳出等待循环，下面会因 code != 0 判失败
                    code = kHelloAckCodeUnset;
                    break;
                }

                //报到应答就是 {code, msg}，与远程桌面 / 音视频中转的报到应答同结构，故
                //直接复用已有的解析函数，不再为同一份 JSON 另写一遍。日后
                //net/FolderProtocolClient 落地时应换成该模块的同名函数。
                net::RDRelayHelloAck ackobj;
                if (net::RemoteDesktopProtocolClient::parseRelayHelloAck(plain, ackobj))
                    code = ackobj.m_code;
                gotAck = true;
                break;
            }
        }
        else if (n == 0)
        {
            //对端关闭：多半是服务端校验令牌不过直接断开
            LOG_ERROR("FolderRelayClient: relay closed connection while waiting hello ack, relay %s:%d, lane=%d.",
                      ip.c_str(), port, lane);
            break;
        }
        else
        {
            int e = ::WSAGetLastError();
            if (e == WSAETIMEDOUT || e == WSAEWOULDBLOCK)
            {
                waitedMs += kRecvTimeoutMs;
                continue;
            }

            LOG_ERROR("FolderRelayClient: recv error %d while waiting hello ack, relay %s:%d, lane=%d.",
                      e, ip.c_str(), port, lane);
            break;
        }
    }

    if (!gotAck || code != kHelloAckCodeOk)
    {
        LOG_ERROR("FolderRelayClient: relay hello rejected, code=%d, relay %s:%d, lane=%d.",
                  code, ip.c_str(), port, lane);
        return false;
    }

    //报到成功：把等待应答期间可能多收的字节（理论上没有）留给接收线程继续处理
    m_recvBuf = acc;
    return true;
}

bool FolderRelayClient::Connect(const std::string& ip, int port, const std::string& token, int lane,
                                FolderRelayClientHandler* handler, int timeoutSec)
{
    if (handler == NULL)
        return false;
    if (m_connected.load())
        return true;

    //lane 越过硬上限的连接服务端一定会拒，没必要发出去；这里先拦下并说清原因
    if (!folderrelay::IsValidLaneIndex(lane))
    {
        LOG_ERROR("FolderRelayClient: invalid lane %d, must be in [%d, %d].",
                  lane, kFolderControlLane, kFolderMaxLaneIndex);
        return false;
    }

    m_handler = handler;
    m_lane.store(lane);
    m_stopping.store(false);

    LOG_INFO("FolderRelayClient: connecting relay %s:%d, lane=%d.", ip.c_str(), port, lane);

    if (!DoConnect(ip, port, timeoutSec))
    {
        LOG_ERROR("FolderRelayClient: connect relay %s:%d failed, lane=%d.", ip.c_str(), port, lane);
        if (m_sock != INVALID_SOCKET)
        {
            ::closesocket(m_sock);
            m_sock = INVALID_SOCKET;
        }
        return false;
    }

    //协议加密握手：必须排在下面的报到帧之前——报到帧里带的是中转令牌，明文发出去等于
    //把凭据白送。此刻接收线程还没启动，同步收发不会与它抢数据。
    if (!PerformHandshake(timeoutSec))
    {
        ::closesocket(m_sock);
        m_sock = INVALID_SOCKET;
        return false;
    }

    if (!SendHelloAndWaitAck(token, lane, ip, port, timeoutSec))
    {
        ::closesocket(m_sock);
        m_sock = INVALID_SOCKET;
        return false;
    }

    //报到成功：起接收线程，此后本通道进入收发业务帧的阶段
    m_connected.store(true);
    m_recvThread.reset(new std::thread(&FolderRelayClient::RecvProc, this));
    LOG_INFO("FolderRelayClient: relay hello ok, relay %s:%d, lane=%d.", ip.c_str(), port, lane);
    return true;
}

bool FolderRelayClient::SendAppFrame(const unsigned char* data, size_t len)
{
    if (!m_connected.load())
    {
        LOG_WARNING("FolderRelayClient: send on closed channel, len=%u, lane=%d.",
                    static_cast<unsigned>(len), m_lane.load());
        return false;
    }

    //超出单帧上限一律判失败，绝不截断后发出去：截断会让对端解出一个残缺的应用帧，
    //查起来比"发送失败"难得多。上限的来历见头文件 kFolderRelayMaxAppFrameBytes 的注释。
    if (len > kFolderRelayMaxAppFrameBytes)
    {
        LOG_ERROR("FolderRelayClient: app frame too large, len=%u, limit=%u, lane=%d.",
                  static_cast<unsigned>(len),
                  static_cast<unsigned>(kFolderRelayMaxAppFrameBytes), m_lane.load());
        return false;
    }

    const std::string body(reinterpret_cast<const char*>(data), len);

    //<u>加密必须在发送锁之内做</u>：一条数据通道上除了工作线程发字节，控制通道上还可能
    //有 UI 线程发中止帧，流加密按调用顺序推进内部计数器，若在锁外加密、锁内发送，写到
    //socket 的先后就可能与加密的先后相反，服务端按流顺序解密必然失败并断开连接。
    //FrameCipher 内部那把锁只保证单次加密是原子的，保证不了这个顺序。
    std::lock_guard<std::mutex> guard(m_sendMtx);
    if (!m_connected.load())
        return false;

    //中转数据帧不压缩：文件字节多半已是压缩格式（图片、压缩包、音视频），再走一遍 zlib
    //既费 CPU 又压不动。服务端也不解压，压缩标志与 originlen 原样透传到对端客户端。
    std::string wire;
    uint8_t     flags = 0;
    uint32_t    originlen = 0;
    if (!net::encodeFramePayload(cmd_folder_relay_data, kDataFrameSeq, body,
                                 &m_handshake.cipher(), 0, net::payload_compress_never,
                                 wire, flags, originlen))
    {
        LOG_ERROR("FolderRelayClient: encrypt data frame failed, len=%u, lane=%d.",
                  static_cast<unsigned>(len), m_lane.load());
        return false;
    }

    std::string frame;
    if (!net::encodeFrame(cmd_folder_relay_data, kDataFrameSeq, flags, wire.data(),
                          static_cast<uint32_t>(wire.size()), originlen, frame))
    {
        LOG_ERROR("FolderRelayClient: encode data frame failed, len=%u, lane=%d.",
                  static_cast<unsigned>(len), m_lane.load());
        return false;
    }

    if (!SendAll(frame.data(), frame.size()))
    {
        LOG_ERROR("FolderRelayClient: send data frame failed, len=%u, lane=%d.",
                  static_cast<unsigned>(len), m_lane.load());
        return false;
    }
    return true;
}

void FolderRelayClient::RecvProc()
{
    while (!m_stopping.load())
    {
        char buf[kRecvChunk];
        int n = ::recv(m_sock, buf, sizeof(buf), 0);
        if (n > 0)
        {
            m_recvBuf.append(buf, static_cast<size_t>(n));
            bool protoErr = false;
            //从累积缓冲循环拆帧，把数据帧的包体解密后交给上层
            while (true)
            {
                frame_header h;
                std::string  body;
                size_t       consumed = 0;
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
                if (h.cmd != cmd_folder_relay_data)
                {
                    //业务期只该来数据帧。收到别的 cmd 不当作错误，记一条日志后忽略，
                    //以免服务端日后在这条通道上加了新帧型就把老客户端断开。
                    LOG_WARNING("FolderRelayClient: unexpected cmd %u on relay channel, ignored, lane=%d.",
                                static_cast<unsigned>(h.cmd), m_lane.load());
                    continue;
                }

                //先把线上包体还原成明文再交给上层。中转通道上服务端会用配对端的密钥
                //重新加密，所以这里解的是「服务端到本端」这个方向的流。
                std::string plain;
                std::string decErr;
                if (!net::decodeFramePayload(h.cmd, h.seq, h.flags, h.originlen, body,
                                             &m_handshake.cipher(), plain, decErr))
                {
                    //解密失败意味着这个方向的流已不可恢复，只能断开重来，不能跳过本帧
                    //继续解下一帧
                    LOG_ERROR("FolderRelayClient: decrypt relay data failed: %s, closing, lane=%d.",
                              decErr.c_str(), m_lane.load());
                    protoErr = true;
                    break;
                }

                //包体原样交给上层，本类不解析应用帧内容
                if (m_handler != NULL)
                    m_handler->OnFolderRelayAppFrame(
                        reinterpret_cast<const unsigned char*>(plain.data()), plain.size());
            }

            if (protoErr)
            {
                LOG_ERROR("FolderRelayClient: recv frame protocol error, closing, lane=%d.",
                          m_lane.load());
                break;
            }
        }
        else if (n == 0)
        {
            LOG_INFO("FolderRelayClient: relay peer closed connection, lane=%d.", m_lane.load());
            break;
        }
        else
        {
            int e = ::WSAGetLastError();
            if (e == WSAETIMEDOUT || e == WSAEWOULDBLOCK)
                continue;   //周期性超时，回去检查停止标志

            LOG_ERROR("FolderRelayClient: recv error %d, closing, lane=%d.", e, m_lane.load());
            break;
        }
    }

    //异常断开（非主动 Close）时回调一次 OnFolderRelayClosed，便于上层按整体失败收场
    bool wasStopping = m_stopping.load();
    m_connected.store(false);
    if (!wasStopping && m_handler != NULL)
    {
        LOG_WARNING("FolderRelayClient: relay channel broken, notify upper layer, lane=%d.",
                    m_lane.load());
        m_handler->OnFolderRelayClosed();
    }
}

void FolderRelayClient::Close()
{
    m_stopping.store(true);
    m_connected.store(false);

    if (m_sock != INVALID_SOCKET)
    {
        LOG_INFO("FolderRelayClient: closing relay channel by local side, lane=%d.", m_lane.load());
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

bool FolderRelayClient::IsConnected() const
{
    return m_connected.load();
}

int FolderRelayClient::GetLane() const
{
    return m_lane.load();
}

} // namespace foldertransfer
