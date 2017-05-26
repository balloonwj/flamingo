#include "stdafx.h"
#include "ChatDlgCommon.h"

//窗口抖动线程
DWORD __stdcall ShakeWindowThreadProc(LPVOID pParameters)
{
    HWND hwnd = (HWND)pParameters;
    
    if (::IsIconic(hwnd))
    {
        ::ShowWindow(hwnd, SW_RESTORE);
    }

    RECT rtWindow;
    ::GetWindowRect(hwnd, &rtWindow);
    long x = rtWindow.left;
    long y = rtWindow.top;
    long cxWidth = rtWindow.right - rtWindow.left;
    long cyHeight = rtWindow.bottom - rtWindow.top;
    const long nOffset = 9;
    const long SLEEP_INTERAL = 60;

    for (long i = 0; i <= 2; ++i)
    {
        ::MoveWindow(hwnd, x + nOffset, y - nOffset, cxWidth, cyHeight, FALSE);
        ::Sleep(SLEEP_INTERAL);
        ::MoveWindow(hwnd, x - nOffset, y - nOffset, cxWidth, cyHeight, FALSE);
        ::Sleep(SLEEP_INTERAL);
        ::MoveWindow(hwnd, x - nOffset, y + nOffset, cxWidth, cyHeight, FALSE);
        ::Sleep(SLEEP_INTERAL);
        ::MoveWindow(hwnd, x + nOffset, y + nOffset, cxWidth, cyHeight, FALSE);
        ::Sleep(SLEEP_INTERAL);
        ::MoveWindow(hwnd, x, y, cxWidth, cyHeight, FALSE);
        ::Sleep(SLEEP_INTERAL);
    }

    return 0;
}

void ShakeWindow(HWND hwnd, long nTimes)
{
	//新建一个现成来抖动窗口，避免Sleep时阻塞主线程消息循环
    HANDLE hShakeWindowThread = ::CreateThread(NULL, 0, ShakeWindowThreadProc, (LPVOID)hwnd, 0, NULL);
    ::CloseHandle(hShakeWindowThread);   
}

