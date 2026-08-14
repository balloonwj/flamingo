# flamingoAdmin —— 界面设计与实施计划

> 本文档是 flamingoAdmin 的界面设计计划与开发路线图，随实施进度更新。
> 开发者：balloonwj@qq.com　|　最后更新：2026.05.21

---

## 1. 项目概述

flamingoAdmin 是 **FlamingoIM 的企业管理控制台**，与 `flamingoserver` /
`flamingoclient` / `flamingoAndroid` 并列，是仓库根目录下的一个新组件。

- **形态**：Windows 桌面应用（无边框自绘窗口）。
- **UI 框架**：`balloonui`（仓内 `third-party/balloonui/` 的无 HWND DUI 控件库，与 PC 客户端共用同一份）。
- **数据**：当前阶段**全部 mock**，由内存数据层 `Mock/DataStore` 提供；
  数据层与 UI 解耦，未来接真实服务端时只替换数据层实现，界面层不动。

## 2. 设计稿

- **来源**：`design/FlamingoIM Enterprise Admin.html`
- **性质**：bundler 打包的 React 原型；解包后得到 12 个组件源文件、种子数据
  与主题 CSS。本计划即依据解包结果整理。
- **基准帧**：1440 × 900。
- **配色**：flamingo 品牌色（主色 `#ff5a44`）+ ink 冷灰中性色阶；语义色
  ok / warn / info / danger。详见 `Theme/AdminTheme.h`。

## 3. 完整 UI 清单

合计 **3 个外壳组件 + 8 个界面 + 4 个弹窗 + ~10 个复用 widget**，与设计稿一一对应。

### 3.1 窗口外壳（所有界面共用）

| 组件 | 说明 |
|---|---|
| 标题栏 Titlebar | F 图标 + 标题 + 连接状态文字 + 审计滚动提示(AuditTicker) + 最小化/最大化/关闭三按钮 |
| 侧边栏 Sidebar | 224px 标准 / 64px 紧凑两态；5 个导航项 + 底部用户信息与登出 |
| 顶栏 TopBar | 面包屑 + 全局搜索框(⌘K) + 通知铃铛 |

### 3.2 八个主界面

| # | 界面 | 主要内容 |
|---|---|---|
| 1 | **登录** LoginScreen | 左：品牌渐变面板（logo / 标语 / 3 个卖点 / 版本号）；右：登录表单卡（账户、密码+显隐、记住登录、错误提示、登录按钮、TLS 脚注） |
| 2 | **仪表板** DashboardScreen | 页头问候语；左列：4 个统计卡 + 近 14 天消息量柱状图 + 活跃企业列表；右列：最新公告 + 近期审计 |
| 3 | **企业管理** EnterprisesScreen | 页头 + 新建按钮；统计卡；分段筛选（全部 / 活跃 / 待审 / 停用）；企业卡片列表（logo / 名称 / 行业 / 地区 / 人数 / 状态徽章） |
| 4 | **组织架构** OrgScreen | 左：组织树（部门 / 员工节点，可展开、增删）；右：详情面板 —— 员工详情（6 个 tab）/ 部门详情 / 未选中占位 |
| 5 | **服务号** ServicesScreen | 服务号网格（头像 / 名称 / 类型徽章 / 描述 / 状态 / 消息数 / 订阅数）；右：服务号详情（设置 / 绑定 / 最近消息 3 个 tab） |
| 6 | **公告中心** AnnouncementsScreen | 公告列表（级别圆点 / 标题 / 受众 / 时间 / 已读数 / 置顶）+ 右侧预览面板 |
| 7 | **审计日志** AuditScreen | 过滤器（动作 / 级别 / 时间）+ 审计表格（时间 / 操作者 / IP / 动作 / 目标 / 级别 / 详情）+ 详情面板 |
| 8 | **平台设置** SettingsScreen | 占位屏（图标 + 标题 + 副标题，设计稿本身即占位） |

### 3.3 弹窗 Modals

