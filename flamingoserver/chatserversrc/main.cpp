/**
 *  聊天服务程序入口函数
 *  zhangyl 2017.03.09
 **/
#include <iostream>
#include <stdlib.h>
#include <chrono>
#include <vector>

#include "../base/Platform.h"
#include "../base/Singleton.h"
#include "../base/ConfigFileReader.h"
#include "../base/AsyncLog.h"
#include "../net/EventLoop.h"
#include "../net/EventLoopThreadPool.h"
#include "../dbapi/DbConfig.h"
#include "../dbapi/DbFactory.h"
#include "../sqliteapi/SqliteManager.h"
#include "../utils/AppPath.h"
#include "../utils/ServerConfigLoader.h"
#include "../net/FrameCrypto.h"          //协议加密：进程级配置装载

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
#include "MemoManager.h"
#include "FileStoreManager.h"
#include "GroupFileManager.h"
#include "RemoteDesktopManager.h"
#include "RemoteDesktopProtocol.h"   // rd_state_disconnected / buildStateNotify（掉线宽限清理时通知双方）
#include "CallManager.h"
#include "ScreenShareManager.h"
#include "ScreenShareService.h"
#include "GroupCallManager.h"       //群通话：房间表与运行参数
#include "GroupCallService.h"       //群通话：每秒的振铃超时与掉线宽限清理
#include "GroupCallServerEnv.h"     //群通话：清理时推送通知用的线上环境
#include "CallProtocol.h"
#include "CallCardWriter.h"         //一对一通话卡片：振铃超时、掉线结束时改写
//单聊文件夹传输：内存会话表（含邀请超时与失败保留期的清理）+ 八条信令的报文构造。
//邀请超时会话的状态推送与留记录已收口到 ChatSession::finishFolderTransferSession，
//本文件不再直接构造报文；FolderTransferProtocol.h 仍要留着 —— 超时清理用到的
//folder_state_timeout 与 error_code_folder_invite_timeout 定义在 Protocol.h，
//而本文件正是经它间接引入 Protocol.h 的（ChatSession.h 因与遗留的 AdminApiCodes.h 枚举同名，
//刻意不包含 Protocol.h）
#include "FolderTransferManager.h"
#include "FileShareManager.h"
#include "FileShareSessionManager.h"
#include "FileSharePolicy.h"   //ShareSweepPlan：启动一致性清扫的结果
#include "FolderTransferProtocol.h"
#include "DepartmentManager.h"
#include "AdminManager.h"
#include "EnterpriseManager.h"
#include "EmployeeManager.h"
#include "AnnouncementManager.h"
#include "ServiceAccountManager.h"
#include "SensitiveWordManager.h"      //企业级敏感词：拦截记录的过期清理
#include "FeatureSwitchManager.h"      //按企业的功能开关：启动时载入
#include "SecretChatManager.h"         //单聊密聊的内存会话：启动时按配置设置上限，周期扫描邀请超时
#include "P2PSessionManager.h"         //P2P 信令的会话管理：启动时按配置初始化
#include "P2PServerSetup.h"            //P2P 配置项的读取
#include "../utils/ConfigHostCheck.h"  //启动时检查下发给客户端的主机地址是否只有本机才能访问
#include "ChatServer.h"
#include "MonitorServer.h"
#include "HttpServer.h"
#include "AdminLinkServer.h"   //管理控制台长连接（FlamingoLink），docs/FlamingoLink方案.md
#include "BotManager.h"        //机器人的存储与快照（docs/机器人方案.md）
#include "BotEngine.h"         //机器人线程与外部服务工作线程
#include "BotServerEnv.h"      //机器人运行环境的生产实现
#include "../utils/HttpsClient.h"   //机器人呼叫外部服务的出站代理设置
#include "CloudRecordLink.h"        //云端录制：到 imgserver 的控制连接（第 26 期）
#include "CloudRecordServerEnv.h"   //云端录制：线上环境与控制连接的桥接
#include "CloudRecordService.h"     //云端录制：服务端处理流程
#include "CloudRecordStore.h"       //云端录制：存储

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
/**
 *  群通话的定时清理：移除振铃超时的被邀请人；已加入的人掉线超过宽限期按离开处理，最后一个已加入的人走了则
 *  结束通话、改写通话卡片并通知群成员。由 main() 注册为每秒一次的周期任务（docs/协议设计-v2.md 21.6）。
 */
static void ReapGroupCallsTick()
{
    GroupCallServerEnv env;
    GroupCallService service(Singleton<GroupCallManager>::Instance(), env);
    service.reapTick(static_cast<int64_t>(time(NULL)) * 1000);
}

/**
 *  通话的掉线宽限清理：参与方最后一个登录连接断开后超过宽限期仍未重新登录的通话，
 *  由服务端结束，向双方推送 call_state_ended（掉线的一方不在线时推送自然送不到），并删除会话。
 *  由 main() 注册为每秒一次的周期任务。
 */
static void ReapOfflineCallsTick()
{
    const int64_t now = static_cast<int64_t>(time(NULL)) * 1000;
    std::vector<CallSessionInfo> reaped;
    Singleton<CallManager>::Instance().reapOfflineSessions(now, reaped);
    for (size_t i = 0; i < reaped.size(); ++i)
    {
        std::vector<int64_t> users;
        users.push_back(reaped[i].callerId);
        users.push_back(reaped[i].calleeId);
        Singleton<ChatServer>::Instance().pushToUsers(
            users, cmd_call_state_notify,
            CallProtocol::buildStateNotify(reaped[i].sessionid, call_state_ended, "peer offline"));
        CallCardWriter::finishCard(reaped[i], call_state_ended, now);
        LOGI("call ended because a participant stayed offline, session: %s, caller: %lld (offline since %lld), "
             "callee: %lld (offline since %lld)",
             reaped[i].sessionid.c_str(), (long long)reaped[i].callerId, (long long)reaped[i].callerOfflineAt,
             (long long)reaped[i].calleeId, (long long)reaped[i].calleeOfflineAt);
    }
}

/**
 *  远程桌面的掉线宽限清理：参与方最后一个登录连接断开后超过宽限期仍未重新登录的会话，由服务端结束，
 *  向双方推送 rd_state_disconnected（掉线的一方不在线时推送自然送不到），并删除会话。由 main() 注册为每秒一次的周期任务。
 */
static void ReapOfflineRemoteDesktopTick()
{
    const int64_t now = static_cast<int64_t>(time(NULL)) * 1000;
    std::vector<RemoteDesktopSessionInfo> reaped;
    Singleton<RemoteDesktopManager>::Instance().reapOfflineSessions(now, reaped);
    for (size_t i = 0; i < reaped.size(); ++i)
    {
        std::vector<int64_t> users;
        users.push_back(reaped[i].controllerId);
        users.push_back(reaped[i].controlledId);
        Singleton<ChatServer>::Instance().pushToUsers(
            users, cmd_rd_state_notify,
            RemoteDesktopProtocol::buildStateNotify(reaped[i].sessionid, rd_state_disconnected, "peer offline"));
        LOGI("remote desktop ended because a participant stayed offline, session: %s, controller: %lld "
             "(offline since %lld), controlled: %lld (offline since %lld)",
             reaped[i].sessionid.c_str(), (long long)reaped[i].controllerId, (long long)reaped[i].controllerOfflineAt,
             (long long)reaped[i].controlledId, (long long)reaped[i].controlledOfflineAt);
    }
}

