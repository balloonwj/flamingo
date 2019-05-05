#include "stdafx.h"
#include "File2.h"
#include "MD5Sum.h"
#include "MiniBuffer.h"
#include "Path.h"
#include "EncodingUtil.h"
#include "net/IUProtocolData.h"
#include "UserSessionData.h"
#include <stdint.h>

//打开一个文件并获得该文件的句柄
HANDLE GetFileHandle(PCTSTR pszFileName)
{
	HANDLE hFile = ::CreateFile(pszFileName, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

	return hFile;
}
//获取文件大小
DWORD IUGetFileSize(PCTSTR pszFileName)
{
	HANDLE hFile = GetFileHandle(pszFileName);
	if(hFile == INVALID_HANDLE_VALUE)
		return 0;
	
	CAutoFileHandle autoFile(hFile);
	DWORD dwFileSize = ::GetFileSize(hFile, NULL);
	if(dwFileSize == INVALID_FILE_SIZE)
		return 0;
	
	return dwFileSize;
}

DWORD IUGetFileSize(HANDLE hFile)
{
	DWORD dwFileSize = ::GetFileSize(hFile, NULL);
	if(dwFileSize == INVALID_FILE_SIZE)
		return 0;

	return dwFileSize;
}

UINT64 IUGetFileSize2(PCTSTR pszFileName)
{
	WIN32_FIND_DATA fd = {0};
	HANDLE hFindFile = ::FindFirstFile(pszFileName, &fd);
	if(hFindFile == INVALID_HANDLE_VALUE)
		return 0;

	::FindClose(hFindFile);
	return (((UINT64)fd.nFileSizeHigh) << 32) + (UINT64)fd.nFileSizeLow;
}

//获取文件md5值
long GetFileMd5ValueA(PCTSTR pszFileName, char* pszMd5, long nMd5Length, int64_t& nFileSize, HWND hwndReflection/*=NULL*/, HANDLE hCancelEvent/*=NULL*/)
{
    nFileSize = 0;

    HANDLE hFile = GetFileHandle(pszFileName);
	if(hFile == INVALID_HANDLE_VALUE)	
		return GET_FILE_MD5_FAILED;

	CAutoFileHandle autoFile(hFile);
    DWORD dwFileSizeHigh;
    DWORD dwFileSizeLow = ::GetFileSize(hFile, &dwFileSizeHigh);
    //获取文件失败或者0字节的文件不能上传
    if (dwFileSizeLow == INVALID_FILE_SIZE || (dwFileSizeLow == 0 && dwFileSizeHigh == 0))
		return GET_FILE_MD5_FAILED;

	BOOL bError = FALSE;

    nFileSize = (((int64_t)(((int64_t)dwFileSizeHigh) << 32)) + (int64_t)dwFileSizeLow);
	//每次最多读取500k大小
	int64_t nSizeEachRead = 500*1024;
    if (nFileSize <= nSizeEachRead)
        nSizeEachRead = nFileSize;

	int64_t nFileOffset = 0;
	BOOL bRet = 0;
	//读取的字节数
    DWORD dwFileRead = 0;
	MD5_CTX ctx;
    MD5Init(&ctx);
	FileProgress* pFileProgress = NULL;

	while(TRUE)
	{
		if(hCancelEvent!=NULL && ::WaitForSingleObject(hCancelEvent, 0)==WAIT_OBJECT_0)
		{
			return GET_FILE_MD5_USERCANCEL;
		}
		
		//如果剩下的文件大小已经不足一个nSizeEachRead
        if (nFileOffset + nSizeEachRead > nFileSize)
			nSizeEachRead = nFileSize-nFileOffset;
		
		CMiniBuffer miniBuffer(nSizeEachRead);

        bRet = ::ReadFile(hFile, miniBuffer.GetBuffer(), (DWORD)nSizeEachRead, &dwFileRead, NULL);
		if(!bRet || nSizeEachRead!=dwFileRead)
		{
			bError = TRUE;
			break;
		}

		MD5Update(&ctx, (unsigned char*)miniBuffer.GetBuffer(), (unsigned int)nSizeEachRead);

		nFileOffset += nSizeEachRead;

		if(nFileOffset >= nFileSize)
			break;

		
		pFileProgress = new FileProgress();
		memset(pFileProgress, 0, sizeof(FileProgress));
		pFileProgress->nPercent = -1;
		pFileProgress->nVerificationPercent = (long)((__int64)nFileOffset*100/nFileSize);
		_tcscpy_s(pFileProgress->szDestPath, ARRAYSIZE(pFileProgress->szDestPath), pszFileName);
		::PostMessage(hwndReflection, FMG_MSG_SEND_FILE_PROGRESS, 0, (LPARAM)pFileProgress);

		::Sleep(1);
		
	}// end while-loop
	
	
	if(!bError)
	{
		unsigned char szTempMd5[16] = {0};
		MD5Final(szTempMd5 , &ctx );
		CStringA strTemp;
		CStringA strMd5;
		for (int i = 0; i < 16; i++)
		{
			strTemp.Format("%02x", szTempMd5[i]);
			strMd5 += strTemp;
		}

		strcpy_s((char*)pszMd5, nMd5Length, strMd5);

		pFileProgress = new FileProgress();
		memset(pFileProgress, 0, sizeof(FileProgress));
		pFileProgress->nPercent = -1;
		pFileProgress->nVerificationPercent = 100;
		_tcscpy_s(pFileProgress->szDestPath, ARRAYSIZE(pFileProgress->szDestPath), pszFileName);
		::PostMessage(hwndReflection, FMG_MSG_SEND_FILE_PROGRESS, 0, (LPARAM)pFileProgress);
	}

	return !bError ? GET_FILE_MD5_SUCESS:GET_FILE_MD5_FAILED;
}

BOOL GetFileMd5ValueW(PCTSTR pszFileName, TCHAR* pszMd5, long nMd5Length)
{
	char szMd5[64] = {0};
    int64_t nFileSize;
    if (!GetFileMd5ValueA(pszFileName, szMd5, ARRAYSIZE(szMd5), nFileSize))
		return FALSE;

	return EncodeUtil::AnsiToUnicode(szMd5, pszMd5, nMd5Length);
}

//获取文件的上传名称：大小|md5值
BOOL GetFileUploadName(PCTSTR pszFileName, char* pszUploadName, long nUploadNameLength)
{
	char szMd5[40] = {0};
    int64_t nFileSize;
    if (!GetFileMd5ValueA(pszFileName, szMd5, ARRAYSIZE(szMd5), nFileSize))
		return FALSE;

	sprintf_s(pszUploadName, nUploadNameLength, "%s.%s", szMd5, (char*)Hootina::CPath::GetExtension(pszFileName).c_str());
	return TRUE;
}
//获取文件指定部分
BOOL GetFileData(HANDLE hFile, DWORD dwOffset, DWORD dwLength, char* buffer)
{
	::SetFilePointer(hFile, dwOffset, NULL, FILE_BEGIN);
	DWORD dwFileRead;
	if(!::ReadFile(hFile, buffer, dwLength, &dwFileRead, NULL) || dwFileRead!=dwLength)
		return FALSE;

	return TRUE;
}

BOOL GetFileData(PCTSTR pszFileName, DWORD dwOffset, DWORD dwLength, char* buffer)
{
	HANDLE hFile = GetFileHandle(pszFileName);
	if(hFile == INVALID_HANDLE_VALUE)
		return FALSE;
	CAutoFileHandle autoFile(hFile);
	
	return GetFileData(hFile, dwOffset, dwLength, buffer);
}


CAutoFileHandle::CAutoFileHandle(HANDLE hFile)
{
	m_hFile = hFile;
}

CAutoFileHandle::~CAutoFileHandle()
{
	if(m_hFile != INVALID_HANDLE_VALUE)
		::CloseHandle(m_hFile);
}

CAutoFileHandle::operator HANDLE()
{
	return m_hFile;
}

void CAutoFileHandle::Release()
{
	if(m_hFile != INVALID_HANDLE_VALUE)
	{
		::CloseHandle(m_hFile);
		m_hFile = INVALID_HANDLE_VALUE;
	}
}



CFile::CFile()
{
	m_hFile = INVALID_HANDLE_VALUE;
}

CFile::~CFile()
{
	if(m_hFile != INVALID_HANDLE_VALUE)
	{
		::CloseHandle(m_hFile);
		m_Buffer.Release();
	}
}

BOOL CFile::Open(LPCTSTR lpszFileName, BOOL bCreateAlways/* = TRUE*/)
{
	DWORD dwCreationFlag = (bCreateAlways ? CREATE_ALWAYS : OPEN_EXISTING);
	m_hFile = ::CreateFile(lpszFileName, GENERIC_READ|GENERIC_WRITE, FILE_SHARE_READ, NULL, dwCreationFlag, FILE_ATTRIBUTE_NORMAL, NULL);
	
	return (m_hFile!=INVALID_HANDLE_VALUE);
}

long CFile::GetSize()
{
	if(m_hFile == INVALID_HANDLE_VALUE)
		return 0;
	else 
		return ::GetFileSize(m_hFile, NULL);
}

const char* CFile::Read()
{
	if(m_hFile == INVALID_HANDLE_VALUE)
		return NULL;

	m_Buffer.Release();

	long nSize = GetSize();
	if(nSize <= 0)
		return NULL;

	char* pBuffer = new char[nSize+1];
	memset(pBuffer, 0, nSize+1);
	DWORD dwBytesRead = 0;
	DWORD dwTotalBytes = 0;
	do{
		if(!::ReadFile(m_hFile, pBuffer+dwTotalBytes, nSize-dwTotalBytes, &dwBytesRead, NULL))
			return NULL;
		
		dwTotalBytes += dwBytesRead;
		if(dwTotalBytes >= (DWORD)nSize)
			break;

		::SetFilePointer(m_hFile, dwTotalBytes, NULL, FILE_BEGIN);

	}while(TRUE);

	m_Buffer.Add(pBuffer, nSize);

	return (const char*)m_Buffer.GetData();
}

BOOL CFile::Write(const char* pBuffer, long nSize)
{
	if(m_hFile == INVALID_HANDLE_VALUE)
		return FALSE;

	DWORD dwBytesWritten = 0;
	DWORD dwBytesTotal = 0;
	DWORD dwSizeToWriteEachTime = 4*1024;
	//写文件的次数
	if((DWORD)nSize <= dwSizeToWriteEachTime)
		dwSizeToWriteEachTime = nSize;
	
	do 
	{
		//为了防止文件较大一次性写失败，这里每次只写4K
		if(!::WriteFile(m_hFile, pBuffer+dwBytesTotal, dwSizeToWriteEachTime, &dwBytesWritten, NULL) || dwBytesWritten!=dwSizeToWriteEachTime)
			return FALSE;

		dwBytesTotal += dwBytesWritten;
		if(dwBytesTotal >= (DWORD)nSize)
			break;
		else if(nSize-dwBytesTotal <  dwSizeToWriteEachTime)		//剩下的字节数已经小于4K
			dwSizeToWriteEachTime = nSize-dwBytesTotal;

		::SetFilePointer(m_hFile, dwBytesTotal, NULL, FILE_BEGIN);
	} while (TRUE);

	return TRUE;
}

void CFile::Close()
{
	if(m_hFile != INVALID_HANDLE_VALUE)
	{
		::CloseHandle(m_hFile);
		m_Buffer.Release();
		m_hFile = INVALID_HANDLE_VALUE;
	}
}



//////////////////////////////////////////////////////////
//                 CFile2 Implementions
//////////////////////////////////////////////////////////

const HANDLE CFile2::hFileNull = INVALID_HANDLE_VALUE;

CFile2::CFile2()
{
	m_hFile = INVALID_HANDLE_VALUE;
	m_bCloseOnDelete = FALSE;
}

CFile2::CFile2(HANDLE hFile)
{
	m_hFile = hFile;
	m_bCloseOnDelete = FALSE;
}

CFile2::CFile2(LPCTSTR lpszFileName, UINT nOpenFlags)
{
	m_hFile = INVALID_HANDLE_VALUE;

	Open(lpszFileName, nOpenFlags);
}

CFile2::~CFile2()
{
	if (m_hFile != INVALID_HANDLE_VALUE && m_bCloseOnDelete)
		Close();
}

BOOL CFile2::Open(LPCTSTR lpszFileName, UINT nOpenFlags)
{
	// CFile2 objects are always binary and CreateFile does not need flag
	nOpenFlags &= ~(UINT)typeBinary;

	m_bCloseOnDelete = FALSE;

	m_hFile = INVALID_HANDLE_VALUE;
	m_strFileName.Empty();

	m_strFileName = lpszFileName;

	// map read/write mode
	//modeRead|modeWrite|modeReadWrite) == 3
	DWORD dwAccess = 0;
	switch (nOpenFlags & 3)
	{
	case modeRead:
		dwAccess = GENERIC_READ;
		break;
	case modeWrite:
		dwAccess = GENERIC_WRITE;
		break;
	case modeReadWrite:
		dwAccess = GENERIC_READ | GENERIC_WRITE;
		break;
	default:
		return FALSE;
	}

	// map share mode
	DWORD dwShareMode = 0;
	switch (nOpenFlags & 0x70)    // map compatibility mode to exclusive
	{
	case shareCompat:
	case shareExclusive:
		dwShareMode = 0;
		break;
	case shareDenyWrite:
		dwShareMode = FILE_SHARE_READ;
		break;
	case shareDenyRead:
		dwShareMode = FILE_SHARE_WRITE;
		break;
	case shareDenyNone:
		dwShareMode = FILE_SHARE_WRITE | FILE_SHARE_READ;
		break;
	default:
		return FALSE;  // invalid share mode?
	}

	// Note: typeText and typeBinary are used in derived classes only.

	// map modeNoInherit flag
	SECURITY_ATTRIBUTES sa;
	sa.nLength = sizeof(sa);
	sa.lpSecurityDescriptor = NULL;
	sa.bInheritHandle = (nOpenFlags & modeNoInherit) == 0;

	// map creation flags
	DWORD dwCreateFlag;
	if (nOpenFlags & modeCreate)
	{
		if (nOpenFlags & modeNoTruncate)
			dwCreateFlag = OPEN_ALWAYS;
		else
			dwCreateFlag = CREATE_ALWAYS;
	}
	else
		dwCreateFlag = OPEN_EXISTING;

	DWORD dwFlags = FILE_ATTRIBUTE_NORMAL;
	if (nOpenFlags & osNoBuffer)
		dwFlags |= FILE_FLAG_NO_BUFFERING;
	if (nOpenFlags & osWriteThrough)
		dwFlags |= FILE_FLAG_WRITE_THROUGH;
	if (nOpenFlags & osRandomAccess)
		dwFlags |= FILE_FLAG_RANDOM_ACCESS;
	if (nOpenFlags & osSequentialScan)
		dwFlags |= FILE_FLAG_SEQUENTIAL_SCAN;

	// attempt file creation
	HANDLE hFile = ::CreateFile(lpszFileName, dwAccess, dwShareMode, &sa,
		dwCreateFlag, dwFlags, NULL);
	if (hFile == INVALID_HANDLE_VALUE)
		return FALSE;

	m_hFile = hFile;
	m_bCloseOnDelete = TRUE;

	return TRUE;
}

UINT CFile2::Read(void* lpBuf, UINT nCount)
{
	if (nCount == 0)
		return 0;   // avoid Win32 "null-read"

	DWORD dwRead;
	if (!::ReadFile(m_hFile, lpBuf, nCount, &dwRead, NULL))
		return -1;

	return (UINT)dwRead;
}

void CFile2::Write(const void* lpBuf, UINT nCount)
{
	if (nCount == 0)
		return;     // avoid Win32 "null-write" option


	DWORD nWritten;
	if (!::WriteFile(m_hFile, lpBuf, nCount, &nWritten, NULL) || nWritten != nCount)
		return;
}

ULONGLONG CFile2::Seek(LONGLONG lOff, UINT nFrom)
{
   LARGE_INTEGER liOff;

   liOff.QuadPart = lOff;
	liOff.LowPart = ::SetFilePointer(m_hFile, liOff.LowPart, &liOff.HighPart,
	  (DWORD)nFrom);
	if (liOff.LowPart  == (DWORD)-1)
	  return 0;

	return liOff.QuadPart;
}

ULONGLONG CFile2::GetPosition() const
{
   LARGE_INTEGER liPos;
   liPos.QuadPart = 0;
	liPos.LowPart = ::SetFilePointer(m_hFile, liPos.LowPart, &liPos.HighPart , FILE_CURRENT);
	if (liPos.LowPart == (DWORD)-1)
	  return 0;

	return liPos.QuadPart;
}

void CFile2::Flush()
{
	::FlushFileBuffers(m_hFile);
}

void CFile2::Close()
{
	BOOL bError = FALSE;
	if (m_hFile != INVALID_HANDLE_VALUE)
		bError = !::CloseHandle(m_hFile);

	m_hFile = INVALID_HANDLE_VALUE;
	m_bCloseOnDelete = FALSE;
	m_strFileName.Empty();
}

void CFile2::Abort()
{
	if (m_hFile != INVALID_HANDLE_VALUE)
	{
		// close but ignore errors
		::CloseHandle(m_hFile);
		m_hFile = INVALID_HANDLE_VALUE;
	}
	m_strFileName.Empty();
}

void CFile2::LockRange(ULONGLONG dwPos, ULONGLONG dwCount)
{
   ULARGE_INTEGER liPos;
   ULARGE_INTEGER liCount;

   liPos.QuadPart = dwPos;
   liCount.QuadPart = dwCount;
   ::LockFile(m_hFile, liPos.LowPart, liPos.HighPart, liCount.LowPart, liCount.HighPart); 
}

void CFile2::UnlockRange(ULONGLONG dwPos, ULONGLONG dwCount)
{
   ULARGE_INTEGER liPos;
   ULARGE_INTEGER liCount;

   liPos.QuadPart = dwPos;
   liCount.QuadPart = dwCount;
   ::UnlockFile(m_hFile, liPos.LowPart, liPos.HighPart, liCount.LowPart, liCount.HighPart);
}

void CFile2::SetLength(ULONGLONG dwNewLen)
{
	Seek(dwNewLen, (UINT)begin);

	::SetEndOfFile(m_hFile);
}

ULONGLONG CFile2::GetLength() const
{
   ULARGE_INTEGER liSize;
   liSize.LowPart = ::GetFileSize(m_hFile, &liSize.HighPart);
   if (liSize.LowPart == INVALID_FILE_SIZE)
	  return 0;

	return liSize.QuadPart;
}


void PASCAL CFile2::Rename(LPCTSTR lpszOldName, LPCTSTR lpszNewName)
{
	::MoveFile((LPTSTR)lpszOldName, (LPTSTR)lpszNewName);
}

void PASCAL CFile2::Remove(LPCTSTR lpszFileName)
{
	::DeleteFile((LPTSTR)lpszFileName);
}