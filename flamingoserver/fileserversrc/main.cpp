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
#include "UploadPartPolicy.h"

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

    //协议加密：读 encryption / serverkeyfile 并加载身份密钥，默认开启。
    //密钥文件必须与 chatserver 指向同一份，否则客户端要为每个服务各配一把公钥。
    //开着加密却拿不到密钥时必须让启动失败，不能退回明文继续跑。
    std::string cryptoError;
    if (!serverconfig::InitProtocolEncryption(config, cryptoError))
    {
        serverconfig::ReportStartupFailure(SERVER_NAME,
            std::string("protocol encryption setup failed: ") + cryptoError);
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
    //上传分片登记表由各 I/O 线程并发访问，而 Singleton::Instance() 的首次构造不加锁，
    //故在网络线程启动之前先构造好（见 UploadPartPolicy.h）
    Singleton<fileserver::UploadPartRegistry>::Instance();

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

    //relaytokenkey 是单聊文件夹传输中转令牌的 HMAC 校验密钥，须与 chatserver.conf 的同名项
    //一致：令牌由 chatserver 在接收端同意之后签发，由 fileserver 本地验签。
    //它是可选项而不是必需项——老部署的配置文件里没有这一行，此时本服务照常提供上传下载，
    //只是不承载文件夹中转（FileServer::init 会为此记一条日志）
    const char* relaytokenkey = config.getConfigName("relaytokenkey");

    //文件端口的网络 I/O 工作线程数。可选配置项：缺失或取值非法时回落到
    //FileServer::kDefaultIoThreadCount（即改造前写死的 6）并记一条告警日志。
    //imgserver 复用 FileServer，但读的是 imgserver.conf 里<u>自己那一份</u> iothreadnum，
    //两个服务各配各的。这段放在 CAsyncLog::init() 之后，告警才写得出来
    int ioThreadCount = serverconfig::ReadIoThreadCountConfig(
                            config, "iothreadnum", FileServer::kDefaultIoThreadCount);

    //监听地址、端口与令牌密钥在函数开头已随其它必需配置项一并取出并校验过，这里直接用。
    //filetokenkey 是上传 / 下载令牌的 HMAC 校验密钥，须与 chatserver.conf 的同名项一致。
    //fileserver 以 file_server_kind_file 启动，只放行聊天文件 / 群文件场景
    Singleton<FileServer>::Instance().init(listenip.c_str(), (short)listenport, &g_mainLoop,
                                           filecachedir.c_str(), filetokenkey.c_str(),
                                           file_server_kind_file,
                                           relaytokenkey != NULL ? relaytokenkey : "",
                                           ioThreadCount);

    LOGI("fileserver initialization completed, now you can use client to connect it.");
    
    g_mainLoop.loop();

    LOGI("exit fileserver.");

    return 0;
}
