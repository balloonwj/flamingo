/**
 * TcpSession.cpp
 * zhangyl 2017.03.09
 **/
#include "../base/logging.h"
#include "TcpSession.h"
#include "FileMsg.h"

TcpSession::TcpSession(const std::shared_ptr<TcpConnection>& conn) : tmpConn_(conn)
{
    
}

TcpSession::~TcpSession()
{
    
}

void TcpSession::Send(const std::string& buf)
{
    //TODO: 确认下是否是每个TcpConnection是否只在其归属的EvenLoop中发送
    string strSendData;
    file_msg header = { (int64_t)buf.length() };
    LOG_INFO << "Send data, header length:" << sizeof(header) << ", body length:" << buf.length();
    strSendData.append((const char*)&header, sizeof(header));
    strSendData.append(buf.c_str(), buf.length());
    std::shared_ptr<TcpConnection> conn = tmpConn_.lock();
    if (conn)
    {
        size_t length = strSendData.length();
        //LOG_INFO << "Send data, length:" << length;
        LOG_DEBUG_BIN((unsigned char*)strSendData.c_str(), length);
        conn->send(strSendData.c_str(), length);
    }
}

void TcpSession::Send(const char* p, int length)
{
    //TODO: 这些Session和connection对象的生命周期要好好梳理一下
    std::shared_ptr<TcpConnection> conn = tmpConn_.lock();
    if (conn)
    {
        LOG_INFO << "Send data, length:" << length;
        LOG_DEBUG_BIN((unsigned char*)p, length);
        conn->send(p, length);
    }
}