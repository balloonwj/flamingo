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
	tstring strPath(g_szHomePath);
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
	TCHAR szMyDocumentPath[MAX_PATH] = {0};
	::SHGetSpecialFolderPath(NULL, szMyDocumentPath, CSIDL_MYDOCUMENTS, TRUE);

	tstring strRecvFilePath(szMyDocumentPath);
	strRecvFilePath += _T("\\Flamingo\\FileRecv\\");
	strRecvFilePath += m_UserInfo.m_strAccount;

	if(!Hootina::CPath::IsDirectoryExist(strRecvFilePath.c_str()))
	{
		if(ERROR_SUCCESS != ::SHCreateDirectoryEx(NULL, strRecvFilePath.c_str(), NULL))
			return _T("");
	}

	return strRecvFilePath;
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


BOOL CUserMgr::IsFriend(UINT uAccountID)
{
	CBuddyTeamInfo* pTeamInfo = NULL;
	CBuddyInfo* pBuddyInfo = NULL;
	for(size_t i=0; i<m_BuddyList.m_arrBuddyTeamInfo.size(); ++i)
	{
		pTeamInfo = m_BuddyList.m_arrBuddyTeamInfo[i];
		for(size_t j=0; j<pTeamInfo->m_arrBuddyInfo.size(); ++j)
		{
			pBuddyInfo = pTeamInfo->m_arrBuddyInfo[j];
			if(uAccountID == pBuddyInfo->m_uUserID)
				return TRUE;
		}
	}

	return FALSE;
}

BOOL CUserMgr::IsFriend(PCTSTR pszAccountName)
{
	CBuddyTeamInfo* pTeamInfo = NULL;
	CBuddyInfo* pBuddyInfo = NULL;
	for(size_t i=0; i<m_BuddyList.m_arrBuddyTeamInfo.size(); ++i)
	{
		pTeamInfo = m_BuddyList.m_arrBuddyTeamInfo[i];
		for(size_t j=0; j<pTeamInfo->m_arrBuddyInfo.size(); ++j)
		{
			pBuddyInfo = pTeamInfo->m_arrBuddyInfo[j];
			if(pBuddyInfo->m_strAccount == pszAccountName)
				return TRUE;
		}
	}

	return FALSE;
}

tstring CUserMgr::GetAccountName(UINT uAccountID) const
{
	if(uAccountID == m_UserInfo.m_uUserID)
		return m_UserInfo.m_strAccount;

	CBuddyTeamInfo* pTeamInfo = NULL;
	CBuddyInfo* pBuddyInfo = NULL;
	for(size_t i=0; i<m_BuddyList.m_arrBuddyTeamInfo.size(); ++i)
	{
		pTeamInfo = m_BuddyList.m_arrBuddyTeamInfo[i];
		for(size_t j=0; j<pTeamInfo->m_arrBuddyInfo.size(); ++j)
		{
			pBuddyInfo = pTeamInfo->m_arrBuddyInfo[j];
			if(uAccountID == pBuddyInfo->m_uUserID)
				return pBuddyInfo->m_strAccount;
		}
	}

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

	CBuddyTeamInfo* pTeamInfo = NULL;
	CBuddyInfo* pBuddyInfo = NULL;
	for(size_t i=0; i<m_BuddyList.m_arrBuddyTeamInfo.size(); ++i)
	{
		pTeamInfo = m_BuddyList.m_arrBuddyTeamInfo[i];
		for(size_t j=0; j<pTeamInfo->m_arrBuddyInfo.size(); ++j)
		{
			pBuddyInfo = pTeamInfo->m_arrBuddyInfo[j];
			if(uAccountID == pBuddyInfo->m_uUserID)
			{
				pBuddyInfo->m_strAccount = pszAccountName;
				break;
			}
		}
	}

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

	CBuddyTeamInfo* pTeamInfo = NULL;
	CBuddyInfo* pBuddyInfo = NULL;
	for(size_t i=0; i<m_BuddyList.m_arrBuddyTeamInfo.size(); ++i)
	{
		pTeamInfo = m_BuddyList.m_arrBuddyTeamInfo[i];
		for(size_t j=0; j<pTeamInfo->m_arrBuddyInfo.size(); ++j)
		{
			pBuddyInfo = pTeamInfo->m_arrBuddyInfo[j];
			if(uAccountID == pBuddyInfo->m_uUserID)
				return pBuddyInfo->m_strNickName;
		}
	}

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

	CBuddyTeamInfo* pTeamInfo = NULL;
	CBuddyInfo* pBuddyInfo = NULL;
	for(size_t i=0; i<m_BuddyList.m_arrBuddyTeamInfo.size(); ++i)
	{
		pTeamInfo = m_BuddyList.m_arrBuddyTeamInfo[i];
		for(size_t j=0; j<pTeamInfo->m_arrBuddyInfo.size(); ++j)
		{
			pBuddyInfo = pTeamInfo->m_arrBuddyInfo[j];
			if(uAccountID == pBuddyInfo->m_uUserID)
			{
				pBuddyInfo->m_strNickName = pszNickName;
				break;
			}
		}
	}

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
	

	CBuddyTeamInfo* pTeamInfo = NULL;
	CBuddyInfo* pBuddyInfo = NULL;
	for(size_t i=0; i<m_BuddyList.m_arrBuddyTeamInfo.size(); ++i)
	{
		pTeamInfo = m_BuddyList.m_arrBuddyTeamInfo[i];
		for(size_t j=0; j<pTeamInfo->m_arrBuddyInfo.size(); ++j)
		{
			pBuddyInfo = pTeamInfo->m_arrBuddyInfo[j];
			if(uAccountID == pBuddyInfo->m_uUserID)
				return pBuddyInfo->m_nFace;
		}
	}

	return 0;
}
	
