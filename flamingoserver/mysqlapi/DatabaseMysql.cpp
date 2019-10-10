#include "DatabaseMysql.h"
#include <fstream>
#include <stdarg.h>
#include <string.h>

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


bool CDatabaseMysql::initialize(const std::string& host, const std::string& user, const std::string& pwd, const std::string& dbname)
{
    //LOGI << "CDatabaseMysql::Initialize, begin...";

    //ClearStoredResults();
    if (m_bInit)
    {
        mysql_close(m_Mysql);
    }

    m_Mysql = mysql_init(m_Mysql);
    m_Mysql = mysql_real_connect(m_Mysql, host.c_str(), user.c_str(), pwd.c_str(), dbname.c_str(), 0, NULL, 0);

    //ClearStoredResults();

    //LOGI << "mysql info: host=" << host << ", user=" << user << ", password=" << pwd << ", dbname=" << dbname;

    m_DBInfo.strDBName = dbname;
    m_DBInfo.strHost = host;
    m_DBInfo.strUser = user;
    m_DBInfo.strPwd = pwd;

    if (m_Mysql)
    {
        //LOGI << "m_Mysql address " << (long)m_Mysql;
        //LOGI << "CDatabaseMysql::Initialize, set names utf8";
        mysql_query(m_Mysql, "set names utf8");
        //mysql_query(m_Mysql, "set names latin1");
        m_bInit = true;
        return true;
    }
    else
    {
        //LOGE << "Could not connect to MySQL database at " << host.c_str()
        //    << ", " << mysql_error(m_Mysql);
        mysql_close(m_Mysql);
        return false;
    }

    //LOGI << "CDatabaseMysql::Initialize, init failed!";
    return false;
}

//TODO: 这个函数要区分一下空数据集和出错两种情况
QueryResult* CDatabaseMysql::query(const char* sql)
{
    if (!m_Mysql)
    {
        //LOGI << "CDatabaseMysql::Query, mysql is disconnected!";
        if (false == initialize(m_DBInfo.strHost, m_DBInfo.strUser,
            m_DBInfo.strPwd, m_DBInfo.strDBName))
        {
            return NULL;
        }
    }

    if (!m_Mysql)
        return 0;

    MYSQL_RES* result = 0;
    uint64_t rowCount = 0;
    uint32_t fieldCount = 0;

    {
        //LOGI << sql;
        int iTempRet = mysql_real_query(m_Mysql, sql, strlen(sql));
        if (iTempRet)
        {
            unsigned int uErrno = mysql_errno(m_Mysql);
            //LOGI << "CDatabaseMysql::Query, mysql is abnormal, errno : " << uErrno;
            if (CR_SERVER_GONE_ERROR == uErrno)
            {
                //LOGI << "CDatabaseMysql::Query, mysql is disconnected!";
                if (false == initialize(m_DBInfo.strHost, m_DBInfo.strUser,
                    m_DBInfo.strPwd, m_DBInfo.strDBName))
                {
                    return NULL;
                }
                //LOGI << sql;
                iTempRet = mysql_real_query(m_Mysql, sql, strlen(sql));
                if (iTempRet)
                {
                    //LOGE << "SQL: " << sql ;
                    //LOGE << "query ERROR: " << mysql_error(m_Mysql);
                }
            }
            else
            {
                //LOGE << "SQL: " << sql ;
                //LOGE << "query ERROR: " << mysql_error(m_Mysql);
                return NULL;
            }
        }

        //LOGI << "call mysql_store_result";
        result = mysql_store_result(m_Mysql);

        rowCount = mysql_affected_rows(m_Mysql);
        fieldCount = mysql_field_count(m_Mysql);
        // end guarded block
    }

    if (!result)
        return NULL;

    //  if (!rowCount)
    //  {
          //LOGI << "call mysql_free_result";
    //      mysql_free_result(result);
    //      return NULL;
    //  }

    QueryResult* queryResult = new QueryResult(result, rowCount, fieldCount);

    queryResult->nextRow();

    return queryResult;
}

QueryResult* CDatabaseMysql::pquery(const char* format, ...)
{
    if (!format) return NULL;

    va_list ap;
    char szQuery[MAX_QUERY_LEN];
    va_start(ap, format);
    int res = vsnprintf(szQuery, MAX_QUERY_LEN, format, ap);
    va_end(ap);

    if (res == -1)
    {
        //LOGE << "SQL Query truncated (and not execute) for format: " << format;
        return NULL;
    }

    return query(szQuery);
}

