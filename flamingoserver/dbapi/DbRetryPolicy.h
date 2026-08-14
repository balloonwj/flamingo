/**
 *  数据库断线重试策略：判定一条 SQL 执行失败之后，究竟应当「重连并把它重跑一遍」，
 *  还是「直接按失败返回，让调用方走自己的错误路径」。
 *
 *  这段判定原本散在 mysqlapi/DatabaseMysql.cpp 的三个分支里各写了一遍，且只认
 *  CR_SERVER_GONE_ERROR 一个错误码。更要紧的是它完全不看当前有没有事务在进行：
 *  事务进行到一半时连接断掉，服务端一侧的事务已被整体回滚，而重连拿到的是一条
 *  全新的、处于自动提交状态的连接；被重跑的那条语句于是独立生效并立即提交，其后
 *  的语句同样各自提交，最后的 COMMIT 成了空操作，失败路径上发的 ROLLBACK 也退不掉
 *  任何东西 —— 原子性被静默破坏，日志里还看不出异常。
 *
 *  故把判定抽成本文件里的几个纯函数，一处定义、多处共用，并且能被单元测试直接
 *  表驱动地验证：真造一个「执行到一半就断开」的 MySQL 服务端来测这段逻辑并不现实，
 *  把判定隔离出来单测是可行且有价值的做法。对应用例见 tests/test_dbtransaction.cpp
 *  的 Suite DRP。
 *
 *  本文件刻意<u>不</u>包含 mysql/errmsg.h：测试程序是带 FLAMINGO_NO_MYSQL 编译的，
 *  既不链接也不包含 MySQL 客户端库（见 CMakeLists.txt 与 windowsproj/Tests.vcxproj），
 *  包含了就编不过。两个错误码在这里按数值复写，mysqlapi/DatabaseMysql.cpp 里有
 *  static_assert 保证它们与官方宏定义始终一致，将来若数值对不上会直接编译报错。
 *
 *  balloonwj@qq.com   2026-08-14
 */
#pragma once

namespace dbapi {

//MySQL 客户端错误码 CR_SERVER_GONE_ERROR 的数值，出处是 MySQL / MariaDB 客户端头文件
//errmsg.h（本仓库内为 deps/mariadb-connector/include/mysql/errmsg.h）。
//含义是「向服务端发送这条语句时才发现连接已经不可用」，最常见的成因是服务端的
//wait_timeout 把一条闲置连接回收掉了。此时语句<u>尚未</u>在服务端执行过，
//重连后重跑一遍不会产生重复数据。
constexpr unsigned int DB_MYSQL_ERR_SERVER_GONE = 2006;

//MySQL 客户端错误码 CR_SERVER_LOST 的数值，出处同上。
//含义是「语句已经发出去了，在等待服务端返回结果的过程中连接断掉了」。
//与 2006 的关键差别在于：这条语句究竟有没有在服务端执行过<u>无法确定</u>。
//长事务、耗时较久的语句最容易撞上的就是这一种。
constexpr unsigned int DB_MYSQL_ERR_SERVER_LOST = 2013;

//语句类别。断线之后能不能安全地把这条语句重跑一遍，取决于重复执行会不会改变数据，
//所以判定必须知道调用方跑的是哪一类语句。
enum DbStatementKind
{
    //只读语句（SELECT 等）。重复执行不改变任何数据，因此只要连接能重建，
    //重跑一遍总是安全的，两种断线错误码都可以重试。
    DB_STMT_READ_ONLY = 0,

    //写语句（INSERT / UPDATE / DELETE / DDL 等）。重复执行可能留下重复数据 ——
    //聊天消息落库就是典型的非幂等写入，重跑一遍用户就会看到两条一样的消息。
    //因此只有在「能确定语句根本没到达服务端」的那一类断线上才允许重试。
    DB_STMT_WRITE = 1
};

/**
 *  判断一个 MySQL 客户端错误码是否属于「连接已断开」这一类。
 *  @param uMysqlErrno mysql_errno() 的返回值。
 *  @return true 表示这次失败的原因是连接断开，而不是 SQL 本身有问题
 *          （语法错误、主键冲突、权限不足等都不算）。
 *  说明：判定结果为 true 时，无论后续是否重试，连接层都必须认为
 *       「服务端一侧此前开着的事务已经不复存在」。
 */
inline bool IsDisconnectMysqlErrno(unsigned int uMysqlErrno)
{
    return uMysqlErrno == DB_MYSQL_ERR_SERVER_GONE
        || uMysqlErrno == DB_MYSQL_ERR_SERVER_LOST;
}

/**
 *  判断一条 SQL 执行失败之后，是否应当重连并把它重新执行一遍。
 *
 *  @param uMysqlErrno   mysql_errno() 的返回值。
 *  @param bInTransaction 调用方当前是否处于一个由 IDatabase::beginTransaction()
 *                        开启的显式事务中。
 *  @param kind          本条语句的类别，取值见 DbStatementKind。
 *  @return true 表示「重连之后把这一条重跑一遍」；false 表示按失败返回，
 *          由调用方走自己的错误路径。
 *
 *  三条规则，按优先级从高到低：
 *    1. 事务进行中一律不重试。重连得到的是一条全新的、处于自动提交状态的连接，
 *       在它上面重跑这一条只会让这条语句单独生效并立即提交，破坏整批操作的原子性。
 *    2. 只读语句在两种断线错误码上都重试。SELECT 重复执行没有副作用。
 *    3. 写语句只在 CR_SERVER_GONE_ERROR 上重试。CR_SERVER_LOST 说明语句已经发出，
 *       它有没有在服务端生效无从判断，重跑一遍可能写进两份数据，宁可返回失败。
 */
inline bool ShouldRetryAfterDisconnect(unsigned int uMysqlErrno,
                                       bool bInTransaction,
                                       DbStatementKind kind)
{
    if (bInTransaction)
        return false;

    if (kind == DB_STMT_READ_ONLY)
        return IsDisconnectMysqlErrno(uMysqlErrno);

    return uMysqlErrno == DB_MYSQL_ERR_SERVER_GONE;
}

} // namespace dbapi
