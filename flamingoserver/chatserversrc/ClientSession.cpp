/**
 * ClientSession.cpp
 * zhangyl, 2017.03.10
 **/
#include <string.h>
#include <sstream>
#include <list>
#include "../net/tcpconnection.h"
#include "../net/protocolstream.h"
#include "../base/logging.h"
#include "../base/singleton.h"
#include "../jsoncpp-0.5.0/json.h"
#include "ClientSession.h"
#include "Msg.h"
#include "UserManager.h"
#include "IMServer.h"
#include "MsgCacheManager.h"

using namespace std;
using namespace net;
using namespace balloon;

ClientSession::ClientSession(const std::shared_ptr<TcpConnection>& conn) :  
TcpSession(conn), 
m_id(0),
m_seq(0)
{
	m_userinfo.userid = 0;
}

ClientSession::~ClientSession()
{

}

void ClientSession::OnRead(const std::shared_ptr<TcpConnection>& conn, Buffer* pBuffer, Timestamp receivTime)
{
    while (true)
    {
        //不够一个包头大小
        if (pBuffer->readableBytes() < (size_t)sizeof(msg))
        {
            LOG_INFO << "buffer is not enough for a package header, pBuffer->readableBytes()=" << pBuffer->readableBytes() << ", sizeof(msg)=" << sizeof(msg);
            return;
        }

        //不够一个整包大小
        msg header;
        memcpy(&header, pBuffer->peek(), sizeof(msg));
        if (pBuffer->readableBytes() < (size_t)header.packagesize + sizeof(msg))
            return;

        pBuffer->retrieve(sizeof(msg));
        std::string inbuf;
        inbuf.append(pBuffer->peek(), header.packagesize);
        pBuffer->retrieve(header.packagesize);
        if (!Process(conn, inbuf.c_str(), inbuf.length()))
        {
            LOG_WARN << "Process error, close TcpConnection";
            conn->forceClose();
        }
    }// end while-loop

}

bool ClientSession::Process(const std::shared_ptr<TcpConnection>& conn, const char* inbuf, size_t length)
{
    balloon::BinaryReadStream readStream(inbuf, length);
    int32_t cmd;
    if (!readStream.ReadInt32(cmd))
    {
        LOG_WARN << "read cmd error !!!";
        return false;
    }

    //int seq;
    if (!readStream.ReadInt32(m_seq))
    {
        LOG_WARN << "read seq error !!!";
        return false;
    }

    std::string data;
    size_t datalength;
    if (!readStream.ReadString(&data, 0, datalength))
    {
        LOG_WARN << "read data error !!!";
        return false;
    }
   
    LOG_INFO << "Recv from client: cmd=" << cmd << ", seq=" << m_seq << ", header.packagesize:" << length << ", data=" << data << ", datalength=" << datalength;
    LOG_DEBUG_BIN((unsigned char*)inbuf, length);

    switch (cmd)
    {
        //心跳包
        case msg_type_heartbeart:
        {
            OnHeartbeatResponse(conn);
        }
            break;

        //注册
        case msg_type_register:
        {
            OnRegisterResponse(data, conn);
        }
            break;
        
        //登录
        case msg_type_login:
        {                              
            OnLoginResponse(data, conn);
        }
            break;

        //获取好友列表
        case msg_type_getofriendlist:
        {
            OnGetFriendListResponse(conn);
        }
            break;

        //查找用户
        case msg_type_finduser:
        {
            OnFindUserResponse(data, conn);
        }
            break;

        //加好友
        case msg_type_operatefriend:
        {        
            OnOperateFriendResponse(data, conn);
        }
            break;

        //更新用户信息
        case msg_type_updateuserinfo:
        {
            OnUpdateUserInfoResponse(data, conn);
        }
            break;
        
        //修改密码
        case msg_type_modifypassword:
        {
            OnModifyPasswordResponse(data, conn);
        }
            break;
        
        //创建群
        case msg_type_creategroup:
        {
            OnCreateGroupResponse(data, conn);
        }
            break;

        //获取指定群成员信息
        case msg_type_getgroupmembers:
        {
            OnGetGroupMembersResponse(data, conn);
        }
            break;

        //聊天消息
        case msg_type_chat:
        {
            int32_t target;
            if (!readStream.ReadInt32(target))
            {
                LOG_WARN << "read target error !!!";
                return false;
            }
            OnChatResponse(target, data, conn);
        }
            break;
        
        //群发消息
        case msg_type_multichat:
        {
            std::string targets;
            size_t targetslength;
            if (!readStream.ReadString(&targets, 0, targetslength))
            {
                LOG_WARN << "read targets error !!!";
                return false;
            }

            OnMultiChatResponse(targets, data, conn);
        }

            break;

        default:
            //pBuffer->retrieveAll();
            LOG_WARN << "unsupport cmd, cmd:" << cmd << ", data=" << data << ", connection name:" << conn->peerAddress().toIpPort();
            //conn->forceClose();
            return false;
    }// end switch

    ++ m_seq;

    return true;
}

void ClientSession::OnHeartbeatResponse(const std::shared_ptr<TcpConnection>& conn)
{
    std::string outbuf;
    BinaryWriteStream writeStream(&outbuf);
    writeStream.WriteInt32(msg_type_heartbeart);
    writeStream.WriteInt32(m_seq);
    std::string dummy;
    writeStream.WriteString(dummy);
    writeStream.Flush();

    LOG_INFO << "Response to client: cmd=1000" << ", sessionId=" << m_id;

    Send(outbuf);
}

