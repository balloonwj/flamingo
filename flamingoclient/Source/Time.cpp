#include "stdafx.h"
#include "Time.h"


void GenerateChatImageSavedName(CString& strFileName)
{
	SYSTEMTIME st;
	::GetLocalTime(&st);
	strFileName.Format(_T("FlamingoͼƬ%04d%02d%02d%02d%02d%02d"), st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
}