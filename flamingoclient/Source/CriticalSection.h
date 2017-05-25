#pragma once

class CAutoCriticalSection
{
public:
	CAutoCriticalSection(CRITICAL_SECTION& cs);
	~CAutoCriticalSection();

	void Lock();
	void Unlock();

private:
	CRITICAL_SECTION&	 m_cs;	
};