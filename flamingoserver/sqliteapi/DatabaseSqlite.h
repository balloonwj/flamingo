/**
 *  IDatabase 的 SQLite 实现，DatabaseSqlite.h
 *  balloonwj@qq.com   2026-07-31
 *
 *  封装一条本地 SQLite 连接：打开（必要时创建）数据库文件、配置并发相关的 PRAGMA、
 *  注册 MySQL 兼容函数，然后按 IDatabase 的约定执行 SQL 并把结果集包成 SqliteQueryResult。
 *
 *  与 MySQL 实现的几处行为差异，实现时要留意：
 *    1. 没有「断线重连」这回事。SQLite 是进程内库，连接就是一个打开的文件句柄，
 *       不存在 CR_SERVER_GONE_ERROR 那种服务端跑掉的情况，所以没有重试逻辑。
 *    2. 并发写会撞上 SQLITE_BUSY。本实现开 WAL 日志模式并设置 busy_timeout，
 *       由 SQLite 内部自动重试，超时才真正失败。
 *    3. 事务中途出错时，SQLite 有可能<u>自己</u>把整个事务回滚掉，而调用方从返回值上
 *       完全看不出来 —— 这与 MySQL「事务中途断线导致事务丢失」是同一类隐患：
 *       事务没了却没人知道，后续语句于是各自独立提交。故本实现在语句失败后会核对
 *       sqlite3_get_autocommit()，发现事务已被自动回滚就清掉标志并记一条错误日志。
 *    4. 开启事务发的是 BEGIN IMMEDIATE 而不是默认的 BEGIN（deferred），
 *       理由见 DatabaseSqlite.cpp 里 beginTransaction 的注释。
 *
 *  业务层不直接构造本类，而是通过 DbFactory::createConnection() 拿到 IDatabase*。
 */
#ifndef __DATABASE_SQLITE_H__
#define __DATABASE_SQLITE_H__

#include <stdint.h>
#include <string>

#include "../dbapi/IDatabase.h"
#include "../dbapi/DbHandlePool.h"

//SQLite 连接句柄的前向声明。这里刻意不包含 sqlite3.h：
//那是个 60 多万行的合并头文件，没必要让每个包含本头文件的编译单元都吃一遍
struct sqlite3;

/**
 *  SQLite 连接句柄池：DbHandlePool 配上 SQLite 的关闭函数（sqlite3_close），池中保存的是
 *  sqlite3* 句柄，按数据库文件路径区分。全进程一个，通过 Singleton<CSqliteHandlePool>::Instance() 访问。
 *
 *  SQLite 是进程内库，没有「服务端断开空闲连接」这回事，所以不做探活与保活，只按空闲时长收缩。
 *  复用一条连接省掉的是打开文件、设置 PRAGMA、注册兼容函数，以及首次查询时解析整个库结构的开销。
 *  只有经 DbFactory::createConnection() 创建的连接才会进池（见 CDatabaseSqlite::initialize 的
 *  usePool 参数）；池的打开、关闭与定时维护统一经 DbFactory 进行，见 DbFactory.h。
 */
class CSqliteHandlePool : public DbHandlePool
{
public:
    //创建一个空池，初始为关闭状态，容量与时间阈值取 DbHandlePool::Options 的默认值
    CSqliteHandlePool();
};

class CDatabaseSqlite : public IDatabase
{
public:
    CDatabaseSqlite(void);
    virtual ~CDatabaseSqlite(void);

    /**
     *  打开数据库文件并完成连接配置，重复调用会先关闭旧连接。
     *  @param dbPath 数据库文件路径，相对路径以进程工作目录为基准。
     *                文件不存在时会自动创建；所在目录不存在时会逐级创建。
     *  @param usePool 是否使用连接池。为 true 时先从 CSqliteHandlePool 里取一条连到同一个文件的
     *                 空闲连接（它的 PRAGMA 与兼容函数都已设置过），取不到再新开；本对象析构时，
     *                 若连接上没有未结束的事务、没有未释放的语句，就把连接放回池中而不是关闭。
     *                 只有 DbFactory 传 true。
     *  @return true 表示连接可用（文件已打开、PRAGMA 已生效、兼容函数已注册）。
     */
    bool initialize(const std::string& dbPath, bool usePool = false);

