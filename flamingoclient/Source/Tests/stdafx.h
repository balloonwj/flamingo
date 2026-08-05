/**
 *  stdafx.h  (FlamingoUnit 测试工程)
 *
 *  FlamingoUnit 的预编译头。把 balloonui 头文件所需的 ATL/WTL 前置依赖
 *  一次性引入 —— 与 balloonui 各 Demo 的 stdafx.h 同思路。测试 .cpp 在
 *  include 任何 balloonui / BalloonChat 头之前必须先 include 本文件。
 *
 *  balloonwj@qq.com   2026.05.21
 */

#pragma once

// FlamingoUnit.vcxproj 命令行也定义了 _CRT_SECURE_NO_WARNINGS，加卫哨避免
// C4005 宏重定义。
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#define _WINSOCK_DEPRECATED_NO_WARNINGS

#define WINVER          0x0601      // 目标系统下限 = Windows 7
#define _WIN32_WINNT    0x0601
#define _WIN32_IE       0x0601
#define _RICHEDIT_VER   0x0200
#define _WTL_NO_CSTRING             // 用 ATL 的 CString
#define _WTL_NO_WTYPES

#include <atlbase.h>
#include <atlstr.h>
#include <atltypes.h>
#include <atlapp.h>

// balloonui 的 ATL 窗口类引用一个全局 _Module，定义在 main.cpp。
extern CAppModule _Module;

#include <atlwin.h>
#include <atlcrack.h>
#include <atlframe.h>
#include <atlctrls.h>
#include <atlctrlx.h>
#include <atlmisc.h>

#include <vector>
#include <memory>
#include <string>
#include <algorithm>
