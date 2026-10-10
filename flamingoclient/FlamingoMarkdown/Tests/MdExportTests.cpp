/**
 *  FlamingoMarkdown.dll 第二版接口的单元测试，用例 MX1 ~ MX6（docs/Markdown消息方案.md 第 11.5 节）：
 *
 *    - MX1 函数表第二、三版：大小不小于 kMdApiV2Size / kMdApiV3Size，新增的函数指针都不为空，版本号为 1.5
 *    - MX2 卡片标题与摘要：优先取标题块；没有标题时取第一行；摘要不含标题、空白合并、超长截断；空文档
 *    - MX3 HTML 导出：块与行内样式的标签、任务列表、有序列表起始编号、代码语言、表格表头与对齐、转义
 *    - MX4 HTML 里不出现不安全的链接，原始 HTML 按文字转义
 *    - MX5 分页：不切在行的中间（含同一表格行里高度不同的单元格），超过一页的单行硬切，页与页首尾相接
 *    - MX6 接口约定：参数检查、缓冲区约定、未排版时分页返回 md_not_laid_out
 *    - MX11 HTML 里的代码块按语法着色（第 12 节）：着色的段包进带颜色的 span、转义照旧；不写语言名时输出不变；
 *      切段不拆开代理对
 *    - MX13 ~ MX15 按选区导出 HTML（第 12 节，复制到剪贴板用）：段落里的半句、跨块与列表的起始编号、表格的行与
 *      代码块的一段
 *    - MX16 第四版接口 m_exportHtmlRange 的约定：片段标记、起止相同为空、顺序不限、越界截断、全选与整篇导出的正文
 *      相同、参数检查
 *
 *  框架 Catch2 v3，标签 [mddll]。
 *
 *  balloonwj@qq.com   2026-10-06
 */
#include "catch_amalgamated.hpp"

#include <windows.h>
#include <string.h>
#include <string>
#include <vector>

#include "../FlamingoMarkdownApi.h"
#include "../MdApiTable.h"
#include "../Core/MdBuilder.h"     // MX13 ~ MX15：解析成模型，按渲染后文字的下标选区
#include "../Core/MdCodeColor.h"   // MX11：MdCodeColorRunEnd
#include "../Core/MdHtml.h"        // MX13 ~ MX16：BuildMdHtmlRange 与片段标记
#include "../Core/MdPaginate.h"

using namespace flamingomd;

namespace {

//测试用的正文字体与字号
const wchar_t* const kMxFont = L"Microsoft YaHei";
const int kMxFontPx = 14;

//测试用的样式
MdStyle MxStyle()
{
    MdStyle s;
    memset(&s, 0, sizeof(s));
    s.m_cbSize = sizeof(s);
    s.m_fontName = kMxFont;
    s.m_fontPx = kMxFontPx;
    s.m_scalePercent = 100;
    s.m_textColor = RGB(0x2A, 0x2A, 0x2A);
    s.m_linkColor = RGB(0x2F, 0x6F, 0xD0);
    return s;
}

//UTF-16 转 UTF-8
std::string U8(const std::wstring& text)
{
    if (text.empty())
        return std::string();
    const int len = ::WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), NULL, 0, NULL, NULL);
    std::string out(static_cast<size_t>(len), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), &out[0], len, NULL, NULL);
    return out;
}

//解析，要求成功
MdDocument* MxParse(const MdApi* api, const std::string& source)
{
    MdDocument* doc = NULL;
    REQUIRE(api->m_parse(source.data(), static_cast<uint32_t>(source.size()), NULL, &doc) == md_ok);
    REQUIRE(doc != NULL);
    return doc;
}

//取卡片的一部分（先查长度再取）
std::wstring CardPart(const MdApi* api, const MdDocument* doc, int part)
{
    uint32_t needed = 0;
    const int rc = api->m_getCardText(doc, part, NULL, 0, &needed);
    REQUIRE(rc == md_buffer_too_small);
    std::vector<wchar_t> buf(needed + 1, 0);
    uint32_t written = 0;
    REQUIRE(api->m_getCardText(doc, part, &buf[0], needed + 1, &written) == md_ok);
    REQUIRE(written == needed);
    return std::wstring(&buf[0], written);
}