    virtual QueryResult* query(const char* sql);

    //基类里 query(const std::string&) 这个便利重载会被本类的 query 声明遮蔽，
    //用 using 把它重新引入本类作用域，理由同 CDatabaseMysql
    using IDatabase::query;

    virtual QueryResult* pquery(const char* format, ...);
    virtual bool execute(const char* sql);
    virtual bool execute(const char* sql, uint32_t& uAffectedCount, int& nErrno);
    virtual bool pexecute(const char* format, ...);

    //事务接口，语义见 dbapi/IDatabase.h 里逐个方法的说明。
    //SQLite 一侧有两点与 MySQL 不同，调用方按 MySQL 的行为写代码之前要看清：
    //  · beginTransaction 发的是 BEGIN IMMEDIATE（开启时就拿写锁）；
    //  · commit 失败后事务<u>可能仍然开着</u>（典型是撞上 SQLITE_BUSY），必须再回滚一次。
    virtual bool beginTransaction();
    virtual bool commit();
    virtual bool rollback();
    virtual bool inTransaction() const;

    virtual uint32_t getInsertID();
    virtual int32_t escapeString(char* szDst, const char* szSrc, uint32_t uSize);

private:
    //打印一条数据库错误日志：包含 SQLite 结果码、错误文本与出错的 SQL 原文。
    //operation：出错的操作名（如 "query"/"execute"），用于定位；
    //sql：出错的 SQL 语句原文，可为 NULL（连接类错误无对应 SQL）。
    void logDbError(const char* operation, const char* sql);

    //连接建立后统一配置 busy_timeout、WAL 等并发相关设置，并注册 MySQL 兼容函数。
    //返回 false 表示连接不可用，调用方须关闭连接。
    bool applyConnectionSettings();

    //执行一条不关心结果集的语句（PRAGMA 等），失败时打日志。
    bool execSimpleSql(const char* sql);

    //一条语句执行失败之后，核对 SQLite 是不是顺手把整个事务也回滚掉了。
    //SQLite 官方文档在 sqlite3_get_autocommit 的说明里写明：SQLITE_FULL / SQLITE_IOERR /
    //SQLITE_NOMEM / SQLITE_BUSY / SQLITE_INTERRUPT 这几类错误发生在多语句事务中时，
    //事务有可能被自动回滚，而「唯一能查出是否发生了自动回滚的办法就是调用本接口」
    //（见 sqlite3/sqlite3.h 该接口的注释）。不核对的话，事务已经没了却没人知道，
    //后续语句会各自独立提交 —— 与 MySQL 事务中途断线是同一类隐患。
    //sql：刚刚失败的那条 SQL 原文，仅用于日志，可为 NULL。
    void noteTransactionAbortedBySqlite(const char* sql);

    //当前连接能否放回连接池：没有未结束的事务（以 sqlite3_get_autocommit 为准），也没有未释放的语句。
    //SQL 执行出错不影响连接本身，所以与 MySQL 不同，出过错的连接照常可以放回
    bool canReturnToPool() const;

    struct sqlite3* m_db;               //SQLite 连接句柄；NULL 表示尚未打开或已关闭

    //本连接是否使用连接池，见 initialize 的 usePool 参数。为 true 时析构可能把连接放回池中
    bool            m_bPooled;
    std::string     m_strDbPath;        //数据库文件路径，仅用于日志与重复打开时的记录

    //本连接上是否有一个由 beginTransaction() 开启、尚未了结的事务。
    //注意它只是本类自己的记录，**不是**事务是否存在的权威来源 —— 权威来源是
    //sqlite3_get_autocommit()，见 inTransaction()。保留这个标志是为了能识别出
    //「本来开着事务，结果被 SQLite 自动回滚掉了」这一转变并记日志。
    bool            m_bInTransaction;
};

#endif // __DATABASE_SQLITE_H__
