/**
 *  FlamingoLang.dll 的 .po 解析（PoParser）与译文查找表（CLangTable）的单元测试，用例 LD1 ~ LD11。
 *
 *  用例清单（2026-10-03 与用户确认，见 docs/多语言方案.md 第 11 节 1a）：
 *    - LD1  单行 msgid / msgstr 能查到译文
 *    - LD2  多行字符串拼接
 *    - LD3  \" \\ \n \t 转义还原
 *    - LD4  msgctxt 上下文区分同一原文；不带上下文的查找不命中带上下文的条目
 *    - LD5  空译文视为没有译文
 *    - LD6  注释行与作废条目忽略，标了「待审校」的条目照常生效
 *    - LD7  文件头条目不当作译文
 *    - LD8  损坏输入不崩溃，跳过坏条目、保留好条目，并报告错误数
 *    - LD9  UTF-8 BOM 与 CRLF 换行；非法 UTF-8 的条目跳过
 *    - LD10 重复条目取第一条并计一次警告
 *    - LD11 哈希冲突时各条仍查到各自的译文
 *
 *  本文件与被测源文件一起直接编进 FlamingoUnit，不经过 DLL。框架 Catch2 v3，标签 [langdll]。
 *  balloonwj@qq.com   2026-10-03
 */
#include "catch_amalgamated.hpp"

#include <windows.h>
#include <string.h>
#include <string>

#include "../PoParser.h"
#include "../LangTable.h"

using flamingolang::CLangTable;
using flamingolang::ParsePo;
using flamingolang::PoParseResult;

namespace {

//解析一段 .po 文本并建表，返回解析结果供用例检查
PoParseResult ParseAndBuild(const std::string& po, CLangTable& table)
{
    PoParseResult parsed;
    ParsePo(po.data(), po.size(), parsed);
    table.Build(parsed);
    return parsed;
}

//查找结果与期望值相同（期望为 NULL 时要求查不到）
bool LookupEquals(const CLangTable& table, const wchar_t* context, const wchar_t* source, const wchar_t* expect)
{
    const wchar_t* got = table.Lookup(context, source);
    if (expect == NULL)
        return got == NULL;
    return got != NULL && wcscmp(got, expect) == 0;
}

//故意让所有输入都得到同一个哈希值，用来制造冲突
const uint64_t kCollidingHash = 42;

uint64_t CollidingHash(const wchar_t* context, const wchar_t* source)
{
    (void)context;
    (void)source;
    return kCollidingHash;
}

} // namespace

TEST_CASE("LD1 单行 msgid / msgstr 能查到译文", "[langdll]")
{
    CLangTable table;
    PoParseResult parsed = ParseAndBuild(
        u8"msgid \"记住密码\"\n"
        u8"msgstr \"Remember password\"\n", table);

    CHECK(parsed.m_errors == 0);
    CHECK(parsed.m_entries.size() == 1);
    CHECK(table.GetEntryCount() == 1);
    CHECK(LookupEquals(table, NULL, L"记住密码", L"Remember password"));
    CHECK(LookupEquals(table, L"", L"记住密码", L"Remember password"));
    CHECK(LookupEquals(table, NULL, L"自动登录", NULL));
    CHECK(table.Lookup(NULL, NULL) == NULL);
}

TEST_CASE("LD2 多行字符串拼接成完整文字", "[langdll]")
{
    CLangTable table;
    PoParseResult parsed = ParseAndBuild(
        u8"msgid \"\"\n"
        u8"\"第一段\"\n"
        u8"\"第二段\"\n"
        u8"msgstr \"\"\n"
        u8"\"Part one, \"\n"
        u8"\"part two\"\n", table);

    CHECK(parsed.m_errors == 0);
    REQUIRE(parsed.m_entries.size() == 1);
    CHECK(parsed.m_entries[0].m_msgid == u8"第一段第二段");
    CHECK(LookupEquals(table, NULL, L"第一段第二段", L"Part one, part two"));
}

