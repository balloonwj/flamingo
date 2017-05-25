#include "stdafx.h"
#include "CriticalSection.h"

CAutoCriticalSection::CAutoCriticalSection(CRITICAL_SECTION& cs)
: m_cs(cs)
{
	Lock();
}

CAutoCriticalSection::~CAutoCriticalSection()
{
	Unlock();
}

void CAutoCriticalSection::Lock()
{
	::EnterCriticalSection(&m_cs);
}
void CAutoCriticalSection::Unlock()
{
	::LeaveCriticalSection(&m_cs);
}