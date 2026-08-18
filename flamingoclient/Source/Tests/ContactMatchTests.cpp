/**
 *  联系人搜索匹配器单元测试（Suite CM），ContactMatchTests.cpp
 *  覆盖：昵称命中+高亮区间、账号命中、地区命中、拼音首字母命中+汉字区间映射、
 *       昵称优先于拼音、空查询、无命中、大小写不敏感；以及全拼档（CM8~CM13）：
 *       全拼完整命中、全拼前缀、从中间音节起头、音节内部起头不命中、二级字的
 *       首字母命中、首字母档优先于全拼档，并逐个校验高亮区间落在哪几个字上。
 *       框架 Catch2 v3。
 *
 *  说明：汉字一律用显式 Unicode 码点构造宽字符串，避免源文件编码影响。
 *  balloonwj@qq.com   2026-06-01
 */
#include "catch_amalgamated.hpp"
#include "../UI/BalloonSearch/ContactMatch.h"

using namespace contactsearch;

// 测试用汉字码点。
static const wchar_t kSu    = 0x82CF;   // 苏
static const wchar_t kMu    = 0x6C90;   // 沐
static const wchar_t kChen  = 0x8FB0;   // 辰
static const wchar_t kHang  = 0x676D;   // 杭
static const wchar_t kZhou  = 0x5DDE;   // 州
static const wchar_t kZhang = 0x5F20;   // 张
static const wchar_t kSan   = 0x4E09;   // 三

static std::wstring W(const wchar_t* cps, size_t n)
{
    return std::wstring(cps, cps + n);
}

TEST_CASE("CM1 昵称命中并给出高亮区间")
{
    const wchar_t name[] = { kSu, kMu, kChen };   // 苏沐辰
    const wchar_t q[]    = { kSu };               // 苏
    MatchResult r = Match(W(q, 1), W(name, 3), L"72100345", L"");
    CHECK(r.m_matched);
    CHECK(r.m_field == MATCH_NAME);
    CHECK(r.m_nameHiStart == 0);
    CHECK(r.m_nameHiLen == 1);
}

TEST_CASE("CM2 账号命中(不在昵称上高亮)")
{
    const wchar_t name[] = { kSu, kMu, kChen };
    MatchResult r = Match(L"7210", W(name, 3), L"72100345", L"");
    CHECK(r.m_matched);
    CHECK(r.m_field == MATCH_ACCOUNT);
    CHECK(r.m_nameHiStart == -1);
}

TEST_CASE("CM3 地区命中")
{
    const wchar_t name[]   = { kSu, kMu, kChen };
    const wchar_t region[] = { kHang, kZhou };   // 杭州
    const wchar_t q[]      = { kHang };          // 杭
    MatchResult r = Match(W(q, 1), W(name, 3), L"72100345", W(region, 2));
    CHECK(r.m_matched);
    CHECK(r.m_field == MATCH_REGION);
}

TEST_CASE("CM4 拼音首字母命中并映射回汉字区间")
{
    const wchar_t name[] = { kZhang, kSan };   // 张三 -> 拼音首字母 zs

    // 整串 "zs"
    MatchResult r1 = Match(L"zs", W(name, 2), L"", L"");
    CHECK(r1.m_matched);
    CHECK(r1.m_field == MATCH_PINYIN);
    CHECK(r1.m_nameHiStart == 0);
    CHECK(r1.m_nameHiLen == 2);

    // 仅 "s" 应命中第 2 个字(三)
    MatchResult r2 = Match(L"s", W(name, 2), L"", L"");
    CHECK(r2.m_matched);
    CHECK(r2.m_field == MATCH_PINYIN);
    CHECK(r2.m_nameHiStart == 1);
    CHECK(r2.m_nameHiLen == 1);
}

TEST_CASE("CM5 昵称子串优先于拼音首字母")
{
    // 英文名 "Bob"：查询 "b" 应作为昵称子串命中(而非拼音路径)
    MatchResult r = Match(L"b", L"Bob", L"", L"");
    CHECK(r.m_matched);
    CHECK(r.m_field == MATCH_NAME);
    CHECK(r.m_nameHiStart == 0);
    CHECK(r.m_nameHiLen == 1);
}

TEST_CASE("CM6 大小写不敏感")
{
    // 账号大写查询命中小写存储
    CHECK(Match(L"MY00", L"x", L"my0002", L"").m_matched);
    // 拼音大写查询
    const wchar_t name[] = { kZhang, kSan };
    MatchResult r = Match(L"ZS", W(name, 2), L"", L"");
    CHECK(r.m_matched);
    CHECK(r.m_field == MATCH_PINYIN);
}

TEST_CASE("CM7 空查询与无命中")
{
    const wchar_t name[] = { kZhang, kSan };
    CHECK_FALSE(Match(L"", W(name, 2), L"acc", L"reg").m_matched);
    CHECK_FALSE(Match(L"zzz", W(name, 2), L"acc", L"reg").m_matched);
}

TEST_CASE("CM8 全拼完整命中并高亮整个名字")
{
    // 苏沐辰 -> su / mu / chen
    const wchar_t name[] = { kSu, kMu, kChen };

    MatchResult r = Match(L"sumuchen", W(name, 3), L"", L"");
    CHECK(r.m_matched);
    CHECK(r.m_field == MATCH_PINYIN_FULL);
    CHECK(r.m_nameHiStart == 0);
    CHECK(r.m_nameHiLen == 3);

    // 大小写不敏感，全拼档同样适用
    MatchResult rUpper = Match(L"SuMuChen", W(name, 3), L"", L"");
    CHECK(rUpper.m_matched);
    CHECK(rUpper.m_field == MATCH_PINYIN_FULL);

    // 多打一个字母就不该命中了
    CHECK_FALSE(Match(L"sumuchenx", W(name, 3), L"", L"").m_matched);
}

