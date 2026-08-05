/**
 *  SqliteSqlTranslator 的实现，详见 SqliteSqlTranslator.h。
 *  balloonwj@qq.com   2026-07-31
 */
#include "SqliteSqlTranslator.h"

#include <string.h>

namespace sqliteapi {

//改写后统一使用的关键字文本
static const char* const REWRITTEN_INSERT_IGNORE = "INSERT OR IGNORE INTO";

//判断字符是否可以构成 SQL 标识符（字母、数字、下划线）。
//用于关键字的边界判定：MYINSERT 里的 INSERT 不是独立关键字，不能改写。
static bool isSqlWordChar(char c)
{
    if (c >= 'a' && c <= 'z')
        return true;
    if (c >= 'A' && c <= 'Z')
        return true;
    if (c >= '0' && c <= '9')
        return true;
    if (c == '_')
        return true;

    return false;
}

//判断字符是否为 SQL 里的空白（关键字之间的分隔符）
static bool isSqlSpace(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v';
}

//从 sql 的 pos 处开始，尝试匹配一个大小写不敏感的关键字 keyword。
//匹配成功返回 true，并把 pos 推进到关键字之后；失败返回 false 且不改动 pos。
static bool matchKeyword(const std::string& sql, size_t& pos, const char* keyword)
{
    size_t keywordLen = strlen(keyword);
    if (pos + keywordLen > sql.size())
        return false;

    for (size_t i = 0; i < keywordLen; i++)
    {
        char actual = sql[pos + i];
        //统一转成大写再比，实现大小写不敏感匹配
        if (actual >= 'a' && actual <= 'z')
        {
            actual = actual - ('a' - 'A');
        }

        if (actual != keyword[i])
            return false;
    }

    pos += keywordLen;
    return true;
}

//从 sql 的 pos 处开始跳过连续空白，要求至少有一个。
//跳过成功返回 true 并推进 pos；一个空白都没有则返回 false 且不改动 pos。
static bool skipSpaces(const std::string& sql, size_t& pos)
{
    size_t start = pos;
    while (pos < sql.size() && isSqlSpace(sql[pos]))
    {
        pos++;
    }

    return pos > start;
}

//尝试在 sql 的 pos 处匹配完整的 "INSERT <空白> IGNORE <空白> INTO" 序列。
//匹配成功返回该序列在原文中占用的字节数；不匹配返回 0。
static size_t matchInsertIgnoreInto(const std::string& sql, size_t pos)
{
    //左边界：INSERT 前一个字符不能是标识符字符，否则命中的是别的标识符的尾部
    if (pos > 0 && isSqlWordChar(sql[pos - 1]))
        return 0;

    size_t cursor = pos;
    if (!matchKeyword(sql, cursor, "INSERT"))
        return 0;
    if (!skipSpaces(sql, cursor))
        return 0;
    if (!matchKeyword(sql, cursor, "IGNORE"))
        return 0;
    if (!skipSpaces(sql, cursor))
        return 0;
    if (!matchKeyword(sql, cursor, "INTO"))
        return 0;

    //右边界：INTO 后一个字符不能是标识符字符，否则命中的是 INTOX 之类
    if (cursor < sql.size() && isSqlWordChar(sql[cursor]))
        return 0;

    return cursor - pos;
}

std::string TranslateInsertIgnore(const std::string& sql)
{
    std::string result;
    //改写只会让语句变长（多出 " OR "），按原长加一点余量预留，避免反复扩容
    result.reserve(sql.size() + strlen(REWRITTEN_INSERT_IGNORE));

    //是否正处于单引号字符串字面量内部。字面量内的文本一律原样拷贝，不做任何改写
    bool inStringLiteral = false;
    size_t i = 0;
    while (i < sql.size())
    {
        char c = sql[i];

        if (inStringLiteral)
        {
            result.push_back(c);
            i++;

            if (c == '\'')
            {
                //连续两个单引号是字面量内部一个转义的单引号，不是字面量结束
                if (i < sql.size() && sql[i] == '\'')
                {
                    result.push_back(sql[i]);
                    i++;
                }
                else
                {
                    inStringLiteral = false;
                }
            }

            continue;
        }

        if (c == '\'')
        {
            inStringLiteral = true;
            result.push_back(c);
            i++;
            continue;
        }

        size_t matchedLen = matchInsertIgnoreInto(sql, i);
        if (matchedLen > 0)
        {
            result.append(REWRITTEN_INSERT_IGNORE);
            i += matchedLen;
            continue;
        }

        result.push_back(c);
        i++;
    }

    return result;
}

int32_t EscapeSqlString(char* szDst, const char* szSrc, uint32_t uSize)
{
    if (szDst == NULL || szSrc == NULL)
        return 0;

    int32_t written = 0;
    for (uint32_t i = 0; i < uSize; i++)
    {
        char c = szSrc[i];
        //SQLite 的字符串字面量里只有单引号需要转义，做法是把它翻倍；
        //反斜杠在 SQLite 里就是普通字符，**不能**像 MySQL 那样加转义，否则会多出一个反斜杠
        if (c == '\'')
        {
            szDst[written] = '\'';
            written++;
        }

        szDst[written] = c;
        written++;
    }

    szDst[written] = '\0';
    return written;
}

} // namespace sqliteapi
