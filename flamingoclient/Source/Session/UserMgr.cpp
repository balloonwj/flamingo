#include "stdafx.h"
#include "UserMgr.h"
#include "../net/IUProtocolData.h"
#include "..\Common\Util\MiniBuffer.h"
#include "UserSessionData.h"
#include "..\Common\Util\File2.h"
#include "..\Common\Util\Path.h"
#include "..\Common\Util\Utils.h"

CUserMgr::CUserMgr(void)
{
	m_hProxyWnd = NULL;
	m_hCallBackWnd = NULL;
}

CUserMgr::~CUserMgr(void)
{
}

// 获取用户文件夹存放路径
tstring CUserMgr::GetUserFolder()
{
	tstring strPath(g_szDataRoot);
	strPath += _T("Users\\");

	return strPath;
}

// 获取个人文件夹存放路径
tstring CUserMgr::GetPersonalFolder()
{
	tstring strPath(GetUserFolder());
	strPath += m_UserInfo.m_strAccount;
	strPath += _T("\\");
	
	return strPath;
}

// 获取聊天图片存放路径
tstring CUserMgr::GetChatPicFolder()
{
	return GetPersonalFolder()+_T("ChatImage\\");
}

tstring CUserMgr::GetCustomUserThumbFolder()
{
	return  GetPersonalFolder()+_T("UserThumb\\");
}

// 获取群成员头像图片全路径文件名
tstring CUserMgr::GetSessHeadPicFullName(UINT nUTalkNum)
{
	//TCHAR szPath[MAX_PATH] = {0};
	//LPCTSTR lpFmt = _T("%s%u\\HeadImage\\Sess\\%u.png");
	//wsprintf(szPath, lpFmt, m_strUserFolder.c_str(), m_nUTalkUin, nUTalkNum);
	//return szPath;

	return _T("");
}

// 获取聊天图片全路径文件名
tstring CUserMgr::GetChatPicFullName(LPCTSTR lpszFileName)
{
	if (NULL == lpszFileName || NULL ==*lpszFileName)
		return _T("");
	return GetChatPicFolder()+lpszFileName;
}

// 获取消息记录全路径文件名
tstring CUserMgr::GetMsgLogFullName()
{
	return GetPersonalFolder()+_T("ChatLog\\msg.db");
}

tstring CUserMgr::GetNickName(UINT uAccountID) const
{
	if(uAccountID == m_UserInfo.m_uUserID)
		return m_UserInfo.m_strNickName;

	CBuddyInfo* pBuddyInfo = NULL;

	CGroupInfo* pGroupInfo = NULL;
	for(size_t i=0; i<m_GroupList.m_arrGroupInfo.size(); ++i)
	{
		pGroupInfo = m_GroupList.m_arrGroupInfo[i];
        if (IsGroupTarget(uAccountID))
        {
            return pGroupInfo->m_strName;
        }
        else
        {
            for (size_t j = 0; j<pGroupInfo->m_arrMember.size(); ++j)
            {
                pBuddyInfo = pGroupInfo->m_arrMember[j];
                if (uAccountID == pBuddyInfo->m_uUserID)
                {
                    return pBuddyInfo->m_strNickName;
                }
            }
        }	
	}

	return _T("");
}



void CUserMgr::SetStatus(UINT uAccountID, long nStatus)
{
	if(uAccountID == 0)
		return;
	
	if(uAccountID == m_UserInfo.m_uUserID)
	{
		m_UserInfo.m_nStatus = nStatus;
	}

	CBuddyInfo* pBuddyInfo = NULL;

	CGroupInfo* pGroupInfo = NULL;
	for(size_t i=0; i<m_GroupList.m_arrGroupInfo.size(); ++i)
	{
		pGroupInfo = m_GroupList.m_arrGroupInfo[i];
		for(size_t j=0; j<pGroupInfo->m_arrMember.size(); ++j)
		{
			pBuddyInfo = pGroupInfo->m_arrMember[j];
			if(uAccountID == pBuddyInfo->m_uUserID)
			{
				pBuddyInfo->m_nStatus = nStatus;
				break;
			}
		}
	}

}

void CUserMgr::SetClientType(UINT uAccountID, long nNewClientType)
{
    if (uAccountID == 0)
        return;

    if (uAccountID == m_UserInfo.m_uUserID)
    {
        m_UserInfo.m_nClientType = nNewClientType;
    }

    CBuddyInfo* pBuddyInfo = NULL;

    CGroupInfo* pGroupInfo = NULL;
    for (size_t i = 0; i<m_GroupList.m_arrGroupInfo.size(); ++i)
    {
        pGroupInfo = m_GroupList.m_arrGroupInfo[i];
        for (size_t j = 0; j<pGroupInfo->m_arrMember.size(); ++j)
        {
            pBuddyInfo = pGroupInfo->m_arrMember[j];
            if (uAccountID == pBuddyInfo->m_uUserID)
            {
                pBuddyInfo->m_nClientType = nNewClientType;
                break;
            }
        }
    }
}

