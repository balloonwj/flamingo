/**
 *  带超时的非阻塞 connect（net::ConnectWithTimeout）与四个中转客户端连接失败时的返回速度，
 *  TcpConnectWaitTests.cpp（Suite TCW，标签 [tcpconnect]）
 *
 *  背景：2026-10-12 服务端把中转地址误配成 127.0.0.1，发起屏幕共享的客户端连本机 20003 端口，
 *  端口没有监听却要等满 5 秒才报失败，日志里也分不清是被拒绝还是超时。原因是各处只用 select 等「可写」，
 *  而 Winsock 把非阻塞 connect 的失败放在异常集合里。修正后的实现见 Source/net/TcpConnectWait.h。
 *
 *  覆盖（2026-10-12 与用户确认的用例）：
 *    - TCW1 连接本机正在监听的端口：返回成功，耗时远小于超时；
 *    - TCW2 连接本机没有监听的端口、超时 10 秒：5 秒内返回「失败」，错误码为 WSAECONNREFUSED；
 *    - TCW3 连接不会应答的文档保留地址 192.0.2.1、超时 1 秒：不成功，耗时不超过 2.5 秒
 *           （不同网络环境下可能是超时，也可能立即报网络不可达，两种都接受）；
 *    - TCW4 结果描述：三种结果各有固定的英文描述，带错误码或超时毫秒数；
 *    - TCW5 ShareRelayClient、CallRelayClient、RdRelayClient、FolderRelayClient 连接本机没有监听的端口、
 *           超时 10 秒：都在 5 秒内返回失败，证明四处都已改用新的等待方式；
 *    - TCW-O1～O3（2026-10-14，四个中转客户端的建连合并为 net::ConnectBlockingSocket）：主机名解析失败时返回失败并写明原因；
 *           端口被拒绝时立即返回、不等满超时；连上后为阻塞模式并设好收发超时，SendAllBlocking 的字节对端完整收到。
 *  需要本机回环连接，不访问外部服务器（TCW3 的地址不会有应答）。
 *  框架 Catch2 v3。
 *
 *  balloonwj@qq.com   2026-10-12
 */
#include "catch_amalgamated.hpp"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <string.h>
#include <chrono>
#include <string>

#include "../net/TcpConnectWait.h"
#include "../Feature/FolderTransfer/FolderRelayClient.h"
#include "../../FlamingoMedia/Share/ShareRelayClient.h"
#include "../../FlamingoMedia/Call/CallRelayClient.h"
#include "../../FlamingoMedia/RemoteDesktop/RdRelayClient.h"

namespace {

//本机回环地址
const char* const kLoopbackIp = "127.0.0.1";

//RFC 5737 的文档保留地址（TEST-NET-1），公网上不会有主机应答
const char* const kUnreachableIp = "192.0.2.1";

//连接不会应答的地址时用的端口：取中转端口的默认值，数值本身无关紧要
const int kUnreachablePort = 20003;

//TCW1 / TCW2 的等待上限（毫秒）与四个客户端 Connect 的超时（秒）：都取 10 秒，
//修改前的实现会等满它，修改后应远早于它返回
const int kLongTimeoutMs = 10000;
const int kLongTimeoutSec = 10;

//TCW1：连本机正在监听的端口，应在这么多毫秒内连上
const long long kConnectFastMs = 2000;

//TCW2 / TCW5：连本机没有监听的端口，应在这么多毫秒内返回失败（本机实测约 2 秒）
const long long kRefusedWithinMs = 5000;

//TCW3 的等待上限（毫秒），以及允许的最长耗时（上限再加 1.5 秒余量）
const int kShortTimeoutMs = 1000;
const long long kShortTimeoutLimitMs = 2500;

//TCW4 用到的错误码：网络不可达（WSAENETUNREACH），作为「被拒绝以外的失败」的样本
const int kSampleOtherError = WSAENETUNREACH;

//TCW4 用到的超时毫秒数
const int kSampleTimeoutMs = 5000;

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

//离开作用域时关闭 socket
struct ScopedSocket
{
    explicit ScopedSocket(SOCKET s)
        : m_sock(s)
    {
    }
    ~ScopedSocket()
    {
        if (m_sock != INVALID_SOCKET)
            ::closesocket(m_sock);
    }

