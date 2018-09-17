/**
 * ChatSession.cpp
 * zhangyl, 2017.03.10
 **/
#include "ChatSession.h"
#include <string.h>
#include <sstream>
#include <list>
#include "../net/TcpConnection.h"
#include "../net/ProtocolStream.h"
#include "../base/Logging.h"
#include "../base/Singleton.h"
#include "../jsoncpp-0.5.0/json.h"
#include "Msg.h"
#include "UserManager.h"
#include "IMServer.h"
#include "MsgCacheManager.h"
#include "../zlib1.2.11/ZlibUtil.h"
#include "BussinessLogic.h"

//包最大字节数限制为10M
#define MAX_PACKAGE_SIZE    10 * 1024 * 1024

using namespace std;
using namespace net;

//允许的最大时数据包来往间隔，这里设置成30秒
#define MAX_NO_PACKAGE_INTERVAL  30

ChatSession::ChatSession(const std::shared_ptr<TcpConnection>& conn, int sessionid) :
TcpSession(conn), 
m_id(sessionid),
m_seq(0),
m_isLogin(false)
{
	m_userinfo.userid = 0;
    m_lastPackageTime = time(NULL);

    //暂且注释掉，不利于调试
    //EnableHearbeatCheck();
}

ChatSession::~ChatSession()
{
    
}

void ChatSession::OnRead(const std::shared_ptr<TcpConnection>& conn, Buffer* pBuffer, Timestamp receivTime)
{
    while (true)
    {
        //不够一个包头大小
        if (pBuffer->readableBytes() < (size_t)sizeof(msg))
        {
            //LOG_INFO << "buffer is not enough for a package header, pBuffer->readableBytes()=" << pBuffer->readableBytes() << ", sizeof(msg)=" << sizeof(msg);
            return;
        }

        //取包头信息
        msg header;
        memcpy(&header, pBuffer->peek(), sizeof(msg));
        //数据包压缩过
        if (header.compressflag == PACKAGE_COMPRESSED)
        {
            //包头有错误，立即关闭连接
            if (header.compresssize <= 0 || header.compresssize > MAX_PACKAGE_SIZE ||
                header.originsize <= 0 || header.originsize > MAX_PACKAGE_SIZE)
            {
                //客户端发非法数据包，服务器主动关闭之
                LOG_ERROR << "Illegal package, compresssize: " << header.compresssize
                          << ", originsize: " << header.originsize 
                          << ", close TcpConnection, client : " << conn->peerAddress().toIpPort();
                conn->forceClose();
                return;
            }

            //收到的数据不够一个完整的包
            if (pBuffer->readableBytes() < (size_t)header.compresssize + sizeof(msg))
                return;

            pBuffer->retrieve(sizeof(msg));
            std::string inbuf;
            inbuf.append(pBuffer->peek(), header.compresssize);
            pBuffer->retrieve(header.compresssize);
            std::string destbuf;
            if (!ZlibUtil::UncompressBuf(inbuf, destbuf, header.originsize))
            {
                LOG_ERROR << "uncompress error, client: " << conn->peerAddress().toIpPort();
                conn->forceClose();
                return;
            }

            if (!Process(conn, destbuf.c_str(), destbuf.length()))
            {
                //客户端发非法数据包，服务器主动关闭之
                LOG_ERROR << "Process error, close TcpConnection, client: " << conn->peerAddress().toIpPort();
                conn->forceClose();
                return;
            }

            m_lastPackageTime = time(NULL);
        }
        //数据包未压缩
        else
        {
            //包头有错误，立即关闭连接
            if (header.originsize <= 0 || header.originsize > MAX_PACKAGE_SIZE)
            {
                //客户端发非法数据包，服务器主动关闭之
                LOG_ERROR << "Illegal package, originsize: " << header.originsize
                          << ", close TcpConnection, client : " << conn->peerAddress().toIpPort();
                conn->forceClose();
                return;
            }

            //收到的数据不够一个完整的包
            if (pBuffer->readableBytes() < (size_t)header.originsize + sizeof(msg))
                return;

            pBuffer->retrieve(sizeof(msg));
            std::string inbuf;
            inbuf.append(pBuffer->peek(), header.originsize);
            pBuffer->retrieve(header.originsize);
            if (!Process(conn, inbuf.c_str(), inbuf.length()))
            {
                //客户端发非法数据包，服务器主动关闭之
                LOG_ERROR << "Process error, close TcpConnection, client: " << conn->peerAddress().toIpPort();
                conn->forceClose();
                return;
            }
                
            m_lastPackageTime = time(NULL);
        }// end else

    }// end while-loop

}

