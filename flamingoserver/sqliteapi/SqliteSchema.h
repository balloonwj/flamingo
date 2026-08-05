/**
 *  SQLite 库结构数据的声明，SqliteSchema.h
 *  balloonwj@qq.com   2026-07-31
 *
 *  数据本体在 SqliteSchema.cpp，由 tools/gen_table_sqlite.py 从 table.sql 自动生成，
 *  与同时生成的 table_sqlite.sql 同源。改了 table.sql 之后请重跑生成脚本。
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

//全部 CREATE INDEX 与 CREATE TRIGGER 语句。
//**必须在补列之后再执行**：结构落后的存量库里，索引要用到的列可能还没补上，
//这时建索引会直接报 "no such column"。执行顺序固定为 建表 -> 补列 -> 建索引与触发器。
extern const char* const SCHEMA_POST_STATEMENTS[];
extern const int SCHEMA_POST_STATEMENT_COUNT;

//全部表名，供启动自检与缺列比对使用
extern const char* const SCHEMA_TABLES[];
extern const int SCHEMA_TABLE_COUNT;

//一张表里的一个列，用于「存量库缺列自动补齐」
struct SchemaColumn
{
    const char* m_szTable;      //所属表名
    const char* m_szColumn;     //列名
    const char* m_szAddClause;  //ALTER TABLE <表> ADD COLUMN <列名> 之后要跟的定义片段
};

//全部列的补齐定义。不含自增主键列 —— 主键列都缺了的表已经没救，补一列也修不回来。
//另外 SQLite 不允许 ADD COLUMN 时用表达式默认值，所以 timestamp 类列的补齐片段里
//既没有 NOT NULL 也没有默认值，存量行会取 NULL，详见生成脚本的说明。
extern const SchemaColumn SCHEMA_COLUMNS[];
extern const int SCHEMA_COLUMN_COUNT;

} // namespace sqliteapi

#endif // __SQLITE_SCHEMA_H__
