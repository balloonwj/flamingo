/**
 *  fileserver / imgserver 会话基类实现，TcpSession.cpp
 *  balloonwj@qq.com   2026-05-22
 */
#include "TcpSession.h"
#include "../base/AsyncLog.h"

TcpSession::TcpSession(const std::weak_ptr<TcpConnection>& tmpconn)
    : tmpConn_(tmpconn)
{
}

TcpSession::~TcpSession()
{
}

void TcpSession::sendData(const char* data, size_t len)
{
    std::shared_ptr<TcpConnection> conn = tmpConn_.lock();
    if (conn)
        conn->send(data, static_cast<int>(len));
	else
		LOGE("TcpConnection is destroyed, drop sending %u bytes", (unsigned)len);
}
