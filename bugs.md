# 已知问题 (bugs)

记录排查中发现、尚未修复的问题。每条注明：发现日期 / 现象 / 复现 / 影响 / 初步排查 / 状态。

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

## BUG-9　flamingoAdmin「编辑公告」弹窗首次点入正文输入框时闪烁一次

- **发现日期**：2026-08-16（迁移公告预览面板到无窗口富文本控件时顺带发现）
- **位置**：`flamingoAdmin/Screens/ComposeModal.cpp` 的 `m_editBody`。发现时它的类型是 `balloonwjui::DuiEditHost`，内嵌一个真的 Win32 输入框子窗口；该类已于 2026-08-17 删除，现在这个成员是无窗口的 `balloonwjui::DuiEdit`。
- **现象**：打开「编辑公告」弹窗后**第一次**点进正文输入框，输入框内容会闪烁一次；此后再点不再出现。点击弹窗**背后**的公告预览正文不会闪烁。
- **与本次迁移的关系**（据实证）：**无关**。本次迁移改的是公告预览面板的标题 / 正文（已从旧的 HWND 宿主富文本控件换成无窗口的 `DuiRichEdit`），而弹窗里这个输入框是 `DuiEditHost`，两处是不同的控件，`AdminFrame` 里针对 `DuiEditHost` 的创建与显隐处理也原样保留着。
- **初步排查**：据推测与「真 EDIT 子窗口首次获得焦点时自绘一帧」有关 —— `AdminFrame::HideModalEdits` 会在主窗口显示前把这些子窗口先建出来再 `ShowWindow(SW_HIDE)`，弹窗打开时再显示，首次点击获得焦点时子窗口重绘。尚未用埋点确认。
- **状态**：2026-08-17 **推定已随控件替换消失，待实机确认**。当日普通输入框整体改为无窗口实现（新控件 `DuiEdit`，旧类 `DuiEditHost` 已删除），`ComposeModal` 的三个输入框不再有真子窗口；同时 `AdminFrame` 里那套"主窗口显示前先把子窗口创建出来再隐藏"的处理（`EnsureEditsCreated` / `HideModalEdits` / `HideEditHwnds`）也一并删除，本条推测的成因从结构上不再成立。**尚未实机确认** —— 需要打开「编辑公告」弹窗、第一次点进正文输入框，确认不再闪烁，确认后本条方可关闭。

## BUG-12　CSkinManager::SetCurSkin 校验的是切换前的皮肤编号，传入不存在的编号也会成功

- **发现日期**：2026-08-17（给 DuiGallery 补「资源与皮肤」演示页时发现）
- **位置**：`third-party/balloonui/balloonui/SkinManager.cpp` 的 `CSkinManager::SetCurSkin`（第 109 至 123 行）。
- **问题**（据实证）：函数体是 `lpSkinInfo = GetSkinInfo(m_nCurSkinID);` —— 查的是**切换前**的当前皮肤编号有没有对应的皮肤信息，而不是调用方传进来的 `nSkinID`。只要当前编号是有效的，函数就把 `m_nCurSkinID` 改成 `nSkinID` 并返回 TRUE，完全不校验目标皮肤是否存在。
- **影响**：传一个不存在的皮肤编号会得到「切换成功」的返回值，此后 `GetAbsolutePath` 因为查不到皮肤信息一律返回空串，于是所有按皮肤取图的调用都拿不到图片，而调用方从返回值上看不出任何异常。反过来，若当前编号本身无效（例如配置文件里第一条皮肤缺失），即便传入一个有效编号也会返回 FALSE、切不过去。
- **状态**：未修复。修法是把 `GetSkinInfo(m_nCurSkinID)` 改成 `GetSkinInfo(nSkinID)`。**改动落在 balloonui 库内，需事先获得用户批准。**

## BUG-24　DuiFrameWindow 窗口用 ResizeClient 设尺寸，实际窗口比设计尺寸大一圈

