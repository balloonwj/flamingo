/**
 *  flamingoAdmin —— mock 数据模型与种子数据声明。
 *
 *  本文件定义管理控制台用到的全部数据结构（企业、组织节点、服务号、公告、
 *  审计日志），以及构建种子数据的工厂函数。结构与字段对应设计稿
 *  design/FlamingoIM Enterprise Admin.html 中的 SEED_* 数据。
 *
 *  数据的内存态持有与变更由 Mock/DataStore 负责；本文件只管「类型 + 初始值」。
 *
 *  balloonwj@qq.com   2026.05.21
 */
#pragma once

#include <atlstr.h>
#include <windows.h>
#include <vector>
#include <map>

namespace flamingoadmin {

// ===========================================================================
// 枚举
// ===========================================================================

// 企业入驻状态。对应设计稿 SEED_ENTERPRISES[].status。
enum class EnterpriseStatus
{
    Active,     // 活跃：企业已通过审核且正常使用
    Pending,    // 待审核：刚提交入驻申请，等待平台审核
    Suspended,  // 已停用：被平台暂停服务（欠费 / 违规等）
};

// 组织节点类型。组织树用同一结构承载部门与员工两类节点。
enum class OrgNodeKind
{
    Dept,   // 部门节点：可包含子部门与员工，有负责人
    User,   // 员工节点：叶子节点，挂在某部门或根部门下
};

// 员工在职状态。对应设计稿 user.status。
enum class UserStatus
{
    Active,    // 在职：正常在岗
    OnLeave,   // 休假：请假 / 长假中
    Disabled,  // 已停用：账号被禁用（离职等）
};

// 公告 / 审计的严重级别，决定圆点与徽章配色。
enum class Severity
{
    Info,    // 信息：常规通知（灰 / 蓝）
    Normal,  // 普通：默认级别（仅公告使用）
    Warn,    // 警告：需关注（橙）
    Danger,  // 危险：高危事件（红）
};

// 服务号类型。对应设计稿 service.kind。
enum class ServiceKind
{
    Notification,  // 通知类：单向推送（HR 助手、财务通知等）
    Assistant,     // 助手类：可交互的自助式机器人（IT 支持、考勤等）
    Integration,   // 集成类：对接外部系统（Prometheus / Sentry 告警等）
};

// 服务号状态。对应设计稿 service.status。
enum class ServiceStatus
{
    Active,    // 已启用：正常推送 / 服务
    Draft,     // 草稿：创建中，尚未发布
    Disabled,  // 已停用：管理员手动停用，保留历史但不再推送
};

// 服务号绑定目标类型，决定服务号推送给谁。
enum class BindingKind
{
    Enterprise,  // 绑定到整个企业（全员）
    Dept,        // 绑定到某个部门
    User,        // 绑定到某个员工
};

// ===========================================================================
// 结构体
// ===========================================================================

// 企业记录。一条对应一家入驻 FlamingoIM 的企业。
struct Enterprise
{
    CString          m_id;                              // 企业唯一 ID，如 "ent-001"
    CString          m_name;                            // 企业全称
    CString          m_shortName;                       // 企业简称（头像 / 紧凑展示用）
    CString          m_industry;                        // 所属行业
    CString          m_region;                          // 所在地区（城市）
    int              m_members  = 0;                    // 员工总数
    int              m_depts    = 0;                    // 部门总数
    EnterpriseStatus m_status   = EnterpriseStatus::Active;  // 入驻状态
    CString          m_plan;                            // 套餐名称（专业版 / 企业版 / 旗舰版）
    CString          m_createdAt;                       // 入驻日期，"YYYY-MM-DD"
    CString          m_admin;                           // 初始管理员姓名
    CString          m_contact;                         // 管理员联系邮箱
    COLORREF         m_logoColor = RGB(0, 0, 0);        // 头像底色
    // 公司根部门 ID（= 服务端 t_enterprise.f_root_dept_id）。组织架构树上代表
    // "公司本身"的那个真实部门，员工可以直接挂在它下面。空串表示服务端尚未为
    // 该企业建立根部门（存量数据迁移之前），此时组织树退回旧的合成根显示。
    CString          m_rootDeptId;
    // 新建账号的前缀（= 服务端 t_enterprise.f_account_prefix，大写；2026-10-06 起）。空串表示没设，
    // 新账号按默认的 EMP 生成。在企业设置窗口里修改，只影响之后新建的账号
    CString          m_accountPrefix;
};

// 组织树节点。部门与员工共用一个结构，由 m_kind 区分：
//   m_kind == Dept 时，m_leader / m_children 有效；
//   m_kind == User 时，m_title 及之后的员工字段有效。
struct OrgNode
{
    OrgNodeKind          m_kind = OrgNodeKind::Dept;  // 节点类型：部门 / 员工

    CString              m_id;          // 节点 ID
    CString              m_name;        // 部门名 / 员工姓名

    // ---- 部门节点字段（m_kind == Dept 时有效）----
    CString              m_leader;      // 部门负责人姓名
    std::vector<OrgNode> m_children;    // 子节点（子部门 + 员工）
    // 是否为"公司根部门"，即组织架构树上代表企业本身的那个节点。它可以新增子部门
    // 和员工，但不允许删除；判定依据是服务端返回的 rootdeptid，不要改用节点 ID
    // 是否等于某个约定字符串来判断。
    bool                 m_isCompanyRoot = false;