bool ChatSession::Process(const std::shared_ptr<TcpConnection>& conn, const char* inbuf, size_t buflength)
{   
    BinaryReadStream readStream(inbuf, buflength);
    int32_t cmd;
    if (!readStream.ReadInt32(cmd))
    {
        LOG_WARN << "read cmd error, client: " << conn->peerAddress().toIpPort();
        return false;
    }

    //int seq;
    if (!readStream.ReadInt32(m_seq))
    {
        LOG_ERROR << "read seq error, client: " << conn->peerAddress().toIpPort();
        return false;
    }

    std::string data;
    size_t datalength;
    if (!readStream.ReadString(&data, 0, datalength))
    {
        LOG_ERROR << "read data error, client: " << conn->peerAddress().toIpPort();
        return false;
    }
   
    //心跳包太频繁，不打印
    if (cmd != msg_type_heartbeat)
        LOG_INFO << "Request from client: userid=" << m_userinfo.userid << ", cmd=" << cmd << ", seq=" << m_seq << ", data=" << data << ", datalength=" << datalength << ", buflength=" << buflength;
    //LOG_DEBUG_BIN((unsigned char*)inbuf, length);

    switch (cmd)
    {
        //心跳包
        case msg_type_heartbeat:
            OnHeartbeatResponse(conn);
            break;

        //注册
        case msg_type_register:
            OnRegisterResponse(data, conn);
            break;
        
        //登录
        case msg_type_login:                          
            OnLoginResponse(data, conn);
            break;
        
        //其他命令必须在已经登录的前提下才能进行操作
        default:
        {
            if (m_isLogin)
            {
                switch (cmd)
                {
                    //获取好友列表
                    case msg_type_getofriendlist:
                        OnGetFriendListResponse(conn);
                        break;

                    //查找用户
                    case msg_type_finduser:
                        OnFindUserResponse(data, conn);
                        break;

                    //加好友
                    case msg_type_operatefriend:    
                        OnOperateFriendResponse(data, conn);
                        break;

                    //用户主动更改自己在线状态
                    case msg_type_userstatuschange:
        	            OnChangeUserStatusResponse(data, conn);
                        break;

                    //更新用户信息
                    case msg_type_updateuserinfo:
                        OnUpdateUserInfoResponse(data, conn);
                        break;
        
                    //修改密码
                    case msg_type_modifypassword:
                        OnModifyPasswordResponse(data, conn);
                        break;
        
                    //创建群
                    case msg_type_creategroup:
                        OnCreateGroupResponse(data, conn);
                        break;

                    //获取指定群成员信息
                    case msg_type_getgroupmembers:
                        OnGetGroupMembersResponse(data, conn);
                        break;

                    //聊天消息
                    case msg_type_chat:
                    {
                        int32_t target;
                        if (!readStream.ReadInt32(target))
                        {
                            LOG_ERROR << "read target error, client: " << conn->peerAddress().toIpPort();
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
                            LOG_ERROR << "read targets error, client: " << conn->peerAddress().toIpPort();
                            return false;
                        }

                        OnMultiChatResponse(targets, data, conn);
                    }

                        break;

                    //屏幕截图
                    case msg_type_remotedesktop:
                    {
                        string bmpHeader;
                        size_t bmpHeaderlength;
                        if (!readStream.ReadString(&bmpHeader, 0, bmpHeaderlength))
                        {
                            LOG_ERROR << "read bmpheader error, client: " << conn->peerAddress().toIpPort();
                            return false;
                        }

                        string bmpData;
                        size_t bmpDatalength;
                        if (!readStream.ReadString(&bmpData, 0, bmpDatalength))
                        {
                            LOG_ERROR << "read bmpdata error, client: " << conn->peerAddress().toIpPort();
                            return false;
                        }
                                   
                        int32_t target;
                        if (!readStream.ReadInt32(target))
                        {
                            LOG_ERROR << "read target error, client: " << conn->peerAddress().toIpPort();
                            return false;
                        }
                        OnScreenshotResponse(target, bmpHeader, bmpData, conn);
                    }
                        break;

                    //更新用户好友信息
                    case msg_type_updateteaminfo:
                    {
                        int32_t operationType;
                        if (!readStream.ReadInt32(operationType))
                        {
                            LOG_ERROR << "read operationType error, client: " << conn->peerAddress().toIpPort();
                            return false;
                        }

                        string newTeamName;
                        size_t newTeamNameLength;
                        if (!readStream.ReadString(&newTeamName, 0, newTeamNameLength))
                        {
                            LOG_ERROR << "read newTeamName error, client: " << conn->peerAddress().toIpPort();
                            return false;
                        }

                        string oldTeamName;
                        size_t oldTeamNameLength;
                        if (!readStream.ReadString(&oldTeamName, 0, oldTeamNameLength))
                        {
                            LOG_ERROR << "read newTeamName error, client: " << conn->peerAddress().toIpPort();
                            return false;
                        }
                        
                        OnUpdateTeamInfoResponse(operationType, newTeamName, oldTeamName, conn);
                        break;
                    }
                        
                    //修改好友备注信息
                    case msg_type_modifyfriendmarkname:
                    {
                        int32_t friendid;
                        if (!readStream.ReadInt32(friendid))
                        {
                            LOG_ERROR << "read friendid error, client: " << conn->peerAddress().toIpPort();
                            return false;
                        }

                        string newmarkname;
                        size_t newmarknamelength;
                        if (!readStream.ReadString(&newmarkname, 0, newmarknamelength))
                        {
                            LOG_ERROR << "read newmarkname error, client: " << conn->peerAddress().toIpPort();
                            return false;
                        }

                        OnModifyMarknameResponse(friendid, newmarkname, conn);
                        break;
                    }
                    
                    //移动好友至其他分组
                    case msg_type_movefriendtootherteam:
                    {
                        int32_t friendid;
                        if (!readStream.ReadInt32(friendid))
                        {
                            LOG_ERROR << "read friendid error, client: " << conn->peerAddress().toIpPort();
                            return false;
                        }

                        string newteamname;
                        size_t newteamnamelength;
                        if (!readStream.ReadString(&newteamname, 0, newteamnamelength))
                        {
                            LOG_ERROR << "read newteamname error, client: " << conn->peerAddress().toIpPort();
                            return false;
                        }

                        string oldteamname;
                        size_t oldteamnamelength;
                        if (!readStream.ReadString(&oldteamname, 0, oldteamnamelength))
                        {
                            LOG_ERROR << "read oldteamname error, client: " << conn->peerAddress().toIpPort();
                            return false;
                        }

                        OnMoveFriendToOtherTeamResponse(friendid, newteamname, oldteamname, conn);
                    }
                        break;
#ifdef FXN_VERSION
                    //上传设备信息
                    case msg_type_uploaddeviceinfo:
                    {
                        int32_t deviceid;
                        if (!readStream.ReadInt32(deviceid))
                        {
                            LOG_ERROR << "read deviceid error, client: " << conn->peerAddress().toIpPort();
                            return false;
                        }

                        int32_t classtype;
                        if (!readStream.ReadInt32(classtype))
                        {
                            LOG_ERROR << "read classtype error, client: " << conn->peerAddress().toIpPort();
                            return false;
                        }

                        int64_t uploadtime;
                        if (!readStream.ReadInt64(uploadtime))
                        {
                            LOG_ERROR << "read uploadtime error, client: " << conn->peerAddress().toIpPort();
                            return false;
                        }

                        OnUploadDeviceInfo(deviceid, classtype, uploadtime, data, conn);
                    }
                        break;
#endif                        

                    default:
                        //pBuffer->retrieveAll();
                        LOG_ERROR << "unsupport cmd, cmd:" << cmd << ", data=" << data << ", connection name:" << conn->peerAddress().toIpPort();
                        //conn->forceClose();
                        return false;
                }// end inner-switch
            }
            else
            {
                //用户未登录，告诉客户端不能进行操作提示“未登录”
                std::string data = "{\"code\": 2, \"msg\": \"not login, please login first!\"}";
                Send(cmd, m_seq, data);
                LOG_INFO << "Response to client: cmd=" << cmd << ", data=" << data << ", sessionId=" << m_id;                
            }// end if
         }// end default
    }// end outer-switch

    ++ m_seq;

    return true;
}

void ChatSession::OnHeartbeatResponse(const std::shared_ptr<TcpConnection>& conn)
{
    std::string dummydata;    
    Send(msg_type_heartbeat, m_seq, dummydata);

    //心跳包日志就不要打印了，很容易写满日志
    //LOG_INFO << "Response to client: cmd=1000" << ", sessionId=" << m_id;
}

