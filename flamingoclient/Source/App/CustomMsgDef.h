/**
 *  客户端早期（v1）就存在的一批窗口自定义消息号，收件方主要是各个
 *  聊天窗口。新增的 v2 消息一律定义在 UserSessionData.h，不要再往本文件里加。
 *
 *  号段约定见开发约定文档「UI 约定」第 6 条：本文件这批消息全部落在
 *  FMG_MSG_FIRST 之下，刻意排除在 CFlamingoClient::ProxyWndProc 的收件范围之外
 *  —— 它们都是窗口之间直接投递的消息，不经代理窗中转。
 *
 *  号值不得重复，由单元测试 Source\Tests\MessageIdRegistryTests.cpp 守着。
 *
 *  balloonwj@qq.com   2026-08-16
 */

#pragma once

// MingUTalk 时期的自定义消息
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
#define			WM_CANCEL_FLASH			WM_USER + 18	// 取消托盘图标闪烁消息

// 2026-08-16 删除以下五条零引用的遗留定义：WM_USER + 17 WM_DEL_MSG_SENDER、
// WM_USER + 19 WM_SETDLGINITFOCUS、WM_USER + 20 WM_SHOW_USERINFODLG、
// WM_USER + 21 WM_CLOSE_USERINFODLG、WM_USER + 1 WM_CLOSE_MSGLOGDLG。
// 它们对应的界面早已改由 v2 那套窗口实现，全仓库没有任何投递点与处理点。
// 其中 WM_CLOSE_MSGLOGDLG 与本文件开头的 WM_TRAYICON_NOTIFY 同取 WM_USER + 1，
// 删掉它也就消除了这处同文件内的重号。腾出的号可以重新分配，重号由
// Source\Tests\MessageIdRegistryTests.cpp 的守卫用例把关。

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
// 本条原先误取 WM_USER + 31，与上一行的 WM_CHAT_PIN_REQUEST 完全撞号，而
// BalloonGroupChatWindow.h 的消息映射里两条<u>同时注册</u>：ATL 只派发给先注册的那条，
// 于是群聊窗的 @ 菜单永远走进 Pin 的处理函数，MentionActionData* 被当成 long long*
// 读取并按错误类型 delete（该结构体含 CString 成员，析构不匹配）。2026-08-14 改到
// WM_USER + 41（本文件当时已用到 +40）。
#define			WM_CHAT_MENTION_ACTION	WM_USER + 41	// 气泡里 @某人 的片段右键选中某项：lParam = 堆上 balloonchatwnd::MentionActionData*（被 @ 成员的 uid / 显示名 + 选中的菜单项，宿主群聊窗据此打开单聊 / 资料窗 / 插入 @ / 复制名字，处理后 delete，异步消息）。气泡不认识业务对象，故经宿主窗中转
#define			WM_CHAT_LOAD_OLDER		WM_USER + 32	// 消息区滚到接近顶部：请宿主聊天窗判断要不要拉更早的一页历史（无参，异步消息）。判定所需的分页状态归宿主窗持有，消息列表只报告"滚到顶部附近了"这一事实
#define			WM_CHAT_REPLY_REQUEST	WM_USER + 33	// 气泡右键「引用回复」：lParam = 被引用消息的服务器 msgid（long long*，堆上，宿主聊天窗据它组装引用快照并在输入区挂出引用条，处理后 delete，异步消息）
#define			WM_CHAT_LOCATE_QUOTED	WM_USER + 34	// 点击气泡里的引用块：请宿主聊天窗滚动定位到被引用的原消息（lParam = 堆上 long long*，原消息的服务器 msgid，处理后 delete，异步消息）。消息列表只报告"点了引用块"，找不到原消息时由宿主决定怎么提示
#define			WM_CHAT_FOCUS_AREA		WM_USER + 39	// 报告聊天窗里「用户当前在哪一块操作」，wParam 取 CHAT_FOCUS_INPUT / CHAT_FOCUS_MESSAGES（异步消息，无 lParam）。窗口据它路由 Ctrl+C：焦点在发送框就让输入框自己复制，焦点在接收框才复制气泡选区。之所以要自己维护这个"逻辑焦点"而不是查 Win32 焦点：点消息区时 DuiHost 只挪 DUI 内部焦点、<u>不调 ::SetFocus</u>（见 DuiHost::OnLButtonDown），输入框始终握着系统焦点，查它永远得到"焦点在发送框"
#define			CHAT_FOCUS_INPUT		1				// 发送框（输入区）
#define			CHAT_FOCUS_MESSAGES		2				// 接收框（消息区）
#define			WM_CHAT_CANCEL_LINK_OPEN	WM_USER + 38// 撤销上一条 WM_CHAT_OPEN_LINK（无参，异步消息）。气泡双击时投出：双击的第一下本来就是一次完整单击，落在链接上会请求打开浏览器，而双击的语义是"选中整条"，故用它把还没执行的那次打开取消掉
#define			WM_CHAT_COPY_SELECTION	WM_USER + 37// 请宿主聊天窗把「消息区里当前选中的那段文字」复制到剪贴板（无参，<u>同步</u> SendMessage）。返回 1 表示确实有选区且已复制、调用方应吞掉这次按键；返回 0 表示消息区没有选区、按键该照常交给原处理者（输入框）。输入框的键盘钩子据此实现 Ctrl+C：气泡有选区就复制气泡的，没有才让输入框自己复制
#define			WM_CHAT_OPEN_LINK		WM_USER + 36// 气泡里的超链接被点击 / 右键选了「在浏览器中打开」：lParam = 堆上 std::wstring*（规范化后的完整 URL，处理后 delete，异步消息）。宿主聊天窗先过 balloonchatwnd::IsSafeLinkUrl 的协议白名单再 ShellExecute —— 气泡不该直接拉起外部程序，安全判断也只该有一处
#define			WM_CHAT_FOLDER_ACTION	WM_USER + 40	// 文件夹气泡按钮点击：lParam = 堆上 balloonchatwnd::ChatFolderActionData*（宿主聊天窗处理后 delete，异步消息）。与 WM_CHAT_FILE_ACTION 分开一条：文件夹的动作枚举、定位方式（会话 id）都与文件那套不同，共用一条消息只会让宿主先猜"这条是文件还是文件夹"
#define			WM_CHAT_IMAGE_ACTION	WM_USER + 35	// 图片气泡右键选了「图片另存为」/「再次发送」：lParam = 堆上 balloonchatwnd::ChatImageActionData*（图片本地路径 + 另存为默认文件名 + 选中的菜单项，处理后 delete，异步消息）。另存为要弹模态文件对话框、再次发送要调宿主窗的 SendImageFile，气泡两样都不该碰，故经宿主窗中转

#define			IPC_CODE_MSGLOG_PASTE		1		// 消息记录浏览窗口粘贴消息
#define			IPC_CODE_MSGLOG_EXIT		2		// 消息记录浏览窗口退出消息