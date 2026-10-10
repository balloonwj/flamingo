/**
 *  文件共享的本机存储 CShareLocalStore 的实现，见 ShareLocalStore.h。
 *  balloonwj@qq.com   2026-10-02
 */
#include "stdafx.h"

#include "ShareLocalStore.h"

#include <windows.h>
#include "sqlite3.h"

namespace fileshare {

namespace {

//建表语句。t_share_local 以 clientshareid 为主键（发起时还没有 shareid），shareid 另建索引；
//t_share_download 以 (shareid, relpath) 为主键；t_share_activity 自增主键，按 id 倒序即最新在前
const char* const kCreateTablesSql =
    "CREATE TABLE IF NOT EXISTS t_share_local ("
    "  clientshareid TEXT PRIMARY KEY,"
    "  shareid INTEGER NOT NULL DEFAULT 0,"
    "  rootpath TEXT NOT NULL,"
    "  kind INTEGER NOT NULL,"
    "  includehidden INTEGER NOT NULL DEFAULT 0,"
    "  createtime INTEGER NOT NULL DEFAULT 0);"
    "CREATE INDEX IF NOT EXISTS idx_share_local_shareid ON t_share_local(shareid);"
    "CREATE TABLE IF NOT EXISTS t_share_download ("
    "  shareid INTEGER NOT NULL,"
    "  relpath TEXT NOT NULL,"
    "  srcsize INTEGER NOT NULL,"
    "  srcmtime INTEGER NOT NULL,"
    "  localpath TEXT NOT NULL,"
    "  offset INTEGER NOT NULL DEFAULT 0,"
    "  updatetime INTEGER NOT NULL DEFAULT 0,"
    "  PRIMARY KEY (shareid, relpath));"
    "CREATE TABLE IF NOT EXISTS t_share_activity ("
    "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
    "  shareid INTEGER NOT NULL,"
    "  visitorid INTEGER NOT NULL,"
    "  action INTEGER NOT NULL,"
    "  path TEXT NOT NULL,"
    "  result INTEGER NOT NULL DEFAULT 0,"
    "  time INTEGER NOT NULL DEFAULT 0);";

//t_share_local 查询的列，按 FillShare 的下标顺序
const char* const kShareColumns = "SELECT clientshareid, shareid, rootpath, kind, includehidden, createtime FROM t_share_local ";

//UTF-16 转 UTF-8
std::string ToUtf8(const std::wstring& text)
{
    if (text.empty())
        return std::string();
    const int len = ::WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), NULL, 0, NULL, NULL);
    std::string out(static_cast<size_t>(len > 0 ? len : 0), '\0');
    if (len > 0)
        ::WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), &out[0], len, NULL, NULL);
    return out;
}

//UTF-8 转 UTF-16
std::wstring ToWide(const char* text)
{
    if (text == NULL || text[0] == '\0')
        return std::wstring();
    const int len = ::MultiByteToWideChar(CP_UTF8, 0, text, -1, NULL, 0);
    if (len <= 1)
        return std::wstring();
    std::wstring out(static_cast<size_t>(len - 1), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, text, -1, &out[0], len);
    return out;
}

//取一列文本（NULL 时为空串）
std::string ColumnText(sqlite3_stmt* stmt, int col)
{
    const unsigned char* p = sqlite3_column_text(stmt, col);
    return p != NULL ? std::string(reinterpret_cast<const char*>(p)) : std::string();
}

//把 t_share_local 查询结果的当前行填进记录（列顺序见 kShareColumns）
void FillShare(sqlite3_stmt* stmt, ShareLocalRecord& rec)
{
    rec.m_clientShareId = ColumnText(stmt, 0);
    rec.m_shareId = sqlite3_column_int64(stmt, 1);
    rec.m_rootPath = ToWide(ColumnText(stmt, 2).c_str());
    rec.m_kind = sqlite3_column_int(stmt, 3);
    rec.m_includeHidden = (sqlite3_column_int(stmt, 4) != 0);
    rec.m_createTime = sqlite3_column_int64(stmt, 5);
}

