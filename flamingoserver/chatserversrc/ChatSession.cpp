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
#include "../base/AsyncLog.h"
#include "../base/Singleton.h"
#include "../jsoncpp1.9.0/json.h"
#include "Msg.h"
#include "UserManager.h"
#include "ChatServer.h"
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

//#ifndef _DEBUG
    //暂且注释掉，不利于调试
    //EnableHearbeatCheck();
//#endif
}

ChatSession::~ChatSession()
{
    std::shared_ptr<TcpConnection> conn = getConnectionPtr();
    if (conn)
    {
        LOGI("remove check online timerId, userid: %d, clientType: %d, client address: %s", m_userinfo.userid, m_userinfo.clienttype, conn->peerAddress().toIpPort().c_str());
        conn->getLoop()->remove(m_checkOnlineTimerId);
    }
}

void ChatSession::onRead(const std::shared_ptr<TcpConnection>& conn, Buffer* pBuffer, Timestamp receivTime)
{
    while (true)
    {
        //不够一个包头大小
        if (pBuffer->readableBytes() < (size_t)sizeof(chat_msg_header))
        {
            //LOGI << "buffer is not enough for a package header, pBuffer->readableBytes()=" << pBuffer->readableBytes() << ", sizeof(msg)=" << sizeof(msg);
            return;
        }

        //取包头信息
        chat_msg_header header;
        memcpy(&header, pBuffer->peek(), sizeof(chat_msg_header));
        //数据包压缩过
        if (header.compressflag == PACKAGE_COMPRESSED)
        {
            //包头有错误，立即关闭连接
            if (header.compresssize <= 0 || header.compresssize > MAX_PACKAGE_SIZE ||
                header.originsize <= 0 || header.originsize > MAX_PACKAGE_SIZE)
            {
                //客户端发非法数据包，服务器主动关闭之
                LOGE("Illegal package, compresssize: %lld, originsize: %lld, close TcpConnection, client: %s",  header.compresssize, header.originsize, conn->peerAddress().toIpPort().c_str());
                conn->forceClose();
                return;
            }

            //收到的数据不够一个完整的包
            if (pBuffer->readableBytes() < (size_t)header.compresssize + sizeof(chat_msg_header))
                return;

            pBuffer->retrieve(sizeof(chat_msg_header));
            std::string inbuf;
            inbuf.append(pBuffer->peek(), header.compresssize);
            pBuffer->retrieve(header.compresssize);
            std::string destbuf;
            if (!ZlibUtil::uncompressBuf(inbuf, destbuf, header.originsize))
            {
                LOGE("uncompress error, client: %s", conn->peerAddress().toIpPort().c_str());
                conn->forceClose();
                return;
            }

            if (!process(conn, destbuf.c_str(), destbuf.length()))
            {
                //客户端发非法数据包，服务器主动关闭之
                LOGE("Process error, close TcpConnection, client: %s", conn->peerAddress().toIpPort().c_str());
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
                LOGE("Illegal package, compresssize: %lld, originsize: %lld, close TcpConnection, client: %s", header.compresssize, header.originsize, conn->peerAddress().toIpPort().c_str());
                conn->forceClose();
                return;
            }

            //收到的数据不够一个完整的包
            if (pBuffer->readableBytes() < (size_t)header.originsize + sizeof(chat_msg_header))
                return;

            pBuffer->retrieve(sizeof(chat_msg_header));
            std::string inbuf;
            inbuf.append(pBuffer->peek(), header.originsize);
            pBuffer->retrieve(header.originsize);
            if (!process(conn, inbuf.c_str(), inbuf.length()))
            {
                //客户端发非法数据包，服务器主动关闭之
                LOGE("Process error, close TcpConnection, client: %s", conn->peerAddress().toIpPort().c_str());
                conn->forceClose();
                return;
            }
                
            m_lastPackageTime = time(NULL);
        }// end else

    }// end while-loop

}

bool ChatSession::process(const std::shared_ptr<TcpConnection>& conn, const char* inbuf, size_t buflength)
{   
    BinaryStreamReader readStream(inbuf, buflength);
    int32_t cmd;
    if (!readStream.ReadInt32(cmd))
    {
        LOGE("read cmd error, client: %s", conn->peerAddress().toIpPort().c_str());
        return false;
    }

    //int seq;
    if (!readStream.ReadInt32(m_seq))
    {
        LOGE("read seq error, client: %s", conn->peerAddress().toIpPort().c_str());
        return false;
    }

    std::string data;
    size_t datalength;
    if (!readStream.ReadString(&data, 0, datalength))
    {
        LOGE("read data error, client: %s", conn->peerAddress().toIpPort().c_str());
        return false;
    }
   
    //心跳包太频繁，不打印
    if (cmd != msg_type_heartbeat)
        LOGI("Request from client: userid=%d, cmd=%d, seq=%d, data=%s, datalength=%d, buflength=%d", m_userinfo.userid, cmd, m_seq, data.c_str(), datalength, buflength);
    
    if (Singleton<ChatServer>::Instance().isLogPackageBinaryEnabled())
    {
        LOGI("body stream, buflength: %d, client: %s", buflength, conn->peerAddress().toIpPort().c_str());
        //LOG_DEBUG_BIN((unsigned char*)inbuf, buflength);
    }
        
    switch (cmd)
    {
        //心跳包
        case msg_type_heartbeat:
            onHeartbeatResponse(conn);
            break;

        //注册
        case msg_type_register:
            onRegisterResponse(data, conn);
            break;
        
        //登录
        case msg_type_login:                          
            onLoginResponse(data, conn);
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
                        onGetFriendListResponse(conn);
                        break;

                    //查找用户
                    case msg_type_finduser:
                        onFindUserResponse(data, conn);
                        break;

                    //加好友
                    case msg_type_operatefriend:    
                        onOperateFriendResponse(data, conn);
                        break;

                    //用户主动更改自己在线状态
                    case msg_type_userstatuschange:
        	            onChangeUserStatusResponse(data, conn);
                        break;

                    //更新用户信息
                    case msg_type_updateuserinfo:
                        onUpdateUserInfoResponse(data, conn);
                        break;
        
                    //修改密码
                    case msg_type_modifypassword:
                        onModifyPasswordResponse(data, conn);
                        break;
        
                    //创建群
                    case msg_type_creategroup:
                        onCreateGroupResponse(data, conn);
                        break;

                    //获取指定群成员信息
                    case msg_type_getgroupmembers:
                        onGetGroupMembersResponse(data, conn);
                        break;

                    //聊天消息
                    case msg_type_chat:
                    {
                        int32_t target;
                        if (!readStream.ReadInt32(target))
                        {
                            LOGE("read target error, client: %s", conn->peerAddress().toIpPort().c_str());
                            return false;
                        }
                        onChatResponse(target, data, conn);
                    }
                        break;
        
                    //群发消息
                    case msg_type_multichat:
                    {
                        std::string targets;
                        size_t targetslength;
                        if (!readStream.ReadString(&targets, 0, targetslength))
                        {
                            LOGE("read targets error, client: %s", conn->peerAddress().toIpPort().c_str());
                            return false;
                        }

                        onMultiChatResponse(targets, data, conn);
                    }

                        break;

                    //屏幕截图
                    case msg_type_remotedesktop:
                    {
                        string bmpHeader;
                        size_t bmpHeaderlength;
                        if (!readStream.ReadString(&bmpHeader, 0, bmpHeaderlength))
                        {
                            LOGE("read bmpheader error, client: %s", conn->peerAddress().toIpPort().c_str());
                            return false;
                        }

                        string bmpData;
                        size_t bmpDatalength;
                        if (!readStream.ReadString(&bmpData, 0, bmpDatalength))
                        {
                            LOGE("read bmpdata error, client: %s", conn->peerAddress().toIpPort().c_str());
                            return false;
                        }
                                   
                        int32_t target;
                        if (!readStream.ReadInt32(target))
                        {
                            LOGE("read target error, client: %s", conn->peerAddress().toIpPort().c_str());
                            return false;
                        }
                        onScreenshotResponse(target, bmpHeader, bmpData, conn);
                    }
                        break;

                    //更新用户好友信息
                    case msg_type_updateteaminfo:
                    {
                        int32_t operationType;
                        if (!readStream.ReadInt32(operationType))
                        {
                            LOGE("read operationType error, client: %s", conn->peerAddress().toIpPort().c_str());
                            return false;
                        }

                        string newTeamName;
                        size_t newTeamNameLength;
                        if (!readStream.ReadString(&newTeamName, 0, newTeamNameLength))
                        {
                            LOGE("read newTeamName error, client: %s", conn->peerAddress().toIpPort().c_str());
                            return false;
                        }

                        string oldTeamName;
                        size_t oldTeamNameLength;
                        if (!readStream.ReadString(&oldTeamName, 0, oldTeamNameLength))
                        {
                            LOGE("read newTeamName error, client: %s", conn->peerAddress().toIpPort().c_str());
                            return false;
                        }
                        
                        onUpdateTeamInfoResponse(operationType, newTeamName, oldTeamName, conn);
                        break;
                    }
                        
                    //修改好友备注信息
                    case msg_type_modifyfriendmarkname:
                    {
                        int32_t friendid;
                        if (!readStream.ReadInt32(friendid))
                        {
                            LOGE("read friendid error, client: %s", conn->peerAddress().toIpPort().c_str());
                            return false;
                        }

                        string newmarkname;
                        size_t newmarknamelength;
                        if (!readStream.ReadString(&newmarkname, 0, newmarknamelength))
                        {
                            LOGE("read newmarkname error, client: %s", conn->peerAddress().toIpPort().c_str());
                            return false;
                        }

                        onModifyMarknameResponse(friendid, newmarkname, conn);
                        break;
                    }
                    
                    //移动好友至其他分组
                    case msg_type_movefriendtootherteam:
                    {
                        int32_t friendid;
                        if (!readStream.ReadInt32(friendid))
                        {
                            LOGE("read friendid error, client: %s", conn->peerAddress().toIpPort().c_str());
                            return false;
                        }

                        string newteamname;
                        size_t newteamnamelength;
                        if (!readStream.ReadString(&newteamname, 0, newteamnamelength))
                        {
                            LOGE("read newteamname error, client: %s", conn->peerAddress().toIpPort().c_str());
                            return false;
                        }

                        string oldteamname;
                        size_t oldteamnamelength;
                        if (!readStream.ReadString(&oldteamname, 0, oldteamnamelength))
                        {
                            LOGE("read oldteamname error, client: %s", conn->peerAddress().toIpPort().c_str());
                            return false;
                        }

                        onMoveFriendToOtherTeamResponse(friendid, newteamname, oldteamname, conn);
                    }
                        break;                      

                    default:
                        //pBuffer->retrieveAll();
                        LOGE("unsupport cmd, cmd: %d, data=%s, connection name:", cmd, data.c_str(), conn->peerAddress().toIpPort().c_str());
                        //conn->forceClose();
                        return false;
                }// end inner-switch
            }
            else
            {
                //用户未登录，告诉客户端不能进行操作提示“未登录”
                std::string data = "{\"code\": 2, \"msg\": \"not login, please login first!\"}";
                send(cmd, m_seq, data);
                LOGI("Response to client: cmd=%d, , data=%s, , sessionId=%d", cmd, data.c_str(), m_id);                
            }// end if
         }// end default
    }// end outer-switch

    ++ m_seq;

    return true;
}

