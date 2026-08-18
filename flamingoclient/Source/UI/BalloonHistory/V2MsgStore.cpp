/**
 *  v2 历史消息本地库——SQLite 读写封装实现，V2MsgStore.cpp
 *  balloonwj@qq.com   2026-06-09
 */
#include "V2MsgStore.h"
#include "sqlite3.h"

namespace balloonhistory {

//建表 SQL：列与 V2MsgQuery 的 kColumns 一致；(ctype,cid,msgid) 主键实现去重。
static const char* const kCreateTableSql =
    "CREATE TABLE IF NOT EXISTS t_msg ("
    "msgid INTEGER NOT NULL,"
    "clientmsgid TEXT,"
    "ctype INTEGER NOT NULL,"
    "cid INTEGER NOT NULL,"
    "fromid INTEGER NOT NULL,"
    "msgtype INTEGER NOT NULL,"
    "content TEXT,"
    "search_text TEXT,"
    "is_link INTEGER NOT NULL DEFAULT 0,"
    "timestamp INTEGER NOT NULL,"
    "recalled INTEGER NOT NULL DEFAULT 0,"
    "pinned INTEGER NOT NULL DEFAULT 0,"
    "pin_time INTEGER NOT NULL DEFAULT 0,"
    "PRIMARY KEY (ctype,cid,msgid))";

//一张表里的一个列，用于"存量库缺列自动补齐"。
//建表语句是 CREATE TABLE IF NOT EXISTS：库文件已存在时它整条跳过，后加的列<u>永远不会</u>
//出现在存量库里，随后所有带新列的 SQL 全部报 no such column，"消息记录"窗直接空白。
//形态照服务端 sqliteapi/SqliteSchema.h 的 SchemaColumn，但客户端只有一张表，
//内嵌这张小表即可，不必引入生成脚本。
struct SchemaColumn
{
    const char* m_column;     //列名
    const char* m_addClause;  //ALTER TABLE t_msg ADD COLUMN <列名> 之后要跟的定义片段
};

//t_msg 全部需要补齐的列。主键列不列在这里 —— 主键都缺了的库已经没救，补一列也修不回来。
//新增列时在此追加一行，Open 会自动给存量库补上。
static const SchemaColumn kSchemaColumns[] =
{
    { "pinned",   "INTEGER NOT NULL DEFAULT 0" },
    { "pin_time", "INTEGER NOT NULL DEFAULT 0" },
};
static const int kSchemaColumnCount = (int)(sizeof(kSchemaColumns) / sizeof(kSchemaColumns[0]));

//按会话 + 时间建索引，支撑翻页与时间区间过滤。
static const char* const kCreateIndexTimeSql =
    "CREATE INDEX IF NOT EXISTS idx_conv_time ON t_msg (ctype,cid,timestamp)";
//按会话 + 类型建索引，支撑类型筛选。
static const char* const kCreateIndexTypeSql =
    "CREATE INDEX IF NOT EXISTS idx_conv_type ON t_msg (ctype,cid,msgtype)";

//Upsert 用的固定语句；占位符顺序与下方绑定一一对应。
//
//这里<u>不能</u>用 INSERT OR REPLACE：那是"整行替换"，撞主键时会把 pinned / pin_time
//一并冲成默认值。同一条消息重复落库是常态（同步回填与实时推送撞上同一条），于是
//"钉住的消息过一会儿自己没了"。改成显式 UPSERT，冲突时只更新内容类字段。
//依赖 SQLite 3.24+ 的 ON CONFLICT 语法；本工程编入的是 3.53.1（见 Flamingo.vcxproj
//里的 sqlite-src-3530100），换回更老的 amalgamation 会让本语句 prepare 失败、
//Upsert 恒返回 false。
static const char* const kUpsertSql =
    "INSERT INTO t_msg "
    "(msgid,clientmsgid,ctype,cid,fromid,msgtype,content,search_text,is_link,timestamp,recalled) "
    "VALUES (?,?,?,?,?,?,?,?,?,?,?) "
    "ON CONFLICT(ctype,cid,msgid) DO UPDATE SET "
    "clientmsgid=excluded.clientmsgid,fromid=excluded.fromid,msgtype=excluded.msgtype,"
    "content=excluded.content,search_text=excluded.search_text,is_link=excluded.is_link,"
    "timestamp=excluded.timestamp,recalled=excluded.recalled";

//MarkRecalled 用的固定 UPDATE 语句；WHERE 按主键 (ctype,cid,msgid) 精确定位一行，
//保证不同会话里出现相同 msgid 时互不影响。占位符顺序与下方绑定一一对应。
static const char* const kMarkRecalledSql =
    "UPDATE t_msg SET recalled=1 WHERE ctype=? AND cid=? AND msgid=?";

//SetPinned 用的固定 UPDATE 语句；定位方式与 kMarkRecalledSql 同理。
static const char* const kSetPinnedSql =
    "UPDATE t_msg SET pinned=?, pin_time=? WHERE ctype=? AND cid=? AND msgid=?";

//ReplacePinnedSet 第一步：把该会话中 timestamp 不早于给定下界的行 pinned 清零。
//下界的意义见 CV2MsgStore::ReplacePinnedSet 的注释。
static const char* const kClearPinnedSql =
    "UPDATE t_msg SET pinned=0, pin_time=0 WHERE ctype=? AND cid=? AND timestamp>=?";

CV2MsgStore::CV2MsgStore()
    : m_db(NULL)
{
}

CV2MsgStore::~CV2MsgStore()
{
    Close();
}

bool CV2MsgStore::Open(const wchar_t* dbPathUtf16)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    if (m_db != NULL)
    {
        return true;
    }
    if (dbPathUtf16 == NULL)
    {
        return false;
    }
    sqlite3* db = NULL;
    int rc = sqlite3_open16(dbPathUtf16, &db);
    if (rc != SQLITE_OK)
    {
        if (db != NULL)
        {
            sqlite3_close(db);
        }
        return false;
    }
    m_db = db;
    //建表与索引；建表失败则回退到未打开态，避免半初始化。
    if (!ExecLocked(kCreateTableSql))
    {
        sqlite3_close(m_db);
        m_db = NULL;
        return false;
    }

