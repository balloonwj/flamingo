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

## BUG-5　聊天窗「离线时输入框置为只读」从未生效（类型转换恒失败）

- **发现日期**：2026-08-14（为无窗口富文本控件梳理接口时顺带查出）
- **现象**：网络断开后，单聊 / 群聊窗口的输入框仍然可以正常打字。按设计此时应变成只读 —— 已写的草稿看得见、能复制，但打不了新字。
- **复现**：打开单聊或群聊窗口 → 断开网络（或停掉 chatserver）→ 顶部离线提示条出现、发送按钮正确置灰，但输入框照样能输入。
- **影响**：离线期间用户可以继续输入并尝试发送，与"发送按钮已置灰"的视觉状态自相矛盾。该功能自输入框从普通编辑框改为富文本框那次改动起就一直没生效过。
- **初步排查**（据实证）：
  - 出错代码在 `flamingoclient/Source/UI/BalloonChatWnd/BalloonChatWindow.cpp:3239` 与 `BalloonGroupChatWindow.cpp:1854`，两处都是：
    ```cpp
    balloonwjui::DuiEditHost* edit = dynamic_cast<balloonwjui::DuiEditHost*>(
        content->FindCtrlById(balloonchatwnd::kIdInputEdit));
    if (edit != NULL)
    {
        edit->SetReadOnly(!bOnline);   // 从未执行
    }
    ```
  - 但这两个 id 对应的控件是富文本输入框，不是 `DuiEditHost`。两者是**兄弟类**，分处不同的继承链，彼此之间没有继承关系，因此 `dynamic_cast` 恒返回 NULL，`if` 分支永不进入。（上面这段代码与类名都是发现该缺陷时的原貌。`DuiEditHost` 已于 2026-08-17 删除，普通输入框统一为无窗口的 `DuiEdit`，它与富文本控件是父子关系而不再是兄弟类 —— 也就是说同样的写法今天不会再有这个问题。）
  - 同一文件里取同一个 id 的其它九处都正确地转换成了富文本类型，只有"离线置只读"这一处漏改，应是输入框换类型时遗漏。
  - 这类错误编译期无法发现：`dynamic_cast` 到无关类型是合法写法，失败只表现为运行期返回 NULL，而代码又恰好用 `if (edit != NULL)` 把失败情形当作正常分支静默跳过。
- **状态**：2026-08-16 已修（据实证，仅限编译层面）。输入框迁移到无窗口富文本控件之后，两处的目标类型改为 `balloonchatwnd::ChatInputRichEdit*`，见 `BalloonChatWindow.cpp` 与 `BalloonGroupChatWindow.cpp` 中 `UpdateOnlineState` 一带的注释。**尚未做断网实测** —— 需确认离线时确实打不了字、且草稿仍可选中复制，实测通过后本条方可关闭。

## BUG-6　聊天窗富文本拆分模块残留死代码，且单测在为已废弃实现提供覆盖

- **发现日期**：2026-08-14（为无窗口富文本控件梳理接口时顺带查出）
- **位置**：`flamingoclient/Source/UI/BalloonChatWnd/`。
- **问题**（据实证，三处）：
  1. `BalloonChatWindow.cpp:3086` 与 `BalloonGroupChatWindow.cpp:402` 各定义了一个 `static CString GetRichEditPlainText(HWND hRich)`，**全仓库零调用**。函数上方 `:3093` 的注释还记录了它被弃用的原因（读回的内嵌对象占位符实测是空格，导致取不到内联图）。
  2. `ChatRichContentSplit.cpp:34` 的 `SplitRichInputToParts` **只有单测在调**（`Source/Tests/ChatRichContentSplitTests.cpp` 共 9 处）。生产代码已全部改走 `BuildOutgoingParts` —— `ChatRichContentSplit.h:55` 的注释明确写着新函数"取代上面的 SplitRichInputToParts"。
- **影响**：
  - 死代码本身不影响运行，但会误导后续阅读者，让人以为这是当前的正式路径。
  - 更实际的危害在第 2 条：`ChatRichContentSplitTests.cpp` 有 9 条用例在测一个生产环境**已经不走**的函数。测试全绿会给人"富文本拆分这条路径有覆盖"的错觉，而真正在用的 `BuildOutgoingParts` 的覆盖情况需要另行确认。
- **状态**：未修复。修法建议：删掉两个 `GetRichEditPlainText`；确认 `BuildOutgoingParts` 的用例覆盖足够后，把 `SplitRichInputToParts` 连同它那 9 条用例一并删除，或把用例改写为针对 `BuildOutgoingParts`。

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

## BUG-8　DuiRichEdit 量出来的内容高度偏小，且同样的行数会给出相差数倍的结果

