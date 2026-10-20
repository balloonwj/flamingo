# 已知问题 (bugs)

记录排查中发现、尚未修复的问题。每条注明：发现日期 / 现象 / 复现 / 影响 / 初步排查 / 状态。

**问题修复并验证之后，直接删掉该条目，同时删掉代码注释与文档里对它的引用（如「见 bugs.md BUG-xxx」），不保留「已修复」状态的条目**（2026-10-10 起）。已用过的编号不复用：新条目的编号取下一行记的值加一，并同步更新这一行。

> 已用到的最大编号：**BUG-166**

> 发现日期一律用 `YYYY-MM-DD`。BUG-1 至 BUG-4 填的是本文件建档那天（2026-08-10），
> 因为这四条是随建档一次性写入的，实际发现日期已无从考证；此后新增的条目填真实发现日期。

---

## BUG-1　chatserver 在并发 admin 请求下崩溃（access violation）

- **发现日期**：2026-08-10（建档日期，实际发现日期不详）
- **现象**：chatserver 运行中突然消失，进程退出码 `0xC0000005`（访问违例）。
- **复现**：在 flamingoAdmin 的「企业管理」tab 上连续操作（多次刷新 / 改状态 → 连续触发 `enterprise/list.do`、`enterprise/setstatus.do`），服务端多个 I/O 线程并发处理时崩溃。本次调试中已自发崩溃 3 次。
  - 崩溃前最后日志为某次 `list.do` 正常完成（`HttpServer.cpp:68 monitor client disconnected`），之后进程即终止。
  - 单线程顺序请求（脚本 `create→setstatus→delete→export` 逐个）**不崩**；并发才出问题 → 疑似竞态。
- **影响**：admin 控制台（flamingoAdmin）一旦连续操作就会把服务端打崩，严重影响测试与使用。**与 flamingoAdmin 客户端改动无关**，是纯服务端问题。
- **初步排查**：
  - `HttpServer` / `HttpSession` 的会话容器 `m_sessions` 有 `m_sessionMutex` 保护，单连接内 `onRead` 与 `onDisconnected` 在同一 I/O 线程串行，未见明显 UAF。
  - `EnterpriseManager` 用「每次请求新建一个 `CDatabaseMysql` 连接」+ 写操作加 `m_mutex`，连接不跨线程共享。
  - **重点嫌疑**：`mysqlapi/DatabaseMysql.cpp` —— 见 BUG-3；以及全程未调用 `mysql_library_init()` / `mysql_thread_init()`（见 BUG-4），多 I/O 线程并发建连接 / 查询时的 libmysql 线程安全存疑。
  - 尝试在 cdb 下复现以抓崩溃栈：cdb 拖慢 + 每请求都远程建连接，压测速率被限制，本轮未在 cdb 下复现（疑似竞态被 cdb 时序改变而不触发）。**崩溃调用栈仍待抓取。**
- **状态**：未修复。下一步：用 WER LocalDumps / postmortem dump 在全速运行下抓崩溃栈，再定位。

## BUG-2　chatserver manager 初始化失败时崩溃，而非优雅退出

- **发现日期**：2026-08-10（建档日期，实际发现日期不详）
- **现象**：某个 manager（如 `UserManager`）init 失败时，`main.cpp:143` 打了 `LOGF` 但**没有 `return`**，继续往下执行，随后解引用未初始化对象 → access violation 崩溃。
- **复现**：库 schema 不匹配导致 `UserManager::loadUsersFromDb` 查询失败时必现（本次因 `t_user` 缺 `f_salt`/`f_usertype` 两列触发过，列已补，问题本身仍在）。
- **影响**：配置 / 库异常时不能给出清晰错误并退出，而是崩溃，掩盖真实原因。
- **状态**：未修复。修法：各 manager init 失败后应 `return 1;` 终止启动。

## BUG-3　CDatabaseMysql::initialize 连接失败时句柄泄漏 + 对 NULL 调 mysql_close

- **发现日期**：2026-08-10（建档日期，实际发现日期不详）
- **位置**：`flamingoserver/mysqlapi/DatabaseMysql.cpp` `initialize()`。
- **问题**：
  ```cpp
  m_Mysql = mysql_init(m_Mysql);                 // m_Mysql = 有效句柄 H
  m_Mysql = mysql_real_connect(m_Mysql, ...);    // 连接失败时返回 NULL → m_Mysql=NULL，句柄 H 泄漏
  ...
  else { mysql_close(m_Mysql); }                 // 实为 mysql_close(NULL)
  ```
  连接失败时：① `mysql_init` 分配的 MYSQL 结构泄漏；② 对 `NULL` 调 `mysql_close`（部分旧版 libmysql 不做 NULL 检查 → 崩溃）。
- **影响**：远程库连接数被打满 / 网络抖动导致 `mysql_real_connect` 失败时，触发泄漏，并可能直接崩溃 —— 与 BUG-1「并发负载下崩溃」高度相关（并发压测会快速建大量连接）。
- **状态**：未修复。修法：用独立局部句柄，连接失败时 `mysql_close` 真句柄并把 `m_Mysql` 置空。

## BUG-4　全程未调用 mysql_library_init() / mysql_thread_init()

- **发现日期**：2026-08-10（建档日期，实际发现日期不详）
- **位置**：`flamingoserver/mysqlapi/DatabaseMysql.cpp` 及整个服务端。
- **问题**：MySQL C API 要求多线程程序在创建线程前单线程调用一次 `mysql_library_init()`，每个用到 libmysql 的线程调用 `mysql_thread_init()` / 退出前 `mysql_thread_end()`。当前代码只有 `mysql_init`，其余一概没有。
- **影响**：多个 I/O 线程并发建连接 / 查询时，libmysql 的隐式全局初始化存在竞态；线程局部数据未清理。可能是 BUG-1 的根因之一。
- **状态**：未修复。

## BUG-7　服务端多条写语句普遍缺少事务，中途失败会留下半套数据

- **发现日期**：2026-08-14（为「MySQL 驱动断线重试破坏事务原子性」做影响面复核时顺带查出）
- **位置**：`flamingoserver/chatserversrc/` 下多个 Manager。
- **问题**（据实证）：仓库里**只有 `ContactTagManager::setTags` 一处用了事务**，而实际需要原子性的多语句写入远不止一处。其余那些路径连"被断线破坏原子性"的资格都没有——它们压根就没有原子性。按严重程度排：

  | 位置 | 问题 |
  | --- | --- |
  | `EmployeeManager::setPermissions`（`EmployeeManager.cpp:909-985`） | **先清空该员工的全部权限覆盖项，再逐条重新插入**。插入中途失败直接 `return error_code_unknown`（`:977`），已删掉的权限不会恢复，员工权限就此静默变成半套。**直接影响权限判定**，是本表里危害最大的一条。 |
  | `EnterpriseManager::createEnterprise`（`EnterpriseManager.cpp:312-403`） | 跨**三条不同连接**依次建企业、建根部门、建管理员账号。后两步失败时只 `LOGE` 后继续往下走，会留下"企业建出来了、但 `f_root_dept_id` 为 0、且一个管理员账号都没有"的租户。 |
  | `EnterpriseManager::deleteEnterprise`（`EnterpriseManager.cpp:501-560`） | 连续 8 条删除语句，其中多条写成 `pConn->execute(sql);` **连返回值都不接**。中途失败会留下部分关联数据。 |
  | `DepartmentManager::moveMember` / `moveDepartment` | 多表更新，中途失败组织架构进入不一致状态。 |
  | `GroupManager::createGroup` / `transferOwner` | 建群/转让群主涉及多表写入。 |
  | `AnnouncementManager::replaceTargets` | 与 `setPermissions` 同型：先清空投放目标再重写。 |
  | `ServiceAccountManager::replaceBindings` | 同上。 |

- **影响**：这类故障不报错、不崩溃，只在数据层面留下半套状态，往往要等很久之后表现为"权限莫名其妙少了一半""某个企业没有管理员登不进去"才被发现，届时已经无从追溯是哪一次操作造成的。
- **初步排查**：基础设施已经就绪 —— 2026-08-14 给 `dbapi/IDatabase.h` 加了 `beginTransaction` / `commit` / `rollback` / `inTransaction` 四个方法，两个后端都已实现，并提供了 RAII 守卫 `dbapi/DbTransaction.h`（`CDbTransaction`，析构时若未提交则自动回滚）。`ContactTagManager::setTags` 已改用这套接口，可作范例。
- **状态**：未修复，**建议单独立项分批推进**。注意其中 `createEnterprise` 这类**跨连接写入**的路径光加事务解决不了 —— 事务是连接级的，得先把这些调用改成共用同一个 `IDatabase*` 才谈得上原子性。改造顺序建议从 `setPermissions` 开始（危害最大且是单连接、改动最小）。

## BUG-12　CSkinManager::SetCurSkin 校验的是切换前的皮肤编号，传入不存在的编号也会成功

- **发现日期**：2026-08-17（给 DuiGallery 补「资源与皮肤」演示页时发现）
- **位置**：`third-party/balloonui/balloonui/SkinManager.cpp` 的 `CSkinManager::SetCurSkin`（第 109 至 123 行）。
- **问题**（据实证）：函数体是 `lpSkinInfo = GetSkinInfo(m_nCurSkinID);` —— 查的是**切换前**的当前皮肤编号有没有对应的皮肤信息，而不是调用方传进来的 `nSkinID`。只要当前编号是有效的，函数就把 `m_nCurSkinID` 改成 `nSkinID` 并返回 TRUE，完全不校验目标皮肤是否存在。
- **影响**：传一个不存在的皮肤编号会得到「切换成功」的返回值，此后 `GetAbsolutePath` 因为查不到皮肤信息一律返回空串，于是所有按皮肤取图的调用都拿不到图片，而调用方从返回值上看不出任何异常。反过来，若当前编号本身无效（例如配置文件里第一条皮肤缺失），即便传入一个有效编号也会返回 FALSE、切不过去。
- **状态**：未修复。修法是把 `GetSkinInfo(m_nCurSkinID)` 改成 `GetSkinInfo(nSkinID)`。**改动落在 balloonui 库内，需事先获得用户批准。**

## BUG-27　ChatServer::getSessionsByUserId 只返回找到的第一个会话，多设备在线时邀请只发到其中一台

- **发现日期**：2026-09-30（设计"清理聊天记录"的多设备推送时发现）
- **位置**：`flamingoserver/chatserversrc/ChatServer.cpp` 的 `getSessionsByUserId`（第 203 至 217 行）。
- **问题**（据实证）：函数名与出参都是"会话列表"（`std::list<std::shared_ptr<ChatSession>>& sessions`），但循环里一旦找到 userid 相同的会话，`push_back` 之后立即 `return true`，不再往下找。服务端允许同一用户在不同客户端类型上同时登录（`ChatSession.cpp` 约第 723 至 737 行只踢掉同一客户端类型的旧会话），所以一个用户可能同时有多个会话，但本函数只会返回其中一个，且返回哪一个取决于 `m_sessions` 的遍历顺序。
- **影响**：共 6 处调用都受影响：
  - `ChatSession::onRemoteDesktopInvite`、`onCallInvite`、`onFolderInvite`、`onFolderRetry` 用它找对方的会话来投递邀请。对方多台设备同时在线时，邀请只送到其中一台，不一定是对方正在使用的那台（据推测，未实测）。
  - `ChatServer::kickUserSessions` 用它找要踢下线的会话，只能踢掉一台。
  - `HttpSession::onAdminUserLoginRecords` 用它列出用户的在线会话，管理后台看到的在线设备会少。
- **注意**：本人所有设备的推送另有 `ChatSession::sendToUser`（`ChatSession.cpp` 第 1517 行起），它遍历全部会话，不受本问题影响；新写的多设备推送应当用它，不要用本函数。
- **状态**：未修复。修法是去掉循环里的 `return true`，遍历完再按 `!sessions.empty()` 返回。修之前要逐个确认上述调用方是否本来就只想要一个会话（例如通话邀请是否有意只发给一台设备）。

## BUG-29　客户端主连接发送缓冲写满时，发送线程忙等

