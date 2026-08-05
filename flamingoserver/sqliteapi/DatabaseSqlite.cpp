/**
 *  CDatabaseSqlite 的实现，详见 DatabaseSqlite.h。
 *  balloonwj@qq.com   2026-07-31
 */
#include "DatabaseSqlite.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <vector>

#ifdef WIN32
#include <direct.h>
#else
#include <sys/types.h>
#include <sys/stat.h>
#endif

#include "../sqlite3/sqlite3.h"
#include "../base/AsyncLog.h"
#include "SqliteFunctions.h"
#include "SqliteQueryResult.h"
#include "SqliteSqlTranslator.h"

//并发写冲突时等待重试的时长，单位毫秒。
//SQLite 同一时刻只允许一个写事务，多个 IO 线程并发写会拿到 SQLITE_BUSY；
//设了这个超时之后由 SQLite 内部自动退避重试，超过该时长才真正返回失败。
#define SQLITE_BUSY_TIMEOUT_MS  5000

//进程内是否已经打过「连接就绪」的 INFO 日志。
//业务层是「每次数据库调用新开一条连接、用完即弃」的用法，一次请求就可能开好几条；
//若每条连接都按 INFO 打日志，正常跑一会儿日志就全是这些行了。
//所以只有第一条连接打完整信息（含实际生效的日志模式），之后的连接降到 DEBUG 级别。
static bool s_bConnectionDetailLogged = false;

