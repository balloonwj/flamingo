/**
 *  拼音首字母转换单元测试（Suite PY），PinyinInitialsTests.cpp
 *  覆盖：常用汉字→首字母、1:1 等长映射、中英混合、ASCII 大小写、空串、
 *       数字保持。框架 Catch2 v3。
 *
 *  说明：为不依赖源文件编码（本机 MSVC 默认按 GBK 读源码，直接写 L"中文"
 *  会被误解析），测试里的汉字一律用显式 Unicode 码点构造宽字符串。
 *  balloonwj@qq.com   2026-06-01
 */
#include "catch_amalgamated.hpp"
#include "../UI/BalloonSearch/PinyinInitials.h"

using namespace pinyin;

// 测试用常用汉字（显式码点，避免源码编码影响）。
static const wchar_t kZhang = 0x5F20;   // 张  -> z
static const wchar_t kSan   = 0x4E09;   // 三  -> s
static const wchar_t kWang  = 0x738B;   // 王  -> w
static const wchar_t kZhong = 0x4E2D;   // 中  -> z
static const wchar_t kGuo   = 0x56FD;   // 国  -> g
static const wchar_t kLi    = 0x674E;   // 李  -> l

// 由码点数组拼成宽字符串。
static std::wstring MakeW(const wchar_t* cps, size_t n)
{
    return std::wstring(cps, cps + n);
}

TEST_CASE("PY1 changyong hanzi -> pinyin initials")
{
    const wchar_t zhangsan[] = { kZhang, kSan };
    CHECK(Initials(MakeW(zhangsan, 2)) == "zs");

    const wchar_t wangzhongguo[] = { kWang, kZhong, kGuo };
    CHECK(Initials(MakeW(wangzhongguo, 3)) == "wzg");

    const wchar_t li[] = { kLi };
    CHECK(Initials(MakeW(li, 1)) == "l");
}

TEST_CASE("PY2 1:1 length mapping")
{
    const wchar_t wangzhongguo[] = { kWang, kZhong, kGuo };
    std::string s = Initials(MakeW(wangzhongguo, 3));
    CHECK(s.size() == 3);
}

TEST_CASE("PY3 mixed ascii + hanzi")
{
    // "Ab" + 张 -> "abz"
    const wchar_t mixed[] = { L'A', L'b', kZhang };
    CHECK(Initials(MakeW(mixed, 3)) == "abz");

    // pure ascii name stays lowercase
    CHECK(Initials(L"Bob") == "bob");
}

TEST_CASE("PY4 empty string")
{
    CHECK(Initials(L"").empty());
}

TEST_CASE("PY5 digits kept 1:1, not turned into letters")
{
    // 张 + "123" -> 'z','1','2','3'
    const wchar_t s123[] = { kZhang, L'1', L'2', L'3' };
    std::string s = Initials(MakeW(s123, 4));
    REQUIRE(s.size() == 4);
    CHECK(s[0] == 'z');
    CHECK(s[1] == '1');
    CHECK(s[2] == '2');
    CHECK(s[3] == '3');
}
