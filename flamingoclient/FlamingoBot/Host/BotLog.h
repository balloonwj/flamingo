/**
 *  FlamingoBot.dll 的日志出口：保存宿主接口，把格式化好的一行日志交给宿主写进 exe 的日志文件。
 *
 *  DLL 里的代码照常使用 LOG_INFO / LOG_WARNING / LOG_ERROR 宏（Source/Common/Log/IULog.h），宏展开为
 *  CIULog::Log，由同目录的 HostLogBridge.cpp 实现：格式化成「[bot] [调用处:行号] 正文」后调用本文件的
 *  BotLogWriteLine。宿主接口在 IBotModule::Init 时登记、Shutdown 时清除；未登记时日志直接丢弃。
 *
 *  balloonwj@qq.com   2026-10-08
 */
#pragma once

#include "../BotApi.h"

namespace flamingobot {

//单行日志（含前缀）格式化后的最大字节数（含结尾 '\0'），与其它插件 DLL 的取值一致
const int kBotLogLineBytes = 2048;

//每行日志开头固定加的前缀
const char* const kBotLogPrefix = "[bot] ";

/**
 *  登记或清除宿主接口。线程安全。
 *  @param host 宿主接口（借用）；传 NULL 表示清除，之后的日志被丢弃
 */
void BotLogSetHost(IBotHost* host);

/**
 *  把一行已格式化的日志交给宿主。线程安全。
 *  @param level 日志级别，取 BotLogLevel
 *  @param text  UTF-8 文本，以 '\0' 结尾
 *  @return 已交给宿主返回 true；宿主未登记或 text 为 NULL 时返回 false
 */
bool BotLogWriteLine(int level, const char* text);

} // namespace flamingobot
