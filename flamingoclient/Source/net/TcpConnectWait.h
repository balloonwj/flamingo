/**
 *  带超时的非阻塞 TCP connect，TcpConnectWait.h
 *
 *  四个中转客户端（屏幕共享 ShareRelayClient、一对一通话 CallRelayClient、远程桌面 RdRelayClient、
 *  文件夹传输 FolderRelayClient）与 CIUSocket 的主连接、文件连接、图片连接共用这一份实现。
 *
 *  以前各处都只用 select 等「可写」。按 Winsock 的约定，非阻塞 connect 成功时 socket 进入可写集合，
 *  失败（对方拒绝、网络或主机不可达）时只进入异常集合，不进入可写集合；只等可写的写法在端口被拒绝时
 *  也要等满超时，日志里也分不清是「被拒绝」还是「超时」。2026-10-12 服务端把中转地址误配成 127.0.0.1，
 *  发起屏幕共享的客户端等满 5 秒才报「connect relay 127.0.0.1:20003 failed」，就是这种情况。
 *  本函数同时等待可写集合与异常集合：失败时立即返回并带回 WSA 错误码，超时与失败可以区分。
 *  注意 Windows 在本机回环上收到 RST 后还会重试 SYN，连接被拒绝仍需约 2 秒才会报告（2026-10-12 实测）。
 *
 *  只有头文件，不需要把实现文件加进各个工程（exe、FlamingoMedia、FlamingoFileShare、FlamingoUnit 都直接包含）。
 *
 *  典型用法：
 *      //socket 已设为非阻塞模式
 *      int err = 0;
 *      net::tcp_connect_result r = net::ConnectWithTimeout(sock, addr, timeoutSec * 1000, err);
 *      if (r != net::tcp_connect_ok)
 *          LOG_ERROR("connect %s:%d %s.", ip, port, net::DescribeTcpConnect(r, err, timeoutMs).c_str());
 *
 *  2026-10-14 起另有 ConnectBlockingSocket（解析地址、建 socket、带超时 connect、切回阻塞并设收发超时）与
 *  SendAllBlocking，四个中转客户端的建连与发送都改用它们（docs/远程桌面控制权限与观看工具条方案.md 8.2 第 6 条）。
 *
 *  单元测试见 Source/Tests/TcpConnectWaitTests.cpp（标签 [tcpconnect]）。
 *
 *  balloonwj@qq.com   2026-10-12
 */
#pragma once

#include <winsock2.h>
#include <stdio.h>
#include <string.h>
#include <string>

namespace net {

//非阻塞 connect 的结果
enum tcp_connect_result
{
    tcp_connect_ok      = 0,   //连接已建立
    tcp_connect_failed  = 1,   //连接失败：对方拒绝（端口无人监听）、网络或主机不可达等，具体原因见错误码
    tcp_connect_timeout = 2,   //在等待上限内既没有连上，也没有收到失败（典型如防火墙丢弃了报文）
};

//每秒的毫秒数，换算超时
const int kTcpConnectMsPerSecond = 1000;

//每毫秒的微秒数，换算 select 的 timeval
const int kTcpConnectMicrosPerMilli = 1000;

//描述文字的缓冲区字节数：最长的一种是「timed out after <int> ms」，64 字节足够
const size_t kTcpConnectDescribeBufBytes = 64;

/**
 *  在一个已设为非阻塞模式的 socket 上发起 connect，并在 timeoutMs 毫秒内等待结果。
 *  @param sock      已创建、已设为非阻塞模式的 TCP socket（借用，不关闭；返回后仍为非阻塞模式）
 *  @param addr      目标地址（IPv4）
 *  @param timeoutMs 等待上限，单位毫秒，须大于 0
 *  @param errorCode 输出：返回 tcp_connect_failed 时为 WSA 错误码（如 WSAECONNREFUSED），其余情况为 0
 *  @return 连接结果，见 tcp_connect_result
 */
inline tcp_connect_result ConnectWithTimeout(SOCKET sock, const struct sockaddr_in& addr, int timeoutMs,
                                             int& errorCode)
{
    errorCode = 0;
    const int ret = ::connect(sock, reinterpret_cast<const struct sockaddr*>(&addr), sizeof(addr));
    if (ret == 0)
        return tcp_connect_ok;
    const int lastError = ::WSAGetLastError();
    if (lastError != WSAEWOULDBLOCK)
    {
        errorCode = lastError;
        return tcp_connect_failed;
    }

    //连接成功时进入可写集合，失败时进入异常集合，两个集合都要等
    fd_set writeset;
    FD_ZERO(&writeset);
    FD_SET(sock, &writeset);
    fd_set exceptset;
    FD_ZERO(&exceptset);
    FD_SET(sock, &exceptset);
    struct timeval tv;
    tv.tv_sec = timeoutMs / kTcpConnectMsPerSecond;
    tv.tv_usec = (timeoutMs % kTcpConnectMsPerSecond) * kTcpConnectMicrosPerMilli;
    const int n = ::select(0, NULL, &writeset, &exceptset, &tv);
    if (n == 0)
        return tcp_connect_timeout;
    if (n == SOCKET_ERROR)
    {
        errorCode = ::WSAGetLastError();
        return tcp_connect_failed;
    }
    if (FD_ISSET(sock, &exceptset))
    {
        //失败原因保存在 socket 的 SO_ERROR 里；取不到时记下 getsockopt 自己的错误码
        int soError = 0;
        int soErrorLen = sizeof(soError);
        if (::getsockopt(sock, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&soError), &soErrorLen) != 0)
            soError = ::WSAGetLastError();
        errorCode = soError;
        return tcp_connect_failed;
    }
    return tcp_connect_ok;
}

