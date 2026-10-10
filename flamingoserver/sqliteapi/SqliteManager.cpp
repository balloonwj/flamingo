/**
 *  CSqliteManager 的实现，详见 SqliteManager.h。
 *  balloonwj@qq.com   2026-07-31
 */
#include "SqliteManager.h"

#include <memory>

#include "../base/AsyncLog.h"
#include "../dbapi/IDatabase.h"
#include "DatabaseSqlite.h"
#include "SqliteSchema.h"

CSqliteManager::CSqliteManager(void)
{
}

CSqliteManager::~CSqliteManager(void)
{
}

bool CSqliteManager::init(const char* dbPath)
{
    if (dbPath == NULL || dbPath[0] == '\0')
    {
        LOGE("CSqliteManager::init failed, db path is empty");
        return false;
    }

    m_strDbPath = dbPath;

    //这里直接用具体实现而不走 DbFactory：本函数是 SQLite 专属的引导流程，
    //跟 CMysqlManager 直接使用 CDatabaseMysql 是一个道理
    std::unique_ptr<CDatabaseSqlite> pConn(new CDatabaseSqlite());
    if (!pConn->initialize(m_strDbPath))
    {
        LOGE("CSqliteManager::init failed, cannot open db file: %s", m_strDbPath.c_str());
        return false;
    }

    //先建表，再建索引与触发器（它们建在表上）。语句都是 IF NOT EXISTS 形式，已有的表不做任何改动：
    //自 2026-10-09 起启动时不再给已有的表补列，表结构有变化时，已有的库须手工修改或重建
    if (!execStatements(pConn.get(), sqliteapi::SCHEMA_TABLE_STATEMENTS,
                        sqliteapi::SCHEMA_TABLE_STATEMENT_COUNT, "create table"))
    {
        return false;
    }

    if (!execStatements(pConn.get(), sqliteapi::SCHEMA_POST_STATEMENTS,
                        sqliteapi::SCHEMA_POST_STATEMENT_COUNT, "create index/trigger"))
    {
        return false;
    }

    LOGI("CSqliteManager::init ok, db path: %s, tables: %d",
         m_strDbPath.c_str(), sqliteapi::SCHEMA_TABLE_COUNT);
    return true;
}

bool CSqliteManager::execStatements(IDatabase* pConn, const char* const statements[],
                                    int count, const char* stageName)
{
    for (int i = 0; i < count; i++)
    {
        if (!pConn->execute(statements[i]))
        {
            //建库失败属于致命问题：结构不全的话业务跑起来只会到处报错。
            //这里必须把出错的语句原文打出来，否则很难定位是哪一条
            LOGE("CSqliteManager schema stage \"%s\" failed at statement %d/%d, sql: %s",
                 stageName, i + 1, count, statements[i]);
            return false;
        }
    }

    LOGI("CSqliteManager schema stage \"%s\" ok, statements executed: %d", stageName, count);
    return true;
}