- **发现日期**：2026-09-30
- **位置**：客户端二十来处 `ResizeClient(宽, 高)` 调用（`grep -rn "ResizeClient(" flamingoclient/Source --include=*.cpp` 可列出），涉及聊天窗、群聊窗、资料窗、消息记录窗、看图窗、群管理窗、公告窗、通话窗以及主面板下的各个对话框。登录窗已于同日修复，见下文。
- **问题**（登录窗据实证，其余窗口据推测）：ATL 的 `CWindow::ResizeClient` 先用 `AdjustWindowRectEx` 按窗口样式叠加系统标题栏与边框，再 `SetWindowPos`（`atlmfc/include/atlwin.h` 第 1946 行）。而 `DuiFrameWindow` 在 `WM_NCCALCSIZE` 里把非客户区整个并入了客户区，叠加出来的那一圈全部变成客户区。登录窗以 `WS_OVERLAPPEDWINDOW` 创建，120 DPI 下实测窗口为 398×609，设计尺寸是 380×562（`snapshots/login_before.png`）。
- **影响**：窗口比设计稿宽、高各多出十几到几十像素，多出的量随 DPI 与系统主题变化；按固定尺寸排好的布局会在某处多出一块空白。登录窗的表现是底部链接离登录按钮 106 像素、显得松散（用户 2026-09-30 反馈）。其余窗口是否受影响取决于各自的窗口样式：样式里不带 `WS_CAPTION` / `WS_THICKFRAME` 的不会叠加。
- **初步排查**：登录窗改为 `SetWindowPos(NULL, 0, 0, 宽, 高, SWP_NOZORDER | SWP_NOMOVE | SWP_NOACTIVATE)` 直接设整窗尺寸（`BalloonLoginDlg.cpp` 的 `DoModal`）。其余窗口逐个核对创建样式后再改；改完窗口会变小，布局若靠这一圈多余空间才排得下，须同时调整尺寸常量，最好每个窗口配一条「按设计尺寸排得下」的布局单测。也可以考虑在 `DuiFrameWindow` 里提供一个按整窗尺寸设置大小的方法，但那属于 balloonui 改动，须先经用户同意。
- **状态**：登录窗已修复（2026-09-30），其余窗口未核对。

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
- **位置**：`flamingoclient/Source/UI/BalloonHistory/HistoryWindowLayout.cpp` 的 `MeasureDefaultFontWidth`（发送人、时间范围两个下拉框，以及「选日期」按钮与日期区间标签的定宽）；`flamingoclient/Source/UI/BalloonMain/BalloonProfileLayout.cpp` 的 `ComputeFieldLabelColumnWidth`（资料卡左列宽）。
- **问题**（据实证，读代码所得）：这两处在构建布局时，按当时的全局 DPI 取默认字体量出文字宽度，再写成 `DuiLayout::Hint().Fixed(...)` 的固定像素宽。控件的字号现在会随所在窗口的 DPI 变化（`DuiControl::GetDpi`，宿主收到 `WM_DPICHANGED` 后经 `OnDpiChanged` 通知整棵控件树），但这些固定宽度只在建窗时算一次，DPI 变化后不会重算。
- **影响**：据推测，窗口从 100% 缩放的显示器拖到 150% / 200% 的显示器后，字号变大而下拉框、左列宽度不变，文字会被截断（资料卡左列设了 `DT_END_ELLIPSIS`，显示省略号）；反方向拖动则留白偏多。单显示器、不改缩放时不出现。
- **初步排查**：balloonui 的 Demo（XChat、CloudMelodyDesktop、DemoChatBubble 等）与 DuiGallery 页面里另有约 35 处仍用 `DuiResMgr::Inst().GetDefaultFont()` 按全局 DPI 取字体，多显示器缩放比例不同时，字号按最近一次建窗或 DPI 变化时的 DPI 走，属同类问题。库内控件与客户端界面已改为按控件 DPI 取字体，不在此列。
- **状态**：未修复。修法是让这两处布局在 DPI 变化后重新测量：例如在承载它们的控件的 `OnDpiChanged` 里按新 DPI 重算宽度并更新布局提示，或改由控件的 `GetDesiredSize` 按当前字体报告宽度、布局用 `Hint().Auto()`。Demo 与 DuiGallery 页面改为调用 `DuiControl::GetDefaultFont()` 即可，改动在 balloonui 仓库内，需事先获得用户批准。

