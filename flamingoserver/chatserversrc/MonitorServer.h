/**
 * 监控服务器类，MonitorServer.h
 * zhangyl 2018.03.09
 */
#ifndef __MONITOR_SERVER_H__
#define __MONITOR_SERVER_H__
#include "MonitorSession.h"
#include <memory>
#include <mutex>
#include <list>

using namespace net;

//class MonitorSession;

class MonitorServer final
{
public:
    MonitorServer() = default;
    ~MonitorServer() = default;
    MonitorServer(const MonitorServer& rhs) = delete;
    MonitorServer& operator =(const MonitorServer& rhs) = delete;

public:
    //监控端口所用网络 I/O 工作线程数的默认值，单位：条线程。取 1 是为了与本项改造前
    //写死在 MonitorServer::init 里的那个数完全一致：监控端口同时只有零星几条 telnet
    //连接，一条线程足够。配置项 monitoriothreadnum 缺失或取值非法时回落到这里
    static const int kDefaultIoThreadCount = 1;

    /**
     *  启动监控端口的侦听。
     *  @param ip             监听地址，取自配置项 monitorlistenip
     *  @param port           监听端口，取自配置项 monitorlistenport
     *  @param loop           主事件循环，本类只使用不接管其生命周期
     *  @param token          监控端口的访问口令，取自配置项 monitortoken，不得为 NULL
     *  @param ioThreadCount  网络 I/O 工作线程数，单位：条线程，语义同 ChatServer::init
     *  @return 恒返回 true
     */
    bool init(const char* ip, short port, EventLoop* loop, const char* token, int ioThreadCount);
    void uninit();

    bool isMonitorTokenValid(const char* token);

    //新连接到来调用或连接断开，所以需要通过conn->connected()来判断，一般只在主loop里面调用
    void onConnected(std::shared_ptr<TcpConnection> conn);
    //连接断开
    void onDisconnected(const std::shared_ptr<TcpConnection>& conn);

private:
    std::unique_ptr<TcpServer>                     m_server;
    std::list<std::shared_ptr<MonitorSession>>     m_sessions;
    std::mutex                                     m_sessionMutex;      //多线程之间保护m_sessions
    std::string                                    m_token;             //查看某些敏感数据需要的token
};

#endif //!__MONITOR_SERVER_H__