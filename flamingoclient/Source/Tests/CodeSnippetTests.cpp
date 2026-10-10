/**
 *  聊天「代码片段」的着色、识别与卡片排版单测（Common/Code/CodeLexer、UI/BalloonChatWnd/ChatCodeBlock），CodeSnippetTests.cpp
 *
 *  [codelex] 着色与语言识别（纯逻辑）：
 *    CL1  协议标识与别名互相转换，不认识的标识按纯文本；
 *    CL2  C/C++：预处理指令、头文件名、类型、函数名、行注释、关键字、十六进制数、字符字面量；
 *    CL3  块注释可以跨行；没有闭合的字符串只着色到行尾，下一行照常着色；
 *    CL4  Python：装饰器、class 后的类型名、跨行的三引号字符串、函数名、科学计数法、f 字符串、# 注释；
 *    CL5  SQL：关键字不区分大小写、'' 表示一个单引号、-- 注释；
 *    CL6  Rust：'z' 是字符字面量，生命周期 'a 不是；
 *    CL7  JSON：键名与字符串值分开着色；
 *    CL8  XML：标签、属性名、属性值、注释，标签之间的文字不着色；
 *    CL9  Shell：$ 变量与 ${} 变量，a#b 里的 # 不是注释；
 *    CL10 任何文字按任何语言着色，输出都按起点升序、互不重叠、不越界；
 *    CL11 典型代码能识别出语言，中文正文与空串识别为纯文本；
 *    CL12 像不像代码：代码（含大量中文注释的代码）判为代码，中文正文、英文正文、一句话判为普通文字。
 *  [codeblock] 卡片排版（GDI 只用于测量，在内存设备上下文上完成）：
 *    CB1  换行规范化：\r\n、\r 改成 \n，去掉开头与末尾的空白行，第一行的缩进与行尾空格保留；
 *    CB2  Tab 按列展开到 4 的倍数列，中文占两列，展开后的字符能对应回原行下标；
 *    CB3  折行位置：按宽度折行、续行宽度不同、优先在空格之后折行、单个字符比宽度还宽时也能前进；
 *    CB4  排版保持格式：各行文字完整、缩进按列对齐、中文改用回退字体、着色种类正确；
 *    CB5  超过 15 行时折叠并给出底栏，展开后显示全部行；不超过时不折叠；
 *    CB6  长行：开启折行时不越出代码区、续行比首行缩进更多；关闭折行时只有一行并标出被截断；
 *    CB7  复制的文字：只把换行改成 \r\n，Tab 原样保留；
 *    CB8  点击判断：「复制」、底栏、卡片内其它位置与卡片外；关闭行号时代码区左移。
 *    CB9  聊天窗口径：底栏写「查看全部」且不在原地展开；切换「已复制」字样不改点击区。
 *    CB10 批注：只标出显示出来的被批注的行（跨过折叠处的截到折叠处），顶部说明带批注条数，卡片高度不变。
 *    CB11 追加批注（docs/代码片段方案.md 第 11.3 节）：不同作者在同一行的批注都标出；顶部说明写出其中几条是新的，
 *         没有新批注时与原来相同；卡片高度不变。
 *
 *  标签 [codelex][codeblock]。框架 Catch2 v3。全局 _Module 由 BalloonChatLayoutTests.cpp 统一定义。
 *  balloonwj@qq.com   2026-10-06
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <string>
#include <vector>

#include "../Common/Code/CodeLexer.h"
#include "../UI/BalloonChatWnd/ChatCodeBlock.h"

using namespace codesnippet;
using namespace balloonchatwnd;

namespace {

//code 里第 occurrence 次（从 0 起）出现的 needle 是否恰好是一个 kind 种类的着色区间
bool TokenIs(const std::wstring& code, const std::vector<CodeToken>& tokens, const std::wstring& needle, int kind,
             int occurrence = 0)
{
    size_t pos = code.find(needle);
    for (int i = 0; i < occurrence && pos != std::wstring::npos; ++i)
        pos = code.find(needle, pos + 1);
    if (pos == std::wstring::npos)
        return false;
    for (size_t i = 0; i < tokens.size(); ++i)
    {
        if (static_cast<size_t>(tokens[i].m_begin) == pos && static_cast<size_t>(tokens[i].m_length) == needle.size())
            return tokens[i].m_kind == kind;
    }
    return false;
}

//code 里第一次出现的 needle 的首字符落在哪种着色区间里；不在任何区间里时为 code_token_plain
int KindAt(const std::wstring& code, const std::vector<CodeToken>& tokens, const std::wstring& needle)
{
    const size_t pos = code.find(needle);
    if (pos == std::wstring::npos)
        return -1;
    for (size_t i = 0; i < tokens.size(); ++i)
    {
        const size_t begin = static_cast<size_t>(tokens[i].m_begin);
        if (pos >= begin && pos < begin + static_cast<size_t>(tokens[i].m_length))
            return tokens[i].m_kind;
    }
    return code_token_plain;
}

//去掉所有空格
std::wstring WithoutSpaces(const std::wstring& s)
{
    std::wstring out;
    for (size_t i = 0; i < s.size(); ++i)
    {
        if (s[i] != L' ')
            out += s[i];
    }
    return out;
}

//各语言的典型代码，CL10、CL11 共用
struct DetectSample
{
    int            m_lang;   //期望识别出的语言
    const wchar_t* m_code;   //代码
};

const DetectSample kDetectSamples[] =
{
    { code_lang_cpp,
      L"#include <vector>\n#include <string>\n\n// 求和\nint Sum(const std::vector<int>& v)\n{\n    int total = 0;\n"
      L"    for (size_t i = 0; i < v.size(); ++i)\n        total += v[i];\n    return total;\n}\n" },
    { code_lang_python,
      L"import os\n\ndef walk(root):\n    for name in os.listdir(root):\n        if name.startswith(\".\"):\n"
      L"            continue\n        print(name)\n" },
    { code_lang_sql, L"SELECT id, name FROM t_user WHERE age > 18 ORDER BY id;" },
    { code_lang_json, L"{\n  \"a\": 1,\n  \"b\": [true, false]\n}" },
    { code_lang_shell, L"#!/bin/bash\nfor f in *.log; do\n  gzip \"$f\"\ndone\n" },
    { code_lang_javascript, L"const add = (a, b) => a + b;\nconsole.log(add(1, 2));\n" },
    { code_lang_go, L"package main\n\nimport \"fmt\"\n\nfunc main() {\n\tfmt.Println(\"hi\")\n}\n" },
    { code_lang_xml, L"<root>\n  <item id=\"1\"/>\n</root>" },
    { code_lang_java,
      L"public class Main {\n    public static void main(String[] args) {\n        System.out.println(\"hi\");\n    }\n}\n" },
    { code_lang_csharp,
      L"using System;\n\nclass P\n{\n    static void Main()\n    {\n        Console.WriteLine(\"hi\");\n    }\n}\n" },
    { code_lang_rust, L"use std::io;\n\nfn main() {\n    let mut s = String::new();\n    println!(\"{}\", s);\n}\n" },
    { code_lang_lua, L"local t = {}\nfor i, v in ipairs(t) do\n  print(v)\nend\n" },
    { code_lang_php, L"<?php\n$name = $_GET['n'];\necho \"Hello $name\";\n" },
};

//含大量中文注释、本身识别不出语言的 C 风格代码（CL12）
const wchar_t* const kCodeWithChineseComments =
    L"// 计算用户的在线时长\n"
    L"// 单位为秒，结果四舍五入\n"
    L"int OnlineSeconds(const Session& s)\n"
    L"{\n"
    L"    return (int)(s.lastActive - s.loginTime);   // 两个时间戳相减\n"
    L"}\n";

//按测试需要的选项排版一段代码
CodeBlockLayout LayoutFor(const std::wstring& code, int lang, const CodeBlockOptions& options, int maxWidth)
{
    CodeBlockLayout layout;
    HDC hdc = ::CreateCompatibleDC(NULL);
    REQUIRE(hdc != NULL);
    LayoutCodeBlock(hdc, PrepareCodeBlockContent(code, lang), options, maxWidth, layout);
    ::DeleteDC(hdc);
    return layout;
}

//某一视觉行上的全部文字段，按横坐标排列（排版结果本来就按行、按横坐标升序）
std::vector<CodeBlockRun> RunsOfRow(const CodeBlockLayout& layout, int row)
{
    std::vector<CodeBlockRun> runs;
    for (size_t i = 0; i < layout.m_runs.size(); ++i)
    {
        if (layout.m_runs[i].m_row == row)
            runs.push_back(layout.m_runs[i]);
    }
    return runs;
}

//宽度足够、不折行时排版测试用的卡片最大宽度（像素）
const int kWideCard = 2000;

} // anonymous namespace

TEST_CASE("CL1 language ids and aliases round-trip", "[codelex]")
{
    for (int lang = 0; lang < code_lang_count; ++lang)
        CHECK(CodeLanguageFromId(CodeLanguageId(lang)) == lang);
    CHECK(CodeLanguageFromId("c") == code_lang_cpp);
    CHECK(CodeLanguageFromId("H") == code_lang_cpp);
    CHECK(CodeLanguageFromId("C++") == code_lang_cpp);
    CHECK(CodeLanguageFromId("py") == code_lang_python);
    CHECK(CodeLanguageFromId("ts") == code_lang_javascript);
    CHECK(CodeLanguageFromId("sh") == code_lang_shell);
    CHECK(CodeLanguageFromId("html") == code_lang_xml);
    CHECK(CodeLanguageFromId("") == code_lang_plain);
    CHECK(CodeLanguageFromId("cobol") == code_lang_plain);
    CHECK(std::string(CodeLanguageId(-1)) == "text");
    CHECK(std::string(CodeLanguageId(code_lang_count)) == "text");
}

TEST_CASE("CL2 C++ directives, types, functions, comments, numbers, chars", "[codelex]")
{
    const std::wstring code = L"#include <stdio.h>\nint main() // \x5165\x53E3\n{\n\treturn 0x1F + 'a';\n}\n";
    std::vector<CodeToken> tokens;
    TokenizeCode(code, code_lang_cpp, tokens);
    CHECK(TokenIs(code, tokens, L"#include", code_token_preprocessor));
    CHECK(TokenIs(code, tokens, L"<stdio.h>", code_token_string));
    CHECK(TokenIs(code, tokens, L"int", code_token_type));
    CHECK(TokenIs(code, tokens, L"main", code_token_function));
    CHECK(TokenIs(code, tokens, L"// \x5165\x53E3", code_token_comment));
    CHECK(TokenIs(code, tokens, L"return", code_token_keyword));
    CHECK(TokenIs(code, tokens, L"0x1F", code_token_number));
    CHECK(TokenIs(code, tokens, L"'a'", code_token_string));
    CHECK(KindAt(code, tokens, L"+") == code_token_plain);
}

TEST_CASE("CL3 block comments span lines, unterminated strings stop at the line end", "[codelex]")
{
    const std::wstring code = L"/* a\nb */ int x = \"abc\nint y;";
    std::vector<CodeToken> tokens;
    TokenizeCode(code, code_lang_cpp, tokens);
    CHECK(TokenIs(code, tokens, L"/* a\nb */", code_token_comment));
    CHECK(TokenIs(code, tokens, L"\"abc", code_token_string));
    CHECK(TokenIs(code, tokens, L"int", code_token_type, 1));
}