void ChatSession::onHeartbeatResponse(const std::shared_ptr<TcpConnection>& conn)
{
    std::string dummydata;    
    send(msg_type_heartbeat, m_seq, dummydata);

    //心跳包日志就不要打印了，很容易写满日志
    //LOGI << "Response to client: cmd=1000" << ", sessionId=" << m_id;
}

void ChatSession::onRegisterResponse(const std::string& data, const std::shared_ptr<TcpConnection>& conn)
{
    string retData;
    BussinessLogic::registerUser(data, conn, true, retData);

    if (!retData.empty())
    {
        send(msg_type_register, m_seq, retData);

        LOGI("Response to client: cmd=msg_type_register, data: %s. client: %s", retData.c_str(), conn->peerAddress().toIpPort().c_str());
    }
}

void ChatSession::onLoginResponse(const std::string& data, const std::shared_ptr<TcpConnection>& conn)
{
    //{"username": "13917043329", "password": "123", "clienttype": 1, "status": 1}
    Json::CharReaderBuilder b;
    Json::CharReader* reader(b.newCharReader());
    Json::Value jsonRoot;
    JSONCPP_STRING errs;
    bool ok = reader->parse(data.c_str(), data.c_str() + data.length(), &jsonRoot, &errs);
    if (!ok || errs.size() != 0)
    {
        LOGE("onLoginResponse failed, invalid json: %s, sessionId: %d, client: %s", data.c_str(), m_id, conn->peerAddress().toIpPort().c_str());
        delete reader;
        return;
    }
    delete reader;

    if (!jsonRoot["username"].isString() || !jsonRoot["password"].isString() || !jsonRoot["clienttype"].isInt() || !jsonRoot["status"].isInt())
    {
        LOGE("invalid json: %s, sessionId: %d, client: %s", data.c_str(), m_id, conn->peerAddress().toIpPort().c_str());
        return;
    }

    string username = jsonRoot["username"].asString();
    string password = jsonRoot["password"].asString();
    int clientType = jsonRoot["clienttype"].asInt();
    std::ostringstream os;
    User cachedUser;
    cachedUser.userid = 0;
    Singleton<UserManager>::Instance().getUserInfoByUsername(username, cachedUser);
    ChatServer& imserver = Singleton<ChatServer>::Instance();
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
            imserver.getSessionByUserIdAndClientType(targetSession, cachedUser.userid, clientType);
            if (targetSession)
            {                              
                string dummydata;
                targetSession->send(msg_type_kickuser, m_seq, dummydata);
                //被踢下线的Session标记为无效的
                targetSession->makeSessionInvalid();

                LOGI("Response to client, userid: %d, cmd=msg_type_kickuser", targetSession->getUserId());

                //关闭连接
                //targetSession->GetConnectionPtr()->forceClose();
            }           
            
            //记录用户信息
            m_userinfo.userid = cachedUser.userid;
            m_userinfo.username = username;
            m_userinfo.nickname = cachedUser.nickname;
            m_userinfo.password = password;
            m_userinfo.clienttype = jsonRoot["clienttype"].asInt();
            m_userinfo.status = jsonRoot["status"].asInt();

            os << "{\"code\": 0, \"msg\": \"ok\", \"userid\": " << m_userinfo.userid << ",\"username\":\"" << cachedUser.username << "\", \"nickname\":\"" 
               << cachedUser.nickname << "\", \"facetype\": " << cachedUser.facetype << ", \"customface\":\"" << cachedUser.customface << "\", \"gender\":" << cachedUser.gender
               << ", \"birthday\":" << cachedUser.birthday << ", \"signature\":\"" << cachedUser.signature << "\", \"address\": \"" << cachedUser.address
               << "\", \"phonenumber\": \"" << cachedUser.phonenumber << "\", \"mail\":\"" << cachedUser.mail << "\"}";            
        }
    }
   
    //登录信息应答
    send(msg_type_login, m_seq, os.str());

    LOGI("Response to client: cmd=msg_type_login, data=%s, userid=", os.str().c_str(), m_userinfo.userid);

    //设置已经登录的标志
    m_isLogin = true;

    //推送离线通知消息
    std::list<NotifyMsgCache> listNotifyCache;
    Singleton<MsgCacheManager>::Instance().getNotifyMsgCache(m_userinfo.userid, listNotifyCache);
    for (const auto &iter : listNotifyCache)
    {
        send(iter.notifymsg);
    }

    //推送离线聊天消息
    std::list<ChatMsgCache> listChatCache;
    Singleton<MsgCacheManager>::Instance().getChatMsgCache(m_userinfo.userid, listChatCache);
    for (const auto &iter : listChatCache)
    {
        send(iter.chatmsg);
    }

    //给其他用户推送上线消息
    std::list<User> friends;
    Singleton<UserManager>::Instance().getFriendInfoByUserId(m_userinfo.userid, friends);
    for (const auto& iter : friends)
    {
        //因为存在一个用户id，多个终端，所以，同一个userid可能对应多个session
        std::list<std::shared_ptr<ChatSession>> sessions;
        imserver.getSessionsByUserId(sessions, iter.userid);
        for (auto& iter2 : sessions)
        {
            if (iter2)
            {
                iter2->sendUserStatusChangeMsg(m_userinfo.userid, 1, m_userinfo.status);

                LOGI("sendUserStatusChangeMsg to user(userid: %d): user go online, online userid: %d, status: %d", iter2->getUserId(), m_userinfo.userid, m_userinfo.status);
            }
        }
    }  
}

