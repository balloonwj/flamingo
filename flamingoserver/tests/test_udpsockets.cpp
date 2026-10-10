/**
 *  net/Sockets 的 UDP 辅助函数单元测试（Suite UDP），test_udpsockets.cpp
 *
 *  覆盖 docs/P2P通信方案.md 第 2 期 2e-1（imgserver 地址探测用到的 UDP 收发）：
 *    - UDP1 本机收发：两个套接字绑定 127.0.0.1 的随机端口互相发包，内容与来源地址正确；报文长于接收缓冲时
 *           两个平台都截断到缓冲大小；
 *    - UDP2 非阻塞：没有数据时 recvFrom 立即返回 -1，且 lastErrorIsWouldBlock 为真；
 *    - UDP3 向已关闭的本机端口发包后，同一个套接字仍能正常收到别处发来的数据（Windows 上靠关闭
 *           SIO_UDP_CONNRESET，否则 ICMP 端口不可达会让之后的 recvfrom 报 WSAECONNRESET）。
 *  只用本机回环，不依赖网络环境。
 *
 *  典型用法：Tests.exe --test-case="UDP*"
 *
 *  balloonwj@qq.com   2026-10-03
 */
#include <string.h>
#include <chrono>
#include <string>
#include <thread>

#include "doctest.h"
#include "../base/Platform.h"
#include "../net/Sockets.h"

#ifdef WIN32
//Windows 下用 socket 之前必须先 WSAStartup，做法与 test_pollerrevents.cpp 相同（WSAStartup 是引用计数的）
static NetworkInitializer s_udpNetworkInitializer;
#endif

namespace {

//本机回环地址
const char* const kLoopback = "127.0.0.1";

//等一个报文到达的最长时间（毫秒）与轮询间隔（毫秒）：回环上正常在毫秒级
const int kRecvWaitMs = 2000;
const int kPollIntervalMs = 1;

//UDP2：没有数据时 recvFrom 必须在这个时间（毫秒）内返回，用来区分「立即返回」与「阻塞到超时」
const long long kNonBlockingMaxMs = 200;

//UDP3：向已关闭端口发包后，等 ICMP 端口不可达回到本机的时间（毫秒）
const int kIcmpSettleMs = 100;

//接收缓冲（字节），以及截断测试用的小缓冲与长报文
const int kRecvBufBytes = 2048;
const int kSmallBufBytes = 16;
const int kLongPacketBytes = 100;

//建一个绑定在 127.0.0.1 随机端口上的非阻塞 UDP 套接字，返回它与实际绑定的地址
SOCKET OpenBoundUdp(struct sockaddr_in& bound)
{
    SOCKET s = net::sockets::createUdpNonblocking();
    REQUIRE(s != INVALID_SOCKET);
    struct sockaddr_in addr;
    net::sockets::fromIpPort(kLoopback, 0, &addr);
    REQUIRE(net::sockets::bindUdp(s, addr));
    bound = net::sockets::getLocalAddr(s);
    REQUIRE(bound.sin_port != 0);
    return s;
}

//在 kRecvWaitMs 内等一个报文；收到返回字节数，超时返回 -1。sawError 记录期间是否出现过「暂无数据」以外的错误
int WaitRecv(SOCKET s, char* buf, int count, struct sockaddr_in& from, bool& sawError)
{
    const std::chrono::steady_clock::time_point deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(kRecvWaitMs);
    while (std::chrono::steady_clock::now() < deadline)
    {
        const int32_t n = net::sockets::recvFrom(s, buf, count, &from);
        if (n >= 0)
            return n;
        if (!net::sockets::lastErrorIsWouldBlock())
            sawError = true;
        std::this_thread::sleep_for(std::chrono::milliseconds(kPollIntervalMs));
    }
    return -1;
}

} // anonymous namespace

