/**
 *  代码格式化的单测（Common/Code/CodeFormatter，docs/代码片段方案.md 第 10.4 节），CodeFormatTests.cpp
 *
 *  [codeformat]
 *    CF1  通用整理：换行统一、去掉行尾空白与首尾空行、三行以上的空行压成一行、两行空行保留、去掉共同缩进
 *    CF2  C/C++ 按括号重新缩进：花括号、if / else、跨行的函数参数
 *    CF3  字符串、字符与注释里的括号不计数；跨行块注释的续行保持相对缩进
 *    CF4  预处理行顶格；case / default 标签与 switch 的花括号同一级，标签下的语句再缩一级
 *    CF5  一行里同时打开圆括号与花括号（JavaScript 回调）时里面只缩一级，「});」回到打开它们的那一行
 *    CF6  缩进单位：Tab、2 个空格、4 个空格，判断不出时 4 个空格；按 Tab 缩进的代码格式化后仍用 Tab
 *    CF7  JSON 美化：键的顺序、数字与字符串的写法不变，空对象与空数组写在一行；不合法时只做通用整理
 *    CF8  XML 按标签层数缩进：结束标签、自闭合标签、跨行注释；HTML 的空元素不加层
 *    CF9  SQL：关键字大写，子句分行，ON / WHERE 之后的 AND 另起一行并多缩一级，BETWEEN ... AND 不拆开
 *    CF10 SQL：SELECT 字段太长时每个字段一行；子查询缩进一级、右括号回到原来那一级；不认识的语句保留原有换行
 *    CF11 Python 只做通用整理，保持相对缩进
 *    CF12 选区格式化：结果加上第一行原有的缩进，选区首尾的空行保留；已经整齐的代码没有变化
 *
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-06
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <string>

#include "../Common/Code/CodeFormatter.h"
#include "../Common/Code/CodeLexer.h"

using namespace codesnippet;

TEST_CASE("CF1 general cleanup for every language", "[codeformat]")
{
    const CodeFormatResult r = FormatCode(L"\r\n\r\n    int a = 1;   \r\n\r\n\r\n\r\n    int b = 2;\t\r\n\r\n", code_lang_plain);
    CHECK(r.m_text == L"int a = 1;\n\nint b = 2;");
    CHECK(r.m_changedLines > 0);
    CHECK_FALSE(r.m_jsonInvalid);

    //两行空行保留
    CHECK(FormatCode(L"a\n\n\nb", code_lang_plain).m_text == L"a\n\n\nb");
    //全是空白
    CHECK(FormatCode(L"  \r\n\t\r\n", code_lang_cpp).m_text.empty());
}

TEST_CASE("CF2 C/C++ is re-indented by brackets", "[codeformat]")
{
    const std::wstring input =
        L"int main()\n"
        L"{\n"
        L"if (x)\n"
        L"{\n"
        L"foo(1,\n"
        L"2);\n"
        L"}\n"
        L"else {\n"
        L"bar();\n"
        L"}\n"
        L"return 0;\n"
        L"}";
    const std::wstring expected =
        L"int main()\n"
        L"{\n"
        L"    if (x)\n"
        L"    {\n"
        L"        foo(1,\n"
        L"            2);\n"
        L"    }\n"
        L"    else {\n"
        L"        bar();\n"
        L"    }\n"
        L"    return 0;\n"
        L"}";
    const CodeFormatResult r = FormatCode(input, code_lang_cpp);
    CHECK(r.m_text == expected);
    CHECK(r.m_changedLines == 9);
}

TEST_CASE("CF3 brackets in strings and comments are not counted", "[codeformat]")
{
    const std::wstring input =
        L"void f()\n"
        L"{\n"
        L"const char* s = \"{ not a brace\";\n"
        L"// } neither\n"
        L"/* {\n"
        L"   multi line */\n"
        L"call('}');\n"
        L"}";
    const std::wstring expected =
        L"void f()\n"
        L"{\n"
        L"    const char* s = \"{ not a brace\";\n"
        L"    // } neither\n"
        L"    /* {\n"
        L"       multi line */\n"
        L"    call('}');\n"
        L"}";
    CHECK(FormatCode(input, code_lang_cpp).m_text == expected);
}