TEST_CASE("LD3 转义字符正确还原", "[langdll]")
{
    CLangTable table;
    PoParseResult parsed = ParseAndBuild(
        u8"msgid \"引号\\\"与反斜杠\\\\\"\n"
        u8"msgstr \"Quote \\\" backslash \\\\ line\\nbreak\\ttab\"\n", table);

    CHECK(parsed.m_errors == 0);
    CHECK(LookupEquals(table, NULL, L"引号\"与反斜杠\\", L"Quote \" backslash \\ line\nbreak\ttab"));
}

TEST_CASE("LD4 msgctxt 上下文区分同一原文", "[langdll]")
{
    const std::string contextOnly =
        u8"msgctxt \"群成员菜单\"\n"
        u8"msgid \"移出\"\n"
        u8"msgstr \"Remove\"\n"
        u8"\n"
        u8"msgctxt \"文件菜单\"\n"
        u8"msgid \"移出\"\n"
        u8"msgstr \"Move out\"\n";

    SECTION("只有带上下文的条目时，不带上下文的查找查不到")
    {
        CLangTable table;
        PoParseResult parsed = ParseAndBuild(contextOnly, table);
        CHECK(parsed.m_errors == 0);
        CHECK(table.GetEntryCount() == 2);
        CHECK(LookupEquals(table, L"群成员菜单", L"移出", L"Remove"));
        CHECK(LookupEquals(table, L"文件菜单", L"移出", L"Move out"));
        CHECK(LookupEquals(table, NULL, L"移出", NULL));
        CHECK(LookupEquals(table, L"不存在的上下文", L"移出", NULL));
    }

    SECTION("同时有不带上下文的条目时，三者互不干扰")
    {
        CLangTable table;
        PoParseResult parsed = ParseAndBuild(contextOnly +
            u8"\n"
            u8"msgid \"移出\"\n"
            u8"msgstr \"Take out\"\n", table);
        CHECK(parsed.m_errors == 0);
        CHECK(table.GetEntryCount() == 3);
        CHECK(table.GetWarningCount() == 0);
        CHECK(LookupEquals(table, NULL, L"移出", L"Take out"));
        CHECK(LookupEquals(table, L"群成员菜单", L"移出", L"Remove"));
        CHECK(LookupEquals(table, L"文件菜单", L"移出", L"Move out"));
    }
}

TEST_CASE("LD5 空译文视为没有译文", "[langdll]")
{
    CLangTable table;
    PoParseResult parsed = ParseAndBuild(
        u8"msgid \"消息记录\"\n"
        u8"msgstr \"\"\n"
        u8"\n"
        u8"msgid \"发送\"\n"
        u8"msgstr \"Send\"\n", table);

    CHECK(parsed.m_errors == 0);
    CHECK(parsed.m_emptyCount == 1);
    CHECK(table.GetEntryCount() == 1);
    CHECK(LookupEquals(table, NULL, L"消息记录", NULL));
    CHECK(LookupEquals(table, NULL, L"发送", L"Send"));
}

TEST_CASE("LD6 注释与作废条目被忽略，待审校条目照常生效", "[langdll]")
{
    CLangTable table;
    PoParseResult parsed = ParseAndBuild(
        u8"# 译者注释\n"
        u8"#. 待审校\n"
        u8"#: Source/UI/BalloonAuth/BalloonLoginLayout.cpp:120\n"
        u8"#, c-format\n"
        u8"msgid \"发送\"\n"
        u8"msgstr \"Send\"\n"
        u8"\n"
        u8"#~ msgid \"旧文字\"\n"
        u8"#~ msgstr \"Old text\"\n"
        u8"\n"
        u8"# 注释后面紧跟下一条\n"
        u8"msgid \"取消\"\n"
        u8"msgstr \"Cancel\"\n", table);

    CHECK(parsed.m_errors == 0);
    CHECK(table.GetEntryCount() == 2);
    CHECK(LookupEquals(table, NULL, L"发送", L"Send"));
    CHECK(LookupEquals(table, NULL, L"取消", L"Cancel"));
    CHECK(LookupEquals(table, NULL, L"旧文字", NULL));
}

