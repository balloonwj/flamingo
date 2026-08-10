/**
 *  控制台管理员管理器，AdminManager.h
 *  balloonwj@qq.com   2026.05.21
 *
 *  负责 admin 控制台的登录鉴权：校验管理员账号密码（MD5+盐）、生成与校验会话
 *  token（t_admin_session）、写控制台操作审计日志（t_admin_audit_log）。
 *  设计参照 UserManager / DepartmentManager。
 */
#ifndef __ADMIN_MANAGER_H__
#define __ADMIN_MANAGER_H__

#include <stdint.h>
#include <string>
#include <vector>
#include <mutex>

//token 有效期（单位：小时）。管理员登录后 token 在此时长内有效，超时需重新登录。
#define ADMIN_TOKEN_VALID_HOURS   12

//审计日志单次导出的最大行数。审计表会持续增长，导出全表可能产生过大的应答，
//故设上限；超过的部分不导出，由管理员收紧时间范围后再导。
#define ADMIN_AUDIT_EXPORT_MAX    10000

//前向声明：数据库连接类，仅在 .cpp 中需要其完整定义。
class IDatabase;

//控制台管理员信息（鉴权通过后返回给处理器使用）。
struct AdminInfo
{
    AdminInfo() : adminid(0), enterpriseid(0), status(0) {}

    int64_t     adminid;       //管理员业务ID
    std::string email;         //登录邮箱
    std::string name;          //管理员姓名
    int64_t     enterpriseid;  //管辖企业ID：0=平台超级管理员，非0=仅管辖该企业
    int32_t     status;        //状态：0=正常 1=停用
};

//默认超级管理员的引导配置。chatserver 启动时据此决定要不要往空库里自动注入一个平台
//超管账号，解决"新部署的库里一个管理员都没有、admin 控制台压根登不进去"的先有鸡还是
//先有蛋问题。四个字段都来自 etc/chatserver.conf，配置项缺失时由 main.cpp 填内置默认值。
struct DefaultAdminConfig
{
    DefaultAdminConfig() : enable(true) {}

    bool        enable;    //是否启用自动注入：true=启用（配置项缺失时的默认值），false=完全不碰数据库
    std::string email;     //默认超管的登录邮箱，落库到 f_email，同时是登录账号
    std::string password;  //默认超管的明文初始口令，落库前转成 MD5(明文 + 盐)，明文不入库
    std::string name;      //默认超管的显示姓名，落库到 f_name
};

//仪表盘概览统计。
struct DashboardStats
{
    DashboardStats()
        : enterprisesTotal(0), enterprisesActive(0), enterprisesPending(0)
        , usersTotal(0), auditToday(0), auditTodayWarn(0)
    {
    }

    int32_t enterprisesTotal;    //企业总数
    int32_t enterprisesActive;   //活跃企业数
    int32_t enterprisesPending;  //待审核企业数
    int32_t usersTotal;          //员工总数
    int32_t auditToday;          //今日审计事件数
    int32_t auditTodayWarn;      //今日审计事件中的告警/危险数
};

//「近 14 天消息量」柱状图的一个数据点。
struct MessageTrendPoint
{
    MessageTrendPoint() : count(0) {}

    std::string date;   //日期，格式 "MM-DD"
    int32_t     count;  //当天消息条数
};

//某员工的消息与会话统计（user/messagestats.do 用）。
struct UserMessageStats
{
    UserMessageStats() : sent(0), received(0), conversations(0) {}

    int32_t sent;           //该员工发出的消息总数
    int32_t received;       //该员工收到的单聊消息总数
    int32_t conversations;  //该员工的单聊会话数（去重对端数）
    std::vector<MessageTrendPoint> trend;  //该员工近 14 天每日发出的消息数
};

//企业管理员列表项（enterprise/admins.do 用）。
struct AdminListItem
{
    AdminListItem() : adminid(0), status(0) {}

    int64_t     adminid;        //管理员业务ID
    std::string name;           //管理员姓名
    std::string email;          //登录邮箱
    int32_t     status;         //状态：0=正常 1=停用
    std::string lastLoginTime;  //末次登录时间
};

//审计日志查询过滤条件（audit/list.do 与 audit/export.do 共用）。
struct AuditQuery
{
    AuditQuery() : actorType(-1), enterpriseId(0), severity(-1) {}

