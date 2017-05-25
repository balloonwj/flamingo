#include "stdafx.h"
#include "ChatDlgCommon.h"

//´°¿Ú¶¶¶¯
void ShakeWindow(HWND hwnd, long nTimes)
{
	if(nTimes <= 0)
		return;
	
	if(::IsIconic(hwnd))
	{
		::ShowWindow(hwnd, SW_RESTORE);
	}

	RECT rtWindow;
	::GetWindowRect(hwnd, &rtWindow);
	long x = rtWindow.left;
	long y = rtWindow.top;
	long cxWidth = rtWindow.right-rtWindow.left;
	long cyHeight = rtWindow.bottom-rtWindow.top;
	const long nOffset  = 9;
	const long SLEEP_INTERAL = 60;
	
	for(long i=0; i<=2*nTimes; ++i)
	{
		::MoveWindow(hwnd, x+nOffset, y-nOffset, cxWidth, cyHeight, FALSE);
		::Sleep(SLEEP_INTERAL);
		::MoveWindow(hwnd, x-nOffset, y-nOffset, cxWidth, cyHeight, FALSE);
		::Sleep(SLEEP_INTERAL);
		::MoveWindow(hwnd, x-nOffset, y+nOffset, cxWidth, cyHeight, FALSE);
		::Sleep(SLEEP_INTERAL);
		::MoveWindow(hwnd, x+nOffset, y+nOffset ,cxWidth, cyHeight, FALSE);
		::Sleep(SLEEP_INTERAL);
		::MoveWindow(hwnd, x, y, cxWidth, cyHeight, FALSE);
		::Sleep(SLEEP_INTERAL);
	}
}