void ClientSession::OnRegisterResponse(const std::string& data, const std::shared_ptr<TcpConnection>& conn)
{
    //{ "user": "13917043329", "nickname" : "balloon", "password" : "123" }
    Json::Reader JsonReader;
    Json::Value JsonRoot;
    if (!JsonReader.parse(data, JsonRoot))
    {
        LOG_WARN << "invalid json: " << data << ", sessionId = " << m_id << ", client: " << conn->peerAddress().toIpPort();
        return;
    }

    if (!JsonRoot["username"].isString() || !JsonRoot["nickname"].isString() || !JsonRoot["password"].isString())
    {
        LOG_WARN << "invalid json: " << data << ", sessionId = " << m_id << ", client: " << conn->peerAddress().toIpPort();
        return;
    }

    User u;
    u.username = JsonRoot["username"].asString();
    u.nickname = JsonRoot["nickname"].asString();
    u.password = JsonRoot["password"].asString();

    std::string retData;
    User cachedUser;
    cachedUser.userid = 0;
    Singleton<UserManager>::Instance().GetUserInfoByUsername(u.username, cachedUser);
    if (cachedUser.userid != 0)
        retData = "{\"code\": 101, \"msg\": \"registered already\"}";
    else
    {
        if (!Singleton<UserManager>::Instance().AddUser(u))
            retData = "{\"code\": 100, \"msg\": \"register failed\"}";
        else
            retData = "{\"code\": 0, \"msg\": \"ok\"}";
    }
    
    std::string outbuf;
    BinaryWriteStream writeStream(&outbuf);
    writeStream.WriteInt32(msg_type_register);
    writeStream.WriteInt32(m_seq);
    writeStream.WriteString(retData);
    writeStream.Flush();

    LOG_INFO << "Response to client: cmd=msg_type_register" << ", userid=" << u.userid << ", data=" << retData;

    Send(outbuf);
}

void ClientSession::OnLoginResponse(const std::string& data, const std::shared_ptr<TcpConnection>& conn)
{
    //{"username": "13917043329", "password": "123", "clienttype": 1, "status": 1}
    Json::Reader JsonReader;
    Json::Value JsonRoot;
    if (!JsonReader.parse(data, JsonRoot))
    {
        LOG_WARN << "invalid json: " << data << ", sessionId = " << m_id  << ", client: " << conn->peerAddress().toIpPort();
        return;
    }

    if (!JsonRoot["username"].isString() || !JsonRoot["password"].isString() || !JsonRoot["clienttype"].isInt() || !JsonRoot["status"].isInt())
    {
        LOG_WARN << "invalid json: " << data << ", sessionId = " << m_id << ", client: " << conn->peerAddress().toIpPort();
        return;
    }

    string username = JsonRoot["username"].asString();
    string password = JsonRoot["password"].asString();
    int clientType = JsonRoot["clienttype"].asInt();
    std::ostringstream os;
    User cachedUser;
    cachedUser.userid = 0;
    Singleton<UserManager>::Instance().GetUserInfoByUsername(username, cachedUser);
    IMServer& imserver = Singleton<IMServer>::Instance();
    if (cachedUser.userid == 0)
    {
        //TODO: 这些硬编码的字符应该统一放到某个地方统一管理
        os << "{\"code\": 102, \"msg\": \"not registered\"}";
    }
    else
    {
        if (cachedUser.password != password)
            os << "{\"code\": 103, \"msg\": \"incorrect password\"}";
        else
        {
            //如果该账号已经登录，则将前一个账号踢下线
            std::shared_ptr<ClientSession> targetSession;
            imserver.GetSessionByUserId(targetSession, cachedUser.userid);          
            if (targetSession)
            {
                //由于服务器端支持多类型终端登录，所以只有同一类型的终端用同一个账号登录才踢下前一个账号
                if (targetSession->GetClientType() == clientType)
                {
                    string outbuf;
                    BinaryWriteStream writeStream(&outbuf);
                    writeStream.WriteInt32(msg_type_kickuser);
                    writeStream.WriteInt32(m_seq);
                    string dummydata;
                    writeStream.WriteString(dummydata);
                    writeStream.Flush();
                    targetSession->Send(outbuf);
                    //关闭连接
                    //targetSession->GetConnectionPtr()->shutdown();
                }
            }
            
            
            //记录用户信息
            m_userinfo.userid = cachedUser.userid;
            m_userinfo.username = username;
            m_userinfo.nickname = cachedUser.nickname;
            m_userinfo.password = password;
            m_userinfo.clienttype = JsonRoot["clienttype"].asInt();
            m_userinfo.status = JsonRoot["status"].asInt();

            os << "{\"code\": 0, \"msg\": \"ok\", \"userid\": " << m_userinfo.userid << ",\"username\":\"" << cachedUser.username << "\", \"nickname\":\"" 
               << cachedUser.nickname << "\", \"facetype\": " << cachedUser.facetype << ", \"customface\":\"" << cachedUser.customface << "\", \"gender\":" << cachedUser.gender
               << ", \"birthday\":" << cachedUser.birthday << ", \"signature\":\"" << cachedUser.signature << "\", \"address\": \"" << cachedUser.address
               << "\", \"phonenumber\": \"" << cachedUser.phonenumber << "\", \"mail\":\"" << cachedUser.mail << "\"}";            
        }
    }
   
    //登录信息应答
    std::string outbuf;
    BinaryWriteStream writeStream(&outbuf);
    writeStream.WriteInt32(msg_type_login);
    writeStream.WriteInt32(m_seq);
    writeStream.WriteString(os.str());
    writeStream.Flush();

    LOG_INFO << "Response to client: cmd=msg_type_login, data=" << os.str() << ", userid=" << m_userinfo.userid;
    
    Send(outbuf);

    //推送通知消息
    std::list<NotifyMsgCache> listNotifyCache;
    Singleton<MsgCacheManager>::Instance().GetNotifyMsgCache(m_userinfo.userid, listNotifyCache);
    for (const auto &iter : listNotifyCache)
    {
        Send(iter.notifymsg);
    }

    //推送聊天消息
    std::list<ChatMsgCache> listChatCache;
    Singleton<MsgCacheManager>::Instance().GetChatMsgCache(m_userinfo.userid, listChatCache);
    for (const auto &iter : listChatCache)
    {
        Send(iter.chatmsg);
    }

    //给其他用户推送上线消息
    std::list<User> friends;
    Singleton<UserManager>::Instance().GetFriendInfoByUserId(m_userinfo.userid, friends);
    for (const auto& iter : friends)
    {
        //先看目标用户是否在线
        std::shared_ptr<ClientSession> targetSession;
        imserver.GetSessionByUserId(targetSession, iter.userid);
        if (targetSession)
            targetSession->SendUserStatusChangeMsg(m_userinfo.userid, 1);
    }  
}

