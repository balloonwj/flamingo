/** 
 *  服务器主服务类，IMServer.h
 *  zhangyl 2017.03.09
 **/
#pragma once
#include <memory>
#include <list>
#include <map>
#include <vector>
#include <mutex>
#include <atomic>
#include "../net/TcpServer.h"
#include "../net/EventLoop.h"
#include "ChatSession.h"

using namespace net;

enum CLIENT_TYPE
{
    CLIENT_TYPE_UNKOWN,
    CLIENT_TYPE_PC,
    CLIENT_TYPE_ANDROID,
    CLIENT_TYPE_IOS,
    CLIENT_TYPE_MAC
};

struct StoredUserInfo
{
    int32_t         userid;
    std::string     username;
    std::string     password;
    std::string     nickname;
};

//admin 控制台建群的整体编排结果。与对外 HTTP 包体的 code 解耦：HttpSession 据此映射成
//AdminApiCodes.h 里的对外错误码，从而 HttpSession 无需引入会与 AdminApiCodes.h 冲突的 Protocol.h。
enum class AdminCreateGroupOutcome
{
    Ok                    = 0,   //建群成功
    BadParams             = 1,   //包体解析失败 / 群名为空 / 群主缺失 / 管理员不在成员集内
    OwnerNotInEnterprise  = 2,   //群主不属于目标企业（跨租户）
    AdminNotInEnterprise  = 3,   //某管理员不属于目标企业（跨租户）
    MemberNotInEnterprise = 4,   //某普通成员不属于目标企业（跨租户）
    DbFail                = 5,   //落库失败
};

class ChatServer final
{
public:
    ChatServer();
    ~ChatServer() = default;

    ChatServer(const ChatServer& rhs) = delete;
    ChatServer& operator =(const ChatServer& rhs) = delete;

    //客户端连接所用网络 I/O 工作线程数的默认值，单位：条线程。取 6 是为了与本项改造前
    //写死在 ChatServer::init 里的那个数完全一致，从而不改配置文件的部署行为不变。
    //配置项 iothreadnum 缺失或取值非法时回落到这里，见 utils/ServerConfigLoader.h
    static const int kDefaultIoThreadCount = 6;

    /**
     *  启动客户端连接的侦听。
     *  @param ip             监听地址，取自配置项 listenip
     *  @param port           监听端口，取自配置项 listenport
     *  @param loop           主事件循环，本类只使用不接管其生命周期
     *  @param ioThreadCount  网络 I/O 工作线程数，单位：条线程，取值范围见
     *                        serverconfig::MIN_IO_THREAD_COUNT / MAX_IO_THREAD_COUNT。
     *                        调用方须先用 serverconfig::ReadIoThreadCountConfig 把配置里
     *                        的非法取值挡掉，本函数不再校验
     *  @return 恒返回 true
     */
    bool init(const char* ip, short port, EventLoop* loop, int ioThreadCount);
    void uninit();

    void enableLogPackageBinary(bool enable);
    bool isLogPackageBinaryEnabled();

    void getSessions(std::list<std::shared_ptr<ChatSession>>& sessions);
    //用户id和clienttype会唯一确定一个session
    bool getSessionByUserIdAndClientType(std::shared_ptr<ChatSession>& session, int32_t userid, int32_t clientType);

    bool getSessionsByUserId(std::list<std::shared_ptr<ChatSession>>& sessions, int32_t userid);

    /**
     *  向指定企业的全部在线 IM 客户端推送组织架构变更通知（cmd_org_structure_notify）。
     *  供 Admin 控制台的部门/成员写接口在改动成功后调用，使在线客户端刷新通讯录。
     *  @param enterpriseId 发生变更的企业ID；为 0 时不推送
     *  @param changetype   变更类型，取值见 dept::org_change_type
     *  @param deptid       受影响部门ID，仅作提示
     */
    void broadcastOrgStructureChanged(int64_t enterpriseId, int32_t changetype, int64_t deptid);

    /**
     *  把某企业的全部在线会话踢下线。供管理控制台「停用企业」「删除企业」调用，
     *  使这两个操作即时生效，而不是等这些人自己退出。
     *
     *  实现上<u>先发 cmd_kickout 再置会话失效，不强制关闭连接</u>，与 onLogin 里
     *  顶号踢旧会话的做法一致。这一点很关键：若直接 forceClose，客户端只会认为
     *  网络断了并启动自动重连，而不会弹下线提示、也不会退回登录窗；收到
     *  cmd_kickout 它才知道是被服务器踢的，从而停掉重连、提示用户、回到登录界面。
     *
     *  <u>删除企业时必须在删库之前调用</u>：本方法靠 DepartmentManager::getUserEnterpriseId
     *  把在线会话归到企业上，而删企业会把部门数据连同那份缓存一起清掉，删完再调
     *  一个人都匹配不到。这与 recallAnnouncementAndNotify「先筛受众再撤回」是同一类
     *  顺序约束。
     *
     *  @param enterpriseId 目标企业ID；为 0 时直接返回（0 是「无企业归属」的哨兵值，
     *                      真按它踢会把所有未归属用户全踢下线）
     *  @param reason       下发给客户端的被踢原因，取值见 enum kickout_reason；
     *                      本方法的两个场景分别用 kickout_reason_enterprise_disabled
     *                      与 kickout_reason_enterprise_deleted
     *  @return 实际踢下线的会话数，供调用方记日志
     */
    int32_t kickEnterpriseSessions(int64_t enterpriseId, int32_t reason);

