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
    kApiTagSwList            = 61, // 敏感词库列表 sensitive/list.do
    kApiTagSwAdd             = 62, // 添加敏感词 sensitive/add.do
    kApiTagSwUpdate          = 63, // 修改敏感词 sensitive/update.do
    kApiTagSwDelete          = 64, // 删除敏感词（可批量）sensitive/delete.do
    kApiTagSwImport          = 65, // 批量导入的一批 sensitive/import.do
    kApiTagSwExport          = 66, // 导出全部敏感词 sensitive/export.do
    kApiTagSwSetEnabled      = 67, // 敏感词总开关 sensitive/setenabled.do
    kApiTagSwHits            = 68, // 拦截记录列表 sensitive/hits.do
    kApiTagSwEnterprises     = 69, // 敏感词界面的企业选择框拉取的企业列表 enterprise/list.do
    kApiTagFeatureGet        = 70, // 企业详情「功能开关」页里「文件共享」的面板读企业的功能设置 setting/get.do
    kApiTagFeatureSet        = 71, // 企业详情「功能开关」页里「文件共享」的面板改企业的功能设置 setting/set.do
    // 72、73、82、83、105、106、122、123、126、127、130、131、134、135、138、139、142、143 已作废，不得复用：原为平台设置页
    // 「功能开关」卡（企业管理员设置本企业）各面板读 / 改设置的标签，2026-10-08 该卡删除，功能开关集中到企业详情「功能开关」页
    kApiTagFsList            = 74, // 文件共享列表 fileshare/list.do
    kApiTagFsMembers         = 75, // 选中共享的成员与权限 fileshare/members.do
    kApiTagFsCancel          = 76, // 强制取消共享 fileshare/cancel.do
    kApiTagFsEnterprises     = 77, // 文件共享页的企业筛选框拉取的企业列表 enterprise/list.do
    kApiTagFsAudit           = 78, // 共享审计列表 fileshare/audit.do
    kApiTagFsAuditEnterprises = 79, // 共享审计页的企业筛选框拉取的企业列表 enterprise/list.do
    kApiTagP2PSwitchGet      = 80, // 企业详情「功能开关」页里点对点直连的面板读企业的功能设置 setting/get.do
    kApiTagP2PSwitchSet      = 81, // 企业详情「功能开关」页里点对点直连的面板改企业的功能设置 setting/set.do
    kApiTagSvcDetail         = 84, // 服务号面板选中某个服务号后拉取它的绑定范围 service/detail.do
    kApiTagSvcDetailForBinding = 85, // 点「修改绑定」后先拉取当前绑定，成功才打开绑定弹窗 service/detail.do
    kApiTagInviteConfirmGet  = 86, // 企业详情「功能开关」页里「邀请入群需被邀请人同意」的面板读企业的功能设置 setting/get.do
    kApiTagInviteConfirmSet  = 87, // 企业详情「功能开关」页里「邀请入群需被邀请人同意」的面板改企业的功能设置 setting/set.do
    // 100～119：控制台第二批完善（外壳刷新、公告重新推送、企业列表更多筛选等）使用，与上面按顺序追加的编号错开。
    kApiTagTickerAudit       = 100, // 顶栏「最新审计」滚动提示单独拉取的最近几条审计 audit/list.do
    kApiTagAnnCopyFetch      = 101, // 公告中心「复制」先拉取原公告详情，再按其内容打开新建公告 announcement/detail.do
    kApiTagAnnRepush         = 102, // 公告中心「重新推送」announcement/repush.do
    kApiTagRegisterSwitchGet    = 103, // 企业详情「功能开关」页里「允许用户自助注册」的面板读企业的功能设置 setting/get.do
    kApiTagRegisterSwitchSet    = 104, // 企业详情「功能开关」页里「允许用户自助注册」的面板改企业的功能设置 setting/set.do
    // 120～123：群语音 / 群视频通话开关（docs/群音视频通话方案.md 第 6 期），避开上面 100～119 的号段。
    kApiTagGroupCallSwitchGet    = 120, // 企业详情「功能开关」页里「群语音 / 群视频通话」的面板读企业的功能设置 setting/get.do
    kApiTagGroupCallSwitchSet    = 121, // 企业详情「功能开关」页里「群语音 / 群视频通话」的面板改企业的功能设置 setting/set.do
    // 124～127：语音字幕开关（docs/语音字幕方案.md 第 10 节）。
    kApiTagCaptionSwitchGet    = 124, // 企业详情「功能开关」页里「语音字幕」的面板读企业的功能设置 setting/get.do
    kApiTagCaptionSwitchSet    = 125, // 企业详情「功能开关」页里「语音字幕」的面板改企业的功能设置 setting/set.do
    // 128～135：云端录制（docs/屏幕共享与录制方案.md 12.6.5，第 27 期 27g）：开关一组，配额与保留天数一组。
    kApiTagCloudRecordSwitchGet    = 128, // 企业详情「功能开关」页里「云端录制」的面板读企业的功能设置 setting/get.do
    kApiTagCloudRecordSwitchSet    = 129, // 企业详情「功能开关」页里「云端录制」的面板改企业的功能设置 setting/set.do
    kApiTagCloudQuotaGet    = 132,        // 企业详情「功能开关」页的「云端录像空间」面板读配额、保留天数与已用空间 setting/get.do
    kApiTagCloudQuotaSet    = 133,        // 企业详情「功能开关」页的「云端录像空间」面板改配额或保留天数 setting/set.do（一次保存可能发两个）
    // 136～143：单聊密聊开关（docs/密聊方案.md 4.9）一组，帖子群开关（docs/帖子群方案.md 4.7）一组。
    kApiTagSecretChatSwitchGet     = 136, // 企业详情「功能开关」页里「单聊密聊」的面板读企业的功能设置 setting/get.do
    kApiTagSecretChatSwitchSet     = 137, // 企业详情「功能开关」页里「单聊密聊」的面板改企业的功能设置 setting/set.do
    kApiTagThreadGroupSwitchGet    = 140, // 企业详情「功能开关」页里「帖子群」的面板读企业的功能设置 setting/get.do
    kApiTagThreadGroupSwitchSet    = 141, // 企业详情「功能开关」页里「帖子群」的面板改企业的功能设置 setting/set.do
    // 160～165：企业问答页（docs/群问答方案.md，Util/QaAdminApi.h），与上面按顺序追加的编号错开
    kApiTagQaList            = 160, // 企业问答列表 qa/list.do
    kApiTagQaSave            = 161, // 新增或修改企业问答 qa/add.do、qa/update.do
    kApiTagQaDelete          = 162, // 删除企业问答 qa/delete.do
    kApiTagQaGroups          = 163, // 本企业的群（选择应用范围、显示群名）qa/groups.do
    kApiTagQaEnterprises     = 164, // 企业问答页的企业选择框拉取的企业列表 enterprise/list.do
    // 170～180：机器人页（docs/机器人方案.md 第 7 节，Util/BotAdminApi.h）
    kApiTagBotList           = 170, // 企业机器人列表 bot/list.do
    kApiTagBotDetail         = 171, // 企业机器人详情（点「编辑」后拉取，带规则与密钥）bot/detail.do
    kApiTagBotSave           = 172, // 新建或修改企业机器人 bot/save.do
    kApiTagBotDelete         = 173, // 删除企业机器人 bot/delete.do
    kApiTagBotSetStatus      = 174, // 停用 / 启用企业机器人 bot/setstatus.do
    kApiTagBotGroups         = 175, // 本企业的群（应用范围、定时目标群、「试一试」的群）bot/groups.do
    kApiTagBotTry            = 176, // 编辑窗里的「试一试」bot/try.do
    kApiTagBotResetToken     = 177, // 重新生成 Webhook 令牌与签名密钥 bot/resettoken.do
    kApiTagBotDomains        = 178, // 外部服务的域名白名单 bot/domains.do
    kApiTagBotSetDomains     = 179, // 修改外部服务的域名白名单 bot/setdomains.do
    kApiTagBotEnterprises    = 180, // 机器人页的企业选择框拉取的企业列表 enterprise/list.do
    // 181～196：机器人的四个开关（docs/机器人方案.md 4.1），每个开关原有四个标签：企业设置弹窗读 / 改，平台设置页读 / 改；
    // 2026-10-08 起只剩企业详情「功能开关」页的读 / 改，183、184、187、188、191、192、195、196 作废，不得复用
    kApiTagBotSwitchGet               = 181, // 企业详情「功能开关」页里「机器人」的面板读企业的功能设置 setting/get.do
    kApiTagBotSwitchSet               = 182, // 企业详情「功能开关」页里「机器人」的面板改企业的功能设置 setting/set.do
    kApiTagBotGroupSwitchGet          = 185, // 企业详情「功能开关」页里「允许群主添加群机器人」的面板读企业的功能设置
    kApiTagBotGroupSwitchSet          = 186, // 企业详情「功能开关」页里「允许群主添加群机器人」的面板改企业的功能设置
    kApiTagBotPersonalSwitchGet       = 189, // 企业详情「功能开关」页里「允许员工配置个人机器人」的面板读企业的功能设置
    kApiTagBotPersonalSwitchSet       = 190, // 企业详情「功能开关」页里「允许员工配置个人机器人」的面板改企业的功能设置
    kApiTagBotExternalSwitchGet       = 193, // 企业详情「功能开关」页里「允许群主给群机器人配置外部服务」的面板读企业的功能设置
    kApiTagBotExternalSwitchSet       = 194, // 企业详情「功能开关」页里「允许群主给群机器人配置外部服务」的面板改企业的功能设置
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
    int       m_linkPort;      // 管理端口（FlamingoLink 长连接，成功时有效）；0 表示服务器未开启或未下发
    std::string m_linkPubkey;  // 服务端身份公钥的 base64（成功时有效）；空串表示服务器未开启加密

    LoginResult()
        : m_transportOk(false), m_code(0), m_adminId(0), m_enterpriseId(0), m_linkPort(0)
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
    // 定时发送时间（服务端原文，如 "2026-10-05 09:30:00"；未设定时为空）。编辑一条尚未
    // 到点的定时公告时据此回填弹窗，否则提交会按「立即发布」处理。
    CString      m_scheduledTime;

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
// 企业列表与导出的筛选条件：搜索框、状态分段与「更多筛选」条合在一起（enterprise/list.do、
// export.do）。行业、套餐、开通日期三项未设置时不出现在请求里。
struct EnterpriseListFilter
{
    CString m_keyword;   // 搜索词；空串表示不按关键字筛选
    int     m_status;    // 状态 0 待审核 / 1 活跃 / 2 停用；-1 表示不筛选
    CString m_industry;  // 行业（服务端精确匹配）；空串表示不限
    int     m_plan;      // 套餐编号（0 基础版 / 1 专业版 / 2 旗舰版）；-1 表示不限
    CString m_openFrom;  // 开通日期下限「YYYY-MM-DD」，当天包含在内；空串或格式不对时不带
    CString m_openTo;    // 开通日期上限「YYYY-MM-DD」，当天包含在内；空串或格式不对时不带

