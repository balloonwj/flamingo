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
	// 只弹一条托盘气泡提示，不改动图标与鼠标悬停提示文字。
	// 与 ModifyIcon(..., bPopupBalloon=TRUE, ...) 的区别：本方法提交给系统的
	// uFlags 只含 NIF_INFO，故不会把当前图标覆盖掉 —— 托盘正在闪新消息头像时
	// 弹提示也不会打断闪烁。
	//   lpszBalloonInfoTitle：气泡标题，可为 NULL / 空串（则不显示标题）。
	//   lpszBalloonInfo：     气泡正文，为 NULL / 空串时本方法什么都不做。
	//   uTimeout：            期望停留时长（毫秒）；Vista 及以后由系统统一控制，仅作参考。
	//   uID：                 托盘图标 id，须与 AddIcon 时一致（本程序固定为 1）。
	//   返回值：TRUE=已提交给系统，FALSE=参数为空或 Shell_NotifyIcon 失败。
	BOOL ShowBalloonTip(LPCTSTR lpszBalloonInfoTitle, LPCTSTR lpszBalloonInfo,
	                    UINT uTimeout = 3000, UINT uID = 1);
	// 只改鼠标悬停提示文字，不改动图标、也不弹气泡。
	// 与 ModifyIcon(hIcon, lpszTip) 的区别：本方法提交给系统的 uFlags 只含 NIF_TIP，
	// 不含 NIF_ICON，故托盘正在闪新消息头像时刷新提示文字也不会把闪烁打断。
	// 典型用法：用户切换在线状态后刷新"昵称 / 账号 / 当前状态"那行提示。
	//   lpszTip：悬停提示文字，为 NULL 时本方法什么都不做。
	//   uID：    托盘图标 id，须与 AddIcon 时一致（本程序固定为 1）。
	//   返回值：TRUE=已提交给系统，FALSE=参数为空或 Shell_NotifyIcon 失败。
	BOOL ModifyTip(LPCTSTR lpszTip, UINT uID = 1);
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
