/**
 *  LogCleanupPolicyTests.cpp
 *
 *  「启动时清空历史日志」开关的取值规则单元测试（Suite LRP）。
 *
 *  这个开关有两个使用方（启动流程与系统设置对话框的存储页），而它的默认值又跟
 *  构建配置有关（Debug 关、Release 开），最容易出错的正是"配置项不存在 / 被写坏
 *  时到底取什么"。本组用例把这些边界钉住。
 *
 *  覆盖用例：
 *    LRP1 配置项不存在时取本构建的默认值；
 *    LRP2 显式写 0 / 1 时按显式值，不受默认值影响；
 *    LRP3 空串、非数字、越界等无效内容一律回落默认值；
 *    LRP4 值前后带空白时能正确解析；
 *    LRP5 写回配置后再读回来一致（往返一致性，走真实的 ini 文件）；
 *    LRP6 本构建的默认值与构建配置相符。
 *
 *  全局 _Module 由其它测试文件统一定义，本文件只是随 stdafx.h 引用其声明。
 *
 *  balloonwj@qq.com   2026-08-17
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <string>

#include "../LogCleanupPolicy.h"

using namespace logcleanup;

namespace {

//两种默认值都要覆盖：解析函数不该因为"本次是 Debug 构建"就只走得到一条路径。
const bool kDefaultOn  = true;
const bool kDefaultOff = false;

/**
 *  造一个本次测试专用的临时 ini 路径。
 *
 *  文件名里带进程 id —— 两份测试进程并发跑时（命令行一份、Visual Studio 里一份）
 *  若用固定文件名就会共用同一个文件，先起的那个正开着它，后起的删不掉也写不进，
 *  表现为莫名其妙的偶发失败。仓库里其它用到临时文件的用例都是这个做法。
 *  @return 完整路径。
 */
std::wstring MakeTempIniPath()
{
    wchar_t szDir[MAX_PATH] = { 0 };
    const DWORD nLen = ::GetTempPathW(MAX_PATH, szDir);
    if (nLen == 0 || nLen >= MAX_PATH)
    {
        return std::wstring();
    }

    wchar_t szName[MAX_PATH] = { 0 };
    ::swprintf_s(szName, MAX_PATH, L"%sflamingo_logcleanup_%u.ini",
                 szDir, (unsigned)::GetCurrentProcessId());
    return std::wstring(szName);
}

} // namespace

TEST_CASE("LRP1 配置项不存在时取本构建的默认值", "[logcleanup]")
{
    //NULL 表示"配置里根本没有这一项"。两种默认值各走一遍。
    CHECK(ParseClearLogOnStart(NULL, kDefaultOn));
    CHECK_FALSE(ParseClearLogOnStart(NULL, kDefaultOff));

    //读一个不存在的文件，等价于配置项不存在，应当拿到本构建的默认值。
    CHECK(ReadClearLogOnStart(L"Z:\\no-such-dir\\no-such-file.ini")
          == DefaultClearLogOnStart());

    //路径为空也按同样口径处理，不该崩。
    CHECK(ReadClearLogOnStart(L"") == DefaultClearLogOnStart());
    CHECK(ReadClearLogOnStart(NULL) == DefaultClearLogOnStart());
}

TEST_CASE("LRP2 显式写 0 / 1 时按显式值", "[logcleanup]")
{
    //显式取值必须压过默认值，否则用户在界面上关掉之后，Release 版下次启动又会自己打开。
    CHECK_FALSE(ParseClearLogOnStart(L"0", kDefaultOn));
    CHECK_FALSE(ParseClearLogOnStart(L"0", kDefaultOff));

    CHECK(ParseClearLogOnStart(L"1", kDefaultOn));
    CHECK(ParseClearLogOnStart(L"1", kDefaultOff));
}