TEST_CASE("CL4 Python decorators, class names, triple quotes, f-strings", "[codelex]")
{
    const std::wstring code =
        L"@dataclass\n"
        L"class Foo(Base):\n"
        L"    \"\"\"doc\n"
        L"    more\"\"\"\n"
        L"    def bar(self, x=1.5e-3):\n"
        L"        return f\"v={x}\"  # \x6CE8\x91CA\n";
    std::vector<CodeToken> tokens;
    TokenizeCode(code, code_lang_python, tokens);
    CHECK(TokenIs(code, tokens, L"@dataclass", code_token_preprocessor));
    //第一次出现的 class 在 @dataclass 里，取第二次
    CHECK(TokenIs(code, tokens, L"class", code_token_keyword, 1));
    CHECK(TokenIs(code, tokens, L"Foo", code_token_type));
    CHECK(KindAt(code, tokens, L"Base") == code_token_plain);
    CHECK(TokenIs(code, tokens, L"\"\"\"doc\n    more\"\"\"", code_token_string));
    CHECK(TokenIs(code, tokens, L"def", code_token_keyword));
    CHECK(TokenIs(code, tokens, L"bar", code_token_function));
    CHECK(TokenIs(code, tokens, L"self", code_token_keyword));
    CHECK(TokenIs(code, tokens, L"1.5e-3", code_token_number));
    CHECK(TokenIs(code, tokens, L"f\"v={x}\"", code_token_string));
    CHECK(TokenIs(code, tokens, L"# \x6CE8\x91CA", code_token_comment));
}

