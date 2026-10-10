/**
 *  FlamingoMarkdown.dll Word 导出（第三版接口）的单元测试，用例 MX7 ~ MX10（docs/Markdown消息方案.md 第 11.6 节）：
 *
 *    - MX7  ZIP 包：六个部件都在、文件名正确、CRC 与大小一致；CRC-32 的标准校验值
 *    - MX8  document.xml 的内容：标题样式、超链接与关系、有序列表起始编号、嵌套级别、任务项、代码块、表格、引用、分隔线、转义
 *    - MX9  每一份 XML 都是合法的（标签成对、根元素正确）
 *    - MX10 接口约定：缓冲区约定、参数检查、文件以 ZIP 签名开头
 *    - MX12 代码块语法着色（第 12 节）：着色的 run 带 w:color，制表符仍为 <w:tab/>，XML 合法；不写语言名时没有颜色
 *    - MX17 下划线（第 13 节）：HTML 写 <u>、Word 写 <w:u w:val="single"/>；渲染后文字与卡片摘要里没有标签
 *    - MX18 警告框（第 13 节）：HTML 的 <div> 与标题段、Word 的彩色标题段与段落边框底色、宿主设置的标题
 *
 *  框架 Catch2 v3，标签 [mddll]。Word / WPS 实际打开的核对见 docs/Markdown消息方案.md 第 11 节的实施记录。
 *
 *  balloonwj@qq.com   2026-10-06
 */
#include "catch_amalgamated.hpp"

#include <windows.h>
#include <string.h>
#include <map>
#include <string>
#include <vector>

#include "../FlamingoMarkdownApi.h"
#include "../MdApiTable.h"
#include "../Core/MdAlert.h"     // MX18：SetMdAlertTitles
#include "../Core/MdBuilder.h"
#include "../Core/MdDocx.h"
#include "../Core/MdHtml.h"      // MX17：BuildMdHtml
#include "../Core/MdOutline.h"   // MX17：BuildMdCardText

using namespace flamingomd;

namespace {

//UTF-16 转 UTF-8
std::string U8x(const std::wstring& text)
{
    if (text.empty())
        return std::string();
    const int len = ::WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), NULL, 0, NULL, NULL);
    std::string out(static_cast<size_t>(len), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), &out[0], len, NULL, NULL);
    return out;
}

//小端读取 16 位与 32 位整数
uint32_t Get16(const std::string& s, size_t at)
{
    return static_cast<unsigned char>(s[at]) | (static_cast<unsigned char>(s[at + 1]) << 8);
}

uint32_t Get32(const std::string& s, size_t at)
{
    return Get16(s, at) | (Get16(s, at + 2) << 16);
}

/**
 *  解开不压缩的 ZIP：按中央目录逐项读出，并核对本地文件头里的 CRC 与大小。
 *  @return 文件名 → 内容；结构不对时返回空表
 */
std::map<std::string, std::string> Unzip(const std::string& zip)
{
    std::map<std::string, std::string> files;
    const size_t endAt = zip.rfind(std::string("PK\x05\x06", 4));
    if (endAt == std::string::npos)
        return files;
    const uint32_t count = Get16(zip, endAt + 10);
    size_t p = Get32(zip, endAt + 16);
    for (uint32_t i = 0; i < count; ++i)
    {
        if (Get32(zip, p) != 0x02014b50)
            return std::map<std::string, std::string>();
        const uint32_t crc = Get32(zip, p + 16);
        const uint32_t size = Get32(zip, p + 24);
        const uint32_t nameLen = Get16(zip, p + 28);
        const uint32_t extraLen = Get16(zip, p + 30);
        const uint32_t commentLen = Get16(zip, p + 32);
        const uint32_t local = Get32(zip, p + 42);
        const std::string name = zip.substr(p + 46, nameLen);
        //本地文件头：签名、不压缩、CRC 与大小与中央目录一致
        if (Get32(zip, local) != 0x04034b50 || Get16(zip, local + 8) != 0 || Get32(zip, local + 14) != crc
            || Get32(zip, local + 18) != size)
            return std::map<std::string, std::string>();
        const size_t dataAt = local + 30 + Get16(zip, local + 26) + Get16(zip, local + 28);
        const std::string data = zip.substr(dataAt, size);
        if (MdCrc32(data.data(), data.size()) != crc)
            return std::map<std::string, std::string>();
        files[name] = data;
        p += 46 + nameLen + extraLen + commentLen;
    }
    return files;
}