BOOL CUserMgr::SetMsgID(UINT uAccountID, UINT uMsgID)
{
	if(uAccountID==0 || uAccountID==m_UserInfo.m_uUserID)
		return FALSE;

	return FALSE;
}

void CUserMgr::ResetToOfflineStatus()
{
	m_UserInfo.m_nStatus = STATUS_OFFLINE;
	
	CBuddyInfo* pBuddyInfo = NULL;

	CGroupInfo* pGroupInfo = NULL;
	for(size_t i=0; i<m_GroupList.m_arrGroupInfo.size(); ++i)
	{
		pGroupInfo = m_GroupList.m_arrGroupInfo[i];
		for(size_t j=0; j<pGroupInfo->m_arrMember.size(); ++j)
		{
			pBuddyInfo = pGroupInfo->m_arrMember[j];
			pBuddyInfo->m_nStatus = STATUS_OFFLINE;
			
		}
	}
}

BOOL CUserMgr::LoadRecentList()
{
	CString strRecentFilePath;
	strRecentFilePath.Format(_T("%sUsers\\%s\\RecentSessionList.rsl"), g_szDataRoot, m_UserInfo.m_strAccount.c_str());
	CFile RecentFile;
	if(!RecentFile.Open(strRecentFilePath, FALSE))
		return FALSE;

	long nSize = RecentFile.GetSize();
	if(nSize <= 0)
		return FALSE;

	long nCount = nSize/sizeof(RecentInfo);
	if(nCount <= 0)
		return FALSE;

	RecentInfo* pRecentInfoFromFile = (RecentInfo*)RecentFile.Read();
	if(pRecentInfoFromFile == NULL)
		return FALSE;

	m_RecentList.m_arrRecentInfo.clear();
	CRecentInfo* pRecentInfo = NULL;
	for(long i=0; i<nCount; ++i)
	{
		if(pRecentInfoFromFile[i].uUserID==0 || pRecentInfoFromFile[i].nType<=0 || pRecentInfoFromFile[i].uMsgTime==0)
			continue;
		
		pRecentInfo	= new CRecentInfo();
		pRecentInfo->m_uUserID = pRecentInfoFromFile[i].uUserID;
		pRecentInfo->m_uFaceID = pRecentInfoFromFile[i].uFaceID;
		pRecentInfo->m_nType = pRecentInfoFromFile[i].nType;
		pRecentInfo->m_MsgTime = pRecentInfoFromFile[i].uMsgTime;
		_tcscpy_s(pRecentInfo->m_szName, ARRAYSIZE(pRecentInfo->m_szName), pRecentInfoFromFile[i].szName);
		_tcscpy_s(pRecentInfo->m_szLastMsgText, ARRAYSIZE(pRecentInfo->m_szLastMsgText), pRecentInfoFromFile[i].szLastMsgText);
		m_RecentList.m_arrRecentInfo.push_back(pRecentInfo);
	}

	return TRUE;
}

BOOL CUserMgr::StoreRecentList()
{
	CString strRecentFilePath;
	strRecentFilePath.Format(_T("%sUsers\\%s\\RecentSessionList.rsl"), g_szDataRoot, m_UserInfo.m_strAccount.c_str());
	long nCount = m_RecentList.GetRecentCount();
	//如果最近联系人个数为0，删除最近联系人文件
	if(nCount <= 0)
	{
		::DeleteFile(strRecentFilePath);
		return TRUE;
	}

	CMiniBuffer miniBuffer(nCount*sizeof(RecentInfo));
	RecentInfo* pBufferToWrite = (RecentInfo*)miniBuffer.GetBuffer();
	CRecentInfo* pRecentInfo = NULL;
	for(long i=0; i<nCount; ++i)
	{
		pRecentInfo = m_RecentList.GetRecent(i);
		if(pRecentInfo == NULL)
			continue;
		pBufferToWrite[i].nType = pRecentInfo->m_nType;
		pBufferToWrite[i].uUserID = pRecentInfo->m_uUserID;
		pBufferToWrite[i].uFaceID = pRecentInfo->m_uFaceID;
		pBufferToWrite[i].uMsgTime = pRecentInfo->m_MsgTime;
		_tcscpy_s(pBufferToWrite[i].szName, ARRAYSIZE(pBufferToWrite[i].szName), pRecentInfo->m_szName);
		_tcscpy_s(pBufferToWrite[i].szLastMsgText, ARRAYSIZE(pBufferToWrite[i].szLastMsgText), pRecentInfo->m_szLastMsgText);
	}

	CFile RecentFile;
    //FIXME: 将int64_t强制转换成int32可能会有问题
	if(!RecentFile.Open(strRecentFilePath, TRUE) || !RecentFile.Write(miniBuffer.GetBuffer(), (int)miniBuffer.GetSize()))
		return FALSE;

	return TRUE;
}

void CUserMgr::ClearUserInfo()
{
	//企业版说明：v1 好友列表已删除，这里只需清群组信息。原先在此逐个 delete 好友分组 /
	//好友对象的循环随 m_BuddyList 一并移除。
	m_GroupList.Reset();
}
