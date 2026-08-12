#include "stdafx.h"
#include "UserMgr.h"
#include "net/IUProtocolData.h"
#include "MiniBuffer.h"
#include "UserSessionData.h"
#include "File2.h"
#include "Path.h"
#include "Utils.h"

CUserMgr& CUserMgr::GetInstance()
{
    static CUserMgr user;
    return user;
}

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

// 获取用户头像图片全路径文件名
tstring CUserMgr::GetUserHeadPicFullName(UINT uFace)
{
	TCHAR szPath[MAX_PATH] = {0};
	_stprintf_s(szPath, MAX_PATH, _T("%sImage\\UserThumbs\\%u.png"), g_szHomePath, uFace);
	tstring strPath(szPath);

	return strPath;
}

// 获取好友头像图片全路径文件名
tstring CUserMgr::GetBuddyHeadPicFullName(UINT uFace)
{
	return GetUserHeadPicFullName(uFace);
}

// 获取群头像图片全路径文件名
tstring CUserMgr::GetGroupHeadPicFullName(UINT nGroupNum)
{
	//TCHAR szPath[MAX_PATH] = {0};
	//LPCTSTR lpFmt = _T("%s%u\\HeadImage\\Group\\%u.png");
	//wsprintf(szPath, lpFmt, m_strUserFolder.c_str(), m_nUTalkUin, nGroupNum);
	//return szPath;

	return _T("");
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

tstring CUserMgr::GetDefaultRecvFilePath()
{
	tstring strRecvFilePath;

	// 优先用用户在系统设置里选的自定义接收目录；未设置时退回默认推导路径
	// （我的文档\Flamingo\FileRecv\账号）。
	if (!m_strCustomRecvPath.empty())
	{
		strRecvFilePath = m_strCustomRecvPath;
	}
	else
	{
		TCHAR szMyDocumentPath[MAX_PATH] = {0};
		::SHGetSpecialFolderPath(NULL, szMyDocumentPath, CSIDL_MYDOCUMENTS, TRUE);

		strRecvFilePath = szMyDocumentPath;
		strRecvFilePath += _T("\\Flamingo\\FileRecv\\");
		strRecvFilePath += m_UserInfo.m_strAccount;
	}

	if(!Hootina::CPath::IsDirectoryExist(strRecvFilePath.c_str()))
	{
		if(ERROR_SUCCESS != ::SHCreateDirectoryEx(NULL, strRecvFilePath.c_str(), NULL))
			return _T("");
	}

	return strRecvFilePath;
}

void CUserMgr::SetCustomRecvFilePath(PCTSTR pszPath)
{
	m_strCustomRecvPath = (pszPath != NULL) ? pszPath : _T("");
}

// 判断是否需要更新好友头像
BOOL CUserMgr::IsNeedUpdateBuddyHeadPic(UINT nUTalkNum)
{
	tstring strFullName = GetBuddyHeadPicFullName(0);

	if (!Hootina::CPath::IsFileExist(strFullName.c_str()))
		return TRUE;

	SYSTEMTIME stLastWriteTime = {0};
	GetFileLastWriteTime(strFullName.c_str(), &stLastWriteTime);

	return ((!IsToday(&stLastWriteTime)) ? TRUE : FALSE);
}

// 判断是否需要更新群头像
BOOL CUserMgr::IsNeedUpdateGroupHeadPic(UINT nGroupNum)
{
	tstring strFullName = GetGroupHeadPicFullName(nGroupNum);

	if (!Hootina::CPath::IsFileExist(strFullName.c_str()))
		return TRUE;

	SYSTEMTIME stLastWriteTime = {0};
	GetFileLastWriteTime(strFullName.c_str(), &stLastWriteTime);

	return ((!IsToday(&stLastWriteTime)) ? TRUE : FALSE);
}

// 判断是否需要更新群成员头像
BOOL CUserMgr::IsNeedUpdateSessHeadPic(UINT nUTalkNum)
{
	tstring strFullName = GetSessHeadPicFullName(nUTalkNum);

	if (!Hootina::CPath::IsFileExist(strFullName.c_str()))
		return TRUE;

	SYSTEMTIME stLastWriteTime = {0};
	GetFileLastWriteTime(strFullName.c_str(), &stLastWriteTime);

	return ((!IsToday(&stLastWriteTime)) ? TRUE : FALSE);
}

BOOL CUserMgr::IsSelf(PCTSTR pszAccountName)
{
	return m_UserInfo.m_strAccount==pszAccountName;
}

tstring CUserMgr::GetAccountName(UINT uAccountID) const
{
	if(uAccountID == m_UserInfo.m_uUserID)
		return m_UserInfo.m_strAccount;

	return _T("");
}

void CUserMgr::SetAccountName(UINT uAccountID, PCTSTR pszAccountName)
{
	if(pszAccountName== NULL || pszAccountName[0]==NULL)
		return;

	if(uAccountID == m_UserInfo.m_uUserID)
	{
		m_UserInfo.m_strAccount = pszAccountName;
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
				pBuddyInfo->m_strAccount = pszAccountName;
				break;
			}
		}
	}
	
}

tstring CUserMgr::GetGroupName(UINT uGroupCode)
{
	CGroupInfo* pGroupInfo = NULL;
	for(size_t i=0; i<m_GroupList.m_arrGroupInfo.size(); ++i)
	{
		pGroupInfo = m_GroupList.m_arrGroupInfo[i];
		if(pGroupInfo->m_nGroupCode == uGroupCode)
			return pGroupInfo->m_strName;
	}

	return _T("");
}

