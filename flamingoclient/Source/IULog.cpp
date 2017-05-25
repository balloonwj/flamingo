#include "stdafx.h"
#include "IULog.h"
#include "Path.h"
#include "UserSessionData.h"
#include "EncodingUtil.h"

#ifndef LOG_OUTPUT
#define LOG_OUTPUT
#endif

BOOL CIULog::m_bToFile = FALSE;
CString CIULog::m_strLogFileName = _T("");
HANDLE CIULog::m_hLogFile = INVALID_HANDLE_VALUE;

BOOL CIULog::Init(BOOL bToFile, PCTSTR pszLogFileName)
{
#ifdef LOG_OUTPUT
	m_bToFile = bToFile;
	m_strLogFileName = pszLogFileName;

	if(m_hLogFile != INVALID_HANDLE_VALUE)
	{
		::CloseHandle(m_hLogFile);
		m_hLogFile = INVALID_HANDLE_VALUE;
	}

	if(m_strLogFileName.IsEmpty())
		return FALSE;
		
	CString strLogDirectory;
	strLogDirectory.Format(_T("%sLog\\"), g_szHomePath);
	if(!Hootina::CPath::IsDirectoryExist(strLogDirectory))
		Hootina::CPath::CreateDirectory(strLogDirectory);
		
	m_hLogFile = ::CreateFile(m_strLogFileName, GENERIC_READ|GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
	if(m_hLogFile == INVALID_HANDLE_VALUE)
		return FALSE;

#endif // end LOG_OUTPUT

	return TRUE;
}

void CIULog::Unit()
{
#ifdef LOG_OUTPUT
	if(m_hLogFile != INVALID_HANDLE_VALUE)
	{
		::CloseHandle(m_hLogFile);
		m_hLogFile = INVALID_HANDLE_VALUE;
	}
#endif //end LOG_OUTPUT
}

BOOL CIULog::Log(long nLevel, PCTSTR pszFmt, ...)
{
#ifdef LOG_OUTPUT
	CString strDebugInfo(GetCurrentTime());
	
	CString strLevel(_T("[Normal] "));
	if(nLevel == LOG_WARNING)
		strLevel = _T("[Warning] ");
	else if(nLevel == LOG_ERROR)
		strLevel = _T("[Error] ");

	strDebugInfo += strLevel;
	
	CString str;
	va_list argList;
	va_start(argList, pszFmt);
	str.FormatV(pszFmt, argList);
	va_end(argList);

	strDebugInfo += str;
	strDebugInfo += _T("\r\n");
	
	if(m_bToFile)
	{
		if(m_hLogFile == INVALID_HANDLE_VALUE)
			return FALSE;
		
		::SetFilePointer(m_hLogFile, 0, NULL, FILE_END);
		DWORD dwBytesWritten = 0;
		CStringA strAsciiDebugIfo;
		UnicodeToAnsi(strDebugInfo, strAsciiDebugIfo.GetBuffer(strDebugInfo.GetLength()*2), strDebugInfo.GetLength()*2);
		strAsciiDebugIfo.ReleaseBuffer();
		::WriteFile(m_hLogFile, strAsciiDebugIfo.GetString(), strAsciiDebugIfo.GetLength(), &dwBytesWritten, NULL);
		return TRUE;
	}

	::OutputDebugString(strDebugInfo);

#endif // end LOG_OUTPUT

	return TRUE;
}

BOOL CIULog::Log(long nLevel, PCSTR pszFunctionSig, PCTSTR pszFmt, ...)
{
#ifdef LOG_OUTPUT
	//时间
	CString strDebugInfo(GetCurrentTime());
	
	//错误级别
	CString strLevel(_T("[Normal] "));
	if(nLevel == LOG_WARNING)
		strLevel = _T("[Warning] ");
	else if(nLevel == LOG_ERROR)
		strLevel = _T("[Error] ");

	strDebugInfo += strLevel;
	
	//当前线程信息
	TCHAR szThreadID[32] = {0};
	DWORD dwThreadID = ::GetCurrentThreadId();
	_stprintf_s(szThreadID, ARRAYSIZE(szThreadID), _T("[ThreadID:%u]"), dwThreadID);
	strDebugInfo += szThreadID;

	//函数签名
	TCHAR szFuncSig[512] = {0};
	AnsiToUnicode(pszFunctionSig, szFuncSig, ARRAYSIZE(szFuncSig));
	strDebugInfo += _T("[");
	strDebugInfo += szFuncSig;
	strDebugInfo += _T("]");
	
	//log正文
	CString str;
	va_list argList;
	va_start(argList, pszFmt);
	str.FormatV(pszFmt, argList);
	va_end(argList);

	strDebugInfo += str;
	strDebugInfo += _T("\r\n");
	
	if(m_bToFile)
	{
		if(m_hLogFile == INVALID_HANDLE_VALUE)
			return FALSE;
		
		::SetFilePointer(m_hLogFile, 0, NULL, FILE_END);
		DWORD dwBytesWritten = 0;
		CStringA strAsciiDebugIfo;
		UnicodeToAnsi(strDebugInfo, strAsciiDebugIfo.GetBuffer(strDebugInfo.GetLength()*2), strDebugInfo.GetLength()*2);
		strAsciiDebugIfo.ReleaseBuffer();
		::WriteFile(m_hLogFile, strAsciiDebugIfo.GetString(), strAsciiDebugIfo.GetLength(), &dwBytesWritten, NULL);
		return TRUE;
	}

	::OutputDebugString(strDebugInfo);

#endif // end LOG_OUTPUT

	return TRUE;
}

BOOL CIULog::Log(long nLevel, PCSTR pszFunctionSig, PCSTR pszFmt, ...)
{
#ifdef LOG_OUTPUT
	//时间
	CStringA strDebugInfo(GetCurrentTime());
	
	//错误级别
	CStringA strLevel("[Normal] ");
	if(nLevel == LOG_WARNING)
		strLevel = "[Warning] ";
	else if(nLevel == LOG_ERROR)
		strLevel = "[Error] ";

	strDebugInfo += strLevel;
	
	//当前线程信息
	char szThreadID[32] = {0};
	DWORD dwThreadID = ::GetCurrentThreadId();
	sprintf_s(szThreadID, ARRAYSIZE(szThreadID), "[ThreadID:%u]", dwThreadID);
	strDebugInfo += szThreadID;

	//函数签名
	strDebugInfo += "[";
	strDebugInfo += pszFunctionSig;
	strDebugInfo += "]";
	
	//log正文
	CStringA str;
	va_list argList;
	va_start(argList, pszFmt);
	str.FormatV(pszFmt, argList);
	va_end(argList);

	strDebugInfo += str;
	strDebugInfo += "\r\n";
	
	if(m_bToFile)
	{
		if(m_hLogFile == INVALID_HANDLE_VALUE)
			return FALSE;
		
		::SetFilePointer(m_hLogFile, 0, NULL, FILE_END);
		DWORD dwBytesWritten = 0;
		::WriteFile(m_hLogFile, strDebugInfo.GetString(), strDebugInfo.GetLength(), &dwBytesWritten, NULL);
		return TRUE;
	}

	::OutputDebugStringA(strDebugInfo);

#endif // end LOG_OUTPUT

	return TRUE;
}

CString CIULog::GetCurrentTime()
{
	SYSTEMTIME st = {0};
	::GetLocalTime(&st);

	CString strTime;
	strTime.Format(_T("[%04d-%02d-%02d %02d:%02d:%02d:%04d]"), st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
	
	return strTime;
}