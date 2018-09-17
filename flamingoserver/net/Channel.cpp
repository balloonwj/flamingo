#include "Channel.h"
#include <sstream>
#include <assert.h>
#include <poll.h>
#include "../base/Logging.h"
#include "EventLoop.h"

using namespace net;

const int Channel::kNoneEvent = 0;
const int Channel::kReadEvent = POLLIN | POLLPRI;
const int Channel::kWriteEvent = POLLOUT;

Channel::Channel(EventLoop* loop, int fd__)
: loop_(loop),
fd_(fd__),
events_(0),
revents_(0),
index_(-1),
logHup_(true),
tied_(false)/*,
eventHandling_(false),
addedToLoop_(false)
*/
{
}

Channel::~Channel()
{
	//assert(!eventHandling_);
	//assert(!addedToLoop_);
	if (loop_->isInLoopThread())
	{
		//assert(!loop_->hasChannel(this));
	}
}

void Channel::tie(const std::shared_ptr<void>& obj)
{
	tie_ = obj;
	tied_ = true;
}

bool Channel::enableReading() 
{ 
    events_ |= kReadEvent;
    return update();
}

bool Channel::disableReading()
{
    events_ &= ~kReadEvent; 
    
    return update();
}

bool Channel::enableWriting() 
{
    events_ |= kWriteEvent; 
    
    return update(); 
}

bool Channel::disableWriting()
{ 
    events_ &= ~kWriteEvent; 
    return update();
}

bool Channel::disableAll()
{ 
    events_ = kNoneEvent; 
    return update(); 
}

bool Channel::update()
{
	//addedToLoop_ = true;
	return loop_->updateChannel(this);
}

void Channel::remove()
{
	assert(isNoneEvent());
	//addedToLoop_ = false;
	loop_->removeChannel(this);
}

void Channel::handleEvent(Timestamp receiveTime)
{
	std::shared_ptr<void> guard;
	if (tied_)
	{
		guard = tie_.lock();
		if (guard)
		{
			handleEventWithGuard(receiveTime);
		}
	}
	else
	{
		handleEventWithGuard(receiveTime);
	}
}

void Channel::handleEventWithGuard(Timestamp receiveTime)
{
	//eventHandling_ = true;
    /*
    POLLIN ，读事件
    POLLPRI，读事件，但表示紧急数据，例如tcp socket的带外数据
    POLLRDNORM , 读事件，表示有普通数据可读　　　
    POLLRDBAND ,　读事件，表示有优先数据可读　　　　
    POLLOUT，写事件
    POLLWRNORM , 写事件，表示有普通数据可写
    POLLWRBAND ,　写事件，表示有优先数据可写　　　   　　　　
    POLLRDHUP (since Linux 2.6.17)，Stream socket的一端关闭了连接（注意是stream socket，我们知道还有raw socket,dgram socket），或者是写端关闭了连接，如果要使用这个事件，必须定义_GNU_SOURCE 宏。这个事件可以用来判断链路是否发生异常（当然更通用的方法是使用心跳机制）。要使用这个事件，得这样包含头文件：
    　　#define _GNU_SOURCE
      　　#include <poll.h>
    POLLERR，仅用于内核设置传出参数revents，表示设备发生错误
    POLLHUP，仅用于内核设置传出参数revents，表示设备被挂起，如果poll监听的fd是socket，表示这个socket并没有在网络上建立连接，比如说只调用了socket()函数，但是没有进行connect。
    POLLNVAL，仅用于内核设置传出参数revents，表示非法请求文件描述符fd没有打开
    */
	LOG_TRACE << reventsToString();
	if ((revents_ & POLLHUP) && !(revents_ & POLLIN))
	{
		if (logHup_)
		{
			LOG_WARN << "Channel::handle_event() POLLHUP";
		}
		if (closeCallback_) closeCallback_();
	}

	if (revents_ & POLLNVAL)
	{
		LOG_WARN << "Channel::handle_event() POLLNVAL";
	}

	if (revents_ & (POLLERR | POLLNVAL))
	{
		if (errorCallback_) errorCallback_();
	}
    
	if (revents_ & (POLLIN | POLLPRI | POLLRDHUP))
	{
		//当是侦听socket时，readCallback_指向Acceptor::handleRead
        //当是客户端socket时，调用TcpConnection::handleRead 
        if (readCallback_) readCallback_(receiveTime);
	}

	if (revents_ & POLLOUT)
	{
		//如果是连接状态服的socket，则writeCallback_指向Connector::handleWrite()
        if (writeCallback_) writeCallback_();
	}
	//eventHandling_ = false;
}

string Channel::reventsToString() const
{
	std::ostringstream oss;
	oss << fd_ << ": ";
	if (revents_ & POLLIN)
		oss << "IN ";
	if (revents_ & POLLPRI)
		oss << "PRI ";
	if (revents_ & POLLOUT)
		oss << "OUT ";
	if (revents_ & POLLHUP)
		oss << "HUP ";
	if (revents_ & POLLRDHUP)
		oss << "RDHUP ";
	if (revents_ & POLLERR)
		oss << "ERR ";
	if (revents_ & POLLNVAL)
		oss << "NVAL ";

	return oss.str().c_str();
}
