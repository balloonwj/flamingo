/**
 * Http会话类, HttpSession.h
 * zhangyl 2018.05.16
 */
#ifndef __MONITOR_SESSION_H__
#define __MONITOR_SESSION_H__

#include "../net/Buffer.h"
#include "../base/Timestamp.h"
#include "../net/TcpConnection.h"
#include "TcpSession.h"
#include <memory>

using namespace net;

class HttpSession
{
public:
    HttpSession(std::shared_ptr<TcpConnection>& conn);
    ~HttpSession() = default;
    HttpSession(const HttpSession& rhs) = delete;
    HttpSession& operator =(const HttpSession& rhs) = delete;

public:
    //有数据可读, 会被多个工作loop调用
    void OnRead(const std::shared_ptr<TcpConnection>& conn, Buffer* pBuffer, Timestamp receivTime);

    std::shared_ptr<TcpConnection> GetConnectionPtr()
    {
        if (m_tmpConn.expired())
            return NULL;

        return m_tmpConn.lock();
    }

    void Send(const char* data, size_t length);

private:
    bool Process(const std::shared_ptr<TcpConnection>& conn, const std::string& url, const std::string& param);
    void MakeupResponse(const std::string& input, std::string& output);

    void OnRegisterResponse(const std::string& data, const std::shared_ptr<TcpConnection>& conn);
    void OnLoginResponse(const std::string& data, const std::shared_ptr<TcpConnection>& conn);
    
private:
    std::weak_ptr<TcpConnection>       m_tmpConn;
};


#endif //!__MONITOR_SESSION_H__