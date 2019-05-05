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
    : loop_(loop),
    serverAddr_(serverAddr),
    connect_(false),
    state_(kDisconnected),
    retryDelayMs_(kInitRetryDelayMs)
{
}

Connector::~Connector()
{
}

void Connector::start()
{
    connect_ = true;
    loop_->runInLoop(std::bind(&Connector::startInLoop, this)); // FIXME: unsafe
}

void Connector::startInLoop()
{
    loop_->assertInLoopThread();
    //assert(state_ == kDisconnected);
    if (state_ != kDisconnected)
        return;

    if (connect_)
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
    connect_ = false;
    loop_->queueInLoop(std::bind(&Connector::stopInLoop, shared_from_this())); // FIXME: unsafe
    // FIXME: cancel timer
}

void Connector::stopInLoop()
{
    //std::stringstream ss;	
    //ss << "stopInLoop eventloop threadid = " << std::this_thread::get_id();
    //std::cout << ss.str() << std::endl;

    loop_->assertInLoopThread();
    if (state_ == kConnecting)
    {
        setState(kDisconnected);
        int sockfd = removeAndResetChannel();
        retry(sockfd);
    }
}

void Connector::connect()
{
    int sockfd = sockets::createNonblockingOrDie();
    int ret = sockets::connect(sockfd, serverAddr_.getSockAddrInet());
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
    loop_->assertInLoopThread();
    setState(kDisconnected);
    retryDelayMs_ = kInitRetryDelayMs;
    connect_ = true;
    startInLoop();
}

void Connector::connecting(int sockfd)
{
    setState(kConnecting);
    //assert(!channel_);
    channel_.reset(new Channel(loop_, sockfd));
    channel_->setWriteCallback(std::bind(&Connector::handleWrite, this)); // FIXME: unsafe
    channel_->setErrorCallback(std::bind(&Connector::handleError, this)); // FIXME: unsafe

    // channel_->tie(shared_from_this()); is not working,
    // as channel_ is not managed by shared_ptr
    channel_->enableWriting();
}

int Connector::removeAndResetChannel()
{
    channel_->disableAll();
    channel_->remove();
    int sockfd = channel_->fd();
    // Can't reset channel_ here, because we are inside Channel::handleEvent
    loop_->queueInLoop(std::bind(&Connector::resetChannel, shared_from_this())); // FIXME: unsafe
    return sockfd;
}

void Connector::resetChannel()
{
    channel_.reset();
}

void Connector::handleWrite()
{
    LOGD("Connector::handleWrite %d", state_);

    if (state_ == kConnecting)
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
            if (connect_)
            {
                //newConnectionCallback_指向TcpClient::newConnection(int sockfd)
                newConnectionCallback_(sockfd);
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
        if (state_ != kDisconnected)
            LOGSYSE("state_ != kDisconnected");
    }
}

void Connector::handleError()
{
    LOGE("Connector::handleError state=%d", state_);
    if (state_ == kConnecting)
    {
        int sockfd = removeAndResetChannel();
        int err = sockets::getSocketError(sockfd);
        LOGD("SO_ERROR = %d %s", err, strerror(err));
        LOGE("Connector::handleError state=%d", state_);
        retry(sockfd);
    }
}

void Connector::retry(int sockfd)
{
    sockets::close(sockfd);
    setState(kDisconnected);
    if (connect_)
    {
        LOGI("Connector::retry - Retry connecting to %s in %d  milliseconds.", serverAddr_.toIpPort().c_str(), retryDelayMs_);
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