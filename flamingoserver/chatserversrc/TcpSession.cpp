/**
 *  TcpSession.cpp —— ChatSession 的基类，负责把消息编码成 v2 帧并发送。
 *  balloonwj@qq.com   2026-05-21
 */
#include "TcpSession.h"
#include "../net/EventLoop.h"
#include "../net/TcpConnection.h"
#include "../net/FrameCodec.h"
#include "../net/FramePayload.h"
#include "../base/AsyncLog.h"

#include <functional>

TcpSession::TcpSession(const std::weak_ptr<TcpConnection>& tmpconn) : tmpConn_(tmpconn)
{
}

TcpSession::~TcpSession()
{
}

net::EventLoop* TcpSession::getOwnLoop() const
{
    std::shared_ptr<TcpConnection> conn = tmpConn_.lock();
    if (!conn)
        return NULL;

    return conn->getLoop();
}

void TcpSession::send(int32_t cmd, int32_t seq, const std::string& data)
{
    net::EventLoop* loop = getOwnLoop();
    if (loop == NULL)
    {
        LOGE("Tcp connection is destroyed, but TcpSession is still alive, drop frame, cmd: %d", cmd);
        return;
    }

    //不在本连接所属的 EventLoop 线程时，把整段「组帧 + 加密 + 写 socket」投递回去执行。
    //
    //只把最后写 socket 那一步投回去是不够的：加密器是每条连接一份的流式对象，每帧推进
    //一次内部状态，接收端严格按序解密。若在调用方线程里加密、只把写 socket 投递回去，
    //两个线程同时给同一个人推消息时，「加密的先后」与「写到线上的先后」就可能相反，
    //接收端解密必然失败（表现为客户端报 decrypt failed 后断开重连）。把加密也一并放到
    //连接自己的线程里，两个顺序就恒等，加密器也回到「只被它自己的线程使用」的状态，
    //与框架「一条连接的收发都归它自己的 EventLoop 线程管」的线程模型一致。
    //
    //任务里捕获 shared_from_this() 而不是裸 this：任务真正执行时本会话可能已被
    //会话表移除，捕获 shared_ptr 才能保证那时对象还在。
    if (!loop->isInLoopThread())
    {
        loop->runInLoop(std::bind(&TcpSession::sendFrameInLoop, shared_from_this(), cmd, seq, data));
        return;
    }

    sendFrameInLoop(cmd, seq, data);
}

void TcpSession::send(int32_t cmd, int32_t seq, const char* data, int32_t dataLength)
{
    const size_t length = (data != NULL && dataLength > 0) ? static_cast<size_t>(dataLength) : 0;
    send(cmd, seq, std::string(data != NULL ? data : "", length));
}

void TcpSession::sendFrameInLoop(int32_t cmd, int32_t seq, const std::string& data)
{
    const uint16_t cmd16 = static_cast<uint16_t>(cmd);
    const uint32_t seq32 = static_cast<uint32_t>(seq);

    //包体的压缩与加密都由 FramePayload 这一条管线负责，顺序也在那里定死。
    //压缩策略取 never：服务端此前发出的帧就不压缩，收敛到管线时保持行为不变。
    //加密器未就绪（未启用加密 / 握手未完成）时它自然产出明文，握手帧正是走这条。
    std::string wire;
    uint8_t     flags = 0;
    uint32_t    originlen = 0;
    if (!net::encodeFramePayload(cmd16, seq32, data, &m_handshake.cipher(), 0,
                                 net::payload_compress_never, wire, flags, originlen))
    {
        LOGE("encode v2 frame payload failed, cmd: %d, dataLength: %d",
             cmd, static_cast<int>(data.size()));
        return;
    }

    //把 cmd / seq / 包体编码成一个完整的 v2 帧（24 字节帧头 + 包体）
    std::string frame;
    if (!net::encodeFrame(cmd16, seq32, flags, wire.c_str(),
                          static_cast<uint32_t>(wire.size()), originlen, frame))
    {
        LOGE("encodeFrame failed, cmd: %d, dataLength: %d", cmd, static_cast<int>(data.size()));
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
