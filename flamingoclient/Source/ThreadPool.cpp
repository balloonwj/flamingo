#include "stdafx.h"
#include "ThreadPool.h"

CThreadPoolTask::CThreadPoolTask()
{
	m_bRunning = FALSE;
}

CThreadPoolTask::~CThreadPoolTask()
{

}

BOOL CThreadPoolTask::IsRunning()
{
	return m_bRunning;
}

void CThreadPoolTask::SetRunning(BOOL bRunning)
{
	m_bRunning = bRunning;
	if (!m_bRunning)
		TaskFinish();
}

int CThreadPoolTask::Run()
{
	return 0;
}

int CThreadPoolTask::Stop()
{
	return 0;
}

void CThreadPoolTask::TaskFinish()
{
	return;
}

CThreadPool::CThreadPool()
{
	// memset(&m_csThreadInfo, 0, sizeof(CRITICAL_SECTION));
	// memset(&m_csTask, 0, sizeof(CRITICAL_SECTION));

	m_hSemaphore_Task = NULL;
	m_hEvent_Exit = NULL;
	m_hEvent_ExitAll = NULL;

	m_lThreadNums = 0;
	m_lRunningThreadNums = 0;
}

CThreadPool::~CThreadPool()
{
	
}

BOOL CThreadPool::Init(int nThreadNums)
{
	TP_THREAD_INFO* lpThreadInfo;
	HANDLE hThread;
	DWORD dwThreadID;

	if (nThreadNums <= 0)
		return FALSE;

	m_lThreadNums = 0;
	m_lRunningThreadNums = 0;

	::InitializeCriticalSection(&m_csThreadInfo);
	::InitializeCriticalSection(&m_csTask);

	m_hSemaphore_Task = ::CreateSemaphore(NULL, 0, 0x7FFFFFFF, NULL);
	m_hEvent_Exit = ::CreateEvent(NULL, TRUE, FALSE, NULL);
	m_hEvent_ExitAll = ::CreateEvent(NULL, FALSE, FALSE, NULL);

	::EnterCriticalSection(&m_csThreadInfo);
	for (int i = 0; i < nThreadNums; i++)
	{
		lpThreadInfo = new TP_THREAD_INFO;
		if (lpThreadInfo != NULL)
		{
			hThread = (HANDLE)(ULONG_PTR)_beginthreadex(NULL, 0,  
				&ThreadProc, lpThreadInfo, CREATE_SUSPENDED, (UINT*)&dwThreadID);
			if (hThread != NULL)
			{
				lpThreadInfo->hThread = hThread;
				lpThreadInfo->lParam = (LPARAM)this;
				m_arrThreadInfo.push_back(lpThreadInfo);

				::ResumeThread(hThread);
			}
			else
			{
				delete lpThreadInfo;
			}
		}
	}
	::LeaveCriticalSection(&m_csThreadInfo);

	return TRUE;
}

void CThreadPool::UnInit()
{
	RemoveAllTask();

	::SetEvent(m_hEvent_Exit);

	::WaitForSingleObject(m_hEvent_ExitAll, 30*1000);	// 等待所有线程退出完毕，超时时间设为30秒

	::EnterCriticalSection(&m_csThreadInfo);
	for (int i = 0; i < (int)m_arrThreadInfo.size(); i++)
	{
		TP_THREAD_INFO* lpThreadInfo = m_arrThreadInfo[i];
		if (lpThreadInfo != NULL)
		{
			::CloseHandle(lpThreadInfo->hThread);
			DWORD dwExitCode = 0;
			::GetExitCodeThread(lpThreadInfo->hThread, &dwExitCode);
			if (STILL_ACTIVE == dwExitCode)
				::TerminateThread(lpThreadInfo->hThread, 0);
			delete lpThreadInfo;
		}
	}
	m_arrThreadInfo.clear();
	::LeaveCriticalSection(&m_csThreadInfo);

	::EnterCriticalSection(&m_csTask);
	m_arrTask.clear();
	::LeaveCriticalSection(&m_csTask);

	if (m_hSemaphore_Task != NULL)
	{
		::CloseHandle(m_hSemaphore_Task);
		m_hSemaphore_Task = NULL;
	}

	if (m_hEvent_Exit != NULL)
	{
		::CloseHandle(m_hEvent_Exit);
		m_hEvent_Exit = NULL;
	}

	if (m_hEvent_ExitAll != NULL)
	{
		::CloseHandle(m_hEvent_ExitAll);
		m_hEvent_ExitAll = NULL;
	}

	::DeleteCriticalSection(&m_csThreadInfo);
	::DeleteCriticalSection(&m_csTask);
}

