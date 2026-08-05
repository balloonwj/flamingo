# SQLite 本地数据库模式方案与开发计划

> 作者：balloonwj@qq.com　起草日期：2026-07-31
> 用途：记录把 `flamingoserver` 的 `chatserver` 从「只支持远程 MySQL」扩展为「MySQL 与本地 SQLite 双模式，运行时由配置项切换」的整体方案、关键取舍、分期路线与开发步骤。后续每步开工前据此细化、收尾后回填实际结果。

---

## 1. 背景与目标

`chatserver` 目前只能连远程 MySQL/MariaDB，部署时必须先准备一台数据库服务器。对于单机小规模部署、演示环境、开发调试这些场景，这个前置条件偏重。

本方案的目标是**增加一种本地 SQLite 文件数据库模式，同时完整保留原有的 MySQL 模式**。两种模式共用同一套业务代码，由 `etc/chatserver.conf` 里的配置项在运行时选择，现有 MySQL 部署不需要改动任何配置文件。

---

## 2. 现状盘点

本节结论均为**据实证**，逐条附了代码位置。

### 2.1 业务层直接依赖 MySQL 具体类，没有抽象层

`chatserversrc/` 下有 12 个 Manager 直接 `#include "../mysqlapi/DatabaseMysql.h"`：`AdminManager`、`AnnouncementManager`、`CardManager`、`DepartmentManager`、`EmployeeManager`、`EnterpriseManager`、`FileStoreManager`、`GroupFileManager`、`GroupManager`、`MessageManager`、`ServiceAccountManager`、`UserManager`。其中约 208 行是 `query` / `pquery` / `execute` / `pexecute` 调用。

### 2.2 三条降低改造成本的有利条件

1. **`Field` 类可原样复用。** `mysqlapi/Field.h` 内部只有一个 `std::string m_strValue` 和一个 `DataTypes` 枚举，跟 MySQL API 完全无关。只有 `QueryResult` 持有 `MYSQL_RES*` 并 `#include <mysql/mysql.h>`（`mysqlapi/QueryResult.h:4,63`）。
2. **业务层完全没有使用事务。** 在 `chatserversrc/` 下检索 `START TRANSACTION` / `COMMIT` / `ROLLBACK` 零命中。每个 Manager 都是 `createDbConnection()` 开一条连接、跑完即由 `unique_ptr` 析构（典型实现见 `chatserversrc/DepartmentManager.cpp:96`）。没有跨语句的会话状态需要迁移。
3. **`mysqlmgr/` 的异步任务框架是死代码。** `MysqlThrd` / `MysqlThrdMgr` / `TaskList` 在 `chatserversrc` 中一处都没被引用；`CMysqlManager` 也只在 `chatserversrc/main.cpp:144` 用于启动时建库建表。SQLite 版本不需要移植这套线程池，只需要对应一个「建库建表」的引导逻辑。

### 2.3 数据库调用发生在网络 IO 线程上，且本来就是同步阻塞的

`chatserversrc/ChatSession.cpp:927` 在消息处理函数里直接同步调用 `GroupManager::getUserGroups()`；`chatserversrc/main.cpp:255` 的定时器会在主循环线程上驱动公告的定时发布，同样会落库。而 MySQL 的 `mysql_query`（`mysqlapi/DatabaseMysql.cpp:191`）本身就是同步阻塞 API。

这条结论很重要：换成 SQLite 之后，线程模型并没有变差，只是从「阻塞在网络往返」变成「阻塞在本地磁盘写入」。

### 2.4 MySQL 方言使用密度

对 `chatserversrc/*.cpp` 按关键字统计的结果：

| 方言特征 | 出现次数 | SQLite 能否直接跑 |
| --- | --- | --- |
| `NOW()` | 25 | 不能，可注册同名函数 |
| `AUTO_INCREMENT` | 25（均在 DDL 中） | 不能，DDL 单独出一份 |
| `INSERT IGNORE` | 13 | 不能，可机械改写为 `INSERT OR IGNORE` |
| `INTERVAL`（配合 `DATE_ADD` / `DATE_SUB` / `CURDATE`） | 12 | **不能，且无法用函数补，必须改写 SQL** |
| `LIKE` | 12 | 能 |
| `IFNULL` | 9 | 能，SQLite 原生支持 |
| `CURDATE()` | 7 | 不能，可注册同名函数 |
| `FROM_UNIXTIME()` | 7 | 不能，可注册同名函数 |
| `LAST_INSERT_ID()` | 6 | 不能，可注册同名函数 |
| `ON DUPLICATE KEY UPDATE` | 4 | **不能，必须逐处改写为 UPSERT** |
| `GREATEST` | 2 | 不能，可注册同名函数 |
| `CONCAT` / `SUBSTRING` | 各 1 | 不能，可注册同名函数 |
| `LIMIT` / `OFFSET` / `GROUP BY` / `UNION` / `LEFT JOIN` | 59 / 31 / 5 / 2 / 3 | 全部能 |

此外还有 16 处 `escapeString()` 调用（依赖 `mysql_real_escape_string`），以及 `table.sql` 里的 28 张表。

---

## 3. 已确认的决策

以下四项经与用户逐条确认后定稿，实施过程中不再反复。

| 决策项 | 结论 | 理由 |
| --- | --- | --- |
| **并发模型** | 每次业务调用新建一条 SQLite 连接，配 WAL 日志模式加 `busy_timeout=5000` | 保持现有 `unique_ptr<IDatabase>` 用完即弃的形态，改动最小；`LAST_INSERT_ID` 的 6 处调用天然正确（见 5.4）。压测若发现 `SQLITE_BUSY` 不可接受，再讨论换成专用写线程 |
| **编译形态** | 一个二进制两种模式，运行时由 `etc/chatserver.conf` 的 `dbtype` 决定；CMake 保留 `WITH_MYSQL` / `WITH_SQLITE` 开关且默认均为 `ON` | 现有部署零配置改动；将来要出不依赖 `libmysqlclient` 的纯本地版本时不必回头改构建脚本 |
| **改造范围** | 只改 `chatserver` | `fileserver` / `imgserver` 不访问数据库：`CMakeLists.txt:122-137` 的源文件列表里没有 `mysqlapi`，`TARGET_LINK_LIBRARIES` 也没链接 `mysqlclient` |
| **数据迁移工具** | 本期不做 | 本地模式主要面向全新部署，起步就是空库。先把两种模式跑通，迁移工具确有需求时再单独立项 |

### 3.1 关于并发模型的一个澄清

讨论中曾考虑过「写操作串行化到专用线程」的方案，并提出「数据库报错业务层如何知晓」的疑问。这里记录结论，避免将来重复讨论：

