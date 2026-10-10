/**
 *  FlamingoOcr.dll 的日志出口实现，用法见 OcrHostLog.h。
 *
 *  balloonwj@qq.com   2026-10-05
 */
#include "OcrHostLog.h"

#include <stdarg.h>
#include <stdio.h>

namespace flamingoocr {

namespace {

//单条日志的缓冲区大小（字节），超出部分截断
const size_t kLogBufferBytes = 1024;

//宿主提供的日志回调；m_init 时设置，之后只读。指针大小的读写是原子的，跨线程读取无需加锁
OcrLogFn volatile g_logSink = NULL;

//把可变参数格式化后交给回调
void WriteV(int level, const char* fmt, va_list args)
{
    OcrLogFn sink = g_logSink;
    if (sink == NULL || fmt == NULL)
        return;

    char buffer[kLogBufferBytes];
    int written = _vsnprintf_s(buffer, sizeof(buffer), _TRUNCATE, fmt, args);
    if (written < 0)
        buffer[sizeof(buffer) - 1] = '\0';   //被截断时 _vsnprintf_s 返回 -1，缓冲区已以 0 结尾
    sink(level, buffer);
}

} // namespace

void SetOcrLogSink(OcrLogFn sink)
{
    g_logSink = sink;
}

void OcrLog(int level, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    WriteV(level, fmt, args);
    va_end(args);
}

void OcrLogInfo(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    WriteV(ocr_log_info, fmt, args);
    va_end(args);
}

void OcrLogWarning(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    WriteV(ocr_log_warning, fmt, args);
    va_end(args);
}

void OcrLogError(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    WriteV(ocr_log_error, fmt, args);
    va_end(args);
}

} // namespace flamingoocr
