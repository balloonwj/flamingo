/**
 *  FlamingoMarkdown.dll 文档模型的单元测试，用例 MD1 ~ MD18（docs/Markdown消息方案.md 第 8 节），下划线 MD19、警告框 MD20（第 13 节）。
 *
 *  直接调用 Core/MdBuilder 生成模型，核对块结构、行内样式、渲染后文字、链接与上限处理，不经过 DLL 边界。
 *  框架 Catch2 v3，标签 [mddll]。
 *
 *  balloonwj@qq.com   2026-10-06
 */
#include "catch_amalgamated.hpp"

#include <windows.h>
#include <string>
#include <vector>

#include "md4c.h"

#include "../FlamingoMarkdownApi.h"
#include "../Core/MdAlert.h"
#include "../Core/MdBuilder.h"
#include "../Core/MdModel.h"
#include "../Core/MdText.h"

using namespace flamingomd;

namespace {

//病态输入测试允许的最长解析时间（毫秒）。Debug 版未优化，留足余量；正常应在几十毫秒内
const DWORD kPathologicalParseLimitMs = 3000;

//病态输入的目标长度（字节）：与聊天消息的内容上限相同
const size_t kPathologicalBytes = 256 * 1024;

//深层嵌套测试的嵌套层数，远大于上限 kMdMaxBlockDepth
const int kDeepNesting = 200;

//超宽表格测试的列数，大于上限 kMdMaxTableColumns
const int kWideTableColumns = 40;

//把 UTF-8 源文本解析成模型，要求成功
MdModel Parse(const std::string& source, const std::wstring& imagePrefix = std::wstring())
{
    MdModel model;
    MdBuildOptions options;
    options.m_imageLinkPrefix = imagePrefix;
    const int rc = BuildMdModel(source.data(), source.size(), options, model);
    REQUIRE(rc == md_ok);
    return model;
}

//UTF-8 字面量转宽字符串（测试里的中文都写成 UTF-8）
std::wstring W(const char* utf8)
{
    return Utf8ToWide(utf8, strlen(utf8));
}

//文档根的第 i 个子块
const MdBlock& RootChild(const MdModel& model, size_t i)
{
    REQUIRE(model.m_blocks.size() > 0);
    REQUIRE(i < model.m_blocks[0].m_children.size());
    return model.m_blocks[model.m_blocks[0].m_children[i]];
}

//某个块的第 i 个子块
const MdBlock& Child(const MdModel& model, const MdBlock& block, size_t i)
{
    REQUIRE(i < block.m_children.size());
    return model.m_blocks[block.m_children[i]];
}

//把一个文字块的行内文字连起来
std::wstring InlineText(const MdBlock& block)
{
    std::wstring out;
    for (size_t i = 0; i < block.m_inlines.size(); ++i)
        out += block.m_inlines[i].m_text;
    return out;
}

//MD19：文字块里文字恰为 text 的行内元素的样式位；找不到时返回 kNoSuchInline
const uint32_t kNoSuchInline = 0xFFFFFFFFu;
uint32_t StyleOf(const MdBlock& block, const std::wstring& text)
{
    for (size_t i = 0; i < block.m_inlines.size(); ++i)
    {
        if (block.m_inlines[i].m_text == text)
            return block.m_inlines[i].m_style;
    }
    return kNoSuchInline;
}

} // namespace