**串行化不等于异步回调，这两件事可以分开。** 如果采用「同步投递加阻塞等待」，业务层的 `execute()` / `query()` 签名、返回值、错误码一个都不变——内部把 SQL 打包成任务推进写线程队列，用条件变量阻塞等待写线程回填结果后原样返回。业务层 `if (!pConn->execute(sql))` 该怎么判错还是怎么判错。所以错误传递本身不构成障碍。

真正让我们放弃这个方案的是另一个坑，见 5.4。

---

## 4. 总体设计

核心思路是**在业务层和数据库之间插一层薄接口，让 MySQL 和 SQLite 成为这层接口的两个实现**。业务层的 SQL 文本尽量保持一份，方言差异优先在驱动层消化，实在消化不掉的少数几处才让业务代码按方言分支。

曾考虑过的另一条路是不做抽象、另写一套 `SqliteXxxManager` 与现有 Manager 并行。不采纳的理由是：12 个 Manager 合计 7300 多行业务逻辑，复制一份意味着此后每个功能都要改两处，很快就会失配。

### 4.1 目录结构

```
flamingoserver/
├── dbapi/                    ← 新增：数据库抽象层
│   ├── IDatabase.h           ← 连接接口（query/execute/escapeString/getInsertID）
│   ├── QueryResult.h/.cpp    ← 从 mysqlapi 迁入的基类，去掉 MYSQL_RES 依赖
│   ├── Field.h/.cpp          ← 从 mysqlapi 原样迁入，代码不改
│   ├── DbConfig.h/.cpp       ← 全局数据库配置单例（dbtype/host/user/pwd/dbname/dbpath）
│   ├── DbFactory.h/.cpp      ← 按 dbtype 创建对应实现
│   └── SqlDialect.h/.cpp     ← 生成方言相关的 SQL 片段（日期运算、UPSERT 子句）
├── mysqlapi/                 ← 保留：DatabaseMysql 改为实现 IDatabase
│   ├── DatabaseMysql.h/.cpp
│   └── MysqlQueryResult.h/.cpp  ← 新增：QueryResult 的 MySQL 子类
├── sqliteapi/                ← 新增：SQLite 实现
│   ├── DatabaseSqlite.h/.cpp
│   ├── SqliteQueryResult.h/.cpp
│   ├── SqliteFunctions.h/.cpp      ← 注册 NOW/CURDATE/… 等 MySQL 兼容函数
│   ├── SqliteSqlTranslator.h/.cpp  ← 纯逻辑：INSERT IGNORE 改写与 SQLite 规则的转义
│   └── SqliteManager.h/.cpp        ← 对应 CMysqlManager，负责建表与补列
├── sqlite3/                  ← 新增：SQLite amalgamation（sqlite3.c / sqlite3.h）
└── table_sqlite.sql          ← 新增：SQLite 方言的建表脚本
```

### 4.2 接口设计

`IDatabase` 把 `CDatabaseMysql` 现有的公开方法抽成纯虚接口，签名一个都不变，业务层的调用点因此一行都不用动：

```cpp
class IDatabase
{
public:
    virtual ~IDatabase() {}

    //执行一条产生结果集的 SQL；返回的 QueryResult 由调用方 delete，失败返回 NULL
    virtual QueryResult* query(const char* sql) = 0;
    virtual QueryResult* query(const std::string& sql) = 0;
    virtual QueryResult* pquery(const char* format, ...) = 0;

    //执行一条不产生结果集的 SQL
    virtual bool execute(const char* sql) = 0;
    virtual bool execute(const char* sql, uint32_t& uAffectedCount, int& nErrno) = 0;
    virtual bool pexecute(const char* format, ...) = 0;

    //取最近一次插入产生的自增 ID
    virtual uint32_t getInsertID() = 0;

    //把 szSrc 转义后写入 szDst，返回写入的字节数
    virtual int32_t escapeString(char* szDst, const char* szSrc, uint32_t uSize) = 0;
};
```

`QueryResult` 拆成「基类加两个子类」。基类只保留纯字符串的行缓存（也就是现在 `QueryResult.h:56-61` 那几个成员），`nextRow()` 与析构改为虚函数；`MysqlQueryResult` 继续持有 `MYSQL_RES*`，`SqliteQueryResult` 持有查询结果的行数组。**类名 `QueryResult` 保持不变**，所以业务层那 208 行 `QueryResult* pResult = pConn->query(...)` 一个字都不用改。

业务层唯一需要改的是类型名和构造点。改造前（`chatserversrc/DepartmentManager.cpp:96`）：

```cpp
CDatabaseMysql* DepartmentManager::createDbConnection()
{
    CDatabaseMysql* pConn = new CDatabaseMysql();
    if (!pConn->initialize(m_strDbServer, m_strDbUserName, m_strDbPassword, m_strDbName))
    ...
}
```

改造后：

```cpp
IDatabase* DepartmentManager::createDbConnection()
{
    //由工厂按全局配置的 dbtype 决定创建 MySQL 还是 SQLite 连接，连接失败返回 NULL
    IDatabase* pConn = DbFactory::createConnection();
    ...
}
```

其余所有 `CDatabaseMysql* pConn` 形参统一换成 `IDatabase* pConn`。这是纯机械替换，编译器会兜底。

各 Manager 的 `init(dbServer, dbUserName, dbPassword, dbName)` 签名**保持不变**（`main.cpp:149-249` 那 13 行不用动），因为它们保存的连接参数在 SQLite 模式下由工厂直接忽略。

### 4.3 方言差异的三层处理策略

**第一层，注册 MySQL 兼容函数，业务 SQL 零改动。**
`NOW()`、`CURDATE()`、`FROM_UNIXTIME()`、`LAST_INSERT_ID()`、`GREATEST()`、`CONCAT()`、`SUBSTRING()` 都是普通函数调用形式，`sqlite3_create_function` 可以完全接住，一次性解决 49 处。`IFNULL` 是 SQLite 原生就有的，不用管。

**第二层，在驱动层做确定性的字符串改写。**
只有一条规则：把语句开头的 `INSERT IGNORE INTO` 替换成 `INSERT OR IGNORE INTO`（13 处）。这是前缀级别的精确替换，不是通用 SQL 解析，风险可控。**这一层不再增加别的规则**——正则改写 SQL 一旦复杂化就很难保证正确。

**第三层，无法自动处理的，让业务代码按方言生成 SQL 片段，通过 `SqlDialect` 统一出口。**
这类只有两种，合计约 16 处：

