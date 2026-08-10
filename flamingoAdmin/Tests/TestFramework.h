/**
 *  flamingoAdmin —— 自建轻量测试框架。
 *
 *  按开发约定文档测试规范，flamingoAdmin 不引入外部测试框架（与 balloonui 的
 *  Tests/ 同样不依赖 GoogleTest / Catch2），只用一组极简断言宏 + 通过/失败
 *  计数。断言失败不中断，继续执行后续用例，便于一次性看到全部失败。
 *
 *  断言宏一律以 FA_EXPECT_ 开头，用法见下方各宏注释。
 *
 *  balloonwj@qq.com   2026.05.21
 */
#pragma once

#include <atlstr.h>

namespace flamingoadmin { namespace test {

// 断言统计：累计通过 / 失败的断言数。整个测试进程共一份。
struct TestStats
{
    int m_passed;  // 通过的断言数
    int m_failed;  // 失败的断言数
};

// 取全局断言统计实例。
TestStats& Stats();

// 打印一个测试用例的标题，便于在输出里分组。
//   name：用例名（窄字符串）。
void BeginCase(const char* name);

// 向控制台打印一行 UTF-8 文本。
//   text：待打印的文本。
void PrintLine(const CString& text);

// 记录一次断言结果。由 FA_EXPECT_* 宏调用，业务测试代码不直接调用。
//   ok：断言是否通过。
//   expr：断言表达式文本（窄字符串，来自宏的 #cond）。
//   detail：失败时的补充说明（期望值 vs 实际值）；通过时忽略。
//   file / line：断言所在源码位置。
void Report(bool ok, const char* expr, const CString& detail,
            const char* file, int line);

}} // namespace flamingoadmin::test

// ---- 断言宏。失败不中断，继续执行 ----

// 断言 cond 为真。
#define FA_EXPECT_TRUE(cond) \
    flamingoadmin::test::Report((cond), #cond, _T("期望为真"), __FILE__, __LINE__)

// 断言 cond 为假。
#define FA_EXPECT_FALSE(cond) \
    flamingoadmin::test::Report(!(cond), #cond, _T("期望为假"), __FILE__, __LINE__)

// 断言两个整型（或可转 long long 的值，如 size_t / 枚举）相等。
#define FA_EXPECT_EQ(actual, expected) \
    do { \
        long long faActual__   = static_cast<long long>(actual); \
        long long faExpected__ = static_cast<long long>(expected); \
        CString faDetail__; \
        faDetail__.Format(_T("期望 %lld，实际 %lld"), faExpected__, faActual__); \
        flamingoadmin::test::Report(faActual__ == faExpected__, \
            #actual " == " #expected, faDetail__, __FILE__, __LINE__); \
    } while (0)

// 断言两个字符串（CString / LPCTSTR）相等。
#define FA_EXPECT_STREQ(actual, expected) \
    do { \
        CString faActual__(actual); \
        CString faExpected__(expected); \
        CString faDetail__; \
        faDetail__.Format(_T("期望 \"%s\"，实际 \"%s\""), \
            faExpected__.GetString(), faActual__.GetString()); \
        flamingoadmin::test::Report(faActual__ == faExpected__, \
            #actual " STREQ " #expected, faDetail__, __FILE__, __LINE__); \
    } while (0)

// 断言指针为空。
#define FA_EXPECT_NULL(ptr) \
    flamingoadmin::test::Report((ptr) == nullptr, #ptr " == nullptr", \
        _T("期望空指针"), __FILE__, __LINE__)

// 断言指针非空。
#define FA_EXPECT_NOTNULL(ptr) \
    flamingoadmin::test::Report((ptr) != nullptr, #ptr " != nullptr", \
        _T("期望非空指针"), __FILE__, __LINE__)