void ChatSession::onGetFriendListResponse(const std::shared_ptr<TcpConnection>& conn)
{
    std::string friendlist;
    makeUpFriendListInfo(friendlist, conn);
    std::ostringstream os;
    os << "{\"code\": 0, \"msg\": \"ok\", \"userinfo\":" << friendlist << "}";
    send(msg_type_getofriendlist, m_seq, os.str());

    LOGI("Response to client: userid: %d, cmd=msg_type_getofriendlist, data: %s", m_userinfo.userid, os.str().c_str());    
}

void ChatSession::onChangeUserStatusResponse(const std::string& data, const std::shared_ptr<TcpConnection>& conn)
{
    //{"type": 1, "onlinestatus" : 1}
    Json::CharReaderBuilder b;
    Json::CharReader* reader(b.newCharReader());
    Json::Value jsonRoot;
    JSONCPP_STRING errs;
    bool ok = reader->parse(data.c_str(), data.c_str() + data.length(), &jsonRoot, &errs);
    if (!ok || errs.size() != 0)
    {
        LOGE("invalid json: %s, userid: %d, client: %s", data.c_str(), m_userinfo.userid, conn->peerAddress().toIpPort().c_str());
        delete reader;
        return;
    }
    delete reader;

    if (!jsonRoot["type"].isInt() || !jsonRoot["onlinestatus"].isInt())
    {
        LOGE("invalid json: %s, userid: %d, client: %s", data.c_str(), m_userinfo.userid, conn->peerAddress().toIpPort().c_str());
        return;
    }

    int newstatus = jsonRoot["onlinestatus"].asInt();
    if (m_userinfo.status == newstatus)
        return;

    //更新下当前用户的状态
    m_userinfo.status = newstatus;

    //TODO: 应答下自己告诉客户端修改成功

    ChatServer& imserver = Singleton<ChatServer>::Instance();
    std::list<User> friends;
    Singleton<UserManager>::Instance().getFriendInfoByUserId(m_userinfo.userid, friends);
    for (const auto& iter : friends)
    {
        //因为存在一个用户id，多个终端，所以，同一个userid可能对应多个session
        std::list<std::shared_ptr<ChatSession>> sessions;
        imserver.getSessionsByUserId(sessions, iter.userid);
        for (auto& iter2 : sessions)
        {
            if (iter2)
                iter2->sendUserStatusChangeMsg(m_userinfo.userid, 1, newstatus);
        }
    }
}

void ChatSession::onFindUserResponse(const std::string& data, const std::shared_ptr<TcpConnection>& conn)
{
    //{ "type": 1, "username" : "zhangyl" }
    Json::CharReaderBuilder b;
    Json::CharReader* reader(b.newCharReader());
    Json::Value jsonRoot;
    JSONCPP_STRING errs;
    bool ok = reader->parse(data.c_str(), data.c_str() + data.length(), &jsonRoot, &errs);
    if (!ok || errs.size() != 0)
    {
        LOGE("invalid json: %s, userid: %d, client: %s", data.c_str(), m_userinfo.userid, conn->peerAddress().toIpPort().c_str());
        delete reader;
        return;
    }
    delete reader;
   
    if (!jsonRoot["type"].isInt() || !jsonRoot["username"].isString())
    {
        LOGE("invalid json: %s, userid: %d, client: %s", data.c_str(), m_userinfo.userid, conn->peerAddress().toIpPort().c_str());       
        return;
    }

    string retData;
    //TODO: 目前只支持查找单个用户
    string username = jsonRoot["username"].asString();
    User cachedUser;
    if (!Singleton<UserManager>::Instance().getUserInfoByUsername(username, cachedUser))
        retData = "{ \"code\": 0, \"msg\": \"ok\", \"userinfo\": [] }";
    else
    {
        //TODO: 用户比较多的时候，应该使用动态string
        char szUserInfo[256] = { 0 };
        snprintf(szUserInfo, 256, "{ \"code\": 0, \"msg\": \"ok\", \"userinfo\": [{\"userid\": %d, \"username\": \"%s\", \"nickname\": \"%s\", \"facetype\":%d}] }", cachedUser.userid, cachedUser.username.c_str(), cachedUser.nickname.c_str(), cachedUser.facetype);
        retData = szUserInfo;
    } 

    send(msg_type_finduser, m_seq, retData);

    LOGI("Response to client: userid: %d, cmd=msg_type_finduser, data: %s", m_userinfo.userid, retData.c_str());
}

void ChatSession::onOperateFriendResponse(const std::string& data, const std::shared_ptr<TcpConnection>& conn)
{
    Json::CharReaderBuilder b;
    Json::CharReader* reader(b.newCharReader());
    Json::Value jsonRoot;
    JSONCPP_STRING errs;
    bool ok = reader->parse(data.c_str(), data.c_str() + data.length(), &jsonRoot, &errs);
    if (!ok || errs.size() != 0)
    {
        LOGE("invalid json: %s, userid: %d, client: %s", data.c_str(), m_userinfo.userid, conn->peerAddress().toIpPort().c_str());
        delete reader;
        return;
    }
    delete reader;

    if (!jsonRoot["type"].isInt() || !jsonRoot["userid"].isInt())
    {
        LOGE("invalid json: %s, userid: %d, client: %s", data.c_str(), m_userinfo.userid, conn->peerAddress().toIpPort().c_str());
        return;
    }

    int type = jsonRoot["type"].asInt();
    int32_t targetUserid = jsonRoot["userid"].asInt();
    if (targetUserid >= GROUPID_BOUBDARY)
    {
        if (type == 4)
        {
            //退群
            deleteFriend(conn, targetUserid);
            return;
        }

        if (Singleton<UserManager>::Instance().isFriend(m_userinfo.userid, targetUserid))
        {            
            LOGE("In group already, unable to join in group, groupid: %d, , userid: %d, , client: %s",  targetUserid, m_userinfo.userid, conn->peerAddress().toIpPort().c_str());
            //TODO: 通知下客户端
            return;
        }

        //加群直接同意
        onAddGroupResponse(targetUserid, conn);
        return;
    }

    char szData[256] = { 0 };
    //删除好友
    if (type == 4)
    {
        deleteFriend(conn, targetUserid);
        return;
    }
    //发出加好友申请
    if (type == 1)
    {
        if (Singleton<UserManager>::Instance().isFriend(m_userinfo.userid, targetUserid))
        {
            LOGE("Friendship already, unable to add friend, friendid: %d, userid: %d, client: %s", targetUserid, m_userinfo.userid, conn->peerAddress().toIpPort().c_str());
            //TODO: 通知下客户端
            return;
        }
        
        //{"userid": 9, "type": 1, }        
        snprintf(szData, 256, "{\"userid\":%d, \"type\":2, \"username\": \"%s\"}", m_userinfo.userid, m_userinfo.username.c_str());
    }
    //应答加好友
    else if (type == 3)
    {
        if (!jsonRoot["accept"].isInt())
        {
            LOGE("invalid json: %s, userid: %d, client: %s", data.c_str(), m_userinfo.userid, conn->peerAddress().toIpPort().c_str());
            return;
        }

        int accept = jsonRoot["accept"].asInt();
        //接受加好友申请后，建立好友关系
        if (accept == 1)
        {
            if (!Singleton<UserManager>::Instance().makeFriendRelationshipInDB(targetUserid, m_userinfo.userid))
            {
                LOGE("make relationship error: %s, userid: %d, client:  %s", data.c_str(), m_userinfo.userid,  conn->peerAddress().toIpPort().c_str());
                return;
            }

            if (!Singleton<UserManager>::Instance().updateUserRelationshipInMemory(m_userinfo.userid, targetUserid, FRIEND_OPERATION_ADD))
            {
                LOGE("UpdateUserTeamInfo error: %s, , userid: %d, client: %s", data.c_str(), m_userinfo.userid, conn->peerAddress().toIpPort().c_str());
                return;
            }
        }

        //{ "userid": 9, "type" : 3, "userid" : 9, "username" : "xxx", "accept" : 1 }
        snprintf(szData, 256, "{\"userid\": %d, \"type\": 3, \"username\": \"%s\", \"accept\": %d}", m_userinfo.userid, m_userinfo.username.c_str(), accept);

        //提示自己当前用户加好友成功
        User targetUser;
        if (!Singleton<UserManager>::Instance().getUserInfoByUserId(targetUserid, targetUser))
        {
            LOGE("Get Userinfo by id error, targetuserid: %d, userid: %d, data: %s, client: %s", targetUserid, m_userinfo.userid, data.c_str(), conn->peerAddress().toIpPort().c_str());
            return;
        }
        char szSelfData[256] = { 0 };
        snprintf(szSelfData, 256, "{\"userid\": %d, \"type\": 3, \"username\": \"%s\", \"accept\": %d}", targetUser.userid, targetUser.username.c_str(), accept);
        send(msg_type_operatefriend, m_seq, szSelfData, strlen(szSelfData));
        LOGI("Response to client: userid: %d, cmd=msg_type_addfriend, data: %s", m_userinfo.userid, szSelfData);
    }

    //提示对方加好友成功
    std::string outbuf;
    BinaryStreamWriter writeStream(&outbuf);
    writeStream.WriteInt32(msg_type_operatefriend);
    writeStream.WriteInt32(m_seq);
    writeStream.WriteCString(szData, strlen(szData));
    writeStream.Flush();

    //先看目标用户是否在线
    std::list<std::shared_ptr<ChatSession>> sessions;
    Singleton<ChatServer>::Instance().getSessionsByUserId(sessions, targetUserid);
    //目标用户不在线，缓存这个消息
    if (sessions.empty())
    {
        Singleton<MsgCacheManager>::Instance().addNotifyMsgCache(targetUserid, outbuf);
        LOGI("userid: %d, is not online, cache notify msg, msg: %s", targetUserid, outbuf.c_str());
        return;
    }

    for (auto& iter : sessions)
    {
        iter->send(outbuf);
    }

    LOGI("Response to client: userid: %d, cmd=msg_type_addfriend, data: %s", targetUserid, data.c_str());
}

