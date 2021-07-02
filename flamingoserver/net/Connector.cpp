#include "Connector.h"
#include <functional>
#include <errno.h>
#include <sstream>
#include <iostream>
#include <string.h> //for strerror
#include "../base/AsyncLog.h"
#include "../base/Platform.h"
#include "Channel.h"
#include "EventLoop.h"
#include "Sockets.h"

using namespace net;

const int Connector::kMaxRetryDelayMs;

Connector::Connector(EventLoop* loop, const InetAddress& serverAddr)
    : m_loop(loop),
    m_serverAddr(serverAddr),
    m_connect(false),
    m_state(kDisconnected),
    m_retryDelayMs(kInitRetryDelayMs)
{
}

Connector::~Connector()
{
}

void Connector::start()
{
    m_connect = true;
    m_loop->runInLoop(std::bind(&Connector::startInLoop, this)); // FIXME: unsafe
}

void Connector::startInLoop()
{
    m_loop->assertInLoopThread();
    //assert(state_ == kDisconnected);
    if (m_state != kDisconnected)
        return;

    if (m_connect)
    {
        connect();
    }
    else
    {
        LOGD("do not connect");
    }
}

void Connector::stop()
{
    m_connect = false;
    m_loop->queueInLoop(std::bind(&Connector::stopInLoop, shared_from_this())); // FIXME: unsafe
    // FIXME: cancel timer
}

void Connector::stopInLoop()
{
    //std::stringstream ss; 
    //ss << "stopInLoop eventloop threadid = " << std::this_thread::get_id();
    //std::cout << ss.str() << std::endl;

    m_loop->assertInLoopThread();
    if (m_state == kConnecting)
    {
        setState(kDisconnected);
        int sockfd = removeAndResetChannel();
        retry(sockfd);
    }
}

void Connector::connect()
{
    int sockfd = sockets::createNonblockingOrDie();
    int ret = sockets::connect(sockfd, m_serverAddr.getSockAddrInet());
#ifdef WIN32
    int savedErrno = ::WSAGetLastError();
    if (savedErrno == EWOULDBLOCK)
        connecting(sockfd);
    else
    {
        //TODO: 确定下是否要关闭
        retry(sockfd);
        sockets::close(sockfd);
    }

#else
    int savedErrno = (ret == 0) ? 0 : errno;
    switch (savedErrno)
    {
    case 0:
    case EINPROGRESS:
    case EINTR:
    case EISCONN:
        connecting(sockfd);
        break;

    case EAGAIN:
    case EADDRINUSE:
    case EADDRNOTAVAIL:
    case ECONNREFUSED:
    case ENETUNREACH:
        retry(sockfd);
        break;

    case EACCES:
    case EPERM:
    case EAFNOSUPPORT:
    case EALREADY:
    case EBADF:
    case EFAULT:
    case ENOTSOCK:
        LOGSYSE("connect error in Connector::startInLoop, %d ", savedErrno);
        sockets::close(sockfd);
        break;

    default:
        LOGSYSE("Unexpected error in Connector::startInLoop, %d ", savedErrno);
        sockets::close(sockfd);
        // connectErrorCallback_();
        break;
    }

#endif
}

void Connector::restart()
{
    m_loop->assertInLoopThread();
    setState(kDisconnected);
    m_retryDelayMs = kInitRetryDelayMs;
    m_connect = true;
    startInLoop();
}

void Connector::connecting(int sockfd)
{
    setState(kConnecting);
    //assert(!channel_);
    m_channel.reset(new Channel(m_loop, sockfd));
    m_channel->setWriteCallback(std::bind(&Connector::handleWrite, this)); // FIXME: unsafe
    m_channel->setErrorCallback(std::bind(&Connector::handleError, this)); // FIXME: unsafe

    // channel_->tie(shared_from_this()); is not working,
    // as channel_ is not managed by shared_ptr
    m_channel->enableWriting();
}

int Connector::removeAndResetChannel()
{
    m_channel->disableAll();
    m_channel->remove();
    int sockfd = m_channel->fd();
    // Can't reset channel_ here, because we are inside Channel::handleEvent
    m_loop->queueInLoop(std::bind(&Connector::resetChannel, shared_from_this())); // FIXME: unsafe
    return sockfd;
}

void Connector::resetChannel()
{
    m_channel.reset();
}

void Connector::handleWrite()
{
    LOGD("Connector::handleWrite %d", m_state);

    if (m_state == kConnecting)
    {
        int sockfd = removeAndResetChannel();
        int err = sockets::getSocketError(sockfd);
        if (err)
        {
            LOGW("Connector::handleWrite - SO_ERROR = %d %s", err, strerror(err));
            retry(sockfd);
        }
        else if (sockets::isSelfConnect(sockfd))
        {
            LOGW("Connector::handleWrite - Self connect");
            retry(sockfd);
        }
        else
        {
            setState(kConnected);
            if (m_connect)
            {
                //newConnectionCallback_指向TcpClient::newConnection(int sockfd)
                m_newConnectionCallback(sockfd);
            }
            else
            {
                sockets::close(sockfd);
            }
        }
    }
    else
    {
        // what happened?
        //assert(state_ == kDisconnected);
        if (m_state != kDisconnected)
            LOGSYSE("state_ != kDisconnected");
    }
}

void Connector::handleError()
{
    LOGE("Connector::handleError state=%d", m_state);
    if (m_state == kConnecting)
    {
        int sockfd = removeAndResetChannel();
        int err = sockets::getSocketError(sockfd);
        LOGD("SO_ERROR = %d %s", err, strerror(err));
        LOGE("Connector::handleError state=%d", m_state);
        retry(sockfd);
    }
}

void Connector::retry(int sockfd)
{
    sockets::close(sockfd);
    setState(kDisconnected);
    if (m_connect)
    {
        LOGI("Connector::retry - Retry connecting to %s in %d  milliseconds.", m_serverAddr.toIpPort().c_str(), m_retryDelayMs);
        //loop_->runAfter(retryDelayMs_/1000.0,
        //                std::bind(&Connector::startInLoop, shared_from_this()));
        //retryDelayMs_ = std::min(retryDelayMs_ * 2, kMaxRetryDelayMs);
        //定时器重试， todo
    }
    else
    {
        LOGD("do not connect");
    }
}
