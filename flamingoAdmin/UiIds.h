/**
 *  flamingoAdmin —— 业务控件 ID 常量。
 *
 *  DuiFrameWindow 标题栏内置的最小化 / 最大化 / 关闭按钮占用 ID 1 / 2 / 3，
 *  业务控件 ID 一律从 100 起，避免冲突。控件经 SetCtrlId 设置后，点击等
 *  事件经 WM_DUI_NOTIFY 上报时即用这些 ID 区分来源。
 *
 *  balloonwj@qq.com   2026.05.21
 */
#pragma once

namespace flamingoadmin {

// 业务控件 ID。
enum UiId
{
    // ---- 登录页 ----
    kIdLoginAccount  = 101,   // 账户输入框
    kIdLoginPassword = 102,   // 密码输入框
    kIdLoginSubmit   = 103,   // 登录按钮
    kIdLoginNetwork  = 104,   // 「网络设置」链接按钮
    kIdLoginConnInfo = 105,   // 「连接到 <host:port>」信息标签

    // ---- 网络设置页（NetworkSettingsScreen）----
    kIdNetBack = 110,   // 「返回登录」链接
    kIdNetHost = 111,   // 服务器主机输入框
    kIdNetPort = 112,   // 服务器端口输入框
    kIdNetTest = 113,   // 「测试连接」按钮
    kIdNetSave = 114,   // 「保存并返回」按钮

    // ---- 侧边栏导航项（数值顺序与 Screen 枚举一致）----
    kIdNavDashboard     = 201,   // 仪表板
    kIdNavEnterprises   = 202,   // 企业管理
    kIdNavAnnouncements = 203,   // 公告中心
    kIdNavAudit         = 204,   // 审计日志
    kIdNavSettings      = 205,   // 平台设置

    // ---- 侧边栏 / 顶栏其它控件 ----
    kIdLogout        = 210,   // 侧边栏登出按钮
    kIdTopSearch     = 211,   // 顶栏全局搜索框
    kIdTopBell       = 212,   // 顶栏通知铃铛
    kIdSidebarToggle = 213,   // 顶栏「折叠 / 展开侧边栏」按钮

    // ---- 仪表板（DashboardScreen）----
    kIdDashViewEnterprises   = 220,   // 「活跃企业」卡片的「查看全部」链接
    kIdDashViewAnnouncements = 221,   // 「最新公告」卡片的「查看全部」链接
    kIdDashViewAudit         = 222,   // 「近期审计」卡片的「查看全部」链接

    // ---- 企业管理（EnterprisesScreen）----
    kIdEntNew     = 230,   // 「新增企业」按钮（打开新增企业弹窗）
    kIdEntExport  = 231,   // 「导出 CSV」按钮（Phase 3 仅弹 Toast 占位）
    kIdEntSearch  = 232,   // 企业搜索输入框
    kIdEntSeg     = 233,   // 状态分段筛选控件（全部 / 活跃 / 待审核 / 已停用）
    kIdEntMore    = 234,   // 「更多筛选」按钮（Phase 3 仅弹 Toast 占位）
    kIdEntRefresh = 235,   // 「刷新」按钮
    kIdEntTable   = 236,   // 企业管理界面自身（EnterprisesScreen，承载行通知）
    kIdEntTableRows = 237, // 企业表格（DuiVirtualList）—— 行双击经此 ctrlId 上报 DUIN_DBLCLK

    // ---- 新增企业弹窗（NewEnterpriseModal）----
    kIdModalClose    = 240,   // 关闭按钮（X）/ 点击遮罩关闭
    kIdModalCancel   = 241,   // 「取消」按钮
    kIdModalSubmit   = 242,   // 「创建企业」按钮
    kIdModalName     = 243,   // 企业全称输入框
    kIdModalShort    = 244,   // 企业简称输入框
    kIdModalIndustry = 245,   // 所属行业下拉框
    kIdModalRegion   = 246,   // 主要地区输入框
    kIdModalPlan     = 247,   // 套餐下拉框
    kIdModalAdmin    = 248,   // 主管理员姓名输入框
    kIdModalContact  = 249,   // 管理员邮箱输入框

