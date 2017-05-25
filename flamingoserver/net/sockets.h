#pragma once

#include <arpa/inet.h>

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
		explicit Socket(int sockfd)
			: sockfd_(sockfd)
		{ }

		// Socket(Socket&&) // move constructor in C++11
		~Socket();

		int fd() const { return sockfd_; }
		// return true if success.
		bool getTcpInfo(struct tcp_info*) const;
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
		const int sockfd_;
	};

	namespace sockets
	{

		///
		/// Creates a non-blocking socket file descriptor,
		/// abort if any error.
		int createNonblockingOrDie();

		int  connect(int sockfd, const struct sockaddr_in& addr);
		void bindOrDie(int sockfd, const struct sockaddr_in& addr);
		void listenOrDie(int sockfd);
		int  accept(int sockfd, struct sockaddr_in* addr);
		ssize_t read(int sockfd, void *buf, size_t count);
		ssize_t readv(int sockfd, const struct iovec *iov, int iovcnt);
		ssize_t write(int sockfd, const void *buf, size_t count);
		void close(int sockfd);
		void shutdownWrite(int sockfd);

		void toIpPort(char* buf, size_t size,
			const struct sockaddr_in& addr);
		void toIp(char* buf, size_t size,
			const struct sockaddr_in& addr);
		void fromIpPort(const char* ip, uint16_t port,
		struct sockaddr_in* addr);

		int getSocketError(int sockfd);

		const struct sockaddr* sockaddr_cast(const struct sockaddr_in* addr);
		struct sockaddr* sockaddr_cast(struct sockaddr_in* addr);
		const struct sockaddr_in* sockaddr_in_cast(const struct sockaddr* addr);
		struct sockaddr_in* sockaddr_in_cast(struct sockaddr* addr);

		struct sockaddr_in getLocalAddr(int sockfd);
		struct sockaddr_in getPeerAddr(int sockfd);
		bool isSelfConnect(int sockfd);

	}
}
