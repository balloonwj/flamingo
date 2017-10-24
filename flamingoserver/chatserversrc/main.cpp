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
#include "../base/logging.h"
#include "../base/singleton.h"
#include "../base/configfilereader.h"
#include "../base/asynclogging.h"
#include "../net/eventloop.h"
#include "../net/eventloopthreadpool.h"
#include "../mysql/mysqlmanager.h"
#include "UserManager.h"
#include "IMServer.h"

using namespace net;

EventLoop g_mainLoop;

AsyncLogging* g_asyncLog = NULL;
void asyncOutput(const char* msg, int len)
{
    if (g_asyncLog != NULL)
    {
        g_asyncLog->append(msg, len);
        std::cout << msg << std::endl;
    }
}

void prog_exit(int signo)
{
    std::cout << "program recv signal [" << signo << "] to exit." << std::endl;

    g_mainLoop.quit();
}

void daemon_run()
{
    int pid;
    signal(SIGCHLD, SIG_IGN);
    //1）在父进程中，fork返回新创建子进程的进程ID；
    //2）在子进程中，fork返回0；
    //3）如果出现错误，fork返回一个负值；
    pid = fork();
    if (pid < 0)
    {
        std::cout << "fork error" << std::endl;
        exit(-1);
    }
    //父进程退出，子进程独立运行
    else if (pid > 0) {
        exit(0);
    }
    //之前parent和child运行在同一个session里,parent是会话（session）的领头进程,
    //parent进程作为会话的领头进程，如果exit结束执行的话，那么子进程会成为孤儿进程，并被init收养。
    //执行setsid()之后,child将重新获得一个新的会话(session)id。
    //这时parent退出之后,将不会影响到child了。
    setsid();
    int fd;
    fd = open("/dev/null", O_RDWR, 0);
    if (fd != -1)
    {
        dup2(fd, STDIN_FILENO);
        dup2(fd, STDOUT_FILENO);
        dup2(fd, STDERR_FILENO);
    }
    if (fd > 2)
        close(fd);
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


    CConfigFileReader config("chatserver.conf");

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
    int kRollSize = 500 * 1000 * 1000;
    AsyncLogging log(strLogFileFullPath.c_str(), kRollSize);
    log.start();
    g_asyncLog = &log;
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

    LOG_INFO << "chatserver initialization complete.";
    
    g_mainLoop.loop();

    return 0;
}