- **发现日期**：2026-08-14（给无窗口富文本控件补自动增高的端到端测试时发现）
- **位置**：`third-party/balloonui/balloonui/Controls/Input/DuiRichEdit.cpp` 的 `MeasureContentHeight`（内部走排版引擎的 `ITextServices::TxGetNaturalSize`）。
- **问题**（据实证，2026-08-14 用 `BUI_DUI_TRACE=1` 抓到的埋点数据）：同一个控件尺寸（文本区 390×14）、同为 3 个段落的内容，量出来的高度差了将近 5 倍：

  | 内容 | 量出的内容高度 |
  | --- | --- |
  | `SetText("line one\r\nline two\r\nline three")` | 42 像素（每行 14，与 9 磅字号在 120 DPI 下的行高相符，是合理值） |
  | 逐字打入的 `"a\r b\r c"`（3 段） | 9 像素（每行 3） |
  | `SetText("a\r\nb\r\nc")`（同一控件实例，紧接着上一行执行） | 同样是 9 像素 |

  埋点同时显示出参宽度也不对：单个字符 `a` 量出宽度 1，`"aaa"` 量出宽度 2 —— 按像素算不可能这么小。已排除的因素（逐一实测）：多行属性确实是开着的（`multiline=1`）；引擎持有的默认字号正常（`EM_GETCHARFORMAT` 取回 180 twip，即 9 磅）；不是焦点导致的（给 `SetText` 那条用例补上取焦点后依旧正常）；不是「范围参数」被缩放导致的（把客户区矩形与范围成比例一起撑大后结果不变）；屏幕每英寸点数实测为 120，换算无误。
- **影响**：自动增高按这个高度决定控件长多高，量小了控件就长不够，内容会被截掉一截。目前**尚未在界面上观察到该现象** —— 演示页的富文本控件显示正常，据推测是因为默认下限（按一行的高度算）恰好把偏小的结果盖住了。风险在于把控件用在「最多五行」这类需要真实内容高度的场景时会显出来。
- **初步排查**：`MeasureContentHeight` 里已留有埋点（`BUI_TRACE("MEASURE ...")`，默认关闭，设环境变量 `BUI_DUI_TRACE=1` 打开），会输出调用返回值、出参宽高、范围参数与客户区矩形，可直接复现上表。下一步建议查两处：一是 `TxGetNaturalSize` 的出参单位到底是不是像素（当前代码按像素用）；二是引擎在文档刚被修改、尚未重新排版时是否会给出未定稿的结果。
- **状态**：未修复。相关测试 `Source/../Tests/DuiRichEditTests.cpp` 中的 `MeasureIndependentOfControlHeight` 已经钉住了「随行数增长」「不受控件当前高度影响」两条性质，但**没有**校验绝对值是否合理 —— 该用例里那条「一行不能低于 8 像素」的下界断言是唯一的绝对值防线，且当前恰好压线通过。

## BUG-9　登录必需目录建在程序目录，而按账号隔离的数据读写在数据根目录

