/**
 *  MySQL 连接池与保活的集成测试（Suite PM），test_mysqlpool.cpp
 *
 *  连真实的 MySQL 服务器，验证 CMysqlHandlePool 接进 CDatabaseMysql / DbFactory 之后的行为：
 *  连接确实被复用、不该放回池的连接没有放回、被服务器断开的空闲连接在取用时能被发现并替换、
 *  保活能让空闲连接撑过 wait_timeout、清空连接池后服务器一侧的连接也随之关闭，以及复用前后的耗时。
 *  判断「是不是同一条连接」靠 MySQL 的 CONNECTION_ID()：它是服务器分配给每条连接的线程号，
 *  新建连接时单调递增，同一条连接上多次查询返回同一个值。
 *
 *  只有带 MySQL 后端的 flamingo_tests_mysql 目标会编进这些用例（默认的 flamingo_tests 与
 *  Windows 的 Tests.vcxproj 定义了 FLAMINGO_NO_MYSQL，整个文件编译为空）；
 *  运行时还要设 FLAMINGO_TEST_MYSQL=1，否则各用例直接返回。跑法（在 flamingoserver 目录）：
 *      FLAMINGO_TEST_MYSQL=1 ./build-mysql/flamingo_tests_mysql --test-case="PM*"
 *  连接参数与 test_dbbackend.h 相同，取自 FLAMINGO_TEST_MYSQL_HOST / _USER / _PWD 三个环境变量。
 *  用例使用专用的库 flamingo_test_pool，每次运行开始时整个 DROP 重建。
 *
 *  PM4b、PM4c、PM5b、PM10 另外验证连接被服务端断开之后的重连重试规则（dbapi/DbRetryPolicy.h）：
 *  空闲连接被 KILL（走 TLS 时报 2026）或因 wait_timeout 断开（报 4031）之后，读语句重连重跑，
 *  写语句只在能确定没有执行过的 4031 上重跑。
 *
 *  PM5、PM5b、PM6、PM10 要等 MySQL 的 wait_timeout 到期，整套用例约需 25 秒。
 *
 *  balloonwj@qq.com   2026-10-04
 */
#ifndef FLAMINGO_NO_MYSQL

#include <stdio.h>
#include <stdlib.h>
#include <chrono>
#include <memory>
#include <sstream>
#include <string>
#include <thread>

#include "doctest.h"
#include "test_dbbackend.h"
#include "../base/Singleton.h"
#include "../dbapi/DbConfig.h"
#include "../dbapi/DbFactory.h"
#include "../dbapi/DbHandlePool.h"
#include "../dbapi/IDatabase.h"
#include "../dbapi/QueryResult.h"
#include "../mysqlapi/DatabaseMysql.h"

