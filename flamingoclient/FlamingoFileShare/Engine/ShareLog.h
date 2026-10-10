/**
 *  DLL 内部的日志出口：把格式化好的一行日志加上 "[share] " 前缀，经宿主（IFileShareHost::Log）
 *  写进 exe 的日志文件。
 *
 *  DLL 里的代码（包括与 exe 共用的源文件）一律照常使用客户端的 LOG_INFO / LOG_WARNING / LOG_ERROR
 *  宏（Source/Common/Log/IULog.h）。DLL 不链接 exe 的日志实现，而是由 HostLogBridge.cpp 在 DLL 内
 *  实现 CIULog::Log，格式化后调用本文件的 ShareLogWriteLine。模块初始化时登记宿主，关闭时解除；
 *  未登记宿主期间写的日志直接丢弃。
 *
 *  前缀 "[share] " 让文件共享的日志在 exe 的日志文件里一眼可辨、便于 grep（docs/文件共享方案.md 2.6）。
 *
 *  可在任意线程调用。
 *
 *  典型用法（见 FileShareModule.cpp）：
 *      ShareLogSetHost(host);     // Init 时
 *      LOG_INFO("FlamingoFileShare initialized.");
 *      ShareLogSetHost(NULL);     // Shutdown 时
 *
 *  balloonwj@qq.com   2026-10-02
 */
#pragma once

#include "../FileShareApi.h"

namespace fileshare {

//单行日志（含 "[share] " 前缀与调用处前缀）格式化后的最大字节数（含结尾 '\0'），超出部分截断。
//与 FlamingoMedia 的 kMaxLogLineBytes 取值一致，两个 DLL 写出的行长度上限相同。
const int kShareLogLineBytes = 2048;

//每行日志开头固定加的前缀
const char* const kShareLogPrefix = "[share] ";

/**
 *  登记或解除宿主。模块 Init 时登记、Shutdown 时传 NULL 解除。
 *  与 ShareLogWriteLine 互斥：解除返回之后，不会再有任何线程经旧宿主写日志。
 *  @param host 宿主接口（不转移所有权）；传 NULL 表示解除，之后的日志被丢弃
 */
void ShareLogSetHost(IFileShareHost* host);

/**
 *  把一行已经格式化好（已含前缀）的日志交给宿主写出。
 *  @param level 日志级别，取值见 enum FileShareLogLevel
 *  @param text  日志行（UTF-8），以 '\0' 结尾；为 NULL 时忽略
 *  @return 已交给宿主返回 true；未登记宿主或 text 为 NULL 返回 false
 */
bool ShareLogWriteLine(int level, const char* text);

} // namespace fileshare