- **日期区间运算**（12 处），例如 `AdminManager.cpp:179` 的 `DATE_ADD(NOW(), INTERVAL %d HOUR)` 和 `AdminManager.cpp:462` 的 `CURDATE() - INTERVAL 13 DAY`。`INTERVAL` 是 MySQL 的语法结构而非函数，SQLite 的解析器认不了，只能改写。做法是提供 `SqlDialect::nowPlusHours(int)` / `SqlDialect::todayMinusDays(int)` 这类函数，MySQL 下返回 `DATE_ADD(NOW(), INTERVAL 24 HOUR)`，SQLite 下返回 `datetime('now','+24 hours')`。
- **UPSERT**（4 处），例如 `MessageManager.cpp:435` 的 `ON DUPLICATE KEY UPDATE f_readmsgid = GREATEST(...)`。MySQL 的 `ON DUPLICATE KEY UPDATE` 不需要写明冲突列，SQLite 的 `ON CONFLICT(列) DO UPDATE SET` 必须写明，没法机械转换。这 4 处由 `SqlDialect::upsertClause(冲突列, 更新表达式)` 生成。

### 4.4 配置开关

`etc/chatserver.conf` 新增两项，默认值保证现有 MySQL 部署完全不用改配置：

```ini
#数据库类型：mysql（默认，远程 MySQL/MariaDB）或 sqlite（本地单机文件库）
dbtype=mysql
#dbtype=sqlite 时使用的数据库文件路径，相对路径以进程工作目录为基准
dbpath=data/flamingo.db
```

`main.cpp` 读到配置后灌进 `DbConfig` 单例，再按 `dbtype` 决定调用 `CMysqlManager::init()` 还是 `CSqliteManager::init()` 做建表引导。

---

## 5. SQLite 侧的实现要点

### 5.1 连接参数

以 `SQLITE_OPEN_FULLMUTEX`（serialized 模式）打开，连接建立后立即执行：

```sql
PRAGMA journal_mode = WAL;      -- 允许「多读一写」并发，避免读写互斥
PRAGMA busy_timeout = 5000;     -- 写冲突时自动重试 5 秒而非立刻报错
PRAGMA synchronous  = NORMAL;   -- WAL 模式下的推荐档位，兼顾安全与吞吐
PRAGMA foreign_keys = ON;       -- SQLite 默认关闭外键约束，需显式打开
```

**关于并发能力的说明属于据推测**：WAL 加 busy_timeout 能否扛住本项目实际的并发写入量，取决于写入频率和单次事务时长，目前没有实测数据。验证动作是改造完成后用 `tools/gen_testdata.py` 造并发消息写入压测，观察日志里 `SQLITE_BUSY` 的出现率。若确实吃紧，退路是改成「写操作串行化到专用写线程」，`mysqlmgr/` 里那套现成的 `MysqlThrd` 加 `TaskList` 任务队列正好可以改造复用。

### 5.2 建表引导

不复用 `CMysqlManager` 里 `m_vecTableInfo` 那套表定义。原因是它的字段类型存的是 MySQL DDL 片段（`mysqlmgr/MysqlManager.cpp:25-50` 那样的 `varchar(32) DEFAULT NULL COMMENT '…'`），而且 `m_strKeyString` 用的是 MySQL 内联索引语法（`KEY idx_parent (f_parent_dept_id)`），SQLite 要求索引必须用独立的 `CREATE INDEX` 语句。硬做翻译既复杂又容易出错。

采用的做法是单独维护一份 `table_sqlite.sql`，28 张表加对应的 `CREATE INDEX IF NOT EXISTS`，由脚本从 `table.sql` 半自动生成后人工校对。`CSqliteManager::init()` 启动时执行这份 DDL，然后对每张表用 `PRAGMA table_info(表名)` 拿到实际列，缺列就 `ALTER TABLE … ADD COLUMN` 补上——这与 `CMysqlManager::checkTable()`（`mysqlmgr/MysqlManager.cpp:705` 起）用 `desc 表名` 做的事情一一对应。

### 5.3 转义

SQLite 侧用 `sqlite3_mprintf("%q", src)`，或者手写「单引号翻倍」。