/**
 *  简单的 XML 合法性检查：跳过声明与注释，开始标签与结束标签按名字成对，属性值的引号成对；不检查实体。
 *  @param rootName [出参] 根元素的名字
 */
bool WellFormed(const std::string& xml, std::string& rootName)
{
    std::vector<std::string> stack;
    rootName.clear();
    size_t i = 0;
    while (i < xml.size())
    {
        const size_t lt = xml.find('<', i);
        if (lt == std::string::npos)
            break;
        //文字里不能有裸的 >（开始标签外出现的 > 说明有没转义的字符）
        if (xml.find('>', i) < lt)
            return false;
        if (xml.compare(lt, 2, "<?") == 0)
        {
            const size_t end = xml.find("?>", lt);
            if (end == std::string::npos)
                return false;
            i = end + 2;
            continue;
        }
        //标签结束位置：跳过引号里的内容
        size_t j = lt + 1;
        char quote = 0;
        while (j < xml.size() && (quote != 0 || xml[j] != '>'))
        {
            if (quote == 0 && (xml[j] == '"' || xml[j] == '\''))
                quote = xml[j];
            else if (quote != 0 && xml[j] == quote)
                quote = 0;
            else if (quote == 0 && xml[j] == '<')
                return false;
            ++j;
        }
        if (j >= xml.size())
            return false;
        const std::string tag = xml.substr(lt + 1, j - lt - 1);
        i = j + 1;
        if (!tag.empty() && tag[0] == '/')
        {
            const std::string name = tag.substr(1);
            if (stack.empty() || stack.back() != name)
                return false;
            stack.pop_back();
            continue;
        }
        const size_t nameEnd = tag.find_first_of(" \t\r\n/");
        const std::string name = tag.substr(0, nameEnd);
        if (name.empty())
            return false;
        if (rootName.empty())
            rootName = name;
        if (tag[tag.size() - 1] != '/')
            stack.push_back(name);
    }
    return stack.empty() && !rootName.empty();
}

//解析源文本得到模型
MdModel ModelOf(const std::wstring& source)
{
    const std::string utf8 = U8x(source);
    MdModel model;
    MdBuildOptions options;
    REQUIRE(BuildMdModel(utf8.data(), utf8.size(), options, model) == md_ok);
    return model;
}

//覆盖全部语法的测试文档
const wchar_t* const kDocxSource =
    L"# 发布说明\n\n"
    L"**粗体** *斜体* ~~删除~~ `代码` [链接](https://example.com/a?b=1&c=2) <b>raw</b>\n\n"
    L"- 第一项\n  - 嵌套项\n- 第二项\n\n"
    L"3. 第三\n4. 第四\n\n"
    L"- [x] 已完成\n- [ ] 待办\n\n"
    L"> 引用\n\n"
    L"```cpp\nint main()\n{\n    x =\t0;\n}\n```\n\n"
    L"| 左 | 中 |\n| :- | :-: |\n| 1 |\n\n"
    L"---\n";

bool Has(const std::string& text, const std::string& piece)
{
    return text.find(piece) != std::string::npos;
}

/**
 *  document.xml 里各个代码块段落（CodeBlock 样式）的文字：同一段落里各 run 的 <w:t> 文字依次拼接，<w:tab/> 记成
 *  制表符。代码着色把一行切成几个 run，按整行比对文字不受切法影响。
 */
std::vector<std::string> CodeParagraphTexts(const std::string& doc)
{
    std::vector<std::string> lines;
    const std::string paraOpen = "<w:p>";
    const std::string paraClose = "</w:p>";
    const std::string textOpen = "<w:t xml:space=\"preserve\">";
    const std::string textClose = "</w:t>";
    const std::string tab = "<w:tab/>";
    size_t at = doc.find(paraOpen);
    while (at != std::string::npos)
    {
        const size_t end = doc.find(paraClose, at);
        if (end == std::string::npos)
            break;
        const std::string para = doc.substr(at, end - at);
        if (para.find("<w:pStyle w:val=\"CodeBlock\"/>") != std::string::npos)
        {
            std::string text;
            size_t p = 0;
            for (;;)
            {
                const size_t t = para.find(textOpen, p);
                const size_t b = para.find(tab, p);
                if (t == std::string::npos && b == std::string::npos)
                    break;
                if (b != std::string::npos && (t == std::string::npos || b < t))
                {
                    text += '\t';
                    p = b + tab.size();
                    continue;
                }
                const size_t close = para.find(textClose, t);
                REQUIRE(close != std::string::npos);
                text += para.substr(t + textOpen.size(), close - t - textOpen.size());
                p = close + textClose.size();
            }
            lines.push_back(text);
        }
        at = doc.find(paraOpen, end);
    }
    return lines;
}

} // namespace

