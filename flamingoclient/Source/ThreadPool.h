#pragma once

#include <vector>

class CThreadPoolTask
{
public:
	CThreadPoolTask();
	virtual ~CThreadPoolTask();

public:
	virtual BOOL IsRunning();
	virtual void SetRunning(BOOL bRunning);
	virtual int Run();
	virtual int Stop();						//让正在运行的任务结束的接口（必须实现）
	virtual void TaskFinish();				//重载这个函数让已经结束的任务提供一个清理资源的机会

private:
	BOOL m_bRunning;
};

struct TP_THREAD_INFO		// 线程信息结构
{
	HANDLE hThread;
	LPARAM lParam;
};

class CThreadPool
{
public:
	CThreadPool();
	~CThreadPool();

public:
	BOOL Init(int nThreadNums);
	void UnInit();
	BOOL AddTask(CThreadPoolTask* lpTask);
	BOOL InsertTask(int nIndex, CThreadPoolTask* lpTask);
	void RemoveTask(CThreadPoolTask* lpTask);
	void RemoveAllTask();

private:
	static UINT APIENTRY ThreadProc(LPVOID lpParam);	// 线程函数

private:
	std::vector<TP_THREAD_INFO*> m_arrThreadInfo;		// 线程信息数组
	std::vector<CThreadPoolTask*> m_arrTask;			// 任务信息数组
	
	CRITICAL_SECTION m_csThreadInfo;					// 线程信息临界区
	CRITICAL_SECTION m_csTask;							// 任务信息临界区

	HANDLE m_hSemaphore_Task;							// 任务通知信号量
	HANDLE m_hEvent_Exit;								// 退出线程通知事件句柄
	HANDLE m_hEvent_ExitAll;							// 所有线程均退出完成通知事件句柄

	long m_lThreadNums;									// 线程总数
	long m_lRunningThreadNums;							// 正在工作中的线程计数
};