void ChatSession::OnRegisterResponse(const std::string& data, const std::shared_ptr<TcpConnection>& conn)
{
    string retData;
    BussinessLogic::RegisterUser(data, conn, true, retData);

    if (!retData.empty())
    {
        Send(msg_type_register, m_seq, retData);

        LOG_INFO << "Response to client: cmd=msg_type_register" << ", data=" << retData << conn->peerAddress().toIpPort();;
    }
}

void ChatSession::OnLoginResponse(const std::string& data, const std::shared_ptr<TcpConnection>& conn)
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
            std::shared_ptr<ChatSession> targetSession;
            //由于服务器端支持多类型终端登录，所以只有同一类型的终端且同一客户端类型才认为是同一个session
            imserver.GetSessionByUserIdAndClientType(targetSession, cachedUser.userid, clientType);
            if (targetSession)
            {                              
                string dummydata;
                targetSession->Send(msg_type_kickuser, m_seq, dummydata);
                //被踢下线的Session标记为无效的
                targetSession->MakeSessionInvalid();

                LOG_INFO << "Response to client: userid=" << targetSession->GetUserId() << ", cmd=msg_type_kickuser";

                //关闭连接
                //targetSession->GetConnectionPtr()->forceClose();
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
    Send(msg_type_login, m_seq, os.str());

    LOG_INFO << "Response to client: cmd=msg_type_login, data=" << os.str() << ", userid=" << m_userinfo.userid;

    //设置已经登录的标志
    m_isLogin = true;

    //推送离线通知消息
    std::list<NotifyMsgCache> listNotifyCache;
    Singleton<MsgCacheManager>::Instance().GetNotifyMsgCache(m_userinfo.userid, listNotifyCache);
    for (const auto &iter : listNotifyCache)
    {
        Send(iter.notifymsg);
    }

    //推送离线聊天消息
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
        //因为存在一个用户id，多个终端，所以，同一个userid可能对应多个session
        std::list<std::shared_ptr<ChatSession>> sessions;
        imserver.GetSessionsByUserId(sessions, iter.userid);
        for (auto& iter2 : sessions)
        {
            if (iter2)
            {
                iter2->SendUserStatusChangeMsg(m_userinfo.userid, 1, m_userinfo.status);

                LOG_INFO << "SendUserStatusChangeMsg to user(userid=" << iter2->GetUserId() << "): user go online, online userid = " << m_userinfo.userid << ", status = " << m_userinfo.status;
            }
        }
    }  
}

void ChatSession::OnGetFriendListResponse(const std::shared_ptr<TcpConnection>& conn)
{
    std::string friendlist;
    MakeUpFriendListInfo(friendlist, conn);
    std::ostringstream os;
    os << "{\"code\": 0, \"msg\": \"ok\", \"userinfo\":" << friendlist << "}";
    Send(msg_type_getofriendlist, m_seq, os.str());

    LOG_INFO << "Response to client: userid=" << m_userinfo.userid << ", cmd=msg_type_getofriendlist, data=" << os.str();    
}

void ChatSession::OnChangeUserStatusResponse(const std::string& data, const std::shared_ptr<TcpConnection>& conn)
{
    //{"type": 1, "onlinestatus" : 1}
    Json::Reader JsonReader;
    Json::Value JsonRoot;
    if (!JsonReader.parse(data, JsonRoot))
    {
        LOG_WARN << "invalid json: " << data << ", userid: " << m_userinfo.userid << ", client: " << conn->peerAddress().toIpPort();
        return;
    }

    if (!JsonRoot["type"].isInt() || !JsonRoot["onlinestatus"].isInt())
    {
        LOG_WARN << "invalid json: " << data << ", userid: " << m_userinfo.userid << ", client: " << conn->peerAddress().toIpPort();
        return;
    }

    int newstatus = JsonRoot["onlinestatus"].asInt();
    if (m_userinfo.status == newstatus)
        return;

    //更新下当前用户的状态
    m_userinfo.status = newstatus;

    //TODO: 应答下自己告诉客户端修改成功

    IMServer& imserver = Singleton<IMServer>::Instance();
    std::list<User> friends;
    Singleton<UserManager>::Instance().GetFriendInfoByUserId(m_userinfo.userid, friends);
    for (const auto& iter : friends)
    {
        //因为存在一个用户id，多个终端，所以，同一个userid可能对应多个session
        std::list<std::shared_ptr<ChatSession>> sessions;
        imserver.GetSessionsByUserId(sessions, iter.userid);
        for (auto& iter2 : sessions)
        {
            if (iter2)
                iter2->SendUserStatusChangeMsg(m_userinfo.userid, 1, newstatus);
        }
    }
}

void ChatSession::OnFindUserResponse(const std::string& data, const std::shared_ptr<TcpConnection>& conn)
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

    Send(msg_type_finduser, m_seq, retData);

    LOG_INFO << "Response to client: userid = " << m_userinfo.userid << ", cmd=msg_type_finduser, data=" << retData;
}