TEST_CASE("CM9 全拼前缀命中，高亮只覆盖已打到的字")
{
    const wchar_t name[] = { kSu, kMu, kChen };

    // "sumu" 打完了前两个字，高亮"苏沐"
    MatchResult r1 = Match(L"sumu", W(name, 3), L"", L"");
    CHECK(r1.m_matched);
    CHECK(r1.m_field == MATCH_PINYIN_FULL);
    CHECK(r1.m_nameHiStart == 0);
    CHECK(r1.m_nameHiLen == 2);

    // "sum" 的第二个字只打了一半，那个字仍按整字高亮
    MatchResult r2 = Match(L"sum", W(name, 3), L"", L"");
    CHECK(r2.m_matched);
    CHECK(r2.m_field == MATCH_PINYIN_FULL);
    CHECK(r2.m_nameHiStart == 0);
    CHECK(r2.m_nameHiLen == 2);

    // "sumuc" 打到了第三个字的开头，高亮三个字
    MatchResult r3 = Match(L"sumuc", W(name, 3), L"", L"");
    CHECK(r3.m_matched);
    CHECK(r3.m_field == MATCH_PINYIN_FULL);
    CHECK(r3.m_nameHiStart == 0);
    CHECK(r3.m_nameHiLen == 3);

    // 前缀对不上不命中
    CHECK_FALSE(Match(L"sumuk", W(name, 3), L"", L"").m_matched);
}

TEST_CASE("CM10 从中间某个字的音节起头也能命中，高亮从那个字开始")
{
    const wchar_t name[] = { kSu, kMu, kChen };

    // "muchen" 覆盖"沐辰"
    MatchResult r1 = Match(L"muchen", W(name, 3), L"", L"");
    CHECK(r1.m_matched);
    CHECK(r1.m_field == MATCH_PINYIN_FULL);
    CHECK(r1.m_nameHiStart == 1);
    CHECK(r1.m_nameHiLen == 2);

    // "much" 的"辰"只打了一半，仍整字高亮，区间还是"沐辰"
    MatchResult r2 = Match(L"much", W(name, 3), L"", L"");
    CHECK(r2.m_matched);
    CHECK(r2.m_field == MATCH_PINYIN_FULL);
    CHECK(r2.m_nameHiStart == 1);
    CHECK(r2.m_nameHiLen == 2);

    // "chen" 只覆盖最后一个字
    MatchResult r3 = Match(L"chen", W(name, 3), L"", L"");
    CHECK(r3.m_matched);
    CHECK(r3.m_field == MATCH_PINYIN_FULL);
    CHECK(r3.m_nameHiStart == 2);
    CHECK(r3.m_nameHiLen == 1);

    // 张三：查 "san" 应高亮到第 2 个字(三)，不能连"张"一起高亮
    const wchar_t zhangsan[] = { kZhang, kSan };
    MatchResult r4 = Match(L"san", W(zhangsan, 2), L"", L"");
    CHECK(r4.m_matched);
    CHECK(r4.m_field == MATCH_PINYIN_FULL);
    CHECK(r4.m_nameHiStart == 1);
    CHECK(r4.m_nameHiLen == 1);
}

TEST_CASE("CM11 从音节内部起头不命中，避免误联想")
{
    const wchar_t name[] = { kSu, kMu, kChen };

    // "umuch" 是 sumuchen 的子串，但它从"苏"这个音节的中间起头，不该命中
    CHECK_FALSE(Match(L"umuch", W(name, 3), L"", L"").m_matched);
    CHECK_FALSE(Match(L"uchen", W(name, 3), L"", L"").m_matched);
    CHECK_FALSE(Match(L"hen", W(name, 3), L"", L"").m_matched);
}

TEST_CASE("CM12 二级字的首字母也能命中")
{
    // "沐"是 GB2312 二级字(区位码 6769)，老的 pinyin::Initials 取不到它的首字母，
    // 会让 smc 搜不到"苏沐辰"；改用 pinyin::InitialsOf 之后这个缺口没有了。
    const wchar_t name[] = { kSu, kMu, kChen };

    MatchResult r1 = Match(L"smc", W(name, 3), L"", L"");
    CHECK(r1.m_matched);
    CHECK(r1.m_field == MATCH_PINYIN);
    CHECK(r1.m_nameHiStart == 0);
    CHECK(r1.m_nameHiLen == 3);

    // 首字母串与昵称 1:1 等长，"mc" 应高亮到后两个字
    MatchResult r2 = Match(L"mc", W(name, 3), L"", L"");
    CHECK(r2.m_matched);
    CHECK(r2.m_field == MATCH_PINYIN);
    CHECK(r2.m_nameHiStart == 1);
    CHECK(r2.m_nameHiLen == 2);
}

TEST_CASE("CM13 首字母档优先于全拼档")
{
    // "s" 两档都能命中(既是首字母 s，也是 su 的前缀)，按优先级应走首字母档
    const wchar_t name[] = { kSu, kMu, kChen };
    MatchResult r = Match(L"s", W(name, 3), L"", L"");
    CHECK(r.m_matched);
    CHECK(r.m_field == MATCH_PINYIN);
    CHECK(r.m_nameHiStart == 0);
    CHECK(r.m_nameHiLen == 1);
}
