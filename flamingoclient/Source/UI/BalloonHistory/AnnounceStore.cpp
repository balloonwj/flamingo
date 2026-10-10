/**
 *  平台公告本地库——SQLite 读写封装实现，AnnounceStore.cpp
 *  设计与约定见 AnnounceStore.h。
 *  balloonwj@qq.com   2026-08-05
 */
#include "AnnounceStore.h"
#include "sqlite3.h"

namespace balloonhistory {

//建表 SQL。announcementid 作主键：服务端全局分配、单调递增，天然去重，
//也正好用作增量拉取的游标。
static const char* const kCreateTableSql =
    "CREATE TABLE IF NOT EXISTS t_announce ("
    "announcementid INTEGER PRIMARY KEY,"
    "title TEXT NOT NULL DEFAULT '',"
    "body TEXT NOT NULL DEFAULT '',"
    "severity INTEGER NOT NULL DEFAULT 0,"
    "sendername TEXT NOT NULL DEFAULT '',"
    "pinned INTEGER NOT NULL DEFAULT 0,"
    "senttime INTEGER NOT NULL DEFAULT 0,"
    "read INTEGER NOT NULL DEFAULT 0,"
    "recalled INTEGER NOT NULL DEFAULT 0)";

//一张表里的一个列，用于"存量库缺列自动补齐"。
//建表语句是 CREATE TABLE IF NOT EXISTS：库文件已存在时它整条跳过，后加的列<u>永远不会</u>
//出现在存量库里，随后所有带新列的 SQL 全部报 no such column，公告中心直接空白。
//这套机制照搬 V2MsgStore —— 那边就是踩过这个坑才补上的，本库从第一版就带着。
struct SchemaColumn
{
    const char* m_column;     //列名
    const char* m_addClause;  //ALTER TABLE t_announce ADD COLUMN <列名> 之后要跟的定义片段
};

//t_announce 全部需要补齐的列。主键列不列在这里 —— 主键都缺了的库已经没救，补一列也修不回来。
//新增列时在此追加一行，Open 会自动给存量库补上。
static const SchemaColumn kSchemaColumns[] =
{
    { "title",      "TEXT NOT NULL DEFAULT ''" },
    { "body",       "TEXT NOT NULL DEFAULT ''" },
    { "severity",   "INTEGER NOT NULL DEFAULT 0" },
    { "sendername", "TEXT NOT NULL DEFAULT ''" },
    { "pinned",     "INTEGER NOT NULL DEFAULT 0" },
    { "senttime",   "INTEGER NOT NULL DEFAULT 0" },
    { "read",       "INTEGER NOT NULL DEFAULT 0" },
    { "recalled",   "INTEGER NOT NULL DEFAULT 0" },
};
static const int kSchemaColumnCount = (int)(sizeof(kSchemaColumns) / sizeof(kSchemaColumns[0]));

//按「置顶 + 发布时间」建索引，支撑公告中心列表的默认排序。
static const char* const kCreateIndexSql =
    "CREATE INDEX IF NOT EXISTS idx_ann_order ON t_announce (recalled,pinned,senttime)";

//写入 / 更新一条公告。
//
//这里<u>不能</u>用 INSERT OR REPLACE：那是"整行替换"，撞主键时会把 read / recalled
//一并冲成默认值。同一条公告重复落库是常态（登录时全量拉一次、之后推送又来一次），
//用整行替换的话，用户读过的公告下次登录又会变回未读。故改成显式 UPSERT，冲突时
//只更新内容类字段。
//
//read 一列特殊：取本地值与服务端下发值的较大者。已读只能从「未读」走向「已读」，
//不会倒退——在别的设备上读过时服务端会下发 read=1，本地据此跟上；而本地已经读过、
//服务端因为上报还没到而仍下发 0 时，也不该把本地的已读状态抹掉。
//依赖 SQLite 3.24+ 的 ON CONFLICT 语法；本工程编入的是 3.53.1。
static const char* const kUpsertSql =
    "INSERT INTO t_announce "
    "(announcementid,title,body,severity,sendername,pinned,senttime,read,recalled) "
    "VALUES (?,?,?,?,?,?,?,?,?) "
    "ON CONFLICT(announcementid) DO UPDATE SET "
    "title=excluded.title,"
    "body=excluded.body,"
    "severity=excluded.severity,"
    "sendername=excluded.sendername,"
    "pinned=excluded.pinned,"
    "senttime=excluded.senttime,"
    "read=MAX(t_announce.read,excluded.read)";

//标记已读。
static const char* const kMarkReadSql =
    "UPDATE t_announce SET read=1 WHERE announcementid=?";

//标记已撤回。
static const char* const kMarkRecalledSql =
    "UPDATE t_announce SET recalled=1 WHERE announcementid=?";

//取全部未撤回的公告，按公告中心的展示顺序排好。
//发布时间相同时再按公告ID倒序，保证顺序稳定，不会每次刷新都跳来跳去。
static const char* const kQueryAllSql =
    "SELECT announcementid,title,body,severity,sendername,pinned,senttime,read,recalled "
    "FROM t_announce WHERE recalled=0 "
    "ORDER BY pinned DESC, senttime DESC, announcementid DESC";

//取本地最大公告ID，作增量拉取的游标。
//已撤回的也要算进来——它们的ID一样占位，漏算会导致每次登录都把那几条重新拉一遍。
static const char* const kMaxIdSql =
    "SELECT MAX(announcementid) FROM t_announce";

//数未读条数（未撤回且未读），供 Banner 铃铛角标显示。
static const char* const kUnreadCountSql =
    "SELECT COUNT(*) FROM t_announce WHERE recalled=0 AND read=0";

CAnnounceStore& CAnnounceStore::GetInstance()
{
    //函数内静态：首次使用时构造，避免与其它静态对象的初始化顺序纠缠。
    static CAnnounceStore s_instance;
    return s_instance;
}

CAnnounceStore::CAnnounceStore()
    : m_db(NULL)
{
}

CAnnounceStore::~CAnnounceStore()
{
    Close();
}

bool CAnnounceStore::Open(const wchar_t* dbPathUtf16)
{
    if (dbPathUtf16 == NULL)
    {
        return false;
    }

    std::lock_guard<std::mutex> guard(m_mutex);

    //同一个库重复 Open 直接成功；换库（账号切换）先关掉旧的，免得写进上一个账号的库。
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

    //建表失败则回退到未打开态，避免半初始化后续每次读写都失败。
    if (!ExecLocked(kCreateTableSql))
    {
        sqlite3_close(m_db);
        m_db = NULL;
        return false;
    }

    //存量库补列：必须在建表之后、任何带新列的 SQL 之前。
    EnsureColumnsLocked();

    //索引建失败不算致命：查询照样能跑，只是慢一点，不必因此让整个库不可用。
    ExecLocked(kCreateIndexSql);

    m_dbPath = dbPathUtf16;
    return true;
}

void CAnnounceStore::Close()
{
    std::lock_guard<std::mutex> guard(m_mutex);
    if (m_db != NULL)
    {
        sqlite3_close(m_db);
        m_db = NULL;
    }
    m_dbPath.clear();
}

bool CAnnounceStore::IsOpen() const
{
    //仅读取指针做"是否需要 Open"的粗判，不加锁可接受（与 CV2MsgStore / CFileLocalStore 同口径）。
    return m_db != NULL;
}

bool CAnnounceStore::ExecLocked(const char* sql)
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

void CAnnounceStore::EnsureColumnsLocked()
{
    if (m_db == NULL)
    {
        return;
    }

    //先问出这张表现在到底有哪些列。
    std::vector<std::string> existing;
    sqlite3_stmt* stmt = NULL;
    if (sqlite3_prepare_v2(m_db, "PRAGMA table_info(t_announce)", -1, &stmt, NULL) == SQLITE_OK)
    {
        while (sqlite3_step(stmt) == SQLITE_ROW)
        {
            //PRAGMA table_info 的结果集列序：0 cid(序号) 1 name 2 type 3 notnull 4 dflt_value 5 pk
            const unsigned char* name = sqlite3_column_text(stmt, 1);
            if (name != NULL)
            {
                existing.push_back((const char*)name);
            }
        }
    }
    sqlite3_finalize(stmt);

    //读不到任何列说明这张表压根没建成，此时 ALTER 只会连着报错，直接返回。
    if (existing.empty())
    {
        return;
    }

    for (int i = 0; i < kSchemaColumnCount; ++i)
    {
        bool found = false;
        for (size_t j = 0; j < existing.size(); ++j)
        {
            if (existing[j] == kSchemaColumns[i].m_column)
            {
                found = true;
                break;
            }
        }
        if (found)
        {
            continue;
        }

        std::string sql = "ALTER TABLE t_announce ADD COLUMN ";
        sql += kSchemaColumns[i].m_column;
        sql += " ";
        sql += kSchemaColumns[i].m_addClause;
        ExecLocked(sql.c_str());
    }
}

bool CAnnounceStore::Upsert(const AnnounceRecord& rec)
{
    //没有公告ID这条记录就没有意义：主键都定不下来，也没法去重与增量拉取。
    if (rec.m_announcementId <= 0)
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
    sqlite3_bind_int64(stmt, 1, rec.m_announcementId);
    sqlite3_bind_text (stmt, 2, rec.m_title.c_str(),      -1, SQLITE_TRANSIENT);
    sqlite3_bind_text (stmt, 3, rec.m_body.c_str(),       -1, SQLITE_TRANSIENT);
    sqlite3_bind_int  (stmt, 4, rec.m_severity);
    sqlite3_bind_text (stmt, 5, rec.m_senderName.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int  (stmt, 6, rec.m_pinned);
    sqlite3_bind_int64(stmt, 7, rec.m_sentTimeMs);
    sqlite3_bind_int  (stmt, 8, rec.m_read);
    sqlite3_bind_int  (stmt, 9, rec.m_recalled);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

bool CAnnounceStore::MarkRead(int64_t announcementId)
{
    if (announcementId <= 0)
    {
        return false;
    }

    std::lock_guard<std::mutex> guard(m_mutex);
    if (m_db == NULL)
    {
        return false;
    }

    sqlite3_stmt* stmt = NULL;
    if (sqlite3_prepare_v2(m_db, kMarkReadSql, -1, &stmt, NULL) != SQLITE_OK)
    {
        return false;
    }
    sqlite3_bind_int64(stmt, 1, announcementId);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    //无匹配行时 sqlite3_step 同样返回 DONE，视为成功（幂等）。
    return rc == SQLITE_DONE;
}

bool CAnnounceStore::MarkRecalled(int64_t announcementId)
{
    if (announcementId <= 0)
    {
        return false;
    }

    std::lock_guard<std::mutex> guard(m_mutex);
    if (m_db == NULL)
    {
        return false;
    }

    sqlite3_stmt* stmt = NULL;
    if (sqlite3_prepare_v2(m_db, kMarkRecalledSql, -1, &stmt, NULL) != SQLITE_OK)
    {
        return false;
    }
    sqlite3_bind_int64(stmt, 1, announcementId);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

bool CAnnounceStore::QueryAll(std::vector<AnnounceRecord>& out)
{
    out.clear();

    std::lock_guard<std::mutex> guard(m_mutex);
    if (m_db == NULL)
    {
        return false;
    }

    sqlite3_stmt* stmt = NULL;
    if (sqlite3_prepare_v2(m_db, kQueryAllSql, -1, &stmt, NULL) != SQLITE_OK)
    {
        return false;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        AnnounceRecord rec;
        rec.m_announcementId = sqlite3_column_int64(stmt, 0);

        //文本列可能为 NULL（存量库补列前写入的行），取不到就留空串，不要解引用空指针。
        const unsigned char* title = sqlite3_column_text(stmt, 1);
        if (title != NULL)
        {
            rec.m_title = (const char*)title;
        }
        const unsigned char* body = sqlite3_column_text(stmt, 2);
        if (body != NULL)
        {
            rec.m_body = (const char*)body;
        }
        rec.m_severity = sqlite3_column_int(stmt, 3);
        const unsigned char* sender = sqlite3_column_text(stmt, 4);
        if (sender != NULL)
        {
            rec.m_senderName = (const char*)sender;
        }
        rec.m_pinned     = sqlite3_column_int(stmt, 5);
        rec.m_sentTimeMs = sqlite3_column_int64(stmt, 6);
        rec.m_read       = sqlite3_column_int(stmt, 7);
        rec.m_recalled   = sqlite3_column_int(stmt, 8);

        out.push_back(rec);
    }
    sqlite3_finalize(stmt);
    return true;
}

int64_t CAnnounceStore::GetMaxId()
{
    std::lock_guard<std::mutex> guard(m_mutex);
    if (m_db == NULL)
    {
        return 0;
    }

    sqlite3_stmt* stmt = NULL;
    if (sqlite3_prepare_v2(m_db, kMaxIdSql, -1, &stmt, NULL) != SQLITE_OK)
    {
        return 0;
    }

    int64_t maxId = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW)
    {
        //库为空时 MAX() 返回 NULL，sqlite3_column_int64 对 NULL 给出 0，正是我们要的。
        maxId = sqlite3_column_int64(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return maxId;
}

int CAnnounceStore::GetUnreadCount()
{
    std::lock_guard<std::mutex> guard(m_mutex);
    if (m_db == NULL)
    {
        return 0;
    }

    sqlite3_stmt* stmt = NULL;
    if (sqlite3_prepare_v2(m_db, kUnreadCountSql, -1, &stmt, NULL) != SQLITE_OK)
    {
        return 0;
    }

    int count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW)
    {
        count = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return count;
}

} // namespace balloonhistory
