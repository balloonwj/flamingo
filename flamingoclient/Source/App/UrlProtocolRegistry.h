/**
 *  UrlProtocolRegistry.h
 *
 *  把 flamingo:// 注册为当前用户的 Windows 协议（docs/邀请入群方案.md §12）：写 HKCU\Software\Classes\flamingo，
 *  浏览器、文档或别的程序里点开 flamingo://joingroup?code=... 时，系统用「"<exe>" "%1"」启动本程序，
 *  链接作为命令行参数传进来。只写当前用户的注册表，不需要管理员权限。
 *
 *  每次启动调用一次：已注册且命令行与当前 exe 一致时不重复写；exe 换了位置（升级、改目录）时覆盖为当前路径。
 *
 *  典型用法（进程入口，日志初始化之后）：
 *      apputil::RegisterFlamingoUrlProtocol(exePath);
 *
 *  balloonwj@qq.com   2026-10-07
 */
#pragma once

#include <string>

namespace apputil {

/**
 *  注册 flamingo:// 协议到当前 exe。
 *    exePath：当前 exe 的完整路径。
 *  @return 已是当前路径或写入成功返回 true；注册表写入失败返回 false（只记日志，不影响启动）
 */
bool RegisterFlamingoUrlProtocol(const std::wstring& exePath);

} // namespace apputil
