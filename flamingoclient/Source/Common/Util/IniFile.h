#pragma once

class CIniFile
{
public:
	CIniFile();
	~CIniFile();

public:
	UINT ReadInt(LPCTSTR lpAppName, LPCTSTR lpKeyName, long nDefault, LPCTSTR lpFileName);
	BOOL ReadString(LPCTSTR lpAppName, LPCTSTR lpKeyName,LPCTSTR lpDefault, LPTSTR lpReturnedString, DWORD nSize, LPCTSTR lpFileName);
	BOOL WriteInt(LPCTSTR lpAppName, LPCTSTR lpKeyName, long nValue, LPCTSTR lpFileName);
	BOOL WriteString(LPCTSTR lpAppName, LPCTSTR lpKeyName, LPCTSTR lpString, LPCTSTR lpFileName);
};