//绑定一个 UTF-8 文本参数（SQLite 自行拷贝）
void BindText(sqlite3_stmt* stmt, int index, const std::string& text)
{
    sqlite3_bind_text(stmt, index, text.c_str(), static_cast<int>(text.size()), SQLITE_TRANSIENT);
}

//执行一条已绑定参数、不取结果的语句并释放；返回是否成功
bool StepAndFinalize(sqlite3_stmt* stmt)
{
    const int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

} // namespace

CShareLocalStore::CShareLocalStore()
    : m_db(NULL)
    , m_activityKeep(kShareActivityKeep)
{
}

CShareLocalStore::~CShareLocalStore()
{
    Close();
}

bool CShareLocalStore::Open(const wchar_t* dbPath)
{
    if (dbPath == NULL)
        return false;
    Close();

    std::lock_guard<std::mutex> guard(m_mutex);
    if (sqlite3_open16(dbPath, &m_db) != SQLITE_OK)
    {
        if (m_db != NULL)
            sqlite3_close(m_db);
        m_db = NULL;
        return false;
    }
    if (!CreateTables())
    {
        sqlite3_close(m_db);
        m_db = NULL;
        return false;
    }
    return true;
}

void CShareLocalStore::Close()
{
    std::lock_guard<std::mutex> guard(m_mutex);
    if (m_db != NULL)
    {
        sqlite3_close(m_db);
        m_db = NULL;
    }
}

bool CShareLocalStore::IsOpen() const
{
    std::lock_guard<std::mutex> guard(m_mutex);
    return m_db != NULL;
}

void CShareLocalStore::SetActivityKeep(size_t keep)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    m_activityKeep = (keep < 1) ? 1 : keep;
}

bool CShareLocalStore::CreateTables()
{
    return ExecLocked(kCreateTablesSql);
}

bool CShareLocalStore::ExecLocked(const char* sql)
{
    if (m_db == NULL)
        return false;
    char* err = NULL;
    const int rc = sqlite3_exec(m_db, sql, NULL, NULL, &err);
    if (err != NULL)
        sqlite3_free(err);
    return rc == SQLITE_OK;
}

bool CShareLocalStore::AddPendingShare(const std::string& clientShareId, const std::wstring& rootPath, int kind,
                                       bool includeHidden, int64_t createTime)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    if (m_db == NULL)
        return false;
    sqlite3_stmt* stmt = NULL;
    const char* sql = "INSERT OR REPLACE INTO t_share_local(clientshareid, shareid, rootpath, kind, includehidden, createtime) "
                      "VALUES(?, 0, ?, ?, ?, ?)";
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, NULL) != SQLITE_OK)
        return false;
    BindText(stmt, 1, clientShareId);
    BindText(stmt, 2, ToUtf8(rootPath));
    sqlite3_bind_int(stmt, 3, kind);
    sqlite3_bind_int(stmt, 4, includeHidden ? 1 : 0);
    sqlite3_bind_int64(stmt, 5, createTime);
    return StepAndFinalize(stmt);
}

bool CShareLocalStore::BindShareId(const std::string& clientShareId, int64_t shareId)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    if (m_db == NULL)
        return false;
    sqlite3_stmt* stmt = NULL;
    if (sqlite3_prepare_v2(m_db, "UPDATE t_share_local SET shareid = ? WHERE clientshareid = ?", -1, &stmt, NULL) != SQLITE_OK)
        return false;
    sqlite3_bind_int64(stmt, 1, shareId);
    BindText(stmt, 2, clientShareId);
    if (!StepAndFinalize(stmt))
        return false;
    return sqlite3_changes(m_db) > 0;
}

bool CShareLocalStore::QueryShareLocked(const char* whereSql, const std::string* textKey, int64_t intKey,
                                        ShareLocalRecord& out)
{
    if (m_db == NULL)
        return false;
    const std::string sql = std::string(kShareColumns) + whereSql;
    sqlite3_stmt* stmt = NULL;
    if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, NULL) != SQLITE_OK)
        return false;
    if (textKey != NULL)
        BindText(stmt, 1, *textKey);
    else
        sqlite3_bind_int64(stmt, 1, intKey);
    const bool found = (sqlite3_step(stmt) == SQLITE_ROW);
    if (found)
        FillShare(stmt, out);
    sqlite3_finalize(stmt);
    return found;
}

