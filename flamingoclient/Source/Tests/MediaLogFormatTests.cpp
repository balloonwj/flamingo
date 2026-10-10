/**
 *  FlamingoMedia.dll 日志桥接格式化函数单元测试（Suite MLF，标签 [mediadll]），MediaLogFormatTests.cpp
 *  覆盖（对应 docs/屏幕共享与录制方案.md 第 1b 步，2026-10-02 与用户确认的用例 LB1~LB4）：
 *    - LB1 窄字符格式串：输出「[调用处:行号] 正文」；
 *    - LB2 宽字符格式串、参数含中文：正确转成 UTF-8；
 *    - LB3 超长日志：在上限处截断，以 '\0' 结尾，不把多字节字符截成半个；
 *    - LB4 参数里含 %：原样输出，不被当成格式符；
 *    - 非法参数：格式串为 NULL、缓冲容量不大于 0 时返回 -1。
 *  被测函数是纯函数（FlamingoMedia/Host/LogFormat.cpp），本工程直接编译它，不加载 DLL。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-02
 */
#include "catch_amalgamated.hpp"

#include <windows.h>
#include <string.h>
#include <string>

#include "../../FlamingoMedia/Host/LogFormat.h"

using namespace flamingomedia;

namespace {

//测试用输出缓冲的字节数（足够容纳 LB1 / LB2 / LB4 的完整结果）
static const int kBigCap = 512;

//LB3 使用的小容量缓冲（含结尾 '\0'），用来触发截断
static const int kSmallCap = 32;

//LB3 中重复拼接的中文字数，使正文远超 kSmallCap
static const int kLongRepeat = 40;

//变参包装：窄字符
static int CallA(char* out, int cap, const char* sig, int line, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    int n = FormatLogLineA(out, cap, sig, line, fmt, args);
    va_end(args);
    return n;
}

//变参包装：宽字符
static int CallW(char* out, int cap, const char* sig, int line, const wchar_t* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    int n = FormatLogLineW(out, cap, sig, line, fmt, args);
    va_end(args);
    return n;
}

//判断一段字节是否为合法的 UTF-8（借助系统转换函数的严格模式）
static bool IsValidUtf8(const char* s, int len)
{
    if (len == 0)
    {
        return true;
    }
    return ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, len, NULL, 0) > 0;
}

} // namespace

TEST_CASE("MLF-LB1 窄字符格式串输出调用处与正文", "[mediadll]")
{
    char out[kBigCap];
    int n = CallA(out, kBigCap, "void __cdecl Foo(void)", 12, "x=%d, s=%s", 5, "ok");
    CHECK(std::string(out) == "[void __cdecl Foo(void):12] x=5, s=ok");
    CHECK(n == static_cast<int>(strlen(out)));
}

TEST_CASE("MLF-LB2 宽字符格式串与中文参数转成 UTF-8", "[mediadll]")
{
    char out[kBigCap];
    int n = CallW(out, kBigCap, "Bar", 3, L"name=%s, n=%d", L"\x5f20\x4e09", 7);
    //期望正文：name=张三, n=7（张 = E5 BC A0，三 = E4 B8 89）
    const char expect[] = "[Bar:3] name=\xe5\xbc\xa0\xe4\xb8\x89, n=7";
    CHECK(std::string(out) == std::string(expect));
    CHECK(n == static_cast<int>(strlen(expect)));
    CHECK(IsValidUtf8(out, n));

    //调用处为 NULL 时写 "?"
    CallA(out, kBigCap, NULL, 9, "%s", "z");
    CHECK(std::string(out) == "[?:9] z");
}

TEST_CASE("MLF-LB3 超长日志截断且不截断多字节字符", "[mediadll]")
{
    std::wstring longText;
    for (int i = 0; i < kLongRepeat; ++i)
    {
        longText += L"\x4e2d";   //「中」，UTF-8 下占 3 字节
    }
    char out[kSmallCap];
    memset(out, 'X', sizeof(out));
    int n = CallW(out, kSmallCap, "F", 1, L"%s", longText.c_str());
    REQUIRE(n >= 0);
    CHECK(n <= kSmallCap - 1);
    CHECK(out[n] == '\0');
    CHECK(static_cast<int>(strlen(out)) == n);
    CHECK(IsValidUtf8(out, n));

    //窄字符同样截断并以 '\0' 结尾
    std::string longAscii(kSmallCap * 4, 'a');
    memset(out, 'X', sizeof(out));
    n = CallA(out, kSmallCap, "F", 1, "%s", longAscii.c_str());
    REQUIRE(n >= 0);
    CHECK(n == kSmallCap - 1);
    CHECK(out[n] == '\0');
}

TEST_CASE("MLF-LB4 参数中的百分号原样输出", "[mediadll]")
{
    char out[kBigCap];
    CallA(out, kBigCap, "G", 2, "%s", "100%done %d %s");
    CHECK(std::string(out) == "[G:2] 100%done %d %s");
    CallW(out, kBigCap, "G", 2, L"%s", L"50% %x");
    CHECK(std::string(out) == "[G:2] 50% %x");
}

TEST_CASE("MLF-LB5 非法参数返回 -1 且不改动输出", "[mediadll]")
{
    char out[kBigCap];
    strcpy_s(out, "untouched");
    CHECK(CallA(out, kBigCap, "H", 1, NULL) == -1);
    CHECK(std::string(out) == "untouched");
    CHECK(CallW(out, kBigCap, "H", 1, NULL) == -1);
    CHECK(std::string(out) == "untouched");
    CHECK(CallA(out, 0, "H", 1, "%d", 1) == -1);
    CHECK(std::string(out) == "untouched");
}
