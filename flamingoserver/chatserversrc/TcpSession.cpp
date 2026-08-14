/**
 *  TcpSession.cpp —— ChatSession 的基类，负责把消息编码成 v2 帧并发送。
 *  balloonwj@qq.com   2026-05-21
 */
#include "TcpSession.h"
#include "../net/TcpConnection.h"
#include "../net/FrameCodec.h"
#include "../net/FramePayload.h"
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
    const uint16_t cmd16 = static_cast<uint16_t>(cmd);
    const uint32_t seq32 = static_cast<uint32_t>(seq);
    const std::string plain(data != NULL ? data : "", static_cast<size_t>(dataLength));

    //包体的压缩与加密都由 FramePayload 这一条管线负责，顺序也在那里定死。
    //压缩策略取 never：服务端此前发出的帧就不压缩，收敛到管线时保持行为不变。
    //加密器未就绪（未启用加密 / 握手未完成）时它自然产出明文，握手帧正是走这条。
    std::string wire;
    uint8_t     flags = 0;
    uint32_t    originlen = 0;
    if (!net::encodeFramePayload(cmd16, seq32, plain, &m_handshake.cipher(), 0,
                                 net::payload_compress_never, wire, flags, originlen))
    {
        LOGE("encode v2 frame payload failed, cmd: %d, dataLength: %d", cmd, dataLength);
        return;
    }

    //把 cmd / seq / 包体编码成一个完整的 v2 帧（24 字节帧头 + 包体）
    std::string frame;
    if (!net::encodeFrame(cmd16, seq32, flags, wire.c_str(),
                          static_cast<uint32_t>(wire.size()), originlen, frame))
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
