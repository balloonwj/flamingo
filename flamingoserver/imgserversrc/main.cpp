/**
 *  聊天图片服务程序入口函数
 *  zhangyl 2017.03.09
 **/
#include <iostream>
#include <stdlib.h>

#include "../base/Platform.h"
#include "../base/Singleton.h"
#include "../base/ConfigFileReader.h"
#include "../base/AsyncLog.h"
#include "../net/EventLoop.h"
#include "../fileserversrc/FileManager.h"
#include "../fileserversrc/FileServer.h"
#include "../fileserversrc/FileMsg.h"
#include "RelayServer.h"

#ifndef WIN32
#include <string.h>
#include "../utils/DaemonRun.h"
#endif 

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

    Singleton<FileServer>::Instance().uninit();
    Singleton<RelayServer>::Instance().uninit();
    g_mainLoop.quit();
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
    CConfigFileReader config("../etc/imgserver.conf");
#else
    CConfigFileReader config("etc/imgserver.conf");
#endif

    std::string logFileFullPath;


    const char* logfilepath = config.getConfigName("logfiledir");
    if (logfilepath == NULL)
    {
        LOGF("logdir is not set in config file");
        return 1;
    }

#ifndef WIN32
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

#endif

    logFileFullPath = logfilepath;

    const char* logfilename = config.getConfigName("logfilename");
    logFileFullPath += logfilename;

#ifdef _DEBUG
    CAsyncLog::init();
#else
    CAsyncLog::init(logFileFullPath.c_str());
#endif

    const char* filecachedir = config.getConfigName("imgcachedir");
    Singleton<FileManager>::Instance().init(filecachedir);

    //上传 / 下载令牌的 HMAC 校验密钥，须与 chatserver.conf 的 filetokenkey 一致
    const char* filetokenkey = config.getConfigName("filetokenkey");

    const char* listenip = config.getConfigName("listenip");
    short listenport = (short)atol(config.getConfigName("listenport"));
    //imgserver 以 file_server_kind_image 启动，只放行头像 / 图片消息场景
    Singleton<FileServer>::Instance().init(listenip, listenport, &g_mainLoop,
        filecachedir, filetokenkey, file_server_kind_image);

    //远程桌面中转：在独立端口起 RelayServer，令牌密钥须与 chatserver.conf 的 relaytokenkey 一致
    const char* relaytokenkey = config.getConfigName("relaytokenkey");
    const char* relaylistenip = config.getConfigName("relaylistenip");
    const char* relaylistenportStr = config.getConfigName("relaylistenport");
    short relaylistenport = (relaylistenportStr != NULL) ? (short)atol(relaylistenportStr) : 0;
    Singleton<RelayServer>::Instance().init(
        relaylistenip != NULL ? relaylistenip : "0.0.0.0",
        relaylistenport, &g_mainLoop,
        relaytokenkey != NULL ? relaytokenkey : "");

    LOGI("imgserver initialization complete, now you can use client to connect it.");

    g_mainLoop.loop();

    LOGI("exit imgserver.");

    return 0;
}
