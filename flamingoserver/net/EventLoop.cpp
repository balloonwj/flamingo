#include "EventLoop.h"

#include <sstream>
#include <iostream>
#include <string.h>

#include "../base/AsyncLog.h"
#include "Channel.h"
#include "Sockets.h"
#include "InetAddress.h"

#ifdef WIN32
#include "SelectPoller.h"
#else
#include "EpollPoller.h"
#endif


using namespace net;

//内部侦听唤醒fd的侦听端口，因此外部可以再使用这个端口
//#define INNER_WAKEUP_LISTEN_PORT 10000

thread_local  EventLoop* t_loopInThisThread = 0;

const int kPollTimeMs = 1;

EventLoop* getEventLoopOfCurrentThread()
{
    return t_loopInThisThread;
}

// 在线程函数中创建eventloop
EventLoop::EventLoop()
    : looping_(false),
    quit_(false),
    eventHandling_(false),
    callingPendingFunctors_(false),
    threadId_(std::this_thread::get_id()),
    timerQueue_(new TimerQueue(this)),
    iteration_(0L),
    currentActiveChannel_(NULL)
{
    createWakeupfd();

#ifdef WIN32
    wakeupChannel_.reset(new Channel(this, wakeupFdSend_));
    poller_.reset(new SelectPoller(this));

#else
    wakeupChannel_.reset(new Channel(this, wakeupFd_));
    poller_.reset(new EPollPoller(this));
#endif

    if (t_loopInThisThread)
    {
        LOGF("Another EventLoop  exists in this thread ");
    }
    else
    {
        t_loopInThisThread = this;
    }
    wakeupChannel_->setReadCallback(std::bind(&EventLoop::handleRead, this));
    // we are always reading the wakeupfd
    wakeupChannel_->enableReading();

    //std::stringstream ss;	
    //ss << "eventloop create threadid = " << threadId_;
    //std::cout << ss.str() << std::endl;
}

EventLoop::~EventLoop()
{
    assertInLoopThread();
    LOGD("EventLoop 0x%x destructs.", this);

    //std::stringstream ss;
    //ss << "eventloop destructs threadid = " << threadId_;
    //std::cout << ss.str() << std::endl;

    wakeupChannel_->disableAll();
    wakeupChannel_->remove();

#ifdef WIN32
    sockets::close(wakeupFdSend_);
    sockets::close(wakeupFdRecv_);
    sockets::close(wakeupFdListen_);
#else
    sockets::close(wakeupFd_);
#endif

    //_close(fdpipe_[0]);
    //_close(fdpipe_[1]);

    t_loopInThisThread = NULL;
}

void EventLoop::loop()
{
    //assert(!looping_);
    assertInLoopThread();
    looping_ = true;
    quit_ = false;  // FIXME: what if someone calls quit() before loop() ?
    LOGD("EventLoop 0x%x  start looping", this);

    while (!quit_)
    {
        timerQueue_->doTimer();

        activeChannels_.clear();
        pollReturnTime_ = poller_->poll(kPollTimeMs, &activeChannels_);
        //if (Logger::logLevel() <= Logger::TRACE)
        //{
        printActiveChannels();
        //}
        ++iteration_;
        // TODO sort channel by priority
        eventHandling_ = true;
        for (const auto& it : activeChannels_)
        {
            currentActiveChannel_ = it;
            currentActiveChannel_->handleEvent(pollReturnTime_);
        }
        currentActiveChannel_ = nullptr;
        eventHandling_ = false;
        doPendingFunctors();

        if (frameFunctor_)
        {
            frameFunctor_();
        }
    }

    LOGD("EventLoop 0x%0x stop looping", this);
    looping_ = false;


    std::ostringstream oss;
    oss << std::this_thread::get_id();
    std::string stid = oss.str();
    LOGI("Exiting loop, EventLoop object: 0x%x , threadID: %s", this, stid.c_str());
}

void EventLoop::quit()
{
    quit_ = true;
    // There is a chance that loop() just executes while(!quit_) and exists,
    // then EventLoop destructs, then we are accessing an invalid object.
    // Can be fixed using mutex_ in both places.
    if (!isInLoopThread())
    {
        wakeup();
    }
}

