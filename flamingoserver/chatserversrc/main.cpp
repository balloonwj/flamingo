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
#include "../dbapi/DbConfig.h"
#include "../sqliteapi/SqliteManager.h"
#include "../utils/AppPath.h"
#include "../utils/ServerConfigLoader.h"

//FLAMINGO_NO_MYSQL 由 CMake 在 -DWITH_MYSQL=OFF 时定义，
//用于编出一份不依赖 libmysqlclient 的纯本地版本，见 CMakeLists.txt 顶部的说明
#ifndef FLAMINGO_NO_MYSQL
#include "../mysqlmgr/MysqlManager.h"
#endif

#ifndef WIN32
#include <string.h>
#include "../utils/DaemonRun.h"
#endif 

#include "UserManager.h"
#include "UpdatePackageScanner.h"
#include "MessageManager.h"
#include "MessagePinManager.h"
#include "GroupManager.h"
#include "CardManager.h"
#include "FileStoreManager.h"
#include "GroupFileManager.h"
#include "RemoteDesktopManager.h"
#include "CallManager.h"
#include "CallProtocol.h"
#include "DepartmentManager.h"
#include "AdminManager.h"
#include "EnterpriseManager.h"
#include "EmployeeManager.h"
#include "AnnouncementManager.h"
#include "ServiceAccountManager.h"
#include "ChatServer.h"
#include "MonitorServer.h"
#include "HttpServer.h"

using namespace net;

#ifdef WIN32
//初始化Windows socket库
NetworkInitializer windowsNetworkInitializer;
#endif

EventLoop g_mainLoop;

//默认超级管理员的内置兜底值。etc/chatserver.conf 里没写对应配置项时用这三个值，
//保证老配置文件不改也能引导出一个能登录 admin 控制台的账号。
//注意：口令是公开写在仓库里的，正式部署务必在配置文件里改掉，或把 defaultadminenable 置 0。
//姓名必须带 u8 前缀：chatserver.vcxproj 没有 /utf-8 选项，不加前缀的中文字面量会被
//MSVC 按系统代码页（GBK）编码写进库里，客户端读出来就是乱码。全仓库其它中文字面量同此。
#define DEFAULT_ADMIN_EMAIL      "admin@flamingo.com"
#define DEFAULT_ADMIN_PASSWORD   "admin123"
#define DEFAULT_ADMIN_NAME       u8"超级管理员"

//本服务的名字与配置文件名。名字只出现在启动失败的提示里，让用户一眼看出是哪个进程报的错；
//配置文件名交给 ServerConfigLoader 去各个候选目录里找，不再写死一条相对路径
#define SERVER_NAME              "chatserver"
#define SERVER_CONFIG_FILE_NAME  "chatserver.conf"

/**
 *  定时公告调度器的一轮工作：把到点的定时公告转为「已发布」，并把它们推给在线受众。
 *
 *  由 main() 注册为 EventLoop 的周期任务（周期见 ANNOUNCEMENT_SCHEDULER_INTERVAL_US）。
 *  写成具名函数而不是 lambda，一来符合本仓库的代码约定，二来这段逻辑不再是一行。
 *
 *  <u>推送这一步很容易漏</u>：定时发布是批量的、没有请求上下文，不像管理员点「立即发布」
 *  那样有个明确的 HTTP 入口摆在那里。漏掉的表现是「定时公告到点了，客户端要等下次登录
 *  才看得到」，不容易被发现。
 */
static void PublishDueAnnouncementsTick()
{
    std::vector<int64_t> publishedIds;
    Singleton<AnnouncementManager>::Instance().publishDueScheduled(&publishedIds);

    for (size_t i = 0; i < publishedIds.size(); ++i)
    {
        Singleton<ChatServer>::Instance().broadcastAnnouncementPublished(publishedIds[i]);
    }
}