TEST_CASE("CL5 SQL keywords ignore case, doubled quotes stay in the string", "[codelex]")
{
    const std::wstring code = L"select * FROM t WHERE a = 'it''s' -- c";
    std::vector<CodeToken> tokens;
    TokenizeCode(code, code_lang_sql, tokens);
    CHECK(TokenIs(code, tokens, L"select", code_token_keyword));
    CHECK(TokenIs(code, tokens, L"FROM", code_token_keyword));
    CHECK(TokenIs(code, tokens, L"WHERE", code_token_keyword));
    CHECK(TokenIs(code, tokens, L"'it''s'", code_token_string));
    CHECK(TokenIs(code, tokens, L"-- c", code_token_comment));
}

TEST_CASE("CL6 Rust char literals versus lifetimes", "[codelex]")
{
    const std::wstring code = L"fn f<'a>(x: &'a str) -> char { 'z' }";
    std::vector<CodeToken> tokens;
    TokenizeCode(code, code_lang_rust, tokens);
    CHECK(TokenIs(code, tokens, L"fn", code_token_keyword));
    CHECK(TokenIs(code, tokens, L"'z'", code_token_string));
    CHECK(KindAt(code, tokens, L"'a>") == code_token_plain);
    CHECK(TokenIs(code, tokens, L"str", code_token_type));
    CHECK(TokenIs(code, tokens, L"char", code_token_type));
}

