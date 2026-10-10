#pragma once

#include <stdint.h>
#include "../base/Platform.h"

// struct tcp_info is in <netinet/tcp.h>
struct tcp_info;

namespace net
{
    class InetAddress;

    class Socket
    {
    public:
        explicit Socket(SOCKET sockfd) : m_sockfd(sockfd) { }
        ~Socket();

        SOCKET fd() const { return m_sockfd; }

        void bindAddress(const InetAddress& localaddr);
        void listen();

        SOCKET accept(InetAddress* peeraddr);

        void shutdownWrite();
        void setTcpNoDelay(bool on);
        void setReuseAddr(bool on);
        void setReusePort(bool on);
        void setKeepAlive(bool on);

    private:
        const SOCKET m_sockfd;
    };

    namespace sockets
    {
        SOCKET createOrDie();
        SOCKET createNonblockingOrDie();

        void setNonBlockAndCloseOnExec(SOCKET sockfd);

        void setReuseAddr(SOCKET sockfd, bool on);
        void setReusePort(SOCKET sockfd, bool on);

        int connect(SOCKET sockfd, const struct sockaddr_in& addr);
        void bindOrDie(SOCKET sockfd, const struct sockaddr_in& addr);
        void listenOrDie(SOCKET sockfd);
        SOCKET accept(SOCKET sockfd, struct sockaddr_in* addr);
        int32_t read(SOCKET sockfd, void* buf, int32_t count);
#ifndef WIN32
        ssize_t readv(SOCKET sockfd, const struct iovec* iov, int iovcnt);
#endif
        int32_t write(SOCKET sockfd, const void* buf, int32_t count);
        void close(SOCKET sockfd);
        void shutdownWrite(SOCKET sockfd);

        void toIpPort(char* buf, size_t size, const struct sockaddr_in& addr);
        void toIp(char* buf, size_t size, const struct sockaddr_in& addr);
        void fromIpPort(const char* ip, uint16_t port, struct sockaddr_in* addr);

        int getSocketError(SOCKET sockfd);

        const struct sockaddr* sockaddr_cast(const struct sockaddr_in* addr);
        struct sockaddr* sockaddr_cast(struct sockaddr_in* addr);
        const struct sockaddr_in* sockaddr_in_cast(const struct sockaddr* addr);
        struct sockaddr_in* sockaddr_in_cast(struct sockaddr* addr);

        struct sockaddr_in getLocalAddr(SOCKET sockfd);
        struct sockaddr_in getPeerAddr(SOCKET sockfd);
        bool isSelfConnect(SOCKET sockfd);

        //==================== UDP（docs/P2P通信方案.md 第 2 期 2e，imgserver 的地址探测服务用） ====================

        /**
         *  创建一个非阻塞的 IPv4 UDP 套接字。Windows 上同时关闭 SIO_UDP_CONNRESET：否则向已关闭的端口发包后
         *  收到的 ICMP 端口不可达，会让此后的 recvfrom 报 WSAECONNRESET，一个收发多方的服务端套接字就此无法正常接收。
         *  @return 新套接字；失败返回 INVALID_SOCKET（不终止进程，由调用方记日志后放弃这个端口）
         */
        SOCKET createUdpNonblocking();

        /**
         *  把 UDP 套接字绑定到指定地址。
         *  @param sockfd 套接字
         *  @param addr   本地地址；端口为 0 时由系统分配
         *  @return 成功返回 true；失败（端口被占用、地址不属于本机等）返回 false
         */
        bool bindUdp(SOCKET sockfd, const struct sockaddr_in& addr);

        /**
         *  从 UDP 套接字收一个报文。报文长于缓冲时截断到缓冲大小（两个平台行为一致：Windows 的 WSAEMSGSIZE
         *  在此按截断处理）。
         *  @param sockfd 套接字
         *  @param buf    接收缓冲，不接管所有权
         *  @param count  缓冲大小（字节），须大于 0
         *  @param from   [出参] 来源地址，不得为 NULL
         *  @return 收到的字节数；没有数据时返回 -1 且 lastErrorIsWouldBlock() 为真；其它错误返回 -1
         */
        int32_t recvFrom(SOCKET sockfd, void* buf, int32_t count, struct sockaddr_in* from);

        /**
         *  向指定地址发一个 UDP 报文。
         *  @param sockfd 套接字
         *  @param buf    报文内容，不接管所有权
         *  @param count  报文长度（字节）
         *  @param to     目的地址
         *  @return 发出的字节数；失败返回 -1
         */
        int32_t sendTo(SOCKET sockfd, const void* buf, int32_t count, const struct sockaddr_in& to);

        /**
         *  上一次套接字调用的失败是否只是「暂时没有数据或发送缓冲已满」（EAGAIN / EWOULDBLOCK / WSAEWOULDBLOCK）。
         *  须紧跟在失败的调用之后使用（错误码是线程局部的，中间的其它系统调用会覆盖它）。
         *  @return 是返回 true
         */
        bool lastErrorIsWouldBlock();
    }
}