    SOCKET m_sock;   //持有的 socket；INVALID_SOCKET 表示没有
};

//构造 IPv4 地址
sockaddr_in MakeAddr(const char* ip, int port)
{
    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<u_short>(port));
    addr.sin_addr.s_addr = ::inet_addr(ip);
    return addr;
}

//建一个非阻塞模式的 TCP socket；失败返回 INVALID_SOCKET
SOCKET MakeNonBlockingSocket()
{
    SOCKET s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET)
        return INVALID_SOCKET;
    unsigned long on = 1;
    if (::ioctlsocket(s, FIONBIO, &on) == SOCKET_ERROR)
    {
        ::closesocket(s);
        return INVALID_SOCKET;
    }
    return s;
}

//在本机回环上建一个监听 socket（系统分配端口），把端口写到 outPort；失败返回 INVALID_SOCKET
SOCKET MakeLoopbackListener(int& outPort)
{
    outPort = 0;
    SOCKET s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET)
        return INVALID_SOCKET;
    sockaddr_in addr = MakeAddr(kLoopbackIp, 0);
    int len = sizeof(addr);
    if (::bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 ||
        ::listen(s, SOMAXCONN) != 0 ||
        ::getsockname(s, reinterpret_cast<sockaddr*>(&addr), &len) != 0)
    {
        ::closesocket(s);
        return INVALID_SOCKET;
    }
    outPort = ntohs(addr.sin_port);
    return s;
}

//取一个本机回环上当前没有程序监听的端口：先占一个系统分配的端口再立即释放；失败返回 0
int GetUnusedLoopbackPort()
{
    int port = 0;
    SOCKET s = MakeLoopbackListener(port);
    if (s == INVALID_SOCKET)
        return 0;
    ::closesocket(s);
    return port;
}

//单调时钟的当前时刻（毫秒）
long long NowMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch()).count();
}

//四个中转客户端的回调桩：本组用例只测连接失败，回调不会被调用
class StubShareHandler : public screenshare::ShareRelayClientHandler
{
public:
    virtual void OnRelayBody(const char* body, size_t len) override
    {
        (void)body;
        (void)len;
    }
    virtual void OnRelayClosed() override
    {
    }
};

class StubCallHandler : public call::CallRelayClientHandler
{
public:
    virtual void OnRelayData(const unsigned char* data, size_t len) override
    {
        (void)data;
        (void)len;
    }
    virtual void OnRelayClosed() override
    {
    }
};

class StubRdHandler : public remotedesktop::RdRelayClientHandler
{
public:
    virtual void OnRelayEngineFrame(const unsigned char* data, size_t len) override
    {
        (void)data;
        (void)len;
    }
    virtual void OnRelayClosed() override
    {
    }
};

class StubFolderHandler : public foldertransfer::FolderRelayClientHandler
{
public:
    virtual void OnFolderRelayAppFrame(const unsigned char* data, size_t len) override
    {
        (void)data;
        (void)len;
    }
    virtual void OnFolderRelayClosed() override
    {
    }
};

} // namespace

TEST_CASE("TCW1 连接本机正在监听的端口：返回成功，耗时远小于超时", "[tcpconnect]")
{
    ScopedWinsock winsock;
    int port = 0;
    ScopedSocket listener(MakeLoopbackListener(port));
    REQUIRE(listener.m_sock != INVALID_SOCKET);
    ScopedSocket client(MakeNonBlockingSocket());
    REQUIRE(client.m_sock != INVALID_SOCKET);

    int err = -1;
    const long long startMs = NowMs();
    const net::tcp_connect_result r =
        net::ConnectWithTimeout(client.m_sock, MakeAddr(kLoopbackIp, port), kLongTimeoutMs, err);
    const long long elapsedMs = NowMs() - startMs;

    CHECK(r == net::tcp_connect_ok);
    CHECK(err == 0);
    CHECK(elapsedMs < kConnectFastMs);
}

