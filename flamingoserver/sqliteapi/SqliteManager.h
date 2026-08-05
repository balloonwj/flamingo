/**
 *  SQLite 模式的建库建表引导，SqliteManager.h
 *  balloonwj@qq.com   2026-07-31
 *
 *  与 MySQL 侧的 CMysqlManager 一一对应，做的是同一件事：进程启动时保证库结构就绪，
 *  然后把连接放掉，后续业务各自去建连接。两者的差别在于：
 *    - MySQL 要先连上服务器、判断库是否存在、必要时 CREATE DATABASE，再逐表 desc 比对；
 *    - SQLite 的「库」就是一个文件，打开即建，只需要执行建表语句再逐表补列。
 *
 *  表结构来自 SqliteSchema.h（由 tools/gen_table_sqlite.py 从 table.sql 生成），
 *  不从磁盘读 .sql 文件，理由见该头文件的说明。
 *
 *  典型用法（见 chatserversrc/main.cpp）：
 *      if (!Singleton<CSqliteManager>::Instance().init(dbpath))
 *          LOGF("Init sqlite failed ...");
 */
#ifndef __SQLITE_MANAGER_H__
#define __SQLITE_MANAGER_H__

#include <set>
#include <string>

class IDatabase;

class CSqliteManager
{
public:
    CSqliteManager(void);
    ~CSqliteManager(void);

    /**
     *  建库建表引导：打开（必要时创建）数据库文件，建出全部表、索引与触发器，
     *  再逐表比对、补齐存量库缺失的列。进程启动时调用一次。
     *  @param dbPath 数据库文件路径，相对路径以进程工作目录为基准；
     *                文件与所在目录不存在时都会自动创建。
     *  @return true 表示库结构已就绪，可以让各业务 Manager 开始加载数据。
     *          任何一步失败都返回 false，失败原因已写入日志。
     */
    bool init(const char* dbPath);

private:
    //依次执行一组建库语句。语句都是 IF NOT EXISTS 形式，重复执行安全。
    //pConn：已经连接好的数据库连接，所有权仍归调用方；
    //statements / count：要执行的语句数组及条数；
    //stageName：阶段名，仅用于日志，便于区分是建表阶段还是建索引阶段出的错。
    bool execStatements(IDatabase* pConn, const char* const statements[],
                        int count, const char* stageName);

    //逐列比对 SCHEMA_COLUMNS 与库中实际结构，缺的用 ALTER TABLE ADD COLUMN 补上。
    //对应 CMysqlManager::checkTable 里的补列逻辑，用于结构升级后的存量库。
    bool fillMissingColumns(IDatabase* pConn);

    //用 PRAGMA table_info 取出某张表实际拥有的列名集合。
    //tableName：表名，来自 SCHEMA_TABLES / SCHEMA_COLUMNS，不含外部输入；
    //outColumns：出参，调用前会被清空。
    //返回 false 表示查询失败（表不存在时返回 true 且集合为空）。
    bool loadTableColumns(IDatabase* pConn, const char* tableName,
                          std::set<std::string>& outColumns);

    std::string m_strDbPath;    //数据库文件路径，仅用于日志
};

#endif // __SQLITE_MANAGER_H__
