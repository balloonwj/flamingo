/**
 *  flamingoAdmin —— 崩溃捕获。
 *
 *  安装进程级「未处理异常过滤器」：一旦崩溃，把异常码、出错地址与符号化的
 *  调用栈写入日志文件，便于事后定位。须在 WinMain 早期、Logger::Init()
 *  之后调用一次。
 *
 *  balloonwj@qq.com   2026.05.21
 */
#pragma once

namespace flamingoadmin {

// 安装未处理异常过滤器。仅需调用一次。
void InstallCrashHandler();

} // namespace flamingoadmin