TEST_CASE("MX7 Word 导出的 ZIP 包：部件齐全，CRC 与大小一致", "[mddll]")
{
    //CRC-32 的标准校验值
    CHECK(MdCrc32("123456789", 9) == 0xCBF43926u);
    CHECK(MdCrc32("", 0) == 0u);

    const std::map<std::string, std::string> files = Unzip(BuildMdDocx(ModelOf(kDocxSource)));
    REQUIRE(files.size() == 6);
    const char* const names[] =
    {
        "[Content_Types].xml", "_rels/.rels", "word/document.xml", "word/styles.xml", "word/numbering.xml",
        "word/_rels/document.xml.rels",
    };
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
    {
        INFO("part " << names[i]);
        CHECK(files.count(names[i]) == 1);
    }

    //打包函数本身：两项往返不变
    std::vector<MdZipEntry> entries(2);
    entries[0].m_name = "a.txt";
    entries[0].m_data = "hello";
    entries[1].m_name = "dir/b.bin";
    entries[1].m_data = std::string("\x00\x01\x02", 3);
    const std::map<std::string, std::string> back = Unzip(BuildMdStoredZip(entries));
    REQUIRE(back.size() == 2);
    CHECK(back.find("a.txt")->second == "hello");
    CHECK(back.find("dir/b.bin")->second == std::string("\x00\x01\x02", 3));
}

TEST_CASE("MX8 document.xml 里各种语法的写法", "[mddll]")
{
    std::vector<std::wstring> links;
    std::vector<unsigned> starts;
    const std::string doc = BuildMdDocxDocumentXml(ModelOf(kDocxSource), links, starts);
    //标题：内置标题样式；标题里的文字不再重复加粗
    CHECK(Has(doc, "<w:pStyle w:val=\"Heading1\"/>"));
    //行内样式
    CHECK(Has(doc, "<w:b/>"));
    CHECK(Has(doc, "<w:i/>"));
    CHECK(Has(doc, "<w:strike/>"));
    CHECK(Has(doc, "<w:rFonts w:ascii=\"Consolas\""));
    //超链接：rId3，地址交给关系文件
    REQUIRE(links.size() == 1);
    CHECK(links[0] == L"https://example.com/a?b=1&c=2");
    CHECK(Has(doc, "<w:hyperlink r:id=\"rId3\""));
    CHECK(Has(doc, "<w:rStyle w:val=\"Hyperlink\"/>"));
    //原始 HTML 按文字转义
    CHECK(Has(doc, "&lt;b&gt;raw&lt;/b&gt;"));
    //无序列表 numId 1，嵌套项为第 1 级；有序列表 numId 2，起始编号 3
    CHECK(Has(doc, "<w:ilvl w:val=\"0\"/><w:numId w:val=\"1\"/>"));
    CHECK(Has(doc, "<w:ilvl w:val=\"1\"/><w:numId w:val=\"1\"/>"));
    CHECK(Has(doc, "<w:numId w:val=\"2\"/>"));
    REQUIRE(starts.size() == 1);
    CHECK(starts[0] == 3u);
    //任务项：勾选框符号，不带列表编号
    CHECK(Has(doc, U8x(L"\x2611 ")));
    CHECK(Has(doc, U8x(L"\x2610 ")));
    //引用与代码块
    CHECK(Has(doc, "<w:pStyle w:val=\"Quote\"/>"));
    CHECK(Has(doc, "<w:pStyle w:val=\"CodeBlock\"/>"));
    //代码块每行一个段落；行中间的制表符写成 <w:tab/>（行首的 Tab 属于缩进，md4c 已展开成空格，见 MD8）；行首空格
    //原样保留。按整行比对文字：语言名为 cpp，一行被语法着色切成几个 run（着色本身见 MX12）
    CHECK(Has(doc, "<w:r><w:tab/></w:r>"));
    const std::vector<std::string> codeLines = CodeParagraphTexts(doc);
    REQUIRE(codeLines.size() == 4);
    CHECK(codeLines[0] == "int main()");
    CHECK(codeLines[1] == "{");
    CHECK(codeLines[2] == "    x =\t0;");
    CHECK(codeLines[3] == "}");
    //表格：表头行重复显示、加底色，第二列居中；少写的单元格由 md4c 补成空格子，两行共四个单元格
    CHECK(Has(doc, "<w:tblHeader/>"));
    CHECK(Has(doc, "w:fill=\"EBE9E4\""));
    CHECK(Has(doc, "<w:jc w:val=\"center\"/>"));
    size_t cells = 0;
    for (size_t at = doc.find("<w:tc>"); at != std::string::npos; at = doc.find("<w:tc>", at + 1))
        ++cells;
    CHECK(cells == 4);
    //分隔线与页面设置
    CHECK(Has(doc, "<w:pBdr><w:bottom w:val=\"single\""));
    CHECK(Has(doc, "<w:pgSz w:w=\"11906\" w:h=\"16838\"/>"));

    //XML 不允许的控制字符被去掉
    const std::string ctrl = BuildMdDocxDocumentXml(ModelOf(std::wstring(L"a") + wchar_t(0x01) + L"b"), links, starts);
    CHECK(Has(ctrl, ">ab</w:t>"));
}