    //顺序不能反：先建表、再补列、最后建索引。结构落后的存量库里，索引要用到的列
    //可能还没补上，先建索引会直接报 no such column。
    EnsureColumnsLocked();

    ExecLocked(kCreateIndexTimeSql);
    ExecLocked(kCreateIndexTypeSql);
    return true;
}

void CV2MsgStore::EnsureColumnsLocked()
{
    if (m_db == NULL)
    {
        return;
    }

    //先把现有列名读出来。用 PRAGMA table_info 而不是"直接 ALTER、失败就当已存在"：
    //后者会把真正的失败（磁盘只读、库损坏）也一并吞掉，出问题时无从分辨。
    std::vector<std::string> existing;
    sqlite3_stmt* stmt = NULL;
    if (sqlite3_prepare_v2(m_db, "PRAGMA table_info(t_msg)", -1, &stmt, NULL) == SQLITE_OK)
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

        std::string sql = "ALTER TABLE t_msg ADD COLUMN ";
        sql += kSchemaColumns[i].m_column;
        sql += " ";
        sql += kSchemaColumns[i].m_addClause;
        ExecLocked(sql.c_str());
    }
}

void CV2MsgStore::Close()
{
    std::lock_guard<std::mutex> guard(m_mutex);
    if (m_db != NULL)
    {
        sqlite3_close(m_db);
        m_db = NULL;
    }
}

bool CV2MsgStore::IsOpen() const
{
    //仅读取指针做“是否需要 Open”的粗判，不加锁可接受。
    return m_db != NULL;
}

bool CV2MsgStore::ExecLocked(const char* sql)
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