void EventLoop::runInLoop(const Functor& cb)
{
    if (isInLoopThread())
    {
        cb();
    }
    else
    {
        queueInLoop(cb);
    }
}

void EventLoop::queueInLoop(const Functor& cb)
{
    {
        std::unique_lock<std::mutex> lock(mutex_);
        pendingFunctors_.push_back(cb);
    }

    if (!isInLoopThread() || callingPendingFunctors_)
    {
        wakeup();
    }
}

void EventLoop::setFrameFunctor(const Functor& cb)
{
    frameFunctor_ = cb;
}

TimerId EventLoop::runAt(const Timestamp& time, const TimerCallback& cb)
{
    //只执行一次
    return timerQueue_->addTimer(cb, time, 0, 1);
}

TimerId EventLoop::runAfter(int64_t delay, const TimerCallback& cb)
{
    Timestamp time(addTime(Timestamp::now(), delay));
    return runAt(time, cb);
}

TimerId EventLoop::runEvery(int64_t interval, const TimerCallback& cb)
{
    Timestamp time(addTime(Timestamp::now(), interval));
    //-1表示一直重复下去
    return timerQueue_->addTimer(cb, time, interval, -1);
}

TimerId EventLoop::runAt(const Timestamp& time, TimerCallback&& cb)
{
    return timerQueue_->addTimer(std::move(cb), time, 0, 1);
}

TimerId EventLoop::runAfter(int64_t delay, TimerCallback&& cb)
{
    Timestamp time(addTime(Timestamp::now(), delay));
    return runAt(time, std::move(cb));
}

TimerId EventLoop::runEvery(int64_t interval, TimerCallback&& cb)
{
    Timestamp time(addTime(Timestamp::now(), interval));
    return timerQueue_->addTimer(std::move(cb), time, interval, -1);
}

void EventLoop::cancel(TimerId timerId, bool off)
{
    return timerQueue_->cancel(timerId, off);
}

void EventLoop::remove(TimerId timerId)
{
    return timerQueue_->removeTimer(timerId);
}

bool EventLoop::updateChannel(Channel* channel)
{
    //assert(channel->ownerLoop() == this);
    if (channel->ownerLoop() != this)
        return false;

    assertInLoopThread();

    return poller_->updateChannel(channel);
}

void EventLoop::removeChannel(Channel* channel)
{
    //assert(channel->ownerLoop() == this);
    if (channel->ownerLoop() != this)
        return;

    assertInLoopThread();
    if (eventHandling_)
    {
        //assert(currentActiveChannel_ == channel || std::find(activeChannels_.begin(), activeChannels_.end(), channel) == activeChannels_.end());
    }

    LOGD("Remove channel, channel = 0x%x, fd = %d", channel, channel->fd());
    poller_->removeChannel(channel);
}

bool EventLoop::hasChannel(Channel* channel)
{
    //assert(channel->ownerLoop() == this);
    assertInLoopThread();
    return poller_->hasChannel(channel);
}

