/**
 * TcpSession.h
 * zhangyl 2017.03.09
 **/

#pragma once

#include <memory>
#include "../net/TcpConnection.h"
#include "../net/FrameCrypto.h"   //协议加密：握手状态与帧体加解密

using namespace net;

//为了让业务与逻辑分开，实际应该新增一个子类继承自TcpSession，让TcpSession中只有逻辑代码，其子类存放业务代码
class TcpSession
{
public:
    TcpSession(const std::weak_ptr<TcpConnection>& tmpconn);
    ~TcpSession();

    TcpSession(const TcpSession& rhs) = delete;
    TcpSession& operator =(const TcpSession& rhs) = delete;

    std::shared_ptr<TcpConnection> getConnectionPtr()
    {
        if (tmpConn_.expired())
            return NULL;

        return tmpConn_.lock();
    }

    void send(int32_t cmd, int32_t seq, const std::string& data);
    void send(int32_t cmd, int32_t seq, const char* data, int32_t dataLength);
    void send(const std::string& p);
    void send(const char* p, int32_t length);

private:
    void sendPackage(const char* p, int32_t length);

protected:
    //TcpSession引用TcpConnection类必须是弱指针，因为TcpConnection可能会因网络出错自己销毁，此时TcpSession应该也要销毁
    std::weak_ptr<TcpConnection>    tmpConn_;

    //本连接的协议加密状态。握手完成前 ready() 为 false，此时 send 走明文——
    //握手帧本身必须明文发，否则双方都没有密钥、无从谈起。
    //放在基类而不是 ChatSession，是因为发送漏斗 send() 在这里，子类各自持有的话
    //发送时还要绕回子类去取，反而绕。
    net::ServerHandshake            m_handshake;
    //std::shared_ptr<TcpConnection>    tmpConn_;
};