TEST_CASE("CF4 preprocessor lines and case labels", "[codeformat]")
{
    const std::wstring input =
        L"int g(int x)\n"
        L"{\n"
        L"    #ifdef DEBUG\n"
        L"log(x);\n"
        L"#endif\n"
        L"switch (x)\n"
        L"{\n"
        L"case 1:\n"
        L"return 1;\n"
        L"default:\n"
        L"return 0;\n"
        L"}\n"
        L"}";
    const std::wstring expected =
        L"int g(int x)\n"
        L"{\n"
        L"#ifdef DEBUG\n"
        L"    log(x);\n"
        L"#endif\n"
        L"    switch (x)\n"
        L"    {\n"
        L"    case 1:\n"
        L"        return 1;\n"
        L"    default:\n"
        L"        return 0;\n"
        L"    }\n"
        L"}";
    CHECK(FormatCode(input, code_lang_cpp).m_text == expected);
}

TEST_CASE("CF5 a line opening two brackets indents its body once", "[codeformat]")
{
    const std::wstring input =
        L"foo(function () {\n"
        L"bar();\n"
        L"});";
    const std::wstring expected =
        L"foo(function () {\n"
        L"    bar();\n"
        L"});";
    CHECK(FormatCode(input, code_lang_javascript).m_text == expected);
}

TEST_CASE("CF6 indent unit follows the existing style", "[codeformat]")
{
    CHECK(DetectIndentUnit(L"a\n\tb\n\t\tc") == L"\t");
    CHECK(DetectIndentUnit(L"a\n  b\n    c") == L"  ");
    CHECK(DetectIndentUnit(L"a\n    b\n        c") == L"    ");
    CHECK(DetectIndentUnit(L"a\n   b") == L"    ");
    CHECK(DetectIndentUnit(L"a\nb") == L"    ");

    //按 Tab 缩进的代码格式化后仍用 Tab
    CHECK(FormatCode(L"void f() {\n\tx();\n\t\ty();\n}", code_lang_cpp).m_text == L"void f() {\n\tx();\n\ty();\n}");
    //按 2 个空格缩进的代码仍用 2 个空格
    CHECK(FormatCode(L"if (a) {\n  if (b) {\n      c();\n  }\n}", code_lang_javascript).m_text
          == L"if (a) {\n  if (b) {\n    c();\n  }\n}");
}

TEST_CASE("CF7 JSON is pretty printed keeping key order", "[codeformat]")
{
    const std::wstring input = L"{\"b\":1,\"a\":[1,2.50,{\"c\":null}],\"e\":{},\"f\":[],\"s\":\"x\\\"y\",\"t\":true}";
    const std::wstring expected =
        L"{\n"
        L"    \"b\": 1,\n"
        L"    \"a\": [\n"
        L"        1,\n"
        L"        2.50,\n"
        L"        {\n"
        L"            \"c\": null\n"
        L"        }\n"
        L"    ],\n"
        L"    \"e\": {},\n"
        L"    \"f\": [],\n"
        L"    \"s\": \"x\\\"y\",\n"
        L"    \"t\": true\n"
        L"}";
    const CodeFormatResult r = FormatCode(input, code_lang_json);
    CHECK(r.m_text == expected);
    CHECK_FALSE(r.m_jsonInvalid);

    //不合法：尾随逗号、拼错的字面量、多出来的内容
    const CodeFormatResult bad = FormatCode(L"  {\"a\": 1,}  ", code_lang_json);
    CHECK(bad.m_jsonInvalid);
    CHECK(bad.m_text == L"{\"a\": 1,}");
    CHECK(FormatCode(L"{\"a\": tru}", code_lang_json).m_jsonInvalid);
    CHECK(FormatCode(L"[1] [2]", code_lang_json).m_jsonInvalid);
}