- **发现日期**：2026-08-16（修「`CFlamingoClient::GetInstance()` 返回的不是在用实例」这个缺陷时，顺带核对缓存目录口径发现）
- **位置**：`flamingoclient/Source/App/MainDlg.cpp` 的 `CMainDlg::CreateEssentialDirectories`（第 4884 行起）。
- **问题**（据实证）：该函数用 `g_szHomePath`（程序目录）拼出 `Users\<账号>\`、`Users\<账号>\ChatImage\`、`Users\<账号>\UserThumb\` 三级目录并创建；而按账号隔离的数据实际读写走的是 `g_szDataRoot`（数据根目录）—— 账号配置见 `FlamingoClient.cpp:1382` / `1399` 的 `<g_szDataRoot>Users\<账号>\<账号>.cfg`，最近联系人列表见 `UserMgr.cpp:470` / `510` / `553`。两者默认相等，用户在「系统设置 → 存储」里改过数据目录之后就分成两棵树。
- **影响**（据推测，尚未实测）：改过数据目录的用户，首次登录时 `<g_szDataRoot>Users\<账号>\` 这一级没有任何地方预先创建，`SaveUserConfig` 与最近联系人列表落盘可能因父目录不存在而失败。缓存目录（ChatFile / ChatImage / Avatar）不受影响 —— 它们各自的 `buildXxxCachePath` 会在写文件前逐级创建。表现为「改过存储位置之后，用户设置与最近联系人列表每次重启都丢」。
- **初步排查**：`net/ImageTransfer.cpp` 的 `AvatarCacheFilePathForFileId` 原本也有同一类根目录错配（路径按 `g_szHomePath` 拼、父目录按 `g_szDataRoot` 建），已于 2026-08-16 改为统一取 `g_szDataRoot`。`CreateEssentialDirectories` 属于同一批遗漏，但它牵涉的是用户配置而非缓存，改动面与验证方式都不同，故单独记录。
- **状态**：未修复。修法：把 `CreateEssentialDirectories` 里的 `g_szHomePath` 改为 `g_szDataRoot`，并核对 `UserThumb` 目录的其余使用点（`CUserMgr::GetCustomUserThumbFolder`）是否同一口径。

## BUG-9　flamingoAdmin「编辑公告」弹窗首次点入正文输入框时闪烁一次

- **发现日期**：2026-08-16（迁移公告预览面板到无窗口富文本控件时顺带发现）
- **位置**：`flamingoAdmin/Screens/ComposeModal.cpp` 的 `m_editBody`。发现时它的类型是 `balloonwjui::DuiEditHost`，内嵌一个真的 Win32 输入框子窗口；该类已于 2026-08-17 删除，现在这个成员是无窗口的 `balloonwjui::DuiEdit`。
- **现象**：打开「编辑公告」弹窗后**第一次**点进正文输入框，输入框内容会闪烁一次；此后再点不再出现。点击弹窗**背后**的公告预览正文不会闪烁。
- **与本次迁移的关系**（据实证）：**无关**。本次迁移改的是公告预览面板的标题 / 正文（已从旧的 HWND 宿主富文本控件换成无窗口的 `DuiRichEdit`），而弹窗里这个输入框是 `DuiEditHost`，两处是不同的控件，`AdminFrame` 里针对 `DuiEditHost` 的创建与显隐处理也原样保留着。
- **初步排查**：据推测与「真 EDIT 子窗口首次获得焦点时自绘一帧」有关 —— `AdminFrame::HideModalEdits` 会在主窗口显示前把这些子窗口先建出来再 `ShowWindow(SW_HIDE)`，弹窗打开时再显示，首次点击获得焦点时子窗口重绘。尚未用埋点确认。
- **状态**：2026-08-17 **推定已随控件替换消失，待实机确认**。当日普通输入框整体改为无窗口实现（新控件 `DuiEdit`，旧类 `DuiEditHost` 已删除），`ComposeModal` 的三个输入框不再有真子窗口；同时 `AdminFrame` 里那套"主窗口显示前先把子窗口创建出来再隐藏"的处理（`EnsureEditsCreated` / `HideModalEdits` / `HideEditHwnds`）也一并删除，本条推测的成因从结构上不再成立。**尚未实机确认** —— 需要打开「编辑公告」弹窗、第一次点进正文输入框，确认不再闪烁，确认后本条方可关闭。

## BUG-10　群管理窗正在输入的公告会被后台刷新清空

- **发现日期**：2026-08-17（清理输入框无窗口化留下的过时注释时顺带发现）
- **位置**：`flamingoclient/Source/UI/BalloonGroupManage/GroupManageLayout.cpp` 的群公告页构建代码（第 1223 行起），与 `GroupManageWindowData.cpp` 的 `RefreshNavAndPage`（第 111 行）。
- **现象**：在群管理窗的「群公告」页里填写新公告，标题与正文可能在用户毫无操作的情况下被清空。
- **问题**（据实证）：`RefreshNavAndPage` 会整体重建客户区（其注释写明"InitUi 内部走的是 SetClientContent，它会先摘掉旧的客户区再挂新的"），重建时 `BuildPage` 重新构造标题与正文两个输入框，**只调用 SetPlaceholder 设置提示文字，不回填用户已经输入的内容**（第 1223-1238 行，两个输入框从头到尾没有 SetText）。用户输入的文本只存在于控件里，模型 `m_model` 中没有对应字段，因此重建之后无从恢复。
- **触发时机不止切换页面**，下列每一处都会调用 `RefreshNavAndPage`：拉取群资料返回（`GroupManageWindowData.cpp:150`）、拉取群成员返回（`:174`）、拉取加群申请返回（`:193`）、拉取公告列表返回（`:250`，该处注释写明"无论当前停在哪一页都要重建一次"）、以及收到服务端推送的群资料变更（`:356`）。也就是说用户正在输入时，后台任何一次数据返回或服务端推送都会清空输入。
- **影响**：写长公告时输入内容随机丢失，用户无法预判也无从挽回。据推测触发概率不低 —— 群管理窗打开后会并发拉取四类数据，且群资料推送随时可能到达。
- **与输入框无窗口化的关系**：无关。该缺陷在控件还是内嵌真子窗口时就已存在，两种实现下重建控件树的后果相同。
- **状态**：未修复。修法有两种：一是把输入中的标题与正文暂存进 `m_model`（或窗口成员），`BuildPage` 构建时回填；二是让 `RefreshNavAndPage` 在公告页且输入框非空时跳过重建、只更新左侧导航的计数徽章。前者更彻底，后者改动小。

## BUG-11　DuiResMgr::SetDpi 会销毁控件仍在持有的字体句柄

- **发现日期**：2026-08-17（重做 DuiGallery 界面布局、给高 DPI 补演示页面时发现）
- **位置**：`third-party/balloonui/balloonui/DuiResMgr.cpp` 的 `CDuiResMgr::SetDpi`，与 `balloonui/Controls/Basic/DuiLabel.h` 的 `SetFont`、`balloonui/Controls/Basic/DuiButton.h` 的 `SetTextPointSize`。
- **问题**（据实证）：`DuiResMgr::GetFontByPointSize(pt, bold)` 按 (磅值, 是否加粗) 缓存 `HFONT` 并返回，头文件写明"所有权在 manager，不要 DeleteObject"；而 `DuiLabel::SetFont` 的语义是"caller 持有，控件不会 DeleteObject"，控件里只存句柄。两处合起来的实际后果是：**控件长期持有一个由 manager 拥有的句柄**。`SetDpi` 在 DPI 变化时会对字体缓存里的每个句柄调 `DeleteObject` 并清空缓存表，此时所有仍在界面上的控件手里那份句柄立刻失效，随后的绘制会拿一个已销毁的 GDI 对象去 `SelectObject`。
- **触发路径**：`DuiHost` 的消息映射挂了 `WM_DPICHANGED`，处理函数 `OnDpiChangedMsg` 第一件事就是 `DuiResMgr::Inst().SetDpi(dpi)`。也就是说把窗口拖到另一块缩放比例不同的显示器上、或者运行期改系统缩放，都会走到这条路径。
- **影响**（据推测，尚未实测）：跨显示器拖动之后，凡是调过 `SetFont` / `SetTextPointSize` 的控件文字可能不再绘制、绘制成系统默认字体，或者在 GDI 句柄被复用后画出别的字体。DuiGallery 的段落标题（`DuiGallery/PageKit.cpp` 的 `AddSection`）与 `DuiButton::SetTextPointSize` 的每一个调用点都在这条路径上。
- **初步排查**：单显示器、不改系统缩放的环境下 `SetDpi` 只在启动时被调用一次，那时还没有控件持有句柄，所以日常开发中碰不到。
- **状态**：未修复。修法有两种：一是让控件不存句柄、改存 (磅值, 是否加粗) 并在每次绘制时向 manager 现取；二是让 `SetDpi` 不销毁旧句柄，改为把它们挂进一条待回收链、在进程退出时统一释放。前者彻底，后者改动小但会短暂多占一些 GDI 对象。**改动落在 balloonui 库内，需事先获得用户批准。**

## BUG-12　CSkinManager::SetCurSkin 校验的是切换前的皮肤编号，传入不存在的编号也会成功

- **发现日期**：2026-08-17（给 DuiGallery 补「资源与皮肤」演示页时发现）
- **位置**：`third-party/balloonui/balloonui/SkinManager.cpp` 的 `CSkinManager::SetCurSkin`（第 109 至 123 行）。
- **问题**（据实证）：函数体是 `lpSkinInfo = GetSkinInfo(m_nCurSkinID);` —— 查的是**切换前**的当前皮肤编号有没有对应的皮肤信息，而不是调用方传进来的 `nSkinID`。只要当前编号是有效的，函数就把 `m_nCurSkinID` 改成 `nSkinID` 并返回 TRUE，完全不校验目标皮肤是否存在。
- **影响**：传一个不存在的皮肤编号会得到「切换成功」的返回值，此后 `GetAbsolutePath` 因为查不到皮肤信息一律返回空串，于是所有按皮肤取图的调用都拿不到图片，而调用方从返回值上看不出任何异常。反过来，若当前编号本身无效（例如配置文件里第一条皮肤缺失），即便传入一个有效编号也会返回 FALSE、切不过去。
- **状态**：未修复。修法是把 `GetSkinInfo(m_nCurSkinID)` 改成 `GetSkinInfo(nSkinID)`。**改动落在 balloonui 库内，需事先获得用户批准。**

## BUG-13　拖入位图时回调拿到的句柄可能在回调返回后立即失效

- **发现日期**：2026-08-17（给 DuiGallery 补「拖放接收」演示页时发现）
- **位置**：`third-party/balloonui/balloonui/DuiDropTarget.cpp` 的 `Drop` 中处理 CF_BITMAP 的分支（第 203 至 207 行）。
- **问题**（据实证）：代码是 `HBITMAP copy = (HBITMAP)::CopyImage(med.hBitmap, IMAGE_BITMAP, 0, 0, LR_COPYRETURNORG);` 随后 `m_bmpCb(copy ? copy : med.hBitmap);`，回调返回后立即 `::ReleaseStgMedium(&med);`。`LR_COPYRETURNORG` 的语义是「当请求的尺寸与色深与原图一致时直接返回原句柄，不做复制」，因此 `copy` 很可能就是 `med.hBitmap` 本身；而 `ReleaseStgMedium` 对 TYMED_GDI 的介质会释放这个位图。
- **影响**：回调**执行期间**句柄是有效的，但调用方只要把它存下来供后续绘制（这正是拖入图片最常见的用法），存下的就是一个已被释放的 GDI 句柄。表现为图片时有时无，或在句柄编号被复用后画出别的内容。
- **初步排查**：库内目前没有生产代码使用位图拖入这条路径，所以尚未在实际功能中暴露。DuiGallery 的拖放演示页在自己这一侧用 `LR_CREATEDIBSECTION` 当场复制了一份来规避。
- **状态**：未修复。修法是把 `LR_COPYRETURNORG` 换成 `LR_CREATEDIBSECTION`（或去掉该标志），确保交给回调的一定是一份独立副本，并在头文件里写明句柄的所有权归回调方。**改动落在 balloonui 库内，需事先获得用户批准。**

## BUG-14　登出时聊天窗不会被关闭，且主面板的本次登录缓存从不清空

- **发现日期**：2026-08-18（评估「移除 CMainDlg、改由主面板作顶层类」的方案时，核对主面板生命周期发现）
- **位置**：`flamingoclient/Source/App/MainDlg.cpp` 的 `CMainDlg::ReturnToLoginWindow`（第 2426 行起），以及 `flamingoclient/Source/UI/BalloonMain/BalloonMainPanel.cpp`。
- **问题**（据实证，代码层面已逐行确认，尚未实机复现）：登出与被踢下线共用的收尾函数 `ReturnToLoginWindow` 只调了 `m_BalloonMainPanel.DestroyWindow()`，此外没有任何代码去关闭已经打开的单聊窗、群聊窗、群管理窗。这些窗口是**独立顶层窗口**——主面板建它们时走的是 `pWin->CreateAndShow(NULL)`（`BalloonMainPanel.cpp:2130`），而 `CBalloonChatWindow::CreateAndShow` 内部是 `Create(NULL, ...)`（`BalloonChatWindow.cpp:616`），父窗口与属主都是 NULL，所以销毁主面板的窗口不会连带销毁它们。主面板析构函数里确实有统一销毁的循环（`BalloonMainPanel.cpp:420-480`），但 `m_BalloonMainPanel` 是 `CMainDlg` 的值成员、随进程存活，登出时根本不会析构。
- 同一个根因还带来第二个后果：主面板对象跨越了登出边界，而它持有的本次登录缓存**从来没有被清空过**——`m_contacts`、`m_departments`、`m_lastContactsResult`、`m_groupInfo`、`m_myGroups`、`m_favorites`、`m_starred`、`m_chatWindows`、`m_groupChatWindows` 等全部保留到下一次登录。`m_conversations` 是唯一有 `clear()` 的（`BalloonMainPanel.cpp:3357` / `5945`），且那两处都在「会话列表到达后重填」的路径上，不是登出复位。
- **影响**（据推测，需实机验证）：
  - 登出后回到登录窗，上一个账号的聊天窗仍留在屏幕上，且窗口内还能看到聊天记录。登出确认框的文案「登出后将断开连接并关闭所有聊天窗口」（`MainDlg.cpp:2402`）与实际行为不符。
  - 换账号登录后，`GetOrCreateChatWindow` 对同一个 userid 会直接复用上一个账号留下的那个窗口实例（`BalloonMainPanel.cpp:2096-2102` 判 `IsWindow()` 为真即返回），于是新账号看到的是旧账号的会话窗口。
  - 群资料、收藏、特别关注等缓存同理跨账号残留。多数会被新登录拉取的应答覆盖，但键值在新账号下不存在的条目（例如上个账号才加入的群）不会被覆盖掉。
- **初步排查**：`ReturnToLoginWindow` 的注释写着「断开三条连接，再关掉所有聊天窗等子窗口」，但函数体内找不到对应实现，据推测是早期版本里由别处代劳、后来重构时丢失的。
- **状态**：已修复（2026-08-18）。做法就是上面建议的那条：给 `CBalloonMainPanel` 加了 `ResetForLogout()`，由 `CMainDlg::ReturnToLoginWindow` 在 `DestroyWindow()` **之前**调用（顺序不能颠倒——该方法要按控件 id 现查三棵树来清空节点，窗口销毁后就查不到了）。它依次做四件事：销毁全部子窗口、清空三棵树的节点、释放三个头像位图池、清空本次登录期间累积的全部数据缓存。
  - 子窗口销毁与位图池释放的代码原先只存在于析构函数里，现已抽成两个私有辅助函数 `DestroyOwnedSubWindows()` / `ReleaseAvatarBitmapPools()`，析构与 `ResetForLogout()` 共用，避免两处各写一份、日后只改了其中一份。
  - 顺手补了同一处的三个遗留问题：析构函数原先漏销毁 `m_historyWindow`、`m_announceBoxWindow`、`m_announcePopup`、`m_starredPopup`、`m_announcePopupDlg` 这五个按值持有的顶层窗口；`CreateAndShow` 无条件覆盖 `m_hTitleIcon`，第二次登录时上一只标题栏图标位图不会被释放；`m_groupInfoRequested` 不清会让新账号的群永远不再发出资料请求。
  - 未采用「把仅本次登录有效的成员集中到内嵌结构体」这一条，因为涉及的成员散布在 20 余处代码里，改动面远大于本次修复本身；该重构留待「移除 CMainDlg」的阶段 2 一并评估。
  - **验证情况**：客户端 `Debug|x64` 编译通过，`FlamingoUnit` 全量用例通过（22522 个断言 / 2112 个用例，过滤 `~[live]`）。**实机复现与回归尚未做**（需要一台可登录的服务端），据推测修复有效，实机验证后再把这一句改掉。

## BUG-15　BalloonHistory 下四个源文件用的是 balloonui 的 stdafx.h，不是客户端自己那份

- **发现日期**：2026-08-18（重排 `Source/` 目录、评估能否给包含目录加 `$(ProjectDir)` 时发现）
- **位置**：`flamingoclient/Source/UI/BalloonHistory/` 下的 `BalloonHistoryWindow.cpp`、`HistoryHighlightLabel.cpp`、`HistoryContentView.cpp`、`HistoryRow.cpp`，以及 `flamingoclient/Source/Flamingo.vcxproj` 里这四个条目的 `<PrecompiledHeader>NotUsing</PrecompiledHeader>` 设置。
- **问题**（据实证）：`Flamingo.vcxproj` 的四个配置默认都是 `<PrecompiledHeader>Use</PrecompiledHeader>`，而这四个文件被单独设成了 `NotUsing`。走预编译头的文件，编译器按 `#include` 的字面文本匹配 `/Yu` 的值、根本不去磁盘找文件；设成 `NotUsing` 之后这一行就变成一次真实的文件查找。而 `Source/` 目录本身**不在**包含目录里（工程的 `AdditionalIncludeDirectories` 只有 libsodium、balloonui、sqlite、opus、speexdsp 五项，`IncludePath` 只有 `third-party\wtl10.1\`），逐个检查这几个目录，其中唯一存在 `stdafx.h` 的是 `third-party/balloonui/balloonui/stdafx.h`。也就是说这四个文件包含到的是 balloonui 的预编译头。
- **影响**：目前编译与运行都正常，因为 balloonui 的 `stdafx.h` 恰好也拉齐了 ATL / WTL 那一套。但客户端 `stdafx.h` 里独有的宏与包含项对这四个文件是缺失的，日后往客户端 `stdafx.h` 里加东西时，这四个文件不会跟着变，表现为「同一个宏在别的文件里有、在这四个文件里没有」，排查起来很难想到根因。反过来，balloonui 的 `stdafx.h` 改动会意外影响到客户端这四个文件。
- **初步排查**：这四个文件被设成 `NotUsing` 的原因未查明，据推测是当初某次编译报错后就近关掉了预编译头。全工程 228 个 `NotUsing` 的 `.cpp` 里，只有这四个同时包含了 `stdafx.h`，其余都没有这一行，所以不受影响。
- **状态**：未修复。修法有两条：一是把这四个文件恢复成走预编译头（去掉 `NotUsing`），若当初是因为编译错误才关的，需要先确认那个错误是否还在；二是给工程的包含目录加上 `$(ProjectDir)`，让这一行能找到客户端自己的 `stdafx.h`。**本次目录重排刻意没有采用第二条**，因为那会在无声无息之间改变这四个文件包含到的是哪一份预编译头，属于超出重排范围的行为改动。

