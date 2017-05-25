#include "stdafx.h"
#include "LoginAccountList.h"

CLoginAccountList::CLoginAccountList(void)
{
	m_nLastLoginUser = 0;
}

CLoginAccountList::~CLoginAccountList(void)
{
	Clear();
}

// 加载登录帐号列表文件
BOOL CLoginAccountList::LoadFile(LPCTSTR lpszFileName)
{
	if (NULL == lpszFileName || NULL ==*lpszFileName)
		return FALSE;

	FILE* fp = _tfopen(lpszFileName, _T("rb"));
	if (NULL == fp)
		return FALSE;

	int nCount = 0;
	_fread(&nCount, sizeof(nCount), 1, fp);

	_fread(&m_nLastLoginUser, sizeof(m_nLastLoginUser), 1, fp);
	
	for (int i = 0; i < nCount; i++)
	{
		LOGIN_ACCOUNT_INFO* lpAccount = new LOGIN_ACCOUNT_INFO;
		if (lpAccount != NULL)
		{
			_fread(lpAccount, sizeof(LOGIN_ACCOUNT_INFO), 1, fp);
			m_arrLoginAccount.push_back(lpAccount);
		}
	}
	
	fclose(fp);
	return TRUE;
}

// 保存登录帐号列表文件
BOOL CLoginAccountList::SaveFile(LPCTSTR lpszFileName)
{
	if (NULL == lpszFileName || NULL ==*lpszFileName)
		return FALSE;

	int nCount = (int)m_arrLoginAccount.size();
	if (nCount <= 0)
		return FALSE;

	FILE* fp = _tfopen(lpszFileName, _T("wb"));
	if (NULL == fp)
		return FALSE;

	_fwrite(&nCount, sizeof(nCount), 1, fp);
	_fwrite(&m_nLastLoginUser, sizeof(m_nLastLoginUser), 1, fp);

	int nCount2 = 0;
	for (int i = 0; i < nCount; i++)
	{
		LOGIN_ACCOUNT_INFO* lpAccount = m_arrLoginAccount[i];
		if (lpAccount != NULL)
		{
			_fwrite(lpAccount, sizeof(LOGIN_ACCOUNT_INFO), 1, fp);
			nCount2++;
		}
	}

	if (nCount2 != nCount)
	{
		fseek(fp, 0, SEEK_SET);
		_fwrite(&nCount2, sizeof(nCount2), 1, fp);
	}

	fclose(fp);
	return TRUE;
}

// 添加帐号
BOOL CLoginAccountList::Add(LPCTSTR lpszUser, LPCTSTR lpszPwd, 
							int nStatus, BOOL bRememberPwd, BOOL bAutoLogin)
{
	if (NULL == lpszUser)
		return FALSE;

	LOGIN_ACCOUNT_INFO* lpAccount = Find(lpszUser);
	if (lpAccount != NULL)
		return FALSE;

	lpAccount = new LOGIN_ACCOUNT_INFO;
	if (NULL == lpAccount)
		return FALSE;

	int nCount = sizeof(lpAccount->szUser)/sizeof(TCHAR);
	_tcsncpy(lpAccount->szUser, lpszUser, nCount);
	lpAccount->szUser[nCount-1] = _T('\0');

	if (bRememberPwd && lpszPwd != NULL)
	{
		nCount = sizeof(lpAccount->szPwd)/sizeof(TCHAR);
		_tcsncpy(lpAccount->szPwd, lpszPwd, nCount);
		lpAccount->szPwd[nCount-1] = _T('\0');
	}

	lpAccount->nStatus = nStatus;
	lpAccount->bRememberPwd = bRememberPwd;
	lpAccount->bAutoLogin = bAutoLogin;
	m_arrLoginAccount.push_back(lpAccount);
	return TRUE;
}

// 删除帐号
BOOL CLoginAccountList::Del(int nIndex)
{
	if (nIndex < 0 || nIndex >= (int)m_arrLoginAccount.size())
		return FALSE;

	LOGIN_ACCOUNT_INFO* lpAccount = m_arrLoginAccount[nIndex];
	if (lpAccount != NULL)
		delete lpAccount;
	m_arrLoginAccount.erase(m_arrLoginAccount.begin()+nIndex);
	return TRUE;
}