bool CShareLocalStore::GetByShareId(int64_t shareId, ShareLocalRecord& out)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    //还没拿到 shareid 的记录 shareid 为 0，不能按 0 查
    if (shareId <= 0)
        return false;
    return QueryShareLocked("WHERE shareid = ?", NULL, shareId, out);
}

bool CShareLocalStore::GetByClientShareId(const std::string& clientShareId, ShareLocalRecord& out)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    return QueryShareLocked("WHERE clientshareid = ?", &clientShareId, 0, out);
}

bool CShareLocalStore::ListShares(std::vector<ShareLocalRecord>& out)
{
    out.clear();
    std::lock_guard<std::mutex> guard(m_mutex);
    if (m_db == NULL)
        return false;
    const std::string sql = std::string(kShareColumns) + "ORDER BY createtime, clientshareid";
    sqlite3_stmt* stmt = NULL;
    if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, NULL) != SQLITE_OK)
        return false;
    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        ShareLocalRecord rec;
        FillShare(stmt, rec);
        out.push_back(rec);
    }
    sqlite3_finalize(stmt);
    return true;
}

bool CShareLocalStore::RemoveShare(int64_t shareId)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    if (m_db == NULL || shareId <= 0)
        return false;
    sqlite3_stmt* stmt = NULL;
    if (sqlite3_prepare_v2(m_db, "DELETE FROM t_share_local WHERE shareid = ?", -1, &stmt, NULL) != SQLITE_OK)
        return false;
    sqlite3_bind_int64(stmt, 1, shareId);
    return StepAndFinalize(stmt);
}

bool CShareLocalStore::RemovePendingShare(const std::string& clientShareId)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    if (m_db == NULL)
        return false;
    sqlite3_stmt* stmt = NULL;
    if (sqlite3_prepare_v2(m_db, "DELETE FROM t_share_local WHERE clientshareid = ?", -1, &stmt, NULL) != SQLITE_OK)
        return false;
    BindText(stmt, 1, clientShareId);
    return StepAndFinalize(stmt);
}

bool CShareLocalStore::SaveDownloadProgress(const ShareDownloadRecord& rec)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    if (m_db == NULL)
        return false;
    sqlite3_stmt* stmt = NULL;
    const char* sql = "INSERT OR REPLACE INTO t_share_download(shareid, relpath, srcsize, srcmtime, localpath, offset, updatetime) "
                      "VALUES(?, ?, ?, ?, ?, ?, ?)";
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, NULL) != SQLITE_OK)
        return false;
    sqlite3_bind_int64(stmt, 1, rec.m_shareId);
    BindText(stmt, 2, rec.m_relPath);
    sqlite3_bind_int64(stmt, 3, rec.m_srcSize);
    sqlite3_bind_int64(stmt, 4, rec.m_srcMtime);
    BindText(stmt, 5, ToUtf8(rec.m_localPath));
    sqlite3_bind_int64(stmt, 6, rec.m_offset);
    sqlite3_bind_int64(stmt, 7, rec.m_updateTime);
    return StepAndFinalize(stmt);
}

bool CShareLocalStore::SaveDownloadProgressBatch(const std::vector<ShareDownloadRecord>& recs)
{
    if (recs.empty())
        return true;
    std::lock_guard<std::mutex> guard(m_mutex);
    if (m_db == NULL || !ExecLocked("BEGIN IMMEDIATE"))
        return false;
    sqlite3_stmt* stmt = NULL;
    const char* sql = "INSERT OR REPLACE INTO t_share_download(shareid, relpath, srcsize, srcmtime, localpath, offset, updatetime) "
                      "VALUES(?, ?, ?, ?, ?, ?, ?)";
    bool ok = (sqlite3_prepare_v2(m_db, sql, -1, &stmt, NULL) == SQLITE_OK);
    for (size_t i = 0; ok && i < recs.size(); ++i)
    {
        const ShareDownloadRecord& rec = recs[i];
        sqlite3_bind_int64(stmt, 1, rec.m_shareId);
        BindText(stmt, 2, rec.m_relPath);
        sqlite3_bind_int64(stmt, 3, rec.m_srcSize);
        sqlite3_bind_int64(stmt, 4, rec.m_srcMtime);
        BindText(stmt, 5, ToUtf8(rec.m_localPath));
        sqlite3_bind_int64(stmt, 6, rec.m_offset);
        sqlite3_bind_int64(stmt, 7, rec.m_updateTime);
        ok = (sqlite3_step(stmt) == SQLITE_DONE);
        sqlite3_reset(stmt);
        sqlite3_clear_bindings(stmt);
    }
    if (stmt != NULL)
        sqlite3_finalize(stmt);
    ExecLocked(ok ? "COMMIT" : "ROLLBACK");
    return ok;
}