## BUG-16　ZlibUtil 把 size_t 变量的地址当作 uLongf* 传给 zlib，x64 下只有低 4 字节被写入

- **发现日期**：2026-08-18（清理客户端编译告警时发现）
- **位置**：`flamingoclient/Source/Common/Util/ZlibUtil.cpp`，`CompressBuf(const char*, size_t, char*, size_t&)` 与 `UncompressBuf(const std::string&, std::string&, size_t)` 两个函数里对 `compress()` / `uncompress()` 的调用。
- **问题**（据实证）：这两处都写成 `compress(..., (uLongf*)&nDestBufLength, ...)`，而 `nDestBufLength` 的类型是 `size_t`。zlib 的 `uLongf` 是 32 位，`size_t` 在 x64 下是 64 位，因此 zlib 只会写入该变量的低 4 字节，高 4 字节保持调用前的值。
- **影响**：目前没有观察到实际故障。调用前 `nDestBufLength` 被赋的是 `compressBound()` 的结果或调用方传入的解压后长度，取值都远小于 4GB，高 4 字节本来就是零，加上 x86 / x64 都是小端序，于是低 4 字节被覆盖后整个变量的值恰好正确。也就是说这段代码的正确性依赖「高位恰好为零」与「小端序」两个前提，任一不成立就会得到错误的长度，随后 `strDestBuf.append(pDestBuf, nDestBufLength)` 会按错误长度读取。
- **初步排查**：正确写法是引入一个 `uLongf` 类型的局部变量承接长度，调用结束后再赋回 `nDestBufLength`，而不是对 `size_t` 变量的地址做类型强转。
- **状态**：未修复。本次清理编译告警时刻意没有改，因为它不在告警清单里，且改动会触及运行期行为，应当单独评估并配测试后再动。

