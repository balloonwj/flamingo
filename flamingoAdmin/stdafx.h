/**
 *  flamingoAdmin —— 公共预编译头 / 依赖汇总。
 *
 *  集中 include ATL / WTL 与常用标准库头，供全工程各翻译单元统一引入。
 *  与同仓 balloonui Demo（DemoTaskManager/stdafx.h）保持一致的目标平台与
 *  WTL 裁剪开关，确保链接 balloonui 时 ABI 一致。
 *
 *  balloonwj@qq.com   2026.05.21
 */
#pragma once

// ---- 目标平台版本：Windows 7 及以上（与 balloonui 运行平台要求一致）----
// _CRT_SECURE_NO_WARNINGS 在 vcxproj 的预处理器定义里已给出，这里加 #ifndef
// 守卫，避免「宏重定义」warning C4005。
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

// 下面三个平台版本宏的正式取值写在两个工程文件（flamingoAdmin.vcxproj
// 与 Tests/flamingoAdminTests.vcxproj）的预处理器定义里，改目标版本请改那里。
// 之所以不只写在本文件：个别源文件必须在 stdafx.h 之前引入 Windows SDK
// 的头文件（例如 Util/NetProbe.cpp 要让 winsock2.h 排在 windows.h 之前），
// 而 SDK 的 sdkddkver.h 会为尚未定义的平台版本宏填上默认值。若只在本文件
// 定义，那些源文件里就会先拿到 SDK 默认值、再被本文件改写，既报
// warning C4005，又让同一个翻译单元内前后两段代码看到不同的 _WIN32_IE
// （SDK 默认值是 0x0800，本工程要的是 0x0601），埋下结构体定义不一致的隐患。
// 这里保留带 #ifndef 守卫的定义，是为了本文件脱离工程单独编译时仍有取值。
#ifndef WINVER
#define WINVER          0x0601      // 0x0601 = Windows 7
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT    0x0601      // 0x0601 = Windows 7
#endif
#ifndef _WIN32_IE
#define _WIN32_IE       0x0601      // 0x0601 = IE 6.0 SP1 对应的 comctl32 特性集
#endif

// ---- WTL 裁剪开关：禁用 WTL 自带 CString / WTYPES，统一使用 ATL 版本，----
// ---- 避免与 atlstr.h 的 CString 冲突。                                ----
#define _WTL_NO_CSTRING
#define _WTL_NO_WTYPES

#include <atlbase.h>
#include <atlstr.h>
#include <atltypes.h>
#include <atlapp.h>
extern CAppModule _Module;          // 定义在 main.cpp，ATL/WTL 要求的全局模块单例
#include <atlwin.h>
#include <atlcrack.h>
#include <atlframe.h>
#include <atlctrls.h>
#include <atlmisc.h>

#include <memory>
#include <string>
#include <vector>
