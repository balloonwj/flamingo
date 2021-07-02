#include "Channel.h"
#include <sstream>

#include "../base/Platform.h"
#include "../base/AsyncLog.h"
#include "Poller.h"
#include "EventLoop.h"

using namespace net;

const int Channel::kNoneEvent = 0;
const int Channel::kReadEvent = XPOLLIN | XPOLLPRI;
const int Channel::kWriteEvent = XPOLLOUT;

Channel::Channel(EventLoop* loop, int fd__) : m_loop(loop),
m_fd(fd__),
m_events(0),
m_revents(0),
m_index(-1)
{
}

Channel::~Channel()
{
}

bool Channel::enableReading()
{
    m_events |= kReadEvent;
    return update();
}

bool Channel::disableReading()
{
    m_events &= ~kReadEvent;

    return update();
}

bool Channel::enableWriting()
{
    m_events |= kWriteEvent;

    return update();
}

bool Channel::disableWriting()
{
    m_events &= ~kWriteEvent;
    return update();
}

bool Channel::disableAll()
{
    m_events = kNoneEvent;
    return update();
}

bool Channel::update()
{
    //addedToLoop_ = true;
    return m_loop->updateChannel(this);
}

void Channel::remove()
{
    if (!isNoneEvent())
        return;

    m_loop->removeChannel(this);
}

void Channel::handleEvent(Timestamp receiveTime)
{
    /*
    XPOLLIN ，读事件
    XPOLLPRI，读事件，但表示紧急数据，例如tcp socket的带外数据
    POLLRDNORM , 读事件，表示有普通数据可读　　　
    POLLRDBAND ,　读事件，表示有优先数据可读　　　　
    XPOLLOUT，写事件
    POLLWRNORM , 写事件，表示有普通数据可写
    POLLWRBAND ,　写事件，表示有优先数据可写　　　   　　　　
    XPOLLRDHUP (since Linux 2.6.17)，Stream socket的一端关闭了连接（注意是stream socket，我们知道还有raw socket,dgram socket），或者是写端关闭了连接，如果要使用这个事件，必须定义_GNU_SOURCE 宏。这个事件可以用来判断链路是否发生异常（当然更通用的方法是使用心跳机制）。要使用这个事件，得这样包含头文件：
    　　#define _GNU_SOURCE
      　　#include <poll.h>
    XPOLLERR，仅用于内核设置传出参数revents，表示设备发生错误
    XPOLLHUP，仅用于内核设置传出参数revents，表示设备被挂起，如果poll监听的fd是socket，表示这个socket并没有在网络上建立连接，比如说只调用了socket()函数，但是没有进行connect。
    XPOLLNVAL，仅用于内核设置传出参数revents，表示非法请求文件描述符fd没有打开
    */
    LOGD(reventsToString().c_str());
    if ((m_revents & XPOLLHUP) && !(m_revents & XPOLLIN))
    {
        if (m_closeCallback) m_closeCallback();
    }

    if (m_revents & XPOLLNVAL)
    {
        LOGW("Channel::handle_event() XPOLLNVAL");
    }

    if (m_revents & (XPOLLERR | XPOLLNVAL))
    {
        if (m_errorCallback)
            m_errorCallback();
    }

    if (m_revents & (XPOLLIN | XPOLLPRI | XPOLLRDHUP))
    {
        //当是侦听socket时，readCallback_指向Acceptor::handleRead
        //当是客户端socket时，调用TcpConnection::handleRead 
        if (m_readCallback)
            m_readCallback(receiveTime);
    }

    if (m_revents & XPOLLOUT)
    {
        //如果是连接状态服的socket，则writeCallback_指向Connector::handleWrite()
        if (m_writeCallback)
            m_writeCallback();
    }
    //eventHandling_ = false;
}

string Channel::reventsToString() const
{
    std::ostringstream oss;
    oss << m_fd << ": ";
    if (m_revents & XPOLLIN)
        oss << "IN ";
    if (m_revents & XPOLLPRI)
        oss << "PRI ";
    if (m_revents & XPOLLOUT)
        oss << "OUT ";
    if (m_revents & XPOLLHUP)
        oss << "HUP ";
    if (m_revents & XPOLLRDHUP)
        oss << "RDHUP ";
    if (m_revents & XPOLLERR)
        oss << "ERR ";
    if (m_revents & XPOLLNVAL)
        oss << "NVAL ";

    return oss.str().c_str();
}