namespace {

//本套用例专用的库名。每次运行开始时会被整个 DROP 重建，不得与其他套件共用
const char* const PM_MYSQL_DB = "flamingo_test_pool";

//KILL 一条连接之后等服务器把它真正关掉的时长，单位毫秒。
//KILL 语句返回时服务器只是给目标线程打上了终止标记，套接字随后才关闭
const int PM_KILL_SETTLE_MS = 200;

//PM5、PM6 给被测连接设置的会话级 wait_timeout，单位秒。
//MySQL 服务器在连接空闲达到这个时长后主动断开它
const int PM5_WAIT_TIMEOUT_SEC = 2;
const int PM6_WAIT_TIMEOUT_SEC = 3;

//PM5：放回池中之后等待的时长，单位毫秒，要比 PM5_WAIT_TIMEOUT_SEC 长出一截，确保服务器已经断开连接
const int PM5_IDLE_MS = 3500;

//PM6：保活的轮数与每轮间隔（毫秒）。总时长 6 秒，是 PM6_WAIT_TIMEOUT_SEC 的两倍；
//每轮间隔 1 秒，比 wait_timeout 短，只要每轮都 ping 了，服务器就不会断开连接
const int PM6_ROUNDS = 6;
const int PM6_ROUND_MS = 1000;

//PM7：清空连接池后，等待服务器进程列表里那两条连接消失的最长时长与轮询间隔，单位毫秒
const int PM7_WAIT_MS = 3000;
const int PM7_POLL_MS = 100;

//PM9：取连接并执行一条 SELECT 1 的轮数，开池、关池各跑这么多轮比较耗时
const int PM9_ROUNDS = 200;

//读环境变量，未设置或为空时取默认值。默认值与 test_dbbackend.cpp 一致
const char* pmEnv(const char* name, const char* fallback)
{
    const char* v = ::getenv(name);
    if (v == NULL || v[0] == '\0')
        return fallback;
    return v;
}

std::string pmHost()
{
    return pmEnv("FLAMINGO_TEST_MYSQL_HOST", "127.0.0.1");
}

std::string pmUser()
{
    return pmEnv("FLAMINGO_TEST_MYSQL_USER", "root");
}

std::string pmPwd()
{
    return pmEnv("FLAMINGO_TEST_MYSQL_PWD", "123456");
}

//MySQL 连接池本身
DbHandlePool& pmPool()
{
    return Singleton<CMysqlHandlePool>::Instance();
}

//本进程是否已经重建过测试库。整套用例只需要一张自己的小表，不需要 flamingo 的完整表结构，
//所以不走 testdb::resetDb（那会建出全部业务表，慢），只在第一个用例里 DROP 重建一次库
bool g_pmDbReady = false;

//每个用例开头调用：未设 FLAMINGO_TEST_MYSQL=1 时返回 false，用例应直接返回；
//否则确保测试库与测试表存在，并把全局 CDbConfig 指向测试库
bool pmPrepare()
{
    if (!testdb::useMysql())
    {
        MESSAGE("Suite PM skipped: set FLAMINGO_TEST_MYSQL=1 to run it against a real MySQL server");
        return false;
    }

    //别的套件可能把连接池打开过，也可能把 CDbConfig 指到了别的库：两样都恢复到本套用例需要的状态
    DbFactory::setConnectionPoolEnabled(false);

    if (!g_pmDbReady)
    {
        CDatabaseMysql admin;
        REQUIRE(admin.initialize(pmHost(), pmUser(), pmPwd(), "mysql"));
        std::ostringstream drop;
        drop << "DROP DATABASE IF EXISTS " << PM_MYSQL_DB;
        REQUIRE(admin.execute(drop.str().c_str()));
        std::ostringstream create;
        create << "CREATE DATABASE " << PM_MYSQL_DB << " DEFAULT CHARACTER SET utf8mb4";
        REQUIRE(admin.execute(create.str().c_str()));
        std::ostringstream table;
        table << "CREATE TABLE " << PM_MYSQL_DB << ".t_pm_test("
              << "f_id INT NOT NULL AUTO_INCREMENT PRIMARY KEY, f_note VARCHAR(32) NOT NULL) ENGINE=InnoDB";
        REQUIRE(admin.execute(table.str().c_str()));
        g_pmDbReady = true;
    }

    Singleton<CDbConfig>::Instance().init(DB_TYPE_NAME_MYSQL, pmHost().c_str(), pmUser().c_str(),
                                          pmPwd().c_str(), PM_MYSQL_DB, NULL);
    return true;
}

//打开一条不经过连接池的连接，用来执行 KILL、查进程列表等管理操作
std::unique_ptr<CDatabaseMysql> pmOpenAdmin()
{
    std::unique_ptr<CDatabaseMysql> admin(new CDatabaseMysql());
    REQUIRE(admin->initialize(pmHost(), pmUser(), pmPwd(), PM_MYSQL_DB));
    return admin;
}

//经 DbFactory 取一条连接，与业务代码的取法相同
std::unique_ptr<IDatabase> pmOpen()
{
    std::unique_ptr<IDatabase> conn(DbFactory::createConnection());
    REQUIRE(conn);
    return conn;
}

//执行一条只返回一行一列整数的查询。查询失败或没有结果行时返回 false
bool pmQueryUInt64(IDatabase* conn, const char* sql, uint64_t& value)
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

//这条连接在 MySQL 服务器上的线程号，用来判断两次拿到的是不是同一条连接
uint64_t pmConnectionId(IDatabase* conn)
{
    uint64_t id = 0;
    REQUIRE(pmQueryUInt64(conn, "SELECT CONNECTION_ID()", id));
    return id;
}

//测试表现有的行数
uint64_t pmCountRows(IDatabase* conn)
{
    uint64_t count = 0;
    REQUIRE(pmQueryUInt64(conn, "SELECT COUNT(*) FROM t_pm_test", count));
    return count;
}

//在管理连接上 KILL 掉指定线程号的连接，并等服务器把它真正关掉
void pmKill(uint64_t connectionId)
{
    std::unique_ptr<CDatabaseMysql> admin = pmOpenAdmin();
    std::ostringstream sql;
    sql << "KILL " << connectionId;
    REQUIRE(admin->execute(sql.str().c_str()));
    std::this_thread::sleep_for(std::chrono::milliseconds(PM_KILL_SETTLE_MS));
}

//本套用例默认的连接池阈值：取用前不校验、不保活、不收缩，各用例再按需打开
DbHandlePool::Options pmOptions()
{
    DbHandlePool::Options opt;
    opt.m_validateAfterMs = -1;
    opt.m_keepAliveAfterMs = -1;
    opt.m_maxIdleMs = -1;
    return opt;
}

//在一个用例的作用域内打开连接池并设置阈值，离开作用域时关闭连接池（池中连接随之关闭）
//并恢复默认阈值，保证不影响后面的用例。同时记下进入时的累计统计，供用例计算本用例内的增量
class PmPoolScope
{
public:
    //打开连接池，并以 options 作为阈值
    explicit PmPoolScope(const DbHandlePool::Options& options)
    {
        pmPool().setOptions(options);
        DbFactory::setConnectionPoolEnabled(true);
        m_start = pmPool().getStats();
    }