static void PublishDueAnnouncementsTick()
{
    std::vector<int64_t> publishedIds;
    Singleton<AnnouncementManager>::Instance().publishDueScheduled(&publishedIds);

    for (size_t i = 0; i < publishedIds.size(); ++i)
    {
        Singleton<ChatServer>::Instance().broadcastAnnouncementPublished(publishedIds[i]);
    }
}

//敏感词拦截记录过期清理的周期（微秒）：每小时一轮。保留期按天计（SENSITIVE_HIT_RETENTION_DAYS），
//记录到期后最多晚一小时被删除，不影响「保留 180 天」的约定；删除按 f_create_time 索引进行，代价很小
static const int64_t SENSITIVE_HIT_PURGE_INTERVAL_US = 3600LL * 1000 * 1000;

/**
 *  清理过期的敏感词拦截记录：删除保留期（SENSITIVE_HIT_RETENTION_DAYS 天）之前的记录
 *  （docs/敏感词方案.md 第 6.5 节）。
 *  @param alwaysLog 为 true 时无论删没删都记一行日志（服务端启动时那一次，便于确认清理在运行）；
 *                   为 false 时只在确实删除了记录时才记（每小时那几次，避免每天多出几十行无用日志）
 */
static void PurgeExpiredSensitiveHits(bool alwaysLog)
{
    const int64_t kMillisPerSecond = 1000;
    const int64_t nowMs = static_cast<int64_t>(time(NULL)) * kMillisPerSecond;
    const int64_t cutoff = SensitiveWordManager::HitRetentionCutoff(nowMs);
    int deleted = 0;
    //失败时 purgeHitsBefore 内部已记错误日志，这里不再重复
    if (!Singleton<SensitiveWordManager>::Instance().purgeHitsBefore(cutoff, deleted))
        return;
    if (alwaysLog || deleted > 0)
        LOGI("sensitive hit log purged, cutoff: %lld, deleted: %d", (long long)cutoff, deleted);
}

//由 main() 注册为 EventLoop 的周期任务（周期见 SENSITIVE_HIT_PURGE_INTERVAL_US）。
//写成具名函数而不是 lambda，与 PublishDueAnnouncementsTick 一致
static void PurgeExpiredSensitiveHitsTick()
{
    PurgeExpiredSensitiveHits(false);
}

//云端录像的维护周期（微秒）：每小时一次，推送到期前 3 天的提醒、删除已到期的录像（docs/屏幕共享与录制方案.md 12.6.5）
static const int64_t CLOUD_RECORD_MAINTAIN_INTERVAL_US = 3600LL * 1000 * 1000;
//秒到毫秒的换算系数（本文件后面的 MILLISECONDS_PER_SECOND 定义在使用处之后，这里另起一个）
static const int64_t CLOUD_RECORD_MS_PER_SECOND = 1000;

//由 main() 注册为 EventLoop 的周期任务；没有配置云端录制时什么也不做
static void CloudRecordMaintenanceTick()
{
    CloudRecordService* service = CloudRecordService::instance();
    if (service != NULL)
        service->runMaintenance(static_cast<int64_t>(time(NULL)) * CLOUD_RECORD_MS_PER_SECOND);
}

//数据库连接池的维护周期（微秒）：30 秒一轮。保活阈值是 60 秒（DbHandlePool 的默认值），
//维护周期取它的一半，空闲连接最迟在空闲约 90 秒时被 ping 到，远小于 MySQL wait_timeout 的默认值 8 小时
static const int64_t DB_POOL_MAINTAIN_INTERVAL_US = 30LL * 1000 * 1000;

//每隔多少轮维护把连接池的统计写一行日志：20 轮，即每 10 分钟一行
static const int DB_POOL_STATS_LOG_ROUNDS = 20;

//已经执行过的维护轮数，只在主线程的周期任务里读写
static int s_dbPoolMaintainRounds = 0;

//由 main() 注册为 EventLoop 的周期任务（周期见 DB_POOL_MAINTAIN_INTERVAL_US）：维护数据库连接池，
//并每 DB_POOL_STATS_LOG_ROUNDS 轮记一行统计，用于确认连接池的命中率与保活情况。
//写成具名函数而不是 lambda，与 PublishDueAnnouncementsTick 一致
static void MaintainDbConnectionPoolsTick()
{
    DbFactory::maintainConnectionPools();

    ++s_dbPoolMaintainRounds;
    if (s_dbPoolMaintainRounds % DB_POOL_STATS_LOG_ROUNDS == 0)
        LOGI("%s", DbFactory::describeConnectionPools().c_str());
}

//存量消息搜索文本（t_chatmsg.f_search_text）的回填：每批处理多少行、隔多久处理一批（微秒）。
//一批在一个事务里写回，SQLite 下约十毫秒量级；主循环只负责接受连接与定时任务，这样的间隔不影响它。
//每秒五批即每秒一千行，十万条存量消息约两分钟回填完；回填期间会话内搜索对未回填的行仍按旧方式查 f_content。
static const int SEARCH_TEXT_BACKFILL_BATCH = 200;
static const int64_t SEARCH_TEXT_BACKFILL_INTERVAL_US = 200LL * 1000;

//回填是否已经结束（全部回填完，或出错后放弃、等下次启动再继续）。结束后周期任务直接返回
static bool g_searchTextBackfillDone = false;
//本次启动以来回填的行数，结束时记进日志
static long long g_searchTextBackfillRows = 0;

//由 main() 注册为 EventLoop 的周期任务（周期见 SEARCH_TEXT_BACKFILL_INTERVAL_US）：每次回填一批，
//没有待回填的行或出错时结束
static void BackfillSearchTextTick()
{
    if (g_searchTextBackfillDone)
        return;
    const int rows = Singleton<MessageManager>::Instance().backfillSearchText(SEARCH_TEXT_BACKFILL_BATCH);
    if (rows > 0)
    {
        g_searchTextBackfillRows += rows;
        return;
    }
    g_searchTextBackfillDone = true;
    if (rows == 0)
        LOGI("search text backfill finished, rows: %lld", g_searchTextBackfillRows);
    else
        LOGE("search text backfill stopped on error after %lld rows, it resumes on next start", g_searchTextBackfillRows);
}

//单聊文件夹传输的会话清理扫描周期（微秒）。邀请超时默认 30 分钟、失败会话保留期默认
//10 分钟，两者都远大于本周期，故 5 秒一轮足以让"到点"与"实际被清理"之间的偏差可以忽略；
//会话表本身很小（上限即配置项 foldermaxsessions），扫描代价可以不计。
//取值与音视频通话的超时扫描（CALL_TIMEOUT_SCAN_INTERVAL_US）同一量级
static const int64_t FOLDER_TRANSFER_SCAN_INTERVAL_US = 5LL * 1000 * 1000;

