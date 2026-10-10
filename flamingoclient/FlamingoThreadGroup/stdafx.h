/**
 *  FlamingoThreadGroup.dll 工程的公共前置头文件。
 *
 *  与 exe 共用的源文件（如 Source/Common/I18n/I18n.cpp）开头若写着 #include "stdafx.h"，编进本 DLL 时找到的是
 *  本文件（工程把本目录放在包含路径的最前面）。内容与 FlamingoLock/stdafx.h 保持同一套前置定义（系统版本宏、ATL / WTL），
 *  保证同一份源文件在各工程里看到的声明一致；帖子群窗口用 balloonui，所以要带 ATL / WTL。
 *
 *  本工程不使用预编译头，本文件只是普通头文件。
 *
 *  balloonwj@qq.com   2026-10-07
 */
#pragma once

//工程命令行也定义了这两个宏，加卫哨避免 C4005 宏重定义
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#ifndef _WINSOCK_DEPRECATED_NO_WARNINGS
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#endif

//目标系统下限与 exe 相同（Windows 7）：Win8 起才有的系统接口一律运行时动态取用
#define WINVER          0x0601
#define _WIN32_WINNT    0x0601
#define _WIN32_IE       0x0601
#define _RICHEDIT_VER   0x0300      // RichEdit 3.0，WTL 10 要求的下限
#define _WTL_NO_CSTRING             // 用 ATL 的 CString
#define _WTL_NO_WTYPES

#include <atlbase.h>
#include <atlstr.h>
#include <atltypes.h>
#include <atlapp.h>

//balloonui 的 ATL 窗口类引用一个全局 _Module。DLL 有自己的一份，在 DllMain.cpp 定义并初始化
extern CAppModule _Module;

#include <atlwin.h>
#include <atlcrack.h>
#include <atlframe.h>
#include <atlctrls.h>
#include <atlctrlx.h>
#include <atlmisc.h>

#include <stdint.h>
#include <vector>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <algorithm>
