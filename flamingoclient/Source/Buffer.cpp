#include "stdafx.h"
#include "Buffer.h"

CBuffer::CBuffer()
{
	memset(m_cFixData, 0, sizeof(m_cFixData));

	m_lpData = NULL;
	m_dwSize = m_dwMaxSize = 0;
	m_dwGrowBy = 1024* 4;
}

CBuffer::~CBuffer(void)
{
	Release();
}

char* CBuffer::GetBuffer(DWORD dwSize)
{
	Release();
	SetSize(dwSize);

	return (char*)m_lpData;
}

void CBuffer::SetSize(DWORD dwNewSize)
{
	if (dwNewSize == 0)
	{
		if (m_lpData != NULL)
		{
			if (m_lpData != m_cFixData)
				free(m_lpData);
		}
		m_lpData = NULL;
		m_dwSize = m_dwMaxSize = 0;
	}
	else
	{
		dwNewSize += 8;
		if (dwNewSize > m_dwMaxSize)			// 剩余空间不足,重新分配空间
		{
			ReAlloc(dwNewSize);
		}
		else
		{
			if (dwNewSize < m_dwSize)
			{
				memset(m_lpData + dwNewSize, 0, (m_dwSize-dwNewSize)* sizeof(BYTE));
				m_dwSize = dwNewSize;
			}
		}
	}
}

void CBuffer::SetGrowBy(DWORD dwGrowBy)
{
	m_dwGrowBy = dwGrowBy;
}

BOOL CBuffer::Add(const CHAR* lpStr)
{
	return Add(lpStr, strlen(lpStr));
}

BOOL CBuffer::Add(const WCHAR* lpStr)
{
	return Add((const char*)lpStr, wcslen(lpStr)* sizeof(WCHAR));
}

BOOL CBuffer::Add(CBuffer& src)
{
	return Add(src.GetData(), src.GetSize());
}

BOOL CBuffer::Add(const char* lpData, DWORD dwSize)
{
	if (NULL == lpData || dwSize <= 0)
		return FALSE;

	DWORD dwNeedSize = m_dwSize + dwSize + 8;
	if (dwNeedSize > m_dwMaxSize)			// 剩余空间不足,重新分配空间
	{
		BOOL bRet = ReAlloc(dwNeedSize);
		if (!bRet)
			return FALSE;
	}

	memcpy(m_lpData + m_dwSize, lpData, dwSize);
	m_dwSize += dwSize;

	return TRUE;
}

const char* CBuffer::GetData()
{
	return m_lpData;
}

void* CBuffer::GetExtra()
{
	return m_Extra;
}

void CBuffer::SetExtra(void* pExtra)
{
	m_Extra = pExtra;
}

DWORD CBuffer::GetSize()
{
	return m_dwSize;
}

void CBuffer::Release()
{
	SetSize(0);
}

BOOL CBuffer::ReAlloc(DWORD dwSize)
{
	DWORD dwGap = dwSize - m_dwMaxSize;
	DWORD dwTemp = dwGap / m_dwGrowBy + 1;
	DWORD dwReAllocSize = m_dwMaxSize + dwTemp* m_dwGrowBy;

	if (m_lpData == NULL)
	{
		if (dwReAllocSize <= sizeof(m_cFixData))
		{
			memset(m_cFixData, 0, sizeof(m_cFixData));
			m_lpData = m_cFixData;
			m_dwMaxSize = sizeof(m_cFixData);
			m_dwSize = 0;
		}
		else
		{
			char* lpTemp = (char*)malloc(dwReAllocSize);
			if (NULL == lpTemp)
				return FALSE;

			m_lpData = lpTemp;
			memset(m_lpData, 0, dwReAllocSize);
			m_dwMaxSize = dwReAllocSize;
			m_dwSize = 0;
		}
	}
	else
	{
		if (m_lpData == m_cFixData)
		{
			char* lpTemp = (char*)malloc(dwReAllocSize);
			if (NULL == lpTemp)
				return FALSE;

			m_lpData = lpTemp;
			if (m_dwSize > 0)
				memcpy(m_lpData, m_cFixData, m_dwSize);
			memset(m_lpData + m_dwSize, 0, dwReAllocSize - m_dwSize);
			m_dwMaxSize = dwReAllocSize;
		}
		else
		{
			char* lpTemp = (char*)realloc(m_lpData, dwReAllocSize);
			if (NULL == lpTemp)
				return FALSE;

			m_lpData = lpTemp;
			memset(m_lpData + m_dwSize, 0, dwReAllocSize - m_dwSize);
			m_dwMaxSize = dwReAllocSize;
		}
	}

	return TRUE;
}