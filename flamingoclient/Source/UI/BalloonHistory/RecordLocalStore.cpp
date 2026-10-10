/**
 *  录像记录本地库——SQLite 读写封装实现，RecordLocalStore.cpp
 *  设计与约定见 RecordLocalStore.h。
 *  balloonwj@qq.com   2026-10-05
 */
#include "stdafx.h"

#include "RecordLocalStore.h"
#include "sqlite3.h"

namespace balloonhistory {

//建表 SQL。路径唯一且不区分大小写（Windows 路径不区分大小写，同一文件只记一条）；
//路径以 UTF-16 存取（sqlite3_bind_text16 / sqlite3_column_text16），免去往返转码。
static const char* const kCreateTableSql =
    "CREATE TABLE IF NOT EXISTS t_record ("
    "id INTEGER PRIMARY KEY AUTOINCREMENT,"
    "path TEXT NOT NULL COLLATE NOCASE UNIQUE,"
    "duration_ms INTEGER NOT NULL DEFAULT 0,"
    "size_bytes INTEGER NOT NULL DEFAULT 0,"
    "end_time_ms INTEGER NOT NULL DEFAULT 0)";

//按路径查记录 id（添加时判断是覆盖还是新增）
static const char* const kFindByPathSql = "SELECT id FROM t_record WHERE path=?";

//覆盖已有记录的时长、大小与结束时间
static const char* const kUpdateInfoSql =
    "UPDATE t_record SET duration_ms=?, size_bytes=?, end_time_ms=? WHERE id=?";

//新增一条记录
static const char* const kInsertSql =
    "INSERT INTO t_record (path,duration_ms,size_bytes,end_time_ms) VALUES (?,?,?,?)";

//列出全部记录：最新的在前，结束时间相同时后添加的在前
static const char* const kListSql =
    "SELECT id,path,duration_ms,size_bytes,end_time_ms FROM t_record ORDER BY end_time_ms DESC, id DESC";

//修改路径
static const char* const kUpdatePathSql = "UPDATE t_record SET path=? WHERE id=?";

//删除一条记录
static const char* const kRemoveSql = "DELETE FROM t_record WHERE id=?";

RecordLocalEntry::RecordLocalEntry()
    : m_id(0),
      m_durationMs(0),
      m_sizeBytes(0),
      m_endTimeMs(0)
{
}

CRecordLocalStore& CRecordLocalStore::GetInstance()
{
    //函数内静态：首次使用时构造，避免与其它静态对象的初始化顺序纠缠
    static CRecordLocalStore s_instance;
    return s_instance;
}

CRecordLocalStore::CRecordLocalStore()
    : m_db(NULL)
{
}

std::wstring CRecordLocalStore::DbPathIn(const std::wstring& personalFolder)
{
    if (personalFolder.empty())
    {
        return std::wstring();
    }
    //与历史消息库 msgv2.db 同在个人目录的 ChatLog 子目录
    return personalFolder + L"ChatLog\\recordlocal.db";
}

CRecordLocalStore::~CRecordLocalStore()
{
    Close();
}

bool CRecordLocalStore::Open(const wchar_t* dbPathUtf16)
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

void CRecordLocalStore::Close()
{
    std::lock_guard<std::mutex> guard(m_mutex);
    if (m_db != NULL)
    {
        sqlite3_close(m_db);
        m_db = NULL;
    }
    m_dbPath.clear();
}

bool CRecordLocalStore::IsOpen() const
{
    //仅读取指针做"是否需要 Open"的粗判，不加锁可接受（与 CFileLocalStore 同口径）
    return m_db != NULL;
}

bool CRecordLocalStore::ExecLocked(const char* sql)
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

bool CRecordLocalStore::Add(const RecordLocalEntry& entry, int64_t* outId)
{
    if (entry.m_path.empty())
    {
        return false;
    }

    std::lock_guard<std::mutex> guard(m_mutex);
    if (m_db == NULL)
    {
        return false;
    }

    //先按路径查：同一文件已有记录就覆盖它，id 保持不变
    int64_t existingId = 0;
    sqlite3_stmt* stmt = NULL;
    if (sqlite3_prepare_v2(m_db, kFindByPathSql, -1, &stmt, NULL) != SQLITE_OK)
    {
        return false;
    }
    sqlite3_bind_text16(stmt, 1, entry.m_path.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(stmt) == SQLITE_ROW)
    {
        existingId = sqlite3_column_int64(stmt, 0);
    }
    sqlite3_finalize(stmt);
    stmt = NULL;

    if (existingId > 0)
    {
        if (sqlite3_prepare_v2(m_db, kUpdateInfoSql, -1, &stmt, NULL) != SQLITE_OK)
        {
            return false;
        }
        sqlite3_bind_int64(stmt, 1, entry.m_durationMs);
        sqlite3_bind_int64(stmt, 2, entry.m_sizeBytes);
        sqlite3_bind_int64(stmt, 3, entry.m_endTimeMs);
        sqlite3_bind_int64(stmt, 4, existingId);
        const int rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        if (rc != SQLITE_DONE)
        {
            return false;
        }
        if (outId != NULL)
        {
            *outId = existingId;
        }
        return true;
    }

    if (sqlite3_prepare_v2(m_db, kInsertSql, -1, &stmt, NULL) != SQLITE_OK)
    {
        return false;
    }
    sqlite3_bind_text16(stmt, 1, entry.m_path.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 2, entry.m_durationMs);
    sqlite3_bind_int64(stmt, 3, entry.m_sizeBytes);
    sqlite3_bind_int64(stmt, 4, entry.m_endTimeMs);
    const int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE)
    {
        return false;
    }
    if (outId != NULL)
    {
        *outId = sqlite3_last_insert_rowid(m_db);
    }
    return true;
}

