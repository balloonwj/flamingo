#include "stdafx.h"
#include "PerformanceCounter.h"
#include "EncodeUtil.h"
#include <tchar.h>

#ifndef PERFORMANCE_OUTPUT
#define PERFORMANCE_OUTPUT
#endif

bool CPerformanceCounter::m_bToFile = false;
HANDLE CPerformanceCounter::m_hPerformanceFile = INVALID_HANDLE_VALUE;
LARGE_INTEGER CPerformanceCounter::m_liFreq;
LARGE_INTEGER CPerformanceCounter::m_liBegin;
LARGE_INTEGER CPerformanceCounter::m_liEnd;

bool CPerformanceCounter::Init(bool bToFile, PCTSTR pszLogFileName)
{
#ifdef PERFORMANCE_OUTPUT
    m_bToFile = bToFile;

    if (0 >= _tcslen(pszLogFileName))
        return FALSE;

    TCHAR szHomePath[MAX_PATH] = { 0 };
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
    _stprintf_s(szLogDirectory, _T("%s\\logs\\"), szHomePath);

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

    m_hPerformanceFile = ::CreateFile(pszLogFileName, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (m_hPerformanceFile == INVALID_HANDLE_VALUE)
    {
        if (GetLastError() == ERROR_ALREADY_EXISTS)
        {
            return true;
        }
        return false;
    }

    ::QueryPerformanceFrequency(&m_liFreq);

#endif // end PERFORMANCE_OUTPUT

    return true;
}

void CPerformanceCounter::Uninit()
{
#ifdef PERFORMANCE_OUTPUT
    if (m_hPerformanceFile != INVALID_HANDLE_VALUE)
    {
        ::CloseHandle(m_hPerformanceFile);
        m_hPerformanceFile = INVALID_HANDLE_VALUE;
    }
#endif //end PERFORMANCE_OUTPUT
}

bool CPerformanceCounter::Begin(PCSTR pszFunctionSig, int nLineNo)
{
#ifdef PERFORMANCE_OUTPUT  
    //时间
    char szTime[64] = { 0 };
    SYSTEMTIME st = { 0 };
    ::GetLocalTime(&st);
    sprintf_s(szTime, ARRAYSIZE(szTime), "[%04d-%02d-%02d %02d:%02d:%02d:%04d]", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    std::string strDebugInfo(szTime);

    //当前线程信息
    char szThreadID[32] = { 0 };
    DWORD dwThreadID = ::GetCurrentThreadId();
    sprintf_s(szThreadID, ARRAYSIZE(szThreadID), "[ThreadID: %u]", dwThreadID);
    strDebugInfo += szThreadID;

    //函数签名
    char szFuncSig[512] = { 0 };
    sprintf_s(szFuncSig, "[%s:%d]", pszFunctionSig, nLineNo);
    strDebugInfo += szFuncSig;

    //正文
    strDebugInfo += "Performance counter begin:\r\n";

    if (m_bToFile)
    {
        if (m_hPerformanceFile == INVALID_HANDLE_VALUE)
            return false;

        ::SetFilePointer(m_hPerformanceFile, 0, NULL, FILE_END);
        DWORD dwBytesWritten = 0;
        ::WriteFile(m_hPerformanceFile, strDebugInfo.c_str(), strDebugInfo.length(), &dwBytesWritten, NULL);
        ::FlushFileBuffers(m_hPerformanceFile);      
    }
    else
        ::OutputDebugStringA(strDebugInfo.c_str());

    ::QueryPerformanceCounter(&m_liBegin);

#endif // end PERFORMANCE_OUTPUT

    return true;
}

bool CPerformanceCounter::End(PCSTR pszFunctionSig, int nLineNo)
{
#ifdef PERFORMANCE_OUTPUT

    ::QueryPerformanceCounter(&m_liEnd);

    //时间
    char szTime[64] = { 0 };
    SYSTEMTIME st = { 0 };
    ::GetLocalTime(&st);
    sprintf_s(szTime, ARRAYSIZE(szTime), "[%04d-%02d-%02d %02d:%02d:%02d:%04d]", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    std::string strDebugInfo(szTime);

    //当前线程信息
    char szThreadID[32] = { 0 };
    DWORD dwThreadID = ::GetCurrentThreadId();
    sprintf_s(szThreadID, ARRAYSIZE(szThreadID), "[ThreadID: %u]", dwThreadID);
    strDebugInfo += szThreadID;

    //函数签名
    char szFuncSig[512] = { 0 };
    sprintf_s(szFuncSig, "[%s:%d]", pszFunctionSig, nLineNo);
    strDebugInfo += szFuncSig;

    //正文
    double dbInterval = (double)(LONGLONG)((LONGLONG)m_liEnd.QuadPart - (LONGLONG)m_liBegin.QuadPart) / (double)m_liFreq.QuadPart;
    char szInterval[64] = { 0 };
    //精确到纳秒
    sprintf_s(szInterval, ARRAYSIZE(szInterval), "Performance counter end: %2.12fms", (dbInterval * 1000));

    strDebugInfo += szInterval;

    strDebugInfo += "\r\n";

    if (m_bToFile)
    {
        if (m_hPerformanceFile == INVALID_HANDLE_VALUE)
            return false;

        ::SetFilePointer(m_hPerformanceFile, 0, NULL, FILE_END);
        DWORD dwBytesWritten = 0;
        ::WriteFile(m_hPerformanceFile, strDebugInfo.c_str(), strDebugInfo.length(), &dwBytesWritten, NULL);
        ::FlushFileBuffers(m_hPerformanceFile);
        return true;
    }

    ::OutputDebugStringA(strDebugInfo.c_str());

#endif // end PERFORMANCE_OUTPUT

    return true;
}