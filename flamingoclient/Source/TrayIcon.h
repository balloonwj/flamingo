#pragma once

class CTrayIcon		// 托盘图标封装类
{
public:
	CTrayIcon(void);
	~CTrayIcon(void);

public:
	BOOL AddIcon(HWND hCallBackWnd, UINT uCallBackMsg, 
		        UINT uID, HICON hIcon, LPCTSTR lpszTip = NULL, 
                BOOL bPopupBalloon = FALSE, LPCTSTR lpszBalloonInfoTitle = NULL, LPCTSTR lpszBalloonInfo = NULL, UINT uTimeout = 3000);
	BOOL ModifyIcon(HICON hIcon, LPCTSTR lpszTip = NULL, UINT uID = 1,
                    BOOL bPopupBalloon = FALSE, LPCTSTR lpszBalloonInfoTitle = NULL, LPCTSTR lpszBalloonInfo = NULL, UINT uTimeout = 3000);
	BOOL RemoveIcon();
	LRESULT OnTrayIconNotify(WPARAM wParam, LPARAM lParam);
	void OnTimer(UINT_PTR nIDEvent);
	BOOL GetTrayIconRect(RECT* lpRect);			// 获取托盘图标区域位置

private:
	BOOL EnumNotifyWindow(HWND hWnd, RECT &rect);	// 枚举获取托盘图标区域位置
	HWND FindTrayNotifyWnd();						// 获取普通托盘区窗口句柄
	HWND FindNotifyIconOverflowWindow();			// 获取溢出托盘区窗口句柄

private:
	NOTIFYICONDATA m_stNotifyIconData;
	BOOL m_bHover;
	DWORD m_dwTimerId;
};