//一秒折合的毫秒数：文件夹传输会话表一律以 Unix 毫秒计时，而 time(NULL) 给出的是秒
static const int64_t MILLISECONDS_PER_SECOND = 1000;

/**
 *  读取一个整型配置项；配置项缺失或写成空值时返回给定的默认值。
 *
 *  chatserver 的必填项走 CRequiredConfig 统一校验，而文件夹传输那几项都是可选的
 *  （老配置文件不改也要能启动），故单独走本函数取值。
 *
 *  @param config       已装载好的配置文件读取器
 *  @param key          配置项名
 *  @param defaultValue 配置项缺失或为空时的取值
 *  @return 配置项按十进制解析出的整数值；缺失或为空时为 defaultValue
 */
//机器人运行环境（生产实现）；生命周期与进程相同，BotEngine 只持有它的指针
static BotServerEnv g_botEnv;
//机器人外部服务工作线程数的默认值（配置项 botexternalthreads 缺失时）
static const int64_t kDefaultBotExternalThreads = 2;

static int64_t ReadOptionalInt64Config(CConfigFileReader& config, const char* key, int64_t defaultValue)
{
    const char* value = config.getConfigName(key);
    if (value == NULL || value[0] == '\0')
        return defaultValue;

    //foldermaxsize 默认 50 GiB，超出 32 位，故一律按 64 位解析
    return static_cast<int64_t>(atoll(value));
}

/**
 *  启动时检查实际下发给客户端的主机地址，只有本机才能访问的（如默认的 127.0.0.1）按配置项各打一条 WARN。
 *  这类地址下发出去以后，其他机器上的客户端连的是它自己那台电脑，中转、P2P 地址探测、自动更新与云端录像传输全部失败，
 *  而服务端这一侧收不到任何连接、也就没有任何错误日志，只能在启动时提醒部署人员（见 utils/ConfigHostCheck.h）。
 *  须在各功能都配置完成之后调用：P2P 的探测地址取 P2PSessionManager 实际使用的配置（已按规则回落）。
 *  @param config 已加载的配置文件（借用）
 */
static void WarnLocalOnlyPublishedHosts(CConfigFileReader& config)
{
    std::vector<PublishedHost> published;
    //中转与 P2P 地址探测：按 P2P 会话管理器实际使用的配置列出
    const char* probeHost = config.getConfigName("p2pprobehost");
    P2PServerSetup::appendPublishedHosts(Singleton<P2PSessionManager>::Instance().getConfig(),
                                         probeHost != NULL && probeHost[0] != '\0', published);
    //FileStoreManager 在上传、下载应答里下发的地址，与交给它及 FolderTransferManager、FileShareSessionManager 的取值相同。
    //客户端只在云端录像传输（imgserver）与自动更新（fileserver）时按下发的地址连接，聊天里的图片、文件按自己
    //flamingo.ini 里的地址连接（docs/服务端配置项说明.md），所以用途只写这几项
    const char* imgserverhost = config.getConfigName("imgserverhost");
    const char* imgserverport = config.getConfigName("imgserverport");
    const char* fileserverhost = config.getConfigName("fileserverhost");
    const char* fileserverport = config.getConfigName("fileserverport");
    published.push_back(PublishedHost("imgserverhost", imgserverhost != NULL ? imgserverhost : "",
                                      imgserverport != NULL ? atoi(imgserverport) : 0,
                                      "cloud recording upload and download"));
    published.push_back(PublishedHost("fileserverhost", fileserverhost != NULL ? fileserverhost : "",
                                      fileserverport != NULL ? atoi(fileserverport) : 0,
                                      "update package download, folder transfer and file share"));

    const std::vector<LocalOnlyHostWarning> warnings = ConfigHostCheck::findLocalOnly(published);
    for (size_t i = 0; i < warnings.size(); ++i)
    {
        LOGW("config item %s=%s is reachable from this machine only, so clients on other machines cannot use %s. "
             "set it to an address the clients can reach (normally the server address in their flamingo.ini).",
             warnings[i].m_key.c_str(), warnings[i].m_host.c_str(), warnings[i].m_usages.c_str());
    }
}

/**
 *  单聊文件夹传输的会话清理一轮工作：把邀请超时的会话结束掉并通知双方，把已过失败
 *  保留期的会话静默销毁。
 *
 *  由 main() 注册为 EventLoop 的周期任务（周期见 FOLDER_TRANSFER_SCAN_INTERVAL_US）。
 *  之所以由 chatserver 主循环驱动而不是挂在某条会话上：超时的判定与任何一条客户端连接
 *  都无关，发起邀请的那一端完全可能在超时之前就断开；FolderTransferManager 本身又是纯
 *  逻辑、不持有定时器，时间必须由外部传入。音视频通话的响铃超时清理也是这么做的
 *  （见下面的 CALL_TIMEOUT_SCAN_INTERVAL_US），两者放在一处便于对照。
 *
 *  写成具名函数而不是 lambda，与 PublishDueAnnouncementsTick 一致。
 */
//单聊密聊的邀请超时扫描周期（微秒）：邀请有效期默认 60 秒，客户端卡片按 expirems 倒计时，
//1 秒一轮让「服务端判定超时」与「卡片倒计时走完」基本同时发生（docs/密聊方案.md 4.2）
static const int64_t SECRET_REAP_INTERVAL_US = 1000LL * 1000;

/**
 *  单聊密聊的邀请超时扫描一轮：结束已到期的待应答邀请，给发起方与被邀请人推 2605（未接受）。
 *  会话表只在内存里，大小以在线用户数为上限，扫描代价可以不计。写成具名函数，与 ReapFolderTransferSessionsTick 一致。
 */
static void ReapSecretInvitesTick()
{
    const int64_t now = static_cast<int64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
    std::vector<SecretOutFrame> frames;
    const size_t expired = Singleton<SecretChatManager>::Instance().reapExpired(now, frames);
    if (expired == 0)
        return;
    LOGI("secret invites expired: %u, frames: %u", static_cast<unsigned>(expired), static_cast<unsigned>(frames.size()));
    ChatSession::sendSecretFrames(frames);
}