//取源文本的卡片标题与摘要
void CardOf(const std::wstring& source, std::wstring& title, std::wstring& summary)
{
    const MdApi* api = GetMdApiTable();
    MdDocument* doc = MxParse(api, U8(source));
    title = CardPart(api, doc, md_card_title);
    summary = CardPart(api, doc, md_card_summary);
    api->m_freeDoc(doc);
}

//导出 HTML（先查长度再取）
std::string HtmlOf(const std::string& source, const wchar_t* title)
{
    const MdApi* api = GetMdApiTable();
    MdDocument* doc = MxParse(api, source);
    MdHtmlOptions options;
    memset(&options, 0, sizeof(options));
    options.m_cbSize = sizeof(options);
    options.m_title = title;
    uint32_t needed = 0;
    REQUIRE(api->m_exportHtml(doc, &options, NULL, 0, &needed) == md_buffer_too_small);
    std::vector<char> buf(needed + 1, 0);
    uint32_t written = 0;
    REQUIRE(api->m_exportHtml(doc, &options, &buf[0], needed + 1, &written) == md_ok);
    REQUIRE(written == needed);
    api->m_freeDoc(doc);
    return std::string(&buf[0], written);
}

//是否包含
bool Has(const std::string& text, const char* piece)
{
    return text.find(piece) != std::string::npos;
}

//piece 在 text 里出现的次数
size_t CountOf(const std::string& text, const char* piece)
{
    size_t n = 0;
    const size_t len = strlen(piece);
    for (size_t at = text.find(piece); at != std::string::npos; at = text.find(piece, at + len))
        ++n;
    return n;
}

//解析成模型，要求成功（MX13 ~ MX15 直接调用 BuildMdHtmlRange）
MdModel RangeModel(const std::wstring& source)
{
    MdModel model;
    MdBuildOptions options;
    const std::string utf8 = U8(source);
    REQUIRE(BuildMdModel(utf8.data(), utf8.size(), options, model) == md_ok);
    return model;
}

//piece 在渲染后文字里的起点；找不到时用例失败
int FlatPos(const MdModel& model, const std::wstring& piece)
{
    const size_t at = model.m_flatText.find(piece);
    REQUIRE(at != std::wstring::npos);
    return static_cast<int>(at);
}

//按选区导出后两个片段标记之间的部分
std::string RangeFragment(const MdModel& model, int begin, int end)
{
    const std::string html = BuildMdHtmlRange(model, std::wstring(), begin, end);
    const size_t s = html.find(kMdHtmlFragmentStart);
    const size_t e = html.find(kMdHtmlFragmentEnd);
    REQUIRE(s != std::string::npos);
    REQUIRE(e != std::string::npos);
    REQUIRE(e > s);
    const size_t from = s + strlen(kMdHtmlFragmentStart);
    return html.substr(from, e - from);
}

//经函数表按选区导出 HTML（先查长度再取）
std::string HtmlRangeOf(const std::string& source, int begin, int end)
{
    const MdApi* api = GetMdApiTable();
    MdDocument* doc = MxParse(api, source);
    uint32_t needed = 0;
    REQUIRE(api->m_exportHtmlRange(doc, NULL, begin, end, NULL, 0, &needed) == md_buffer_too_small);
    std::vector<char> buf(needed + 1, 0);
    uint32_t written = 0;
    REQUIRE(api->m_exportHtmlRange(doc, NULL, begin, end, &buf[0], needed + 1, &written) == md_ok);
    REQUIRE(written == needed);
    api->m_freeDoc(doc);
    return std::string(&buf[0], written);
}

//HTML 里 <div class="md">\n 之后、closing 之前的正文
std::string BodyOf(const std::string& html, const char* closing)
{
    const char* const open = "<div class=\"md\">\n";
    const size_t s = html.find(open);
    REQUIRE(s != std::string::npos);
    const size_t from = s + strlen(open);
    const size_t e = html.find(closing, from);
    REQUIRE(e != std::string::npos);
    return html.substr(from, e - from);
}

//造一行
MdLine MxLine(int top, int bottom)
{
    MdLine line;
    line.m_top = top;
    line.m_bottom = bottom;
    return line;
}

} // namespace