TEST_CASE("MX9 每一份 XML 都是合法的", "[mddll]")
{
    const std::map<std::string, std::string> files = Unzip(BuildMdDocx(ModelOf(kDocxSource)));
    REQUIRE(files.size() == 6);
    const char* const roots[][2] =
    {
        { "[Content_Types].xml", "Types" },
        { "_rels/.rels", "Relationships" },
        { "word/document.xml", "w:document" },
        { "word/styles.xml", "w:styles" },
        { "word/numbering.xml", "w:numbering" },
        { "word/_rels/document.xml.rels", "Relationships" },
    };
    for (size_t i = 0; i < sizeof(roots) / sizeof(roots[0]); ++i)
    {
        INFO("part " << roots[i][0]);
        std::string root;
        CHECK(WellFormed(files.find(roots[i][0])->second, root));
        CHECK(root == roots[i][1]);
    }
    //关系文件里有超链接，地址里的 & 已转义
    const std::string& rels = files.find("word/_rels/document.xml.rels")->second;
    CHECK(Has(rels, "Target=\"https://example.com/a?b=1&amp;c=2\" TargetMode=\"External\""));
    //编号定义里有起始编号覆写
    CHECK(Has(files.find("word/numbering.xml")->second, "<w:startOverride w:val=\"3\"/>"));
    //空文档也能生成合法的 document.xml
    std::string root;
    CHECK(WellFormed(Unzip(BuildMdDocx(ModelOf(L""))).find("word/document.xml")->second, root));
}

TEST_CASE("MX10 m_exportDocx 的接口约定", "[mddll]")
{
    const MdApi* api = GetMdApiTable();
    const std::string source = U8x(L"# 标题\n\n正文");
    MdDocument* doc = NULL;
    REQUIRE(api->m_parse(source.data(), static_cast<uint32_t>(source.size()), NULL, &doc) == md_ok);
    uint32_t n = 0;
    CHECK(api->m_exportDocx(NULL, NULL, 0, &n) == md_bad_argument);
    CHECK(api->m_exportDocx(doc, NULL, 0, NULL) == md_bad_argument);
    REQUIRE(api->m_exportDocx(doc, NULL, 0, &n) == md_buffer_too_small);
    REQUIRE(n > 0);
    std::vector<unsigned char> buf(n, 0);
    uint32_t written = 0;
    //容量少一个字节也不够
    CHECK(api->m_exportDocx(doc, &buf[0], n - 1, &written) == md_buffer_too_small);
    REQUIRE(api->m_exportDocx(doc, &buf[0], n, &written) == md_ok);
    CHECK(written == n);
    CHECK(memcmp(&buf[0], "PK\x03\x04", 4) == 0);
    api->m_freeDoc(doc);
}

