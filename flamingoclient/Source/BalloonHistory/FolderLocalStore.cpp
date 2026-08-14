/**
 *  文件夹本地落点登记库——SQLite 读写封装实现，FolderLocalStore.cpp
 *  设计与约定见 FolderLocalStore.h。写法与同目录的 FileLocalStore 一致，只是键从
 *  (md5, 文件名) 换成了会话 id。
 *  balloonwj@qq.com   2026-08-14
 */
#include "FolderLocalStore.h"
#include "sqlite3.h"

namespace balloonhistory {

//建表 SQL。sessionid 作主键：一次传输在本机只有一个根目录，重复登记即覆盖。
//localroot 以 UTF-16 存（sqlite3_bind_text16），取出时直接就是宽字符路径，免去往返转码。
static const char* const kCreateTableSql =
    "CREATE TABLE IF NOT EXISTS t_folder_local ("
    "sessionid TEXT NOT NULL,"
    "localroot TEXT NOT NULL,"
    "updatetime INTEGER NOT NULL DEFAULT 0,"
    "PRIMARY KEY (sessionid))";

//登记 / 覆盖一条落点。INSERT OR REPLACE 依赖上面的主键实现"存在即覆盖"。
static const char* const kUpsertSql =
    "INSERT OR REPLACE INTO t_folder_local (sessionid,localroot,updatetime) VALUES (?,?,?)";

//按会话 id 取落点。
static const char* const kQuerySql =
    "SELECT localroot FROM t_folder_local WHERE sessionid=?";

//按会话 id 删除一条落点。
static const char* const kRemoveSql =
    "DELETE FROM t_folder_local WHERE sessionid=?";

CFolderLocalStore& CFolderLocalStore::GetInstance()
{
    //函数内静态：首次使用时构造，避免与其它静态对象的初始化顺序纠缠。
    static CFolderLocalStore s_instance;
    return s_instance;
}

CFolderLocalStore::CFolderLocalStore()
    : m_db(NULL)
{
}

CFolderLocalStore::~CFolderLocalStore()
{
    Close();
}

bool CFolderLocalStore::Open(const wchar_t* dbPathUtf16)
{
    if (dbPathUtf16 == NULL)
    {
        return false;
    }

    std::lock_guard<std::mutex> guard(m_mutex);

    //同一个库重复 Open 直接成功；换库（账号切换）先关掉旧的，免得写进上一个账号的库
    if (m_db != NULL)
    {
        if (m_dbPath == dbPathUtf16)
        {
            return true;
        }
        sqlite3_close(m_db);
        m_db = NULL;
        m_dbPath.clear();
    }

    sqlite3* db = NULL;
    if (sqlite3_open16(dbPathUtf16, &db) != SQLITE_OK)
    {
        if (db != NULL)
        {
            sqlite3_close(db);
        }
        return false;
    }
    m_db = db;

    //建表失败则回退到未打开态，避免半初始化后续每次读写都失败
    if (!ExecLocked(kCreateTableSql))
    {
        sqlite3_close(m_db);
        m_db = NULL;
        return false;
    }
    m_dbPath = dbPathUtf16;
    return true;
}

void CFolderLocalStore::Close()
{
    std::lock_guard<std::mutex> guard(m_mutex);
    if (m_db != NULL)
    {
        sqlite3_close(m_db);
        m_db = NULL;
    }
    m_dbPath.clear();
}

bool CFolderLocalStore::IsOpen() const
{
    //仅读取指针做"是否需要 Open"的粗判，不加锁可接受（与 CFileLocalStore 同口径）。
    return m_db != NULL;
}

bool CFolderLocalStore::ExecLocked(const char* sql)
{
    if (m_db == NULL || sql == NULL)
    {
        return false;
    }
    char* err = NULL;
    int rc = sqlite3_exec(m_db, sql, NULL, NULL, &err);
    if (err != NULL)
    {
        sqlite3_free(err);
    }
    return rc == SQLITE_OK;
}

bool CFolderLocalStore::Upsert(const std::string& sessionId, const std::wstring& localRoot)
{
    //两者缺一这条登记就没有意义：查不回来，或查回来是空路径
    if (sessionId.empty() || localRoot.empty())
    {
        return false;
    }

    std::lock_guard<std::mutex> guard(m_mutex);
    if (m_db == NULL)
    {
        return false;
    }

    sqlite3_stmt* stmt = NULL;
    if (sqlite3_prepare_v2(m_db, kUpsertSql, -1, &stmt, NULL) != SQLITE_OK)
    {
        return false;
    }
    //文本字段用 SQLITE_TRANSIENT：让 SQLite 立即拷贝，避免依赖临时串生命周期。
    sqlite3_bind_text(stmt, 1, sessionId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text16(stmt, 2, localRoot.c_str(), -1, SQLITE_TRANSIENT);
    //登记时间仅作诊断与将来的清理策略之用，当前不参与查询
    sqlite3_bind_int64(stmt, 3, 0);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

bool CFolderLocalStore::Query(const std::string& sessionId, std::wstring& outPath)
{
    outPath.clear();
    if (sessionId.empty())
    {
        return false;
    }

    std::lock_guard<std::mutex> guard(m_mutex);
    if (m_db == NULL)
    {
        return false;
    }

    sqlite3_stmt* stmt = NULL;
    if (sqlite3_prepare_v2(m_db, kQuerySql, -1, &stmt, NULL) != SQLITE_OK)
    {
        return false;
    }
    sqlite3_bind_text(stmt, 1, sessionId.c_str(), -1, SQLITE_TRANSIENT);

    bool hit = false;
    if (sqlite3_step(stmt) == SQLITE_ROW)
    {
        const void* text = sqlite3_column_text16(stmt, 0);
        if (text != NULL)
        {
            outPath.assign(static_cast<const wchar_t*>(text));
            hit = !outPath.empty();
        }
    }
    sqlite3_finalize(stmt);
    return hit;
}

bool CFolderLocalStore::Remove(const std::string& sessionId)
{
    if (sessionId.empty())
    {
        return false;
    }

    std::lock_guard<std::mutex> guard(m_mutex);
    if (m_db == NULL)
    {
        return false;
    }

    sqlite3_stmt* stmt = NULL;
    if (sqlite3_prepare_v2(m_db, kRemoveSql, -1, &stmt, NULL) != SQLITE_OK)
    {
        return false;
    }
    sqlite3_bind_text(stmt, 1, sessionId.c_str(), -1, SQLITE_TRANSIENT);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    //无匹配行时 sqlite3_step 同样返回 DONE，视为成功（幂等删除）
    return rc == SQLITE_DONE;
}

} // namespace balloonhistory
