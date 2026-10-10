/**
 *  DLL 内部的日志出口：把格式化好的一行日志（已含 "[p2p] " 前缀）经宿主（IP2PHost::Log）写进 exe 的日志文件。
 *
 *  DLL 里的代码（包括以后编入的、与 exe 共用的源文件）一律照常使用客户端的 LOG_INFO / LOG_WARNING / LOG_ERROR
 *  宏（Source/Common/Log/IULog.h）。DLL 不链接 exe 的日志实现，而是由 HostLogBridge.cpp 在 DLL 内实现
 *  CIULog::Log，格式化后调用本文件的 P2PLogWriteLine。模块初始化时登记宿主，关闭时解除；未登记宿主期间写的
 *  日志直接丢弃。
 *
 *  前缀 "[p2p] " 让 P2P 的日志在 exe 的日志文件里一眼可辨、便于 grep，做法与文件共享的 "[share] " 相同。
 *
 *  可在任意线程调用。
 *
 *  典型用法（见 P2PModule.cpp）：
 *      P2PLogSetHost(host);     // Init 时
 *      LOG_INFO("FlamingoP2P initialized.");
 *      P2PLogSetHost(NULL);     // Shutdown 时
 *
 *  balloonwj@qq.com   2026-10-03
 */
#pragma once

#include "../FlamingoP2PApi.h"

namespace p2p {

//单行日志（含 "[p2p] " 前缀与调用处前缀）格式化后的最大字节数（含结尾 '\0'），超出部分截断。
//与 FlamingoMedia、FlamingoFileShare 的取值一致，三个 DLL 写出的行长度上限相同。
const int kP2PLogLineBytes = 2048;

//每行日志开头固定加的前缀
const char* const kP2PLogPrefix = "[p2p] ";

/**
 *  登记或解除宿主。模块 Init 时登记、Shutdown 时传 NULL 解除。
 *  与 P2PLogWriteLine 互斥：解除返回之后，不会再有任何线程经旧宿主写日志。
 *  @param host 宿主接口（不转移所有权）；传 NULL 表示解除，之后的日志被丢弃
 */
void P2PLogSetHost(IP2PHost* host);

/**
 *  把一行已经格式化好（已含前缀）的日志交给宿主写出。
 *  @param level 日志级别，取值见 enum P2PLogLevel
 *  @param text  日志行（UTF-8），以 '\0' 结尾；为 NULL 时忽略
 *  @return 已交给宿主返回 true；未登记宿主或 text 为 NULL 返回 false
 */
bool P2PLogWriteLine(int level, const char* text);

} // namespace p2p