    // ---- 企业详情屏（EnterpriseDetailScreen）----
    kIdDetailScreen   = 260,  // 企业详情屏自身（用于上报组织操作通知）
    kIdDetailBack     = 261,  // 返回（面包屑 / 返回按钮）
    kIdDetailSettings = 262,  // 企业设置按钮（Phase 5 占位）
    kIdDetailAnnounce = 263,  // 发布公告按钮（Phase 5 占位）
    kIdDetailPrimary  = 264,  // 主操作按钮（组织 tab=新增员工 / 服务 tab=新建服务号）
    kIdDetailCreateGroup = 265, // 「新建群」按钮（组织 tab：把企业成员拉进一个新群）
    kIdOrgSearch      = 265,  // 组织架构树搜索框（按部门名 / 员工姓名过滤树）

    // ---- 节点弹窗（NodeModal：新增部门 / 新增员工）----
    kIdNodeClose    = 270,    // 关闭按钮（X）
    kIdNodeCancel   = 271,    // 「取消」按钮
    kIdNodeSubmit   = 272,    // 「创建」按钮
    kIdNodeDeptName = 273,    // 部门名称输入框
    kIdNodeDeptLead = 274,    // 部门负责人输入框
    kIdNodeUserName = 275,    // 员工姓名输入框
    kIdNodeUserTitle = 276,   // 员工职位输入框
    kIdNodeUserEmail = 277,   // 员工邮箱输入框
    kIdNodeUserPhone = 278,   // 员工手机号输入框
    kIdNodeUserGender = 279,  // 员工性别下拉框

    // ---- 通用确认 / 信息弹窗（ConfirmModal）----
    kIdConfirmClose  = 290,   // 关闭按钮（X）
    kIdConfirmCancel = 291,   // 「取消」按钮
    kIdConfirmOk     = 292,   // 「确认」/「知道了」按钮
    kIdConfirmCopy   = 293,   // 「复制密码」按钮（仅信息模式且有可复制文本时）

    // ---- 服务号面板（ServicesPanel）----
    kIdServicesPanel = 300,   // 服务号面板自身（上报新建 / 停用 / 删除等动作）

    // ---- 新建服务号弹窗（ServiceModal）----
    kIdSvcClose  = 310,       // 关闭按钮（X）
    kIdSvcCancel = 311,       // 「取消」按钮
    kIdSvcSubmit = 312,       // 「创建服务号」按钮
    kIdSvcName   = 313,       // 服务号名称输入框
    kIdSvcGlyph  = 314,       // 头像文字输入框
    kIdSvcDesc   = 315,       // 描述输入框

    // ---- 绑定范围弹窗（BindingModal）----
    kIdBindClose  = 320,      // 关闭按钮（X）
    kIdBindCancel = 321,      // 「取消」按钮
    kIdBindSave   = 322,      // 「保存绑定」按钮

    // ---- 公告中心（AnnouncementsScreen）----
    kIdAnnScreen = 330,       // 公告界面自身（上报选公告等动作）
    kIdAnnExport = 331,       // 「导出」按钮
    kIdAnnNew    = 332,       // 「新建公告」按钮
    // 右栏「公告详情」面板(AnnouncementPreviewPanel)的 5 个操作按钮 ——
    // 每个独立 ctrlId,DuiButton 默认 NotifyParent(DUIN_CLICK) 一路冒到
    // AdminFrame 的 WM_DUI_NOTIFY switch,与 kIdAnnExport / kIdAnnNew 同
    // pattern。AdminFrame case 里通过 m_announcementsScreen->GetSelectedId()
    // 取当前 annId。
    kIdAnnPreviewEdit       = 333,  // 「编辑」按钮
    kIdAnnPreviewCopy       = 334,  // 「复制公告」按钮
    kIdAnnPreviewRepush     = 335,  // 「重新推送」按钮
    kIdAnnPreviewTogglePin  = 336,  // 「置顶 / 取消置顶」按钮
    kIdAnnPreviewDelete     = 337,  // 「撤回」按钮(仅已发布显示,把公告改回未发布状态)
    kIdAnnPreviewDeleteAnn  = 338,  // 「删除公告」按钮(任意状态物理删除,服务端写审计)