TEST_CASE("LD7 文件头条目不当作译文", "[langdll]")
{
    CLangTable table;
    PoParseResult parsed = ParseAndBuild(
        u8"# FlamingoIM 界面文字：英文译文\n"
        u8"msgid \"\"\n"
        u8"msgstr \"\"\n"
        u8"\"Content-Type: text/plain; charset=UTF-8\\n\"\n"
        u8"\"Language: en-US\\n\"\n"
        u8"\n"
        u8"msgid \"发送\"\n"
        u8"msgstr \"Send\"\n", table);

    CHECK(parsed.m_errors == 0);
    CHECK(parsed.m_emptyCount == 0);
    CHECK(table.GetEntryCount() == 1);
    CHECK(LookupEquals(table, NULL, L"", NULL));
    CHECK(LookupEquals(table, NULL, L"发送", L"Send"));
}

TEST_CASE("LD8 损坏输入不崩溃，只跳过坏条目", "[langdll]")
{
    const std::string good =
        u8"\n"
        u8"msgid \"发送\"\n"
        u8"msgstr \"Send\"\n";

    SECTION("缺右引号")
    {
        CLangTable table;
        PoParseResult parsed = ParseAndBuild(
            u8"msgid \"坏条目\n"
            u8"msgstr \"Bad\"\n" + good, table);
        CHECK(parsed.m_errors == 1);
        CHECK(parsed.m_firstErrorLine == 1);
        CHECK(LookupEquals(table, NULL, L"坏条目", NULL));
        CHECK(LookupEquals(table, NULL, L"发送", L"Send"));
    }

    SECTION("有 msgid 没有 msgstr")
    {
        CLangTable table;
        PoParseResult parsed = ParseAndBuild(
            u8"msgid \"缺译文\"\n" + good, table);
        CHECK(parsed.m_errors == 1);
        CHECK(LookupEquals(table, NULL, L"发送", L"Send"));
    }

    SECTION("未知关键字")
    {
        CLangTable table;
        PoParseResult parsed = ParseAndBuild(
            u8"msgid \"未知\"\n"
            u8"msgfoo \"x\"\n"
            u8"msgstr \"Unknown\"\n" + good, table);
        CHECK(parsed.m_errors == 1);
        CHECK(parsed.m_firstErrorLine == 1);
        CHECK(LookupEquals(table, NULL, L"未知", NULL));
        CHECK(LookupEquals(table, NULL, L"发送", L"Send"));
    }

    SECTION("复数形式不支持，按坏条目处理")
    {
        CLangTable table;
        PoParseResult parsed = ParseAndBuild(
            u8"msgid \"一个文件\"\n"
            u8"msgid_plural \"{0} 个文件\"\n"
            u8"msgstr[0] \"One file\"\n"
            u8"msgstr[1] \"{0} files\"\n" + good, table);
        CHECK(parsed.m_errors == 1);
        CHECK(LookupEquals(table, NULL, L"一个文件", NULL));
        CHECK(LookupEquals(table, NULL, L"发送", L"Send"));
    }

    SECTION("未知转义")
    {
        CLangTable table;
        PoParseResult parsed = ParseAndBuild(
            u8"msgid \"转义\\q\"\n"
            u8"msgstr \"Escape\"\n" + good, table);
        CHECK(parsed.m_errors == 1);
        CHECK(LookupEquals(table, NULL, L"发送", L"Send"));
    }

    SECTION("中途截断")
    {
        CLangTable table;
        //good 占第 1 ~ 3 行，再空一行，截断的条目从第 5 行开始
        PoParseResult parsed = ParseAndBuild(good + u8"\nmsgid \"截断", table);
        CHECK(parsed.m_errors == 1);
        CHECK(parsed.m_firstErrorLine == 5);
        CHECK(LookupEquals(table, NULL, L"发送", L"Send"));
    }

    SECTION("任意二进制内容")
    {
        std::string garbage;
        for (int i = 0; i < 4096; ++i)
            garbage.push_back((char)((i * 131 + 7) & 0xFF));
        CLangTable table;
        PoParseResult parsed = ParseAndBuild(garbage, table);
        CHECK(parsed.m_errors > 0);
        CHECK(table.GetEntryCount() == 0);
    }

    SECTION("空输入与 NULL")
    {
        PoParseResult parsed;
        ParsePo(NULL, 0, parsed);
        CHECK(parsed.m_errors == 0);
        CHECK(parsed.m_entries.empty());
        ParsePo("", 0, parsed);
        CHECK(parsed.m_entries.empty());
    }
}

