/**
 *  数据库事务接口与断线重试策略的单元测试，test_dbtransaction.cpp
 *
 *  分成两套：
 *    · Suite DRP —— dbapi/DbRetryPolicy.h 里那几个纯函数的表驱动断言。
 *      要测的是「事务进行中断线时不许重连重试」这条规则，而真造一个执行到一半就断开的
 *      MySQL 服务端并不现实，所以把判定逻辑单独隔离成纯函数再对它做表驱动验证。
 *      这套用例不碰数据库，也不需要链接 MySQL 客户端库。
 *    · Suite DBT —— IDatabase 事务接口与 CDbTransaction 守卫的行为，对着一个临时 .db
 *      文件真读真写。SQLite 是嵌入式的，不需要任何外部服务，放进默认测试集没有负担。
 *      两种后端里只有 SQLite 能在默认测试程序里跑（默认测试目标带 FLAMINGO_NO_MYSQL
 *      编译，根本没有 MySQL 分支），MySQL 一侧的事务行为靠 Suite DRP 与代码审查覆盖。
 *
 *  balloonwj@qq.com   2026-08-14
 */
#include <stdio.h>
#include <string>

#include "doctest.h"
#include "../dbapi/DbRetryPolicy.h"
#include "../dbapi/DbTransaction.h"
#include "../sqliteapi/DatabaseSqlite.h"

//本套用例专用的临时数据库文件，与 SQD / CTM / CRM 几套用的文件分开，避免互相干扰
static const char* const DBT_DB_PATH = "flamingo_sqlite_dbtransaction_test.db";

//测试表里预置的两行数据的主键。写成具名常量而不是散在各处的裸数字，
//读用例时才看得出哪一行是事务开始前就存在的、哪一行是事务里新写的
static const int DBT_SEED_ID       = 1;     //每个用例开头就插好的一行，用来验证回滚不会误伤已有数据
static const int DBT_FIRST_NEW_ID  = 10;    //事务里写入的第一行
static const int DBT_SECOND_NEW_ID = 11;    //事务里写入的第二行

//删除测试库文件，连同 WAL 模式产生的两个附属文件一起清掉，
//保证每个用例都从一个全新的空库开始
static void removeDbtDbFiles()
{
    std::string basePath(DBT_DB_PATH);
    remove(basePath.c_str());
    remove((basePath + "-wal").c_str());
    remove((basePath + "-shm").c_str());
}

//打开测试库并建好测试表，再插入一行种子数据。
//f_id 是主键，DBT8 靠它制造一次主键冲突来观察 SQLite 在事务中出错时的实际行为。
static void openDbtDb(CDatabaseSqlite& db)
{
    REQUIRE(db.initialize(DBT_DB_PATH));
    REQUIRE(db.execute("CREATE TABLE IF NOT EXISTS t_dbt_test(f_id INTEGER PRIMARY KEY, f_note TEXT)"));
    REQUIRE(db.execute("DELETE FROM t_dbt_test"));

    char szSql[128];
    snprintf(szSql, sizeof(szSql), "INSERT INTO t_dbt_test(f_id, f_note) VALUES(%d, 'seed')", DBT_SEED_ID);
    REQUIRE(db.execute(szSql));
}

//数一数测试表里现在有多少行。用 COUNT(*) 而不是遍历结果集，是因为 COUNT(*)
//总会返回恰好一行，不必处理空结果集的边界
static int countRows(IDatabase& db)
{
    QueryResult* pResult = db.query("SELECT COUNT(*) FROM t_dbt_test");
    if (pResult == NULL)
        return -1;

    Field* pRow = pResult->fetch();
    int count = (pRow != NULL) ? pRow[0].getInt32() : -1;
    delete pResult;
    return count;
}

//往测试表里插一行，返回是否成功。note 只是便于人工查库时辨认，不参与断言
static bool insertRow(IDatabase& db, int id, const char* note)
{
    char szSql[256];
    snprintf(szSql, sizeof(szSql), "INSERT INTO t_dbt_test(f_id, f_note) VALUES(%d, '%s')", id, note);
    return db.execute(szSql);
}

// ---------------------------------------------------------------------------
// Suite DRP：断线重试策略（纯函数，不碰数据库）
// ---------------------------------------------------------------------------