TEST_CASE("MD1 解析器启用了表格、删除线、任务列表、自动链接、警告框与软换行按硬换行处理，关闭了块级 HTML 与其它扩展", "[mddll]")
{
    const unsigned flags = GetMdParserFlags();
    CHECK((flags & MD_FLAG_TABLES) != 0);
    CHECK((flags & MD_FLAG_STRIKETHROUGH) != 0);
    CHECK((flags & MD_FLAG_TASKLISTS) != 0);
    CHECK((flags & MD_FLAG_PERMISSIVEURLAUTOLINKS) != 0);
    CHECK((flags & MD_FLAG_PERMISSIVEWWWAUTOLINKS) != 0);
    CHECK((flags & MD_FLAG_PERMISSIVEEMAILAUTOLINKS) != 0);
    CHECK((flags & MD_FLAG_HARD_SOFT_BREAKS) != 0);
    CHECK((flags & MD_FLAG_NOHTMLBLOCKS) != 0);
    //行内 HTML 开启（第 13 节）：只为认出 <u>…</u>，其余行内标签仍按文字显示（MD19）
    CHECK((flags & MD_FLAG_NOHTMLSPANS) == 0);
    CHECK((flags & MD_FLAG_UNDERLINE) == 0);
    CHECK((flags & MD_FLAG_LATEXMATHSPANS) == 0);
    CHECK((flags & MD_FLAG_FOOTNOTES) == 0);
    //警告框开启（第 13 节，MD20）
    CHECK((flags & MD_FLAG_ADMONITIONS) != 0);
    CHECK((flags & MD_FLAG_SUBSCRIPTS) == 0);
    CHECK((flags & MD_FLAG_SPOILERS) == 0);
}

TEST_CASE("MD2 段落里的加粗、斜体、删除线、行内代码各自成段并带对应样式", "[mddll]")
{
    MdModel model = Parse("Hello **bold** *it* ~~del~~ `code`");
    REQUIRE(model.m_blocks[0].m_children.size() == 1);
    const MdBlock& p = RootChild(model, 0);
    CHECK(p.m_kind == md_block_paragraph);
    CHECK(model.m_flatText == L"Hello bold it del code");

    bool sawBold = false;
    bool sawItalic = false;
    bool sawStrike = false;
    bool sawCode = false;
    for (size_t i = 0; i < p.m_inlines.size(); ++i)
    {
        const MdInline& in = p.m_inlines[i];
        if (in.m_text == L"bold")
            sawBold = (in.m_style == md_style_bold);
        if (in.m_text == L"it")
            sawItalic = (in.m_style == md_style_italic);
        if (in.m_text == L"del")
            sawStrike = (in.m_style == md_style_strike);
        if (in.m_text == L"code")
            sawCode = (in.m_style == md_style_code);
    }
    CHECK(sawBold);
    CHECK(sawItalic);
    CHECK(sawStrike);
    CHECK(sawCode);
}

TEST_CASE("MD3 单个换行显示为换行：同一段落里出现强制换行", "[mddll]")
{
    MdModel model = Parse("\xE7\xAC\xAC\xE4\xB8\x80\xE8\xA1\x8C\n\xE7\xAC\xAC\xE4\xBA\x8C\xE8\xA1\x8C");   //第一行\n第二行
    REQUIRE(model.m_blocks[0].m_children.size() == 1);
    const MdBlock& p = RootChild(model, 0);
    CHECK(p.m_kind == md_block_paragraph);
    REQUIRE(p.m_inlines.size() == 3);
    CHECK(p.m_inlines[1].m_isBreak);
    CHECK(model.m_flatText == W("\xE7\xAC\xAC\xE4\xB8\x80\xE8\xA1\x8C\n\xE7\xAC\xAC\xE4\xBA\x8C\xE8\xA1\x8C"));
}

TEST_CASE("MD4 \\r\\n 与 \\r 换行与 \\n 效果相同，空一行另起一段", "[mddll]")
{
    CHECK(Parse("a\r\nb").m_flatText == L"a\nb");
    CHECK(Parse("a\rb").m_flatText == L"a\nb");

    MdModel two = Parse("a\r\n\r\nb");
    CHECK(two.m_blocks[0].m_children.size() == 2);
    CHECK(two.m_flatText == L"a\nb");
}

TEST_CASE("MD5 六级标题都能识别并记下级别", "[mddll]")
{
    MdModel model = Parse("# h1\n## h2\n### h3\n#### h4\n##### h5\n###### h6\n####### h7");
    REQUIRE(model.m_blocks[0].m_children.size() == 7);
    for (int level = 1; level <= 6; ++level)
    {
        const MdBlock& h = RootChild(model, static_cast<size_t>(level - 1));
        CHECK(h.m_kind == md_block_heading);
        CHECK(h.m_headingLevel == level);
    }
    //七个 # 不是标题，按段落显示
    CHECK(RootChild(model, 6).m_kind == md_block_paragraph);
}

