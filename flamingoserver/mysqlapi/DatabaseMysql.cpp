#include "DatabaseMysql.h"
#include <fstream>
#include <stdarg.h>
#include <string.h>

#include "MysqlQueryResult.h"
#include "../base/AsyncLog.h"

CDatabaseMysql::CDatabaseMysql(void)
{
    //m_Mysql = new MYSQL;
    m_Mysql = NULL;
    m_bInit = false;
}

CDatabaseMysql::~CDatabaseMysql(void)
{
    if (m_Mysql != NULL)
    {
        if (m_bInit)
        {
            mysql_close(m_Mysql);
        }

        //delete m_Mysql;
    }
}


void CDatabaseMysql::logDbError(const char* operation, const char* sql)
{
    //句柄为空时无法取错误码，仅打印操作名
    if (m_Mysql == NULL)
    {
        LOGSYSE("db error, operation: %s, mysql handle is null", operation);
        return;
    }

    //按需求完整打印 SQL 原文；sql 可能为 NULL（连接类错误无对应 SQL）
    LOGSYSE("db error, operation: %s, errno: %u, error: %s, sql: %s",
            operation, mysql_errno(m_Mysql), mysql_error(m_Mysql),
            sql != NULL ? sql : "(none)");
}

bool CDatabaseMysql::initialize(const std::string& host, const std::string& user, const std::string& pwd, const std::string& dbname)
{
    //重复初始化（如断线重连）时，先关闭旧连接
    if (m_bInit)
    {
        mysql_close(m_Mysql);
    }
    m_Mysql = NULL;
    m_bInit = false;

    //保存连接参数，断线重连时复用
    m_DBInfo.strDBName = dbname;
    m_DBInfo.strHost = host;
    m_DBInfo.strUser = user;
    m_DBInfo.strPwd = pwd;

    //先分配连接句柄。句柄必须用独立变量持有——mysql_real_connect 失败时返回 NULL，
    //若像旧代码那样直接覆盖 m_Mysql，会丢失 mysql_init 分配的句柄（内存泄漏），
    //且之后再也取不到失败原因（mysql_error 需要一个有效句柄）。
    MYSQL* handle = mysql_init(NULL);
    if (handle == NULL)
    {
        //句柄分配失败通常是内存不足，此时没有连接对象可供取错误码
        LOGSYSE("connect mysql failed, mysql_init returned null, host: %s, dbname: %s",
                host.c_str(), dbname.c_str());
        return false;
    }

    MYSQL* conn = mysql_real_connect(handle, host.c_str(), user.c_str(),
                                     pwd.c_str(), dbname.c_str(), 0, NULL, 0);
    if (conn == NULL)
    {
        //连接失败：须在关闭句柄前取出错误原因（关闭后 handle 即失效）。出于安全不打印密码。
        LOGSYSE("connect mysql failed, host: %s, user: %s, dbname: %s, errno: %u, error: %s",
                host.c_str(), user.c_str(), dbname.c_str(),
                mysql_errno(handle), mysql_error(handle));
        mysql_close(handle);
        return false;
    }

    //连接成功：mysql_real_connect 成功时返回的就是传入的句柄
    m_Mysql = conn;
    m_bInit = true;
    //统一连接字符集为 utf8，与库表、通信协议保持一致
    mysql_query(m_Mysql, "set names utf8");
    LOGI("connect mysql ok, host: %s, user: %s, dbname: %s",
         host.c_str(), user.c_str(), dbname.c_str());
    return true;
}

QueryResult* CDatabaseMysql::query(const char* sql)
{
    //连接尚未建立：尝试建立一次
    if (!m_Mysql)
    {
        if (!initialize(m_DBInfo.strHost, m_DBInfo.strUser,
                        m_DBInfo.strPwd, m_DBInfo.strDBName))
        {
            LOGSYSE("query failed, mysql not connected and reconnect failed, sql: %s", sql);
            return NULL;
        }
    }

    int iTempRet = mysql_real_query(m_Mysql, sql, strlen(sql));
    if (iTempRet)
    {
        unsigned int uErrno = mysql_errno(m_Mysql);
        if (CR_SERVER_GONE_ERROR == uErrno)
        {
            //连接已断开，重连后重试一次
            LOGW("query: mysql server gone, reconnecting, sql: %s", sql);
            if (!initialize(m_DBInfo.strHost, m_DBInfo.strUser,
                            m_DBInfo.strPwd, m_DBInfo.strDBName))
            {
                LOGSYSE("query failed, reconnect failed, sql: %s", sql);
                return NULL;
            }
            iTempRet = mysql_real_query(m_Mysql, sql, strlen(sql));
            if (iTempRet)
            {
                //重连后重试仍失败
                logDbError("query(retry)", sql);
                return NULL;
            }
        }
        else
        {
            logDbError("query", sql);
            return NULL;
        }
    }

    MYSQL_RES* result = mysql_store_result(m_Mysql);
    uint32_t fieldCount = mysql_field_count(m_Mysql);
    if (result == NULL)
    {
        //store_result 返回 NULL 有两种情况，须区分：
        //  1) field_count==0：该 SQL 本就不产生结果集（如误用 query 跑了 UPDATE），不算错误；
        //  2) field_count>0：本应有结果集却取不到，是真正的错误。
        if (fieldCount != 0)
        {
            logDbError("query(store_result)", sql);
        }
        return NULL;
    }

    uint64_t rowCount = mysql_affected_rows(m_Mysql);
    QueryResult* queryResult = new MysqlQueryResult(result, rowCount, fieldCount);
    //先取一行，让调用方拿到手的结果集就停在第一行上（业务层的遍历循环依赖这个约定）
    queryResult->nextRow();
    return queryResult;
}

