#include "Sockets.h"

#include <stdio.h>  // snprintf
#include <string.h>

#include "../base/AsyncLog.h"
#include "../base/Platform.h"
#include "InetAddress.h"
#include "Endian.h"
#include "Callbacks.h"

using namespace net;

Socket::~Socket()
{
    sockets::close(m_sockfd);
}

void Socket::bindAddress(const InetAddress& addr)
{
    sockets::bindOrDie(m_sockfd, addr.getSockAddrInet());
}

void Socket::listen()
{
    sockets::listenOrDie(m_sockfd);
}

SOCKET Socket::accept(InetAddress* peeraddr)
{
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    SOCKET connfd = sockets::accept(m_sockfd, &addr);
    if (connfd >= 0)
    {
        peeraddr->setSockAddrInet(addr);
    }
    return connfd;
}

void Socket::shutdownWrite()
{
    sockets::shutdownWrite(m_sockfd);
}

void Socket::setTcpNoDelay(bool on)
{
    int optval = on ? 1 : 0;
#ifdef WIN32
    ::setsockopt(m_sockfd, IPPROTO_TCP, TCP_NODELAY, (char*)&optval, sizeof(optval));
#else
    ::setsockopt(m_sockfd, IPPROTO_TCP, TCP_NODELAY, &optval, static_cast<socklen_t>(sizeof optval));
#endif
    // FIXME CHECK
}

bool Socket::getTcpNoDelay() const
{
    //先清零：Windows 的 getsockopt 对 TCP_NODELAY 可能只写回 1 个字节，其余字节须为 0 才能按整数判断
    int optval = 0;
#ifdef WIN32
    int optlen = static_cast<int>(sizeof(optval));
    if (::getsockopt(m_sockfd, IPPROTO_TCP, TCP_NODELAY, (char*)&optval, &optlen) != 0)
        return false;
#else
    socklen_t optlen = static_cast<socklen_t>(sizeof(optval));
    if (::getsockopt(m_sockfd, IPPROTO_TCP, TCP_NODELAY, &optval, &optlen) != 0)
        return false;
#endif
    return optval != 0;
}

void Socket::setReuseAddr(bool on)
{
    sockets::setReuseAddr(m_sockfd, on);
}

void Socket::setReusePort(bool on)
{
    sockets::setReusePort(m_sockfd, on);
}

void Socket::setKeepAlive(bool on)
{
#ifdef WIN32
    //TODO: 补全Windows的写法
#else
    int optval = on ? 1 : 0;
    ::setsockopt(m_sockfd, SOL_SOCKET, SO_KEEPALIVE, &optval, static_cast<socklen_t>(sizeof optval));
#endif
    // FIXME CHECK
}

//namespace
//{
//  //typedef struct sockaddr SA;
//
//  
//
//}

const struct sockaddr* sockets::sockaddr_cast(const struct sockaddr_in* addr)
{
    return static_cast<const struct sockaddr*>((const void*)(addr));
}

struct sockaddr* sockets::sockaddr_cast(struct sockaddr_in* addr)
{
    return static_cast<struct sockaddr*>((void*)(addr));
}

const struct sockaddr_in* sockets::sockaddr_in_cast(const struct sockaddr* addr)
{
    return static_cast<const struct sockaddr_in*>((const void*)(addr));
}

struct sockaddr_in* sockets::sockaddr_in_cast(struct sockaddr* addr)
{
    return static_cast<struct sockaddr_in*>((void*)(addr));
}

SOCKET sockets::createOrDie()
{
#ifdef WIN32
    SOCKET sockfd = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sockfd == INVALID_SOCKET)
    {
        LOGF("sockets::createNonblockingOrDie");
    }
#else
    SOCKET sockfd = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, IPPROTO_TCP);
    if (sockfd == INVALID_SOCKET)
    {
        LOGF("sockets::createNonblockingOrDie");
    }
#endif

    return sockfd;
}

SOCKET sockets::createNonblockingOrDie()
{
#ifdef WIN32
    SOCKET sockfd = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sockfd == INVALID_SOCKET)
    {
        LOGF("sockets::createNonblockingOrDie");
    }
#else
    SOCKET sockfd = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, IPPROTO_TCP);
    if (sockfd == INVALID_SOCKET)
    {
        LOGF("sockets::createNonblockingOrDie");
    }
