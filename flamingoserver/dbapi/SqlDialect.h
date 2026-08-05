/**
 *  按数据库后端生成 SQL 片段，SqlDialect.h
 *  balloonwj@qq.com   2026-07-31
 *
 *  绝大多数 MySQL 与 SQLite 的差异都不需要业务代码操心：函数级差异由
 *  sqliteapi/SqliteFunctions.h 注册的同名兼容函数接住（NOW / CURDATE / GREATEST 等），
 *  INSERT IGNORE 由 sqliteapi/SqliteSqlTranslator.h 做文本改写。
 *
 *  剩下**语法结构**层面的差异，函数与文本改写都接不住，只能由业务代码按方言生成片段，
 *  本文件就是这些片段的唯一出口。目前只有两类，合计 8 处调用点：
 *
 *    1. 日期区间运算。MySQL 的 INTERVAL 是语法结构而非函数，SQLite 的解析器认不了：
 *           MySQL   DATE_ADD(NOW(), INTERVAL 24 HOUR)
 *           SQLite  datetime('now','localtime','+24 hours')
 *       注意 SQLite 侧一律带 'localtime' 修饰符 —— 它的 'now' 是 UTC，
 *       而 MySQL 的 NOW() 是服务器本地时间，不加修饰符两边会差出几个时区。
 *
 *    2. UPSERT。MySQL 不需要写明冲突列，SQLite 必须写明：
 *           MySQL   ... VALUES(...) ON DUPLICATE KEY UPDATE f_a = 1
 *           SQLite  ... VALUES(...) ON CONFLICT(f_k1, f_k2) DO UPDATE SET f_a = 1
 *       两种方言里**赋值部分的文本完全一样**，所以业务代码只需要换掉子句关键字，
 *       赋值表达式原样保留。这一点成立的前提是：赋值右侧出现的裸列名，
 *       在 MySQL 与 SQLite 里都指代「已存在的那一行」的值（要取新值 SQLite 用 excluded.列名），
 *       所以 f_readmsgid = GREATEST(f_readmsgid, 123) 这种写法两边语义一致。
 *
 *  每个函数都有一个显式传入后端类型的重载，便于单元测试在不依赖全局配置的情况下
 *  同时验证两种方言；业务代码用不带该参数的重载即可，它取 DbConfig 里的当前后端。
 */
#ifndef __SQL_DIALECT_H__
#define __SQL_DIALECT_H__

#include <string>

#include "DbConfig.h"

namespace dbapi {

/**
 *  生成「当前日期时间往后 hours 小时」的 SQL 表达式片段。
 *  @param dbType 目标数据库后端。
 *  @param hours  小时数，可以为负数（表示往前）。
 *  @return 可直接嵌进 SQL 的表达式文本，结果精确到秒。
 */
std::string NowPlusHours(DbType dbType, int hours);
std::string NowPlusHours(int hours);

/**
 *  生成「当前日期时间往前 days 天」的 SQL 表达式片段。
 *  @param dbType 目标数据库后端。
 *  @param days   天数，可以为负数（表示往后）。
 *  @return 可直接嵌进 SQL 的表达式文本，结果精确到秒。
 */
std::string NowMinusDays(DbType dbType, int days);
std::string NowMinusDays(int days);

/**
 *  生成「今天零点往前 days 天」的 SQL 表达式片段，结果是日期粒度、不含时分秒。
 *  用于「近 N 天」这类按自然日切分的统计。
 *  @param dbType 目标数据库后端。
 *  @param days   天数，可以为负数（表示往后）。
 *  @return 可直接嵌进 SQL 的表达式文本，形如 MySQL 的 CURDATE() - INTERVAL 13 DAY。
 */
std::string TodayMinusDays(DbType dbType, int days);
std::string TodayMinusDays(int days);

/**
 *  生成 UPSERT 的冲突处理子句，接在 INSERT ... VALUES(...) 之后、赋值表达式之前。
 *  @param dbType          目标数据库后端。
 *  @param conflictColumns 冲突列清单，逗号分隔，如 "f_userid, f_ctype, f_cid"。
 *                         必须与目标表上某个主键或唯一索引的列完全对应，否则 SQLite 会报错；
 *                         MySQL 侧不使用该参数（它自己判断撞了哪个唯一键）。
 *  @return 两端各带一个空格的子句文本，可直接拼进 SQL。
 */
std::string UpsertClause(DbType dbType, const char* conflictColumns);
std::string UpsertClause(const char* conflictColumns);

/**
 *  按当前数据库后端的规则转义字符串，供拼进单引号字符串字面量使用。
 *
 *  这是**不持有连接**时的转义入口。业务里有两种转义场景：
 *    - 手头有连接对象的，用 IDatabase::escapeString（MySQL 侧会走 mysql_real_escape_string，
 *      能感知连接字符集，比这里的规则更严格）；
 *    - 只有一个待拼的字符串、拿不到连接的（各 Manager 里原先各写一份的 EscapeSql），
 *      用本函数。
 *
 *  两种后端的规则不同，必须区分，否则 SQLite 下会出错：
 *    - MySQL  反斜杠是转义引导符，`\` 与 `'` 都要在前面补一个 `\`；
 *    - SQLite 反斜杠只是普通字符，只需把 `'` 翻倍。
 *  若在 SQLite 上沿用 MySQL 规则，`it's` 会被转成 `it\'s`，字面量在第二个引号处
 *  提前闭合，轻则语法错误，重则写进错误数据。
 *
 *  @param dbType 目标数据库后端。
 *  @param src    待转义的原串，可含任意字节。
 *  @return 转义后的字符串，可直接拼进一对单引号之间。
 */
std::string EscapeString(DbType dbType, const std::string& src);
std::string EscapeString(const std::string& src);

} // namespace dbapi

#endif // __SQL_DIALECT_H__
