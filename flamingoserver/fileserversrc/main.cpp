/**
 *  文件服务程序入口函数
 *  zhangyl 2017.03.09
 **/
#include <iostream>
#include <stdlib.h>

#include "../base/Platform.h"
#include "../base/Singleton.h"
#include "../base/ConfigFileReader.h"
#include "../base/AsyncLog.h"
#include "../net/EventLoop.h"
#include "FileManager.h"

#ifndef WIN32
#include <string.h>
#include "../utils/DaemonRun.h"
#endif 

#include "FileServer.h"
#include "FileMsg.h"
#include "UpdatePackageIndex.h"
#include "../utils/AppPath.h"
#include "../utils/ServerConfigLoader.h"

using namespace net;

//本服务的名字与配置文件名。名字只出现在启动失败的提示里，让用户一眼看出是哪个进程报的错；
//配置文件名交给 ServerConfigLoader 去各个候选目录里找，不再写死一条相对路径
#define SERVER_NAME              "fileserver"
#define SERVER_CONFIG_FILE_NAME  "fileserver.conf"

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
    g_mainLoop.quit();

    //必须停掉异步日志：它的写日志线程是 std::thread，进程退出时静态成员析构，
    //析构一个仍然 joinable 的 std::thread 会直接 std::terminate 掉整个进程，
    //表现为服务收到 SIGTERM 后 core dump。chatserver 一直有这一行，这里补齐
    CAsyncLog::uninit();
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

    //配置文件按「可执行文件所在目录优先」的顺序查找，理由与 chatserver 相同，
    //详见 utils/ServerConfigLoader.h
    CConfigFileReader config;
    std::string triedConfigPaths;
    if (!serverconfig::LoadServerConfig(config, SERVER_CONFIG_FILE_NAME, triedConfigPaths))
    {
        serverconfig::ReportStartupFailure(SERVER_NAME,
            std::string("cannot open config file ") + SERVER_CONFIG_FILE_NAME
            + ", tried: " + triedConfigPaths);
        return 1;
    }

    //必需配置项先一次性取齐、再统一判断，缺哪几项一次报全
    serverconfig::CRequiredConfig required(config);
    std::string logfiledir   = required.getString("logfiledir");
    std::string logfilename  = required.getString("logfilename");
    std::string filecachedir = required.getString("filecachedir");
    std::string filetokenkey = required.getString("filetokenkey");
    std::string listenip     = required.getString("listenip");
    int         listenport   = required.getInt("listenport");
    if (required.hasMissing())
    {
        serverconfig::ReportStartupFailure(SERVER_NAME,
            std::string("missing required config item(s): ") + required.missingKeys()
            + ", config file: " + config.getFilePath());
        return 1;
    }

    //日志目录的解析与创建规则同 chatserver，见 chatserversrc/main.cpp 的说明
    std::string resolvedLogDir = serverconfig::StripTrailingSeparators(
                                     apppath::ResolveAgainstDirectory(
                                         apppath::GetExecutableDirectory(), logfiledir));
    std::string logDirError;
    if (!serverconfig::EnsureDirectoryExists(resolvedLogDir, logDirError))
    {
        serverconfig::ReportStartupFailure(SERVER_NAME,
            std::string("cannot create log dir ") + resolvedLogDir + ", " + logDirError);
        return 1;
    }

    std::string logFileFullPath = apppath::ResolveAgainstDirectory(resolvedLogDir, logfilename);

    CAsyncLog::init(logFileFullPath.c_str());

    Singleton<FileManager>::Instance().init(filecachedir.c_str());

    //客户端升级包与聊天文件分目录存放：升级包由运维手工放入 updatecachedir，
    //文件名是人起的，靠 UpdatePackageIndex 按内容MD5 反查。
    //缺省时回落到 filecachedir，老配置文件不改也能跑（行为与本功能上线前一致）。
    const char* updatecachedirCfg = config.getConfigName("updatecachedir");
    std::string updatecachedir;
    if (updatecachedirCfg != NULL && updatecachedirCfg[0] != '\0')
    {
        //与 chatserver 的 dbpath 一样按「可执行文件所在目录」解析，而不是按进程工作目录。
        //工作目录是启动方式决定的，而升级包必须始终是同一批文件，否则换种方式启动
        //就等于换了一个空目录、所有升级包凭空消失。
        updatecachedir = apppath::ResolveAgainstDirectory(
                             apppath::GetExecutableDirectory(), updatecachedirCfg);
        LOGI("update package dir resolved against executable directory: %s -> %s",
             updatecachedirCfg, updatecachedir.c_str());
    }
    else
    {
        updatecachedir = filecachedir;
        LOGW("updatecachedir not configured, fall back to filecachedir: %s",
             updatecachedir.c_str());
    }
    Singleton<UpdatePackageIndex>::Instance().init(
        updatecachedir, static_cast<int64_t>(time(NULL)) * 1000);

    //监听地址、端口与令牌密钥在函数开头已随其它必需配置项一并取出并校验过，这里直接用。
    //filetokenkey 是上传 / 下载令牌的 HMAC 校验密钥，须与 chatserver.conf 的同名项一致。
    //fileserver 以 file_server_kind_file 启动，只放行聊天文件 / 群文件场景
    Singleton<FileServer>::Instance().init(listenip.c_str(), (short)listenport, &g_mainLoop,
                                           filecachedir.c_str(), filetokenkey.c_str(),
                                           file_server_kind_file);

    LOGI("fileserver initialization completed, now you can use client to connect it.");
    
    g_mainLoop.loop();

    LOGI("exit fileserver.");

    return 0;
}