- **发现日期**：2026-10-01（去掉主连接收发循环里的 `Sleep(1)` 时读代码发现）
- **位置**：`flamingoclient/Source/net/IUSocket.cpp` 的 `CIUSocket::SendThreadProc` 与 `CIUSocket::Send`。
- **问题**（据实证，读代码所得）：`Send()` 遇到 `send` 报告 `WSAEWOULDBLOCK`（系统发送缓冲已满）时退出循环并返回 true，剩余数据留在 `m_strSendBuf` 里。`SendThreadProc` 的外层循环只在 `m_strSendBuf` 为空时才等待条件变量，缓冲非空就立即再次调用 `Send()`，于是发送缓冲持续写满期间，发送线程反复调用 `send` 而不等待，是一个忙等循环。每一轮还要重新获取 `m_mtSendBuf`，同时想往缓冲里追加数据的线程（`EnqueueFrame` 的调用方）会与它争抢这把锁。
- **与 2026-10-01 的改动的关系**：当天去掉的 `Sleep(1)` 只在"发出去一部分"之后执行，不在 `WSAEWOULDBLOCK` 这条路径上，所以这个忙等在改动前就存在，改动没有让它变多或变少。
- **影响**：据推测很少触发。主连接上走的是 JSON 信令帧，图片和文件走各自的端口，正常情况下系统发送缓冲写不满；只有网络很慢、对端长时间不读时才会出现，表现为发送线程占满一个 CPU 核。未实测。
- **状态**：未修复。修法候选：遇到 `WSAEWOULDBLOCK` 时先释放 `m_mtSendBuf`，再用 `select` 等待 socket 可写（带超时），然后重试。

## BUG-34　单聊里收到的文件行点「删除」只在本地移除，服务端没有删

- **发现日期**：2026-10-01（做多选转发、梳理气泡的 msgid 取值时发现）
- **位置**：`flamingoclient/Source/UI/BalloonChatWnd/ChatMockData.cpp` 的 `MakeIncomingFileMessage`（约第 400 至 415 行）；`ChatMessageList.cpp` 的 `RequestDeleteBubble`、`DeleteSelected`、`RemoveMessagesByMsgid`。
- **问题**（据实证，读代码所得）：单聊窗渲染收到的文件与历史文件时用 `MakeIncomingFileMessage`，它只把服务器 msgid 写进 `fileMsgid`，`msgid` 保持 0。删除的两条路径都按 `Msgid()` 判断：为 0 就当作发送中 / 发送失败的本地行，直接从列表移除，不发 `cmd_delete_message`。删除推送到达后，`RemoveMessagesByMsgid` 同样按 `Msgid()` 匹配，也找不到这些文件行。
- **影响**：据推测，在单聊里删除一条收到的文件消息，界面上消失了，但服务端没有删，重新打开聊天窗或拉取历史后又会出现；在别的设备上删除它时，本机已打开的聊天窗也不会移除这一行。Pin、引用回复同样按 `msgid` 判断，这类行上也不可用。
- **状态**：未修复。转发一侧已经用 `ForwardMsgidOf`（`msgid` 为 0 时取 `fileMsgid`）绕开了这个问题。根本的修法是让文件行的 `msgid` 与 `fileMsgid` 一致，改动前要核对所有按 `msgid` 为 0 判定"发送中"的地方。

## BUG-45　布局构建时按像素测出的宽度不随窗口 DPI 变化

- **发现日期**：2026-10-01（让控件字号随所在窗口的 DPI 变化时发现）
- **位置**：`flamingoclient/Source/UI/BalloonHistory/HistoryWindowLayout.cpp` 的 `MeasureDefaultFontWidth`（发送人、时间范围两个下拉框，以及「选日期」按钮与日期区间标签的定宽）；`flamingoclient/Source/UI/BalloonMain/BalloonProfileLayout.cpp` 的 `ComputeFieldLabelColumnWidth`（资料卡左列宽；该文件随本人资料对话框于 2026-10-07 删除，这一处已不存在，只剩消息记录窗一处）。
- **问题**（据实证，读代码所得）：这两处在构建布局时，按当时的全局 DPI 取默认字体量出文字宽度，再写成 `DuiLayout::Hint().Fixed(...)` 的固定像素宽。控件的字号现在会随所在窗口的 DPI 变化（`DuiControl::GetDpi`，宿主收到 `WM_DPICHANGED` 后经 `OnDpiChanged` 通知整棵控件树），但这些固定宽度只在建窗时算一次，DPI 变化后不会重算。
- **影响**：据推测，窗口从 100% 缩放的显示器拖到 150% / 200% 的显示器后，字号变大而下拉框、左列宽度不变，文字会被截断（资料卡左列设了 `DT_END_ELLIPSIS`，显示省略号）；反方向拖动则留白偏多。单显示器、不改缩放时不出现。
- **初步排查**：balloonui 的 Demo（XChat、CloudMelodyDesktop、DemoChatBubble 等）与 DuiGallery 页面里另有约 35 处仍用 `DuiResMgr::Inst().GetDefaultFont()` 按全局 DPI 取字体，多显示器缩放比例不同时，字号按最近一次建窗或 DPI 变化时的 DPI 走，属同类问题。库内控件与客户端界面已改为按控件 DPI 取字体，不在此列。
- **状态**：未修复。修法是让这两处布局在 DPI 变化后重新测量：例如在承载它们的控件的 `OnDpiChanged` 里按新 DPI 重算宽度并更新布局提示，或改由控件的 `GetDesiredSize` 按当前字体报告宽度、布局用 `Hint().Auto()`。Demo 与 DuiGallery 页面改为调用 `DuiControl::GetDefaultFont()` 即可，改动在 balloonui 仓库内，需事先获得用户批准。

## BUG-46　各窗口对显示缩放的处理不一致：群管理窗随 DPI 放大，其余窗口按 100% 的像素绘制

- **发现日期**：2026-10-01（用户反馈「群管理窗的文字和窗口尺寸比其他界面大」时查出）
- **位置**：
  - 放大的一方：`flamingoclient/Source/UI/BalloonGroupManage/`。窗口按 960×660 设计（`GroupManageLayout.cpp` 的 `kClientDesignWidth` / `kClientDesignHeight`），经 `DuiDpi::Scale` 换算；其余尺寸常量经 `GmScale`、字号经 `GmFontPixelHeight` 换算（`GroupManagePaint.cpp`）。
  - 没有放大的一方：单聊窗（`BalloonChatWindow.cpp` 的 `kChatWindowWidth` / `kChatWindowHeight` = 640×620）、群聊窗（`BalloonGroupChatWindow.cpp`，880×620）、消息记录窗（`BalloonHistoryWindow.cpp`，880×690）、主面板（`BalloonMainPanel.cpp`，320×680）都把常量直接交给 `ResizeClient`；这些窗口里的自绘控件用固定像素字号建字体，如群成员行昵称 `kNamePx = 13`（`GroupMemberItem.cpp`），`BalloonChatWnd/` 下 `ChatMessageList`、`ChatBubble`、`ChatHeaderInfo`、`ChatSendButton` 等十余处 `CreateFontW(-像素, ...)` 同样不随 DPI 变化。
- **问题**（据实证）：进程在 `App/Startup.cpp` 里调用 `DuiDpi::OptInPerMonitorV2()` 声明自行处理 DPI，系统不再替窗口整体拉伸，每个窗口须自己按 DPI 换算尺寸与字号。群管理窗做了换算，其余窗口没有。120 DPI（125% 缩放）下实测：群管理窗客户区 1200×825，即设计尺寸的 1.25 倍（日志 `Group manage window: dpi=120, client=1200x825`）；单聊窗、群聊窗、消息记录窗、主面板的窗口尺寸为 658×667、898×667、898×737、338×727，没有乘 1.25。（2026-10-02 修复 `ResizeClient` 让窗口比设计尺寸大一圈的问题时，这几个窗口按用户决定保持原有大小，常量已直接改成上面这组数，即原设计尺寸加上 120 DPI 下的 18×47。）
- **影响**：缩放比例大于 100% 的屏幕上，群管理窗按正确比例显示，其余窗口只有应有大小的约八成，文字偏小，两类窗口并排时差别明显；缩放比例越高差别越大。另外，未放大的窗口里 balloonui 标准控件（按钮、输入框、下拉框）用的默认字体是按 DPI 算的（`DuiResMgr.cpp`：`lfHeight = -MulDiv(9, dpi, 72)`，120 DPI 下为 15 像素），而同一窗口里的自绘文字是 13 像素，据推测同一窗口内会出现两种字号（尚未逐个截图核对）。100% 缩放的屏幕上不出现。
- **初步排查**：统一的方向应是让其余窗口也按 DPI 换算，而不是把群管理窗改回固定像素。改动面大：单聊、群聊、主面板、消息记录等窗口里写死的窗口尺寸、行高、边距、自绘字号都要经 DPI 换算，位图图标放大后可能发虚，需要按窗口逐个推进，每个窗口配布局单测（可参照群管理窗的 `GmSetDpiOverride`，让排版结果不随跑测试的机器变化）。与 BUG-45（按像素测出的宽度在 DPI 变化后不重算）属于同一类问题，修复时宜一并考虑。另外，2026-10-02 为保持可缩放窗口原有大小而加进常量的 18×47 是 120 DPI 下的量，按 DPI 换算时应先把它去掉、回到原设计尺寸再乘缩放比例。
- **状态**：未修复。用户 2026-10-01 决定先记录、暂不处理。

## BUG-50　单聊窗与群聊窗头部的「…」按钮点了没有任何反应

- **发现日期**：2026-10-02（给两个聊天窗头部加「清空聊天记录」按钮时发现）
- **位置**：`flamingoclient/Source/UI/BalloonChatWnd/BalloonChatWindow.cpp`（按钮 `balloonchatwnd::kIdHdrMore`，取值 413）与 `BalloonGroupChatWindow.cpp`（按钮 `balloonchatwnd::kGIdHdrMore`，取值 514）。
- **现象**（据实证）：两个按钮都只在初始化时注册了悬停提示「更多」（`BalloonChatWindow.cpp` 的 `RegisterTip(content, balloonchatwnd::kIdHdrMore, ...)`、`BalloonGroupChatWindow.cpp` 的 `RegisterTip(content, balloonchatwnd::kGIdHdrMore, ...)`），两个窗口的 `OnDuiNotify` 里都没有处理它们的点击分支，点击后通知被忽略，界面上什么也不发生。
- **影响**：按钮排在头部最右边，用户点了以为功能失灵。其余头部按钮不受影响。
- **初步排查**：需要先定这个按钮要放什么——例如单聊打开联系人资料、群聊打开群资料，或者弹出一个汇集低频操作的菜单。定下来之前也可以先把按钮从两个窗口的布局里拿掉。「清空聊天记录」按用户要求做成了它左边单独的按钮，没有放进这里。
- **状态**：未修复。

## BUG-55　balloonui 的 XML 属性 margin 只认第一个数，网络设置窗分隔线写的四值外边距被当成四边各 19 像素

- **发现日期**：2026-10-02（修 BUG-24 时实机截图发现网络设置窗的表单区比按 XML 加总的位置低了 22 像素）
- **位置**：`third-party/balloonui/balloonui/DuiXmlBuilder.cpp` 的 `MakeHint` 用 `ParseInt(Get(n, "margin"), 0)` 读外边距，只取第一个整数后统一设到四边；`flamingoclient/Source/UI/BalloonNetSettings/NetSettingsLayout.cpp` 内嵌 XML 里的 `<separator fixedHeight="1" margin="19,7,19,9"/>` 按「左、上、右、下」写了四个值。全工程只有这一处用了多值 margin。
- **现象**（据实证）：分隔线上下各留 19 像素而不是 7 与 9，这一段共占 39 像素（1 + 19 + 19），比 XML 字面上的 17 像素多 22 像素。FlamingoUnit 的 N10 打印的矩形：第三张服务器卡片下边 210，「服务器地址」输入框上边 261。`padding` 支持四个值，`margin` 不支持，写法相同而含义不同，容易误用。
- **影响**：网络设置窗表单区上方多出 22 像素空白，内容所需高度因此从约 383 变成 405。BUG-24 修复时已按实测把该窗口的内容高度定为 415（含 10 像素余量），界面没有重叠；但若有人按 XML 字面重新核算高度，会算少。
- **初步排查**：两种改法，二选一。一是在客户端把这一处改成单值 margin，并按实际想要的上下间距调整（例如在分隔线前后各放一个固定高度的占位）；二是让 balloonui 的 margin 像 padding 一样支持四个值（属于 balloonui 改动，须先经用户同意）。改后要重跑 N10 并实机核对网络设置窗。
- **状态**：未修复。

## BUG-56　通话结束时与对方的单聊窗没有打开，通话结果提示会丢失