TEST_CASE("CF8 XML and HTML are re-indented by tags", "[codeformat]")
{
    const std::wstring xml =
        L"<root>\n"
        L"<item id=\"1\">\n"
        L"<name>A</name>\n"
        L"<empty/>\n"
        L"</item>\n"
        L"<!-- comment\n"
        L"spanning -->\n"
        L"</root>";
    const std::wstring xmlExpected =
        L"<root>\n"
        L"    <item id=\"1\">\n"
        L"        <name>A</name>\n"
        L"        <empty/>\n"
        L"    </item>\n"
        L"    <!-- comment\n"
        L"    spanning -->\n"
        L"</root>";
    CHECK(FormatCode(xml, code_lang_xml).m_text == xmlExpected);

    const std::wstring html =
        L"<div>\n"
        L"<br>\n"
        L"<img src=\"a.png\">\n"
        L"<p>text</p>\n"
        L"</div>";
    const std::wstring htmlExpected =
        L"<div>\n"
        L"    <br>\n"
        L"    <img src=\"a.png\">\n"
        L"    <p>text</p>\n"
        L"</div>";
    CHECK(FormatCode(html, code_lang_xml).m_text == htmlExpected);
}

TEST_CASE("CF9 SQL keywords, clauses and conditions", "[codeformat]")
{
    const std::wstring input =
        L"select id, nick from t_user u left join t_dept d on u.dept_id = d.id and d.flag = 1 "
        L"where u.age between 18 and 30 and u.nick like 'a%' order by id;";
    const std::wstring expected =
        L"SELECT id, nick\n"
        L"FROM t_user u\n"
        L"LEFT JOIN t_dept d ON u.dept_id = d.id\n"
        L"    AND d.flag = 1\n"
        L"WHERE u.age BETWEEN 18 AND 30\n"
        L"    AND u.nick LIKE 'a%'\n"
        L"ORDER BY id;";
    CHECK(FormatCode(input, code_lang_sql).m_text == expected);
}

TEST_CASE("CF10 SQL select lists, subqueries and other statements", "[codeformat]")
{
    const std::wstring wide =
        L"select aaaaaaaaaa, bbbbbbbbbb, cccccccccc, dddddddddd, eeeeeeeeee, ffffffffff, gggggggggg from t";
    const std::wstring wideExpected =
        L"SELECT\n"
        L"    aaaaaaaaaa,\n"
        L"    bbbbbbbbbb,\n"
        L"    cccccccccc,\n"
        L"    dddddddddd,\n"
        L"    eeeeeeeeee,\n"
        L"    ffffffffff,\n"
        L"    gggggggggg\n"
        L"FROM t";
    CHECK(FormatCode(wide, code_lang_sql).m_text == wideExpected);

    const std::wstring sub = L"select id from t where id in (select uid from u where flag = 1)";
    const std::wstring subExpected =
        L"SELECT id\n"
        L"FROM t\n"
        L"WHERE id IN (\n"
        L"    SELECT uid\n"
        L"    FROM u\n"
        L"    WHERE flag = 1\n"
        L")";
    CHECK(FormatCode(sub, code_lang_sql).m_text == subExpected);

    //不认识的语句只把关键字转大写，保留原有的换行；与可识别的语句混在一起时各自处理
    const std::wstring mixed = L"select count(*) from t;\ncreate table x (\n  id int,\n  nick varchar(20)\n);";
    const std::wstring mixedExpected =
        L"SELECT COUNT(*)\n"
        L"FROM t;\n"
        L"CREATE TABLE x (\n"
        L"  id INT,\n"
        L"  nick VARCHAR(20)\n"
        L");";
    CHECK(FormatCode(mixed, code_lang_sql).m_text == mixedExpected);
}

TEST_CASE("CF11 Python is only cleaned up", "[codeformat]")
{
    CHECK(FormatCode(L"    def f():\n        return 1\n", code_lang_python).m_text == L"def f():\n    return 1");
}

TEST_CASE("CF12 formatting a selection keeps its context", "[codeformat]")
{
    const CodeFormatResult r = FormatCodeSelection(L"        foo(1,\n        2);\n", code_lang_cpp);
    CHECK(r.m_text == L"        foo(1,\n            2);\n");
    CHECK(r.m_changedLines == 1);

    //已经整齐的代码没有变化
    const CodeFormatResult same = FormatCode(L"int a;\nint b;", code_lang_cpp);
    CHECK(same.m_text == L"int a;\nint b;");
    CHECK(same.m_changedLines == 0);
}