TEST_CASE("MD6 有序、无序、嵌套与任务列表", "[mddll]")
{
    MdModel model = Parse("3. one\n4. two\n\n- a\n  - b\n- [x] done\n- [ ] todo");
    REQUIRE(model.m_blocks[0].m_children.size() == 2);

    const MdBlock& ol = RootChild(model, 0);
    CHECK(ol.m_kind == md_block_list);
    CHECK(ol.m_ordered);
    CHECK(ol.m_startNumber == 3);
    CHECK(ol.m_children.size() == 2);

    const MdBlock& ul = RootChild(model, 1);
    CHECK(ul.m_kind == md_block_list);
    CHECK_FALSE(ul.m_ordered);
    REQUIRE(ul.m_children.size() == 3);

    //第一项里嵌着一个子列表
    const MdBlock& first = Child(model, ul, 0);
    bool hasNested = false;
    for (size_t i = 0; i < first.m_children.size(); ++i)
    {
        if (model.m_blocks[first.m_children[i]].m_kind == md_block_list)
            hasNested = true;
    }
    CHECK(hasNested);

    CHECK(Child(model, ul, 1).m_taskState == md_task_checked);
    CHECK(Child(model, ul, 2).m_taskState == md_task_unchecked);
}

TEST_CASE("MD7 紧凑列表项里没有段落时补一个隐式段落，渲染后文字逐项换行", "[mddll]")
{
    MdModel model = Parse("- a\n- b");
    const MdBlock& ul = RootChild(model, 0);
    CHECK(ul.m_tight);
    REQUIRE(ul.m_children.size() == 2);
    const MdBlock& item = Child(model, ul, 0);
    REQUIRE(item.m_children.size() == 1);
    const MdBlock& para = Child(model, item, 0);
    CHECK(para.m_kind == md_block_paragraph);
    CHECK(InlineText(para) == L"a");
    CHECK(model.m_flatText == L"a\nb");
}

TEST_CASE("MD8 代码块记下语言名与原文，去掉结尾换行，显示文字展开 Tab", "[mddll]")
{
    //行首的 Tab 属于缩进，md4c 按 CommonMark 规则自行展开成空格；这里用行中间的 Tab 核对「原文保留、显示展开」
    MdModel model = Parse("```cpp\nint a;\nx\tb();\n```\n\n    indented");
    REQUIRE(model.m_codeBlocks.size() == 2);
    const MdBlock& fenced = model.m_blocks[model.m_codeBlocks[0]];
    CHECK(fenced.m_kind == md_block_code);
    CHECK(fenced.m_codeLang == L"cpp");
    CHECK(fenced.m_codeRaw == L"int a;\nx\tb();");
    CHECK(fenced.m_code == L"int a;\nx   b();");
    CHECK(fenced.m_codeIndex == 0);

    const MdBlock& indented = model.m_blocks[model.m_codeBlocks[1]];
    CHECK(indented.m_codeRaw == L"indented");
    CHECK(indented.m_codeIndex == 1);

    //渲染后文字里是展开 Tab 之后的显示文字
    CHECK(model.m_flatText == L"int a;\nx   b();\nindented");
    CHECK(fenced.m_codeFlatBegin == 0);
}

TEST_CASE("MD9 引用块可以嵌套", "[mddll]")
{
    MdModel model = Parse("> outer\n>> inner");
    const MdBlock& q = RootChild(model, 0);
    CHECK(q.m_kind == md_block_quote);
    bool nested = false;
    for (size_t i = 0; i < q.m_children.size(); ++i)
    {
        if (model.m_blocks[q.m_children[i]].m_kind == md_block_quote)
            nested = true;
    }
    CHECK(nested);
    CHECK(model.m_flatText == L"outer\ninner");
}