// 修改帐号
BOOL CLoginAccountList::Modify(int nIndex, LPCTSTR lpszUser, LPCTSTR lpszPwd, 
							   int nStatus, BOOL bRememberPwd, BOOL bAutoLogin)
{
	if (nIndex < 0 || nIndex >= (int)m_arrLoginAccount.size() || NULL == lpszUser)
		return FALSE;

	LOGIN_ACCOUNT_INFO* lpAccount = m_arrLoginAccount[nIndex];
	if (NULL == lpAccount)
		return FALSE;

	int nCount = sizeof(lpAccount->szUser)/sizeof(TCHAR);
	_tcsncpy(lpAccount->szUser, lpszUser, nCount);
	lpAccount->szUser[nCount-1] = _T('\0');

	if (bRememberPwd && lpszPwd != NULL)
	{
		nCount = sizeof(lpAccount->szPwd)/sizeof(TCHAR);
		_tcsncpy(lpAccount->szPwd, lpszPwd, nCount);
		lpAccount->szPwd[nCount-1] = _T('\0');
	}

	lpAccount->nStatus = nStatus;
	lpAccount->bRememberPwd = bRememberPwd;
	lpAccount->bAutoLogin = bAutoLogin;
	return TRUE;
}

// 清除所有帐号
void CLoginAccountList::Clear()
{
	for (int i = 0; i < (int)m_arrLoginAccount.size(); i++)
	{
		LOGIN_ACCOUNT_INFO* lpAccount = m_arrLoginAccount[i];
		if (lpAccount != NULL)
			delete lpAccount;
	}
	m_arrLoginAccount.clear();
}

// 获取帐号总数
int CLoginAccountList::GetCount()
{
	return (int)m_arrLoginAccount.size();
}

// 获取帐号信息
LOGIN_ACCOUNT_INFO* CLoginAccountList::GetAccountInfo(int nIndex)
{
	if (nIndex < 0 || nIndex >= (int)m_arrLoginAccount.size())
		return NULL;

	return m_arrLoginAccount[nIndex];
}

// 查找帐号
LOGIN_ACCOUNT_INFO* CLoginAccountList::Find(LPCTSTR lpszUser)
{
	if (NULL == lpszUser)
		return NULL;

	for (int i = 0; i < (int)m_arrLoginAccount.size(); i++)
	{
		LOGIN_ACCOUNT_INFO* lpAccount = m_arrLoginAccount[i];
		if (lpAccount != NULL && _tcscmp(lpAccount->szUser, lpszUser) == 0)
			return lpAccount;
	}
	return NULL;
}

int CLoginAccountList::GetLastLoginUser()
{
	return m_nLastLoginUser;
}

LOGIN_ACCOUNT_INFO* CLoginAccountList::GetLastLoginAccountInfo()
{
	return GetAccountInfo(m_nLastLoginUser);
}

BOOL CLoginAccountList::GetLastLoginAccountInfo(LOGIN_ACCOUNT_INFO* lpAccount)
{
	if (NULL == lpAccount)
		return FALSE;

	LOGIN_ACCOUNT_INFO* lpAccount2 = GetAccountInfo(m_nLastLoginUser);
	if (NULL == lpAccount2)
		return FALSE;

	memcpy(lpAccount, lpAccount2, sizeof(LOGIN_ACCOUNT_INFO));
	return TRUE;
}

void CLoginAccountList::SetLastLoginUser(LPCTSTR lpszUser)
{
	if (NULL == lpszUser)
		return;

	for (int i = 0; i < (int)m_arrLoginAccount.size(); i++)
	{
		LOGIN_ACCOUNT_INFO* lpAccount = m_arrLoginAccount[i];
		if (lpAccount != NULL && _tcscmp(lpAccount->szUser, lpszUser) == 0)
		{
			m_nLastLoginUser = i;
			return;
		}
	}
}

BOOL CLoginAccountList::IsAutoLogin()
{
	LOGIN_ACCOUNT_INFO* lpAccount = GetAccountInfo(m_nLastLoginUser);
	return (lpAccount != NULL) ? lpAccount->bAutoLogin : FALSE;
}

// 加密
void CLoginAccountList::Encrypt(char* lpBuf, int nLen)
{
	for (int i = 0; i < nLen; i++)
	{
		lpBuf[i] = lpBuf[i] ^ 0x88;
	}
}

// 解密
void CLoginAccountList::Decrypt(char* lpBuf, int nLen)
{
	for (int i = 0; i < nLen; i++)
	{
		lpBuf[i] = lpBuf[i] ^ 0x88;
	}
}

int CLoginAccountList::_fread(void* lpBuf, int nSize, int nCount, FILE* fp)
{
	int nRet = fread(lpBuf, nSize, nCount, fp);
	Decrypt((char*)lpBuf, nSize*nCount);
	return nRet;
}

int CLoginAccountList::_fwrite(const void* lpBuf, int nSize, int nCount, FILE* fp)
{
	int nLen = nSize*nCount;
	if (nLen > sizeof(LOGIN_ACCOUNT_INFO))
		return 0;
	char cBuf[sizeof(LOGIN_ACCOUNT_INFO)];
	memcpy(cBuf, lpBuf, nLen);
	Encrypt(cBuf, nLen);
	return fwrite(cBuf, nSize, nCount, fp);
}