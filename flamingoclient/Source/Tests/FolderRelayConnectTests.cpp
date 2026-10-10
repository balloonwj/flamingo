/**
 *  文件夹中转通道 FolderRelayClient 的连接测试（Suite FRC-CONN，标签 [folderrelay]），FolderRelayConnectTests.cpp
 *  覆盖（对应 docs/文件共享方案.md 第 1 期第 3 步，2026-10-02 与用户确认的用例）：
 *    - FRC-CONN1 回调为空、通道序号越界时直接失败，不发起连接；
 *    - FRC-CONN2 连接本机一个没有监听的端口：失败，IsConnected 为假，Close 可重复调用；
 *    - FRC-CONN3 本机假中转服务、不传公钥：客户端首帧是明文报到帧（带令牌与通道序号），
 *           假服务回 code=0 后连接成功；
 *    - FRC-CONN4 传入公钥：客户端首帧是加密握手帧（cmd_handshake），而不是报到帧 —— 证明调用方
 *           传入的公钥确实被用于握手。
 *
 *  第 3 步之前 FolderRelayClient.cpp 从 CIUSocket 单例取服务器公钥，而测试工程不编译 IUSocket.cpp，
 *  故本文件之前无法链接；改为由调用方传入公钥之后，测试工程能直接编译它，链接通过本身就证明
 *  它已不依赖主程序的全局对象（文件共享 DLL 也要编译同一份源文件）。
 *  报到帧包体、通道序号区间等纯逻辑的用例在 FolderRelayClientTests.cpp（Suite FRC1~FRC6），
 *  那份文件约定不起网络连接，故需要本机回环连接的用例单独放在本文件。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-02
 */
#include "catch_amalgamated.hpp"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <string.h>
#include <string>
#include <thread>
#include <atomic>
#include <memory>

#include "../Feature/FolderTransfer/FolderRelayClient.h"
#include "../net/FrameCodec.h"
#include "../net/Protocol.h"
#include "../third-party/jsoncpp-1.9.0/json.h"

using foldertransfer::FolderRelayClient;
using foldertransfer::FolderRelayClientHandler;

namespace {

//本机回环地址
const char* const kLoopbackIp = "127.0.0.1";

//各用例里 Connect 的总超时（秒）：本机回环足够快，取小值让失败路径的用例不拖时间
const int kConnectTimeoutSec = 3;

//假中转服务等待连接、等待数据的上限（毫秒），防止用例出错时线程永远挂着
const int kServerWaitMs = 5000;

//假中转服务每次 select 的等待片（毫秒）
const int kServerPollMs = 50;

//每毫秒对应的微秒数，换算 select 的超时
const int kMicrosPerMilli = 1000;

//假中转服务收数据用的缓冲字节数
const int kServerRecvChunk = 4096;

//回给客户端的报到应答（与 fileserver 的报到应答同结构）
const char* const kHelloAckOk = "{\"code\":0,\"msg\":\"ok\"}";

//FRC-CONN4 传入的公钥：任意 32 字节即可，假服务在收到握手首帧后就断开，不会真的完成握手
const unsigned char kFakePubKeyByte = 0x5A;

//越界的通道序号：低于控制通道、高于硬上限
const int kLaneBelowRange = -1;

//测试用的通道回调桩：记下是否收到过「连接已关闭」
class StubRelayHandler : public FolderRelayClientHandler
{
public:
    StubRelayHandler()
        : m_closed(false)
    {
    }

    virtual void OnFolderRelayAppFrame(const unsigned char* data, size_t len) override
    {
        (void)data;
        (void)len;
    }

    virtual void OnFolderRelayClosed() override
    {
        m_closed.store(true);
    }

    std::atomic<bool> m_closed;   //是否收到过连接关闭回调
};

//进程内初始化 / 清理 Winsock。WSAStartup 按调用次数计数，每个用例各配一对即可
struct ScopedWinsock
{
    ScopedWinsock()
    {
        WSADATA wsaData;
        ::WSAStartup(MAKEWORD(2, 2), &wsaData);
    }
    ~ScopedWinsock()
    {
        ::WSACleanup();
    }
};

//在 socket 上等待可读，最多 timeoutMs 毫秒。可读返回 true
bool WaitReadable(SOCKET s, int timeoutMs)
{
    fd_set readSet;
    FD_ZERO(&readSet);
    FD_SET(s, &readSet);
    timeval tv;
    tv.tv_sec = timeoutMs / 1000;
    tv.tv_usec = (timeoutMs % 1000) * kMicrosPerMilli;
    return ::select(0, &readSet, NULL, NULL, &tv) > 0;
}

/**
 *  本机假中转服务：只接一条连接、只读它的第一帧并记下 cmd 与包体。
 *  m_replyHelloAck 为 true 且首帧是报到帧时，回一帧 code=0 的报到应答，然后等客户端断开。
 */
class FakeRelayServer
{
public:
    FakeRelayServer()
        : m_listener(INVALID_SOCKET)
        , m_port(0)
        , m_replyHelloAck(false)
        , m_gotFrame(false)
        , m_firstCmd(0)
    {
    }