void ChatSession::onAddGroupResponse(int32_t groupId, const std::shared_ptr<TcpConnection>& conn)
{
    if (!Singleton<UserManager>::Instance().makeFriendRelationshipInDB(m_userinfo.userid, groupId))
    {
        LOGE("make relationship error, groupId: %d, userid: %d, client: %s", groupId, m_userinfo.userid, conn->peerAddress().toIpPort().c_str());
        return;
    }
    
    User groupUser;
    if (!Singleton<UserManager>::Instance().getUserInfoByUserId(groupId, groupUser))
    {
        LOGE("Get group info by id error, targetuserid: %d, userid: %d, client: %s", groupId, m_userinfo.userid, conn->peerAddress().toIpPort().c_str());
        return;
    }
    char szSelfData[256] = { 0 };
    snprintf(szSelfData, 256, "{\"userid\": %d, \"type\": 3, \"username\": \"%s\", \"accept\": 3}", groupUser.userid, groupUser.username.c_str());
    send(msg_type_operatefriend, m_seq, szSelfData, strlen(szSelfData));
    LOGI("Response to client: cmd=msg_type_addfriend, data: %s, userid: %d", szSelfData, m_userinfo.userid);

    if (!Singleton<UserManager>::Instance().updateUserRelationshipInMemory(m_userinfo.userid, groupId, FRIEND_OPERATION_ADD))
    {
        LOGE("UpdateUserTeamInfo error, targetUserid: %d, userid: %d, client: %s", groupId, m_userinfo.userid, conn->peerAddress().toIpPort().c_str());
        return;
    }

    //给其他在线群成员推送群信息发生变化的消息
    std::list<User> friends;
    Singleton<UserManager>::Instance().getFriendInfoByUserId(groupId, friends);
    ChatServer& imserver = Singleton<ChatServer>::Instance();
    for (const auto& iter : friends)
    {
        //先看目标用户是否在线
        std::list< std::shared_ptr<ChatSession>> targetSessions;
        imserver.getSessionsByUserId(targetSessions, iter.userid);
        for (auto& iter2 : targetSessions)
        {
            if (iter2)
                iter2->sendUserStatusChangeMsg(groupId, 3);
        }
    }
}

void ChatSession::onUpdateUserInfoResponse(const std::string& data, const std::shared_ptr<TcpConnection>& conn)
{
    Json::CharReaderBuilder b;
    Json::CharReader* reader(b.newCharReader());
    Json::Value jsonRoot;
    JSONCPP_STRING errs;
    bool ok = reader->parse(data.c_str(), data.c_str() + data.length(), &jsonRoot, &errs);
    if (!ok || errs.size() != 0)
    {
        LOGE("invalid json: %s, userid: %d, client: %s", data.c_str(), m_userinfo.userid, conn->peerAddress().toIpPort().c_str());
        delete reader;
        return;
    }
    delete reader;
    
    if (!jsonRoot["nickname"].isString() || !jsonRoot["facetype"].isInt() || 
        !jsonRoot["customface"].isString() || !jsonRoot["gender"].isInt() || 
        !jsonRoot["birthday"].isInt() || !jsonRoot["signature"].isString() || 
        !jsonRoot["address"].isString() || !jsonRoot["phonenumber"].isString() || 
        !jsonRoot["mail"].isString())
    {
        LOGE("invalid json: %s, userid: %d, client: %s", data.c_str(), m_userinfo.userid, conn->peerAddress().toIpPort().c_str());
        return;
    }

    User newuserinfo;
    newuserinfo.nickname = jsonRoot["nickname"].asString();
    newuserinfo.facetype = jsonRoot["facetype"].asInt();
    newuserinfo.customface = jsonRoot["customface"].asString();
    newuserinfo.gender = jsonRoot["gender"].asInt();
    newuserinfo.birthday = jsonRoot["birthday"].asInt();
    newuserinfo.signature = jsonRoot["signature"].asString();
    newuserinfo.address = jsonRoot["address"].asString();
    newuserinfo.phonenumber = jsonRoot["phonenumber"].asString();
    newuserinfo.mail = jsonRoot["mail"].asString();
    
    ostringstream retdata;
    ostringstream currentuserinfo;
    if (!Singleton<UserManager>::Instance().updateUserInfoInDb(m_userinfo.userid, newuserinfo))
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
    send(msg_type_updateuserinfo, m_seq, retdata.str());

    LOGI("Response to client: userid: %d, cmd=msg_type_updateuserinfo, data: %s", m_userinfo.userid, retdata.str().c_str());

    //给其他在线好友推送个人信息发生改变消息
    std::list<User> friends;
    Singleton<UserManager>::Instance().getFriendInfoByUserId(m_userinfo.userid, friends);
    ChatServer& imserver = Singleton<ChatServer>::Instance();
    for (const auto& iter : friends)
    {
        //先看目标用户是否在线
        std::list<std::shared_ptr<ChatSession>> targetSessions;
        imserver.getSessionsByUserId(targetSessions, iter.userid);
        for (auto& iter2 : targetSessions)
        {
            if (iter2)
                iter2->sendUserStatusChangeMsg(m_userinfo.userid, 3);
        }
    }
}

void ChatSession::onModifyPasswordResponse(const std::string& data, const std::shared_ptr<TcpConnection>& conn)
{    
    Json::CharReaderBuilder b;
    Json::CharReader* reader(b.newCharReader());
    Json::Value jsonRoot;
    JSONCPP_STRING errs;
    bool ok = reader->parse(data.c_str(), data.c_str() + data.length(), &jsonRoot, &errs);
    if (!ok || errs.size() != 0)
    {
        LOGE("invalid json: %s, userid: %d, client: %s", data.c_str(), m_userinfo.userid, conn->peerAddress().toIpPort().c_str());
        delete reader;
        return;
    }
    delete reader;
    
    if (!jsonRoot["oldpassword"].isString() || !jsonRoot["newpassword"].isString())
    {
        LOGE("invalid json: %s, userid: %d, client: %s", data.c_str(), m_userinfo.userid, conn->peerAddress().toIpPort().c_str());
        return;
    }

    string oldpass = jsonRoot["oldpassword"].asString();
    string newPass = jsonRoot["newpassword"].asString();

    string retdata;
    User cachedUser;
    if (!Singleton<UserManager>::Instance().getUserInfoByUserId(m_userinfo.userid, cachedUser))
    {
        LOGE("get userinfo error, userid: %d, data: %s, client: %s", m_userinfo.userid, data.c_str(), conn->peerAddress().toIpPort().c_str());
        return;
    }

    if (cachedUser.password != oldpass)
    {
        retdata = "{\"code\": 103, \"msg\": \"incorrect old password\"}";
    }
    else
    {       
        if (!Singleton<UserManager>::Instance().modifyUserPassword(m_userinfo.userid, newPass))
        {
            retdata = "{\"code\": 105, \"msg\": \"modify password error\"}";
            LOGE("modify password error, userid: %d, data:%s, client: %s", m_userinfo.userid, data.c_str(), conn->peerAddress().toIpPort().c_str());
        }
        else
            retdata = "{\"code\": 0, \"msg\": \"ok\"}";
    }

    //应答客户端
    send(msg_type_modifypassword, m_seq, retdata);

    LOGI("Response to client: userid: %d, cmd=msg_type_modifypassword, data: %s", m_userinfo.userid, data.c_str());
}

