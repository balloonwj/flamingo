/**
 *  chatserver 到 imgserver 的云端录制控制连接，职责见 CloudRecordLink.h。
 *  balloonwj@qq.com   2026-10-06
 */
#include "CloudRecordLink.h"

#include <stdio.h>
#include <string.h>
#include <chrono>
#ifndef WIN32
#include <errno.h>
#include <fcntl.h>
#endif
#include "../base/AsyncLog.h"
#include "../jsoncpp1.9.0/json.h"
#include "../net/FrameCodec.h"
#include "../net/FrameCrypto.h"
#include "../net/FramePayload.h"
#include "../utils/CloudRecordCtlMsg.h"
#include "../utils/RelayToken.h"
#include "Protocol.h"

namespace {

//报到令牌的有效期（毫秒）：只在报到那一刻校验，每次重连重签
const int64_t kHelloTokenTtlMs = 60 * 1000;
//重连退避的各档等待（毫秒），超出表长后一律取最后一档
const int kReconnectDelaysMs[] = { 1000, 2000, 5000, 10000, 30000 };
//每次 recv 的缓冲大小（字节）
const size_t kRecvChunkBytes = 64 * 1024;

int64_t nowMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch()).count();
}

int64_t steadyMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch()).count();
}

int lastSocketError()
{
#ifdef WIN32
    return WSAGetLastError();
#else
    return errno;
#endif
}

//非阻塞操作「暂时做不了、稍后再试」的错误码
bool isWouldBlock(int err)
{
#ifdef WIN32
    return err == WSAEWOULDBLOCK;
#else
    return err == EWOULDBLOCK || err == EAGAIN || err == EINPROGRESS || err == EINTR;
#endif
}

bool setNonBlocking(SOCKET s)
{
#ifdef WIN32
    u_long on = 1;
    return ioctlsocket(s, FIONBIO, &on) == 0;
#else
    const int flags = fcntl(s, F_GETFL, 0);
    return flags >= 0 && fcntl(s, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
}

/**
 *  等套接字可读或可写，最多 timeoutMs 毫秒。
 *  @return 1 表示就绪，0 表示超时，-1 表示出错
 */
int waitSocket(SOCKET s, bool forWrite, int timeoutMs)
{
    fd_set set;
    FD_ZERO(&set);
    FD_SET(s, &set);
    fd_set errSet;
    FD_ZERO(&errSet);
    FD_SET(s, &errSet);
    struct timeval tv;
    tv.tv_sec = timeoutMs / 1000;
    tv.tv_usec = (timeoutMs % 1000) * 1000;
    //Windows 忽略第一个参数；POSIX 要求最大描述符加 1
    const int n = select(static_cast<int>(s) + 1, forWrite ? NULL : &set, forWrite ? &set : NULL, &errSet, &tv);
    if (n < 0)
        return isWouldBlock(lastSocketError()) ? 0 : -1;
    if (n == 0)
        return 0;
    return FD_ISSET(s, &errSet) && !FD_ISSET(s, &set) ? -1 : 1;
}

std::string writeCompact(const Json::Value& v)
{
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    return Json::writeString(wb, v);
}

bool parseObject(const std::string& json, Json::Value& root)
{
    Json::CharReaderBuilder b;
    JSONCPP_STRING errs;
    Json::CharReader* reader = b.newCharReader();
    const bool ok = reader->parse(json.c_str(), json.c_str() + json.size(), &root, &errs);
    delete reader;
    return ok && errs.empty() && root.isObject();
}

} // namespace

//类内初始化的静态常量的类外定义：被 ODR-use 时 g++ 要求有类外定义，MSVC 不要求
const int CloudRecordLink::kPingIntervalMs;
const int CloudRecordLink::kIdleTimeoutMs;
const int CloudRecordLink::kStepTimeoutMs;
const int CloudRecordLink::kPollSliceMs;

CloudRecordLinkConfig::CloudRecordLinkConfig()
    : m_port(0)
    , m_encrypt(true)
{
}

CloudRecordLink::CloudRecordLink()
    : m_listener(NULL)
    , m_stop(false)
    , m_up(false)
    , m_sock(INVALID_SOCKET)
{
}

CloudRecordLink::~CloudRecordLink()
{
    stop();
}

void CloudRecordLink::setListener(ICloudRecordLinkListener* listener)
{
    m_listener = listener;
}