bool CDatabaseMysql::execute(const char* sql)
{
    if (!m_Mysql)
        return false;

    {
        int iTempRet = mysql_query(m_Mysql, sql);
        if (iTempRet)
        {
            unsigned int uErrno = mysql_errno(m_Mysql);
            //LOGI << "CDatabaseMysql::Query, mysql is abnormal, errno : " << uErrno;
            if (CR_SERVER_GONE_ERROR == uErrno)
            {
                //LOGI << "CDatabaseMysql::Query, mysql is disconnected!";
                if (false == initialize(m_DBInfo.strHost, m_DBInfo.strUser, m_DBInfo.strPwd, m_DBInfo.strDBName))
                {
                    return false;
                }
                //LOGI << sql;
                iTempRet = mysql_real_query(m_Mysql, sql, strlen(sql));
                if (iTempRet)
                {
                    LOGE("sql error: %s, sql: %s", mysql_error(m_Mysql), sql);
                    //LOGE << "query ERROR: " << mysql_error(m_Mysql);
                }
            }
            else
            {
                //LOGE << "SQL: " << sql;
                //LOGE << "query ERROR: " << mysql_error(m_Mysql);
                LOGE("sql error: %s, sql: %s", mysql_error(m_Mysql), sql);
            }
            return false;
        }
    }

    return true;
}

bool CDatabaseMysql::execute(const char* sql, uint32_t& uAffectedCount, int& nErrno)
{
    if (!m_Mysql)
        return false;

    {
        int iTempRet = mysql_query(m_Mysql, sql);
        if (iTempRet)
        {
            unsigned int uErrno = mysql_errno(m_Mysql);
            //LOGE << "CDatabaseMysql::Query, mysql is abnormal, errno : " << uErrno;
            if (CR_SERVER_GONE_ERROR == uErrno)
            {
                //LOGE << "CDatabaseMysql::Query, mysql is disconnected!";
                if (false == initialize(m_DBInfo.strHost, m_DBInfo.strUser,
                    m_DBInfo.strPwd, m_DBInfo.strDBName))
                {
                    return false;
                }
                //LOGI << sql;
                iTempRet = mysql_query(m_Mysql, sql);
                nErrno = iTempRet;
                if (iTempRet)
                {
                    //LOGE << "SQL: " << sql;
                    //LOGE << "query ERROR: " << mysql_error(m_Mysql);
                }
            }
            else
            {
                //LOGE << "SQL: " << sql;
                //LOGE << "query ERROR: " << mysql_error(m_Mysql);
            }
            return false;
        }
        uAffectedCount = static_cast<uint32_t>(mysql_affected_rows(m_Mysql));
    }

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

    if (res == -1)
    {
        //LOGE << "SQL Query truncated (and not execute) for format: " << format;
        return false;
    }

    if (!m_Mysql)
        return false;

    {
        int iTempRet = mysql_query(m_Mysql, szQuery);
        if (iTempRet)
        {
            unsigned int uErrno = mysql_errno(m_Mysql);
            //LOGE << "CDatabaseMysql::Query, mysql is abnormal, errno : " << uErrno;
            if (CR_SERVER_GONE_ERROR == uErrno)
            {
                //LOGE << "CDatabaseMysql::Query, mysql is disconnected!";
                if (false == initialize(m_DBInfo.strHost, m_DBInfo.strUser,
                    m_DBInfo.strPwd, m_DBInfo.strDBName))
                {
                    return false;
                }
                //LOGI << szQuery;
                iTempRet = mysql_query(m_Mysql, szQuery);
                if (iTempRet)
                {
                    //LOGE << "SQL: " << szQuery;
                    //LOGE << "query ERROR: " << mysql_error(m_Mysql);
                }
            }
            else
            {
                //LOGE << "SQL: " << szQuery;
                //LOGE << "query ERROR: " << mysql_error(m_Mysql);
            }
            return false;
        }
    }

    return true;
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
    if (m_Mysql == NULL)
    {
        return 0;
    }
    if (szDst == NULL || szSrc == NULL)
    {
        return 0;
    }

    return mysql_real_escape_string(m_Mysql, szDst, szSrc, uSize);
}