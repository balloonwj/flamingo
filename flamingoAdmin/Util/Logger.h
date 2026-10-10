/**
 *  flamingoAdmin —— 轻量文件日志器。
 *
 *  开发约定文档「测试规范」要求关键路径（登录、屏切换、数据读写等）必须打日志。
 *  日志大多在 UI 线程产生；FlamingoLink.dll 的连接线程也经宿主接口写日志
 *  （2026-10-04 起），所以写文件那一段加了锁，任意线程都可以调用。
 *
 *  业务代码统一通过 FA_LOGI / FA_LOGW / FA_LOGE 三个宏打日志，不要直接调用
 *  Logger 类的方法。
 *
 *  balloonwj@qq.com   2026.05.21
 */
#pragma once

#include <atlstr.h>

namespace flamingoadmin {

// 日志级别。决定行首标记，便于在日志文件中按级别过滤。
enum class LogLevel
{
    Info,   // 常规信息：连接、屏切换、数据变更成功等
    Warn,   // 警告：可恢复的异常、降级路径
    Error,  // 错误：操作失败、资源缺失
};

// 文件日志器。日志写入 exe 同目录下的 flamingoAdmin.log（UTF-8、追加模式）。
class Logger
{
public:
    // 初始化日志器：在日志文件追加一行启动分隔。须在 WinMain 早期调用一次。
    static void Init();

    // 写一条日志。
    //   level：日志级别，见 LogLevel。
    //   format / ...：printf 风格的宽字符格式串与可变参数；单条上限约 1KB，
    //                 超出部分被截断。
    static void Writef(LogLevel level, LPCTSTR format, ...);

private:
    // 解析日志文件绝对路径，返回 exe 同目录下的 flamingoAdmin.log。
    static CString ResolveLogPath();
};

} // namespace flamingoadmin

// 关键路径日志宏。第一个参数为格式串，其后为可变参数。
#define FA_LOGI(...) flamingoadmin::Logger::Writef(flamingoadmin::LogLevel::Info,  __VA_ARGS__)
#define FA_LOGW(...) flamingoadmin::Logger::Writef(flamingoadmin::LogLevel::Warn,  __VA_ARGS__)
#define FA_LOGE(...) flamingoadmin::Logger::Writef(flamingoadmin::LogLevel::Error, __VA_ARGS__)