- **发现日期**：2026-10-02（屏幕共享方案 1c-3 双账号联调一对一通话时发现，`tmp/calltest`）
- **位置**：`flamingoclient/Source/UI/BalloonMain/BalloonMainPanel.cpp` 的 `CBalloonMainPanel::AppendCallTipToChat`，与 `flamingoclient/Source/UI/BalloonChatWnd/BalloonChatWindow.cpp` 的 `CBalloonChatWindow::OnHistoryMessages`。
- **现象**（据实证）：A 呼叫 B、接通后挂断，B 当时没有打开与 A 的单聊窗。B 收到「通话结束」推送后，客户端为显示提示新建了单聊窗，但窗口里看不到「通话结束 mm:ss」（截图 `tmp/calltest/b_chat_tip.png`）。日志顺序：通话控制器调用 `AppendCallTip`，主面板 `EnsureChatWindow` 新建窗口并 `AppendSystemTip`，随后约 270 毫秒第一页历史到达并渲染。聊天窗事先已打开时，提示正常显示（拒绝、取消两个用例的截图 `a_reject_tip.png`、`b_cancel_tip.png`）。
- **原因**（据实证，读代码所得）：`OnHistoryMessages` 渲染第一页之前会调用 `list->ClearAllMessages()`（`BalloonChatWindow.cpp` 约 5157 行，用来去掉历史在途时乐观追加的推送行），把刚追加的通话提示一并清掉了。`AppendCallTipToChat` 的注释写的是「窗口没开就建一个，不让提示丢掉」，与实际行为不符。
- **影响**：通话被拒、无人接听、对方取消、正常结束时，只要本端没开着与对方的聊天窗，通话结果就不会留下任何记录。该提示只在本地显示、不入库，所以重新打开窗口也找不回来。与 2026-10-02 通话迁入 FlamingoMedia.dll 无关，迁移前的通话协调器走的是同一个函数。
- **再次遇到**（2026-10-03 修 BUG-57 后联调）：通话中对方退出程序，本端 5 秒后自行结束通话并调用 `AppendCallTip` 追加「通话已中断 00:05」，同样因为与对方的单聊窗是新建的，截图里看不到这条提示（`tmp/bug57/e_chat_tip.png`）。据推测原因与上面相同，未单独核实。
- **初步排查**：可以让单聊窗在第一页历史渲染完成后再补上在途期间追加的系统提示（与乐观追加的推送行分开对待），或者在窗口新建、历史未到时先把提示暂存，渲染完成后追加。
- **状态**：未修复。

## BUG-58　撤回的消息原文仍会随同步、Pin 列表与搜索下发给会话里的每个人

- **发现日期**：2026-10-02（群接龙 1f 核对卡片撤回后的数据去向时发现）
- **位置**：`flamingoserver/chatserversrc/MessageManager.cpp`（撤回只执行 `UPDATE t_chatmsg SET f_recalled = 1`，读消息时照常读出 `f_content`）；`flamingoserver/chatserversrc/MessageProtocol.cpp` 的 `messageBodyValue`（对 `recalled` 为真的消息仍原样输出 `content`）。
- **问题**（据实证，读代码所得）：消息被撤回后，服务端在 `cmd_sync_messages`、`cmd_get_pinned_messages`、`cmd_search_messages` 的应答里仍带着它的完整 `content`，只是另带 `recalled: true`，由客户端负责不显示。所有消息类型都是如此。
- **影响**：据推测，撤回只在界面上生效；任何会话成员用抓包或改过的客户端都能读到被撤回消息的原文。库里保留原文是为了审计（群解散时群消息同样保留），这一点本身不需要改，问题只在下发。群接龙 1f 已让已撤回的卡片在下发时不再合并接龙名单与投票结果（`CardProtocol::shouldAugmentCard`），但卡片自身的标题与说明仍随 `content` 下发。
- **初步排查**：在 `messageBodyValue` 里对已撤回的消息输出空的 `content`。动手前要核对客户端是否依赖撤回消息的原文（例如「撤回后重新编辑」、本地库按原文显示撤回提示），并确认会话列表摘要的取值路径不受影响。改动面覆盖所有消息类型，须先与用户确认。
- **状态**：未修复。

## BUG-65　balloonui 的 DuiTab 在排版之前选中靠后的页签，页签文字整体画不出来

- **发现日期**：2026-10-03（屏幕共享第 2 期双账号联调，共享窗口关闭后自动弹出的重新选择窗）
- **位置**：`third-party/balloonui/balloonui/Controls/List/DuiTab.cpp` 的 `DuiTab::SetCurSel`（210 行起）与 `DuiTab::EnsureVisible`（525 行起）。
- **问题**（据实证）：`SetCurSel` 选中的页签不是当前页时会调 `EnsureVisible`；控件还没排版时宽度为 0，`NeedsScroll` 判为「放不下」，于是把滚动偏移设成正值，把页签整体滚出可见区。排版之后宽度够了，但 `m_scrollOffset` 只在下一次 `EnsureVisible` 时才会复位，页签文字一直画不出来，只剩一条底线。屏幕共享选择窗第二次打开时停在「窗口」页（第二个页签），截图里两个页签都没有文字。
- **影响**：屏幕共享选择窗已绕开：改为 `SetClientContent` 排版之后再 `SetCurSel`（`FlamingoMedia/Share/SharePickerDialog.cpp` 的 `BuildUi`）。其它在建控件树时就选中非首个页签的地方，据推测也会出现同样的现象，尚未逐一排查。
- **初步排查**：根本的修法是在 `EnsureVisible` 里矩形为空时不改滚动偏移，或在 `Layout` 之后按当前选中项重新计算一次。这属于 balloonui 改动，须先经用户同意。
- **状态**：未修复（屏幕共享选择窗已绕开）。

## BUG-66　balloonui 的 DuiFrameWindow 销毁时不复位全屏标志，同一个窗口对象再次建窗时「切换全屏」变成退出全屏

- **发现日期**：2026-10-03（屏幕共享第 2 期双账号联调：观看者离开后从卡片重新加入，观看窗以窗口模式打开，而不是默认的全屏）
- **位置**：`third-party/balloonui/balloonui/Controls/Window/DuiFrameWindow.cpp` 的 `ToggleFullscreen` / `ExitFullscreen`（933-988 行），`m_fullscreen` 只在这两处改动。
- **问题**（据实证）：全屏状态下直接 `DestroyWindow`，`m_fullscreen` 仍为 true；同一个 C++ 对象之后再 `Create` 一个新窗口并调用 `ToggleFullscreen`，走的是退出全屏的分支，新窗口停在普通大小，而且 `IsFullscreen()` 的结果与实际不符。
- **影响**：屏幕共享观看窗已绕开：销毁前若处于全屏先退出全屏，打开时只在不处于全屏时才切换（`FlamingoMedia/Share/ShareViewerWindow.cpp`）。远程桌面观看窗（`FlamingoMedia/RemoteDesktop/RdViewerWindow.cpp`）也是 `DuiFrameWindow`、可以全屏，据推测在「全屏时会话结束、再次发起远程桌面」时会遇到同样的问题，尚未实测。
- **初步排查**：根本的修法是在 `DuiFrameWindow` 处理 `WM_DESTROY`（或 `OnFinalMessage`）时把 `m_fullscreen` 复位。这属于 balloonui 改动，须先经用户同意。
- **状态**：未修复（屏幕共享观看窗已绕开）。

## BUG-70　balloonui 的 DuiAvatar 设为圆角矩形且圆角半径为 0 时，位图整个不显示

- **发现日期**：2026-10-03（文件共享的本会话共享浮层给每一行加系统类型图标时发现）
- **位置**：`third-party/balloonui/balloonui/Controls/Basic/DuiAvatar.cpp` 的 `OnPaint`（约 300～345 行）：按形状拼一条裁剪路径，圆角矩形由四段 `AddArc` 拼成，圆弧的宽高是圆角半径的两倍。
- **问题**：`SetShape(ShapeRoundRect)` 加 `SetCornerRadius(0)` 时，`SetBitmap` 设的位图一点也画不出来（据实证：`FlamingoFileShare/Tests/ShareManageTests.cpp` 的 SCP3 截图 `snapshots/fileshare_conversation_popup.png`，半径为 0 时图标处是空白，半径改为 1 后图标正常显示，其余代码不变）。据推测是四段圆弧的宽高为 0，GDI+ 拒绝这样的圆弧，裁剪路径为空，`DrawImage` 被整个裁掉；没有位图时的纯色底是否同样画不出来，尚未验证。
- **影响**：想用 DuiAvatar 显示方形位图的地方。文件共享浮层已改用半径 1 绕开（`FlamingoFileShare/UI/ShareConversationPopup.cpp` 的 `kIconCornerRadius`），系统图标四角本来透明，看不出差别。
- **初步排查**：在 `OnPaint` 里半径不大于 0 时改用 `AddRectangle` 拼裁剪路径即可。这属于 balloonui 改动，须先经用户同意。
- **状态**：未修复（文件共享已绕开）。

## BUG-72　balloonui 的 DuiTab 最后一个页签未选中时，页签条底线越出控件右边缘

- **发现日期**：2026-10-03（文件共享的「文件共享」窗口把页签条宽度收到正好放下两个页签时发现）
- **位置**：`third-party/balloonui/balloonui/Controls/List/DuiTab.cpp` 的 `OnPaint`，不需要左右滚动时画页签条底线的分支（约 873～881 行）：给每个未选中的页签画底线时一律画到「页签宽 + 页签间距」，最后一个页签也不例外。
- **问题**：最后一个页签未选中时，底线比它的右边框多出一个页签间距（默认 2 像素）；页签条宽度正好等于各页签总宽时，这一段画到了控件矩形之外（据实证：`FlamingoFileShare/Tests/ShareManageTests.cpp` 的 SCW3 截图 `snapshots/fileshare_center.png`，「共享给我的」右边框在 x=169，页签条右边缘在 x=170，底线一直画到 x=171）。
- **影响**：页签条右边紧挨着别的控件时，会在别的控件上画出 2 像素的浅灰线；肉眼几乎看不出。「文件共享」窗口页签条右边是空白，只是底线比页签右边框长出 2 像素。
- **初步排查**：最后一个页签的底线画到页签右边缘为止（不加页签间距）即可。这属于 balloonui 改动，须先经用户同意。
- **状态**：未修复。

## BUG-76　客户端 Debug 版在 STL 容器操作中途崩溃时，崩溃处理函数写日志会死锁，进程挂住、不弹框也不写转储

- **发现日期**：2026-10-03（P2P 方案第 1 期 1c 在真实 exe 上核对启动加载时发现，见 `docs/P2P通信方案.md` 第 14.6 节）
- **位置**：`flamingoclient/Source/App/Crash.cpp` 的 `AutoUnhandleExceptionFilter::MyUnhandledExceptionFilter`：先 `LOG_ERROR`，再 `CIULog::Flush(kIULogFlushTimeoutMs)`，之后才弹框、写转储；`CAsyncLog::flush` 拿日志互斥量时用的是不带超时的 `std::unique_lock`。
- **问题**：据实证（cdb 非侵入式附加 `Bin\Flamingo_p2p.exe` 取得的调用栈）：主线程在 `std::map` 析构的 `_Container_base12::_Orphan_all_locked_v3` 里崩溃，崩溃时持有 STL 的迭代器调试全局锁 `_Lockit`；进入崩溃处理函数后卡在 `CIULog::Flush → CAsyncLog::flush` 拿日志互斥量。与此同时，日志写线程持有日志互斥量，在 `CAsyncLog::writeThreadProc` 里给 `std::string` 赋值，卡在同一个 `_Lockit` 上。两边互相等待，`Flush` 的超时只管等队列写完、管不住拿锁这一步，进程就一直挂着：不弹「程序出错」框、不写转储、也不退出。
- **影响**：Debug 版里，只要崩溃恰好发生在 STL 容器操作中途、日志写线程又正好在处理字符串，就拿不到转储，现场只能靠调试器附加。据推测 Release 版不受影响（Release 不开迭代器调试，没有 `_Lockit`），尚未验证。本次触发崩溃的原因与此无关：隔离编译期间别的会话改了 `CreateGroupDlg.h`，`AppShell.obj` 与 `CreateGroupDlg.obj` 用了不同的类布局，析构时出错。
- **初步排查**：可以先写转储、后刷日志；或让 `CAsyncLog::flush` 用带超时的锁（`std::timed_mutex::try_lock_for`），拿不到就放弃刷新。改之前先写一条用例：在日志写线程持锁时，让另一线程在持有 `_Lockit` 的状态下触发崩溃处理路径，确认会挂住，再验证修复。
- **状态**：未修复。

## BUG-79　服务端 `sockets::toIp` 的长度判断写反了，缓冲足够大时什么都不写

- **发现日期**：2026-10-04（P2P 第 2 期 2e-3 联调，imgserver 地址探测的限频日志里来源 IP 为空）
- **位置**：`flamingoserver/net/Sockets.cpp` 的 `sockets::toIp`（第 333 行附近）
- **现象**：函数开头是 `if (size >= sizeof(struct sockaddr_in)) return;`，缓冲不小于 16 字节时直接返回、不写缓冲。点分十进制的 IPv4 地址最长要 16 字节（含结尾的 0），所以凡是够用的缓冲都拿不到结果。紧挨着的 `toIpPort` 里同样的判断已经被注释掉了，那个函数工作正常。
- **影响**：目前生产代码里没有调用方受影响：`InetAddress::toIp()` 没有任何地方调用（它用未初始化的 `char buf[32]` 调 `sockets::toIp`，若被调用会返回栈上的随机内容）；唯一用过 `sockets::toIp` 的 P2P 地址探测已改用 `toIpPort`。以后有人调用 `toIp` 会立刻踩到。
- **初步排查**：据推测原意是 muduo 里的 `assert(size >= INET_ADDRSTRLEN)`，移植时把条件写反了。修法是去掉这个判断（或改成 `size < INET_ADDRSTRLEN` 时返回），再补一条单测核对输出的点分十进制。`net/` 是三个服务共用的代码，修改前须确认。
- **状态**：未修复。