这里有个**必须注意的语义差异**：MySQL 的转义会处理反斜杠（MySQL 字符串里 `\` 是转义引导符），而 SQLite 的字符串字面量里反斜杠只是普通字符。同一份含反斜杠的数据两边转义规则不同，**不能共用一个实现，必须各写各的**。

`f_teaminfo`、`f_content` 是 blob 列（`mysqlmgr/MysqlManager.cpp:37,103`），实际存的是 JSON 文本。如果内容里出现 `\0`，两边都会在拼 SQL 时被截断——这是现有 MySQL 实现已有的隐患，不是 SQLite 引入的，本方案不顺手改，仅在此记录。

### 5.4 自增 ID 的连接私有语义（重要）

业务层有 6 处是「先 `execute` 插入，紧接着 `query("SELECT LAST_INSERT_ID()")` 取自增 ID」：`MessageManager.cpp:69`、`GroupManager.cpp:643`、`GroupManager.cpp:969`、`UserManager.cpp:963`、`FileStoreManager.cpp:147`、`GroupFileManager.cpp:60`。

自增 ID 在 MySQL 和 SQLite 里**都是连接私有的状态**。目前每个 Manager 在同一条连接上先插后读，所以取得到。一旦把写操作挪到专用线程、而 `SELECT LAST_INSERT_ID()` 走调用方自己的读连接，**这个 ID 就读不到了，会静默拿到 0**，不报错、只写出错误数据。

这正是 3 中放弃「专用写线程」方案的决定性理由：在「每次调用新建连接」的模型下，先插后读始终在同一条连接上，这个坑天然不存在。

### 5.5 行为差异清单

以下各条**属于据推测**，需要在第二、三步实施时逐条写测试验证：

1. **字符串比较的大小写敏感性。** MySQL 默认排序规则 `utf8_general_ci` 不区分大小写，`table.sql:32` 的 `UNIQUE KEY uk_f_username (f_username)` 因此是「不区分大小写唯一」；SQLite 的 `=` 和唯一索引默认区分大小写。这会直接影响登录和注册的重名判断。对策是在 SQLite DDL 里给用户名、邮箱这类列显式加 `COLLATE NOCASE`。
2. **`ON UPDATE CURRENT_TIMESTAMP` 不存在。** 多张表的 `f_update_time` 用了这个特性（`mysqlmgr/MysqlManager.cpp:39` 等），SQLite 没有自动更新列，需要建 `AFTER UPDATE` 触发器，或让业务层显式写入时间。倾向用触发器，业务层改动更小。
3. **类型长度不做约束。** SQLite 是弱类型，`varchar(64)` 不会限制长度，超长字符串会被静默写入而不是像 MySQL 那样报错或截断。
4. **自增 ID 的复用行为。** SQLite 的 `INTEGER PRIMARY KEY` 在删除行后会复用 ID，加 `AUTOINCREMENT` 关键字才能保证单调递增。`t_chatmsg.f_msgid` 明确要求「全局自增、单调递增」（`mysqlmgr/MysqlManager.cpp:100`），这张表必须加 `AUTOINCREMENT`。
5. **中文排序。** `ORDER BY` 中文字段在两边的结果顺序不一致（SQLite 按字节序）。影响的是通讯录之类的展示顺序，不影响正确性。

---

## 6. 构建改动

### 6.1 CMake（Linux）

把 `sqlite3/sqlite3.c`（amalgamation 单文件）和 `dbapi/`、`sqliteapi/` 的源文件加进 `chatserver` 目标，并引入一个开关：

```cmake
option(WITH_MYSQL "编译 MySQL 数据库后端" ON)
```

默认编进去，运行时按 `dbtype` 选择，一个二进制两种模式都能跑。强制查找 `libmysqlclient.so` 的 `FATAL_ERROR` 移进 `if(WITH_MYSQL)` 里面，这样 `-DWITH_MYSQL=OFF` 编译时不会因为机器上没装 `mysql-devel` 而直接失败。

**没有对应的 `WITH_SQLITE` 开关。** 起初方案里写了两个，实施时收窄为一个：SQLite 是随仓库自带的单文件 amalgamation、不依赖任何外部库，关掉它省不下依赖、只省一点二进制体积，却要在 `DbFactory` / `SqlDialect` / `main` 等处到处加条件编译，不划算。`WITH_MYSQL` 则有真实用途 —— 编出一份不依赖 `libmysqlclient` 的纯本地版本。

### 6.2 Visual Studio（Windows）

修改 `windowsproj/chatserver.vcxproj` 与对应的 `.filters`，加入新增源文件与 `sqlite3.c`。

### 6.3 SQLite 版本与来源

**UPSERT 语法需要 SQLite 3.24 以上**，客户端 vendored 的那份（`flamingoclient/Source/SQLite 3.7.17/sqlite3.h`）是 3.7.17，太老，不能复用。

好在仓库里已经有一份合适的：`third-party/sqlite-src-3530100/`，版本 **3.53.1**，顶层就有现成的 amalgamation `sqlite3.c` 与 `sqlite3.h`。服务端直接用这一份，不需要另外下载或引入。

---

## 7. 分阶段实施计划

拆成五步，每一步结束时代码都是可编译、可运行的，MySQL 模式全程不能坏。

| 步骤 | 内容 | 验收标准 |
| --- | --- | --- |
| **第一步** | 抽象接口，MySQL 走通（纯重构）。建 `dbapi/`，拆 `QueryResult` 基类，`CDatabaseMysql` 改为实现 `IDatabase`，12 个 Manager 类型名替换，加 `DbConfig` 与 `DbFactory`（此时工厂只返回 MySQL 实现） | 编译通过，现有 `flamingo_tests` 全部通过，MySQL 模式功能零回归 |
| **第二步** | SQLite 驱动实现。引入 amalgamation，实现 `CDatabaseSqlite` 与 `SqliteQueryResult`，注册 7 个兼容函数，做 `INSERT IGNORE` 前缀改写与 `escapeString` | 新增单测与集成测试通过 |
| **第三步** | SQLite 建表引导。产出 `table_sqlite.sql`（含 `COLLATE NOCASE`、`AUTOINCREMENT`、更新时间触发器）与 `CSqliteManager` | 空目录启动能自动建出 28 张表；缺列场景能自动补列 |
| **第四步** | 清理剩下的 16 处方言 SQL。实现 `SqlDialect`，改写 12 处日期区间运算与 4 处 UPSERT | 两种模式下相同业务路径结果一致 |
| **第五步** | 配置开关接线、构建脚本、文档收尾 | `dbtype=sqlite` 能完整跑通登录、发消息、建群等主流程 |

---

## 8. 测试

仓库里已有 doctest 测试框架（`CMakeLists.txt:150-197` 的 `flamingo_tests` 目标），直接扩展即可。规划两类用例：

- **纯逻辑单测**，覆盖 `SqlDialect` 的方言片段生成、`INSERT IGNORE` 改写、`escapeString` 的转义规则。不依赖任何数据库进程。
- **SQLite 集成测试**，用临时 `.db` 文件跑真实的建表、增删改查、UPSERT、日期区间查询，验证与 MySQL 语义一致。SQLite 是嵌入式的，不需要外部服务，适合放进 CI。

按项目的测试规范，**测试用例的覆盖范围需要在第二步开工前先与用户确认**，本文档不预先锁定清单。

---

## 9. 实施记录

> 每步收尾后在此回填实际结果、与计划的偏差、以及新发现的问题。

- **2026-07-31**　方案定稿，四项关键决策确认完毕（见第 3 节）。第一步开工。

- **2026-07-31　第一步完成。** 实际改动如下：

  新增 `dbapi/`：`IDatabase.h`（连接接口）、`QueryResult.h/.cpp`（从 `mysqlapi` 拆出的、不依赖 `MYSQL_RES` 的基类）、`Field.h/.cpp`（从 `mysqlapi` 原样迁入）、`DbConfig.h/.cpp`、`DbFactory.h/.cpp`。
  新增 `mysqlapi/MysqlQueryResult.h/.cpp`，`CDatabaseMysql` 改为实现 `IDatabase`；删除 `mysqlapi/Field.*` 与 `mysqlapi/QueryResult.*`。

  业务层的改动量与预估一致，且全部是机械替换：78 处 `new CDatabaseMysql()` 换成 `DbFactory::createConnection()`（其中 72 处是 `unique_ptr::reset` 形态、6 处是 `createDbConnection()` 辅助函数里的裸指针形态），78 处 `if (!pConn->initialize(m_strDbServer, m_strDbUserName, m_strDbPassword, m_strDbName))` 换成 `if (!pConn)`，其余 `CDatabaseMysql` 类型名统一换成 `IDatabase`。12 个 Manager 的 `#include` 由 `../mysqlapi/DatabaseMysql.h` 换成 `../dbapi/IDatabase.h` 加 `../dbapi/DbFactory.h`。

  与原计划的两点偏差：

  1. **`DbConfig` 的接线提前到了第一步**（原计划放在第五步）。因为 `DbFactory` 的连接参数取自 `DbConfig`，不在 `main.cpp` 里先灌好，MySQL 模式当场就连不上库。`main.cpp` 现在会读 `dbtype` / `dbpath` 两个新配置项，老配置文件没有这两项时 `getConfigName` 返回 NULL，`CDbConfig::init` 据此回落到 mysql，行为与改造前完全一致。`etc/chatserver.conf` 暂未写入这两项，留到第五步随 SQLite 模式一起加。
  2. **`IDatabase` 最终没有 `initialize()` 方法**。原设想是「工厂造对象、调用方再调 `initialize`」，实际改成由 `DbFactory::createConnection()` 一次性完成创建与连接、失败返回 NULL。这样调用点不会拿到一个未连接的对象，也就没有空指针解引用的风险；`CDatabaseMysql::initialize(host, user, pwd, dbname)` 作为 MySQL 专有方法保留，因为 `CMysqlManager` 建库阶段需要用「不指定库名」的方式连接。

  验收结果：`FlamingoServer.sln` 以 Debug x86 完整重新生成，`chatserver` / `fileserver` / `imgserver` / `Tests` 四个工程全部编译通过，无新增警告（唯一的 C4244 来自 vendored 的 jsoncpp，改造前就存在）；`Tests.exe` 的 208 个用例、1321 条断言全部通过。

  遗留到第五步的收尾项：78 处连接失败日志（形如 `LOGE("XxxManager db connect failed, dbserver: %s, dbname: %s", ...)`）仍在打印 MySQL 的主机与库名，SQLite 模式下这两个值不参与连接、打出来会误导。届时统一改用 `CDbConfig::describe()`，并评估能否顺带删掉各 Manager 里已经不再用于建连接的 `m_strDbServer` / `m_strDbUserName` / `m_strDbPassword` 成员。

