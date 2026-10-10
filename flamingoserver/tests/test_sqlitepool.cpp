/**
 *  SQLite 连接池的集成测试（Suite PS），test_sqlitepool.cpp
 *
 *  对着临时 .db 文件验证 CSqliteHandlePool 接进 CDatabaseSqlite / DbFactory 之后的行为：
 *  连接确实被复用、有未结束事务的连接不放回池、SQL 出错不影响归还、清空连接池后库文件可以删除、
 *  不同库文件的连接互不混用，以及复用前后的耗时。
 *  判断「是不是同一条连接」靠 SQLite 的 total_changes()：它返回这条连接打开以来增删改过的总行数，
 *  新打开的连接一定是 0。
 *
 *  SQLite 是嵌入式的，不需要外部服务，Windows 与 Linux 的默认测试目标都能跑。
 *  临时库文件在当前目录，多个会话同时运行测试时请各自在独立目录里运行。
 *
 *  balloonwj@qq.com   2026-10-04
 */
#include <stdio.h>
#include <chrono>
#include <memory>
#include <string>

#include "doctest.h"
#include "../base/Singleton.h"
#include "../dbapi/DbConfig.h"
#include "../dbapi/DbFactory.h"
#include "../dbapi/DbHandlePool.h"
#include "../dbapi/IDatabase.h"
#include "../dbapi/QueryResult.h"
#include "../sqliteapi/DatabaseSqlite.h"
#include "../sqliteapi/SqliteManager.h"

namespace {

//本套用例专用的两个临时库文件，与其他套件的文件分开
const char* const PS_DB_PATH = "flamingo_sqlite_pool_test.db";
const char* const PS_OTHER_DB_PATH = "flamingo_sqlite_pool_other_test.db";

//PS6：取连接并执行一条查询的轮数，开池、关池各跑这么多轮比较耗时
const int PS6_ROUNDS = 200;

//删除一个临时库文件，连同 WAL 模式的两个附属文件。返回主文件是否已不存在
bool psRemoveDbFiles(const char* path)
{
    std::string base(path);
    remove((base + "-wal").c_str());
    remove((base + "-shm").c_str());
    if (remove(base.c_str()) == 0)
        return true;
    FILE* f = fopen(base.c_str(), "rb");
    if (f == NULL)
        return true;
    fclose(f);
    return false;
}

//把全局 CDbConfig 指向 path 这个库
void psUseDb(const char* path)
{
    Singleton<CDbConfig>::Instance().init(DB_TYPE_NAME_SQLITE, NULL, NULL, NULL, NULL, path);
}

//每个用例开头调用：关闭并清空连接池，重建一个完整结构的测试库（建表与 chatserver 启动时一致，
//这样 PS6 量到的开连接耗时包含解析完整库结构的开销），再建一张本套用例自己的小表
void psPrepare()
{
    DbFactory::setConnectionPoolEnabled(false);
    REQUIRE(psRemoveDbFiles(PS_DB_PATH));
    REQUIRE(Singleton<CSqliteManager>::Instance().init(PS_DB_PATH));
    psUseDb(PS_DB_PATH);

    std::unique_ptr<IDatabase> conn(DbFactory::createConnection());
    REQUIRE(conn);
    REQUIRE(conn->execute("CREATE TABLE IF NOT EXISTS t_ps_test(f_id INTEGER PRIMARY KEY, f_note TEXT)"));
}

//SQLite 连接池本身
DbHandlePool& psPool()
{
    return Singleton<CSqliteHandlePool>::Instance();
}

//经 DbFactory 取一条连接，与业务代码的取法相同
std::unique_ptr<IDatabase> psOpen()
{
    std::unique_ptr<IDatabase> conn(DbFactory::createConnection());
    REQUIRE(conn);
    return conn;
}

//执行一条只返回一行一列整数的查询。查询失败或没有结果行时返回 false
bool psQueryUInt64(IDatabase* conn, const char* sql, uint64_t& value)
{
    QueryResult* result = conn->query(sql);
    if (result == NULL)
        return false;

    bool ok = false;
    Field* row = result->fetch();
    if (row != NULL)
    {
        value = row[0].getUInt64();
        ok = true;
    }
    result->endQuery();
    delete result;
    return ok;
}

//这条连接打开以来增删改过的总行数，新连接为 0
uint64_t psTotalChanges(IDatabase* conn)
{
    uint64_t changes = 0;
    REQUIRE(psQueryUInt64(conn, "SELECT total_changes()", changes));
    return changes;
}

//测试表现有的行数
uint64_t psCountRows(IDatabase* conn)
{
    uint64_t count = 0;
    REQUIRE(psQueryUInt64(conn, "SELECT COUNT(*) FROM t_ps_test", count));
    return count;
}

//在一个用例的作用域内打开连接池，离开作用域时关闭连接池（池中连接随之关闭）
class PsPoolScope
{
public:
    //打开连接池，阈值取默认值
    PsPoolScope()
    {
        DbFactory::setConnectionPoolEnabled(true);
    }

    //关闭连接池
    ~PsPoolScope()
    {
        DbFactory::setConnectionPoolEnabled(false);
    }
};

} // anonymous namespace