    // ---- 新建公告弹窗（ComposeModal）----
    kIdComposeClose  = 340,   // 关闭按钮（X）
    kIdComposeCancel = 341,   // 「取消」按钮
    kIdComposeSubmit = 342,   // 「发布」按钮
    kIdComposeTitle  = 343,   // 标题输入框
    kIdComposeBody   = 344,   // 正文输入框
    kIdComposeDate   = 345,   // 定时发送日期输入框
    kIdComposeTime   = 346,   // 定时发送时间输入框
    kIdComposeModal  = 347,   // 弹窗自身（目标选择器上报「拉取某企业组织树」）

    // ---- 审计日志（AuditScreen）----
    kIdAuditRefresh = 350,    // 「刷新」按钮
    kIdAuditExport  = 351,    // 「导出」按钮
    kIdAuditScreen  = 352,    // 审计界面自身（上报筛选切换 / 翻页动作）

    // ---- 平台设置（SettingsScreen）----
    kIdSettingsName        = 400,   // 个人资料 - 姓名输入框
    kIdSettingsSaveProfile = 401,   // 「保存」按钮（提交个人资料）
    kIdSettingsOldPw       = 402,   // 修改密码 - 当前密码输入框
    kIdSettingsNewPw       = 403,   // 修改密码 - 新密码输入框
    kIdSettingsConfirmPw   = 404,   // 修改密码 - 确认新密码输入框
    kIdSettingsChangePw    = 405,   // 「修改密码」按钮

    // ---- 修改员工资料弹窗（EmployeeEditModal）----
    kIdEmpEditClose       = 360,   // 关闭按钮（X）
    kIdEmpEditCancel      = 361,   // 「取消」按钮
    kIdEmpEditSubmit      = 362,   // 「保存」按钮
    kIdEmpEditName        = 363,   // 姓名输入框
    kIdEmpEditTitle       = 364,   // 职位输入框
    kIdEmpEditEmployeeNo  = 365,   // 工号输入框
    kIdEmpEditJoinDate    = 366,   // 入职日期输入框
    kIdEmpEditEmail       = 367,   // 邮箱输入框
    kIdEmpEditPhone       = 368,   // 手机号输入框
    kIdEmpEditLandline    = 369,   // 座机输入框
    kIdEmpEditWorkstation = 370,   // 工位地址输入框
    kIdEmpEditSignature   = 371,   // 个性签名输入框

    // ---- 企业设置弹窗（EnterpriseSettingsModal）----
    kIdEntSettings     = 380,   // 弹窗自身（上报「移除管理员」动作）
    kIdEntSetClose     = 381,   // 关闭按钮（X）
    kIdEntSetCancel    = 382,   // 「取消」按钮
    kIdEntSetSave      = 383,   // 「保存企业信息」按钮
    kIdEntSetAddAdmin  = 384,   // 「添加」管理员按钮
    kIdEntSetName      = 385,   // 企业全称输入框
    kIdEntSetShort     = 386,   // 企业简称输入框
    kIdEntSetIndustry  = 387,   // 所属行业输入框
    kIdEntSetRegion    = 388,   // 主要地区输入框

    // ---- 新建群弹窗（GroupModal）----
    kIdGroupModal   = 390,   // 弹窗自身
    kIdGroupClose   = 391,   // 关闭按钮（X）
    kIdGroupCancel  = 392,   // 「取消」按钮
    kIdGroupSubmit  = 393,   // 「创建」按钮
    kIdGroupName    = 394,   // 群名称输入框
    kIdEntSetPlan      = 389,   // 套餐下拉框
    kIdEntSetNewName   = 390,   // 新增管理员 - 姓名输入框
    kIdEntSetNewEmail  = 391,   // 新增管理员 - 邮箱输入框
};

} // namespace flamingoadmin
