#include "StdAfx.h"
#include "File.h"
#include <tchar.h>

CFileX::CFileX() : m_hFile(INVALID_HANDLE_VALUE)
{
    
}

CFileX::CFileX(PCTSTR pszFileName)
{
    Open(pszFileName);
}

CFileX::~CFileX()
{
    Close();
}

bool CFileX::Open(PCTSTR pszFileName, bool bCreateIfNotExsit/* = true*/)
{
    //TODO: 将来根据需要可以继续扩展更多标志
    DWORD dwCreateFlag = OPEN_EXISTING;
    if (bCreateIfNotExsit)
        dwCreateFlag = CREATE_ALWAYS;
    m_hFile = ::CreateFile(pszFileName, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, NULL, dwCreateFlag, FILE_ATTRIBUTE_NORMAL, NULL);
    if (m_hFile == INVALID_HANDLE_VALUE)
        return false;

    return true;
}

bool CFileX::Read(char* pszBuffer, int nBufferLength, int* nReadLength/* = NULL*/)
{
    if (m_hFile == INVALID_HANDLE_VALUE)
        return false;

    int nFileSize = GetFileSize();
    if (nFileSize == 0)
        return false;

    if (nBufferLength > nFileSize)
        nBufferLength = nFileSize;

    DWORD dwBytesEachToRead = 4096;
    if ((int)dwBytesEachToRead > nFileSize)
        dwBytesEachToRead = nFileSize;

    DWORD dwTotalRead = 0;
    DWORD dwBytesEachRead;
    while (true)
    {
        if (!::ReadFile(m_hFile, pszBuffer, dwBytesEachToRead, &dwBytesEachRead, NULL))
            return false;

        if (dwBytesEachToRead != dwBytesEachRead)
            return false;

        dwTotalRead += dwBytesEachToRead;
        if ((int)dwTotalRead >= nBufferLength)
            break;

        pszBuffer += dwBytesEachToRead;

        if (nBufferLength - dwTotalRead < dwBytesEachToRead)
            dwBytesEachToRead = nBufferLength - dwTotalRead;
    }
    
    return true;
}

bool CFileX::Write(const char* pszBuffer, int nBufferLength)
{
    if (m_hFile == INVALID_HANDLE_VALUE)
        return false;

    DWORD dwBytesWritten = 0;
    DWORD dwBytesWrittenEach;
    DWORD dwBytesToWrite = 4096;
    if (nBufferLength < (int)dwBytesToWrite)
        dwBytesToWrite = (int)nBufferLength;
    while (true)
    {
        //出错
        if (!::WriteFile(m_hFile, pszBuffer, dwBytesToWrite, &dwBytesWrittenEach, NULL))
            return false;
        //出错
        if (dwBytesToWrite != dwBytesWrittenEach)
            return false;

        dwBytesWritten += dwBytesWrittenEach;
        //成功写完
        if (dwBytesWritten == nBufferLength)
            break;

        pszBuffer += dwBytesWrittenEach;

        //剩下的不够4096个字节，写入剩余的字节数目
        if (nBufferLength - dwBytesWritten < dwBytesToWrite)
            dwBytesToWrite = nBufferLength - dwBytesWritten;
    }

    return true;
}

bool CFileX::IsValid()
{
    return m_hFile != INVALID_HANDLE_VALUE ? true : false;
}

int CFileX::GetFileSize()
{
    //TODO: 后期为了支持大文件，应该把GetFileSize的第二个参数用起来
    if (!IsValid())
        return 0;
    int dwFileSize = ::GetFileSize(m_hFile, NULL);
    return (int)dwFileSize;
}

void CFileX::Close()
{
    if (!IsValid())
        return;

    ::CloseHandle(m_hFile);
}

bool CFileX::DeleteAllFileByExtension(PCTSTR pszDir, PCTSTR pszExtension)
{
    WIN32_FIND_DATA win32FindData = { 0 };
    TCHAR szFileName[MAX_PATH] = { 0 };
    _stprintf_s(szFileName, MAX_PATH, _T("%s%s"), pszDir, pszExtension);
    HANDLE hFindFile = ::FindFirstFile(szFileName, &win32FindData);
    if (hFindFile == INVALID_HANDLE_VALUE)
        return false;

    

    do
    {
        if (_tcsicmp(win32FindData.cFileName, _T(".")) != 0 ||
            _tcsicmp(win32FindData.cFileName, _T("..")) != 0)
        {
            memset(szFileName, 0, sizeof(szFileName));
            _stprintf_s(szFileName, MAX_PATH, _T("%s%s"), pszDir, win32FindData.cFileName);
            ::DeleteFile(szFileName);
        }

        if (!::FindNextFile(hFindFile, &win32FindData))
            break;

    } while (true);

    ::FindClose(hFindFile);

    return true;
}

// 检测指定目录是否存在
bool CFileX::IsDirectoryExist(LPCTSTR lpszPath)
{
    if (NULL == lpszPath || NULL == *lpszPath)
        return FALSE;

    DWORD dwAttr = ::GetFileAttributes(lpszPath);
    return (((dwAttr != 0xFFFFFFFF) && (dwAttr & FILE_ATTRIBUTE_DIRECTORY)) ? true : false);
}