static void ReapFolderTransferSessionsTick()
{
    int64_t now = static_cast<int64_t>(time(NULL)) * MILLISECONDS_PER_SECOND;

    std::vector<FolderTransferSessionInfo> timedOut;
    std::vector<FolderTransferSessionInfo> expired;
    Singleton<FolderTransferManager>::Instance().tick(now, timedOut, expired);

    //（1）邀请超时：会话已被 tick 移出会话表，这里只负责通知。按 docs/协议设计-v2.md
    //12.2.6 的推送时机表推给<u>双方</u> —— 发送端要把气泡转成"已超时"，接收端也要把
    //还挂着的「接收 / 拒绝」气泡收起来
    for (size_t i = 0; i < timedOut.size(); ++i)
    {
        std::vector<int64_t> users;
        users.push_back(timedOut[i].senderId);
        users.push_back(timedOut[i].receiverId);

        //推终态与留记录一并交给 ChatSession::finishFolderTransferSession —— 全部终态分支
        //都收口在那一个函数里，本条路径不自己拼推送。它是静态方法，正是为了让这条没有
        //ChatSession 可用的定时器路径也能走同一个出口（超时的判定与任何一条客户端连接
        //都无关，发起邀请的那一端完全可能早已断开）。
        //
        //本条路径同样要经那道终结闸门（FolderTransferManager::finishAttempt）：定时器判定
        //超时的同一时刻，发起方那帧 cmd_folder_close 完全可能正落在别的工作线程上，两条
        //路径抢着终结同一次尝试时只有一条能拿到 true。会话此时已被 tick 移出会话表，
        //闸门只登记"这一次尝试已终结"、不再改会话状态。
        //返回 false 即表示这一次尝试已由别的路径收尾，本轮不必再记这条超时日志
        if (ChatSession::finishFolderTransferSession(timedOut[i], folder_state_timeout,
                                                     error_code_folder_invite_timeout,
                                                     "invite timeout", users))
        {
            LOGI("folder transfer invite timed out, session: %s, sender: %lld, receiver: %lld.",
                 timedOut[i].sessionid.c_str(),
                 (long long)timedOut[i].senderId, (long long)timedOut[i].receiverId);
        }
    }

    //（2）失败会话的保留期届满：不推送，客户端的「重试」按钮到点自行置灰；这里只记一条
    //日志，使"用户点重试却被回 618"能在服务端日志里找到对应的销毁记录
    for (size_t i = 0; i < expired.size(); ++i)
    {
        LOGI("folder transfer session destroyed after retry window, session: %s, "
             "sender: %lld, receiver: %lld.",
             expired[i].sessionid.c_str(),
             (long long)expired[i].senderId, (long long)expired[i].receiverId);
    }
}

