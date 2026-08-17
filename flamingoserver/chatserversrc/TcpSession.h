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
//
//继承 std::enable_shared_from_this 是发送路径的需要：跨线程发送时要把「组帧 + 加密 + 写 socket」
//整段投递回本连接所属的 EventLoop 线程执行，投递出去的任务执行时本对象必须还活着，故任务里
//捕获的是自己的 shared_ptr 而不是裸 this。这要求本类的对象<u>一律由 shared_ptr 持有</u>
//（ChatServer::onConnected 正是这么建的），否则 shared_from_this 会抛 std::bad_weak_ptr。
class TcpSession : public std::enable_shared_from_this<TcpSession>
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

    /**
     *  把一段明文包体组成完整的 v2 帧发给本会话的客户端。
     *
     *  <b>可以从任意线程调用</b>：不在本连接所属的 EventLoop 线程时，本函数会把整段
     *  「组帧 + 加密 + 写 socket」投递回那个线程执行，理由见 .cpp 里的说明。
     *
     *  @param cmd  消息类型，取值见 Protocol.h 的 enum cmd_type
     *  @param seq  序列号；应答填请求里的原值，服务端主动推送填 0
     *  @param data 明文包体（通常是 UTF-8 的 JSON 文本），可为空串
     *  副作用：向本连接写出一个完整 v2 帧，并推进本连接发送方向的加密流状态；
     *          连接已销毁或组帧失败时只记日志并丢弃本帧。
     */
    void send(int32_t cmd, int32_t seq, const std::string& data);

    /**
     *  同上，包体按「起始地址 + 长度」给出。
     *  @param cmd        消息类型
     *  @param seq        序列号
     *  @param data       明文包体起始地址；为 NULL 时按空包体处理
     *  @param dataLength 明文包体字节数；小于等于 0 时按空包体处理
     */
    void send(int32_t cmd, int32_t seq, const char* data, int32_t dataLength);

    /**
     *  发送一段<u>已经组装好的</u>原始字节，本函数不再组帧、不再加密。
     *  @param p 完整的 v2 帧字节，由调用方保证其完整性
     */
    void send(const std::string& p);

    /**
     *  同上，原始字节按「起始地址 + 长度」给出。
     *  @param p      字节起始地址
     *  @param length 字节数
     */
    void send(const char* p, int32_t length);

protected:
    /**
     *  取本连接所属的 EventLoop。一条连接的收发都由它这一个线程负责，
     *  跨线程发送时据此判断要不要把动作投递回去。
     *  @return 连接已销毁时返回 NULL；返回的指针由网络层持有，本类不获取其所有权
     */
    net::EventLoop* getOwnLoop() const;

private:
    /**
     *  在本连接所属的 EventLoop 线程里完成「组帧 + 加密 + 写 socket」。
     *  只允许由 send 直接调用（已在该线程时）或经 runInLoop 投递后执行。
     */
    void sendFrameInLoop(int32_t cmd, int32_t seq, const std::string& data);

    void sendPackage(const char* p, int32_t length);

protected:
    //TcpSession引用TcpConnection类必须是弱指针，因为TcpConnection可能会因网络出错自己销毁，此时TcpSession应该也要销毁
    std::weak_ptr<TcpConnection>    tmpConn_;

    //本连接的协议加密状态。握手完成前 ready() 为 false，此时 send 走明文——
    //握手帧本身必须明文发，否则双方都没有密钥、无从谈起。
    //放在基类而不是 ChatSession，是因为发送漏斗 send() 在这里，子类各自持有的话
    //发送时还要绕回子类去取，反而绕。
    //
    //<u>它只允许在本连接所属的 EventLoop 线程里被使用</u>：加密器是流式的，每帧推进
    //一次内部状态，接收端严格按序解密。发送方向的入口只有 sendFrameInLoop 一个，
    //而那个函数保证运行在该线程上。
    net::ServerHandshake            m_handshake;
    //std::shared_ptr<TcpConnection>    tmpConn_;
};