    //关闭连接池并恢复默认阈值
    ~PmPoolScope()
    {
        DbFactory::setConnectionPoolEnabled(false);
        pmPool().setOptions(DbHandlePool::Options());
    }

    //进入作用域以来，取用前校验或保活 ping 失败的次数
    uint64_t pingFailures() const
    {
        return pmPool().getStats().m_pingFailures - m_start.m_pingFailures;
    }

    //进入作用域以来，保活发出的 ping 次数
    uint64_t keepAlivePings() const
    {
        return pmPool().getStats().m_keepAlivePings - m_start.m_keepAlivePings;
    }

private:
    DbHandlePool::Stats m_start;   //进入作用域时的累计统计
};

} // anonymous namespace

TEST_CASE("PM1 with the pool enabled consecutive connections reuse one server connection")
{
    if (!pmPrepare())
        return;

    //连接池关闭：每次都是新连接，线程号各不相同
    uint64_t closedFirst = 0;
    uint64_t closedSecond = 0;
    {
        std::unique_ptr<IDatabase> conn = pmOpen();
        closedFirst = pmConnectionId(conn.get());
    }
    {
        std::unique_ptr<IDatabase> conn = pmOpen();
        closedSecond = pmConnectionId(conn.get());
    }
    CHECK(closedFirst != closedSecond);

    //连接池打开：第一条连接用完放回池中，第二次取到的是同一条
    PmPoolScope scope(pmOptions());
    uint64_t pooledFirst = 0;
    uint64_t pooledSecond = 0;
    {
        std::unique_ptr<IDatabase> conn = pmOpen();
        pooledFirst = pmConnectionId(conn.get());
    }
    CHECK(pmPool().idleCount() == 1);
    {
        std::unique_ptr<IDatabase> conn = pmOpen();
        pooledSecond = pmConnectionId(conn.get());
        CHECK(pmPool().idleCount() == 0);
    }
    CHECK(pooledFirst == pooledSecond);
    CHECK(pmPool().idleCount() == 1);

    //定期写进日志的那一行统计里，空闲连接数与池的实际状态一致
    std::string description = DbFactory::describeConnectionPools();
    INFO(description);
    CHECK(description.find("(mysql) enabled: idle 1,") != std::string::npos);
}