int CloudRecordLink::reconnectDelayMs(int failures)
{
    const int count = static_cast<int>(sizeof(kReconnectDelaysMs) / sizeof(kReconnectDelaysMs[0]));
    if (failures < 1)
        failures = 1;
    if (failures > count)
        failures = count;
    return kReconnectDelaysMs[failures - 1];
}

bool CloudRecordLink::start(const CloudRecordLinkConfig& config)
{
    if (m_thread)
        return false;
    if (config.m_host.empty() || config.m_port <= 0 || config.m_tokenKey.empty())
    {
        LOGW("cloud record link: imgserver host, relay port or relay token key not configured, cloud record unavailable");
        return false;
    }
    m_config = config;
    m_stop = false;
    m_thread.reset(new std::thread(&CloudRecordLink::threadProc, this));
    LOGI("cloud record link: started, imgserver: %s:%d, encrypt: %d", config.m_host.c_str(), config.m_port,
         config.m_encrypt ? 1 : 0);
    return true;
}

void CloudRecordLink::stop()
{
    if (!m_thread)
        return;
    m_stop = true;
    {
        std::lock_guard<std::mutex> guard(m_sleepMutex);
        m_sleepCv.notify_all();
    }
    if (m_thread->joinable())
        m_thread->join();
    m_thread.reset();
    LOGI("cloud record link: stopped");
}

bool CloudRecordLink::isUp() const
{
    return m_up.load();
}

void CloudRecordLink::sleepMs(int ms)
{
    std::unique_lock<std::mutex> lock(m_sleepMutex);
    const std::chrono::steady_clock::time_point deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (!m_stop.load())
    {
        if (m_sleepCv.wait_until(lock, deadline) == std::cv_status::timeout)
            break;
    }
}

void CloudRecordLink::closeSocket()
{
    std::lock_guard<std::mutex> guard(m_sendMutex);
    m_up = false;
    if (m_sock != INVALID_SOCKET)
    {
        closesocket(m_sock);
        m_sock = INVALID_SOCKET;
    }
    m_hs.reset();
    m_inbuf.clear();
}

bool CloudRecordLink::send(const std::string& body)
{
    std::lock_guard<std::mutex> guard(m_sendMutex);
    if (!m_up.load() || m_sock == INVALID_SOCKET)
        return false;
    return sendFrameLocked(cmd_cloudrec_ctl_data, 0, body, false);
}

bool CloudRecordLink::sendFrameLocked(uint16_t cmd, uint32_t seq, const std::string& body, bool forcePlain)
{
    net::FrameCipher* cipher = (!forcePlain && m_hs) ? &m_hs->cipher() : NULL;
    std::string wire;
    uint8_t flags = 0;
    uint32_t originlen = 0;
    if (!net::encodeFramePayload(cmd, seq, body, cipher, 0, net::payload_compress_never, wire, flags, originlen))
    {
        LOGE("cloud record link: encode frame payload failed, cmd: %u", static_cast<unsigned>(cmd));
        return false;
    }
    std::string out;
    if (!net::encodeFrame(cmd, seq, flags, wire.data(), static_cast<uint32_t>(wire.size()), originlen, out))
    {
        LOGE("cloud record link: encode frame failed, cmd: %u", static_cast<unsigned>(cmd));
        return false;
    }
    return sendAll(out);
}

bool CloudRecordLink::sendAll(const std::string& data)
{
    size_t sent = 0;
    const int64_t deadline = steadyMs() + kStepTimeoutMs;
    while (sent < data.size())
    {
        const int n = ::send(m_sock, data.data() + sent, static_cast<int>(data.size() - sent), 0);
        if (n > 0)
        {
            sent += static_cast<size_t>(n);
            continue;
        }
        if (n < 0 && isWouldBlock(lastSocketError()))
        {
            if (m_stop.load() || steadyMs() > deadline)
                return false;
            if (waitSocket(m_sock, true, kPollSliceMs) < 0)
                return false;
            continue;
        }
        LOGE("cloud record link: send failed, error: %d", lastSocketError());
        return false;
    }
    return true;
}

