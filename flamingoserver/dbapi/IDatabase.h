/**
 *  数据库连接的抽象接口：把「执行 SQL、取结果集、转义字符串、取自增 ID」这几件事
 *  从具体数据库里抽出来，让业务层只依赖本接口，不再直接依赖 MySQL 或 SQLite 的 API。
 *  目前有两个实现：mysqlapi/DatabaseMysql.h 的 CDatabaseMysql，以及
 *  sqliteapi/DatabaseSqlite.h 的 CDatabaseSqlite。
 *
 *  连接对象一律由 dbapi/DbFactory.h 的 DbFactory::createConnection() 创建，
 *  它会按 DbConfig 里配置的数据库类型造出对应实现并完成连接。典型用法：
 *      std::unique_ptr<IDatabase> pConn(DbFactory::createConnection());
 *      if (!pConn)
 *          return false;
 *      QueryResult* pResult = pConn->query("SELECT ...");
 *      ...
 *      delete pResult;
 *  连接对象出作用域即析构、连接随之关闭，业务层不需要（也不应该）缓存连接。
 *
 *  balloonwj@qq.com   2026-07-31
 */
#pragma once

#include <stdint.h>
#include <string>

#include "QueryResult.h"

//pquery / pexecute 内部拼接 SQL 所用栈上缓冲区的大小，单位为字节。
//拼接结果超过该长度会被判定为截断并直接返回失败（各驱动实现里都会打错误日志），
//需要更长的语句时请改用 std::stringstream 自行拼好，再调用 query / execute。
//取值沿用历史实现的 1024；调整它会影响所有 pquery / pexecute 调用点，务必谨慎。
constexpr int MAX_QUERY_LEN = 1024;

class IDatabase
{
public:
    virtual ~IDatabase() {}

    //执行一条会产生结果集的 SQL（SELECT 等）。
    //sql：SQL 语句原文，不得为 NULL。
    //返回值：成功返回结果集对象，**所有权归调用方**，用完须 delete；
    //       失败返回 NULL，失败原因已由实现写入日志。
    //注意：返回前实现内部已调用过一次 nextRow()，结果集停在第一行上。
    virtual QueryResult* query(const char* sql) = 0;

    //query(const char*) 的便利重载，供拼在 std::string / stringstream 里的 SQL 直接使用。
    //刻意做成非虚：所有实现的行为都应当与 query(const char*) 完全一致，没有覆写的必要。
    QueryResult* query(const std::string& sql)
    {
        return query(sql.c_str());
    }

    //printf 风格的 query：按 format 拼出 SQL 后执行。
    //拼接结果超过 MAX_QUERY_LEN 时判定为截断，返回 NULL。
    virtual QueryResult* pquery(const char* format, ...) = 0;

    //执行一条不产生结果集的 SQL（INSERT / UPDATE / DELETE / DDL 等）。
    //sql：SQL 语句原文，不得为 NULL。返回 true 表示执行成功。
    virtual bool execute(const char* sql) = 0;

    //execute 的详细版本，额外回填执行结果供调用方判断。
    //uAffectedCount：出参，本次语句影响的行数；失败时置 0。
    //nErrno：出参，数据库返回的错误码（各后端的错误码空间不同）；成功时置 0。
    virtual bool execute(const char* sql, uint32_t& uAffectedCount, int& nErrno) = 0;

    //printf 风格的 execute，截断规则同 pquery
    virtual bool pexecute(const char* format, ...) = 0;

    //取本连接上最近一次 INSERT 产生的自增 ID。
    //注意：自增 ID 是**连接私有**的状态，必须在写入它的那条连接上读取，
    //换一条连接读到的是 0。业务层「先 execute 插入、紧接着查自增 ID」的写法
    //之所以成立，正是因为两条语句跑在同一个 IDatabase 对象上。
    virtual uint32_t getInsertID() = 0;

    //把 szSrc 按当前数据库的转义规则写入 szDst，返回写入 szDst 的字节数（不含结尾的 '\0'）。
    //szDst：调用方提供的输出缓冲区，容量至少要有 uSize * 2 + 1 字节；
    //szSrc：待转义的原串，可含 '\0' 以外的任意字节；
    //uSize：szSrc 的有效字节数。
    //注意：MySQL 与 SQLite 的转义规则不同（MySQL 要处理反斜杠，SQLite 不需要），
    //     所以本方法必须由各驱动分别实现，不能共用。
    virtual int32_t escapeString(char* szDst, const char* szSrc, uint32_t uSize) = 0;
};