#endif

    setNonBlockAndCloseOnExec(sockfd);
    return sockfd;
}

void sockets::setNonBlockAndCloseOnExec(SOCKET sockfd)
{
#ifdef WIN32
    //将socket设置成非阻塞的
    unsigned long on = 1;
    ::ioctlsocket(sockfd, FIONBIO, &on);
#else
    // non-block
    int flags = ::fcntl(sockfd, F_GETFL, 0);
    flags |= O_NONBLOCK;
    int ret = ::fcntl(sockfd, F_SETFL, flags);
    // FIXME check

    // close-on-exec
    flags = ::fcntl(sockfd, F_GETFD, 0);
    flags |= FD_CLOEXEC;
    ret = ::fcntl(sockfd, F_SETFD, flags);
    // FIXME check

    (void)ret;
#endif       
}

void sockets::bindOrDie(SOCKET sockfd, const struct sockaddr_in& addr)
{
    int ret = ::bind(sockfd, sockaddr_cast(&addr), static_cast<socklen_t>(sizeof addr));
    if (ret == SOCKET_ERROR)
    {
        LOGF("sockets::bindOrDie");
    }
}

void sockets::listenOrDie(SOCKET sockfd)
{
    int ret = ::listen(sockfd, SOMAXCONN);
    if (ret == SOCKET_ERROR)
    {
        LOGF("sockets::listenOrDie");
    }
}

SOCKET sockets::accept(SOCKET sockfd, struct sockaddr_in* addr)
{
    socklen_t addrlen = static_cast<socklen_t>(sizeof * addr);
#ifdef WIN32
    SOCKET connfd = ::accept(sockfd, sockaddr_cast(addr), &addrlen);
    setNonBlockAndCloseOnExec(connfd);
#else  
    SOCKET connfd = ::accept4(sockfd, sockaddr_cast(addr), &addrlen, SOCK_NONBLOCK | SOCK_CLOEXEC);
#endif
    if (connfd == SOCKET_ERROR)
    {
#ifdef WIN32
        int savedErrno = ::WSAGetLastError();
        LOGSYSE("Socket::accept");
        if (savedErrno != WSAEWOULDBLOCK)
            LOGF("unexpected error of ::accept %d", savedErrno);
#else
        int savedErrno = errno;
        LOGSYSE("Socket::accept");
        switch (savedErrno)
        {
        case EAGAIN:
        case ECONNABORTED:
        case EINTR:
        case EPROTO: // ???
        case EPERM:
        case EMFILE: // per-process lmit of open file desctiptor ???
            // expected errors
            errno = savedErrno;
            break;
        case EBADF:
        case EFAULT:
        case EINVAL:
        case ENFILE:
        case ENOBUFS:
        case ENOMEM:
        case ENOTSOCK:
        case EOPNOTSUPP:
            // unexpected errors
            LOGF("unexpected error of ::accept %d", savedErrno);
            break;
        default:
            LOGF("unknown error of ::accept %d", savedErrno);
            break;
        }

#endif
    }

    return connfd;
}

void sockets::setReuseAddr(SOCKET sockfd, bool on)
{
    int optval = on ? 1 : 0;
#ifdef WIN32
    ::setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (char*)&optval, sizeof(optval));
#else
    ::setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &optval, static_cast<socklen_t>(sizeof optval));
#endif
    // FIXME CHECK
}

void sockets::setReusePort(SOCKET sockfd, bool on)
{
    //Windows 系统没有 SO_REUSEPORT 选项
#ifndef WIN32
    int optval = on ? 1 : 0;
    int ret = ::setsockopt(sockfd, SOL_SOCKET, SO_REUSEPORT, &optval, static_cast<socklen_t>(sizeof optval));
    if (ret < 0 && on)
    {
        LOGSYSE("SO_REUSEPORT failed.");
    }
#endif
}

int sockets::connect(SOCKET sockfd, const struct sockaddr_in& addr)
{
    return ::connect(sockfd, sockaddr_cast(&addr), static_cast<socklen_t>(sizeof addr));
}

int32_t sockets::read(SOCKET sockfd, void* buf, int32_t count)
{
#ifdef WIN32
    return ::recv(sockfd, (char*)buf, count, 0);
#else
    return ::read(sockfd, buf, count);
#endif
}