    // ---- 员工节点字段（m_kind == User 时有效）----
    CString    m_title;                            // 职位
    CString    m_email;                            // 邮箱
    CString    m_phone;                            // 手机号
    CString    m_joined;                           // 入职日期，"YYYY-MM-DD"
    UserStatus m_userStatus = UserStatus::Active;   // 在职状态
    CString    m_gender;                           // 性别的显示文字（保密 / 男 / 女）
    int        m_genderCode = 0;                   // 性别编码：0 保密 / 1 男 / 2 女（与服务端 enum profile_gender 一致），编辑员工时预选下拉框用
    CString    m_workstation;                      // 工位
    CString    m_landline;                         // 座机
    CString    m_signature;                        // 个性签名（可空）
    CString    m_employeeNo;                       // 工号（= 服务端 f_employee_no，HR 编号）
    CString    m_loginAccount;                     // 登录账号（= 服务端 f_username，登录凭据；与工号 m_employeeNo 不同）
};

// 服务号的一条绑定记录：服务号推送到哪个目标。
struct ServiceBinding
{
    BindingKind m_kind = BindingKind::Enterprise;  // 绑定目标类型
    CString     m_id;                              // 目标 ID（企业 root / 部门 ID / 员工 ID）
    CString     m_label;                           // 目标显示名
};

// 服务号记录。
struct Service
{
    CString                     m_id;                              // 服务号 ID
    CString                     m_name;                            // 服务号名称
    CString                     m_glyph;                           // 头像文字（1-2 字）
    COLORREF                    m_color  = RGB(0, 0, 0);           // 头像底色
    ServiceKind                 m_kind   = ServiceKind::Notification;  // 服务号类型
    CString                     m_desc;                            // 描述
    ServiceStatus               m_status = ServiceStatus::Active;  // 状态
    CString                     m_owner;                           // 负责人姓名
    CString                     m_createdAt;                       // 创建日期，"YYYY-MM-DD"
    std::vector<ServiceBinding> m_bindings;                        // 绑定列表
    // m_bindings 是否已从服务端读到。列表接口（service/list.do）不带绑定，要另调
    // service/detail.do；读到之前 m_bindings 为空不代表「没有绑定」，界面据此区分
    // 「正在读取」与「尚未绑定」。
    bool                        m_bindingsLoaded = false;
    int                         m_msgCount = 0;                    // 累计推送消息数
    int                         m_subs     = 0;                    // 订阅人数
};

// 平台公告记录。
struct Announcement
{
    CString  m_id;                          // 公告 ID
    CString  m_title;                       // 标题
    CString  m_body;                        // 正文
    CString  m_audience;                    // 受众描述
    Severity m_severity  = Severity::Info;  // 严重级别
    CString  m_sender;                      // 发送者
    bool     m_pinned    = false;           // 是否置顶
    bool     m_scheduled = false;           // 是否定时发送
    CString  m_sentAt;                      // 发送 / 计划发送时间，"YYYY-MM-DD HH:MM"
    int      m_read      = 0;               // 已读人数
    int      m_total     = 0;               // 应送达总人数
    int      m_status    = 1;               // 状态：0=草稿 1=已发布 2=已撤回
};

// 「近 14 天消息量」柱状图的一个数据点。
struct MessageTrendPoint
{
    CString m_date;        // 日期文案，格式 "MM-DD"
    int     m_count = 0;   // 当天消息条数
};

// 审计日志记录。所有管理动作均留痕于此。
struct AuditLog
{
    int      m_id       = 0;              // 日志序号（自增）
    CString  m_ts;                        // 时间戳 "YYYY-MM-DD HH:MM:SS"
    CString  m_actor;                     // 操作者
    int      m_actorType = 0;             // 操作者类型：0=管理员 1=系统
    CString  m_ip;                        // 来源 IP
    CString  m_action;                    // 动作代码，如 "login.success"
    CString  m_target;                    // 操作目标
    Severity m_severity = Severity::Info; // 严重级别
    CString  m_detail;                    // 详情描述
};

// ===========================================================================
// 种子数据工厂
//
// 以下函数各返回一份全新的种子数据；由 DataStore 在构造时调用一次。
// ===========================================================================

// 构建 5 家种子企业。
std::vector<Enterprise> BuildSeedEnterprises();

// 构建种子组织树。键为企业 ID，值为该企业组织树的根（部门）节点。
// 当前仅 "ent-001"（明远科技）有完整组织树，其余企业暂无。
std::map<CString, OrgNode> BuildSeedOrgTrees();

// 构建种子服务号。键为企业 ID，值为该企业的服务号列表。
std::map<CString, std::vector<Service>> BuildSeedServices();

// 构建 5 条种子公告（最新在前）。
std::vector<Announcement> BuildSeedAnnouncements();

// 构建 14 条种子审计日志（最新在前）。
std::vector<AuditLog> BuildSeedAuditLogs();

} // namespace flamingoadmin