    EnterpriseListFilter()
        : m_status(-1), m_plan(-1)
    {
    }
};

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

// 服务端返回的一条原始绑定（service/detail.do 的 bindings 项）：只有类型与目标 ID，
// 显示名要再按组织树查（见 ResolveServiceBindings）。
struct RawServiceBinding
{
    int       m_bindType;   // 绑定类型：0 本企业全员 / 1 部门 / 2 员工（接口文档 9.5）
    long long m_targetId;   // 目标 ID：部门 ID 或员工 ID；全员时为 0

    RawServiceBinding()
        : m_bindType(0), m_targetId(0)
    {
    }
};

// 服务号详情（service/detail.do）的解析结果。
struct ServiceDetailResult
{
    bool                           m_transportOk;  // HTTP 往返是否成功
    int                            m_code;         // 业务错误码
    CString                        m_message;      // 失败时给用户看的中文提示
    Service                        m_service;      // 服务号资料（成功时有效；m_bindings 留空，见下）
    std::vector<RawServiceBinding> m_bindings;     // 原始绑定列表（成功时有效）

    ServiceDetailResult()
        : m_transportOk(false), m_code(0)
    {
    }

    bool Succeeded() const { return m_transportOk && m_code == 0; }
};

// 重新推送公告（announcement/repush.do）的解析结果。
struct AnnRepushResult
{
    bool    m_transportOk;  // HTTP 往返是否成功
    int     m_code;         // 业务错误码（140 表示公告不是已发布状态）
    CString m_message;      // 失败时给用户看的中文提示
    int     m_pushed;       // 实际推送的人数（受众里在线且未读的成员数），成功时有效