    /**
     *  把某个账号的在线会话踢下线。供管理控制台「停用员工」「删除员工」「强制注销
     *  设备」调用。
     *
     *  与 kickEnterpriseSessions 同理：<u>先发 cmd_kickout 再置会话失效，不强制关闭
     *  连接</u>。直接 forceClose 的话客户端只当是网络断了并自动重连，看不到下线提示、
     *  也不会退回登录窗。
     *
     *  本方法放在 ChatServer 而非 HttpSession：组 cmd_kickout 报文要用 Protocol.h 的
     *  cmd 与 reason 取值，而 HttpSession 属 AdminApiCodes.h 世界、引不进 Protocol.h（两者枚举
     *  冲突），故由本方法居中编排，与 broadcastAnnouncementPublished 同一理由。
     *
     *  @param userId           目标账号userid
     *  @param clientTypeFilter 只踢该客户端类型的设备（见 enum platform_type）；
     *                          <=0 表示该账号的全部设备
     *  @param reason           下发给客户端的被踢原因，取值见 enum kickout_reason
     *  @return 实际踢下线的会话数，供调用方记日志 / 回给控制台
     */
    int32_t kickUserSessions(int64_t userId, int32_t clientTypeFilter, int32_t reason);

    /**
     *  向指定企业的全部在线 IM 客户端广播一条通知（按企业范围，而非好友范围）。
     *  用于"客户端联系人树是企业组织架构（同事），而非好友列表"的场景——典型是
     *  在线状态变更（cmd_presence_changed_notify），让同企业的在线用户都能实时刷新该
     *  联系人的状态点。实现与 broadcastOrgStructureChanged 同构（遍历在线会话、按
     *  getUserEnterpriseId 过滤同企业）。
     *  @param enterpriseId  目标企业ID；为 0 时不推送
     *  @param cmd           要推送的消息类型，取值见 enum msg_type
     *  @param body          已序列化好的消息包体（UTF-8 JSON）
     *  @param excludeUserId 不推送的 userid（通常为引发本次变更的用户本身；为 0 表示不排除）
     */
    void broadcastToEnterprise(int64_t enterpriseId, uint16_t cmd,
                               const std::string& body, int32_t excludeUserId);

    /**
     *  向某账号的其它在线设备推送设备上/下线通知（cmd_device_changed_notify）。
     *  供登录 / 登出 / 踢设备等场景调用，使该账号其它设备的设备列表保持同步。
     *  @param userid           发生设备变更的账号userid
     *  @param changetype       变更类型，取值见 enum device_change_type
     *  @param clienttype       发生上/下线的设备客户端类型
     *  @param excludeSessionId 不推送的会话ID（通常为引发本次变更的会话本身）
     */
    void broadcastDeviceChanged(int32_t userid, int32_t changetype,
                                int32_t clienttype, int32_t excludeSessionId);

    /**
     *  向一组指定 userid 的全部在线会话精确推送一条消息。
     *  供 Admin 控制台建群等"只影响名单内成员"的写操作在成功后调用，
     *  使在线的群成员立即收到 cmd_group_member_changed_notify 并刷新群列表。
     *  与 broadcastOrgStructureChanged 的区别：后者按企业全员广播，本方法只推给名单内的 userid。
     *  @param userids 目标成员userid列表（空列表直接返回，不做任何事）
     *  @param msgType 要推送的消息类型，取值见 enum msg_type
     *  @param body    已序列化好的消息包体（UTF-8 JSON）
     */
    void pushToUsers(const std::vector<int64_t>& userids, int32_t msgType, const std::string& body);

    /**
     *  重算一个群的自动群名；群名确实改变时，向该群全体在线成员推送
     *  cmd_group_info_changed_notify，客户端据此刷新会话列表与聊天窗标题。
     *  群名由用户设定（t_group.f_name_custom = 1）、或重算结果与原名相同时不做任何事。
     *  供邀请入群、审批入群、退群、踢人、转让群主等改变成员或群主的操作在成功后调用。
     *  @param groupid 目标群ID
     *  @return 群名改变并已推送返回 true
     */
    bool refreshAutoGroupNameAndNotify(int64_t groupid);

