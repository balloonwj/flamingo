/**
 *  IDatabase 的 MySQL / MariaDB 实现：封装一条 MySQL 连接，负责建立连接、执行 SQL、
 *  断线后自动重连重试一次，以及把结果集包装成 MysqlQueryResult 交给调用方。
 *
 *  业务层不直接构造本类，而是通过 DbFactory::createConnection() 拿到 IDatabase*；
 *  只有 mysqlmgr/MysqlManager 这类 MySQL 专属的建库建表代码才直接使用本类，
 *  因为它需要用「不指定库名」的方式连接以执行 show databases / create database。
 *
 *  balloonwj@qq.com   2026-07-31
 */
#pragma once

#include <stdint.h>
#include <string>
#include <mysql/mysql.h>
#include <mysql/errmsg.h>

#include "../dbapi/IDatabase.h"

class CDatabaseMysql : public IDatabase
{
public:
	//一条连接所需的全部参数，保存下来供断线重连时复用
	struct DatabaseInfo
	{
		std::string strHost;
        std::string strUser;
        std::string strPwd;
        std::string strDBName;
	};

public:
	CDatabaseMysql(void);
	virtual ~CDatabaseMysql(void);

	//建立到 MySQL 的连接，重复调用会先关闭旧连接再重连。
	//host：数据库主机地址；user：登录账号；pwd：登录口令；
	//dbname：要选用的库名，传空串表示只连接服务器而不选库
	//        （CMysqlManager 建库阶段就是这么用的：库还不存在，没法选）。
	//返回值：true 表示连接成功，参数会被保存下来供后续断线重连复用。
	bool initialize(const std::string& host, const std::string& user, const std::string& pwd, const std::string& dbname);

	virtual QueryResult* query(const char *sql);

	//基类里 query(const std::string&) 这个便利重载会被本类的 query 声明遮蔽，
	//用 using 把它重新引入本类作用域；否则通过 CDatabaseMysql* 调用 query(ss.str()) 编译不过。
	using IDatabase::query;

	virtual QueryResult* pquery(const char *format,...);
	virtual bool execute(const char* sql);
	virtual bool execute(const char* sql, uint32_t& uAffectedCount, int& nErrno);
	virtual bool pexecute(const char *format,...);

	virtual uint32_t getInsertID();

	//丢弃连接上尚未取走的多结果集，避免下一条语句报 "commands out of sync"。
	//MySQL 专有行为，不进 IDatabase 接口。
	void clearStoredResults();

	virtual int32_t escapeString(char* szDst, const char* szSrc, uint32_t uSize);

private:
	//打印一条数据库错误日志：包含 mysql 错误码、错误文本与出错的 SQL 原文。
	//operation：出错的操作名（如 "query"/"execute"），用于定位；
	//sql：出错的 SQL 语句原文，可为 NULL（连接类错误无对应 SQL）。
	void logDbError(const char* operation, const char* sql);

	DatabaseInfo    m_DBInfo;    //数据库连接参数（主机/用户/密码/库名），断线重连时复用
	MYSQL*          m_Mysql;     //MySQL 连接句柄；NULL 表示尚未连接或连接已断开
	bool            m_bInit;     //是否已成功建立过连接，析构时据此决定是否 mysql_close
};