    AnnRepushResult()
        : m_transportOk(false), m_code(0), m_pushed(0)
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
    int     m_kicked;        // 因密码重置而被强制下线的会话数（应答的 kicked 字段，2026-10-07 起展示；老服务端不带时为 0）

    UserResetPwdResult()
        : m_transportOk(false), m_code(0), m_kicked(0)
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

// 套餐名称，下标即套餐编号，与服务端 t_enterprise.f_plan 一致：0 基础版 / 1 专业版 / 2 旗舰版。
// 新增企业、企业设置与「更多筛选」的套餐下拉框都用它。2026-10-04 起统一为这三档：产品本身就是
// 企业版 IM，2 号套餐不叫「企业版」；控制台原先自行约定的编号 3 不再使用。
extern const TCHAR* const kPlanOptions[];
// kPlanOptions 的项数。
extern const int kPlanOptionCount;

// 套餐编号转名称：0～2 为 kPlanOptions 中的名称；列表外的编号显示成「套餐N」，以便察觉与
// 服务端约定不一致的数据，并可由 PlanCodeFromName 还原。
//   plan：套餐编号。
CString PlanNameFromInt(int plan);

// 套餐名称转编号：kPlanOptions 中的名称返回其下标；「套餐N」返回 N；其余返回 -1。
//   name：套餐名称。
int PlanCodeFromName(const CString& name);

// 企业账号前缀的长度上限（字符数），与服务端 AccountName.h 的 ACCOUNT_PREFIX_MAX_LENGTH 一致。
const int kAccountPrefixMaxLength = 10;

// 校验并规整企业账号前缀（docs/登录账号与手机号登录方案.md 第 4 节），规则与服务端 NormalizeAccountPrefix
// 一致：去掉首尾空白后，空串表示清除（合法）；否则须为 1～kAccountPrefixMaxLength 个字符、只含英文字母与
// 点号「.」、至少一个字母，规整为大写。企业设置窗口保存前用它做本地校验，不合格时不发请求。
//   raw：管理员填写的原文；out：[出参] 合法时为规整后的前缀（可能为空串）。
//   返回：合法返回 true。
bool NormalizeAccountPrefix(const CString& raw, CString& out);

// 手机号的位数（中国大陆手机号），与服务端 AccountName.h 的 PHONE_NUMBER_LENGTH 一致。
const int kPhoneNumberLength = 11;

// 手机号格式是否合法：恰好 11 位、以 1 开头、全部是数字，与服务端 IsValidPhoneNumber 一致。
// 新建与编辑员工时手机号必填（2026-10-06 起，手机号可用于登录），保存前用它做本地校验。
//   phone：手机号原文，不做去空白处理。
bool IsValidPhoneNumber(const CString& phone);

// 公告级别的名称，下标即服务端的级别编号（announcement 的 severity：0 普通 / 1 提醒 / 2 警告）。
// 新建公告弹窗的级别选项用它；公告列表、详情徽章与导出经 AnnSeverityLabel 取同一套名称。
extern const TCHAR* const kAnnSeverityLabels[];
// kAnnSeverityLabels 的项数。
extern const int kAnnSeverityLabelCount;

// 公告级别（客户端 Severity）转名称：Warn 为「提醒」，Danger 为「警告」，其余为「普通」。
//   severity：公告级别。
LPCTSTR AnnSeverityLabel(Severity severity);

// 新增企业与「更多筛选」共用的行业选项（下标顺序即下拉框里的顺序）。
extern const TCHAR* const kIndustryOptions[];
// kIndustryOptions 的项数。
extern const int kIndustryOptionCount;

// 按完整的筛选条件构造企业列表请求（POST /admin/enterprise/list.do）。关键字与状态总是带上；
// 行业、套餐、开通日期只在已设置（日期还须格式正确）时带上。
//   filter：筛选条件；page / pagesize：分页参数。
HttpRequest MakeEnterpriseListRequest(const ServerConfig& server,
                                      const CString& token,
                                      const EnterpriseListFilter& filter,
                                      int page, int pagesize);

// 判断文字是否为合法的「YYYY-MM-DD」日期：长度 10、分隔符为「-」、其余为数字、月份 1～12、
// 日 1～31（不按月份核对天数，与服务端 EnterpriseManager::isValidDateText 一致）。
bool IsValidDateText(const CString& text);

// 「更多筛选」里已设置的条件数：行业非空、套餐不为 -1、起止日期格式正确，各计 1。
int CountMoreFilters(const EnterpriseListFilter& filter);

// 「更多筛选」按钮文字：没有条件时为「更多筛选」，有 N 个时为「更多筛选 · N」。
//   count：已设置的条件数，见 CountMoreFilters。
CString MoreFilterButtonText(int count);

// 解析企业列表应答，得到 EnterpriseListResult。
EnterpriseListResult ParseEnterpriseListResponse(const HttpResult& http);

// 构造新增企业请求（POST /admin/enterprise/create.do）。
//   name / shortName / industry / region：企业基本信息。
//   plan：套餐编号（与服务端 t_enterprise.f_plan 一致：0 基础版 / 1 专业版 / 2 旗舰版）。
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

// 按完整的筛选条件构造导出请求（POST /admin/enterprise/export.do），条件的带法与
// 按条件构造的列表请求相同。
HttpRequest MakeEnterpriseExportRequest(const ServerConfig& server,
                                        const CString& token,
                                        const EnterpriseListFilter& filter);

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

// 构造服务号详情请求（POST /admin/service/detail.do）。列表接口不带绑定范围，
// 绑定只能从这里读。
//   serviceId：目标服务号 ID。
HttpRequest MakeServiceDetailRequest(const ServerConfig& server,
                                     const CString& token,
                                     long long serviceId);

// 解析服务号详情应答：服务号资料与原始绑定列表。失败时 m_message 为中文提示。
ServiceDetailResult ParseServiceDetailResponse(const HttpResult& http);

// 把服务端的原始绑定翻译成界面用的绑定项（带显示名）。
//   raw：detail.do 返回的原始绑定；
//   orgRoot：该企业的组织树根节点（借用，可为 nullptr —— 组织树尚未加载时）；
//   enterpriseShortName：企业简称，用于「全员（简称）」这一项的显示名。
//   返回：与 raw 一一对应的绑定项。全员的 ID 固定为 "root"（与绑定弹窗一致）；
//         部门 / 员工按 ID 在组织树里查名字，查不到时显示「部门 #ID」「员工 #ID」；
//         类型不在 0~2 之内的项被跳过。
std::vector<ServiceBinding> ResolveServiceBindings(
    const std::vector<RawServiceBinding>& raw, const OrgNode* orgRoot,
    const CString& enterpriseShortName);

// 重新拉取服务号列表后，把旧列表里已经读到的绑定按服务号 ID 带到新列表上 ——
// 列表接口不带绑定，直接覆盖会把已读到的绑定清空。
//   oldList：覆盖前的服务号列表；
//   newList：刚拉到的服务号列表（输入输出：其中 ID 在 oldList 里出现、且旧项的
//            m_bindingsLoaded 为 true 的服务号，m_bindings 与 m_bindingsLoaded 取旧值）。
void CarryOverLoadedBindings(const std::vector<Service>& oldList,
                             std::vector<Service>& newList);

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
//   gender：性别编码 0 保密 / 1 男 / 2 女（见 GenderComboIndexToCode）。
HttpRequest MakeUserCreateRequest(const ServerConfig& server,
                                  const CString& token, long long enterpriseId,
                                  long long deptId, const CString& name,
                                  const CString& title, const CString& employeeNo,
                                  const CString& email, const CString& phone,
                                  const CString& joinDate, int gender);

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

// 解析重置员工密码应答（含新密码与被强制下线的会话数）。
UserResetPwdResult ParseUserResetPasswordResponse(const HttpResult& http);

// 拼出「密码已重置」弹窗的正文（2026-10-07 起，有设备被强制下线时多一句说明）。
//   newPassword：重置后的新密码。
//   kicked：     被强制下线的会话数；不大于 0 时不提下线。
// 返回：正文，行之间用 \n 分隔。纯函数，无副作用。
CString FormatResetPasswordNotice(const CString& newPassword, int kicked);

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
//   员工的姓名 / 职位 / 工号 / 入职日期 / 座机 / 工位 / 手机 / 邮箱 / 签名；
//   gender：性别编码 0 保密 / 1 男 / 2 女（服务端据此修改性别）。
//   应答只关心成败，复用 ParseEnterpriseActionResponse。
HttpRequest MakeUserUpdateRequest(const ServerConfig& server,
                                  const CString& token, long long userId,
                                  const CString& name, const CString& title,
                                  const CString& employeeNo,
                                  const CString& joinDate,
                                  const CString& landline,
                                  const CString& workstation,
                                  const CString& phone, const CString& email,
                                  const CString& signature, int gender);

// 性别编码（0 保密 / 1 男 / 2 女，与服务端 enum profile_gender 一致）转显示文字；
// 其它取值按「保密」。
CString GenderDisplayText(int gender);

// 性别下拉框（依次为「保密 / 男 / 女」）的序号转编码；越界（含未选中的 -1）按 0。
int GenderComboIndexToCode(int index);

// 性别编码转下拉框序号；未知编码选中「保密」（序号 0）。
int GenderCodeToComboIndex(int gender);

// 构造调整员工所属部门请求（POST /admin/user/move.do）。
//   deptId：员工当前所属部门 ID；newDeptId：要调入的目标部门 ID。
//   应答只关心成败，复用 ParseEnterpriseActionResponse。
HttpRequest MakeUserMoveRequest(const ServerConfig& server,
                                const CString& token, long long userId,
                                long long deptId, long long newDeptId);

// ---- 企业设置（模块 3：改企业信息 + 企业管理员管理）----
// 说明：update / removeadmin 只关心成败，应答复用 ParseEnterpriseActionResponse。

// 构造修改企业信息请求（POST /admin/enterprise/update.do）。
//   name/shortName/industry/region：企业基本信息；plan：套餐编号（0/1/2，见 kPlanOptions）；
//   accountPrefix：新建账号的前缀（2026-10-06 起总是带上；空串表示清除、改回默认的 EMP，服务端会再校验一次）。
HttpRequest MakeEnterpriseUpdateRequest(const ServerConfig& server,
                                        const CString& token,
                                        long long enterpriseId,
                                        const CString& name,
                                        const CString& shortName,
                                        const CString& industry,
                                        const CString& region, int plan,
                                        const CString& accountPrefix);

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

// 构造重新推送公告请求（POST /admin/announcement/repush.do）。服务端只接受已发布的公告，
// 推给受众里当前在线且未读的成员；仅平台超管可调用。
//   announcementId：公告 ID。
HttpRequest MakeAnnRepushRequest(const ServerConfig& server, const CString& token,
                                 long long announcementId);

// 解析重新推送应答：成功时 m_pushed 为推送人数（缺少 pushed 字段时按 0）。
AnnRepushResult ParseAnnRepushResponse(const HttpResult& http);

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

// 把字节数格式化成人能读的大小文本，如 36 -> "36 B"、1536 -> "1.5 KB"、
// 3221225472 -> "3.00 GB"。列表与下拉里显示包大小用。
//   bytes：字节数；负数按 0 处理。
CString FormatPackageSize(long long bytes);

// 判断版本号文字是否合法：2～4 段十进制数字，以点分隔，每段 1～5 位，不含空白或其它
// 字符。服务端比较版本号时会忽略非数字字符（LoginProtocol::isVersionAtLeast），格式
// 不对时不报错、却让门槛变了样，所以在控制台提交前校验。
//   text：版本号文字（调用方负责去掉首尾空白）。
bool IsValidVersionText(const CString& text);

// 比较两个版本号：逐段按数值比较，缺少的段按 0 处理，与服务端的比较规则一致。
//   a / b：版本号文字，应已通过 IsValidVersionText；无法识别的段按 0 处理。
//   返回：a 低于 b 时为负数，相等为 0，高于为正数。
int CompareVersionText(const CString& a, const CString& b);

// 校验客户端版本记录的「最低版本」。
//   minVersion：最低版本输入，先去掉首尾空白；为空表示不另设门槛，直接通过。
//   version：这条记录的版本号（新建时是输入的版本号，编辑时是记录原有的版本号）；
//            为空或格式不对时只校验最低版本自身的格式。
//   返回：空串表示通过；否则是给用户看的中文提示。
CString CheckMinVersion(const CString& minVersion, const CString& version);

// ---- 敏感词（模块 11，docs/Admin控制台接口文档.md 第十一节）----
// 说明：词库按企业划分。平台超管须在每个请求里指定 enterpriseid；企业管理员传 0，请求里不带
//       enterpriseid，由服务端改写为本企业。修改与总开关只关心成败，应答复用
//       ParseEnterpriseActionResponse；导出成功时回纯文本、失败时回 JSON，复用
//       ParseEnterpriseExportResponse（文字在 m_csvUtf8 里）。

// 一条敏感词（list.do 返回）。
struct SensitiveWordItem
{
    long long m_id;           // 词条 ID
    CString   m_word;         // 词条原文
    int       m_category;     // 分类，1～6，名称见 swtext::CategoryName
    CString   m_remark;       // 备注
    CString   m_creatorName;  // 添加者姓名
    long long m_createTime;   // 添加时间，Unix 毫秒
    long long m_updateTime;   // 最后修改时间，Unix 毫秒

