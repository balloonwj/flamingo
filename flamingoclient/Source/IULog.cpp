#include "stdafx.h"
#include "IULog.h"
//#include "Path.h"
//#include "UserSessionData.h"
#include "EncodeUtil.h"
#include <tchar.h>

#ifndef LOG_OUTPUT
#define LOG_OUTPUT
#endif

#define MAX_LINE_LENGTH 256

bool CIULog::m_bToFile = false;
bool CIULog::m_bTruncateLongLog = false;
HANDLE CIULog::m_hLogFile = INVALID_HANDLE_VALUE;
LOG_LEVEL CIULog::m_nLogLevel = LOG_LEVEL_INFO;

bool CIULog::Init(bool bToFile, bool bTruncateLongLog, PCTSTR pszLogFileName)
{
#ifdef LOG_OUTPUT
	m_bToFile = bToFile;
    m_bTruncateLongLog = bTruncateLongLog;
   
    if (pszLogFileName == NULL || pszLogFileName[0] == NULL)
		return FALSE;

    TCHAR szHomePath[MAX_PATH] = {0};
    ::GetModuleFileName(NULL, szHomePath, MAX_PATH);
    for (int i = _tcslen(szHomePath); i >= 0; --i)
    {
        if (szHomePath[i] == _T('\\'))
        {
            szHomePath[i] = _T('\0');
            break;
        }
    }

    TCHAR szLogDirectory[MAX_PATH] = { 0 };
    _stprintf_s(szLogDirectory, _T("%s\\Logs\\"), szHomePath);
    
    DWORD dwAttr = ::GetFileAttributes(szLogDirectory);
    if (!((dwAttr != 0xFFFFFFFF) && (dwAttr & FILE_ATTRIBUTE_DIRECTORY)))
    {
        TCHAR cPath[MAX_PATH] = { 0 };
        TCHAR cTmpPath[MAX_PATH] = { 0 };
        TCHAR* lpPos = NULL;
        TCHAR cTmp = _T('\0');

        _tcsncpy_s(cPath, szLogDirectory, MAX_PATH);

        for (int i = 0; i < (int)_tcslen(cPath); i++)
        {
            if (_T('\\') == cPath[i])
                cPath[i] = _T('/');
        }

        lpPos = _tcschr(cPath, _T('/'));
        while (lpPos != NULL)
        {
            if (lpPos == cPath)
            {
                lpPos++;
            }
            else
            {
                cTmp = *lpPos;
                *lpPos = _T('\0');
                _tcsncpy_s(cTmpPath, cPath, MAX_PATH);
                ::CreateDirectory(cTmpPath, NULL);
                *lpPos = cTmp;
                lpPos++;
            }
            lpPos = _tcschr(lpPos, _T('/'));
        }
    }
		
    m_hLogFile = ::CreateFile(pszLogFileName, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (m_hLogFile == INVALID_HANDLE_VALUE)
        return false;

#endif // end LOG_OUTPUT

	return true;
}

void CIULog::Uninit()
{
#ifdef LOG_OUTPUT
	if(m_hLogFile != INVALID_HANDLE_VALUE)
	{
		::CloseHandle(m_hLogFile);
		m_hLogFile = INVALID_HANDLE_VALUE;
	}
#endif //end LOG_OUTPUT
}

void CIULog::SetLevel(LOG_LEVEL nLevel)
{
    m_nLogLevel = nLevel;
}

bool CIULog::Log(long nLevel, PCTSTR pszFmt, ...)
{
#ifdef LOG_OUTPUT
    if (nLevel < m_nLogLevel)
        return false;
    
    char szTime[64] = { 0 };
    GetTime(szTime,ARRAYSIZE(szTime));
    std::string strDebugInfo(szTime);
	
	std::string strLevel("[INFO]");
    if (nLevel == LOG_LEVEL_WARNING)
		strLevel = "[Warning]";
    else if (nLevel == LOG_LEVEL_ERROR)
		strLevel = "[Error]";

	strDebugInfo += strLevel;

    //当前线程信息
    char szThreadID[32] = { 0 };
    DWORD dwThreadID = ::GetCurrentThreadId();
    sprintf_s(szThreadID, ARRAYSIZE(szThreadID), "[ThreadID: %u]", dwThreadID);  
    strDebugInfo += szThreadID;

    //log正文
    std::wstring strLogMsg;
    va_list ap;
    va_start(ap, pszFmt);
    int nLogMsgLength = _vsctprintf(pszFmt, ap);
    //容量必须算上最后一个\0
    if ((int)strLogMsg.capacity() < nLogMsgLength + 1)
    {
        strLogMsg.resize(nLogMsgLength + 1);
    }
    _vstprintf_s((TCHAR*)strLogMsg.data(), strLogMsg.capacity(), pszFmt, ap);
    va_end(ap);

    //string内容正确但length不对，恢复一下其length
    std::wstring strMsgFormal;
    strMsgFormal.append(strLogMsg.c_str(), nLogMsgLength);

    //如果日志开启截断，长日志只取前MAX_LINE_LENGTH个字符
    if (m_bTruncateLongLog)
        strMsgFormal = strMsgFormal.substr(0, MAX_LINE_LENGTH);

    std::string strLogMsgAscii;
    strLogMsgAscii = EncodeUtil::UnicodeToAnsi(strMsgFormal);

    strDebugInfo += strLogMsgAscii;
	strDebugInfo += "\r\n";
	
	if(m_bToFile)
	{
		if(m_hLogFile == INVALID_HANDLE_VALUE)
			return false;
		
		::SetFilePointer(m_hLogFile, 0, NULL, FILE_END);
		DWORD dwBytesWritten = 0;       
        ::WriteFile(m_hLogFile, strDebugInfo.c_str(), strDebugInfo.length(), &dwBytesWritten, NULL);
        ::FlushFileBuffers(m_hLogFile);
        return true;
	}

    ::OutputDebugStringA(strDebugInfo.c_str());

#endif // end LOG_OUTPUT

	return true;
}

bool CIULog::Log(long nLevel, PCSTR pszFunctionSig, int nLineNo, PCTSTR pszFmt, ...)
{
#ifdef LOG_OUTPUT
    if (nLevel < m_nLogLevel)
        return false;

	//时间
    char szTime[64] = { 0 };
    GetTime(szTime, ARRAYSIZE(szTime));
    std::string strDebugInfo(szTime);
	
	//错误级别
	std::string strLevel("[INFO]");
    if (nLevel == LOG_LEVEL_WARNING)
		strLevel = "[Warning]";
    else if (nLevel == LOG_LEVEL_ERROR)
		strLevel = "[Error]";

	strDebugInfo += strLevel;
	
	//当前线程信息
	char szThreadID[32] = {0};
	DWORD dwThreadID = ::GetCurrentThreadId();
    sprintf_s(szThreadID, ARRAYSIZE(szThreadID), "[ThreadID: %u]", dwThreadID);
	strDebugInfo += szThreadID;

	//函数签名
    char szFuncSig[512] = { 0 };
    sprintf_s(szFuncSig, "[%s:%d]", pszFunctionSig, nLineNo);
    strDebugInfo += szFuncSig;

	//log正文
    std::wstring strLogMsg;
    va_list ap;
    va_start(ap, pszFmt);
    int nLogMsgLength = _vsctprintf(pszFmt, ap);
    //容量必须算上最后一个\0 
    if ((int)strLogMsg.capacity() < nLogMsgLength + 1)
    {
        strLogMsg.resize(nLogMsgLength + 1);
    }
    _vstprintf_s((TCHAR*)strLogMsg.data(), strLogMsg.capacity(), pszFmt, ap);
    va_end(ap);

    //string内容正确但length不对，恢复一下其length
    std::wstring strMsgFormal;
    strMsgFormal.append(strLogMsg.c_str(), nLogMsgLength);

    //如果日志开启截断，长日志只取前MAX_LINE_LENGTH个字符
    if (m_bTruncateLongLog)
        strMsgFormal = strMsgFormal.substr(0, MAX_LINE_LENGTH);

    std::string strLogMsgAscii;
    strLogMsgAscii = EncodeUtil::UnicodeToAnsi(strMsgFormal);

    strDebugInfo += strLogMsgAscii;
	strDebugInfo += "\r\n";
	
	if(m_bToFile)
	{
		if(m_hLogFile == INVALID_HANDLE_VALUE)
			return false;
		
		::SetFilePointer(m_hLogFile, 0, NULL, FILE_END);
		DWORD dwBytesWritten = 0;
        ::WriteFile(m_hLogFile, strDebugInfo.c_str(), strDebugInfo.length(), &dwBytesWritten, NULL);
        ::FlushFileBuffers(m_hLogFile);
        return true;
	}

    ::OutputDebugStringA(strDebugInfo.c_str());

#endif // end LOG_OUTPUT

	return true;
}

bool CIULog::Log(long nLevel, PCSTR pszFunctionSig, int nLineNo, PCSTR pszFmt, ...)
{
#ifdef LOG_OUTPUT
    if (nLevel < m_nLogLevel)
        return false;

	//时间
    char szTime[64] = { 0 };
    GetTime(szTime, ARRAYSIZE(szTime));
    std::string strDebugInfo(szTime);
	
	//错误级别
	std::string strLevel("[INFO]");
    if (nLevel == LOG_LEVEL_WARNING)
		strLevel = "[Warning]";
    else if (nLevel == LOG_LEVEL_ERROR)
		strLevel = "[Error]";

	strDebugInfo += strLevel;
	
	//当前线程信息
	char szThreadID[32] = {0};
	DWORD dwThreadID = ::GetCurrentThreadId();
	sprintf_s(szThreadID, ARRAYSIZE(szThreadID), "[ThreadID: %u]", dwThreadID);
    strDebugInfo += szThreadID;

    //函数签名
    char szFuncSig[512] = { 0 };
    sprintf_s(szFuncSig, "[%s:%d]", pszFunctionSig, nLineNo);
    strDebugInfo += szFuncSig;

    //日志正文
    std::string strLogMsg;
    va_list ap;
    va_start(ap, pszFmt);
    int nLogMsgLength = _vscprintf(pszFmt, ap);
    //容量必须算上最后一个\0
    if ((int)strLogMsg.capacity() < nLogMsgLength + 1)
    {
        strLogMsg.resize(nLogMsgLength + 1);
    }
    vsprintf_s((char*)strLogMsg.data(), strLogMsg.capacity(), pszFmt, ap);
    va_end(ap);

    //string内容正确但length不对，恢复一下其length
    std::string strMsgFormal;
    strMsgFormal.append(strLogMsg.c_str(), nLogMsgLength);

    //如果日志开启截断，长日志只取前MAX_LINE_LENGTH个字符
    if (m_bTruncateLongLog)
        strMsgFormal = strMsgFormal.substr(0, MAX_LINE_LENGTH);

    strDebugInfo += strMsgFormal;
    strDebugInfo += "\r\n";
	
	if(m_bToFile)
	{
		if(m_hLogFile == INVALID_HANDLE_VALUE)
			return false;
		
		::SetFilePointer(m_hLogFile, 0, NULL, FILE_END);
		DWORD dwBytesWritten = 0;
        ::WriteFile(m_hLogFile, strDebugInfo.c_str(), strDebugInfo.length(), &dwBytesWritten, NULL);
        ::FlushFileBuffers(m_hLogFile);
        return true;
	}

    ::OutputDebugStringA(strDebugInfo.c_str());

#endif // end LOG_OUTPUT

	return true;
}

void CIULog::GetTime(char* pszTime, int nTimeStrLength)
{
	SYSTEMTIME st = {0};
	::GetLocalTime(&st);
    sprintf_s(pszTime, nTimeStrLength, "[%04d-%02d-%02d %02d:%02d:%02d:%04d]", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
}
