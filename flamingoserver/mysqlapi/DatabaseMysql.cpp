#include "DatabaseMysql.h"
#include <fstream>
#include <stdarg.h>
#include <string.h>

#include "MysqlQueryResult.h"
#include "../base/AsyncLog.h"
#include "../dbapi/DbRetryPolicy.h"

//DbRetryPolicy.h 刻意不包含 mysql/errmsg.h（测试程序不链接 MySQL 客户端库），
//两个错误码是在那边按数值复写的。这里校验它们与官方宏定义始终一致：
//将来换一版客户端库若改了数值，会在本文件直接编译报错，而不是在运行期悄悄失效。
static_assert(dbapi::DB_MYSQL_ERR_SERVER_GONE == CR_SERVER_GONE_ERROR,
              "DB_MYSQL_ERR_SERVER_GONE must match CR_SERVER_GONE_ERROR in mysql/errmsg.h");
static_assert(dbapi::DB_MYSQL_ERR_SERVER_LOST == CR_SERVER_LOST,
              "DB_MYSQL_ERR_SERVER_LOST must match CR_SERVER_LOST in mysql/errmsg.h");

CDatabaseMysql::CDatabaseMysql(void)
{
    //m_Mysql = new MYSQL;
    m_Mysql = NULL;
    m_bInit = false;
    m_bInTransaction = false;
    m_uConnectionGeneration = 0;
    m_uInsertIdGeneration = 0;
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

bool CDatabaseMysql::abortTransactionOnDisconnect(unsigned int uMysqlErrno, const char* operation, const char* sql)
{
    if (!m_bInTransaction)
        return false;

    if (!dbapi::IsDisconnectMysqlErrno(uMysqlErrno))
        return false;

    //连接一断，服务端就把这个事务整体回滚掉了。此时既不能重连重试这一条
    //（新连接处于自动提交状态，重跑只会让它单独生效并立即提交），
    //也没必要再发 ROLLBACK（没有事务可退）。把标志清掉、让本次操作整体失败即可。
    m_bInTransaction = false;
    LOGE("db error, operation: %s, connection lost during a transaction, errno: %u, sql: %s. "
         "The transaction has already been rolled back by the server, so this operation fails as a whole; "
         "no reconnect and no retry is performed.",
         operation, uMysqlErrno, sql != NULL ? sql : "(none)");
    return true;
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

    //旧连接一旦关闭，它上面开着的事务就不复存在了。重连拿到的是一条全新的、处于
    //自动提交状态的连接，绝不能让事务标志跨着重连留下来，否则后续语句会被误判成
    //「还在事务里」而拒绝重试，或者反过来把不属于事务的语句算进事务
    m_bInTransaction = false;

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

    //代数自增，使此前记下的自增 ID 代数立刻失效（详见 getInsertID）
    m_uConnectionGeneration++;
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

    int iTempRet = mysql_real_query(m_Mysql, sql, static_cast<unsigned long>(strlen(sql)));
    if (iTempRet)
    {
        unsigned int uErrno = mysql_errno(m_Mysql);

        //事务进行中断线：整体失败，不重连也不重试
        if (abortTransactionOnDisconnect(uErrno, "query", sql))
            return NULL;

        //SELECT 重复执行没有副作用，两种断线错误码都可以重连后重跑一遍
        if (dbapi::ShouldRetryAfterDisconnect(uErrno, m_bInTransaction, dbapi::DB_STMT_READ_ONLY))
        {
            //连接已断开，重连后重试一次
            LOGW("query: mysql connection lost, errno: %u, reconnecting, sql: %s", uErrno, sql);
            if (!initialize(m_DBInfo.strHost, m_DBInfo.strUser,
                            m_DBInfo.strPwd, m_DBInfo.strDBName))
            {
                LOGSYSE("query failed, reconnect failed, sql: %s", sql);
                return NULL;
            }
            iTempRet = mysql_real_query(m_Mysql, sql, static_cast<unsigned long>(strlen(sql)));
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

        //事务进行中断线：整体失败，不重连也不重试
        if (abortTransactionOnDisconnect(uErrno, "execute", sql))
            return false;

        //写语句只在 CR_SERVER_GONE_ERROR 上重试，理由见 dbapi/DbRetryPolicy.h：
        //CR_SERVER_LOST 说明语句已经发出去了，它有没有在服务端生效无从判断，
        //重跑一遍可能写进两份数据（聊天消息落库就是典型的非幂等写入）
        if (dbapi::ShouldRetryAfterDisconnect(uErrno, m_bInTransaction, dbapi::DB_STMT_WRITE))
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
            m_uInsertIdGeneration = m_uConnectionGeneration;
            return true;
        }

        logDbError("execute", sql);
        return false;
    }

    m_uInsertIdGeneration = m_uConnectionGeneration;
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

        //事务进行中断线：整体失败，不重连也不重试。错误码照常回填，
        //调用方若要区分「事务被服务端回滚」与普通 SQL 错误就靠它
        if (abortTransactionOnDisconnect(uErrno, "execute", sql))
        {
            nErrno = static_cast<int>(uErrno);
            return false;
        }

        //写语句的重试范围同上一个重载，理由见 dbapi/DbRetryPolicy.h
        if (dbapi::ShouldRetryAfterDisconnect(uErrno, m_bInTransaction, dbapi::DB_STMT_WRITE))
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
            m_uInsertIdGeneration = m_uConnectionGeneration;
            return true;
        }

        //非断连类错误，回填 mysql 错误码
        nErrno = static_cast<int>(uErrno);
        logDbError("execute", sql);
        return false;
    }

    uAffectedCount = static_cast<uint32_t>(mysql_affected_rows(m_Mysql));
    m_uInsertIdGeneration = m_uConnectionGeneration;
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