    SensitiveWordItem()
        : m_id(0), m_category(0), m_createTime(0), m_updateTime(0)
    {
    }
};

// 词库列表（list.do）的解析结果。
struct SensitiveWordListResult
{
    bool                           m_transportOk;  // HTTP 往返是否成功
    int                            m_code;         // 业务错误码
    CString                        m_message;      // 失败时给用户看的中文提示
    int                            m_total;        // 满足筛选条件的总条数（分页条用）
    bool                           m_enabled;      // 该企业的敏感词总开关是否打开
    int                            m_wordCount;    // 该企业的词条总数（不受筛选影响，显示「已用 N / 10000」）
    std::vector<SensitiveWordItem> m_list;         // 本页词条（成功时有效）

    SensitiveWordListResult()
        : m_transportOk(false), m_code(0), m_total(0), m_enabled(false), m_wordCount(0)
    {
    }

    bool Succeeded() const { return m_transportOk && m_code == 0; }
};

// 添加敏感词（add.do）的解析结果，带回新词条的 ID。
struct SensitiveWordAddResult
{
    bool      m_transportOk;  // HTTP 往返是否成功
    int       m_code;         // 业务错误码；词条为空 / 超长 / 重复 / 词库已满时为 120～123
    CString   m_message;      // 失败时给用户看的中文提示
    long long m_id;           // 新词条 ID（成功时有效）

