/**
 *  PinyinFullTests.cpp
 *
 *  全拼匹配（pinyin::FullPinyinOf / SyllablesOf / MatchesFullPinyin）的单元测试。
 *  覆盖与用户对齐过的几类情形：
 *    PY1  全拼完整匹配；
 *    PY2  全拼前缀（打一半也能搜到）；
 *    PY3  从中间某个字起头（muchen 命中"苏沐辰"）；
 *    PY4  音节内部起头<u>不</u>命中（umuch 不该命中，属误联想）；
 *    PY8  表外字 / 标点 / 空格的处理，以及空格打断连续匹配；
 *    PY9  空查询与非 ASCII 查询一律不命中（交给原文子串匹配去处理）；
 *    PY10 多音字取 Unihan 的最常用读音。
 *
 *  查表数据由 tools/gen_pinyin_table.py 从 Unicode 官方 Unihan 生成，故这里的
 *  期望读音以该数据为准（如"重"取 zhong、"绿"取 lv）。
 *
 *  用 Catch2 v3（amalgamated）作框架，纯内存断言。
 *
 *  balloonwj@qq.com   2026-08-05
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include "../BalloonSearch/PinyinFull.h"

TEST_CASE("PY1 全拼完整匹配", "[pinyin]")
{
    CHECK(pinyin::MatchesFullPinyin(L"苏沐辰", "sumuchen"));
    CHECK(pinyin::MatchesFullPinyin(L"王洋", "wangyang"));
    //整串对不上不命中
    CHECK_FALSE(pinyin::MatchesFullPinyin(L"苏沐辰", "sumuchenx"));
    CHECK_FALSE(pinyin::MatchesFullPinyin(L"苏沐辰", "wangyang"));
}

TEST_CASE("PY2 全拼前缀：打一半也能搜到", "[pinyin]")
{
    CHECK(pinyin::MatchesFullPinyin(L"苏沐辰", "s"));
    CHECK(pinyin::MatchesFullPinyin(L"苏沐辰", "su"));
    CHECK(pinyin::MatchesFullPinyin(L"苏沐辰", "sum"));
    CHECK(pinyin::MatchesFullPinyin(L"苏沐辰", "sumu"));
    CHECK(pinyin::MatchesFullPinyin(L"苏沐辰", "sumuc"));
    //前缀对不上不命中
    CHECK_FALSE(pinyin::MatchesFullPinyin(L"苏沐辰", "sv"));
    CHECK_FALSE(pinyin::MatchesFullPinyin(L"苏沐辰", "sumuk"));
}

TEST_CASE("PY3 从中间某个字起头也能搜到", "[pinyin]")
{
    CHECK(pinyin::MatchesFullPinyin(L"苏沐辰", "mu"));
    CHECK(pinyin::MatchesFullPinyin(L"苏沐辰", "muchen"));
    CHECK(pinyin::MatchesFullPinyin(L"苏沐辰", "much"));
    CHECK(pinyin::MatchesFullPinyin(L"苏沐辰", "chen"));
    CHECK(pinyin::MatchesFullPinyin(L"苏沐辰", "ch"));
}

TEST_CASE("PY4 音节内部起头不命中，避免误联想", "[pinyin]")
{
    //"umuch" 是 sumuchen 的子串，但它从"苏"这个音节的中间起头，不该命中
    CHECK_FALSE(pinyin::MatchesFullPinyin(L"苏沐辰", "umuch"));
    CHECK_FALSE(pinyin::MatchesFullPinyin(L"苏沐辰", "uchen"));
    CHECK_FALSE(pinyin::MatchesFullPinyin(L"苏沐辰", "hen"));
}

TEST_CASE("PY8 中英混排、标点与空格的处理", "[pinyin]")
{
    //ASCII 字母 / 数字按自身参与匹配，中英混排的名字能被整串搜到
    CHECK(pinyin::MatchesFullPinyin(L"张三abc", "zhangsanabc"));
    CHECK(pinyin::MatchesFullPinyin(L"张三abc", "sanabc"));
    CHECK(pinyin::MatchesFullPinyin(L"张三2组", "zhangsan2"));

    //空格 / 标点产出空音节占位，会打断跨越它的连续匹配 —— 否则"张三 李四"
    //会被 "sanli" 命中，那是明显的误联想
    CHECK_FALSE(pinyin::MatchesFullPinyin(L"张三 李四", "sanli"));
    //但空格两侧各自仍可命中
    CHECK(pinyin::MatchesFullPinyin(L"张三 李四", "zhangsan"));
    CHECK(pinyin::MatchesFullPinyin(L"张三 李四", "lisi"));

    //纯 ASCII 文本按自身匹配，且同样只能从头起（不是任意子串）
    CHECK(pinyin::MatchesFullPinyin(L"abc", "abc"));
    CHECK(pinyin::MatchesFullPinyin(L"abc", "bc"));   // b 自己就是一个音节的开头
}

TEST_CASE("PY9 空查询与非 ASCII 查询一律不命中", "[pinyin]")
{
    CHECK_FALSE(pinyin::MatchesFullPinyin(L"苏沐辰", ""));
    CHECK_FALSE(pinyin::MatchesFullPinyin(L"", "sumu"));
    //查询词里带汉字：走全拼没有意义，交给调用方的原文子串匹配处理
    CHECK_FALSE(pinyin::MatchesFullPinyin(L"苏沐辰", "\xE8\x8B\x8F"));   // "苏" 的 UTF-8
}

TEST_CASE("PY10 单字读音与多音字取最常用音", "[pinyin]")
{
    CHECK(pinyin::FullPinyinOf(L'苏') == "su");
    CHECK(pinyin::FullPinyinOf(L'沐') == "mu");
    CHECK(pinyin::FullPinyinOf(L'辰') == "chen");
    //多音字：Unihan 的 kMandarin 首个读音即最常用读音
    CHECK(pinyin::FullPinyinOf(L'重') == "zhong");
    //ü 记作 v，与主流输入法习惯一致
    CHECK(pinyin::FullPinyinOf(L'吕') == "lv");
    CHECK(pinyin::FullPinyinOf(L'绿') == "lv");
    //非汉字与表外字返回空串，不崩
    CHECK(pinyin::FullPinyinOf(L'a').empty());
    CHECK(pinyin::FullPinyinOf(L' ').empty());
    CHECK(pinyin::FullPinyinOf(L'，').empty());
}

TEST_CASE("PY10b 音节拆分与原文逐字对应", "[pinyin]")
{
    std::vector<std::string> syllables = pinyin::SyllablesOf(L"苏a沐 辰");
    REQUIRE(syllables.size() == 5);
    CHECK(syllables[0] == "su");
    CHECK(syllables[1] == "a");
    CHECK(syllables[2] == "mu");
    CHECK(syllables[3].empty());     // 空格：占位，不参与匹配
    CHECK(syllables[4] == "chen");

    //空文本产出空数组
    CHECK(pinyin::SyllablesOf(L"").empty());
}

TEST_CASE("PY5 首字母串：与原文等长，且二级字也取得到", "[pinyin]")
{
    // "沐"是 GB2312 <u>二级</u>字（区位码 6769），老的 PinyinInitials 覆盖不到它，
    // 会让 "smc" 搜不到"苏沐辰"。本函数走 Unihan 全表，没有这个缺口 —— 这正是
    // 选人对话框改用它的原因。
    CHECK(pinyin::InitialsOf(L"苏沐辰") == "smc");
    CHECK(pinyin::InitialsOf(L"谢知行") == "xzx");
    CHECK(pinyin::InitialsOf(L"王洋")   == "wy");

    // 与原文 1:1 等长：ASCII 字母 / 数字取自身小写，其余字符取占位符 '\x01'
    const std::string s = pinyin::InitialsOf(L"张三ABC 李");
    REQUIRE(s.size() == 7);
    CHECK(s.substr(0, 2) == "zs");
    CHECK(s.substr(2, 3) == "abc");
    CHECK(s[5] == '\x01');          // 空格
    CHECK(s[6] == 'l');

    // 空文本产出空串
    CHECK(pinyin::InitialsOf(L"").empty());
}
