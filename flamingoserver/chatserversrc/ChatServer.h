/** 
 *  服务器主服务类，IMServer.h
 *  zhangyl 2017.03.09
 **/
#pragma once
#include <memory>
#include <list>
#include <map>
#include <mutex>
#include <atomic>
#include "../net/TcpServer.h"
#include "../net/EventLoop.h"
#include "ChatSession.h"

using namespace net;

enum CLIENT_TYPE
{
    CLIENT_TYPE_UNKOWN,
    CLIENT_TYPE_PC,
    CLIENT_TYPE_ANDROID,
    CLIENT_TYPE_IOS,
    CLIENT_TYPE_MAC
};

struct StoredUserInfo
{
    int32_t         userid;
    std::string     username;
    std::string     password;
    std::string     nickname;
};

class ChatServer final
{
public:
    ChatServer();
    ~ChatServer() = default;

    ChatServer(const ChatServer& rhs) = delete;
    ChatServer& operator =(const ChatServer& rhs) = delete;

    bool init(const char* ip, short port, EventLoop* loop);
    void uninit();

    void enableLogPackageBinary(bool enable);
    bool isLogPackageBinaryEnabled();

    void getSessions(std::list<std::shared_ptr<ChatSession>>& sessions);
    //用户id和clienttype会唯一确定一个session
    bool getSessionByUserIdAndClientType(std::shared_ptr<ChatSession>& session, int32_t userid, int32_t clientType);

    bool getSessionsByUserId(std::list<std::shared_ptr<ChatSession>>& sessions, int32_t userid);

    //获取用户状态，若该用户不存在，则返回0
    int32_t getUserStatusByUserId(int32_t userid);
    //获取用户客户端类型，如果该用户不存在，则返回0
    int32_t getUserClientTypeByUserId(int32_t userid);

private:
    //新连接到来调用或连接断开，所以需要通过conn->connected()来判断，一般只在主loop里面调用
    void onConnected(std::shared_ptr<TcpConnection> conn);  
    //连接断开
    void onDisconnected(const std::shared_ptr<TcpConnection>& conn);
   
private:
    std::unique_ptr<TcpServer>                     m_server;
    std::list<std::shared_ptr<ChatSession>>        m_sessions;
    std::mutex                                     m_sessionMutex;      //多线程之间保护m_sessions
    std::atomic_int                                m_sessionId{};
    std::mutex                                     m_idMutex;           //多线程之间保护m_baseUserId
    std::atomic_bool                               m_logPackageBinary;  //是否日志打印出包的二进制数据
};