- **2026-07-31　第二步完成。** SQLite 驱动已经跑通。

  amalgamation 取自仓库里现成的 `third-party/sqlite-src-3530100/`（3.53.1），拷进 `flamingoserver/sqlite3/`。之所以拷贝而不是直接引用 `third-party/`：服务端现有的第三方库（`zlib1.2.11/`、`jsoncpp1.9.0/`）都是放在 `flamingoserver/` 下并纳入版本管理的，而 `third-party/` 整个目录都没有被 git 跟踪，直接引用会让服务端的构建依赖一个不在仓库里的目录。代价是仓库多出一个 9.5 MB 的 `sqlite3.c`，是否入库由提交时决定。

  新增 `sqliteapi/`：`SqliteSqlTranslator`（纯逻辑，负责 INSERT IGNORE 改写与 SQLite 规则的转义）、`SqliteQueryResult`、`SqliteFunctions`（注册 7 个 MySQL 兼容函数）、`CDatabaseSqlite`。`DbFactory` 的 `DB_TYPE_SQLITE` 分支已接上真实实现。

  实现过程中确定的几个细节：

  1. **结果集一次性读进内存。** `CDatabaseSqlite::query` 会把 `sqlite3_stmt` 的所有行 step 完、finalize 掉句柄之后才构造 `SqliteQueryResult`，对齐 MySQL 的 `mysql_store_result` 语义。这样结果集的生命周期与连接彻底解耦，业务层「拿着 `QueryResult*` 慢慢遍历」的既有写法不会踩到句柄失效的坑。
  2. **`NOW()` / `CURDATE()` / `FROM_UNIXTIME()` 必须用本地时区。** SQLite 原生的 `datetime('now')` 是 UTC，直接照搬会与 MySQL 差出几个时区，所以这三个函数是用 `localtime_r` / `localtime_s` 加 `strftime` 实现的。
  3. **`GREATEST` 全整数时走 int64 比较。** 业务层用它比的是消息 ID 这类大整数，经 `double` 中转会丢精度。
  4. **`SUBSTRING` 按字符而非字节计数。** MySQL 的语义是按字符，中文内容按字节切会切出乱码，所以实现里做了 UTF-8 字符边界索引。
  5. **数据库文件的父目录会自动逐级创建。** SQLite 自己不建目录，`dbpath=data/flamingo.db` 而 `data` 目录不存在时会直接打开失败，对「解压即用」的本地部署很不友好。

  测试：新增 `tests/test_sqlite_translator.cpp`（Suite SQT，8 个用例，纯逻辑）与 `tests/test_sqlite_database.cpp`（Suite SQD，13 个用例，用临时 `.db` 文件跑真实 SQL），合计 21 个用例、168 条断言，全部通过。整个测试集从 208 个用例增加到 229 个，`FlamingoServer.sln` 以 Debug x86 完整重新生成无错误。

  **做了一次反向验证确认测试有效**：把 `EscapeSqlString` 临时改成 MySQL 那样也转义反斜杠，测试立刻报出 3 条失败断言 —— 既有纯逻辑用例（SQT11）也有端到端往返用例（SQD13）。验证完已还原实现并重新跑通全部用例。

