/**
 *  DLL 日志桥接用的格式化函数：把一次 LOG_INFO / LOG_WARNING / LOG_ERROR 调用格式化成一行
 *  UTF-8 文本「[调用处函数签名:行号] 正文」，再由桥接（IULogBridge.cpp）经宿主写进 exe 的日志。
 *
 *  窄字符格式串按 UTF-8 原样输出（源文件以 /utf-8 编译）；宽字符格式串格式化后转成 UTF-8。
 *  超长时在上限处截断，并保证不会把一个多字节的 UTF-8 字符截成半个。
 *
 *  纯函数，不依赖宿主，便于单元测试（Tests/MediaLogFormatTests.cpp）。可在任意线程调用。
 *
 *  典型用法（见 IULogBridge.cpp）：
 *      char line[kMaxLogLineBytes];
 *      FormatLogLineA(line, sizeof(line), __FUNCSIG__, __LINE__, fmt, args);
 *
 *  balloonwj@qq.com   2026-10-02
 */
#pragma once

#include <stdarg.h>
#include <wchar.h>

namespace flamingomedia {

/**
 *  按窄字符（UTF-8）格式串格式化一行日志。
 *  @param out     [出参] 输出缓冲，结果总以 '\0' 结尾
 *  @param cap     out 的字节数（含结尾 '\0'），须大于 0
 *  @param funcSig 调用处的函数签名（一般为 __FUNCSIG__），为 NULL 时写 "?"
 *  @param line    调用处行号
 *  @param fmt     printf 风格的格式串（UTF-8），不可为 NULL
 *  @param args    格式串对应的参数
 *  @return 写入 out 的字节数（不含结尾 '\0'）；fmt 为 NULL 或 cap 不大于 0 时返回 -1，此时 out 不变
 */
int FormatLogLineA(char* out, int cap, const char* funcSig, int line, const char* fmt, va_list args);

/**
 *  按宽字符格式串格式化一行日志，正文转成 UTF-8 输出。参数与返回值同 FormatLogLineA，
 *  只是 fmt 为 UTF-16 格式串（如 _T("...") / L"..."）。
 */
int FormatLogLineW(char* out, int cap, const char* funcSig, int line, const wchar_t* fmt, va_list args);

} // namespace flamingomedia
