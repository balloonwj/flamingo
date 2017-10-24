#include "stdafx.h"
#include "HeartbeatTask.h"
#include "FlamingoClient.h"
#include <assert.h>
#include "IULog.h"
#include "Utils.h"

CHeartbeatTask::CHeartbeatTask(void)
{
	m_pTalkClient = NULL;
	m_bStop = FALSE;
	m_nFrequency = 60*1000;
	::InitializeCriticalSection(&m_cs);
	m_bAlive = TRUE;
}

CHeartbeatTask::~CHeartbeatTask(void)
{
	::DeleteCriticalSection(&m_cs);
}

int CHeartbeatTask::Run()
{
	long nCount = 0;
	
	while(TRUE)
	{
		if(m_bStop)
			break;

		if(!IsAlive())
		{
			LOG_WARNING("None of 5 heartbeat results received, make the user offline.");
			::SendMessage(m_pTalkClient->m_UserMgr.m_hProxyWnd, FMG_MSG_HEARTBEAT, HEARTBEAT_DEAD, 0);
			return 1;
		}

		//发送之前先将心跳状态复位
		SetAlive(FALSE);
		
		//for(long i=0; i<5; ++i)
		//{
			m_pTalkClient->SendHeartbeatMessage();
		//}
		
		nCount = 0;
		while(TRUE)
		{
			if(m_bStop || nCount>=60)
				break;

			::Sleep(1000);
			++nCount;
		}
	}
	
	return 1;
}

int CHeartbeatTask::Stop()
{
	m_bStop = TRUE;
	return 0;
}

void CHeartbeatTask::TaskFinish()
{

}

void CHeartbeatTask::SetAlive(BOOL bAlive)
{
	::EnterCriticalSection(&m_cs);
	m_bAlive = bAlive;
	::LeaveCriticalSection(&m_cs);
}

BOOL CHeartbeatTask::IsAlive()
{
	BOOL bAlive = FALSE;
	::EnterCriticalSection(&m_cs);
	bAlive = m_bAlive;
	::LeaveCriticalSection(&m_cs);

	return bAlive;
}