## BUG-17　六条 v2 应答消息在代理窗口没有转发分支，被静默丢弃并泄漏堆负载

- **发现日期**：2026-08-18（清点 `CMainDlg` 的 130 条消息映射、准备移除 CMainDlg 时发现）
- **位置**：`flamingoclient/Source/App/FlamingoClient.cpp` 的 `CFlamingoClient::ProxyWndProc`。
- **问题**（据实证）：客户端的网络应答要经过四层才能到达界面——网络线程 `PostMessage` 给代理窗口 → `ProxyWndProc` 按消息号 `switch` 转发给主窗口 → 主窗口的消息映射派发到处理器 → 处理器转交给具体窗口。`ProxyWndProc` 的 `switch` 末尾是 `default: return DefWindowProc(...)`（`FlamingoClient.cpp:2434` 附近），也就是说**没有 `case` 的消息会被直接丢弃**，`lParam` 上的堆对象一并泄漏。下列六条 v2 应答的 `case` 一直缺失：

  | 消息 | 消息号 | 投递点 | 处理器 | 后果 |
  | --- | --- | --- | --- | --- |
  | `FMG_MSG_V2_CHANGE_PWD_RESULT` | `WM_USER+462` | `IUSocket.cpp:2372` | `OnV2ChangePwdResult` | 改密对话框永远等不到结果 |
  | `FMG_MSG_V2_UPDATE_PROFILE_RESULT` | `WM_USER+463` | `IUSocket.cpp:2396` | `OnV2UpdateProfileResult` | 改签名 / 昵称失败时界面不回退；新昵称不会同步进历史登录账号列表 |
  | `FMG_MSG_V2_CHANGE_PRESENCE_RESULT` | `WM_USER+464` | `IUSocket.cpp:2445` | `OnV2ChangePresenceResult` | 改在线状态失败时状态胶囊不回退 |
  | `FMG_MSG_V2_SET_CONVERSATION_RESULT` | `WM_USER+485` | `IUSocket.cpp:2835` | `OnV2GroupWriteResult` | 置顶 / 免打扰设置失败时没有任何提示 |
  | `FMG_MSG_V2_SET_MEMBER_CARD_RESULT` | `WM_USER+486` | `IUSocket.cpp:2838` | `OnV2GroupWriteResult` | 改群名片既不提示成功也不回退 |
  | `FMG_MSG_V2_LEAVE_GROUP_RESULT` | `WM_USER+487` | `IUSocket.cpp:2842` | `OnV2GroupWriteResult` | 退群失败时没有任何提示 |

  六条的消息号都落在代理窗口的收件区间 `FMG_MSG_FIRST`(`WM_USER+50`) ~ `FMG_MSG_LAST`(`WM_USER+4999`) 之内，所以能通过 `ProxyWndProc` 开头的区间判断，只是随后在 `switch` 里无人认领。四个处理器函数本身都写好了、也都登记在 `MainDlg.h` 的消息映射表里，唯独中间这一层缺失。