void CUserMgr::SetFaceID(UINT uAccountID, UINT uFaceID)
{
	if(uAccountID == m_UserInfo.m_uUserID)
	{
		m_UserInfo.m_nFace = uFaceID;
	}
	
	CBuddyTeamInfo* pTeamInfo = NULL;
	CBuddyInfo* pBuddyInfo = NULL;
	for(size_t i=0; i<m_BuddyList.m_arrBuddyTeamInfo.size(); ++i)
	{
		pTeamInfo = m_BuddyList.m_arrBuddyTeamInfo[i];
		for(size_t j=0; j<pTeamInfo->m_arrBuddyInfo.size(); ++j)
		{
			pBuddyInfo = pTeamInfo->m_arrBuddyInfo[j];
			if(uAccountID == pBuddyInfo->m_uUserID)
			{
				pBuddyInfo->m_nFace = uFaceID;
				break;;
			}
		}
	}

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
	
	CBuddyTeamInfo* pTeamInfo = NULL;
	CBuddyInfo* pBuddyInfo = NULL;
	for(size_t i=0; i<m_BuddyList.m_arrBuddyTeamInfo.size(); ++i)
	{
		pTeamInfo = m_BuddyList.m_arrBuddyTeamInfo[i];
		for(size_t j=0; j<pTeamInfo->m_arrBuddyInfo.size(); ++j)
		{
			pBuddyInfo = pTeamInfo->m_arrBuddyInfo[j];
			if(uAccountID == pBuddyInfo->m_uUserID)
			{
				return pBuddyInfo->m_strCustomFace.c_str();
			}
		}
	}

	return NULL;
}

BOOL CUserMgr::AddFriend(UINT uAccountID, PCTSTR pszAccountName, PCTSTR pszNickName)
{	
	if(uAccountID==0 || uAccountID==m_UserInfo.m_uUserID)
		return FALSE;

	if(pszAccountName==NULL || pszAccountName[0]==NULL || pszNickName==NULL || pszNickName[0]==NULL)
		return FALSE;
	
	if(IsFriend(uAccountID))
		return FALSE;

	CBuddyInfo* pBuddyInfo = new CBuddyInfo();
	pBuddyInfo->m_uUserID = uAccountID;
	pBuddyInfo->m_strAccount = pszAccountName;
	pBuddyInfo->m_strNickName = pszNickName;
	
	//TODO: 目前只有一个分组
	size_t nTeamCount = m_BuddyList.m_arrBuddyTeamInfo.size();
	if(nTeamCount == 0)
		return FALSE;
	
	CBuddyTeamInfo* pTeamInfo = m_BuddyList.m_arrBuddyTeamInfo[0];
	pTeamInfo->m_arrBuddyInfo.push_back(pBuddyInfo);
	
	return TRUE;
}

