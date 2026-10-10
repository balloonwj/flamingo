/**
 *  chatserver 到 imgserver 的云端录制控制连接（CloudRecordLink）单元测试（Suite CRL），test_cloudrecordlink.cpp
 *
 *  第 26 期（docs/屏幕共享与录制方案.md 第 12.6.2 节）。本文件在 127.0.0.1 的临时端口上起一个假的 imgserver（阻塞套接字，
 *  服务端侧握手用真的 net::ServerHandshake，报到用真的 RelayToken::verify），让 CloudRecordLink 去连：
 *    - CRL1 重连退避的各档等待；
 *    - CRL2 加密握手、报到（令牌角色 15、未过期）成功后回报 up；send 的消息对端能解开；对端发来的消息回报给 listener；
 *           对端断开后回报 down，并自动重连、再次 up；stop 之后不再回报；
 *    - CRL3 报到被拒（code 不为 0）时不回报 up，并按退避重连；
 *    - CRL4 没有配置地址、端口或令牌密钥时不启动。
 *
 *  典型用法：Tests.exe --test-case="CRL*"
 *
 *  balloonwj@qq.com   2026-10-06
 */
#include <stdio.h>
#include <string.h>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "doctest.h"
#include "../base/Platform.h"
#include "../chatserversrc/CloudRecordLink.h"
#include "../chatserversrc/Protocol.h"
#include "../jsoncpp1.9.0/json.h"
#include "../net/FrameCodec.h"
#include "../net/FrameCrypto.h"
#include "../net/FramePayload.h"
#include "../utils/RelayToken.h"

#ifdef WIN32
//Windows 下用 socket 之前必须先 WSAStartup；理由见 test_sendthreadaffinity.cpp 的同名对象
static NetworkInitializer s_crlNetworkInitializer;
#endif

namespace {

//假 imgserver 用的令牌密钥
const char* const kCrlTokenKey = "crl-test-relay-key";
//假 imgserver 的身份密钥种子文件（当前目录，用完删除）
const char* const kCrlKeyFile = "flamingo_crypto_crl_test.key";
//等待回报的上限（毫秒）
const int kCrlWaitMs = 8000;

//记下 CloudRecordLink 的回报
class CrlListener : public ICloudRecordLinkListener
{
public:
    CrlListener()
        : m_ups(0)
        , m_downs(0)
    {
    }

    virtual void onCloudRecordLinkUp() override
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        ++m_ups;
        m_cv.notify_all();
    }

    virtual void onCloudRecordLinkDown() override
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        ++m_downs;
        m_cv.notify_all();
    }

    virtual void onCloudRecordLinkMessage(const std::string& body) override
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_messages.push_back(body);
        m_cv.notify_all();
    }

    //等到 up 次数、down 次数、消息条数都不少于给定值
    bool waitFor(int ups, int downs, size_t messages)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        const std::chrono::steady_clock::time_point deadline =
            std::chrono::steady_clock::now() + std::chrono::milliseconds(kCrlWaitMs);
        while (m_ups < ups || m_downs < downs || m_messages.size() < messages)
        {
            if (m_cv.wait_until(lock, deadline) == std::cv_status::timeout)
                break;
        }
        return m_ups >= ups && m_downs >= downs && m_messages.size() >= messages;
    }

    std::mutex               m_mutex;      //保护以下各项
    std::condition_variable  m_cv;         //有回报时通知
    int                      m_ups;        //up 次数
    int                      m_downs;      //down 次数
    std::vector<std::string> m_messages;   //收到的消息
};

//假 imgserver：监听一个临时端口，逐个接受连接，按脚本应答
class FakeImgServer
{
public:
    FakeImgServer()
        : m_listen(INVALID_SOCKET)
        , m_conn(INVALID_SOCKET)
        , m_port(0)
    {
    }

    ~FakeImgServer()
    {
        closeConn();
        if (m_listen != INVALID_SOCKET)
            closesocket(m_listen);
    }

    bool listenAny()
    {
        m_listen = ::socket(AF_INET, SOCK_STREAM, 0);
        if (m_listen == INVALID_SOCKET)
            return false;
        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = 0;
        if (::bind(m_listen, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) != 0)
            return false;
        if (::listen(m_listen, 4) != 0)
            return false;
        socklen_t len = sizeof(addr);
        if (getsockname(m_listen, reinterpret_cast<struct sockaddr*>(&addr), &len) != 0)
            return false;
        m_port = ntohs(addr.sin_port);
        return true;
    }

    int port() const
    {
        return m_port;
    }

    //接受一条连接（阻塞），并换一套新的握手状态
    bool accept()
    {
        closeConn();
        m_conn = ::accept(m_listen, NULL, NULL);
        m_hs.reset(new net::ServerHandshake());
        m_inbuf.clear();
        return m_conn != INVALID_SOCKET;
    }

