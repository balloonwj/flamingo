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
EventLoop::EventLoop() :
    m_looping(false),
    m_quit(false),
    m_eventHandling(false),
    m_doingOtherTasks(false),
    m_threadId(std::this_thread::get_id()),
    m_timerQueue(new TimerQueue(this)),
    m_iteration(0L),
    currentActiveChannel_(NULL)
{
    createWakeupfd();

#ifdef WIN32
    m_wakeupChannel.reset(new Channel(this, m_wakeupFdRecv));
    m_poller.reset(new SelectPoller(this));

#else
    m_wakeupChannel.reset(new Channel(this, m_wakeupFd));
    m_poller.reset(new EPollPoller(this));
#endif

    if (t_loopInThisThread)
    {
        LOGF("Another EventLoop  exists in this thread ");
    }
    else
    {
        t_loopInThisThread = this;
    }
    m_wakeupChannel->setReadCallback(std::bind(&EventLoop::handleRead, this));
    // we are always reading the wakeupfd
    m_wakeupChannel->enableReading();
}

EventLoop::~EventLoop()
{
    assertInLoopThread();
    LOGD("EventLoop 0x%x destructs.", this);

    //std::stringstream ss;
    //ss << "eventloop destructs threadid = " << threadId_;
    //std::cout << ss.str() << std::endl;

    m_wakeupChannel->disableAll();
    m_wakeupChannel->remove();

#ifdef WIN32
    sockets::close(m_wakeupFdSend);
    sockets::close(m_wakeupFdRecv);
    sockets::close(m_wakeupFdListen);
#else
    sockets::close(m_wakeupFd);
#endif

    //_close(fdpipe_[0]);
    //_close(fdpipe_[1]);

    t_loopInThisThread = NULL;
}

void EventLoop::loop()
{
    //assert(!looping_);
    assertInLoopThread();
    m_looping = true;
    m_quit = false;  // FIXME: what if someone calls quit() before loop() ?
    LOGD("EventLoop 0x%x  start looping", this);

    while (!m_quit)
    {
        m_timerQueue->doTimer();

        m_activeChannels.clear();
        m_pollReturnTime = m_poller->poll(kPollTimeMs, &m_activeChannels);
        //if (Logger::logLevel() <= Logger::TRACE)
        //{
        printActiveChannels();
        //}
        ++m_iteration;
        // TODO sort channel by priority
        m_eventHandling = true;
        for (const auto& it : m_activeChannels)
        {
            currentActiveChannel_ = it;
            currentActiveChannel_->handleEvent(m_pollReturnTime);
        }
        currentActiveChannel_ = nullptr;
        m_eventHandling = false;
        doOtherTasks();

        if (m_frameFunctor)
        {
            m_frameFunctor();
        }
    }

    LOGD("EventLoop 0x%0x stop looping", this);
    m_looping = false;


    std::ostringstream oss;
    oss << std::this_thread::get_id();
    std::string stid = oss.str();
    LOGI("Exiting loop, EventLoop object: 0x%x , threadID: %s", this, stid.c_str());
}

void EventLoop::quit()
{
    m_quit = true;
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
        std::unique_lock<std::mutex> lock(m_mutex);
        m_pendingFunctors.push_back(cb);
    }

    if (!isInLoopThread() || m_doingOtherTasks)
    {
        wakeup();
    }
}

void EventLoop::setFrameFunctor(const Functor& cb)
{
    m_frameFunctor = cb;
}

TimerId EventLoop::runAt(const Timestamp& time, const TimerCallback& cb)
{
    //只执行一次
    return m_timerQueue->addTimer(cb, time, 0, 1);
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
    return m_timerQueue->addTimer(cb, time, interval, -1);
}

TimerId EventLoop::runAt(const Timestamp& time, TimerCallback&& cb)
{
    return m_timerQueue->addTimer(std::move(cb), time, 0, 1);
}

TimerId EventLoop::runAfter(int64_t delay, TimerCallback&& cb)
{
    Timestamp time(addTime(Timestamp::now(), delay));
    return runAt(time, std::move(cb));
}

TimerId EventLoop::runEvery(int64_t interval, TimerCallback&& cb)
{
    Timestamp time(addTime(Timestamp::now(), interval));
    return m_timerQueue->addTimer(std::move(cb), time, interval, -1);
}

void EventLoop::cancel(TimerId timerId, bool off)
{
    return m_timerQueue->cancel(timerId, off);
}

void EventLoop::remove(TimerId timerId)
{
    return m_timerQueue->removeTimer(timerId);
}

bool EventLoop::updateChannel(Channel* channel)
{
    //assert(channel->ownerLoop() == this);
    if (channel->ownerLoop() != this)
        return false;

    assertInLoopThread();

    return m_poller->updateChannel(channel);
}