bool CRecordLocalStore::List(std::vector<RecordLocalEntry>& out)
{
    out.clear();

    std::lock_guard<std::mutex> guard(m_mutex);
    if (m_db == NULL)
    {
        return false;
    }

    sqlite3_stmt* stmt = NULL;
    if (sqlite3_prepare_v2(m_db, kListSql, -1, &stmt, NULL) != SQLITE_OK)
    {
        return false;
    }
    int rc = sqlite3_step(stmt);
    while (rc == SQLITE_ROW)
    {
        RecordLocalEntry e;
        e.m_id = sqlite3_column_int64(stmt, 0);
        const void* path = sqlite3_column_text16(stmt, 1);
        if (path != NULL)
        {
            e.m_path.assign(static_cast<const wchar_t*>(path));
        }
        e.m_durationMs = sqlite3_column_int64(stmt, 2);
        e.m_sizeBytes = sqlite3_column_int64(stmt, 3);
        e.m_endTimeMs = sqlite3_column_int64(stmt, 4);
        out.push_back(e);
        rc = sqlite3_step(stmt);
    }
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

bool CRecordLocalStore::UpdatePath(int64_t id, const std::wstring& newPath)
{
    if (newPath.empty())
    {
        return false;
    }

    std::lock_guard<std::mutex> guard(m_mutex);
    if (m_db == NULL)
    {
        return false;
    }

    sqlite3_stmt* stmt = NULL;
    if (sqlite3_prepare_v2(m_db, kUpdatePathSql, -1, &stmt, NULL) != SQLITE_OK)
    {
        return false;
    }
    sqlite3_bind_text16(stmt, 1, newPath.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 2, id);
    //新路径已被另一条记录占用时违反唯一约束，sqlite3_step 返回 SQLITE_CONSTRAINT
    const int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE && sqlite3_changes(m_db) == 1;
}

bool CRecordLocalStore::Remove(int64_t id)
{
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
    sqlite3_bind_int64(stmt, 1, id);
    const int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    //id 不存在时 sqlite3_step 同样返回 DONE，以受影响的行数区分
    return rc == SQLITE_DONE && sqlite3_changes(m_db) == 1;
}

} // namespace balloonhistory
