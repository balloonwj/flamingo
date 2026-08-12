/**
 *  flamingoAdmin —— admin 控制台接口的业务层。
 *
 *  本文件是「业务层」：把 admin 接口的请求 / 应答与 JSON 细节封装成纯函数，
 *  对 AdminFrame / 各业务屏屏蔽编码细节。它【不做 I/O】—— 真正的 HTTP 往返
 *  交给 HttpWorker（异步层），传输原语在 HttpClient。
 *
 *  组成：
 *    · ApiTag       —— 异步结果标签，随请求带出、随结果带回，供 AdminFrame
 *                      把 WM_APP_API_RESULT 派发到正确的处理逻辑。
 *    · AdminSession —— 进程级单例，保存登录后拿到的会话 token 与管理员信息。
 *    · adminapi::MakeXxxRequest —— 在 UI 线程构造好可直接 Post 给 HttpWorker
 *                      的 HttpRequest（含拼好的 JSON 请求体）。
 *    · adminapi::ParseXxxResponse —— 在 UI 线程解析应答，得到强类型结果。
 *
 *  覆盖接口：认证（login/logout，模块 1）、企业管理（list/create/setstatus/
 *  delete/export，模块 3 的读取与写入部分）。详见 docs/Admin控制台接口文档.md。
 *
 *  balloonwj@qq.com   2026-05-21
 */
#pragma once

#include <atlstr.h>
#include <string>
#include <vector>
#include "Util/AppConfig.h"     // ServerConfig
#include "Util/HttpClient.h"    // HttpResult
#include "Util/HttpWorker.h"    // HttpRequest
#include "Mock/MockData.h"      // Enterprise / EnterpriseStatus（暂复用为领域模型）

namespace flamingoadmin {

// 异步 API 结果标签。构造请求时写入 HttpRequest.m_tag，工作线程把它原样放进
// WM_APP_API_RESULT 的 wParam 回传；AdminFrame::OnApiResult 据此把结果派发到
// 对应的业务屏。后续接入更多接口时在此续加枚举值。
enum ApiTag
{
    kApiTagNone         = 0,   // 无标签（登录 / 登出等不走 OnApiResult 的请求）
    kApiTagEntList      = 1,   // 企业列表       enterprise/list.do
    kApiTagEntCreate    = 2,   // 新增企业       enterprise/create.do
    kApiTagEntSetStatus = 3,   // 企业改状态     enterprise/setstatus.do
    kApiTagEntDelete    = 4,   // 删除企业       enterprise/delete.do
    kApiTagEntExport    = 5,   // 导出企业 CSV   enterprise/export.do
    kApiTagEntDetail    = 6,   // 企业详情       enterprise/detail.do
    kApiTagDeptTree     = 7,   // 组织架构树     dept/tree.do
    kApiTagSvcList      = 8,   // 服务号列表     service/list.do
    kApiTagDeptCreate   = 9,   // 新增部门       dept/create.do
    kApiTagUserCreate   = 10,  // 新增员工       user/create.do
    kApiTagOrgNodeDelete = 11, // 删除部门/员工  dept/delete.do · user/delete.do
    kApiTagOrgTreeReload = 12, // 组织写操作成功后重新拉取组织树
    kApiTagSvcSave        = 13, // 新建/编辑服务号 service/save.do
    kApiTagSvcSetStatus   = 14, // 服务号改状态   service/setstatus.do
    kApiTagSvcDelete      = 15, // 删除服务号     service/delete.do
    kApiTagSvcSetBindings = 16, // 设置服务号绑定 service/setbindings.do
    kApiTagSvcListReload  = 17, // 服务号写操作成功后重新拉取服务号列表
    kApiTagUserGetPerms   = 18, // 取员工权限矩阵 user/getpermissions.do
    kApiTagUserSetRole    = 19, // 设置员工角色   user/setrole.do
    kApiTagUserSetPerms   = 20, // 设置员工权限覆盖 user/setpermissions.do
    kApiTagUserLoginRecords = 21, // 员工登录记录 user/loginrecords.do
    kApiTagUserActivity     = 22, // 员工操作历史 user/activity.do
    kApiTagUserSetStatus    = 23, // 启用/停用员工 user/setstatus.do
    kApiTagUserResetPwd     = 24, // 重置员工密码 user/resetpassword.do
    kApiTagUserKickDevice   = 25, // 强制下线设备 user/kickdevice.do
    kApiTagUserDetail       = 26, // 员工完整资料 user/detail.do
    kApiTagUserUpdate       = 27, // 修改员工资料 user/update.do
    kApiTagUserMove         = 28, // 调整员工部门 user/move.do
    kApiTagEntUpdate        = 29, // 修改企业信息 enterprise/update.do
    kApiTagEntAdmins        = 30, // 企业管理员列表 enterprise/admins.do
    kApiTagEntAddAdmin      = 31, // 新增企业管理员 enterprise/addadmin.do
    kApiTagEntRemoveAdmin   = 32, // 移除企业管理员 enterprise/removeadmin.do
    kApiTagDashStats        = 33, // 仪表盘概览统计 dashboard/stats.do
    kApiTagDashEnterprises  = 34, // 仪表盘「活跃企业」列表（复用 enterprise/list.do）
    kApiTagAuditList        = 35, // 审计日志查询 audit/list.do
    kApiTagAuditExport      = 36, // 审计日志导出 audit/export.do
    kApiTagAuditStats       = 37, // 审计统计概览 audit/stats.do
    kApiTagProfile          = 38, // 取当前管理员资料 profile.do
    kApiTagUpdateProfile    = 39, // 修改自己的资料 updateprofile.do
    kApiTagChangePassword   = 40, // 修改自己的登录密码 changepassword.do
    kApiTagAnnList          = 41, // 公告列表 announcement/list.do
    kApiTagAnnDetail        = 42, // 公告详情 announcement/detail.do
    kApiTagAnnRecall        = 43, // 撤回公告 announcement/recall.do
    kApiTagAnnDelete        = 44, // 删除公告 announcement/delete.do
    kApiTagAnnSetPinned     = 45, // 切换公告置顶 announcement/setpinned.do
    kApiTagAnnSave          = 46, // 保存公告 announcement/save.do
    kApiTagAnnSend          = 47, // 发布公告 announcement/send.do
    kApiTagComposeTree      = 48, // 新建公告弹窗目标选择器拉取的组织树 dept/tree.do
    kApiTagDashAnnouncements = 49, // 仪表盘「最新公告」列表 announcement/list.do
    kApiTagDashAudit         = 50, // 仪表盘「近期审计」列表 audit/list.do
    kApiTagAnnEditFetch      = 51, // 「编辑公告」拉取既有公告详情 announcement/detail.do
    kApiTagEntStats          = 52, // 企业管理界面 5 个统计卡 dashboard/stats.do
    kApiTagDashTrend         = 53, // 仪表盘近 14 天消息量 dashboard/messagetrend.do
    kApiTagUserMsgStats      = 54, // 员工消息与会话统计 user/messagestats.do
    kApiTagGroupCreate       = 55, // 新建群（企业详情页把成员拉进群）group/create.do
    kApiTagCvScanPackages    = 56, // 扫描升级包目录 clientversion/scanpackages.do
    kApiTagCvList            = 57, // 版本记录列表 clientversion/list.do
    kApiTagCvCreate          = 58, // 新建版本记录 clientversion/create.do
    kApiTagCvUpdate          = 59, // 修改版本记录 clientversion/update.do
    kApiTagCvSetEnabled      = 60, // 发布 / 撤回 clientversion/setenabled.do
    kApiTagCvRepublish       = 61, // 回滚（重新发布旧版本）clientversion/republish.do
};

// 当前登录管理员的会话信息（进程级单例）。
// 登录成功后由 AdminFrame 调 Set() 填充，登出时调 Clear() 清空。
class AdminSession
{
public:
    // 取全局唯一实例。
    static AdminSession& Instance();

