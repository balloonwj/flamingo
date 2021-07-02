/**
 * Http会话类, HttpSession.h
 * zhangyl 2018.05.16
 */
#ifndef __MONITOR_SESSION_H__
#define __MONITOR_SESSION_H__

#include "../net/ByteBuffer.h"
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
    void onRead(const std::shared_ptr<TcpConnection>& conn, ByteBuffer* pBuffer, Timestamp receivTime);

    std::shared_ptr<TcpConnection> getConnectionPtr()
    {
        if (m_tmpConn.expired())
            return NULL;

        return m_tmpConn.lock();
    }

    void send(const char* data, size_t length);

private:
    bool process(const std::shared_ptr<TcpConnection>& conn, const std::string& url, const std::string& param);
    void makeupResponse(const std::string& input, std::string& output);

    void onRegisterResponse(const std::string& data, const std::shared_ptr<TcpConnection>& conn);
    void onLoginResponse(const std::string& data, const std::shared_ptr<TcpConnection>& conn);
    
private:
    std::weak_ptr<TcpConnection>       m_tmpConn;
};


#endif //!__MONITOR_SESSION_H__