void ClientSession::OnGetFriendListResponse(const std::shared_ptr<TcpConnection>& conn)
{
    std::list<User> friends;
    Singleton<UserManager>::Instance().GetFriendInfoByUserId(m_userinfo.userid, friends);
	std::string strUserInfo;
    bool userOnline = false;
    IMServer& imserver = Singleton<IMServer>::Instance();
    for (const auto& iter : friends)
    {	
        userOnline = imserver.IsUserSessionExsit(iter.userid);
        /*
        {"code": 0, "msg": "ok", "userinfo":[{"userid": 1,"username":"qqq, 
        "nickname":"qqq, "facetype": 0, "customface":"", "gender":0, "birthday":19900101, 
        "signature":", "address": "", "phonenumber": "", "mail":", "clienttype": 1, "status":1"]}
        */
        ostringstream osSingleUserInfo;
        osSingleUserInfo << "{\"userid\": " << iter.userid << ",\"username\":\"" << iter.username << "\", \"nickname\":\"" << iter.nickname
                         << "\", \"facetype\": " << iter.facetype << ", \"customface\":\"" << iter.customface << "\", \"gender\":" << iter.gender
                         << ", \"birthday\":" << iter.birthday << ", \"signature\":\"" << iter.signature << "\", \"address\": \"" << iter.address
                         << "\", \"phonenumber\": \"" << iter.phonenumber << "\", \"mail\":\"" << iter.mail << "\", \"clienttype\": 1, \"status\":"
                         << (userOnline ? 1 : 0) << "}";

        strUserInfo += osSingleUserInfo.str();
        strUserInfo += ",";
    }
	//去掉最后多余的逗号
	strUserInfo = strUserInfo.substr(0, strUserInfo.length() - 1);
	std::ostringstream os;
	os << "{\"code\": 0, \"msg\": \"ok\", \"userinfo\":[" << strUserInfo << "]}";

	std::string outbuf;
	BinaryWriteStream writeStream(&outbuf);
    writeStream.WriteInt32(msg_type_getofriendlist);
	writeStream.WriteInt32(m_seq);
	writeStream.WriteString(os.str());
	writeStream.Flush();

    LOG_INFO << "Response to client: cmd=msg_type_getofriendlist, data=" << os.str() << ", userid=" << m_userinfo.userid;

    Send(outbuf);
}

void ClientSession::OnFindUserResponse(const std::string& data, const std::shared_ptr<TcpConnection>& conn)
{
    //{ "type": 1, "username" : "zhangyl" }
    Json::Reader JsonReader;
    Json::Value JsonRoot;
    if (!JsonReader.parse(data, JsonRoot))
    {
        LOG_WARN << "invalid json: " << data << ", userid: " << m_userinfo.userid << ", client: " << conn->peerAddress().toIpPort();
        return;
    }

    if (!JsonRoot["type"].isInt() || !JsonRoot["username"].isString())
    {
        LOG_WARN << "invalid json: " << data << ", userid: " << m_userinfo.userid << ", client: " << conn->peerAddress().toIpPort();
        return;
    }

    string retData;
    //TODO: 目前只支持查找单个用户
    string username = JsonRoot["username"].asString();
    User cachedUser;
    if (!Singleton<UserManager>::Instance().GetUserInfoByUsername(username, cachedUser))
        retData = "{ \"code\": 0, \"msg\": \"ok\", \"userinfo\": [] }";
    else
    {
        //TODO: 用户比较多的时候，应该使用动态string
        char szUserInfo[256] = { 0 };
        snprintf(szUserInfo, 256, "{ \"code\": 0, \"msg\": \"ok\", \"userinfo\": [{\"userid\": %d, \"username\": \"%s\", \"nickname\": \"%s\", \"facetype\":%d}] }", cachedUser.userid, cachedUser.username.c_str(), cachedUser.nickname.c_str(), cachedUser.facetype);
        retData = szUserInfo;
    }

    std::string outbuf;
    BinaryWriteStream writeStream(&outbuf);
    writeStream.WriteInt32(msg_type_finduser);
    writeStream.WriteInt32(m_seq);
    writeStream.WriteString(retData);
    writeStream.Flush();

    LOG_INFO << "Response to client: cmd=msg_type_finduser, data=" << retData << ", userid=" << m_userinfo.userid;

    Send(outbuf);
}