## BUG-81　chatserver 启动加载用户时把每个用户的口令哈希写进日志

- **发现日期**：2026-10-04（联系人资料字段扩充 1a，改 `UserManager` 加载代码时看到）
- **位置**：`flamingoserver/chatserversrc/UserManager.cpp` 的 `loadUsersFromDb`（第 103 行附近）
- **现象**：逐个加载用户时有一行 `LOGI("userid: %d, username: %s, password: %s, nickname: %s, signature: %s", ...)`，把 `t_user.f_password`（v2 存的是 `MD5(SHA256(明文)+盐)`）连同账号一起写进日志，每次启动全体用户各一行。
- **影响**：拿到服务端日志的人能拿到全部账号的口令哈希与账号名，可以离线尝试破解；用户多时启动日志也被这几千行淹没。
- **初步排查**：删掉这一行，或只保留 userid 与账号名（不带口令、签名等内容），改用调试级别输出。改动只涉及一行日志，但 `UserManager` 是多个会话共用的文件，改之前先确认。
- **状态**：未修复。

## BUG-82　文件共享模块用例 FSD10 在机器负载高时偶发失败：访问者连接中转的那行日志没有出现

- **发现日期**：2026-10-04（P2P 第 6 期连跑文件共享模块用例时）
- **位置**：`flamingoclient/FlamingoFileShare/Tests/FileShareModuleTests.cpp` 的 FSD10（第 931 行）；被测代码是 `FlamingoFileShare/Engine/ShareService.cpp` 的 `StartVisitorChannel`
- **现象**：`CHECK(host.CountLinesContaining("share visitor channel connecting, accessid: share-9-100") == 1)` 得 0，同一轮其余用例与断言都通过，进程没有崩溃。
- **复现**：两个 FlamingoUnit 进程并行连跑（一个跑 `[filesharedll]`，另一个跑 P2P 的各个标签，制造负载），40 次里出现 1 次；单独连跑时没有出现过。2026-10-04 下午另一个会话（a3）在全量运行 `~[live]` 时也遇到一次，单独跑 FSD10、整组跑 `[filesharedll]` 两遍都通过。2026-10-05 用 `tmp/bug82/stress.ps1` 让四份测试程序副本并行连跑 FSD10（各自独立目录，互相制造负载），共 520 次没有复现。
- **影响**：只是用例偶发失败，重跑即可通过；据推测不是产品问题。
- **初步排查**：这行日志在 UI 线程上调用 `StartConnect()` 之后同步写出，DLL 的日志经 `Engine/HostLogBridge.cpp` → `ShareLogWriteLine` 同步交给宿主，所以缺这一行说明那一轮 `Deliver` 抽取消息时，推给访问者的 2222 还没有被处理（或者 2220 应答没有配上访问者）。这条路径不经过 P2P 第 6 期改动的代码，FSD10 的宿主也不提供 P2P 服务，据推测与第 6 期无关。同期查清并已修正的另一个偶发问题（FSD7、FSD11 崩溃，原因是测试进程里载入又卸载了第三方输入法模块）与它不是一回事，见 `docs/P2P通信方案.md` 第 14.22 节。
  - 2026-10-05 查出用例里的测试宿主 `StubHost` 有数据竞争（据实证）：DLL 的中转连接线程也经 `Log` 写日志（`FlamingoFileShare/Engine/ShareVisitorChannel.cpp:79`，在 `ConnectProc` 里，由 `StartVisitorChannel` 起的连接线程执行），而 `StubHost` 追加日志行与测试线程遍历 `m_lines` 都不加锁，`vector` 扩容时测试线程可能读到正在搬移的元素，计数就会少一行。已给 `StubHost` 加锁，测试代码改为经加锁的读取函数访问日志与已发命令；改后 `[filesharedll]` 连跑 3 次、FSD10 四份并行共 100 次全部通过。但原故障本来就极难复现，这是不是 FSD10 失败的原因仍只是推测。
  - FSD10 失败时会用 `UNSCOPED_INFO` 打印宿主收到的全部日志行，以及检查时「连接中转」那一行出现了几次，再遇到时据此判断是数据竞争造成的漏数还是消息确实没有处理。
- **状态**：测试宿主的数据竞争已修正，原故障未再出现，继续观察；再出现一次就按打印出的日志查，长期不出现再删除本节。

## BUG-83　balloonui `DuiFrameWindow` 销毁窗口后全屏标志不复位，同一对象再建窗口时进不了全屏

- **发现日期**：2026-10-04（备忘编辑对话框「预览效果」实机核对时）
- **位置**：`third-party/balloonui/balloonui/Controls/Window/DuiFrameWindow.cpp` 的 `ToggleFullscreen` / `ExitFullscreen`（第 933～988 行），标志是成员 `m_fullscreen`
- **现象**：同一个 `DuiFrameWindow` 派生对象先全屏显示、再 `DestroyWindow`、之后重新 `Create` 时，`IsFullscreen()` 仍返回 true；调用方若按「`if (!IsFullscreen()) ToggleFullscreen();`」进入全屏，这一步会被跳过，新窗口带着标题栏显示。备忘的全屏提醒窗口第二次显示时内容整体下移约 35 像素、顶部多出一条浅色标题栏「备忘提醒」，按钮的点击位置随之偏移。
- **复现**：登录后在备忘编辑对话框里连续点两次「预览效果」（每次用 Esc 关闭），第二次起顶部出现标题栏；真实提醒同理，同一次登录里第二条全屏提醒带标题栏。
- **影响**：凡是「对象常驻、窗口用完即销毁、再次显示时全屏」的窗口都会中招。目前只有 `CMemoFullscreenWindow` 这样用，已在客户端侧绕开：`Present` 建窗后若标志仍为 true，先 `ExitFullscreen()` 一次再进入全屏（整窗透明期间做，看不到）。
- **初步排查**：`m_fullscreen`、`m_savedPlacement` 只在 `ToggleFullscreen` / `ExitFullscreen` 里改，`WM_DESTROY` / `OnFinalMessage` 不复位。根治办法是在 balloonui 里窗口销毁时把全屏标志清零，改 balloonui 须先经用户同意。
- **状态**：未修复（库内）；客户端侧已绕开。

## BUG-84　服务端新建 / 修改部门时不校验负责人是否属于本企业

- **发现日期**：2026-10-04（flamingoAdmin 第一批修复把新增部门的「负责人」改成员工选择器时发现）
- **位置**：`flamingoserver/chatserversrc/HttpSession.cpp` 的 `onAdminDeptCreate` / `onAdminDeptUpdate`（`req.leaderuserid = JInt(root, "leaderuserid", 0)` 两处），`chatserversrc/DepartmentManager.cpp` 写 `f_leader_user_id` 的 INSERT / UPDATE
- **问题**（据实证，按代码路径）：`leaderuserid` 原样落库，既不检查这个用户是否存在，也不检查他是否属于该部门所在的企业。控制台的负责人下拉框只列本企业员工，正常操作不会出错；但企业管理员手工构造请求，可以把别的企业的员工设成本企业部门的负责人，部门详情里会显示出那个人的姓名。另外 `DepartmentManager` 读负责人时用的是 `getInt32()`，与其它地方的 64 位用户 ID 口径不一致（据推测当前用户 ID 都在 32 位范围内，暂无实际影响）。
- **影响**：数据一致性与租户隔离；不影响正常使用。
- **初步排查**：在两个接口里，`leaderuserid` 非 0 时查一次 `t_user.f_enterprise_id`，与部门所属企业不一致就拒绝。
- **状态**：未修复。

## BUG-85　flamingoAdmin 打开「新增部门」对话框后静置约半分钟崩溃（ntdll 访问违例、堆损坏），未能复现

- **发现日期**：2026-10-04（第一批修复的界面联调，`tmp/admin_b1/env`）
- **现象**：14:08:01 打开「新增部门」对话框（含这次新改的可输入负责人下拉框），没有任何操作，14:08:37 进程退出。Windows 应用程序日志：`flamingoAdmin.exe` 在 `ntdll.dll` 偏移 `0x1aa2d` 处 `0xc0000005`，同时有一条 `FaultTolerantHeap`（`ffffbaad`）事件，说明堆已被写坏。控制台自带的崩溃处理没有写出转储（堆损坏这类崩溃据推测绕过了 `SetUnhandledExceptionFilter`），WER 也没有保留转储。
- **复现**：未能复现。随后在 cdb 下按同样步骤打开同一对话框并静置 85 秒、按句柄截图、投递 `WM_ACTIVATE` / `WM_KILLFOCUS` / `WM_SETFOCUS` 模拟失焦，都没有再崩；之后在调试器下完成了全部界面核对（含该对话框里的负责人筛选、回车提交），也没有出现访问违例。
- **初步排查**（据推测，均未证实）：崩溃时段内另有两个会话开始在共用桌面上做界面联调，其中一个的聊天窗会调用 `SetForegroundWindow` 抢前台；控制台进程里注入了华宇输入法（`HuayuIME.ToolBarWindow` 出现在本进程的窗口列表里），真实的前台切换会牵动输入法与文本服务框架对无窗口 RichEdit 的回调。FlamingoUnit 里出现过同类问题（第三方输入法模块载入又卸载后残留消息导致的偶发崩溃，见 `docs/P2P通信方案.md` 第 14.22 节）。也不能排除与这次新加的可输入下拉框（`DuiComboBox` 可输入模式 + 增量搜索）有关。
- **下一步**：再遇到时用 `tmp/admin_b1/env_start_ui_cdb.ps1`（cdb 下启动，访问违例或堆损坏时自动写全量转储 `tmp/admin_b1/crash.dmp`）复现取栈；必要时开页堆（gflags `+hpa`，须用户同意改注册表）定位写坏堆的地方。
- **状态**：未修复（原因未明）。

## BUG-86　flamingoAdmin 的独立对话框在 125% 缩放下底部按钮被裁掉，「修改员工资料」连「保存」都看不见

- **发现日期**：2026-10-04（第一批修复的界面联调，系统缩放 125%）
- **现象**：「新增部门」「新增员工」对话框底部的「取消」「创建」按钮下半截在窗口外（截图 `snapshots/admin_b1_13_add_dept.png`、`admin_b1_16_add_user.png`）；「修改员工资料」对话框的按钮栏整个在窗口外，只能靠在输入框里按回车保存（`snapshots/admin_b1_22_edit_employee.png`）。
- **位置**：`flamingoAdmin/Screens/NodeModal.cpp`、`EmployeeEditDialog.cpp` 的 `CreateAndShow`：窗口尺寸按 96 DPI 的固定像素算（`ResizeClient(kDlgClientWidth, 内容高 + kTitleBarHeight)`），而 balloonui 的标题栏与控件字体随 DPI 放大。
- **初步排查**（据推测，尚未在 100% 缩放下对照）：控制台整体没有按 DPI 换算尺寸（全工程只在 `main.cpp` 声明了 Per-Monitor V2 感知，`UiFont` 固定按 96 DPI 出字），对话框内容按 96 DPI 排、标题栏按 125% 画，多出来的高度把按钮栏挤出窗口；「修改员工资料」内容更多，所以挤得最多。这次第一批修复没有改这两个对话框的尺寸常量（编辑员工把性别放进第一行，就是为了不加高）。与 BUG-68 同属「控制台不随 DPI 缩放」这一类。
- **影响**：高 DPI 屏幕上点不到「保存」「创建」，只能用回车提交。
- **状态**：未修复。

## BUG-92　同一账号多台设备在线时，断开其中一台，同企业同事看到此人「离线」

- **发现日期**：2026-10-04（FlamingoLink 方案调研时读代码发现）
- **位置**：`flamingoserver/chatserversrc/ChatServer.cpp` 的 `ChatServer::onDisconnected` 末尾，按企业广播 `cmd_presence_changed_notify`（1109）那一段。
- **现象**（据实证，读代码）：一条有效会话断开时，无条件向同企业的在线同事广播 `presence=0`，不检查同一 userid 是否还有别的会话在线。例如张三 PC 与手机同时在线，手机断网后，同事的联系人树上张三变成离线，而他的 PC 其实还在线；直到他在 PC 上改一次状态或重新登录才恢复。
- **复现**（据推测，未实测）：同一账号先后登录 PC 客户端与 Android 客户端，另一同事的 PC 客户端看着他；断开 Android 的网络，等服务端发现连接断开，看同事那边的状态点。
- **影响**：员工客户端上的在线状态与实际不符。管理控制台的「在线用户」页（FlamingoLink）由服务端从会话表现算每个人的全部设备，不受此影响。
- **初步排查**：修法是断开时先看同一 userid 还有没有其它已登录的会话，有就按剩余设备里优先级最高的状态广播（或不广播），没有才推 0。1109 的包体带 clienttype，客户端如何处理多设备也要一并核对。
- **状态**：未修复。