TEST_CASE("PM2 a connection destroyed inside a transaction is not returned and its writes are discarded")
{
    if (!pmPrepare())
        return;
    PmPoolScope scope(pmOptions());

    uint64_t abandonedId = 0;
    {
        std::unique_ptr<IDatabase> conn = pmOpen();
        abandonedId = pmConnectionId(conn.get());
        REQUIRE(conn->execute("DELETE FROM t_pm_test"));
        REQUIRE(conn->beginTransaction());
        REQUIRE(conn->execute("INSERT INTO t_pm_test(f_note) VALUES('uncommitted')"));
    }
    CHECK(pmPool().idleCount() == 0);

    uint64_t committedId = 0;
    {
        std::unique_ptr<IDatabase> conn = pmOpen();
        committedId = pmConnectionId(conn.get());
        CHECK(committedId != abandonedId);
        CHECK(pmCountRows(conn.get()) == 0);

        //事务正常提交的连接照常放回池中
        REQUIRE(conn->beginTransaction());
        REQUIRE(conn->execute("INSERT INTO t_pm_test(f_note) VALUES('committed')"));
        REQUIRE(conn->commit());
    }
    CHECK(pmPool().idleCount() == 1);

    {
        std::unique_ptr<IDatabase> conn = pmOpen();
        CHECK(pmConnectionId(conn.get()) == committedId);
        CHECK(pmCountRows(conn.get()) == 1);
    }
}

TEST_CASE("PM3 a connection whose last operation failed is not returned")
{
    if (!pmPrepare())
        return;
    PmPoolScope scope(pmOptions());

    uint64_t failedId = 0;
    {
        std::unique_ptr<IDatabase> conn = pmOpen();
        failedId = pmConnectionId(conn.get());
        CHECK_FALSE(conn->execute("SELEC 1"));
    }
    CHECK(pmPool().idleCount() == 0);

    //出错之后又成功执行过语句：错误码已被清掉，连接照常放回
    {
        std::unique_ptr<IDatabase> conn = pmOpen();
        CHECK(pmConnectionId(conn.get()) != failedId);
        CHECK_FALSE(conn->execute("SELEC 1"));
        CHECK(conn->execute("DELETE FROM t_pm_test WHERE 1 = 0"));
    }
    CHECK(pmPool().idleCount() == 1);
}

TEST_CASE("PM4 an idle connection killed by the server is detected by the validate ping and replaced")
{
    if (!pmPrepare())
        return;
    DbHandlePool::Options opt = pmOptions();
    opt.m_validateAfterMs = 0;
    PmPoolScope scope(opt);

    uint64_t killedId = 0;
    {
        std::unique_ptr<IDatabase> conn = pmOpen();
        killedId = pmConnectionId(conn.get());
    }
    REQUIRE(pmPool().idleCount() == 1);
    pmKill(killedId);

    {
        std::unique_ptr<IDatabase> conn = pmOpen();
        CHECK(pmConnectionId(conn.get()) != killedId);
    }
    CHECK(scope.pingFailures() == 1);
}