void ClientSession::OnOperateFriendResponse(const std::string& data, const std::shared_ptr<TcpConnection>& conn)
{
    Json::Reader JsonReader;
    Json::Value JsonRoot;
    if (!JsonReader.parse(data, JsonRoot))
    {
        LOG_WARN << "invalid json: " << data << ", userid: " << m_userinfo.userid << ", client: " << conn->peerAddress().toIpPort();
        return;
    }

    if (!JsonRoot["type"].isInt() || !JsonRoot["userid"].isInt())
    {
        LOG_WARN << "invalid json: " << data << ", userid: " << m_userinfo.userid << ", client: " << conn->peerAddress().toIpPort();
        return;
    }

    int type = JsonRoot["type"].asInt();
    int32_t targetUserid = JsonRoot["userid"].asInt();
    if (targetUserid >= GROUPID_BOUBDARY)
    {
        if (type == 4)
        {
            //退群
            DeleteFriend(conn, targetUserid);
            return;
        }

        //加群直接同意
        OnAddGroupResponse(targetUserid, conn);
        return;
    }

    char szData[256] = { 0 };
    //删除好友
    if (type == 4)
    {
        DeleteFriend(conn, targetUserid);
        return;
    }
    //发出加好友申请
    if (type == 1)
    {
        //{"userid": 9, "type": 1, }        
        snprintf(szData, 256, "{\"userid\":%d, \"type\":2, \"username\": \"%s\"}", m_userinfo.userid, m_userinfo.username.c_str());
    }
    //应答加好友
    else if (type == 3)
    {
        if (!JsonRoot["accept"].isInt())
        {
            LOG_WARN << "invalid json: " << data << ", userid: " << m_userinfo.userid << "client: " << conn->peerAddress().toIpPort();
            return;
        }

        int accept = JsonRoot["accept"].asInt();
        //接受加好友申请后，建立好友关系
        if (accept == 1)
        {
            int smallid = m_userinfo.userid;
            int greatid = targetUserid;
            //数据库里面互为好友的两个人id，小者在先，大者在后
            if (smallid > greatid)
            {
                smallid = targetUserid;
                greatid = m_userinfo.userid;
            }

            if (!Singleton<UserManager>::Instance().MakeFriendRelationship(smallid, greatid))
            {
                LOG_ERROR << "make relationship error: " << data << ", userid: " << m_userinfo.userid << "client: " << conn->peerAddress().toIpPort();
                return;
            }
        }

        //{ "userid": 9, "type" : 3, "userid" : 9, "username" : "xxx", "accept" : 1 }
        snprintf(szData, 256, "{\"userid\": %d, \"type\": 3, \"username\": \"%s\", \"accept\": %d}", m_userinfo.userid, m_userinfo.username.c_str(), accept);

        //提示自己当前用户加好友成功
        User targetUser;
        if (!Singleton<UserManager>::Instance().GetUserInfoByUserId(targetUserid, targetUser))
        {
            LOG_ERROR << "Get Userinfo by id error, targetuserid: " << targetUserid << ", userid: " << m_userinfo.userid << ", data: "<< data << ", client: " << conn->peerAddress().toIpPort();
            return;
        }
        char szSelfData[256] = { 0 };
        snprintf(szSelfData, 256, "{\"userid\": %d, \"type\": 3, \"username\": \"%s\", \"accept\": %d}", targetUser.userid, targetUser.username.c_str(), accept);
        std::string outbufx;
        BinaryWriteStream writeStream(&outbufx);
        writeStream.WriteInt32(msg_type_operatefriend);
        writeStream.WriteInt32(m_seq);
        writeStream.WriteCString(szSelfData, strlen(szSelfData));
        writeStream.Flush();

        Send(outbufx);
        LOG_INFO << "Response to client: cmd=msg_type_addfriend, data=" << szSelfData << ", userid=" << m_userinfo.userid;
    }

    //提示对方加好友成功
    std::string outbuf;
    balloon::BinaryWriteStream writeStream(&outbuf);
    writeStream.WriteInt32(msg_type_operatefriend);
    writeStream.WriteInt32(m_seq);
    writeStream.WriteCString(szData, strlen(szData));
    writeStream.Flush();

    //先看目标用户是否在线
    std::shared_ptr<ClientSession> targetSession;
    Singleton<IMServer>::Instance().GetSessionByUserId(targetSession, targetUserid);
    //目标用户不在线，缓存这个消息
    if (!targetSession)
    {
        Singleton<MsgCacheManager>::Instance().AddNotifyMsgCache(targetUserid, outbuf);
        LOG_INFO << "userid: " << targetUserid << " is not online, cache notify msg, msg: " << outbuf;
        return;
    }

    targetSession->Send(outbuf);

    LOG_INFO << "Response to client: cmd=msg_type_addfriend, data=" << data << ", userid=" << targetUserid;
}