    void closeConn()
    {
        if (m_conn != INVALID_SOCKET)
        {
            closesocket(m_conn);
            m_conn = INVALID_SOCKET;
        }
    }

    //读一帧并还原包体（握手完成后按密文解）
    bool readFrame(uint16_t& cmd, std::string& body)
    {
        for (;;)
        {
            frame_header header;
            std::string wire;
            size_t consumed = 0;
            const net::frame_decode_result r = net::decodeFrame(m_inbuf.data(), m_inbuf.size(), header, wire,
                                                                consumed, MAX_BODY_FILESERVER);
            if (r == net::frame_decode_error)
                return false;
            if (r == net::frame_decode_ok)
            {
                m_inbuf.erase(0, consumed);
                cmd = header.cmd;
                std::string err;
                net::FrameCipher* cipher = (cmd != cmd_handshake && m_hs->ready()) ? &m_hs->cipher() : NULL;
                return net::decodeFramePayload(header.cmd, header.seq, header.flags, header.originlen, wire, cipher,
                                               body, err);
            }
            char buf[4096];
            const int n = ::recv(m_conn, buf, static_cast<int>(sizeof(buf)), 0);
            if (n <= 0)
                return false;
            m_inbuf.append(buf, static_cast<size_t>(n));
        }
    }

    bool sendFrame(uint16_t cmd, uint32_t seq, const std::string& body)
    {
        net::FrameCipher* cipher = (cmd != cmd_handshake && m_hs->ready()) ? &m_hs->cipher() : NULL;
        std::string wire;
        uint8_t flags = 0;
        uint32_t originlen = 0;
        if (!net::encodeFramePayload(cmd, seq, body, cipher, 0, net::payload_compress_never, wire, flags, originlen))
            return false;
        std::string out;
        if (!net::encodeFrame(cmd, seq, flags, wire.data(), static_cast<uint32_t>(wire.size()), originlen, out))
            return false;
        return ::send(m_conn, out.data(), static_cast<int>(out.size()), 0) == static_cast<int>(out.size());
    }

    /**
     *  走完握手与报到。
     *  @param identity  服务端身份
     *  @param ackCode   报到应答的 code
     *  @param outPayload [出参] 解出的令牌
     *  @return 双方都按协议走完返回 true
     */
    bool handshakeAndHello(const net::ServerIdentity& identity, int ackCode, RelayTokenPayload& outPayload)
    {
        uint16_t cmd = 0;
        std::string body;
        std::string response;
        std::string err;
        //第 1 帧 → 第 2 帧
        if (!readFrame(cmd, body) || cmd != cmd_handshake)
            return false;
        if (!m_hs->onHandshakeFrame(body, identity, response, err))
            return false;
        if (!sendFrame(cmd_handshake, 1, response))
            return false;
        //第 3 帧
        if (!readFrame(cmd, body) || cmd != cmd_handshake)
            return false;
        if (!m_hs->onHandshakeFrame(body, identity, response, err) || !m_hs->ready())
            return false;
        //报到
        if (!readFrame(cmd, body) || cmd != cmd_cloudrec_ctl_hello)
            return false;
        Json::Value root;
        Json::CharReaderBuilder b;
        JSONCPP_STRING errs;
        std::unique_ptr<Json::CharReader> reader(b.newCharReader());
        if (!reader->parse(body.c_str(), body.c_str() + body.size(), &root, &errs))
            return false;
        const int64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                  std::chrono::system_clock::now().time_since_epoch()).count();
        if (!RelayToken::verify(root["token"].asString(), kCrlTokenKey, nowMs, outPayload))
            return false;
        Json::Value ack;
        ack["code"] = ackCode;
        ack["msg"] = ackCode == 0 ? "ok" : "rejected";
        Json::StreamWriterBuilder wb;
        wb["indentation"] = "";
        return sendFrame(cmd_cloudrec_ctl_hello, 0, Json::writeString(wb, ack));
    }

private:
    SOCKET                                 m_listen;   //监听套接字
    SOCKET                                 m_conn;     //当前连接
    int                                    m_port;     //监听端口
    std::unique_ptr<net::ServerHandshake>  m_hs;       //当前连接的握手状态
    std::string                            m_inbuf;    //收到、还没解出完整帧的字节
};

CloudRecordLinkConfig crlConfig(int port, const net::ServerIdentity& identity)
{
    CloudRecordLinkConfig cfg;
    cfg.m_host = "127.0.0.1";
    cfg.m_port = port;
    cfg.m_tokenKey = kCrlTokenKey;
    cfg.m_encrypt = true;
    cfg.m_peerPubKey.assign(reinterpret_cast<const char*>(identity.publicKey()), net::IDENTITY_PUBLICKEY_BYTES);
    return cfg;
}

} // namespace