TEST_CASE("CL7 JSON keys and string values", "[codelex]")
{
    const std::wstring code = L"{\"k\": \"v\", \"n\": 1, \"b\": true}";
    std::vector<CodeToken> tokens;
    TokenizeCode(code, code_lang_json, tokens);
    CHECK(TokenIs(code, tokens, L"\"k\"", code_token_attr));
    CHECK(TokenIs(code, tokens, L"\"v\"", code_token_string));
    CHECK(TokenIs(code, tokens, L"1", code_token_number));
    CHECK(TokenIs(code, tokens, L"true", code_token_keyword));
}

TEST_CASE("CL8 XML tags, attributes, values and comments", "[codelex]")
{
    const std::wstring code = L"<a href=\"x\">t</a><!-- c -->";
    std::vector<CodeToken> tokens;
    TokenizeCode(code, code_lang_xml, tokens);
    CHECK(TokenIs(code, tokens, L"<a", code_token_tag));
    CHECK(TokenIs(code, tokens, L"href", code_token_attr));
    CHECK(TokenIs(code, tokens, L"\"x\"", code_token_string));
    CHECK(KindAt(code, tokens, L"t</") == code_token_plain);
    CHECK(TokenIs(code, tokens, L"</a", code_token_tag));
    CHECK(TokenIs(code, tokens, L"<!-- c -->", code_token_comment));
}

TEST_CASE("CL9 Shell variables and comments", "[codelex]")
{
    const std::wstring code = L"echo $HOME ${x} a#b # c";
    std::vector<CodeToken> tokens;
    TokenizeCode(code, code_lang_shell, tokens);
    CHECK(TokenIs(code, tokens, L"echo", code_token_keyword));
    CHECK(TokenIs(code, tokens, L"$HOME", code_token_variable));
    CHECK(TokenIs(code, tokens, L"${x}", code_token_variable));
    CHECK(KindAt(code, tokens, L"#b") == code_token_plain);
    CHECK(TokenIs(code, tokens, L"# c", code_token_comment));
}

TEST_CASE("CL10 tokens are sorted, disjoint and in range for any text and any language", "[codelex]")
{
    std::vector<std::wstring> texts;
    for (size_t i = 0; i < sizeof(kDetectSamples) / sizeof(kDetectSamples[0]); ++i)
        texts.push_back(kDetectSamples[i].m_code);
    texts.push_back(kCodeWithChineseComments);
    //没有闭合的各种写法
    texts.push_back(L"/* open comment \"x\n'y");
    texts.push_back(L"\"\"\"never closed\n'''");
    texts.push_back(L"R\"delim(raw never closed");
    texts.push_back(L"--[[ lua [==[ x");
    texts.push_back(L"<a b='c");
    texts.push_back(L"$ ${ @ # #! ' ` \\");
    for (size_t t = 0; t < texts.size(); ++t)
    {
        for (int lang = 0; lang < code_lang_count; ++lang)
        {
            std::vector<CodeToken> tokens;
            TokenizeCode(texts[t], lang, tokens);
            int lastEnd = 0;
            for (size_t i = 0; i < tokens.size(); ++i)
            {
                CHECK(tokens[i].m_length > 0);
                CHECK(tokens[i].m_begin >= lastEnd);
                CHECK(tokens[i].m_kind > code_token_plain);
                CHECK(tokens[i].m_kind < code_token_kind_count);
                lastEnd = tokens[i].m_begin + tokens[i].m_length;
                CHECK(static_cast<size_t>(lastEnd) <= texts[t].size());
            }
        }
    }
}

TEST_CASE("CL11 typical code is detected, prose and empty text are plain", "[codelex]")
{
    for (size_t i = 0; i < sizeof(kDetectSamples) / sizeof(kDetectSamples[0]); ++i)
    {
        INFO("sample " << i << " expects " << CodeLanguageId(kDetectSamples[i].m_lang));
        CHECK(DetectCodeLanguage(kDetectSamples[i].m_code) == kDetectSamples[i].m_lang);
    }
    //「今天下午三点开会，请大家准时参加。\n会议内容：讨论下个版本的计划。」
    CHECK(DetectCodeLanguage(L"\x4ECA\x5929\x4E0B\x5348\x4E09\x70B9\x5F00\x4F1A\xFF0C\x8BF7\x5927\x5BB6\x51C6\x65F6"
                             L"\x53C2\x52A0\x3002\n\x4F1A\x8BAE\x5185\x5BB9\xFF1A\x8BA8\x8BBA\x4E0B\x4E2A\x7248"
                             L"\x672C\x7684\x8BA1\x5212\x3002") == code_lang_plain);
    CHECK(DetectCodeLanguage(L"") == code_lang_plain);
    CHECK(DetectCodeLanguage(L"   \n\t\n") == code_lang_plain);
}