#ifndef WIN32
ssize_t sockets::readv(SOCKET sockfd, const struct iovec* iov, int iovcnt)
{
    return ::readv(sockfd, iov, iovcnt);
}
#endif

int32_t sockets::write(SOCKET sockfd, const void* buf, int32_t count)
{
#ifdef WIN32
    return ::send(sockfd, (const char*)buf, count, 0);
#else
    return ::write(sockfd, buf, count);
#endif

}

void sockets::close(SOCKET sockfd)
{
#ifdef WIN32   
    if (::closesocket(sockfd) < 0)
#else
    if (::close(sockfd) < 0)
#endif
    {
        LOGSYSE("sockets::close, fd=%d, errno=%d, errorinfo=%s", sockfd, errno, strerror(errno));
    }
}

void sockets::shutdownWrite(SOCKET sockfd)
{
#ifdef WIN32
    if (::shutdown(sockfd, SD_SEND) < 0)
#else
    if (::shutdown(sockfd, SHUT_WR) < 0)
#endif        
    {
        LOGSYSE("sockets::shutdownWrite");
    }
}

void sockets::toIpPort(char* buf, size_t size, const struct sockaddr_in& addr)
{
    //if (size >= sizeof(struct sockaddr_in))
    //    return;

    ::inet_ntop(AF_INET, &addr.sin_addr, buf, static_cast<socklen_t>(size));
    size_t end = ::strlen(buf);
    uint16_t port = sockets::networkToHost16(addr.sin_port);
    //if (size > end)
    //    return;

    snprintf(buf + end, size - end, ":%u", port);
}

void sockets::toIp(char* buf, size_t size, const struct sockaddr_in& addr)
{
    if (size >= sizeof(struct sockaddr_in))
        return;

    ::inet_ntop(AF_INET, &addr.sin_addr, buf, static_cast<socklen_t>(size));
}

void sockets::fromIpPort(const char* ip, uint16_t port, struct sockaddr_in* addr)
{
    addr->sin_family = AF_INET;
    //TODO: 校验下写的对不对
#ifdef WIN32
    addr->sin_port = htons(port);
#else
    addr->sin_port = htobe16(port);
#endif
    if (::inet_pton(AF_INET, ip, &addr->sin_addr) <= 0)
    {
        LOGSYSE("sockets::fromIpPort");
    }
}

int sockets::getSocketError(SOCKET sockfd)
{
    int optval;
#ifdef WIN32
    int optvallen = sizeof(optval);
    if (::getsockopt(sockfd, SOL_SOCKET, SO_ERROR, (char*)&optval, &optvallen) < 0)
        return ::WSAGetLastError();
#else
    socklen_t optlen = static_cast<socklen_t>(sizeof optval);

    if (::getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &optval, &optlen) < 0)
        return errno;
#endif
    return optval;
}

struct sockaddr_in sockets::getLocalAddr(SOCKET sockfd)
{
    struct sockaddr_in localaddr = { 0 };
    memset(&localaddr, 0, sizeof localaddr);
    socklen_t addrlen = static_cast<socklen_t>(sizeof localaddr);
    ::getsockname(sockfd, sockaddr_cast(&localaddr), &addrlen);
    //if (::getsockname(sockfd, sockaddr_cast(&localaddr), &addrlen) < 0)
    //{
    //  LOG_SYSERR << "sockets::getLocalAddr";
    //  return 
    //}
    return localaddr;
}

struct sockaddr_in sockets::getPeerAddr(SOCKET sockfd)
{
    struct sockaddr_in peeraddr = { 0 };
    memset(&peeraddr, 0, sizeof peeraddr);
    socklen_t addrlen = static_cast<socklen_t>(sizeof peeraddr);
    ::getpeername(sockfd, sockaddr_cast(&peeraddr), &addrlen);
    //if (::getpeername(sockfd, sockaddr_cast(&peeraddr), &addrlen) < 0)
    //{
    //  LOG_SYSERR << "sockets::getPeerAddr";
    //}
    return peeraddr;
}