TEST_CASE("MX1 函数表第二版：大小与新增的函数指针", "[mddll]")
{
    const MdApi* api = GetMdApiTable();
    REQUIRE(api != NULL);
    CHECK(api->m_cbSize >= kMdApiV2Size);
    CHECK(api->m_getCardText != NULL);
    CHECK(api->m_exportHtml != NULL);
    CHECK(api->m_paginate != NULL);
    CHECK(kMdApiV2Size > kMdApiMinSize);
    //第三版（Word 导出）在第二版之后追加
    CHECK(api->m_cbSize >= kMdApiV3Size);
    CHECK(api->m_exportDocx != NULL);
    CHECK(kMdApiV3Size > kMdApiV2Size);
    //1.3 起 MdStyle 追加了 m_layoutFlags（撑满宽度）；1.4 起函数表到第四版（按选区导出 HTML，见 MX16）；
    //1.5 起认 <u>…</u> 下划线（MD19）
    CHECK(wcsncmp(api->m_getVersion(), L"1.5", 3) == 0);
}

TEST_CASE("MX2 卡片标题与摘要", "[mddll]")
{
    std::wstring title;
    std::wstring summary;

    //优先取标题块，哪怕它不在第一行；摘要是其余文字，不含标题
    CardOf(L"开头的一段说明\n\n## 发布说明\n\n- **修复**登录超时\n- 修复上传中断", title, summary);
    CHECK(title == L"发布说明");
    CHECK(summary == L"开头的一段说明 修复登录超时 修复上传中断");

    //没有标题：取第一个块的第一行，同一块剩下的行进摘要
    CardOf(L"第一行\n第二行\n\n另一段", title, summary);
    CHECK(title == L"第一行");
    CHECK(summary == L"第二行 另一段");

    //只有代码块：标题为代码的第一行
    CardOf(L"```cpp\nint main()\n{\n}\n```", title, summary);
    CHECK(title == L"int main()");
    CHECK(summary == L"{ }");

    //表格单元格之间、换行处都压成单个空格
    CardOf(L"# T\n\n| a | b |\n| - | - |\n| 1 | 2 |", title, summary);
    CHECK(title == L"T");
    CHECK(summary == L"a b 1 2");

    //空文档、只有分隔线的文档：都为空
    CardOf(L"", title, summary);
    CHECK(title.empty());
    CHECK(summary.empty());
    CardOf(L"---", title, summary);
    CHECK(title.empty());
    CHECK(summary.empty());

    //超长摘要截断，末尾加省略号
    std::wstring longText = L"# 标题\n\n";
    for (int i = 0; i < 100; ++i)
        longText += L"很长的正文 ";
    CardOf(longText, title, summary);
    CHECK(summary.size() <= 401);
    CHECK(summary[summary.size() - 1] == L'\x2026');
}

TEST_CASE("MX3 HTML 导出的标签与结构", "[mddll]")
{
    const std::string source = U8(
        L"# 发布说明\n\n"
        L"**粗体** *斜体* ~~删除~~ `代码` [链接](https://example.com/a?b=1&c=2)\n\n"
        L"- [x] 已完成\n- [ ] 待办\n\n"
        L"3. 第三\n4. 第四\n\n"
        L"> 引用\n\n"
        L"```cpp\nif (a < b) {}\n```\n\n"
        L"| 左 | 中 |\n| :- | :-: |\n| 1 | 2 |\n\n"
        L"---\n");
    const std::string html = HtmlOf(source, L"发布 <说明>");
    CHECK(Has(html, "<!DOCTYPE html>"));
    CHECK(Has(html, "<meta charset=\"utf-8\">"));
    CHECK(Has(html, U8(L"<title>发布 &lt;说明&gt;</title>").c_str()));
    CHECK(Has(html, U8(L"<h1>发布说明</h1>").c_str()));
    CHECK(Has(html, U8(L"<strong>粗体</strong>").c_str()));
    CHECK(Has(html, U8(L"<em>斜体</em>").c_str()));
    CHECK(Has(html, U8(L"<del>删除</del>").c_str()));
    CHECK(Has(html, U8(L"<code>代码</code>").c_str()));
    CHECK(Has(html, U8(L"<a href=\"https://example.com/a?b=1&amp;c=2\">链接</a>").c_str()));
    CHECK(Has(html, "<ul class=\"task\">"));
    CHECK(Has(html, "<input type=\"checkbox\" disabled checked>"));
    CHECK(Has(html, "<ol start=\"3\">"));
    CHECK(Has(html, "<blockquote>"));
    //写了认得的语言名：关键字按语法着色（MX11）
    CHECK(Has(html, "<pre><code class=\"language-cpp\"><span style=\"color:#0000FF\">if</span> (a &lt; b) {}</code></pre>"));
    CHECK(Has(html, "<thead>"));
    CHECK(Has(html, U8(L"<th style=\"text-align:center\">中</th>").c_str()));
    CHECK(Has(html, "<td>1</td>"));
    CHECK(Has(html, "<hr>"));
    //没有 BOM
    CHECK(html.compare(0, 3, "\xEF\xBB\xBF") != 0);
}

