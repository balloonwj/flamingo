/**
 * 聊天服务支持http请求, HttpServer.h
 * zhangyl 2018.05.16
 */
#ifndef __HTTP_SERVER_H__
#define __HTTP_SERVER_H__

#include <memory>
#include <mutex>
#include <list>
#include "../net/EventLoop.h"
#include "../net/TcpServer.h"

using namespace net;

//class EventLoop;
//class TcpConnection;
//class TcpServer;
//class EventLoopThreadPool;

class HttpSession;

class HttpServer final
{
public:
    HttpServer() = default;
    ~HttpServer() = default;

    HttpServer(const HttpServer& rhs) = delete;
    HttpServer& operator =(const HttpServer& rhs) = delete;

public:
    //HTTP 端口所用网络 I/O 工作线程数的默认值，单位：条线程。取 4 是为了与本项改造前
    //的行为完全一致：那时这里调的是不带参数的 TcpServer::start()，取的正是它声明里
    //的默认参数 4（见 net/TcpServer.h）。配置项 httpiothreadnum 缺失或取值非法时
    //回落到这里
    static const int kDefaultIoThreadCount = 4;

    /**
     *  启动 HTTP 短连接端口的侦听。
     *  @param ip             监听地址。注意这里传的是 monitorlistenip 而不是 httplistenip，
     *                        属于既有行为，见 chatserversrc/main.cpp 调用处的说明
     *  @param port           监听端口，取自配置项 httplistenport
     *  @param loop           主事件循环，本类只使用不接管其生命周期
     *  @param ioThreadCount  网络 I/O 工作线程数，单位：条线程，语义同 ChatServer::init
     *  @return 恒返回 true
     */
    bool init(const char* ip, short port, EventLoop* loop, int ioThreadCount);
    void uninit();

    //新连接到来调用或连接断开，所以需要通过conn->connected()来判断，一般只在主loop里面调用
    void onConnected(std::shared_ptr<TcpConnection> conn);
    //连接断开
    void onDisconnected(const std::shared_ptr<TcpConnection>& conn);

private:
    std::unique_ptr<TcpServer>                     m_server;
    std::list<std::shared_ptr<HttpSession>>        m_sessions;
    std::mutex                                     m_sessionMutex;      //多线程之间保护m_sessions
};


#endif //!__HTTP_SERVER_H__