TEST_CASE("MD10 表格：列数、表头行、对齐；渲染后文字里单元格用 Tab、行之间用换行", "[mddll]")
{
    MdModel model = Parse("| a | b | c |\n|:--|:-:|--:|\n| 1 | 2 | 3 |");
    const MdBlock& table = RootChild(model, 0);
    CHECK(table.m_kind == md_block_table);
    CHECK(table.m_columnCount == 3);
    REQUIRE(table.m_children.size() == 2);

    const MdBlock& head = Child(model, table, 0);
    CHECK(head.m_headerRow);
    REQUIRE(head.m_children.size() == 3);
    CHECK(Child(model, head, 0).m_align == md_align_left);
    CHECK(Child(model, head, 1).m_align == md_align_center);
    CHECK(Child(model, head, 2).m_align == md_align_right);
    CHECK(Child(model, head, 2).m_columnIndex == 2);

    const MdBlock& body = Child(model, table, 1);
    CHECK_FALSE(body.m_headerRow);
    CHECK(model.m_flatText == L"a\tb\tc\n1\t2\t3");
}

TEST_CASE("MD11 原始 HTML 不解析，按文字原样显示", "[mddll]")
{
    MdModel model = Parse("<b>x</b> and <script>alert(1)</script>");
    CHECK(model.m_flatText == L"<b>x</b> and <script>alert(1)</script>");
    CHECK(model.m_links.empty());
}

TEST_CASE("MD12 链接只保留 http、https、mailto，www. 与裸电子邮件补协议，其它地址按文字显示", "[mddll]")
{
    MdModel model = Parse("[t](https://a.com) https://b.com/x www.c.com d@e.com [bad](javascript:alert(1)) [f](file:///c:/x)");
    REQUIRE(model.m_links.size() == 4);
    CHECK(model.m_links[0].m_url == L"https://a.com");
    CHECK(model.m_links[1].m_url == L"https://b.com/x");
    CHECK(model.m_links[2].m_url == L"http://www.c.com");
    CHECK(model.m_links[3].m_url == L"mailto:d@e.com");
    for (size_t i = 0; i < model.m_links.size(); ++i)
        CHECK_FALSE(model.m_links[i].m_isImage);

    //不接受的链接，文字仍在，只是没有链接样式
    CHECK(model.m_flatText.find(L"bad") != std::wstring::npos);
    const MdBlock& p = RootChild(model, 0);
    for (size_t i = 0; i < p.m_inlines.size(); ++i)
    {
        if (p.m_inlines[i].m_text == L"bad" || p.m_inlines[i].m_text == L"f")
        {
            CHECK(p.m_inlines[i].m_linkIndex == -1);
            CHECK((p.m_inlines[i].m_style & md_style_link) == 0);
        }
    }
}

TEST_CASE("MD13 图片不下载，转成带前缀的链接；说明为空时显示地址", "[mddll]")
{
    MdModel model = Parse("![logo](https://x.com/a.png) ![](https://x.com/b.png)", L"[img] ");
    REQUIRE(model.m_links.size() == 2);
    CHECK(model.m_links[0].m_isImage);
    CHECK(model.m_links[0].m_url == L"https://x.com/a.png");
    CHECK(model.m_flatText == L"[img] logo [img] https://x.com/b.png");

    //不给前缀时只显示说明
    CHECK(Parse("![logo](https://x.com/a.png)").m_flatText == L"logo");
}

TEST_CASE("MD14 认识的实体解码，不认识的按原样显示", "[mddll]")
{
    MdModel model = Parse("&amp; &lt; &#x4E2D; &#20013; &unknownent;");
    CHECK(model.m_flatText == W("& < \xE4\xB8\xAD \xE4\xB8\xAD &unknownent;"));
}

TEST_CASE("MD15 超过嵌套层数与表格列数上限时截断但仍保留文字", "[mddll]")
{
    std::string deep;
    for (int i = 0; i < kDeepNesting; ++i)
        deep += "> ";
    deep += "core";
    MdModel nested = Parse(deep);
    CHECK(nested.m_truncated);
    CHECK(nested.m_flatText.find(L"core") != std::wstring::npos);

    std::string header = "|";
    std::string sep = "|";
    std::string row = "|";
    for (int c = 0; c < kWideTableColumns; ++c)
    {
        header += " h |";
        sep += "---|";
        row += " v |";
    }
    MdModel wide = Parse(header + "\n" + sep + "\n" + row);
    const MdBlock& table = RootChild(wide, 0);
    CHECK(table.m_columnCount == kMdMaxTableColumns);
    CHECK(wide.m_truncated);
    for (size_t r = 0; r < table.m_children.size(); ++r)
        CHECK(Child(wide, table, r).m_children.size() <= static_cast<size_t>(kMdMaxTableColumns));
}

