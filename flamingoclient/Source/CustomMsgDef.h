#pragma once

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

#define			WM_VIEW_CHAT_IMAGE		WM_USER + 23	// 聊天气泡双击图片：请求打开看图窗（lParam = 图片本地路径 LPCTSTR，同步消息）
#define			WM_CHAT_FILE_ACTION		WM_USER + 24	// 文件气泡按钮点击：lParam = 堆上 balloonchatwnd::ChatFileActionData*（宿主聊天窗处理后 delete，异步消息）
#define			WM_CHAT_IMAGE_READY		WM_USER + 25	// 后台图片缩放完成：ChatImageCache 通知聊天窗重绘消息区（wParam/lParam 未用，异步消息）
#define			WM_GROUP_FILE_DOWNLOAD	WM_USER + 26	// 群文件行下载箭头点击：lParam = 堆上 CString*（entryid，宿主群聊窗据此查库并发起下载，处理后 delete，异步消息）
#define			WM_GROUP_FILE_UPLOAD_CANCEL	WM_USER + 27	// 群文件「上传中」行 ✕ 点击：lParam = uploadId（int，宿主群聊窗据此取消该笔上传，异步消息）
#define			WM_GROUP_FILE_DELETE	WM_USER + 28	// 群文件行垃圾桶点击：lParam = 堆上 CString*（entryid，宿主群聊窗据此确认并发起删除，处理后 delete，异步消息）
#define			WM_GROUP_FILE_DOWNLOAD_CANCEL	WM_USER + 29	// 群文件「下载中」行 ✕ 点击：lParam = 堆上 CString*（entryid，宿主群聊窗据此取消该笔下载，处理后 delete，异步消息）
#define			WM_CHAT_RECALL_REQUEST	WM_USER + 30	// 气泡右键「撤回」：lParam = 堆上 long long*（被撤消息的服务器 msgid，宿主聊天窗据此连同自己的 ctype/cid 发 cmd_recall_message，处理后 delete，异步消息）。消息列表不直接碰网络，故经宿主窗中转
#define			WM_CHAT_PIN_REQUEST		WM_USER + 31	// 气泡右键「Pin / 取消 Pin」：wParam = 目标状态（1 钉住、0 取消），lParam = 堆上 long long*（目标消息的服务器 msgid，宿主聊天窗据此连同自己的 ctype/cid 发 cmd_pin_message，处理后 delete，异步消息）。理由同上：消息列表不直接碰网络
#define			WM_CHAT_MENTION_ACTION	WM_USER + 31	// 气泡里 @某人 的片段右键选中某项：lParam = 堆上 balloonchatwnd::MentionActionData*（被 @ 成员的 uid / 显示名 + 选中的菜单项，宿主群聊窗据此打开单聊 / 资料窗 / 插入 @ / 复制昵称，处理后 delete，异步消息）。气泡不认识业务对象，故经宿主窗中转
#define			WM_CHAT_LOAD_OLDER		WM_USER + 32	// 消息区滚到接近顶部：请宿主聊天窗判断要不要拉更早的一页历史（无参，异步消息）。判定所需的分页状态归宿主窗持有，消息列表只报告"滚到顶部附近了"这一事实
#define			WM_CHAT_REPLY_REQUEST	WM_USER + 33	// 气泡右键「引用回复」：lParam = 被引用消息的服务器 msgid（long long*，堆上，宿主聊天窗据它组装引用快照并在输入区挂出引用条，处理后 delete，异步消息）
#define			WM_CHAT_LOCATE_QUOTED	WM_USER + 34	// 点击气泡里的引用块：请宿主聊天窗滚动定位到被引用的原消息（lParam = 堆上 long long*，原消息的服务器 msgid，处理后 delete，异步消息）。消息列表只报告"点了引用块"，找不到原消息时由宿主决定怎么提示
#define			WM_CHAT_IMAGE_ACTION	WM_USER + 35	// 图片气泡右键选了「图片另存为」/「再次发送」：lParam = 堆上 balloonchatwnd::ChatImageActionData*（图片本地路径 + 另存为默认文件名 + 选中的菜单项，处理后 delete，异步消息）。另存为要弹模态文件对话框、再次发送要调宿主窗的 SendImageFile，气泡两样都不该碰，故经宿主窗中转

#define			WM_CLOSE_MSGLOGDLG		WM_USER + 1	// 关闭消息记录浏览窗口

#define			IPC_CODE_MSGLOG_PASTE		1		// 消息记录浏览窗口粘贴消息
#define			IPC_CODE_MSGLOG_EXIT		2		// 消息记录浏览窗口退出消息