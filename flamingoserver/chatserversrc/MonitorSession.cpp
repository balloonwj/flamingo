/**
 * 监控会话类, MonitorSession.cpp
 * zhangyl 2017.03.09
 */
#include "MonitorSession.h"
#include <sstream>
#include <string.h>
#include <list>
#include "../net/EventLoopThread.h"
#include "../base/AsyncLog.h"
#include "../base/Singleton.h"
#include "../utils/StringUtil.h"
#include "ChatSession.h"
#include "ChatServer.h"
#include "MonitorServer.h"
#include "UserManager.h"


struct HelpInfo
{
    std::string cmd;
    std::string tip;
};

const HelpInfo g_helpInfo[] = {
    { "help", "show help info" },
    { "ul",   "show online user list" },
    { "su", "show userinfo specified by userid: su [userid]" },
    { "elpb", "enable log package binary data" },
    { "dlpb", "disable log package binary data" }
};

MonitorSession::MonitorSession(std::shared_ptr<TcpConnection>& conn) : m_tmpConn(conn)
{

}

void MonitorSession::OnRead(const std::shared_ptr<TcpConnection>& conn, Buffer* pBuffer, Timestamp receivTime)
{
    std::string buf;
    std::string substr;
    size_t pos;
    size_t totalsize = 0;
    while (true)
    {
        //xxx\nyyy\nuuuu\njjjjj
        buf.clear();
        buf = pBuffer->toStringPiece();
        while (true)
        {
            pos = buf.find("\n");
            if (pos != std::string::npos)
            {
                if (pos == 0)
                    substr = "\n";
                else
                    substr = buf.substr(0, pos);
                totalsize += substr.length();
                buf = buf.substr(pos + 1);
                LOGI("recv cmd: %s", substr.c_str());
                //LOGI << "buf: " << substr;
                Process(conn, substr);
            }
            else
            {
                if (totalsize > 0)
                    pBuffer->retrieve(totalsize);
                return;
            }             
        }// end inner while-loop     
    }// end outer while-loop
}

void MonitorSession::ShowHelp()
{
    std::ostringstream os;
    for (size_t i = 0; i < sizeof(g_helpInfo) / sizeof(g_helpInfo[0]); ++i)
    {
        os << (i + 1) << ". " << g_helpInfo[i].cmd << "-" << g_helpInfo[i].tip << "\n";
    }

    Send(os.str().c_str(), os.str().length());
}


bool MonitorSession::ShowOnlineUserList(const std::string& token/* = ""*/)
{
    std::list<std::shared_ptr<ChatSession>> sessions;
    Singleton<ChatServer>::Instance().GetSessions(sessions);
    std::ostringstream os;
    if (sessions.empty())
    {
        os << "No user online.\n";
    }
    else
    {      
        MonitorServer& monitorServer = Singleton<MonitorServer>::Instance();
        for (const auto& iter : sessions)
        {
            os << "sessionid:" << iter->GetSessionId()
                << ",userid:" << iter->GetUserId()
                << ",username:" << iter->GetUsername();

            //如果输入了token，则显示用户密码，否则不显示
            if (monitorServer.IsMonitorTokenValid(token.c_str()))
            {
                os << ",password:" << iter->GetPassword();
            }   

                os << ",clienttype:" << iter->GetClientType()
                   << ",status:" << iter->GetUserStatus()
                   << ".\n";
        }
    }
    
    Send(os.str().c_str(), os.str().length());
    return false;
}

bool MonitorSession::ShowSpecifiedUserInfoByID(int32_t userid)
{
    UserManager& userMgr = Singleton<UserManager>::Instance();
    User u;
    if (userMgr.GetUserInfoByUserId(userid, u))
    {
        ostringstream os;
        os << "\"address\":\"" << u.address
           << "\", \"birthday\":" << u.birthday
           << ",\"customface\": \"" << u.customface
           << "\", \"facetype\":" << u.facetype
           <<",\"gender\":" << u.gender
           <<",\"mail\":\"" << u.mail
           << "\",\"nickname\":\"" << u.nickname
           << "\",\"phonenumber\":\"" << u.phonenumber
           << "\",\"signature\":\"" << u.signature
           << ",\"userid\":" << u.userid
           << ",\"username\":\"" << u.username << "\""
           << ", teaminfo:" << u.teaminfo
           << "\n";
            
        Send(os.str().c_str(), os.str().length());
    }
    else
    {
        char tip[32] = { "user not found.\n" };
        Send(tip, strlen(tip));
    }

    return true;
}

void MonitorSession::Send(const char* data, size_t length)
{
    if (!m_tmpConn.expired())
    {
        std::shared_ptr<TcpConnection> conn = m_tmpConn.lock();
        conn->send(data, length);
    }
}

bool MonitorSession::Process(const std::shared_ptr<TcpConnection>& conn, const std::string& inbuf)
{
    if (inbuf == "\n")
        return false;

    std::vector<std::string> v;
    StringUtil::Split(inbuf, v, " ");

    if (v.empty())
        return false;
    else
    {
        if (v[0] == g_helpInfo[0].cmd)
            ShowHelp();
        else if (v[0] == g_helpInfo[1].cmd)
        {
            if (v.size() >= 2)
                ShowOnlineUserList(v[1]);
            else
                ShowOnlineUserList("");
        }
        else if (v[0] == g_helpInfo[2].cmd)
        {
            if (v.size() < 2)
            {
                char tip[32] = { "please specify userid.\n" };
                Send(tip, strlen(tip));
            }
            else
            {
                ShowSpecifiedUserInfoByID(atoi(v[1].c_str()));
            }
                
        }
        else if (v[0] == g_helpInfo[3].cmd)
        {
            //开启日志数据包打印二进制字节
            Singleton<ChatServer>::Instance().EnableLogPackageBinary(true);

            char tip[32] = { "OK.\n" };
            Send(tip, strlen(tip));
        }
        else if (v[0] == g_helpInfo[4].cmd)
        {
            //开启日志数据包打印二进制字节
            Singleton<ChatServer>::Instance().EnableLogPackageBinary(false);

            char tip[32] = { "OK.\n" };
            Send(tip, strlen(tip));
        }
        else
        {
            char tip[32] = { "cmd not support\n" };
            Send(tip, strlen(tip));
        }
    }

    return true;
}