TEST_CASE("CL12 looks like code", "[codelex]")
{
    for (size_t i = 0; i < sizeof(kDetectSamples) / sizeof(kDetectSamples[0]); ++i)
    {
        INFO("sample " << i);
        CHECK(LooksLikeCode(kDetectSamples[i].m_code));
    }
    CHECK(LooksLikeCode(kCodeWithChineseComments));
    //中文正文：「今天下午三点开会，\n请大家准时参加，\n不要迟到。」
    CHECK_FALSE(LooksLikeCode(L"\x4ECA\x5929\x4E0B\x5348\x4E09\x70B9\x5F00\x4F1A\xFF0C\n\x8BF7\x5927\x5BB6\x51C6"
                              L"\x65F6\x53C2\x52A0\xFF0C\n\x4E0D\x8981\x8FDF\x5230\x3002"));
    CHECK_FALSE(LooksLikeCode(L"Hello team,\nThe meeting is at 3pm today.\nPlease be on time."));
    CHECK_FALSE(LooksLikeCode(L"\x597D\x7684"));
    CHECK_FALSE(LooksLikeCode(L""));
}

TEST_CASE("CB1 newline normalization keeps indentation", "[codeblock]")
{
    CHECK(NormalizeCodeNewlines(L"a\r\nb\rc\n") == L"a\nb\nc");
    CHECK(NormalizeCodeNewlines(L"\n\n    int x;\n\n  \n") == L"    int x;");
    CHECK(NormalizeCodeNewlines(L"\t\tfoo();  \n\tbar();") == L"\t\tfoo();  \n\tbar();");
    CHECK(NormalizeCodeNewlines(L"x  ") == L"x  ");
    CHECK(NormalizeCodeNewlines(L"a\n\n\nb") == L"a\n\n\nb");
    CHECK(NormalizeCodeNewlines(L" \t\r\n ") == L"");
    CHECK(NormalizeCodeNewlines(L"") == L"");
}

TEST_CASE("CB2 tabs expand to columns", "[codeblock]")
{
    std::vector<int> src;
    CHECK(ExpandCodeTabs(L"\tA", 4, &src) == L"    A");
    REQUIRE(src.size() == 5);
    CHECK(src[0] == 0);
    CHECK(src[3] == 0);
    CHECK(src[4] == 1);
    CHECK(ExpandCodeTabs(L"ab\tc", 4, NULL) == L"ab  c");
    CHECK(ExpandCodeTabs(L"abcd\te", 4, NULL) == L"abcd    e");
    //中文占两列：「中」之后的 Tab 只补两个空格
    CHECK(ExpandCodeTabs(L"\x4E2D\tx", 4, NULL) == L"\x4E2D  x");
    CHECK(ExpandCodeTabs(L"\t\t", 2, NULL) == L"    ");
    CHECK(ExpandCodeTabs(L"a\tb", 0, NULL) == L"a b");
    CHECK(CodeCharColumns(L'a') == 1);
    CHECK(CodeCharColumns(0x4E2D) == 2);
    CHECK(CodeCharColumns(0xFF01) == 2);
}

TEST_CASE("CB3 wrap positions", "[codeblock]")
{
    //每个字符宽 10 像素
    std::vector<int> right;
    for (int i = 1; i <= 10; ++i)
        right.push_back(i * 10);
    const std::wstring text = L"abcdefghij";
    std::vector<int> starts;

    ComputeCodeWrapStarts(right, text, 35, 35, starts);
    REQUIRE(starts.size() == 4);
    CHECK(starts[1] == 3);
    CHECK(starts[2] == 6);
    CHECK(starts[3] == 9);

    //续行更窄
    ComputeCodeWrapStarts(right, text, 35, 25, starts);
    REQUIRE(starts.size() == 5);
    CHECK(starts[1] == 3);
    CHECK(starts[2] == 5);
    CHECK(starts[3] == 7);
    CHECK(starts[4] == 9);

    //优先在空格之后折行：「aaaa bbbb」宽 70 能放 7 个字符，空格之后的位置 50 不少于 70 的六成
    ComputeCodeWrapStarts(right, L"aaaa bbbbb", 70, 70, starts);
    REQUIRE(starts.size() == 2);
    CHECK(starts[1] == 5);

    //空格太靠前时不迁就空格：「a bbbbbbbb」空格之后的位置 20 不到 70 的六成，按字符折行
    ComputeCodeWrapStarts(right, L"a bbbbbbbb", 70, 70, starts);
    REQUIRE(starts.size() == 2);
    CHECK(starts[1] == 7);

    //单个字符比宽度还宽：每行一个字符，照样前进
    ComputeCodeWrapStarts(right, text, 5, 5, starts);
    CHECK(starts.size() == 10);

    //空行
    ComputeCodeWrapStarts(std::vector<int>(), std::wstring(), 100, 100, starts);
    REQUIRE(starts.size() == 1);
    CHECK(starts[0] == 0);
}

