/**
 * 即时通讯的业务逻辑都统一放在这里，BusinessLogic.cpp
 * zhangyl 2018.05.16
 */

#include "BussinessLogic.h"
#include <string>
#include "../net/TcpConnection.h"
#include "IMServer.h"
#include "../jsoncpp-0.5.0/json.h"
#include "../base/Logging.h"
#include "UserManager.h"
#include "../base/Singleton.h"

void BussinessLogic::RegisterUser(const std::string& data, const std::shared_ptr<TcpConnection>& conn, bool keepalive, std::string& retData)
{
    //{ "user": "13917043329", "nickname" : "balloon", "password" : "123" }
    Json::Reader JsonReader;
    Json::Value JsonRoot;
    if (!JsonReader.parse(data, JsonRoot))
    {
        LOG_WARN << "invalid json: " << data << ", client: " << conn->peerAddress().toIpPort();
        return;
    }

    if (!JsonRoot["username"].isString() || !JsonRoot["nickname"].isString() || !JsonRoot["password"].isString())
    {
        LOG_WARN << "invalid json: " << data << ", client: " << conn->peerAddress().toIpPort();
        return;
    }

    User u;
    u.username = JsonRoot["username"].asString();
    u.nickname = JsonRoot["nickname"].asString();
    u.password = JsonRoot["password"].asString();

    //std::string retData;
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
        {
            retData = "{\"code\": 0, \"msg\": \"ok\"}";
        }
    }


    //conn->Send(msg_type_register, m_seq, retData);

    //LOG_INFO << "Response to client: cmd=msg_type_register" << ", userid=" << u.userid << ", data=" << retData;
}