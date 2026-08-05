/**
 *  MySQL 方言到 SQLite 方言的文本级翻译，SqliteSqlTranslator.h
 *  balloonwj@qq.com   2026-07-31
 *
 *  业务层的 SQL 是按 MySQL 方言写的。绝大多数差异由 SqliteFunctions.h 注册的同名兼容函数
 *  接住（NOW / CURDATE / LAST_INSERT_ID 等），不需要改 SQL 文本；只有极少数是语法层面的差异，
 *  函数机制接不住，才在本文件做确定性的文本改写。
 *
 *  目前只有一条改写规则：INSERT IGNORE INTO -> INSERT OR IGNORE INTO。
 *  刻意**不**在这里堆更多规则 —— 文本改写 SQL 一旦复杂化就很难保证正确，
 *  其余方言差异一律走 dbapi/SqlDialect（由业务代码按方言生成 SQL 片段）。
 *
 *  本文件全部为纯函数，仅依赖 STL，不触碰 sqlite3 与网络，便于单元测试。
 */
#ifndef __SQLITE_SQL_TRANSLATOR_H__
#define __SQLITE_SQL_TRANSLATOR_H__

#include <stdint.h>
#include <string>

namespace sqliteapi {

/**
 *  把 MySQL 的 INSERT IGNORE INTO 改写成 SQLite 的 INSERT OR IGNORE INTO。
 *
 *  匹配规则：
 *    - 关键字大小写不敏感（业务代码目前全写大写，但新写的 SQL 未必守这个约定）；
 *    - 三个关键字之间允许任意多个空白（含换行），改写后统一成单个空格；
 *    - 要求 INSERT 之前与 INTO 之后都不是标识符字符，避免误伤 MYINSERT、INTOX 之类；
 *    - **跳过单引号字符串字面量**：字面量里恰好出现 "INSERT IGNORE INTO" 时原样保留，
 *      不做改写（比如往表里存一段 SQL 文本的场景）。字面量内部连续两个单引号
 *      表示一个转义的单引号，不算字面量结束。
 *
 *  @param sql 待改写的 SQL 原文。
 *  @return 改写后的 SQL；不含 INSERT IGNORE INTO 时原样返回。
 */
std::string TranslateInsertIgnore(const std::string& sql);

/**
 *  按 SQLite 的规则转义字符串，供拼进单引号字符串字面量使用。
 *
 *  与 MySQL 的关键差异：SQLite 的字符串字面量里**反斜杠不是转义引导符**，
 *  只需要把单引号翻倍即可；MySQL 还要处理反斜杠。两者不能共用一个实现。
 *
 *  @param szDst 输出缓冲区，由调用方提供，容量至少 uSize * 2 + 1 字节
 *               （全是单引号时每个都要翻倍，再加一个结尾的 '\0'）。
 *  @param szSrc 待转义的原串，按 uSize 指定的长度处理，允许含任意字节。
 *  @param uSize szSrc 的有效字节数。
 *  @return 写入 szDst 的字节数，不含结尾的 '\0'；szDst 或 szSrc 为 NULL 时返回 0。
 */
int32_t EscapeSqlString(char* szDst, const char* szSrc, uint32_t uSize);

} // namespace sqliteapi

#endif // __SQLITE_SQL_TRANSLATOR_H__