- **影响**：多数操作走的是「乐观更新」——发起时界面先按成功的样子改掉，应答只负责失败回退与提示。因此这六条缺失平时看不出来，只在**服务端返回失败**时暴露：界面停在错误的状态上，用户以为改成功了。改密码是唯一一条不走乐观更新的，对话框会一直等不到结果。此外每收到一条就泄漏一个堆对象（`std::string` 或各 Response 结构体）。
- **初步排查**：这是本仓库的一类高发缺陷——自定义消息要接满四层，最容易漏的就是代理窗口这一层，症状是「网络通了但界面不动」，不报错、不崩溃、日志里也看不出来（网络层照常打「Recv v2 xxx result」）。
- **状态**：已修复（2026-08-18）。在 `ProxyWndProc` 里补上了这六个 `case`，转发写法与相邻的既有分支一致：`m_hCallBackWnd` 非空则 `PostMessage` 转发，为空则就地 `delete` 堆负载。三条群写操作应答的负载同为 `std::string*`，合并成一组 `case`。同时给 `FlamingoClient.cpp` 补上了 `net/AccountProtocolClient.h` 的包含（`net::ChangePasswordResponse` 此前在该文件里没有声明）。客户端 `Debug|x64` 编译通过；**实机回归尚未做**，需要一台可登录的服务端来构造失败应答。

