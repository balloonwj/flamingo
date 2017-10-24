#include "stdafx.h"
#include "TrayIcon.h"

struct TRAYDATA  
{  
	HWND hwnd;                                 
	UINT uID;                                 
	UINT uCallbackMessage;         
	DWORD Reserved[2];                 
	HICON hIcon;                                 
}; 

CTrayIcon::CTrayIcon(void)
{
	memset(&m_stNotifyIconData, 0, sizeof(m_stNotifyIconData));
	m_stNotifyIconData.cbSize = sizeof(m_stNotifyIconData);
	m_bHover = FALSE;
	m_dwTimerId = 0;
}

CTrayIcon::~CTrayIcon(void)
{
}

BOOL CTrayIcon::AddIcon(HWND hCallBackWnd, UINT uCallBackMsg, 
			            UINT uID, HICON hIcon, LPCTSTR lpszTip/* = NULL*/,
                        BOOL bPopupBalloon/* = FALSE*/, LPCTSTR lpszBalloonInfoTitle/* = NULL*/,
                        LPCTSTR lpszBalloonInfo/* = NULL*/, UINT uTimeout/* = 3000*/)
{
	m_stNotifyIconData.uFlags = NIF_ICON | NIF_MESSAGE;
    if (bPopupBalloon && lpszBalloonInfo != NULL && lpszBalloonInfo[0] != NULL)
    {
        m_stNotifyIconData.uFlags |= NIF_INFO;
        m_stNotifyIconData.uTimeout = uTimeout;
        _tcscpy_s(m_stNotifyIconData.szInfo, ARRAYSIZE(m_stNotifyIconData.szInfo), lpszBalloonInfo);
        if (lpszBalloonInfoTitle != NULL && lpszBalloonInfoTitle[0] != NULL)
            _tcscpy_s(m_stNotifyIconData.szInfoTitle, ARRAYSIZE(m_stNotifyIconData.szInfoTitle), lpszBalloonInfoTitle);
    }       
    else
        m_stNotifyIconData.uFlags &= (~NIF_INFO);

	m_stNotifyIconData.hWnd = hCallBackWnd;
	m_stNotifyIconData.uCallbackMessage = uCallBackMsg;
	m_stNotifyIconData.uID = uID;
	m_stNotifyIconData.hIcon = hIcon;
	if (lpszTip != NULL && _tcslen(lpszTip) > 0)
	{
		m_stNotifyIconData.uFlags |= NIF_TIP;
		_tcsncpy(m_stNotifyIconData.szTip, lpszTip, 
			sizeof(m_stNotifyIconData.szTip) / sizeof(TCHAR));
	}
	return ::Shell_NotifyIcon(NIM_ADD, &m_stNotifyIconData);
}

BOOL CTrayIcon::ModifyIcon(HICON hIcon, LPCTSTR lpszTip/* = NULL*/, UINT uID/*=1*/,
                           BOOL bPopupBalloon/* = FALSE*/, LPCTSTR lpszBalloonInfoTitle/* = NULL*/,
                           LPCTSTR lpszBalloonInfo/* = NULL*/, UINT uTimeout/* = 3000*/)
{
	m_stNotifyIconData.uFlags = NIF_ICON;
    if (bPopupBalloon && lpszBalloonInfo != NULL && lpszBalloonInfo[0] != NULL)
    {
        m_stNotifyIconData.uFlags |= NIF_INFO;
        m_stNotifyIconData.uTimeout = uTimeout;
        _tcscpy_s(m_stNotifyIconData.szInfo, ARRAYSIZE(m_stNotifyIconData.szInfo), lpszBalloonInfo);
        if (lpszBalloonInfoTitle != NULL && lpszBalloonInfoTitle[0] != NULL)
            _tcscpy_s(m_stNotifyIconData.szInfoTitle, ARRAYSIZE(m_stNotifyIconData.szInfoTitle), lpszBalloonInfoTitle);
    }
    else
        m_stNotifyIconData.uFlags &= (~NIF_INFO);

	m_stNotifyIconData.hIcon = hIcon;
	m_stNotifyIconData.uID = uID;
	if (lpszTip != NULL)
	{
		m_stNotifyIconData.uFlags |= NIF_TIP;
		_tcsncpy(m_stNotifyIconData.szTip, lpszTip, 
			sizeof(m_stNotifyIconData.szTip) / sizeof(TCHAR));
	}
	return ::Shell_NotifyIcon(NIM_MODIFY, &m_stNotifyIconData);
}

