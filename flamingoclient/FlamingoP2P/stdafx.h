/**
 *  FlamingoP2P.dll 工程的公共前置头文件。
 *
 *  与 exe 共用的源文件开头都写着 #include "stdafx.h"。在 exe 里它对应 Source/stdafx.h（预编译头）；编进本 DLL 时，
 *  工程把本目录放进包含路径，于是找到的是本文件。本 DLL 没有界面，不链接 balloonui，所以只包含 Winsock 与
 *  Windows 头文件和常用的标准库头文件，不引入 ATL / WTL。以后编入的共用源文件若用到 ATL，再在这里补上，
 *  并与 Source/stdafx.h 保持同一套系统版本宏。
 *
 *  本工程不使用预编译头，本文件只是普通头文件。
 *
 *  balloonwj@qq.com   2026-10-03
 */
#pragma once

//工程命令行也定义了这两个宏，加卫哨避免 C4005 宏重定义
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#ifndef _WINSOCK_DEPRECATED_NO_WARNINGS
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#endif

//目标系统下限与 exe 相同（Windows 7）：DLL 必须能在 Win7 上载入，Win10 才有的系统接口（如 UDP 发送分段卸载）
//一律在运行时探测后再用
#define WINVER          0x0601
#define _WIN32_WINNT    0x0601

//winsock2.h 必须在 windows.h 之前包含，否则 windows.h 带进来的旧版 winsock.h 会与它冲突
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <stdint.h>
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <mutex>
#include <algorithm>