    // 是否已登录（持有非空 token）。
    bool IsLoggedIn() const { return !m_token.IsEmpty(); }

    // 是否为平台超级管理员。enterpriseid == 0 即平台超管，否则仅管辖单个企业。
    bool IsPlatformAdmin() const { return m_enterpriseId == 0; }

    // 写入登录会话信息。
    //   token：服务端返回的会话令牌，后续所有接口都要带它。
    //   adminId：管理员业务 ID。
    //   name：管理员姓名。
    //   email：管理员登录邮箱。
    //   enterpriseId：管辖企业 ID，0 表示平台超管。
    void Set(const CString& token, long long adminId, const CString& name,
             const CString& email, long long enterpriseId);

    // 清空会话信息（登出 / 会话失效时调用）。
    void Clear();

    // 以下为只读访问器。
    const CString& Token() const { return m_token; }              // 会话令牌
    long long      AdminId() const { return m_adminId; }          // 管理员 ID
    const CString& Name() const { return m_adminName; }           // 管理员姓名
    const CString& Email() const { return m_adminEmail; }         // 管理员邮箱
    long long      EnterpriseId() const { return m_enterpriseId; } // 管辖企业 ID

private:
    AdminSession();
    AdminSession(const AdminSession&) = delete;
    AdminSession& operator=(const AdminSession&) = delete;

    CString   m_token;        // 会话令牌，空串表示未登录
    long long m_adminId;      // 管理员业务 ID
    CString   m_adminName;    // 管理员姓名
    CString   m_adminEmail;   // 管理员登录邮箱
    long long m_enterpriseId; // 管辖企业 ID，0 = 平台超管
};

// 登录应答的解析结果。
struct LoginResult
{
    bool      m_transportOk;   // HTTP 往返是否成功（false 表示网络/服务器不可达）
    int       m_code;          // 业务错误码（仅 m_transportOk 为 true 时有效）
    CString   m_message;       // 失败时给用户看的中文提示
    CString   m_token;         // 会话令牌（成功时有效）
    long long m_adminId;       // 管理员 ID（成功时有效）
    CString   m_adminName;     // 管理员姓名（成功时有效）
    CString   m_adminEmail;    // 管理员邮箱（成功时有效）
    long long m_enterpriseId;  // 管辖企业 ID（成功时有效），0 = 平台超管

    LoginResult()
        : m_transportOk(false), m_code(0), m_adminId(0), m_enterpriseId(0)
    {
    }

    // 登录是否整体成功：网络往返成功且业务错误码为 0。
    bool Succeeded() const { return m_transportOk && m_code == 0; }
};

// 当前管理员资料（profile.do）的解析结果。
struct ProfileResult
{
    bool    m_transportOk;  // HTTP 往返是否成功
    int     m_code;         // 业务错误码
    CString m_message;      // 失败时给用户看的中文提示
    CString m_name;         // 当前管理员姓名
    CString m_email;        // 当前管理员邮箱
    CString m_scope;        // 管辖范围："platform" 平台超管 / "enterprise" 企业级

    ProfileResult()
        : m_transportOk(false), m_code(0)
    {
    }

    bool Succeeded() const { return m_transportOk && m_code == 0; }
};

// 仪表盘概览统计（dashboard/stats.do）的解析结果。
struct DashboardStatsResult
{
    bool    m_transportOk;        // HTTP 往返是否成功
    int     m_code;               // 业务错误码
    CString m_message;            // 失败时给用户看的中文提示
    int     m_enterprisesTotal;   // 企业总数
    int     m_enterprisesActive;  // 活跃企业数
    int     m_enterprisesPending; // 待审核企业数
    int     m_usersTotal;         // 全平台用户总数
    int     m_auditToday;         // 今日审计事件数
    int     m_auditTodayWarn;     // 今日审计中告警 / 高危条数

    DashboardStatsResult()
        : m_transportOk(false), m_code(0), m_enterprisesTotal(0),
          m_enterprisesActive(0), m_enterprisesPending(0), m_usersTotal(0),
          m_auditToday(0), m_auditTodayWarn(0)
    {
    }

    bool Succeeded() const { return m_transportOk && m_code == 0; }
};

// 审计日志列表（audit/list.do）的解析结果。
struct AuditListResult
{
    bool                  m_transportOk;  // HTTP 往返是否成功
    int                   m_code;         // 业务错误码
    CString               m_message;      // 失败时给用户看的中文提示
    int                   m_total;        // 服务端报告的审计记录总数
    std::vector<AuditLog> m_list;         // 审计记录列表（成功时有效）

    AuditListResult()
        : m_transportOk(false), m_code(0), m_total(0)
    {
    }

    bool Succeeded() const { return m_transportOk && m_code == 0; }
};

// 审计统计概览（audit/stats.do）的解析结果。供审计界面 4 个统计卡使用。
struct AuditStatsResult
{
    bool    m_transportOk;  // HTTP 往返是否成功
    int     m_code;         // 业务错误码
    CString m_message;      // 失败时给用户看的中文提示
    int     m_today;        // 今日审计事件数
    int     m_total;        // 全部审计事件数
    int     m_warn;         // 警告级（severity=1）事件数
    int     m_danger;       // 危险级（severity=2）事件数

    AuditStatsResult()
        : m_transportOk(false), m_code(0), m_today(0), m_total(0),
          m_warn(0), m_danger(0)
    {
    }

    bool Succeeded() const { return m_transportOk && m_code == 0; }
};

// 近 14 天消息量趋势（dashboard/messagetrend.do）的解析结果。
struct MessageTrendResult
{
    bool    m_transportOk;  // HTTP 往返是否成功
    int     m_code;         // 业务错误码
    CString m_message;      // 失败时给用户看的中文提示
    std::vector<MessageTrendPoint> m_points;  // 近 14 天每日消息量（旧→新）

    MessageTrendResult()
        : m_transportOk(false), m_code(0)
    {
    }

    bool Succeeded() const { return m_transportOk && m_code == 0; }
};

// 员工消息与会话统计（user/messagestats.do）的解析结果。
struct UserMessageStatsResult
{
    bool    m_transportOk;    // HTTP 往返是否成功
    int     m_code;           // 业务错误码
    CString m_message;        // 失败时给用户看的中文提示
    int     m_sent;           // 该员工发出的消息总数
    int     m_received;       // 该员工收到的单聊消息总数
    int     m_conversations;  // 该员工的单聊会话数
    std::vector<MessageTrendPoint> m_trend;  // 该员工近 14 天每日发送趋势

    UserMessageStatsResult()
        : m_transportOk(false), m_code(0), m_sent(0), m_received(0),
          m_conversations(0)
    {
    }

    bool Succeeded() const { return m_transportOk && m_code == 0; }
};

// 一个公告投放目标（save.do 的 targets 数组元素）。
struct AnnTarget
{
    int       m_type;   // 目标类型：1=企业 2=部门 3=员工
    long long m_id;     // 目标 ID（企业 / 部门 / 用户 ID）

