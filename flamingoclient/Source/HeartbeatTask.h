#pragma once

#include "ThreadPool.h"

class CIUProtocol;
class CFlamingoClient;

//心跳包任务
class CHeartbeatTask : public CThreadPoolTask
{
public:
	CHeartbeatTask(void);
	~CHeartbeatTask(void);

public:
	virtual int Run();
	virtual int Stop();
	virtual void TaskFinish();

	void SetAlive(BOOL bAlive);
	BOOL IsAlive();

public:
	CFlamingoClient*		m_pTalkClient;
	long				m_nFrequency;		//心跳包的发送频率(毫秒为单位)
	BOOL				m_bStop;

	CRITICAL_SECTION	m_cs;
	BOOL				m_bAlive;
};