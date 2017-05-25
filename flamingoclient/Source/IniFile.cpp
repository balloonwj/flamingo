#include "stdafx.h"
#include "IniFile.h"

CIniFile::CIniFile()
{

}

CIniFile::~CIniFile()
{

}

UINT CIniFile::ReadInt(LPCTSTR lpAppName, LPCTSTR lpKeyName, long nDefault, LPCTSTR lpFileName)
{
	return ::GetPrivateProfileInt(lpAppName, lpKeyName, nDefault, lpFileName);
}

BOOL CIniFile::ReadString(LPCTSTR lpAppName, LPCTSTR lpKeyName,LPCTSTR lpDefault, LPTSTR lpReturnedString, DWORD nSize, LPCTSTR lpFileName)
{
	::GetPrivateProfileString(lpAppName, lpKeyName, lpDefault, lpReturnedString, nSize,lpFileName);
	return TRUE;
}

BOOL CIniFile::WriteInt(LPCTSTR lpAppName, LPCTSTR lpKeyName, long nValue, LPCTSTR lpFileName)
{
	TCHAR szValue[32] = {0};
	_ltow(nValue, szValue, 10);
	return ::WritePrivateProfileString(lpAppName, lpKeyName, szValue, lpFileName);
}

BOOL CIniFile::WriteString(LPCTSTR lpAppName, LPCTSTR lpKeyName, LPCTSTR lpString, LPCTSTR lpFileName)
{
	return ::WritePrivateProfileString(lpAppName, lpKeyName, lpString, lpFileName);
}
