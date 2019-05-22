/**
 *  聊天服务程序入口函数
 *  zhangyl 2017.03.09
 **/
#include <iostream>
#include <stdlib.h>

#include "../base/Platform.h"
#include "../base/Singleton.h"
#include "../base/ConfigFileReader.h"
#include "../base/AsyncLog.h"
#include "../net/EventLoop.h"
#include "../net/EventLoopThreadPool.h"
#include "../mysqlmgr/MysqlManager.h"

#ifndef WIN32
#include <string.h>
#include "../utils/DaemonRun.h"
#endif 

#include "UserManager.h"
#include "ChatServer.h"
#include "MonitorServer.h"
#include "HttpServer.h"

using namespace net;

#ifdef WIN32
//初始化Windows socket库
NetworkInitializer windowsNetworkInitializer;
#endif

EventLoop g_mainLoop;

#ifndef WIN32
void prog_exit(int signo)
{
    std::cout << "program recv signal [" << signo << "] to exit." << std::endl;

    Singleton<MonitorServer>::Instance().Uninit();
    Singleton<HttpServer>::Instance().Uninit();
    Singleton<ChatServer>::Instance().Uninit();
    g_mainLoop.quit();

    CAsyncLog::Uninit();
}
#endif

int main(int argc, char* argv[])
{
#ifndef WIN32
    //设置信号处理
    signal(SIGCHLD, SIG_DFL);
    signal(SIGPIPE, SIG_IGN);
    signal(SIGINT, prog_exit);
    signal(SIGTERM, prog_exit);


    int ch;
    bool bdaemon = false;
    while ((ch = getopt(argc, argv, "d")) != -1)
    {
        switch (ch)
        {
        case 'd':
            bdaemon = true;
            break;
        }
    }

    if (bdaemon)
        daemon_run();
#endif

#ifdef WIN32
    CConfigFileReader config("../etc/chatserver.conf");
#else
    CConfigFileReader config("etc/chatserver.conf");
#endif

    const char* logbinarypackage = config.GetConfigName("logbinarypackage");
    if (logbinarypackage != NULL)
    {
        int logbinarypackageint = atoi(logbinarypackage);
        if (logbinarypackageint != 0)
            Singleton<ChatServer>::Instance().EnableLogPackageBinary(true);
        else
            Singleton<ChatServer>::Instance().EnableLogPackageBinary(false);
    }
   
    std::string logFileFullPath;

#ifndef WIN32
    const char* logfilepath = config.GetConfigName("logfiledir");
    if (logfilepath == NULL)
    {
        LOGF("logdir is not set in config file");
        return 1;
    }

    //如果log目录不存在则创建之
    DIR* dp = opendir(logfilepath);
    if (dp == NULL)
    {
        if (mkdir(logfilepath, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) != 0)
        {
            LOGF("create base dir error, %s , errno: %d, %s", logfilepath, errno, strerror(errno));
            return 1;
        }
    }
    closedir(dp);

    logFileFullPath = logfilepath;
#endif

    const char* logfilename = config.GetConfigName("logfilename");
    logFileFullPath += logfilename;

#ifdef _DEBUG
    CAsyncLog::Init();
#else
    CAsyncLog::Init(logFileFullPath.c_str());
#endif
    
    
  
    //初始化数据库配置
    const char* dbserver = config.GetConfigName("dbserver");
    const char* dbuser = config.GetConfigName("dbuser");
    const char* dbpassword = config.GetConfigName("dbpassword");
    const char* dbname = config.GetConfigName("dbname");
    if (!Singleton<CMysqlManager>::Instance().Init(dbserver, dbuser, dbpassword, dbname))
    {
        LOGF("Init mysql failed, please check your database config..............");
    }

    if (!Singleton<UserManager>::Instance().Init(dbserver, dbuser, dbpassword, dbname))
    {
        LOGF("Init UserManager failed, please check your database config..............");
    }

    const char* listenip = config.GetConfigName("listenip");
    short listenport = (short)atol(config.GetConfigName("listenport"));
    Singleton<ChatServer>::Instance().Init(listenip, listenport, &g_mainLoop);

    const char* monitorlistenip = config.GetConfigName("monitorlistenip");
    short monitorlistenport = (short)atol(config.GetConfigName("monitorlistenport"));
    const char* monitortoken = config.GetConfigName("monitortoken");
    Singleton<MonitorServer>::Instance().Init(monitorlistenip, monitorlistenport, &g_mainLoop, monitortoken);

    const char* httplistenip = config.GetConfigName("monitorlistenip");
    short httplistenport = (short)atol(config.GetConfigName("httplistenport"));
    Singleton<HttpServer>::Instance().Init(httplistenip, httplistenport, &g_mainLoop);

    LOGI("chatserver initialization completed, now you can use client to connect it.");

    g_mainLoop.loop();

    LOGI("exit chatserver.");

    return 0;
}