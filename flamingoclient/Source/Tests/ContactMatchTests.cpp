/**
 *  联系人搜索匹配器单元测试（Suite CM），ContactMatchTests.cpp
 *  覆盖：昵称命中+高亮区间、账号命中、地区命中、拼音首字母命中+汉字区间映射、
 *       昵称优先于拼音、空查询、无命中、大小写不敏感；以及全拼档（CM8~CM13）：
 *       全拼完整命中、全拼前缀、从中间音节起头、音节内部起头不命中、二级字的
 *       首字母命中、首字母档优先于全拼档，并逐个校验高亮区间落在哪几个字上。
 *       预处理接口（CMP1~CMP2，标签 [contactmatch][prepared]）：以 Match 为标准答案，
 *       在一组名字 × 账号地区 × 查询词的语料上逐对比对 MatchPrepared；查询词
 *       预处理的转小写与"含汉字不走拼音"两条规则；以及名字预处理得到的首字母串、
 *       音节序列与 pinyin::InitialsOf / SyllablesOf 逐个一致（CMP3）。
 *       框架 Catch2 v3。
 *
 *  说明：汉字一律用显式 Unicode 码点构造宽字符串，避免源文件编码影响。
 *  balloonwj@qq.com   2026-06-01
 */
#include "catch_amalgamated.hpp"
#include "../UI/BalloonSearch/ContactMatch.h"
#include "../UI/BalloonSearch/PinyinFull.h"

#include <string>
#include <utility>
#include <vector>

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

// =======================================================================
// CMP：预处理接口（PrepareQuery / PrepareName / MatchPrepared）
// =======================================================================

// CMP 语料额外用到的码点。
static const wchar_t kLi     = 0x674E;   // 李
static const wchar_t kSi     = 0x56DB;   // 四
static const wchar_t kWei    = 0x4F1F;   // 伟
static const wchar_t kWang   = 0x738B;   // 王
static const wchar_t kDunHao = 0x3001;   // 、（顿号，标点，音节为空）
static const wchar_t kExtA   = 0x3400;   // 㐀（CJK 扩展 A 区，不在拼音表覆盖的 0x4E00~0x9FFF 之内）

// MatchField 的取值个数（0 ~ MATCH_PINYIN_FULL），用于按档位统计命中数。
static const int kMatchFieldCount = MATCH_PINYIN_FULL + 1;

// CMP1 的名字语料：二级字、中英混排、空格分隔、标点、表外字、大小写英文、数字与空名。
static std::vector<std::wstring> CmpNames()
{
    const wchar_t suMuChen[]     = { kSu, kMu, kChen, 0 };
    const wchar_t zhangSan[]     = { kZhang, kSan, 0 };
    const wchar_t zhangSanAbc[]  = { kZhang, kSan, L'a', L'b', L'c', 0 };
    const wchar_t zhangSanLiSi[] = { kZhang, kSan, L' ', kLi, kSi, 0 };
    const wchar_t zhangWei[]     = { kZhang, kWei, 0 };
    const wchar_t wangDunLi[]    = { kWang, kDunHao, kLi, 0 };
    const wchar_t extAZhangSan[] = { kExtA, kZhang, kSan, 0 };
    const wchar_t hangZhou[]     = { kHang, kZhou, 0 };

    std::vector<std::wstring> names;
    names.push_back(suMuChen);
    names.push_back(zhangSan);
    names.push_back(zhangSanAbc);
    names.push_back(zhangSanLiSi);
    names.push_back(zhangWei);
    names.push_back(wangDunLi);
    names.push_back(extAZhangSan);
    names.push_back(hangZhou);
    names.push_back(L"Alice Wang");
    names.push_back(L"BOB");
    names.push_back(L"Zhang3");
    names.push_back(L"");
    return names;
}

// CMP1 的查询词语料：拼音逐字输入、首字母、全拼前缀、音节内部起头、跨空格、
// 汉字、汉字与 ASCII 混排、账号与地区片段、大小写、空格与空串。
static std::vector<std::wstring> CmpQueries()
{
    const wchar_t qZhang[]    = { kZhang, 0 };
    const wchar_t qZhangWei[] = { kZhang, kWei, 0 };
    const wchar_t qHang[]     = { kHang, 0 };
    const wchar_t qMixed[]    = { L'Z', kZhang, 0 };

    const wchar_t* const ascii[] = {
        L"", L"z", L"zh", L"zha", L"zhan", L"ZHANG", L"zs", L"smc", L"SMC", L"sumu",
        L"sum", L"much", L"umuch", L"sanli", L"lisi", L"san", L"wl", L"wang",
        L"zhangsanabc", L"zhangsan3", L"bc", L"3", L"my00", L"bei", L" ", L"zzz"
    };

    std::vector<std::wstring> queries(ascii, ascii + sizeof(ascii) / sizeof(ascii[0]));
    queries.push_back(qZhang);
    queries.push_back(qZhangWei);
    queries.push_back(qHang);
    queries.push_back(qMixed);
    return queries;
}

// 两个匹配结果的四个字段是否完全相同。
static bool SameResult(const MatchResult& a, const MatchResult& b)
{
    return a.m_matched == b.m_matched
        && a.m_field == b.m_field
        && a.m_nameHiStart == b.m_nameHiStart
        && a.m_nameHiLen == b.m_nameHiLen;
}