void ChatSession::OnOperateFriendResponse(const std::string& data, const std::shared_ptr<TcpConnection>& conn)
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

        if (Singleton<UserManager>::Instance().IsFriend(m_userinfo.userid, targetUserid))
        {
            LOG_ERROR << "In group already, unable to join in group, groupid: " << targetUserid << ", userid: " << m_userinfo.userid << ", client: " << conn->peerAddress().toIpPort();
            //TODO: 通知下客户端
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
        if (Singleton<UserManager>::Instance().IsFriend(m_userinfo.userid, targetUserid))
        {
            LOG_ERROR << "Friendship already, unable to add friend, friendid: " << targetUserid << ", userid: " << m_userinfo.userid << ", client: " << conn->peerAddress().toIpPort();
            //TODO: 通知下客户端
            return;
        }
        
        //{"userid": 9, "type": 1, }        
        snprintf(szData, 256, "{\"userid\":%d, \"type\":2, \"username\": \"%s\"}", m_userinfo.userid, m_userinfo.username.c_str());
    }
    //应答加好友
    else if (type == 3)
    {
        if (!JsonRoot["accept"].isInt())
        {
            LOG_ERROR << "invalid json: " << data << ", userid: " << m_userinfo.userid << "client: " << conn->peerAddress().toIpPort();
            return;
        }

        int accept = JsonRoot["accept"].asInt();
        //接受加好友申请后，建立好友关系
        if (accept == 1)
        {
            if (!Singleton<UserManager>::Instance().MakeFriendRelationshipInDB(targetUserid, m_userinfo.userid))
            {
                LOG_ERROR << "make relationship error: " << data << ", userid: " << m_userinfo.userid << "client: " << conn->peerAddress().toIpPort();
                return;
            }

            if (!Singleton<UserManager>::Instance().UpdateUserRelationshipInMemory(m_userinfo.userid, targetUserid, FRIEND_OPERATION_ADD))
            {
                LOG_ERROR << "UpdateUserTeamInfo error: " << data << ", userid: " << m_userinfo.userid << "client: " << conn->peerAddress().toIpPort();
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
        Send(msg_type_operatefriend, m_seq, szSelfData, strlen(szSelfData));
        LOG_INFO << "Response to client: userid=" << m_userinfo.userid << ", cmd=msg_type_addfriend, data=" << szSelfData;
    }

    //提示对方加好友成功
    std::string outbuf;
    BinaryWriteStream writeStream(&outbuf);
    writeStream.WriteInt32(msg_type_operatefriend);
    writeStream.WriteInt32(m_seq);
    writeStream.WriteCString(szData, strlen(szData));
    writeStream.Flush();

    //先看目标用户是否在线
    std::list<std::shared_ptr<ChatSession>> sessions;
    Singleton<IMServer>::Instance().GetSessionsByUserId(sessions, targetUserid);
    //目标用户不在线，缓存这个消息
    if (sessions.empty())
    {
        Singleton<MsgCacheManager>::Instance().AddNotifyMsgCache(targetUserid, outbuf);
        LOG_INFO << "userid: " << targetUserid << " is not online, cache notify msg, msg: " << outbuf;
        return;
    }

    for (auto& iter : sessions)
    {
        iter->Send(outbuf);
    }

    LOG_INFO << "Response to client: userid = " << targetUserid << ", cmd=msg_type_addfriend, data=" << data;
}

void ChatSession::OnAddGroupResponse(int32_t groupId, const std::shared_ptr<TcpConnection>& conn)
{
    if (!Singleton<UserManager>::Instance().MakeFriendRelationshipInDB(m_userinfo.userid, groupId))
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
    Send(msg_type_operatefriend, m_seq, szSelfData, strlen(szSelfData));
    LOG_INFO << "Response to client: cmd=msg_type_addfriend, data=" << szSelfData << ", userid=" << m_userinfo.userid;

    if (!Singleton<UserManager>::Instance().UpdateUserRelationshipInMemory(m_userinfo.userid, groupId, FRIEND_OPERATION_ADD))
    {
        LOG_ERROR << "UpdateUserTeamInfo error, targetuserid: " << groupId << ", userid: " << m_userinfo.userid << ", client: " << conn->peerAddress().toIpPort();
        return;
    }

    //给其他在线群成员推送群信息发生变化的消息
    std::list<User> friends;
    Singleton<UserManager>::Instance().GetFriendInfoByUserId(groupId, friends);
    IMServer& imserver = Singleton<IMServer>::Instance();
    for (const auto& iter : friends)
    {
        //先看目标用户是否在线
        std::list< std::shared_ptr<ChatSession>> targetSessions;
        imserver.GetSessionsByUserId(targetSessions, iter.userid);
        for (auto& iter2 : targetSessions)
        {
            if (iter2)
                iter2->SendUserStatusChangeMsg(groupId, 3);
        }
    }
}

void ChatSession::OnUpdateUserInfoResponse(const std::string& data, const std::shared_ptr<TcpConnection>& conn)
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
    if (!Singleton<UserManager>::Instance().UpdateUserInfoInDb(m_userinfo.userid, newuserinfo))
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

    //应答客户端
    Send(msg_type_updateuserinfo, m_seq, retdata.str());

    LOG_INFO << "Response to client: userid=" << m_userinfo.userid << ", cmd=msg_type_updateuserinfo, data=" << retdata.str();

    //给其他在线好友推送个人信息发生改变消息
    std::list<User> friends;
    Singleton<UserManager>::Instance().GetFriendInfoByUserId(m_userinfo.userid, friends);
    IMServer& imserver = Singleton<IMServer>::Instance();
    for (const auto& iter : friends)
    {
        //先看目标用户是否在线
        std::list<std::shared_ptr<ChatSession>> targetSessions;
        imserver.GetSessionsByUserId(targetSessions, iter.userid);
        for (auto& iter2 : targetSessions)
        {
            if (iter2)
                iter2->SendUserStatusChangeMsg(m_userinfo.userid, 3);
        }
    }
}

void ChatSession::OnModifyPasswordResponse(const std::string& data, const std::shared_ptr<TcpConnection>& conn)
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

    //应答客户端
    Send(msg_type_modifypassword, m_seq, retdata);

    LOG_INFO << "Response to client: userid=" << m_userinfo.userid << ", cmd=msg_type_modifypassword, data=" << data;
}

void ChatSession::OnCreateGroupResponse(const std::string& data, const std::shared_ptr<TcpConnection>& conn)
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
        LOG_ERROR << "Add group error, data: " << data << ", userid: " << m_userinfo.userid << ", client: " << conn->peerAddress().toIpPort();
        retdata << "{ \"code\": 106, \"msg\" : \"create group error\"}";
    }
    else
    {
        retdata << "{\"code\": 0, \"msg\": \"ok\", \"groupid\":" << groupid << ", \"groupname\": \"" << groupname << "\"}";
    }

    //TODO: 如果步骤1成功了，步骤2失败了怎么办？
    //步骤1
    //创建成功以后该用户自动加群
    if (!Singleton<UserManager>::Instance().MakeFriendRelationshipInDB(m_userinfo.userid, groupid))
    {
        LOG_ERROR << "join in group, errordata: " << data << ", userid: " << m_userinfo.userid << ", client: " << conn->peerAddress().toIpPort();
        return;
    }

    //更新内存中的好友关系
    //步骤2
    if (!Singleton<UserManager>::Instance().UpdateUserRelationshipInMemory(m_userinfo.userid, groupid, FRIEND_OPERATION_ADD))
    {
        LOG_ERROR << "UpdateUserTeamInfo error, data: " << data << ", userid: " << m_userinfo.userid << ", client: " << conn->peerAddress().toIpPort();
        return;
    }
    
    //if (!Singleton<UserManager>::Instance().UpdateUserTeamInfo(groupid, m_userinfo.userid, FRIEND_OPERATION_ADD))
    //{
    //    LOG_ERROR << "UpdateUserTeamInfo error, data: " << data << ", userid: " << m_userinfo.userid << ", client: " << conn->peerAddress().toIpPort();
    //    return;
    //}


    //应答客户端，建群成功
    Send(msg_type_creategroup, m_seq, retdata.str());

    LOG_INFO << "Response to client: userid=" << m_userinfo.userid << ", cmd=msg_type_creategroup, data=" << retdata.str();

    //应答客户端，成功加群
    {
        char szSelfData[256] = { 0 };
        snprintf(szSelfData, 256, "{\"userid\": %d, \"type\": 3, \"username\": \"%s\", \"accept\": 1}", groupid, groupname.c_str());
        Send(msg_type_operatefriend, m_seq, szSelfData, strlen(szSelfData));
        LOG_INFO << "Response to client: userid=" << m_userinfo.userid << ", cmd=msg_type_addfriend, data=" << szSelfData;
    }
}