TEST_CASE("CB4 layout keeps text, indentation, fallback font and colors", "[codeblock]")
{
    const std::wstring code = L"int a;\n\tif (x)\n\t\treturn \"\x4E2D\x6587\";";
    CodeBlockOptions options;
    options.m_wrap = false;
    const CodeBlockLayout layout = LayoutFor(code, code_lang_cpp, options, kWideCard);
    REQUIRE(layout.m_rowCount == 3);
    REQUIRE(layout.m_totalLines == 3);
    REQUIRE(layout.m_lineFirstRow.size() == 3);
    CHECK(layout.m_overflowRows.empty());

    //各行的文字完整（排版时省略了纯空格段，比较时去掉空格）
    const std::wstring expected[3] = { L"int a;", L"if (x)", L"return \"\x4E2D\x6587\";" };
    int firstX[3] = { 0, 0, 0 };
    for (int row = 0; row < 3; ++row)
    {
        const std::vector<CodeBlockRun> runs = RunsOfRow(layout, row);
        REQUIRE_FALSE(runs.empty());
        std::wstring joined;
        for (size_t i = 0; i < runs.size(); ++i)
        {
            joined += runs[i].m_text;
            if (i > 0)
                CHECK(runs[i].m_x >= runs[i - 1].m_x + runs[i - 1].m_width);
        }
        CHECK(WithoutSpaces(joined) == WithoutSpaces(expected[row]));
        firstX[row] = runs[0].m_x;
    }

    //缩进按列对齐：第一行顶格，第二行一个 Tab（4 列），第三行两个 Tab（8 列）
    CHECK(firstX[0] == layout.m_textLeft);
    CHECK(firstX[1] > firstX[0]);
    CHECK(firstX[2] - layout.m_textLeft == 2 * (firstX[1] - layout.m_textLeft));

    //中文用回退字体画，并且仍是字符串的颜色；关键字与类型着色正确
    bool sawChinese = false;
    for (size_t i = 0; i < layout.m_runs.size(); ++i)
    {
        const CodeBlockRun& run = layout.m_runs[i];
        if (run.m_text == L"\x4E2D\x6587")
        {
            sawChinese = true;
            CHECK(run.m_fallback);
            CHECK(run.m_kind == code_token_string);
        }
        else
        {
            CHECK_FALSE(run.m_fallback);
        }
        if (run.m_text == L"return" || run.m_text == L"if")
            CHECK(run.m_kind == code_token_keyword);
        if (run.m_text == L"int")
            CHECK(run.m_kind == code_token_type);
    }
    CHECK(sawChinese);
}

TEST_CASE("CB5 long code collapses to 15 lines with a footer", "[codeblock]")
{
    std::wstring code;
    for (int i = 1; i <= 20; ++i)
        code += L"line " + std::to_wstring(i) + L"\n";
    CodeBlockOptions options;
    const CodeBlockLayout collapsed = LayoutFor(code, code_lang_plain, options, kWideCard);
    CHECK(collapsed.m_totalLines == 20);
    CHECK(collapsed.m_collapsible);
    CHECK(collapsed.m_collapsed);
    CHECK(collapsed.m_shownLines == 15);
    CHECK(collapsed.m_lineFirstRow.size() == 15);
    CHECK_FALSE(::IsRectEmpty(&collapsed.m_footerRect));
    CHECK_FALSE(collapsed.m_footerText.empty());

    options.m_expanded = true;
    const CodeBlockLayout expanded = LayoutFor(code, code_lang_plain, options, kWideCard);
    CHECK(expanded.m_collapsible);
    CHECK_FALSE(expanded.m_collapsed);
    CHECK(expanded.m_shownLines == 20);
    CHECK(expanded.m_size.cy > collapsed.m_size.cy);
    CHECK(expanded.m_footerText != collapsed.m_footerText);

    std::wstring shortCode;
    for (int i = 1; i <= 15; ++i)
        shortCode += L"line\n";
    const CodeBlockLayout fits = LayoutFor(shortCode, code_lang_plain, CodeBlockOptions(), kWideCard);
    CHECK_FALSE(fits.m_collapsible);
    CHECK(fits.m_shownLines == 15);
    CHECK(::IsRectEmpty(&fits.m_footerRect));
}

