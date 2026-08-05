/**
 *  CSqliteManager 的实现，详见 SqliteManager.h。
 *  balloonwj@qq.com   2026-07-31
 */
#include "SqliteManager.h"

#include <memory>
#include <sstream>

#include "../base/AsyncLog.h"
#include "../dbapi/IDatabase.h"
#include "DatabaseSqlite.h"
#include "SqliteSchema.h"

//PRAGMA table_info 结果集里列名所在的列下标。
//该 PRAGMA 每行的列依次为：cid、name、type、notnull、dflt_value、pk
#define TABLE_INFO_COLUMN_NAME_INDEX    1

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

    //顺序固定为 建表 -> 补列 -> 建索引与触发器，不能调换：
    //结构落后的存量库里，索引要用到的列可能还没补上，先建索引会直接报 "no such column"
    if (!execStatements(pConn.get(), sqliteapi::SCHEMA_TABLE_STATEMENTS,
                        sqliteapi::SCHEMA_TABLE_STATEMENT_COUNT, "create table"))
    {
        return false;
    }

    if (!fillMissingColumns(pConn.get()))
        return false;

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

bool CSqliteManager::loadTableColumns(IDatabase* pConn, const char* tableName,
                                      std::set<std::string>& outColumns)
{
    outColumns.clear();

    //表名来自生成出来的常量表，不含任何外部输入，这里直接拼进 SQL 是安全的
    //（PRAGMA 的参数本来也不支持绑定变量）
    std::ostringstream os;
    os << "PRAGMA table_info(" << tableName << ")";

    QueryResult* pResult = pConn->query(os.str());
    if (pResult == NULL)
    {
        //表不存在时 PRAGMA table_info 返回空结果集而非报错，走不到这里；
        //真到这里说明是查询本身出了问题
        LOGE("CSqliteManager::loadTableColumns failed, table: %s", tableName);
        return false;
    }

    while (true)
    {
        Field* pRow = pResult->fetch();
        if (pRow == NULL)
            break;

        outColumns.insert(pRow[TABLE_INFO_COLUMN_NAME_INDEX].getString());

        if (!pResult->nextRow())
            break;
    }

    delete pResult;
    return true;
}

bool CSqliteManager::fillMissingColumns(IDatabase* pConn)
{
    std::string currentTable;
    std::set<std::string> existingColumns;
    int addedCount = 0;

    for (int i = 0; i < sqliteapi::SCHEMA_COLUMN_COUNT; i++)
    {
        const sqliteapi::SchemaColumn& column = sqliteapi::SCHEMA_COLUMNS[i];

        //SCHEMA_COLUMNS 是按表分组排列的，只在换表时重新取一次列清单。
        //否则 245 个列就要发 245 条 PRAGMA，启动时白白多出几百次查询
        if (currentTable != column.m_szTable)
        {
            currentTable = column.m_szTable;
            if (!loadTableColumns(pConn, column.m_szTable, existingColumns))
                return false;
        }

        if (existingColumns.find(column.m_szColumn) != existingColumns.end())
            continue;

        std::ostringstream os;
        os << "ALTER TABLE " << column.m_szTable << " ADD COLUMN "
           << column.m_szColumn << " " << column.m_szAddClause;

        std::string sql = os.str();
        if (!pConn->execute(sql.c_str()))
        {
            LOGE("CSqliteManager::fillMissingColumns failed, sql: %s", sql.c_str());
            return false;
        }

        //补列是结构升级的关键动作，必须留痕，否则出问题时无从判断库是什么时候变的
        LOGI("CSqliteManager::fillMissingColumns: added %s.%s",
             column.m_szTable, column.m_szColumn);
        existingColumns.insert(column.m_szColumn);
        addedCount++;
    }

    if (addedCount > 0)
    {
        LOGI("CSqliteManager::fillMissingColumns ok, columns added: %d", addedCount);
    }

    return true;
}
