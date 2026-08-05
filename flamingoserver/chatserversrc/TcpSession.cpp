/**
 *  TcpSession.cpp —— ChatSession 的基类，负责把消息编码成 v2 帧并发送。
 *  balloonwj@qq.com   2026-05-21
 */
#include "TcpSession.h"
#include "../net/TcpConnection.h"
#include "../net/FrameCodec.h"
#include "../base/AsyncLog.h"

TcpSession::TcpSession(const std::weak_ptr<TcpConnection>& tmpconn) : tmpConn_(tmpconn)
{
}

TcpSession::~TcpSession()
{
}

void TcpSession::send(int32_t cmd, int32_t seq, const std::string& data)
{
    send(cmd, seq, data.c_str(), static_cast<int32_t>(data.length()));
}

void TcpSession::send(int32_t cmd, int32_t seq, const char* data, int32_t dataLength)
{
    //把 cmd / seq / 包体编码成一个完整的 v2 帧（24 字节帧头 + 包体）
    std::string frame;
    if (!net::encodeFrame(static_cast<uint16_t>(cmd), static_cast<uint32_t>(seq), 0,
                          data, static_cast<uint32_t>(dataLength),
                          static_cast<uint32_t>(dataLength), frame))
    {
        LOGE("encodeFrame failed, cmd: %d, dataLength: %d", cmd, dataLength);
        return;
    }

    sendPackage(frame.c_str(), static_cast<int32_t>(frame.length()));
}

void TcpSession::send(const std::string& p)
{
    //发送一段已经组装好的原始字节（调用方自行保证其为完整 v2 帧）
    sendPackage(p.c_str(), static_cast<int32_t>(p.length()));
}

void TcpSession::send(const char* p, int32_t length)
{
    sendPackage(p, length);
}

void TcpSession::sendPackage(const char* p, int32_t length)
{
    if (tmpConn_.expired())
    {
        LOGE("Tcp connection is destroyed, but TcpSession is still alive");
        return;
    }

    std::shared_ptr<TcpConnection> conn = tmpConn_.lock();
    if (conn)
        conn->send(std::string(p, length));
}