TEST_CASE("MD16 病态输入（大量未闭合标记、深层嵌套）在限定时间内解析完", "[mddll]")
{
    const char* const kPatterns[] = { "**a", "[", "`", "> ", "- ", "*_", "![", "|" };
    for (size_t p = 0; p < sizeof(kPatterns) / sizeof(kPatterns[0]); ++p)
    {
        std::string source;
        while (source.size() < kPathologicalBytes)
            source += kPatterns[p];
        INFO("pattern: " << kPatterns[p]);
        const DWORD start = ::GetTickCount();
        MdModel model;
        MdBuildOptions options;
        CHECK(BuildMdModel(source.data(), source.size(), options, model) == md_ok);
        CHECK(::GetTickCount() - start < kPathologicalParseLimitMs);
    }
}

TEST_CASE("MD17 非法 UTF-8 替换为 U+FFFD；代理对在 UTF-8 与 UTF-16 之间往返不变", "[mddll]")
{
    const char bad[] = { 'a', static_cast<char>(0xFF), 'b', static_cast<char>(0xC3) };
    const std::wstring w = Utf8ToWide(bad, sizeof(bad));
    REQUIRE(w.size() == 4);
    CHECK(w[0] == L'a');
    CHECK(w[1] == static_cast<wchar_t>(0xFFFD));
    CHECK(w[2] == L'b');
    CHECK(w[3] == static_cast<wchar_t>(0xFFFD));

    const char emoji[] = "\xF0\x9F\x98\x80";   //U+1F600
    const std::wstring we = Utf8ToWide(emoji, strlen(emoji));
    REQUIRE(we.size() == 2);
    CHECK(IsHighSurrogate(we[0]));
    CHECK(IsLowSurrogate(we[1]));
    CHECK(WideToUtf8(we) == std::string(emoji));

    //解析器也不因非法字节失败
    MdModel model = Parse(std::string(bad, sizeof(bad)));
    CHECK(model.m_flatText.size() == 4);
}

TEST_CASE("MD18 链接地址规范化", "[mddll]")
{
    CHECK(NormalizeLinkUrl(L"  https://a.com  ") == L"https://a.com");
    CHECK(NormalizeLinkUrl(L"HTTP://A.COM") == L"HTTP://A.COM");
    CHECK(NormalizeLinkUrl(L"mailto:x@y.z") == L"mailto:x@y.z");
    CHECK(NormalizeLinkUrl(L"www.a.com") == L"http://www.a.com");
    CHECK(NormalizeLinkUrl(L"x@y.z") == L"mailto:x@y.z");
    CHECK(NormalizeLinkUrl(L"javascript:alert(1)").empty());
    CHECK(NormalizeLinkUrl(L"file:///c:/a").empty());
    CHECK(NormalizeLinkUrl(L"/relative/path").empty());
    CHECK(NormalizeLinkUrl(L"").empty());
}

