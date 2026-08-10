# 已知问题 (bugs)

记录排查中发现、尚未修复的问题。每条注明：现象 / 复现 / 影响 / 初步排查 / 状态。

---

## BUG-1　chatserver 在并发 admin 请求下崩溃（access violation）

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

- **现象**：某个 manager（如 `UserManager`）init 失败时，`main.cpp:143` 打了 `LOGF` 但**没有 `return`**，继续往下执行，随后解引用未初始化对象 → access violation 崩溃。
- **复现**：库 schema 不匹配导致 `UserManager::loadUsersFromDb` 查询失败时必现（本次因 `t_user` 缺 `f_salt`/`f_usertype` 两列触发过，列已补，问题本身仍在）。
- **影响**：配置 / 库异常时不能给出清晰错误并退出，而是崩溃，掩盖真实原因。
- **状态**：未修复。修法：各 manager init 失败后应 `return 1;` 终止启动。

## BUG-3　CDatabaseMysql::initialize 连接失败时句柄泄漏 + 对 NULL 调 mysql_close

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

- **位置**：`flamingoserver/mysqlapi/DatabaseMysql.cpp` 及整个服务端。
- **问题**：MySQL C API 要求多线程程序在创建线程前单线程调用一次 `mysql_library_init()`，每个用到 libmysql 的线程调用 `mysql_thread_init()` / 退出前 `mysql_thread_end()`。当前代码只有 `mysql_init`，其余一概没有。
- **影响**：多个 I/O 线程并发建连接 / 查询时，libmysql 的隐式全局初始化存在竞态；线程局部数据未清理。可能是 BUG-1 的根因之一。
- **状态**：未修复。
