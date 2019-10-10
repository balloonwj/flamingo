#include "MysqlThrdMgr.h"
#include "../base/AsyncLog.h"

CMysqlThrdMgr::CMysqlThrdMgr(void)
{
}

CMysqlThrdMgr::~CMysqlThrdMgr(void)
{
}

bool CMysqlThrdMgr::addTask( uint32_t dwHashID, IMysqlTask* poTask )
{
    //LOG_DEBUG << "CMysqlThrdMgr::AddTask, HashID = " << dwHashID;
	uint32_t btIndex = getTableHashID(dwHashID);

    if (btIndex >= m_dwThreadsCount)
    {
        return false;
    }

    return m_aoMysqlThreads[btIndex].addTask(poTask);
}

bool CMysqlThrdMgr::init(const std::string& host, const std::string& user, const std::string& pwd, const std::string& dbname)
{
    for(uint32_t i = 0; i < m_dwThreadsCount+1; ++i)
    {
        if (false == m_aoMysqlThreads[i].start(host, user, pwd, dbname))
        {
            return false;
        }
    }

    return true;
}

bool CMysqlThrdMgr::processReplyTask(int32_t nCount )
{
    bool bResult = false;

	for (uint32_t i = 0; i < m_dwThreadsCount + 1; ++i)
    {
        IMysqlTask* poTask = m_aoMysqlThreads[i].getReplyTask();
		int32_t dwProcessedNbr = 0;

        while (((nCount == -1) || (dwProcessedNbr < nCount)) && (NULL != poTask))
        {
            poTask->reply();
            poTask->release();
            poTask = m_aoMysqlThreads[i].getReplyTask();
            ++dwProcessedNbr;
        }

        if (dwProcessedNbr == nCount)
        {
            bResult = true;
        }
    }

    return bResult;
}