TEST_CASE("CRL1 reconnect backoff")
{
    CHECK(CloudRecordLink::reconnectDelayMs(0) == 1000);
    CHECK(CloudRecordLink::reconnectDelayMs(1) == 1000);
    CHECK(CloudRecordLink::reconnectDelayMs(2) == 2000);
    CHECK(CloudRecordLink::reconnectDelayMs(3) == 5000);
    CHECK(CloudRecordLink::reconnectDelayMs(4) == 10000);
    CHECK(CloudRecordLink::reconnectDelayMs(5) == 30000);
    CHECK(CloudRecordLink::reconnectDelayMs(100) == 30000);
}

TEST_CASE("CRL2 handshake, hello, messages both ways, reconnect after the peer closes")
{
    REQUIRE(net::cryptoInit());
    std::string err;
    net::ServerIdentity identity;
    REQUIRE_MESSAGE(identity.loadOrCreate(kCrlKeyFile, err), err);
    FakeImgServer server;
    REQUIRE(server.listenAny());

    CrlListener listener;
    CloudRecordLink link;
    link.setListener(&listener);
    CHECK_FALSE(link.isUp());
    CHECK_FALSE(link.send("{\"op\":\"ping\"}"));
    REQUIRE(link.start(crlConfig(server.port(), identity)));
    CHECK_FALSE(link.start(crlConfig(server.port(), identity)));

    REQUIRE(server.accept());
    RelayTokenPayload payload;
    REQUIRE(server.handshakeAndHello(identity, 0, payload));
    CHECK(payload.role == RelayToken::role_server_control);
    CHECK(payload.sessionid.compare(0, 7, "srvctl-") == 0);
    REQUIRE(listener.waitFor(1, 0, 0));
    CHECK(link.isUp());

    //chatserver → imgserver
    REQUIRE(link.send("{\"op\":\"stop\",\"recordid\":5}"));
    uint16_t cmd = 0;
    std::string body;
    REQUIRE(server.readFrame(cmd, body));
    CHECK(cmd == cmd_cloudrec_ctl_data);
    CHECK(body == "{\"op\":\"stop\",\"recordid\":5}");
    //imgserver → chatserver
    REQUIRE(server.sendFrame(cmd_cloudrec_ctl_data, 0, "{\"op\":\"pong\"}"));
    REQUIRE(listener.waitFor(1, 0, 1));
    CHECK(listener.m_messages[0] == "{\"op\":\"pong\"}");

    //对端断开：回报 down，随后自动重连
    server.closeConn();
    REQUIRE(listener.waitFor(1, 1, 1));
    REQUIRE(server.accept());
    REQUIRE(server.handshakeAndHello(identity, 0, payload));
    REQUIRE(listener.waitFor(2, 1, 1));
    CHECK(link.isUp());

    link.stop();
    CHECK_FALSE(link.isUp());
    CHECK_FALSE(link.send("{\"op\":\"ping\"}"));
    {
        std::lock_guard<std::mutex> lock(listener.m_mutex);
        CHECK(listener.m_ups == 2);
        CHECK(listener.m_downs == 2);
    }
    remove(kCrlKeyFile);
    remove((std::string(kCrlKeyFile) + ".pub").c_str());
}

TEST_CASE("CRL3 rejected hello does not report up and retries")
{
    REQUIRE(net::cryptoInit());
    std::string err;
    net::ServerIdentity identity;
    REQUIRE_MESSAGE(identity.loadOrCreate(kCrlKeyFile, err), err);
    FakeImgServer server;
    REQUIRE(server.listenAny());

    CrlListener listener;
    CloudRecordLink link;
    link.setListener(&listener);
    REQUIRE(link.start(crlConfig(server.port(), identity)));
    REQUIRE(server.accept());
    RelayTokenPayload payload;
    REQUIRE(server.handshakeAndHello(identity, error_code_session_expired, payload));
    //被拒后退避 1 秒再连：第二条连接到来说明在重试
    REQUIRE(server.accept());
    CHECK_FALSE(link.isUp());
    {
        std::lock_guard<std::mutex> lock(listener.m_mutex);
        CHECK(listener.m_ups == 0);
        CHECK(listener.m_downs == 0);
    }
    link.stop();
    remove(kCrlKeyFile);
    remove((std::string(kCrlKeyFile) + ".pub").c_str());
}

TEST_CASE("CRL4 not started without host, port or token key")
{
    CloudRecordLink link;
    CloudRecordLinkConfig cfg;
    cfg.m_port = 20003;
    cfg.m_tokenKey = "k";
    CHECK_FALSE(link.start(cfg));
    cfg.m_host = "127.0.0.1";
    cfg.m_port = 0;
    CHECK_FALSE(link.start(cfg));
    cfg.m_port = 20003;
    cfg.m_tokenKey.clear();
    CHECK_FALSE(link.start(cfg));
    link.stop();
}