void ChatSession::OnGetGroupMembersResponse(const std::string& data, const std::shared_ptr<TcpConnection>& conn)
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
    int userOnline = 0;
    IMServer& imserver = Singleton<IMServer>::Instance();
    for (const auto& iter : friends)
    {
        userOnline = imserver.GetUserStatusByUserId(iter.userid);
        /*
        {"code": 0, "msg": "ok", "members":[{"userid": 1,"username":"qqq,
        "nickname":"qqq, "facetype": 0, "customface":"", "gender":0, "birthday":19900101,
        "signature":", "address": "", "phonenumber": "", "mail":", "clienttype": 1, "status":1"]}
        */
        ostringstream osSingleUserInfo;
        osSingleUserInfo << "{\"userid\": " << iter.userid << ", \"username\":\"" << iter.username << "\", \"nickname\":\"" << iter.nickname
            << "\", \"facetype\": " << iter.facetype << ", \"customface\":\"" << iter.customface << "\", \"gender\":" << iter.gender
            << ", \"birthday\":" << iter.birthday << ", \"signature\":\"" << iter.signature << "\", \"address\": \"" << iter.address
            << "\", \"phonenumber\": \"" << iter.phonenumber << "\", \"mail\":\"" << iter.mail << "\", \"clienttype\": 1, \"status\":"
            << userOnline << "}";

        strUserInfo += osSingleUserInfo.str();
        strUserInfo += ",";
    }
    //去掉最后多余的逗号
    strUserInfo = strUserInfo.substr(0, strUserInfo.length() - 1);
    std::ostringstream os;
    os << "{\"code\": 0, \"msg\": \"ok\", \"groupid\": " << groupid << ", \"members\":[" << strUserInfo << "]}";
    Send(msg_type_getgroupmembers, m_seq, os.str());

    LOG_INFO << "Response to client: userid=" << m_userinfo.userid << ", cmd=msg_type_getgroupmembers, data=" << os.str();
}

void ChatSession::SendUserStatusChangeMsg(int32_t userid, int type, int status/* = 0*/)
{
    string data; 
    //用户上线
    if (type == 1)
    {
        int32_t clientType = Singleton<IMServer>::Instance().GetUserClientTypeByUserId(userid);
        char szData[64];
        memset(szData, 0, sizeof(szData));
        sprintf(szData, "{ \"type\": 1, \"onlinestatus\": %d, \"clienttype\": %d}", status, clientType);
        data = szData;
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

    LOG_INFO << "Send to client: userid=" << m_userinfo.userid << ", cmd=msg_type_userstatuschange, data=" << data;
}

void ChatSession::MakeSessionInvalid()
{
    m_userinfo.userid = 0;
}

bool ChatSession::IsSessionValid()
{
    return m_userinfo.userid > 0;
}

void ChatSession::OnChatResponse(int32_t targetid, const std::string& data, const std::shared_ptr<TcpConnection>& conn)
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
        std::list<std::shared_ptr<ChatSession>> targetSessions;
        imserver.GetSessionsByUserId(targetSessions, targetid);
        //目标用户不在线，缓存这个消息
        if (targetSessions.empty())
        {
            msgCacheMgr.AddChatMsgCache(targetid, outbuf);
        }
        else
        {
            for (auto& iter : targetSessions)
            {
                if (iter)
                    iter->Send(outbuf);
            }
        }
    }
    //群聊消息
    else
    {       
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
            std::list<std::shared_ptr<ChatSession>> targetSessions;
            imserver.GetSessionsByUserId(targetSessions, iter.userid);
            //目标用户不在线，缓存这个消息
            if (targetSessions.empty())
            {
                msgCacheMgr.AddChatMsgCache(iter.userid, outbuf);
                continue;
            }
            else
            {
                for (auto& iter2 : targetSessions)
                {
                    if (iter2)
                        iter2->Send(outbuf);
                }
            }
        }
    }
    
}

void ChatSession::OnMultiChatResponse(const std::string& targets, const std::string& data, const std::shared_ptr<TcpConnection>& conn)
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

    LOG_INFO << "Send to client: cmd=msg_type_multichat, targets: " << targets << "data : " << data << ", from userid : " << m_userinfo.userid << ", from client : " << conn->peerAddress().toIpPort();
}

void ChatSession::OnScreenshotResponse(int32_t targetid, const std::string& bmpHeader, const std::string& bmpData, const std::shared_ptr<TcpConnection>& conn)
{
    std::string outbuf;
    BinaryWriteStream writeStream(&outbuf);
    writeStream.WriteInt32(msg_type_remotedesktop);
    writeStream.WriteInt32(m_seq);
    std::string dummy;
    writeStream.WriteString(dummy);
    writeStream.WriteString(bmpHeader);
    writeStream.WriteString(bmpData);
    //消息接受者
    writeStream.WriteInt32(targetid);
    writeStream.Flush();

    IMServer& imserver = Singleton<IMServer>::Instance();
    //单聊消息
    if (targetid >= GROUPID_BOUBDARY)
        return;

    std::list<std::shared_ptr<ChatSession>> targetSessions;
    imserver.GetSessionsByUserId(targetSessions, targetid);
    //先看目标用户在线才转发
    if (!targetSessions.empty())
    {
        for (auto& iter : targetSessions)
        {
            if (iter)
                iter->Send(outbuf);
        }
    }

}

