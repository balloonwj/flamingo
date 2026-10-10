/**
 *  SQLite 库结构数据的声明，SqliteSchema.h
 *  balloonwj@qq.com   2026-07-31
 *
 *  数据本体在 SqliteSchema.cpp，由 tools/gen_table_sqlite.py 从 table_mysql.sql 自动生成，
 *  与同时生成的 table_sqlite.sql 同源。改了 table_mysql.sql 之后请重跑生成脚本。
 *
 *  之所以把建表语句嵌进程序而不是运行期读 .sql 文件：本地模式主打「解压即用」，
 *  多一个必须随二进制一起部署的文件就多一处会忘记、且报错不直观的失败点。
 *  这也与 MySQL 侧的做法一致 —— CMysqlManager 的表结构同样写在代码里。
 *
 *  使用方是 SqliteManager.h 的 CSqliteManager。
 */
#ifndef __SQLITE_SCHEMA_H__
#define __SQLITE_SCHEMA_H__

namespace sqliteapi {

//全部 CREATE TABLE 语句。每条都是 IF NOT EXISTS 形式，重复执行安全。
extern const char* const SCHEMA_TABLE_STATEMENTS[];
extern const int SCHEMA_TABLE_STATEMENT_COUNT;

//全部 CREATE INDEX 与 CREATE TRIGGER 语句，在建表之后执行（它们建在表上）。
extern const char* const SCHEMA_POST_STATEMENTS[];
extern const int SCHEMA_POST_STATEMENT_COUNT;

//全部表名，供启动自检使用
extern const char* const SCHEMA_TABLES[];
extern const int SCHEMA_TABLE_COUNT;

} // namespace sqliteapi

#endif // __SQLITE_SCHEMA_H__