TEST_CASE("DRP1 断线错误码的判定：2006、2013、2026、4031 算连接断开")
{
    CHECK(dbapi::IsDisconnectMysqlErrno(dbapi::DB_MYSQL_ERR_SERVER_GONE));
    CHECK(dbapi::IsDisconnectMysqlErrno(dbapi::DB_MYSQL_ERR_SERVER_LOST));
    //TLS 连接被服务端关闭后的读写失败，与空闲超时被服务端断开（实测见 DbRetryPolicy.h 的注释）
    CHECK(dbapi::IsDisconnectMysqlErrno(dbapi::DB_MYSQL_ERR_TLS_CONNECTION));
    CHECK(dbapi::IsDisconnectMysqlErrno(dbapi::DB_MYSQL_ERR_INTERACTION_TIMEOUT));

    //数值本身也钉住，避免有人顺手改了常量却没人发现
    CHECK(dbapi::DB_MYSQL_ERR_SERVER_GONE == 2006);
    CHECK(dbapi::DB_MYSQL_ERR_SERVER_LOST == 2013);
    CHECK(dbapi::DB_MYSQL_ERR_TLS_CONNECTION == 2026);
    CHECK(dbapi::DB_MYSQL_ERR_INTERACTION_TIMEOUT == 4031);

    //0 表示没有错误；1062 是主键冲突；2005 是解析不出主机名，属于连不上而不是连上了又断
    CHECK_FALSE(dbapi::IsDisconnectMysqlErrno(0));
    CHECK_FALSE(dbapi::IsDisconnectMysqlErrno(1062));
    CHECK_FALSE(dbapi::IsDisconnectMysqlErrno(2005));
}

TEST_CASE("DRP2 重试判定的完整取值表：错误码 × 事务中与否 × 语句类别")
{
    //一行一种组合，字段依次是：mysql 错误码、是否处于事务中、语句类别、期望的判定结果。
    //之所以写成表而不是一串独立断言，是因为这里真正要保证的是「所有组合都被想到过」，
    //表格形式漏掉哪一行一眼就能看出来
    struct RetryCase
    {
        unsigned int            m_uErrno;       //mysql_errno() 的返回值
        bool                    m_bInTx;        //调用时是否处于显式事务中
        dbapi::DbStatementKind  m_kind;         //语句类别
        bool                    m_bExpected;    //期望 ShouldRetryAfterDisconnect 返回什么
    };

    static const RetryCase kCases[] =
    {
        //非事务 + 只读：四种断线码都该重试，SELECT 重跑没有副作用
        { dbapi::DB_MYSQL_ERR_SERVER_GONE,         false, dbapi::DB_STMT_READ_ONLY, true  },
        { dbapi::DB_MYSQL_ERR_SERVER_LOST,         false, dbapi::DB_STMT_READ_ONLY, true  },
        { dbapi::DB_MYSQL_ERR_TLS_CONNECTION,      false, dbapi::DB_STMT_READ_ONLY, true  },
        { dbapi::DB_MYSQL_ERR_INTERACTION_TIMEOUT, false, dbapi::DB_STMT_READ_ONLY, true  },
        //非事务 + 只读 + 普通 SQL 错误：不是断线，不该重试
        { 1062,                                    false, dbapi::DB_STMT_READ_ONLY, false },

        //非事务 + 写：只有能确定语句没到达服务端的 2006 与 4031，才允许重试
        { dbapi::DB_MYSQL_ERR_SERVER_GONE,         false, dbapi::DB_STMT_WRITE,     true  },
        { dbapi::DB_MYSQL_ERR_INTERACTION_TIMEOUT, false, dbapi::DB_STMT_WRITE,     true  },
        //非事务 + 写 + 2013 / 2026：语句可能已经发出，有没有生效无从判断，重跑可能写进两份数据
        { dbapi::DB_MYSQL_ERR_SERVER_LOST,         false, dbapi::DB_STMT_WRITE,     false },
        { dbapi::DB_MYSQL_ERR_TLS_CONNECTION,      false, dbapi::DB_STMT_WRITE,     false },
        { 1062,                                    false, dbapi::DB_STMT_WRITE,     false },

        //事务中：无论错误码是什么、语句是哪一类，一律不重试。
        //这是本次改动的核心规则 —— 重连拿到的是一条全新的、处于自动提交状态的连接，
        //在它上面重跑只会让这条语句单独生效并立即提交，把整批操作的原子性破坏掉
        { dbapi::DB_MYSQL_ERR_SERVER_GONE,         true,  dbapi::DB_STMT_READ_ONLY, false },
        { dbapi::DB_MYSQL_ERR_SERVER_LOST,         true,  dbapi::DB_STMT_READ_ONLY, false },
        { dbapi::DB_MYSQL_ERR_TLS_CONNECTION,      true,  dbapi::DB_STMT_READ_ONLY, false },
        { dbapi::DB_MYSQL_ERR_INTERACTION_TIMEOUT, true,  dbapi::DB_STMT_READ_ONLY, false },
        { 1062,                                    true,  dbapi::DB_STMT_READ_ONLY, false },
        { dbapi::DB_MYSQL_ERR_SERVER_GONE,         true,  dbapi::DB_STMT_WRITE,     false },
        { dbapi::DB_MYSQL_ERR_SERVER_LOST,         true,  dbapi::DB_STMT_WRITE,     false },
        { dbapi::DB_MYSQL_ERR_TLS_CONNECTION,      true,  dbapi::DB_STMT_WRITE,     false },
        { dbapi::DB_MYSQL_ERR_INTERACTION_TIMEOUT, true,  dbapi::DB_STMT_WRITE,     false },
        { 1062,                                    true,  dbapi::DB_STMT_WRITE,     false }
    };

    const size_t caseCount = sizeof(kCases) / sizeof(kCases[0]);
    for (size_t i = 0; i < caseCount; i++)
    {
        const RetryCase& item = kCases[i];
        INFO("case index: " << i << ", errno: " << item.m_uErrno
             << ", inTransaction: " << item.m_bInTx << ", kind: " << (int)item.m_kind);
        CHECK(dbapi::ShouldRetryAfterDisconnect(item.m_uErrno, item.m_bInTx, item.m_kind) == item.m_bExpected);
    }
}

