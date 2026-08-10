/**
 *  flamingoAdmin —— 自建轻量测试框架实现。
 *
 *  balloonwj@qq.com   2026.05.21
 */
#include "stdafx.h"
#include "Tests/TestFramework.h"

#include <cstdio>

namespace flamingoadmin { namespace test {

namespace {

// 把宽字符串转 UTF-8 字节并写到 stdout。控制台输出代码页已在 TestMain 中
// 设为 UTF-8，故中文可正常显示。
void WriteUtf8(const CString& text)
{
    int len = ::WideCharToMultiByte(CP_UTF8, 0, text, -1,
                                    nullptr, 0, nullptr, nullptr);
    if (len <= 1)
    {
        return;
    }
    std::vector<char> buf(static_cast<size_t>(len));
    ::WideCharToMultiByte(CP_UTF8, 0, text, -1,
                          buf.data(), len, nullptr, nullptr);
    // len 含结尾 '\0'，写文件时不写它。
    fwrite(buf.data(), 1, static_cast<size_t>(len - 1), stdout);
}

} // anonymous namespace

TestStats& Stats()
{
    static TestStats s_stats = { 0, 0 };
    return s_stats;
}

void PrintLine(const CString& text)
{
    WriteUtf8(text);
}

void BeginCase(const char* name)
{
    // name 是 UTF-8 窄字符串（源码以 /utf-8 编译，窄字面量里是 UTF-8 字节）。
    // 必须按 CP_UTF8 解码成宽串——不能用 CString 的 %hs，那会按 ANSI 代码页
    // 解释字节，中文会乱码。
    CString wideName;
    int wlen = ::MultiByteToWideChar(CP_UTF8, 0, name, -1, nullptr, 0);
    if (wlen > 0)
    {
        ::MultiByteToWideChar(CP_UTF8, 0, name, -1,
                              wideName.GetBuffer(wlen), wlen);
        wideName.ReleaseBuffer();
    }

    CString line;
    line.Format(_T("[CASE] %s\n"), wideName.GetString());
    WriteUtf8(line);
}

void Report(bool ok, const char* expr, const CString& detail,
            const char* file, int line)
{
    if (ok)
    {
        ++Stats().m_passed;
        return;
    }

    ++Stats().m_failed;

    // %hs：在宽格式串里插入窄字符串（expr / file 均为 const char*）。
    CString msg;
    msg.Format(_T("  [FAIL] %hs:%d  %hs  (%s)\n"),
               file, line, expr, detail.GetString());
    WriteUtf8(msg);
}

}} // namespace flamingoadmin::test