- **2026-07-31　第三步完成。** 建库建表引导已跑通，`chatserver` 能以 `dbtype=sqlite` 实际启动。

  **结构的来源改成了 `table.sql`，而不是 `MysqlManager.cpp`。** 核对结果是两者的 28 张表、268 个列完全一致，只有索引定义有差别 —— `table.sql` 多出 `t_user` 的两个唯一键（`uk_f_username`、`uk_ent_employee_no`）。`migrations/` 下几个补列脚本的注释也说明了 `MysqlManager.cpp` 的建表定义历史上会漏东西。所以以 `table.sql` 为权威源头。

  新增 `tools/gen_table_sqlite.py`，从 `table.sql` 单向生成两个产物，保证同源不失配：
  - `table_sqlite.sql` —— 可读、可用 sqlite3 命令行直接执行的建表脚本，与 `table.sql` 并排放；
  - `sqliteapi/SqliteSchema.cpp` —— 把同样的语句嵌进程序的结构数据。之所以嵌进程序而不是运行期读 `.sql` 文件：本地模式主打解压即用，多一个必须随二进制部署的文件就多一处会忘记、且报错不直观的失败点；这也与 MySQL 侧把结构写在代码里的做法一致。

  翻译过程中处理掉的方言差异，除了方案第 5.5 节已列出的那几条，还有三条是动手后才发现的：

  1. **SQLite 的索引名是库内全局唯一的**，而本项目里 `idx_enterprise` / `idx_user` / `idx_group` 这类名字在多张表里重复出现，直接照搬会撞名。生成时统一加了表名前缀。
  2. **MySQL 允许自增列不是主键，SQLite 不允许。** 全仓库只有 `t_user` 是这种情况（自增列 `f_id`、主键 `f_user_id`），处理办法是把自增列升为 SQLite 主键、原主键降级成唯一索引，约束强度不变。其余 27 张表要么自增列本身就是主键，要么没有自增列。
  3. **建索引必须排在补列之后。** 这一条是被测试逼出来的：SQM3 一开始就失败，查下来是结构落后的存量库里，索引要用到的列还没补上，`CREATE INDEX` 直接报 `no such column`。所以生成的语句分成了 `SCHEMA_TABLE_STATEMENTS` 与 `SCHEMA_POST_STATEMENTS` 两组，执行顺序固定为「建表 → 补列 → 建索引与触发器」。

  另外，生成的 C++ 里所有非 ASCII 字节都用**八进制转义**。这不是洁癖：源文件里直接写中文时，MSVC 会把窄字符串字面量转成系统 ANSI 代码页（本机是 GBK），而 SQLite 要的是 UTF-8，`DEFAULT '我的好友'` 这类默认值会被写成乱码。八进制转义描述的是字节，不受执行字符集影响。

  `main.cpp` 已按 `dbtype` 分支选择 `CSqliteManager` 或 `CMysqlManager` 做引导 —— 两者流程完全不同，SQLite 模式下跑 MySQL 引导会直接连不上库。`etc/chatserver.conf` 也补上了 `dbtype` / `dbpath` 两项，默认值仍是 `mysql`。

  测试：新增 `tests/test_sqlite_schema.cpp`（Suite SQM，7 个用例），覆盖空库冷启动、重复执行幂等、缺列自动补齐且不丢数据、关键索引存在、`COLLATE NOCASE` 唯一约束、`AUTOINCREMENT` 单调递增、更新时间触发器。整个测试集 236 个用例、1585 条断言全部通过。

  **端到端验证（据实证）**：把配置临时改成 `dbtype=sqlite` 后启动 `chatserver`，日志显示 WAL 模式生效、28 条建表语句与 56 条索引触发器语句执行成功、12 个业务 Manager 全部初始化通过、进程进入事件循环，整个启动过程零 ERROR 零 WARN，生成的库文件 360 KB。验证完配置已还原为 `mysql`。

  这次验证还暴露并修掉了一个我自己引入的问题：连接建立时按 INFO 打了两行日志，而业务层是「每次数据库调用新开一条连接」的用法，光启动就刷了 9 次，跑起来日志会被淹没。改成只有进程内第一条连接打完整信息（含实际生效的日志模式），后续连接降到 DEBUG 级别，启动日志从 33 行降到 16 行。

- **2026-08-01　第四步完成。** 新增 `dbapi/SqlDialect`，把剩下的方言差异收敛到唯一出口。

  **先纠正本方案第 2.4 节的一处统计错误。** 那张表里「`INTERVAL` 12 处」是按关键字数出来的，其中 7 处其实是 C++ 常量名（`HEARTBEAT_CHECK_INTERVAL_US` 之类），真正的 SQL 只有 5 处；`ON DUPLICATE KEY UPDATE` 的 4 处里有 1 处是注释，实际 3 处。所以「16 处待改写」的说法偏大，实际是 **8 处**。

  但真正的问题在另一个方向：**第 2.4 节漏统计了手写的转义**。动手时才发现 `AdminManager` / `AnnouncementManager` / `DepartmentManager` / `EmployeeManager` / `EnterpriseManager` / `ServiceAccountManager` 这六个文件**各自都有一份字节级完全相同的 `static std::string EscapeSql`**，按 MySQL 规则给反斜杠加转义，合计 62 处调用，一处都没走 `IDatabase::escapeString`。原先只统计了 16 处 `escapeString` 调用，把这一大块整个漏掉了。

  这在 SQLite 下是实打实的错误：输入 `it's` 会被转成 `it\'s`，SQLite 的字符串字面量在第二个引号处就提前闭合，轻则语法错误、重则写进错误数据；输入 `C:\path` 会被存成 `C:\\path`，数据直接损坏。而部门名、企业名、公告标题、管理员邮箱这些内容全都取自客户端，含引号完全正常。

  处理办法是把这六份实现合并进 `dbapi::EscapeString`，按后端分派：MySQL 分支的逻辑与原先那六份逐字一致（保证 MySQL 部署零行为变化），SQLite 分支直接复用 `sqliteapi::EscapeSqlString`，避免同一条规则在两处各写一遍慢慢走样。六个静态函数已删除，62 处调用点改为调用共用实现。

  `SqlDialect` 提供四组函数，每组都有一个「显式传入后端类型」的重载供单元测试同时验证两种方言，业务代码用不带该参数的重载（取全局配置）：

  | 函数 | MySQL | SQLite |
  | --- | --- | --- |
  | `NowPlusHours(n)` | `DATE_ADD(NOW(), INTERVAL n HOUR)` | `datetime('now','localtime','+n hours')` |
  | `NowMinusDays(n)` | `DATE_SUB(NOW(), INTERVAL n DAY)` | `datetime('now','localtime','-n days')` |
  | `TodayMinusDays(n)` | `CURDATE() - INTERVAL n DAY` | `date('now','localtime','-n days')` |
  | `UpsertClause(cols)` | ` ON DUPLICATE KEY UPDATE ` | ` ON CONFLICT(cols) DO UPDATE SET ` |
  | `EscapeString(s)` | 反斜杠转义 | 单引号翻倍 |

  UPSERT 这块比预想的简单：**两种方言里赋值部分的文本完全一样**，只有子句关键字和冲突列不同。因为赋值右侧出现的裸列名在 MySQL 与 SQLite 里都指代「已存在的那一行」的值（要取新值 SQLite 才用 `excluded.列名`），所以 `f_readmsgid = GREATEST(f_readmsgid, N)` 这种写法两边语义一致，业务代码只需换掉一个子句。

  另外顺手清掉一处隐患：`AdminManager` 的趋势图，日期轴用局部常量 `kTrendDays = 14`、SQL 里写死字面量 `13`，两者必须保持「差一」的关系却毫无关联。现在统一成 `ADMIN_TREND_DAYS`，SQL 侧写作 `ADMIN_TREND_DAYS - 1`。

  测试：新增 `tests/test_sqldialect.cpp`（Suite SQL，8 个用例、58 条断言）。纯逻辑部分验证两种方言的片段文本，含负数与零的边界（SQLite 的修饰符必须自带正负号，写成 `'+-3 hours'` 会解析失败）；集成部分在真实临时库上跑 UPSERT 的插入 / 冲突更新 / `GREATEST` 不回退三条路径、日期区间过滤能筛出正确的行、转义后的内容原样往返，以及完整照搬 `AdminManager::getMessageTrend` 的 `DATE(FROM_UNIXTIME(...))` + `GROUP BY` 查询形态。整个测试集 244 个用例、1643 条断言全部通过。

  端到端复验：以 `dbtype=sqlite` 再次启动 `chatserver`，8 个 Manager 初始化成功、零 ERROR 零 WARN，与第三步一致。验证完配置已还原为 `mysql`。