bool CDatabaseMysql::beginTransaction()
{
    //嵌套一律禁止，理由见 dbapi/IDatabase.h：MySQL 会把外层事务隐式提交掉，
    //那是一个不报错却已经把数据写死了的陷阱。这里做成显式失败，正在进行的事务不受影响
    if (m_bInTransaction)
    {
        LOGE("begin transaction failed, a transaction is already active on this connection, nesting is not supported");
        return false;
    }

    //发的是 START TRANSACTION 而不是 BEGIN：BEGIN 在存储程序里是复合语句的起始记号，
    //两种含义会打架，MySQL 官方文档因此推荐用 START TRANSACTION 开启事务。
    //此刻事务标志仍为 false，所以这条语句本身照常受断线重连保护 —— 事务还没开起来，
    //重连一次再开是安全的
    if (!execute("START TRANSACTION"))
    {
        //失败原因已由 execute 打过日志
        LOGE("begin transaction failed");
        return false;
    }

    m_bInTransaction = true;
    LOGD("mysql transaction begun");
    return true;
}

bool CDatabaseMysql::commit()
{
    if (!m_bInTransaction)
    {
        LOGW("commit ignored, no active transaction on this connection");
        return false;
    }

    //COMMIT 必须在事务标志仍然置位的状态下发出：万一此刻连接断掉，
    //abortTransactionOnDisconnect 才能识别出这是「事务中途断线」而不去重连重试
    bool bCommitted = execute("COMMIT");

    //无论成败，本连接上的事务都已经结束：成功即提交；失败则要么服务端已经回滚，
    //要么连接已断（那种情况下 abortTransactionOnDisconnect 也已经把标志清掉了）。
    //MySQL 与 SQLite 在这里语义不同 —— SQLite 的 COMMIT 失败后事务可能还开着，
    //所以那边不能像这里一样无条件清标志
    m_bInTransaction = false;

    if (!bCommitted)
    {
        LOGE("commit transaction failed, the whole operation must be treated as failed");
        return false;
    }

    LOGD("mysql transaction committed");
    return true;
}

bool CDatabaseMysql::rollback()
{
    if (!m_bInTransaction)
    {
        LOGW("rollback ignored, no active transaction on this connection");
        return false;
    }

    bool bRolledBack = execute("ROLLBACK");
    m_bInTransaction = false;

    if (!bRolledBack)
    {
        //回滚语句本身失败：数据同样没有提交（连接一断服务端就会回滚），
        //但这说明连接已经不正常了，值得留一条日志
        LOGE("rollback transaction failed, the connection is probably broken");
        return false;
    }

    LOGD("mysql transaction rolled back");
    return true;
}

bool CDatabaseMysql::inTransaction() const
{
    return m_bInTransaction;
}

uint32_t CDatabaseMysql::getInsertID()
{
    //连接尚未建立或已经断开时，mysql_insert_id 不接受空句柄，直接调用会崩溃。
    //本类其它方法都做了这个判断，唯独这里漏了
    if (m_Mysql == NULL)
    {
        LOGSYSE("getInsertID failed, mysql not connected");
        return 0;
    }

    //mysql_insert_id 取的是客户端句柄上缓存的值，不会去问服务端，所以换了句柄就只剩 0。
    //代数对不上说明中间发生过断线重连，此前那条 INSERT 的自增 ID 已经随着旧连接一起没了。
    //返回值仍是 0（本来也只能是 0），但多一条日志，排查时不至于把它误当成
    //「这条语句本来就没有自增列」。
    //代数为 0 表示本连接上还没有任何语句成功执行过，那种情况下取到 0 是正常的，不必告警 ——
    //连接代数从 1 起算（首次连接成功时自增到 1），所以 0 是个不会与真实代数相混的取值
    if (m_uInsertIdGeneration != 0 && m_uInsertIdGeneration != m_uConnectionGeneration)
    {
        LOGE("getInsertID returns 0, the connection has been re-established since the last successful statement, "
             "so the auto increment id cached on the old connection is lost");
        return 0;
    }

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