QueryResult* CDatabaseMysql::pquery(const char* format, ...)
{
    if (!format)
        return NULL;

    va_list ap;
    char szQuery[MAX_QUERY_LEN];
    va_start(ap, format);
    int res = vsnprintf(szQuery, MAX_QUERY_LEN, format, ap);
    va_end(ap);

    //vsnprintf 截断时返回的是“本应写入的长度”而非 -1，故须同时判负与越界
    if (res < 0 || res >= MAX_QUERY_LEN)
    {
        LOGSYSE("pquery failed, sql truncated (buffer %d bytes), format: %s",
                MAX_QUERY_LEN, format);
        return NULL;
    }

    return query(szQuery);
}

bool CDatabaseMysql::execute(const char* sql)
{
    //连接尚未建立：尝试建立一次
    if (!m_Mysql)
    {
        if (!initialize(m_DBInfo.strHost, m_DBInfo.strUser,
                        m_DBInfo.strPwd, m_DBInfo.strDBName))
        {
            LOGSYSE("execute failed, mysql not connected and reconnect failed, sql: %s", sql);
            return false;
        }
    }

    int iTempRet = mysql_query(m_Mysql, sql);
    if (iTempRet)
    {
        unsigned int uErrno = mysql_errno(m_Mysql);
        if (CR_SERVER_GONE_ERROR == uErrno)
        {
            //连接已断开，重连后重试一次
            LOGW("execute: mysql server gone, reconnecting, sql: %s", sql);
            if (!initialize(m_DBInfo.strHost, m_DBInfo.strUser,
                            m_DBInfo.strPwd, m_DBInfo.strDBName))
            {
                LOGSYSE("execute failed, reconnect failed, sql: %s", sql);
                return false;
            }
            iTempRet = mysql_query(m_Mysql, sql);
            if (iTempRet)
            {
                //重连后重试仍失败
                logDbError("execute(retry)", sql);
                return false;
            }
            return true;
        }

        logDbError("execute", sql);
        return false;
    }

    return true;
}

bool CDatabaseMysql::execute(const char* sql, uint32_t& uAffectedCount, int& nErrno)
{
    uAffectedCount = 0;
    nErrno = 0;

    //连接尚未建立：尝试建立一次
    if (!m_Mysql)
    {
        if (!initialize(m_DBInfo.strHost, m_DBInfo.strUser,
                        m_DBInfo.strPwd, m_DBInfo.strDBName))
        {
            LOGSYSE("execute failed, mysql not connected and reconnect failed, sql: %s", sql);
            return false;
        }
    }

    int iTempRet = mysql_query(m_Mysql, sql);
    if (iTempRet)
    {
        unsigned int uErrno = mysql_errno(m_Mysql);
        if (CR_SERVER_GONE_ERROR == uErrno)
        {
            //连接已断开，重连后重试一次
            LOGW("execute: mysql server gone, reconnecting, sql: %s", sql);
            if (!initialize(m_DBInfo.strHost, m_DBInfo.strUser,
                            m_DBInfo.strPwd, m_DBInfo.strDBName))
            {
                LOGSYSE("execute failed, reconnect failed, sql: %s", sql);
                return false;
            }
            iTempRet = mysql_query(m_Mysql, sql);
            if (iTempRet)
            {
                //重连后重试仍失败，回填 mysql 错误码供调用方判断
                nErrno = static_cast<int>(mysql_errno(m_Mysql));
                logDbError("execute(retry)", sql);
                return false;
            }
            uAffectedCount = static_cast<uint32_t>(mysql_affected_rows(m_Mysql));
            return true;
        }

        //非断连类错误，回填 mysql 错误码
        nErrno = static_cast<int>(uErrno);
        logDbError("execute", sql);
        return false;
    }

    uAffectedCount = static_cast<uint32_t>(mysql_affected_rows(m_Mysql));
    return true;
}

bool CDatabaseMysql::pexecute(const char* format, ...)
{
    if (!format)
        return false;

    va_list ap;
    char szQuery[MAX_QUERY_LEN];
    va_start(ap, format);
    int res = vsnprintf(szQuery, MAX_QUERY_LEN, format, ap);
    va_end(ap);

    //vsnprintf 截断时返回的是“本应写入的长度”而非 -1，故须同时判负与越界
    if (res < 0 || res >= MAX_QUERY_LEN)
    {
        LOGSYSE("pexecute failed, sql truncated (buffer %d bytes), format: %s",
                MAX_QUERY_LEN, format);
        return false;
    }

    //复用 execute()，把连接重连与错误日志逻辑集中在一处
    return execute(szQuery);
}

void CDatabaseMysql::clearStoredResults()
{
    if (!m_Mysql)
    {
        return;
    }

    MYSQL_RES* result = NULL;
    while (!mysql_next_result(m_Mysql))
    {
        if ((result = mysql_store_result(m_Mysql)) != NULL)
        {
            mysql_free_result(result);
        }
    }
}

uint32_t CDatabaseMysql::getInsertID()
{
    return (uint32_t)mysql_insert_id(m_Mysql);
}

int32_t CDatabaseMysql::escapeString(char* szDst, const char* szSrc, uint32_t uSize)
{
    //无连接时无法转义。直接返回 0 会让调用方误以为源串为空，可能拼出未转义的 SQL，须告警
    if (m_Mysql == NULL)
    {
        LOGSYSE("escapeString failed, mysql not connected");
        return 0;
    }
    if (szDst == NULL || szSrc == NULL)
    {
        LOGSYSE("escapeString failed, null argument (dst or src is null)");
        return 0;
    }

    return mysql_real_escape_string(m_Mysql, szDst, szSrc, uSize);
}