TEST_CASE("MX4 HTML 里没有不安全的链接，原始 HTML 按文字转义", "[mddll]")
{
    const std::string html = HtmlOf("<script>alert(1)</script>\n\n[x](javascript:alert(1)) <b>raw</b>", NULL);
    CHECK_FALSE(Has(html, "<script>"));
    CHECK(Has(html, "&lt;script&gt;"));
    CHECK_FALSE(Has(html, "href=\"javascript:"));
    CHECK(Has(html, "&lt;b&gt;raw&lt;/b&gt;"));
    //没有给标题时写「Markdown」
    CHECK(Has(html, "<title>Markdown</title>"));
}

TEST_CASE("MX5 分页不切在行的中间，超高的单行硬切", "[mddll]")
{
    //十行，每行高 30：页高 100 时每页放三行
    MdLayoutResult layout;
    for (int i = 0; i < 10; ++i)
        layout.m_lines.push_back(MxLine(i * 30, i * 30 + 30));
    layout.m_height = 300;
    std::vector<int> starts;
    PaginateMdLayout(layout, 100, starts);
    REQUIRE(starts.size() == 4);
    CHECK(starts[0] == 0);
    CHECK(starts[1] == 90);
    CHECK(starts[2] == 180);
    CHECK(starts[3] == 270);

    //同一表格行里两个单元格：左格 [60, 90)，右格 [60, 140)；90 落在右格中间，不能在那里切
    MdLayoutResult table;
    table.m_lines.push_back(MxLine(0, 30));
    table.m_lines.push_back(MxLine(30, 60));
    table.m_lines.push_back(MxLine(60, 90));
    table.m_lines.push_back(MxLine(60, 140));
    table.m_lines.push_back(MxLine(140, 170));
    table.m_height = 170;
    PaginateMdLayout(table, 100, starts);
    //第一页只能切在 60（90 在右格中间）；第二页从 60 起最多到 160，放不下到 170 的最后一行，在 140 再切
    REQUIRE(starts.size() == 3);
    CHECK(starts[1] == 60);
    CHECK(starts[2] == 140);

    //一行就比一页高：从页面高度处硬切
    MdLayoutResult tall;
    tall.m_lines.push_back(MxLine(0, 250));
    tall.m_height = 250;
    PaginateMdLayout(tall, 100, starts);
    REQUIRE(starts.size() == 3);
    CHECK(starts[1] == 100);
    CHECK(starts[2] == 200);

    //放得下一页：只有一页
    PaginateMdLayout(table, 500, starts);
    CHECK(starts.size() == 1);
    //空文档：只有一页
    MdLayoutResult empty;
    PaginateMdLayout(empty, 100, starts);
    CHECK(starts.size() == 1);
}

