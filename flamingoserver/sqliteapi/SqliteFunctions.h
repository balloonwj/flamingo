/**
 *  在 SQLite 连接上注册 MySQL 兼容函数，SqliteFunctions.h
 *  balloonwj@qq.com   2026-07-31
 *
 *  业务层的 SQL 是按 MySQL 方言写的，里面用了一批 SQLite 没有的内置函数。
 *  这些函数都是普通的「函数调用」形式，可以用 sqlite3_create_function 原样补出来，
 *  从而让同一份 SQL 文本在两种后端上都能跑，业务代码一个字都不用改。
 *
 *  本文件负责补齐的函数（括号内是业务层的使用处数）：
 *      NOW()              25 处   当前日期时间，格式 "YYYY-MM-DD HH:MM:SS"
 *      CURDATE()           7 处   当前日期，格式 "YYYY-MM-DD"
 *      FROM_UNIXTIME(x)    7 处   Unix 秒转日期时间文本
 *      LAST_INSERT_ID()    6 处   本连接最近一次插入产生的自增 ID
 *      GREATEST(...)       2 处   取最大值
 *      CONCAT(...)         1 处   字符串拼接
 *      SUBSTRING(s,p[,n])  1 处   取子串，下标从 1 开始
 *
 *  注意：无法用函数机制补齐的方言差异（INTERVAL 日期运算、ON DUPLICATE KEY UPDATE）
 *  不在本文件处理，它们由业务代码经 dbapi/SqlDialect 按方言生成 SQL 片段。
 */
#ifndef __SQLITE_FUNCTIONS_H__
#define __SQLITE_FUNCTIONS_H__

struct sqlite3;

namespace sqliteapi {

/**
 *  在给定连接上注册全部 MySQL 兼容函数。连接建立后必须调用一次，否则业务 SQL 里的
 *  NOW() 之类会报 "no such function"。
 *  @param db 已经打开的 SQLite 连接句柄，不得为 NULL。
 *  @return true 表示全部注册成功；任意一个注册失败即返回 false，失败原因已写入日志。
 */
bool RegisterMysqlCompatFunctions(sqlite3* db);

} // namespace sqliteapi

#endif // __SQLITE_FUNCTIONS_H__
