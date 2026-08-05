/**
 *  IDatabase 的 SQLite 实现，DatabaseSqlite.h
 *  balloonwj@qq.com   2026-07-31
 *
 *  封装一条本地 SQLite 连接：打开（必要时创建）数据库文件、配置并发相关的 PRAGMA、
 *  注册 MySQL 兼容函数，然后按 IDatabase 的约定执行 SQL 并把结果集包成 SqliteQueryResult。
 *
 *  与 MySQL 实现的两处行为差异，实现时要留意：
 *    1. 没有「断线重连」这回事。SQLite 是进程内库，连接就是一个打开的文件句柄，
 *       不存在 CR_SERVER_GONE_ERROR 那种服务端跑掉的情况，所以没有重试逻辑。
 *    2. 并发写会撞上 SQLITE_BUSY。本实现开 WAL 日志模式并设置 busy_timeout，
 *       由 SQLite 内部自动重试，超时才真正失败。
 *
 *  业务层不直接构造本类，而是通过 DbFactory::createConnection() 拿到 IDatabase*。
 */
#ifndef __DATABASE_SQLITE_H__
#define __DATABASE_SQLITE_H__

#include <stdint.h>
#include <string>

#include "../dbapi/IDatabase.h"

//SQLite 连接句柄的前向声明。这里刻意不包含 sqlite3.h：
//那是个 60 多万行的合并头文件，没必要让每个包含本头文件的编译单元都吃一遍
struct sqlite3;

class CDatabaseSqlite : public IDatabase
{
public:
    CDatabaseSqlite(void);
    virtual ~CDatabaseSqlite(void);

    /**
     *  打开数据库文件并完成连接配置，重复调用会先关闭旧连接。
     *  @param dbPath 数据库文件路径，相对路径以进程工作目录为基准。
     *                文件不存在时会自动创建；所在目录不存在时会逐级创建。
     *  @return true 表示连接可用（文件已打开、PRAGMA 已生效、兼容函数已注册）。
     */
    bool initialize(const std::string& dbPath);

    virtual QueryResult* query(const char* sql);

    //基类里 query(const std::string&) 这个便利重载会被本类的 query 声明遮蔽，
    //用 using 把它重新引入本类作用域，理由同 CDatabaseMysql
    using IDatabase::query;

    virtual QueryResult* pquery(const char* format, ...);
    virtual bool execute(const char* sql);
    virtual bool execute(const char* sql, uint32_t& uAffectedCount, int& nErrno);
    virtual bool pexecute(const char* format, ...);

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

    struct sqlite3* m_db;           //SQLite 连接句柄；NULL 表示尚未打开或已关闭
    std::string     m_strDbPath;    //数据库文件路径，仅用于日志与重复打开时的记录
};

#endif // __DATABASE_SQLITE_H__