    SensitiveWordAddResult()
        : m_transportOk(false), m_code(0), m_id(0)
    {
    }

    bool Succeeded() const { return m_transportOk && m_code == 0; }
};

// 删除敏感词（delete.do）的解析结果，带回实际删掉的条数。
struct SensitiveWordDeleteResult
{
    bool    m_transportOk;  // HTTP 往返是否成功
    int     m_code;         // 业务错误码
    CString m_message;      // 失败时给用户看的中文提示
    int     m_deleted;      // 实际删除的条数（不属于该企业或已被删的 ID 不计）

    SensitiveWordDeleteResult()
        : m_transportOk(false), m_code(0), m_deleted(0)
    {
    }

    bool Succeeded() const { return m_transportOk && m_code == 0; }
};

// 导入时被跳过的一行（import.do 返回）。
struct SensitiveImportSkip
{
    int     m_line;        // 行号：本批请求 lines 数组里的第几行（从 1 起），加上批次首行行号减一即为原文行号
    CString m_word;        // 该行的词条（去掉首尾空白后）
    int     m_reason;      // 跳过原因的错误码，120～124
    CString m_reasonText;  // 跳过原因的中文说明

    SensitiveImportSkip()
        : m_line(0), m_reason(0)
    {
    }
};

// 批量导入一批（import.do）的解析结果。
struct SensitiveImportResult
{
    bool                             m_transportOk;  // HTTP 往返是否成功
    int                              m_code;         // 业务错误码
    CString                          m_message;      // 失败时给用户看的中文提示
    int                              m_added;        // 本批成功添加的条数
    std::vector<SensitiveImportSkip> m_skipped;      // 本批被跳过的行（空行不算，不在其中）

    SensitiveImportResult()
        : m_transportOk(false), m_code(0), m_added(0)
    {
    }

    bool Succeeded() const { return m_transportOk && m_code == 0; }
};

// 一条拦截记录（hits.do 返回）。
struct SensitiveHitItem
{
    long long m_id;          // 记录 ID
    long long m_time;        // 拦截时间，Unix 毫秒
    long long m_userId;      // 提交者 userid；自助注册时为 0
    CString   m_userName;    // 提交者名称（昵称、账号，自助注册时为手机号）
    int       m_source;      // 来源，1～12，名称见 swtext::HitSourceName
    int       m_ctype;       // 会话类型：0 与会话无关，1 单聊，2 群聊
    long long m_cid;         // 会话 ID：单聊为对方 userid，群聊为群 ID；与会话无关时为 0
    CString   m_targetName;  // 会话名称快照（对方昵称、群名，或「群发助手（N 个对象）」）
    CString   m_word;        // 命中的词条
    int       m_category;    // 命中词条的分类
    CString   m_snippet;     // 内容摘要：命中位置前后各 20 个字符

    SensitiveHitItem()
        : m_id(0), m_time(0), m_userId(0), m_source(0), m_ctype(0), m_cid(0), m_category(0)
    {
    }
};

// 拦截记录列表（hits.do）的解析结果。
struct SensitiveHitListResult
{
    bool                          m_transportOk;  // HTTP 往返是否成功
    int                           m_code;         // 业务错误码
    CString                       m_message;      // 失败时给用户看的中文提示
    int                           m_total;        // 满足筛选条件的总条数
    std::vector<SensitiveHitItem> m_list;         // 本页记录，按拦截时间倒序（成功时有效）

    SensitiveHitListResult()
        : m_transportOk(false), m_code(0), m_total(0)
    {
    }