BOOL CUserMgr::DeleteFriend(UINT uAccountID)
{
	if(uAccountID==0 || uAccountID==m_UserInfo.m_uUserID)
		return FALSE;

	if(!IsFriend(uAccountID))
		return FALSE;

	CBuddyTeamInfo* pTeamInfo = NULL;
	CBuddyInfo* pBuddyInfo = NULL;
	for(size_t i=0; i<m_BuddyList.m_arrBuddyTeamInfo.size(); ++i)
	{
		pTeamInfo = m_BuddyList.m_arrBuddyTeamInfo[i];
		for(std::vector<CBuddyInfo*>::iterator iter=pTeamInfo->m_arrBuddyInfo.begin(); iter!=pTeamInfo->m_arrBuddyInfo.end(); ++iter)
		{
			pBuddyInfo =*iter;
			if(uAccountID == pBuddyInfo->m_uUserID)
			{
				DEL(pBuddyInfo);
				pTeamInfo->m_arrBuddyInfo.erase(iter);
				return TRUE;
			}
		}
	}

	return FALSE;
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

	CBuddyTeamInfo* pTeamInfo = NULL;
	CBuddyInfo* pBuddyInfo = NULL;
	for(size_t i=0; i<m_BuddyList.m_arrBuddyTeamInfo.size(); ++i)
	{
		pTeamInfo = m_BuddyList.m_arrBuddyTeamInfo[i];
		for(size_t j=0; j<pTeamInfo->m_arrBuddyInfo.size(); ++j)
		{
			pBuddyInfo = pTeamInfo->m_arrBuddyInfo[j];
			if(uAccountID == pBuddyInfo->m_uUserID)
			{
				pBuddyInfo->m_nStatus = nStatus;
				break;
			}
		}
	}

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

	CBuddyTeamInfo* pTeamInfo = NULL;
	CBuddyInfo* pBuddyInfo = NULL;
	for(size_t i=0; i<m_BuddyList.m_arrBuddyTeamInfo.size(); ++i)
	{
		pTeamInfo = m_BuddyList.m_arrBuddyTeamInfo[i];
		for(size_t j=0; j<pTeamInfo->m_arrBuddyInfo.size(); ++j)
		{
			pBuddyInfo = pTeamInfo->m_arrBuddyInfo[j];
			if(uAccountID == pBuddyInfo->m_uUserID)
			{
				return pBuddyInfo->m_nStatus;
			}
		}
	}

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

    CBuddyTeamInfo* pTeamInfo = NULL;
    CBuddyInfo* pBuddyInfo = NULL;
    for (size_t i = 0; i<m_BuddyList.m_arrBuddyTeamInfo.size(); ++i)
    {
        pTeamInfo = m_BuddyList.m_arrBuddyTeamInfo[i];
        for (size_t j = 0; j<pTeamInfo->m_arrBuddyInfo.size(); ++j)
        {
            pBuddyInfo = pTeamInfo->m_arrBuddyInfo[j];
            if (uAccountID == pBuddyInfo->m_uUserID)
            {
                pBuddyInfo->m_nClientType = nNewClientType;
                break;
            }
        }
    }

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

    CBuddyTeamInfo* pTeamInfo = NULL;
    CBuddyInfo* pBuddyInfo = NULL;
    for (size_t i = 0; i<m_BuddyList.m_arrBuddyTeamInfo.size(); ++i)
    {
        pTeamInfo = m_BuddyList.m_arrBuddyTeamInfo[i];
        for (size_t j = 0; j<pTeamInfo->m_arrBuddyInfo.size(); ++j)
        {
            pBuddyInfo = pTeamInfo->m_arrBuddyInfo[j];
            if (uAccountID == pBuddyInfo->m_uUserID)
            {
                return pBuddyInfo->m_nClientType;
            }
        }
    }

    return -1;
}

UINT CUserMgr::GetMsgID(UINT uAccountID) const
{
	if(uAccountID==0 || uAccountID==m_UserInfo.m_uUserID)
		return 0;

	CBuddyTeamInfo* pTeamInfo = NULL;
	CBuddyInfo* pBuddyInfo = NULL;
	for(size_t i=0; i<m_BuddyList.m_arrBuddyTeamInfo.size(); ++i)
	{
		pTeamInfo = m_BuddyList.m_arrBuddyTeamInfo[i];
		for(size_t j=0; j<pTeamInfo->m_arrBuddyInfo.size(); ++j)
		{
			pBuddyInfo = pTeamInfo->m_arrBuddyInfo[j];
			if(uAccountID == pBuddyInfo->m_uUserID)
			{
				return pBuddyInfo->m_uMsgID;
			}
		}
	}

	return 0;
}
	
