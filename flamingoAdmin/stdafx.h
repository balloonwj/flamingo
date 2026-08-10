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
#define WINVER          0x0601      // 0x0601 = Windows 7
#define _WIN32_WINNT    0x0601      // 0x0601 = Windows 7
#define _WIN32_IE       0x0601      // 0x0601 = IE 7 对应的 comctl32 v6 特性集

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