#ifndef WIN32
void prog_exit(int signo)
{
    std::cout << "program recv signal [" << signo << "] to exit." << std::endl;

    Singleton<MonitorServer>::Instance().uninit();
    Singleton<HttpServer>::Instance().uninit();
    Singleton<AdminLinkServer>::Instance().uninit();
    Singleton<ChatServer>::Instance().uninit();
    //机器人线程与外部服务工作线程：等正在执行的外部请求结束（最长一个请求超时）
    Singleton<BotEngine>::Instance().stop();
    //到 imgserver 的控制连接：线程最多等一个 select 周期退出
    Singleton<CloudRecordLink>::Instance().stop();
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

    //协议加密：读 encryption / serverkeyfile 并加载身份密钥，默认开启。
    //开着加密却拿不到密钥时必须让启动失败，不能退回明文继续跑。
    std::string cryptoError;
    if (!serverconfig::InitProtocolEncryption(config, cryptoError))
    {
        serverconfig::ReportStartupFailure(SERVER_NAME,
            std::string("protocol encryption setup failed: ") + cryptoError);
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

    //<u>不区分 Debug / Release，一律写日志文件</u>。这里原先在 _DEBUG 下调无参的
    //init()（只输出到 stdout、不落盘），结果是 Debug 版从资源管理器双击、或从
    //Visual Studio 启动时，stdout 无人接收，logs 目录建了却始终是空的，排查问题时
    //手上一条日志都没有——现象上很像日志功能坏了。
    //传文件名<u>不会</u>损失终端输出：CAsyncLog 的写线程里 std::cout 与
    //OutputDebugStringA 都是无条件执行的，文件只是多出来的一路（见 base/AsyncLog.cpp
    //的写线程主体）。fileserver 一直就是这么做的，这里与它拉齐。
    CAsyncLog::init(logFileFullPath.c_str());

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

    //数据库连接池，配置项缺失时默认启用。必须在任何 Manager 初始化之前、各工作线程开始访问
    //数据库之前打开：池对象在这里由主线程创建，理由见 DbFactory::setConnectionPoolEnabled
    const bool dbConnPoolEnabled = (ReadOptionalInt64Config(config, "dbconnpool", 1) != 0);
    DbFactory::setConnectionPoolEnabled(dbConnPoolEnabled);

    //建库建表引导。两种后端的引导流程完全不同：MySQL 要先连上服务器、必要时 CREATE DATABASE，
    //再逐表建出缺少的表；SQLite 的库就是一个文件，打开即建，只需执行建表语句。两边都不改动已有的表。
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

    //初始化备忘存储管理器（备忘与提醒，2401~2404）
    if (!Singleton<MemoManager>::Instance().init())
    {
        LOGF("Init MemoManager failed, please check your database config..............");
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

        //远程桌面的掉线宽限期（毫秒，可选配置项，缺失或不大于 0 时用默认 30 秒）
        const char* rdgracems = config.getConfigName("rdofflinegracems");
        if (rdgracems != NULL)
            Singleton<RemoteDesktopManager>::Instance().setOfflineGraceMs(atoll(rdgracems));
        LOGI("remote desktop config: offlinegracems=%lld",
             (long long)Singleton<RemoteDesktopManager>::Instance().offlineGraceMs());

        //云端录制（第 26 期，docs/屏幕共享与录制方案.md 12.6）：chatserver 主动连 imgserver 的中转端口建控制连接。
        //地址默认复用 imgserverhost；两台服务之间走内网时可另配 cloudrecordhost。加密握手验 imgserver 的身份签名，
        //要求两边用同一份 server_key（默认配置即如此）。没有令牌密钥或地址时不启用，1754～1756 回「云端存储服务不可用」
        const char* filetokenkey    = config.getConfigName("filetokenkey");
        const char* cloudrecordhost = config.getConfigName("cloudrecordhost");
        if (filetokenkey != NULL && filetokenkey[0] != '\0')
        {
            static CloudRecordStore s_cloudRecordStore;
            static CloudRecordServerEnv s_cloudRecordEnv;
            static CloudRecordLinkBridge s_cloudRecordBridge(Singleton<CloudRecordLink>::Instance(), &g_mainLoop);
            static CloudRecordService s_cloudRecordService(s_cloudRecordStore, s_cloudRecordEnv, s_cloudRecordBridge,
                                                           filetokenkey);
            s_cloudRecordBridge.setService(&s_cloudRecordService);
            CloudRecordService::setInstance(&s_cloudRecordService);
            CloudRecordLinkConfig linkConfig;
            linkConfig.m_host = (cloudrecordhost != NULL && cloudrecordhost[0] != '\0') ? cloudrecordhost
                                : (imgserverhost != NULL ? imgserverhost : "");
            linkConfig.m_port = relayport != NULL ? atoi(relayport) : 0;
            linkConfig.m_tokenKey = relaytokenkey != NULL ? relaytokenkey : "";
            linkConfig.m_encrypt = net::cryptoEncryptionEnabled();
            Singleton<CloudRecordLink>::Instance().setListener(&s_cloudRecordBridge);
            Singleton<CloudRecordLink>::Instance().start(linkConfig);
        }
        else
        {
            LOGW("cloud record disabled: filetokenkey not configured");
        }

        //通话的掉线宽限期（毫秒，可选配置项，缺失或不大于 0 时用默认 30 秒）
        const char* callgracems = config.getConfigName("callofflinegracems");
        if (callgracems != NULL)
            Singleton<CallManager>::Instance().setOfflineGraceMs(atoll(callgracems));
        LOGI("call config: offlinegracems=%lld", (long long)Singleton<CallManager>::Instance().offlineGraceMs());
    }
    {
        //单聊密聊（docs/密聊方案.md 4.2、4.10、5.3）：全部是可选配置项，缺失或不大于 0 时用默认值，老配置文件不改也能启动。
        //secretinvitetimeout 的单位是秒，其余为字节数或个数
        SecretChatLimits limits;
        limits.m_inviteTimeoutMs = ReadOptionalInt64Config(config, "secretinvitetimeout",
                                                           kSecretDefaultInviteTimeoutMs / MILLISECONDS_PER_SECOND)
                                   * MILLISECONDS_PER_SECOND;
        limits.m_maxFrameBytes = static_cast<uint32_t>(ReadOptionalInt64Config(config, "secretmaxframebytes",
                                                                               kSecretDefaultMaxFrameBytes));
        limits.m_maxSessionsPerUser = static_cast<int>(ReadOptionalInt64Config(config, "secretmaxsessionsperuser",
                                                                               kSecretDefaultMaxSessionsPerUser));
        limits.m_invitesPerMinute = static_cast<int>(ReadOptionalInt64Config(config, "secretinvitesperminute",
                                                                             kSecretDefaultInvitesPerMinute));
        limits.m_framesPerSecond = static_cast<int>(ReadOptionalInt64Config(config, "secretframespersecond",
                                                                            kSecretDefaultFramesPerSecond));
        Singleton<SecretChatManager>::Instance().setLimits(limits);
        const SecretChatLimits applied = Singleton<SecretChatManager>::Instance().getLimits();
        LOGI("secret chat config: invitetimeoutms=%lld, maxframebytes=%u, maxsessionsperuser=%d, invitesperminute=%d, "
             "framespersecond=%d", (long long)applied.m_inviteTimeoutMs, applied.m_maxFrameBytes,
             applied.m_maxSessionsPerUser, applied.m_invitesPerMinute, applied.m_framesPerSecond);
    }
    {
        //屏幕共享：与音视频通话共用 imgserver 的中转服务（relaytokenkey、imgserverhost、relayport）。
        //房间人数上限、同时开摄像头的人数上限、发起人掉线宽限期都是可选配置项，缺失时用默认值，
        //越界时由 configure 回落到默认值，老配置文件不改也能启动
        const char* relaytokenkey  = config.getConfigName("relaytokenkey");
        const char* imgserverhost  = config.getConfigName("imgserverhost");
        const char* relayport      = config.getConfigName("relayport");
        const char* maxmembers     = config.getConfigName("screensharemaxmembers");
        const char* maxcameras     = config.getConfigName("screensharemaxcameras");
        const char* hostgracems    = config.getConfigName("screensharehostgracems");
        //同时共享屏幕的人数上限（第 22 期多人共享，含主持人），可选，缺失时默认 4
        const char* maxsharers     = config.getConfigName("screensharemaxsharers");
        ScreenShareConfig shareConfig;
        shareConfig.m_relayTokenKey = relaytokenkey != NULL ? relaytokenkey : "";
        shareConfig.m_relayHost     = imgserverhost != NULL ? imgserverhost : "";
        shareConfig.m_relayPort     = relayport != NULL ? atoi(relayport) : 0;
        if (maxmembers != NULL)
            shareConfig.m_maxMembers = atoi(maxmembers);
        if (maxcameras != NULL)
            shareConfig.m_maxCameras = atoi(maxcameras);
        if (hostgracems != NULL)
            shareConfig.m_hostGraceMs = atoll(hostgracems);
        if (maxsharers != NULL)
            shareConfig.m_maxSharers = atoi(maxsharers);
        Singleton<ScreenShareManager>::Instance().configure(shareConfig);
        const ScreenShareConfig applied = Singleton<ScreenShareManager>::Instance().getConfig();
        LOGI("screenshare config: maxmembers=%d, maxcameras=%d, maxsharers=%d, hostgracems=%lld",
             applied.m_maxMembers, applied.m_maxCameras, applied.m_maxSharers, (long long)applied.m_hostGraceMs);
    }
    {
        //群通话：与屏幕共享共用 imgserver 的房间中转（relaytokenkey、imgserverhost、relayport）。人数上限、
        //同时开摄像头的人数上限、已加入的人掉线宽限期都是可选配置项，缺失时用默认值，越界时由 configure 回落到默认值
        //（docs/协议设计-v2.md 21.7），老配置文件不改也能启动
        const char* relaytokenkey  = config.getConfigName("relaytokenkey");
        const char* imgserverhost  = config.getConfigName("imgserverhost");
        const char* relayport      = config.getConfigName("relayport");
        const char* maxmembers     = config.getConfigName("groupcallmaxmembers");
        const char* maxcameras     = config.getConfigName("groupcallmaxcameras");
        const char* gracems        = config.getConfigName("groupcallofflinegracems");
        GroupCallConfig callConfig;
        callConfig.m_relayTokenKey = relaytokenkey != NULL ? relaytokenkey : "";
        callConfig.m_relayHost     = imgserverhost != NULL ? imgserverhost : "";
        callConfig.m_relayPort     = relayport != NULL ? atoi(relayport) : 0;
        if (maxmembers != NULL)
            callConfig.m_maxMembers = atoi(maxmembers);
        if (maxcameras != NULL)
            callConfig.m_maxCameras = atoi(maxcameras);
        if (gracems != NULL)
            callConfig.m_offlineGraceMs = atoll(gracems);
        Singleton<GroupCallManager>::Instance().configure(callConfig);
        const GroupCallConfig appliedCall = Singleton<GroupCallManager>::Instance().getConfig();
        LOGI("groupcall config: maxmembers=%d, maxcameras=%d, offlinegracems=%lld",
             appliedCall.m_maxMembers, appliedCall.m_maxCameras, (long long)appliedCall.m_offlineGraceMs);
    }
    {
        //P2P 信令（docs/P2P通信方案.md 9.3）：回落中转与远程桌面、通话共用 relaytokenkey、imgserverhost、relayport；
        //其余 p2p* 配置项都可缺失，缺失或非法时取默认值（规则见 P2PServerSetup.h），老配置文件不改也能启动
        Singleton<P2PSessionManager>::Instance().configure(P2PServerSetup::loadConfig(config));
        const P2PSessionConfig p2p = Singleton<P2PSessionManager>::Instance().getConfig();
        const std::string firstProbe = p2p.m_probes.empty() ? std::string("none")
            : p2p.m_probes[0].m_ip + ":" + std::to_string(p2p.m_probes[0].m_port);
        LOGI("p2p config: relay=%s:%d, probes=%u (first %s), connecttimeoutms=%d, keepalivems=%d, linktimeoutms=%d, "
             "maxperuser=%d, debugopen=%d", p2p.m_relayHost.c_str(), p2p.m_relayPort,
             static_cast<unsigned>(p2p.m_probes.size()), firstProbe.c_str(), p2p.m_connectTimeoutMs, p2p.m_keepaliveMs,
             p2p.m_linkTimeoutMs, p2p.m_maxPerUser, p2p.m_debugOpen ? 1 : 0);
    }
    {
        //单聊文件夹传输的运行参数。
        //令牌密钥复用 relaytokenkey：中转令牌的格式与签发 / 验签流程与远程桌面、音视频
        //完全一致，只是角色编号续到 5 / 6，没有理由再配一把。
        //中转地址与端口复用 fileserverhost / fileserverport：文件夹的字节走 fileserver
        //<u>现有的那个监听端口</u>，不新开端口，而 cmd_request_upload 的应答里告诉客户端
        //去哪里收发文件用的正是这两项 —— 同一个服务在配置里只该有一份对外地址，
        //另立一份迟早会出现"上传能通、文件夹传输连不上"这类两边不一致的故障。
        //几个上限与超时项都允许缺失：缺时传 0 / 传内置默认值，由 configure 回落到默认值，
        //老配置文件不改也能启动（folderlanes 特意传默认值而非 0，避免每次启动都记一条
        //"配置值非法"的告警）。
        const char* relaytokenkey  = config.getConfigName("relaytokenkey");
        const char* fileserverhost = config.getConfigName("fileserverhost");
        const char* fileserverport = config.getConfigName("fileserverport");

        int dataLanes = static_cast<int>(ReadOptionalInt64Config(
            config, "folderlanes", FolderTransferManager::kDefaultDataLanes));
        int64_t maxFiles = ReadOptionalInt64Config(
            config, "foldermaxfiles", FolderTransferManager::kDefaultMaxFiles);
        int64_t maxSize = ReadOptionalInt64Config(
            config, "foldermaxsize", FolderTransferManager::kDefaultMaxSize);
        int maxSessions = static_cast<int>(ReadOptionalInt64Config(
            config, "foldermaxsessions", FolderTransferManager::kDefaultMaxSessions));
        //每个用户同时可参与的笔数上限：作发送方与作接收方各算一笔，与整台服务器的
        //会话总数上限是两道相互独立的闸门
        int maxSessionsPerUser = static_cast<int>(ReadOptionalInt64Config(
            config, "foldermaxperuser", FolderTransferManager::kDefaultMaxSessionsPerUser));
        int64_t inviteTimeoutMs = ReadOptionalInt64Config(
            config, "folderinvitetimeout", FolderTransferManager::kDefaultInviteTimeoutMs);
        int64_t retryWindowMs = ReadOptionalInt64Config(
            config, "folderretrywindow", FolderTransferManager::kDefaultRetryWindowMs);

        Singleton<FolderTransferManager>::Instance().configure(
            relaytokenkey != NULL ? relaytokenkey : "",
            fileserverhost != NULL ? fileserverhost : "",
            fileserverport != NULL ? atoi(fileserverport) : 0,
            dataLanes, maxFiles, maxSize, maxSessions, maxSessionsPerUser,
            inviteTimeoutMs, retryWindowMs);

        //单聊单文件 P2P 直传（2026-10-04 起）：邀请超时与每人同时可参与的笔数单独配置，缺失时取内置默认值
        int64_t directFileInviteTimeoutMs = ReadOptionalInt64Config(
            config, "directfileinvitetimeout", FolderTransferManager::kDefaultDirectFileInviteTimeoutMs);
        int directFileMaxPerUser = static_cast<int>(ReadOptionalInt64Config(
            config, "directfilemaxperuser", FolderTransferManager::kDefaultDirectFileMaxPerUser));
        Singleton<FolderTransferManager>::Instance().configureDirectFile(directFileInviteTimeoutMs,
                                                                         directFileMaxPerUser);
    }
    {
        //文件共享的运行参数（docs/文件共享方案.md 6.4）。各项都允许缺失，缺失时取内置默认值，
        //老配置文件不改也能启动；越界的取值由 FileShareSessionManager::configure 修正。
        //令牌密钥与中转地址复用文件夹传输那一组（relaytokenkey、fileserverhost / fileserverport）：
        //文件共享的字节同样走 fileserver 现有的中转端口，令牌格式与验签流程完全一致
        int maxSharesPerOwner = static_cast<int>(ReadOptionalInt64Config(
            config, "sharemaxpershareowner", FileShareManager::kDefaultMaxSharesPerOwner));
        Singleton<FileShareManager>::Instance().setMaxSharesPerOwner(maxSharesPerOwner);
        //按企业的功能开关（t_system_setting）整表载入内存；读库失败时一律按内置默认值（文件共享默认开启）
        Singleton<FeatureSwitchManager>::Instance().loadFromDb();

        //机器人（docs/机器人方案.md）：载入全部机器人与规则，启动机器人线程与外部服务工作线程。
        //botproxy 为外部服务请求的 HTTP 代理（如 10.0.0.1:3128，缺失表示不用代理；ConfigFileReader 把行内的 # 当注释，
        //代理地址里不能含 #）；botexternalthreads 为外部服务工作线程数（缺失取 2）；
        //botwebhookbaseurl 为外部系统访问本服务 HTTP 端口所用的根地址（如 https://im.example.com:8443，缺失表示不下发
        //完整地址，管理台按自己的连接地址拼、客户端只显示说明），必须在机器人线程启动前设置
        Singleton<BotManager>::Instance().loadFromDb();
        const char* botwebhookbaseurl = config.getConfigName("botwebhookbaseurl");
        Singleton<BotManager>::Instance().setWebhookBaseUrl(botwebhookbaseurl != NULL ? botwebhookbaseurl : "");
        const char* botproxy = config.getConfigName("botproxy");
        if (botproxy != NULL)
            httpclient::SetProxy(botproxy);
        const int botThreads = static_cast<int>(ReadOptionalInt64Config(config, "botexternalthreads", kDefaultBotExternalThreads));
        Singleton<BotEngine>::Instance().start(&g_botEnv, botThreads);

        const char* relaytokenkey  = config.getConfigName("relaytokenkey");
        const char* fileserverhost = config.getConfigName("fileserverhost");
        const char* fileserverport = config.getConfigName("fileserverport");
        FileShareSessionConfig shareConfig;
        shareConfig.m_relayTokenKey = relaytokenkey != NULL ? relaytokenkey : "";
        shareConfig.m_relayHost = fileserverhost != NULL ? fileserverhost : "";
        shareConfig.m_relayPort = fileserverport != NULL ? atoi(fileserverport) : 0;
        shareConfig.m_lanes = static_cast<int>(ReadOptionalInt64Config(
            config, "sharelanes", FileShareSessionManager::kDefaultLanes));
        shareConfig.m_maxAccess = static_cast<int>(ReadOptionalInt64Config(
            config, "sharemaxaccess", FileShareSessionManager::kDefaultMaxAccess));
        shareConfig.m_maxTransfers = static_cast<int>(ReadOptionalInt64Config(
            config, "sharemaxtransfers", FileShareSessionManager::kDefaultMaxTransfers));
        shareConfig.m_maxTransferPerShare = static_cast<int>(ReadOptionalInt64Config(
            config, "sharemaxtransferpershare", FileShareSessionManager::kDefaultMaxTransferPerShare));
        shareConfig.m_openTimeoutMs = static_cast<int>(ReadOptionalInt64Config(
            config, "shareopentimeout", FileShareSessionManager::kDefaultOpenTimeoutMs));
        shareConfig.m_idleTimeoutMs = static_cast<int>(ReadOptionalInt64Config(
            config, "shareidletimeout", FileShareSessionManager::kDefaultIdleTimeoutMs));
        shareConfig.m_searchMaxResults = static_cast<int>(ReadOptionalInt64Config(
            config, "sharesearchmaxresults", FileShareSessionManager::kDefaultSearchMaxResults));
        shareConfig.m_searchTimeoutMs = static_cast<int>(ReadOptionalInt64Config(
            config, "sharesearchtimeout", FileShareSessionManager::kDefaultSearchTimeoutMs));
        Singleton<FileShareSessionManager>::Instance().configure(shareConfig);

        const FileShareSessionConfig& applied = Singleton<FileShareSessionManager>::Instance().getConfig();
        LOGI("file share configured, sharemaxpershareowner: %d, relay: %s:%d, lanes: %d, maxaccess: %d, "
             "maxtransfers: %d, maxtransferpershare: %d, opentimeout: %d ms, idletimeout: %d ms",
             Singleton<FileShareManager>::Instance().getMaxSharesPerOwner(), applied.m_relayHost.c_str(),
             applied.m_relayPort, applied.m_lanes, applied.m_maxAccess, applied.m_maxTransfers,
             applied.m_maxTransferPerShare, applied.m_openTimeoutMs, applied.m_idleTimeoutMs);
    }
    //下发给客户端的地址都已确定：跨机器部署时保留默认的 127.0.0.1 是常见的配置遗漏，按实际下发的地址检查一遍
    WarnLocalOnlyPublishedHosts(config);
    {
        //文件共享的启动一致性清扫：退群、踢人、解散时的联动处理是在群操作成功之后另起的，中途失败会留下
        //不一致（分享者已不在群里、成员已不在群里、群已不存在），这里统一补齐（docs/文件共享方案.md 6.1）。
        //读群信息任何一步出错都放弃本次清扫，避免把一次数据库故障误判成「群已解散」而批量取消共享；
        //启动时没有人在线，只改数据库、不推送
        FileShareManager& shareMgr = Singleton<FileShareManager>::Instance();
        std::set<int64_t> groupIds;
        std::map<int64_t, std::set<int64_t> > groupMembersOf;
        if (!shareMgr.listActiveShareGroupIds(groupIds) || !shareMgr.loadGroupMembership(groupIds, groupMembersOf))
        {
            LOGE("file share startup sweep skipped, reading groups failed");
        }
        else
        {
            const int64_t kMillisPerSecond = 1000;
            ShareSweepPlan applied;
            const bool ok = shareMgr.sweepGroupShares(groupMembersOf,
                static_cast<int64_t>(time(NULL)) * kMillisPerSecond, applied);
            LOGI("file share startup sweep %s, groups: %u, canceled shares: %u, shares with members removed: %u",
                 ok ? "done" : "stopped by db error", static_cast<unsigned>(groupIds.size()),
                 static_cast<unsigned>(applied.m_cancels.size()), static_cast<unsigned>(applied.m_removeMembers.size()));
        }
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

    //保证默认企业存在：PC 客户端注册窗的公司名称默认填的就是它，用户不改默认名直接注册
    //即可加入。全称取自 chatserver.conf 的 defaultenterprise（缺失或为空时为 FlamingoIM.org），
    //须与客户端 flamingo.ini 的 [ui] defaultcompany 配成同一个值。已存在时不改它的状态（管理员
    //可在管理台停用它来关闭这条注册途径），只在缺少公司根部门时补建。须在 DepartmentManager
    //初始化之后调用，建根部门要用到它。
    //失败只记日志不拦启动：少了默认企业，只影响选默认公司的注册，IM 其余功能照常。
    const std::string defaultEnterpriseName =
        EnterpriseManager::resolveDefaultEnterpriseName(config.getConfigName("defaultenterprise"));
    bool defaultEnterpriseCreated = false;
    int32_t defaultEnterpriseRet = Singleton<EnterpriseManager>::Instance().ensureDefaultEnterprise(
        defaultEnterpriseName, defaultEnterpriseCreated);
    if (defaultEnterpriseRet != 0)
    {
        LOGE("Ensure default enterprise failed, code: %d, name: %s. "
             "Registering with the default company name will fail.",
             defaultEnterpriseRet, defaultEnterpriseName.c_str());
    }
    else if (defaultEnterpriseCreated)
    {
        LOGI("Default enterprise created, name: %s.", defaultEnterpriseName.c_str());
    }
    else
    {
        LOGI("Default enterprise in use, name: %s.", defaultEnterpriseName.c_str());
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
    //双方推送 call_state_timeout。扫描间隔 1 秒（微秒单位）：第 15 期起被叫的来电弹窗按 1702 的 ringms
    //显示倒计时，倒计时走完后窗口要随超时推送及时关闭；原先 10 秒一次时实际超时会比倒计时晚到最多 10 秒。
    //扫描只遍历内存里的通话会话表，1 秒一次的开销可以忽略。
    const int64_t CALL_TIMEOUT_SCAN_INTERVAL_US = 1LL * 1000 * 1000;
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
                CallCardWriter::finishCard(timedOut[i], call_state_timeout, now);
            }
        });

    //注册屏幕共享的宽限期清理：发起人掉线超过宽限期仍未回来的房间由服务端解散，通知会话成员并写结束提示。
    //扫描间隔 1 秒（微秒单位）：宽限期默认 30 秒，这一间隔带来的误差可以忽略
    const int64_t SCREENSHARE_REAP_INTERVAL_US = 1000LL * 1000;
    g_mainLoop.runEvery(SCREENSHARE_REAP_INTERVAL_US, &ScreenShareService::reapExpiredHosts);

    //注册群通话的定时清理：振铃超时（45 秒）与已加入成员的掉线宽限期都按秒计，每秒扫描一次
    const int64_t GROUPCALL_REAP_INTERVAL_US = 1000LL * 1000;
    g_mainLoop.runEvery(GROUPCALL_REAP_INTERVAL_US, ReapGroupCallsTick);

    //注册单聊密聊的邀请超时扫描（周期与理由见 SECRET_REAP_INTERVAL_US）
    g_mainLoop.runEvery(SECRET_REAP_INTERVAL_US, ReapSecretInvitesTick);

    //注册通话的掉线宽限清理：参与方掉线超过宽限期仍未回来的通话由服务端结束并通知双方。
    //与屏幕共享同样每秒扫描一次，宽限期默认 30 秒
    const int64_t CALL_OFFLINE_REAP_INTERVAL_US = 1000LL * 1000;
    g_mainLoop.runEvery(CALL_OFFLINE_REAP_INTERVAL_US, ReapOfflineCallsTick);

    //注册远程桌面的掉线宽限清理：与通话相同，每秒扫描一次
    const int64_t RD_OFFLINE_REAP_INTERVAL_US = 1000LL * 1000;
    g_mainLoop.runEvery(RD_OFFLINE_REAP_INTERVAL_US, ReapOfflineRemoteDesktopTick);

    //注册单聊文件夹传输的会话清理：周期扫描邀请超时与失败保留期届满的会话，
    //前者通知双方、后者静默销毁（见 ReapFolderTransferSessionsTick 的说明）
    g_mainLoop.runEvery(FOLDER_TRANSFER_SCAN_INTERVAL_US, ReapFolderTransferSessionsTick);

    //初始化企业级敏感词库管理器（词库在首次检查时按企业装载，这里只清空缓存），并注册拦截
    //记录的过期清理：启动时先清理一次，之后每小时一次（见 PurgeExpiredSensitiveHits 的说明）
    Singleton<SensitiveWordManager>::Instance().init();
    PurgeExpiredSensitiveHits(true);
    g_mainLoop.runEvery(SENSITIVE_HIT_PURGE_INTERVAL_US, PurgeExpiredSensitiveHitsTick);

    //回填存量消息的搜索文本：升级前入库的消息 f_search_text 为 NULL，分批提取后写回
    g_mainLoop.runEvery(SEARCH_TEXT_BACKFILL_INTERVAL_US, BackfillSearchTextTick);

    //云端录像的到期提醒与清理：每小时一次
    g_mainLoop.runEvery(CLOUD_RECORD_MAINTAIN_INTERVAL_US, CloudRecordMaintenanceTick);

    //数据库连接池的定期维护：保活、关闭长时间没有被使用的连接，并定期记录统计
    if (dbConnPoolEnabled)
        g_mainLoop.runEvery(DB_POOL_MAINTAIN_INTERVAL_US, MaintainDbConnectionPoolsTick);

    //三个监听端口各自的网络 I/O 工作线程数。都是可选配置项：缺失或取值非法时回落到各自的
    //默认值（即本项改造前写死在代码里的那个数）并记一条告警日志，老配置文件不改也能启动、
    //且行为与改造前完全一致。判定与日志收在 ReadIoThreadCountConfig 里，见
    //utils/ServerConfigLoader.h。
    //这段必须放在 CAsyncLog::init() 之后：取值非法时的那条告警要真的写得出来才有意义
    int ioThreadCount = serverconfig::ReadIoThreadCountConfig(
                            config, "iothreadnum", ChatServer::kDefaultIoThreadCount);
    int monitorIoThreadCount = serverconfig::ReadIoThreadCountConfig(
                                   config, "monitoriothreadnum", MonitorServer::kDefaultIoThreadCount);
    int httpIoThreadCount = serverconfig::ReadIoThreadCountConfig(
                                config, "httpiothreadnum", HttpServer::kDefaultIoThreadCount);

    //几个监听地址与端口在函数开头已随其它必需配置项一并取出并校验过，这里直接用
    Singleton<ChatServer>::Instance().init(listenip.c_str(), (short)listenport, &g_mainLoop,
                                           ioThreadCount);

    Singleton<MonitorServer>::Instance().init(monitorlistenip.c_str(), (short)monitorlistenport,
                                              &g_mainLoop, monitortoken.c_str(), monitorIoThreadCount);

    //注意：HTTP 监听用的是 monitorlistenip 而不是 httplistenip，这是既有行为，
    //改动它会影响现网部署的监听地址，须先与使用方确认，故此处照原样保留
    Singleton<HttpServer>::Instance().init(monitorlistenip.c_str(), (short)httplistenport,
                                           &g_mainLoop, httpIoThreadCount);

    //管理控制台长连接（flamingoAdmin 的 FlamingoLink.dll，docs/FlamingoLink方案.md 5.1）。三项都是可选配置：
    //端口缺失、为 0 或越界时不开启（login.do 回 linkport=0，控制台页面提示服务器未开启）；
    //监听地址缺失时监听所有网卡；线程数缺失或非法时回落到 AdminLinkServer::kDefaultIoThreadCount
    {
        //监听地址缺失时的默认值：所有网卡
        const char* const kAdminLinkDefaultListenIp = "0.0.0.0";
        //TCP 端口号的上限
        const int kMaxTcpPort = 65535;

        const char* adminlinkportCfg = config.getConfigName("adminlinklistenport");
        const int adminlinkport = (adminlinkportCfg != NULL) ? atoi(adminlinkportCfg) : 0;
        if (adminlinkport > 0 && adminlinkport <= kMaxTcpPort)
        {
            const char* adminlinkipCfg = config.getConfigName("adminlinklistenip");
            std::string adminlinkip = (adminlinkipCfg != NULL && adminlinkipCfg[0] != '\0')
                                          ? std::string(adminlinkipCfg) : std::string(kAdminLinkDefaultListenIp);
            int adminlinkIoThreadCount = serverconfig::ReadIoThreadCountConfig(
                                             config, "adminlinkiothreadnum", AdminLinkServer::kDefaultIoThreadCount);
            Singleton<AdminLinkServer>::Instance().init(adminlinkip.c_str(), (short)adminlinkport,
                                                        &g_mainLoop, adminlinkIoThreadCount);
        }
        else
        {
            LOGI("adminlink server disabled: adminlinklistenport is missing or invalid.");
        }
    }

    LOGI("chatserver initialization completed, now you can use client to connect it.");

    g_mainLoop.loop();

    //主循环已经退出：停掉到 imgserver 的控制连接（Linux 下 prog_exit 已停过，这里再调一次无害）
    Singleton<CloudRecordLink>::Instance().stop();

    //主循环已经退出：关闭连接池里的空闲连接，让 MySQL 服务端及时回收这些会话，
    //而不是等进程结束后由服务端按连接异常断开处理。
    //这里不再写日志：Linux 下收到退出信号时，prog_exit 已经先调用了 CAsyncLog::uninit()，
    //之后的日志写不出来（下面那行 "exit chatserver." 同样如此）；连接池的统计由
    //MaintainDbConnectionPoolsTick 每 10 分钟记录一次
    DbFactory::clearConnectionPools();

    LOGI("exit chatserver.");

    return 0;
}