| 弹窗 | 所属界面 |
|---|---|
| 新建企业 NewEnterpriseModal | 企业管理 |
| 增删组织节点 NodeModal（新增部门 / 新增员工） | 组织架构 |
| 服务号创建/编辑 ServiceModal + 绑定选择器 BindingPicker | 服务号 |
| 撰写公告 ComposeAnnouncement（标题 / 正文 / 受众 / 级别 / 定时发送） | 公告中心 |

### 3.4 复用 widget

`Card` 卡片容器 · `StatTile` 统计卡 · `StatusChip / SeverityChip / KindChip`
状态徽章 · `BarChart` 柱状图 · `Sparkline` 迷你折线 · `GradientPanel` 登录渐变
面板 · `Toast` 提示栈 · `SeverityDot` 级别圆点 · `Avatar` 头像 · `Toggle` 开关

## 4. 技术架构

### 4.1 目录结构

```
flamingoAdmin/
├── flamingoAdmin.sln              含 flamingoAdmin + balloonui(依赖项)
├── flamingoAdmin.vcxproj(.filters)4 配置(Win32/x64 × Debug/Release)，/utf-8
├── main.cpp                       WinMain + AdminFrame(DuiFrameWindow) + 屏路由
├── stdafx.h  resource.h  app.rc  app.ico
├── Theme/      AdminTheme.h        调色板 / 字体 / 圆角 / 阴影 / 间距 命名常量
├── Util/       Logger.h/.cpp       轻量文件日志器（关键路径打点）
├── Mock/       MockData.h/.cpp     数据结构 + 种子数据
│               DataStore.h/.cpp    内存 store + 变更 API（单例）
├── Shell/      Sidebar.*  TopBar.* 外壳组件
├── Screens/    LoginScreen.* … SettingsScreen.*   8 个界面
├── Widgets/    Card.* StatTile.* …                复用 widget
├── Tests/      自建轻量测试框架 + Mock 数据层单元测试（flamingoAdminTests 工程）
└── docs/       UI设计计划.md       本文档
```

### 4.2 数据层与 UI 解耦

- `Mock/DataStore` 是内存态单例，持有全部 mock 数据并暴露变更 API
  （`CreateEnterprise` / `SendAnnouncement` / `PushAudit` 等）。
- 所有界面只通过 `DataStore` 读写数据，不内嵌数据。
- 未来接真实 `chatserver` 时，只需替换 `DataStore` 实现，界面层零改动。

### 4.3 链接方式

flamingoAdmin.exe 采用**全静态链接**，最终产物不依赖任何非系统 DLL：

- **balloonui 静态链接**：链接 `balloonui_static.lib`（由 `balloonui_static.vcxproj`
  产出），不定义 `BUI_USE_DLL`，运行时无需 `balloonui.dll`。`balloonui_static.vcxproj`
  作为 `.sln` 依赖项参与构建。
- **CRT 静态链接**：Debug `/MTd`、Release `/MT`，运行时不依赖 VC++ 运行库 DLL。
  `balloonui_static.vcxproj` 同样以 `/MTd` `/MT` 编译，两侧 CRT 一致。
- 产物 `flamingoAdmin.exe` 仅依赖 Windows 系统 DLL（gdiplus / comctl32 / kernel32 等）。

### 4.4 防崩溃架构（页面 / 屏 / 弹窗一律常驻）

balloonui 的宿主（`DuiHost`）会跟踪「当前 hover / 焦点 / 鼠标按下」的控件裸
指针。若在事件回调里销毁一棵<u>包含被跟踪控件</u>的控件树，宿主即持有悬空
指针，下一次鼠标事件就 use-after-free 崩溃。flamingoAdmin 用三个「容器」从
结构上杜绝这一点 —— 它们都「常驻、切显隐、绝不销毁」：

| 容器 | 位置 | 职责 |
|---|---|---|
| `RootContainer` | 客户区根 | 页面层（登录 / 外壳）+ 弹窗层 + Toast 层 |
| `ScreenHost` | 外壳内容区 | 5 个主界面，切换只切显隐 |
| `NewEnterpriseModal` | `RootContainer` 弹窗层 | 半透明遮罩 + 表单卡片，开 / 关只切显隐 |