    AnnTarget()
        : m_type(0), m_id(0)
    {
    }

    AnnTarget(int type, long long id)
        : m_type(type), m_id(id)
    {
    }
};

// 公告列表（announcement/list.do）的解析结果。
struct AnnouncementListResult
{
    bool                      m_transportOk;  // HTTP 往返是否成功
    int                       m_code;         // 业务错误码
    CString                   m_message;      // 失败时给用户看的中文提示
    int                       m_total;        // 服务端报告的公告总数
    std::vector<Announcement> m_list;         // 公告列表（成功时有效）

    AnnouncementListResult()
        : m_transportOk(false), m_code(0), m_total(0)
    {
    }

    bool Succeeded() const { return m_transportOk && m_code == 0; }
};

// 公告详情（announcement/detail.do）的解析结果。
struct AnnouncementDetailResult
{
    bool         m_transportOk;   // HTTP 往返是否成功
    int          m_code;          // 业务错误码
    CString      m_message;       // 失败时给用户看的中文提示
    Announcement m_announcement;  // 公告完整信息（含正文、受众总数，成功时有效）
    int          m_severityCode;  // 级别整数（0/1/2，编辑回填弹窗用）
    int          m_audienceType;  // 受众类型整数（0-4，编辑回填弹窗用）
    std::vector<AnnTarget> m_targets;  // 投放目标（编辑回填弹窗用）

    AnnouncementDetailResult()
        : m_transportOk(false), m_code(0), m_severityCode(0), m_audienceType(0)
    {
    }

    bool Succeeded() const { return m_transportOk && m_code == 0; }
};

// 公告保存（announcement/save.do）的解析结果。
struct AnnSaveResult
{
    bool      m_transportOk;    // HTTP 往返是否成功
    int       m_code;           // 业务错误码
    CString   m_message;        // 失败时给用户看的中文提示
    long long m_announcementId; // 保存后服务端返回的公告 ID（新建时为新分配 ID）

    AnnSaveResult()
        : m_transportOk(false), m_code(0), m_announcementId(0)
    {
    }

    bool Succeeded() const { return m_transportOk && m_code == 0; }
};

// 企业列表（list.do）的解析结果。
struct EnterpriseListResult
{
    bool                    m_transportOk;  // HTTP 往返是否成功
    int                     m_code;         // 业务错误码
    CString                 m_message;      // 失败时给用户看的中文提示
    int                     m_total;        // 服务端报告的企业总数
    std::vector<Enterprise> m_list;         // 企业记录列表（成功时有效）

    EnterpriseListResult()
        : m_transportOk(false), m_code(0), m_total(0)
    {
    }

    bool Succeeded() const { return m_transportOk && m_code == 0; }
};

// 新增企业（create.do）的解析结果。
struct EnterpriseCreateResult
{
    bool      m_transportOk;     // HTTP 往返是否成功
    int       m_code;            // 业务错误码
    CString   m_message;         // 失败时给用户看的中文提示
    long long m_enterpriseId;    // 新企业 ID（成功时有效）
    long long m_adminId;         // 新建的企业管理员 ID（成功时有效）
    CString   m_initialPassword; // 新管理员初始密码（成功时有效，须展示给用户）

    EnterpriseCreateResult()
        : m_transportOk(false), m_code(0), m_enterpriseId(0), m_adminId(0)
    {
    }

    bool Succeeded() const { return m_transportOk && m_code == 0; }
};

// 只关心成败的写操作（setstatus.do / delete.do）的解析结果。
struct EnterpriseActionResult
{
    bool    m_transportOk;   // HTTP 往返是否成功
    int     m_code;          // 业务错误码
    CString m_message;       // 失败时给用户看的中文提示

    EnterpriseActionResult()
        : m_transportOk(false), m_code(0)
    {
    }

    bool Succeeded() const { return m_transportOk && m_code == 0; }
};

// 导出企业列表（export.do）的解析结果。应答体是 CSV 文本而非 JSON。
struct EnterpriseExportResult
{
    bool        m_transportOk;  // HTTP 往返是否成功
    int         m_code;         // 业务错误码（成功为 0；失败时服务端回 JSON 错误）
    CString     m_message;      // 失败时给用户看的中文提示
    std::string m_csvUtf8;      // CSV 文本，UTF-8 字节（成功时有效）

    EnterpriseExportResult()
        : m_transportOk(false), m_code(0)
    {
    }

    bool Succeeded() const { return m_transportOk && m_code == 0; }
};

// 企业详情（detail.do）的解析结果。
struct EnterpriseDetailResult
{
    bool       m_transportOk;   // HTTP 往返是否成功
    int        m_code;          // 业务错误码
    CString    m_message;       // 失败时给用户看的中文提示
    Enterprise m_enterprise;    // 企业完整信息（成功时有效）

    EnterpriseDetailResult()
        : m_transportOk(false), m_code(0)
    {
    }

    bool Succeeded() const { return m_transportOk && m_code == 0; }
};

// 组织架构树（dept/tree.do）的解析结果。
struct DeptTreeResult
{
    bool    m_transportOk;   // HTTP 往返是否成功
    int     m_code;          // 业务错误码
    CString m_message;       // 失败时给用户看的中文提示
    // 组织树根节点（成功时有效）。企业已建立公司根部门时，它就是那个真实部门节点，
    // m_id 为真实部门 ID；尚未建立时退回合成节点，m_id 为 "root"。两种情况下
    // m_isCompanyRoot 都为 true。
    OrgNode m_root;

    DeptTreeResult()
        : m_transportOk(false), m_code(0)
    {
    }

    bool Succeeded() const { return m_transportOk && m_code == 0; }
};

// 服务号列表（service/list.do）的解析结果。
struct ServiceListResult
{
    bool                 m_transportOk;   // HTTP 往返是否成功
    int                  m_code;          // 业务错误码
    CString              m_message;       // 失败时给用户看的中文提示
    std::vector<Service> m_list;          // 服务号列表（成功时有效）

    ServiceListResult()
        : m_transportOk(false), m_code(0)
    {
    }

    bool Succeeded() const { return m_transportOk && m_code == 0; }
};

// 新增员工（user/create.do）的解析结果。
struct UserCreateResult
{
    bool      m_transportOk;     // HTTP 往返是否成功
    int       m_code;            // 业务错误码
    CString   m_message;         // 失败时给用户看的中文提示
    long long m_userId;          // 新员工 ID（成功时有效）
    CString   m_loginAccount;    // 新员工登录账号（服务器自动生成，成功时有效，须展示）
    CString   m_initialPassword; // 新员工初始登录密码（成功时有效，须展示）

    UserCreateResult()
        : m_transportOk(false), m_code(0), m_userId(0)
    {
    }

    bool Succeeded() const { return m_transportOk && m_code == 0; }
};

// 新建群（group/create.do）的解析结果。
struct GroupCreateResult
{
    bool      m_transportOk;  // HTTP 往返是否成功
    int       m_code;         // 业务错误码
    CString   m_message;      // 失败时给用户看的中文提示
    long long m_groupId;      // 新群 ID（成功时有效）

    GroupCreateResult()
        : m_transportOk(false), m_code(0), m_groupId(0)
    {
    }

    bool Succeeded() const { return m_transportOk && m_code == 0; }
};

// 权限矩阵里的一项（user/getpermissions.do 的 permissions 数组元素）。
struct PermissionEntry
{
    CString m_key;          // 权限键，如 "msg.send"
    bool    m_effective;    // 该员工对此权限的实际生效值
    bool    m_roleDefault;  // 该员工当前角色对此权限的默认值