void ChatSession::onCreateGroupResponse(const std::string& data, const std::shared_ptr<TcpConnection>& conn)
{
    Json::CharReaderBuilder b;
    Json::CharReader* reader(b.newCharReader());
    Json::Value jsonRoot;
    JSONCPP_STRING errs;
    bool ok = reader->parse(data.c_str(), data.c_str() + data.length(), &jsonRoot, &errs);
    if (!ok || errs.size() != 0)
    {
        LOGE("invalid json: %s, userid: %d, client: %s", data.c_str(), m_userinfo.userid, conn->peerAddress().toIpPort().c_str());
        delete reader;
        return;
    }
    delete reader;

    if (!jsonRoot["groupname"].isString())
    {
        LOGE("invalid json: %s, userid: %d, client: %s", data.c_str(), m_userinfo.userid, conn->peerAddress().toIpPort().c_str());
        return;
    }

    ostringstream retdata;
    string groupname = jsonRoot["groupname"].asString();
    int32_t groupid;
    if (!Singleton<UserManager>::Instance().addGroup(groupname.c_str(), m_userinfo.userid, groupid))
    {
        LOGE("Add group error, data: %s, userid: %d, client: %s", data.c_str(), m_userinfo.userid, conn->peerAddress().toIpPort().c_str());
        retdata << "{ \"code\": 106, \"msg\" : \"create group error\"}";
    }
    else
    {
        retdata << "{\"code\": 0, \"msg\": \"ok\", \"groupid\":" << groupid << ", \"groupname\": \"" << groupname << "\"}";
    }

    //TODO: 如果步骤1成功了，步骤2失败了怎么办？
    //步骤1
    //创建成功以后该用户自动加群
    if (!Singleton<UserManager>::Instance().makeFriendRelationshipInDB(m_userinfo.userid, groupid))
    {
        LOGE("join in group, errordata: %s, userid: %d, client: %s", data.c_str(), m_userinfo.userid, conn->peerAddress().toIpPort().c_str());
        return;
    }

    //更新内存中的好友关系
    //步骤2
    if (!Singleton<UserManager>::Instance().updateUserRelationshipInMemory(m_userinfo.userid, groupid, FRIEND_OPERATION_ADD))
    {
        LOGE("UpdateUserTeamInfo error, data: %s, userid: %d, client: %s", data.c_str(), m_userinfo.userid, conn->peerAddress().toIpPort().c_str());
        return;
    }
    
    //if (!Singleton<UserManager>::Instance().UpdateUserTeamInfo(groupid, m_userinfo.userid, FRIEND_OPERATION_ADD))
    //{
    //    LOGE << "UpdateUserTeamInfo error, data: " << data << ", userid: " << m_userinfo.userid << ", client: " << conn->peerAddress().toIpPort();
    //    return;
    //}


    //应答客户端，建群成功
    send(msg_type_creategroup, m_seq, retdata.str());

    LOGI("Response to client: userid: %d, cmd=msg_type_creategroup, data: %s", m_userinfo.userid, retdata.str().c_str());

    //应答客户端，成功加群
    {
        char szSelfData[256] = { 0 };
        snprintf(szSelfData, 256, "{\"userid\": %d, \"type\": 3, \"username\": \"%s\", \"accept\": 1}", groupid, groupname.c_str());
        send(msg_type_operatefriend, m_seq, szSelfData, strlen(szSelfData));
        LOGI("Response to client, userid: %d, cmd=msg_type_addfriend, data: %s", m_userinfo.userid, szSelfData);
    }
}

void ChatSession::onGetGroupMembersResponse(const std::string& data, const std::shared_ptr<TcpConnection>& conn)
{
    //{"groupid": 群id}
    Json::CharReaderBuilder b;
    Json::CharReader* reader(b.newCharReader());
    Json::Value jsonRoot;
    JSONCPP_STRING errs;
    bool ok = reader->parse(data.c_str(), data.c_str() + data.length(), &jsonRoot, &errs);
    if (!ok || errs.size() != 0)
    {
        LOGE("invalid json: %s, userid: %d, client: %s", data.c_str(), m_userinfo.userid, conn->peerAddress().toIpPort().c_str());
        delete reader;
        return;
    }
    delete reader;
    
    if (!jsonRoot["groupid"].isInt())
    {
        LOGE("invalid json: %s, userid: %d, client: %s", data.c_str(), m_userinfo.userid, conn->peerAddress().toIpPort().c_str());
        return;
    }

    int32_t groupid = jsonRoot["groupid"].asInt();
    
    std::list<User> friends;
    Singleton<UserManager>::Instance().getFriendInfoByUserId(groupid, friends);
    std::string strUserInfo;
    int useronline = 0;
    ChatServer& imserver = Singleton<ChatServer>::Instance();
    for (const auto& iter : friends)
    {
        useronline = imserver.getUserStatusByUserId(iter.userid);
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
            << useronline << "}";

        strUserInfo += osSingleUserInfo.str();
        strUserInfo += ",";
    }
    //去掉最后多余的逗号
    strUserInfo = strUserInfo.substr(0, strUserInfo.length() - 1);
    std::ostringstream os;
    os << "{\"code\": 0, \"msg\": \"ok\", \"groupid\": " << groupid << ", \"members\":[" << strUserInfo << "]}";
    send(msg_type_getgroupmembers, m_seq, os.str());

    LOGI("Response to client: userid: %d, cmd=msg_type_getgroupmembers, data: %s", m_userinfo.userid, os.str().c_str());
}