## BUG-18　CloseDialog 的诊断日志格式串比实参多一个占位符

- **发现日期**：2026-08-18（准备把 CMainDlg 的通话与远程桌面子系统拆成协作类时发现）
- **位置**：`flamingoclient/Source/App/MainDlg.cpp`，`CMainDlg::CloseDialog` 里那条 `LOG_INFO("CloseDialog teardown alive: ...")`。
- **问题**（据实证）：格式串里有 13 个 `%d`（依次是 trayMsgTip、panel、remoteDesktop、chatWin、groupChatWin、netSet、reg、profile、contact、groupMgr、sysSet、imgViewer、history），实参只给了 12 个 —— `remoteDesktop` 这个名字没有对应的 `m_rdDlg.IsWindow()`。于是从第三个占位符起，每个名字打印的都是下一个窗口的值，最后一个 `history=%d` 读的是可变参数列表之外的内容。
- **影响**：这条日志本身是为「退出时崩在 ATL 断言上，看日志判断哪个窗口没销毁」准备的，现在它给出的对应关系整体错位一位，照它排查会指向错误的窗口。读越界属于未定义行为，x64 下通常只是读到寄存器或栈上的残留值，暂未观察到崩溃。
- **初步排查**：两种改法都可以 —— 去掉 `remoteDesktop=%d` 这个占位符，或者补上远程桌面窗口的实参。考虑到 BUG-19，应当选后者，把 `m_rdDlg` 一并纳入收尾与诊断。
- **状态**：已修复（2026-08-18）。远程桌面子系统抽成 `balloonmain::CRdCoordinator` 之后，真实会话用的那几个窗口已不再是 `CMainDlg` 的成员，留在 `CMainDlg` 的只有 `--rddlg` / `--rdbar` 两个调试入口的预览对象。诊断日志相应改成 `rdDlgPreview=%d rdBarPreview=%d`，实参补齐，占位符与实参数量一致。客户端 `Debug|x64` 编译通过。

## BUG-19　退出与登出都不收尾通话窗与远程桌面，窗口不销毁、屏幕仍在推流

- **发现日期**：2026-08-18
- **位置**：`flamingoclient/Source/App/MainDlg.cpp` 的 `CMainDlg::CloseDialog`（退出路径）与 `CMainDlg::ReturnToLoginWindow`（登出 / 被踢下线路径）。
- **问题**（据实证）：
  - `CloseDialog` 的窗口收尾清单里列了 13 个窗口对象，**没有** `m_callDlg`（通话会话窗）、`m_rdDlg`（远程桌面会话窗）、`m_rdViewer`（控制端观看窗）、`m_rdControlledBar`（被控端提示条）。这四个都是 `DuiFrameWindow` 派生，也就是 `CWindowImpl` 派生，同一函数上方的注释已经写明：不在此销毁的话，对象析构时仍持有有效 HWND，会触发 `atlwin.h:3238` 的断言。
  - `ReturnToLoginWindow` 做了断开连接、复位文件夹传输、复位并隐藏主面板、摘托盘图标，**唯独没有**停 `m_rdStreamer`（被控端屏幕采集与推流）、关 `m_rdViewer`、隐藏 `m_rdControlledBar`，也没有停 `m_callMedia`（通话中转会话，持有摄像头与麦克风）。