TEST_CASE("TCW2 连接本机没有监听的端口：5 秒内返回失败，错误码为 WSAECONNREFUSED", "[tcpconnect]")
{
    ScopedWinsock winsock;
    const int port = GetUnusedLoopbackPort();
    REQUIRE(port > 0);
    ScopedSocket client(MakeNonBlockingSocket());
    REQUIRE(client.m_sock != INVALID_SOCKET);

    int err = 0;
    const long long startMs = NowMs();
    const net::tcp_connect_result r =
        net::ConnectWithTimeout(client.m_sock, MakeAddr(kLoopbackIp, port), kLongTimeoutMs, err);
    const long long elapsedMs = NowMs() - startMs;

    CHECK(r == net::tcp_connect_failed);
    CHECK(err == WSAECONNREFUSED);
    CHECK(elapsedMs < kRefusedWithinMs);
}

TEST_CASE("TCW3 连接不会应答的地址：不成功，耗时不超过等待上限加余量", "[tcpconnect]")
{
    ScopedWinsock winsock;
    ScopedSocket client(MakeNonBlockingSocket());
    REQUIRE(client.m_sock != INVALID_SOCKET);

    int err = 0;
    const long long startMs = NowMs();
    const net::tcp_connect_result r =
        net::ConnectWithTimeout(client.m_sock, MakeAddr(kUnreachableIp, kUnreachablePort), kShortTimeoutMs, err);
    const long long elapsedMs = NowMs() - startMs;

    CHECK(r != net::tcp_connect_ok);
    CHECK(elapsedMs <= kShortTimeoutLimitMs);
}

TEST_CASE("TCW4 连接结果的英文描述", "[tcpconnect]")
{
    CHECK(net::DescribeTcpConnect(net::tcp_connect_ok, 0, kSampleTimeoutMs) == "connected");
    CHECK(net::DescribeTcpConnect(net::tcp_connect_failed, WSAECONNREFUSED, kSampleTimeoutMs) ==
          "refused (error 10061)");
    CHECK(net::DescribeTcpConnect(net::tcp_connect_failed, kSampleOtherError, kSampleTimeoutMs) ==
          "failed (error 10051)");
    CHECK(net::DescribeTcpConnect(net::tcp_connect_timeout, 0, kSampleTimeoutMs) == "timed out after 5000 ms");
}

TEST_CASE("TCW5 四个中转客户端连接本机没有监听的端口：都在 5 秒内返回失败", "[tcpconnect]")
{
    ScopedWinsock winsock;
    const int port = GetUnusedLoopbackPort();
    REQUIRE(port > 0);

    SECTION("ShareRelayClient")
    {
        StubShareHandler handler;
        screenshare::ShareRelayClient client;
        const long long startMs = NowMs();
        CHECK_FALSE(client.Connect(kLoopbackIp, port, "token", NULL, &handler, kLongTimeoutSec));
        CHECK(NowMs() - startMs < kRefusedWithinMs);
        CHECK_FALSE(client.IsConnected());
    }
    SECTION("CallRelayClient")
    {
        StubCallHandler handler;
        call::CallRelayClient client;
        const long long startMs = NowMs();
        CHECK_FALSE(client.Connect(kLoopbackIp, port, "token", NULL, &handler, kLongTimeoutSec));
        CHECK(NowMs() - startMs < kRefusedWithinMs);
        CHECK_FALSE(client.IsConnected());
    }
    SECTION("RdRelayClient")
    {
        StubRdHandler handler;
        remotedesktop::RdRelayClient client;
        const long long startMs = NowMs();
        CHECK_FALSE(client.Connect(kLoopbackIp, port, "token", NULL, &handler, kLongTimeoutSec));
        CHECK(NowMs() - startMs < kRefusedWithinMs);
        CHECK_FALSE(client.IsConnected());
    }
    SECTION("FolderRelayClient")
    {
        StubFolderHandler handler;
        foldertransfer::FolderRelayClient client;
        const long long startMs = NowMs();
        CHECK_FALSE(client.Connect(kLoopbackIp, port, "token", foldertransfer::kFolderControlLane, NULL, &handler,
                                   kLongTimeoutSec));
        CHECK(NowMs() - startMs < kRefusedWithinMs);
        CHECK_FALSE(client.IsConnected());
    }
}