    bool Succeeded() const { return m_transportOk && m_code == 0; }
};

// 构造词库列表请求（POST /admin/sensitive/list.do）。
//   enterpriseId：平台超管选中的企业 ID；企业管理员传 0（请求里不带）。
//   keyword：按词条模糊搜索；空串表示不筛选。
//   category：按分类筛选，1～6；0 表示全部分类。
//   page：页号，从 1 起。pageSize：每页条数，服务端上限 200。
HttpRequest MakeSensitiveListRequest(const ServerConfig& server, const CString& token, long long enterpriseId,
                                     const CString& keyword, int category, int page, int pageSize);

// 解析词库列表应答，得到 SensitiveWordListResult。
SensitiveWordListResult ParseSensitiveListResponse(const HttpResult& http);

// 构造添加敏感词请求（POST /admin/sensitive/add.do）。
//   word：词条，最多 32 个字；category：分类 1～6；remark：备注，可为空，最多 64 个字。
HttpRequest MakeSensitiveAddRequest(const ServerConfig& server, const CString& token, long long enterpriseId,
                                    const CString& word, int category, const CString& remark);

// 解析添加敏感词应答，得到 SensitiveWordAddResult。
SensitiveWordAddResult ParseSensitiveAddResponse(const HttpResult& http);

// 构造修改敏感词请求（POST /admin/sensitive/update.do）。词条、分类、备注三项整体覆盖。
// 应答复用 ParseEnterpriseActionResponse；改后的词条与别的词条重复时为 122，词条已被删时为 124。
//   id：词条 ID。
HttpRequest MakeSensitiveUpdateRequest(const ServerConfig& server, const CString& token, long long enterpriseId,
                                       long long id, const CString& word, int category, const CString& remark);

// 构造删除敏感词请求（POST /admin/sensitive/delete.do）。
//   ids：要删除的词条 ID，1～500 个。
HttpRequest MakeSensitiveDeleteRequest(const ServerConfig& server, const CString& token, long long enterpriseId,
                                       const std::vector<long long>& ids);

// 解析删除敏感词应答，得到 SensitiveWordDeleteResult。
SensitiveWordDeleteResult ParseSensitiveDeleteResponse(const HttpResult& http);

// 构造批量导入请求（POST /admin/sensitive/import.do），提交一批行。
//   linesUtf8：本批各行（UTF-8），1～1000 行，通常取 swtext::MakeImportBatches 的一批；空行原样提交，
//              服务端报回的行号才能与原文对齐。
//   defaultCategory：只有词条、或分类名写错的行归入的分类，1～6。
// 一批在服务端的一个事务里写库，超时比常规接口宽。
HttpRequest MakeSensitiveImportRequest(const ServerConfig& server, const CString& token, long long enterpriseId,
                                       const std::vector<std::string>& linesUtf8, int defaultCategory);

// 解析批量导入应答，得到 SensitiveImportResult。
SensitiveImportResult ParseSensitiveImportResponse(const HttpResult& http);

// 构造导出请求（POST /admin/sensitive/export.do）。应答用 ParseEnterpriseExportResponse 解析，
// 成功时文字（每行「词条 Tab 分类名 Tab 备注」）在 m_csvUtf8 里，存盘前交给 swtext::BuildExportFileBytes。
HttpRequest MakeSensitiveExportRequest(const ServerConfig& server, const CString& token, long long enterpriseId);

// 构造总开关请求（POST /admin/sensitive/setenabled.do）。应答复用 ParseEnterpriseActionResponse。
//   enabled：true 启用，false 暂停（暂停期间该企业不做任何敏感词检查）。
HttpRequest MakeSensitiveSetEnabledRequest(const ServerConfig& server, const CString& token, long long enterpriseId,
                                           bool enabled);

// 构造拦截记录列表请求（POST /admin/sensitive/hits.do）。
//   keyword：按提交者名称模糊搜索；空串表示不筛选。
//   source：按来源筛选，1～12；0 表示全部来源。category：按分类筛选，1～6；0 表示全部分类。
//   beginTime / endTime：拦截时间范围，Unix 毫秒；0 表示该端不限。
//   page：页号，从 1 起。pageSize：每页条数，服务端上限 200。
HttpRequest MakeSensitiveHitsRequest(const ServerConfig& server, const CString& token, long long enterpriseId,
                                     const CString& keyword, int source, int category,
                                     long long beginTime, long long endTime, int page, int pageSize);

// 解析拦截记录列表应答，得到 SensitiveHitListResult。
SensitiveHitListResult ParseSensitiveHitsResponse(const HttpResult& http);

// ==================== 文件共享（模块 12，docs/Admin控制台接口文档.md 第十二章）====================

// 文件共享开关的设置项名称（setting/get.do 与 setting/set.do 的 key），取值 "1" 开启、"0" 关闭
const wchar_t kSettingFileShareEnabled[] = L"fileshare.enabled";

// 客户端点对点直连开关的设置项名称（docs/P2P通信方案.md 9.4），取值 "1" 开启、"0" 关闭
const wchar_t kSettingP2PEnabled[] = L"p2p.enabled";

// 「邀请入群需被邀请人同意」开关的设置项名称（docs/邀请入群方案.md §3.5、§4.6），取值 "1" 开启、"0" 关闭，未设置时为关闭
const wchar_t kSettingInviteConfirm[] = L"group.inviteconfirm";

// 「允许用户自助注册」开关的设置项名称（2026-10-04），取值 "1" 开启、"0" 关闭，未设置时为开启。关闭后该企业不能在
// PC 客户端自助注册加入，只能由管理员新建账号；服务端只在注册流程里检查，不下发给客户端
const wchar_t kSettingRegisterEnabled[] = L"register.enabled";

// 群语音 / 群视频通话开关的设置项名称（docs/群音视频通话方案.md 第 6 期），取值 "1" 开启、"0" 关闭，未设置时为开启。
// 关闭后该企业成员不能发起或加入群通话（错误码 727），客户端群聊窗隐藏「语音通话」「视频通话」按钮
const wchar_t kSettingGroupCallEnabled[] = L"groupcall.enabled";

// 语音字幕开关的设置项名称（docs/语音字幕方案.md 第 10 节），取值 "1" 开启、"0" 关闭，未设置时为开启。
// 关闭后该企业成员的客户端不再加载字幕模块，录屏与屏幕共享都不再提供字幕；服务端只下发，不做检查
const wchar_t kSettingCaptionEnabled[] = L"caption.enabled";

// 云端录制开关的设置项名称（docs/屏幕共享与录制方案.md 12.6.5），取值 "1" 开启、"0" 关闭，未设置时为关闭。
// 关闭后该企业成员不能再开始云端录制、不能把录像保存到云端，已有的云端录像照常可看、可删
const wchar_t kSettingCloudRecordEnabled[] = L"cloudrecord.enabled";

// 云端录像的企业总配额（单位 GB，不带符号的十进制整数，0～1000000，未设置时为 50）：原始帧文件与合成后的录像都计入，
// 0 表示不允许再新增云端录像
const wchar_t kSettingCloudRecordQuotaGb[] = L"cloudrecord.quotagb";

// 云端录像的保留天数（0～3650，未设置时为 30，0 表示永久保留）。录像结束或上传完成时按当时的天数算出到期时间，
// 改天数只影响之后结束的录像
const wchar_t kSettingCloudRecordRetainDays[] = L"cloudrecord.retaindays";

// 单聊密聊开关的设置项名称（docs/密聊方案.md 4.9），取值 "1" 开启、"0" 关闭，未设置时为开启。关闭后该企业成员的客户端
// 不再显示「密聊」按钮，服务端拒绝发起与应答密聊，并结束该企业成员参与的全部密聊（双方只能查看已有内容）
const wchar_t kSettingSecretChatEnabled[] = L"secretchat.enabled";

// 帖子群开关的设置项名称（docs/帖子群方案.md 4.7），取值 "1" 开启、"0" 关闭，未设置时为开启。关闭后该企业成员的客户端
// 隐藏全部帖子群、不能再新建帖子群，服务端拒绝 2700 段请求；已有的帖子与留言保留，重新打开后恢复
const wchar_t kSettingThreadGroupEnabled[] = L"threadgroup.enabled";

// 机器人总开关的设置项名称（docs/机器人方案.md 4.1），取值 "1" 开启、"0" 关闭，未设置时为开启。关闭后本企业全部机器人
// （企业、群、个人机器人）停止响应，客户端隐藏全部机器人入口
const wchar_t kSettingBotEnabled[] = L"bot.enabled";

// 「允许群主添加群机器人」开关的设置项名称，取值 "1" 开启、"0" 关闭，未设置时为开启
const wchar_t kSettingBotGroup[] = L"bot.groupbot";

// 「允许员工配置个人机器人」开关的设置项名称，取值 "1" 开启、"0" 关闭，未设置时为开启
const wchar_t kSettingBotPersonal[] = L"bot.personal";

// 「允许群主给群机器人配置外部服务」开关的设置项名称，取值 "1" 开启、"0" 关闭，未设置时为关闭。打开后群主才能在群机器人上
// 添加「外部系统推送」触发方式与「呼叫外部服务」动作
const wchar_t kSettingBotGroupExternal[] = L"bot.groupexternal";

// 一项功能设置（setting/get.do 的 settings 数组元素）。
struct FeatureSettingItem
{
    CString m_key;        // 设置项名称，如 kSettingFileShareEnabled
    CString m_value;      // 当前取值（字符串）
    bool    m_isDefault;  // 该企业没有设置记录、取的是内置默认值时为 true

