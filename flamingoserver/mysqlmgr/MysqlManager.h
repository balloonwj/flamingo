#pragma once

#include <memory>
#include <map>
#include <vector>

#include "../mysqlapi/DatabaseMysql.h"

#define MAXCMDLEN 8192

struct STableField
{
	STableField(){}
	STableField(std::string strName,std::string strType,std::string strIndex):
		m_strName(strName),
		m_strType(strType),
		m_strDesc(strIndex)
	{
	}
	std::string m_strName;
	std::string m_strType;
	std::string m_strDesc;
};

struct STableInfo
{
	STableInfo(){}
	STableInfo(std::string strName)
		:m_strName(strName)
	{
	}
	std::string m_strName;
	std::map<std::string,STableField> m_mapField;
	std::string m_strKeyString;
};

class CMysqlManager
{
public:
    CMysqlManager(void);
    virtual ~CMysqlManager(void);

public:
    bool Init(const char* host, const char* user, const char* pwd, const char* dbname);

	std::string GetHost() { return m_strHost; }
	std::string GetUser() { return m_strUser; }
	std::string GetPwd() { return m_strPassword; }
	std::string GetDBName() { return m_strDataBase; }
	std::string GetCharSet() { return m_strCharactSet;  }

private:
	bool _IsDBExist();
	bool _CreateDB();
	bool _CheckTable(const STableInfo& table);
	bool _CreateTable(const STableInfo& table);
	bool _UpdateTable(const STableInfo& table);

protected:
	std::shared_ptr<CDatabaseMysql>     m_poConn;

    std::string                         m_strHost;
    std::string                         m_strUser;
    std::string                         m_strPassword;
    std::string                         m_strDataBase;
    std::string                         m_strCharactSet;

	std::vector<STableInfo>             m_vecTableInfo;
};