BOOL CUserMgr::SetMsgID(UINT uAccountID, UINT uMsgID)
{
	if(uAccountID==0 || uAccountID==m_UserInfo.m_uUserID)
		return FALSE;

	CBuddyTeamInfo* pTeamInfo = NULL;
	CBuddyInfo* pBuddyInfo = NULL;
	for(size_t i=0; i<m_BuddyList.m_arrBuddyTeamInfo.size(); ++i)
	{
		pTeamInfo = m_BuddyList.m_arrBuddyTeamInfo[i];
		for(size_t j=0; j<pTeamInfo->m_arrBuddyInfo.size(); ++j)
		{
			pBuddyInfo = pTeamInfo->m_arrBuddyInfo[j];
			if(uAccountID == pBuddyInfo->m_uUserID)
			{
				pBuddyInfo->m_uMsgID = uMsgID;
				return TRUE;
			}
		}
	}

	return FALSE;
}

tstring CUserMgr::GetNickName(PCTSTR pszAccountName) const
{
	if(pszAccountName==NULL ||*pszAccountName==NULL)
		return _T("");

	if(m_UserInfo.m_strAccount == pszAccountName)
		return m_UserInfo.m_strNickName;

	CBuddyTeamInfo* pTeamInfo = NULL;
	CBuddyInfo* pBuddyInfo = NULL;
	for(size_t i=0; i<m_BuddyList.m_arrBuddyTeamInfo.size(); ++i)
	{
		pTeamInfo = m_BuddyList.m_arrBuddyTeamInfo[i];
		for(size_t j=0; j<pTeamInfo->m_arrBuddyInfo.size(); ++j)
		{
			pBuddyInfo = pTeamInfo->m_arrBuddyInfo[j];
			if(pBuddyInfo->m_strAccount == pszAccountName)
			{
				return pBuddyInfo->m_strNickName;
			}
		}
	}

	return _T("");
}

UINT CUserMgr::GetFaceID(PCTSTR pszAccountName) const
{
	if(pszAccountName==NULL ||*pszAccountName==NULL)
		return 0;

	if(m_UserInfo.m_strAccount == pszAccountName)
		return m_UserInfo.m_nFace;

	CBuddyTeamInfo* pTeamInfo = NULL;
	CBuddyInfo* pBuddyInfo = NULL;
	for(size_t i=0; i<m_BuddyList.m_arrBuddyTeamInfo.size(); ++i)
	{
		pTeamInfo = m_BuddyList.m_arrBuddyTeamInfo[i];
		for(size_t j=0; j<pTeamInfo->m_arrBuddyInfo.size(); ++j)
		{
			pBuddyInfo = pTeamInfo->m_arrBuddyInfo[j];
			if(pBuddyInfo->m_strAccount == pszAccountName)
			{
				return pBuddyInfo->m_nFace;
			}
		}
	}

	return 0;
}

void CUserMgr::ResetToOfflineStatus()
{
	m_UserInfo.m_nStatus = STATUS_OFFLINE;
	
	CBuddyTeamInfo* pTeamInfo = NULL;
	CBuddyInfo* pBuddyInfo = NULL;
	for(size_t i=0; i<m_BuddyList.m_arrBuddyTeamInfo.size(); ++i)
	{
		pTeamInfo = m_BuddyList.m_arrBuddyTeamInfo[i];
		for(size_t j=0; j<pTeamInfo->m_arrBuddyInfo.size(); ++j)
		{
			pBuddyInfo = pTeamInfo->m_arrBuddyInfo[j];
			pBuddyInfo->m_nStatus = STATUS_OFFLINE;
			::PostMessage(m_hProxyWnd, FMG_MSG_UPDATE_BUDDY_HEADPIC, 0, (LPARAM)pBuddyInfo->m_uUserID);
		}
	}

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
	strRecentFilePath.Format(_T("%sUsers\\%s\\RecentSessionList.rsl"), g_szHomePath, m_UserInfo.m_strAccount.c_str());
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
	strRecentFilePath.Format(_T("%sUsers\\%s\\RecentSessionList.rsl"), g_szHomePath, m_UserInfo.m_strAccount.c_str());
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
	strRecentFilePath.Format(_T("%sUsers\\%s\\RecentSessionList.rsl"), g_szHomePath, m_UserInfo.m_strAccount.c_str());
	::DeleteFile(strRecentFilePath);
}

BOOL CUserMgr::DeleteRecentItem(UINT uAccountID)
{
	if(uAccountID == 0)
		return FALSE;

	return m_RecentList.DeleteRecentItem(uAccountID);
}