bool CloudRecordLink::readFrame(uint16_t& cmd, std::string& body, int timeoutMs, bool& timedOut, std::string& err)
{
    timedOut = false;
    const int64_t deadline = steadyMs() + timeoutMs;
    for (;;)
    {
        frame_header header;
        std::string wire;
        size_t consumed = 0;
        const net::frame_decode_result r = net::decodeFrame(m_inbuf.data(), m_inbuf.size(), header, wire, consumed,
                                                            MAX_BODY_FILESERVER);
        if (r == net::frame_decode_error)
        {
            err = "bad frame from imgserver";
            return false;
        }
        if (r == net::frame_decode_ok)
        {
            m_inbuf.erase(0, consumed);
            cmd = header.cmd;
            //握手帧一律明文；其余帧按当前加密状态还原
            net::FrameCipher* cipher = (cmd != cmd_handshake && m_hs) ? &m_hs->cipher() : NULL;
            if (!net::decodeFramePayload(header.cmd, header.seq, header.flags, header.originlen, wire, cipher, body,
                                         err))
                return false;
            return true;
        }

        //不足一帧：等更多字节
        if (m_stop.load())
        {
            err = "stopping";
            return false;
        }
        const int64_t left = deadline - steadyMs();
        if (left <= 0)
        {
            timedOut = true;
            return false;
        }
        const int w = waitSocket(m_sock, false, left < kPollSliceMs ? static_cast<int>(left) : kPollSliceMs);
        if (w < 0)
        {
            err = "socket error while waiting";
            return false;
        }
        if (w == 0)
            continue;
        char buf[kRecvChunkBytes];
        const int n = ::recv(m_sock, buf, static_cast<int>(sizeof(buf)), 0);
        if (n > 0)
        {
            m_inbuf.append(buf, static_cast<size_t>(n));
            continue;
        }
        if (n < 0 && isWouldBlock(lastSocketError()))
            continue;
        err = (n == 0) ? "connection closed by imgserver" : "recv failed";
        return false;
    }
}

bool CloudRecordLink::establish(std::string& err)
{
    //解析地址并发起非阻塞连接
    char portText[16];
    snprintf(portText, sizeof(portText), "%d", m_config.m_port);
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    struct addrinfo* res = NULL;
    if (getaddrinfo(m_config.m_host.c_str(), portText, &hints, &res) != 0 || res == NULL)
    {
        err = "cannot resolve imgserver host " + m_config.m_host;
        return false;
    }
    SOCKET s = INVALID_SOCKET;
    for (struct addrinfo* ai = res; ai != NULL && s == INVALID_SOCKET && !m_stop.load(); ai = ai->ai_next)
    {
        SOCKET c = ::socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (c == INVALID_SOCKET)
            continue;
        if (!setNonBlocking(c))
        {
            closesocket(c);
            continue;
        }
        int rc = ::connect(c, ai->ai_addr, static_cast<int>(ai->ai_addrlen));
        if (rc != 0 && isWouldBlock(lastSocketError()))
        {
            const int64_t deadline = steadyMs() + kStepTimeoutMs;
            int w = 0;
            while (w == 0 && !m_stop.load() && steadyMs() < deadline)
                w = waitSocket(c, true, kPollSliceMs);
            int soErr = 0;
            socklen_t len = sizeof(soErr);
            if (w == 1 && getsockopt(c, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&soErr), &len) == 0 && soErr == 0)
                rc = 0;
        }
        if (rc == 0)
            s = c;
        else
            closesocket(c);
    }
    freeaddrinfo(res);
    if (s == INVALID_SOCKET)
    {
        err = "cannot connect to imgserver";
        return false;
    }
    {
        std::lock_guard<std::mutex> guard(m_sendMutex);
        m_sock = s;
        m_inbuf.clear();
        m_hs.reset();
    }

    uint16_t cmd = 0;
    std::string body;
    bool timedOut = false;

    //协议加密握手：第 1、3 帧明文发出，第 2 帧明文收到
    if (m_config.m_encrypt)
    {
        const unsigned char* peerKey = NULL;
        if (m_config.m_peerPubKey.size() == net::IDENTITY_PUBLICKEY_BYTES)
            peerKey = reinterpret_cast<const unsigned char*>(m_config.m_peerPubKey.data());
        else
            peerKey = net::cryptoServerIdentity().publicKey();
        std::unique_ptr<net::PeerClientHandshake> hs(new net::PeerClientHandshake());
        {
            std::lock_guard<std::mutex> guard(m_sendMutex);
            if (!sendFrameLocked(cmd_handshake, 1, hs->buildClientHello(), true))
            {
                err = "send ClientHello failed";
                return false;
            }
        }
        if (!readFrame(cmd, body, kStepTimeoutMs, timedOut, err))
        {
            if (timedOut)
                err = "ServerHello timeout";
            return false;
        }
        if (cmd != cmd_handshake || !hs->onServerHello(body, peerKey, err))
        {
            if (err.empty())
                err = "unexpected frame during handshake";
            return false;
        }
        std::lock_guard<std::mutex> guard(m_sendMutex);
        //第 3 帧此时 cipher 已就绪，仍须明文发出
        if (!sendFrameLocked(cmd_handshake, 2, hs->buildClientStreamHeader(), true))
        {
            err = "send stream header failed";
            return false;
        }
        m_hs.swap(hs);
    }

    //报到：令牌角色 15，sessionid 带时刻以便在 imgserver 日志里区分每次重连
    RelayTokenPayload payload;
    payload.sessionid = "srvctl-" + std::to_string(static_cast<long long>(nowMs()));
    payload.role = RelayToken::role_server_control;
    payload.uid = 0;
    payload.peeruid = 0;
    payload.expire = nowMs() + kHelloTokenTtlMs;
    Json::Value hello;
    hello["token"] = RelayToken::sign(payload, m_config.m_tokenKey);
    {
        std::lock_guard<std::mutex> guard(m_sendMutex);
        if (!sendFrameLocked(cmd_cloudrec_ctl_hello, 0, writeCompact(hello), false))
        {
            err = "send control hello failed";
            return false;
        }
    }
    if (!readFrame(cmd, body, kStepTimeoutMs, timedOut, err))
    {
        if (timedOut)
            err = "control hello ack timeout";
        return false;
    }
    Json::Value ack;
    if (cmd != cmd_cloudrec_ctl_hello || !parseObject(body, ack) || !ack["code"].isInt() || ack["code"].asInt() != 0)
    {
        err = "control hello rejected: " + body;
        return false;
    }
    m_up = true;
    return true;
}