    PermissionEntry() : m_effective(false), m_roleDefault(false) {}
};

// 一条权限覆盖项（user/setpermissions.do 的 perms 数组元素）。
struct PermOverride
{
    CString m_key;       // 权限键
    bool    m_granted;   // 覆盖后的开关值

    PermOverride() : m_granted(false) {}
};

// 员工权限矩阵（user/getpermissions.do）的解析结果。
struct UserPermissionsResult
{
    bool                         m_transportOk;  // HTTP 往返是否成功
    int                          m_code;         // 业务错误码
    CString                      m_message;      // 失败时给用户看的中文提示
    int                          m_role;         // 员工角色：0 普通成员 1 部门负责人 2 企业管理员
    std::vector<PermissionEntry> m_items;        // 权限矩阵各项（成功时有效）

    UserPermissionsResult()
        : m_transportOk(false), m_code(0), m_role(0)
    {
    }

    bool Succeeded() const { return m_transportOk && m_code == 0; }
};

// user/loginrecords.do 的 devices 数组元素：一台当前在线设备。
struct LoginDevice
{
    int m_clientType;   // 客户端类型：1=PC，2=移动端
    int m_status;       // 在线状态码

    LoginDevice()
        : m_clientType(0), m_status(0)
    {
    }
};

// user/loginrecords.do 的 records 数组元素：一条历史登录记录。
struct LoginRecord
{
    CString m_ts;          // 登录时间
    CString m_device;      // 设备描述
    CString m_ip;          // 登录 IP
    CString m_location;    // 登录地点
    int     m_result;      // 结果：0=成功，1=失败
    CString m_failReason;  // 失败原因（result=1 时有值）

    LoginRecord()
        : m_result(0)
    {
    }
};

// user/activity.do 的 list 数组元素：一条操作历史。
struct ActivityEntry
{
    CString m_ts;        // 操作时间
    CString m_action;    // 操作类型代码，如 "file.upload"
    CString m_detail;    // 操作详情
    int     m_severity;  // 级别：0=info，1=warn，2=danger

    ActivityEntry()
        : m_severity(0)
    {
    }
};

// 员工登录记录（user/loginrecords.do）的解析结果。
struct UserLoginRecordsResult
{
    bool                     m_transportOk;  // HTTP 往返是否成功
    int                      m_code;         // 业务错误码
    CString                  m_message;      // 失败时给用户看的中文提示
    std::vector<LoginDevice> m_devices;      // 当前在线设备（成功时有效）
    std::vector<LoginRecord> m_records;      // 历史登录记录（成功时有效）

    UserLoginRecordsResult()
        : m_transportOk(false), m_code(0)
    {
    }

    bool Succeeded() const { return m_transportOk && m_code == 0; }
};

// 员工操作历史（user/activity.do）的解析结果。
struct UserActivityResult
{
    bool                       m_transportOk;  // HTTP 往返是否成功
    int                        m_code;         // 业务错误码
    CString                    m_message;      // 失败时给用户看的中文提示
    std::vector<ActivityEntry> m_items;        // 操作历史时间线（成功时有效）

    UserActivityResult()
        : m_transportOk(false), m_code(0)
    {
    }

    bool Succeeded() const { return m_transportOk && m_code == 0; }
};

// 员工完整资料（user/detail.do）的解析结果。
struct UserDetailResult
{
    bool    m_transportOk;   // HTTP 往返是否成功
    int     m_code;          // 业务错误码
    CString m_message;       // 失败时给用户看的中文提示
    OrgNode m_user;          // 员工完整资料（成功时有效，m_kind=User）

    UserDetailResult()
        : m_transportOk(false), m_code(0)
    {
    }

    bool Succeeded() const { return m_transportOk && m_code == 0; }
};

// 重置员工密码（user/resetpassword.do）的解析结果。
struct UserResetPwdResult
{
    bool    m_transportOk;   // HTTP 往返是否成功
    int     m_code;          // 业务错误码
    CString m_message;       // 失败时给用户看的中文提示
    CString m_newPassword;   // 重置后的新密码（成功时有效，须展示给用户）

    UserResetPwdResult()
        : m_transportOk(false), m_code(0)
    {
    }

    bool Succeeded() const { return m_transportOk && m_code == 0; }
};

// 企业管理员列表（enterprise/admins.do）的一项。
struct AdminItem
{
    long long m_adminId;        // 管理员 ID
    CString   m_name;           // 姓名
    CString   m_email;          // 登录邮箱
    int       m_status;         // 状态：0 正常 / 1 停用
    CString   m_lastLoginTime;  // 最近登录时间

    AdminItem()
        : m_adminId(0), m_status(0)
    {
    }
};

// 企业管理员列表（enterprise/admins.do）的解析结果。
struct EnterpriseAdminsResult
{
    bool                   m_transportOk;  // HTTP 往返是否成功
    int                    m_code;         // 业务错误码
    CString                m_message;      // 失败时给用户看的中文提示
    std::vector<AdminItem> m_list;         // 管理员列表（成功时有效）

    EnterpriseAdminsResult()
        : m_transportOk(false), m_code(0)
    {
    }

    bool Succeeded() const { return m_transportOk && m_code == 0; }
};

// 新增企业管理员（enterprise/addadmin.do）的解析结果。
struct EnterpriseAddAdminResult
{
    bool      m_transportOk;     // HTTP 往返是否成功
    int       m_code;            // 业务错误码
    CString   m_message;         // 失败时给用户看的中文提示
    long long m_adminId;         // 新管理员 ID（成功时有效）
    CString   m_initialPassword; // 新管理员初始密码（成功时有效，须展示）

    EnterpriseAddAdminResult()
        : m_transportOk(false), m_code(0), m_adminId(0)
    {
    }