void ClientSession::OnAddGroupResponse(int32_t groupId, const std::shared_ptr<TcpConnection>& conn)
{
    if (!Singleton<UserManager>::Instance().MakeFriendRelationship(m_userinfo.userid, groupId))
    {
        LOG_ERROR << "make relationship error, groupId: " << groupId << ", userid: " << m_userinfo.userid << "client: " << conn->peerAddress().toIpPort();
        return;
    }
    
    User groupUser;
    if (!Singleton<UserManager>::Instance().GetUserInfoByUserId(groupId, groupUser))
    {
        LOG_ERROR << "Get group info by id error, targetuserid: " << groupId << ", userid: " << m_userinfo.userid << ", client: " << conn->peerAddress().toIpPort();
        return;
    }
    char szSelfData[256] = { 0 };
    snprintf(szSelfData, 256, "{\"userid\": %d, \"type\": 3, \"username\": \"%s\", \"accept\": 3}", groupUser.userid, groupUser.username.c_str());
    std::string outbufx;
    BinaryWriteStream writeStream(&outbufx);
    writeStream.WriteInt32(msg_type_operatefriend);
    writeStream.WriteInt32(m_seq);
    writeStream.WriteCString(szSelfData, strlen(szSelfData));
    writeStream.Flush();

    Send(outbufx);
    LOG_INFO << "Response to client: cmd=msg_type_addfriend, data=" << szSelfData << ", userid=" << m_userinfo.userid;

    //给其他在线群成员推送群信息发生变化的消息
    std::list<User> friends;
    Singleton<UserManager>::Instance().GetFriendInfoByUserId(groupId, friends);
    IMServer& imserver = Singleton<IMServer>::Instance();
    for (const auto& iter : friends)
    {
        //先看目标用户是否在线
        std::shared_ptr<ClientSession> targetSession;
        imserver.GetSessionByUserId(targetSession, iter.userid);
        if (targetSession)
            targetSession->SendUserStatusChangeMsg(groupId, 3);
    }
}

void ClientSession::OnUpdateUserInfoResponse(const std::string& data, const std::shared_ptr<TcpConnection>& conn)
{
    Json::Reader JsonReader;
    Json::Value JsonRoot;
    if (!JsonReader.parse(data, JsonRoot))
    {
        LOG_WARN << "invalid json: " << data << ", userid: " << m_userinfo.userid << ", client: " << conn->peerAddress().toIpPort();
        return;
    }

    if (!JsonRoot["nickname"].isString() || !JsonRoot["facetype"].isInt() || 
        !JsonRoot["customface"].isString() || !JsonRoot["gender"].isInt() || 
        !JsonRoot["birthday"].isInt() || !JsonRoot["signature"].isString() || 
        !JsonRoot["address"].isString() || !JsonRoot["phonenumber"].isString() || 
        !JsonRoot["mail"].isString())
    {
        LOG_WARN << "invalid json: " << data << ", userid: " << m_userinfo.userid << ", client: " << conn->peerAddress().toIpPort();
        return;
    }

    User newuserinfo;
    newuserinfo.nickname = JsonRoot["nickname"].asString();
    newuserinfo.facetype = JsonRoot["facetype"].asInt();
    newuserinfo.customface = JsonRoot["customface"].asString();
    newuserinfo.gender = JsonRoot["gender"].asInt();
    newuserinfo.birthday = JsonRoot["birthday"].asInt();
    newuserinfo.signature = JsonRoot["signature"].asString();
    newuserinfo.address = JsonRoot["address"].asString();
    newuserinfo.phonenumber = JsonRoot["phonenumber"].asString();
    newuserinfo.mail = JsonRoot["mail"].asString();
    
    ostringstream retdata;
    ostringstream currentuserinfo;
    if (!Singleton<UserManager>::Instance().UpdateUserInfo(m_userinfo.userid, newuserinfo))
    {
        retdata << "{ \"code\": 104, \"msg\": \"update user info failed\" }";
    }
    else
    {
        /*
        { "code": 0, "msg" : "ok", "userid" : 2, "username" : "xxxx", 
         "nickname":"zzz", "facetype" : 26, "customface" : "", "gender" : 0, "birthday" : 19900101, 
         "signature" : "xxxx", "address": "", "phonenumber": "", "mail":""}
        */
        currentuserinfo << "\"userid\": " << m_userinfo.userid << ",\"username\":\"" << m_userinfo.username
                        << "\", \"nickname\":\"" << newuserinfo.nickname
                        << "\", \"facetype\": " << newuserinfo.facetype << ", \"customface\":\"" << newuserinfo.customface
                        << "\", \"gender\":" << newuserinfo.gender
                        << ", \"birthday\":" << newuserinfo.birthday << ", \"signature\":\"" << newuserinfo.signature
                        << "\", \"address\": \"" << newuserinfo.address
                        << "\", \"phonenumber\": \"" << newuserinfo.phonenumber << "\", \"mail\":\""
                        << newuserinfo.mail;
        retdata << "{\"code\": 0, \"msg\": \"ok\"," << currentuserinfo.str()  << "\"}";
    }

    std::string outbuf;
    BinaryWriteStream writeStream(&outbuf);
    writeStream.WriteInt32(msg_type_updateuserinfo);
    writeStream.WriteInt32(m_seq);
    writeStream.WriteString(retdata.str());
    writeStream.Flush();

    //应答客户端
    Send(outbuf);

    LOG_INFO << "Response to client: cmd=msg_type_updateuserinfo, data=" << retdata.str() << ", userid=" << m_userinfo.userid;

    //给其他在线好友推送个人信息发生改变消息
    std::list<User> friends;
    Singleton<UserManager>::Instance().GetFriendInfoByUserId(m_userinfo.userid, friends);
    IMServer& imserver = Singleton<IMServer>::Instance();
    for (const auto& iter : friends)
    {
        //先看目标用户是否在线
        std::shared_ptr<ClientSession> targetSession;
        imserver.GetSessionByUserId(targetSession, iter.userid);
        if (targetSession)
            targetSession->SendUserStatusChangeMsg(m_userinfo.userid, 3);
    }
}