    FeatureSettingItem()
        : m_isDefault(false)
    {
    }
};

// 读功能设置（setting/get.do）的解析结果。
struct FeatureSettingsResult
{
    bool                            m_transportOk;   // HTTP 往返是否成功
    int                             m_code;          // 业务错误码
    CString                         m_message;       // 失败时给用户看的中文提示
    long long                       m_enterpriseId;  // 设置所属的企业（服务端回填，企业管理员即本企业）
    std::vector<FeatureSettingItem> m_settings;      // 各项设置（成功时有效）
    long long                       m_cloudRecordUsedBytes;  // 该企业云端录像的已用字节数；应答里没有（老服务端、云端录制
                                                             // 没有初始化）时为 -1

    FeatureSettingsResult()
        : m_transportOk(false), m_code(0), m_enterpriseId(0), m_cloudRecordUsedBytes(-1)
    {
    }

    bool Succeeded() const { return m_transportOk && m_code == 0; }

    // 按名称查找一项设置。
    //   key：设置项名称。out：[出参] 找到时为该项。
    //   返回：找到返回 true。
    bool Find(const CString& key, FeatureSettingItem& out) const
    {
        for (size_t i = 0; i < m_settings.size(); ++i)
        {
            if (m_settings[i].m_key == key)
            {
                out = m_settings[i];
                return true;
            }
        }
        return false;
    }
};

// 改功能设置（setting/set.do）的解析结果。
struct FeatureSetResult
{
    bool      m_transportOk;   // HTTP 往返是否成功
    int       m_code;          // 业务错误码；设置项或取值不认识时为 133
    CString   m_message;       // 失败时给用户看的中文提示
    long long m_enterpriseId;  // 设置所属的企业
    bool      m_enabled;       // 修改之后文件共享是否开启（成功时有效）

    FeatureSetResult()
        : m_transportOk(false), m_code(0), m_enterpriseId(0), m_enabled(false)
    {
    }

    bool Succeeded() const { return m_transportOk && m_code == 0; }
};

// 一个共享（fileshare/list.do 的 list 项）。
struct FileShareItem
{
    long long m_shareId;           // 共享 ID
    long long m_enterpriseId;      // 分享者所属的企业
    long long m_ownerId;           // 分享者 userid
    CString   m_ownerName;         // 分享者昵称
    CString   m_ownerAccount;      // 分享者账号
    int       m_ctype;             // 会话类型：1 单聊，2 群聊
    long long m_cid;               // 会话 ID：单聊为对方 userid，群聊为群 ID
    CString   m_conversationName;  // 会话名称：群聊为群名，单聊为对方昵称
    int       m_kind;              // 共享类型：1 整个磁盘，2 文件夹，3 单个文件
    CString   m_name;              // 共享名（共享根的名字）
    CString   m_displayPath;       // 分享者电脑上的显示路径
    int       m_memberCount;       // 成员人数
    int       m_state;             // 状态：1 有效，2 已取消
    int       m_cancelReason;      // 取消原因，见 fstext::CancelReason；有效时为 0
    long long m_createTime;        // 发起时间，Unix 毫秒
    long long m_cancelTime;        // 取消时间，Unix 毫秒；有效时为 0

    FileShareItem()
        : m_shareId(0), m_enterpriseId(0), m_ownerId(0), m_ctype(0), m_cid(0), m_kind(0), m_memberCount(0),
          m_state(0), m_cancelReason(0), m_createTime(0), m_cancelTime(0)
    {
    }
};

// 共享列表（fileshare/list.do）的解析结果。
struct FileShareListResult
{
    bool                       m_transportOk;  // HTTP 往返是否成功
    int                        m_code;         // 业务错误码
    CString                    m_message;      // 失败时给用户看的中文提示
    int                        m_total;        // 满足筛选条件的总条数
    std::vector<FileShareItem> m_list;         // 本页共享，按共享 ID 倒序（成功时有效）

    FileShareListResult()
        : m_transportOk(false), m_code(0), m_total(0)
    {
    }