    int32_t     actorType;     //操作者类型过滤：-1=不限，否则 0=管理员 1=系统
    std::string action;        //操作类型前缀过滤：空串=不限（如 "user" 匹配 user.*）
    int64_t     enterpriseId;  //企业过滤：0=不限，否则只取该企业
    int32_t     severity;      //级别过滤：-1=不限，否则 0=info 1=warn 2=danger
    std::string startTime;     //起始时间（含），空串=不限
    std::string endTime;       //结束时间（含），空串=不限
};

//一条审计日志（audit/list.do 应答 list 数组的元素）。
struct AuditLogItem
{
    AuditLogItem() : id(0), actorType(0), enterpriseId(0), severity(0) {}

    int64_t     id;            //日志自增ID
    std::string ts;            //操作发生时间
    int32_t     actorType;     //操作者类型：0=管理员 1=系统
    std::string actorName;     //操作者姓名
    std::string ip;            //操作来源IP
    int64_t     enterpriseId;  //关联企业ID
    std::string action;        //操作类型
    std::string target;        //操作目标描述
    int32_t     severity;      //级别：0=info 1=warn 2=danger
    std::string detail;        //操作详情
};

//控制台管理员管理器（服务器级单例，经 Singleton<AdminManager> 使用）。
class AdminManager final
{
public:
    AdminManager();
    ~AdminManager();

    AdminManager(const AdminManager& rhs) = delete;
    AdminManager& operator=(const AdminManager& rhs) = delete;

    //初始化。连接参数统一由 dbapi/DbConfig 保管，本函数不再收参。管理员数据按请求查询，不做内存缓存。
    bool init();

    /**
     *  确保库里存在一个平台超级管理员，没有就按配置注入一个。chatserver 启动时调一次。
     *
     *  判定口径是「库里有没有任何 f_enterprise_id = 0 的账号」，而不是看默认邮箱在不在。
     *  这样已经导入过 testdata.sql（自带平台超管）的库不会被再多塞一个账号；同时管理员
     *  改过口令之后重启服务，也不会被打回默认口令。
     *
     *  @param cfg        引导配置；cfg.enable 为 false 时立即返回，连数据库连接都不建。
     *  @param outCreated 输出本次是否真的插入了账号：true=新建了；false=跳过了（开关关闭、
     *                    已有超管、参数不合法或写库失败）。
     *  @return 错误码：0 表示流程正常（含"已有超管故跳过"与"开关关闭"）；
     *          101 表示默认邮箱已被别的管理员占用，放弃注入；
     *          1 表示参数不合法或数据库操作失败。
     *          调用方不应因非 0 返回值终止启动 —— 少一个默认账号不影响其它功能。
     */
    int32_t ensureDefaultSuperAdmin(const DefaultAdminConfig& cfg, bool& outCreated);

    /**
     *  管理员登录。
     *  @param email    登录邮箱。
     *  @param password 明文密码。
     *  @param loginIp  登录来源IP（写入会话与审计）。
     *  @param outToken 成功时输出会话 token。
     *  @param outAdmin 成功时输出管理员信息。
     *  @return 错误码：0 成功；103 邮箱或密码错误；2 账号已停用。
     */
    int32_t login(const std::string& email, const std::string& password, const std::string& loginIp,
                  std::string& outToken, AdminInfo& outAdmin);

    //登出：删除该 token 的会话记录，使其立即失效。
    void logout(const std::string& token);

    /**
     *  校验 token 是否有效（存在、未过期、且账号未停用）。
     *  @param token    会话 token。
     *  @param outAdmin 有效时输出对应管理员信息。
     *  @return true 表示 token 有效。
     */
    bool checkToken(const std::string& token, AdminInfo& outAdmin);

    //修改管理员自己的登录密码；oldPassword 校验通过才更新。返回错误码。
    int32_t changePassword(int64_t adminId, const std::string& oldPassword, const std::string& newPassword);

    //修改管理员自己的姓名。返回错误码。
    int32_t updateProfile(int64_t adminId, const std::string& name);

    /**
     *  取仪表盘概览统计。
     *  @param enterpriseId 统计范围：0=全平台口径，非0=仅该企业口径。
     *  @param outStats     输出的统计结果。
     */
    void getDashboardStats(int64_t enterpriseId, DashboardStats& outStats);

    /**
     *  取近 14 天每日消息量（仪表盘柱状图用）。
     *  @param enterpriseId 统计范围：0=全平台，非0=仅该企业员工发出的消息。
     *  @param outTrend     输出 14 个数据点（最旧→今天，缺消息的天 count 为 0）。
     */
    void getMessageTrend(int64_t enterpriseId, std::vector<MessageTrendPoint>& outTrend);

