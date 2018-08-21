/**
 *  聊天服务程序入口函数
 *  zhangyl 2017.03.09
 **/
#include <iostream>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <fcntl.h>
#include "../base/Logging.h"
#include "../base/Singleton.h"
#include "../base/ConfigFileReader.h"
#include "../base/AsyncLogging.h"
#include "../net/EventLoop.h"
#include "../net/EventLoopThreadPool.h"
#include "../mysql/MysqlManager.h"
#include "../utils/DaemonRun.h"
#include "UserManager.h"
#include "IMServer.h"
#include "MonitorServer.h"
#include "HttpServer.h"

using namespace net;

EventLoop g_mainLoop;

AsyncLogging g_asyncLog;

void asyncOutput(const char* msg, int len)
{
    g_asyncLog.append(msg, len);
    std::cout << msg << std::endl;
}

void prog_exit(int signo)
{
    std::cout << "program recv signal [" << signo << "] to exit." << std::endl;

    Singleton<EventLoopThreadPool>::Instance().stop();
    g_mainLoop.quit();

    //Logger::setOutput(defaultOutput);
}

int main(int argc, char* argv[])
{
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


    CConfigFileReader config("etc/chatserver.conf");

    Logger::setLogLevel(Logger::INFO);
    const char* logfilepath = config.GetConfigName("logfiledir");
    if (logfilepath == NULL)
    {
        LOG_SYSFATAL << "logdir is not set in config file";
        return 1;
    }
    //如果log目录不存在则创建之
    DIR* dp = opendir(logfilepath);
    if (dp == NULL)
    {
        if (mkdir(logfilepath, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) != 0)
        {
            LOG_SYSFATAL << "create base dir error, " << logfilepath << ", errno: " << errno << ", " << strerror(errno);
            return 1;
        }
    }
    closedir(dp);

    const char* logfilename = config.GetConfigName("logfilename");
    if (logfilename == NULL)
    {
        LOG_SYSFATAL << "logfilename is not set in config file";
        return 1;
    }
    std::string strLogFileFullPath(logfilepath);
    strLogFileFullPath += logfilename;
    Logger::setLogLevel(Logger::DEBUG);
    int kRollSize = 1024 * 1024 * 1024;
    //AsyncLogging log(strLogFileFullPath.c_str(), kRollSize);
    g_asyncLog.setBaseName(strLogFileFullPath.c_str());
    g_asyncLog.setRollSize(kRollSize);
    g_asyncLog.start();
    Logger::setOutput(asyncOutput);

    //初始化数据库配置
    const char* dbserver = config.GetConfigName("dbserver");
    const char* dbuser = config.GetConfigName("dbuser");
    const char* dbpassword = config.GetConfigName("dbpassword");
    const char* dbname = config.GetConfigName("dbname");
    if (!Singleton<CMysqlManager>::Instance().Init(dbserver, dbuser, dbpassword, dbname))
    {
        LOG_FATAL << "Init mysql failed, please check your database config..............";
    }

    if (!Singleton<UserManager>::Instance().Init(dbserver, dbuser, dbpassword, dbname))
    {
        LOG_FATAL << "Init UserManager failed, please check your database config..............";
    }

    Singleton<EventLoopThreadPool>::Instance().Init(&g_mainLoop, 4);
    Singleton<EventLoopThreadPool>::Instance().start();

    const char* listenip = config.GetConfigName("listenip");
    short listenport = (short)atol(config.GetConfigName("listenport"));
    Singleton<IMServer>::Instance().Init(listenip, listenport, &g_mainLoop);

    const char* monitorlistenip = config.GetConfigName("monitorlistenip");
    short monitorlistenport = (short)atol(config.GetConfigName("monitorlistenport"));
    const char* monitortoken = config.GetConfigName("monitortoken");
    Singleton<MonitorServer>::Instance().Init(monitorlistenip, monitorlistenport, &g_mainLoop, monitortoken);

    const char* httplistenip = config.GetConfigName("monitorlistenip");
    short httplistenport = (short)atol(config.GetConfigName("httplistenport"));
    Singleton<HttpServer>::Instance().Init(httplistenip, httplistenport, &g_mainLoop);

    LOG_INFO << "chatserver initialization completed, now you can use client to connect it.";

    g_mainLoop.loop();

    LOG_INFO << "exit chatserver.";

    return 0;
}