TEST_CASE("LD9 BOM、CRLF 与非法 UTF-8", "[langdll]")
{
    SECTION("带 BOM 与 CRLF 换行的文件")
    {
        CLangTable table;
        PoParseResult parsed = ParseAndBuild(
            "\xEF\xBB\xBF"
            u8"msgid \"取消\"\r\n"
            u8"msgstr \"Cancel\"\r\n"
            u8"\r\n"
            u8"msgid \"\"\r\n"
            u8"\"多行\"\r\n"
            u8"msgstr \"Multi\"\r\n", table);
        CHECK(parsed.m_errors == 0);
        CHECK(LookupEquals(table, NULL, L"取消", L"Cancel"));
        CHECK(LookupEquals(table, NULL, L"多行", L"Multi"));
    }

    SECTION("非法 UTF-8 的条目跳过，其余照常")
    {
        CLangTable table;
        ParseAndBuild(
            "msgid \"\xE8\xAE\"\n"
            "msgstr \"Broken\"\n"
            u8"\n"
            u8"msgid \"发送\"\n"
            u8"msgstr \"Send\"\n", table);
        CHECK(table.GetErrorCount() == 1);
        CHECK(table.GetEntryCount() == 1);
        CHECK(LookupEquals(table, NULL, L"发送", L"Send"));
    }
}

TEST_CASE("LD10 重复条目取第一条并计一次警告", "[langdll]")
{
    CLangTable table;
    PoParseResult parsed = ParseAndBuild(
        u8"msgid \"发送\"\n"
        u8"msgstr \"Send\"\n"
        u8"\n"
        u8"msgid \"发送\"\n"
        u8"msgstr \"Submit\"\n"
        u8"\n"
        u8"msgctxt \"按钮\"\n"
        u8"msgid \"发送\"\n"
        u8"msgstr \"Send it\"\n", table);

    CHECK(parsed.m_errors == 0);
    CHECK(table.GetWarningCount() == 1);
    CHECK(table.GetEntryCount() == 2);
    CHECK(LookupEquals(table, NULL, L"发送", L"Send"));
    CHECK(LookupEquals(table, L"按钮", L"发送", L"Send it"));
}

TEST_CASE("LD11 哈希冲突时各条仍查到各自的译文", "[langdll]")
{
    CLangTable table(CollidingHash);
    ParseAndBuild(
        u8"msgid \"发送\"\n"
        u8"msgstr \"Send\"\n"
        u8"\n"
        u8"msgid \"取消\"\n"
        u8"msgstr \"Cancel\"\n"
        u8"\n"
        u8"msgctxt \"按钮\"\n"
        u8"msgid \"发送\"\n"
        u8"msgstr \"Send it\"\n", table);

    CHECK(table.GetEntryCount() == 3);
    CHECK(table.GetWarningCount() == 0);
    CHECK(LookupEquals(table, NULL, L"发送", L"Send"));
    CHECK(LookupEquals(table, NULL, L"取消", L"Cancel"));
    CHECK(LookupEquals(table, L"按钮", L"发送", L"Send it"));
    CHECK(LookupEquals(table, NULL, L"不存在", NULL));
    CHECK(LookupEquals(table, L"按钮", L"取消", NULL));

    //默认哈希：NULL 上下文与空串上下文结果相同，有上下文时结果不同
    CHECK(flamingolang::DefaultLangHash(NULL, L"发送") == flamingolang::DefaultLangHash(L"", L"发送"));
    CHECK(flamingolang::DefaultLangHash(L"按钮", L"发送") != flamingolang::DefaultLangHash(NULL, L"发送"));
}
