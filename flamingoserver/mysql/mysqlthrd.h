
#pragma once

#include "../database/databasemysql.h"
#include "mysqltask.h"
#include "tasklist.h"

#include <condition_variable> 
#include <thread>

class CMysqlThrd
{
public:
    CMysqlThrd(void);
    ~CMysqlThrd(void);

	void Run();

	bool Start(const string& host, const string& user, const string& pwd, const string& dbname);
	void Stop();
    bool AddTask(IMysqlTask* poTask)    
    { 
        return m_oTask.Push(poTask);
    }

    IMysqlTask* GetReplyTask(void)      
    { 
        return m_oReplyTask.Pop();
    }

protected:
	bool _Init();
	void _MainLoop();
	void _Uninit();

private:
	bool				                 m_bTerminate;
    std::shared_ptr<std::thread>         m_pThread;
    bool                                 m_bStart;
	CDatabaseMysql*                      m_poConn;
    CTaskList                            m_oTask;
    CTaskList                            m_oReplyTask;

	std::mutex                           mutex_;
	std::condition_variable              cond_;
};


