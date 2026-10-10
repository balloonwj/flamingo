/**
 *  FlamingoSecretChat.dll 工程的公共前置头文件。
 *
 *  与 exe 共用的源文件开头写着 #include "stdafx.h"；编进本 DLL 时工程把本目录放进包含路径，找到的是本文件。
 *  本 DLL 没有界面，不链接 balloonui，所以只包含 Windows 头文件与常用的标准库头文件，不引入 ATL / WTL。
 *  本工程不使用预编译头，本文件只是普通头文件。
 *
 *  balloonwj@qq.com   2026-10-07
 */
#pragma once

//工程命令行也定义了这个宏，加卫哨避免 C4005 宏重定义
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

//目标系统下限与 exe 相同（Windows 7）：Win10 才有的系统能力（WDA_EXCLUDEFROMCAPTURE）一律在运行时探测后再用
#define WINVER          0x0601
#define _WIN32_WINNT    0x0601

#include <windows.h>

#include <stdint.h>
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <mutex>
