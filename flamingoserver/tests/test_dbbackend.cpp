/**
 *  测试用的数据库后端切换实现，test_dbbackend.cpp
 *  约定见 test_dbbackend.h。
 *  balloonwj@qq.com   2026-08-12
 */
#include "test_dbbackend.h"

#include <stdio.h>
#include <stdlib.h>
#include <memory>
#include <sstream>
#include <string>

#include "doctest.h"
#include "../dbapi/DbConfig.h"
#include "../dbapi/IDatabase.h"
#include "../dbapi/DbFactory.h"
#include "../sqliteapi/SqliteManager.h"
#include "../base/Singleton.h"

//MySQL 模式只在带 MySQL 后端的构建目标里可用。默认的 flamingo_tests 带
//FLAMINGO_NO_MYSQL 编，连 CMysqlManager 都不该编进去。
#ifndef FLAMINGO_NO_MYSQL
#include "../mysqlmgr/MysqlManager.h"
#endif

namespace {

//读环境变量，空则取默认值。
const char* envOr(const char* name, const char* fallback)
{
    const char* v = ::getenv(name);
    if (v == NULL || v[0] == '\0')
    {
        return fallback;
    }
    return v;
}

} // anonymous namespace

namespace testdb {

bool useMysql()
{
    const char* v = ::getenv("FLAMINGO_TEST_MYSQL");
    return (v != NULL && v[0] == '1' && v[1] == '\0');
}

void resetDb(const char* sqliteFile, const char* mysqlDb)
{
    if (!useMysql())
    {
        //SQLite：删文件重建。CSqliteManager::init 会把空文件建成完整结构库
        //（建表 + 补列 + 建索引与触发器）。
        cleanupDb(sqliteFile);
        REQUIRE(Singleton<CSqliteManager>::Instance().init(sqliteFile));
        Singleton<CDbConfig>::Instance().init(DB_TYPE_NAME_SQLITE, NULL, NULL, NULL,
                                              NULL, sqliteFile);
        return;
    }

#ifdef FLAMINGO_NO_MYSQL
    //构建时没编 MySQL 后端却要求跑 MySQL：直接失败，而不是静默退回 SQLite ——
    //"以为测了 MySQL、其实测的是 SQLite"比测不了更糟。
    FAIL("FLAMINGO_TEST_MYSQL=1 但本构建不含 MySQL 后端，请用 flamingo_tests_mysql 目标");
    (void)mysqlDb;
#else
    const char* host = envOr("FLAMINGO_TEST_MYSQL_HOST", "127.0.0.1");
    const char* user = envOr("FLAMINGO_TEST_MYSQL_USER", "root");
    const char* pwd  = envOr("FLAMINGO_TEST_MYSQL_PWD",  "123456");

    //先把整个测试库 DROP 掉。连的是 MySQL 自带的 mysql 库 —— 要删的那个库此刻
    //可能不存在，连它自己会失败。
    {
        Singleton<CDbConfig>::Instance().init(DB_TYPE_NAME_MYSQL, host, user, pwd,
                                              "mysql", NULL);
        std::unique_ptr<IDatabase> pConn(DbFactory::createConnection());
        REQUIRE(pConn);

        std::ostringstream os;
        os << "DROP DATABASE IF EXISTS " << mysqlDb;
        REQUIRE(pConn->execute(os.str().c_str()));
    }

    //再由产品自己的建库路径把库与表建出来 —— 这样"新建库的表结构对不对"
    //（列、索引是否与 MysqlManager.cpp 里那份定义一致）也一并被测到。
    REQUIRE(Singleton<CMysqlManager>::Instance().init(host, user, pwd, mysqlDb));

    //最后把 CDbConfig 指向测试库，后续 DbFactory 造出来的连接才落在它上面。
    Singleton<CDbConfig>::Instance().init(DB_TYPE_NAME_MYSQL, host, user, pwd,
                                          mysqlDb, NULL);
#endif
}

void cleanupDb(const char* sqliteFile)
{
    if (useMysql())
    {
        //MySQL 模式留着库便于事后排查，下次 resetDb 会整个重建。
        return;
    }

    std::string basePath(sqliteFile);
    remove(basePath.c_str());
    remove((basePath + "-wal").c_str());
    remove((basePath + "-shm").c_str());
}

} // namespace testdb
