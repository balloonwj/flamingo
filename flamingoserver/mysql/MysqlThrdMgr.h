#pragma once

#include "MysqlTask.h"
#include "MysqlThrd.h"

class CMysqlThrdMgr
{
public:
    CMysqlThrdMgr(void);
    virtual ~CMysqlThrdMgr(void);

public:
	bool Init(const string& host, const string& user, const string& pwd, const string& dbname);
	bool AddTask(uint32_t dwHashID, IMysqlTask* poTask);
    bool AddTask(IMysqlTask* poTask) 
    { 
        return m_aoMysqlThreads[m_dwThreadsCount].AddTask(poTask); 
    }

	inline uint32_t GetTableHashID(uint32_t dwHashID) const 
    { 
        return dwHashID % m_dwThreadsCount; 
    }

    bool ProcessReplyTask(int32_t nCount);
    static uint32_t GetThreadsCount() 
    { 
        return m_dwThreadsCount; 
    }

protected:
	static const uint32_t m_dwThreadsCount = 9;
	CMysqlThrd            m_aoMysqlThreads[m_dwThreadsCount+1];
};