void ClientSession::OnModifyPasswordResponse(const std::string& data, const std::shared_ptr<TcpConnection>& conn)
{
    Json::Reader JsonReader;
    Json::Value JsonRoot;
    if (!JsonReader.parse(data, JsonRoot))
    {
        LOG_WARN << "invalid json: " << data << ", userid: " << m_userinfo.userid << ", client: " << conn->peerAddress().toIpPort();
        return;
    }

    if (!JsonRoot["oldpassword"].isString() || !JsonRoot["newpassword"].isString())
    {
        LOG_WARN << "invalid json: " << data << ", userid: " << m_userinfo.userid << ", client: " << conn->peerAddress().toIpPort();
        return;
    }

    string oldpass = JsonRoot["oldpassword"].asString();
    string newPass = JsonRoot["newpassword"].asString();

    string retdata;
    User cachedUser;
    if (!Singleton<UserManager>::Instance().GetUserInfoByUserId(m_userinfo.userid, cachedUser))
    {
        LOG_ERROR << "get userinfo error, userid: " << m_userinfo.userid << ", data: " << data << ", client: " << conn->peerAddress().toIpPort();
        return;
    }

    if (cachedUser.password != oldpass)
    {
        retdata = "{\"code\": 103, \"msg\": \"incorrect old password\"}";
    }
    else
    {       
        if (!Singleton<UserManager>::Instance().ModifyUserPassword(m_userinfo.userid, newPass))
        {
            retdata = "{\"code\": 105, \"msg\": \"modify password error\"}";
            LOG_ERROR << "modify password error, userid: " << m_userinfo.userid << ", data: " << data << ", client: " << conn->peerAddress().toIpPort();
        }
        else
            retdata = "{\"code\": 0, \"msg\": \"ok\"}";
    }

    std::string outbuf;
    BinaryWriteStream writeStream(&outbuf);
    writeStream.WriteInt32(msg_type_modifypassword);
    writeStream.WriteInt32(m_seq);
    writeStream.WriteString(retdata);
    writeStream.Flush();

    //应答客户端
    Send(outbuf);

    LOG_INFO << "Response to client: cmd=msg_type_modifypassword, data=" << data << ", userid=" << m_userinfo.userid;
}

void ClientSession::OnCreateGroupResponse(const std::string& data, const std::shared_ptr<TcpConnection>& conn)
{
    Json::Reader JsonReader;
    Json::Value JsonRoot;
    if (!JsonReader.parse(data, JsonRoot))
    {
        LOG_WARN << "invalid json: " << data << ", userid: " << m_userinfo.userid << ", client: " << conn->peerAddress().toIpPort();
        return;
    }

    if (!JsonRoot["groupname"].isString())
    {
        LOG_WARN << "invalid json: " << data << ", userid: " << m_userinfo.userid << ", client: " << conn->peerAddress().toIpPort();
        return;
    }

    ostringstream retdata;
    string groupname = JsonRoot["groupname"].asString();
    int32_t groupid;
    if (!Singleton<UserManager>::Instance().AddGroup(groupname.c_str(), m_userinfo.userid, groupid))
    {
        LOG_WARN << "Add group error, data: " << data << ", userid: " << m_userinfo.userid << ", client: " << conn->peerAddress().toIpPort();
        retdata << "{ \"code\": 106, \"msg\" : \"create group error\"}";
    }
    else
    {
        retdata << "{\"code\": 0, \"msg\": \"ok\", \"groupid\":" << groupid << ", \"groupname\": \"" << groupname << "\"}";
    }

    //创建成功以后该用户自动加群
    if (!Singleton<UserManager>::Instance().MakeFriendRelationship(m_userinfo.userid, groupid))
    {
        LOG_ERROR << "join in group, errordata: " << data << ", userid: " << m_userinfo.userid << ", client: " << conn->peerAddress().toIpPort();
        return;
    }

    std::string outbuf;
    BinaryWriteStream writeStream(&outbuf);
    writeStream.WriteInt32(msg_type_creategroup);
    writeStream.WriteInt32(m_seq);
    writeStream.WriteString(retdata.str());
    writeStream.Flush();

    //应答客户端，建群成功
    Send(outbuf);

    LOG_INFO << "Response to client: cmd=msg_type_creategroup, data=" << retdata.str() << ", userid=" << m_userinfo.userid;

    //应答客户端，成功加群
    {
        char szSelfData[256] = { 0 };
        snprintf(szSelfData, 256, "{\"userid\": %d, \"type\": 3, \"username\": \"%s\", \"accept\": 1}", groupid, groupname.c_str());
        std::string outbufx;
        BinaryWriteStream writeStream(&outbufx);
        writeStream.WriteInt32(msg_type_operatefriend);
        writeStream.WriteInt32(m_seq);
        writeStream.WriteCString(szSelfData, strlen(szSelfData));
        writeStream.Flush();

        Send(outbufx);
        LOG_INFO << "Response to client: cmd=msg_type_addfriend, data=" << szSelfData << ", userid=" << m_userinfo.userid;
    }
}

