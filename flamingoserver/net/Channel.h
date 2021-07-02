#pragma once

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

        Channel(EventLoop* loop, int fd);
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

        int fd() const { return m_fd; }
        int events() const { return m_events; }
        void set_revents(int revt) { m_revents = revt; }
        void add_revents(int revt) { m_revents |= revt; }
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
        const int                   m_fd;
        int                         m_events;
        int                         m_revents;
        int                         m_index;

        ReadEventCallback           m_readCallback;
        EventCallback               m_writeCallback;
        EventCallback               m_closeCallback;
        EventCallback               m_errorCallback;
    };
}