/**
 *  把连接结果写成一段英文描述，供日志使用。
 *  @param result    ConnectWithTimeout 的返回值
 *  @param errorCode ConnectWithTimeout 带回的错误码
 *  @param timeoutMs 调用时给的等待上限（毫秒），超时的描述里用到
 *  @return 成功为 "connected"；被拒绝为 "refused (error 10061)"；其他失败为 "failed (error <错误码>)"；
 *          超时为 "timed out after <timeoutMs> ms"
 */
inline std::string DescribeTcpConnect(tcp_connect_result result, int errorCode, int timeoutMs)
{
    char buf[kTcpConnectDescribeBufBytes];
    switch (result)
    {
    //连接已建立
    case tcp_connect_ok:
        return "connected";

    //连接失败：目标端口无人监听（被拒绝）时单独写明，其余原因只写错误码
    case tcp_connect_failed:
        if (errorCode == WSAECONNREFUSED)
            _snprintf_s(buf, sizeof(buf), _TRUNCATE, "refused (error %d)", errorCode);
        else
            _snprintf_s(buf, sizeof(buf), _TRUNCATE, "failed (error %d)", errorCode);
        return buf;

    //等满了上限仍没有结果
    case tcp_connect_timeout:
        _snprintf_s(buf, sizeof(buf), _TRUNCATE, "timed out after %d ms", timeoutMs);
        return buf;

    //不会出现的取值：原样写出数值，便于排查
    default:
        _snprintf_s(buf, sizeof(buf), _TRUNCATE, "unknown result %d (error %d)", static_cast<int>(result),
                    errorCode);
        return buf;
    }
}

/**
 *  建立一条阻塞模式、带收发超时的 TCP 连接（四个中转客户端共用，2026-10-14 起；此前各写一份相同的代码）：
 *  解析地址（点分十进制直接用，否则按主机名解析），建 socket，关掉 Nagle（画面帧、文件块是一串连续的分片，
 *  不能被攒着不发），用非阻塞模式做带超时的 connect（ConnectWithTimeout），连上后切回阻塞模式并设收发超时。
 *  @param sock          [出参] socket 一建好就写到这里（调用方的成员变量，便于别的线程照旧通过关闭它打断连接）；
 *                       失败时可能已是有效的 socket，由调用方关闭（与各客户端原来的做法相同），未建起时为 INVALID_SOCKET
 *  @param host          主机：点分十进制 IPv4 或主机名
 *  @param port          端口
 *  @param timeoutMs     连接的等待上限，单位毫秒，须大于 0
 *  @param recvTimeoutMs 连上后的接收超时 SO_RCVTIMEO，单位毫秒（接收线程据此周期返回、检查停止标志）
 *  @param sendTimeoutMs 连上后的发送超时 SO_SNDTIMEO，单位毫秒
 *  @param failure       [出参] 失败原因（英文，供日志使用）：建 socket 失败、主机名解析失败、切换模式失败，
 *                       或 DescribeTcpConnect 的描述；成功时为空
 *  @return 连上返回 true
 */
inline bool ConnectBlockingSocket(SOCKET& sock, const std::string& host, int port, int timeoutMs, int recvTimeoutMs,
                                  int sendTimeoutMs, std::string& failure)
{
    failure.clear();
    sock = ::socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET)
    {
        failure = "socket() failed";
        return false;
    }
    long noDelay = 1;
    ::setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&noDelay), sizeof(long));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<u_short>(port));
    addr.sin_addr.s_addr = ::inet_addr(host.c_str());
    if (addr.sin_addr.s_addr == INADDR_NONE)
    {
        //不是点分十进制，按主机名解析
        struct hostent* he = ::gethostbyname(host.c_str());
        if (he == NULL)
        {
            failure = "host name not resolved";
            return false;
        }
        addr.sin_addr.s_addr = *reinterpret_cast<unsigned long*>(he->h_addr);
    }

    //先用非阻塞模式做带超时的 connect：被拒绝、不可达时立即返回，失败原因与超时分开
    unsigned long on = 1;
    if (::ioctlsocket(sock, FIONBIO, &on) == SOCKET_ERROR)
    {
        failure = "ioctlsocket(FIONBIO) failed";
        return false;
    }
    int connectError = 0;
    const tcp_connect_result result = ConnectWithTimeout(sock, addr, timeoutMs, connectError);
    if (result != tcp_connect_ok)
    {
        failure = DescribeTcpConnect(result, connectError, timeoutMs);
        return false;
    }

    //连上后切回阻塞模式，并设收发超时
    unsigned long off = 0;
    ::ioctlsocket(sock, FIONBIO, &off);
    int tmRecv = recvTimeoutMs;
    int tmSend = sendTimeoutMs;
    ::setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&tmRecv), sizeof(int));
    ::setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&tmSend), sizeof(int));
    return true;
}

/**
 *  在阻塞模式的 socket 上把 data 全部发出（四个中转客户端共用，2026-10-14）。阻塞模式下 WSAEWOULDBLOCK 罕见，
 *  出现时重试；其余错误（含发送超时）按断开处理。
 *  @param sock 已连上的阻塞模式 socket（借用）
 *  @param data 要发送的字节
 *  @param len  字节数
 *  @return 全部发出返回 true；出错返回 false
 */
inline bool SendAllBlocking(SOCKET sock, const char* data, size_t len)
{
    size_t sent = 0;
    while (sent < len)
    {
        const int n = ::send(sock, data + sent, static_cast<int>(len - sent), 0);
        if (n > 0)
        {
            sent += static_cast<size_t>(n);
            continue;
        }
        if (n == SOCKET_ERROR && ::WSAGetLastError() == WSAEWOULDBLOCK)
            continue;
        return false;
    }
    return true;
}

} // namespace net
