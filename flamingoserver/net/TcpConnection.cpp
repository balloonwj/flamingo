#include "TcpConnection.h"

#include <functional>
#include <thread>
#include <sstream>
#include <errno.h>
#include "../base/Platform.h"
#include "../base/AsyncLog.h"
#include "Sockets.h"
#include "EventLoop.h"
#include "Channel.h"

using namespace net;

namespace {

/**
 *  判断一个 socket 错误码是不是「暂时做不了、等下一次事件再来」这一类。
 *
 *  非阻塞 socket 上它是<u>正常现象</u>而不是错误：无数据可读的 recv、发送缓冲已满的 send
 *  都会返回 -1 并带上这个错误码。据此断开连接会把一条健康的连接判死 —— 2026-08-16
 *  修复的那次「客户端读得慢时服务端拆连接」的故障里，这就是最后一环。
 *
 *  @param savedErrno 系统错误码。Windows 上取自 WSAGetLastError()，POSIX 上取自 errno
 *  @return 属于该类返回 true，调用方应当直接返回、等下一次事件
 */
bool isWouldBlockError(int savedErrno)
{
#ifdef WIN32
    //winsock 只有 WSAEWOULDBLOCK 这一个码表示该含义，且不会返回 POSIX 的 EAGAIN
    return savedErrno == WSAEWOULDBLOCK;
#else
    //POSIX 允许 EAGAIN 与 EWOULDBLOCK 取相同值（Linux 上就相同），也允许取不同值，故两个都判
    return savedErrno == EAGAIN || savedErrno == EWOULDBLOCK;
#endif
}

} // namespace

void net::defaultConnectionCallback(const TcpConnectionPtr& conn)
{
    LOGD("%s -> is %s",
        conn->localAddress().toIpPort().c_str(),
        conn->peerAddress().toIpPort().c_str(),
        (conn->connected() ? "UP" : "DOWN"));
    // do not call conn->forceClose(), because some users want to register message callback only.
}

void net::defaultMessageCallback(const TcpConnectionPtr&, ByteBuffer* buf, Timestamp)
{
    buf->retrieveAll();
}

TcpConnection::TcpConnection(EventLoop* loop, const string& nameArg, SOCKET sockfd, const InetAddress& localAddr, const InetAddress& peerAddr)
    : m_loop(loop),
    m_name(nameArg),
    m_state(kConnecting),
    m_socket(new Socket(sockfd)),
    m_channel(new Channel(loop, sockfd)),
    m_localAddr(localAddr),
    m_peerAddr(peerAddr),
    m_highWaterMark(64 * 1024 * 1024)
{
    m_channel->setReadCallback(std::bind(&TcpConnection::handleRead, this, std::placeholders::_1));
    m_channel->setWriteCallback(std::bind(&TcpConnection::handleWrite, this));
    m_channel->setCloseCallback(std::bind(&TcpConnection::handleClose, this));
    m_channel->setErrorCallback(std::bind(&TcpConnection::handleError, this));
    LOGD("TcpConnection::ctor[%s] at 0x%x fd=%d", m_name.c_str(), this, sockfd);
    m_socket->setKeepAlive(true);
}

TcpConnection::~TcpConnection()
{
    LOGD("TcpConnection::dtor[%s] at 0x%x fd=%d state=%s",
        m_name.c_str(), this, m_channel->fd(), stateToString());
    //assert(state_ == kDisconnected);
}

void TcpConnection::send(const void* data, int len)
{
    if (m_state == kConnected)
    {
        if (m_loop->isInLoopThread())
        {
            sendInLoop(data, len);
        }
        else
        {
            string message(static_cast<const char*>(data), len);
            m_loop->runInLoop(
                std::bind(static_cast<void (TcpConnection::*)(const string&)>(&TcpConnection::sendInLoop),
                    this,     // FIXME
                    message));
        }
    }
}

void TcpConnection::send(const string& message)
{
    if (m_state == kConnected)
    {
        if (m_loop->isInLoopThread())
        {
            sendInLoop(message);
        }
        else
        {
            m_loop->runInLoop(
                std::bind(static_cast<void (TcpConnection::*)(const string&)>(&TcpConnection::sendInLoop),
                    this,     // FIXME
                    message));
            //std::forward<string>(message)));
        }
    }
}

// FIXME efficiency!!!
void TcpConnection::send(ByteBuffer* buf)
{
    if (m_state == kConnected)
    {
        if (m_loop->isInLoopThread())
        {
            sendInLoop(buf->peek(), buf->readableBytes());
            buf->retrieveAll();
        }
        else
        {
            m_loop->runInLoop(
                std::bind(static_cast<void (TcpConnection::*)(const string&)>(&TcpConnection::sendInLoop),
                    this,     // FIXME
                    buf->retrieveAllAsString()));
            //std::forward<string>(message)));
        }
    }
}