BOOL CTrayIcon::RemoveIcon()
{
	return ::Shell_NotifyIcon(NIM_DELETE, &m_stNotifyIconData);
}

LRESULT CTrayIcon::OnTrayIconNotify(WPARAM wParam, LPARAM lParam)
{
	UINT uID = (UINT)wParam;
	UINT uMsg = (UINT)lParam;

	if (uID == m_stNotifyIconData.uID)
	{
		if (uMsg == WM_MOUSEMOVE)
		{
			if (!m_bHover)
			{
				m_bHover = TRUE;
				::PostMessage(m_stNotifyIconData.hWnd, m_stNotifyIconData.uCallbackMessage, 
					m_stNotifyIconData.uID, WM_MOUSEHOVER);
				m_dwTimerId = ::SetTimer(m_stNotifyIconData.hWnd, 990, 160, NULL);
			}
		}
	}

	return 0;
}

void CTrayIcon::OnTimer(UINT_PTR nIDEvent)
{
	if (nIDEvent == m_dwTimerId)
	{
		RECT rect;
		GetTrayIconRect(&rect);

		POINT pt = {0};
		::GetCursorPos(&pt);

		if (!::PtInRect(&rect, pt))
		{
			m_bHover = FALSE;
			::PostMessage(m_stNotifyIconData.hWnd, m_stNotifyIconData.uCallbackMessage, 
				m_stNotifyIconData.uID, WM_MOUSELEAVE);
			::KillTimer(m_stNotifyIconData.hWnd, m_dwTimerId);
			m_dwTimerId = NULL;
		}
	}
}

// 获取托盘图标区域位置
BOOL CTrayIcon::GetTrayIconRect(RECT* lpRect)
{
	if (NULL == lpRect)
		return FALSE;

	::SetRectEmpty(lpRect);

	HWND hWnd = FindTrayNotifyWnd();
	if (hWnd!=NULL && IsWindow(hWnd) )
	{
		if (!EnumNotifyWindow(hWnd,*lpRect))		// 如果没在普通托盘区
		{
			hWnd = FindNotifyIconOverflowWindow();	// 在溢出区（win7）
			if (hWnd!=NULL && IsWindow(hWnd))
			{
				EnumNotifyWindow(hWnd,*lpRect);
			}
		}
	}

	return TRUE;
}

//BOOL CTrayIcon::ShowBalloonTip(LPCTSTR szMsg, LPCTSTR szTitle, DWORD dwInfoFlags = NIIF_INFO, UINT uTimeout = 1000)
//{
//    m_nid.cbSize = sizeof(NOTIFYICONDATA);
//    m_nid.uFlags = NIF_INFO;
//    m_nid.uVersion = NOTIFYICON_VERSION;
//    m_nid.uTimeout = uTimeout;
//    m_nid.dwInfoFlags = dwInfoFlags;
//    strcpy(m_nid.szInfo, szMsg ? szMsg : _T(""));
//    strcpy(m_nid.szInfoTitle, szTitle ? szTitle : _T(""));
//
//    return 0 != Shell_NotifyIcon(NIM_MODIFY, &m_nid);
//}