#ifndef WIN32
void prog_exit(int signo)
{
    std::cout << "program recv signal [" << signo << "] to exit." << std::endl;

    Singleton<MonitorServer>::Instance().uninit();
    Singleton<HttpServer>::Instance().uninit();
    Singleton<ChatServer>::Instance().uninit();
    g_mainLoop.quit();

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

    //配置文件按「可执行文件所在目录优先」的顺序查找，找不到再回落到相对工作目录的老路径。
    //工作目录是启动方式决定的：在可执行文件所在目录里双击、用起始位置为空的快捷方式启动、
    //以管理员身份运行、从别的目录敲命令，拿到的工作目录各不相同。改造前这里写死一条相对
    //工作目录的路径，工作目录一变就读不到配置文件，随后在下面第一处取配置项时对空指针
    //取长度而崩溃，且崩溃发生在日志初始化之前，控制台上一个字都看不到。详见 ServerConfigLoader.h
    CConfigFileReader config;
    std::string triedConfigPaths;
    if (!serverconfig::LoadServerConfig(config, SERVER_CONFIG_FILE_NAME, triedConfigPaths))
    {
        serverconfig::ReportStartupFailure(SERVER_NAME,
            std::string("cannot open config file ") + SERVER_CONFIG_FILE_NAME
            + ", tried: " + triedConfigPaths);
        return 1;
    }

    //必需配置项先一次性取齐、再统一判断，这样缺哪几项一次就报全，
    //不必让运维改一项、重启一次、再看下一条报错
    serverconfig::CRequiredConfig required(config);
    std::string logfiledir      = required.getString("logfiledir");
    std::string logfilename     = required.getString("logfilename");
    std::string listenip        = required.getString("listenip");
    int         listenport      = required.getInt("listenport");
    std::string monitorlistenip = required.getString("monitorlistenip");
    int         monitorlistenport = required.getInt("monitorlistenport");
    std::string monitortoken    = required.getString("monitortoken");
    int         httplistenport  = required.getInt("httplistenport");
    if (required.hasMissing())
    {
        serverconfig::ReportStartupFailure(SERVER_NAME,
            std::string("missing required config item(s): ") + required.missingKeys()
            + ", config file: " + config.getFilePath());
        return 1;
    }

    const char* logbinarypackage = config.getConfigName("logbinarypackage");
    if (logbinarypackage != NULL)
    {
        int logbinarypackageint = atoi(logbinarypackage);
        if (logbinarypackageint != 0)
            Singleton<ChatServer>::Instance().enableLogPackageBinary(true);
        else
            Singleton<ChatServer>::Instance().enableLogPackageBinary(false);
    }

    //日志目录取自配置项 logfiledir：相对路径按可执行文件所在目录解析、绝对路径原样使用，
    //与配置文件和 dbpath 的规则一致，这样无论从哪个工作目录启动，日志都落在同一处。
    //目录不存在就创建，两个平台都做 —— 改造前只有 Linux 建目录，Windows 上目录不存在时
    //日志文件建不出来，而那时写日志线程会直接结束，连控制台输出也一起没了
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

#ifdef _DEBUG
    CAsyncLog::init();
#else
    CAsyncLog::init(logFileFullPath.c_str());
#endif

    //初始化数据库配置
    const char* dbserver = config.getConfigName("dbserver");
    const char* dbuser = config.getConfigName("dbuser");
    const char* dbpassword = config.getConfigName("dbpassword");
    const char* dbname = config.getConfigName("dbname");
    //dbtype / dbpath 是本地 SQLite 模式引入的新配置项。老配置文件里没有这两项，
    //getConfigName 会返回 NULL，CDbConfig::init 据此回落到 mysql，行为与改造前一致。
    const char* dbtype = config.getConfigName("dbtype");
    const char* dbpath = config.getConfigName("dbpath");

    //把 dbpath 的相对路径基准从「进程工作目录」改成「可执行文件所在目录」。
    //工作目录是启动方式决定的（从哪个目录敲的命令、被谁拉起来的都会影响它），
    //而库文件必须始终是同一个，否则换种方式启动就等于换了一个空库。
    //绝对路径原样放行；万一取不到可执行文件目录，则回落到原有行为，不让服务起不来。
    //配置项缺失时先补上默认值再解析，否则 CDbConfig 内部回落出来的默认路径会绕过这里。
    std::string configuredDbPath = (dbpath != NULL ? dbpath : "");
    if (configuredDbPath.empty())
    {
        configuredDbPath = DEFAULT_SQLITE_DB_PATH;
    }
    std::string resolvedDbPath = apppath::ResolveAgainstDirectory(
                                     apppath::GetExecutableDirectory(), configuredDbPath);
    LOGI("db path resolved against executable directory: %s -> %s",
         configuredDbPath.c_str(), resolvedDbPath.c_str());
    //客户端升级包目录：控制台的「客户端版本」页要列出目录里的候选包。chatserver 对该目录
    //<u>只读</u>，包由运维手工放入、由 fileserver 提供下载，两个服务必须指向同一个目录。
    //与 dbpath 同样按可执行文件所在目录解析，理由见上：工作目录随启动方式变，目录不能跟着变。
    const char* updatecachedirCfg = config.getConfigName("updatecachedir");
    std::string resolvedUpdateDir;
    if (updatecachedirCfg != NULL && updatecachedirCfg[0] != '\0')
    {
        resolvedUpdateDir = apppath::ResolveAgainstDirectory(
                                apppath::GetExecutableDirectory(), updatecachedirCfg);
        LOGI("update package dir resolved against executable directory: %s -> %s",
             updatecachedirCfg, resolvedUpdateDir.c_str());
    }
    else
    {
        LOGW("updatecachedir not configured, admin console cannot list upgrade packages");
    }
    Singleton<UpdatePackageScanner>::Instance().init(resolvedUpdateDir);

    //全局数据库配置必须在任何一个 Manager 初始化之前灌好：
    //它们建连接时都走 DbFactory，而工厂的连接参数就取自这里。
    CDbConfig& dbConfig = Singleton<CDbConfig>::Instance();
    dbConfig.init(dbtype, dbserver, dbuser, dbpassword, dbname, resolvedDbPath.c_str());

    //建库建表引导。两种后端的引导流程完全不同：MySQL 要先连上服务器、必要时 CREATE DATABASE，
    //再逐表 desc 比对；SQLite 的库就是一个文件，打开即建，只需执行建表语句再补列。
    //所以这里必须按类型分支，不能两个都跑——SQLite 模式下跑 MySQL 引导会连不上库而报错
    if (dbConfig.getDbType() == DB_TYPE_SQLITE)
    {
        if (!Singleton<CSqliteManager>::Instance().init(dbConfig.getDbPath().c_str()))
        {
            LOGF("Init sqlite failed, please check your database config..............");
        }
    }
    else
    {
#ifdef FLAMINGO_NO_MYSQL
        //本次构建关掉了 MySQL 后端，配置却写着 dbtype=mysql，属于配置与构建不匹配，
        //此时接着往下跑只会在每次数据库调用时报错，不如直接停在这里
        LOGF("dbtype is mysql but this build has no mysql backend (built with WITH_MYSQL=OFF), "
             "please set dbtype=sqlite in etc/chatserver.conf");
#else
        //CMysqlManager 仍然显式收参：它要先用「不指定库名」的方式连上服务器判断库是否存在、
        //必要时 CREATE DATABASE，这套流程是 MySQL 专属的，与各业务 Manager 的连接方式不同
        if (!Singleton<CMysqlManager>::Instance().init(dbserver, dbuser, dbpassword, dbname))
        {
            LOGF("Init mysql failed, please check your database config..............");
        }
#endif
    }

    if (!Singleton<UserManager>::Instance().init())
    {
        LOGF("Init UserManager failed, please check your database config..............");
    }

    //初始化消息存储管理器：v2 单聊消息 t_chatmsg 表的落库与查询
    if (!Singleton<MessageManager>::Instance().init())
    {
        LOGF("Init MessageManager failed, please check your database config..............");
    }

    //初始化消息 Pin 管理器：t_message_pin 表的落库与查询（会话内共享的消息 Pin 状态）
    if (!Singleton<MessagePinManager>::Instance().init())
    {
        LOGF("Init MessagePinManager failed, please check your database config..............");
    }

    //初始化群组管理器：v2 群组 t_group / t_group_member 表的落库与查询
    if (!Singleton<GroupManager>::Instance().init())
    {
        LOGF("Init GroupManager failed, please check your database config..............");
    }

    //初始化群互动卡片管理器：v2 t_card_interaction 表的落库与查询
    if (!Singleton<CardManager>::Instance().init())
    {
        LOGF("Init CardManager failed, please check your database config..............");
    }

    //初始化文件元数据管理器：v2 t_file 表的落库与查询
    if (!Singleton<FileStoreManager>::Instance().init())
    {
        LOGF("Init FileStoreManager failed, please check your database config..............");
    }
    {
        //文件令牌密钥与目标 fileserver / imgserver 地址来自配置文件；
        //缺项时取空串 / 0，configure 仍可调用，仅文件功能不可用
        const char* filetokenkey   = config.getConfigName("filetokenkey");
        const char* imgserverhost  = config.getConfigName("imgserverhost");
        const char* imgserverport  = config.getConfigName("imgserverport");
        const char* fileserverhost = config.getConfigName("fileserverhost");
        const char* fileserverport = config.getConfigName("fileserverport");
        //本期暂不启用 TLS（帧标志 frame_flag_encrypted 保留不实现），故 tls 恒为 false
        Singleton<FileStoreManager>::Instance().configure(
            filetokenkey != NULL ? filetokenkey : "",
            imgserverhost != NULL ? imgserverhost : "",
            imgserverport != NULL ? atoi(imgserverport) : 0, false,
            fileserverhost != NULL ? fileserverhost : "",
            fileserverport != NULL ? atoi(fileserverport) : 0, false);
    }
    {
        //远程桌面：relay 令牌密钥来自 relaytokenkey；中转通道主机复用 imgserverhost、
        //端口用独立的 relayport（对应 imgserver 的 RelayServer 监听端口）。缺项时取空串 / 0，
        //仅远程桌面功能不可用，不影响其它功能
        const char* relaytokenkey  = config.getConfigName("relaytokenkey");
        const char* imgserverhost  = config.getConfigName("imgserverhost");
        const char* relayport      = config.getConfigName("relayport");
        Singleton<RemoteDesktopManager>::Instance().configure(
            relaytokenkey != NULL ? relaytokenkey : "",
            imgserverhost != NULL ? imgserverhost : "",
            relayport != NULL ? atoi(relayport) : 0);

        //音视频通话复用同一套中转配置：与远程桌面共用 imgserver 的 RelayServer 与令牌密钥
        Singleton<CallManager>::Instance().configure(
            relaytokenkey != NULL ? relaytokenkey : "",
            imgserverhost != NULL ? imgserverhost : "",
            relayport != NULL ? atoi(relayport) : 0);
    }

    //初始化群文件存储管理器：v2 t_group_file 表的落库与查询
    if (!Singleton<GroupFileManager>::Instance().init())
    {
        LOGF("Init GroupFileManager failed, please check your database config..............");
    }

    //初始化企业部门管理器：从数据库加载组织架构（部门树 + 成员归属）
    if (!Singleton<DepartmentManager>::Instance().init())
    {
        LOGF("Init DepartmentManager failed, please check your database config..............");
    }

    //初始化控制台管理员管理器：admin HTTP 接口的登录鉴权与审计
    if (!Singleton<AdminManager>::Instance().init())
    {
        LOGF("Init AdminManager failed, please check your database config..............");
    }

    //引导默认超级管理员：新部署的库里一个管理员都没有，admin 控制台会陷入"登不进去也就
    //建不了账号"的死循环，故在这里按配置注入一个。四个配置项都允许缺失，缺时用下面的内置
    //默认值，老配置文件不改也能照常跑。
    DefaultAdminConfig defaultAdmin;
    const char* defaultadminenable = config.getConfigName("defaultadminenable");
    //配置项缺失时默认启用；只有显式写 0 才关闭
    defaultAdmin.enable = (defaultadminenable == NULL || atoi(defaultadminenable) != 0);
    const char* defaultadminemail = config.getConfigName("defaultadminemail");
    defaultAdmin.email = (defaultadminemail != NULL ? defaultadminemail : DEFAULT_ADMIN_EMAIL);
    const char* defaultadminpassword = config.getConfigName("defaultadminpassword");
    defaultAdmin.password = (defaultadminpassword != NULL ? defaultadminpassword : DEFAULT_ADMIN_PASSWORD);
    const char* defaultadminname = config.getConfigName("defaultadminname");
    defaultAdmin.name = (defaultadminname != NULL ? defaultadminname : DEFAULT_ADMIN_NAME);

    //注入失败只记日志不拦启动：少一个默认账号不影响 IM 本身的任何功能
    bool defaultAdminCreated = false;
    int32_t defaultAdminRet = Singleton<AdminManager>::Instance().ensureDefaultSuperAdmin(defaultAdmin, defaultAdminCreated);
    if (defaultAdminRet != 0)
    {
        LOGE("Ensure default super admin failed, code: %d, email: %s. "
             "The admin console may have no account to log in with.",
             defaultAdminRet, defaultAdmin.email.c_str());
    }
    else if (defaultAdminCreated)
    {
        LOGI("Default super admin created, email: %s. Please change its password after the first login.",
             defaultAdmin.email.c_str());
    }

    //初始化企业租户管理器：admin 企业管理模块
    if (!Singleton<EnterpriseManager>::Instance().init())
    {
        LOGF("Init EnterpriseManager failed, please check your database config..............");
    }

    //初始化控制台员工管理器：admin 员工/用户管理模块
    if (!Singleton<EmployeeManager>::Instance().init())
    {
        LOGF("Init EmployeeManager failed, please check your database config..............");
    }

    //初始化平台公告管理器：admin 公告管理模块
    if (!Singleton<AnnouncementManager>::Instance().init())
    {
        LOGF("Init AnnouncementManager failed, please check your database config..............");
    }

    //初始化企业服务号管理器：admin 服务号管理模块
    if (!Singleton<ServiceAccountManager>::Instance().init())
    {
        LOGF("Init ServiceAccountManager failed, please check your database config..............");
    }

    //注册定时公告调度器：在主循环上周期性扫描并自动发布到期的定时公告，
    //随后把新发布的那几条推给在线受众（见 PublishDueAnnouncementsTick 的说明）。
    g_mainLoop.runEvery(ANNOUNCEMENT_SCHEDULER_INTERVAL_US, PublishDueAnnouncementsTick);

    //注册音视频通话超时清理：周期扫描响铃超时（无人接听）的呼叫会话，清理之并给仍在线的
    //双方推送 call_state_timeout。扫描间隔 10 秒（微秒单位）。
    const int64_t CALL_TIMEOUT_SCAN_INTERVAL_US = 10LL * 1000 * 1000;
    g_mainLoop.runEvery(CALL_TIMEOUT_SCAN_INTERVAL_US,
        []()
        {
            int64_t now = static_cast<int64_t>(time(NULL)) * 1000;
            std::vector<CallSessionInfo> timedOut;
            Singleton<CallManager>::Instance().reapTimedOutSessions(now, timedOut);
            for (size_t i = 0; i < timedOut.size(); ++i)
            {
                std::vector<int64_t> users;
                users.push_back(timedOut[i].callerId);
                users.push_back(timedOut[i].calleeId);
                Singleton<ChatServer>::Instance().pushToUsers(
                    users, cmd_call_state_notify,
                    CallProtocol::buildStateNotify(timedOut[i].sessionid, call_state_timeout, "timeout"));
            }
        });

    //几个监听地址与端口在函数开头已随其它必需配置项一并取出并校验过，这里直接用
    Singleton<ChatServer>::Instance().init(listenip.c_str(), (short)listenport, &g_mainLoop);

    Singleton<MonitorServer>::Instance().init(monitorlistenip.c_str(), (short)monitorlistenport,
                                              &g_mainLoop, monitortoken.c_str());

    //注意：HTTP 监听用的是 monitorlistenip 而不是 httplistenip，这是既有行为，
    //改动它会影响现网部署的监听地址，须先与使用方确认，故此处照原样保留
    Singleton<HttpServer>::Instance().init(monitorlistenip.c_str(), (short)httplistenport, &g_mainLoop);

    LOGI("chatserver initialization completed, now you can use client to connect it.");

    g_mainLoop.loop();

    LOGI("exit chatserver.");

    return 0;
}