/**
 *  SqliteFunctions 的实现，详见 SqliteFunctions.h。
 *  balloonwj@qq.com   2026-07-31
 */
#include "SqliteFunctions.h"

#include <time.h>
#include <string.h>
#include <string>
#include <vector>

#include "../sqlite3/sqlite3.h"
#include "../base/AsyncLog.h"

namespace sqliteapi {

//MySQL DATETIME 的文本格式，NOW() 与 FROM_UNIXTIME() 按此格式输出
#define MYSQL_DATETIME_FORMAT   "%Y-%m-%d %H:%M:%S"
//MySQL DATE 的文本格式，CURDATE() 按此格式输出
#define MYSQL_DATE_FORMAT       "%Y-%m-%d"
//上述格式化结果的缓冲区大小，单位字节。最长的 DATETIME 文本是 19 字节，取 32 留足余量
#define DATETIME_TEXT_BUF_LEN   32
//UTF-8 续接字节的判定：高两位固定为 10，即 (b & 0xC0) == 0x80。
//数「非续接字节」的个数就等于字符个数，SUBSTRING 靠它按字符而非字节定位
#define UTF8_CONTINUATION_MASK  0xC0
#define UTF8_CONTINUATION_FLAG  0x80

//把 time_t 按**本地时区**格式化成文本。
//必须用本地时区：MySQL 的 NOW() / CURDATE() 返回的是服务器本地时间，
//而 SQLite 的 datetime('now') 默认是 UTC，直接照搬会让两种后端差出几个时区。
static void formatLocalTime(time_t rawTime, const char* format, char* szBuf, size_t bufSize)
{
    struct tm tmValue;
#ifdef WIN32
    localtime_s(&tmValue, &rawTime);
#else
    localtime_r(&rawTime, &tmValue);
#endif

    strftime(szBuf, bufSize, format, &tmValue);
}

//把 sqlite3_value 的文本内容取成 std::string。
//之所以要拷一份而不是直接用返回的指针：SQLite 文档说明对同一个 value 再次调用
//取值接口可能让先前返回的指针失效，拷贝出来最省心。
static std::string valueToString(sqlite3_value* value)
{
    const unsigned char* rawText = sqlite3_value_text(value);
    int rawBytes = sqlite3_value_bytes(value);
    if (rawText == NULL || rawBytes <= 0)
        return std::string();

    return std::string(reinterpret_cast<const char*>(rawText), static_cast<size_t>(rawBytes));
}

//判断参数列表里是否存在 SQL NULL。MySQL 的 GREATEST / CONCAT / SUBSTRING 都遵循
//「任一参数为 NULL 则结果为 NULL」的语义，这里统一判定
static bool hasNullArgument(int argc, sqlite3_value** argv)
{
    for (int i = 0; i < argc; i++)
    {
        if (sqlite3_value_type(argv[i]) == SQLITE_NULL)
            return true;
    }

    return false;
}

//NOW()：返回服务器本地的当前日期时间文本
static void funcNow(sqlite3_context* ctx, int argc, sqlite3_value** argv)
{
    (void)argc;
    (void)argv;

    char szBuf[DATETIME_TEXT_BUF_LEN] = { 0 };
    formatLocalTime(time(NULL), MYSQL_DATETIME_FORMAT, szBuf, sizeof(szBuf));
    sqlite3_result_text(ctx, szBuf, -1, SQLITE_TRANSIENT);
}

//CURDATE()：返回服务器本地的当前日期文本
static void funcCurdate(sqlite3_context* ctx, int argc, sqlite3_value** argv)
{
    (void)argc;
    (void)argv;

    char szBuf[DATETIME_TEXT_BUF_LEN] = { 0 };
    formatLocalTime(time(NULL), MYSQL_DATE_FORMAT, szBuf, sizeof(szBuf));
    sqlite3_result_text(ctx, szBuf, -1, SQLITE_TRANSIENT);
}

//FROM_UNIXTIME(seconds)：把 Unix 时间戳（秒）转成本地时区的日期时间文本。
//业务层的用法是 FROM_UNIXTIME(f_create_time/1000)，除法结果可能带小数，按截断处理
static void funcFromUnixtime(sqlite3_context* ctx, int argc, sqlite3_value** argv)
{
    if (argc < 1 || hasNullArgument(argc, argv))
    {
        sqlite3_result_null(ctx);
        return;
    }

    double seconds = sqlite3_value_double(argv[0]);
    char szBuf[DATETIME_TEXT_BUF_LEN] = { 0 };
    formatLocalTime(static_cast<time_t>(seconds), MYSQL_DATETIME_FORMAT, szBuf, sizeof(szBuf));
    sqlite3_result_text(ctx, szBuf, -1, SQLITE_TRANSIENT);
}

//LAST_INSERT_ID()：返回**本连接**最近一次插入产生的自增 ID。
//自增 ID 是连接私有状态，必须从当前语句所在的连接上取，故用 sqlite3_context_db_handle
static void funcLastInsertId(sqlite3_context* ctx, int argc, sqlite3_value** argv)
{
    (void)argc;
    (void)argv;

    sqlite3* db = sqlite3_context_db_handle(ctx);
    sqlite3_result_int64(ctx, sqlite3_last_insert_rowid(db));
}

//GREATEST(a, b, ...)：取最大值。
//MySQL 的语义：任一参数为 NULL 则结果为 NULL；参数全是数值时按数值比较，否则按文本比较
static void funcGreatest(sqlite3_context* ctx, int argc, sqlite3_value** argv)
{
    if (argc <= 0 || hasNullArgument(argc, argv))
    {
        sqlite3_result_null(ctx);
        return;
    }

    //先看参数的类型构成：全整数、全数值、还是含非数值
    bool allInteger = true;
    bool allNumeric = true;
    for (int i = 0; i < argc; i++)
    {
        int valueType = sqlite3_value_type(argv[i]);
        if (valueType != SQLITE_INTEGER)
        {
            allInteger = false;
        }
        if (valueType != SQLITE_INTEGER && valueType != SQLITE_FLOAT)
        {
            allNumeric = false;
        }
    }

    //全整数时用 int64 比较并返回整数，避免大整数经 double 中转丢精度
    //（业务层的 GREATEST(f_readmsgid, ...) 比的正是消息 ID 这类大整数）
    if (allInteger)
    {
        sqlite3_int64 maxValue = sqlite3_value_int64(argv[0]);
        for (int i = 1; i < argc; i++)
        {
            sqlite3_int64 current = sqlite3_value_int64(argv[i]);
            if (current > maxValue)
            {
                maxValue = current;
            }
        }

        sqlite3_result_int64(ctx, maxValue);
        return;
    }

    if (allNumeric)
    {
        double maxValue = sqlite3_value_double(argv[0]);
        for (int i = 1; i < argc; i++)
        {
            double current = sqlite3_value_double(argv[i]);
            if (current > maxValue)
            {
                maxValue = current;
            }
        }

        sqlite3_result_double(ctx, maxValue);
        return;
    }

    //含非数值参数时退化为文本比较
    std::string maxText = valueToString(argv[0]);
    for (int i = 1; i < argc; i++)
    {
        std::string current = valueToString(argv[i]);
        if (current > maxText)
        {
            maxText = current;
        }
    }

    sqlite3_result_text(ctx, maxText.c_str(), static_cast<int>(maxText.size()), SQLITE_TRANSIENT);
}

//CONCAT(a, b, ...)：字符串拼接。MySQL 语义是任一参数为 NULL 则结果为 NULL
static void funcConcat(sqlite3_context* ctx, int argc, sqlite3_value** argv)
{
    if (hasNullArgument(argc, argv))
    {
        sqlite3_result_null(ctx);
        return;
    }

    std::string result;
    for (int i = 0; i < argc; i++)
    {
        result += valueToString(argv[i]);
    }

    sqlite3_result_text(ctx, result.c_str(), static_cast<int>(result.size()), SQLITE_TRANSIENT);
}

//SUBSTRING(str, pos) / SUBSTRING(str, pos, len)：取子串。
//MySQL 的语义：下标从 1 开始；pos 为负数表示从末尾倒数；pos 为 0 或越界返回空串；
//len 非正数返回空串。按**字符**而非字节计数，故这里要处理 UTF-8 多字节
static void funcSubstring(sqlite3_context* ctx, int argc, sqlite3_value** argv)
{
    if (argc < 2 || hasNullArgument(argc, argv))
    {
        sqlite3_result_null(ctx);
        return;
    }

    std::string text = valueToString(argv[0]);

    //建立「字符序号 -> 字节偏移」索引：只记录每个字符的首字节位置，
    //末尾再放一个哨兵（等于总字节数），这样任意字符区间都能直接换算成字节区间
    std::vector<size_t> charOffsets;
    for (size_t i = 0; i < text.size(); i++)
    {
        if ((static_cast<unsigned char>(text[i]) & UTF8_CONTINUATION_MASK) != UTF8_CONTINUATION_FLAG)
        {
            charOffsets.push_back(i);
        }
    }
    charOffsets.push_back(text.size());

    int charCount = static_cast<int>(charOffsets.size()) - 1;

    int startPos = sqlite3_value_int(argv[1]);
    int startCharIndex = 0;
    if (startPos > 0)
    {
        startCharIndex = startPos - 1;
    }
    else if (startPos < 0)
    {
        startCharIndex = charCount + startPos;
    }
    else
    {
        //MySQL 里起始下标为 0 时返回空串
        sqlite3_result_text(ctx, "", 0, SQLITE_TRANSIENT);
        return;
    }

    if (startCharIndex < 0 || startCharIndex >= charCount)
    {
        sqlite3_result_text(ctx, "", 0, SQLITE_TRANSIENT);
        return;
    }

    //不指定长度时一直取到末尾
    int takeCount = charCount - startCharIndex;
    if (argc >= 3)
    {
        int wantedCount = sqlite3_value_int(argv[2]);
        if (wantedCount <= 0)
        {
            sqlite3_result_text(ctx, "", 0, SQLITE_TRANSIENT);
            return;
        }

        if (wantedCount < takeCount)
        {
            takeCount = wantedCount;
        }
    }

    size_t beginByte = charOffsets[startCharIndex];
    size_t endByte = charOffsets[startCharIndex + takeCount];
    sqlite3_result_text(ctx, text.c_str() + beginByte,
                        static_cast<int>(endByte - beginByte), SQLITE_TRANSIENT);
}

//注册单个兼容函数并检查结果。
//argCount 传 -1 表示变参函数；deterministic 表示同样的输入是否总得到同样的输出
static bool registerOne(sqlite3* db, const char* szName, int argCount,
                        void (*pfnFunc)(sqlite3_context*, int, sqlite3_value**),
                        bool deterministic)
{
    int flags = SQLITE_UTF8;
    if (deterministic)
    {
        //标记为确定性函数后 SQLite 才允许在索引表达式等场景缓存其结果。
        //NOW / CURDATE / LAST_INSERT_ID 的返回值随时间或连接状态变化，绝不能加这个标记
        flags |= SQLITE_DETERMINISTIC;
    }

    int rc = sqlite3_create_function(db, szName, argCount, flags, NULL, pfnFunc, NULL, NULL);
    if (rc != SQLITE_OK)
    {
        LOGE("register sqlite compat function failed, name: %s, argcount: %d, rc: %d, error: %s",
             szName, argCount, rc, sqlite3_errmsg(db));
        return false;
    }

    return true;
}

bool RegisterMysqlCompatFunctions(sqlite3* db)
{
    if (db == NULL)
    {
        LOGE("register sqlite compat functions failed, db handle is null");
        return false;
    }

    bool allOk = true;

    //无参数且结果随时间/连接状态变化的三个函数，不能标记为确定性
    if (!registerOne(db, "NOW", 0, funcNow, false))
        allOk = false;
    if (!registerOne(db, "CURDATE", 0, funcCurdate, false))
        allOk = false;
    if (!registerOne(db, "LAST_INSERT_ID", 0, funcLastInsertId, false))
        allOk = false;

    //单参数的纯计算函数
    if (!registerOne(db, "FROM_UNIXTIME", 1, funcFromUnixtime, true))
        allOk = false;

    //变参函数，参数个数传 -1
    if (!registerOne(db, "GREATEST", -1, funcGreatest, true))
        allOk = false;
    if (!registerOne(db, "CONCAT", -1, funcConcat, true))
        allOk = false;

    //SUBSTRING 有两参数与三参数两种形态，SQLite 要求按参数个数分别注册
    if (!registerOne(db, "SUBSTRING", 2, funcSubstring, true))
        allOk = false;
    if (!registerOne(db, "SUBSTRING", 3, funcSubstring, true))
        allOk = false;

    return allOk;
}

} // namespace sqliteapi
