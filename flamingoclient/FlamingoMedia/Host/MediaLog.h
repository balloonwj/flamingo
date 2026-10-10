/**
 *  DLL 内部的日志出口：把格式化好的一行日志经宿主（IMediaHost::Log）写进 exe 的日志文件。
 *
 *  DLL 里的代码（包括与 exe 共用、从 exe 迁入的代码）一律照常使用客户端的 LOG_INFO /
 *  LOG_WARNING / LOG_ERROR 宏（Source/Common/Log/IULog.h）。DLL 不链接 exe 的日志实现，
 *  而是由 IULogBridge.cpp 在 DLL 内实现 CIULog::Log，格式化后调用本文件的 MediaLogWriteLine。
 *  模块初始化时登记宿主，关闭时解除；未登记宿主期间写的日志直接丢弃。
 *
 *  可在任意线程调用。
 *
 *  balloonwj@qq.com   2026-10-02（1b 起改为桥接后端，去掉 1a 临时的 MLOG_* 宏）
 */
#pragma once

#include "../Api/FlamingoMediaApi.h"

namespace flamingomedia {

//单行日志（含调用处前缀）格式化后的最大字节数（含结尾 '\0'），超出部分截断。
const int kMaxLogLineBytes = 2048;

/**
 *  登记或解除宿主。模块 Init 时登记、Shutdown 时传 NULL 解除。
 *  @param host 宿主接口（不转移所有权）；传 NULL 表示解除，之后的日志被丢弃
 */
void MediaLogSetHost(IMediaHost* host);

/**
 *  把一行已经格式化好的日志交给宿主写出。
 *  @param level 日志级别，取值见 enum MediaLogLevel
 *  @param text  日志行（UTF-8），以 '\0' 结尾；为 NULL 时忽略
 *  @return 已交给宿主返回 true；未登记宿主或 text 为 NULL 返回 false
 */
bool MediaLogWriteLine(int level, const char* text);

} // namespace flamingomedia