TEST_CASE("PS1 with the pool enabled consecutive connections reuse one sqlite connection")
{
    psPrepare();

    //连接池关闭：第二条是新打开的连接，total_changes() 为 0
    {
        std::unique_ptr<IDatabase> conn = psOpen();
        REQUIRE(conn->execute("INSERT INTO t_ps_test(f_note) VALUES('a')"));
        CHECK(psTotalChanges(conn.get()) == 1);
    }
    {
        std::unique_ptr<IDatabase> conn = psOpen();
        CHECK(psTotalChanges(conn.get()) == 0);
    }

    //连接池打开：第二次取到的是第一条连接，它此前写过的一行仍计在 total_changes() 里
    PsPoolScope scope;
    {
        std::unique_ptr<IDatabase> conn = psOpen();
        REQUIRE(conn->execute("INSERT INTO t_ps_test(f_note) VALUES('b')"));
        CHECK(psTotalChanges(conn.get()) == 1);
    }
    CHECK(psPool().idleCount() == 1);
    {
        std::unique_ptr<IDatabase> conn = psOpen();
        CHECK(psPool().idleCount() == 0);
        CHECK(psTotalChanges(conn.get()) == 1);
    }
    CHECK(psPool().idleCount() == 1);

    std::string description = DbFactory::describeConnectionPools();
    INFO(description);
    CHECK(description.find("(sqlite) enabled: idle 1,") != std::string::npos);
}

TEST_CASE("PS2 a connection destroyed inside a transaction is not returned and its writes are rolled back")
{
    psPrepare();
    PsPoolScope scope;

    {
        std::unique_ptr<IDatabase> conn = psOpen();
        REQUIRE(conn->beginTransaction());
        REQUIRE(conn->execute("INSERT INTO t_ps_test(f_note) VALUES('uncommitted')"));
    }
    CHECK(psPool().idleCount() == 0);

    {
        std::unique_ptr<IDatabase> conn = psOpen();
        CHECK(psTotalChanges(conn.get()) == 0);
        CHECK(psCountRows(conn.get()) == 0);

        //事务正常提交的连接照常放回池中
        REQUIRE(conn->beginTransaction());
        REQUIRE(conn->execute("INSERT INTO t_ps_test(f_note) VALUES('committed')"));
        REQUIRE(conn->commit());
    }
    CHECK(psPool().idleCount() == 1);
}

TEST_CASE("PS3 a failed statement does not stop the connection from being returned")
{
    psPrepare();
    PsPoolScope scope;

    {
        std::unique_ptr<IDatabase> conn = psOpen();
        REQUIRE(conn->execute("INSERT INTO t_ps_test(f_note) VALUES('x')"));
        CHECK_FALSE(conn->execute("SELEC 1"));
    }
    //SQLite 的 SQL 错误不影响连接本身，连接照常放回并可以继续使用
    CHECK(psPool().idleCount() == 1);
    {
        std::unique_ptr<IDatabase> conn = psOpen();
        CHECK(psTotalChanges(conn.get()) == 1);
        CHECK(psCountRows(conn.get()) == 1);
    }
}

TEST_CASE("PS4 after clearing the pool the database file can be deleted")
{
    psPrepare();
    {
        PsPoolScope scope;
        {
            std::unique_ptr<IDatabase> conn = psOpen();
            CHECK(psCountRows(conn.get()) == 0);
        }
        REQUIRE(psPool().idleCount() == 1);

        DbFactory::clearConnectionPools();
        CHECK(psPool().idleCount() == 0);
        //Windows 上被打开着的文件删不掉；池中的连接都已关闭，删除应当成功
        CHECK(psRemoveDbFiles(PS_DB_PATH));
    }
}

TEST_CASE("PS5 connections to different database files are never mixed")
{
    psPrepare();
    REQUIRE(psRemoveDbFiles(PS_OTHER_DB_PATH));
    PsPoolScope scope;

    {
        std::unique_ptr<IDatabase> conn = psOpen();
        REQUIRE(conn->execute("INSERT INTO t_ps_test(f_note) VALUES('first db')"));
    }
    REQUIRE(psPool().idleCount() == 1);

    //改连另一个库文件：池里那条连的是第一个库，不能交出来
    psUseDb(PS_OTHER_DB_PATH);
    {
        std::unique_ptr<IDatabase> conn = psOpen();
        CHECK(psTotalChanges(conn.get()) == 0);
    }
    CHECK(psPool().idleCount() == 2);

    psUseDb(PS_DB_PATH);
    {
        std::unique_ptr<IDatabase> conn = psOpen();
        CHECK(psTotalChanges(conn.get()) == 1);
    }

    DbFactory::clearConnectionPools();
    CHECK(psRemoveDbFiles(PS_OTHER_DB_PATH));
}

//PS6 的计时：取 rounds 次连接，每次执行一条查询（会用到库结构）后归还或关闭，返回总耗时（毫秒）
static double ps6Run(int rounds)
{
    std::chrono::steady_clock::time_point begin = std::chrono::steady_clock::now();
    for (int i = 0; i < rounds; ++i)
    {
        std::unique_ptr<IDatabase> conn = psOpen();
        uint64_t count = 0;
        REQUIRE(psQueryUInt64(conn.get(), "SELECT COUNT(*) FROM t_user", count));
    }
    std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::milli>(end - begin).count();
}

TEST_CASE("PS6 timing of connection setup with and without the pool")
{
    psPrepare();

    double withoutPoolMs = ps6Run(PS6_ROUNDS);

    double withPoolMs = 0;
    {
        PsPoolScope scope;
        withPoolMs = ps6Run(PS6_ROUNDS);
    }

    char line[256];
    snprintf(line, sizeof(line),
             "PS6 %d rounds of open + query: without pool %.1f ms (%.3f ms each), with pool %.1f ms (%.3f ms each)",
             PS6_ROUNDS, withoutPoolMs, withoutPoolMs / PS6_ROUNDS, withPoolMs, withPoolMs / PS6_ROUNDS);
    MESSAGE(line);
    CHECK(withPoolMs < withoutPoolMs);

    DbFactory::clearConnectionPools();
    psRemoveDbFiles(PS_DB_PATH);
}