bool CShareLocalStore::GetResumePoint(int64_t shareId, const std::string& relPath, int64_t srcSize, int64_t srcMtime,
                                      std::wstring& localPath, int64_t& offset)
{
    localPath.clear();
    offset = 0;
    std::lock_guard<std::mutex> guard(m_mutex);
    if (m_db == NULL)
        return false;

    sqlite3_stmt* stmt = NULL;
    if (sqlite3_prepare_v2(m_db, "SELECT srcsize, srcmtime, localpath, offset FROM t_share_download "
                                 "WHERE shareid = ? AND relpath = ?", -1, &stmt, NULL) != SQLITE_OK)
        return false;
    sqlite3_bind_int64(stmt, 1, shareId);
    BindText(stmt, 2, relPath);
    if (sqlite3_step(stmt) != SQLITE_ROW)
    {
        sqlite3_finalize(stmt);
        return false;
    }
    const int64_t savedSize = sqlite3_column_int64(stmt, 0);
    const int64_t savedMtime = sqlite3_column_int64(stmt, 1);
    const std::wstring savedPath = ToWide(ColumnText(stmt, 2).c_str());
    const int64_t savedOffset = sqlite3_column_int64(stmt, 3);
    sqlite3_finalize(stmt);

    if (savedSize == srcSize && savedMtime == srcMtime)
    {
        localPath = savedPath;
        offset = savedOffset;
        return true;
    }

    //源文件变了：半截文件已经不能用，删掉记录，调用方从头下载
    sqlite3_stmt* del = NULL;
    if (sqlite3_prepare_v2(m_db, "DELETE FROM t_share_download WHERE shareid = ? AND relpath = ?", -1, &del, NULL) == SQLITE_OK)
    {
        sqlite3_bind_int64(del, 1, shareId);
        BindText(del, 2, relPath);
        StepAndFinalize(del);
    }
    return false;
}

bool CShareLocalStore::ListDownloads(int64_t shareId, std::vector<ShareDownloadRecord>& out)
{
    out.clear();
    std::lock_guard<std::mutex> guard(m_mutex);
    if (m_db == NULL)
        return false;

    sqlite3_stmt* stmt = NULL;
    if (sqlite3_prepare_v2(m_db, "SELECT relpath, srcsize, srcmtime, localpath, offset, updatetime FROM t_share_download "
                                 "WHERE shareid = ? ORDER BY relpath", -1, &stmt, NULL) != SQLITE_OK)
        return false;
    sqlite3_bind_int64(stmt, 1, shareId);
    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        ShareDownloadRecord rec;
        rec.m_shareId = shareId;
        rec.m_relPath = ColumnText(stmt, 0);
        rec.m_srcSize = sqlite3_column_int64(stmt, 1);
        rec.m_srcMtime = sqlite3_column_int64(stmt, 2);
        rec.m_localPath = ToWide(ColumnText(stmt, 3).c_str());
        rec.m_offset = sqlite3_column_int64(stmt, 4);
        rec.m_updateTime = sqlite3_column_int64(stmt, 5);
        out.push_back(rec);
    }
    sqlite3_finalize(stmt);
    return true;
}

