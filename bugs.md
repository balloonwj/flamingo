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
  - 出错代码在 `flamingoclient/Source/BalloonChatWnd/BalloonChatWindow.cpp:3239` 与 `BalloonGroupChatWindow.cpp:1854`，两处都是：
    ```cpp
    balloonwjui::DuiEditHost* edit = dynamic_cast<balloonwjui::DuiEditHost*>(
        content->FindCtrlById(balloonchatwnd::kIdInputEdit));
    if (edit != NULL)
    {
        edit->SetReadOnly(!bOnline);   // 从未执行
    }
    ```
  - 但这两个 id 对应的控件是 `DuiRichEditHost`，不是 `DuiEditHost` —— 见 `ChatWindowLayout.h:37`（`kIdInputEdit = 401, // 输入框（DuiRichEditHost，富文本，支持内联图片）`）与 `GroupChatWindowLayout.h:34`（`kGIdInputEdit = 501`，同样注释）。
  - `DuiEditHost` 与 `DuiRichEditHost` 是**兄弟类**，两者都直接继承 `HwndHostControl`（`third-party/balloonui/balloonui/Controls/Input/DuiEditHost.h:76`、`DuiRichEditHost.h:76`），彼此之间没有继承关系。因此 `dynamic_cast` 恒返回 NULL，`if` 分支永不进入。
  - 同一文件里取同一个 id 的其它九处（如 `BalloonChatWindow.cpp:454` / `:715` / `:3058` / `:3303`，`BalloonGroupChatWindow.cpp:1781`）都正确地转换成了 `DuiRichEditHost*`，只有"离线置只读"这一处漏改，应是输入框换类型时遗漏。
  - 这类错误编译期无法发现：`dynamic_cast` 到无关类型是合法写法，失败只表现为运行期返回 NULL，而代码又恰好用 `if (edit != NULL)` 把失败情形当作正常分支静默跳过。
- **状态**：未修复。修法：把两处的目标类型改成 `balloonwjui::DuiRichEditHost*`。修完需实测确认离线时确实打不了字、且草稿仍可选中复制。

## BUG-6　聊天窗富文本拆分模块残留死代码，且单测在为已废弃实现提供覆盖

- **发现日期**：2026-08-14（为无窗口富文本控件梳理接口时顺带查出）
- **位置**：`flamingoclient/Source/BalloonChatWnd/`。
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