    bool Succeeded() const { return m_transportOk && m_code == 0; }
};

namespace adminapi {

// ---- 认证（模块 1）----

// 构造登录请求（POST /admin/login.do）。
//   server：目标服务器配置。
//   email / password：登录凭据（明文密码仅用于拼请求体，不被留存）。
HttpRequest MakeLoginRequest(const ServerConfig& server,
                             const CString& email, const CString& password);

// 构造登出请求（POST /admin/logout.do）。
//   server：目标服务器配置；token：要注销的会话令牌。
HttpRequest MakeLogoutRequest(const ServerConfig& server, const CString& token);

// 解析登录应答。纯函数、无 I/O，在 UI 线程调用即可。
LoginResult ParseLoginResponse(const HttpResult& http);

// 构造取当前管理员资料请求（POST /admin/profile.do）。
//   server：目标服务器配置；token：会话令牌。
HttpRequest MakeProfileRequest(const ServerConfig& server, const CString& token);

// 解析当前管理员资料应答，得到 ProfileResult。
ProfileResult ParseProfileResponse(const HttpResult& http);

// 构造修改自己资料请求（POST /admin/updateprofile.do）。该接口只接受姓名。
//   name：要改成的新姓名。
// 应答只关心成败，复用 ParseEnterpriseActionResponse。
HttpRequest MakeUpdateProfileRequest(const ServerConfig& server,
                                     const CString& token, const CString& name);

// 构造修改自己登录密码请求（POST /admin/changepassword.do）。
//   oldPassword / newPassword：旧密码与新密码（明文，仅用于拼请求体）。
// 应答只关心成败，复用 ParseEnterpriseActionResponse。
HttpRequest MakeChangePasswordRequest(const ServerConfig& server,
                                      const CString& token,
                                      const CString& oldPassword,
                                      const CString& newPassword);

// ---- 通用 ----

// 从应答里探取业务 code。供 AdminFrame 集中判断会话是否失效（code==2）。
//   http：HttpWorker 回传的 HTTP 结果。
//   返回：应答 JSON 里的 code；传输失败 / 应答非 JSON / 无 code 字段时返回
//         一个负的本地错误码（不会等于任何服务端 error_code）。
int PeekBusinessCode(const HttpResult& http);

// ---- 仪表盘（模块 2）----

// 构造仪表盘概览统计请求（POST /admin/dashboard/stats.do）。
//   server：目标服务器配置；token：会话令牌。
HttpRequest MakeDashboardStatsRequest(const ServerConfig& server,
                                      const CString& token);

// 解析仪表盘概览统计应答，得到 DashboardStatsResult。
DashboardStatsResult ParseDashboardStatsResponse(const HttpResult& http);

// 构造近 14 天消息量请求（POST /admin/dashboard/messagetrend.do）。
//   server：目标服务器配置；token：会话令牌。
HttpRequest MakeMessageTrendRequest(const ServerConfig& server,
                                    const CString& token);

// 解析近 14 天消息量应答，得到 MessageTrendResult。
MessageTrendResult ParseMessageTrendResponse(const HttpResult& http);

// ---- 企业管理（模块 3）----

// 构造企业列表请求（POST /admin/enterprise/list.do）。
//   server：目标服务器配置；token：会话令牌。
//   keyword：搜索词，空串表示不按关键字筛选。
//   status：状态筛选，-1 表示不筛选，否则 0待审核 / 1活跃 / 2停用。
//   page / pagesize：分页参数（客户端筛选模式下 pagesize 取大值一次拉全量）。
HttpRequest MakeEnterpriseListRequest(const ServerConfig& server,
                                      const CString& token,
                                      const CString& keyword, int status,
                                      int page, int pagesize);

// 解析企业列表应答，得到 EnterpriseListResult。
EnterpriseListResult ParseEnterpriseListResponse(const HttpResult& http);

// 构造新增企业请求（POST /admin/enterprise/create.do）。
//   name / shortName / industry / region：企业基本信息。
//   plan：套餐编号（见 docs/Admin控制台接口文档.md，约定 1专业版/2企业版/3旗舰版）。
//   adminName / adminEmail：初始企业管理员姓名与邮箱。
HttpRequest MakeEnterpriseCreateRequest(const ServerConfig& server,
                                        const CString& token,
                                        const CString& name,
                                        const CString& shortName,
                                        const CString& industry,
                                        const CString& region, int plan,
                                        const CString& adminName,
                                        const CString& adminEmail);

// 解析新增企业应答，得到 EnterpriseCreateResult（含初始密码）。
EnterpriseCreateResult ParseEnterpriseCreateResponse(const HttpResult& http);

// 构造企业改状态请求（POST /admin/enterprise/setstatus.do）。
//   enterpriseId：目标企业 ID；status：0待审核 / 1活跃 / 2停用。
HttpRequest MakeEnterpriseSetStatusRequest(const ServerConfig& server,
                                           const CString& token,
                                           long long enterpriseId, int status);

// 构造删除企业请求（POST /admin/enterprise/delete.do）。
//   enterpriseId：目标企业 ID。物理删除，不可恢复。
HttpRequest MakeEnterpriseDeleteRequest(const ServerConfig& server,
                                        const CString& token,
                                        long long enterpriseId);

// 解析只关心成败的写操作应答（setstatus.do / delete.do 共用）。
EnterpriseActionResult ParseEnterpriseActionResponse(const HttpResult& http);

// 构造导出企业列表请求（POST /admin/enterprise/export.do）。
//   keyword / status：与 list.do 同义的筛选条件。
HttpRequest MakeEnterpriseExportRequest(const ServerConfig& server,
                                        const CString& token,
                                        const CString& keyword, int status);

// 解析导出应答。成功时 m_csvUtf8 为 CSV 文本；失败时服务端回 JSON 错误。
EnterpriseExportResult ParseEnterpriseExportResponse(const HttpResult& http);

// ---- 企业详情子屏的读取接口（模块 3 / 4 / 9）----

// 构造企业详情请求（POST /admin/enterprise/detail.do）。
//   enterpriseId：目标企业 ID。
HttpRequest MakeEnterpriseDetailRequest(const ServerConfig& server,
                                        const CString& token,
                                        long long enterpriseId);

// 解析企业详情应答，得到企业完整信息。
EnterpriseDetailResult ParseEnterpriseDetailResponse(const HttpResult& http);

// 构造组织架构树请求（POST /admin/dept/tree.do）。
//   enterpriseId：目标企业 ID。
HttpRequest MakeDeptTreeRequest(const ServerConfig& server,
                                const CString& token, long long enterpriseId);

// 解析组织架构树应答。服务端返回扁平部门数组（每个部门带 members），本函数据
// parentdeptid 组装成 OrgNode 递归树，组装逻辑见 Util/OrgTreeBuilder.h。
//   rootDeptId：该企业的公司根部门 ID，取自企业记录的 m_rootDeptId。树根即这个
//               真实部门节点，员工可以直接挂在它下面。传 0 表示企业尚未建立根部门
//               （存量数据迁移之前），此时退回合成一个 m_id 为 "root" 的根节点。
//   enterpriseName：企业全称，仅在退回合成根节点时用作根节点名称。
DeptTreeResult ParseDeptTreeResponse(const HttpResult& http, long long rootDeptId,
                                     const CString& enterpriseName);

// 构造服务号列表请求（POST /admin/service/list.do）。
//   enterpriseId：目标企业 ID。
HttpRequest MakeServiceListRequest(const ServerConfig& server,
                                   const CString& token,
                                   long long enterpriseId);

// 解析服务号列表应答。
ServiceListResult ParseServiceListResponse(const HttpResult& http);

// ---- 企业详情子屏的写操作（模块 4 部门 / 模块 5 员工）----
// 说明：dept/create、dept/delete、user/delete 只关心成败，应答复用上面的
//       ParseEnterpriseActionResponse 解析；user/create 需取初始密码，单独解析。

// 构造新增部门请求（POST /admin/dept/create.do）。
//   enterpriseId：企业 ID；parentDeptId：父部门 ID（0 表示顶级部门）。
//   deptName：部门名称；deptDesc：部门描述（可空）；
//   leaderUserId：部门负责人用户 ID（0 表示暂不指定）；
//   deptOrder：同级排序值（0 表示由服务端追加到末尾）。
HttpRequest MakeDeptCreateRequest(const ServerConfig& server,
                                  const CString& token, long long enterpriseId,
                                  long long parentDeptId, const CString& deptName,
                                  const CString& deptDesc, long long leaderUserId,
                                  int deptOrder);

// 构造新增员工请求（POST /admin/user/create.do）。
//   enterpriseId：企业 ID；deptId：所属部门 ID。
//   name/title/employeeNo/email/phone/joinDate：员工资料（employeeNo/joinDate 可空）。
HttpRequest MakeUserCreateRequest(const ServerConfig& server,
                                  const CString& token, long long enterpriseId,
                                  long long deptId, const CString& name,
                                  const CString& title, const CString& employeeNo,
                                  const CString& email, const CString& phone,
                                  const CString& joinDate);

// 解析新增员工应答（含 userid 与初始密码）。
UserCreateResult ParseUserCreateResponse(const HttpResult& http);

// 构造新建群请求（POST /admin/group/create.do）。
//   enterpriseId：企业 ID；groupName：群名称（非空）。
//   ownerId：群主用户 ID（必填，须为该企业成员）。
//   adminIds：管理员用户 ID 列表（可空；每个都须出现在 memberIds 内）。
//   memberIds：普通成员用户 ID 列表（不含群主；可含管理员，服务端按角色拆分落库）。
HttpRequest MakeGroupCreateRequest(const ServerConfig& server,
                                   const CString& token, long long enterpriseId,
                                   const CString& groupName, long long ownerId,
                                   const std::vector<long long>& adminIds,
                                   const std::vector<long long>& memberIds);

// 解析新建群应答（含新群 groupid）。
GroupCreateResult ParseGroupCreateResponse(const HttpResult& http);

// 构造删除部门请求（POST /admin/dept/delete.do）。
//   deptId：目标部门 ID。其下若有在用子部门 / 在职成员，服务端回 code=112。
HttpRequest MakeDeptDeleteRequest(const ServerConfig& server,
                                  const CString& token, long long deptId);

// 构造删除员工请求（POST /admin/user/delete.do）。
//   userId：目标员工 ID。
HttpRequest MakeUserDeleteRequest(const ServerConfig& server,
                                  const CString& token, long long userId);

// ---- 企业详情子屏的服务号写操作（模块 9）----
// 说明：以下四个写接口都只关心成败，应答复用 ParseEnterpriseActionResponse。

// 构造新建 / 编辑服务号请求（POST /admin/service/save.do）。
//   serviceId：0 表示新建，非 0 表示编辑该服务号。
//   enterpriseId：所属企业 ID；name/kind/description/avatarColor/avatarGlyph：服务号资料；
//   allowUnsubscribe：是否允许用户取消订阅。
HttpRequest MakeServiceSaveRequest(const ServerConfig& server, const CString& token,
                                   long long serviceId, long long enterpriseId,
                                   const CString& name, ServiceKind kind,
                                   const CString& description, COLORREF avatarColor,
                                   const CString& avatarGlyph,
                                   bool allowUnsubscribe);

// 构造服务号改状态请求（POST /admin/service/setstatus.do）。
//   serviceId：目标服务号 ID；status：0 正常 / 1 停用。
HttpRequest MakeServiceSetStatusRequest(const ServerConfig& server,
                                        const CString& token, long long serviceId,
                                        int status);

// 构造删除服务号请求（POST /admin/service/delete.do）。
HttpRequest MakeServiceDeleteRequest(const ServerConfig& server,
                                     const CString& token, long long serviceId);

// 构造设置服务号绑定范围请求（POST /admin/service/setbindings.do）。
//   serviceId：目标服务号 ID；bindings：新的绑定列表。
HttpRequest MakeServiceSetBindingsRequest(
    const ServerConfig& server, const CString& token, long long serviceId,
    const std::vector<ServiceBinding>& bindings);

// ---- 企业详情子屏的角色与权限（模块 6）----
// 说明：setrole / setpermissions 只关心成败，应答复用 ParseEnterpriseActionResponse。

// 构造取员工权限矩阵请求（POST /admin/user/getpermissions.do）。
HttpRequest MakeUserGetPermissionsRequest(const ServerConfig& server,
                                          const CString& token, long long userId);

// 解析员工权限矩阵应答（含角色与各权限项的生效值 / 角色默认值）。
UserPermissionsResult ParseUserPermissionsResponse(const HttpResult& http);

// 构造设置员工角色请求（POST /admin/user/setrole.do）。
//   role：0 普通成员 / 1 部门负责人 / 2 企业管理员。改角色会清空逐人覆盖。
HttpRequest MakeUserSetRoleRequest(const ServerConfig& server,
                                   const CString& token, long long userId,
                                   int role);

// 构造设置员工权限覆盖请求（POST /admin/user/setpermissions.do）。
//   overrides：与角色默认不同的覆盖项列表。
HttpRequest MakeUserSetPermissionsRequest(
    const ServerConfig& server, const CString& token, long long userId,
    const std::vector<PermOverride>& overrides);

// ---- 企业详情子屏：员工登录记录与操作历史（模块 5 只读部分）----

// 构造员工登录记录请求（POST /admin/user/loginrecords.do）。
//   userId：目标员工 ID；page/pageSize：登录历史分页参数。
HttpRequest MakeUserLoginRecordsRequest(const ServerConfig& server,
                                        const CString& token, long long userId,
                                        int page, int pageSize);

// 解析员工登录记录应答（含当前在线设备与历史登录记录）。
UserLoginRecordsResult ParseUserLoginRecordsResponse(const HttpResult& http);

// 构造员工操作历史请求（POST /admin/user/activity.do）。
//   userId：目标员工 ID。type 固定取 "all"、range 取 "30d"、page 取 1。
HttpRequest MakeUserActivityRequest(const ServerConfig& server,
                                    const CString& token, long long userId);

// 解析员工操作历史应答。
UserActivityResult ParseUserActivityResponse(const HttpResult& http);

// 构造员工消息与会话统计请求（POST /admin/user/messagestats.do）。
//   userId：目标员工的用户 ID。
HttpRequest MakeUserMessageStatsRequest(const ServerConfig& server,
                                        const CString& token, long long userId);

// 解析员工消息与会话统计应答，得到 UserMessageStatsResult。
UserMessageStatsResult ParseUserMessageStatsResponse(const HttpResult& http);

// ---- 企业详情子屏：员工写操作（模块 5 的按钮类操作）----
// 说明：setstatus / kickdevice 只关心成败，应答复用 ParseEnterpriseActionResponse；
//       resetpassword 需取新密码，单独解析。

// 构造启用/停用员工请求（POST /admin/user/setstatus.do）。
//   status：0 启用 / 1 停用。停用后该员工无法登录任何客户端。
HttpRequest MakeUserSetStatusRequest(const ServerConfig& server,
                                     const CString& token, long long userId,
                                     int status);

// 构造重置员工密码请求（POST /admin/user/resetpassword.do）。
HttpRequest MakeUserResetPasswordRequest(const ServerConfig& server,
                                         const CString& token, long long userId);

// 解析重置员工密码应答（含新密码）。
UserResetPwdResult ParseUserResetPasswordResponse(const HttpResult& http);

// 构造强制下线员工设备请求（POST /admin/user/kickdevice.do）。
//   deviceId：要注销的设备 ID；空串表示注销该员工的全部设备。
HttpRequest MakeUserKickDeviceRequest(const ServerConfig& server,
                                      const CString& token, long long userId,
                                      const CString& deviceId);

// 构造员工完整资料请求（POST /admin/user/detail.do）。
HttpRequest MakeUserDetailRequest(const ServerConfig& server,
                                  const CString& token, long long userId);

// 解析员工完整资料应答，得到一个填好各字段的 User 节点。
UserDetailResult ParseUserDetailResponse(const HttpResult& http);

// 构造修改员工资料请求（POST /admin/user/update.do）。
//   员工的姓名 / 职位 / 工号 / 入职日期 / 座机 / 工位 / 手机 / 邮箱 / 签名。
//   应答只关心成败，复用 ParseEnterpriseActionResponse。
HttpRequest MakeUserUpdateRequest(const ServerConfig& server,
                                  const CString& token, long long userId,
                                  const CString& name, const CString& title,
                                  const CString& employeeNo,
                                  const CString& joinDate,
                                  const CString& landline,
                                  const CString& workstation,
                                  const CString& phone, const CString& email,
                                  const CString& signature);

// 构造调整员工所属部门请求（POST /admin/user/move.do）。
//   deptId：员工当前所属部门 ID；newDeptId：要调入的目标部门 ID。
//   应答只关心成败，复用 ParseEnterpriseActionResponse。
HttpRequest MakeUserMoveRequest(const ServerConfig& server,
                                const CString& token, long long userId,
                                long long deptId, long long newDeptId);

// ---- 企业设置（模块 3：改企业信息 + 企业管理员管理）----
// 说明：update / removeadmin 只关心成败，应答复用 ParseEnterpriseActionResponse。

// 构造修改企业信息请求（POST /admin/enterprise/update.do）。
//   name/shortName/industry/region：企业基本信息；plan：套餐编号（1/2/3）。
HttpRequest MakeEnterpriseUpdateRequest(const ServerConfig& server,
                                        const CString& token,
                                        long long enterpriseId,
                                        const CString& name,
                                        const CString& shortName,
                                        const CString& industry,
                                        const CString& region, int plan);

// 构造企业管理员列表请求（POST /admin/enterprise/admins.do）。
HttpRequest MakeEnterpriseAdminsRequest(const ServerConfig& server,
                                        const CString& token,
                                        long long enterpriseId);

// 解析企业管理员列表应答。
EnterpriseAdminsResult ParseEnterpriseAdminsResponse(const HttpResult& http);

// 构造新增企业管理员请求（POST /admin/enterprise/addadmin.do）。
HttpRequest MakeEnterpriseAddAdminRequest(const ServerConfig& server,
                                          const CString& token,
                                          long long enterpriseId,
                                          const CString& name,
                                          const CString& email);

// 解析新增企业管理员应答（含 adminid 与初始密码）。
EnterpriseAddAdminResult ParseEnterpriseAddAdminResponse(const HttpResult& http);

// 构造移除企业管理员请求（POST /admin/enterprise/removeadmin.do）。
HttpRequest MakeEnterpriseRemoveAdminRequest(const ServerConfig& server,
                                             const CString& token,
                                             long long adminId);

// ---- 公告中心（模块 7）----
// 说明：recall / delete / setpinned 只关心成败，应答复用 ParseEnterpriseActionResponse。

// 构造公告列表请求（POST /admin/announcement/list.do）。
//   status：状态筛选，-1 不限 / 0 草稿 / 1 已发布 / 2 已撤回。
//   page / pageSize：分页参数。
HttpRequest MakeAnnListRequest(const ServerConfig& server, const CString& token,
                               int status, int page, int pageSize);

// 解析公告列表应答，得到 AnnouncementListResult。
AnnouncementListResult ParseAnnListResponse(const HttpResult& http);

// 构造公告详情请求（POST /admin/announcement/detail.do）。
//   announcementId：目标公告 ID。
HttpRequest MakeAnnDetailRequest(const ServerConfig& server, const CString& token,
                                 long long announcementId);

// 解析公告详情应答，得到 AnnouncementDetailResult（含正文、受众总数）。
AnnouncementDetailResult ParseAnnDetailResponse(const HttpResult& http);

// 构造撤回公告请求（POST /admin/announcement/recall.do）。仅能撤回已发布公告。
HttpRequest MakeAnnRecallRequest(const ServerConfig& server, const CString& token,
                                 long long announcementId);

// 构造删除公告请求（POST /admin/announcement/delete.do）。
HttpRequest MakeAnnDeleteRequest(const ServerConfig& server, const CString& token,
                                 long long announcementId);

// 构造切换公告置顶请求（POST /admin/announcement/setpinned.do）。
//   pinned：true 置顶 / false 取消置顶。
HttpRequest MakeAnnSetPinnedRequest(const ServerConfig& server, const CString& token,
                                    long long announcementId, bool pinned);

// 构造保存公告请求（POST /admin/announcement/save.do）。新建或编辑。
//   announcementId：0 表示新建，非 0 表示编辑既有公告。
//   severity：级别，0 普通 / 1 提醒 / 2 警告。
//   audienceType：受众类型，0 全部企业 / 1 指定企业 / 2 指定部门 /
//                 3 指定员工 / 4 平台内部。
//   targets：投放目标集（audienceType 为 1/2/3 时有值，否则传空）。
//   pinned：是否置顶。
HttpRequest MakeAnnSaveRequest(const ServerConfig& server, const CString& token,
                               long long announcementId, const CString& title,
                               const CString& body, int severity, int audienceType,
                               const std::vector<AnnTarget>& targets, bool pinned);

// 解析保存公告应答，得到 AnnSaveResult（含服务端返回的公告 ID）。
AnnSaveResult ParseAnnSaveResponse(const HttpResult& http);

// 构造发布公告请求（POST /admin/announcement/send.do）。
//   announcementId：要发布的公告 ID（须先经 save.do 落库）。
//   scheduled：true 定时发布 / false 立即发布。
//   scheduledTime：定时发布时间（scheduled 为 true 时有效，YYYY-MM-DD HH:MM）。
// 应答只关心成败，复用 ParseEnterpriseActionResponse。
HttpRequest MakeAnnSendRequest(const ServerConfig& server, const CString& token,
                               long long announcementId, bool scheduled,
                               const CString& scheduledTime);

// ---- 审计日志（模块 8）----
// 说明：审计数据只读。list.do 走服务端分页 + 服务端筛选；导出应答是 CSV
//       文本，复用 ParseEnterpriseExportResponse。

// 构造审计日志查询请求（POST /admin/audit/list.do）。服务端分页 + 服务端筛选。
//   page / pageSize：分页参数（page 从 1 起）。
//   actorType：操作者类型筛选，-1 不限 / 0 管理员 / 1 系统。
//   severity：级别筛选，-1 不限 / 0 信息 / 1 警告 / 2 危险。
HttpRequest MakeAuditListRequest(const ServerConfig& server,
                                 const CString& token, int page, int pageSize,
                                 int actorType, int severity);

// 解析审计日志查询应答，得到 AuditListResult。
AuditListResult ParseAuditListResponse(const HttpResult& http);

// 构造审计日志导出请求（POST /admin/audit/export.do）。按当前筛选导出。
//   actorType / severity：与 MakeAuditListRequest 同义。
// 应答为 CSV 文本，用 ParseEnterpriseExportResponse 解析。
HttpRequest MakeAuditExportRequest(const ServerConfig& server,
                                   const CString& token,
                                   int actorType, int severity);

// 构造审计统计概览请求（POST /admin/audit/stats.do）。无筛选参数，恒为
// 全局（或本企业）概览，供审计界面顶部 4 个统计卡使用。
HttpRequest MakeAuditStatsRequest(const ServerConfig& server,
                                  const CString& token);

// 解析审计统计概览应答，得到 AuditStatsResult。
AuditStatsResult ParseAuditStatsResponse(const HttpResult& http);


// ---- 客户端版本（模块 10）----
// 说明：升级包由运维手工放进服务器的升级包目录，控制台<u>不负责上传</u>。
//       scanpackages.do 把目录里的候选包连同大小与 MD5 列出来，管理员挑一个即可，
//       这两个值不需要人工输入。整组接口仅平台超管可用。

// 升级包目录里的一个候选文件（scanpackages.do 返回）。
struct UpdatePackageItem
{
    CString   m_fileName;   // 文件名，界面下拉里显示的就是它
    long long m_size;       // 文件字节数
    CString   m_md5;        // 内容 MD5（小写十六进制 32 位），建记录时回传给服务端
    bool      m_isZip;      // 文件头是否为 zip 魔数；false 说明多半放错了文件
    bool      m_used;       // 是否已被某条版本记录引用，避免对同一个包重复建记录