BOOL CThreadPool::AddTask(CThreadPoolTask* lpTask)
{
	if (NULL == lpTask)
		return FALSE;

	::EnterCriticalSection(&m_csTask);
	m_arrTask.push_back(lpTask);
	::LeaveCriticalSection(&m_csTask);

	::ReleaseSemaphore(m_hSemaphore_Task, 1, NULL);		// 增加任务通知信号量

	return TRUE;
}

BOOL CThreadPool::InsertTask(int nIndex, CThreadPoolTask* lpTask)
{
	if (NULL == lpTask)
		return FALSE;

	::EnterCriticalSection(&m_csTask);
	m_arrTask.insert(m_arrTask.begin() + nIndex, lpTask);
	::LeaveCriticalSection(&m_csTask);

	::ReleaseSemaphore(m_hSemaphore_Task, 1, NULL);		// 增加任务通知信号量

	return TRUE;
}

void CThreadPool::RemoveTask(CThreadPoolTask* lpTask)
{
	if (NULL == lpTask)
		return;

	::EnterCriticalSection(&m_csTask);
	for (int i = 0; i < (int)m_arrTask.size(); i++)
	{
        if (m_arrTask[i] == lpTask)
		{
			if (lpTask->IsRunning())
			{
				lpTask->Stop();
			}
			else
			{
				m_arrTask.erase(m_arrTask.begin() + i);
				lpTask->TaskFinish();
			}
			break;
		}
	}
	::LeaveCriticalSection(&m_csTask);
}

void CThreadPool::RemoveAllTask()
{
	::EnterCriticalSection(&m_csTask);
	for (int i = (int)m_arrTask.size() - 1; i >= 0; i--)
	{
		CThreadPoolTask* lpTask = m_arrTask[i];
		if (lpTask != NULL)
		{
			if (lpTask->IsRunning())
			{
				lpTask->Stop();
			}
			else
			{
				m_arrTask.erase(m_arrTask.begin() + i);
				lpTask->TaskFinish();
			}
		}
	}
	::LeaveCriticalSection(&m_csTask);
}

// 线程函数
UINT CThreadPool::ThreadProc(LPVOID lpParam)
{
	TP_THREAD_INFO* lpThreadInfo;
	CThreadPool* lpThis;
	CThreadPoolTask* lpTask,* lpTempTask;
	HANDLE hWaitEvent[2];
	DWORD dwIndex;

	lpThreadInfo = (TP_THREAD_INFO*)lpParam;
	if (NULL == lpThreadInfo)
		return 0;

	lpThis = (CThreadPool*)lpThreadInfo->lParam;
	if (NULL == lpThis)
		return 0;

	::InterlockedIncrement(&lpThis->m_lThreadNums);		// 增加线程计数

	hWaitEvent[0] = lpThis->m_hEvent_Exit;
	hWaitEvent[1] = lpThis->m_hSemaphore_Task;

	for (;;)
	{
		dwIndex = ::WaitForMultipleObjects(2, hWaitEvent, FALSE, INFINITE);

		if (dwIndex == WAIT_OBJECT_0)	// 接到退出线程通知事件
			break;

		lpTask = NULL;

		::EnterCriticalSection(&lpThis->m_csTask);	// 取任务
		for (int i = 0; i < (int)lpThis->m_arrTask.size(); i++)
		{
			lpTempTask = lpThis->m_arrTask[i];
			if (lpTempTask != NULL)
			{
				if (!lpTempTask->IsRunning())
				{
					lpTempTask->SetRunning(TRUE);
					lpTask = lpTempTask;
					break;
				}
			}
		}
		::LeaveCriticalSection(&lpThis->m_csTask);

		if (lpTask != NULL)		// 有任务
		{
			::InterlockedIncrement(&lpThis->m_lRunningThreadNums);	// 增加正在工作中线程计数

			lpTask->Run();
			
			::EnterCriticalSection(&lpThis->m_csTask);	// 删除任务
			for (int i = 0; i < (int)lpThis->m_arrTask.size(); i++)
			{
				lpTempTask = lpThis->m_arrTask[i];
				if (lpTempTask == lpTask)
				{
					lpThis->m_arrTask.erase(lpThis->m_arrTask.begin() + i);
					break;
				}
			}
			lpTask->SetRunning(FALSE);
			::LeaveCriticalSection(&lpThis->m_csTask);

			::InterlockedDecrement(&lpThis->m_lRunningThreadNums);	// 减少正在工作中线程计数
		}
	}

	::InterlockedDecrement(&lpThis->m_lThreadNums);		// 减少线程计数

	if (lpThis->m_lThreadNums <= 0)		// 所有线程都结束完毕，置事件句柄有信号
		::SetEvent(lpThis->m_hEvent_ExitAll);

	return 0;
}