bool CV2MsgStore::Upsert(const HistoryRecord& rec)
{
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
    sqlite3_bind_int64(stmt, 1, rec.m_msgid);
    sqlite3_bind_text(stmt, 2, rec.m_clientmsgid.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 3, rec.m_ctype);
    sqlite3_bind_int64(stmt, 4, rec.m_cid);
    sqlite3_bind_int64(stmt, 5, rec.m_fromid);
    sqlite3_bind_int(stmt, 6, rec.m_msgtype);
    sqlite3_bind_text(stmt, 7, rec.m_content.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 8, rec.m_searchText.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 9, rec.m_isLink ? 1 : 0);
    sqlite3_bind_int64(stmt, 10, rec.m_timestamp);
    sqlite3_bind_int(stmt, 11, rec.m_recalled ? 1 : 0);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

bool CV2MsgStore::MarkRecalled(int ctype, int64_t cid, int64_t msgid)
{
    //msgid 非正说明服务器还没给这条消息分配 ID，本地也不可能按主键存着它。
    if (msgid <= 0)
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
    sqlite3_bind_int(stmt, 1, ctype);
    sqlite3_bind_int64(stmt, 2, cid);
    sqlite3_bind_int64(stmt, 3, msgid);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    //UPDATE 命中 0 行也算成功：本端可能压根没同步过这条历史消息（比如在别的设备上
    //发出、本机从未拉取过），这属于正常情况，不应让调用方当成失败去重试。
    return rc == SQLITE_DONE;
}

bool CV2MsgStore::SetPinned(int ctype, int64_t cid, int64_t msgid, bool pinned,
                            int64_t pinTimeMs, bool& outHit)
{
    outHit = false;

    //msgid 非正说明服务器还没给这条消息分配 ID，本地也不可能按主键存着它。
    if (msgid <= 0)
    {
        return false;
    }

    std::lock_guard<std::mutex> guard(m_mutex);
    if (m_db == NULL)
    {
        return false;
    }

    sqlite3_stmt* stmt = NULL;
    if (sqlite3_prepare_v2(m_db, kSetPinnedSql, -1, &stmt, NULL) != SQLITE_OK)
    {
        return false;
    }
    //取消 Pin 时把 pin_time 一并归零，免得留下"没钉住却有 Pin 时间"这种半截状态，
    //日后按 pin_time 排序或排查时会被它误导。
    sqlite3_bind_int(stmt, 1, pinned ? 1 : 0);
    sqlite3_bind_int64(stmt, 2, pinned ? pinTimeMs : 0);
    sqlite3_bind_int(stmt, 3, ctype);
    sqlite3_bind_int64(stmt, 4, cid);
    sqlite3_bind_int64(stmt, 5, msgid);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE)
    {
        return false;
    }

    //命中 0 行不算失败（理由同 MarkRecalled），但要如实告诉调用方 —— 与撤回不同，
    //Pin 的这一情形<u>需要后续动作</u>：本地没有这条消息就意味着它进不了 Pin 分类，
    //调用方据此向服务端补拉一次消息体。
    outHit = (sqlite3_changes(m_db) > 0);
    return true;
}

bool CV2MsgStore::ReplacePinnedSet(int ctype, int64_t cid, const std::vector<PinnedItem>& items,
                                   int64_t minTimestampMs)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    if (m_db == NULL)
    {
        return false;
    }

    //整体对齐必须是原子的：清零与置位之间若被读到，界面上会闪一下"所有 Pin 都没了"。
    if (!ExecLocked("BEGIN IMMEDIATE"))
    {
        return false;
    }

    bool ok = true;

    //第一步：把该会话中不早于下界的行 pinned 清零。下界的由来见头文件注释 ——
    //服务端应答被截断时，更老的那些 Pin 不在 msgids 里，全量清零会把它们错杀。
    sqlite3_stmt* stmt = NULL;
    if (sqlite3_prepare_v2(m_db, kClearPinnedSql, -1, &stmt, NULL) == SQLITE_OK)
    {
        sqlite3_bind_int(stmt, 1, ctype);
        sqlite3_bind_int64(stmt, 2, cid);
        sqlite3_bind_int64(stmt, 3, minTimestampMs);
        ok = (sqlite3_step(stmt) == SQLITE_DONE);
    }
    else
    {
        ok = false;
    }
    sqlite3_finalize(stmt);

    //第二步：把本次应置为已 Pin 的各条按其 Pin 时间置位。
    //时间必须一起写：上一步清零时把 pin_time 也归了零（不留"没钉住却有 Pin 时间"的
    //半截状态），这里不写回去的话，对账一次就把所有 Pin 时间抹平了。
    for (size_t i = 0; ok && i < items.size(); ++i)
    {
        if (items[i].m_msgid <= 0)
        {
            continue;
        }

        sqlite3_stmt* setStmt = NULL;
        if (sqlite3_prepare_v2(m_db, kSetPinnedSql, -1, &setStmt, NULL) != SQLITE_OK)
        {
            ok = false;
            sqlite3_finalize(setStmt);
            break;
        }
        sqlite3_bind_int(setStmt, 1, 1);
        sqlite3_bind_int64(setStmt, 2, items[i].m_pinTime);
        sqlite3_bind_int(setStmt, 3, ctype);
        sqlite3_bind_int64(setStmt, 4, cid);
        sqlite3_bind_int64(setStmt, 5, items[i].m_msgid);
        ok = (sqlite3_step(setStmt) == SQLITE_DONE);
        sqlite3_finalize(setStmt);
    }

    if (!ok)
    {
        ExecLocked("ROLLBACK");
        return false;
    }
    return ExecLocked("COMMIT");
}