- **影响**：正在通话或正在被远程控制时退出程序，Debug 构建会撞上 ATL 断言；正在被控时登出，屏幕仍在往中转服务器推送、屏幕顶部的"正在被控制"提示条仍挂着，而用户看到的是登录窗，会认为已经断开了。正在通话时登出，采集线程与摄像头也不会释放。
- **初步排查**：这两条路径需要的是同一件事——让通话与远程桌面各自暴露一个收尾入口。拆协作类时正好一并解决：`CRdCoordinator` / `CCallCoordinator` 各提供 `ResetForLogout()` 与 `Shutdown()`，由这两处分别调用。
- **状态**：已修复（2026-08-18）。远程桌面的四个会话对象已经搬进 `balloonmain::CRdCoordinator`（`flamingoclient/Source/UI/BalloonMain/MainPanelRdCoordinator.h/.cpp`），由主面板按值持有。该类提供 `ResetForLogout()`，一次性完成停推流、关观看窗、隐藏受控提示条、关会话对话框、清会话 id；`CBalloonMainPanel::ResetForLogout` 在开头调用它，登出与被踢下线两条路径都会经过。退出路径由该类的析构函数兜底 —— 析构时同样调 `ResetForLogout()`，三个窗口都会被销毁，不再有 ATL 断言的风险。通话那半同理：会话对话框与中转会话已经搬进 `balloonmain::CCallCoordinator`（`flamingoclient/Source/UI/BalloonMain/MainPanelCallCoordinator.h/.cpp`），它的 `ResetForLogout()` 依次停铃声、停中转会话（释放摄像头与麦克风）、关通话窗，同样由 `CBalloonMainPanel::ResetForLogout` 调用，析构函数也调它。客户端 `Debug|x64` 编译通过；**实机回归尚未做**，需要两台能互相呼叫的客户端才能构造出「通话中登出」与「被控中登出」这两个场景。

## BUG-20　通话窗的 owner 一律传 NULL，导致"关窗即停媒体会话"这条兜底路径从未生效

- **发现日期**：2026-08-18
- **位置**：`flamingoclient/Source/App/MainDlg.cpp` 里四处 `m_callDlg.OpenCalling/OpenIncoming/OpenConnected`，与 `flamingoclient/Source/Feature/Call/BalloonCallDlg.cpp` 的 `OnCloseMsg`。
- **问题**（据实证）：`CBalloonCallDlg::EnsureWindow(HWND owner)` 把传入的 owner 记在 `m_hOwnerWnd` 里，`OnCloseMsg` 据它 `PostMessage(m_hOwnerWnd, FMG_MSG_CALL_STOP_MEDIA, 0, 0)`，让协调器停掉媒体会话。但 `CMainDlg` 的四个调用点传的都是 `NULL`，于是 `m_hOwnerWnd` 恒为 NULL，那条 `if (m_hOwnerWnd != NULL && ::IsWindow(m_hOwnerWnd))` 永不成立，消息从未被投递过，`CMainDlg::OnCallStopMedia` 是死代码。
- **影响**：用户点通话窗标题栏的关闭按钮时，信令（取消 / 拒绝 / 挂断）照常发出，但本端的 `CallMediaSession` 不会被停。正常情况下服务端随后会推一条 ended 状态回来，`OnV2CallStateNotify` 在那里也会停一次，所以平时看不出问题；一旦这条推送没有到达（断网、服务端异常），采集线程会继续跑，摄像头指示灯一直亮着。这正是 `BalloonCallDlg.cpp` 里那段注释所要防的情况。
- **初步排查**：把四个调用点的 owner 改成协调器自己的窗口句柄即可。远程桌面窗（`m_rdDlg.OpenCalling` / `OpenAuth`）同样传的是 NULL，需要一并确认它有没有依赖 owner 的类似路径。
- **状态**：已修复（2026-08-18）。通话子系统抽成 `balloonmain::CCallCoordinator` 时，四个调用点的 owner 都改成了协调器绑定的宿主窗口句柄（主面板），`m_hOwnerWnd` 因此是有效值，关窗时那条 `FMG_MSG_CALL_STOP_MEDIA` 能正常投出，协调器的 `OnCallStopMedia` 也就不再是死代码。**这项改动不影响窗口层级**：`CBalloonCallDlg::EnsureWindow` 拿到 owner 之后只把它记进 `m_hOwnerWnd`，建窗仍是 `Create(NULL, ...)`，通话窗依旧是无属主的独立顶层窗口。顺带核对了远程桌面窗：`CBalloonRemoteDesktopDlg::EnsureWindow` 完全不使用传入的 owner，没有类似路径。客户端 `Debug|x64` 编译通过；实机回归尚未做。

## BUG-21　m_bAlreadySendReloginRequest 从未被置真，重连定时器每次都会重发登录请求

- **发现日期**：2026-08-18
- **位置**：`flamingoclient/Source/App/MainDlg.cpp`，成员声明在 `MainDlg.h`，读取点在 `CMainDlg::OnTimer` 的重连定时器分支。
- **问题**（据实证）：这个成员在整个仓库里只有四处出现——构造函数、`OnLoginResult` 的两个分支各赋一次 `false`，以及 `OnTimer` 里的 `if (!m_bAlreadySendReloginRequest)`。**没有任何一处把它赋为 `true`**，因此该判断恒成立。
- **影响**：按成员名与注释（"重连过程中是否已经发送过登录请求包"）看，原本的意图是"一个重连周期内只发一次登录请求，等应答回来再决定下一步"。实际行为是重连定时器每触发一次就调一次 `m_FMGClient.Login()`，并且每次都弹一遍"您已经掉线，正在帮您重连……"的托盘气泡。重连间隔来自 `flamingo.ini`，间隔较短时会向服务端重复发送登录包。
- **初步排查**：要么在发出 `Login()` 之后置真、并在收到登录应答（成功或失败）时复位，与现有的两处 `false` 赋值配对；要么确认这个节流已无必要，把该成员连同判断一并删除。两种改法都会改变运行期行为，须单独评估。
- **状态**：未修复。