//创建单级目录；目录已存在视为成功。
//dir：待创建的目录路径，不含结尾的分隔符。
static bool makeDirectoryIfMissing(const std::string& dir)
{
#ifdef WIN32
    //Windows 上的盘符前缀（如 "E:"）不是目录，不能拿去创建
    if (dir.size() == 2 && dir[1] == ':')
        return true;

    int rc = _mkdir(dir.c_str());
#else
    //权限取属主可读写执行、同组与其他用户可读可执行，与 main.cpp 创建日志目录的做法一致
    int rc = mkdir(dir.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
#endif
    if (rc == 0)
        return true;

    //并发创建或目录本就存在，都算成功
    if (errno == EEXIST)
        return true;

    LOGSYSE("create sqlite db directory failed, dir: %s, errno: %d, %s",
            dir.c_str(), errno, strerror(errno));
    return false;
}

//确保数据库文件所在的目录存在，逐级创建缺失的层级。
//SQLite 自己不会建目录，dbpath 配成 data/flamingo.db 而 data 目录不存在时会直接打开失败，
//对「解压即用」的本地部署很不友好，所以这里替它把目录补上。
static bool ensureParentDirectory(const std::string& dbPath)
{
    size_t lastSep = dbPath.find_last_of("/\\");
    if (lastSep == std::string::npos)
    {
        //路径里没有目录部分，文件直接落在进程工作目录下
        return true;
    }

    std::string dirPath = dbPath.substr(0, lastSep);
    if (dirPath.empty())
    {
        //形如 "/flamingo.db"，父目录就是根目录，无需创建
        return true;
    }

    //从最外层往里逐级创建：每遇到一个分隔符，就把到此为止的前缀路径创建一次
    for (size_t i = 1; i <= dirPath.size(); i++)
    {
        bool atEnd = (i == dirPath.size());
        if (!atEnd && dirPath[i] != '/' && dirPath[i] != '\\')
            continue;

        std::string level = dirPath.substr(0, i);
        if (!makeDirectoryIfMissing(level))
            return false;
    }

    return true;
}

//把 SQLite 的列声明类型文本映射成与数据库无关的 Field::DataTypes。
//declType 取自 sqlite3_column_decltype，即建表时写的类型文本（如 "bigint(20)"）；
//对表达式列（如 COUNT(*)）该接口返回 NULL，此时无从判断，按未知类型处理。
//判定顺序参照 SQLite 官方文档的「列亲和性判定规则」，并与 MysqlQueryResult 的映射结果对齐
//（MySQL 实现里 BLOB 与日期时间类型也都归为 STRING）。
static Field::DataTypes convertDeclaredType(const char* declType)
{
    if (declType == NULL)
        return Field::DB_TYPE_UNKNOWN;

    std::string lower(declType);
    toLowerString(lower);

    if (lower.find("int") != std::string::npos)
        return Field::DB_TYPE_INTEGER;

    if (lower.find("char") != std::string::npos ||
        lower.find("clob") != std::string::npos ||
        lower.find("text") != std::string::npos ||
        lower.find("blob") != std::string::npos ||
        lower.find("date") != std::string::npos ||
        lower.find("time") != std::string::npos)
    {
        return Field::DB_TYPE_STRING;
    }

    if (lower.find("real") != std::string::npos ||
        lower.find("floa") != std::string::npos ||
        lower.find("doub") != std::string::npos ||
        lower.find("deci") != std::string::npos ||
        lower.find("nume") != std::string::npos)
    {
        return Field::DB_TYPE_FLOAT;
    }

    return Field::DB_TYPE_UNKNOWN;
}

//把当前行第 col 列的取值读进 cell。
//BLOB 列按原始字节读，其余类型统一按 UTF-8 文本读 —— Field 内部本来就是按字节串保存、
//按需转成整型/浮点的，所以文本化不会丢信息。
static void readCell(sqlite3_stmt* stmt, int col, sqliteapi::SqliteCell& cell)
{
    int valueType = sqlite3_column_type(stmt, col);
    if (valueType == SQLITE_NULL)
    {
        cell.m_bNull = true;
        cell.m_strValue.clear();
        return;
    }

    cell.m_bNull = false;

    if (valueType == SQLITE_BLOB)
    {
        const void* blob = sqlite3_column_blob(stmt, col);
        int bytes = sqlite3_column_bytes(stmt, col);
        if (blob != NULL && bytes > 0)
        {
            cell.m_strValue.assign(static_cast<const char*>(blob), static_cast<size_t>(bytes));
        }
        else
        {
            cell.m_strValue.clear();
        }

        return;
    }

    const unsigned char* text = sqlite3_column_text(stmt, col);
    int bytes = sqlite3_column_bytes(stmt, col);
    if (text != NULL && bytes > 0)
    {
        cell.m_strValue.assign(reinterpret_cast<const char*>(text), static_cast<size_t>(bytes));
    }
    else
    {
        cell.m_strValue.clear();
    }
}

CDatabaseSqlite::CDatabaseSqlite(void)
    : m_db(NULL)
{
}

CDatabaseSqlite::~CDatabaseSqlite(void)
{
    if (m_db != NULL)
    {
        sqlite3_close(m_db);
        m_db = NULL;
    }
}

void CDatabaseSqlite::logDbError(const char* operation, const char* sql)
{
    if (m_db == NULL)
    {
        LOGSYSE("db error, operation: %s, sqlite handle is null", operation);
        return;
    }

    //按需求完整打印 SQL 原文；sql 可能为 NULL（连接类错误无对应 SQL）
    LOGSYSE("db error, operation: %s, rc: %d, error: %s, sql: %s",
            operation, sqlite3_errcode(m_db), sqlite3_errmsg(m_db),
            sql != NULL ? sql : "(none)");
}

bool CDatabaseSqlite::execSimpleSql(const char* sql)
{
    char* szErrMsg = NULL;
    int rc = sqlite3_exec(m_db, sql, NULL, NULL, &szErrMsg);
    if (rc != SQLITE_OK)
    {
        LOGSYSE("exec sqlite statement failed, rc: %d, error: %s, sql: %s",
                rc, szErrMsg != NULL ? szErrMsg : sqlite3_errmsg(m_db), sql);
        if (szErrMsg != NULL)
        {
            sqlite3_free(szErrMsg);
        }

        return false;
    }

    if (szErrMsg != NULL)
    {
        sqlite3_free(szErrMsg);
    }

    return true;
}

bool CDatabaseSqlite::applyConnectionSettings()
{
    //写冲突时先由 SQLite 内部自动退避重试，超过该时长才返回 SQLITE_BUSY
    int rc = sqlite3_busy_timeout(m_db, SQLITE_BUSY_TIMEOUT_MS);
    if (rc != SQLITE_OK)
    {
        logDbError("set busy timeout", NULL);
        return false;
    }

    //WAL 允许「多读一写」并发。本项目每次业务调用开一条连接、多个 IO 线程会并发访问同一个
    //库文件，不开 WAL 的话读与写之间会互相阻塞，吞吐会很难看
    if (!execSimpleSql("PRAGMA journal_mode = WAL"))
        return false;

    //WAL 模式下 NORMAL 是官方推荐档位，兼顾崩溃安全与写入吞吐
    if (!execSimpleSql("PRAGMA synchronous = NORMAL"))
        return false;

    //SQLite 默认**不**校验外键约束，必须显式打开才能与 MySQL 的行为对齐
    if (!execSimpleSql("PRAGMA foreign_keys = ON"))
        return false;

    //只有进程内第一条连接才把实际生效的日志模式读回来打一条 INFO。
    //某些文件系统（如不支持共享内存的网络盘）上 WAL 会静默回落成别的模式，
    //那样并发写的表现会与预期差很多，必须让日志里能看出来；但这件事每个进程确认一次就够了。
    if (!s_bConnectionDetailLogged)
    {
        //先置位再读，避免并发时重复读取；最坏情况是启动瞬间多打一两行日志，无害。
        //这里刻意不加锁：为一行启动日志引入全局锁不值得
        s_bConnectionDetailLogged = true;

        sqlite3_stmt* stmt = NULL;
        std::string journalMode = "(unknown)";
        if (sqlite3_prepare_v2(m_db, "PRAGMA journal_mode", -1, &stmt, NULL) == SQLITE_OK)
        {
            if (sqlite3_step(stmt) == SQLITE_ROW)
            {
                const unsigned char* mode = sqlite3_column_text(stmt, 0);
                if (mode != NULL)
                {
                    journalMode = reinterpret_cast<const char*>(mode);
                }
            }

            sqlite3_finalize(stmt);
        }

        LOGI("sqlite connection ready, path: %s, journal mode: %s",
             m_strDbPath.c_str(), journalMode.c_str());
    }

    //注册 MySQL 兼容函数：业务 SQL 里的 NOW() / LAST_INSERT_ID() 之类全靠它们
    if (!sqliteapi::RegisterMysqlCompatFunctions(m_db))
        return false;

    return true;
}

bool CDatabaseSqlite::initialize(const std::string& dbPath)
{
    //重复初始化时先关掉旧连接
    if (m_db != NULL)
    {
        sqlite3_close(m_db);
        m_db = NULL;
    }

    m_strDbPath = dbPath;

    if (!ensureParentDirectory(dbPath))
    {
        //失败原因已由 makeDirectoryIfMissing 打过日志
        return false;
    }

    //SQLITE_OPEN_FULLMUTEX 即 serialized 模式，同一个句柄可被多个线程安全使用。
    //当前每次业务调用各开一条连接、理论上用不到，但代价极小，留作防御
    int rc = sqlite3_open_v2(dbPath.c_str(), &m_db,
                             SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
                             NULL);
    if (rc != SQLITE_OK)
    {
        //open 失败时 m_db 通常仍是一个可取错误信息的句柄，须先取错误再关闭
        LOGSYSE("open sqlite db failed, path: %s, rc: %d, error: %s",
                dbPath.c_str(), rc, m_db != NULL ? sqlite3_errmsg(m_db) : "(none)");
        if (m_db != NULL)
        {
            sqlite3_close(m_db);
            m_db = NULL;
        }

        return false;
    }

    if (!applyConnectionSettings())
    {
        sqlite3_close(m_db);
        m_db = NULL;
        return false;
    }

    //这里用 DEBUG 级别：业务层每次数据库调用都会新开一条连接，
    //按 INFO 打的话正常跑一会儿日志就被这一行淹没了。
    //连接成功的信息在进程内第一条连接时已由 applyConnectionSettings 以 INFO 打过
    LOGD("open sqlite db ok, path: %s", dbPath.c_str());
    return true;
}

QueryResult* CDatabaseSqlite::query(const char* sql)
{
    if (m_db == NULL)
    {
        LOGSYSE("query failed, sqlite not opened, sql: %s", sql != NULL ? sql : "(none)");
        return NULL;
    }

    if (sql == NULL)
    {
        LOGSYSE("query failed, sql is null");
        return NULL;
    }

    //业务 SQL 是 MySQL 方言，先做文本级改写（目前只有 INSERT IGNORE 一条规则）
    std::string translated = sqliteapi::TranslateInsertIgnore(sql);

    sqlite3_stmt* stmt = NULL;
    int rc = sqlite3_prepare_v2(m_db, translated.c_str(), -1, &stmt, NULL);
    if (rc != SQLITE_OK)
    {
        logDbError("query(prepare)", translated.c_str());
        return NULL;
    }

    int columnCount = sqlite3_column_count(stmt);
    if (columnCount <= 0)
    {
        //该语句本就不产生结果集（例如误用 query 跑了 UPDATE）。
        //与 MySQL 实现保持一致：仍然把语句执行完，然后返回 NULL，且不当成错误
        while ((rc = sqlite3_step(stmt)) == SQLITE_ROW)
        {
            //没有结果集的语句理论上不会走到这里，留着只是为了把语句跑完
        }

        if (rc != SQLITE_DONE)
        {
            logDbError("query(step)", translated.c_str());
        }

        sqlite3_finalize(stmt);
        return NULL;
    }

    //取列名与列类型
    std::vector<std::string> fieldNames;
    std::vector<Field::DataTypes> fieldTypes;
    for (int i = 0; i < columnCount; i++)
    {
        const char* columnName = sqlite3_column_name(stmt, i);
        fieldNames.push_back(columnName != NULL ? std::string(columnName) : std::string());
        fieldTypes.push_back(convertDeclaredType(sqlite3_column_decltype(stmt, i)));
    }

    //一次性把所有结果行读进内存，对齐 MySQL 的 mysql_store_result 语义，
    //这样结果集的生命周期就与语句句柄无关，业务层可以拿着它慢慢遍历
    std::vector<std::vector<sqliteapi::SqliteCell> > rows;
    while (true)
    {
        rc = sqlite3_step(stmt);
        if (rc == SQLITE_ROW)
        {
            std::vector<sqliteapi::SqliteCell> row(static_cast<size_t>(columnCount));
            for (int i = 0; i < columnCount; i++)
            {
                readCell(stmt, i, row[static_cast<size_t>(i)]);
            }

            rows.push_back(row);
            continue;
        }

        if (rc == SQLITE_DONE)
            break;

        logDbError("query(step)", translated.c_str());
        sqlite3_finalize(stmt);
        return NULL;
    }

    sqlite3_finalize(stmt);

    QueryResult* queryResult = new sqliteapi::SqliteQueryResult(fieldNames, fieldTypes, rows);
    //先取一行，让调用方拿到手的结果集就停在第一行上（与 MySQL 实现约定一致）
    queryResult->nextRow();
    return queryResult;
}

QueryResult* CDatabaseSqlite::pquery(const char* format, ...)
{
    if (!format)
        return NULL;

    va_list ap;
    char szQuery[MAX_QUERY_LEN];
    va_start(ap, format);
    int res = vsnprintf(szQuery, MAX_QUERY_LEN, format, ap);
    va_end(ap);

    //vsnprintf 截断时返回的是「本应写入的长度」而非 -1，故须同时判负与越界
    if (res < 0 || res >= MAX_QUERY_LEN)
    {
        LOGSYSE("pquery failed, sql truncated (buffer %d bytes), format: %s",
                MAX_QUERY_LEN, format);
        return NULL;
    }

    return query(szQuery);
}

bool CDatabaseSqlite::execute(const char* sql)
{
    uint32_t uAffectedCount = 0;
    int nErrno = 0;
    return execute(sql, uAffectedCount, nErrno);
}

bool CDatabaseSqlite::execute(const char* sql, uint32_t& uAffectedCount, int& nErrno)
{
    uAffectedCount = 0;
    nErrno = 0;

    if (m_db == NULL)
    {
        LOGSYSE("execute failed, sqlite not opened, sql: %s", sql != NULL ? sql : "(none)");
        return false;
    }

    if (sql == NULL)
    {
        LOGSYSE("execute failed, sql is null");
        return false;
    }

    std::string translated = sqliteapi::TranslateInsertIgnore(sql);

    char* szErrMsg = NULL;
    int rc = sqlite3_exec(m_db, translated.c_str(), NULL, NULL, &szErrMsg);
    if (rc != SQLITE_OK)
    {
        //回填 SQLite 的结果码供调用方判断。注意它与 MySQL 的错误码不是同一套编号空间，
        //调用方若要区分具体错误，须先看当前是哪种后端
        nErrno = rc;
        LOGSYSE("db error, operation: execute, rc: %d, error: %s, sql: %s",
                rc, szErrMsg != NULL ? szErrMsg : sqlite3_errmsg(m_db), translated.c_str());
        if (szErrMsg != NULL)
        {
            sqlite3_free(szErrMsg);
        }

        return false;
    }

    if (szErrMsg != NULL)
    {
        sqlite3_free(szErrMsg);
    }

    uAffectedCount = static_cast<uint32_t>(sqlite3_changes(m_db));
    return true;
}

bool CDatabaseSqlite::pexecute(const char* format, ...)
{
    if (!format)
        return false;

    va_list ap;
    char szQuery[MAX_QUERY_LEN];
    va_start(ap, format);
    int res = vsnprintf(szQuery, MAX_QUERY_LEN, format, ap);
    va_end(ap);

    //截断判定同 pquery
    if (res < 0 || res >= MAX_QUERY_LEN)
    {
        LOGSYSE("pexecute failed, sql truncated (buffer %d bytes), format: %s",
                MAX_QUERY_LEN, format);
        return false;
    }

    //复用 execute()，把错误日志集中在一处
    return execute(szQuery);
}

uint32_t CDatabaseSqlite::getInsertID()
{
    if (m_db == NULL)
        return 0;

    //返回类型沿用 IDatabase 的约定（uint32_t）；自增 ID 超过 32 位时这里会截断，
    //但业务层真正取大整数 ID 的路径走的是 SELECT LAST_INSERT_ID()，不受影响
    return static_cast<uint32_t>(sqlite3_last_insert_rowid(m_db));
}

int32_t CDatabaseSqlite::escapeString(char* szDst, const char* szSrc, uint32_t uSize)
{
    if (szDst == NULL || szSrc == NULL)
    {
        LOGSYSE("escapeString failed, null argument (dst or src is null)");
        return 0;
    }

    //转义规则与 MySQL 不同（反斜杠不是转义引导符），实现在 SqliteSqlTranslator 里，
    //那边是纯函数、有单元测试覆盖
    return sqliteapi::EscapeSqlString(szDst, szSrc, uSize);
}