TEST_CASE("MX6 第二版接口的参数检查与缓冲区约定", "[mddll]")
{
    const MdApi* api = GetMdApiTable();
    MdDocument* doc = MxParse(api, U8(L"# 标题\n\n正文"));
    uint32_t n = 0;
    wchar_t wbuf[4] = { 0 };
    //卡片：part 不认识、文档为 NULL、出参为 NULL
    CHECK(api->m_getCardText(doc, 7, wbuf, 4, &n) == md_bad_argument);
    CHECK(api->m_getCardText(NULL, md_card_title, wbuf, 4, &n) == md_bad_argument);
    CHECK(api->m_getCardText(doc, md_card_title, wbuf, 4, NULL) == md_bad_argument);
    //缓冲区刚好不够（标题两个字需要 3 个 wchar_t）
    CHECK(api->m_getCardText(doc, md_card_title, wbuf, 2, &n) == md_buffer_too_small);
    CHECK(n == 2);
    CHECK(api->m_getCardText(doc, md_card_title, wbuf, 3, &n) == md_ok);
    CHECK(std::wstring(wbuf) == L"标题");
    //HTML：options 可以为 NULL
    CHECK(api->m_exportHtml(doc, NULL, NULL, 0, &n) == md_buffer_too_small);
    CHECK(n > 0);
    CHECK(api->m_exportHtml(NULL, NULL, NULL, 0, &n) == md_bad_argument);

    //分页：未排版时返回 md_not_laid_out；页高必须大于 0
    int32_t starts[8] = { 0 };
    CHECK(api->m_paginate(doc, 100, starts, 8, &n) == md_not_laid_out);
    CHECK(api->m_paginate(doc, 0, starts, 8, &n) == md_bad_argument);
    HDC hdc = ::CreateCompatibleDC(NULL);
    REQUIRE(hdc != NULL);
    const MdStyle style = MxStyle();
    int w = 0;
    int h = 0;
    REQUIRE(api->m_layout(doc, hdc, &style, 400, &w, &h) == md_ok);
    ::DeleteDC(hdc);
    //页高很大：一页；starts 为 NULL 时只报页数
    CHECK(api->m_paginate(doc, 100000, NULL, 0, &n) == md_buffer_too_small);
    CHECK(n == 1);
    CHECK(api->m_paginate(doc, 100000, starts, 8, &n) == md_ok);
    CHECK(starts[0] == 0);
    api->m_freeDoc(doc);
}

TEST_CASE("MX11 HTML 里的代码块按语法着色：着色的段包进带颜色的 span、转义照旧；不写语言名时不变；切段不拆开代理对",
          "[mddll]")
{
    const std::string html = HtmlOf("```cpp\nreturn a < 42; // done\n```", NULL);
    CHECK(Has(html, "<pre><code class=\"language-cpp\"><span style=\"color:#0000FF\">return</span> a &lt; "
                    "<span style=\"color:#098658\">42</span>; <span style=\"color:#008000\">// done</span></code></pre>"));

    //不写语言名：与着色之前的输出相同
    const std::string plain = HtmlOf("```\nreturn a < 42; // done\n```", NULL);
    CHECK(Has(plain, "<pre><code>return a &lt; 42; // done</code></pre>"));
    CHECK_FALSE(Has(plain, "<span"));

    //字符串里的 emoji（代理对）完整地留在同一段里，UTF-8 的四个字节都在
    const std::string emoji = HtmlOf(U8(L"```js\nlet s = \"\xD83D\xDE00\";\n```"), NULL);
    CHECK(Has(emoji, "\xF0\x9F\x98\x80"));

    //切段：着色种类在代理对中间改变时，终点挪到代理对之后
    const wchar_t text[] = { L'a', 0xD83D, 0xDE00, L'b' };
    const unsigned char kinds[] = { 1, 1, 2, 2 };
    CHECK(MdCodeColorRunEnd(text, 4, kinds, 0) == 3);
    CHECK(MdCodeColorRunEnd(text, 4, kinds, 3) == 4);
    //种类不变时一直到范围终点
    const unsigned char same[] = { 3, 3, 3, 3 };
    CHECK(MdCodeColorRunEnd(text, 4, same, 0) == 4);
    CHECK(MdCodeColorRunEnd(text, 2, same, 0) == 2);
}