TEST_CASE("PM4b without the validate ping a read on a killed pooled connection reconnects and succeeds")
{
    if (!pmPrepare())
        return;
    PmPoolScope scope(pmOptions());

    uint64_t killedId = 0;
    {
        std::unique_ptr<IDatabase> conn = pmOpen();
        killedId = pmConnectionId(conn.get());
    }
    pmKill(killedId);

    //池不校验，交出来的是已被 KILL 的那条连接。走 TLS 时读语句报 2026，
    //它属于断线错误码，CDatabaseMysql::query 重连后重跑一遍
    std::unique_ptr<IDatabase> conn = pmOpen();
    uint64_t newId = 0;
    CHECK(pmQueryUInt64(conn.get(), "SELECT CONNECTION_ID()", newId));
    CHECK(newId != killedId);
}

TEST_CASE("PM4c without the validate ping a write on a killed pooled connection fails and is not retried")
{
    if (!pmPrepare())
        return;
    PmPoolScope scope(pmOptions());

    uint64_t killedId = 0;
    {
        std::unique_ptr<IDatabase> conn = pmOpen();
        killedId = pmConnectionId(conn.get());
        REQUIRE(conn->execute("DELETE FROM t_pm_test"));
    }
    pmKill(killedId);

    //2026 与 2013 一样无法确定语句是否已经执行过，写语句不重试（见 dbapi/DbRetryPolicy.h），
    //宁可返回失败，也不冒写进两份数据的风险
    {
        std::unique_ptr<IDatabase> conn = pmOpen();
        uint32_t affected = 0;
        int errNo = 0;
        CHECK_FALSE(conn->execute("INSERT INTO t_pm_test(f_note) VALUES('after kill')", affected, errNo));
        CHECK(errNo == (int)CR_SSL_CONNECTION_ERROR);
    }

    //失败的连接不会放回池中；换一条新连接核对：这一行确实没有写进去
    CHECK(pmPool().idleCount() == 0);
    std::unique_ptr<IDatabase> conn = pmOpen();
    CHECK(pmCountRows(conn.get()) == 0);
}

TEST_CASE("PM5 a connection closed by wait_timeout while idle is detected and replaced")
{
    if (!pmPrepare())
        return;
    DbHandlePool::Options opt = pmOptions();
    opt.m_validateAfterMs = 1000;
    PmPoolScope scope(opt);

    uint64_t expiredId = 0;
    {
        std::unique_ptr<IDatabase> conn = pmOpen();
        expiredId = pmConnectionId(conn.get());
        std::ostringstream sql;
        sql << "SET SESSION wait_timeout = " << PM5_WAIT_TIMEOUT_SEC;
        REQUIRE(conn->execute(sql.str().c_str()));
    }
    REQUIRE(pmPool().idleCount() == 1);

    std::this_thread::sleep_for(std::chrono::milliseconds(PM5_IDLE_MS));

    {
        std::unique_ptr<IDatabase> conn = pmOpen();
        CHECK(pmConnectionId(conn.get()) != expiredId);
    }
    CHECK(scope.pingFailures() == 1);
}

