#pragma once

//SOCKET 类型来自这里：Windows 下是 winsock 的 UINT_PTR，POSIX 下是 int
#include "../base/Platform.h"
#include <memory>

#include "Callbacks.h"
#include "ByteBuffer.h"
#include "InetAddress.h"

// struct tcp_info is in <netinet/tcp.h>
struct tcp_info;

namespace net
{
    class EventLoop;
    class Channel;
    class Socket;

    class TcpConnection : public std::enable_shared_from_this<TcpConnection>
    {
    public:
        TcpConnection(EventLoop* loop,
            const string& name,
            SOCKET sockfd,
            const InetAddress& localAddr,
            const InetAddress& peerAddr);
        ~TcpConnection();

        EventLoop* getLoop() const { return m_loop; }
        const string& name() const { return m_name; }
        const InetAddress& localAddress() const { return m_localAddr; }
        const InetAddress& peerAddress() const { return m_peerAddr; }
        bool connected() const { return m_state == kConnected; }

        void send(const void* message, int len);
        void send(const string& message);
        void send(ByteBuffer* message);  // this one will swap data
        void shutdown();
        void forceClose();

        void setTcpNoDelay(bool on);

        /**
         *  读取本连接的 socket 当前是否关闭了 Nagle 算法（TCP_NODELAY），供测试与日志核对 setTcpNoDelay 是否生效。
         *  任何线程都可以调用（只读取 socket 选项，不改动连接状态）。
         *  @return 已关闭 Nagle 返回 true；未关闭或读取失败返回 false
         */
        bool isTcpNoDelay() const;

        void setConnectionCallback(const ConnectionCallback& cb)
        {
            m_connectionCallback = cb;
        }

        void setMessageCallback(const MessageCallback& cb)
        {
            m_messageCallback = cb;
        }

        //设置成功发完数据执行的回调
        void setWriteCompleteCallback(const WriteCompleteCallback& cb)
        {
            m_writeCompleteCallback = cb;
        }

        void setHighWaterMarkCallback(const HighWaterMarkCallback& cb, size_t highWaterMark)
        {
            m_highWaterMarkCallback = cb; m_highWaterMark = highWaterMark;
        }

        ByteBuffer* inputBuffer()
        {
            return &m_inputBuffer;
        }

        ByteBuffer* outputBuffer()
        {
            return &m_outputBuffer;
        }

        // Internal use only.
        void setCloseCallback(const CloseCallback& cb)
        {
            m_closeCallback = cb;
        }

        void connectEstablished();
        void connectDestroyed();

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
        void setState(StateE s) { m_state = s; }
        const char* stateToString() const;

    private:
        EventLoop* m_loop;
        const string                m_name;
        StateE                      m_state;
        std::unique_ptr<Socket>     m_socket;
        std::unique_ptr<Channel>    m_channel;
        const InetAddress           m_localAddr;
        const InetAddress           m_peerAddr;
        ConnectionCallback          m_connectionCallback;
        MessageCallback             m_messageCallback;
        WriteCompleteCallback       m_writeCompleteCallback;
        HighWaterMarkCallback       m_highWaterMarkCallback;
        CloseCallback               m_closeCallback;
        size_t                      m_highWaterMark;
        ByteBuffer                  m_inputBuffer;
        ByteBuffer                  m_outputBuffer;
    };

    typedef std::shared_ptr<TcpConnection> TcpConnectionPtr;

}