void ChatSession::OnUpdateTeamInfoResponse(int32_t operationType, const std::string& newTeamName, const std::string& oldTeamName, const std::shared_ptr<TcpConnection>& conn)
{
    if (operationType < updateteaminfo_operation_add || operationType > updateteaminfo_operation_modify)
    {
        LOG_ERROR << "invalid teaminfo, userid: " << m_userinfo.userid << ", client: " << conn->peerAddress().toIpPort();
        return;
    }
    
    string teaminfo;
    if (!Singleton<UserManager>::Instance().GetTeamInfoByUserId(m_userinfo.userid, teaminfo))
    {
        LOG_ERROR << "GetTeamInfoByUserId failed, userid: " << m_userinfo.userid << ", client: " << conn->peerAddress().toIpPort();
        //TODO: 应该应答一下客户端
        return;
    }

    if (teaminfo.empty())
    {
        teaminfo = "[{\"teamname\": \"";
        teaminfo += DEFAULT_TEAMNAME;
        teaminfo += "\"}]";
    }

    Json::Reader JsonReader(Json::Features::strictMode());
    Json::Value JsonRoot;
    if (!JsonReader.parse(teaminfo, JsonRoot) || !JsonRoot.isArray())
    {
        LOG_ERROR << "parse teaminfo json failed, userid: " << m_userinfo.userid << ", client: " << conn->peerAddress().toIpPort();
        //TODO: 应该应答一下客户端
        return;
    }

    string newTeamInfo;

    //新增分组
    if (operationType == updateteaminfo_operation_add)
    {
        uint32_t teamCount = JsonRoot.size();
        for (uint32_t i = 0; i < teamCount; ++i)
        {
            if (!JsonRoot[i]["teamname"].isNull() && JsonRoot[i]["teamname"].asString() == newTeamName)
            {
                //TODO: 提示客户端分组已经存在
                LOG_ERROR << "teamname not exist, userid: " << m_userinfo.userid << ", client: " << conn->peerAddress().toIpPort();

                return;
            }
        }
        
        JsonRoot[teamCount]["teamname"] = newTeamName;

        Json::FastWriter writer;
        newTeamInfo = writer.write(JsonRoot);
        
    }
    else if (operationType == updateteaminfo_operation_delete)
    {
        if (oldTeamName == DEFAULT_TEAMNAME)
        {
            //默认分组不允许删除
            //TODO: 提示客户端
            return;
        }
        
        bool found = false;
        uint32_t teamCount = JsonRoot.size();
        for (uint32_t i = 0; i < teamCount; ++i)
        {
            if (!JsonRoot[i]["teamname"].isNull() && JsonRoot[i]["teamname"].asString() == oldTeamName)
            {
                found = true;
                JsonRoot.removeArrayElement(i);

                //将数据库中该组的好友移动至默认分组
                if (!Singleton<UserManager>::Instance().DeleteTeam(m_userinfo.userid, oldTeamName))
                {
                    LOG_ERROR << "Delete team error, oldTeamName=" << oldTeamName << ", userid: " << m_userinfo.userid << ", client : " << conn->peerAddress().toIpPort();
                    return;
                }
                          
                break;
            }
        }

        if (!found)
        {
            //提示客户端分组名不存在
            LOG_ERROR << "teamname not exist, userid: " << m_userinfo.userid << ", client: " << conn->peerAddress().toIpPort();
        }

        Json::FastWriter writer;
        newTeamInfo = writer.write(JsonRoot);
    }
    //修改分组名
    else
    {
        if (oldTeamName == DEFAULT_TEAMNAME)
        {
            //默认分组不允许修改
            //TODO: 提示客户端
            return;
        }
        
        //修改分组名
        bool found = false;
        uint32_t teamCount = JsonRoot.size();
        for (uint32_t i = 0; i < teamCount; ++i)
        {
            if (!JsonRoot[i]["teamname"].isNull() && JsonRoot[i]["teamname"].asString() == oldTeamName)
            {
                found = true;
                JsonRoot[i]["teamname"] = newTeamName;
              
                break;
            }
        }

        if (!found)
        {
            //提示客户端分组名不存在
        }

        if (!Singleton<UserManager>::Instance().ModifyTeamName(m_userinfo.userid, newTeamName, oldTeamName))
        {
            LOG_ERROR << "Update team info failed, userid: " << m_userinfo.userid << ", newTeamInfo: " << newTeamInfo
                      << ", oldTeamInfo: " << oldTeamName << ", client: " << conn->peerAddress().toIpPort();

            return;
        }

        Json::FastWriter writer;
        newTeamInfo = writer.write(JsonRoot);
    }
 
    //保存到数据库里面去（个人信息表）和更新内存中的分组信息
    if (!Singleton<UserManager>::Instance().UpdateUserTeamInfoInDbAndMemory(m_userinfo.userid, newTeamInfo))
    {
        //TODO: 失败应答客户端
        LOG_ERROR << "Update team info failed, userid: " << m_userinfo.userid << ", newTeamInfo: " << newTeamInfo << ", client: " << conn->peerAddress().toIpPort();
        return;
    }

    std::string friendinfo;
    MakeUpFriendListInfo(friendinfo, conn);

    std::ostringstream os;
    os << "{\"code\": 0, \"msg\": \"ok\", \"userinfo\":" << friendinfo << "}";
    Send(msg_type_getofriendlist, m_seq, os.str());

    LOG_INFO << "Response to client: userid=" << m_userinfo.userid << ", cmd=msg_type_getofriendlist, data=" << os.str();
}

void ChatSession::OnModifyMarknameResponse(int32_t friendid, const std::string& newmarkname, const std::shared_ptr<TcpConnection>& conn)
{
    if (!Singleton<UserManager>::Instance().UpdateMarknameInDb(m_userinfo.userid, friendid, newmarkname))
    {
        //TODO: 失败应答客户端
        LOG_ERROR << "Update markname failed, userid: " << m_userinfo.userid
                  << ", friendid:" << friendid << ", client: " << conn->peerAddress().toIpPort();
        return;
    }

    std::string friendinfo;
    MakeUpFriendListInfo(friendinfo, conn);

    std::ostringstream os;
    os << "{\"code\": 0, \"msg\": \"ok\", \"userinfo\":" << friendinfo << "}";
    Send(msg_type_getofriendlist, m_seq, os.str());

    LOG_INFO << "Response to client: userid=" << m_userinfo.userid << ", cmd=msg_type_getofriendlist, data=" << os.str();
}