    UpdatePackageItem()
        : m_size(0), m_isZip(false), m_used(false)
    {
    }
};

// 扫描升级包目录（scanpackages.do）的解析结果。
struct ScanPackagesResult
{
    bool                           m_transportOk;  // HTTP 往返是否成功
    int                            m_code;         // 业务错误码
    CString                        m_message;      // 失败时给用户看的中文提示
    CString                        m_dir;          // 服务端的升级包目录，界面上提示"把包放到这里"
    std::vector<UpdatePackageItem> m_list;         // 候选包列表（成功时有效）

    ScanPackagesResult()
        : m_transportOk(false), m_code(0)
    {
    }

    bool Succeeded() const { return m_transportOk && m_code == 0; }
};

// 一条客户端版本记录（list.do 返回）。
struct ClientVersionItem
{
    long long m_id;              // 记录 ID
    int       m_platform;        // 客户端平台：1 Windows PC、2 Android
    CString   m_version;         // 版本号
    CString   m_minVersion;      // 允许登录的最低版本号
    CString   m_releaseNote;     // 更新说明
    CString   m_md5;             // 升级包内容 MD5
    long long m_size;            // 升级包字节数
    CString   m_packageName;     // 升级包原始文件名
    bool      m_forced;          // 是否强制升级：勾了之后客户端不给"稍后"出口、也不放行登录
    int       m_enabled;         // 0 草稿、1 已发布
    long long m_publishTime;     // 发布时间，Unix 毫秒
    long long m_creatorAdminId;  // 发布人管理员 ID
    long long m_createTime;      // 记录创建时间，Unix 毫秒
    bool      m_isCurrent;       // 是否为该平台当前真正生效的那一条

