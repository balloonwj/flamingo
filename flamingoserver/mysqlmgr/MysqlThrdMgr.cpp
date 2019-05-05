#include "MysqlThrdMgr.h"
#include "../base/AsyncLog.h"

CMysqlThrdMgr::CMysqlThrdMgr(void)
{
}

CMysqlThrdMgr::~CMysqlThrdMgr(void)
{
}

bool CMysqlThrdMgr::AddTask( uint32_t dwHashID, IMysqlTask* poTask )
{
    //LOG_DEBUG << "CMysqlThrdMgr::AddTask, HashID = " << dwHashID;
	uint32_t btIndex = GetTableHashID(dwHashID);

    if (btIndex >= m_dwThreadsCount)
    {
        return false;
    }

    return m_aoMysqlThreads[btIndex].AddTask(poTask);
}

bool CMysqlThrdMgr::Init(const string& host, const string& user, const string& pwd, const string& dbname)
{
    for(uint32_t i = 0; i < m_dwThreadsCount+1; ++i)
    {
        if (false == m_aoMysqlThreads[i].Start(host, user, pwd, dbname))
        {
            return false;
        }
    }

    return true;
}

bool CMysqlThrdMgr::ProcessReplyTask(int32_t nCount )
{
    bool bResult = false;

	for (uint32_t i = 0; i < m_dwThreadsCount + 1; ++i)
    {
        IMysqlTask* poTask = m_aoMysqlThreads[i].GetReplyTask();
		int32_t dwProcessedNbr = 0;

        while (((nCount == -1) || (dwProcessedNbr < nCount)) && (NULL != poTask))
        {
            poTask->Reply();
            poTask->Release();
            poTask = m_aoMysqlThreads[i].GetReplyTask();
            ++dwProcessedNbr;
        }

        if (dwProcessedNbr == nCount)
        {
            bResult = true;
        }
    }

    return bResult;
}