void TcpConnection::sendInLoop(const string& message)
{
    sendInLoop(message.c_str(), message.size());
}

void TcpConnection::sendInLoop(const void* data, size_t len)
{
    m_loop->assertInLoopThread();
    int32_t nwrote = 0;
    size_t remaining = len;
    bool faultError = false;
    if (m_state == kDisconnected)
    {
        LOGW("disconnected, give up writing");
        return;
    }
    // if no thing in output queue, try writing directly
    if (!m_channel->isWriting() && m_outputBuffer.readableBytes() == 0)
    {
        nwrote = sockets::write(m_channel->fd(), data, static_cast<int32_t>(len));
        //TODO: 打印threadid用于调试，后面去掉
        //std::stringstream ss;
        //ss << std::this_thread::get_id();
        //LOGI << "send data in threadID = " << ss;

        if (nwrote >= 0)
        {
            remaining = len - nwrote;
            if (remaining == 0 && m_writeCompleteCallback)
            {
                m_loop->queueInLoop(std::bind(m_writeCompleteCallback, shared_from_this()));
            }
        }
        else // nwrote < 0
        {
            nwrote = 0;
            if (errno != EWOULDBLOCK)
            {
                LOGSYSE("TcpConnection::sendInLoop");
                if (errno == EPIPE || errno == ECONNRESET) // FIXME: any others?
                {
                    faultError = true;
                }
            }
        }
    }

    //assert(remaining <= len);
    if (remaining > len)
        return;

    if (!faultError && remaining > 0)
    {
        size_t oldLen = m_outputBuffer.readableBytes();
        if (oldLen + remaining >= m_highWaterMark
            && oldLen < m_highWaterMark
            && m_highWaterMarkCallback)
        {
            m_loop->queueInLoop(std::bind(m_highWaterMarkCallback, shared_from_this(), oldLen + remaining));
        }
        m_outputBuffer.append(static_cast<const char*>(data) + nwrote, remaining);
        if (!m_channel->isWriting())
        {
            m_channel->enableWriting();
        }
    }
}

void TcpConnection::shutdown()
{
    // FIXME: use compare and swap
    if (m_state == kConnected)
    {
        setState(kDisconnecting);
        // FIXME: shared_from_this()?
        m_loop->runInLoop(std::bind(&TcpConnection::shutdownInLoop, this));
    }
}

void TcpConnection::shutdownInLoop()
{
    m_loop->assertInLoopThread();
    if (!m_channel->isWriting())
    {
        // we are not writing
        m_socket->shutdownWrite();
    }
}

// void TcpConnection::shutdownAndForceCloseAfter(double seconds)
// {
//   // FIXME: use compare and swap
//   if (state_ == kConnected)
//   {
//     setState(kDisconnecting);
//     loop_->runInLoop(boost::bind(&TcpConnection::shutdownAndForceCloseInLoop, this, seconds));
//   }
// }

// void TcpConnection::shutdownAndForceCloseInLoop(double seconds)
// {
//   loop_->assertInLoopThread();
//   if (!channel_->isWriting())
//   {
//     // we are not writing
//     socket_->shutdownWrite();
//   }
//   loop_->runAfter(
//       seconds,
//       makeWeakCallback(shared_from_this(),
//                        &TcpConnection::forceCloseInLoop));
// }

void TcpConnection::forceClose()
{
    // FIXME: use compare and swap
    if (m_state == kConnected || m_state == kDisconnecting)
    {
        setState(kDisconnecting);
        m_loop->queueInLoop(std::bind(&TcpConnection::forceCloseInLoop, shared_from_this()));
    }
}


void TcpConnection::forceCloseInLoop()
{
    m_loop->assertInLoopThread();
    if (m_state == kConnected || m_state == kDisconnecting)
    {
        // as if we received 0 byte in handleRead();
        handleClose();
    }
}

const char* TcpConnection::stateToString() const
{
    switch (m_state)
    {
    case kDisconnected:
        return "kDisconnected";
    case kConnecting:
        return "kConnecting";
    case kConnected:
        return "kConnected";
    case kDisconnecting:
        return "kDisconnecting";
    default:
        return "unknown state";
    }
}

void TcpConnection::setTcpNoDelay(bool on)
{
    m_socket->setTcpNoDelay(on);
}

bool TcpConnection::isTcpNoDelay() const
{
    return m_socket->getTcpNoDelay();
}

void TcpConnection::connectEstablished()
{
    m_loop->assertInLoopThread();
    if (m_state != kConnecting)
    {
        //一定不能走这个分支
        return;
    }

    setState(kConnected);

    //假如正在执行这行代码时，对端关闭了连接
    if (!m_channel->enableReading())
    {
        LOGE("enableReading failed.");
        //setState(kDisconnected);
        handleClose();
        return;
    }

    //connectionCallback_指向void XXServer::OnConnection(const std::shared_ptr<TcpConnection>& conn)
    m_connectionCallback(shared_from_this());
}

