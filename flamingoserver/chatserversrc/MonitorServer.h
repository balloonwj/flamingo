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
    bool init(const char* ip, short port, EventLoop* loop, const char* token);
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