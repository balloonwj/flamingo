#include "stdafx.h"
#include "CheckNetworkStatusTask.h"
#include "FlamingoClient.h"
#include "IULog.h"
#include "Utils.h"

CCheckNetworkStatusTask::CCheckNetworkStatusTask(void)
{
	m_pProtocol = NULL;
	m_pTalkClient = NULL;
	m_bStop = FALSE;
}

CCheckNetworkStatusTask::~CCheckNetworkStatusTask(void)
{
}

int CCheckNetworkStatusTask::Run()
{
	
	while(TRUE)
	{
		if(m_bStop)
			break;

		
		if(!IUIsNetworkAlive())
		{
			::PostMessage(m_pTalkClient->m_UserMgr.m_hProxyWnd, FMG_MSG_NETWORK_STATUS_CHANGE, 0, 0);
		}
		else
		{
			::PostMessage(m_pTalkClient->m_UserMgr.m_hProxyWnd, FMG_MSG_NETWORK_STATUS_CHANGE, 1, 0);
		}

		::Sleep(3000);
	}
	
	return 1;
}

int CCheckNetworkStatusTask::Stop()
{
	m_bStop = TRUE;
	return 0;
}

void CCheckNetworkStatusTask::TaskFinish()
{

}