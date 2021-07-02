/** 
 * ChatSession.h
 * zhangyl, 2017.03.10
 **/

#pragma once
#include "../net/ByteBuffer.h"
#include "../net/TimerId.h"
#include "TcpSession.h"
using namespace net;

struct OnlineUserInfo
{
    int32_t     userid;
    std::string username;
    std::string nickname;
    std::string password;
    int32_t     clienttype;     //客户端类型, 0未知, pc=1, android/ios=2
    int32_t     status;         //在线状态 0离线 1在线 2忙碌 3离开 4隐身
};

/**
 * 聊天会话类
 */
class ChatSession : public TcpSession
{
public:
    ChatSession(const std::shared_ptr<TcpConnection>& conn, int sessionid);
    virtual ~ChatSession();

    ChatSession(const ChatSession& rhs) = delete;
    ChatSession& operator =(const ChatSession& rhs) = delete;

    //有数据可读, 会被多个工作loop调用
    void onRead(const std::shared_ptr<TcpConnection>& conn, ByteBuffer* pBuffer, Timestamp receivTime);
    
    int32_t getSessionId()
    {
        return m_id;
    }

    int32_t getUserId()
    {
        return m_userinfo.userid;
    }

    std::string getUsername()
    {
        return m_userinfo.username;
    }

    std::string getNickname()
    {
        return m_userinfo.nickname;
    }

    std::string getPassword()
    {
        return m_userinfo.password;
    }

    int32_t getClientType()
    {
        return m_userinfo.clienttype;
    }

    int32_t getUserStatus()
    {
        return m_userinfo.status;
    }

    int32_t getUserClientType()
    {
        return m_userinfo.clienttype;
    }

    /**
     *@param type 取值： 1 用户上线； 2 用户下线； 3 个人昵称、头像、签名等信息更改
     */
    void sendUserStatusChangeMsg(int32_t userid, int type, int status = 0);

    //让Session失效，用于被踢下线的用户的session
    void makeSessionInvalid();
    bool isSessionValid();

    void enableHearbeatCheck();
    void disableHeartbeatCheck();

    //检测心跳包，如果指定时间内（现在是30秒）未收到数据包，则主动断开于客户端的连接
    void checkHeartbeat(const std::shared_ptr<TcpConnection>& conn);

private:
    bool process(const std::shared_ptr<TcpConnection>& conn, const char* inbuf, size_t buflength);
    
    void onHeartbeatResponse(const std::shared_ptr<TcpConnection>& conn);
    void onRegisterResponse(const std::string& data, const std::shared_ptr<TcpConnection>& conn);
    void onLoginResponse(const std::string& data, const std::shared_ptr<TcpConnection>& conn);
    void onGetFriendListResponse(const std::shared_ptr<TcpConnection>& conn);
    void onFindUserResponse(const std::string& data, const std::shared_ptr<TcpConnection>& conn);
    void onChangeUserStatusResponse(const std::string& data, const std::shared_ptr<TcpConnection>& conn);
    void onOperateFriendResponse(const std::string& data, const std::shared_ptr<TcpConnection>& conn);
    void onAddGroupResponse(int32_t groupId, const std::shared_ptr<TcpConnection>& conn);
    void onUpdateUserInfoResponse(const std::string& data, const std::shared_ptr<TcpConnection>& conn);
    void onModifyPasswordResponse(const std::string& data, const std::shared_ptr<TcpConnection>& conn);
    void onCreateGroupResponse(const std::string& data, const std::shared_ptr<TcpConnection>& conn);
    void onGetGroupMembersResponse(const std::string& data, const std::shared_ptr<TcpConnection>& conn);
    void onChatResponse(int32_t targetid, const std::string& data, const std::shared_ptr<TcpConnection>& conn);
    void onMultiChatResponse(const std::string& targets, const std::string& data, const std::shared_ptr<TcpConnection>& conn);
    void onScreenshotResponse(int32_t targetid, const std::string& bmpHeader, const std::string& bmpData, const std::shared_ptr<TcpConnection>& conn);
    void onUpdateTeamInfoResponse(int32_t operationType, const std::string& newTeamName, const std::string& oldTeamName, const std::shared_ptr<TcpConnection>& con);
    void onModifyMarknameResponse(int32_t friendid, const std::string& newmarkname, const std::shared_ptr<TcpConnection>& conn);
    void onMoveFriendToOtherTeamResponse(int32_t friendid, const std::string& newteamname, const std::string& oldteamname, const std::shared_ptr<TcpConnection>& conn);

    void deleteFriend(const std::shared_ptr<TcpConnection>& conn, int32_t friendid);

    //根据用户分组信息组装应答给客户端的好友列表信息
    void makeUpFriendListInfo(std::string& friendinfo, const std::shared_ptr<TcpConnection>& conn);

    //将聊天消息的本地时间改成服务器时间，修改成功返回true,失败返回false。
    bool modifyChatMsgLocalTimeToServerTime(const std::string& chatInputJson, std::string& chatOutputJson);

private:
    int32_t           m_id;                 //session id
    OnlineUserInfo    m_userinfo;
    int32_t           m_seq;                //当前Session数据包序列号
    bool              m_isLogin;            //当前Session对应的用户是否已经登录
    time_t            m_lastPackageTime;    //上一次收发包的时间
    TimerId           m_checkOnlineTimerId; //检测是否在线的定时器id
};