tstring	CUserMgr::GetGroupAccount(UINT uGroupCode)
{
	CGroupInfo* pGroupInfo = NULL;
	for(size_t i=0; i<m_GroupList.m_arrGroupInfo.size(); ++i)
	{
		pGroupInfo = m_GroupList.m_arrGroupInfo[i];
		if(pGroupInfo->m_nGroupCode == uGroupCode)
			return pGroupInfo->m_strAccount;
	}

	return _T("");
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

void CUserMgr::SetNickName(UINT uAccountID, PCTSTR pszNickName)
{
	if(pszNickName==NULL || pszNickName[0]==NULL)
		return;
	
	if(uAccountID == m_UserInfo.m_uUserID)
	{
		m_UserInfo.m_strNickName = pszNickName;
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
				pBuddyInfo->m_strNickName = pszNickName;
				break;
			}
		}
	}

}

UINT CUserMgr::GetFaceID(UINT uAccountID) const
{
	if(uAccountID == m_UserInfo.m_uUserID)
		return m_UserInfo.m_uUserID;
	

	return 0;
}
	
void CUserMgr::SetFaceID(UINT uAccountID, UINT uFaceID)
{
	if(uAccountID == m_UserInfo.m_uUserID)
	{
		m_UserInfo.m_nFace = uFaceID;
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
				pBuddyInfo->m_nFace = uFaceID;
				break;
			}
		}
	}

}

PCTSTR CUserMgr::GetCustomFace(UINT uAccountID) const
{
	if(uAccountID == m_UserInfo.m_uUserID)
	{
		return m_UserInfo.m_strCustomFace.c_str();;
	}
	
	return NULL;
}


BOOL CUserMgr::ExitGroup(UINT uGroupID)
{
	if(!IsGroupTarget(uGroupID))
		return FALSE;

	CGroupInfo* pGroupInfo = NULL;
	CBuddyInfo* pBuddyInfo = NULL;
	for(std::vector<CGroupInfo*>::iterator iter=m_GroupList.m_arrGroupInfo.begin(); iter!=m_GroupList.m_arrGroupInfo.end(); ++iter)
	{
		pGroupInfo =*iter;
		if(pGroupInfo->m_nGroupCode == uGroupID)
		{
			DEL(pGroupInfo);
			m_GroupList.m_arrGroupInfo.erase(iter);
			return TRUE;
		}
	}

	return FALSE;
}

BOOL CUserMgr::IsGroupMember(UINT uAccountID, UINT uGroupID)
{
	CGroupInfo* pGroupInfo = m_GroupList.GetGroupByCode(uGroupID);
	if(pGroupInfo == NULL)
		return FALSE;
	CBuddyInfo* pBuddyInfo = pGroupInfo->GetMemberByUin(uAccountID);
	if(pBuddyInfo == NULL)
		return FALSE;

	return TRUE;
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

long CUserMgr::GetStatus(UINT uAccountID)
{
	if(uAccountID == m_UserInfo.m_uUserID)
		return m_UserInfo.m_nStatus;

	return -1;
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

long CUserMgr::GetClientType(UINT uAccountID)
{
    if (uAccountID == m_UserInfo.m_uUserID)
        return m_UserInfo.m_nClientType;

    return -1;
}

UINT CUserMgr::GetMsgID(UINT uAccountID) const
{
	if(uAccountID==0 || uAccountID==m_UserInfo.m_uUserID)
		return 0;

	return 0;
}
	
BOOL CUserMgr::SetMsgID(UINT uAccountID, UINT uMsgID)
{
	if(uAccountID==0 || uAccountID==m_UserInfo.m_uUserID)
		return FALSE;

	return FALSE;
}

tstring CUserMgr::GetNickName(PCTSTR pszAccountName) const
{
	if(pszAccountName==NULL ||*pszAccountName==NULL)
		return _T("");

	if(m_UserInfo.m_strAccount == pszAccountName)
		return m_UserInfo.m_strNickName;

	return _T("");
}

UINT CUserMgr::GetFaceID(PCTSTR pszAccountName) const
{
	if(pszAccountName==NULL ||*pszAccountName==NULL)
		return 0;

	if(m_UserInfo.m_strAccount == pszAccountName)
		return m_UserInfo.m_nFace;

	return 0;
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

long CUserMgr::GetRecentListCount()
{
	return m_RecentList.GetRecentCount();
}

void CUserMgr::ClearRecentList()
{
	m_RecentList.Reset();

	CString strRecentFilePath;
	strRecentFilePath.Format(_T("%sUsers\\%s\\RecentSessionList.rsl"), g_szDataRoot, m_UserInfo.m_strAccount.c_str());
	::DeleteFile(strRecentFilePath);
}

BOOL CUserMgr::DeleteRecentItem(UINT uAccountID)
{
	if(uAccountID == 0)
		return FALSE;

	return m_RecentList.DeleteRecentItem(uAccountID);
}

void CUserMgr::ClearUserInfo()
{
	//企业版说明：v1 好友列表已删除，这里只需清群组信息。原先在此逐个 delete 好友分组 /
	//好友对象的循环随 m_BuddyList 一并移除。
	m_GroupList.Reset();
}

long CUserMgr::GetTotalBuddyCount()
{
	long nCount = 0;
	CGroupInfo* pGroupInfo = NULL;
	for(std::vector<CGroupInfo*>::iterator iter=m_GroupList.m_arrGroupInfo.begin(); iter!=m_GroupList.m_arrGroupInfo.end(); ++iter)
	{
		pGroupInfo =*iter;
		if(pGroupInfo == NULL)
			continue;

		nCount += (long)pGroupInfo->m_arrMember.size();
	}
	
	//算上自己
	nCount += 1;

	return nCount;
}