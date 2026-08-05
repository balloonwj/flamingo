/**
 *  CDbConfig 的实现，详见 DbConfig.h。
 *  balloonwj@qq.com   2026-07-31
 */
#include "DbConfig.h"

#include <string.h>

#include "../base/AsyncLog.h"

//把字符串就地转成小写，用于 dbtype 取值的大小写不敏感比较。
//这里不复用 Field.h 里的 toLowerString，是为了避免 dbapi 的配置模块反过来依赖结果集头文件。
static void makeLowerCase(std::string& str)
{
    for (size_t i = 0; i < str.size(); i++)
    {
        if (str[i] >= 'A' && str[i] <= 'Z')
        {
            str[i] = str[i] + ('a' - 'A');
        }
    }
}

//把可能为 NULL 的配置项文本安全地转成 std::string，NULL 按空串处理
static std::string safeString(const char* value)
{
    if (value == NULL)
    {
        return std::string();
    }

    return std::string(value);
}

CDbConfig::CDbConfig()
    : m_dbType(DB_TYPE_MYSQL)
{
    //默认值取 MySQL，与历史部署保持一致；真正的取值由 init() 灌入
    m_strDbPath = DEFAULT_SQLITE_DB_PATH;
}

CDbConfig::~CDbConfig()
{
}

void CDbConfig::init(const char* dbtype, const char* host, const char* user, const char* pwd,
                     const char* dbname, const char* dbpath)
{
    m_strHost = safeString(host);
    m_strUser = safeString(user);
    m_strPassword = safeString(pwd);
    m_strDbName = safeString(dbname);

    //SQLite 文件路径缺省时回落到默认值，保证配置文件不写 dbpath 也能起得来
    m_strDbPath = safeString(dbpath);
    if (m_strDbPath.empty())
    {
        m_strDbPath = DEFAULT_SQLITE_DB_PATH;
    }

    //dbtype 缺省时按 mysql 处理：老配置文件里没有这一项，必须保证升级后行为不变
    std::string strType = safeString(dbtype);
    if (strType.empty())
    {
        m_dbType = DB_TYPE_MYSQL;
        LOGI("db config: dbtype not configured, fallback to mysql, config: %s", describe().c_str());
        return;
    }

    makeLowerCase(strType);
    if (strType == DB_TYPE_NAME_MYSQL)
    {
        m_dbType = DB_TYPE_MYSQL;
    }
    else if (strType == DB_TYPE_NAME_SQLITE)
    {
        m_dbType = DB_TYPE_SQLITE;
    }
    else
    {
        //取值非法不直接退出进程，而是回落到 mysql 并告警，避免一个笔误就让服务起不来
        m_dbType = DB_TYPE_MYSQL;
        LOGW("db config: unknown dbtype \"%s\", expect \"%s\" or \"%s\", fallback to mysql",
             dbtype, DB_TYPE_NAME_MYSQL, DB_TYPE_NAME_SQLITE);
    }

    LOGI("db config initialized, config: %s", describe().c_str());
}

std::string CDbConfig::describe() const
{
    if (m_dbType == DB_TYPE_SQLITE)
    {
        return std::string(DB_TYPE_NAME_SQLITE) + " " + m_strDbPath;
    }

    return std::string(DB_TYPE_NAME_MYSQL) + " " + m_strHost + "/" + m_strDbName;
}