TEST_CASE("PM5b without the validate ping reads and writes on a wait_timeout-expired connection reconnect and succeed")
{
    if (!pmPrepare())
        return;
    PmPoolScope scope(pmOptions());

    //两条连接同时取出、各自设好会话 wait_timeout 后一起放回池中，这样一次等待就能让两条都过期
    uint64_t firstId = 0;
    uint64_t secondId = 0;
    {
        std::unique_ptr<IDatabase> first = pmOpen();
        std::unique_ptr<IDatabase> second = pmOpen();
        firstId = pmConnectionId(first.get());
        secondId = pmConnectionId(second.get());
        REQUIRE(first->execute("DELETE FROM t_pm_test"));
        std::ostringstream sql;
        sql << "SET SESSION wait_timeout = " << PM5_WAIT_TIMEOUT_SEC;
        REQUIRE(first->execute(sql.str().c_str()));
        REQUIRE(second->execute(sql.str().c_str()));
    }
    REQUIRE(pmPool().idleCount() == 2);

    std::this_thread::sleep_for(std::chrono::milliseconds(PM5_IDLE_MS));

    //同时取出两条，拿到的正是那两条已过期的连接（池不校验），一条测写、一条测读
    std::unique_ptr<IDatabase> writer = pmOpen();
    std::unique_ptr<IDatabase> reader = pmOpen();
    REQUIRE(pmPool().idleCount() == 0);

    //写：服务端报 4031，语句在连接关闭之后才发出、不可能被执行过，所以重连后重跑
    uint32_t affected = 0;
    int errNo = 0;
    CHECK(writer->execute("INSERT INTO t_pm_test(f_note) VALUES('after timeout')", affected, errNo));
    CHECK(affected == 1);

    //读：同样重连后重跑。顺带核对上面那一行只写进去了一份
    uint64_t rows = 0;
    CHECK(pmQueryUInt64(reader.get(), "SELECT COUNT(*) FROM t_pm_test", rows));
    CHECK(rows == 1);

    //两条都已换成新连接
    uint64_t writerId = pmConnectionId(writer.get());
    uint64_t readerId = pmConnectionId(reader.get());
    CHECK(writerId != firstId);
    CHECK(writerId != secondId);
    CHECK(readerId != firstId);
    CHECK(readerId != secondId);
}

TEST_CASE("PM6 keep-alive pings keep an idle connection open past wait_timeout")
{
    if (!pmPrepare())
        return;
    DbHandlePool::Options opt = pmOptions();
    opt.m_keepAliveAfterMs = PM6_ROUND_MS;
    PmPoolScope scope(opt);

    uint64_t keptId = 0;
    {
        std::unique_ptr<IDatabase> conn = pmOpen();
        keptId = pmConnectionId(conn.get());
        std::ostringstream sql;
        sql << "SET SESSION wait_timeout = " << PM6_WAIT_TIMEOUT_SEC;
        REQUIRE(conn->execute(sql.str().c_str()));
    }

    for (int round = 0; round < PM6_ROUNDS; ++round)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(PM6_ROUND_MS));
        DbFactory::maintainConnectionPools();
    }

    //取用前不校验：连接若已被服务器断开，下面的查询会重连、线程号随之改变，用例因此失败
    {
        std::unique_ptr<IDatabase> conn = pmOpen();
        CHECK(pmConnectionId(conn.get()) == keptId);
    }
    CHECK(scope.keepAlivePings() >= (uint64_t)(PM6_ROUNDS - 1));
    CHECK(scope.pingFailures() == 0);
}

TEST_CASE("PM7 clearing the pool closes the connections on the server side")
{
    if (!pmPrepare())
        return;
    PmPoolScope scope(pmOptions());

    uint64_t firstId = 0;
    uint64_t secondId = 0;
    {
        std::unique_ptr<IDatabase> first = pmOpen();
        std::unique_ptr<IDatabase> second = pmOpen();
        firstId = pmConnectionId(first.get());
        secondId = pmConnectionId(second.get());
    }
    REQUIRE(pmPool().idleCount() == 2);

    std::unique_ptr<CDatabaseMysql> admin = pmOpenAdmin();
    std::ostringstream sql;
    sql << "SELECT COUNT(*) FROM information_schema.PROCESSLIST WHERE ID IN (" << firstId << ", " << secondId << ")";
    uint64_t alive = 0;
    REQUIRE(pmQueryUInt64(admin.get(), sql.str().c_str(), alive));
    CHECK(alive == 2);

    DbFactory::clearConnectionPools();
    CHECK(pmPool().idleCount() == 0);

    //mysql_close 发出退出命令后立即返回，服务器回收线程稍有延迟，所以轮询等它消失
    int waitedMs = 0;
    while (waitedMs < PM7_WAIT_MS)
    {
        REQUIRE(pmQueryUInt64(admin.get(), sql.str().c_str(), alive));
        if (alive == 0)
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(PM7_POLL_MS));
        waitedMs += PM7_POLL_MS;
    }
    CHECK(alive == 0);
}

