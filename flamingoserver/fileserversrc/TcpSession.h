/**
 * TcpSession.h
 * zhangyl 2017.03.09
 **/

#pragma once

#include <memory>
#include "../net/tcpconnection.h"

using namespace net;

//为了让业务与逻辑分开，实际应该新增一个子类继承自TcpSession，让TcpSession中只有逻辑代码，其子类存放业务代码
class TcpSession
{
public:
    TcpSession(const std::shared_ptr<TcpConnection>& conn);
    ~TcpSession();

    TcpSession(const TcpSession& rhs) = delete;
    TcpSession& operator =(const TcpSession& rhs) = delete;

    std::shared_ptr<TcpConnection> GetConnectionPtr()
    {
        return conn_;
    }

    void Send(int32_t cmd, int32_t seq, int32_t errorcode, const std::string& filemd5, int64_t offset, int64_t filesize, const std::string& filedata);

private:
    //支持大文件，用int64_t来存储包长，记得梳理一下文件上传于下载逻辑
    void SendPackage(const char* body, int64_t bodylength);

protected:
    std::shared_ptr<TcpConnection>    conn_;
};