BOOL CUserMgr::LoadTeamInfo()
{
	CString strTeamInfoFilePath;
	strTeamInfoFilePath.Format(_T("%sUsers\\%s\\BuddyTeamInfo.bti"), g_szHomePath, m_UserInfo.m_strAccount.c_str());
	CFile file;
	if(!file.Open(strTeamInfoFilePath, FALSE))
		return FALSE;

	long nSize = file.GetSize();
	if(nSize <= 0)
		return FALSE;

	long nCount = nSize/sizeof(BuddyTeamInfo);
	if(nCount <= 0)
		return FALSE;

	BuddyTeamInfo* pBuddyTeamInfo = (BuddyTeamInfo*)file.Read();
	if(pBuddyTeamInfo == NULL)
		return FALSE;

	m_BuddyList.Reset();
	CBuddyTeamInfo* pTeamInfo = NULL;
	for(long i=0; i<nCount; ++i)
	{
		if(pBuddyTeamInfo[i].nTeamIndex<0 || pBuddyTeamInfo[i].nTeamIndex>nCount-1 || pBuddyTeamInfo[i].szTeamName[0]==NULL)
			continue;
		
		pTeamInfo	= new CBuddyTeamInfo();
		pTeamInfo->m_nIndex = pBuddyTeamInfo[i].nTeamIndex;
		pTeamInfo->m_strName = pBuddyTeamInfo[i].szTeamName;
		m_BuddyList.m_arrBuddyTeamInfo.push_back(pTeamInfo);
	}

	return TRUE;
}

BOOL CUserMgr::StoreTeamInfo()
{
	long nCount = m_BuddyList.GetBuddyTeamCount();
	if(nCount <= 0)
		return TRUE;

	CMiniBuffer miniBuffer(nCount*sizeof(BuddyTeamInfo));
	BuddyTeamInfo* pBufferToWrite = (BuddyTeamInfo*)miniBuffer.GetBuffer();
	CBuddyTeamInfo* pBuddyTeamInfo = NULL;
	for(long i=0; i<nCount; ++i)
	{
		pBuddyTeamInfo = m_BuddyList.m_arrBuddyTeamInfo[i];
		if(pBuddyTeamInfo == NULL)
			continue;
		pBufferToWrite->nTeamIndex = pBuddyTeamInfo->m_nIndex;
		_tcscpy_s(pBufferToWrite->szTeamName, ARRAYSIZE(pBufferToWrite->szTeamName), pBuddyTeamInfo->m_strName.c_str());

		++pBufferToWrite;
	}

	CString strTeamInfoFilePath;
	strTeamInfoFilePath.Format(_T("%sUsers\\%s\\BuddyTeamInfo.bti"), g_szHomePath, m_UserInfo.m_strAccount.c_str());
	CFile file;
    //FIXME: 将int64_t强制转换成int32可能会有问题
	if(!file.Open(strTeamInfoFilePath, TRUE) || !file.Write(miniBuffer.GetBuffer(), (int)miniBuffer.GetSize()))
		return FALSE;

	return TRUE;
}

BOOL CUserMgr::LoadBuddyInfo()
{
	CString strBuddyInfoFilePath;
	strBuddyInfoFilePath.Format(_T("%sUsers\\%s\\BuddyInfo.bi"), g_szHomePath, m_UserInfo.m_strAccount.c_str());
	CFile file;
	if(!file.Open(strBuddyInfoFilePath, FALSE))
		return FALSE;

	long nSize = file.GetSize();
	if(nSize <= 0)
		return FALSE;

	long nCount = nSize/sizeof(BuddyInfo);
	if(nCount <= 0)
		return FALSE;

	BuddyInfo* pBuddyInfo = (BuddyInfo*)file.Read();
	if(pBuddyInfo == NULL)
		return FALSE;

	BuddyInfo* p = NULL;
	for(long i=0; i<nCount; ++i)
	{
		p = new BuddyInfo();
		memset(p, 0, sizeof(BuddyInfo));
		memcpy(p, &pBuddyInfo[i], sizeof(BuddyInfo));
		m_mapLocalBuddyInfo.insert(std::pair<UINT, BuddyInfo*>(p->uAccountID, p));
	}

	return TRUE;
}

