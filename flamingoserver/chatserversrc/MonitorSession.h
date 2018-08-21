/**
 * 监控会话类, MonitorSession.h
 * zhangyl 2017.03.09
 */
#ifndef __MONITOR_SESSION_H__
#define __MONITOR_SESSION_H__

#include "../net/Buffer.h"
#include "../base/Timestamp.h"
#include "../net/TcpConnection.h"
#include <memory>

using namespace net;

class MonitorSession
{
public:
    MonitorSession(std::shared_ptr<TcpConnection>& conn);
    ~MonitorSession() = default;
    MonitorSession(const MonitorSession& rhs) = delete;
    MonitorSession& operator =(const MonitorSession& rhs) = delete;

public:
    //有数据可读, 会被多个工作loop调用
    void OnRead(const std::shared_ptr<TcpConnection>& conn, Buffer* pBuffer, Timestamp receivTime);

    std::shared_ptr<TcpConnection> GetConnectionPtr()
    {
        if (m_tmpConn.expired())
            return NULL;

        return m_tmpConn.lock();
    }

    void ShowHelp();
    void Send(const char* data, size_t length);

private:
    bool Process(const std::shared_ptr<TcpConnection>& conn, const std::string& inbuf);
    bool ShowOnlineUserList(const std::string& token = "");
    bool ShowSpecifiedUserInfoByID(int32_t userid);

private:
    std::weak_ptr<TcpConnection>       m_tmpConn;
};


#endif //!__MONITOR_SESSION_H__