TEST_CASE("MD19 <u>文字</u> 解析为下划线（大小写不限），没闭合的 <u> 到段落结束；其它 HTML 标签按文字显示；"
          "代码里的 <u> 不处理", "[mddll]")
{
    //标签本身不出现在文字里，只有中间的文字带下划线
    const MdModel basic = Parse("a <u>under</u> b");
    const MdBlock& p = RootChild(basic, 0);
    CHECK(InlineText(p) == L"a under b");
    CHECK(StyleOf(p, L"under") == static_cast<uint32_t>(md_style_underline));
    CHECK((StyleOf(p, L"a ") & md_style_underline) == 0);
    CHECK((StyleOf(p, L" b") & md_style_underline) == 0);
    CHECK(basic.m_flatText == L"a under b");

    //大写标签；与加粗叠加
    const MdModel upper = Parse("<U>**x**</U>y");
    const MdBlock& q = RootChild(upper, 0);
    CHECK(StyleOf(q, L"x") == static_cast<uint32_t>(md_style_underline | md_style_bold));
    CHECK(StyleOf(q, L"y") == 0u);

    //没闭合的 <u>：到段落结束为止，下一段不受影响
    const MdModel open = Parse("<u>open\n\nnext");
    CHECK(StyleOf(RootChild(open, 0), L"open") == static_cast<uint32_t>(md_style_underline));
    CHECK(StyleOf(RootChild(open, 1), L"next") == 0u);

    //其它标签与以前一样按文字显示，不带下划线
    const MdModel other = Parse("<b>raw</b> <span>s</span>");
    CHECK(InlineText(RootChild(other, 0)) == L"<b>raw</b> <span>s</span>");
    CHECK(StyleOf(RootChild(other, 0), L"<b>raw</b> <span>s</span>") == 0u);

    //行内代码与代码块里的 <u> 原样保留
    const MdModel code = Parse("`<u>c</u>`\n\n```\n<u>d</u>\n```");
    CHECK(InlineText(RootChild(code, 0)) == L"<u>c</u>");
    CHECK((StyleOf(RootChild(code, 0), L"<u>c</u>") & md_style_underline) == 0);
    CHECK(RootChild(code, 1).m_codeRaw == L"<u>d</u>");

    //没有对应 <u> 的 </u> 丢掉
    CHECK(InlineText(RootChild(Parse("x</u>y"), 0)) == L"xy");
}

TEST_CASE("MD20 第一行为「[!类型]」的引用块解析为警告框（五种类型，大小写不限），类型行不进渲染后文字；"
          "类型不认得、类型行还有其它文字、类型行不在第一行时仍是普通引用", "[mddll]")
{
    //五种类型，大小写混用
    struct AlertCase
    {
        const char* m_source;   //源文本
        int         m_type;     //应解析出的类型
    };
    const AlertCase cases[] =
    {
        { "> [!NOTE]\n> body", md_alert_note },
        { "> [!tip]\n> body", md_alert_tip },
        { "> [!Important]\n> body", md_alert_important },
        { "> [!WARNING]\n> body", md_alert_warning },
        { "> [!CAUTION]\n> body", md_alert_caution },
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i)
    {
        INFO(cases[i].m_source);
        const MdModel m = Parse(cases[i].m_source);
        const MdBlock& q = RootChild(m, 0);
        CHECK(q.m_kind == md_block_quote);
        CHECK(q.m_alertType == cases[i].m_type);
        REQUIRE(q.m_children.size() == 1u);
        CHECK(InlineText(Child(m, q, 0)) == L"body");
        CHECK(m.m_flatText == L"body");
    }

    //只有类型行：警告框没有子块，渲染后文字为空
    const MdModel empty = Parse("> [!NOTE]");
    CHECK(RootChild(empty, 0).m_alertType == md_alert_note);
    CHECK(RootChild(empty, 0).m_children.empty());
    CHECK(empty.m_flatText.empty());

    //列表项里的警告框
    const MdModel nested = Parse("- > [!TIP]\n  > body");
    const MdBlock& item = Child(nested, RootChild(nested, 0), 0);
    CHECK(Child(nested, item, 0).m_alertType == md_alert_tip);

    //仍是普通引用，类型行按文字显示
    const char* const plain[] = { "> [!FOO]\n> body", "> [!NOTE] title\n> body", "> first\n> [!NOTE]" };
    for (size_t i = 0; i < sizeof(plain) / sizeof(plain[0]); ++i)
    {
        INFO(plain[i]);
        const MdModel m = Parse(plain[i]);
        CHECK(RootChild(m, 0).m_kind == md_block_quote);
        CHECK(RootChild(m, 0).m_alertType == md_alert_none);
    }
    CHECK(Parse("> [!FOO]\n> body").m_flatText == L"[!FOO]\nbody");
}