- **2026-08-01　第五步完成，五步全部收尾。**

  **构建开关。** 按 6.1 节的说明只做了 `WITH_MYSQL` 一个，用「关掉时才定义 `FLAMINGO_NO_MYSQL` 宏」的写法而不是「打开时定义」—— 这样 Windows 的 vcxproj 不用改，默认路径上也不存在「忘了定义宏导致两个后端都没编进去」这种失败模式。`DbFactory` 与 `main.cpp` 各加了一处 `#ifdef` 守卫：构建里没有 MySQL 后端而配置又写着 `dbtype=mysql` 时，直接报错停下，而不是静默改用 SQLite（配置写的是 mysql 却连到了别的库，问题更难查）。

  这条分支默认永远不参与编译，属于未经验证的代码，所以专门用 `/FI` 强制包含一个定义了该宏的头文件重新编了一遍 `chatserver`，确认能编过。

  **日志的后端相关信息。** 78 处连接相关日志统一改用 `CDbConfig::describe()`，输出形如 `db: sqlite data/flamingo.db` 或 `db: mysql 127.0.0.1/flamingo`。这里顺带解决了一个**安全问题**：`UserManager` 原先有 11 处日志把数据库密码明文打了出来（`dbpassword: %s`），现在全部消除。

  **清掉死状态。** 连接参数改由 `DbConfig` 统一保管后，12 个业务 Manager 里的 `m_strDbServer` / `m_strDbUserName` / `m_strDbPassword` / `m_strDbName` 变成了只写不读的死字段（经检索确认零处读取）。已连同 `init()` 的四个形参一并删除，`init()` 现在无参；`main.cpp` 的 13 处调用点同步简化。头文件里描述这四个参数的注释也一并更新。

  唯一保留四参数签名的是 `CMysqlManager::init()` —— 它要先用「不指定库名」的方式连上服务器判断库是否存在、必要时 `CREATE DATABASE`，这套流程是 MySQL 专属的，与各业务 Manager 的连接方式不同。（脚本批量改写时误伤过这一处，已改回并加注释说明原因。）

  验收：`FlamingoServer.sln` 以 Debug x86 完整重新生成无错误，244 个测试用例、1643 条断言全部通过；以 `dbtype=sqlite` 启动 `chatserver`，零 ERROR 零 WARN，日志里已无任何 `dbname:` / `dbserver:` / `dbpassword:` 字样，改为正确的 `db: sqlite data/flamingo.db`。配置已还原为 `mysql`。

- **2026-08-03　补做第 10 节遗留的第 2 项：主流程端到端实测。**

  新增 `tools/e2e_sqlite_smoke.py`，直接按 v2 线协议（24 字节 FrameCodec 帧头 + JSON 包体）与服务端对话，不依赖 PC 客户端，跑完九步主流程：注册两个账号（短信验证码从服务端日志里取）→ 双方登录 → 加好友 / 通过好友申请 → 拉联系人 → 单聊发消息并校验对端收到推送 → 同步会话列表与历史消息 → 标记已读 → 建群并在群里发消息 → 最后直接开库核对数据确实落盘。

  为避免打扰用户本机已有的服务，测试用 20100 端口另起一份 `chatserver`，库文件也另放一份。（第一次跑时发现 `127.0.0.1:20000` 被 WSL 的 `wslrelay.exe` 占着，请求其实进了用户 WSL 里那份服务端，换端口后重跑，并确认没有数据写进用户的库。）

  **结果：九步全部通过。** 落盘核对的数据是 `t_user` 2 行、`t_user_relationship` 1 行、`t_chatmsg` 2 行、`t_group` 1 行、`t_group_member` 2 行、`t_friend_request` 1 行、`t_conversation` 已读位点 1 行。消息内容做了逐字节回读比对：写进去的 JSON 包体与从 `f_content` 读出来的完全一致；含英文单引号的内容原样落库、没有被转义符污染；含反斜杠的内容只保留 jsoncpp 自己那一层转义，没有出现 MySQL 规则残留导致的双重转义。这一条正是第四步合并 `EscapeSql` 时最担心的失败模式，现在有了实测数据。

  （测试脚本里一开始写了一条「按中文子串 `LIKE` 匹配 `f_content`」的断言，跑不过。查下来是 jsoncpp 序列化时把中文写成了 `\uXXXX` 转义，`f_content` 里根本不会出现中文字面量 —— 是断言本身写错了，不是落库有问题。已改成上面这套语义正确的比对。）

  **实测过程中撞见两个既有缺陷，均与本次 SQLite 改造无关**，详见第 10 节。

---

## 10. 收尾状态与后续建议

五步已全部完成。当前状态：

- **MySQL 模式**：行为与改造前一致。所有方言相关改动在 MySQL 侧都生成与原先逐字相同的 SQL（有单元测试逐条比对），唯一的实质变化是日志不再打印密码。
- **SQLite 模式**：可用。`chatserver` 能以 `dbtype=sqlite` 冷启动、自动建库建表、12 个业务 Manager 全部初始化通过；注册、登录、加好友、单聊收发、会话与历史消息同步、标记已读、建群、群聊这一整条主流程已按线协议实测走通，数据确实落盘（详见第 9 节 2026-08-03 的记录）。

尚未做、建议后续跟进的事项：

1. **并发写压测。** 方案 5.1 节记的那条「WAL + busy_timeout 能否扛住实际写入量」仍是**据推测**，没有实测数据。建议用 `tools/gen_testdata.py` 造并发消息写入，观察日志里 `SQLITE_BUSY` 的出现率。若不够用，退路是把写操作串行化到专用写线程，但要注意那样会打破 `LAST_INSERT_ID` 的连接私有语义（见 5.4 节），6 处调用点需要一并改造。
2. **MySQL 模式的回归实测。** 改造全程没有连过真实 MySQL（本机没有该环境），MySQL 侧的结论都建立在「编译通过 + 单元测试比对 SQL 文本一致」之上。上线前应在有 MySQL 的环境里跑一轮回归。
3. **数据迁移工具**（第 3 节已决定本期不做）。若要把现有 MySQL 部署改成本地部署，需要一个导出工具。
4. **`table.sql` 改动后务必重跑 `tools/gen_table_sqlite.py`**，否则两种后端的表结构会失配。建议在提交前检查里加上这一条。