bool sockets::isSelfConnect(SOCKET sockfd)
{
    struct sockaddr_in localaddr = getLocalAddr(sockfd);
    struct sockaddr_in peeraddr = getPeerAddr(sockfd);
    return localaddr.sin_port == peeraddr.sin_port && localaddr.sin_addr.s_addr == peeraddr.sin_addr.s_addr;
}

//==================== UDP（docs/P2P通信方案.md 第 2 期 2e） ====================

#ifdef WIN32
//SIO_UDP_CONNRESET 定义在 mstcpip.h 里；为了不在这里引入那个头文件，按它的定义（_WSAIOW(IOC_VENDOR, 12)）给出
#ifndef SIO_UDP_CONNRESET
#define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)
#endif
#endif

SOCKET sockets::createUdpNonblocking()
{
#ifdef WIN32
    SOCKET sockfd = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sockfd == INVALID_SOCKET)
    {
        LOGE("sockets::createUdpNonblocking socket failed, error: %d", ::WSAGetLastError());
        return INVALID_SOCKET;
    }
    //关闭「ICMP 端口不可达转成 WSAECONNRESET」的行为，原因见 Sockets.h 的说明
    BOOL reportConnReset = FALSE;
    DWORD bytesReturned = 0;
    if (::WSAIoctl(sockfd, SIO_UDP_CONNRESET, &reportConnReset, sizeof(reportConnReset), NULL, 0, &bytesReturned,
                   NULL, NULL) == SOCKET_ERROR)
    {
        LOGW("sockets::createUdpNonblocking disable SIO_UDP_CONNRESET failed, error: %d", ::WSAGetLastError());
    }
#else
    SOCKET sockfd = ::socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, IPPROTO_UDP);
    if (sockfd == INVALID_SOCKET)
    {
        LOGE("sockets::createUdpNonblocking socket failed, errno: %d", errno);
        return INVALID_SOCKET;
    }
#endif
    setNonBlockAndCloseOnExec(sockfd);
    return sockfd;
}

bool sockets::bindUdp(SOCKET sockfd, const struct sockaddr_in& addr)
{
    if (::bind(sockfd, sockaddr_cast(&addr), static_cast<socklen_t>(sizeof addr)) == SOCKET_ERROR)
    {
        char ipPort[64] = { 0 };
        toIpPort(ipPort, sizeof(ipPort), addr);
#ifdef WIN32
        LOGE("sockets::bindUdp failed, addr: %s, error: %d", ipPort, ::WSAGetLastError());
#else
        LOGE("sockets::bindUdp failed, addr: %s, errno: %d", ipPort, errno);
#endif
        return false;
    }
    return true;
}

int32_t sockets::recvFrom(SOCKET sockfd, void* buf, int32_t count, struct sockaddr_in* from)
{
    memset(from, 0, sizeof(*from));
    socklen_t addrlen = static_cast<socklen_t>(sizeof(*from));
#ifdef WIN32
    const int n = ::recvfrom(sockfd, (char*)buf, count, 0, sockaddr_cast(from), &addrlen);
    //报文长于缓冲：Windows 返回 SOCKET_ERROR 并置 WSAEMSGSIZE，但缓冲里已经填好截断后的数据。
    //按截断处理，与 Linux（直接返回截断后的长度）保持一致
    if (n == SOCKET_ERROR && ::WSAGetLastError() == WSAEMSGSIZE)
        return count;
    return n;
#else
    const ssize_t n = ::recvfrom(sockfd, buf, static_cast<size_t>(count), 0, sockaddr_cast(from), &addrlen);
    return static_cast<int32_t>(n);
#endif
}

int32_t sockets::sendTo(SOCKET sockfd, const void* buf, int32_t count, const struct sockaddr_in& to)
{
#ifdef WIN32
    return ::sendto(sockfd, (const char*)buf, count, 0, sockaddr_cast(&to), static_cast<socklen_t>(sizeof to));
#else
    return static_cast<int32_t>(::sendto(sockfd, buf, static_cast<size_t>(count), 0, sockaddr_cast(&to),
                                         static_cast<socklen_t>(sizeof to)));
#endif
}

bool sockets::lastErrorIsWouldBlock()
{
#ifdef WIN32
    return ::WSAGetLastError() == WSAEWOULDBLOCK;
#else
    return errno == EAGAIN || errno == EWOULDBLOCK;
#endif
}
