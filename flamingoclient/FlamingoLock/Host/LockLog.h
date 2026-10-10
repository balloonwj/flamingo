/**
 *  FlamingoLock.dll 的日志出口：保存宿主接口，把格式化好的一行日志交给宿主写进 exe 的日志文件。
 *
 *  DLL 里的代码照常使用 LOG_INFO / LOG_WARNING / LOG_ERROR 宏（Source/Common/Log/IULog.h），宏展开为
 *  CIULog::Log，由同目录的 HostLogBridge.cpp 实现：格式化成「[lock] [调用处:行号] 正文」后调用本文件的
 *  LockLogWriteLine。宿主接口在 ILockModule::Init 时登记、Shutdown 时清除；未登记时日志直接丢弃。
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include "../FlamingoLockApi.h"

namespace flamingolock {

//单行日志（含 "[lock] " 前缀与调用处前缀）格式化后的最大字节数（含结尾 '\0'），超出部分截断。
//与 FlamingoP2P、FlamingoMedia 的取值一致，各 DLL 写出的行长度上限相同。
const int kLockLogLineBytes = 2048;

//每行日志开头固定加的前缀
const char* const kLockLogPrefix = "[lock] ";

/**
 *  登记或清除宿主接口。线程安全。
 *  @param host 宿主接口（借用）；传 NULL 表示清除，之后的日志被丢弃
 */
void LockLogSetHost(ILockHost* host);

/**
 *  把一行已格式化的日志交给宿主。线程安全，可在锁屏线程上调用。
 *  @param level 日志级别，取 LockLogLevel
 *  @param text  UTF-8 文本，以 '\0' 结尾
 *  @return 已交给宿主返回 true；宿主未登记或 text 为 NULL 时返回 false
 */
bool LockLogWriteLine(int level, const char* text);

} // namespace flamingolock
