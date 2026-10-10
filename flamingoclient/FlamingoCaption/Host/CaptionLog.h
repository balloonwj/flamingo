/**
 *  FlamingoCaption.dll 的日志出口，CaptionLog.h
 *
 *  DLL 里的代码用 LOG_INFO / LOG_WARNING / LOG_ERROR 宏写日志（展开为 CIULog::Log，见 Host/IULogBridge.cpp），
 *  格式化后经本文件交给宿主 ICaptionHost::Log，由 FlamingoMedia 再转给 exe，最终写进 exe 的同一个日志文件。
 *  模块初始化时调用 CaptionLogSetHost 设置宿主，关闭时设为 NULL；宿主为 NULL 时日志直接丢弃。
 *
 *  典型用法：
 *      caption::CaptionLogSetHost(host);
 *      LOG_INFO("caption: engine loaded in %d ms.", ms);
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include "../CaptionApi.h"

namespace caption {

//单行日志（含调用处前缀）格式化后的最大字节数（含结尾 '\0'），超出部分截断
const int kMaxLogLineBytes = 2048;

/**
 *  设置日志宿主（模块 Init 时设置，Shutdown 时设为 NULL）。任意线程可调用。
 *  @param host 宿主接口（不拥有）；NULL 表示之后的日志直接丢弃
 */
void CaptionLogSetHost(ICaptionHost* host);

/**
 *  写一行日志（任意线程）。
 *  @param level 级别，取值见 CaptionLogLevel
 *  @param text  UTF-8 文字，只在本次调用期间有效
 *  @return 已交给宿主返回 true；宿主为 NULL 时返回 false
 */
bool CaptionLogWriteLine(int level, const char* text);

} // namespace caption