namespace {

//TCW-O1 用的主机名：.invalid 是保留顶级域，不会解析成任何地址（RFC 2606）
const char* const kUnresolvableHost = "no-such-host.invalid";
//TCW-O3：连上后设置的收发超时（毫秒），以及判断「确实等了接收超时」的下限（留出计时误差）
const int kO3RecvTimeoutMs = 300;
const int kO3SendTimeoutMs = 400;
const long long kO3RecvWaitAtLeastMs = 200;
//TCW-O3 用 SendAllBlocking 发出、再在监听端收到的字节
const char kO3Payload[] = "flamingo";
const int kO3PayloadLen = static_cast<int>(sizeof(kO3Payload) - 1);

} // namespace

TEST_CASE("TCW-O1 ConnectBlockingSocket reports an unresolvable host name", "[tcpconnect]")
{
    ScopedWinsock winsock;
    SOCKET s = INVALID_SOCKET;
    std::string failure;
    CHECK_FALSE(net::ConnectBlockingSocket(s, kUnresolvableHost, kUnreachablePort, kShortTimeoutMs, kO3RecvTimeoutMs,
                                           kO3SendTimeoutMs, failure));
    CHECK(failure == "host name not resolved");
    //失败时 socket 可能已建好，由调用方关闭
    ScopedSocket owner(s);
}

TEST_CASE("TCW-O2 ConnectBlockingSocket returns at once when the port is refused", "[tcpconnect]")
{
    ScopedWinsock winsock;
    const int port = GetUnusedLoopbackPort();
    REQUIRE(port != 0);
    SOCKET s = INVALID_SOCKET;
    std::string failure;
    const long long startMs = NowMs();
    CHECK_FALSE(net::ConnectBlockingSocket(s, kLoopbackIp, port, kLongTimeoutMs, kO3RecvTimeoutMs, kO3SendTimeoutMs,
                                           failure));
    CHECK(NowMs() - startMs < kRefusedWithinMs);
    CHECK(failure.find("refused") == 0);
    ScopedSocket owner(s);
}

TEST_CASE("TCW-O3 ConnectBlockingSocket leaves a blocking socket with send and receive timeouts set", "[tcpconnect]")
{
    ScopedWinsock winsock;
    int port = 0;
    ScopedSocket listener(MakeLoopbackListener(port));
    REQUIRE(listener.m_sock != INVALID_SOCKET);
    SOCKET s = INVALID_SOCKET;
    std::string failure;
    REQUIRE(net::ConnectBlockingSocket(s, kLoopbackIp, port, kLongTimeoutMs, kO3RecvTimeoutMs, kO3SendTimeoutMs,
                                       failure));
    ScopedSocket client(s);
    CHECK(failure.empty());

    //收发超时按给定值设好
    int value = 0;
    int len = sizeof(value);
    REQUIRE(::getsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<char*>(&value), &len) == 0);
    CHECK(value == kO3RecvTimeoutMs);
    len = sizeof(value);
    REQUIRE(::getsockopt(s, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<char*>(&value), &len) == 0);
    CHECK(value == kO3SendTimeoutMs);

    //阻塞模式：没有数据时 recv 等到接收超时、报 WSAETIMEDOUT；非阻塞模式会立即报 WSAEWOULDBLOCK
    char ch = 0;
    const long long startMs = NowMs();
    CHECK(::recv(s, &ch, 1, 0) == SOCKET_ERROR);
    CHECK(::WSAGetLastError() == WSAETIMEDOUT);
    CHECK(NowMs() - startMs >= kO3RecvWaitAtLeastMs);

    //SendAllBlocking 发出的字节在对端完整收到
    ScopedSocket server(::accept(listener.m_sock, NULL, NULL));
    REQUIRE(server.m_sock != INVALID_SOCKET);
    REQUIRE(net::SendAllBlocking(s, kO3Payload, kO3PayloadLen));
    char buf[sizeof(kO3Payload)] = { 0 };
    int got = 0;
    while (got < kO3PayloadLen)
    {
        const int n = ::recv(server.m_sock, buf + got, kO3PayloadLen - got, 0);
        if (n <= 0)
            break;
        got += n;
    }
    CHECK(got == kO3PayloadLen);
    CHECK(std::string(buf, got) == kO3Payload);
}