// 枚举获取托盘图标区域位置
BOOL CTrayIcon::EnumNotifyWindow(HWND hWnd, RECT &rect)
{
	BOOL bSuc = FALSE;
	DWORD dwProcessId = 0, dwThreadId = 0, dwDesiredAccess;
	HANDLE hProcess;
	LPVOID lpBuffer;
	int nButtons;
	HWND hOwnerWnd = NULL;

	if (NULL==hWnd || !::IsWindow(hWnd))
		return FALSE;

	dwThreadId = ::GetWindowThreadProcessId(hWnd, &dwProcessId);
	if (0 == dwProcessId || 0 == dwThreadId)
		return FALSE;

	dwDesiredAccess = PROCESS_ALL_ACCESS|PROCESS_VM_OPERATION|PROCESS_VM_READ|PROCESS_VM_WRITE;
	hProcess = ::OpenProcess(dwDesiredAccess, 0, dwProcessId);
	if (NULL == hProcess)
		return FALSE;

	lpBuffer = ::VirtualAllocEx(hProcess, 0, 1024, MEM_COMMIT, PAGE_READWRITE);
	if (lpBuffer != NULL)
	{
		nButtons = ::SendMessage(hWnd, TB_BUTTONCOUNT, 0, 0);	// 获取托盘图标数量
		for (int i = 0; i < nButtons; i++)
		{
			RECT rc = {0}; 
			TBBUTTON stButton = {0};
			TRAYDATA stTrayData = {0};

			::SendMessage(hWnd, TB_GETBUTTON, i, (LPARAM)lpBuffer);	// 获取第i个托盘图标信息
			BOOL bRet = ::ReadProcessMemory(hProcess, lpBuffer, &stButton, sizeof(TBBUTTON), 0);
			///TODO: 解决这个问题
			//这个函数执行失败导致，错误码：0x0000012b 仅完成部分的 ReadProcessMemory 或 WriteProcessMemory 请求。
			//see: http://bbs.csdn.net/topics/90433523
			long nOffset = (long)(&stButton.dwData) - (long)&stButton;
			char* pDest = (char*)lpBuffer+nOffset;
			bRet = ::ReadProcessMemory(hProcess, (LPVOID)pDest, &stTrayData, sizeof(TRAYDATA), 0);
			if (bRet != 0 && stTrayData.hwnd == m_stNotifyIconData.hWnd)
			{
				::SendMessage(hWnd, TB_GETITEMRECT, (WPARAM)i, (LPARAM)lpBuffer); // 获取第i个托盘图标区域
				bRet = ::ReadProcessMemory(hProcess, lpBuffer, &rc, sizeof(rc),0);  // 读取托盘图标区域
				if (bRet != 0)
				{
					::ClientToScreen(hWnd, (LPPOINT)&rc);
					::ClientToScreen(hWnd, ((LPPOINT)&rc)+1);
					rect = rc;
				}
				bSuc = TRUE;
				break;
			}
		}
	}

	if (lpBuffer != NULL)
		::VirtualFreeEx(hProcess, lpBuffer, 0, MEM_RELEASE);
	::CloseHandle(hProcess);

	return bSuc;
}

// 获取普通托盘区窗口句柄
HWND CTrayIcon::FindTrayNotifyWnd()
{
	HWND hWnd = ::FindWindow(_T("Shell_TrayWnd"), NULL);
	if (hWnd!=NULL && ::IsWindow(hWnd))
	{
		hWnd = ::FindWindowEx(hWnd, 0, _T("TrayNotifyWnd"), NULL);
		if (hWnd!=NULL && ::IsWindow(hWnd))
		{
			HWND hWndPaper = ::FindWindowEx(hWnd, 0, _T("SysPager"), NULL);
			if(hWndPaper!=NULL && ::IsWindow(hWnd))
				hWnd = ::FindWindowEx(hWndPaper, 0, _T("ToolbarWindow32"), NULL);
			else
				hWnd = ::FindWindowEx(hWnd, 0, _T("ToolbarWindow32"), NULL);
		}
	}
	return hWnd;
}

// 获取溢出托盘区窗口句柄
HWND CTrayIcon::FindNotifyIconOverflowWindow()
{
	HWND hWnd = ::FindWindow(_T("NotifyIconOverflowWindow"), NULL);
	if (hWnd!=NULL && ::IsWindow(hWnd))
		hWnd = ::FindWindowEx(hWnd, NULL, _T("ToolbarWindow32"), NULL);
	return hWnd;
}