// ---------------------------------------------------------------------------
// Suite DBT：事务接口与 RAII 守卫（对着临时 .db 文件真读真写）
// ---------------------------------------------------------------------------

TEST_CASE("DBT1 提交之后事务里写入的数据可见")
{
    removeDbtDbFiles();
    CDatabaseSqlite db;
    openDbtDb(db);

    REQUIRE(db.beginTransaction());
    REQUIRE(insertRow(db, DBT_FIRST_NEW_ID, "committed"));
    REQUIRE(insertRow(db, DBT_SECOND_NEW_ID, "committed"));
    REQUIRE(db.commit());

    //种子行 + 事务里写的两行
    CHECK(countRows(db) == 3);
    CHECK_FALSE(db.inTransaction());

    removeDbtDbFiles();
}

TEST_CASE("DBT2 回滚之后事务里写入的数据全部不可见，且不误伤事务开始前已有的数据")
{
    removeDbtDbFiles();
    CDatabaseSqlite db;
    openDbtDb(db);

    REQUIRE(db.beginTransaction());
    REQUIRE(insertRow(db, DBT_FIRST_NEW_ID, "rolled back"));
    REQUIRE(insertRow(db, DBT_SECOND_NEW_ID, "rolled back"));
    //回滚之前，本连接自己是看得到这两行的
    REQUIRE(countRows(db) == 3);

    REQUIRE(db.rollback());

    //只剩下事务开始前就有的那一行
    CHECK(countRows(db) == 1);
    CHECK_FALSE(db.inTransaction());

    removeDbtDbFiles();
}

TEST_CASE("DBT3 守卫作用域正常结束却没有显式提交时自动回滚")
{
    removeDbtDbFiles();
    CDatabaseSqlite db;
    openDbtDb(db);

    {
        //模拟「中途 return 忘了回滚」：进了作用域、写了数据，然后什么都不做就出去
        CDbTransaction transaction(&db);
        REQUIRE(transaction.IsActive());
        REQUIRE(insertRow(db, DBT_FIRST_NEW_ID, "abandoned"));
        REQUIRE(insertRow(db, DBT_SECOND_NEW_ID, "abandoned"));
        REQUIRE(countRows(db) == 3);
    }

    //守卫析构时回滚了，两行都没留下
    CHECK(countRows(db) == 1);
    CHECK_FALSE(db.inTransaction());

    removeDbtDbFiles();
}