    bool Succeeded() const { return m_transportOk && m_code == 0; }
};

// 共享的一个成员（fileshare/members.do 的 members 项）。
struct FileShareMemberItem
{
    long long m_userId;    // 成员 userid
    CString   m_name;      // 成员昵称
    CString   m_account;   // 成员账号
    int       m_perms;     // 写权限位的组合，见 fstext::PermBit；0 表示只读

    FileShareMemberItem()
        : m_userId(0), m_perms(0)
    {
    }
};

// 共享成员（fileshare/members.do）的解析结果。
struct FileShareMembersResult
{
    bool                             m_transportOk;  // HTTP 往返是否成功
    int                              m_code;         // 业务错误码；共享不存在 130，不属于本企业 132
    CString                          m_message;      // 失败时给用户看的中文提示
    std::vector<FileShareMemberItem> m_members;      // 成员列表（成功时有效）

    FileShareMembersResult()
        : m_transportOk(false), m_code(0)
    {
    }

    bool Succeeded() const { return m_transportOk && m_code == 0; }
};

// 一条文件共享审计记录（fileshare/audit.do 的 list 项）。
struct FileShareAuditItem
{
    long long m_id;            // 记录 ID
    long long m_time;          // 记录时间，Unix 毫秒
    long long m_shareId;       // 共享 ID
    CString   m_shareName;     // 共享名
    long long m_ownerId;       // 分享者 userid
    CString   m_ownerName;     // 分享者昵称
    long long m_actorId;       // 操作人：普通用户为 userid，管理员为管理员 ID
    CString   m_actorName;     // 操作人名称：普通用户为昵称，管理员为姓名
    bool      m_actorIsAdmin;  // 操作人是否为后台管理员
    int       m_action;        // 动作，见 fstext::AuditAction
    int       m_source;        // 来源：1 服务端记录，2 分享者客户端上报
    CString   m_path;          // 相对共享根的路径；申请传输时为条目清单的摘要
    CString   m_toPath;        // 重命名后的新名字或移动的目标目录；其余动作为空
    long long m_size;          // 涉及的字节数；没有字节数的动作为 0
    int       m_result;        // 0 成功，否则为错误码（800 段）
    CString   m_accessId;      // 所属访问会话 ID；与访问会话无关的动作为空

    FileShareAuditItem()
        : m_id(0), m_time(0), m_shareId(0), m_ownerId(0), m_actorId(0), m_actorIsAdmin(false), m_action(0),
          m_source(0), m_size(0), m_result(0)
    {
    }
};

// 文件共享审计列表（fileshare/audit.do）的解析结果。
struct FileShareAuditListResult
{
    bool                            m_transportOk;  // HTTP 往返是否成功
    int                             m_code;         // 业务错误码
    CString                         m_message;      // 失败时给用户看的中文提示
    int                             m_total;        // 满足筛选条件的总条数
    std::vector<FileShareAuditItem> m_list;         // 本页记录，按时间倒序（成功时有效）

    FileShareAuditListResult()
        : m_transportOk(false), m_code(0), m_total(0)
    {
    }

    bool Succeeded() const { return m_transportOk && m_code == 0; }
};

// 构造读功能设置请求（POST /admin/setting/get.do）。
//   enterpriseId：平台管理员要读的企业，必须大于 0；企业管理员传 0（请求里不带，服务端按本企业处理）。
HttpRequest MakeFeatureSettingsRequest(const ServerConfig& server, const CString& token, long long enterpriseId);

// 解析读功能设置应答，得到 FeatureSettingsResult。
FeatureSettingsResult ParseFeatureSettingsResponse(const HttpResult& http);

// 构造改功能设置请求（POST /admin/setting/set.do）。
//   enterpriseId：同 MakeFeatureSettingsRequest。key：设置项名称。value：新取值（如 "0" / "1"）。
HttpRequest MakeFeatureSetRequest(const ServerConfig& server, const CString& token, long long enterpriseId,
                                  const CString& key, const CString& value);

// 解析改功能设置应答，得到 FeatureSetResult。
FeatureSetResult ParseFeatureSetResponse(const HttpResult& http);

// 构造共享列表请求（POST /admin/fileshare/list.do）。
//   enterpriseId：平台管理员按企业过滤，0 表示全部企业；企业管理员传什么都按本企业处理。
//   keyword：按分享者的账号或昵称模糊搜索；空串表示不筛选。
//   state：1 只看有效，2 只看已取消，0 全部。
//   page：页号，从 1 起。pageSize：每页条数，服务端上限 200。
HttpRequest MakeFileShareListRequest(const ServerConfig& server, const CString& token, long long enterpriseId,
                                     const CString& keyword, int state, int page, int pageSize);

// 解析共享列表应答，得到 FileShareListResult。
FileShareListResult ParseFileShareListResponse(const HttpResult& http);

// 构造共享成员请求（POST /admin/fileshare/members.do）。shareId：共享 ID。
HttpRequest MakeFileShareMembersRequest(const ServerConfig& server, const CString& token, long long shareId);

// 解析共享成员应答，得到 FileShareMembersResult。
FileShareMembersResult ParseFileShareMembersResponse(const HttpResult& http);

// 构造强制取消共享请求（POST /admin/fileshare/cancel.do）。应答复用 ParseEnterpriseActionResponse：
// 共享不存在为 130，已取消过为 131，不属于本企业为 132。
//   shareId：共享 ID。
HttpRequest MakeFileShareCancelRequest(const ServerConfig& server, const CString& token, long long shareId);

// 构造文件共享审计请求（POST /admin/fileshare/audit.do）。
//   enterpriseId：同 MakeFileShareListRequest。
//   actorKeyword：按操作人的账号或昵称（管理员按姓名）模糊搜索；空串表示不筛选。
//   action：动作，见 fstext::AuditAction；0 表示全部动作。
//   startTime / endTime：时间范围，Unix 毫秒，两端都包含；0 表示该端不限。
//   shareId：只看这个共享；0 表示不限。
//   page：页号，从 1 起。pageSize：每页条数，服务端上限 200。
HttpRequest MakeFileShareAuditRequest(const ServerConfig& server, const CString& token, long long enterpriseId,
                                      const CString& actorKeyword, int action, long long startTime,
                                      long long endTime, long long shareId, int page, int pageSize);

// 解析文件共享审计应答，得到 FileShareAuditListResult。
FileShareAuditListResult ParseFileShareAuditResponse(const HttpResult& http);

} // namespace adminapi

} // namespace flamingoadmin