void EventLoop::removeChannel(Channel* channel)
{
    //assert(channel->ownerLoop() == this);
    if (channel->ownerLoop() != this)
        return;

    assertInLoopThread();
    if (m_eventHandling)
    {
        //assert(currentActiveChannel_ == channel || std::find(activeChannels_.begin(), activeChannels_.end(), channel) == activeChannels_.end());
    }

    LOGD("Remove channel, channel = 0x%x, fd = %d", channel, channel->fd());
    m_poller->removeChannel(channel);
}

bool EventLoop::hasChannel(Channel* channel)
{
    //assert(channel->ownerLoop() == this);
    assertInLoopThread();
    return m_poller->hasChannel(channel);
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

    m_wakeupFdListen = sockets::createOrDie();
    m_wakeupFdSend = sockets::createOrDie();

    //Windows上需要创建一对socket  
    struct sockaddr_in bindaddr;
    bindaddr.sin_family = AF_INET;
    bindaddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    //将port设为0，然后进行bind，再接着通过getsockname来获取port，这可以满足获取随机端口的情况。
    bindaddr.sin_port = 0;
    sockets::setReuseAddr(m_wakeupFdListen, true);
    sockets::bindOrDie(m_wakeupFdListen, bindaddr);
    sockets::listenOrDie(m_wakeupFdListen);

    struct sockaddr_in serveraddr;
    int serveraddrlen = sizeof(serveraddr);
    if (getsockname(m_wakeupFdListen, (sockaddr*)&serveraddr, &serveraddrlen) < 0)
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
    if (::connect(m_wakeupFdSend, (struct sockaddr*)&serveraddr, sizeof(serveraddr)) < 0)
    {
        //让程序挂掉
        LOGF("Unable to connect to wakeup peer, EventLoop: 0x%x", this);
        return false;
    }

    struct sockaddr_in clientaddr;
    socklen_t clientaddrlen = sizeof(clientaddr);
    m_wakeupFdRecv = ::accept(m_wakeupFdListen, (struct sockaddr*)&clientaddr, &clientaddrlen);
    if (m_wakeupFdRecv < 0)
    {
        //让程序挂掉
        LOGF("Unable to accept wakeup peer, EventLoop: 0x%x", this);
        return false;
    }

    sockets::setNonBlockAndCloseOnExec(m_wakeupFdSend);
    sockets::setNonBlockAndCloseOnExec(m_wakeupFdRecv);

#else
    //Linux上一个eventfd就够了，可以实现读写
    m_wakeupFd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (m_wakeupFd < 0)
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
    ss << "threadid_ = " << m_threadId << " this_thread::get_id() = " << std::this_thread::get_id();
    LOGF("EventLoop::abortNotInLoopThread - EventLoop %s", ss.str().c_str());
}

bool EventLoop::wakeup()
{
    uint64_t one = 1;
#ifdef WIN32
    int32_t n = sockets::write(m_wakeupFdSend, &one, sizeof(one));
#else
    int32_t n = sockets::write(m_wakeupFd, &one, sizeof(one));
#endif


    if (n != sizeof one)
    {
#ifdef WIN32
        DWORD error = WSAGetLastError();
        LOGSYSE("EventLoop::wakeup() writes %d  bytes instead of 8, fd: %d, error: %d", n, m_wakeupFdSend, (int32_t)error);
#else
        int error = errno;
        LOGSYSE("EventLoop::wakeup() writes %d  bytes instead of 8, fd: %d, error: %d, errorinfo: %s", n, m_wakeupFd, error, strerror(error));
#endif


        return false;
    }

    return true;
}

bool EventLoop::handleRead()
{
    uint64_t one = 1;
#ifdef WIN32
    int32_t n = sockets::read(m_wakeupFdRecv, &one, sizeof(one));
#else
    int32_t n = sockets::read(m_wakeupFd, &one, sizeof(one));
#endif

    if (n != sizeof one)
    {
#ifdef WIN32
        DWORD error = WSAGetLastError();
        LOGSYSE("EventLoop::wakeup() read %d  bytes instead of 8, fd: %d, error: %d", n, m_wakeupFdRecv, (int32_t)error);
#else
        int error = errno;
        LOGSYSE("EventLoop::wakeup() read %d  bytes instead of 8, fd: %d, error: %d, errorinfo: %s", n, m_wakeupFd, error, strerror(error));
#endif
        return false;
    }

    return true;
}

void EventLoop::doOtherTasks()
{
    std::vector<Functor> functors;
    m_doingOtherTasks = true;

    {
        std::unique_lock<std::mutex> lock(m_mutex);
        functors.swap(m_pendingFunctors);
    }

    for (size_t i = 0; i < functors.size(); ++i)
    {
        functors[i]();
    }

    m_doingOtherTasks = false;
}

void EventLoop::printActiveChannels() const
{
    //TODO: 改成for-each 语法
    //std::vector<Channel*>
    for (const auto& iter : m_activeChannels)
    {
        LOGD("{%s}", iter->reventsToString().c_str());
    }
}