## BUG-93　被顶号或被管理员踢下线的会话断开时，不推任何在线状态

- **发现日期**：2026-10-04（FlamingoLink 方案调研时读代码发现）
- **位置**：`flamingoserver/chatserversrc/ChatServer.cpp` 的 `ChatServer::onDisconnected`；会话被 `makeSessionInvalid()` 置为失效（userid 清零）的几处：`ChatSession::onLogin` 顶号、`ChatSession::onKickDevice`、`ChatServer::kickUserSessions` / `kickEnterpriseSessions`。
- **现象**（据实证，读代码）：会话被置为失效后 userid 已是 0，断开时 `isSessionValid()` 为 false，既不推设备下线（1009），也不推在线状态变化（1109）。管理员在控制台「强制注销设备」或停用员工后，同事的联系人树上此人仍显示在线，直到同事重新拉取通讯录。
- **复现**（据推测，未实测）：员工甲只在 PC 上在线，同事乙看着他；管理员在控制台员工详情的「设备登录」里强制注销甲的 PC；看乙那边甲的状态点。
- **影响**：员工客户端上的在线状态与实际不符。顶号（同类型设备重复登录）时新会话随即推了上线，所以只有「踢掉后没有新会话」的情形才看得出来。管理控制台的「在线用户」页（FlamingoLink）在 `makeSessionInvalid` 里记下 userid 再清零，不受此影响。
- **初步排查**：修法是在置为失效时记下原 userid 与 clienttype，断开（或置失效当时）按 BUG-92 的规则为该用户重算并广播。
- **状态**：未修复。

## BUG-97　P2P 宿主用例 P2PH1 偶发失败：DLL 初始化日志没有以 "[p2p] " 开头

- **发现日期**：2026-10-05（P2P 拥塞控制排队延迟优化后，组合运行全部 P2P 标签时）
- **位置**：`flamingoclient/Source/Tests/P2PHostTests.cpp` 的 P2PH1「DLL 存在」一节，当时是第 162 行 `CHECK(line.compare(0, strlen(kP2PPrefix), kP2PPrefix) == 0)`
- **现象**：`FlamingoUnit_p2p.exe "[p2pdll],[p2pcore],[p2pio],[p2pchan],[p2plink],[p2pcc],[p2phost],[folderp2p],[folderrelay]"` 一次运行里 85 个用例只有这一条断言失败；紧挨着的上一条 `REQUIRE(CountLinesContaining("FlamingoP2P initialized") == 1)` 是通过的。同样的组合连跑三次只出现这一次，单独跑 `[p2phost]` 与再次组合运行都通过。当时的输出只筛了 `FAILED` 行，没有留下展开的实际取值。
- **影响**：只是用例偶发失败，重跑即可通过；据推测不是产品问题。
- **初步排查**：用例里的记录型宿主 `RecordingHost` 有数据竞争（据实证）：DLL 初始化时就启动了引擎的 IO 线程，该线程一启动就写日志（`FlamingoP2P/Io/P2PEngine.cpp:234`），与 UI 线程写的初始化日志、测试线程对 `m_lines` 的遍历同时发生，而 `RecordingHost` 追加与读取都不加锁。据推测是 `vector` 扩容时测试线程读到了正在搬移的元素，取到空串，前缀比较因此失败。已给 `RecordingHost` 与假网络层 `FakeNetwork` 加锁，测试代码改为经加锁的读取函数访问；改后 `[p2phost]` 连跑 20 次、上述组合连跑 3 次都通过。同类不加锁的测试宿主还有 `FlamingoFileShare/Tests/ShareManageTests.cpp`、`ShareBrowseLiveTests.cpp` 与 `Source/Tests/MediaModuleLoaderTests.cpp` 三处，尚未修改。
- **状态**：测试宿主的数据竞争已修正，原故障未再出现，继续观察；长期不出现再删除本节。

## BUG-98　文件共享用例 SPV8 偶发失败：写操作的审计次数为 0

- **发现日期**：2026-10-05（屏幕共享 F7 编译后跑完整单元测试 `FlamingoUnit_share.exe "~[live]"` 时）
- **位置**：`flamingoclient/FlamingoFileShare/Tests/ShareProviderTests.cpp` 的 SPV8「经线程池异步执行」，第 822 行 `CHECK(listener.m_writeReports.load() == 1)`
- **现象**：4182 个用例里只有这一条断言失败，实际值为 0。随后单独运行 `FlamingoUnit_share.exe "SPV8*"` 连续 30 次都通过。
- **影响**：只是用例偶发失败，重跑即可通过；据推测不是产品问题。
- **初步排查**：据实证，是用例与工作线程之间的时序竞争：异步写任务先在 `FlamingoFileShare/Engine/ShareProvider.cpp:256` 用 `SendViaCore` 发出结果帧，到第 275 行才调用 `ReportWrite` 记审计；用例只等到通道里出现结果帧（第 812~814 行的循环）就检查审计次数，工作线程此时可能还没执行到 `ReportWrite`。修法是用例在检查审计次数之前也等待 `m_writeReports` 变为 1（带超时），或者工作线程先记审计再发结果帧。发现时这两个文件属于另一个会话尚未提交的文件共享功能，没有修改。
- **状态**：未修复。

## BUG-99　系统设置「快捷键」页写的快捷键与程序实际注册的不一致

- **发现日期**：2026-10-05（为单聊 / 群聊截图加快捷键做方案时）
- **位置**：`flamingoclient/Source/UI/BalloonSettings/BalloonSystemSettingDlg.cpp` 的 `BuildShortcutPage`（第 1285、1286 行）
- **现象**（据实证）：快捷键页把「打开主面板」写成 `Ctrl + Alt + F`，而登录成功后注册的是 `Ctrl + Alt + D`（`Source/UI/BalloonMain/MainPanelSessionController.h:68` 的 `kShowPanelHotKey`，注册在 `MainPanelSessionController.cpp:605`）；同一页写着「截图 `Ctrl + Alt + A`」，但整个 `Source/` 里只有上述 `Ctrl+Alt+D` 与录屏的 `Ctrl+Alt+R`（`Feature/ScreenShare/RecordEntry.cpp:25`）两处 `RegisterHotKey`，截图快捷键并不存在。
- **复现**：登录后打开系统设置的「快捷键」页，按页上写的 `Ctrl+Alt+F` 没有反应，按 `Ctrl+Alt+D` 才调出主面板；按 `Ctrl+Alt+A` 没有反应。
- **影响**：用户照着设置页按快捷键无效，会误以为快捷键功能坏了。
- **初步排查**：快捷键页的键帽文字是写死的字符串，没有引用 `kShowPanelHotKey` 等常量，两边各改各的就会不一致。截图快捷键会随「截图快捷键与隐藏当前窗口」功能一起实现。2026-10-05 与用户确认「打开主面板」以程序实际注册的 `Ctrl + Alt + D` 为准，只改设置页上的文字。
- **状态**：代码已改、尚未编译验证（2026-10-05）：「打开主面板」的键帽文字改由 `kShowPanelHotKey` 生成，「截图」一行改为可录入的键帽（默认 `Alt + Z`）。进度与后续步骤见 `docs/截图快捷键与隐藏当前窗口方案.md` 第 6 节，核对通过后再改为已修复。

## BUG-102　日志耗时用例 LOG1 在机器负载高时偶发失败

- **发现日期**：2026-10-05（屏幕共享 F9 完成后跑完整单元测试 `FlamingoUnit_share.exe "~[live]"` 时）
- **位置**：`flamingoclient/Source/Tests/IULogTests.cpp` 的 LOG1「连续打 2000 行日志，调用线程上每次调用平均耗时低于 0.1 毫秒」，第 228 行 `CHECK(avgMs < kLog1MaxAvgMs)`
- **现象**：完整运行时实测平均 0.177 毫秒；随后单独连跑五次，三次通过、两次失败，实测在 0.078~0.117 毫秒之间。当时有五个编译器进程在并行编译（十几个会话同时在编），机器负载很高。
- **影响**：只是用例偶发失败；据推测不是产品问题，负载低时能稳定通过。
- **初步排查**：用例按墙钟计时、阈值固定为 0.1 毫秒，没有考虑机器上其他进程占满 CPU 的情况。可选的修法：按 CPU 时间（`GetThreadTimes`）计时，或在负载高时放宽阈值；也可以给它加一个只在单独运行时检查的标签。尚未修改。
- **补充实测**（2026-10-08，屏幕共享第 14 节收尾验证时）：整机 CPU 负载 95%～100% 时完整运行与单独运行各失败一次，分别为 0.128、0.187 毫秒，另一次完整运行为 0.108 毫秒；负载降到约 71% 后单独连跑三次全部通过（0.091～0.099 毫秒）。结论同上：贴着阈值随负载波动，与产品代码无关。
- **状态**：未修复。

## BUG-106　单聊窗粘贴一张图片后点「发送」没有反应，图片发不出去

- **发现日期**：2026-10-05（用户反馈，当时运行的是 23:16 编译的 `Bin\Flamingo.exe`，账号 MY0303，23:29:40 启动）
- **位置**（初步）：`flamingoclient/Source/UI/BalloonChatWnd/BalloonChatWindow.cpp` 的 `DoSend()`；输入框粘贴走 `ChatInputRichEdit::TryPasteChatContent`（只接管 Flamingo 自己的剪贴板格式），其余交给 balloonui `DuiRichEdit::Paste()`，即把 `WM_PASTE` 交给 RichEdit 自行处理
- **现象**：粘贴图片后点发送，界面没有变化，图片没有发出去
- **初步排查**（据实证，依据为日志 `Bin\Logs\flamingo.20261005232940.22376.log` 与本地库 `Users\MY0303\ChatLog\msgv2.db`）：这段时间里的四次「单聊窗发送一段文字」（msgid 378～381）都是用户打字发出的文字（如「xxx 」「sss」），与粘贴图片无关；粘贴图片后点发送的那几次，日志里没有留下任何记录，连 `SendImagePartReal` 找不到源文件时的警告「内嵌图序号 … 无对应源文件」也没有出现。`DoSend()` 中能不打日志就返回的地方有：离线检查、取不到输入框或消息列表、以及枚举结果为空（第 4933 行 `parts.empty()`）。据推测，可能是粘贴进来的图片既没有被 `DuiRichEdit::EnumContent` 枚举成图片段，也没有作为文字出现，导致内容被当成空的；也可能是点击根本没有走到 `DoSend()`。二者尚未区分。另外，balloonui 与客户端都没有给 RichEdit 设置 `IRichEditOleCallback`，从剪贴板粘贴的位图没有登记到 `m_pendingShotPaths`，即使被枚举成图片段，现有代码也只能按「无对应源文件」跳过
- **下一步**：确认图片来源（截图工具 / 浏览器复制 / 资源管理器复制文件）与粘贴后的界面状态；在 `DoSend()` 的各个静默返回处补日志；用不碰系统剪贴板的方式（`IRichEditOle::ImportDataObject` 送入 CF_DIB）写用例复现粘贴后 `EnumContent` 的结果
- **原因**（2026-10-08 据实证，实验用例 PIM0，`tmp/verify7/pim0.log`）：输入框按 Ctrl+V 时按键直接交给 RichEdit 引擎，绕过了聊天窗设置的「默认按纯文本粘贴」；剪贴板里只有位图时（截图「完成」、系统截图工具），引擎把位图作为原生图片插入（不是 OLE 对象），`EnumContent` 把它读成一个空格，`BuildOutgoingParts` 把只有空白的文字丢掉，`DoSend` 在 `parts.empty()` 处静默返回。2026-10-08 用户实测截图后粘贴再次遇到（`Bin/Logs/flamingo.20261007235500.11308.log`）
- **修法**（2026-10-08 用户确认）：剪贴板里没有文字、只有图片时，把图片存成临时 PNG，交给聊天窗按截图「发送到对话框」同一路径（`StageImageInInput`）插入；`DoSend` 静默返回处补日志。详见 `docs/截图独立模块方案.md` 第 7 节
- **状态**：代码已修复（2026-10-08，用例 PIM1～PIM3 通过），待实机核对：截图进剪贴板后在单聊窗、群聊窗各按 Ctrl+V 与右键「粘贴」，点「发送」能发出图片

## BUG-107　消息记录窗右侧取最小宽度时，筛选行最后一枚「Pin」胶囊被时间下拉压住

