/**
 *  FlamingoLink.dll —— 日志转发：DLL 不自己写日志文件，格式化之后经 ILinkHost::Log 交给控制台的日志器。
 *
 *  可在任意线程调用（连接线程也会调用）。宿主接口由 LinkModule::Init 登记、Shutdown 撤销；
 *  未登记时日志直接丢弃。日志内容一律英文（开发约定文档代码规范第 15 条）。
 *
 *  典型用法：
 *      LINK_LOG_INFO("connected to %s:%d", host.c_str(), port);
 *
 *  balloonwj@qq.com   2026-10-04
 */
#pragma once

namespace flamingolink {

class ILinkHost;

/**
 *  登记或撤销宿主接口。只在界面线程上、连接线程未运行时调用。
 *  @param host 宿主接口；传 NULL 表示撤销
 */
void SetLogHost(ILinkHost* host);

/**
 *  格式化一行日志并交给宿主。
 *  @param level 日志级别，取值见 FlamingoLinkApi.h 的 enum LinkLogLevel
 *  @param fmt   printf 风格的格式串（英文）
 */
void LinkLogf(int level, const char* fmt, ...);

} // namespace flamingolink

#define LINK_LOG_INFO(...)    ::flamingolink::LinkLogf(0, __VA_ARGS__)
#define LINK_LOG_WARNING(...) ::flamingolink::LinkLogf(1, __VA_ARGS__)
#define LINK_LOG_ERROR(...)   ::flamingolink::LinkLogf(2, __VA_ARGS__)
