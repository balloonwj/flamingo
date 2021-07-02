#pragma once

#include <stdint.h>
#include <mysql/mysql.h>
#include <mysql/errmsg.h>
#include "QueryResult.h"

#define MAX_QUERY_LEN   1024

class CDatabaseMysql
{
public:
	struct DatabaseInfo
	{
		std::string strHost;
        std::string strUser;
        std::string strPwd;
        std::string strDBName;
	};

public:
	CDatabaseMysql(void);
	~CDatabaseMysql(void);

	bool initialize(const std::string& host, const std::string& user, const std::string& pwd, const std::string& dbname);
	QueryResult* query(const char *sql);
	QueryResult* query(const std::string& sql)
	{
	    return query(sql.c_str());
    }

	QueryResult* pquery(const char *format,...);
	bool execute(const char* sql);
	bool execute(const char* sql, uint32_t& uAffectedCount, int& nErrno);
	bool pexecute(const char *format,...);

	uint32_t getInsertID();

	void clearStoredResults();

	int32_t escapeString(char* szDst, const char* szSrc, uint32_t uSize);

private:
	DatabaseInfo    m_DBInfo;
	MYSQL*          m_Mysql;
	bool            m_bInit;
};