    ~FakeRelayServer()
    {
        Join();
    }

    //绑定本机随机端口并开始监听，起服务线程。成功返回 true
    bool Start(bool replyHelloAck)
    {
        m_replyHelloAck = replyHelloAck;
        m_listener = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (m_listener == INVALID_SOCKET)
            return false;

        sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = ::inet_addr(kLoopbackIp);
        addr.sin_port = 0;
        int addrLen = sizeof(addr);
        if (::bind(m_listener, (sockaddr*)&addr, sizeof(addr)) != 0
            || ::listen(m_listener, 1) != 0
            || ::getsockname(m_listener, (sockaddr*)&addr, &addrLen) != 0)
        {
            return false;
        }
        m_port = ntohs(addr.sin_port);
        m_thread = std::thread(&FakeRelayServer::Run, this);
        return true;
    }

    //等服务线程结束并关闭监听 socket。重复调用安全
    void Join()
    {
        if (m_thread.joinable())
            m_thread.join();
        if (m_listener != INVALID_SOCKET)
        {
            ::closesocket(m_listener);
            m_listener = INVALID_SOCKET;
        }
    }

    int Port() const
    {
        return m_port;
    }

    bool GotFrame() const
    {
        return m_gotFrame.load();
    }

    uint16_t FirstCmd() const
    {
        return m_firstCmd;
    }

    const std::string& FirstBody() const
    {
        return m_firstBody;
    }

private:
    void Run()
    {
        if (!WaitReadable(m_listener, kServerWaitMs))
            return;
        SOCKET conn = ::accept(m_listener, NULL, NULL);
        if (conn == INVALID_SOCKET)
            return;

        //收到能拆出第一帧为止
        std::string acc;
        frame_header header;
        std::string body;
        bool decoded = false;
        int waitedMs = 0;
        while (!decoded && waitedMs < kServerWaitMs)
        {
            if (!WaitReadable(conn, kServerPollMs))
            {
                waitedMs += kServerPollMs;
                continue;
            }
            char buf[kServerRecvChunk];
            int n = ::recv(conn, buf, sizeof(buf), 0);
            if (n <= 0)
                break;
            acc.append(buf, static_cast<size_t>(n));
            size_t consumed = 0;
            if (net::decodeFrame(acc.data(), acc.size(), header, body, consumed, MAX_BODY_FILESERVER)
                == net::frame_decode_ok)
            {
                decoded = true;
            }
        }

        if (decoded)
        {
            m_firstCmd = header.cmd;
            m_firstBody = body;
            m_gotFrame.store(true);
        }

        //只有首帧是报到帧、且要求回应答时才回 code=0，然后等客户端自己断开
        if (decoded && m_replyHelloAck && header.cmd == cmd_folder_relay_hello)
        {
            std::string ack;
            if (net::encodeFrame(cmd_folder_relay_hello, header.seq, std::string(kHelloAckOk), ack))
                ::send(conn, ack.data(), static_cast<int>(ack.size()), 0);

            int idleMs = 0;
            while (idleMs < kServerWaitMs)
            {
                if (!WaitReadable(conn, kServerPollMs))
                {
                    idleMs += kServerPollMs;
                    continue;
                }
                char buf[kServerRecvChunk];
                if (::recv(conn, buf, sizeof(buf), 0) <= 0)
                    break;
            }
        }

        ::closesocket(conn);
    }

private:
    SOCKET            m_listener;       //监听 socket
    int               m_port;           //实际绑定的端口
    bool              m_replyHelloAck;  //首帧是报到帧时是否回 code=0 的应答
    std::thread       m_thread;         //服务线程
    std::atomic<bool> m_gotFrame;       //是否已拆出第一帧
    uint16_t          m_firstCmd;       //第一帧的 cmd（m_gotFrame 为真后有效）
    std::string       m_firstBody;      //第一帧的包体（m_gotFrame 为真后有效）
};

//取一个当前没有监听的本机端口：绑定随机端口后立即关闭
int GetUnusedLoopbackPort()
{
    SOCKET s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET)
        return 0;
    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = ::inet_addr(kLoopbackIp);
    addr.sin_port = 0;
    int addrLen = sizeof(addr);
    int port = 0;
    if (::bind(s, (sockaddr*)&addr, sizeof(addr)) == 0
        && ::getsockname(s, (sockaddr*)&addr, &addrLen) == 0)
    {
        port = ntohs(addr.sin_port);
    }
    ::closesocket(s);
    return port;
}

} // namespace

