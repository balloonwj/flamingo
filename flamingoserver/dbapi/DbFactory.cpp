/**
 *  DbFactory 的实现，详见 DbFactory.h。
 *  balloonwj@qq.com   2026-07-31
 */
#include "DbFactory.h"

#include "DbConfig.h"
#include "IDatabase.h"
#include "../base/AsyncLog.h"
#include "../base/Singleton.h"
#include "../sqliteapi/DatabaseSqlite.h"

//FLAMINGO_NO_MYSQL 由 CMake 在 -DWITH_MYSQL=OFF 时定义，用于编出一份
//不依赖 libmysqlclient 的纯本地版本，见 CMakeLists.txt 顶部的说明
#ifndef FLAMINGO_NO_MYSQL
#include "../mysqlapi/DatabaseMysql.h"
#endif

#include <atomic>
#include <stdio.h>

//连接池是否打开，见 DbFactory::setConnectionPoolEnabled。
//各 I/O 线程在 createConnection 里读它，主线程在启动阶段写它，所以用原子变量
static std::atomic<bool> s_bPoolEnabled(false);

//连接池对象是否已经创建过。只有创建过，maintainConnectionPools / clearConnectionPools /
//describeConnectionPools 才去访问它们 —— 否则会在工作线程里第一次调用 Singleton<>::Instance()，
//而它的创建不是线程安全的
static std::atomic<bool> s_bPoolCreated(false);

void DbFactory::setConnectionPoolEnabled(bool enabled)
{
    //两个后端的池都在这里由调用方线程（启动阶段的主线程）创建，理由见 s_bPoolCreated
#ifndef FLAMINGO_NO_MYSQL
    Singleton<CMysqlHandlePool>::Instance().setEnabled(enabled);
#endif
    Singleton<CSqliteHandlePool>::Instance().setEnabled(enabled);
    s_bPoolCreated = true;
    s_bPoolEnabled = enabled;
    LOGI("db connection pool %s", enabled ? "enabled" : "disabled");
}

bool DbFactory::isConnectionPoolEnabled()
{
    return s_bPoolEnabled;
}

void DbFactory::maintainConnectionPools()
{
    if (!s_bPoolCreated)
        return;

#ifndef FLAMINGO_NO_MYSQL
    Singleton<CMysqlHandlePool>::Instance().maintain();
#endif
    Singleton<CSqliteHandlePool>::Instance().maintain();
}

void DbFactory::clearConnectionPools()
{
    if (!s_bPoolCreated)
        return;

#ifndef FLAMINGO_NO_MYSQL
    Singleton<CMysqlHandlePool>::Instance().clear();
#endif
    Singleton<CSqliteHandlePool>::Instance().clear();
}

//生成一个后端连接池的统计描述。name 为后端名（mysql / sqlite），pool 为该后端的池
static std::string describePool(const char* name, const DbHandlePool& pool)
{
    DbHandlePool::Stats stats = pool.getStats();
    char text[512];
    snprintf(text, sizeof(text),
             "(%s) %s: idle %u, hits %llu, misses %llu, validate pings %llu, "
             "keep-alive pings %llu, ping failures %llu, closed overflow %llu, closed idle %llu",
             name, pool.isEnabled() ? "enabled" : "disabled",
             (unsigned int)pool.idleCount(),
             (unsigned long long)stats.m_acquireHits, (unsigned long long)stats.m_acquireMisses,
             (unsigned long long)stats.m_validatePings, (unsigned long long)stats.m_keepAlivePings,
             (unsigned long long)stats.m_pingFailures, (unsigned long long)stats.m_closedOverflow,
             (unsigned long long)stats.m_closedIdle);
    return text;
}

std::string DbFactory::describeConnectionPools()
{
    if (!s_bPoolCreated)
        return "db connection pool not created";

    std::string text = "db connection pool";
#ifndef FLAMINGO_NO_MYSQL
    text += " " + describePool("mysql", Singleton<CMysqlHandlePool>::Instance()) + ";";
#endif
    text += " " + describePool("sqlite", Singleton<CSqliteHandlePool>::Instance());
    return text;
}

IDatabase* DbFactory::createConnection()
{
    CDbConfig& config = Singleton<CDbConfig>::Instance();

    switch (config.getDbType())
    {
    case DB_TYPE_MYSQL:
#ifdef FLAMINGO_NO_MYSQL
        //本次构建关掉了 MySQL 后端，只能用 dbtype=sqlite。
        //明确报错而不是静默改用 SQLite：配置写的是 mysql 却连到了别的库，问题更难查
        LOGE("create db connection failed, this build has no mysql backend "
             "(built with WITH_MYSQL=OFF), please set dbtype=sqlite, config: %s",
             config.describe().c_str());
        return NULL;
#else
        {
            CDatabaseMysql* pConn = new CDatabaseMysql();
            if (!pConn->initialize(config.getHost(), config.getUser(),
                                   config.getPassword(), config.getDbName(), s_bPoolEnabled))
            {
                //连接失败的详细原因（错误码、错误文本）已由 CDatabaseMysql::initialize 打过日志，
                //这里不再重复打印，只负责回收对象
                delete pConn;
                return NULL;
            }

            return pConn;
        }
#endif
    case DB_TYPE_SQLITE:
        {
            CDatabaseSqlite* pConn = new CDatabaseSqlite();
            if (!pConn->initialize(config.getDbPath(), s_bPoolEnabled))
            {
                //打开失败的详细原因已由 CDatabaseSqlite::initialize 打过日志
                delete pConn;
                return NULL;
            }

            return pConn;
        }
    default:
        LOGE("create db connection failed, unknown db type: %d, config: %s",
             (int)config.getDbType(), config.describe().c_str());
        return NULL;
    }
}