// 把匹配结果写成 "{matched,field,hiStart,hiLen}"，用于失败时的诊断信息。
static std::string DescribeResult(const MatchResult& r)
{
    return "{" + std::to_string(r.m_matched ? 1 : 0) + "," + std::to_string(r.m_field) + ","
         + std::to_string(r.m_nameHiStart) + "," + std::to_string(r.m_nameHiLen) + "}";
}

TEST_CASE("CMP1 预处理后的匹配与 Match 逐对一致", "[contactmatch][prepared]")
{
    const wchar_t hangZhou[] = { kHang, kZhou, 0 };

    const std::vector<std::wstring> names   = CmpNames();
    const std::vector<std::wstring> queries = CmpQueries();

    // 账号 / 地区组合：都为空；纯小写账号 + 汉字地区；大写账号 + 英文地区。
    std::vector<std::pair<std::wstring, std::wstring> > fields;
    fields.push_back(std::make_pair(std::wstring(), std::wstring()));
    fields.push_back(std::make_pair(std::wstring(L"zhangwei01"), std::wstring(hangZhou)));
    fields.push_back(std::make_pair(std::wstring(L"MY0002"), std::wstring(L"Beijing")));

    int compared   = 0;
    int mismatches = 0;
    std::string firstMismatch;
    int hitsByField[kMatchFieldCount] = { 0 };

    for (size_t n = 0; n < names.size(); ++n)
    {
        // 每个名字只预处理一次，在全部查询词上反复使用 —— 这正是搜索框的用法
        const PreparedName pn = PrepareName(names[n]);
        for (size_t f = 0; f < fields.size(); ++f)
        {
            const std::wstring accountLower = ToLowerAscii(fields[f].first);
            const std::wstring regionLower  = ToLowerAscii(fields[f].second);
            for (size_t q = 0; q < queries.size(); ++q)
            {
                const MatchResult expect = Match(queries[q], names[n], fields[f].first, fields[f].second);
                const MatchResult got    = MatchPrepared(PrepareQuery(queries[q]), pn,
                                                         accountLower, regionLower);
                ++compared;
                if (expect.m_matched && expect.m_field >= 0 && expect.m_field < kMatchFieldCount)
                {
                    ++hitsByField[expect.m_field];
                }
                if (!SameResult(expect, got))
                {
                    ++mismatches;
                    if (firstMismatch.empty())
                    {
                        firstMismatch = "name #" + std::to_string(n) + ", fields #" + std::to_string(f)
                                      + ", query #" + std::to_string(q) + ": expect "
                                      + DescribeResult(expect) + " got " + DescribeResult(got);
                    }
                }
            }
        }
    }

    INFO("compared " << compared << " pairs, first mismatch: " << firstMismatch);
    CHECK(mismatches == 0);

    // 语料必须五档都有命中，否则比对再多也证明不了某一档等价
    CHECK(hitsByField[MATCH_NAME] > 0);
    CHECK(hitsByField[MATCH_ACCOUNT] > 0);
    CHECK(hitsByField[MATCH_REGION] > 0);
    CHECK(hitsByField[MATCH_PINYIN] > 0);
    CHECK(hitsByField[MATCH_PINYIN_FULL] > 0);
}

TEST_CASE("CMP2 查询词预处理：转小写，含汉字时拼音两档不参与", "[contactmatch][prepared]")
{
    // 纯 ASCII：转小写，窄串形式与之相同
    const PreparedQuery a = PrepareQuery(L"ZhAng");
    CHECK(a.m_lower == L"zhang");
    CHECK(a.m_ascii == "zhang");

    // 纯汉字：原样保留，窄串为空
    const wchar_t zhangWei[] = { kZhang, kWei, 0 };
    const PreparedQuery b = PrepareQuery(zhangWei);
    CHECK(b.m_lower == std::wstring(zhangWei));
    CHECK(b.m_ascii.empty());

    // ASCII 与汉字混排：ASCII 部分照样转小写，但窄串为空
    const wchar_t mixed[]      = { L'Z', kZhang, 0 };
    const wchar_t mixedLower[] = { L'z', kZhang, 0 };
    const PreparedQuery c = PrepareQuery(mixed);
    CHECK(c.m_lower == std::wstring(mixedLower));
    CHECK(c.m_ascii.empty());

    // 空查询：两个字段都为空
    const PreparedQuery d = PrepareQuery(L"");
    CHECK(d.m_lower.empty());
    CHECK(d.m_ascii.empty());
}

TEST_CASE("CMP3 名字预处理的首字母串与音节序列与拼音模块一致", "[contactmatch][prepared]")
{
    // PrepareName 为省一次查表，首字母串由音节序列推出，而不是再调一次 InitialsOf。
    // 两者必须逐字相同：占位符、ASCII 字母与数字、表外字的处理都不能有出入。
    const std::vector<std::wstring> names = CmpNames();
    int mismatches = 0;
    std::string firstMismatch;
    for (size_t n = 0; n < names.size(); ++n)
    {
        const PreparedName pn = PrepareName(names[n]);
        const bool ok = (pn.m_initials == pinyin::InitialsOf(names[n]))
                     && (pn.m_syllables == pinyin::SyllablesOf(names[n]))
                     && (pn.m_lower == ToLowerAscii(names[n]));
        if (!ok)
        {
            ++mismatches;
            if (firstMismatch.empty())
            {
                firstMismatch = "name #" + std::to_string(n);
            }
        }
    }
    INFO("first mismatch: " << firstMismatch);
    CHECK(mismatches == 0);
}