TEST_CASE("FRC-CONN1 回调为空、通道序号越界时直接失败", "[folderrelay]")
{
    ScopedWinsock winsock;
    StubRelayHandler handler;
    FolderRelayClient client;

    CHECK_FALSE(client.Connect(kLoopbackIp, 1, "token", foldertransfer::kFolderControlLane, NULL,
                               NULL, kConnectTimeoutSec));
    CHECK_FALSE(client.Connect(kLoopbackIp, 1, "token", kLaneBelowRange, NULL,
                               &handler, kConnectTimeoutSec));
    CHECK_FALSE(client.Connect(kLoopbackIp, 1, "token", foldertransfer::kFolderMaxLaneIndex + 1, NULL,
                               &handler, kConnectTimeoutSec));
    CHECK_FALSE(client.IsConnected());
}

TEST_CASE("FRC-CONN2 连接没有监听的本机端口失败，Close 可重复调用", "[folderrelay]")
{
    ScopedWinsock winsock;
    int port = GetUnusedLoopbackPort();
    REQUIRE(port > 0);

    StubRelayHandler handler;
    FolderRelayClient client;
    CHECK_FALSE(client.Connect(kLoopbackIp, port, "token", foldertransfer::kFolderControlLane, NULL,
                               &handler, kConnectTimeoutSec));
    CHECK_FALSE(client.IsConnected());
    client.Close();
    client.Close();
    CHECK_FALSE(client.IsConnected());
}

TEST_CASE("FRC-CONN3 不传公钥时首帧是明文报到帧，收到 code=0 后连接成功", "[folderrelay]")
{
    ScopedWinsock winsock;
    FakeRelayServer server;
    REQUIRE(server.Start(true));

    const std::string token = "token-frc3";
    StubRelayHandler handler;
    FolderRelayClient client;
    REQUIRE(client.Connect(kLoopbackIp, server.Port(), token, foldertransfer::kFolderControlLane, NULL,
                           &handler, kConnectTimeoutSec));
    CHECK(client.IsConnected());

    REQUIRE(server.GotFrame());
    CHECK(server.FirstCmd() == cmd_folder_relay_hello);

    //包体是明文 JSON，带令牌与通道序号
    Json::Value root;
    Json::CharReaderBuilder builder;
    JSONCPP_STRING errs;
    std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
    const std::string& body = server.FirstBody();
    REQUIRE(reader->parse(body.data(), body.data() + body.size(), &root, &errs));
    CHECK(root["token"].asString() == token);
    CHECK(root["lane"].asInt() == foldertransfer::kFolderControlLane);

    client.Close();
    server.Join();
}

TEST_CASE("FRC-CONN4 传入公钥时首帧是加密握手帧", "[folderrelay]")
{
    ScopedWinsock winsock;
    FakeRelayServer server;
    REQUIRE(server.Start(false));

    unsigned char pubKey[net::IDENTITY_PUBLICKEY_BYTES];
    memset(pubKey, kFakePubKeyByte, sizeof(pubKey));

    StubRelayHandler handler;
    FolderRelayClient client;
    //假服务收到握手首帧后直接断开，握手完成不了，连接必然失败
    CHECK_FALSE(client.Connect(kLoopbackIp, server.Port(), "token-frc4", foldertransfer::kFolderControlLane,
                               pubKey, &handler, kConnectTimeoutSec));
    server.Join();

    REQUIRE(server.GotFrame());
    CHECK(server.FirstCmd() == cmd_handshake);
    CHECK(server.FirstCmd() != cmd_folder_relay_hello);
    CHECK_FALSE(client.IsConnected());
}
