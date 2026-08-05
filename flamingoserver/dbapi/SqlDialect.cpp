/**
 *  SqlDialect 的实现，详见 SqlDialect.h。
 *  balloonwj@qq.com   2026-07-31
 */
#include "SqlDialect.h"

#include <stdio.h>
#include <vector>

#include "../base/Singleton.h"
#include "../sqliteapi/SqliteSqlTranslator.h"

namespace dbapi {

//生成单个片段用的栈上缓冲区大小，单位字节。
//这些片段最长也就 "datetime('now','localtime','+2147483647 hours')" 这个量级，128 足够
#define DIALECT_FRAGMENT_BUF_LEN    128

//取当前进程配置的数据库后端
static DbType currentDbType()
{
    return Singleton<CDbConfig>::Instance().getDbType();
}

std::string NowPlusHours(DbType dbType, int hours)
{
    char szBuf[DIALECT_FRAGMENT_BUF_LEN] = { 0 };

    if (dbType == DB_TYPE_SQLITE)
    {
        //修饰符必须自带正负号，所以用 %+d：hours 为负时会输出 "-3 hours"，
        //若写成 "+%d" 再传负数就成了 "+-3 hours"，SQLite 直接解析失败。
        //'localtime' 不能省：SQLite 的 'now' 是 UTC，而 MySQL 的 NOW() 是本地时间
        snprintf(szBuf, sizeof(szBuf), "datetime('now','localtime','%+d hours')", hours);
        return std::string(szBuf);
    }

    snprintf(szBuf, sizeof(szBuf), "DATE_ADD(NOW(), INTERVAL %d HOUR)", hours);
    return std::string(szBuf);
}

std::string NowPlusHours(int hours)
{
    return NowPlusHours(currentDbType(), hours);
}

std::string NowMinusDays(DbType dbType, int days)
{
    char szBuf[DIALECT_FRAGMENT_BUF_LEN] = { 0 };

    if (dbType == DB_TYPE_SQLITE)
    {
        //往前 days 天即修饰符取 -days，正负号由 %+d 负责
        snprintf(szBuf, sizeof(szBuf), "datetime('now','localtime','%+d days')", -days);
        return std::string(szBuf);
    }

    snprintf(szBuf, sizeof(szBuf), "DATE_SUB(NOW(), INTERVAL %d DAY)", days);
    return std::string(szBuf);
}

std::string NowMinusDays(int days)
{
    return NowMinusDays(currentDbType(), days);
}

std::string TodayMinusDays(DbType dbType, int days)
{
    char szBuf[DIALECT_FRAGMENT_BUF_LEN] = { 0 };

    if (dbType == DB_TYPE_SQLITE)
    {
        //用 date() 而不是 datetime()，结果是 "YYYY-MM-DD" 的日期粒度，
        //与 MySQL 的 CURDATE() 对齐。和 "YYYY-MM-DD HH:MM:SS" 比较时按字符串比也是对的，
        //因为日期前缀相同长度、字典序与时间序一致
        snprintf(szBuf, sizeof(szBuf), "date('now','localtime','%+d days')", -days);
        return std::string(szBuf);
    }

    snprintf(szBuf, sizeof(szBuf), "CURDATE() - INTERVAL %d DAY", days);
    return std::string(szBuf);
}

std::string TodayMinusDays(int days)
{
    return TodayMinusDays(currentDbType(), days);
}

std::string UpsertClause(DbType dbType, const char* conflictColumns)
{
    if (dbType == DB_TYPE_SQLITE)
    {
        //SQLite 必须写明冲突目标，否则 ON CONFLICT 无从判断该更新哪一行
        std::string clause = " ON CONFLICT(";
        clause += (conflictColumns != NULL ? conflictColumns : "");
        clause += ") DO UPDATE SET ";
        return clause;
    }

    //MySQL 自己判断撞了哪个唯一键，不需要冲突列
    return std::string(" ON DUPLICATE KEY UPDATE ");
}

std::string UpsertClause(const char* conflictColumns)
{
    return UpsertClause(currentDbType(), conflictColumns);
}

std::string EscapeString(DbType dbType, const std::string& src)
{
    if (dbType == DB_TYPE_SQLITE)
    {
        //复用 SQLite 驱动里那份转义实现，避免同一条规则在两处各写一遍而慢慢走样。
        //那边是按字符缓冲区工作的，最坏情况每个字节都要翻倍，故按 2n+1 备足空间
        std::vector<char> buffer(src.size() * 2 + 1, '\0');
        int32_t written = sqliteapi::EscapeSqlString(&buffer[0], src.c_str(),
                                                     static_cast<uint32_t>(src.size()));
        return std::string(&buffer[0], static_cast<size_t>(written));
    }

    //MySQL 默认模式（未开启 NO_BACKSLASH_ESCAPES）：反斜杠与单引号都要用反斜杠转义。
    //这段逻辑原样保留自各 Manager 里那六份完全相同的手写 EscapeSql，行为不变
    std::string out;
    out.reserve(src.size() + 8);
    for (size_t i = 0; i < src.size(); ++i)
    {
        char c = src[i];
        if (c == '\\' || c == '\'')
        {
            out.push_back('\\');
        }
        out.push_back(c);
    }

    return out;
}

std::string EscapeString(const std::string& src)
{
    return EscapeString(currentDbType(), src);
}

} // namespace dbapi
