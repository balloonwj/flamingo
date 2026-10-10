/**
 *  DLL 日志桥接格式化函数的实现，见 LogFormat.h。
 *  balloonwj@qq.com   2026-10-02
 */
#include "LogFormat.h"

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

namespace flamingomedia {

//格式化正文的中间缓冲上限（字符数）。正文先在这里格式化，再与前缀拼接并按输出容量截断
static const int kMaxBodyChars = 4096;

//行号格式化缓冲的字节数（int 的十进制表示最多 11 个字符）
static const int kLineNoChars = 16;

//UTF-8 后续字节的掩码与取值：形如 10xxxxxx 的字节不是一个字符的起始字节
static const unsigned char kUtf8ContinuationMask  = 0xC0;
static const unsigned char kUtf8ContinuationValue = 0x80;

//返回 text 截到不超过 maxBytes 字节后的长度，保证不把一个多字节 UTF-8 字符截成半个
static size_t Utf8SafeLength(const char* text, size_t len, size_t maxBytes)
{
    if (len <= maxBytes)
    {
        return len;
    }
    size_t n = maxBytes;
    //text[n] 是第一个放不下的字节；若它是后续字节，说明当前字符跨过了边界，退回到该字符的起始字节
    while (n > 0 && (static_cast<unsigned char>(text[n]) & kUtf8ContinuationMask) == kUtf8ContinuationValue)
    {
        --n;
    }
    return n;
}

//把「[调用处:行号] 」前缀与正文拼接后写进 out（按 cap 截断并以 '\0' 结尾），返回写入的字节数
static int Assemble(char* out, int cap, const char* funcSig, int line, const char* body, size_t bodyLen)
{
    char lineNo[kLineNoChars];
    sprintf_s(lineNo, "%d", line);
    std::string full;
    full += '[';
    full += (funcSig != NULL) ? funcSig : "?";
    full += ':';
    full += lineNo;
    full += "] ";
    full.append(body, bodyLen);
    size_t n = Utf8SafeLength(full.c_str(), full.size(), static_cast<size_t>(cap - 1));
    memcpy(out, full.c_str(), n);
    out[n] = '\0';
    return static_cast<int>(n);
}

int FormatLogLineA(char* out, int cap, const char* funcSig, int line, const char* fmt, va_list args)
{
    if (out == NULL || fmt == NULL || cap <= 0)
    {
        return -1;
    }
    std::vector<char> body(kMaxBodyChars);
    int n = _vsnprintf_s(&body[0], body.size(), _TRUNCATE, fmt, args);
    //n < 0 表示正文超过中间缓冲被截断；_TRUNCATE 模式下缓冲仍以 '\0' 结尾，按实际长度取，并避开半个字符
    size_t len = (n >= 0) ? static_cast<size_t>(n) : strlen(&body[0]);
    if (n < 0)
    {
        len = Utf8SafeLength(&body[0], len, len);
    }
    return Assemble(out, cap, funcSig, line, &body[0], len);
}

int FormatLogLineW(char* out, int cap, const char* funcSig, int line, const wchar_t* fmt, va_list args)
{
    if (out == NULL || fmt == NULL || cap <= 0)
    {
        return -1;
    }
    std::vector<wchar_t> wbody(kMaxBodyChars);
    int wn = _vsnwprintf_s(&wbody[0], wbody.size(), _TRUNCATE, fmt, args);
    int wlen = (wn >= 0) ? wn : static_cast<int>(wcslen(&wbody[0]));
    std::string body;
    if (wlen > 0)
    {
        int bytes = ::WideCharToMultiByte(CP_UTF8, 0, &wbody[0], wlen, NULL, 0, NULL, NULL);
        if (bytes > 0)
        {
            body.resize(static_cast<size_t>(bytes));
            ::WideCharToMultiByte(CP_UTF8, 0, &wbody[0], wlen, &body[0], bytes, NULL, NULL);
        }
    }
    return Assemble(out, cap, funcSig, line, body.c_str(), body.size());
}

} // namespace flamingomedia