void ChatSession::OnMoveFriendToOtherTeamResponse(int32_t friendid, const std::string& newteamname, const std::string& oldteamname, const std::shared_ptr<TcpConnection>& conn)
{
    if (newteamname.empty() || oldteamname.empty() || newteamname == oldteamname)
    {
        LOG_ERROR << "Failed to move to other team, newteamname or oldteamname is invalid, userid: " << m_userinfo.userid
                  << ", friendid:" << friendid << ", client: " << conn->peerAddress().toIpPort();

        //TODO: 通知客户端
        return;
    }
    
    //不是你的好友，不能操作
    if (!Singleton<UserManager>::Instance().IsFriend(m_userinfo.userid, friendid))
    {
        LOG_ERROR << "Failed to move to other team, not your friend, userid: " << m_userinfo.userid
                  << ", friendid:" << friendid << ", client: " << conn->peerAddress().toIpPort();
        //TODO: 通知客户端
        return;
    }

    User currentUser;
    if (!Singleton<UserManager>::Instance().GetUserInfoByUserId(m_userinfo.userid, currentUser))
    {
        LOG_ERROR << "User not exist in memory, userid: " << m_userinfo.userid;
        //TODO: 通知客户端
        return;
    }

    string teaminfo = currentUser.teaminfo;
    if (teaminfo.empty())
    {
        teaminfo = "[\"teamname\":\"";
        teaminfo += DEFAULT_TEAMNAME;
        teaminfo += "\"]";
    }

    Json::Reader JsonReader(Json::Features::strictMode());
    Json::Value JsonRoot;
    if (!JsonReader.parse(teaminfo, JsonRoot) || !JsonRoot.isArray())
    {
        LOG_ERROR << "parse teaminfo json failed, userid: " << m_userinfo.userid << ", teaminfo: " << teaminfo << ", client: " << conn->peerAddress().toIpPort();
        return;
    }

    bool foundNewTeam = false;
    bool foundOldTeam = false;
    for (uint32_t i = 0; i < JsonRoot.size(); ++i)
    {
        if (JsonRoot[i]["teamname"].isString())
        {
            if (JsonRoot[i]["teamname"].asString() == newteamname)
            {
                foundNewTeam = true;
                continue;
            }
            else if (JsonRoot[i]["teamname"].asString() == oldteamname)
            {
                foundOldTeam = true;
                continue;
            }
        }
    }

    if (!foundNewTeam || !foundOldTeam)
    {
        LOG_ERROR << "Failed to move to other team, oldTeamName or NewTeamName not exist, userid: " << m_userinfo.userid
                    << ", friendid:" << friendid << ", OldTeamName: "<< oldteamname << ", NewTeamName: " << newteamname
                    << ", client: " << conn->peerAddress().toIpPort();
        
        return;
    }

    if (!Singleton<UserManager>::Instance().MoveFriendToOtherTeam(m_userinfo.userid, friendid, newteamname))
    {
        LOG_ERROR << "Failed to MoveFriendToOtherTeam, db operation error, userid: " << m_userinfo.userid
                  << ", friendid:" << friendid << ", client: " << conn->peerAddress().toIpPort();

        return;
    }
    
    std::string friendinfo;
    MakeUpFriendListInfo(friendinfo, conn);

    std::ostringstream os;
    os << "{\"code\": 0, \"msg\": \"ok\", \"userinfo\":" << friendinfo << "}";
    Send(msg_type_getofriendlist, m_seq, os.str());

    LOG_INFO << "Response to client: userid=" << m_userinfo.userid << ", cmd=msg_type_getofriendlist, data=" << os.str();
}

#ifdef FXN_VERSION
void ChatSession::OnUploadDeviceInfo(int32_t deviceid, int32_t classtype, int64_t uploadtime, const std::string& strDeviceInfo, const std::shared_ptr<TcpConnection>& conn)
{
    if (!Singleton<UserManager>::Instance().InsertDeviceInfo(m_userinfo.userid, deviceid, classtype, uploadtime, strDeviceInfo))
    {
        LOG_ERROR << "InsertDeviceInfo failed, userid: " << m_userinfo.userid << ", client: " << conn->peerAddress().toIpPort();
        return;
    }

    std::string retData = "{ \"code\": 0, \"msg\" : \"ok\" }";
    Send(msg_type_uploaddeviceinfo, m_seq, retData);

    LOG_INFO << "Send to client: userid=" << m_userinfo.userid << ", cmd=msg_type_uploaddeviceinfo, data=" << retData << ", client: " << conn->peerAddress().toIpPort();;
}
#endif

