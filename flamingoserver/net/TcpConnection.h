#pragma once

#include <memory>

#include "Callbacks.h"
#include "Buffer.h"
#include "InetAddress.h"
//#include "EventLoop.h"

// struct tcp_info is in <netinet/tcp.h>
struct tcp_info;

namespace net
{
    class EventLoop;
	class Channel;
	class Socket;

	///
	/// TCP connection, for both client and server usage.
	///
	/// This is an interface class, so don't expose too much details.
	class TcpConnection : public std::enable_shared_from_this<TcpConnection>
	{
	public:
		/// Constructs a TcpConnection with a connected sockfd
		///
		/// User should not create this object.
		TcpConnection(EventLoop* loop,
			            const string& name,
			            int sockfd,
			            const InetAddress& localAddr,
			            const InetAddress& peerAddr);
		~TcpConnection();

		EventLoop* getLoop() const { return loop_; }
		const string& name() const { return name_; }
		const InetAddress& localAddress() const { return localAddr_; }
		const InetAddress& peerAddress() const { return peerAddr_; }
		bool connected() const { return state_ == kConnected; }
		// return true if success.
		bool getTcpInfo(struct tcp_info*) const;
		string getTcpInfoString() const;

		// void send(string&& message); // C++11
		void send(const void* message, int len);
		void send(const string& message);
		// void send(Buffer&& message); // C++11
		void send(Buffer* message);  // this one will swap data
		void shutdown(); // NOT thread safe, no simultaneous calling
		// void shutdownAndForceCloseAfter(double seconds); // NOT thread safe, no simultaneous calling
		void forceClose();

		void setTcpNoDelay(bool on);

		void setConnectionCallback(const ConnectionCallback& cb)
		{
			connectionCallback_ = cb;
		}

		void setMessageCallback(const MessageCallback& cb)
		{
			messageCallback_ = cb;
		}

        //设置成功发完数据执行的回调
		void setWriteCompleteCallback(const WriteCompleteCallback& cb)
		{
			writeCompleteCallback_ = cb;
		}

		void setHighWaterMarkCallback(const HighWaterMarkCallback& cb, size_t highWaterMark)
		{
			highWaterMarkCallback_ = cb; highWaterMark_ = highWaterMark;
		}

		/// Advanced interface
		Buffer* inputBuffer()
		{
			return &inputBuffer_;
		}

		Buffer* outputBuffer()
		{
			return &outputBuffer_;
		}

		/// Internal use only.
		void setCloseCallback(const CloseCallback& cb)
		{
			closeCallback_ = cb;
		}

		// called when TcpServer accepts a new connection
		void connectEstablished();   // should be called only once
		// called when TcpServer has removed me from its map
		void connectDestroyed();  // should be called only once

	private:
		enum StateE { kDisconnected, kConnecting, kConnected, kDisconnecting };
		void handleRead(Timestamp receiveTime);
		void handleWrite();
		void handleClose();
		void handleError();
		// void sendInLoop(string&& message);
		void sendInLoop(const string& message);
		void sendInLoop(const void* message, size_t len);
		void shutdownInLoop();
		// void shutdownAndForceCloseInLoop(double seconds);
		void forceCloseInLoop();
		void setState(StateE s) { state_ = s; }
		const char* stateToString() const;

    private:
		EventLoop*                  loop_;
		const string                name_;
		StateE                      state_;  // FIXME: use atomic variable
		// we don't expose those classes to client.
		std::shared_ptr<Socket>     socket_;
		std::shared_ptr<Channel>    channel_;
		const InetAddress           localAddr_;
		const InetAddress           peerAddr_;
		ConnectionCallback          connectionCallback_;
		MessageCallback             messageCallback_;
		WriteCompleteCallback       writeCompleteCallback_;
		HighWaterMarkCallback       highWaterMarkCallback_;
		CloseCallback               closeCallback_;
		size_t                      highWaterMark_;
		Buffer                      inputBuffer_;
		Buffer                      outputBuffer_; // FIXME: use list<Buffer> as output buffer.

		// FIXME: creationTime_, lastReceiveTime_
		//        bytesReceived_, bytesSent_
	};

	typedef std::shared_ptr<TcpConnection> TcpConnectionPtr;

}
