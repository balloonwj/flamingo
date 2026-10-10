/**
 *  输入区 Markdown 格式条编辑命令（UI/BalloonChatWnd/MarkdownEditCommands）的单元测试：格式命令 ME1 ~ ME17
 *  （docs/Markdown消息方案.md 第 8 节与 11.7 节），编辑器回车续项 ME18 ~ ME26（第 12 节），标题级别与快捷键 ME27 ~ ME32、下划线 ME33、清除样式 ME34、分割线 ME35、警告框 ME36
 *  （第 13 节）。纯函数，不需要窗口。
 *  框架 Catch2 v3，标签 [mdedit]。
 *
 *  balloonwj@qq.com   2026-10-06
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <string>
#include <vector>

#include "../UI/BalloonChatWnd/MarkdownEditCommands.h"
#include "../../FlamingoMarkdown/MdApiTable.h"   // ME17：解析插入后的文字，确认代码块没有并进表格
#include "../../FlamingoMarkdown/Core/MdBuilder.h"   // ME35：解析插入分割线后的文字，确认没有变成标题

using namespace balloonchatwnd;

namespace {

//执行命令（换行字符为 RichEdit 的 \r）
MarkdownEditResult Run(const std::wstring& text, int s, int e, int command, const std::wstring& placeholder = L"P")
{
    MarkdownEditInput in;
    in.m_text = text;
    in.m_selStart = s;
    in.m_selEnd = e;
    in.m_lineBreak = L'\r';
    in.m_placeholder = placeholder;
    in.m_tableHeader1 = L"H1";
    in.m_tableHeader2 = L"H2";
    return ApplyMarkdownEditCommand(in, command);
}

//替换区间与替换文字能还原出新全文
void CheckMinimalReplace(const std::wstring& oldText, const MarkdownEditResult& r)
{
    REQUIRE(r.m_replaceStart >= 0);
    REQUIRE(r.m_replaceEnd >= r.m_replaceStart);
    REQUIRE(r.m_replaceEnd <= static_cast<int>(oldText.size()));
    const std::wstring rebuilt = oldText.substr(0, r.m_replaceStart) + r.m_replacement + oldText.substr(r.m_replaceEnd);
    CHECK(rebuilt == r.m_newText);
    CHECK(r.m_selStart >= 0);
    CHECK(r.m_selEnd <= static_cast<int>(r.m_newText.size()));
}

//选中的文字
std::wstring Selected(const MarkdownEditResult& r)
{
    return r.m_newText.substr(r.m_selStart, r.m_selEnd - r.m_selStart);
}

//把编辑框里的文字交给 DLL 解析（函数表直接编进测试程序），返回其中的代码块个数；解析失败返回 -1
int CodeBlockCount(const std::wstring& text)
{
    const flamingomd::MdApi* api = flamingomd::GetMdApiTable();
    const int bytes = ::WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), NULL, 0, NULL, NULL);
    std::string utf8(static_cast<size_t>(bytes), '\0');
    if (bytes > 0)
        ::WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), &utf8[0], bytes, NULL, NULL);
    flamingomd::MdDocument* doc = NULL;
    if (api->m_parse(utf8.c_str(), static_cast<uint32_t>(utf8.size()), NULL, &doc) != flamingomd::md_ok)
        return -1;
    flamingomd::MdDocInfo info;
    memset(&info, 0, sizeof(info));
    info.m_cbSize = sizeof(info);
    api->m_getDocInfo(doc, &info);
    api->m_freeDoc(doc);
    return info.m_codeBlockCount;
}

//按下回车（新插入的换行默认为 RichEdit 的 \r）：返回是否续项，结果写进 out
bool Enter(const std::wstring& text, int s, int e, MarkdownEditResult& out, wchar_t lineBreak = L'\r')
{
    MarkdownEditInput in;
    in.m_text = text;
    in.m_selStart = s;
    in.m_selEnd = e;
    in.m_lineBreak = lineBreak;
    return ContinueMarkdownListOnEnter(in, out);
}

//光标在 caret 处回车应当续项，得到 expected，光标停在 expectedCaret，替换区间最小
void CheckEnter(const char* label, const std::wstring& text, int caret, const std::wstring& expected, int expectedCaret,
                wchar_t lineBreak = L'\r')
{
    INFO(label);
    MarkdownEditResult r;
    REQUIRE(Enter(text, caret, caret, r, lineBreak));
    CHECK(r.m_newText == expected);
    CHECK(r.m_selStart == expectedCaret);
    CHECK(r.m_selEnd == expectedCaret);
    CheckMinimalReplace(text, r);
}

//选区 [s, e) 时回车不由续项处理，结果不被改动
void CheckNotHandled(const char* label, const std::wstring& text, int s, int e)
{
    INFO(label);
    MarkdownEditResult r;
    CHECK_FALSE(Enter(text, s, e, r));
    CHECK(r.m_newText.empty());
    CHECK(r.m_replacement.empty());
    CHECK(r.m_replaceStart == 0);
    CHECK(r.m_replaceEnd == 0);
}

//文字长度（int）
int Len(const std::wstring& text)
{
    return static_cast<int>(text.size());
}

//把选区 [s, e) 覆盖的行设为 level 级标题（换行为 RichEdit 的 \r）
MarkdownEditResult Heading(const std::wstring& text, int s, int e, int level)
{
    MarkdownEditInput in;
    in.m_text = text;
    in.m_selStart = s;
    in.m_selEnd = e;
    in.m_lineBreak = L'\r';
    return SetMarkdownHeadingLevel(in, level);
}

//ME32：快捷键里 ` 与 \ 两个键的虚拟键码（美式键盘布局，VK_OEM_3 与 VK_OEM_5）
const unsigned kVkBacktick = 0xC0;
const unsigned kVkBackslash = 0xDC;

} // namespace

TEST_CASE("ME1 有选区时加粗：两侧加 **，选区仍是原文字，替换区间最小", "[mdedit]")
{
    const std::wstring text = L"hello world";
    MarkdownEditResult r = Run(text, 6, 11, md_edit_bold);
    CHECK(r.m_newText == L"hello **world**");
    CHECK(Selected(r) == L"world");
    CheckMinimalReplace(text, r);
    CHECK(r.m_replaceStart == 6);
}

TEST_CASE("ME2 选区两侧已有 ** 时再点加粗去掉标记", "[mdedit]")
{
    MarkdownEditResult r = Run(L"hello **world**", 8, 13, md_edit_bold);
    CHECK(r.m_newText == L"hello world");
    CHECK(Selected(r) == L"world");
}

TEST_CASE("ME3 斜体按奇数个 * 判断，不会把加粗当成斜体去掉", "[mdedit]")
{
    MarkdownEditResult r = Run(L"**x**", 2, 3, md_edit_italic);
    CHECK(r.m_newText == L"***x***");
    MarkdownEditResult back = Run(r.m_newText, 3, 4, md_edit_italic);
    CHECK(back.m_newText == L"**x**");
}

TEST_CASE("ME4 没有选区时插入带占位文字的标记并选中占位文字", "[mdedit]")
{
    MarkdownEditResult r = Run(L"ab", 1, 1, md_edit_bold, L"\x7C97\x4F53");
    CHECK(r.m_newText == L"a**\x7C97\x4F53**b");
    CHECK(Selected(r) == L"\x7C97\x4F53");
    CheckMinimalReplace(L"ab", r);
}

TEST_CASE("ME5 选区本身带着两侧标记时去掉；删除线与行内代码同理", "[mdedit]")
{
    CHECK(Run(L"`code`", 0, 6, md_edit_code).m_newText == L"code");
    CHECK(Run(L"a b", 0, 1, md_edit_strike).m_newText == L"~~a~~ b");
    CHECK(Run(L"~~a~~ b", 2, 3, md_edit_strike).m_newText == L"a b");
}

TEST_CASE("ME6 标题前缀可加可去，光标随前缀平移", "[mdedit]")
{
    MarkdownEditResult r = Run(L"title", 2, 2, md_edit_heading);
    CHECK(r.m_newText == L"## title");
    CHECK(r.m_selStart == 5);
    CHECK(r.m_selEnd == 5);
    MarkdownEditResult back = Run(r.m_newText, 5, 5, md_edit_heading);
    CHECK(back.m_newText == L"title");
    CHECK(back.m_selStart == 2);
    //已是其它级别的标题也认作标题，再点去掉
    CHECK(Run(L"### t", 0, 0, md_edit_heading).m_newText == L"t");
}

TEST_CASE("ME7 无序列表给选区覆盖的每一行加前缀，都已有时去掉（换行为 \\r）", "[mdedit]")
{
    const std::wstring text = L"a\rb\rc";
    MarkdownEditResult r = Run(text, 0, 5, md_edit_bullet_list);
    CHECK(r.m_newText == L"- a\r- b\r- c");
    CheckMinimalReplace(text, r);
    MarkdownEditResult back = Run(r.m_newText, 0, static_cast<int>(r.m_newText.size()), md_edit_bullet_list);
    CHECK(back.m_newText == text);
}

TEST_CASE("ME8 有序列表逐行编号，已有编号的行重新编号", "[mdedit]")
{
    CHECK(Run(L"x\ry", 0, 3, md_edit_number_list).m_newText == L"1. x\r2. y");
    CHECK(Run(L"5. x\ry", 0, 6, md_edit_number_list).m_newText == L"1. x\r2. y");
    CHECK(Run(L"1. x\r2. y", 0, 9, md_edit_number_list).m_newText == L"x\ry");
}

TEST_CASE("ME9 多行时空行不加前缀", "[mdedit]")
{
    CHECK(Run(L"a\r\rb", 0, 4, md_edit_quote).m_newText == L"> a\r\r> b");
    //单独一个空行（光标所在行为空）照样加
    CHECK(Run(L"", 0, 0, md_edit_quote).m_newText == L"> ");
}

TEST_CASE("ME10 选区终点正好在下一行行首时，那一行不算", "[mdedit]")
{
    CHECK(Run(L"a\rb", 0, 2, md_edit_bullet_list).m_newText == L"- a\rb");
    //\r\n 换行同样认得
    CHECK(Run(L"a\r\nb", 0, 4, md_edit_bullet_list).m_newText == L"- a\r\n- b");
}

TEST_CASE("ME11 代码块包裹选中的行，再点一次拆除围栏", "[mdedit]")
{
    const std::wstring text = L"line1\rline2";
    MarkdownEditResult r = Run(text, 2, 8, md_edit_code_block);
    CHECK(r.m_newText == L"```\rline1\rline2\r```");
    CHECK(Selected(r) == text);
    MarkdownEditResult back = Run(r.m_newText, 0, static_cast<int>(r.m_newText.size()), md_edit_code_block);
    CHECK(back.m_newText == text);
}

TEST_CASE("ME12 没有选区时在行中插入空代码块：先空一行，围栏各占一行，选中占位文字", "[mdedit]")
{
    MarkdownEditResult r = Run(L"ab", 1, 1, md_edit_code_block, L"code");
    CHECK(r.m_newText == L"a\r\r```\rcode\r```\rb");
    CHECK(Selected(r) == L"code");
    MarkdownEditResult atEnd = Run(L"", 0, 0, md_edit_code_block, L"code");
    CHECK(atEnd.m_newText == L"```\rcode\r```");
}

TEST_CASE("ME13 链接：选中文字作说明并选中地址占位；选中网址时放进括号并选中说明占位", "[mdedit]")
{
    MarkdownEditResult a = Run(L"see docs", 4, 8, md_edit_link, L"L");
    CHECK(a.m_newText == L"see [docs](https://)");
    CHECK(Selected(a) == L"https://");

    MarkdownEditResult b = Run(L"https://x.com", 0, 13, md_edit_link, L"L");
    CHECK(b.m_newText == L"[L](https://x.com)");
    CHECK(Selected(b) == L"L");

    MarkdownEditResult c = Run(L"", 0, 0, md_edit_link, L"L");
    CHECK(c.m_newText == L"[L](https://)");
    CHECK(Selected(c) == L"L");
}

TEST_CASE("ME14 表格另起一段插入模板，选中第一列列名", "[mdedit]")
{
    MarkdownEditResult r = Run(L"abc", 3, 3, md_edit_table);
    CHECK(r.m_newText == L"abc\r\r| H1 | H2 |\r| --- | --- |\r|  |  |");
    CHECK(Selected(r) == L"H1");

    //空文档：直接插入
    CHECK(Run(L"", 0, 0, md_edit_table).m_newText == L"| H1 | H2 |\r| --- | --- |\r|  |  |");

    //行中：表格后面的文字另起一行
    MarkdownEditResult mid = Run(L"ab", 1, 1, md_edit_table);
    CHECK(mid.m_newText == L"a\r\r| H1 | H2 |\r| --- | --- |\r|  |  |\rb");
}

TEST_CASE("ME15 所有命令的替换区间与替换文字都能还原出新全文", "[mdedit]")
{
    const std::wstring texts[] = { L"", L"abc", L"a\rb\rc", L"**x** and `y`", L"line1\r\nline2\r\n" };
    for (size_t t = 0; t < sizeof(texts) / sizeof(texts[0]); ++t)
    {
        const int len = static_cast<int>(texts[t].size());
        for (int command = 0; command < kMarkdownEditCommandCount; ++command)
        {
            INFO("text " << t << ", command " << command);
            CheckMinimalReplace(texts[t], Run(texts[t], 0, len, command));
            CheckMinimalReplace(texts[t], Run(texts[t], len / 2, len / 2, command));
        }
    }
}

TEST_CASE("ME16 任务列表可加可去；选区越界被截掉；不认识的命令不改动", "[mdedit]")
{
    CHECK(Run(L"todo", 0, 4, md_edit_task_list).m_newText == L"- [ ] todo");
    CHECK(Run(L"- [x] done", 0, 10, md_edit_task_list).m_newText == L"done");
    //任务项不算普通列表项：点无序列表时加上「- 」
    CHECK(Run(L"- [ ] a", 0, 7, md_edit_bullet_list).m_newText == L"- - [ ] a");

    MarkdownEditResult clamped = Run(L"ab", -5, 99, md_edit_bold);
    CHECK(clamped.m_newText == L"**ab**");

    MarkdownEditResult none = Run(L"ab", 0, 1, 999);
    CHECK(none.m_newText == L"ab");
    CHECK(none.m_replacement.empty());
    CHECK(none.m_selStart == 0);
    CHECK(none.m_selEnd == 1);
}

TEST_CASE("ME17 紧跟表格插入或包裹代码块时先空一行，解析后是独立的代码块", "[mdedit]")
{
    const std::wstring table = L"| a | b |\r| --- | --- |\r| 1 | 2 |";
    const int tableLen = static_cast<int>(table.size());

    //对照：围栏紧跟在表格行后面时，md4c 把围栏当成表格的行，解析结果里没有代码块（2026-10-06 实机核对发现）
    CHECK(CodeBlockCount(table + L"\r```\rcode\r```") == 0);

    //光标在表格最后一行末尾插入：先换行再空一行
    MarkdownEditResult r = Run(table, tableLen, tableLen, md_edit_code_block, L"code");
    CHECK(r.m_newText == table + L"\r\r```\rcode\r```");
    CHECK(Selected(r) == L"code");
    CHECK(CodeBlockCount(r.m_newText) == 1);

    //选中表格下面紧挨着的一行再包裹：上一行是表格行，先补一个空行
    const std::wstring withLine = table + L"\rprintf(1);";
    MarkdownEditResult w = Run(withLine, tableLen + 1, static_cast<int>(withLine.size()), md_edit_code_block);
    CHECK(w.m_newText == table + L"\r\r```\rprintf(1);\r```");
    CHECK(Selected(w) == L"printf(1);");
    CHECK(CodeBlockCount(w.m_newText) == 1);

    //光标在行首、上一行已经是空行：不再补
    MarkdownEditResult blank = Run(L"a\r\r", 3, 3, md_edit_code_block, L"c");
    CHECK(blank.m_newText == L"a\r\r```\rc\r```");

    //\r\n 换行同样认得：光标在行首、上一行不是空行时补一个空行
    MarkdownEditResult crlf = Run(L"a\r\n", 3, 3, md_edit_table);
    CHECK(crlf.m_newText.compare(0, 4, L"a\r\n\r") == 0);
    CHECK(Selected(crlf) == L"H1");
}

TEST_CASE("ME18 无序列表回车续项：新行用同一个符号，光标落在新标记之后，替换区间最小", "[mdedit]")
{
    CheckEnter("dash", L"- a", 3, L"- a\r- ", 6);
    CheckEnter("star", L"* a", 3, L"* a\r* ", 6);
    CheckEnter("plus", L"+ a", 3, L"+ a\r+ ", 6);

    //替换只是在光标处插入「换行 + 标记」
    MarkdownEditResult r;
    REQUIRE(Enter(L"- a", 3, 3, r));
    CHECK(r.m_replaceStart == 3);
    CHECK(r.m_replaceEnd == 3);
    CHECK(r.m_replacement == L"\r- ");

    //列表中间的一项：新项插在它下面
    CheckEnter("middle", L"- a\r- b", 3, L"- a\r- \r- b", 6);
    //列表符号后的空白照原样
    CheckEnter("spacing", L"-   a", 5, L"-   a\r-   ", 10);
}

TEST_CASE("ME19 有序列表回车续项：编号加一，句点与括号的写法、编号的位数保持", "[mdedit]")
{
    CheckEnter("one", L"1. a", 4, L"1. a\r2. ", 8);
    CheckEnter("nine", L"9. x", 4, L"9. x\r10. ", 9);
    CheckEnter("paren", L"3) y", 4, L"3) y\r4) ", 8);
    CheckEnter("zero pad", L"09. z", 5, L"09. z\r10. ", 10);
    CheckEnter("zero pad 3", L"007. z", 6, L"007. z\r008. ", 12);
    //后面已有的编号不重排
    CheckEnter("no renumber", L"1. a\r2. b", 4, L"1. a\r2. \r2. b", 8);
}

TEST_CASE("ME20 任务列表回车续项：新项一律是未完成的任务框", "[mdedit]")
{
    CheckEnter("open", L"- [ ] a", 7, L"- [ ] a\r- [ ] ", 14);
    CheckEnter("done", L"- [x] d", 7, L"- [x] d\r- [ ] ", 14);
    CheckEnter("upper", L"* [X] d", 7, L"* [X] d\r* [ ] ", 14);
    CheckEnter("ordered", L"1. [x] a", 8, L"1. [x] a\r2. [ ] ", 16);
}

TEST_CASE("ME21 引用回车续「> 」，引用里的列表连同引用符一起续", "[mdedit]")
{
    CheckEnter("quote", L"> q", 3, L"> q\r> ", 6);
    CheckEnter("nested quote", L"> > deep", 8, L"> > deep\r> > ", 13);
    CheckEnter("bare", L">q", 2, L">q\r>", 4);
    CheckEnter("quoted bullet", L"> - a", 5, L"> - a\r> - ", 10);
    CheckEnter("quoted number", L"> 1. a", 6, L"> 1. a\r> 2. ", 12);
}

TEST_CASE("ME22 嵌套列表回车续项：行首缩进（空格与制表符）照原样", "[mdedit]")
{
    CheckEnter("spaces", L"  - a", 5, L"  - a\r  - ", 10);
    CheckEnter("tab", L"\t- a", 4, L"\t- a\r\t- ", 8);
    CheckEnter("ordered", L"    1. a", 8, L"    1. a\r    2. ", 16);
    CheckEnter("quote indent", L">   - a", 7, L">   - a\r>   - ", 14);
}

TEST_CASE("ME23 空项上回车：去掉列表标记、结束列表，不换行", "[mdedit]")
{
    CheckEnter("last", L"- a\r- ", 6, L"- a\r", 4);
    CheckEnter("middle", L"- a\r- \r- b", 6, L"- a\r\r- b", 4);
    CheckEnter("task", L"- [ ] ", 6, L"", 0);
    CheckEnter("task no space", L"- [ ]", 5, L"", 0);
    CheckEnter("ordered", L"1. a\r2. ", 8, L"1. a\r", 5);
    CheckEnter("trailing blanks", L"-   ", 4, L"", 0);
    CheckEnter("nested", L"- a\r  - ", 8, L"- a\r", 4);
    //空引用：去掉引用符
    CheckEnter("quote", L"> a\r> ", 6, L"> a\r", 4);
    //引用里的空列表项：只去掉列表标记，保留引用符
    CheckEnter("quoted bullet", L"> - a\r> - ", 10, L"> - a\r> ", 8);
}

TEST_CASE("ME24 光标在行中间：光标后面的文字带到新行，新行写上标记", "[mdedit]")
{
    CheckEnter("bullet", L"- helloworld", 7, L"- hello\r- world", 10);
    CheckEnter("ordered", L"1. ab", 4, L"1. a\r2. b", 8);
    CheckEnter("quote", L"> ab", 3, L"> a\r> b", 6);
    //光标正好在正文起点：上面留下一个空项，原有内容带着标记移到下一行
    CheckEnter("content start", L"- abc", 2, L"- \r- abc", 5);
}

TEST_CASE("ME25 不续项的情况：有选区、光标在标记前、不是列表或引用、分隔线、围栏代码块里、光标在全文之外",
          "[mdedit]")
{
    CheckNotHandled("selection", L"- ab", 2, 4);
    CheckNotHandled("caret at line start", L"- a", 0, 0);
    CheckNotHandled("caret inside indent", L"  - a", 1, 1);
    CheckNotHandled("caret inside task box", L"- [ ] a", 3, 3);
    CheckNotHandled("caret inside quote prefix", L"> - a", 2, 2);
    CheckNotHandled("paragraph", L"abc", 3, 3);
    CheckNotHandled("heading", L"# t", 3, 3);
    CheckNotHandled("table row", L"| a |", 5, 5);
    CheckNotHandled("no space after bullet", L"-a", 2, 2);
    CheckNotHandled("no space after number", L"1.a", 3, 3);
    CheckNotHandled("number too long", L"1234567890. a", 13, 13);
    CheckNotHandled("bold at line start", L"**b**", 5, 5);
    CheckNotHandled("rule ---", L"---", 3, 3);
    CheckNotHandled("rule * * *", L"* * *", 5, 5);
    CheckNotHandled("rule - - -", L"- - -", 5, 5);
    CheckNotHandled("inside fence", L"```\r- a", 7, 7);
    //~~~ 比开启时的 ~~~~ 短，闭合不了，后面的列表项仍在代码块里
    const std::wstring tilde = L"~~~~\r- a\r~~~\r- b";
    CheckNotHandled("inside longer tilde fence", tilde, Len(tilde), Len(tilde));
    //宿主取到的全文去掉了末尾的空行，光标在空行上时下标超出全文
    CheckNotHandled("caret past trimmed end", L"- a", 5, 5);
    CheckNotHandled("negative caret", L"- a", -1, -1);

    //对照：围栏闭合之后的列表照常续项
    CheckEnter("after fence", L"```\rx\r```\r- a", 13, L"```\rx\r```\r- a\r- ", 16);
}

TEST_CASE("ME26 \\r\\n 与 \\n 换行：认得所在的行，新插入的换行用调用方给出的字符", "[mdedit]")
{
    //\r\n 文字里的第一行、最后一行、中间行
    CheckEnter("crlf first", L"- a\r\n- b", 3, L"- a\r- \r\n- b", 6);
    CheckEnter("crlf last", L"- a\r\n- b", 8, L"- a\r\n- b\r- ", 11);
    CheckEnter("crlf middle", L"- a\r\n- b\r\n- c", 8, L"- a\r\n- b\r- \r\n- c", 11);
    //\r\n 文字里的空项
    CheckEnter("crlf empty", L"- \r\n- b", 2, L"\r\n- b", 0);
    //调用方要求用 \n 换行
    CheckEnter("lf", L"1. a\n2. b", 9, L"1. a\n2. b\n3. ", 13, L'\n');
    //\r\n 文字里的围栏同样认得
    CheckNotHandled("crlf fence", L"```\r\n- a", 8, 8);
}

TEST_CASE("ME27 正文行设为 1 ~ 6 级标题：行首加对应个数的 # 与空格，光标随前缀平移，替换区间最小", "[mdedit]")
{
    for (int level = 1; level <= 6; ++level)
    {
        INFO("level " << level);
        const std::wstring text = L"title";
        MarkdownEditResult r = Heading(text, 2, 2, level);
        CHECK(r.m_newText == std::wstring(static_cast<size_t>(level), L'#') + L" title");
        CHECK(r.m_selStart == 2 + level + 1);
        CHECK(r.m_selEnd == r.m_selStart);
        CheckMinimalReplace(text, r);
        CHECK(r.m_replaceStart == 0);
        CHECK(r.m_replaceEnd == 0);
    }
}

TEST_CASE("ME28 已是标题的行换级别；设为正文去掉前缀；设为同一级别不改动", "[mdedit]")
{
    const std::wstring h3 = L"### t";
    MarkdownEditResult toH1 = Heading(h3, 5, 5, 1);
    CHECK(toH1.m_newText == L"# t");
    CHECK(toH1.m_selStart == 3);
    CheckMinimalReplace(h3, toH1);

    CHECK(Heading(L"# t", 3, 3, 6).m_newText == L"###### t");

    MarkdownEditResult toBody = Heading(h3, 5, 5, 0);
    CHECK(toBody.m_newText == L"t");
    CHECK(toBody.m_selStart == 1);

    //同一级别：不改动，选区保持原样（不像格式条原来的「H」那样再点一下就去掉）
    MarkdownEditResult same = Heading(h3, 4, 5, 3);
    CHECK(same.m_newText == h3);
    CHECK(same.m_replacement.empty());
    CHECK(same.m_replaceStart == same.m_replaceEnd);
    CHECK(same.m_selStart == 4);
    CHECK(same.m_selEnd == 5);

    //正文设为正文同样不改动
    CHECK(Heading(L"body", 1, 1, 0).m_replacement.empty());
}

TEST_CASE("ME29 多行选区：每个非空行设为同一级别，原来级别不同的一并统一；空行不加；选区终点在下一行行首时那一行不算",
          "[mdedit]")
{
    const std::wstring text = L"a\r\r## b\r#### c";
    MarkdownEditResult r = Heading(text, 0, Len(text), 2);
    CHECK(r.m_newText == L"## a\r\r## b\r## c");
    CheckMinimalReplace(text, r);

    CHECK(Heading(L"a\rb", 0, 2, 1).m_newText == L"# a\rb");
    CHECK(Heading(L"# a\r### b", 0, 9, 0).m_newText == L"a\rb");
}

TEST_CASE("ME30 取当前行的标题级别：正文为 0，# ~ ###### 为 1 ~ 6；7 个 # 与缺空格不算；有选区时按第一行", "[mdedit]")
{
    CHECK(MarkdownHeadingLevelAt(L"body", 2, 2) == 0);
    CHECK(MarkdownHeadingLevelAt(L"# a", 0, 0) == 1);
    CHECK(MarkdownHeadingLevelAt(L"###### a", 8, 8) == 6);
    CHECK(MarkdownHeadingLevelAt(L"####### a", 3, 3) == 0);
    CHECK(MarkdownHeadingLevelAt(L"#a", 1, 1) == 0);
    //只有 # 的一行也是（空的）标题
    CHECK(MarkdownHeadingLevelAt(L"##", 2, 2) == 2);
    //光标在第二行行中
    CHECK(MarkdownHeadingLevelAt(L"a\r## bc", 5, 5) == 2);
    //选区从第一行开始：按第一行；起止顺序不限
    CHECK(MarkdownHeadingLevelAt(L"# x\r## y", 6, 1) == 1);
    //越界截断
    CHECK(MarkdownHeadingLevelAt(L"### z", -3, 99) == 3);
}

TEST_CASE("ME31 越界的级别不改动；\\r\\n 换行同样正确", "[mdedit]")
{
    CHECK(Heading(L"a", 0, 0, -1).m_replacement.empty());
    CHECK(Heading(L"a", 0, 0, 7).m_replacement.empty());
    CHECK(Heading(L"a", 0, 0, 7).m_newText == L"a");
    const std::wstring crlf = L"a\r\nb";
    MarkdownEditResult r = Heading(crlf, 0, Len(crlf), 1);
    CHECK(r.m_newText == L"# a\r\n# b");
    CheckMinimalReplace(crlf, r);
}

TEST_CASE("ME32 快捷键对应的格式命令：截图里的组合键都对应正确，其它组合不对应", "[mdedit]")
{
    for (int level = 1; level <= 6; ++level)
    {
        INFO("Ctrl+" << level);
        CHECK(MarkdownShortcutFor(static_cast<unsigned>('0' + level), true, false, false)
              == md_shortcut_heading1 + level - 1);
    }
    CHECK(MarkdownShortcutFor('B', true, false, false) == md_shortcut_bold);
    CHECK(MarkdownShortcutFor('I', true, false, false) == md_shortcut_italic);
    CHECK(MarkdownShortcutFor('U', true, false, false) == md_shortcut_underline);
    CHECK(MarkdownShortcutFor(kVkBacktick, true, true, false) == md_shortcut_code);
    CHECK(MarkdownShortcutFor('5', false, true, true) == md_shortcut_strike);
    CHECK(MarkdownShortcutFor('K', true, false, false) == md_shortcut_link);
    CHECK(MarkdownShortcutFor(kVkBackslash, true, false, false) == md_shortcut_clear_style);

    //不对应：截图里没有的组合、多按或少按了修饰键
    CHECK(MarkdownShortcutFor('0', true, false, false) == md_shortcut_none);
    CHECK(MarkdownShortcutFor('7', true, false, false) == md_shortcut_none);
    CHECK(MarkdownShortcutFor('1', true, true, false) == md_shortcut_none);
    CHECK(MarkdownShortcutFor('5', false, false, true) == md_shortcut_none);
    CHECK(MarkdownShortcutFor('5', true, true, true) == md_shortcut_none);
    CHECK(MarkdownShortcutFor('K', true, false, true) == md_shortcut_none);
    CHECK(MarkdownShortcutFor('B', false, false, false) == md_shortcut_none);
    CHECK(MarkdownShortcutFor(kVkBacktick, true, false, false) == md_shortcut_none);
    CHECK(MarkdownShortcutFor('S', true, false, false) == md_shortcut_none);
}

TEST_CASE("ME33 下划线命令：选区两侧加上 <u> 与 </u>；已包着时去掉；没有选区时插入占位文字并选中", "[mdedit]")
{
    const std::wstring text = L"hello world";
    MarkdownEditResult r = Run(text, 6, 11, md_edit_underline);
    CHECK(r.m_newText == L"hello <u>world</u>");
    CHECK(Selected(r) == L"world");
    CheckMinimalReplace(text, r);

    //选区两侧已有标签：去掉
    MarkdownEditResult back = Run(r.m_newText, r.m_selStart, r.m_selEnd, md_edit_underline);
    CHECK(back.m_newText == text);
    CHECK(Selected(back) == L"world");

    //选区本身带着标签：去掉
    MarkdownEditResult inner = Run(L"a <u>b</u>", 2, 10, md_edit_underline);
    CHECK(inner.m_newText == L"a b");
    CHECK(Selected(inner) == L"b");

    //没有选区：插入占位文字并选中
    MarkdownEditResult empty = Run(L"ab", 1, 1, md_edit_underline, L"U");
    CHECK(empty.m_newText == L"a<u>U</u>b");
    CHECK(Selected(empty) == L"U");
    CheckMinimalReplace(L"ab", empty);

    //大写的标签同样认得
    CHECK(Run(L"<U>x</U>", 3, 4, md_edit_underline).m_newText == L"x");
}

namespace {

//清除选区 [s, e) 的样式（换行为 RichEdit 的 \r）
MarkdownEditResult Clear(const std::wstring& text, int s, int e)
{
    MarkdownEditInput in;
    in.m_text = text;
    in.m_selStart = s;
    in.m_selEnd = e;
    in.m_lineBreak = L'\r';
    return ClearMarkdownInlineStyle(in);
}

} // namespace

TEST_CASE("ME34 清除样式：去掉选区里的加粗、斜体、删除线、行内代码、下划线标记，链接变成文字；单词里的下划线、"
          "行首的列表符号与标题前缀不动；选区正好被标记包着时外侧标记一起去掉；没有选区不改动", "[mdedit]")
{
    const std::wstring mixed = L"**bold** and *it* and ~~del~~ and `c*d` and <u>u</u> and __x__";
    MarkdownEditResult all = Clear(mixed, 0, Len(mixed));
    CHECK(all.m_newText == L"bold and it and del and c*d and u and x");
    CheckMinimalReplace(mixed, all);
    CHECK(Selected(all) == all.m_newText);

    //单词中间的下划线不是强调，不去掉
    const std::wstring snake = L"a snake_case_name";
    CHECK(Clear(snake, 0, Len(snake)).m_newText == snake);

    //链接与图片变成文字
    const std::wstring link = L"see [docs](https://x.com) and ![pic](a.png) now";
    CHECK(Clear(link, 0, Len(link)).m_newText == L"see docs and pic now");

    //选区正好被标记包着：外侧标记一起去掉，选中剩下的文字
    MarkdownEditResult wrapped = Clear(L"x **abc** y", 4, 7);
    CHECK(wrapped.m_newText == L"x abc y");
    CHECK(Selected(wrapped) == L"abc");
    CHECK(Clear(L"***x***", 3, 4).m_newText == L"x");
    CHECK(Clear(L"a <u>b</u> c", 5, 6).m_newText == L"a b c");
    CHECK(Clear(L"go [here](https://x.com) now", 4, 8).m_newText == L"go here now");

    //行首的列表符号、引用符、标题前缀不动
    const std::wstring lines = L"- **a**\r> *b*\r## __c__\r1. `d`";
    CHECK(Clear(lines, 0, Len(lines)).m_newText == L"- a\r> b\r## c\r1. d");

    //没有选区：不改动
    MarkdownEditResult none = Clear(L"**a**", 2, 2);
    CHECK(none.m_newText == L"**a**");
    CHECK(none.m_replacement.empty());

    //\r\n 换行
    const std::wstring crlf = L"**a**\r\n*b*";
    CHECK(Clear(crlf, 0, Len(crlf)).m_newText == L"a\r\nb");
}

namespace {

//把编辑框里的文字（\r 换行）交给解析器，数出分割线与标题的个数
void CountRulesAndHeadings(const std::wstring& text, int& rules, int& headings)
{
    rules = 0;
    headings = 0;
    std::wstring lf = text;
    for (size_t i = 0; i < lf.size(); ++i)
    {
        if (lf[i] == L'\r')
            lf[i] = L'\n';
    }
    const int bytes = ::WideCharToMultiByte(CP_UTF8, 0, lf.c_str(), static_cast<int>(lf.size()), NULL, 0, NULL, NULL);
    std::string utf8(static_cast<size_t>(bytes), '\0');
    if (bytes > 0)
        ::WideCharToMultiByte(CP_UTF8, 0, lf.c_str(), static_cast<int>(lf.size()), &utf8[0], bytes, NULL, NULL);
    flamingomd::MdModel model;
    flamingomd::MdBuildOptions options;
    REQUIRE(flamingomd::BuildMdModel(utf8.data(), utf8.size(), options, model) == flamingomd::md_ok);
    for (size_t i = 0; i < model.m_blocks.size(); ++i)
    {
        if (model.m_blocks[i].m_kind == flamingomd::md_block_rule)
            ++rules;
        else if (model.m_blocks[i].m_kind == flamingomd::md_block_heading)
            ++headings;
    }
}

} // namespace

TEST_CASE("ME35 分割线：在行尾、行中、空行处插入「---」，与上面的文字之间隔一个空行，解析出分割线而不是标题",
          "[mdedit]")
{
    //对照：「---」紧跟在文字下一行时，上一行会成为二级标题（Markdown 的另一种标题写法）
    int rules = 0;
    int headings = 0;
    CountRulesAndHeadings(L"abc\r---", rules, headings);
    CHECK(rules == 0);
    CHECK(headings == 1);

    //行尾：先换行再空一行，光标停在「---」之后
    MarkdownEditResult end = Run(L"abc", 3, 3, md_edit_rule);
    CHECK(end.m_newText == L"abc\r\r---");
    CHECK(end.m_selStart == Len(end.m_newText));
    CHECK(end.m_selEnd == end.m_selStart);
    CheckMinimalReplace(L"abc", end);
    CountRulesAndHeadings(end.m_newText, rules, headings);
    CHECK(rules == 1);
    CHECK(headings == 0);

    //空文档：直接插入
    CHECK(Run(L"", 0, 0, md_edit_rule).m_newText == L"---");

    //行中：分割线前空一行，后面的文字另起一行
    MarkdownEditResult mid = Run(L"ab", 1, 1, md_edit_rule);
    CHECK(mid.m_newText == L"a\r\r---\rb");
    CountRulesAndHeadings(mid.m_newText, rules, headings);
    CHECK(rules == 1);
    CHECK(headings == 0);

    //行首、上一行不是空行：补一个空行；上一行已是空行：不再补
    CHECK(Run(L"a\r", 2, 2, md_edit_rule).m_newText == L"a\r\r---");
    CHECK(Run(L"a\r\r", 3, 3, md_edit_rule).m_newText == L"a\r\r---");

    //有选区：插在选区之后，不替换选中的文字
    CHECK(Run(L"ab", 0, 2, md_edit_rule).m_newText == L"ab\r\r---");
}

namespace {

//ME36：插入警告框（占位文字为「P」，换行字符为 RichEdit 的 \r）
MarkdownEditResult Alert(const std::wstring& text, int s, int e, int type)
{
    MarkdownEditInput in;
    in.m_text = text;
    in.m_selStart = s;
    in.m_selEnd = e;
    in.m_lineBreak = L'\r';
    in.m_placeholder = L"P";
    return InsertMarkdownAlert(in, type);
}

//ME36：解析编辑框里的文字，按顺序列出文档根下各块的警告框类型（不是警告框的块记为 0）
std::vector<int> RootAlertTypes(const std::wstring& text)
{
    std::wstring lf = text;
    for (size_t i = 0; i < lf.size(); ++i)
    {
        if (lf[i] == L'\r')
            lf[i] = L'\n';
    }
    const int bytes = ::WideCharToMultiByte(CP_UTF8, 0, lf.c_str(), static_cast<int>(lf.size()), NULL, 0, NULL, NULL);
    std::string utf8(static_cast<size_t>(bytes), '\0');
    if (bytes > 0)
        ::WideCharToMultiByte(CP_UTF8, 0, lf.c_str(), static_cast<int>(lf.size()), &utf8[0], bytes, NULL, NULL);
    flamingomd::MdModel model;
    flamingomd::MdBuildOptions options;
    REQUIRE(flamingomd::BuildMdModel(utf8.data(), utf8.size(), options, model) == flamingomd::md_ok);
    std::vector<int> types;
    const flamingomd::MdBlock& root = model.m_blocks[0];
    for (size_t i = 0; i < root.m_children.size(); ++i)
        types.push_back(model.m_blocks[root.m_children[i]].m_alertType);
    return types;
}

} // namespace

TEST_CASE("ME36 警告框：没有选区时另起一段插入类型行与占位文字并选中占位文字；有选区时把各行放进警告框；"
          "光标在类型行上时只换类型；上下紧挨着文字时各空一行；越界的类型不改动", "[mdedit]")
{
    //空文档：直接插入，选中占位文字
    MarkdownEditResult empty = Alert(L"", 0, 0, md_edit_alert_note);
    CHECK(empty.m_newText == L"> [!NOTE]\r> P");
    CHECK(Selected(empty) == L"P");

    //光标在一行文字里：插在这一行之后，隔一个空行
    MarkdownEditResult after = Alert(L"abc", 1, 1, md_edit_alert_tip);
    CHECK(after.m_newText == L"abc\r\r> [!TIP]\r> P");
    CHECK(Selected(after) == L"P");
    CheckMinimalReplace(L"abc", after);

    //光标在两段文字之间的空行上：就地插入，上下各空一行
    MarkdownEditResult between = Alert(L"a\r\rb", 2, 2, md_edit_alert_note);
    CHECK(between.m_newText == L"a\r\r> [!NOTE]\r> P\r\rb");
    CHECK(Selected(between) == L"P");
    const int kParagraph = 0;
    std::vector<int> types = RootAlertTypes(between.m_newText);
    REQUIRE(types.size() == 3u);
    CHECK(types[0] == kParagraph);
    CHECK(types[1] == md_edit_alert_note);
    CHECK(types[2] == kParagraph);

    //有选区：各行加「> 」，空行写成「>」，已是引用的行不再加；选中整个警告框
    MarkdownEditResult two = Alert(L"a\rb", 0, 3, md_edit_alert_warning);
    CHECK(two.m_newText == L"> [!WARNING]\r> a\r> b");
    CHECK(Selected(two) == two.m_newText);
    CHECK(Alert(L"a\r\rb", 0, 4, md_edit_alert_caution).m_newText == L"> [!CAUTION]\r> a\r>\r> b");
    CHECK(Alert(L"> a\r> b", 0, 7, md_edit_alert_important).m_newText == L"> [!IMPORTANT]\r> a\r> b");

    //选中中间一行：上下的文字各隔一个空行，解析后是段落、警告框、段落
    MarkdownEditResult middle = Alert(L"x\ry\rz", 2, 3, md_edit_alert_note);
    CHECK(middle.m_newText == L"x\r\r> [!NOTE]\r> y\r\rz");
    CHECK(Selected(middle) == L"> [!NOTE]\r> y");
    CheckMinimalReplace(L"x\ry\rz", middle);
    types = RootAlertTypes(middle.m_newText);
    REQUIRE(types.size() == 3u);
    CHECK(types[1] == md_edit_alert_note);

    //光标或选区起点在类型行上：只换类型名（小写的也换），选区随之平移
    MarkdownEditResult retype = Alert(L"> [!NOTE]\r> x", 9, 9, md_edit_alert_tip);
    CHECK(retype.m_newText == L"> [!TIP]\r> x");
    CHECK(retype.m_selStart == 8);
    CHECK(Alert(L"> [!NOTE]\r> x", 0, 13, md_edit_alert_caution).m_newText == L"> [!CAUTION]\r> x");
    CHECK(Alert(L"> [!note]", 3, 3, md_edit_alert_warning).m_newText == L"> [!WARNING]");

    //越界的类型不改动；格式条命令 md_edit_alert 按「提示」插入
    MarkdownEditResult bad = Alert(L"ab", 0, 1, 0);
    CHECK(bad.m_newText == L"ab");
    CHECK(bad.m_replacement.empty());
    CHECK(Alert(L"ab", 0, 1, kMarkdownAlertTypeCount + 1).m_newText == L"ab");
    CHECK(Run(L"", 0, 0, md_edit_alert).m_newText == L"> [!NOTE]\r> P");
    CHECK(std::wstring(MarkdownAlertKeyword(md_edit_alert_important)) == L"IMPORTANT");
    CHECK(std::wstring(MarkdownAlertKeyword(0)).empty());
}