void ChatSession::DeleteFriend(const std::shared_ptr<TcpConnection>& conn, int32_t friendid)
{
    /**
    *  操作好友，包括加好友、删除好友
    **/
    /*
    //type为1发出加好友申请 2 收到加好友请求(仅客户端使用) 3应答加好友 4删除好友请求 5应答删除好友
    //当type=3时，accept是必须字段，0对方拒绝，1对方接受
    cmd = 1005, seq = 0, {"userid": 9, "type": 1}
    cmd = 1005, seq = 0, {"userid": 9, "type": 2, "username": "xxx"}
    cmd = 1005, seq = 0, {"userid": 9, "type": 3, "username": "xxx", "accept": 1}

    //发送
    cmd = 1005, seq = 0, {"userid": 9, "type": 4}
    //应答
    cmd = 1005, seq = 0, {"userid": 9, "type": 5, "username": "xxx"}
    **/

    if (!Singleton<UserManager>::Instance().ReleaseFriendRelationshipInDBAndMemory(friendid, m_userinfo.userid))
    {
        LOG_ERROR << "Delete friend error, friendid: " << friendid << ", userid: " << m_userinfo.userid << ", client: " << conn->peerAddress().toIpPort();
        return;
    }

    //更新一下当前用户的分组信息
    User cachedUser;
    if (!Singleton<UserManager>::Instance().GetUserInfoByUserId(friendid, cachedUser))
    {
        LOG_ERROR << "Delete friend - Get user error, friendid: " << friendid << ", userid: " << m_userinfo.userid << ", client: " << conn->peerAddress().toIpPort();
        return;
    }

    if (!Singleton<UserManager>::Instance().UpdateUserRelationshipInMemory(m_userinfo.userid, friendid, FRIEND_OPERATION_DELETE))
    {
        LOG_ERROR << "UpdateUserTeamInfo failed, friendid: " << friendid << ", userid: " << m_userinfo.userid << ", client: " << conn->peerAddress().toIpPort();
        return;
    }
    
    char szData[256] = { 0 };
    //发给主动删除的一方
    //{"userid": 9, "type": 1, }        
    snprintf(szData, 256, "{\"userid\":%d, \"type\":5, \"username\": \"%s\"}", friendid, cachedUser.username.c_str());
    Send(msg_type_operatefriend, m_seq, szData, strlen(szData));

    LOG_INFO << "Send to client: userid=" << m_userinfo.userid << ", cmd=msg_type_operatefriend, data=" << szData;

    //发给被删除的一方
    //删除好友消息
    if (friendid < GROUPID_BOUBDARY)
    {
        //先看目标用户是否在线
        std::list<std::shared_ptr<ChatSession>>targetSessions;
        Singleton<IMServer>::Instance().GetSessionsByUserId(targetSessions, friendid);
        //仅给在线用户推送这个消息
        if (!targetSessions.empty())
        {
            memset(szData, 0, sizeof(szData));
            snprintf(szData, 256, "{\"userid\":%d, \"type\":5, \"username\": \"%s\"}", m_userinfo.userid, m_userinfo.username.c_str());
            for (auto& iter : targetSessions)
            {
                if (iter)
                    iter->Send(msg_type_operatefriend, m_seq, szData, strlen(szData));
            }

            LOG_INFO << "Send to client: userid=" << friendid << ", cmd=msg_type_operatefriend, data=" << szData;
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
        std::list<std::shared_ptr<ChatSession>> targetSessions;
        imserver.GetSessionsByUserId(targetSessions, iter.userid);
        if (!targetSessions.empty())
        {
            for (auto& iter2 : targetSessions)
            {
                if (iter2)
                    iter2->SendUserStatusChangeMsg(friendid, 3);
            }
        }
    }

}

void ChatSession::MakeUpFriendListInfo(std::string& friendinfo, const std::shared_ptr<TcpConnection>& conn)
{
    std::string teaminfo;
    UserManager& userManager = Singleton<UserManager>::Instance();
    IMServer& imserver = Singleton<IMServer>::Instance();
    userManager.GetTeamInfoByUserId(m_userinfo.userid, teaminfo);

    /*
    [
    {
    "teamindex": 0,
    "teamname": "我的好友",
    "members": [
    {
    "userid": 1,
    
    },
    {
    "userid": 2,
    "markname": "张xx"
    }
    ]
    }
    ]
    */

    string markname = "";
    if (teaminfo.empty())
    {
        teaminfo = "[{\"teamname\": \"";
        teaminfo += DEFAULT_TEAMNAME;
        teaminfo += "\", \"members\": []}]";
    }
           
    Json::Value emptyArrayValue(Json::arrayValue);
    Json::Reader JsonReader(Json::Features::strictMode());
    Json::Value JsonRoot;
    if (!JsonReader.parse(teaminfo, JsonRoot) || !JsonRoot.isArray())
    {
        LOG_ERROR << "parse teaminfo json failed, userid: " << m_userinfo.userid << ", teaminfo: " << teaminfo << ", client: " << conn->peerAddress().toIpPort();
        return;
    }

    // 解析分组信息，添加好友其他信息
    uint32_t teamCount = JsonRoot.size();
    int32_t userid = 0;

    //std::list<User> friends;
    User currentUserInfo;
    userManager.GetUserInfoByUserId(m_userinfo.userid, currentUserInfo);
    User u;
    for (auto& friendinfo : currentUserInfo.friends)
    {
        for (uint32_t i = 0; i < teamCount; ++i)
        {
            if (JsonRoot[i]["members"].isNull() || !(JsonRoot[i]["members"]).isArray())
            {
                JsonRoot[i]["members"] = emptyArrayValue;
            }

            if (JsonRoot[i]["teamname"].isNull() || JsonRoot[i]["teamname"].asString() != friendinfo.teamname)
                continue;
            
            uint32_t memberCount = JsonRoot[i]["members"].size();
                                            
            if (!userManager.GetUserInfoByUserId(friendinfo.friendid, u))
                continue;

            if (!userManager.GetFriendMarknameByUserId(m_userinfo.userid, friendinfo.friendid, markname))
                continue;

            JsonRoot[i]["members"][memberCount]["userid"] = u.userid;
            JsonRoot[i]["members"][memberCount]["username"] = u.username;
            JsonRoot[i]["members"][memberCount]["nickname"] = u.nickname;
            JsonRoot[i]["members"][memberCount]["markname"] = markname;
            JsonRoot[i]["members"][memberCount]["facetype"] = u.facetype;
            JsonRoot[i]["members"][memberCount]["customface"] = u.customface;
            JsonRoot[i]["members"][memberCount]["gender"] = u.gender;
            JsonRoot[i]["members"][memberCount]["birthday"] = u.birthday;
            JsonRoot[i]["members"][memberCount]["signature"] = u.signature;
            JsonRoot[i]["members"][memberCount]["address"] = u.address;
            JsonRoot[i]["members"][memberCount]["phonenumber"] = u.phonenumber;
            JsonRoot[i]["members"][memberCount]["mail"] = u.mail;
            JsonRoot[i]["members"][memberCount]["clienttype"] = imserver.GetUserClientTypeByUserId(friendinfo.friendid);
            JsonRoot[i]["members"][memberCount]["status"] = imserver.GetUserStatusByUserId(friendinfo.friendid);;
       }// end inner for-loop
        
    }// end outer for - loop

    //JsonRoot.toStyledString()返回的是格式化好的json，不实用
    //friendinfo = JsonRoot.toStyledString();
    Json::FastWriter writer;
    friendinfo = writer.write(JsonRoot); 
}

void ChatSession::EnableHearbeatCheck()
{
    std::shared_ptr<TcpConnection> conn = GetConnectionPtr();
    if (conn)
    {
        //每三秒钟检测一下是否有掉线现象
        m_checkOnlineTimerId = conn->getLoop()->runEvery(5, std::bind(&ChatSession::CheckHeartbeat, this, conn));
    }
}

void ChatSession::DisableHeartbeatCheck()
{
    std::shared_ptr<TcpConnection> conn = GetConnectionPtr();
    if (conn)
    {
        LOG_INFO << "remove check online timerId, userid=" << m_userinfo.userid
                 << ", clientType=" << m_userinfo.clienttype
                 << ", client address: " << conn->peerAddress().toIpPort();
        conn->getLoop()->cancel(m_checkOnlineTimerId);
    }
}

void ChatSession::CheckHeartbeat(const std::shared_ptr<TcpConnection>& conn)
{
    if (!conn)
        return;
    
    //LOG_INFO << "check heartbeat, userid=" << m_userinfo.userid
    //        << ", clientType=" << m_userinfo.clienttype
    //        << ", client address: " << conn->peerAddress().toIpPort();

    if (time(NULL) - m_lastPackageTime < MAX_NO_PACKAGE_INTERVAL)
        return;
    
    conn->forceClose();
    LOG_INFO << "in max no-package time, no package, close the connection, userid=" << m_userinfo.userid 
             << ", clientType=" << m_userinfo.clienttype 
             << ", client address: " << conn->peerAddress().toIpPort();
}