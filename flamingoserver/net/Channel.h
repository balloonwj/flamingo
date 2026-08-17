#pragma once

//SOCKET 类型来自这里：Windows 下是 winsock 的 UINT_PTR，POSIX 下是 int
#include "../base/Platform.h"
#include <memory>
#include <functional>

#include "../base/Timestamp.h"

namespace net
{
    class EventLoop;

    class Channel
    {
    public:
        typedef std::function<void()> EventCallback;
        typedef std::function<void(Timestamp)> ReadEventCallback;

        Channel(EventLoop* loop, SOCKET fd);
        ~Channel();

        void handleEvent(Timestamp receiveTime);
        void setReadCallback(const ReadEventCallback& cb)
        {
            m_readCallback = cb;
        }
        void setWriteCallback(const EventCallback& cb)
        {
            m_writeCallback = cb;
        }
        void setCloseCallback(const EventCallback& cb)
        {
            m_closeCallback = cb;
        }
        void setErrorCallback(const EventCallback& cb)
        {
            m_errorCallback = cb;
        }

        SOCKET fd() const { return m_fd; }
        int events() const { return m_events; }
        //由 poller 在每一轮 poll 返回之后调用，写入<u>本轮</u>真正发生的事件位。
        //约定是「整体覆盖写」而非累加：m_revents 表示本轮发生了什么，不是历史事件的累积量，
        //本类也不会在派发完之后把它清零。三个 poller（EpollPoller / PollPoller / SelectPoller）
        //都必须按这个口径写 —— 一旦有人改成按位或累加，上一轮的可读位就会残留到下一轮的
        //可写事件里，把一条健康的连接判死。2026-08-16 之前 SelectPoller 用的正是一个名为
        //add_revents 的累加写法，Windows 上因此出现过「客户端读得慢时服务端拆连接」的故障，
        //该写法已随修复一并删除。守卫用例见 tests/test_pollerrevents.cpp（Suite PRV）
        void set_revents(int revt) { m_revents = revt; }
        // int revents() const { return revents_; }
        bool isNoneEvent() const { return m_events == kNoneEvent; }

        bool enableReading();
        bool disableReading();
        bool enableWriting();
        bool disableWriting();
        bool disableAll();

        bool isWriting() const { return m_events & kWriteEvent; }

        int index() { return m_index; }
        void set_index(int idx) { m_index = idx; }

        string reventsToString() const;

        EventLoop* ownerLoop() { return m_loop; }
        void remove();

    private:
        bool update();

        static const int            kNoneEvent;
        static const int            kReadEvent;
        static const int            kWriteEvent;

        EventLoop* m_loop;
        const SOCKET                m_fd;
        int                         m_events;
        int                         m_revents;
        int                         m_index;

        ReadEventCallback           m_readCallback;
        EventCallback               m_writeCallback;
        EventCallback               m_closeCallback;
        EventCallback               m_errorCallback;
    };
}