    ClientVersionItem()
        : m_id(0), m_platform(0), m_size(0), m_forced(false), m_enabled(0)
        , m_publishTime(0), m_creatorAdminId(0), m_createTime(0), m_isCurrent(false)
    {
    }
};

// 版本记录列表（list.do）的解析结果。
struct ClientVersionListResult
{
    bool                           m_transportOk;  // HTTP 往返是否成功
    int                            m_code;         // 业务错误码
    CString                        m_message;      // 失败时给用户看的中文提示
    std::vector<ClientVersionItem> m_list;         // 版本记录列表（成功时有效）

    ClientVersionListResult()
        : m_transportOk(false), m_code(0)
    {
    }

    bool Succeeded() const { return m_transportOk && m_code == 0; }
};

// 新建版本记录（create.do）的解析结果，带回服务端分配的记录 ID。
struct ClientVersionCreateResult
{
    bool      m_transportOk;  // HTTP 往返是否成功
    int       m_code;         // 业务错误码
    CString   m_message;      // 失败时给用户看的中文提示
    long long m_id;           // 新记录 ID（成功时有效）

    ClientVersionCreateResult()
        : m_transportOk(false), m_code(0), m_id(0)
    {
    }

    bool Succeeded() const { return m_transportOk && m_code == 0; }
};

// 构造扫描升级包目录请求（POST /admin/clientversion/scanpackages.do）。
// 服务端要把目录里每个文件完整读一遍算 MD5，故超时给得比别的接口宽。
HttpRequest MakeScanPackagesRequest(const ServerConfig& server, const CString& token);

// 解析扫描升级包目录应答，得到 ScanPackagesResult。
ScanPackagesResult ParseScanPackagesResponse(const HttpResult& http);

// 构造版本记录列表请求（POST /admin/clientversion/list.do）。
//   platform：按平台过滤；取 0 表示不过滤，返回全部平台。
HttpRequest MakeClientVersionListRequest(const ServerConfig& server,
                                         const CString& token, int platform);

// 解析版本记录列表应答，得到 ClientVersionListResult。
ClientVersionListResult ParseClientVersionListResponse(const HttpResult& http);

// 构造新建版本记录请求（POST /admin/clientversion/create.do）。
//   platform：客户端平台（1 Windows PC）。
//   version：新版本号，须与客户端内置版本号一致。
//   minVersion：允许登录的最低版本号，低于它的客户端会被强制升级。
//   releaseNote：更新说明，展示在客户端升级窗里。
//   md5：选中的候选包 MD5，来自 scanpackages.do 的结果。
//   forced：是否强制升级。勾上之后客户端的升级窗不给"稍后"出口、也不放行登录，
//           直到装完为止。它与 minVersion 相互独立：后者是版本门槛（低于它连登录都
//           会被服务端拒绝），前者是发布人的主观决定（这个版本必须装）。
//   publish：true 直接发布 / false 先存草稿。
// 包的大小与文件名由服务端按 md5 自行查出，<u>不由本请求携带</u>——两者一旦不一致，
// 表现为客户端下完校验失败，那时很难查出是谁写错的。
HttpRequest MakeClientVersionCreateRequest(const ServerConfig& server, const CString& token,
                                           int platform, const CString& version,
                                           const CString& minVersion,
                                           const CString& releaseNote,
                                           const CString& md5, bool forced, bool publish);

// 解析新建版本记录应答，得到 ClientVersionCreateResult。
ClientVersionCreateResult ParseClientVersionCreateResponse(const HttpResult& http);

// 构造修改版本记录请求（POST /admin/clientversion/update.do）。
//   minVersion：新的最低版本号；传空串表示不改动该项。
//   releaseNote：新的更新说明；空串是合法值，表示清空说明。
//   forced：新的强制升级开关。
// 只能改这两项：版本号与升级包三件套一旦发布就可能已被客户端下载，原地改会让
// 同一个版本号对应两份内容。要换包请新建一条记录。应答复用 ParseEnterpriseActionResponse。
HttpRequest MakeClientVersionUpdateRequest(const ServerConfig& server, const CString& token,
                                           long long id, const CString& minVersion,
                                           const CString& releaseNote, bool forced);

// 构造发布 / 撤回请求（POST /admin/clientversion/setenabled.do）。
//   enabled：true 发布 / false 撤回。
// 应答复用 ParseEnterpriseActionResponse。
HttpRequest MakeClientVersionSetEnabledRequest(const ServerConfig& server, const CString& token,
                                               long long id, bool enabled);

// 构造回滚请求（POST /admin/clientversion/republish.do）：把旧版本重新发布一次，
// 使其立刻成为"最新发布"。数据层与"发布"是同一个动作，分成两个接口是为了让
// 审计日志能区分意图。应答复用 ParseEnterpriseActionResponse。
HttpRequest MakeClientVersionRepublishRequest(const ServerConfig& server,
                                              const CString& token, long long id);

// 把字节数格式化成人能读的大小文本，如 36 -> "36 B"、1536 -> "1.5 KB"、
// 3221225472 -> "3.00 GB"。列表与下拉里显示包大小用。
//   bytes：字节数；负数按 0 处理。
CString FormatPackageSize(long long bytes);

} // namespace adminapi

} // namespace flamingoadmin