void CloudRecordLink::serve()
{
    int64_t lastRecv = steadyMs();
    int64_t lastPing = lastRecv;
    CloudRecordCtlMsg ping;
    ping.m_op = cloudrecctl_op_ping;
    const std::string pingBody = BuildCloudRecordCtlMsg(ping);
    while (!m_stop.load())
    {
        uint16_t cmd = 0;
        std::string body;
        bool timedOut = false;
        std::string err;
        if (readFrame(cmd, body, kPollSliceMs, timedOut, err))
        {
            lastRecv = steadyMs();
            if (cmd == cmd_cloudrec_ctl_data)
            {
                if (m_listener != NULL)
                    m_listener->onCloudRecordLinkMessage(body);
            }
            else
            {
                LOGW("cloud record link: unexpected cmd %u from imgserver, ignored", static_cast<unsigned>(cmd));
            }
            continue;
        }
        if (!timedOut)
        {
            if (!m_stop.load())
                LOGE("cloud record link: connection lost: %s", err.c_str());
            return;
        }
        const int64_t now = steadyMs();
        if (now - lastRecv > kIdleTimeoutMs)
        {
            LOGE("cloud record link: no message from imgserver for %d ms, reconnect", kIdleTimeoutMs);
            return;
        }
        if (now - lastPing >= kPingIntervalMs)
        {
            lastPing = now;
            if (!send(pingBody))
            {
                LOGE("cloud record link: send ping failed, reconnect");
                return;
            }
        }
    }
}

void CloudRecordLink::threadProc()
{
    int failures = 0;
    while (!m_stop.load())
    {
        std::string err;
        if (establish(err))
        {
            failures = 0;
            LOGI("cloud record link: connected to imgserver %s:%d", m_config.m_host.c_str(), m_config.m_port);
            if (m_listener != NULL)
                m_listener->onCloudRecordLinkUp();
            serve();
            closeSocket();
            if (m_listener != NULL)
                m_listener->onCloudRecordLinkDown();
            LOGI("cloud record link: disconnected from imgserver");
        }
        else
        {
            closeSocket();
            ++failures;
            //第一次失败记错误，之后每到最长退避档再记一次，免得日志刷屏
            const int delay = reconnectDelayMs(failures);
            if (failures == 1 || delay == reconnectDelayMs(failures + 1))
                LOGE("cloud record link: connect to imgserver %s:%d failed: %s, retry in %d ms (failures: %d)",
                     m_config.m_host.c_str(), m_config.m_port, err.c_str(), delay, failures);
            sleepMs(delay);
            continue;
        }
        if (!m_stop.load())
            sleepMs(reconnectDelayMs(1));
    }
    closeSocket();
}