TEST_CASE("MX13 按选区导出 HTML：段落里只选半句时只含选中的文字，加粗、链接的标签完整闭合", "[mddll]")
{
    const MdModel model = RangeModel(L"前言 **粗体文字** 和 [链接](https://a.com/x) 结尾");

    //从加粗的后半到链接的末尾
    const std::string frag = RangeFragment(model, FlatPos(model, L"文字"), FlatPos(model, L"链接") + 2);
    CHECK(frag == U8(L"<div class=\"md\">\n<p><strong>文字</strong> 和 <a href=\"https://a.com/x\">链接</a></p>\n</div>"));

    //加粗里面的一段
    const int inner = FlatPos(model, L"体文");
    CHECK(RangeFragment(model, inner, inner + 2) == U8(L"<div class=\"md\">\n<p><strong>体文</strong></p>\n</div>"));

    //链接的一半：<a> 照样闭合
    const int link = FlatPos(model, L"链接");
    CHECK(RangeFragment(model, link, link + 1)
          == U8(L"<div class=\"md\">\n<p><a href=\"https://a.com/x\">链</a></p>\n</div>"));
}

TEST_CASE("MX14 按选区导出 HTML：跨块时含各块的标签；列表只含选中的项，有序列表的起始编号等于第一项的实际编号",
          "[mddll]")
{
    const MdModel model = RangeModel(L"# 标题一\n\n1. 第一项\n2. 第二项\n3. 第三项\n\n尾段");

    //从标题的后半到第一项：标题，加上只有一项、从 1 开始（不写 start）的有序列表
    const std::string head = RangeFragment(model, FlatPos(model, L"题一"), FlatPos(model, L"第一项") + 3);
    CHECK(Has(head, U8(L"<h1>题一</h1>").c_str()));
    CHECK(Has(head, "<ol>\n"));
    CHECK(CountOf(head, "<li>") == 1);
    CHECK(Has(head, U8(L"第一项").c_str()));
    CHECK_FALSE(Has(head, U8(L"第二项").c_str()));
    CHECK_FALSE(Has(head, U8(L"尾段").c_str()));

    //从第二项的后半到末尾：列表从 2 开始，含第二、三项，后面是尾段，没有标题
    const int length = static_cast<int>(model.m_flatText.size());
    const std::string tail = RangeFragment(model, FlatPos(model, L"二项"), length);
    CHECK(Has(tail, "<ol start=\"2\">"));
    CHECK(CountOf(tail, "<li>") == 2);
    CHECK(Has(tail, U8(L"二项").c_str()));
    CHECK_FALSE(Has(tail, U8(L"第二项").c_str()));
    CHECK(Has(tail, U8(L"第三项").c_str()));
    CHECK(Has(tail, U8(L"<p>尾段</p>").c_str()));
    CHECK_FALSE(Has(tail, "<h1>"));
    CHECK_FALSE(Has(tail, U8(L"第一项").c_str()));

    //写了起始编号的列表：从第二项开始选，编号接得上
    const MdModel numbered = RangeModel(L"3. 第三\n4. 第四\n5. 第五");
    const std::string from4 = RangeFragment(numbered, FlatPos(numbered, L"第四"),
                                            static_cast<int>(numbered.m_flatText.size()));
    CHECK(Has(from4, "<ol start=\"4\">"));
    CHECK(CountOf(from4, "<li>") == 2);
}

TEST_CASE("MX15 按选区导出 HTML：表格只含相交的行、列数不变；代码块只含选中的一段，颜色按整块代码算", "[mddll]")
{
    const MdModel table = RangeModel(L"| 名称 | 数量 |\n| --- | --: |\n| 苹果 | 3 |\n| 香蕉 | 5 |\n| 橙子 | 7 |");
    const std::string rows = RangeFragment(table, FlatPos(table, L"果"), FlatPos(table, L"香蕉") + 2);
    CHECK_FALSE(Has(rows, "<thead>"));
    CHECK(Has(rows, "<tbody>"));
    CHECK(CountOf(rows, "<tr>") == 2);
    CHECK(CountOf(rows, "<td") == 4);
    CHECK(Has(rows, U8(L"<td>果</td>").c_str()));
    CHECK(Has(rows, "<td style=\"text-align:right\">3</td>"));
    CHECK(Has(rows, U8(L"<td>香蕉</td>").c_str()));
    //选区在「香蕉」之后结束：同一行的「5」不在选区内，单元格留空
    CHECK(Has(rows, "<td style=\"text-align:right\"></td>"));
    CHECK_FALSE(Has(rows, U8(L"名称").c_str()));
    CHECK_FALSE(Has(rows, U8(L"橙子").c_str()));

    //代码块从字符串中间开始选：只含选中的一段，仍是字符串的颜色
    const MdModel code = RangeModel(L"```cpp\nint a = 1;\nreturn \"str\";\n```");
    const std::string part = RangeFragment(code, FlatPos(code, L"tr\""), static_cast<int>(code.m_flatText.size()));
    CHECK(Has(part, "<pre><code class=\"language-cpp\"><span style=\"color:#A31515\">tr&quot;</span>;</code></pre>"));
    CHECK_FALSE(Has(part, "int"));
}

