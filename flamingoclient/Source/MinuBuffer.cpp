#include "stdafx.h"
#include "MiniBuffer.h"
#include <assert.h>
#include "Utils.h"

CMiniBuffer::CMiniBuffer(long nSize, BOOL bAutoRelease)
{
	assert(nSize > 0);
	
	m_nSize = nSize;
	m_bAutoRelease = bAutoRelease;

	m_pData = new char[nSize];
	memset(m_pData, 0, nSize);
}

CMiniBuffer::~CMiniBuffer()
{
	if(m_bAutoRelease)
		DEL_ARR(m_pData);
}

void CMiniBuffer::Release()
{
	DEL_ARR(m_pData);
	m_nSize = 0;
}

long CMiniBuffer::GetSize()
{
	return m_nSize;
}

char* CMiniBuffer::GetBuffer()
{
	return m_pData;
}

//char* CMiniBuffer::operator PSTR()
//{
//	return m_pData;
//}
	
void CMiniBuffer::EnableAutoRelease(BOOL bAutoRelease)
{
	m_bAutoRelease = bAutoRelease;
}

BOOL CMiniBuffer::IsAutoRelease()
{
	return m_bAutoRelease;
}