    /**
     *  取某员工的消息与会话统计。
     *  @param userId   目标员工的用户ID。
     *  @param outStats 输出的统计结果（含近 14 天发送趋势）。
     */
    void getUserMessageStats(int64_t userId, UserMessageStats& outStats);

    /**
     *  写一条控制台审计日志（actor 类型固定为管理员）。
     *  @param actorId     操作者管理员ID。
     *  @param actorName   操作者姓名（冗余存储，便于日志直读）。
     *  @param ip          操作来源IP。
     *  @param enterpriseId 关联企业ID，平台级操作传 0。
     *  @param action      操作类型，如 login.success。
     *  @param target      操作目标的文字描述。
     *  @param severity    级别：0=info 1=warn 2=danger。
     *  @param detail      操作详情。
     */
    void writeAudit(int64_t actorId, const std::string& actorName, const std::string& ip,
                    int64_t enterpriseId, const std::string& action, const std::string& target,
                    int32_t severity, const std::string& detail);

    /**
     *  为某企业创建一个管理员账号，自动生成随机初始密码。
     *  @param enterpriseId       管辖企业ID（0=平台超管）。
     *  @param name               管理员姓名。
     *  @param email              登录邮箱（唯一）。
     *  @param outAdminId         成功时输出新管理员ID。
     *  @param outInitialPassword 成功时输出生成的明文初始密码（供控制台展示给用户）。
     *  @return 错误码：0 成功；101 邮箱已被占用。
     */
    int32_t createAdmin(int64_t enterpriseId, const std::string& name, const std::string& email,
                        int64_t& outAdminId, std::string& outInitialPassword);

    //列出某企业的全部管理员（enterpriseId 传 0 列出平台超管）。
    void listAdmins(int64_t enterpriseId, std::vector<AdminListItem>& outList);

    //删除一个管理员账号及其全部会话。返回错误码。
    int32_t removeAdmin(int64_t adminId);

    /**
     *  分页查询控制台审计日志（按操作时间倒序）。
     *  @param query    过滤条件。
     *  @param page     页码，从 1 起。
     *  @param pageSize 每页条数。
     *  @param outTotal 输出满足条件的总条数。
     *  @param outList  输出当前页的审计日志。
     */
    void queryAuditLogs(const AuditQuery& query, int32_t page, int32_t pageSize,
                        int32_t& outTotal, std::vector<AuditLogItem>& outList);

    //把满足过滤条件的审计日志导出为 CSV 文本（最多 ADMIN_AUDIT_EXPORT_MAX 行）。
    void exportAuditLogsCsv(const AuditQuery& query, std::string& outCsv);

    /**
     *  取审计日志统计概览（供控制台审计界面顶部 4 个统计卡使用）。
     *  四项计数用一条 SQL 同时算出，WHERE 仅按企业作用域过滤，不接受其它筛选。
     *  @param enterpriseId 统计范围：0=全平台（统计全表），非0=仅该企业。
     *  @param outToday  输出今日（服务器本地日期 00:00 起）审计事件数。
     *  @param outTotal  输出作用域内全部审计事件数。
     *  @param outWarn   输出作用域内 f_severity=1（warn）的事件数。
     *  @param outDanger 输出作用域内 f_severity=2（danger）的事件数。
     */
    void getAuditStats(int32_t enterpriseId, int32_t& outToday, int32_t& outTotal,
                       int32_t& outWarn, int32_t& outDanger);

private:
    //创建一个已 initialize 的数据库连接；失败返回 NULL，成功时所有权归调用方。
    IDatabase* createDbConnection();

    //按邮箱查管理员；查到则填充 outAdmin 及密码哈希、盐值，返回 true。
    bool queryAdminByEmail(IDatabase* pConn, const std::string& email,
                           AdminInfo& outAdmin, std::string& outPwdHash, std::string& outSalt);

    //执行一条返回单个计数值的 SQL（形如 SELECT COUNT(*) ...）；查询失败返回 0。
    int32_t queryCount(IDatabase* pConn, const char* sql);

    //据 AuditQuery 构造审计日志查询的 WHERE 子句（含各过滤项），无过滤时返回空串。
    std::string buildAuditWhere(const AuditQuery& query);

private:
    std::mutex  m_mutex;          //串行化登录/改密等写操作
};

#endif // __ADMIN_MANAGER_H__