TEST_CASE("MX16 第四版接口 m_exportHtmlRange：片段标记、全选与整篇导出的正文相同、顺序不限、越界截断、起止相同为空、"
          "参数检查", "[mddll]")
{
    const MdApi* api = GetMdApiTable();
    REQUIRE(api->m_cbSize >= kMdApiV4Size);
    REQUIRE(api->m_exportHtmlRange != NULL);
    CHECK(kMdApiV4Size > kMdApiV3Size);

    const std::wstring wide =
        L"# 发布说明\n\n"
        L"**粗体** *斜体* ~~删除~~ `代码` [链接](https://example.com/a?b=1&c=2)\n\n"
        L"- [x] 已完成\n- [ ] 待办\n\n"
        L"3. 第三\n4. 第四\n\n"
        L"> 引用\n\n"
        L"```cpp\nif (a < b)\t{}\n```\n\n"
        L"| 左 | 中 |\n| :- | :-: |\n| 1 | 2 |\n\n"
        L"---\n";
    const std::string source = U8(wide);
    const int length = static_cast<int>(RangeModel(wide).m_flatText.size());

    //全选：正文与整篇导出相同（代码块整块选中时用原文，制表符不展开；文末的分隔线也在内），前后带片段标记
    const std::string all = HtmlRangeOf(source, 0, length);
    CHECK(Has(all, "<body>\n<!--StartFragment--><div class=\"md\">\n"));
    CHECK(Has(all, "</div><!--EndFragment-->\n</body>\n</html>\n"));
    CHECK(BodyOf(all, "</div><!--EndFragment-->") == BodyOf(HtmlOf(source, NULL), "</div>\n</body>"));
    CHECK(Has(all, "<hr>"));
    //头部与样式表和整篇导出相同，没给标题时写「Markdown」
    CHECK(Has(all, "<title>Markdown</title>"));
    CHECK(Has(all, "<style>"));

    //顺序不限、越界截断
    CHECK(HtmlRangeOf(source, 12, 3) == HtmlRangeOf(source, 3, 12));
    CHECK(HtmlRangeOf(source, -5, length + 100) == all);

    MdDocument* doc = MxParse(api, source);
    //起止相同：内容为空，只写结尾的 0
    char one[1] = { 'x' };
    uint32_t n = 99;
    CHECK(api->m_exportHtmlRange(doc, NULL, 5, 5, one, 1, &n) == md_ok);
    CHECK(n == 0);
    CHECK(one[0] == 0);

    //标题写进 <title>
    MdHtmlOptions options;
    memset(&options, 0, sizeof(options));
    options.m_cbSize = sizeof(options);
    options.m_title = L"T";
    uint32_t needed = 0;
    REQUIRE(api->m_exportHtmlRange(doc, &options, 0, 3, NULL, 0, &needed) == md_buffer_too_small);
    std::vector<char> buf(needed + 1, 0);
    REQUIRE(api->m_exportHtmlRange(doc, &options, 0, 3, &buf[0], needed + 1, &n) == md_ok);
    CHECK(Has(std::string(&buf[0], n), "<title>T</title>"));

    //参数检查
    CHECK(api->m_exportHtmlRange(NULL, NULL, 0, 1, NULL, 0, &n) == md_bad_argument);
    CHECK(api->m_exportHtmlRange(doc, NULL, 0, 1, NULL, 0, NULL) == md_bad_argument);
    api->m_freeDoc(doc);
}
