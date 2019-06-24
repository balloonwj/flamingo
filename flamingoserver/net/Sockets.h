#pragma once

#include <stdint.h>
#include "../base/Platform.h"

// struct tcp_info is in <netinet/tcp.h>
struct tcp_info;

namespace net
{
	class InetAddress;

	///
	/// Wrapper of socket file descriptor.
	///
	/// It closes the sockfd when desctructs.
	/// It's thread safe, all operations are delagated to OS.
	class Socket
	{
	public:      
		explicit Socket(int sockfd) : sockfd_(sockfd)
		{ }

		// Socket(Socket&&) // move constructor in C++11
		~Socket();

        SOCKET fd() const { return sockfd_; }
		// return true if success.
        //TODO: ÔÝÇÒ×¢ÊÍµô
		//bool getTcpInfo(struct tcp_info*) const;
		bool getTcpInfoString(char* buf, int len) const;

		/// abort if address in use
		void bindAddress(const InetAddress& localaddr);
		/// abort if address in use
		void listen();

		/// On success, returns a non-negative integer that is
		/// a descriptor for the accepted socket, which has been
		/// set to non-blocking and close-on-exec. *peeraddr is assigned.
		/// On error, -1 is returned, and *peeraddr is untouched.
		int accept(InetAddress* peeraddr);

		void shutdownWrite();

		///
		/// Enable/disable TCP_NODELAY (disable/enable Nagle's algorithm).
		///
		void setTcpNoDelay(bool on);

		///
		/// Enable/disable SO_REUSEADDR
		///
		void setReuseAddr(bool on);

		///
		/// Enable/disable SO_REUSEPORT
		///
		void setReusePort(bool on);

		///
		/// Enable/disable SO_KEEPALIVE
		///
		void setKeepAlive(bool on);

	private:
		const SOCKET sockfd_;
	};

	namespace sockets
	{
		///
		/// Creates a socket file descriptor,
		/// abort if any error.
        SOCKET createOrDie();
        SOCKET createNonblockingOrDie();

        void setNonBlockAndCloseOnExec(SOCKET sockfd);

        void setReuseAddr(SOCKET sockfd, bool on);
        void setReusePort(SOCKET sockfd, bool on);

		int  connect(SOCKET sockfd, const struct sockaddr_in& addr);
		void bindOrDie(SOCKET sockfd, const struct sockaddr_in& addr);
		void listenOrDie(SOCKET sockfd);
		int  accept(SOCKET sockfd, struct sockaddr_in* addr);
        int32_t read(SOCKET sockfd, void *buf, int32_t count);
#ifndef WIN32
		ssize_t readv(SOCKET sockfd, const struct iovec *iov, int iovcnt);
#endif
		int32_t write(SOCKET sockfd, const void *buf, int32_t count);
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
	}
}