void ClientSession::OnGetGroupMembersResponse(const std::string& data, const std::shared_ptr<TcpConnection>& conn)
{
    //{"groupid": 群id}
    Json::Reader JsonReader;
    Json::Value JsonRoot;
    if (!JsonReader.parse(data, JsonRoot))
    {
        LOG_WARN << "invalid json: " << data << ", userid: " << m_userinfo.userid << ", client: " << conn->peerAddress().toIpPort();
        return;
    }

    if (!JsonRoot["groupid"].isInt())
    {
        LOG_WARN << "invalid json: " << data << ", userid: " << m_userinfo.userid << ", client: " << conn->peerAddress().toIpPort();
        return;
    }

    int32_t groupid = JsonRoot["groupid"].asInt();
    
    std::list<User> friends;
    Singleton<UserManager>::Instance().GetFriendInfoByUserId(groupid, friends);
    std::string strUserInfo;
    bool userOnline = false;
    IMServer& imserver = Singleton<IMServer>::Instance();
    for (const auto& iter : friends)
    {
        userOnline = imserver.IsUserSessionExsit(iter.userid);
        /*
        {"code": 0, "msg": "ok", "members":[{"userid": 1,"username":"qqq,
        "nickname":"qqq, "facetype": 0, "customface":"", "gender":0, "birthday":19900101,
        "signature":", "address": "", "phonenumber": "", "mail":", "clienttype": 1, "status":1"]}
        */
        ostringstream osSingleUserInfo;
        osSingleUserInfo << "{\"userid\": " << iter.userid << ",\"username\":\"" << iter.username << "\", \"nickname\":\"" << iter.nickname
            << "\", \"facetype\": " << iter.facetype << ", \"customface\":\"" << iter.customface << "\", \"gender\":" << iter.gender
            << ", \"birthday\":" << iter.birthday << ", \"signature\":\"" << iter.signature << "\", \"address\": \"" << iter.address
            << "\", \"phonenumber\": \"" << iter.phonenumber << "\", \"mail\":\"" << iter.mail << "\", \"clienttype\": 1, \"status\":"
            << (userOnline ? 1 : 0) << "}";

        strUserInfo += osSingleUserInfo.str();
        strUserInfo += ",";
    }
    //去掉最后多余的逗号
    strUserInfo = strUserInfo.substr(0, strUserInfo.length() - 1);
    std::ostringstream os;
    os << "{\"code\": 0, \"msg\": \"ok\", \"groupid\": " << groupid << ", \"members\":[" << strUserInfo << "]}";

    std::string outbuf;
    BinaryWriteStream writeStream(&outbuf);
    writeStream.WriteInt32(msg_type_getgroupmembers);
    writeStream.WriteInt32(m_seq);
    writeStream.WriteString(os.str());
    writeStream.Flush();

    LOG_INFO << "Response to client: cmd=msg_type_getgroupmembers, data=" << os.str() << ", userid=" << m_userinfo.userid;

    Send(outbuf);
}

void ClientSession::SendUserStatusChangeMsg(int32_t userid, int type)
{
    string data; 
    //用户上线
    if (type == 1)
    {
        data = "{\"type\": 1, \"onlinestatus\": 1}";
    }
    //用户下线
    else if (type == 2)
    {
        data = "{\"type\": 2, \"onlinestatus\": 0}";
    }
    //个人昵称、头像、签名等信息更改
    else if (type == 3)
    {
        data = "{\"type\": 3}";
    }

    std::string outbuf;
    BinaryWriteStream writeStream(&outbuf);
    writeStream.WriteInt32(msg_type_userstatuschange);
    writeStream.WriteInt32(m_seq);
    writeStream.WriteString(data);
    writeStream.WriteInt32(userid);
    writeStream.Flush();

    Send(outbuf);

    LOG_INFO << "Send to client: cmd=msg_type_userstatuschange, data=" << data << ", userid=" << m_userinfo.userid;
}

void ClientSession::OnChatResponse(int32_t targetid, const std::string& data, const std::shared_ptr<TcpConnection>& conn)
{
    std::string outbuf;
    BinaryWriteStream writeStream(&outbuf);
    writeStream.WriteInt32(msg_type_chat);
    writeStream.WriteInt32(m_seq);
    writeStream.WriteString(data);
    //消息发送者
    writeStream.WriteInt32(m_userinfo.userid);
    //消息接受者
    writeStream.WriteInt32(targetid);
    writeStream.Flush();

    UserManager& userMgr = Singleton<UserManager>::Instance();
    //写入消息记录
    if (!userMgr.SaveChatMsgToDb(m_userinfo.userid, targetid, data))
    {
        LOG_ERROR << "Write chat msg to db error, , senderid = " << m_userinfo.userid << ", targetid = " << targetid << ", chatmsg:" << data;
    }

    IMServer& imserver = Singleton<IMServer>::Instance();
    MsgCacheManager& msgCacheMgr = Singleton<MsgCacheManager>::Instance();
    //单聊消息
    if (targetid < GROUPID_BOUBDARY)
    {
        //先看目标用户是否在线
        std::shared_ptr<ClientSession> targetSession;
        imserver.GetSessionByUserId(targetSession, targetid);
        //目标用户不在线，缓存这个消息
        if (!targetSession)
        {
            msgCacheMgr.AddChatMsgCache(targetid, outbuf);
            return;
        }

        targetSession->Send(outbuf);
        return;
    }

    //群聊消息
    std::list<User> friends;
    userMgr.GetFriendInfoByUserId(targetid, friends);
    std::string strUserInfo;
    bool userOnline = false;
    for (const auto& iter : friends)
    {
        //排除群成员中的自己
        if (iter.userid == m_userinfo.userid)
            continue;
        
        //先看目标用户是否在线
        std::shared_ptr<ClientSession> targetSession;
        imserver.GetSessionByUserId(targetSession, iter.userid);
        //目标用户不在线，缓存这个消息
        if (!targetSession)
        {
            msgCacheMgr.AddChatMsgCache(iter.userid, outbuf);
           continue;
        }

        targetSession->Send(outbuf);
    }
    
}

