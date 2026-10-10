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
    kIdLoginRememberPwd = 106, // 「记住密码」复选框（勾选后口令经 DPAPI 加密存入 ini）

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
    // 客户端版本：仅平台超管可见（客户端版本是平台级资产，不属于任何企业），
    // 侧栏在登录后按管辖范围决定加不加这一项。数值必须紧接上一项 ——
    // AdminFrame 的导航分发靠 kNavScreenBase + (ctrlId - kIdNavDashboard)
    // 映射到 Screen 枚举，跳号会把界面切错。
    kIdNavClientVersions = 206,  // 客户端版本
    // 敏感词：平台超管与企业管理员都可见（企业管理员维护本企业的词库）。
    // 数值同样必须紧接上一项，理由同上。
    kIdNavSensitiveWords = 207,  // 敏感词
    // 文件共享与共享审计（docs/文件共享方案.md 第 10 节）：平台超管与企业管理员都可见。
    // 数值同样必须紧接上一项。2026-10-08 起「共享审计」并入文件共享页成为第二个页签，侧栏不再建这一项；
    // 209 仍然保留（Screen::FileShareAudit 靠它的序号与后面的导航项对齐），不得挪作他用。
    kIdNavFileShares     = 208,  // 文件共享（「文件共享」「共享审计」两个页签）
    kIdNavFileShareAudit = 209,  // 保留编号：原为侧栏「共享审计」导航项，现不再创建
    // 企业问答（docs/群问答方案.md）：平台超管与企业管理员都可见。数值同样必须紧接上一项；
    // 210 原是登出按钮，2026-10-07 登出按钮改为 214，让出 210 给本项。再要加导航项时同样先把 211、212 之外的控件挪开
    kIdNavEnterpriseQa   = 210,  // 企业问答
    // 机器人（docs/机器人方案.md 第 7 节，2026-10-08）：平台超管与企业管理员都可见。数值同样必须紧接上一项。
    // 211 历史为顶栏的「全局搜索框」，2026-08-12 删除后源码里已没有引用（2026-10-08 全工程搜索 211 与旧控件名核实），
    // 故直接取用，不必挪开别的控件。再要加导航项时 212 也可直接取用，再往后须先把 213、214 挪开
    kIdNavEnterpriseBot  = 211,  // 机器人

    // ---- 侧边栏 / 顶栏其它控件 ----
    kIdLogout        = 214,   // 登出按钮（2026-08-12 起位于顶栏右上角，此前在侧边栏页脚；2026-10-07 由 210 改为 214）
    // 211 / 212 历史为顶栏的「全局搜索框」与「通知铃铛」，2026-08-12 移除这两个
    // 控件（搜索一直未接入实际跳转，铃铛也无通知源）。编号曾留空不复用，避免日后
    // 新控件误用旧 ID 时与残留代码对上；2026-10-08 核实没有残留代码后，211 给了导航项「机器人」。
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
    // 「更多筛选」条（点「更多筛选」后在筛选行下方展开的一行；本段只剩 238、239 两个空位，故从 250 起取号）
    kIdEntMoreIndustry = 250, // 行业下拉框（第 0 项「不限行业」）
    kIdEntMorePlan     = 251, // 套餐下拉框（第 0 项「不限套餐」，第 i 项为套餐编号 i - 1）
    kIdEntMoreFrom     = 252, // 开通起始日期输入框（YYYY-MM-DD）
    kIdEntMoreTo       = 253, // 开通截止日期输入框（YYYY-MM-DD）
    kIdEntMoreClear    = 254, // 「清除」按钮：清空以上四项条件

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
    kIdOrgSearch      = 266,  // 组织架构树搜索框（按部门名 / 员工姓名过滤树）
    kIdOrgRefresh     = 267,  // 组织树工具条「刷新」按钮（重新拉取部门树）
    kIdOrgExpandAll   = 268,  // 组织树工具条「展开全部」按钮
    kIdOrgCollapse    = 269,  // 组织树工具条「折叠」按钮

    // ---- 节点弹窗（NodeModal：新增部门 / 新增员工）----
    kIdNodeClose    = 270,    // 关闭按钮（X）
    kIdNodeCancel   = 271,    // 「取消」按钮
    kIdNodeSubmit   = 272,    // 「创建」按钮
    kIdNodeDeptName = 273,    // 部门名称输入框
    kIdNodeDeptLead = 274,    // 部门负责人下拉框（可输入姓名筛选）
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
    kIdEmpEditGender      = 372,   // 性别下拉框（保密 / 男 / 女）

    // ---- 企业设置弹窗（EnterpriseSettingsModal）----
    kIdEntSettings     = 380,   // 弹窗自身（上报「移除管理员」动作）
    kIdEntSetClose     = 381,   // 关闭按钮（X）
    kIdEntSetCancel    = 382,   // 「取消」按钮
    kIdEntSetSave      = 383,   // 「保存企业信息」按钮
    kIdEntSetAddAdmin  = 384,   // 「添加」管理员按钮
    kIdEntSetName      = 385,   // 企业全称输入框
    kIdEntSetShort     = 386,   // 企业简称输入框
    kIdEntSetIndustry  = 387,   // 所属行业下拉框（2026-10-04 起由输入框改为下拉框）
    kIdEntSetRegion    = 388,   // 主要地区输入框
    kIdEntSetPlan      = 389,   // 套餐下拉框
    // 下面两个跳到 395 起：本段 380-389 已用满，而 390-394 归新建群弹窗。
    // 它们曾被误编为 390 / 391，与 kIdGroupModal / kIdGroupClose 撞号 ——
    // 控件 ID 是 WM_DUI_NOTIFY 分辨事件来源的唯一依据，撞号会让点击落到
    // 另一个弹窗的分支上，故此处宁可跳号也不复用。
    kIdEntSetNewName   = 395,   // 新增管理员 - 姓名输入框
    kIdEntSetNewEmail  = 396,   // 新增管理员 - 邮箱输入框
    kIdEntSetPrefix    = 397,   // 账号前缀输入框（2026-10-06，企业信息第三行右列）

    // ---- 新建群弹窗（GroupModal）----
    kIdGroupModal   = 390,   // 弹窗自身
    kIdGroupClose   = 391,   // 关闭按钮（X）
    kIdGroupCancel  = 392,   // 「取消」按钮
    kIdGroupSubmit  = 393,   // 「创建」按钮
    kIdGroupName    = 394,   // 群名称输入框

    // ---- 客户端版本界面（ClientVersionsScreen）----
    // 取 430 起而不是紧接 396：400-405 已归平台设置界面（kIdSettings*）。
    // 控件 ID 是 WM_DUI_NOTIFY 分辨事件来源的唯一依据，撞号会让点击落到
    // 另一个界面的分支上 —— 这里最初就误编成 400 起，编译期 case 重复才发现。
    kIdCvRefresh    = 430,   // 页头「刷新」按钮
    kIdCvNew        = 431,   // 页头「新建版本」按钮
    // 表格行内的操作（发布 / 撤回 / 回滚 / 编辑）都由本控件自绘并自行命中，
    // 统一用这一个 ID 上报；具体点了哪一行的哪个动作经 GetPendingAction 取。
    kIdCvRowAction  = 432,   // 表格行内操作

    // ---- 新建 / 编辑客户端版本弹窗（ClientVersionModal）----
    kIdCvModal      = 440,   // 弹窗自身（上报「刷新候选包」动作）
    kIdCvModalClose = 441,   // 关闭按钮（X）
    kIdCvModalCancel = 442,  // 「取消」按钮
    kIdCvModalSaveDraft = 443,  // 「存为草稿」按钮
    kIdCvModalPublish   = 444,  // 「直接发布」按钮
    kIdCvModalRescan    = 445,  // 「重新扫描」按钮
    kIdCvModalVersion   = 446,  // 版本号输入框
    kIdCvModalMinVer    = 447,  // 最低版本号输入框
    kIdCvModalNote      = 448,  // 更新说明输入框
    kIdCvModalPackage   = 449,  // 候选包下拉框
    kIdCvModalForced    = 450,  // 「强制升级」复选框

    // ---- 敏感词界面（SensitiveWordsScreen，docs/敏感词方案.md 第 7 节）----
    // 取 460 起，与上面客户端版本弹窗的 440-450 之间留出空档。
    kIdSwTabs       = 460,   // 「词库 / 拦截记录」页签切换（SegControl）
    kIdSwEnterprise = 461,   // 企业选择框（仅平台超管可见）
    kIdSwSwitch     = 462,   // 敏感词过滤总开关（DuiSwitch）
    kIdSwSearch     = 463,   // 搜索框（按词条模糊搜索）
    kIdSwCategory   = 464,   // 分类筛选下拉框
    kIdSwAdd        = 465,   // 「添加」按钮
    kIdSwWordTable  = 466,   // 词库表格
    kIdSwImport     = 467,   // 「批量导入」按钮
    kIdSwExport     = 468,   // 「导出」按钮

    // ---- 添加 / 编辑敏感词弹窗（SensitiveWordModal）----
    kIdSwModal         = 470,   // 弹窗自身
    kIdSwModalClose    = 471,   // 关闭按钮（X）
    kIdSwModalCancel   = 472,   // 「取消」按钮
    kIdSwModalSave     = 473,   // 「保存」按钮
    kIdSwModalWord     = 474,   // 词条输入框
    kIdSwModalCategory = 475,   // 分类下拉框
    kIdSwModalRemark   = 476,   // 备注输入框

    // ---- 批量导入敏感词弹窗（SensitiveImportModal）----
    kIdSwImpModal      = 480,   // 弹窗自身
    kIdSwImpClose      = 481,   // 关闭按钮（X）
    kIdSwImpCancel     = 482,   // 「取消」按钮
    kIdSwImpSubmit     = 483,   // 「开始导入」按钮；导入结束后变为「完成」
    kIdSwImpReadFile   = 484,   // 「从文件读取」按钮
    kIdSwImpText       = 485,   // 多行输入框（导入结束后显示汇总）
    kIdSwImpCategory   = 486,   // 默认分类下拉框

    // ---- 敏感词界面「拦截记录」页签 ----
    kIdSwHitRange      = 490,   // 时间范围下拉框（近 7 天 / 近 30 天 / 近 180 天）
    kIdSwHitSearch     = 491,   // 发送者搜索框
    kIdSwHitSource     = 492,   // 来源筛选下拉框
    kIdSwHitCategory   = 493,   // 分类筛选下拉框
    kIdSwHitTable      = 494,   // 拦截记录表格

    // ---- 功能开关面板（FeatureSwitchPanel，docs/文件共享方案.md 第 10 节）----
    // 取 600 起：与并行开发的其它功能事先约定好编号段，避免各自新增控件时撞号。
    // 2026-10-08 起全部功能开关集中到企业详情屏的「功能开关」子 tab（FeatureSwitchesPage），沿用原企业设置弹窗那一组
    // 编号（名称去掉了 Modal）；原平台设置页「功能开关」卡那一组编号随卡片删除而作废，不得复用。
    kIdFeatSwitchFileShare  = 600,   // 「功能开关」页里的「文件共享」开关（fileshare.enabled）
    // 601 已作废：原为平台设置页里的「文件共享」开关
    kIdFeatSwitchP2P        = 602,   // 「功能开关」页里的「客户端点对点直连」开关（p2p.enabled，docs/P2P通信方案.md 9.5）
    // 603 已作废：原为平台设置页里的「客户端点对点直连」开关
    kIdFeatSwitchInviteConfirm = 604,   // 「功能开关」页里的「邀请入群需被邀请人同意」开关（docs/邀请入群方案.md §3.5）
    kIdFeatSwitchRegister   = 605,   // 「功能开关」页里的「允许用户自助注册」开关（register.enabled）
    // 606 已作废：原为平台设置页里的「允许用户自助注册」开关
    kIdFeatSwitchGroupCall  = 607,   // 「功能开关」页里的「群语音 / 群视频通话」开关（docs/群音视频通话方案.md 第 6 期）
    // 608 已作废：原为平台设置页里的「群语音 / 群视频通话」开关

    // ---- 文件共享页（FileSharesScreen）----
    kIdFsEnterprise  = 610,   // 企业筛选框（仅平台超管可见，第一项为「全部企业」）
    kIdFsSearch      = 611,   // 分享者搜索框
    kIdFsState       = 612,   // 状态筛选下拉框（全部 / 有效 / 已取消）
    kIdFsRefresh     = 613,   // 页头「刷新」按钮
    kIdFsTable       = 614,   // 共享表格（选中行变化经 DUIN_VALUECHANGED 上报）
    kIdFsMemberTable = 615,   // 详情卡里的成员表格
    // 2026-10-08 起「共享审计」并入文件共享页成为它的第二个页签（FileShareCenterScreen）：页头与「刷新」属于外层页面，
    // kIdFsRefresh 刷新当前页签；kIdFsTabs 是页头右侧（「刷新」左边）的页签切换
    kIdFsTabs        = 616,   // 「文件共享 / 共享审计」页签切换（SegControl，DUIN_VALUECHANGED 的 extra 为页签下标）

    // ---- 功能开关面板（续）：600～609 段的编号不够两个一组时，从 620 起接着分配 ----
    kIdFeatSwitchCaption     = 620,   // 「功能开关」页里的「语音字幕」开关（docs/语音字幕方案.md 第 10 节）
    // 621 已作废：原为平台设置页里的「语音字幕」开关
    kIdFeatSwitchCloudRecord = 622,   // 「功能开关」页里的「云端录制」开关（docs/屏幕共享与录制方案.md 12.6.5）
    // 623 已作废：原为平台设置页里的「云端录制」开关
    kIdCloudQuotaSave        = 624,   // 「功能开关」页「云端录像空间」面板的「保存」按钮
    kIdCloudQuotaQuota       = 625,   // 「功能开关」页「云端录像空间」面板的配额输入框（GB）
    kIdCloudQuotaDays        = 626,   // 「功能开关」页「云端录像空间」面板的保留天数输入框
    // 627～629 已作废：原为平台设置页「云端录像空间」面板的「保存」按钮、配额输入框与保留天数输入框

    // ---- 共享审计页（FileShareAuditScreen）----
    kIdFaEnterprise  = 630,   // 企业筛选框（仅平台超管可见，第一项为「全部企业」）
    kIdFaRange       = 631,   // 时间范围下拉框
    kIdFaSearch      = 632,   // 操作人搜索框
    kIdFaAction      = 633,   // 动作筛选下拉框
    kIdFaShareId     = 634,   // 共享 ID 输入框
    // 635 已作废：原为共享审计页页头的「刷新」按钮，2026-10-08 页头移到外层页面后改用 kIdFsRefresh
    kIdFaTable       = 636,   // 审计表格（选中行变化经 DUIN_VALUECHANGED 上报）

    // ---- 功能开关面板（续二）：620～629 已用完、630 起是共享审计页，从 640 起接着分配 ----
    kIdFeatSwitchSecretChat  = 640,   // 「功能开关」页里的「单聊密聊」开关（docs/密聊方案.md 4.9）
    // 641 已作废：原为平台设置页里的「单聊密聊」开关
    kIdFeatSwitchThreadGroup = 642,   // 「功能开关」页里的「帖子群」开关（docs/帖子群方案.md 4.7）
    // 643 已作废：原为平台设置页里的「帖子群」开关
    // 机器人的四个开关（docs/机器人方案.md 4.1，2026-10-08）；645、647、649、651 已作废，原为平台设置页里的同名开关
    kIdFeatSwitchBot              = 644,   // 「功能开关」页里的「机器人」总开关（bot.enabled）
    kIdFeatSwitchBotGroup         = 646,   // 「功能开关」页里的「允许群主添加群机器人」开关（bot.groupbot）
    kIdFeatSwitchBotPersonal      = 648,   // 「功能开关」页里的「允许员工配置个人机器人」开关（bot.personal）
    kIdFeatSwitchBotGroupExternal = 650,   // 「功能开关」页里的「允许群主给群机器人配置外部服务」开关（bot.groupexternal）

    // ---- 企业问答页（EnterpriseQaScreen，docs/群问答方案.md）：取 660 起，与上面的功能开关段留出空档 ----
    kIdQaEnterprise  = 660,   // 企业选择框（仅平台超管可见）
    kIdQaSearch      = 661,   // 搜索框（按问题或答案筛选，只在本地筛选）
    kIdQaRefresh     = 662,   // 「刷新」按钮
    kIdQaAdd         = 663,   // 「新增问答」按钮
    kIdQaTable       = 664,   // 问答表格

    // ---- 新增 / 编辑企业问答弹窗（EnterpriseQaModal）----
    kIdQaModal         = 670,   // 弹窗自身
    kIdQaModalClose    = 671,   // 关闭按钮（X）
    kIdQaModalCancel   = 672,   // 「取消」按钮
    kIdQaModalSave     = 673,   // 「保存」按钮
    kIdQaModalQuestion = 674,   // 问题输入框
    kIdQaModalAnswer   = 675,   // 答案输入框（多行）
    kIdQaModalScope    = 676,   // 应用范围下拉框（全部群 / 所选的群）
    kIdQaModalGroups   = 677,   // 群列表（带勾选框，应用范围为「所选的群」时可用）

    // ---- 机器人页（EnterpriseBotScreen，docs/机器人方案.md 第 7 节）：取 680 起 ----
    kIdBotEnterprise = 680,   // 企业选择框（仅平台超管可见）
    kIdBotRefresh    = 681,   // 「刷新」按钮
    kIdBotAdd        = 682,   // 「新建机器人」按钮
    kIdBotDomains    = 683,   // 「外部服务域名白名单」按钮
    kIdBotTable      = 684,   // 机器人表格

    // ---- 新建 / 编辑企业机器人的编辑窗（EnterpriseBotModal）：690～712 ----
    kIdBotModal           = 690,   // 编辑窗自身
    kIdBotModalClose      = 691,   // 关闭按钮（X）
    kIdBotModalCancel     = 692,   // 「取消」按钮
    kIdBotModalSave       = 693,   // 「保存」按钮
    kIdBotModalName       = 694,   // 名称输入框
    kIdBotModalGlyph      = 695,   // 头像文字输入框
    kIdBotModalDesc       = 696,   // 简介输入框
    kIdBotModalStatus     = 697,   // 状态下拉框（启用 / 停用）
    kIdBotModalAllowOff   = 698,   // 「允许群主在本群关闭」勾选框
    kIdBotModalScope      = 699,   // 应用范围下拉框（全部群 / 所选的群）
    kIdBotModalGroups     = 700,   // 群列表（带勾选框，应用范围为「所选的群」时可用）
    kIdBotModalRules      = 701,   // 规则列表（选中行变化经 DUIN_VALUECHANGED、双击经 DUIN_DBLCLK 上报）
    kIdBotModalRuleAdd    = 702,   // 规则「添加」按钮
    kIdBotModalRuleEdit   = 703,   // 规则「编辑」按钮
    kIdBotModalRuleUp     = 704,   // 规则「上移」按钮
    kIdBotModalRuleDown   = 705,   // 规则「下移」按钮
    kIdBotModalRuleDelete = 706,   // 规则「删除」按钮
    kIdBotModalCopy       = 707,   // Webhook「复制」按钮（地址、令牌、签名密钥放进剪贴板）
    kIdBotModalResetToken = 708,   // Webhook「重新生成」按钮
    kIdBotModalTryText    = 709,   // 「试一试」输入框
    kIdBotModalTryAt      = 710,   // 「试一试」的「@ 机器人」勾选框
    kIdBotModalTryGroup   = 711,   // 「试一试」的群下拉框
    kIdBotModalTryRun     = 712,   // 「试一试」按钮

    // ---- 添加 / 编辑规则的规则编辑窗（BotRuleModal）：720～748 ----
    kIdBotRule            = 720,   // 规则编辑窗自身
    kIdBotRuleClose       = 721,   // 关闭按钮（X）
    kIdBotRuleCancel      = 722,   // 「取消」按钮
    kIdBotRuleSave        = 723,   // 「确定」按钮
    kIdBotRuleName        = 724,   // 规则名称输入框
    kIdBotRuleEnabled     = 725,   // 「启用这条规则」勾选框
    kIdBotRuleTrigger     = 726,   // 触发方式下拉框
    kIdBotRuleKeywords    = 727,   // 关键词输入框
    kIdBotRuleMatch       = 728,   // 匹配方式下拉框
    kIdBotRuleNeedAt      = 729,   // 「必须同时 @ 机器人」勾选框
    kIdBotRuleSchedKind   = 730,   // 定时种类下拉框（每天 / 工作日 / 每周 / 每月）
    kIdBotRuleSchedTime   = 731,   // 定时时刻输入框（HH:MM）
    kIdBotRuleSchedDay    = 732,   // 每月几号输入框
    kIdBotRuleSchedGroups = 733,   // 定时发往的群（带勾选框）
    kIdBotRuleWindowOn    = 734,   // 「只在以下时间段响应」勾选框
    kIdBotRuleWindowDays  = 735,   // 时间段的日子下拉框（每天 / 工作日 / 周末）
    kIdBotRuleWindowFrom  = 736,   // 时间段起始时刻输入框
    kIdBotRuleWindowTo    = 737,   // 时间段结束时刻输入框
    kIdBotRuleAction      = 738,   // 动作下拉框
    kIdBotRuleText        = 739,   // 回复文字 / 兜底文字输入框（多行）
    kIdBotRuleMarkdown    = 740,   // 「按 Markdown 显示」勾选框
    kIdBotRuleUrl         = 741,   // 外部服务地址输入框
    kIdBotRuleWeekdayFirst = 742,  // 每周定时的「一」勾选框；「二」～「日」依次为 743～748
    kIdBotRuleWeekdayLast  = 748,  // 每周定时的「日」勾选框

    // ---- 外部服务域名白名单窗（BotDomainsModal）：760～764 ----
    kIdBotDomainsModal  = 760,   // 白名单窗自身
    kIdBotDomainsClose  = 761,   // 关闭按钮（X）
    kIdBotDomainsCancel = 762,   // 「取消」按钮
    kIdBotDomainsSave   = 763,   // 「保存」按钮
    kIdBotDomainsText   = 764,   // 白名单输入框（多行，一行一个域名）
};

} // namespace flamingoadmin