    /**
     *  某用户的昵称改变后，重算他所在的全部自动命名群，群名改变的逐群向在线成员推送
     *  cmd_group_info_changed_notify。
     *  供客户端修改个人资料（ChatSession）与管理后台修改员工信息（HttpSession）两处调用；
     *  昵称其实没变时调用也无害，只是多几次查询。
     *  @param userid 昵称改变的用户userid
     */
    void refreshAutoGroupNamesOfMemberAndNotify(int32_t userid);

    /**
     *  把一条平台公告的「新发布」通知（cmd_announcement_notify）推给它的在线受众。
     *
     *  管理员点「立即发布」、以及后台调度器把定时公告转为已发布之后调用。只推在线用户
     *  ——离线的不必操心，他们下次登录时客户端会主动发 cmd_get_announcements 拉一遍。
     *
     *  本方法之所以放在 ChatServer 而不是 AnnouncementManager：推送要同时用到「在线会话
     *  表」（ChatServer 持有）与「受众匹配」（AnnouncementManager 负责），而 Manager 不
     *  应该反向依赖 ChatServer。发起方 HttpSession 属 AdminApiCodes.h 世界、引不进 Protocol.h，
     *  也没法自己组这个 v2 报文，所以由本方法居中编排。
     *
     *  @param announcementId 公告ID；公告不存在、非「已发布」状态、或没有在线受众时静默返回
     */
    void broadcastAnnouncementPublished(int64_t announcementId);

    /**
     *  撤回一条平台公告，并把「已撤回」通知推给它的在线受众。
     *
     *  <u>撤回与推送必须一起做</u>，所以包成一个方法：受众筛选只认「已发布」的公告，而
     *  撤回会把状态改成「已撤回」——先撤回再筛的话一个受众都筛不到，通知就发不出去了。
     *  本方法保证先筛后撤，且撤回失败时不推送。
     *
     *  @param announcementId 公告ID
     *  @return AnnouncementManager::recallAnnouncement 的错误码（按 AdminApiCodes.h 的 error_code
     *          语义，0 表示成功），供 HttpSession 直接回给管理控制台
     */
    int32_t recallAnnouncementAndNotify(int64_t announcementId);

    /**
     *  admin 控制台建群的整体编排：解析包体 -> 校验并规整角色（群主/管理员/普通成员）
     *  -> 逐一校验成员的企业归属 -> 落库 -> 给在线成员推送 cmd_group_member_changed_notify，
     *  使其立即看到新群。本方法在 ChatServer.cpp（Protocol.h 世界）实现，让 HttpSession
     *  （AdminApiCodes.h 世界）无需引入与 AdminApiCodes.h 冲突的 Protocol.h 即可复用 v2 群协议与群存储。
     *  @param enterpriseId 目标企业ID（调用方已据 token 解析、并裁决平台超管 / 企业管理员）
     *  @param jsonBody     /admin/group/create.do 的原始请求包体（UTF-8 JSON）
     *  @param outGroupId   [出参] 成功时为新群ID，否则为 0
     *  @return 见 AdminCreateGroupOutcome
     */
    AdminCreateGroupOutcome adminCreateGroup(int64_t enterpriseId, const std::string& jsonBody,
                                             int64_t& outGroupId);

    //获取用户状态，若该用户不存在，则返回0
    int32_t getUserStatusByUserId(int32_t userid);
    //获取用户客户端类型，如果该用户不存在，则返回0
    int32_t getUserClientTypeByUserId(int32_t userid);

private:
    //新连接到来调用或连接断开，所以需要通过conn->connected()来判断，一般只在主loop里面调用
    void onConnected(std::shared_ptr<TcpConnection> conn);
    //连接断开
    void onDisconnected(const std::shared_ptr<TcpConnection>& conn);

    /**
     *  收集当前全部在线用户的 userid，已去重（同一账号多设备登录会有多个会话）。
     *  供平台公告推送把「在线用户」作为受众筛选的候选集。
     *  @param outUserIds [出参] 在线用户ID列表，先清空再填充；无人在线时为空
     */
    void collectOnlineUserIds(std::vector<int32_t>& outUserIds);
   
private:
    std::unique_ptr<TcpServer>                     m_server;
    std::list<std::shared_ptr<ChatSession>>        m_sessions;
    std::mutex                                     m_sessionMutex;      //多线程之间保护m_sessions
    std::atomic_int                                m_sessionId{};
    std::mutex                                     m_idMutex;           //多线程之间保护m_baseUserId
    std::atomic_bool                               m_logPackageBinary;  //是否日志打印出包的二进制数据
};