void ClientSession::OnMultiChatResponse(const std::string& targets, const std::string& data, const std::shared_ptr<TcpConnection>& conn)
{
    Json::Reader JsonReader;
    Json::Value JsonRoot;
    if (!JsonReader.parse(targets, JsonRoot))
    {
        LOG_ERROR << "invalid json: targets: " << targets  << "data: " << data << ", userid: " << m_userinfo.userid << ", client: " << conn->peerAddress().toIpPort();
        return;
    }

    if (!JsonRoot["targets"].isArray())
    {
        LOG_ERROR << "invalid json: targets: " << targets << "data: " << data << ", userid: " << m_userinfo.userid << ", client: " << conn->peerAddress().toIpPort();
        return;
    }

    for (uint32_t i = 0; i < JsonRoot["targets"].size(); ++i)
    {
        OnChatResponse(JsonRoot["targets"][i].asInt(), data, conn);
    }

    LOG_INFO << "Send to client: cmd=msg_type_multichat, targets: " << targets << "data : " << data << ", userid : " << m_userinfo.userid << ", client : " << conn->peerAddress().toIpPort();
}

void ClientSession::DeleteFriend(const std::shared_ptr<TcpConnection>& conn, int32_t friendid)
{
    int32_t smallerid = friendid;
    int32_t greaterid = m_userinfo.userid;
    if (smallerid > greaterid)
    {
        smallerid = m_userinfo.userid;
        greaterid = friendid;
    }

    if (!Singleton<UserManager>::Instance().ReleaseFriendRelationship(smallerid, greaterid))
    {
        LOG_ERROR << "Delete friend error, friendid: " << friendid << ", userid: " << m_userinfo.userid << ", client: " << conn->peerAddress().toIpPort();
        return;
    }

    User cachedUser;
    if (!Singleton<UserManager>::Instance().GetUserInfoByUserId(friendid, cachedUser))
    {
        LOG_ERROR << "Delete friend - Get user error, friendid: " << friendid << ", userid: " << m_userinfo.userid << ", client: " << conn->peerAddress().toIpPort();
        return;
    }

    char szData[256] = { 0 };
    //发给主动删除的一方
    //{"userid": 9, "type": 1, }        
    snprintf(szData, 256, "{\"userid\":%d, \"type\":5, \"username\": \"%s\"}", friendid, cachedUser.username.c_str());
    std::string outbuf;
    BinaryWriteStream writeStream(&outbuf);
    writeStream.WriteInt32(msg_type_operatefriend);
    writeStream.WriteInt32(m_seq);
    writeStream.WriteCString(szData, strlen(szData));
    writeStream.Flush();

    Send(outbuf);

    LOG_INFO << "Send to client: cmd=msg_type_operatefriend, data=" << szData << ", userid=" << m_userinfo.userid;

    //发给被删除的一方
    //删除好友消息
    if (friendid < GROUPID_BOUBDARY)
    {
        outbuf.clear();
        //先看目标用户是否在线
        std::shared_ptr<ClientSession> targetSession;
        Singleton<IMServer>::Instance().GetSessionByUserId(targetSession, friendid);
        //仅给在线用户推送这个消息
        if (targetSession)
        {
            memset(szData, 0, sizeof(szData));
            snprintf(szData, 256, "{\"userid\":%d, \"type\":5, \"username\": \"%s\"}", m_userinfo.userid, m_userinfo.username.c_str());
            outbuf.clear();
            writeStream.Clear();
            writeStream.WriteInt32(msg_type_operatefriend);
            writeStream.WriteInt32(m_seq);
            writeStream.WriteCString(szData, strlen(szData));
            writeStream.Flush();

            targetSession->Send(outbuf);

            LOG_INFO << "Send to client: cmd=msg_type_operatefriend, data=" << szData << ", userid=" << friendid;
        }

        return;
    }
    
    //退群消息
    //给其他在线群成员推送群信息发生变化的消息
    std::list<User> friends;
    Singleton<UserManager>::Instance().GetFriendInfoByUserId(friendid, friends);
    IMServer& imserver = Singleton<IMServer>::Instance();
    for (const auto& iter : friends)
    {
        //先看目标用户是否在线
        std::shared_ptr<ClientSession> targetSession;
        imserver.GetSessionByUserId(targetSession, iter.userid);
        if (targetSession)
            targetSession->SendUserStatusChangeMsg(friendid, 3);
    }

}