TEST_CASE("CB6 long lines wrap inside the card or are marked as cut", "[codeblock]")
{
    const std::wstring code = L"    " + std::wstring(200, L'x');
    const int maxWidth = 300;
    CodeBlockOptions options;
    options.m_lineNumbers = false;
    const CodeBlockLayout wrapped = LayoutFor(code, code_lang_plain, options, maxWidth);
    CHECK(wrapped.m_rowCount > 1);
    CHECK(wrapped.m_size.cx <= maxWidth);
    for (size_t i = 0; i < wrapped.m_runs.size(); ++i)
        CHECK(wrapped.m_runs[i].m_x + wrapped.m_runs[i].m_width <= wrapped.m_textRight);
    const std::vector<CodeBlockRun> row0 = RunsOfRow(wrapped, 0);
    const std::vector<CodeBlockRun> row1 = RunsOfRow(wrapped, 1);
    REQUIRE_FALSE(row0.empty());
    REQUIRE_FALSE(row1.empty());
    //首行的文字段从行首开始（含 4 个空格的缩进），续行缩进到原缩进再加两列
    CHECK(row1[0].m_x > row0[0].m_x);
    CHECK(WithoutSpaces(row1[0].m_text).size() == row1[0].m_text.size());

    options.m_wrap = false;
    const CodeBlockLayout cut = LayoutFor(code, code_lang_plain, options, maxWidth);
    CHECK(cut.m_rowCount == 1);
    REQUIRE(cut.m_overflowRows.size() == 1);
    CHECK(cut.m_overflowRows[0] == 0);
    CHECK(cut.m_size.cx == maxWidth);
}

TEST_CASE("CB7 clipboard text keeps tabs and uses CRLF", "[codeblock]")
{
    CHECK(CodeTextForClipboard(L"a\n\tb\n") == L"a\r\n\tb\r\n");
    const CodeBlockContent content = PrepareCodeBlockContent(L"\tint x;\r\n\r\n\treturn x;\r\n", code_lang_cpp);
    CHECK(content.m_lineCount == 3);
    CHECK(CodeTextForClipboard(content.m_code) == L"\tint x;\r\n\r\n\treturn x;");
}

TEST_CASE("CB8 hit testing and line number gutter", "[codeblock]")
{
    std::wstring code;
    for (int i = 1; i <= 20; ++i)
        code += L"x = " + std::to_wstring(i) + L";\n";
    const CodeBlockLayout layout = LayoutFor(code, code_lang_cpp, CodeBlockOptions(), kWideCard);

    POINT copy = { (layout.m_copyRect.left + layout.m_copyRect.right) / 2,
                   (layout.m_copyRect.top + layout.m_copyRect.bottom) / 2 };
    CHECK(HitTestCodeBlock(layout, copy) == kCodePartCopy);
    POINT footer = { (layout.m_footerRect.left + layout.m_footerRect.right) / 2,
                     (layout.m_footerRect.top + layout.m_footerRect.bottom) / 2 };
    CHECK(HitTestCodeBlock(layout, footer) == kCodePartFooter);
    POINT body = { layout.m_textLeft + 1, layout.m_bodyTop + 1 };
    CHECK(HitTestCodeBlock(layout, body) == kCodePartBody);
    POINT outside = { layout.m_size.cx + 1, layout.m_size.cy + 1 };
    CHECK(HitTestCodeBlock(layout, outside) == kCodePartNone);

    CHECK(layout.m_gutterRight > 0);
    CodeBlockOptions noNumbers;
    noNumbers.m_lineNumbers = false;
    const CodeBlockLayout plain = LayoutFor(code, code_lang_cpp, noNumbers, kWideCard);
    CHECK(plain.m_gutterRight == 0);
    CHECK(plain.m_textLeft < layout.m_textLeft);
}