TEST_CASE("MX12 Word 导出的代码块按语法着色：着色的 run 带 w:color，制表符仍为 <w:tab/>；不写语言名时没有颜色", "[mddll]")
{
    std::vector<std::wstring> links;
    std::vector<unsigned> starts;
    const std::string doc = BuildMdDocxDocumentXml(ModelOf(L"```cpp\nreturn 42;\t// done\n```"), links, starts);
    CHECK(Has(doc, "<w:r><w:rPr><w:color w:val=\"0000FF\"/></w:rPr><w:t xml:space=\"preserve\">return</w:t></w:r>"));
    CHECK(Has(doc, "<w:r><w:rPr><w:color w:val=\"098658\"/></w:rPr><w:t xml:space=\"preserve\">42</w:t></w:r>"));
    CHECK(Has(doc, "<w:r><w:rPr><w:color w:val=\"008000\"/></w:rPr><w:t xml:space=\"preserve\">// done</w:t></w:r>"));
    CHECK(Has(doc, "<w:r><w:tab/></w:r>"));
    const std::vector<std::string> lines = CodeParagraphTexts(doc);
    REQUIRE(lines.size() == 1);
    CHECK(lines[0] == "return 42;\t// done");
    std::string root;
    CHECK(WellFormed(doc, root));

    //不写语言名：不着色，文字照旧
    const std::string plain = BuildMdDocxDocumentXml(ModelOf(L"```\nreturn 42;\t// done\n```"), links, starts);
    CHECK_FALSE(Has(plain, "<w:color"));
    const std::vector<std::string> plainLines = CodeParagraphTexts(plain);
    REQUIRE(plainLines.size() == 1);
    CHECK(plainLines[0] == "return 42;\t// done");
}

TEST_CASE("MX17 下划线的导出：HTML 写 <u>，Word 写 <w:u w:val=\"single\"/>；渲染后文字与卡片摘要里没有标签", "[mddll]")
{
    const MdModel model = ModelOf(L"# <u>标题</u>\n\n正文 <u>重点</u> 结束");

    //HTML：下划线包进 <u>
    const std::string html = BuildMdHtml(model, std::wstring());
    CHECK(Has(html, U8x(L"<h1><u>标题</u></h1>")));
    CHECK(Has(html, U8x(L"<p>正文 <u>重点</u> 结束</p>")));

    //Word：下划线的 run 带 w:u，且排在 w:rPr 里的规定位置；XML 合法
    std::vector<std::wstring> links;
    std::vector<unsigned> starts;
    const std::string doc = BuildMdDocxDocumentXml(model, links, starts);
    CHECK(Has(doc, "<w:rPr><w:u w:val=\"single\"/></w:rPr><w:t xml:space=\"preserve\">" + U8x(L"重点") + "</w:t>"));
    std::string root;
    CHECK(WellFormed(doc, root));

    //渲染后文字、卡片标题与摘要里都没有标签
    CHECK(model.m_flatText.find(L"<u>") == std::wstring::npos);
    std::wstring title;
    std::wstring summary;
    BuildMdCardText(model, title, summary);
    CHECK(title == L"标题");
    CHECK(summary == L"正文 重点 结束");
}

namespace {

//MX18：piece 在 text 里出现的次数
size_t CountOf(const std::string& text, const std::string& piece)
{
    size_t n = 0;
    for (size_t at = text.find(piece); at != std::string::npos; at = text.find(piece, at + piece.size()))
        ++n;
    return n;
}

} // namespace