void TcpConnection::connectDestroyed()
{
    m_loop->assertInLoopThread();
    if (m_state == kConnected)
    {
        setState(kDisconnected);
        m_channel->disableAll();

        m_connectionCallback(shared_from_this());
    }
    m_channel->remove();
}

void TcpConnection::handleRead(Timestamp receiveTime)
{
    m_loop->assertInLoopThread();
    int savedErrno = 0;
    int32_t n = m_inputBuffer.readFd(m_channel->fd(), &savedErrno);
    if (n > 0)
    {
        //messageCallback_指向CTcpSession::OnRead(const std::shared_ptr<TcpConnection>& conn, Buffer* pBuffer, Timestamp receiveTime)
        m_messageCallback(shared_from_this(), &m_inputBuffer, receiveTime);
    }
    else if (n == 0)
    {
        handleClose();
    }
    else
    {
        //非阻塞 socket 上「当前没有数据可读」返回的同样是 -1，这是正常情况而不是错误：
        //本轮什么都不做，等下一次可读事件即可。之所以要防这一手，是因为一旦 poller 把
        //事件位写错，本函数就会在根本没有数据的时候被叫起来，而原先的写法会顺着
        //handleError -> handleClose 把一条健康的连接判死（2026-08-16 修复的故障即如此）
        if (isWouldBlockError(savedErrno))
        {
            LOGD("TcpConnection::handleRead, fd = %d, no data available yet, wait for the next read event",
                m_channel->fd());
            return;
        }

        errno = savedErrno;
        LOGSYSE("TcpConnection::handleRead");
        handleError();
    }
}

void TcpConnection::handleWrite()
{
    m_loop->assertInLoopThread();
    if (m_channel->isWriting())
    {
        int32_t n = sockets::write(m_channel->fd(), m_outputBuffer.peek(),
                                   static_cast<int32_t>(m_outputBuffer.readableBytes()));
        if (n > 0)
        {
            m_outputBuffer.retrieve(n);
            if (m_outputBuffer.readableBytes() == 0)
            {
                m_channel->disableWriting();
                if (m_writeCompleteCallback)
                {
                    m_loop->queueInLoop(std::bind(m_writeCompleteCallback, shared_from_this()));
                }
                if (m_state == kDisconnecting)
                {
                    shutdownInLoop();
                }
            }
        }
        else
        {
            //取错误码必须紧挨着失败的那次调用，中间不能插入任何别的系统调用，否则会被覆盖
            int savedErrno = 0;
#ifdef WIN32
            savedErrno = (int)WSAGetLastError();
#else
            savedErrno = errno;
#endif

            //与 handleRead 同理：非阻塞 socket 上「发送缓冲暂时满了」返回的也是 -1，
            //本轮什么都不做，剩下的字节等下一次可写事件再写出去
            if (isWouldBlockError(savedErrno))
            {
                LOGD("TcpConnection::handleWrite, fd = %d, send buffer is full, wait for the next write event",
                    m_channel->fd());
                return;
            }

            errno = savedErrno;
            LOGSYSE("TcpConnection::handleWrite");
            // if (state_ == kDisconnecting)
            // {
            //   shutdownInLoop();
            // }
            //added by zhangyl 2019.05.06
            handleClose();
        }
    }
    else
    {
        LOGD("Connection fd = %d  is down, no more writing", m_channel->fd());
    }
}

void TcpConnection::handleClose()
{
    //在Linux上当一个链接出了问题，会同时触发handleError和handleClose
    //为了避免重复关闭链接，这里判断下当前状态
    //已经关闭了，直接返回
    if (m_state == kDisconnected)
        return;

    m_loop->assertInLoopThread();
    LOGD("fd = %d  state = %s", m_channel->fd(), stateToString());
    //assert(state_ == kConnected || state_ == kDisconnecting);
    // we don't close fd, leave it to dtor, so we can find leaks easily.
    setState(kDisconnected);
    m_channel->disableAll();

    TcpConnectionPtr guardThis(shared_from_this());
    m_connectionCallback(guardThis);
    // must be the last line
    m_closeCallback(guardThis);

    //只处理业务上的关闭，真正的socket fd在TcpConnection析构函数中关闭
    //if (socket_)
    //{
    //    sockets::close(socket_->fd());
    //}
}

void TcpConnection::handleError()
{
    int err = sockets::getSocketError(m_channel->fd());
    LOGE("TcpConnection::%s handleError [%d] - SO_ERROR = %s", m_name.c_str(), err, strerror(err));

    //调用handleClose()关闭连接，回收Channel和fd
    handleClose();
}
