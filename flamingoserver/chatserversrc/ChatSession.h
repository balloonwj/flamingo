/** 
 * ChatSession.h
 * zhangyl, 2017.03.10
 **/

#pragma once
#include "../net/Buffer.h"
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
    void OnRead(const std::shared_ptr<TcpConnection>& conn, Buffer* pBuffer, Timestamp receivTime);   
    
    int32_t GetSessionId()
    {
        return m_id;
    }

    int32_t GetUserId()
    {
        return m_userinfo.userid;
    }

    std::string GetUsername()
    {
        return m_userinfo.username;
    }

    std::string GetNickname()
    {
        return m_userinfo.nickname;
    }

    std::string GetPassword()
    {
        return m_userinfo.password;
    }

    int32_t GetClientType()
    {
        return m_userinfo.clienttype;
    }

    int32_t GetUserStatus()
    {
        return m_userinfo.status;
    }

    int32_t GetUserClientType()
    {
        return m_userinfo.clienttype;
    }

    /**
     *@param type 取值： 1 用户上线； 2 用户下线； 3 个人昵称、头像、签名等信息更改
     */
    void SendUserStatusChangeMsg(int32_t userid, int type, int status = 0);

    //让Session失效，用于被踢下线的用户的session
    void MakeSessionInvalid();
    bool IsSessionValid();

    void EnableHearbeatCheck();
    void DisableHeartbeatCheck();

    //检测心跳包，如果指定时间内（现在是30秒）未收到数据包，则主动断开于客户端的连接
    void CheckHeartbeat(const std::shared_ptr<TcpConnection>& conn);

private:
    bool Process(const std::shared_ptr<TcpConnection>& conn, const char* inbuf, size_t buflength);
    
    void OnHeartbeatResponse(const std::shared_ptr<TcpConnection>& conn);
    void OnRegisterResponse(const std::string& data, const std::shared_ptr<TcpConnection>& conn);
    void OnLoginResponse(const std::string& data, const std::shared_ptr<TcpConnection>& conn);
    void OnGetFriendListResponse(const std::shared_ptr<TcpConnection>& conn);
    void OnFindUserResponse(const std::string& data, const std::shared_ptr<TcpConnection>& conn);
    void OnChangeUserStatusResponse(const std::string& data, const std::shared_ptr<TcpConnection>& conn);
    void OnOperateFriendResponse(const std::string& data, const std::shared_ptr<TcpConnection>& conn);
    void OnAddGroupResponse(int32_t groupId, const std::shared_ptr<TcpConnection>& conn);
    void OnUpdateUserInfoResponse(const std::string& data, const std::shared_ptr<TcpConnection>& conn);
    void OnModifyPasswordResponse(const std::string& data, const std::shared_ptr<TcpConnection>& conn);
    void OnCreateGroupResponse(const std::string& data, const std::shared_ptr<TcpConnection>& conn);
    void OnGetGroupMembersResponse(const std::string& data, const std::shared_ptr<TcpConnection>& conn);
    void OnChatResponse(int32_t targetid, const std::string& data, const std::shared_ptr<TcpConnection>& conn);
    void OnMultiChatResponse(const std::string& targets, const std::string& data, const std::shared_ptr<TcpConnection>& conn);
    void OnScreenshotResponse(int32_t targetid, const std::string& bmpHeader, const std::string& bmpData, const std::shared_ptr<TcpConnection>& conn);
    void OnUpdateTeamInfoResponse(int32_t operationType, const std::string& newTeamName, const std::string& oldTeamName, const std::shared_ptr<TcpConnection>& con);
    void OnModifyMarknameResponse(int32_t friendid, const std::string& newmarkname, const std::shared_ptr<TcpConnection>& conn);
    void OnMoveFriendToOtherTeamResponse(int32_t friendid, const std::string& newteamname, const std::string& oldteamname, const std::shared_ptr<TcpConnection>& conn);

    void DeleteFriend(const std::shared_ptr<TcpConnection>& conn, int32_t friendid);

    //根据用户分组信息组装应答给客户端的好友列表信息
    void MakeUpFriendListInfo(std::string& friendinfo, const std::shared_ptr<TcpConnection>& conn);

    //将聊天消息的本地时间改成服务器时间，修改成功返回true,失败返回false。
    bool ModifyChatMsgLocalTimeToServerTime(const std::string& chatInputJson, std::string& chatOutputJson);

private:
    int32_t           m_id;                 //session id
    OnlineUserInfo    m_userinfo;
    int32_t           m_seq;                //当前Session数据包序列号
    bool              m_isLogin;            //当前Session对应的用户是否已经登录
    time_t            m_lastPackageTime;    //上一次收发包的时间
    TimerId           m_checkOnlineTimerId; //检测是否在线的定时器id
};