- **发现日期**：2026-10-06（聊天记录管理器增强实机核对时，调试入口 `--history`，窗口缩到最小宽度）
- **位置**：`flamingoclient/Source/UI/BalloonHistory/HistoryWindowLayout.cpp` 的 `BuildHistoryWindowBody`，筛选行（`kIdHistoryFilterRow`）里七枚类型胶囊所在的容器按权重分宽度，右侧是时间下拉与「选日期」两个固定宽度的控件
- **现象**（据实证，截图 `tmp/histenhance/shots/13_narrow_list.png`）：右侧界面取最小宽度 640 时，筛选行去掉左右内边距只剩 600 像素，七枚胶囊合计 440、时间下拉至少 120、「选日期」约 60，加上间距超过可用宽度；胶囊容器分到的宽度不够，最后一枚「Pin」画到了时间下拉底下，只露出左边一小截。这一行的代码在聊天记录管理器增强（2026-10-05）之前就是这样，增强期间没有改动它。
- **复现**：打开聊天记录管理器，把窗口拖到最小宽度（或在左栏展开时把分隔条往右拖到底）。
- **影响**：最小宽度下「Pin」胶囊几乎点不到；宽一点的窗口不受影响。
- **尺寸账**（据实证，2026-10-06 截图 `tmp/histenhance/shots/13_filter_row.png` 上量得，125% 缩放）：筛选行可用 600 像素；右侧时间下拉 120（628～748）、「选日期」约 69（756～825）、两段间距 16，共 205；留给胶囊 395，而七枚胶囊各 56 加六段间距 8 需要 440，差 45。胶囊容器放不下时不压缩子控件，照样按 56 一枚往右排，「Pin」排在 609～665，被从 628 开始、后画的下拉盖住。右侧宽度小于约 685 像素时出现；选了「只看这段时间」时日期区间标签比下拉更宽，需要的宽度更大。默认窗口（右侧约 898）不出现。
- **初步排查**：三种修法：A. 放不下时压缩胶囊的宽度与间距（最小宽度不变，日期区间标签变宽时也能自适应；改动最多，要在尺寸变化时重算胶囊宽度）；B. 把「选日期」并进时间下拉（省出约 77 像素；改变 2026-10-01 定下的交互，日期区间标签很宽时仍可能放不下）；C. 提高右侧最小宽度 `kMgrBodyMinW`（只改常量；要覆盖日期区间标签得加大约 100 像素）。我倾向 A。
- **状态**：未修复，2026-10-06 用户要求先记录，暂不修。

## BUG-108　MySQL 的 t_chatmsg.f_content 是 blob（上限 65535 字节），而服务端允许单条消息内容到 256 KiB

- **发现日期**：2026-10-06（调研「Markdown 消息」方案时，代码走读发现，未实测）
- **位置**：`flamingoserver/table_mysql.sql:58`（`f_content blob`）与 `flamingoserver/mysqlmgr/MysqlManager.cpp` 里自动建表的同名列；上限常量 `flamingoserver/chatserversrc/MessageProtocol.h:329` 的 `MESSAGE_CONTENT_MAX_BYTES = 256 KiB`（检查函数 `isMessageContentTooLarge`），客户端 `ChatSendLimit.h` 按同一上限预检
- **现象**（据推测，尚未实测）：`f_content` 存的是 `{"content":…,"mentions":…,"quote":…}` 整体 JSON，非 ASCII 字符被转义成 `\uXXXX`，一个汉字占 6 字节，所以约一万个汉字就会超过 65535 字节，而服务端与客户端的检查都会放行到 256 KiB。服务端连接只执行了 `set names utf8`（`mysqlapi/DatabaseMysql.cpp:185`），没有设置 `sql_mode`：若 MySQL 处于严格模式，插入失败，客户端收到「发送失败」；若不是严格模式，JSON 被静默截断，之后读出这条消息时解析失败。SQLite 模式的列是 BLOB，不受此限
- **复现**（待验证）：在 MySQL 后端下发送一条转义后约 7 万字节的文字消息，查看发送结果与 `t_chatmsg` 中这一行的长度
- **影响**：长文字消息（包括今后的 Markdown 长文）可能发送失败或存坏
- **初步排查**：修复需要把列改成 `MEDIUMBLOB` 并写迁移语句——启动时的建表检查只补缺失的列，不会修改已有列的类型（`table_mysql.sql:77` 的注释）；或者把两端的上限降到 64 KiB 以内
- **状态**：未修复，待实测确认严格模式下的表现

## BUG-109　中转客户端握手只等 500 毫秒，握手往返超过 0.5 秒时连接失败（通话、远程桌面、文件夹传输）

- **发现日期**：2026-10-06（网络状况标识第 3 期 3d 弱网校准时）
- **位置**：`flamingoclient/FlamingoMedia/Call/CallRelayClient.cpp` 的 `HandshakeChannel::RecvAll`；同样的写法还在 `FlamingoMedia/RemoteDesktop/RdRelayClient.cpp` 与 `Source/Feature/FolderTransfer/FolderRelayClient.cpp`（三处注释都写着「握手是一来一回的短交互，超时即视为对端没有按协议应答，不再续等」）
- **现象**（据实证）：弱网校准用例 CRL2（`Source/Tests/CallReportLiveTests.cpp`，`[.live][callcal]`）经 `tmp/callaudio/netem_proxy.py` 给中转连接加单向 250 毫秒延迟（握手往返约 500 毫秒）时，两端都在开始后约 0.51 秒记 `CallRelayClient: handshake failed: no handshake response from server (timed out or connection closed).`，通话没有建立，双方各收到 0 份对方报告；每 2 秒卡住 500 毫秒的条件下同样失败。原因：`DoConnect` 连上后给套接字设了 `SO_RCVTIMEO = kRecvTimeoutMs`（500 毫秒，本意是让接收线程定期醒来检查停止标志），握手用的是同一个套接字，`RecvAll` 第一次超时就判失败，`Connect` 传入的 5 秒握手时限（`timeoutSec`）实际没有用上。
- **影响**：网络很差（手机热点、跨境线路拥塞、无线信号弱）、握手往返超过 0.5 秒时，一对一通话连不上中转、没有声音和画面；远程桌面与文件夹传输的中转连接同理。P2P 直连失败要退回中转时恰恰多是网络差的场景。
- **修复**：通话这一处已修（2026-10-06）：`RecvAll` 遇到接收超时时，只要握手总时限（`timeoutSec`，默认 5 秒）未到、也没有被要求停止就继续等，连接关闭或出错仍立即失败。验证（据实证）：修复后 CRL2 在单向延迟 250、450 毫秒（往返约 1.0、1.8 秒）和每 2 秒卡住 500 毫秒三种原先失败的条件下都建立了通话，双方每秒互收报告；直连中转的 CRL1 照常通过。远程桌面与文件夹传输两处未改（不在本次工作范围内，模块由其它会话负责），修法相同。
- **状态**：通话已修复；远程桌面、文件夹传输未修复。

## BUG-114　PC 客户端断线重连时把「登录方式」当成 clienttype 发给服务端，账号形如手机号时以 clienttype=0 登录

- **发现日期**：2026-10-06（做「登录账号与手机号登录」时排查客户端登录名的用法，读代码发现）
- **位置**：`flamingoclient/Source/App/FlamingoClient.cpp` 第 353～356 行按 `IsMobileNumber(m_strAccount)` 把请求的 `m_nLoginType` 设成 `LOGIN_USE_MOBILE_NUMBER`（0）或 `LOGIN_USE_ACCOUNT`（1）；`Threading/SendMsgThread.cpp` 第 155～158 行的 `HandleLogon` 把它原样传给 `CIUSocket::Login` 的 `nClientType` 形参，最终写进登录请求的 `clienttype`（`net/IUSocket.cpp` 第 4754 行）。登录窗那条路径传的是常量 `kLoginClientType = 1`（`UI/BalloonAuth/BalloonLoginDlg.cpp` 第 107 行），不受影响。
- **现象**（据推测，读代码得出，未实际复现）：断线重连与「从离线恢复在线」走 `CFlamingoClient::Login`，此时 `m_strAccount` 已是服务端返回的账号。账号是 11 个字符、以 13 / 15 / 18 开头时（`Common/Util/Utils.cpp` 第 1328 行的 `IsMobileNumber` 只看长度与前两个字符），重连请求带的是 `clienttype=0`；其余账号带 1，与登录窗一致，只是碰巧对上。
- **影响**（据推测）：服务端按 clienttype 做同类型设备顶号（`ChatSession.cpp` 的 onLogin 第 6 步）并在设备列表里展示，clienttype 为 0 的会话既不会被另一台 PC 顶掉，也不会被认成 PC。目前服务端生成的账号是「字母前缀 + 8 位数字」或 `EMP` + 用户 ID，都不会被认成手机号，所以现有账号触发不了；只有存量里本身就是 11 位手机号样式的老账号会触发。用户输入手机号登录不受影响：重连用的是服务端返回的账号，不是输入的手机号。
- **初步排查**（据推测）：`HandleLogon` 应当与登录窗一样传 `kLoginClientType`，`m_nLoginType` 这个 v1 遗留字段不再参与 v2 登录请求。修复前先补一条单测，断言重连请求的 clienttype 与账号形态无关。
- **状态**：未修复。

## BUG-115　登出或被踢下线后，登录窗不按历史记录恢复「记住密码」勾选，直接再登录会清掉已存的密码

- **发现日期**：2026-10-06（做「登录账号与手机号登录」界面联调时发现）
- **位置**：`flamingoclient/Source/UI/BalloonAuth/BalloonLoginDlg.cpp` 第 1268～1273 行（`PopulateAccountCombo`）：调用方给了默认账号（登出 / 被踢下线后 `CSessionController::StartLogin` 经 `SetDefaultAccount` 传入）时只填账号与密码文字，不调 `LoadAccountIntoUi`；只有没给默认账号的冷启动分支才按历史记录恢复两个勾选框。
- **现象**（据实证）：账号勾了「记住密码」与「自动登录」登录后点「登出」，历史记录里该账号为「记住密码 1、自动登录 0」（读 `LoginAccountList.dat` 所得），登录窗里密码已填好，但两个勾选框都是空的（截图 `tmp/loginacct/e2e/ui_12_after_logout_fixed.png`）。与用账号还是手机号登录无关。
- **影响**（据推测）：用户此时直接点「登录」，`RecordLoginSuccess` 按「没勾记住密码」处理，把已存的密码清掉，下次启动要重新输密码。
- **初步排查**（据推测）：默认账号分支在填文字之后，若历史列表里有这个账号，也按记录恢复勾选框（密码仍用调用方给的那份）。
- **状态**：未修复。

## BUG-116　登录窗删掉最后一个历史账号后，账号列表文件不会写回，下次启动这个账号还在

- **发现日期**：2026-10-06（做「登录账号与手机号登录」界面联调时发现）
- **位置**：`flamingoclient/Source/Session/LoginAccountList.cpp` 的 `SaveFile`：列表为空时（`nCount <= 0`）直接返回 FALSE、不写文件。`CBalloonLoginDlg::RemoveAccountFromHistory` 删完后调它落盘。
- **现象**（据实证）：本机只有一个历史账号时在下拉框里删掉它，日志为 `history account removed: account=13591257836, index=0, saved=0, remaining=0`，而 `Users\LoginAccountList.dat` 仍是删除前的内容（条数 1、账号 13591257836）。
- **影响**（据推测）：下次启动登录窗照样预填这个账号，记住的密码与自动登录也一并保留；用户以为已经删掉了本机的登录记录，实际上没有。
- **初步排查**（据推测）：列表为空时写一个条数为 0 的文件（或删除文件），而不是返回失败；`LoadFile` 读到条数 0 时本来就能正常处理。改前先补一条单测：删空后落盘再读回，列表应为空。
- **状态**：未修复。

## BUG-117　在同一个进程里登出后删除该账号并勾选「同时删除本地聊天记录」，本地库删不掉