TEST_CASE("UDP1 本机收发与截断")
{
    struct sockaddr_in addrA;
    struct sockaddr_in addrB;
    SOCKET a = OpenBoundUdp(addrA);
    SOCKET b = OpenBoundUdp(addrB);

    //A → B：内容与来源地址正确
    const std::string ping = "ping-from-a";
    REQUIRE(net::sockets::sendTo(a, ping.data(), (int32_t)ping.size(), addrB) == (int32_t)ping.size());
    char buf[kRecvBufBytes];
    struct sockaddr_in from;
    bool sawError = false;
    int n = WaitRecv(b, buf, kRecvBufBytes, from, sawError);
    REQUIRE(n == (int)ping.size());
    CHECK(std::string(buf, n) == ping);
    CHECK(from.sin_port == addrA.sin_port);
    CHECK(from.sin_addr.s_addr == addrA.sin_addr.s_addr);

    //B → A
    const std::string pong = "pong-from-b";
    REQUIRE(net::sockets::sendTo(b, pong.data(), (int32_t)pong.size(), addrA) == (int32_t)pong.size());
    n = WaitRecv(a, buf, kRecvBufBytes, from, sawError);
    REQUIRE(n == (int)pong.size());
    CHECK(std::string(buf, n) == pong);
    CHECK(from.sin_port == addrB.sin_port);

    //报文长于接收缓冲：截断到缓冲大小，内容是报文的前若干字节
    std::string longPacket(kLongPacketBytes, 'x');
    for (int i = 0; i < kLongPacketBytes; ++i)
        longPacket[i] = (char)('a' + i % 26);
    REQUIRE(net::sockets::sendTo(a, longPacket.data(), (int32_t)longPacket.size(), addrB) == kLongPacketBytes);
    //变量名不能用 small：Windows 的 rpcndr.h 把它定义成了 char
    char shortBuf[kSmallBufBytes];
    n = WaitRecv(b, shortBuf, kSmallBufBytes, from, sawError);
    CHECK(n == kSmallBufBytes);
    CHECK(std::string(shortBuf, kSmallBufBytes) == longPacket.substr(0, kSmallBufBytes));
    CHECK_FALSE(sawError);

    net::sockets::close(a);
    net::sockets::close(b);
}

TEST_CASE("UDP2 没有数据时立即返回")
{
    struct sockaddr_in addr;
    SOCKET s = OpenBoundUdp(addr);
    char buf[kRecvBufBytes];
    struct sockaddr_in from;
    const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    const int32_t n = net::sockets::recvFrom(s, buf, kRecvBufBytes, &from);
    const long long elapsedMs = (long long)std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();
    CHECK(n == -1);
    CHECK(net::sockets::lastErrorIsWouldBlock());
    CHECK(elapsedMs < kNonBlockingMaxMs);
    net::sockets::close(s);
}

TEST_CASE("UDP3 向已关闭端口发包后仍能正常接收")
{
    struct sockaddr_in addrA;
    struct sockaddr_in addrB;
    struct sockaddr_in addrClosed;
    SOCKET a = OpenBoundUdp(addrA);
    SOCKET b = OpenBoundUdp(addrB);
    //先占一个端口再关掉，得到一个确定没有人监听的本机端口
    SOCKET c = OpenBoundUdp(addrClosed);
    net::sockets::close(c);

    const std::string probe = "to-closed-port";
    net::sockets::sendTo(a, probe.data(), (int32_t)probe.size(), addrClosed);
    std::this_thread::sleep_for(std::chrono::milliseconds(kIcmpSettleMs));

    //B → A：A 照样收到，期间不出现「暂无数据」以外的错误
    const std::string hello = "hello-after-icmp";
    REQUIRE(net::sockets::sendTo(b, hello.data(), (int32_t)hello.size(), addrA) == (int32_t)hello.size());
    char buf[kRecvBufBytes];
    struct sockaddr_in from;
    bool sawError = false;
    const int n = WaitRecv(a, buf, kRecvBufBytes, from, sawError);
    CHECK_FALSE(sawError);
    REQUIRE(n == (int)hello.size());
    CHECK(std::string(buf, n) == hello);
    CHECK(from.sin_port == addrB.sin_port);

    net::sockets::close(a);
    net::sockets::close(b);
}
