// MingUTalk CMainDlg自定义消息
#define			WM_TRAYICON_NOTIFY		WM_USER + 1		// 系统托盘图标通知消息
#define			WM_SHOW_BUDDYCHATDLG	WM_USER + 2		// 显示好友会话聊天对话框消息
#define			WM_SHOW_GROUPCHATDLG	WM_USER + 3		// 显示群会话聊天对话框消息
#define			WM_SHOW_SESSCHATDLG		WM_USER + 4		// 显示临时会话聊天对话框消息
#define			WM_SHOW_SYSGROUPCHATDLG	WM_USER + 5		// 显示群系统对话框消息
#define			WM_SHOW_BUDDYINFODLG	WM_USER + 6		// 显示好友信息对话框消息
#define			WM_SHOW_GMEMBERINFODLG	WM_USER + 8		// 显示群成员信息对话框消息
#define			WM_SHOW_GROUPINFODLG	WM_USER + 9		// 显示群信息对话框消息
#define			WM_CLOSE_BUDDYCHATDLG	WM_USER + 10	// 关闭好友会话聊天对话框消息
#define			WM_CLOSE_GROUPCHATDLG	WM_USER + 11	// 关闭群会话聊天对话框消息
#define			WM_CLOSE_SESSCHATDLG	WM_USER + 12	// 关闭临时会话聊天对话框消息
#define			WM_CLOSE_SYSGROUPCHATDLG WM_USER + 13	// 关闭群系统对话框消息
#define			WM_CLOSE_BUDDYINFODLG	WM_USER + 14	// 关闭好友信息对话框消息
#define			WM_CLOSE_GROUPINFODLG	WM_USER + 15	// 关闭群成员信息对话框消息
#define			WM_CLOSE_GMEMBERINFODLG	WM_USER + 16	// 关闭群信息对话框消息
#define			WM_DEL_MSG_SENDER		WM_USER + 17	// 删除发送者消息
#define			WM_CANCEL_FLASH			WM_USER + 18	// 取消托盘图标闪烁消息
#define			WM_SETDLGINITFOCUS		WM_USER + 19	// 设置对话框初始焦点

#define			WM_SHOW_USERINFODLG		WM_USER + 20    // 显示个人信息对话框消息
#define			WM_CLOSE_USERINFODLG	WM_USER + 21	// 关闭个人信息对话框消息

#define			WM_CLOSE_MAINDLG		WM_USER + 22	// 关闭主对话框

#define			WM_CLOSE_MSGLOGDLG		WM_USER + 1	// 关闭消息记录浏览窗口

#define			IPC_CODE_MSGLOG_PASTE		1		// 消息记录浏览窗口粘贴消息
#define			IPC_CODE_MSGLOG_EXIT		2		// 消息记录浏览窗口退出消息