- **发现日期**：2026-10-06（做「登录账号与手机号登录」界面联调时发现）
- **位置**：`flamingoclient/Source/UI/BalloonAuth/AccountRemoval.cpp` 的 `DeleteAccountLocalData`（用 `SHFileOperation` 删 `Users\<账号>\` 整个目录）。
- **现象**（据实证）：登录后登出，在登录窗删除这个账号并勾选「同时删除本地聊天记录」，日志为 `local data of the removed account deleted: account=13591257836, data folder=FL.52527657, result=0`，目录里其余文件已删掉，只剩 `ChatLog` 下的 `announce.db`、`filelocal.db`、`folderlocal.db`、`msgv2.db`、`recordlocal.db` 五个库。
- **原因**（据推测）：登出时没有关闭这几个本地库（`CBalloonMainPanel::EnsureMsgStoreOpen` 等处打开），进程仍持有文件句柄，删除失败。
- **影响**：用户以为聊天记录已经删掉，实际还留在磁盘上；界面上没有任何提示。退出程序后再删则不受影响（据推测）。
- **初步排查**（据推测）：登出时（`ResetForLogout`）关闭本人的各个本地库；或删除失败时提示用户退出程序后再删。
- **状态**：未修复。

## BUG-118　中转路径的视频通话里，开摄像头的一方少采约 5% 的声音，对方听到持续断续；对方的播放缓冲还可能积压到约 700 毫秒

- **发现日期**：2026-10-06（通话播放端改用自适应抖动缓冲之后，在本机两个联调客户端之间做听感核对时）
- **位置**：
  - `flamingoclient/FlamingoMedia/Call/WasapiAudioIo.cpp` 的采集循环（约第 300~325 行）：不检查 `AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY`，采集数据丢失时没有任何记录，帧序号照常连续；
  - `CallAudioEngine::OnAudioCaptured`：在采集线程上依次做回声消除、Opus 编码，并同步调用 sink 发送；
  - `CallMediaSession::OnEncodedAudio`（约第 616 行）在采集线程上调用 `CallRelayClient::SendData`，与视频线程（`OnEncodedVideo`，约第 637 行）共用一把发送锁，阻塞写同一个 TCP 连接（`CallRelayClient.cpp` 约第 342 行起）；
  - `CallJitterBuffer.cpp` 的最大迟到率 `kMaxLateRatePercent` = 1（2026-10-06 按弱网校准设定，数据见 `docs/音视频通话方案.md` 第 9.9.9 节）。
- **现象**（据实证）：会话 `5127-5308-1791270834000-1`，2026-10-06 15:14:05 起约 2 分钟，视频通话，中转路径，弱网代理全程 0 延迟，两端收发包数对得上、没有丢包。依据是 `tmp/callaudio/live/clientA/Logs/flamingo.20261006151304.03580.log` 与 `tmp/callaudio/live/clientB/Logs/flamingo.20261006151304.20844.log` 的 `call stats` 行：
  - B 开着摄像头（720p、2 Mbps），每 5 秒只发出 219~248 个音频包，平均 236.8 个，应为 250 个；A 的摄像头被 B 占用打不开（`hr=0xc00d3704`），每 5 秒稳定发出 249~250 个。
  - A 每个 5 秒窗口里的抖动缓冲缺失次数，与 B 少发的数目基本一致：收到 219 个时缺失 32 次，收到 216 个时缺失 37 次，收到 252 个时缺失 0 次。A 端卡顿率 63~243‰，评级多在「差」与「较差」之间。
  - A 的播放缓冲在 15:15:43~15:16:04 从约 30 毫秒涨到约 700 毫秒（5 秒统计依次为 189、401、629、696 毫秒），这段时间 A 听到的声音明显滞后。起点紧跟在 B 第二次发起屏幕共享（B 日志 15:15:42）之后。
  - B 收 A 时也有缺失（每 5 秒 0~39 次），但 A 发得稳定、包也都到了，现有日志区分不出是包成串到达还是 B 的播放线程没有按时取数。
  - 对照：2026-10-05 那次 P2P 通话（UDP 发送，不经过中转的发送锁）中，两端开麦时每 5 秒都发出 249~252 个（`tmp/callaudio/live/clientA/Logs/flamingo.20261005232141.10248.log`）。
- **原因**（据推测，未验证）：
  1. B 的采集线程被阻塞：音频包在采集线程上同步写中转 TCP 连接，需要等视频分片写完；也可能是整机 CPU 紧张（同一台机器上运行两个 Debug 客户端、三个服务端和代理，并做 1080p 软件编码的屏幕共享，B 端 NVIDIA 硬件编码器初始化失败）。阻塞时间超过 WASAPI 共享模式的采集缓冲时长后，这段声音丢失；由于帧序号照常连续，接收端看到的是「对方的时钟变慢了」。
  2. 对方时钟变慢后，speex 有时返回「缺失」并推进播放指针，之后到达的那个包就被当作过期包丢弃，因此还会多丢一部分。按 A 一个窗口的计数推算：收到 219 个，缺失 32 次、插入 18 次，按每 5 秒取 250 帧计算，实际播出的只有约 200 个。
  3. A 的播放缓冲积压：A 的播放线程在那段时间没有按时取数（B3 起 Opus 解码也在播放线程上做）；最大迟到率为 1% 时，speex 的时序历史窗口是 100×40÷1 = 4000 个包，约 80 秒（`third-party/speexdsp-1.2.1/libspeexdsp/jitter.c:821`，据实证），积起来的延迟要等早先的记录移出窗口才会降下来。CRL2 不接声音设备，收发两端节奏都很准，测不出这一点。
- **影响**：中转路径的视频通话（尤其同时共享屏幕时）对方听到持续断续、声音滞后；网络状况标识因卡顿率高而显示「较差」或「差」，但问题并不在网络。在同一台机器上运行两个客户端会放大 CPU 争抢，实际部署中的严重程度未知。
- **下一步**：先加诊断计数再复测，拿到实证后再定修法。诊断项写进现有的 5 秒统计日志：采集端的数据不连续次数、处理一帧的最长耗时、音频发往中转时等锁加写 socket 的最长耗时；播放端每 5 秒实际取的帧数、两次取数之间的最长间隔、单次混音（含解码）的最长耗时；抖动缓冲放入与取出的包数。设想的修复方向：中转发送挪到独立线程，采集线程只负责入队；采集与播放线程注册 MMCSS；采集遇到数据不连续时让帧序号随之前进；重新权衡最大迟到率。
- **状态**：未修复。

## BUG-119　系统设置「聊天」页的发送键与聊天窗 ▾ 菜单的发送键是两份配置，互不同步

- **发现日期**：2026-10-06（写「快捷回复」方案时调查发送按钮的 ▾ 菜单发现）
- **位置**：设置页读写本人账号 ini 的 `[ChatDlg] EnableEnterToSend`（`flamingoclient/Source/UI/BalloonSettings/BalloonSystemSettingDlg.cpp:912` 读、`:2175` 写，`Session/UserConfig.cpp:262` / `:389`）；聊天窗只读写 `Bin/config/flamingo.ini` 的 `[chat] sendmsgkey`，并在进程内缓存（`UI/BalloonChatWnd/ChatSendMode.cpp:16-57`，单聊窗读取点 `BalloonChatWindow.cpp:836`，▾ 菜单写入点 `:4834` / `:4839`，群聊窗 `BalloonGroupChatWindow.cpp:595` / `:2461` / `:2466`）。
- **现象**（据实证，按代码）：在设置页改发送键不影响聊天窗；在聊天窗 ▾ 菜单里改了，设置页仍显示旧值。
- **影响**：用户在设置页改了发送键后，聊天窗里按 Enter 的行为不变，看起来像设置不起作用。`flamingo.ini` 是本机公共配置，多个账号共用同一个发送键，也与「设置按账号保存」的约定不一致。
- **初步排查**（据推测）：统一以账号 ini 为准，`ChatSendMode` 改为读写 `CUserConfig`，两处改动后都能立即看到对方的结果；`[chat] sendmsgkey` 只在账号 ini 没有该项时作为初值读一次。计划在「快捷回复」一并处理，见 `docs/快捷回复方案.md` §3.4。
- **状态**：代码已修复（2026-10-06，随「快捷回复」一并处理，见 `docs/快捷回复方案.md` §3.4、§8；单测 QR1、QR6 通过），待实机核对：设置页与聊天窗 ▾ 菜单任一处改发送键，另一处立即显示新值。

## BUG-120　独立录屏勾了「生成字幕」时点「停止」，客户端偶发卡死（无响应）

- **发现日期**：2026-10-06（用户实测语音字幕时遇到一次）
- **位置**：未定位。停止录制时界面线程在 `CRecordController::StopRecording` → `RecordSession::Stop` 里 `join` 写入线程；写入线程每 20 毫秒取一次混音，经 `FlamingoMedia/Caption/RecordCaptionTap` 把声音交给字幕草稿会话（`FlamingoCaption/Record/CaptionRecording` → `CaptionStreamImpl::PushPcm`）。
- **现象**（据实证，客户端日志 `Bin/Logs/flamingo.20261006200414.06800.log`）：20:05:40 开始录整块显示器（只录电脑声音），20:06:21 在录制条上打开麦克风，20:06:33 第一句字幕到达时字幕浮层排除采集失败（错误码 8，浮层的问题已另行修复，见 `docs/语音字幕方案.md` 14.12）；此后日志里再没有任何记录，连录制收尾（`endCaption`）的日志都没有，用户点「停止」后程序无响应，最后被手动结束，没有留下转储。
- **影响**：录像能否保存未知；用户只能结束进程。
- **初步排查**（据推测）：界面线程卡在等待写入线程退出的可能性最大；逐一核对了字幕旁路、草稿会话、识别线程、实时字幕浮窗之间的加锁关系，没有找到与界面线程互等的环；录的是整块显示器（DXGI 屏幕复制），不涉及向本进程窗口同步发消息的截取方式。随后在 cdb 下运行客户端复现，这次没有卡住。
- **下一步**：再出现时不要结束进程，用 `tmp/caption/cdbrun` 的做法（cdb 启动客户端、`breakin.ps1` 中断后按 `on_break.txt` 导出全部线程堆栈与完整转储）拿到堆栈再定位。
- **已加的诊断**（2026-10-07，`docs/屏幕共享与录制方案.md` 第 14.9 节第 6 条）：一是 `RecordSession` 的写入线程每进入一步就记下步骤与时刻，`Stop` 等它退出超过 3 秒时写日志说明停在哪一步、之后每 5 秒一条（用例 RSTALL1～4）；二是 exe 的界面线程无响应检测 `Source/App/HangWatchdog`，界面线程卡住 15 秒以上时自动写一份带全部线程堆栈的转储到 `Users\dump\Flamingo_hang_*.dmp`（用例 HWD1～4）。再出现时即使用户结束了进程，也能从日志与这份转储定位。
- **状态**：未修复，未能稳定复现；诊断已加。

## BUG-121　账号配置读单聊窗、群聊窗高度时用的是「宽度」的键

- **发现日期**：2026-10-06（写「群成员部门树」方案时调查 `CUserConfig` 的群聊窗配置发现）
- **位置**：`flamingoclient/Source/Session/UserConfig.cpp:275` 读 `m_cyChatDlg` 用的是 `[ChatDlg] ChatDlgWidth`，`:305` 读 `m_cyGroupDlg` 用的是 `[GroupDlg] GroupDlgWidth`；写入时用的是正确的 `ChatDlgHeight` / `GroupDlgHeight`（`:427`、`:440`）。
- **现象**（据实证，按代码）：读回的高度等于存进去的宽度。
- **影响**（据实证）：`GetChatDlgHeight` / `GetGroupDlgHeight` 等四个读取接口目前在 `UserConfig.cpp` 之外没有任何调用者，所以眼下不影响界面；将来有人用它们恢复窗口尺寸时会得到错误的高度。
- **初步排查**：把两处读取的键改成 `ChatDlgHeight` / `GroupDlgHeight`，并补一条写入读回的单测。
- **状态**：未修复。

## BUG-123　登录窗与注册窗翻转时，转动的卡片四周露出窗口后面的画面，看上去像闪了一下

- **发现日期**：2026-10-07（用户实测翻转切换时反馈「翻转结束后目标对话框闪一下」）
- **位置**：`flamingoclient/Source/UI/BalloonAuth/WindowFlipTransition.cpp`（过渡窗口 `CWindowFlipTransition` 与软件绘制 `WindowFlipFrame`）。
- **现象**（据实证，`tmp/verify7/fliprec.py` 以每秒约 60 次录下真实客户端的翻转，图片在 `tmp/verify7/fliprec_out2/`）：过渡窗口画布是两个窗口矩形的并集，转动的卡片是一个梯形，梯形以外全透明；转到 90 度时卡片宽度为 0（用例 WF2），整块区域露出后面的桌面。窗口是白色、橙色，后面若是深色窗口，0.36 秒内亮、暗、亮变化一次，看上去像闪烁。过渡窗口销毁之后（含窗口四周 60 像素的阴影范围）画面不再变化。
- **已修复的另一原因**：注册窗每次新建，第一次显示时系统播放约 300 毫秒的淡入放大动画，同样会露出后面的画面；2026-10-07 已在 `SwitchDuiWindows` 里于翻转期间关掉目标窗口的 DWM 窗口动画（`docs/登录注册窗口翻转切换.md` 第 7 节，用例 WF9、实验 WFC）。
- **可选的改法**（2026-10-07 列给用户）：一、过渡窗口先铺一层不透明的底再画卡片；二、改成淡入淡出加轻微缩放；三、只转到约 60 度、中途交叉渐变切换画面，减少露出的面积。
- **状态**：未修复，用户 2026-10-07 决定暂不处理。

## BUG-124　客户端可以发送 msgtype=17（系统提示），群里任何成员都能伪造一行系统提示

- **发现日期**：2026-10-07（起草 `docs/机器人方案.md`、考虑机器人消息如何防伪时，读代码发现）
- **位置**：服务端 `flamingoserver/chatserversrc/MessageProtocol.cpp:1326-1334` 的 `isClientOperableMsgType` 只排除了共享、屏幕共享、入群邀请、通话、群公告、群备忘、云端录像这几类卡片，没有排除 `message_type_system`（17）；`ChatSession::onSendMessage`（`ChatSession.cpp:2726`）靠它拦截「只能由服务端生成」的消息类型，其后的 `checkSendContent` 只检查语音（`ChatSession.cpp:2737-2740` 的注释）。客户端 `flamingoclient/Source/net/MessageProtocolClient.cpp:957-961` 只要求发送者 `from > 0`，`from = 0` 的豁免只是为了放行服务端的系统提示，并不要求系统提示的 `from` 必须为 0。
- **现象**（据推测，读代码得出，未实际发请求验证）：任何群成员构造一条 `msgtype = 17`、`content = {"text": "张三 已被移出群聊"}` 的群消息，服务端照常落库并推给全体成员；群聊窗对 `msgtype == 17` 的消息不看发送者，一律画成居中的系统提示行（`flamingoclient/Source/UI/BalloonChatWnd/BalloonGroupChatWindow.cpp:5098-5110`），看上去与服务端发出的「退出群聊」「转让群主」等提示没有区别。带上 `tipkind` / `tipargs` 还能让各种界面语言都显示成对应的结构化提示。单聊同理（`BalloonChatWindow.cpp:7121`）。
- **影响**：普通成员可以伪造「某人被移出群聊」「群主已转让给某人」之类的提示误导其他成员。将来机器人消息靠 `content` 里的标记区分（`docs/机器人方案.md` §5.6），同样需要服务端保证这类标记只能由服务端产生。
- **初步排查**（据推测）：在 `isClientOperableMsgType` 里加上 `message_type_system`，撤回、置顶、转发等也用这个函数判断的路径（`ChatSession.cpp:3256`、`3391`、`5894`）需要逐个核对，确认系统提示本来就不允许这些操作；客户端可以另外只把 `from == 0` 的 17 画成系统提示行，作为第二道判断。修复前先补一条服务端单测复现（客户端发送 17 应被拒绝）。计划在 `docs/机器人方案.md` 第 1a 步一并修复。
- **状态**：服务端已修复（2026-10-08，`docs/机器人方案.md` 第 1a 步）。没有改 `isClientOperableMsgType`（撤回、置顶等路径的行为保持不变），而是新增 `MessageProtocol::isClientSendableMsgType`（在前者之外再排除 17），`onSendMessage`（1503）与 `onMassSend`（1516）改用它；同时服务端删掉客户端带来的 `content.bot`，转发时一并删掉 `bot` 与 `autoreply`。据实证：用例 BOT-S15（`tests/test_botengine.cpp`）通过（WSL g++，2026-10-08）。客户端「只把 `from == 0` 的 17 画成系统提示行」的第二道判断没有做。

## BUG-136　消息提醒管理窗口：点在会话行身上不会切换勾选，只有点复选框才行

- **发现日期**：2026-10-08（机器人「选择部门」小窗实机核对时，在同一套控件上发现）
- **位置**：`flamingoclient/Source/UI/BalloonNotifyMode/BalloonNotifyManageWindow.cpp` 第 453～458 行，按树控件的行级 `DUIN_CLICK` 切换勾选（注释写「点在行身上：同样切换勾选」）。
- **原因**（据实证，代码与实机）：树的每一行挂着 `MemberPickerRow`，它没有覆写 `OnLButtonUp`，于是按 `DuiControl::OnLButtonUp` 的默认处理在点落在自身矩形内时返回 true（`third-party/balloonui/balloonui/DuiControl.cpp` 第 288～296 行）；`DuiTreeView::OnLButtonUp` 看到挂着的控件「消耗」了这次松开，就不再发行级 `DUIN_CLICK`（`DuiTreeView.cpp` 第 3726～3733 行）。选择部门小窗最初也是这样写的，实机点在行身上只选中了这一行、没有勾上，证实了这条路径走不到。
- **现象**（据推测，消息提醒管理窗口本身没有实机核对）：在该窗口里点会话行的名字或右侧模式文字，行被选中（蓝底）但复选框不变；只有点最左侧的复选框才切换。
- **修复建议**：2026-10-08 起 `MemberPickerTreeView` 有一个默认关闭的开关 `SetToggleOnRowClick(true)`，打开后点在行内容区任何位置都发 `DUIN_PICKER_CHECK_TOGGLE`（选择部门小窗已经这样用）。在 `NotifyManageLayout.cpp` 建树时打开它，并删掉窗口里那条走不到的 `DUIN_CLICK` 分支即可。
- **状态**：未修复（不属于机器人功能，留给消息提醒模式的维护者处理）。

## BUG-137　「云端录像空间」面板的「配额」「GB」「保留」三个标签被截断

- **发现日期**：2026-10-08（功能开关搬到企业详情「功能开关」子 tab 后的实机核对）
- **位置**：`flamingoAdmin/Screens/CloudRecordQuotaPanel.cpp` 第 34～36 行，输入行里的文字标签是固定宽度：「配额」「保留」`kLabelW` 为 28 像素，「GB」`kUnitGbW` 为 22 像素。
- **现象**（据实证，截图 `tmp/featuretab/live/s6_features2.png`；改动前同样如此，见上午平台设置页的截图 `tmp/bot/live/b_settings2.png`）：输入行显示为「配… [50] G.. 保… [30] 天」。与面板放在哪个页面、页面多宽无关，固定宽度放不下当前字号的两个汉字与「GB」。
- **影响**：管理员看不出两个输入框分别是什么，只能靠下面一行说明文字猜。
- **初步排查**：据推测与本机的显示缩放有关（其它固定宽度的标签在 100% 缩放下按两个汉字定宽）；改法是按文字实际宽度给标签定宽，或把常量加大到在常用缩放下都放得下，未验证。
- **状态**：未修复（不属于这次搬迁，留待处理）。

## BUG-152　客户端用例 JBB6 在 Release 版里多数情况下失败：取包线程一个包也没取到

- **发现日期**：2026-10-10（文档与注释清理之后，在隐藏桌面上跑 Release|x64 的 FlamingoUnit 全量时发现）
- **位置**：`flamingoclient/Source/Tests/CallJitterBufferTests.cpp` 的 JBB6「线程安全：一个线程放、一个线程取，取出的时间戳严格递增」（第 253 行起）。生产者线程 `JbbProducer` 放 `kJbbThreadPackets`（500）个包、每 5 个包 `yield` 一次；取包循环不做任何等待，固定空转至多 `kJbbThreadPackets * 4` 次，生产者结束且已空转超过 `kJbbThreadPackets * 2` 次就退出。
- **现象**（据实证）：Release|x64 下单独运行 `FlamingoUnit.exe "JBB6*"` 10 次，2 次通过、8 次失败，失败都是第 283 行 `CHECK(got > 0)`，信息为「got 0 packets, last index -1」；同一时刻 Debug|x64 下运行 10 次全部通过。
- **影响**：只影响 Release 版测试结果，不涉及产品代码；Release 版跑全量时会多出一条失败，掩盖真正的问题。
- **初步排查**（据推测，未验证）：Release 版的取包循环空转得太快，在抖动缓冲攒够起播所需的包、开始出包之前就已经空转完并退出。修法是让取包循环按时间等待（例如每次取不到时短暂休眠，并以墙钟时间而不是空转次数作为退出条件），先在 Release 版上复现、修改后反复运行确认。
- **状态**：未修复。

## BUG-153　客户端用例 LSP1 在 Release 版里必然失败：它要复现的 ATL 断言只在 Debug 版存在

- **发现日期**：2026-10-10（同上，Release|x64 的 FlamingoUnit 全量）
- **位置**：`flamingoclient/Source/Tests/LanguagePromptTests.cpp` 的 LSP1「旧写法复现：在下拉框的选中通知里同步弹询问框，选「否」后触发 atlwin.h 的断言」。用例启动子进程，期望子进程因 CRT 断言报告而以 `kLspAssertExitCode`（0x4C53）退出，且结果里出现 `atlwin.h`。
- **现象**（据实证）：Release|x64 下子进程正常结束，退出码 0，结果为「selected: prompt now / prompt answered 7 / done」，第 433 行 `CHECK(code == kLspAssertExitCode)` 失败；Debug|x64 的全量运行里这条通过（flamingo-dc 会话 2026-10-10 的运行结果）。
- **影响**：只影响 Release 版测试结果。
- **初步排查**（据推测）：Release 版不编译 ATL 与 CRT 的断言，旧写法在 Release 下不会触发断言，用例的前提不成立。修法是 Release 版跳过这条用例（例如在非 `_DEBUG` 时直接 `SKIP`），或只在 Debug 版编进这条用例。
- **状态**：未修复。

## BUG-154　远程桌面与屏幕共享都没有处理屏幕旋转

- **发现日期**：2026-10-12（调查分辨率变化时的处理，docs/远程桌面控制权限与观看工具条方案.md 第 3.2 节）
- **现象**（据推测，未实测）：被控端或共享者把显示器旋转成纵向后，采集到的画面宽高互换。远程桌面在采集尺寸变化时会补发关键帧（2026-10-12 修复的部分），
  画面本身能恢复；但两条路径都没有按 `DXGI_OUTDUPL_DESC::Rotation` 旋转采集到的像素，DXGI 复制出的是未旋转的表面时画面会侧着显示，坐标换算也随之错位。
- **复现**：被控端（或共享者）在系统显示设置里把方向改为「纵向」，观看端看画面与鼠标落点。
- **影响**：只影响使用纵向显示器的人。
- **初步排查**：远程桌面 `RemoteDesktop/DxgiScreenCapturer.cpp`、屏幕共享 `Share/DxgiOutputCapturer.cpp` 都只取 `ModeDesc` 的宽高，没有读 `Rotation`。
- **状态**：未修复，本次（2026-10-12）不处理。

## BUG-155　远程桌面只采集 0 号显卡上的主显示器，不能选择别的显示器

- **发现日期**：2026-10-12（同上）
- **现象**（据实证）：被控端有多块显示器时，控制端只能看到并操作主显示器；指针移到别的显示器上时观看窗不画指针（`RdViewFit.h` 的 `MapRdRemoteToView` 按「不在被采集的屏幕上」处理）。
  `RemoteDesktop/DxgiScreenCapturer.cpp` 固定取 0 号显卡的 0 号输出，GDI 降级路径取 `SM_CXSCREEN / SM_CYSCREEN`。
- **复现**：被控端接两块显示器，把窗口拖到副屏，控制端看不到。
- **影响**：多显示器的被控端只能远程处理主屏上的内容。
- **初步排查**：屏幕共享已能选任意显示器（`Share/ShareSources.cpp` 的 `EnumerateShareMonitors`），远程桌面可以复用它加一个选择，键鼠注入的坐标原点也要换成那块显示器的左上角。
- **状态**：未修复，本次（2026-10-12）不处理。

## BUG-156　屏幕共享整块显示器时没有 GDI 降级

- **发现日期**：2026-10-12（同上）
- **现象**（据推测，未实测）：显示器共享只走 DXGI 桌面复制（`Share/DxgiOutputCapturer.cpp`）。在不支持桌面复制的环境里（部分虚拟机、远程登录会话、老显卡驱动）开始共享整块屏幕会直接失败；
  2026-10-12 起采集丢失后会按间隔持续重建，但重建一直失败时观看端始终是「画面暂停」。远程桌面在同样的情况下会降级到 GDI。
- **复现**：在远程登录（RDP）会话里发起屏幕共享，选整块显示器。
- **影响**：这类环境只能共享单个窗口。
- **初步排查**：可以参照远程桌面的 `RemoteDesktop/CaptureFallbackPolicy` 与 `GdiScreenCapturer`，DXGI 建立失败或连续丢失时改用 GDI `BitBlt` 抓整块显示器。
- **状态**：未修复，本次（2026-10-12）不处理。

## BUG-157　屏幕共享观看端没有「1:1 原始大小」显示

- **发现日期**：2026-10-12（同上）
- **现象**（据实证）：观看窗总是把画面等比缩放到窗口大小（`Share/ShareViewerWindow.cpp` 的 `CShareVideoCanvas::drawFrameIn`），共享者的屏幕分辨率比观看者高时文字变小、看不清，没有按原始像素显示并滚动查看的选项。
- **复现**：共享者用 2560×1440 的屏幕共享，观看者在 1366×768 的笔记本上看。
- **影响**：高分辨率共享在小屏幕上可读性差。
- **初步排查**：可以在浮动工具条上加「原始大小」开关，开启时画面按 1:1 绘制，鼠标拖动或滚轮平移；控制对方屏幕时坐标换算要加上平移偏移。
- **状态**：未修复，本次（2026-10-12）不处理。