TEST_CASE("LRP3 无效内容一律回落默认值", "[logcleanup]")
{
    //空串：ini 里写了键但没写值，或者读取时用空串作默认。
    CHECK(ParseClearLogOnStart(L"", kDefaultOn));
    CHECK_FALSE(ParseClearLogOnStart(L"", kDefaultOff));

    //非数字。
    CHECK(ParseClearLogOnStart(L"abc", kDefaultOn));
    CHECK_FALSE(ParseClearLogOnStart(L"abc", kDefaultOff));

    //看着像布尔、但不是本项认的写法。
    CHECK(ParseClearLogOnStart(L"true", kDefaultOn));
    CHECK_FALSE(ParseClearLogOnStart(L"false", kDefaultOff));

    //数字但不在 {0,1} 内 —— 含负数、多位数、以及带前导零的写法。
    CHECK(ParseClearLogOnStart(L"-1", kDefaultOn));
    CHECK_FALSE(ParseClearLogOnStart(L"-1", kDefaultOff));
    CHECK(ParseClearLogOnStart(L"2", kDefaultOn));
    CHECK_FALSE(ParseClearLogOnStart(L"99", kDefaultOff));
    CHECK(ParseClearLogOnStart(L"01", kDefaultOn));

    //只有空白，去掉之后什么都不剩。
    CHECK(ParseClearLogOnStart(L"   ", kDefaultOn));
    CHECK_FALSE(ParseClearLogOnStart(L"\t\r\n", kDefaultOff));
}

TEST_CASE("LRP4 值前后带空白时能正确解析", "[logcleanup]")
{
    //手工编辑配置文件时很容易在等号后面多敲一个空格，不该因此丢掉用户的选择。
    CHECK_FALSE(ParseClearLogOnStart(L" 0", kDefaultOn));
    CHECK_FALSE(ParseClearLogOnStart(L"0 ", kDefaultOn));
    CHECK_FALSE(ParseClearLogOnStart(L"  0  ", kDefaultOn));

    CHECK(ParseClearLogOnStart(L" 1", kDefaultOff));
    CHECK(ParseClearLogOnStart(L"1\t", kDefaultOff));
    CHECK(ParseClearLogOnStart(L"\r\n 1 \r\n", kDefaultOff));
}

TEST_CASE("LRP5 写回配置后再读回来一致", "[logcleanup]")
{
    const std::wstring path = MakeTempIniPath();
    REQUIRE_FALSE(path.empty());

    //上一轮可能留下残留，先清干净再开始。
    ::DeleteFileW(path.c_str());

    //文件还不存在时读到的是本构建的默认值。
    CHECK(ReadClearLogOnStart(path.c_str()) == DefaultClearLogOnStart());

    //写"开"再读，必须为开；此时不管本构建默认值是什么。
    REQUIRE(WriteClearLogOnStart(path.c_str(), true));
    CHECK(ReadClearLogOnStart(path.c_str()));

    //写"关"再读，必须为关 —— 这条尤其重要：Release 构建默认是开，用户在界面上
    //关掉之后若读不出来，下次启动就又把日志清了。
    REQUIRE(WriteClearLogOnStart(path.c_str(), false));
    CHECK_FALSE(ReadClearLogOnStart(path.c_str()));

    //再翻回开，确认能反复改。
    REQUIRE(WriteClearLogOnStart(path.c_str(), true));
    CHECK(ReadClearLogOnStart(path.c_str()));

    //路径为空时写入应当失败而不是崩。
    CHECK_FALSE(WriteClearLogOnStart(L"", true));
    CHECK_FALSE(WriteClearLogOnStart(NULL, true));

    ::DeleteFileW(path.c_str());
}

TEST_CASE("LRP6 本构建的默认值与构建配置相符", "[logcleanup]")
{
    //Debug 关、Release 开。这条把"两处 #ifdef 写反了一处"挡住。
#ifdef _DEBUG
    CHECK_FALSE(DefaultClearLogOnStart());
#else
    CHECK(DefaultClearLogOnStart());
#endif
}
