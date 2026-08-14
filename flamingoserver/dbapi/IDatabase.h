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

    //在本连接上开启一个显式事务。
    //返回 true 表示事务已经开起来，此后的语句都属于这个事务，直到 commit() 或 rollback()。
    //返回 false 有两种情形：本连接上已经有一个事务在进行（见下面的嵌套约定），
    //或者开启事务的语句本身执行失败（连不上、权限不足等）；两种情形下都<u>没有</u>事务被开启，
    //调用方直接按失败返回即可，不需要（也不应该）再调 rollback()。
    //
    //**嵌套一律禁止。** 已经有事务在进行时再调用本方法，会记一条错误日志并返回 false，
    //正在进行的那个事务<u>不受任何影响</u>，仍由最初开启它的那一方负责提交或回滚。
    //之所以禁止而不是做成引用计数：两种后端对「事务里再开事务」的处理完全不同 ——
    //MySQL 会把外层事务隐式提交掉（一个没有任何报错、却已经把数据写死了的陷阱），
    //SQLite 则直接报 "cannot start a transaction within a transaction"。做成显式失败，
    //两边行为才一致、才可预期。确实需要局部回滚点时应当用保存点（SAVEPOINT），
    //那是另一套语义，本接口目前不提供。
    //
    //两种后端的事务语义差异，调用方按其中一个后端的行为写代码之前务必看清：
    //  · MySQL（InnoDB）默认隔离级别是可重复读（REPEATABLE READ），事务内的多次读取
    //    看到的是同一份快照；本接口在 MySQL 上发的是 START TRANSACTION 而不是 BEGIN。
    //  · SQLite 本接口发的是 BEGIN IMMEDIATE，也就是<u>开启时就拿写锁</u>，
    //    而不是 SQLite 默认的 BEGIN（deferred，写锁推迟到第一条写语句才拿）。
    //    理由见 sqliteapi/DatabaseSqlite.cpp 里 beginTransaction 的注释：
    //    deferred 事务在 WAL 模式下升级写锁失败时不会走 busy_timeout 重试，会立刻失败。
    //    代价是所有事务（哪怕只读）都会互相串行，本项目的事务都是写事务，可以接受。
    virtual bool beginTransaction() = 0;

    //提交当前事务。
    //返回 true 表示提交成功、数据已经落库。
    //返回 false 有两种情形，行为不同，务必区分：
    //  · 本连接上根本没有事务在进行：记一条告警日志后原样返回 false，什么语句都不会发出；
    //  · COMMIT 语句执行失败：此时数据<u>一定没有</u>提交，但事务是否还开着两种后端不一样 ——
    //    MySQL 上事务已经结束，SQLite 上（典型是 COMMIT 撞 SQLITE_BUSY）事务<u>仍然开着</u>，
    //    必须再显式回滚一次才算了结。用 CDbTransaction 守卫（dbapi/DbTransaction.h）
    //    就不必操心这个差异，它会在提交失败后由析构补一次回滚。
    virtual bool commit() = 0;

    //回滚当前事务，丢弃本次事务里的全部改动。
    //返回 true 表示 ROLLBACK 确实发出并执行成功。
    //本连接上没有事务在进行时，记一条告警日志并返回 false，不会发出任何语句 ——
    //这种情况多半是事务已经被数据库单方面终止了（MySQL 连接断开、SQLite 出错后自动回滚），
    //此时数据同样没有提交，调用方通常不需要区分这两种 false。
    virtual bool rollback() = 0;

    //查询本连接上当前是否有事务在进行。
    //这个状态不只是「调用方有没有调过 beginTransaction」，它会反映数据库单方面终止事务的情况：
    //  · MySQL：事务进行中连接断开时，连接层会把本状态清掉（服务端那边事务已被回滚）；
    //  · SQLite：以 sqlite3_get_autocommit() 为准，某些错误会让 SQLite 自动回滚整个事务。
    //所以「调了 beginTransaction 且还没提交，本方法却返回 false」是可能的，
    //它恰恰说明事务已经在中途丢失，整批操作必须整体判失败。
    virtual bool inTransaction() const = 0;

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