TEST_CASE("DBT4 守卫显式提交之后析构不会再回滚一次")
{
    removeDbtDbFiles();
    CDatabaseSqlite db;
    openDbtDb(db);

    {
        CDbTransaction transaction(&db);
        REQUIRE(transaction.IsActive());
        REQUIRE(insertRow(db, DBT_FIRST_NEW_ID, "committed by guard"));
        REQUIRE(transaction.Commit());
        //提交成功之后守卫不再持有事务，析构时什么都不做
        CHECK_FALSE(transaction.IsActive());
    }

    //数据还在，说明析构没有把已经提交的事务再回滚一遍
    CHECK(countRows(db) == 2);
    CHECK_FALSE(db.inTransaction());

    removeDbtDbFiles();
}

TEST_CASE("DBT5 守卫显式回滚之后析构不会重复回滚，且回滚后已提交的数据不受影响")
{
    removeDbtDbFiles();
    CDatabaseSqlite db;
    openDbtDb(db);

    {
        CDbTransaction transaction(&db);
        REQUIRE(transaction.IsActive());
        REQUIRE(insertRow(db, DBT_FIRST_NEW_ID, "explicit rollback"));
        REQUIRE(transaction.Rollback());
        CHECK_FALSE(transaction.IsActive());

        //已经回滚过，再回滚一次应当被拒绝（守卫手里已经没有事务了）
        CHECK_FALSE(transaction.Rollback());
    }

    CHECK(countRows(db) == 1);
    CHECK_FALSE(db.inTransaction());

    //守卫析构没有误伤后续数据：再写一行仍然写得进去
    CHECK(insertRow(db, DBT_SECOND_NEW_ID, "after guard"));
    CHECK(countRows(db) == 2);

    removeDbtDbFiles();
}

TEST_CASE("DBT6 禁止嵌套：事务进行中再开事务被拒绝，且原事务不受影响仍能正常提交")
{
    removeDbtDbFiles();
    CDatabaseSqlite db;
    openDbtDb(db);

    REQUIRE(db.beginTransaction());
    REQUIRE(insertRow(db, DBT_FIRST_NEW_ID, "outer"));

    //嵌套按接口约定明确失败，而不是隐式提交外层事务（MySQL 的 BEGIN 就会那样做）
    CHECK_FALSE(db.beginTransaction());

    //被拒绝之后外层事务仍然活着
    CHECK(db.inTransaction());
    REQUIRE(insertRow(db, DBT_SECOND_NEW_ID, "outer again"));
    REQUIRE(db.commit());

    CHECK(countRows(db) == 3);

    removeDbtDbFiles();
}

TEST_CASE("DBT7 守卫套两层时内层不激活，外层事务照常完整生效")
{
    removeDbtDbFiles();
    CDatabaseSqlite db;
    openDbtDb(db);

    {
        CDbTransaction outer(&db);
        REQUIRE(outer.IsActive());
        REQUIRE(insertRow(db, DBT_FIRST_NEW_ID, "outer guard"));

        {
            //内层守卫拿不到事务，析构时也不会把外层的事务回滚掉
            CDbTransaction inner(&db);
            CHECK_FALSE(inner.IsActive());
        }

        //内层守卫已经析构，外层事务必须还在
        CHECK(db.inTransaction());
        REQUIRE(insertRow(db, DBT_SECOND_NEW_ID, "outer guard again"));
        REQUIRE(outer.Commit());
    }

    CHECK(countRows(db) == 3);

    removeDbtDbFiles();
}

TEST_CASE("DBT8 没有事务时 commit / rollback 一律被拒绝，且不影响后续正常使用")
{
    removeDbtDbFiles();
    CDatabaseSqlite db;
    openDbtDb(db);

    CHECK_FALSE(db.inTransaction());
    CHECK_FALSE(db.commit());
    CHECK_FALSE(db.rollback());

    //被拒绝之后连接仍然可用
    CHECK(insertRow(db, DBT_FIRST_NEW_ID, "after refused commit"));
    CHECK(countRows(db) == 2);

    //事务照常开得起来
    REQUIRE(db.beginTransaction());
    REQUIRE(db.rollback());
    CHECK_FALSE(db.inTransaction());

    removeDbtDbFiles();
}