### 10.1 端到端实测中撞见的两个既有缺陷（不属于本次改造范围）

两个都在 `ChatServer.cpp` / `ChatSession.cpp` 的会话簿记与联系人逻辑里，与数据库后端无关，两种后端下表现相同。

**缺陷一已于 2026-08-03 修复**（经用户确认后动手，见下文「修复」小节）；缺陷二仍未处理，记录在此备查。

**缺陷一：用户下线时若其好友在线，`chatserver` 自我死锁并 `abort`。**

据实证，cdb 抓到的调用栈是：

```
TcpConnection::handleRead → handleError → handleClose
  → ChatServer::onDisconnected (ChatServer.cpp:87 持有 m_sessionMutex)
    → ChatSession::sendUserStatusChangeMsg (ChatServer.cpp:118 调用)
      → ChatServer::getUserClientTypeByUserId (ChatSession.cpp:1229 调用)
        → std::lock_guard<std::mutex>(m_sessionMutex)  (ChatServer.cpp:403)
          → std::_Throw_Cpp_error(5) → terminate → abort
```

`ChatServer.cpp:87` 已经锁住了 `m_sessionMutex`，`ChatServer.cpp:118` 在这把锁里遍历在线好友并调 `sendUserStatusChangeMsg`；该函数内部（`ChatSession.cpp:1229`）又去调 `getUserClientTypeByUserId`，后者在 `ChatServer.cpp:403` 对同一把**非递归** mutex 再加一次锁。错误码 5 就是 `resource_deadlock_would_occur`。触发条件是「下线用户至少有一个好友在线」，与是否走数据库无关 —— 整条栈上没有任何数据库调用。

值得一提的是，同一个函数里已经写着这个隐患的注释（`ChatServer.cpp:79-81`）：`broadcastToEnterprise` 因为内部要再取会话表快照，作者特意把它挪到了锁外执行。第 118 行这处是同类问题的漏网之鱼。

**修复（2026-08-03）。** 照抄作者在同一个函数里已有的做法：持锁期间只把目标好友会话收进局部的 `friendSessions`（存 `shared_ptr`，保证解锁后会话仍存活），解锁之后再逐个推送。

推送内容不再走 `sendUserStatusChangeMsg`，而是直接用 `ProfileProtocol::buildPresenceChangedNotify(下线用户ID, 0, disconnectedClientType)` 拼一次、发多份。这样处理有两个原因：一是此刻该会话已从 `m_sessions` 摘除，回头去查 `getUserClientTypeByUserId` 也查不到；二是这份通知与紧随其后的企业广播（`ChatServer.cpp` 原有代码）内容完全一致，同企业好友先后收到的两份通知不会互相打架 —— 原注释所说的「幂等无害」这才真正成立。

`ChatSession::sendUserStatusChangeMsg` 改完后已无调用方，**未删除**，但在头文件里补了调用约束说明（禁止在持有 `m_sessionMutex` 期间调用，以及原因），避免以后有人重新踩进来。顺带修正了该方法一处过时的注释 —— 原注释写着「当前为空实现」，但实现里其实是会真发包的。

**回归测试。** 新增 `tools/e2e_offline_notify.py`，按线协议把触发条件原样跑一遍：两人互加好友 → 双方在线 → 一方 FIN 正常断开 → 一方 RST 异常断开（走 `handleError` 而非 `handleClose`，路径不同）→ 反方向再来一次，每一步都校验在线的一方确实收到 `cmd_presence_changed_notify`（`presence=0`、`userid` 正确），并补发一次真实请求确认服务端还能干活（只看端口是否监听不够，进程 abort 后端口往往还挂着）。

这个测试特意做了**非空验证**：先把修复代码临时改回原样重编一次，测试在第 3 步失败且服务端进程直接消失（日志停在 `TcpConnection::handleRead` → `handleError`，与当初 cdb 抓到的栈顶完全一致）；换回修复版重编后 6 步全过、进程存活。也就是说这条用例确实能抓到这个缺陷，不是空跑通过。

（过程中踩到一个 MSBuild 的坑值得记一笔：用 `Copy-Item` 还原源文件会把备份的旧时间戳一并带回来，比 `.obj` 还旧，MSBuild 据此判定无需重编，于是「修复版」跑的其实还是缺陷版的二进制。识别方法是看 MSBuild 输出里有没有列出该 `.cpp` 文件名；还原源文件后应显式刷新其修改时间再编译。）

**～~缺陷二：`cmd_get_contacts` 从不返回普通好友~～ —— 撤回，这不是缺陷。**

端到端实测时观察到：双方加好友成功、`t_user_relationship` 确实落了 1 行，但 `cmd_get_contacts` 返回的 `contacts` 是空数组。当时只看了实现（`data.contacts` 全文只有 `ChatSession.cpp:916` 一处填充，位于部门成员遍历循环内，而该循环的数据源 `orgDepts` 只在 `enterpriseId != 0` 时才填充；`onGetContacts` 自始至终不查 `t_user_relationship`），就据此判定为缺陷，**没有先核对协议规范，结论下早了**。

2026-08-03 经用户确认并复核 `docs/协议设计-v2.md` 后更正：**这是设计如此，实现与规范完全一致。**

- 规范第 1116 行定义 `contacts` 为「去重的联系人资料池；**部门成员与服务号**都在此，靠 `usertype` 区分」—— 本就不含好友。
- 规范第 491 行：「v2 通讯录以企业组织架构为中心，**取消 QQ 式『好友分组』**……联系人的部门归属由 §7.5 企业组织架构表达。」
- 规范第 1665 行的已决事项表同样记着这一条。

一句话概括用户的定调：**企业版没有好友关系，通讯录即组织架构。** 实测里 alice / bob 是临时注册的账号、不属于任何企业，所以 `contacts` 为空是正确结果。

这条记录保留在此，是为了留下「据推测的结论不该当成据实证来用」的教训：当时该做的是先查协议文档，而不是只读实现就断言行为不对。

**一处遗留的文档与代码不一致（未处理）：** `chatserversrc/Protocol.h:105` 给 `cmd_get_contacts` 的注释仍写作「拉取好友列表（含分组/标签/收藏）」，与规范矛盾；`cmd_move_friend_team`（1211）、`cmd_operate_team`（1212）两个枚举及配套的分组操作类型枚举（`Protocol.h:481`）也仍留在文件里，而规范明确写着这两条「已删除、编号留空」。客户端 `flamingoclient/Source/net/Protocol.h` 有同样的残留。上面那次误判正是被这条过时注释带偏的。是否清理由用户决定。