TEST_CASE("PM8 getInsertID stays correct on a reused connection")
{
    if (!pmPrepare())
        return;
    PmPoolScope scope(pmOptions());

    uint64_t firstConnId = 0;
    uint32_t firstInsertId = 0;
    {
        std::unique_ptr<IDatabase> conn = pmOpen();
        firstConnId = pmConnectionId(conn.get());
        REQUIRE(conn->execute("INSERT INTO t_pm_test(f_note) VALUES('first')"));
        firstInsertId = conn->getInsertID();
        CHECK(firstInsertId > 0);
    }

    {
        std::unique_ptr<IDatabase> conn = pmOpen();
        CHECK(pmConnectionId(conn.get()) == firstConnId);
        REQUIRE(conn->execute("INSERT INTO t_pm_test(f_note) VALUES('second')"));
        CHECK(conn->getInsertID() == firstInsertId + 1);
    }
}

TEST_CASE("PM10 a long-lived connection outside the pool recovers after being killed or timed out")
{
    if (!pmPrepare())
        return;

    //与 CMysqlThrd、CMysqlManager 的用法相同：直接持有一条 CDatabaseMysql，长期反复使用
    CDatabaseMysql conn;
    REQUIRE(conn.initialize(pmHost(), pmUser(), pmPwd(), PM_MYSQL_DB));

    SUBCASE("killed by the server")
    {
        uint64_t oldId = pmConnectionId(&conn);
        pmKill(oldId);
        uint64_t newId = 0;
        CHECK(pmQueryUInt64(&conn, "SELECT CONNECTION_ID()", newId));
        CHECK(newId != oldId);
    }

    SUBCASE("closed by wait_timeout")
    {
        uint64_t oldId = pmConnectionId(&conn);
        std::ostringstream sql;
        sql << "SET SESSION wait_timeout = " << PM5_WAIT_TIMEOUT_SEC;
        REQUIRE(conn.execute(sql.str().c_str()));
        std::this_thread::sleep_for(std::chrono::milliseconds(PM5_IDLE_MS));
        uint64_t newId = 0;
        CHECK(pmQueryUInt64(&conn, "SELECT CONNECTION_ID()", newId));
        CHECK(newId != oldId);
    }
}

//PM9 的计时：取 rounds 次连接，每次执行一条 SELECT 1 后归还或关闭，返回总耗时（毫秒）
static double pm9Run(int rounds)
{
    std::chrono::steady_clock::time_point begin = std::chrono::steady_clock::now();
    for (int i = 0; i < rounds; ++i)
    {
        std::unique_ptr<IDatabase> conn = pmOpen();
        uint64_t one = 0;
        REQUIRE(pmQueryUInt64(conn.get(), "SELECT 1", one));
    }
    std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::milli>(end - begin).count();
}

TEST_CASE("PM9 timing of connection setup with and without the pool")
{
    if (!pmPrepare())
        return;

    double withoutPoolMs = pm9Run(PM9_ROUNDS);

    double withPoolMs = 0;
    {
        DbHandlePool::Options opt;   //生产默认阈值：空闲 5 秒以上才校验，本用例里连续取用不会触发
        PmPoolScope scope(opt);
        withPoolMs = pm9Run(PM9_ROUNDS);
    }

    char line[256];
    snprintf(line, sizeof(line),
             "PM9 %d rounds of connect + SELECT 1: without pool %.1f ms (%.3f ms each), with pool %.1f ms (%.3f ms each)",
             PM9_ROUNDS, withoutPoolMs, withoutPoolMs / PM9_ROUNDS, withPoolMs, withPoolMs / PM9_ROUNDS);
    MESSAGE(line);
    CHECK(withPoolMs < withoutPoolMs);
}

#endif // FLAMINGO_NO_MYSQL