TEST_CASE("DBT9 inTransaction 的取值随 begin / commit / rollback 变化")
{
    removeDbtDbFiles();
    CDatabaseSqlite db;
    openDbtDb(db);

    CHECK_FALSE(db.inTransaction());
    REQUIRE(db.beginTransaction());
    CHECK(db.inTransaction());
    REQUIRE(db.commit());
    CHECK_FALSE(db.inTransaction());

    REQUIRE(db.beginTransaction());
    CHECK(db.inTransaction());
    REQUIRE(db.rollback());
    CHECK_FALSE(db.inTransaction());

    removeDbtDbFiles();
}

TEST_CASE("DBT10 事务里某条语句因主键冲突失败时 SQLite 不会自动回滚，事务仍然开着")
{
    removeDbtDbFiles();
    CDatabaseSqlite db;
    openDbtDb(db);

    REQUIRE(db.beginTransaction());
    REQUIRE(insertRow(db, DBT_FIRST_NEW_ID, "ok"));

    //故意撞主键：种子行的 f_id 已经存在。SQLITE_CONSTRAINT 不在「可能触发自动回滚」
    //那几个错误码之列（见 sqlite3.h 中 sqlite3_get_autocommit 的说明：那几个是
    //SQLITE_FULL / SQLITE_IOERR / SQLITE_NOMEM / SQLITE_BUSY / SQLITE_INTERRUPT），
    //所以这里只有这一条语句失败，整个事务还在。本用例把这个边界钉死，
    //免得日后有人以为「语句一失败事务就没了」而省掉回滚
    CHECK_FALSE(insertRow(db, DBT_SEED_ID, "duplicate primary key"));
    CHECK(db.inTransaction());

    //事务还在，回滚才能把前面那条成功的插入一并撤掉 —— 这正是必须显式回滚的理由
    REQUIRE(db.rollback());
    CHECK(countRows(db) == 1);

    removeDbtDbFiles();
}

TEST_CASE("DBT11 事务未提交时另一条连接读不到这些行，提交之后才读得到")
{
    removeDbtDbFiles();
    CDatabaseSqlite writer;
    openDbtDb(writer);

    //第二条连接指向同一个库文件，模拟另一个 IO 线程在读
    CDatabaseSqlite reader;
    REQUIRE(reader.initialize(DBT_DB_PATH));
    REQUIRE(countRows(reader) == 1);

    REQUIRE(writer.beginTransaction());
    REQUIRE(insertRow(writer, DBT_FIRST_NEW_ID, "uncommitted"));
    REQUIRE(insertRow(writer, DBT_SECOND_NEW_ID, "uncommitted"));

    //写连接自己看得到，读连接看不到 —— 未提交的中间状态不会泄漏出去。
    //WAL 模式下读不会被写阻塞，所以这一步不会卡住
    CHECK(countRows(writer) == 3);
    CHECK(countRows(reader) == 1);

    REQUIRE(writer.commit());

    //提交之后读连接才看得到全部三行，而且是一次性看到两行，不会看到只写了一行的中间态
    CHECK(countRows(reader) == 3);

    removeDbtDbFiles();
}

TEST_CASE("DBT12 inTransaction 以 SQLite 自身的状态为准，而不是以本类记的标志为准")
{
    removeDbtDbFiles();
    CDatabaseSqlite db;
    openDbtDb(db);

    //绕过 beginTransaction，直接发裸的事务控制语句。此时本类内部的标志位没有被置位，
    //inTransaction 若只看标志位就会答错。它必须答对，因为整个「事务被 SQLite 自动回滚了
    //却没人知道」的检测都建立在「以 sqlite3_get_autocommit 为权威来源」这一点上
    REQUIRE(db.execute("BEGIN"));
    CHECK(db.inTransaction());

    REQUIRE(insertRow(db, DBT_FIRST_NEW_ID, "raw begin"));
    REQUIRE(db.rollback());

    CHECK_FALSE(db.inTransaction());
    CHECK(countRows(db) == 1);

    removeDbtDbFiles();
}

TEST_CASE("DBT13 守卫接受空连接：不激活、析构不崩溃")
{
    //调用方拿连接失败时可能直接把 NULL 传进来，守卫必须扛住而不是崩掉
    CDbTransaction transaction(NULL);
    CHECK_FALSE(transaction.IsActive());
    CHECK_FALSE(transaction.Commit());
    CHECK_FALSE(transaction.Rollback());
}