int64_t CV2MsgStore::Count(const HistoryFilter& filter, int64_t nowMs)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    if (m_db == NULL)
    {
        return 0;
    }
    std::string sql = BuildCountSql(filter, nowMs);
    sqlite3_stmt* stmt = NULL;
    if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, NULL) != SQLITE_OK)
    {
        return 0;
    }
    //关键字非空时 BuildCountSql 末尾会带一个 LIKE ? 占位，这里绑定 "%关键字%"。
    if (!filter.m_keyword.empty())
    {
        std::string like = "%" + filter.m_keyword + "%";
        sqlite3_bind_text(stmt, 1, like.c_str(), -1, SQLITE_TRANSIENT);
    }
    int64_t total = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW)
    {
        total = sqlite3_column_int64(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return total;
}

bool CV2MsgStore::QueryPage(const HistoryFilter& filter, int64_t nowMs,
                            std::vector<HistoryRecord>& out)
{
    out.clear();
    std::lock_guard<std::mutex> guard(m_mutex);
    if (m_db == NULL)
    {
        return false;
    }
    std::string sql = BuildPageSql(filter, nowMs);
    sqlite3_stmt* stmt = NULL;
    if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, NULL) != SQLITE_OK)
    {
        return false;
    }
    if (!filter.m_keyword.empty())
    {
        std::string like = "%" + filter.m_keyword + "%";
        sqlite3_bind_text(stmt, 1, like.c_str(), -1, SQLITE_TRANSIENT);
    }
    //列读取顺序须与 V2MsgQuery::kColumns 一致：
    //0 msgid,1 clientmsgid,2 ctype,3 cid,4 fromid,5 msgtype,6 content,7 search_text,
    //8 is_link,9 timestamp,10 recalled,11 pinned,12 pin_time
    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        HistoryRecord rec;
        rec.m_msgid = sqlite3_column_int64(stmt, 0);
        const unsigned char* clientmsgid = sqlite3_column_text(stmt, 1);
        rec.m_clientmsgid = (clientmsgid != NULL) ? (const char*)clientmsgid : "";
        rec.m_ctype = sqlite3_column_int(stmt, 2);
        rec.m_cid = sqlite3_column_int64(stmt, 3);
        rec.m_fromid = sqlite3_column_int64(stmt, 4);
        rec.m_msgtype = sqlite3_column_int(stmt, 5);
        const unsigned char* content = sqlite3_column_text(stmt, 6);
        rec.m_content = (content != NULL) ? (const char*)content : "";
        const unsigned char* searchText = sqlite3_column_text(stmt, 7);
        rec.m_searchText = (searchText != NULL) ? (const char*)searchText : "";
        rec.m_isLink = sqlite3_column_int(stmt, 8) != 0;
        rec.m_timestamp = sqlite3_column_int64(stmt, 9);
        rec.m_recalled = sqlite3_column_int(stmt, 10) != 0;
        rec.m_pinned = sqlite3_column_int(stmt, 11) != 0;
        rec.m_pinTime = sqlite3_column_int64(stmt, 12);
        out.push_back(rec);
    }
    sqlite3_finalize(stmt);
    return true;
}

} // namespace balloonhistory