## BUG-46　各窗口对显示缩放的处理不一致：群管理窗随 DPI 放大，其余窗口按 100% 的像素绘制

- **发现日期**：2026-10-01（用户反馈「群管理窗的文字和窗口尺寸比其他界面大」时查出）
- **位置**：
  - 放大的一方：`flamingoclient/Source/UI/BalloonGroupManage/`。窗口按 960×660 设计（`GroupManageLayout.cpp` 的 `kClientDesignWidth` / `kClientDesignHeight`），经 `DuiDpi::Scale` 换算；其余尺寸常量经 `GmScale`、字号经 `GmFontPixelHeight` 换算（`GroupManagePaint.cpp`）。
  - 没有放大的一方：单聊窗（`BalloonChatWindow.cpp` 的 `kChatWindowWidth` / `kChatWindowHeight` = 640×620）、群聊窗（`BalloonGroupChatWindow.cpp`，880×620）、消息记录窗（`BalloonHistoryWindow.cpp`，880×690）、主面板（`BalloonMainPanel.cpp`，320×680）都把常量直接交给 `ResizeClient`；这些窗口里的自绘控件用固定像素字号建字体，如群成员行昵称 `kNamePx = 13`（`GroupMemberItem.cpp`），`BalloonChatWnd/` 下 `ChatMessageList`、`ChatBubble`、`ChatHeaderInfo`、`ChatSendButton` 等十余处 `CreateFontW(-像素, ...)` 同样不随 DPI 变化。
- **问题**（据实证）：进程在 `App/Startup.cpp` 里调用 `DuiDpi::OptInPerMonitorV2()` 声明自行处理 DPI，系统不再替窗口整体拉伸，每个窗口须自己按 DPI 换算尺寸与字号。群管理窗做了换算，其余窗口没有。120 DPI（125% 缩放）下实测：群管理窗客户区 1200×825，即设计尺寸的 1.25 倍（日志 `Group manage window: dpi=120, client=1200x825`）；单聊窗、群聊窗、消息记录窗、主面板的窗口尺寸为 658×667、898×667、898×737、338×727，即设计常量加上 BUG-24 所述的那一圈，没有乘 1.25。
- **影响**：缩放比例大于 100% 的屏幕上，群管理窗按正确比例显示，其余窗口只有应有大小的约八成，文字偏小，两类窗口并排时差别明显；缩放比例越高差别越大。另外，未放大的窗口里 balloonui 标准控件（按钮、输入框、下拉框）用的默认字体是按 DPI 算的（`DuiResMgr.cpp`：`lfHeight = -MulDiv(9, dpi, 72)`，120 DPI 下为 15 像素），而同一窗口里的自绘文字是 13 像素，据推测同一窗口内会出现两种字号（尚未逐个截图核对）。100% 缩放的屏幕上不出现。
- **初步排查**：统一的方向应是让其余窗口也按 DPI 换算，而不是把群管理窗改回固定像素。改动面大：单聊、群聊、主面板、消息记录等窗口里写死的窗口尺寸、行高、边距、自绘字号都要经 DPI 换算，位图图标放大后可能发虚，需要按窗口逐个推进，每个窗口配布局单测（可参照群管理窗的 `GmSetDpiOverride`，让排版结果不随跑测试的机器变化）。与 BUG-24（`ResizeClient` 使窗口比设计尺寸大一圈）、BUG-45（按像素测出的宽度在 DPI 变化后不重算）属于同一类问题，修复时宜一并考虑。
- **状态**：未修复。用户 2026-10-01 决定先记录、暂不处理。