bool CShareLocalStore::RemoveDownload(int64_t shareId, const std::string& relPath)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    if (m_db == NULL)
        return false;
    sqlite3_stmt* stmt = NULL;
    if (sqlite3_prepare_v2(m_db, "DELETE FROM t_share_download WHERE shareid = ? AND relpath = ?", -1, &stmt, NULL) != SQLITE_OK)
        return false;
    sqlite3_bind_int64(stmt, 1, shareId);
    BindText(stmt, 2, relPath);
    return StepAndFinalize(stmt);
}

bool CShareLocalStore::RemoveDownloads(int64_t shareId, const std::vector<std::string>& relPaths)
{
    if (relPaths.empty())
        return true;
    std::lock_guard<std::mutex> guard(m_mutex);
    if (m_db == NULL || !ExecLocked("BEGIN IMMEDIATE"))
        return false;
    sqlite3_stmt* stmt = NULL;
    bool ok = (sqlite3_prepare_v2(m_db, "DELETE FROM t_share_download WHERE shareid = ? AND relpath = ?", -1, &stmt, NULL)
               == SQLITE_OK);
    for (size_t i = 0; ok && i < relPaths.size(); ++i)
    {
        sqlite3_bind_int64(stmt, 1, shareId);
        BindText(stmt, 2, relPaths[i]);
        ok = (sqlite3_step(stmt) == SQLITE_DONE);
        sqlite3_reset(stmt);
        sqlite3_clear_bindings(stmt);
    }
    if (stmt != NULL)
        sqlite3_finalize(stmt);
    ExecLocked(ok ? "COMMIT" : "ROLLBACK");
    return ok;
}

bool CShareLocalStore::AddActivity(const ShareActivityRecord& rec)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    if (m_db == NULL)
        return false;
    sqlite3_stmt* stmt = NULL;
    if (sqlite3_prepare_v2(m_db, "INSERT INTO t_share_activity(shareid, visitorid, action, path, result, time) "
                                 "VALUES(?, ?, ?, ?, ?, ?)", -1, &stmt, NULL) != SQLITE_OK)
        return false;
    sqlite3_bind_int64(stmt, 1, rec.m_shareId);
    sqlite3_bind_int64(stmt, 2, rec.m_visitorId);
    sqlite3_bind_int(stmt, 3, rec.m_action);
    BindText(stmt, 4, rec.m_path);
    sqlite3_bind_int(stmt, 5, rec.m_result);
    sqlite3_bind_int64(stmt, 6, rec.m_time);
    if (!StepAndFinalize(stmt))
        return false;

    //只保留最近 m_activityKeep 条：删掉 id 不在最新 N 条之内的
    sqlite3_stmt* trim = NULL;
    if (sqlite3_prepare_v2(m_db, "DELETE FROM t_share_activity WHERE id NOT IN "
                                 "(SELECT id FROM t_share_activity ORDER BY id DESC LIMIT ?)", -1, &trim, NULL) != SQLITE_OK)
        return false;
    sqlite3_bind_int64(trim, 1, static_cast<sqlite3_int64>(m_activityKeep));
    return StepAndFinalize(trim);
}

bool CShareLocalStore::ListActivities(size_t limit, std::vector<ShareActivityRecord>& out)
{
    out.clear();
    std::lock_guard<std::mutex> guard(m_mutex);
    if (m_db == NULL)
        return false;
    sqlite3_stmt* stmt = NULL;
    if (sqlite3_prepare_v2(m_db, "SELECT id, shareid, visitorid, action, path, result, time FROM t_share_activity "
                                 "ORDER BY id DESC LIMIT ?", -1, &stmt, NULL) != SQLITE_OK)
        return false;
    sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(limit));
    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        ShareActivityRecord rec;
        rec.m_id = sqlite3_column_int64(stmt, 0);
        rec.m_shareId = sqlite3_column_int64(stmt, 1);
        rec.m_visitorId = sqlite3_column_int64(stmt, 2);
        rec.m_action = sqlite3_column_int(stmt, 3);
        rec.m_path = ColumnText(stmt, 4);
        rec.m_result = sqlite3_column_int(stmt, 5);
        rec.m_time = sqlite3_column_int64(stmt, 6);
        out.push_back(rec);
    }
    sqlite3_finalize(stmt);
    return true;
}

} // namespace fileshare