void ChatSession::sendUserStatusChangeMsg(int32_t userid, int type, int status/* = 0*/)
{
    string data; 
    //用户上线
    if (type == 1)
    {
        int32_t clientType = Singleton<ChatServer>::Instance().getUserClientTypeByUserId(userid);
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
    BinaryStreamWriter writeStream(&outbuf);
    writeStream.WriteInt32(msg_type_userstatuschange);
    writeStream.WriteInt32(m_seq);
    writeStream.WriteString(data);
    writeStream.WriteInt32(userid);
    writeStream.Flush();

    send(outbuf);

    LOGI("send to client: userid: %d, cmd=msg_type_userstatuschange, data: %s", m_userinfo.userid, data.c_str());
}

void ChatSession::makeSessionInvalid()
{
    m_userinfo.userid = 0;
}

bool ChatSession::isSessionValid()
{
    return m_userinfo.userid > 0;
}

void ChatSession::onChatResponse(int32_t targetid, const std::string& data, const std::shared_ptr<TcpConnection>& conn)
{
    std::string modifiedChatData;
    if (!modifyChatMsgLocalTimeToServerTime(data, modifiedChatData))
    {
        LOGE("invalid chat json, chatjson: %s, senderid: %d, targetid: %d, chatmsg: %s, client: %s", data.c_str(), m_userinfo.userid, targetid, data.c_str(), conn->peerAddress().toIpPort().c_str());
        return;
    }
    
    std::string outbuf;
    BinaryStreamWriter writeStream(&outbuf);
    writeStream.WriteInt32(msg_type_chat);
    writeStream.WriteInt32(m_seq);
    writeStream.WriteString(modifiedChatData);
    //消息发送者
    writeStream.WriteInt32(m_userinfo.userid);
    //消息接受者
    writeStream.WriteInt32(targetid);
    writeStream.Flush();

    UserManager& userMgr = Singleton<UserManager>::Instance();
    //写入消息记录
    if (!userMgr.saveChatMsgToDb(m_userinfo.userid, targetid, data))
    {
        LOGE("Write chat msg to db error, senderid: %d, targetid: %d, chatmsg: %s, client: %s", m_userinfo.userid, targetid, data.c_str(), conn->peerAddress().toIpPort().c_str());
    }

    ChatServer& imserver = Singleton<ChatServer>::Instance();
    MsgCacheManager& msgCacheMgr = Singleton<MsgCacheManager>::Instance();
    //单聊消息
    if (targetid < GROUPID_BOUBDARY)
    {
        //先看目标用户是否在线
        std::list<std::shared_ptr<ChatSession>> targetSessions;
        imserver.getSessionsByUserId(targetSessions, targetid);
        //目标用户不在线，缓存这个消息
        if (targetSessions.empty())
        {
            msgCacheMgr.addChatMsgCache(targetid, outbuf);
        }
        else
        {
            for (auto& iter : targetSessions)
            {
                if (iter)
                    iter->send(outbuf);
            }
        }
    }
    //群聊消息
    else
    {       
        std::list<User> friends;
        userMgr.getFriendInfoByUserId(targetid, friends);
        std::string strUserInfo;
        bool useronline = false;
        for (const auto& iter : friends)
        {
            //排除群成员中的自己
            if (iter.userid == m_userinfo.userid)
                continue;

            //先看目标用户是否在线
            std::list<std::shared_ptr<ChatSession>> targetSessions;
            imserver.getSessionsByUserId(targetSessions, iter.userid);
            //目标用户不在线，缓存这个消息
            if (targetSessions.empty())
            {
                msgCacheMgr.addChatMsgCache(iter.userid, outbuf);
                continue;
            }
            else
            {
                for (auto& iter2 : targetSessions)
                {
                    if (iter2)
                        iter2->send(outbuf);
                }
            }
        }
    }   
}

void ChatSession::onMultiChatResponse(const std::string& targets, const std::string& data, const std::shared_ptr<TcpConnection>& conn)
{
    Json::CharReaderBuilder b;
    Json::CharReader* reader(b.newCharReader());
    Json::Value jsonRoot;
    JSONCPP_STRING errs;
    bool ok = reader->parse(targets.c_str(), targets.c_str() + targets.length(), &jsonRoot, &errs);
    if (!ok || errs.size() != 0)
    {
        LOGE("invalid targets string: targets: %s, data: %s, userid: %d, , client: %s", targets.c_str(), data.c_str(), m_userinfo.userid, conn->peerAddress().toIpPort().c_str());
        delete reader;
        return;
    }
    delete reader;    

    if (!jsonRoot["targets"].isArray())
    {
        LOGE("[targets] node is not array in targets string: targets: %s, data: %s, userid: %d, client: %s", targets.c_str(), data.c_str(), m_userinfo.userid, conn->peerAddress().toIpPort().c_str());
        return;
    }

    for (uint32_t i = 0; i < jsonRoot["targets"].size(); ++i)
    {
        onChatResponse(jsonRoot["targets"][i].asInt(), data, conn);
    }

    LOGI("send to client, cmd=msg_type_multichat, targets: %s, data: %s, from userid: %d, from client: %s", targets.c_str(), data.c_str(), m_userinfo.userid, conn->peerAddress().toIpPort().c_str());
}

void ChatSession::onScreenshotResponse(int32_t targetid, const std::string& bmpHeader, const std::string& bmpData, const std::shared_ptr<TcpConnection>& conn)
{
    std::string outbuf;
    BinaryStreamWriter writeStream(&outbuf);
    writeStream.WriteInt32(msg_type_remotedesktop);
    writeStream.WriteInt32(m_seq);
    std::string dummy;
    writeStream.WriteString(dummy);
    writeStream.WriteString(bmpHeader);
    writeStream.WriteString(bmpData);
    //消息接受者
    writeStream.WriteInt32(targetid);
    writeStream.Flush();

    ChatServer& imserver = Singleton<ChatServer>::Instance();
    //单聊消息
    if (targetid >= GROUPID_BOUBDARY)
        return;

    std::list<std::shared_ptr<ChatSession>> targetSessions;
    imserver.getSessionsByUserId(targetSessions, targetid);
    //先看目标用户在线才转发
    if (!targetSessions.empty())
    {
        for (auto& iter : targetSessions)
        {
            if (iter)
                iter->send(outbuf);
        }
    }
}

void ChatSession::onUpdateTeamInfoResponse(int32_t operationType, const std::string& newTeamName, const std::string& oldTeamName, const std::shared_ptr<TcpConnection>& conn)
{
    if (operationType < updateteaminfo_operation_add || operationType > updateteaminfo_operation_modify)
    {
        LOGE("invalid teaminfo, userid: %d, , client: %s", m_userinfo.userid, conn->peerAddress().toIpPort().c_str());
        return;
    }
    
    string teaminfo;
    if (!Singleton<UserManager>::Instance().getTeamInfoByUserId(m_userinfo.userid, teaminfo))
    {
        LOGE("GetTeamInfoByUserId failed, userid: %d, client: %s", m_userinfo.userid, conn->peerAddress().toIpPort().c_str());
        //TODO: 应该应答一下客户端
        return;
    }

    if (teaminfo.empty())
    {
        teaminfo = "[{\"teamname\": \"";
        teaminfo += DEFAULT_TEAMNAME;
        teaminfo += "\", \"members\":[]}]";
    }

    Json::CharReaderBuilder b;
    Json::CharReader* reader(b.newCharReader());
    Json::Value jsonRoot;
    JSONCPP_STRING errs;
    bool ok = reader->parse(teaminfo.c_str(), teaminfo.c_str() + teaminfo.length(), &jsonRoot, &errs);
    if (!ok || errs.size() != 0)
    {
        //TODO: 应该应答一下客户端
        LOGE("parse teaminfo json failed, userid: %d, client: %s", m_userinfo.userid, conn->peerAddress().toIpPort().c_str());
        delete reader;
        return;
    }
    delete reader;

    string newTeamInfo;

    //新增分组
    if (operationType == updateteaminfo_operation_add)
    {
        uint32_t teamCount = jsonRoot.size();
        for (uint32_t i = 0; i < teamCount; ++i)
        {
            if (!jsonRoot[i]["teamname"].isNull() && jsonRoot[i]["teamname"].asString() == newTeamName)
            {
                //TODO: 提示客户端分组已经存在
                LOGE("teamname not exist, userid: %d, client: %s", m_userinfo.userid, conn->peerAddress().toIpPort().c_str());
                return;
            }
        }
        
        jsonRoot[teamCount]["teamname"] = newTeamName;
        Json::Value emptyArrayValue(Json::arrayValue);
        jsonRoot[teamCount]["members"] = emptyArrayValue;

        //Json::FastWriter writer;
        //newTeamInfo = writer.write(JsonRoot);

        Json::StreamWriterBuilder streamWriterBuilder;
        //消除json中的\t和\n符号
        streamWriterBuilder.settings_["indentation"] = "";
        newTeamInfo = Json::writeString(streamWriterBuilder, jsonRoot);      
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
        uint32_t teamCount = jsonRoot.size();
        for (uint32_t i = 0; i < teamCount; ++i)
        {
            if (!jsonRoot[i]["teamname"].isNull() && jsonRoot[i]["teamname"].asString() == oldTeamName)
            {
                found = true;
                //TODO：可能有问题
                jsonRoot.removeIndex(i, &jsonRoot[i]["teamname"]);

                //将数据库中该组的好友移动至默认分组
                if (!Singleton<UserManager>::Instance().deleteTeam(m_userinfo.userid, oldTeamName))
                {
                    LOGE("Delete team error, oldTeamName: %s, userid: %s, client: %s", oldTeamName.c_str(), m_userinfo.userid, conn->peerAddress().toIpPort().c_str());
                    return;
                }
                          
                break;
            }
        }

        if (!found)
        {
            //提示客户端分组名不存在
            LOGE("teamname not exist, userid: %d, client: %s", m_userinfo.userid, conn->peerAddress().toIpPort().c_str());
        }

        //Json::FastWriter writer;
        //newTeamInfo = writer.write(JsonRoot);

        Json::StreamWriterBuilder streamWriterBuilder;
        //消除json中的\t和\n符号
        streamWriterBuilder.settings_["indentation"] = "";
        newTeamInfo = Json::writeString(streamWriterBuilder, jsonRoot);
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
        uint32_t teamCount = jsonRoot.size();
        for (uint32_t i = 0; i < teamCount; ++i)
        {
            if (!jsonRoot[i]["teamname"].isNull() && jsonRoot[i]["teamname"].asString() == oldTeamName)
            {
                found = true;
                jsonRoot[i]["teamname"] = newTeamName;
              
                break;
            }
        }

        if (!found)
        {
            //提示客户端分组名不存在
        }

        if (!Singleton<UserManager>::Instance().modifyTeamName(m_userinfo.userid, newTeamName, oldTeamName))
        {
            LOGE("Update team info failed, userid: %d, newTeamInfo: %s, oldTeamInfo: %s, client: %s", m_userinfo.userid, newTeamInfo.c_str(), oldTeamName.c_str(), conn->peerAddress().toIpPort().c_str());
            return;
        }

        //Json::FastWriter writer;
        //newTeamInfo = writer.write(JsonRoot);

        Json::StreamWriterBuilder streamWriterBuilder;
        streamWriterBuilder.settings_["indentation"] = "";
        newTeamInfo = Json::writeString(streamWriterBuilder, jsonRoot);
    }
 
    //保存到数据库里面去（个人信息表）和更新内存中的分组信息
    if (!Singleton<UserManager>::Instance().updateUserTeamInfoInDbAndMemory(m_userinfo.userid, newTeamInfo))
    {
        //TODO: 失败应答客户端
        LOGE("Update team info failed, userid: %d, , newTeamInfo: %s, , client: %s", m_userinfo.userid, newTeamInfo.c_str(), conn->peerAddress().toIpPort().c_str());
        return;
    }

    std::string friendinfo;
    makeUpFriendListInfo(friendinfo, conn);

    std::ostringstream os;
    os << "{\"code\": 0, \"msg\": \"ok\", \"userinfo\":" << friendinfo << "}";
    send(msg_type_getofriendlist, m_seq, os.str());

    LOGI("Response to client, userid: %d, cmd=msg_type_getofriendlist, data: %s", m_userinfo.userid, os.str().c_str());
}

void ChatSession::onModifyMarknameResponse(int32_t friendid, const std::string& newmarkname, const std::shared_ptr<TcpConnection>& conn)
{
    if (!Singleton<UserManager>::Instance().updateMarknameInDb(m_userinfo.userid, friendid, newmarkname))
    {
        //TODO: 失败应答客户端
        LOGE("Update markname failed, userid: %d, friendid: %d, client: %s", m_userinfo.userid, friendid, conn->peerAddress().toIpPort().c_str());
        return;
    }

    std::string friendinfo;
    makeUpFriendListInfo(friendinfo, conn);

    std::ostringstream os;
    os << "{\"code\": 0, \"msg\": \"ok\", \"userinfo\":" << friendinfo << "}";
    send(msg_type_getofriendlist, m_seq, os.str());

    LOGI("Response to client, userid: %d, cmd=msg_type_getofriendlist, data: %s", m_userinfo.userid, os.str().c_str());
}

void ChatSession::onMoveFriendToOtherTeamResponse(int32_t friendid, const std::string& newteamname, const std::string& oldteamname, const std::shared_ptr<TcpConnection>& conn)
{
    if (newteamname.empty() || oldteamname.empty() || newteamname == oldteamname)
    {
        LOGE("Failed to move to other team, newteamname or oldteamname is invalid, userid: %d, friendid:%d, client: %s", m_userinfo.userid, friendid, conn->peerAddress().toIpPort().c_str());
        //TODO: 通知客户端
        return;
    }
    
    //不是你的好友，不能操作
    if (!Singleton<UserManager>::Instance().isFriend(m_userinfo.userid, friendid))
    {
        LOGE("Failed to move to other team, not your friend, userid: %d, friendid: %d, client: %s", m_userinfo.userid, friendid, conn->peerAddress().toIpPort().c_str());
        //TODO: 通知客户端
        return;
    }

    User currentUser;
    if (!Singleton<UserManager>::Instance().getUserInfoByUserId(m_userinfo.userid, currentUser))
    {
        LOGE("User not exist in memory, userid: %d", m_userinfo.userid);
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

    Json::CharReaderBuilder b;
    Json::CharReader* reader(b.newCharReader());
    Json::Value jsonRoot;
    JSONCPP_STRING errs;
    bool ok = reader->parse(teaminfo.c_str(), teaminfo.c_str() + teaminfo.length(), &jsonRoot, &errs);
    if (!ok || errs.size() != 0)
    {
        LOGE("parse teaminfo json failed, userid: %d, teaminfo: %s, client: %s", m_userinfo.userid, teaminfo.c_str(), conn->peerAddress().toIpPort().c_str());
        delete reader;
        return;
    }
    delete reader;

    if (!jsonRoot.isArray())
    {
        LOGE("parse teaminfo json failed, userid: %d, teaminfo: %s, client: %s",  m_userinfo.userid, teaminfo.c_str(), conn->peerAddress().toIpPort().c_str());
        return;
    }

    bool foundNewTeam = false;
    bool foundOldTeam = false;
    for (uint32_t i = 0; i < jsonRoot.size(); ++i)
    {
        if (jsonRoot[i]["teamname"].isString())
        {
            if (jsonRoot[i]["teamname"].asString() == newteamname)
            {
                foundNewTeam = true;
                continue;
            }
            else if (jsonRoot[i]["teamname"].asString() == oldteamname)
            {
                foundOldTeam = true;
                continue;
            }
        }
    }

    if (!foundNewTeam || !foundOldTeam)
    {
        LOGE("Failed to move to other team, oldTeamName or newTeamName not exist, userid: %d, friendid: %d, oldTeamName: %s, newTeamName: %s, client: %s", 
            m_userinfo.userid, friendid, oldteamname.c_str(), newteamname.c_str(), conn->peerAddress().toIpPort().c_str());       
        return;
    }

    if (!Singleton<UserManager>::Instance().moveFriendToOtherTeam(m_userinfo.userid, friendid, newteamname))
    {
        LOGE("Failed to MoveFriendToOtherTeam, db operation error, userid: %d, friendid: %d, client: %s", m_userinfo.userid, friendid, conn->peerAddress().toIpPort().c_str());
        return;
    }
    
    std::string friendinfo;
    makeUpFriendListInfo(friendinfo, conn);

    std::ostringstream os;
    os << "{\"code\": 0, \"msg\": \"ok\", \"userinfo\":" << friendinfo << "}";
    send(msg_type_getofriendlist, m_seq, os.str());

    LOGI("Response to client: userid: %d, cmd=msg_type_getofriendlist, data: %s", m_userinfo.userid, os.str().c_str());
}

void ChatSession::deleteFriend(const std::shared_ptr<TcpConnection>& conn, int32_t friendid)
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

    if (!Singleton<UserManager>::Instance().releaseFriendRelationshipInDBAndMemory(friendid, m_userinfo.userid))
    {
        LOGE("Delete friend error, friendid: %d, userid: %d, client: %d", friendid, m_userinfo.userid, conn->peerAddress().toIpPort().c_str());
        return;
    }

    //更新一下当前用户的分组信息
    User cachedUser;
    if (!Singleton<UserManager>::Instance().getUserInfoByUserId(friendid, cachedUser))
    {
        LOGE("Delete friend - Get user error, friendid: %d, userid: %d, client: %s", friendid, m_userinfo.userid, conn->peerAddress().toIpPort().c_str());
        return;
    }

    if (!Singleton<UserManager>::Instance().updateUserRelationshipInMemory(m_userinfo.userid, friendid, FRIEND_OPERATION_DELETE))
    {
        LOGE("UpdateUserTeamInfo failed, friendid: %d, userid: %d, client: %s", friendid, m_userinfo.userid, conn->peerAddress().toIpPort().c_str());
        return;
    }
    
    char szData[256] = { 0 };
    //发给主动删除的一方
    //{"userid": 9, "type": 1, }        
    snprintf(szData, 256, "{\"userid\":%d, \"type\":5, \"username\": \"%s\"}", friendid, cachedUser.username.c_str());
    send(msg_type_operatefriend, m_seq, szData, strlen(szData));

    LOGI("send to client: userid： %d, cmd=msg_type_operatefriend, data: %s", m_userinfo.userid, szData);

    //发给被删除的一方
    //删除好友消息
    if (friendid < GROUPID_BOUBDARY)
    {
        //先看目标用户是否在线
        std::list<std::shared_ptr<ChatSession>>targetSessions;
        Singleton<ChatServer>::Instance().getSessionsByUserId(targetSessions, friendid);
        //仅给在线用户推送这个消息
        if (!targetSessions.empty())
        {
            memset(szData, 0, sizeof(szData));
            snprintf(szData, 256, "{\"userid\":%d, \"type\":5, \"username\": \"%s\"}", m_userinfo.userid, m_userinfo.username.c_str());
            for (auto& iter : targetSessions)
            {
                if (iter)
                    iter->send(msg_type_operatefriend, m_seq, szData, strlen(szData));
            }

            LOGI("send to client: userid: %d, cmd=msg_type_operatefriend, data: %s", friendid, szData);
        }

        return;
    }
    
    //退群消息
    //给其他在线群成员推送群信息发生变化的消息
    std::list<User> friends;
    Singleton<UserManager>::Instance().getFriendInfoByUserId(friendid, friends);
    ChatServer& imserver = Singleton<ChatServer>::Instance();
    for (const auto& iter : friends)
    {
        //先看目标用户是否在线
        std::list<std::shared_ptr<ChatSession>> targetSessions;
        imserver.getSessionsByUserId(targetSessions, iter.userid);
        if (!targetSessions.empty())
        {
            for (auto& iter2 : targetSessions)
            {
                if (iter2)
                    iter2->sendUserStatusChangeMsg(friendid, 3);
            }
        }
    }

}

void ChatSession::makeUpFriendListInfo(std::string& friendinfo, const std::shared_ptr<TcpConnection>& conn)
{
    std::string teaminfo;
    UserManager& userManager = Singleton<UserManager>::Instance();
    ChatServer& imserver = Singleton<ChatServer>::Instance();
    userManager.getTeamInfoByUserId(m_userinfo.userid, teaminfo);

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

    Json::CharReaderBuilder b;
    Json::CharReader* reader(b.newCharReader());
    Json::Value jsonRoot;
    JSONCPP_STRING errs;
    bool ok = reader->parse(teaminfo.c_str(), teaminfo.c_str() + teaminfo.length(), &jsonRoot, &errs);
    if (!ok || errs.size() != 0)
    {
        LOGE("parse teaminfo json failed, userid: %d, teaminfo: %s, client: %s", m_userinfo.userid, teaminfo.c_str(), conn->peerAddress().toIpPort().c_str());
        delete reader;
        return;
    }
    delete reader;

    if (!jsonRoot.isArray())
    {
        LOGE("parse teaminfo json failed, userid: %d, teaminfo: %s, client: %s", m_userinfo.userid, teaminfo.c_str(),  conn->peerAddress().toIpPort().c_str());
        return;
    }

    // 解析分组信息，添加好友其他信息
    uint32_t teamCount = jsonRoot.size();
    int32_t userid = 0;

    //std::list<User> friends;
    User currentUserInfo;
    userManager.getUserInfoByUserId(m_userinfo.userid, currentUserInfo);
    User u;
    for (auto& friendinfo : currentUserInfo.friends)
    {
        for (uint32_t i = 0; i < teamCount; ++i)
        {
            if (jsonRoot[i]["members"].isNull() || !(jsonRoot[i]["members"]).isArray())
            {
                jsonRoot[i]["members"] = emptyArrayValue;
            }

            if (jsonRoot[i]["teamname"].isNull() || jsonRoot[i]["teamname"].asString() != friendinfo.teamname)
                continue;
            
            uint32_t memberCount = jsonRoot[i]["members"].size();
                                            
            if (!userManager.getUserInfoByUserId(friendinfo.friendid, u))
                continue;

            if (!userManager.getFriendMarknameByUserId(m_userinfo.userid, friendinfo.friendid, markname))
                continue;

            jsonRoot[i]["members"][memberCount]["userid"] = u.userid;
            jsonRoot[i]["members"][memberCount]["username"] = u.username;
            jsonRoot[i]["members"][memberCount]["nickname"] = u.nickname;
            jsonRoot[i]["members"][memberCount]["markname"] = markname;
            jsonRoot[i]["members"][memberCount]["facetype"] = u.facetype;
            jsonRoot[i]["members"][memberCount]["customface"] = u.customface;
            jsonRoot[i]["members"][memberCount]["gender"] = u.gender;
            jsonRoot[i]["members"][memberCount]["birthday"] = u.birthday;
            jsonRoot[i]["members"][memberCount]["signature"] = u.signature;
            jsonRoot[i]["members"][memberCount]["address"] = u.address;
            jsonRoot[i]["members"][memberCount]["phonenumber"] = u.phonenumber;
            jsonRoot[i]["members"][memberCount]["mail"] = u.mail;
            jsonRoot[i]["members"][memberCount]["clienttype"] = imserver.getUserClientTypeByUserId(friendinfo.friendid);
            jsonRoot[i]["members"][memberCount]["status"] = imserver.getUserStatusByUserId(friendinfo.friendid);;
       }// end inner for-loop
        
    }// end outer for - loop

    //JsonRoot.toStyledString()返回的是格式化好的json，不实用
    //friendinfo = JsonRoot.toStyledString();
    //Json::FastWriter writer;
    //friendinfo = writer.write(JsonRoot); 

    Json::StreamWriterBuilder streamWriterBuilder;
    streamWriterBuilder.settings_["indentation"] = "";
    friendinfo = Json::writeString(streamWriterBuilder, jsonRoot);
}

bool ChatSession::modifyChatMsgLocalTimeToServerTime(const std::string& chatInputJson, std::string& chatOutputJson)
{
    /*
        消息格式：
        {
            "msgType": 1, //消息类型 0未知类型 1文本 2窗口抖动 3文件
            "time": 2434167,
            "clientType": 0,		//0未知 1pc端 2苹果端 3安卓端
            "font":["fontname", fontSize, fontColor, fontBold, fontItalic, fontUnderline],
            "content":
            [
                {"msgText": "text1"},
                {"msgText": "text2"},
                {"faceID": 101},
                {"faceID": 102},
                {"pic": ["name", "server_path", 400, w, h]},
                {"remotedesktop": 1},
                {"shake": 1},
                {"file":["name", "server_path", 400, onlineflag]}		//onlineflag为0是离线文件，不为0为在线文件
            ]
        }
    */
    if (chatInputJson.empty())
        return false;
    
    Json::CharReaderBuilder b;
    Json::CharReader* reader(b.newCharReader());
    Json::Value jsonRoot;
    JSONCPP_STRING errs;
    bool ok = reader->parse(chatInputJson.c_str(), chatInputJson.c_str() + chatInputJson.length(), &jsonRoot, &errs);
    if (!ok || errs.size() != 0)
    {
        LOGE("parse chatInputJson json failed, userid: %d, chatInputJson: %s", m_userinfo.userid, chatInputJson.c_str());
        delete reader;
        return false;
    }
    delete reader;

    unsigned int now = (unsigned int)time(NULL);
    //if (JsonRoot["time"].isNull())
    jsonRoot["time"] = now;

    //Json::FastWriter writer;
    //chatOutputJson = writer.write(JsonRoot);
    Json::StreamWriterBuilder streamWriterBuilder;
    //消除json中的\t和\n符号
    streamWriterBuilder.settings_["indentation"] = "";
    chatOutputJson = Json::writeString(streamWriterBuilder, jsonRoot);

    return true;
}

void ChatSession::enableHearbeatCheck()
{
    std::shared_ptr<TcpConnection> conn = getConnectionPtr();
    if (conn)
    {        
        //每15秒钟检测一下是否有掉线现象
        m_checkOnlineTimerId = conn->getLoop()->runEvery(15000000, std::bind(&ChatSession::checkHeartbeat, this, conn));
    }
}

void ChatSession::disableHeartbeatCheck()
{
    std::shared_ptr<TcpConnection> conn = getConnectionPtr();
    if (conn)
    {
        LOGI("remove check online timerId, userid: %d, clientType: %d, client address: %s", m_userinfo.userid, m_userinfo.clienttype, conn->peerAddress().toIpPort().c_str());        
        conn->getLoop()->cancel(m_checkOnlineTimerId, true);
    }
}

void ChatSession::checkHeartbeat(const std::shared_ptr<TcpConnection>& conn)
{   
    if (!conn)
        return;
    
    //LOGI("check heartbeat, userid: %d, clientType: %d, client address: %s", m_userinfo.userid, m_userinfo.clienttype, conn->peerAddress().toIpPort().c_str());

    if (time(NULL) - m_lastPackageTime < MAX_NO_PACKAGE_INTERVAL)
        return;
    
    conn->forceClose();
    //LOGI("in max no-package time, no package, close the connection, userid: %d, clientType: %d, client address: %s", m_userinfo.userid, m_userinfo.clienttype, conn->peerAddress().toIpPort().c_str());
}