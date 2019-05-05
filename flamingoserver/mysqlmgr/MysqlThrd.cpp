#include "MysqlThrd.h"
#include <functional> //for std::bind
#include "../base/AsyncLog.h"


CMysqlThrd::CMysqlThrd(void)
{
	m_bTerminate	= false;

    m_bStart        = false;
    m_poConn        = NULL;
}

CMysqlThrd::~CMysqlThrd(void)
{
}

void CMysqlThrd::Run()
{
	_MainLoop();
	_Uninit();

	if (NULL != m_pThread)
	{
		m_pThread->join();
	}
}

bool CMysqlThrd::Start(const string& host, const string& user, const string& pwd, const string& dbname)
{
	m_poConn = new CDatabaseMysql();

    if (NULL == m_poConn)
    {
        //LOG_FATAL << "CMysqlThrd::Start, Cannot open database";
        return false;
    }

	if (m_poConn->Initialize(host, user, pwd, dbname) == false)
	{
		return false;
	}

    return _Init();
}

void CMysqlThrd::Stop()
{
	if (m_bTerminate)
	{
		return;
	}
	m_bTerminate = true;
	m_pThread->join();
}

bool CMysqlThrd::_Init()
{
    if (m_bStart)
    {
        return true;
    }

    // Æô¶¯Ïß³Ì
	m_pThread.reset(new std::thread(std::bind(&CMysqlThrd::_MainLoop, this)));

	{
		std::unique_lock<std::mutex> lock(mutex_);
		while (m_bStart == false)
		{
			cond_.wait(lock);
		}
	} 

    return true;
}

void CMysqlThrd::_Uninit()
{
    //m_poConn->Close();
}

void CMysqlThrd::_MainLoop()
{
	m_bStart = true;

	{
		std::unique_lock<std::mutex> lock(mutex_);
		cond_.notify_all();
	}

    IMysqlTask* poTask;

	while(!m_bTerminate)
	{
        if(NULL != (poTask = m_oTask.Pop()))
        {
            poTask->Execute(m_poConn);
            m_oReplyTask.Push(poTask);
            continue;
        }

		this_thread::sleep_for(chrono::milliseconds(1000));
	}
}

