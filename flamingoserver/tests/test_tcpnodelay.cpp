/**
 *  连接关闭 Nagle 算法的单元测试（Suite NDL），test_tcpnodelay.cpp
 *
 *  imgserver 的中转（远程桌面、屏幕共享、文件夹传输）转发的键鼠帧、指针帧都是几十字节的小包。
 *  框架默认不关 Nagle：上一个小包被对端确认之前，后续小包会被攒着不发，再遇上接收端的延迟确认
 *  （Windows 默认最长 200 毫秒），键鼠操作就会成批晚到。2026-10-17 起 RelayServer 在建连时调用
 *  TcpConnection::setTcpNoDelay(true)。这个函数此前从没被调用过，本套用例先确认它在 Windows 与
 *  Linux 上都真的改到了 socket 上，并用 isTcpNoDelay 读回核对。
 *
 *    NDL1：一条真实的 127.0.0.1 连接，服务端那一端交给 TcpConnection：默认 Nagle 开着，
 *          setTcpNoDelay(true) 之后关闭，setTcpNoDelay(false) 之后恢复。
 *
 *  balloonwj@qq.com   2026-10-17
 */
#include "doctest.h"

#include <string.h>

#include <string>

#include "../base/Platform.h"
#include "../net/EventLoop.h"
#include "../net/EventLoopThread.h"
#include "../net/InetAddress.h"
#include "../net/Sockets.h"
#include "../net/TcpConnection.h"

#ifdef WIN32
//Windows 下用 socket 之前必须先 WSAStartup。测试程序没有服务的 main.cpp，故在本文件里补一个全局对象，
//它的构造早于 main，也就早于任何用例执行（WSAStartup 按引用计数，与别的用例文件里的同类对象互不影响）
static NetworkInitializer s_ndlNetworkInitializer;
#endif

namespace {

/**
 *  在 127.0.0.1 上建一条真实的 TCP 连接。监听端口取 0，由系统分配空闲端口，用完即关。
 *  @param clientFd [出参] 客户端一端，由调用方关闭
 *  @param serverFd [出参] 服务端 accept 出的一端，由调用方关闭或交给 TcpConnection 管理
 *  @param errOut   [出参] 失败原因（英文）
 *  @return 两端都建好返回 true；失败时已建的 socket 都已关闭
 */
bool makeLoopbackPair(SOCKET& clientFd, SOCKET& serverFd, std::string& errOut)
{
    clientFd = INVALID_SOCKET;
    serverFd = INVALID_SOCKET;

    SOCKET listenFd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listenFd == INVALID_SOCKET)
    {
        errOut = "listen socket() failed";
        return false;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port        = 0;
    socklen_t addrLen = sizeof(addr);
    if (::bind(listenFd, (const struct sockaddr*)&addr, sizeof(addr)) != 0
        || ::listen(listenFd, 1) != 0
        || ::getsockname(listenFd, (struct sockaddr*)&addr, &addrLen) != 0)
    {
        closesocket(listenFd);
        errOut = "bind(), listen() or getsockname() failed";
        return false;
    }

    clientFd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (clientFd == INVALID_SOCKET || ::connect(clientFd, (const struct sockaddr*)&addr, sizeof(addr)) != 0)
    {
        if (clientFd != INVALID_SOCKET)
            closesocket(clientFd);
        clientFd = INVALID_SOCKET;
        closesocket(listenFd);
        errOut = "client socket() or connect() failed";
        return false;
    }

    serverFd = ::accept(listenFd, NULL, NULL);
    closesocket(listenFd);
    if (serverFd == INVALID_SOCKET)
    {
        closesocket(clientFd);
        clientFd = INVALID_SOCKET;
        errOut = "accept() failed";
        return false;
    }
    return true;
}

} // namespace

TEST_CASE("NDL1 连接默认开着 Nagle，setTcpNoDelay(true) 之后关闭、setTcpNoDelay(false) 之后恢复")
{
    //连接对象要求有所属的 EventLoop；本用例不建立连接（不调 connectEstablished），loop 只是构造参数
    net::EventLoopThread loopThread;
    net::EventLoop* loop = loopThread.startLoop();
    REQUIRE(loop != NULL);

    SOCKET clientFd = INVALID_SOCKET;
    SOCKET serverFd = INVALID_SOCKET;
    std::string err;
    REQUIRE_MESSAGE(makeLoopbackPair(clientFd, serverFd, err), err);

    {
        //服务端那一端交给 TcpConnection 管理，连接对象析构时关闭它
        net::TcpConnection conn(loop, "ndl-test-conn", serverFd, net::InetAddress(), net::InetAddress());

        //默认开着 Nagle：这正是 RelayServer 改动之前的状态
        CHECK_FALSE(conn.isTcpNoDelay());

        //中转建连时的做法
        conn.setTcpNoDelay(true);
        CHECK(conn.isTcpNoDelay());

        //关掉之后还能恢复，确认读到的是 socket 的实际状态而不是一个固定值
        conn.setTcpNoDelay(false);
        CHECK_FALSE(conn.isTcpNoDelay());
    }

    closesocket(clientFd);
    loopThread.stopLoop();
}