配套两条约定：

- **结构 / 可见性变动延迟执行**：导航、登出、弹窗开关均经
  `PostMessage(WM_APP_NAVIGATE)` 延迟到 DUI 事件派发栈结束后再执行。
- **弹窗打开时隐藏外壳内真 EDIT 子窗**：`DuiEditHost` 是寄宿真 HWND 的控件，
  HWND 子窗总绘制在 DUI 背景之上，不隐藏会透过半透明遮罩显形。

## 5. 分阶段实施计划

每阶段末暂停，等 review 通过后再进入下一阶段。

| 阶段 | 内容 | 状态 |
|---|---|---|
| **0 脚手架** | 目录、`.sln`/`.vcxproj`、balloonui 依赖、post-build 拷 DLL、最小空窗口 | ✅ 已完成（Debug x64 编译 0 错误，窗口可运行） |
| **1 主题+数据层** | `AdminTheme.h`、`Logger`、`MockData`、`DataStore`；`flamingoAdminTests` 单元测试 | ✅ 已完成（19 用例 / 50 断言全过，Debug x64） |
| **2 外壳+登录** | `AdminFrame`、`Sidebar`、`TopBar`、`LoginScreen`、屏路由 | ✅ 已完成（登录 / 屏切换 / 登出反复无崩溃，Debug x64） |
| **3 核心屏** | `DashboardScreen`、`EnterprisesScreen` + 新建企业弹窗 | ✅ 已完成（人工验收通过，Debug x64） |
| **4a-① 企业详情+组织树** | `EnterpriseDetailScreen`、`OrgTree`、`OrgDetailPanel`、`NodeModal`、`ConfirmModal`（删除二次确认） | ✅ 已完成（人工验收通过，Debug x64） |
| **4a-② 员工详情 5 个 tab** | 权限角色 / 服务号 / 消息会话 / 设备登录 / 操作历史 tab | ✅ 已完成（人工验收通过，Debug x64） |
| **4b-① 服务号面板** | `ServicesPanel`（列表 + 4-tab 详情）、`ServiceModal`（新建）、停用 / 删除 | ✅ 已完成（人工验收通过，Debug x64） |
| **4b-② 绑定编辑器** | `BindingModal`（企业 / 部门 / 员工 绑定选择器） | ✅ 已完成（编译 0 错误，Debug x64；待人工验收） |
| **5a 公告中心** | `AnnouncementsScreen`（列表 + 预览）、`ComposeModal`（新建公告） | ✅ 已完成（人工验收通过，Debug x64） |
| **5b 审计+设置** | `AuditScreen`（统计 + 筛选 + 表格 + 详情）；平台设置沿用占位屏 | ✅ 已完成（编译 0 错误，Debug x64；待人工验收） |
| **6 收尾** | 顶栏审计滚动提示 AuditTicker + 可折叠侧边栏（224↔64 两态）；全量 Rebuild 验证 | ✅ 已完成（编译 0 错误，Debug x64；待人工验收） |
| **追加 登录网络设置** | `Util/AppConfig`（flamingoAdmin.ini 持久化）、`NetworkSettingsScreen`（服务器地址 / 端口 / TLS）、`Util/NetProbe`（工作线程真实 TCP 连通性探测） | ✅ 已完成（编译 0 错误，Debug x64；待人工验收） |

## 6. 代码与测试规范

严格遵循仓库根开发约定文档的「代码与测试规范」：

- Allman 花括号；`namespace` 左花括号同行、嵌套不缩进。
- 成员变量 `m_` 前缀；类、public 方法、文件头均有注释；enum 每个值有注释。
- 禁魔数：所有有含义的数值进 `AdminTheme.h` 或各屏 layout 常量块。
- 关键路径（登录、屏切换、数据变更）经 `Util/Logger` 打日志。
- 文件头文档注释，开发者标识统一 `balloonwj@qq.com`。
- 源文件 UTF-8（无 BOM）+ `/utf-8` 编译开关，与 balloonui 子项目一致。
- 测试：每个新功能先与负责人确认测试用例与框架，再写产品代码。
