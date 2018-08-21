/**
 *  文件服务程序入口函数
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
#include "../utils/DaemonRun.h"
#include "FileManager.h"
#include "FileServer.h"

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

    CConfigFileReader config("etc/fileserver.conf");
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

    const char* filecachedir = config.GetConfigName("filecachedir");
    Singleton<FileManager>::Instance().Init(filecachedir);

    Singleton<EventLoopThreadPool>::Instance().Init(&g_mainLoop, 6);
    Singleton<EventLoopThreadPool>::Instance().start();

    const char* listenip = config.GetConfigName("listenip");
    short listenport = (short)atol(config.GetConfigName("listenport"));
    Singleton<FileServer>::Instance().Init(listenip, listenport, &g_mainLoop, filecachedir);

    LOG_INFO << "fileserver initialization completed, now you can use client to connect it.";
    
    g_mainLoop.loop();

    LOG_INFO << "exit fileserver.";

    return 0;
}
