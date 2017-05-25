#pragma once

//放置聊天对话框公用常用和函数


#define CHATDLG_TOP_FIXED_HEIGHT			100
#define CHATDLG_BOTTOM_FIXED_HEIGHT			32
#define CHATDLG_LEFT_FIXED_WIDTH			4
#define CHATDLG_RIGHT_FIXED_WIDTH			4

#define CHATDLG_TOOLBAR_HEIGHT				32

#define CHATDLG_MSGLOG_WIDTH				367

#define MAX_CHAT_IMAGE_SIZE					10*1024*1024
#define MAX_OFFLINE_FILE_SIZE				INT_MAX

void ShakeWindow(HWND hwnd, long nTimes);