BOOL CUserMgr::SaveBuddyInfo()
{
	//先释放内存
	BuddyInfo* pbi = NULL;
	std::map<UINT, BuddyInfo*>::iterator iter;
	while(!m_mapLocalBuddyInfo.empty())
	{
		iter = m_mapLocalBuddyInfo.begin();
		pbi = iter->second;
		DEL(pbi);
		m_mapLocalBuddyInfo.erase(iter);
	}
	
	long nTotalBuddyCount = m_BuddyList.GetBuddyTotalCount();
	if(nTotalBuddyCount <= 0)
		return TRUE;

	CMiniBuffer miniBuffer(nTotalBuddyCount*sizeof(BuddyInfo));
	BuddyInfo* bi = (BuddyInfo*)miniBuffer.GetBuffer();
	
	CBuddyTeamInfo* pBuddyTeamInfo = NULL;
	CBuddyInfo* pBuddyInfo = NULL;
	
	long nIndex = 0;
	size_t nTeamCount = m_BuddyList.GetBuddyTeamCount();
	for(size_t i=0; i<nTeamCount; ++i)
	{
		pBuddyTeamInfo = m_BuddyList.m_arrBuddyTeamInfo[i];
		if(pBuddyTeamInfo == NULL)
			continue;
		for(size_t j=0; j<pBuddyTeamInfo->m_arrBuddyInfo.size(); ++j)
		{
			pBuddyInfo = pBuddyTeamInfo->m_arrBuddyInfo[j];
			if(pBuddyInfo == NULL)
				continue;

			bi[nIndex].uAccountID = pBuddyInfo->m_uUserID;
			_tcscpy_s(bi[nIndex].szAccountName, ARRAYSIZE(bi[nIndex].szAccountName), pBuddyInfo->m_strAccount.c_str());
			_tcscpy_s(bi[nIndex].szMarkName, ARRAYSIZE(bi[nIndex].szMarkName), pBuddyInfo->m_strMarkName.c_str());
			bi[nIndex].nTeamIndex = pBuddyInfo->m_nTeamIndex;
			bi[nIndex].nGroupCount = 0;
			memset(bi[nIndex].GroupID, 0, sizeof(bi[nIndex].GroupID));

			++nIndex;
		}
	}

	CString strBuddyInfoFilePath;
	strBuddyInfoFilePath.Format(_T("%sUsers\\%s\\BuddyInfo.bi"), g_szHomePath, m_UserInfo.m_strAccount.c_str());
	CFile file;
    //FIXME: 将int64_t强制转换成int32可能会有问题
	if(!file.Open(strBuddyInfoFilePath, TRUE) || !file.Write(miniBuffer.GetBuffer(), (int)miniBuffer.GetSize()))
		return FALSE;

	return TRUE;
}

PCTSTR CUserMgr::GetMarkNameByAccountID(UINT uAccountID)
{
	std::map<UINT, BuddyInfo*>::iterator iter = m_mapLocalBuddyInfo.find(uAccountID);
	if(iter != m_mapLocalBuddyInfo.end())
		return iter->second->szMarkName;

	return NULL;
}

long CUserMgr::GetTeamIndexByAccountID(UINT uAccountID)
{
	std::map<UINT, BuddyInfo*>::iterator iter = m_mapLocalBuddyInfo.find(uAccountID);
	if(iter != m_mapLocalBuddyInfo.end())
		return iter->second->nTeamIndex;

	return 0;
}

void CUserMgr::ClearUserInfo()
{
	long nMemberCount = 0;
	CBuddyTeamInfo* pBuddyTeamInfo = NULL;
	CBuddyInfo*		pBuddyInfo	   = NULL;

	while(m_BuddyList.m_arrBuddyTeamInfo.size() > 0)
	{
		long nLast = m_BuddyList.m_arrBuddyTeamInfo.size()-1;
		nMemberCount = m_BuddyList.m_arrBuddyTeamInfo[nLast]->m_arrBuddyInfo.size();
		for(long j=0; j<nMemberCount; ++j)
		{
			pBuddyInfo = m_BuddyList.m_arrBuddyTeamInfo[nLast]->m_arrBuddyInfo[j];
			if(pBuddyInfo != NULL)
				delete pBuddyInfo;

		}
		m_BuddyList.m_arrBuddyTeamInfo[nLast]->m_arrBuddyInfo.clear();
		
		pBuddyTeamInfo = m_BuddyList.m_arrBuddyTeamInfo[nLast];
		if(pBuddyTeamInfo != NULL)
			delete pBuddyTeamInfo;
		
		//删除最后一个元素
		m_BuddyList.m_arrBuddyTeamInfo.pop_back();
	}

	//清除群组信息
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
	
	nCount += m_BuddyList.GetBuddyTotalCount();
	//算上自己
	nCount += 1;

	return nCount;
}