TEST_CASE("MX18 警告框的导出：HTML 写带类型 class 的 <div> 与标题段、颜色写在 style 上，按选区导出也带标题；"
          "纯文字与卡片摘要里没有类型行；Word 写彩色标题段，框内段落带彩色左边框与底色、不用引用样式，代码段落只加边框；"
          "宿主设置的标题生效", "[mddll]")
{
    SetMdAlertTitles(NULL, 0);
    const MdModel model = ModelOf(L"> [!TIP]\n> 正文 **加粗**\n>\n> ```\n> code\n> ```\n\n> 普通引用");

    //HTML：结构与 GitHub 一致，颜色直接写在元素上；普通引用仍是 <blockquote>；类型行不出现
    const std::string html = BuildMdHtml(model, std::wstring());
    CHECK(Has(html, "<div class=\"markdown-alert markdown-alert-tip\" style=\"margin:0.6em 0;padding:6px 12px;"
                    "border-left:4px solid #1A7F37;background:#EEF7F0;\">\n"
                    "<p class=\"markdown-alert-title\" style=\"margin:0.2em 0;color:#1A7F37;font-weight:bold;\">TIP</p>\n"
                    + U8x(L"<p>正文 <strong>加粗</strong></p>")));
    CHECK(Has(html, "<blockquote>\n" + U8x(L"<p>普通引用</p>")));
    CHECK_FALSE(Has(html, "[!TIP]"));

    //纯文字（m_toPlainText 取的就是渲染后文字）与卡片摘要里没有类型行，也没有标题
    CHECK(model.m_flatText == L"正文 加粗\ncode\n普通引用");
    std::wstring cardTitle;
    std::wstring cardSummary;
    BuildMdCardText(model, cardTitle, cardSummary);
    CHECK(cardTitle.find(L"[!") == std::wstring::npos);
    CHECK(cardSummary.find(L"[!") == std::wstring::npos);
    CHECK(cardSummary.find(L"TIP") == std::wstring::npos);

    //按选区导出：只选中正文的前两个字，也带警告框的外层与标题
    const std::string range = BuildMdHtmlRange(model, std::wstring(), 0, 2);
    CHECK(Has(range, "markdown-alert-tip"));
    CHECK(Has(range, ">TIP</p>"));
    CHECK(Has(range, U8x(L"<p>正文</p>")));

    //Word：标题段与正文段都带彩色左边框与底色，正文段不用引用样式；代码段落保留自己的底色，只加边框
    std::vector<std::wstring> links;
    std::vector<unsigned> starts;
    const std::string doc = BuildMdDocxDocumentXml(model, links, starts);
    const std::string border = "<w:pBdr><w:left w:val=\"single\" w:sz=\"24\" w:space=\"8\" w:color=\"1A7F37\"/></w:pBdr>";
    const std::string shade = "<w:shd w:val=\"clear\" w:color=\"auto\" w:fill=\"EEF7F0\"/>";
    const std::string alertPara = "<w:p><w:pPr>" + border + shade + "<w:ind w:left=\"420\"/></w:pPr>";
    CHECK(Has(doc, alertPara + "<w:r><w:rPr><w:b/><w:color w:val=\"1A7F37\"/></w:rPr>"
                   "<w:t xml:space=\"preserve\">TIP</w:t></w:r></w:p>"));
    CHECK(CountOf(doc, alertPara) == 2);
    CHECK(Has(doc, "<w:pStyle w:val=\"CodeBlock\"/>" + border + "<w:ind w:left=\"420\"/></w:pPr>"));
    CHECK(CountOf(doc, "<w:pStyle w:val=\"Quote\"/>") == 1);
    std::string root;
    CHECK(WellFormed(doc, root));

    //宿主设置译文后，HTML 与 Word 的标题都用译文
    const wchar_t* const titles[] = { L"提示", L"技巧" };
    SetMdAlertTitles(titles, sizeof(titles) / sizeof(titles[0]));
    const std::string translatedHtml = BuildMdHtml(model, std::wstring());
    const std::string translatedDoc = BuildMdDocxDocumentXml(model, links, starts);
    SetMdAlertTitles(NULL, 0);
    CHECK(Has(translatedHtml, U8x(L">技巧</p>")));
    CHECK(Has(translatedDoc, U8x(L"<w:t xml:space=\"preserve\">技巧</w:t>")));
}

TEST_CASE("MXS 生成覆盖全部语法的样例 .docx 供 Word / WPS 打开核对（默认不运行，输出到 MD_SHOT_DIR）", "[.mddocx]")
{
    wchar_t dir[MAX_PATH] = { 0 };
    if (::GetEnvironmentVariableW(L"MD_SHOT_DIR", dir, MAX_PATH) == 0)
    {
        WARN("MD_SHOT_DIR is not set, skipped.");
        return;
    }
    std::wstring source = kDocxSource;
    source += L"\n## 第二节\n\n段落里有强制换行：第一行  \n第二行。\n\n"
              L"1. 外层\n   1. 内层甲\n   2. 内层乙\n2. 外层二\n\n"
              L"![图片说明](https://example.com/a.png) 与 www.example.com\n\n"
              L"| 名称 | 数量 | 说明 |\n| --- | ---: | :---: |\n| 苹果 | 3 | 红 |\n| 香蕉 | 12 | 黄 |\n\n"
              L"> [!WARNING]\n> 警告框正文，**加粗** 与 <u>下划线</u>。\n>\n> ```cpp\n> int x = 0;\n> ```\n";
    const std::string docx = BuildMdDocx(ModelOf(source));
    const std::wstring path = std::wstring(dir) + L"\\md_sample.docx";
    HANDLE file = ::CreateFileW(path.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    REQUIRE(file != INVALID_HANDLE_VALUE);
    DWORD written = 0;
    ::WriteFile(file, docx.data(), static_cast<DWORD>(docx.size()), &written, NULL);
    ::CloseHandle(file);
    CHECK(written == docx.size());
}