bool EventLoop::createWakeupfd()
{
#ifdef WIN32
    //if (_pipe(fdpipe_, 256, O_BINARY) == -1)
    //{
    //    //让程序挂掉
    //    LOGF("Unable to create pipe, EventLoop: 0x%x", this);
    //    return false;
    //}

    wakeupFdListen_ = sockets::createOrDie();
    wakeupFdSend_ = sockets::createOrDie();

    //Windows上需要创建一对socket  
    struct sockaddr_in bindaddr;
    bindaddr.sin_family = AF_INET;
    bindaddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    //将port设为0，然后进行bind，再接着通过getsockname来获取port，这可以满足获取随机端口的情况。
    bindaddr.sin_port = 0;
    sockets::setReuseAddr(wakeupFdListen_, true);
    sockets::bindOrDie(wakeupFdListen_, bindaddr);
    sockets::listenOrDie(wakeupFdListen_);

    struct sockaddr_in serveraddr;
    int serveraddrlen = sizeof(serveraddr);
    if (getsockname(wakeupFdListen_, (sockaddr*)& serveraddr, &serveraddrlen) < 0)
    {
        //让程序挂掉
        LOGF("Unable to bind address info, EventLoop: 0x%x", this);
        return false;
    }

    int useport = ntohs(serveraddr.sin_port);
    LOGD("wakeup fd use port: %d", useport);

    //serveraddr.sin_family = AF_INET;
    //serveraddr.sin_addr.s_addr = inet_addr("127.0.0.1");
    //serveraddr.sin_port = htons(INNER_WAKEUP_LISTEN_PORT);   
    if (::connect(wakeupFdSend_, (struct sockaddr*) & serveraddr, sizeof(serveraddr)) < 0)
    {
        //让程序挂掉
        LOGF("Unable to connect to wakeup peer, EventLoop: 0x%x", this);
        return false;
    }

    struct sockaddr_in clientaddr;
    socklen_t clientaddrlen = sizeof(clientaddr);
    wakeupFdRecv_ = ::accept(wakeupFdListen_, (struct sockaddr*) & clientaddr, &clientaddrlen);
    if (wakeupFdRecv_ < 0)
    {
        //让程序挂掉
        LOGF("Unable to accept wakeup peer, EventLoop: 0x%x", this);
        return false;
    }

    sockets::setNonBlockAndCloseOnExec(wakeupFdSend_);
    sockets::setNonBlockAndCloseOnExec(wakeupFdRecv_);

#else
    //Linux上一个eventfd就够了，可以实现读写
    wakeupFd_ = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (wakeupFd_ < 0)
    {
        //让程序挂掉
        LOGF("Unable to create wakeup eventfd, EventLoop: 0x%x", this);
        return false;
    }

#endif

    return true;
}

void EventLoop::abortNotInLoopThread()
{
    std::stringstream ss;
    ss << "threadid_ = " << threadId_ << " this_thread::get_id() = " << std::this_thread::get_id();
    LOGF("EventLoop::abortNotInLoopThread - EventLoop %s", ss.str().c_str());
}

bool EventLoop::wakeup()
{
    uint64_t one = 1;
#ifdef WIN32
    int32_t n = sockets::write(wakeupFdSend_, &one, sizeof(one));
#else
    int32_t n = sockets::write(wakeupFd_, &one, sizeof(one));
#endif


    if (n != sizeof one)
    {
#ifdef WIN32
        DWORD error = WSAGetLastError();
        LOGSYSE("EventLoop::wakeup() writes %d  bytes instead of 8, fd: %d, error: %d", n, wakeupFdSend_, (int32_t)error);
#else
        int error = errno;
        LOGSYSE("EventLoop::wakeup() writes %d  bytes instead of 8, fd: %d, error: %d, errorinfo: %s", n, wakeupFd_, error, strerror(error));
#endif


        return false;
    }

    return true;
}

bool EventLoop::handleRead()
{
    uint64_t one = 1;
#ifdef WIN32
    int32_t n = sockets::read(wakeupFdRecv_, &one, sizeof(one));
#else
    int32_t n = sockets::read(wakeupFd_, &one, sizeof(one));
#endif

    if (n != sizeof one)
    {
#ifdef WIN32
        DWORD error = WSAGetLastError();
        LOGSYSE("EventLoop::wakeup() read %d  bytes instead of 8, fd: %d, error: %d", n, wakeupFdRecv_, (int32_t)error);
#else
        int error = errno;
        LOGSYSE("EventLoop::wakeup() read %d  bytes instead of 8, fd: %d, error: %d, errorinfo: %s", n, wakeupFd_, error, strerror(error));
#endif
        return false;
    }

    return true;
}

void EventLoop::doPendingFunctors()
{
    std::vector<Functor> functors;
    callingPendingFunctors_ = true;

    {
        std::unique_lock<std::mutex> lock(mutex_);
        functors.swap(pendingFunctors_);
    }

    for (size_t i = 0; i < functors.size(); ++i)
    {
        functors[i]();
    }
    callingPendingFunctors_ = false;
}

void EventLoop::printActiveChannels() const
{
    //TODO: 改成for-each 语法
    for (ChannelList::const_iterator it = activeChannels_.begin(); it != activeChannels_.end(); ++it)
    {
        const Channel* ch = *it;
        LOGD("{%s}", ch->reventsToString().c_str());
    }
}