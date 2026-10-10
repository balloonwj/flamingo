/**
 *  FlamingoMarkdown.dll 内部的日志出口：把格式化好的英文日志交给宿主在 m_init 时传入的回调
 *  （MdHostInfo::m_log），由宿主写进客户端日志。宿主没有给回调时日志被丢弃。
 *
 *  典型用法：
 *
 *      flamingomd::MdLogWarning("parse: nesting depth over %d, extra levels flattened.", kMaxDepth);
 *
 *  日志正文一律为 ASCII 英文，<u>不得</u>包含消息正文（聊天内容可能敏感）。
 *  所有函数可在任意线程调用；回调只在 m_init 时设置一次。
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include "../FlamingoMarkdownApi.h"

namespace flamingomd {

/**
 *  设置日志回调。m_init 时调用；传 NULL 表示此后不写日志。
 *  @param sink 宿主提供的回调，可为 NULL
 */
void SetMdLogSink(MdLogFn sink);

/**
 *  按级别格式化并写一条日志（printf 风格，正文超过内部缓冲区时截断）。
 *  @param level 日志级别，取值见 enum MdLogLevel
 *  @param fmt   格式串，ASCII 英文，不可为 NULL
 */
void MdLog(int level, const char* fmt, ...);

//写一条普通信息日志，参数同 printf
void MdLogInfo(const char* fmt, ...);

//写一条警告日志，参数同 printf
void MdLogWarning(const char* fmt, ...);

//写一条错误日志，参数同 printf
void MdLogError(const char* fmt, ...);

} // namespace flamingomd
