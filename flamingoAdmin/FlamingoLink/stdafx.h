/**
 *  FlamingoLink.dll 工程的公共前置头文件。
 *
 *  本 DLL 直接编译了控制台的几个源文件（Theme / Widgets 下的表格控件）与 PC 客户端 Source 下的
 *  帧编解码文件，它们开头写着 #include "stdafx.h"。工程把本目录放在包含路径最前面，于是这些文件
 *  在本工程里找到的是本文件。目标平台与 WTL 裁剪开关与 flamingoAdmin/stdafx.h 保持一致，保证同一份
 *  源文件在控制台与本 DLL 里看到的声明相同。
 *
 *  与控制台不同的一点：本 DLL 要用 Winsock 2，winsock2.h 必须排在 windows.h（由 atlbase.h 引入）之前，
 *  否则 windows.h 先引入旧的 winsock.h，两者的声明冲突。
 *
 *  本工程不使用预编译头，本文件只是普通头文件。
 *
 *  balloonwj@qq.com   2026-10-04
 */
#pragma once

//工程命令行也定义了这两个宏，加卫哨避免 C4005 宏重定义
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#ifndef _WINSOCK_DEPRECATED_NO_WARNINGS
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#endif

//目标平台：Windows 7 及以上，与 flamingoAdmin 相同。正式取值写在工程文件的预处理器定义里，这里只是兜底
#ifndef WINVER
#define WINVER          0x0601
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT    0x0601
#endif
#ifndef _WIN32_IE
#define _WIN32_IE       0x0601
#endif

#include <winsock2.h>
#include <ws2tcpip.h>

//WTL 裁剪开关：禁用 WTL 自带 CString / WTYPES，统一使用 ATL 版本
#define _WTL_NO_CSTRING
#define _WTL_NO_WTYPES

#include <atlbase.h>
#include <atlstr.h>
#include <atltypes.h>
#include <atlapp.h>

//balloonui 的 ATL 窗口类经全局 _Module 注册窗口类。本 DLL 有自己的一份，在 DllMain.cpp 定义并初始化，
//于是本 DLL 注册的窗口类归 DLL 所有，与控制台里同名的窗口类互不冲突
extern CAppModule _Module;

#include <atlwin.h>
#include <atlcrack.h>
#include <atlframe.h>
#include <atlctrls.h>
#include <atlmisc.h>

#include <memory>
#include <string>
#include <vector>
