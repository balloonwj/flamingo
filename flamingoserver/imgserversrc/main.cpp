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
#include "../utils/AppPath.h"
#include "../utils/ServerConfigLoader.h"

#ifndef WIN32
#include <string.h>
#include "../utils/DaemonRun.h"
#endif

using namespace net;

//本服务的名字与配置文件名。名字只出现在启动失败的提示里，让用户一眼看出是哪个进程报的错；
//配置文件名交给 ServerConfigLoader 去各个候选目录里找，不再写死一条相对路径
#define SERVER_NAME              "imgserver"
#define SERVER_CONFIG_FILE_NAME  "imgserver.conf"

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
    std::string imgcachedir  = required.getString("imgcachedir");
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

    //不区分 Debug / Release，一律写日志文件，理由同 chatserversrc/main.cpp：
    //原先 _DEBUG 下只输出 stdout、不落盘，双击或从 IDE 启动时日志就全丢了。
    //传文件名不影响终端输出，CAsyncLog 的 stdout 一路是无条件执行的。
    CAsyncLog::init(logFileFullPath.c_str());

    Singleton<FileManager>::Instance().init(imgcachedir.c_str());

    //监听地址、端口与令牌密钥在函数开头已随其它必需配置项一并取出并校验过，这里直接用。
    //filetokenkey 是上传 / 下载令牌的 HMAC 校验密钥，须与 chatserver.conf 的同名项一致。
    //imgserver 以 file_server_kind_image 启动，只放行头像 / 图片消息场景。
    //最后一个参数是文件夹传输中转令牌的密钥，这里<u>刻意传空串</u>：单聊文件夹传输由
    //fileserver 承载（见 docs/单聊文件夹传输方案.md 第 2 节的表），imgserver 的图片端口
    //不提供这项能力，空密钥使 cmd_folder_relay_hello 一律被拒并记日志。注意本行与下面
    //RelayServer 用的 relaytokenkey 是同一把密钥的两处用途，但走的是两个不同的监听端口：
    //那边是远程桌面 / 音视频的中转端口，与这里的图片端口互不相干
    //图片端口的网络 I/O 工作线程数。可选配置项：缺失或取值非法时回落到
    //FileServer::kDefaultIoThreadCount（即改造前写死的 6）并记一条告警日志。
    //本服务与 fileserver 共用 FileServer 这一个类，但线程数各读各的 .conf，互不影响。
    //这段放在 CAsyncLog::init() 之后，告警才写得出来
    int ioThreadCount = serverconfig::ReadIoThreadCountConfig(
                            config, "iothreadnum", FileServer::kDefaultIoThreadCount);

    Singleton<FileServer>::Instance().init(listenip.c_str(), (short)listenport, &g_mainLoop,
        imgcachedir.c_str(), filetokenkey.c_str(), file_server_kind_image, "", ioThreadCount);

    //远程桌面中转：在独立端口起 RelayServer，令牌密钥须与 chatserver.conf 的 relaytokenkey 一致
    const char* relaytokenkey = config.getConfigName("relaytokenkey");
    const char* relaylistenip = config.getConfigName("relaylistenip");
    const char* relaylistenportStr = config.getConfigName("relaylistenport");
    short relaylistenport = (relaylistenportStr != NULL) ? (short)atol(relaylistenportStr) : 0;

    //中转端口的网络 I/O 工作线程数，与上面图片端口的那一项相互独立：中转连接数远少于
    //图片传输，默认值也更小。缺失或取值非法时回落到 RelayServer::kDefaultIoThreadCount
    int relayIoThreadCount = serverconfig::ReadIoThreadCountConfig(
                                 config, "relayiothreadnum", RelayServer::kDefaultIoThreadCount);

    Singleton<RelayServer>::Instance().init(
        relaylistenip != NULL ? relaylistenip : "0.0.0.0",
        relaylistenport, &g_mainLoop,
        relaytokenkey != NULL ? relaytokenkey : "",
        relayIoThreadCount);

    LOGI("imgserver initialization complete, now you can use client to connect it.");

    g_mainLoop.loop();

    LOGI("exit imgserver.");

    return 0;
}