TEST_CASE("CB9 chat footer opens the viewer and the copied label does not move the hit area", "[codeblock]")
{
    std::wstring code;
    for (int i = 1; i <= 20; ++i)
        code += L"x = " + std::to_wstring(i) + L";\n";
    CodeBlockOptions options;
    options.m_footerOpensViewer = true;
    options.m_expanded = true;   //聊天窗口径下 m_expanded 不起作用
    CodeBlockLayout layout = LayoutFor(code, code_lang_cpp, options, kWideCard);
    CHECK(layout.m_collapsible);
    CHECK(layout.m_collapsed);
    CHECK(layout.m_shownLines == 15);
    CodeBlockOptions demo;
    const CodeBlockLayout demoLayout = LayoutFor(code, code_lang_cpp, demo, kWideCard);
    CHECK(layout.m_footerText != demoLayout.m_footerText);

    const RECT copyRect = layout.m_copyRect;
    const std::wstring copyText = layout.m_copyText;
    CHECK(SetCodeBlockCopied(layout, true));
    CHECK(layout.m_copied);
    CHECK(layout.m_copyText != copyText);
    CHECK(::EqualRect(&layout.m_copyRect, &copyRect));
    CHECK_FALSE(SetCodeBlockCopied(layout, true));
    CHECK(SetCodeBlockCopied(layout, false));
    CHECK(layout.m_copyText == copyText);
}

TEST_CASE("CB10 annotated lines are marked and counted in the header", "[codeblock]")
{
    std::wstring code;
    for (int i = 1; i <= 20; ++i)
        code += L"x = " + std::to_wstring(i) + L";\n";
    std::vector<CodeNote> notes;
    notes.push_back(CodeNote(2, 3, L"first"));
    notes.push_back(CodeNote(3, 4, L"overlaps the first, dropped"));
    notes.push_back(CodeNote(14, 17, L"crosses the fold"));
    notes.push_back(CodeNote(19, 19, L"below the fold"));

    CodeBlockOptions options;
    options.m_footerOpensViewer = true;   //聊天窗口径：只显示前 15 行
    CodeBlockLayout plain;
    CodeBlockLayout marked;
    HDC hdc = ::CreateCompatibleDC(NULL);
    REQUIRE(hdc != NULL);
    LayoutCodeBlock(hdc, PrepareCodeBlockContent(code, code_lang_cpp), options, kWideCard, plain);
    const CodeBlockContent content = PrepareCodeBlockContent(code, code_lang_cpp, notes);
    LayoutCodeBlock(hdc, content, options, kWideCard, marked);
    ::DeleteDC(hdc);

    //相交的那条被整理掉，剩三条；行号从 0 起
    REQUIRE(content.m_noteLines.size() == 3);
    CHECK(content.m_noteLines[0] == std::make_pair(1, 2));
    //只标显示出来的行：第一条是第 2～3 行；跨过折叠处的那条只标到第 15 行；折叠处以下的那条不标
    REQUIRE(marked.m_markedRows.size() == 2);
    CHECK(marked.m_markedRows[0] == std::make_pair(1, 2));
    CHECK(marked.m_markedRows[1] == std::make_pair(13, 14));
    //顶部的说明带上批注条数，卡片高度与不带批注时相同
    CHECK(marked.m_info != plain.m_info);
    CHECK(marked.m_info.find(L"3") != std::wstring::npos);
    CHECK(marked.m_size.cy == plain.m_size.cy);
    CHECK(plain.m_markedRows.empty());
}

TEST_CASE("CB11 the header counts new notes", "[codeblock]")
{
    const std::wstring code = L"a();\nb();\nc();\nd();";
    //第 1 行上有两位作者的批注；两条是这一轮新加的
    std::vector<CodeNote> notes;
    notes.push_back(CodeNote(1, 1, L"old", 100));
    notes.push_back(CodeNote(1, 2, L"new by another author", 200, true));
    notes.push_back(CodeNote(3, 3, L"new too", 100, true));
    std::vector<CodeNote> oldOnly = notes;
    for (size_t i = 0; i < oldOnly.size(); ++i)
        oldOnly[i].m_isNew = false;

    const CodeBlockContent content = PrepareCodeBlockContent(code, code_lang_cpp, notes);
    CHECK(content.m_noteLines.size() == 3);
    CHECK(content.m_newNoteCount == 2);

    CodeBlockOptions options;
    CodeBlockLayout withNew;
    CodeBlockLayout withoutNew;
    HDC hdc = ::CreateCompatibleDC(NULL);
    REQUIRE(hdc != NULL);
    LayoutCodeBlock(hdc, content, options, kWideCard, withNew);
    LayoutCodeBlock(hdc, PrepareCodeBlockContent(code, code_lang_cpp, oldOnly), options, kWideCard, withoutNew);
    ::DeleteDC(hdc);
    //测试进程不加载语言包，TRF 按原文填参数
    CHECK(withNew.m_info == L"4 行 · 3 条批注，其中 2 条是新的");
    CHECK(withoutNew.m_info == L"4 行 · 3 条批注");
    CHECK(withNew.m_size.cy == withoutNew.m_size.cy);
    //第 1～2 行被标出（两条批注在第 1 行相交），第 3 行被标出
    CHECK(withNew.m_markedRows.size() == 3);
}
