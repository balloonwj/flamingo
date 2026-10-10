/**
 *  敏感词匹配引擎单元测试（Suite SW），test_sensitiveword.cpp
 *
 *  覆盖 chatserversrc/SensitiveWordMatcher：归一化（大小写、全角半角、空白与符号、表情标记
 *  及其转义、非法 UTF-8）、词条校验（为空、超长）、Aho-Corasick 匹配（多命中时的取舍、前缀词）、
 *  内容摘要的截取，以及 10000 条词库下的耗时。用例清单与预期见 docs/敏感词方案.md 第 10.1 节。
 *
 *  balloonwj@qq.com   2026-10-02
 */
#include "doctest.h"
#include "../chatserversrc/SensitiveWordMatcher.h"

#include <chrono>
#include <string>
#include <vector>

using namespace chatserver;

namespace {

// 省略号 "…"（U+2026）的 UTF-8 编码，摘要两端被截断时用它。
const char* const kEllipsis = "\xE2\x80\xA6";

// U+FFFD 的 UTF-8 编码，摘要里原文的非法字节被替换成它。
const char* const kReplacementChar = "\xEF\xBF\xBD";

// 一个 4 字节 UTF-8 的表情（U+1F600），用字节写法以免受源文件编码影响。
const char* const kEmojiGrinning = "\xF0\x9F\x98\x80";

// 造一条词条。
SensitiveWordEntry MakeEntry(int64_t id, const std::string& word)
{
    SensitiveWordEntry e;
    e.m_id = id;
    e.m_word = word;
    e.m_category = sensitive_category_other;
    return e;
}

// 造只有一条词条的列表。
std::vector<SensitiveWordEntry> OneWord(int64_t id, const std::string& word)
{
    return std::vector<SensitiveWordEntry>(1, MakeEntry(id, word));
}

// 匹配一段文字，命中时返回词条ID，未命中返回 0。
int64_t HitId(const SensitiveWordMatcher& m, const std::string& text)
{
    SensitiveHit hit;
    if (!m.Match(text, &hit))
        return 0;
    return hit.m_wordId;
}

// 把一串码点编码成 UTF-8，用来把 Normalize 的结果与期望的字符串比较。
std::string EncodeUtf8(const std::vector<uint32_t>& cps)
{
    std::string out;
    for (size_t i = 0; i < cps.size(); ++i)
    {
        const uint32_t cp = cps[i];
        if (cp < 0x80)
        {
            out += static_cast<char>(cp);
        }
        else if (cp < 0x800)
        {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
        else if (cp < 0x10000)
        {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
        else
        {
            out += static_cast<char>(0xF0 | (cp >> 18));
            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }
    return out;
}

// 归一化一段文字并编码回 UTF-8。
std::string NormalizedText(const std::string& text)
{
    std::vector<uint32_t> cps;
    SensitiveWordMatcher::Normalize(text, cps, NULL);
    return EncodeUtf8(cps);
}

// 重复一个字符串 n 次。
std::string Repeat(const std::string& s, int n)
{
    std::string out;
    for (int i = 0; i < n; ++i)
        out += s;
    return out;
}

} // anonymous namespace

TEST_CASE("SW-M1 大小写与全角半角视为相同")
{
    SensitiveWordMatcher m;
    REQUIRE(m.Build(OneWord(1, "abc")) == 1);
    CHECK(HitId(m, "xxABCxx") == 1);
    CHECK(HitId(m, u8"ｘＡＢＣ") == 1);
    CHECK(HitId(m, "ab") == 0);
}

TEST_CASE("SW-M2 字与字之间的空白和符号被忽略")
{
    SensitiveWordMatcher m;
    REQUIRE(m.Build(OneWord(2, u8"敏感")) == 1);
    CHECK(HitId(m, u8"敏 感") == 2);
    CHECK(HitId(m, u8"敏*感") == 2);
    CHECK(HitId(m, u8"敏，感") == 2);
    CHECK(HitId(m, u8"敏\t\n感") == 2);
    CHECK(HitId(m, u8"敏　感") == 2);    //全角空格
    CHECK(HitId(m, u8"敏…感") == 2);
}

TEST_CASE("SW-M3 夹在词中间的表情被忽略")
{
    SensitiveWordMatcher m;
    REQUIRE(m.Build(OneWord(3, u8"敏感")) == 1);
    //表情标记
    CHECK(HitId(m, u8"敏/f[\"12\"]感") == 3);
    //4 字节 UTF-8 的 emoji
    CHECK(HitId(m, std::string(u8"敏") + kEmojiGrinning + u8"感") == 3);
}

TEST_CASE("SW-M4 被转义的表情标记按字面文字处理")
{
    //用户打出来的字面文字 /f["3"] 在发送时被客户端转义成 //f["3"]：按字面处理，
    //符号去掉后剩下 f3。真正的表情标记 /f["3"] 整体去掉。不在表情标记之前的 //（如网址）
    //不是转义，按普通符号去掉。
    CHECK(NormalizedText("//f[\"3\"]") == "f3");
    CHECK(NormalizedText("/f[\"3\"]") == "");
    CHECK(NormalizedText("http://x") == "httpx");

    SensitiveWordMatcher m;
    REQUIRE(m.Build(OneWord(4, "f3")) == 1);
    CHECK(HitId(m, "//f[\"3\"]") == 4);
    CHECK(HitId(m, "/f[\"3\"]") == 0);
}

TEST_CASE("SW-M5 非法 UTF-8 不崩溃且照常匹配")
{
    //开头两个孤立字节，结尾一个被截断的多字节序列的首字节
    const std::string text = std::string("\xFF\xFE") + u8"敏感" + "\xC3";
    CHECK(SensitiveWordMatcher::CountChars(text) == 5);

    SensitiveWordMatcher m;
    REQUIRE(m.Build(OneWord(5, u8"敏感")) == 1);
    SensitiveHit hit;
    REQUIRE(m.Match(text, &hit));
    CHECK(hit.m_wordId == 5);
    CHECK(hit.m_beginChar == 2);
    CHECK(hit.m_endChar == 4);
}

TEST_CASE("SW-M6 Aho-Corasick 经典用例：取最先结束、同时结束时取最长的")
{
    std::vector<SensitiveWordEntry> words;
    words.push_back(MakeEntry(61, "he"));
    words.push_back(MakeEntry(62, "she"));
    words.push_back(MakeEntry(63, "hers"));
    words.push_back(MakeEntry(64, "his"));
    SensitiveWordMatcher m;
    REQUIRE(m.Build(words) == 4);

    //"ushers" 里 she 与 he 同在下标 3 结束，取更长的 she；hers 结束得更晚
    SensitiveHit hit;
    REQUIRE(m.Match("ushers", &hit));
    CHECK(hit.m_wordId == 62);
    CHECK(hit.m_beginChar == 1);
    CHECK(hit.m_endChar == 4);

    CHECK(HitId(m, "ahishe") == 64);   //his 先于 she / he 结束
}

TEST_CASE("SW-M7 空词库、空文字与全是符号的文字都不命中")
{
    SensitiveWordMatcher empty;
    CHECK(empty.WordCount() == 0);
    CHECK(HitId(empty, "anything") == 0);

    SensitiveWordMatcher m;
    REQUIRE(m.Build(OneWord(7, "abc")) == 1);
    CHECK(m.WordCount() == 1);
    CHECK(HitId(m, "") == 0);
    CHECK(HitId(m, "!!!") == 0);
}

TEST_CASE("SW-M8 词条校验：去掉符号后为空的词条不合格，构建时跳过")
{
    std::string normalized = "unchanged";
    CHECK(SensitiveWordMatcher::CheckWord("!!!", &normalized) == sensitive_word_check_empty);
    CHECK(normalized.empty());
    CHECK(SensitiveWordMatcher::CheckWord("   ", NULL) == sensitive_word_check_empty);
    CHECK(SensitiveWordMatcher::CheckWord("/f[\"1\"]", NULL) == sensitive_word_check_empty);

    CHECK(SensitiveWordMatcher::CheckWord(u8"Ａ b-C", &normalized) == sensitive_word_check_ok);
    CHECK(normalized == "abc");

    std::vector<SensitiveWordEntry> words;
    words.push_back(MakeEntry(81, "!!!"));
    words.push_back(MakeEntry(82, "abc"));
    SensitiveWordMatcher m;
    CHECK(m.Build(words) == 1);
    CHECK(HitId(m, "!!!") == 0);
    CHECK(HitId(m, "abc") == 82);
}

TEST_CASE("SW-M9 内容摘要按字符截取，被截断的一端加省略号")
{
    SensitiveWordMatcher m;
    REQUIRE(m.Build(OneWord(9, u8"敏感")) == 1);
    const uint32_t kContext = 20;

    //命中在正中：两端都被截断
    const std::string middle = Repeat("a", 30) + u8"敏感" + Repeat("b", 30);
    SensitiveHit hit;
    REQUIRE(m.Match(middle, &hit));
    CHECK(hit.m_beginChar == 30);
    CHECK(hit.m_endChar == 32);
    CHECK(SensitiveWordMatcher::MakeSnippet(middle, hit.m_beginChar, hit.m_endChar, kContext)
          == std::string(kEllipsis) + Repeat("a", 20) + u8"敏感" + Repeat("b", 20) + kEllipsis);

    //命中在开头：只有后面被截断
    const std::string head = std::string(u8"敏感") + Repeat("b", 30);
    REQUIRE(m.Match(head, &hit));
    CHECK(SensitiveWordMatcher::MakeSnippet(head, hit.m_beginChar, hit.m_endChar, kContext)
          == std::string(u8"敏感") + Repeat("b", 20) + kEllipsis);

    //全文不足前后各 20 字：原样返回
    const std::string shortText = u8"x敏感y";
    REQUIRE(m.Match(shortText, &hit));
    CHECK(SensitiveWordMatcher::MakeSnippet(shortText, hit.m_beginChar, hit.m_endChar, kContext)
          == shortText);

    //命中区间包含被忽略的符号，符号保留在摘要里
    const std::string withSymbol = u8"前文敏*感后文";
    REQUIRE(m.Match(withSymbol, &hit));
    CHECK(hit.m_beginChar == 2);
    CHECK(hit.m_endChar == 5);
    CHECK(SensitiveWordMatcher::MakeSnippet(withSymbol, hit.m_beginChar, hit.m_endChar, 1)
          == std::string(kEllipsis) + u8"文敏*感后" + kEllipsis);

    //原文里的非法字节在摘要里替换成 U+FFFD
    const std::string invalid = std::string("\xFF") + u8"敏感";
    CHECK(SensitiveWordMatcher::MakeSnippet(invalid, 1, 3, kContext)
          == std::string(kReplacementChar) + u8"敏感");
}

TEST_CASE("SW-M10 10000 条、每条 32 字的词库下建树与匹配的耗时")
{
    //词条与文字都由随机汉字组成（U+4E00～U+9FA5），用固定种子的线性同余生成器，每次运行
    //结果相同。32 字的随机词条之间几乎没有公共前缀，自动机节点数接近最坏情况。
    const int kWordCount     = 10000;
    const int kWordChars     = 32;
    const size_t kTextBytes  = 256 * 1024;
    const uint32_t kCjkFirst = 0x4E00;
    const uint32_t kCjkCount = 0x9FA5 - 0x4E00 + 1;
    const long long kMaxBuildMs = 3000;   //Debug 构建下建树的耗时上限（毫秒）
    const long long kMaxMatchMs = 500;    //Debug 构建下单次匹配的耗时上限（毫秒）

    uint32_t seed = 20261002;
    std::vector<SensitiveWordEntry> words;
    words.reserve(kWordCount);
    for (int i = 0; i < kWordCount; ++i)
    {
        std::vector<uint32_t> cps;
        for (int j = 0; j < kWordChars; ++j)
        {
            seed = seed * 1103515245u + 12345u;
            cps.push_back(kCjkFirst + (seed >> 8) % kCjkCount);
        }
        words.push_back(MakeEntry(i + 1, EncodeUtf8(cps)));
    }

    //随机文字，末尾放进第 5000 条词条，确认在大词库下也能命中
    std::vector<uint32_t> textCps;
    while (textCps.size() * 3 < kTextBytes)
    {
        seed = seed * 1103515245u + 12345u;
        textCps.push_back(kCjkFirst + (seed >> 8) % kCjkCount);
    }
    const int64_t kPlantedId = 5000;
    const std::string text = EncodeUtf8(textCps) + words[kPlantedId - 1].m_word;

    SensitiveWordMatcher m;
    const std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
    const size_t built = m.Build(words);
    const std::chrono::steady_clock::time_point t1 = std::chrono::steady_clock::now();
    SensitiveHit hit;
    const bool matched = m.Match(text, &hit);
    const std::chrono::steady_clock::time_point t2 = std::chrono::steady_clock::now();

    const long long buildMs = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
    const long long matchMs = std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1).count();
    MESSAGE("SW-M10 build " << buildMs << " ms, match " << matchMs << " ms, text bytes "
            << text.size());

    CHECK(built == static_cast<size_t>(kWordCount));
    REQUIRE(matched);
    CHECK(hit.m_wordId == kPlantedId);
    CHECK(buildMs <= kMaxBuildMs);
    CHECK(matchMs <= kMaxMatchMs);
}

TEST_CASE("SW-M11 词条长度按去掉首尾空白后的字符数计")
{
    const std::string word32 = Repeat(u8"字", 32);
    const std::string word33 = Repeat(u8"字", 33);
    CHECK(SensitiveWordMatcher::CountChars(word32) == 32);
    CHECK(SensitiveWordMatcher::CheckWord(word32, NULL) == sensitive_word_check_ok);
    CHECK(SensitiveWordMatcher::CheckWord(word33, NULL) == sensitive_word_check_too_long);

    std::string normalized;
    CHECK(SensitiveWordMatcher::CheckWord("  abc  ", &normalized) == sensitive_word_check_ok);
    CHECK(normalized == "abc");
}

TEST_CASE("SW-M12 不该命中的反例：数字与汉字不属于被忽略的字符")
{
    SensitiveWordMatcher m;
    REQUIRE(m.Build(OneWord(12, u8"敏感")) == 1);
    CHECK(HitId(m, u8"敏捷感谢") == 0);
    CHECK(HitId(m, u8"感敏") == 0);

    SensitiveWordMatcher ab;
    REQUIRE(ab.Build(OneWord(121, "ab")) == 1);
    CHECK(HitId(ab, "a1b") == 0);
    CHECK(HitId(ab, u8"a中b") == 0);
}

TEST_CASE("SW-M13 前缀词：先结束的那个先命中")
{
    std::vector<SensitiveWordEntry> words;
    words.push_back(MakeEntry(131, u8"敏感"));
    words.push_back(MakeEntry(132, u8"敏感词"));
    SensitiveWordMatcher m;
    REQUIRE(m.Build(words) == 2);
    CHECK(HitId(m, u8"敏感词") == 131);
    CHECK(HitId(m, u8"词库里的敏感词") == 131);
}
