/**
 *  FlamingoMarkdown.dll 导出函数表的单元测试，用例 MA1 ~ MA8（docs/Markdown消息方案.md 第 8 节），第五版 MA9（第 13 节）。
 *
 *  直接调用 MdApiTable.cpp 里的函数表（与 DLL 导出的是同一张），核对参数检查、缓冲区约定、排版缓存、
 *  纯文本提取与文档校验。框架 Catch2 v3，标签 [mddll]。
 *
 *  balloonwj@qq.com   2026-10-06
 */
#include "catch_amalgamated.hpp"

#include <windows.h>
#include <stddef.h>
#include <string.h>
#include <string>
#include <vector>

#include "../FlamingoMarkdownApi.h"
#include "../MdApiTable.h"

using namespace flamingomd;

namespace {

//测试用的正文字体与字号
const wchar_t* const kTestFont = L"Microsoft YaHei";
const int kTestFontPx = 14;

//常用的最大宽度（像素）
const int kTestWidth = 400;

//测试用的样式
MdStyle TestStyle()
{
    MdStyle s;
    memset(&s, 0, sizeof(s));
    s.m_cbSize = sizeof(s);
    s.m_fontName = kTestFont;
    s.m_fontPx = kTestFontPx;
    s.m_scalePercent = 100;
    s.m_textColor = RGB(0x2A, 0x2A, 0x2A);
    s.m_linkColor = RGB(0x2F, 0x6F, 0xD0);
    return s;
}

//收集日志的回调
std::vector<std::string> g_logs;
void __cdecl CollectLog(int level, const char* text)
{
    (void)level;
    g_logs.push_back(text);
}

//解析，要求成功
MdDocument* ParseOk(const MdApi* api, const std::string& source)
{
    MdDocument* doc = NULL;
    REQUIRE(api->m_parse(source.data(), static_cast<uint32_t>(source.size()), NULL, &doc) == md_ok);
    REQUIRE(doc != NULL);
    return doc;
}

//用「先查长度再取」的方式取 m_getText 的结果
std::wstring GetTextVia(const MdApi* api, const MdDocument* doc, int begin, int end)
{
    uint32_t needed = 0;
    const int rc = api->m_getText(doc, begin, end, NULL, 0, &needed);
    if (rc != md_buffer_too_small)
        return std::wstring();
    std::vector<wchar_t> buf(needed + 1, 0);
    uint32_t written = 0;
    REQUIRE(api->m_getText(doc, begin, end, &buf[0], needed + 1, &written) == md_ok);
    REQUIRE(written == needed);
    return std::wstring(&buf[0], written);
}

} // namespace

TEST_CASE("MA1 函数表大小不小于第一版，所有函数指针都不为空", "[mddll]")
{
    const MdApi* api = GetMdApiTable();
    REQUIRE(api != NULL);
    CHECK(api->m_cbSize >= kMdApiMinSize);
    CHECK(api->m_init != NULL);
    CHECK(api->m_getVersion != NULL);
    CHECK(api->m_parse != NULL);
    CHECK(api->m_freeDoc != NULL);
    CHECK(api->m_getDocInfo != NULL);
    CHECK(api->m_layout != NULL);
    CHECK(api->m_paint != NULL);
    CHECK(api->m_hitTest != NULL);
    CHECK(api->m_getText != NULL);
    CHECK(api->m_getLinkUrl != NULL);
    CHECK(api->m_getCodeBlockText != NULL);
    CHECK(api->m_toPlainText != NULL);
    CHECK(MdApiHasField(api, offsetof(MdApi, m_toPlainText) + sizeof(void*)));
    CHECK(wcsstr(api->m_getVersion(), L"md4c") != NULL);
}

TEST_CASE("MA2 m_init 检查参数，只有第一次调用生效", "[mddll]")
{
    const MdApi* api = GetMdApiTable();
    CHECK(api->m_init(NULL) == md_bad_argument);
    MdHostInfo tooSmall;
    tooSmall.m_cbSize = 1;
    tooSmall.m_log = NULL;
    CHECK(api->m_init(&tooSmall) == md_bad_argument);

    MdHostInfo host;
    host.m_cbSize = sizeof(host);
    host.m_log = &CollectLog;
    CHECK(api->m_init(&host) == md_ok);
    CHECK(api->m_init(&host) == md_ok);
}

TEST_CASE("MA3 m_parse 与 m_getDocInfo：参数检查与文档概况", "[mddll]")
{
    const MdApi* api = GetMdApiTable();
    MdDocument* doc = NULL;
    CHECK(api->m_parse("x", 1, NULL, NULL) == md_bad_argument);
    CHECK(api->m_parse(NULL, 5, NULL, &doc) == md_bad_argument);
    CHECK(doc == NULL);
    CHECK(api->m_parse("x", kMdMaxSourceBytes + 1, NULL, &doc) == md_bad_argument);
    CHECK(doc == NULL);

    //空文本可以解析
    CHECK(api->m_parse(NULL, 0, NULL, &doc) == md_ok);
    REQUIRE(doc != NULL);
    api->m_freeDoc(doc);

    doc = ParseOk(api, "[a](https://a.com) ![b](https://b.com/x.png)\n\n```\nc\n```");
    MdDocInfo info;
    memset(&info, 0, sizeof(info));
    info.m_cbSize = sizeof(info);
    REQUIRE(api->m_getDocInfo(doc, &info) == md_ok);
    CHECK(info.m_linkCount == 2);
    CHECK(info.m_codeBlockCount == 1);
    CHECK(info.m_truncated == 0);
    CHECK(info.m_textLength > 0);

    MdDocInfo tinyInfo;
    tinyInfo.m_cbSize = 1;
    CHECK(api->m_getDocInfo(doc, &tinyInfo) == md_bad_argument);
    api->m_freeDoc(doc);
}

TEST_CASE("MA4 输出字符串的缓冲区约定：先查长度再取，缓冲区不够时报告所需长度", "[mddll]")
{
    const MdApi* api = GetMdApiTable();
    MdDocument* doc = ParseOk(api, "line1\nline2 [x](https://x.com)\n\n```\na\tb\nc\n```");

    CHECK(GetTextVia(api, doc, 0, 11) == L"line1\r\nline2");

    uint32_t needed = 0;
    wchar_t tinyBuf[3] = { 0 };
    CHECK(api->m_getText(doc, 0, 5, tinyBuf, 3, &needed) == md_buffer_too_small);
    CHECK(needed == 5);

    //链接地址
    CHECK(api->m_getLinkUrl(doc, 0, NULL, 0, &needed) == md_buffer_too_small);
    std::vector<wchar_t> url(needed + 1, 0);
    CHECK(api->m_getLinkUrl(doc, 0, &url[0], needed + 1, &needed) == md_ok);
    CHECK(std::wstring(&url[0]) == L"https://x.com");
    CHECK(api->m_getLinkUrl(doc, 1, NULL, 0, &needed) == md_out_of_range);
    CHECK(api->m_getLinkUrl(doc, -1, NULL, 0, &needed) == md_out_of_range);

    //代码块原文：Tab 不展开，换行为 \r\n
    CHECK(api->m_getCodeBlockText(doc, 0, NULL, 0, &needed) == md_buffer_too_small);
    std::vector<wchar_t> code(needed + 1, 0);
    CHECK(api->m_getCodeBlockText(doc, 0, &code[0], needed + 1, &needed) == md_ok);
    CHECK(std::wstring(&code[0]) == L"a\tb\r\nc");
    CHECK(api->m_getCodeBlockText(doc, 1, NULL, 0, &needed) == md_out_of_range);
    api->m_freeDoc(doc);
}

TEST_CASE("MA5 m_layout 参数检查与缓存；未排版时绘制与命中返回 md_not_laid_out", "[mddll]")
{
    const MdApi* api = GetMdApiTable();
    MdDocument* doc = ParseOk(api, "some **text** here");
    HDC hdc = ::CreateCompatibleDC(NULL);
    REQUIRE(hdc != NULL);

    CHECK(api->m_paint(doc, hdc, 0, 0, NULL) == md_not_laid_out);
    MdHitResult hit;
    memset(&hit, 0, sizeof(hit));
    hit.m_cbSize = sizeof(hit);
    CHECK(api->m_hitTest(doc, 0, 0, &hit) == md_not_laid_out);

    MdStyle style = TestStyle();
    int w = 0;
    int h = 0;
    CHECK(api->m_layout(doc, hdc, &style, 0, &w, &h) == md_bad_argument);
    CHECK(api->m_layout(doc, NULL, &style, kTestWidth, &w, &h) == md_bad_argument);
    MdStyle noFont = style;
    noFont.m_fontName = NULL;
    CHECK(api->m_layout(doc, hdc, &noFont, kTestWidth, &w, &h) == md_bad_argument);
    MdStyle oldSize = style;
    oldSize.m_cbSize = 8;
    CHECK(api->m_layout(doc, hdc, &oldSize, kTestWidth, &w, &h) == md_bad_argument);

    REQUIRE(api->m_layout(doc, hdc, &style, kTestWidth, &w, &h) == md_ok);
    CHECK(w > 0);
    CHECK(h > 0);
    int w2 = 0;
    int h2 = 0;
    REQUIRE(api->m_layout(doc, hdc, &style, kTestWidth, &w2, &h2) == md_ok);
    CHECK(w2 == w);
    CHECK(h2 == h);

    //字号变大后重新排版，高度随之变大
    MdStyle bigger = style;
    bigger.m_fontPx = kTestFontPx * 2;
    int w3 = 0;
    int h3 = 0;
    REQUIRE(api->m_layout(doc, hdc, &bigger, kTestWidth, &w3, &h3) == md_ok);
    CHECK(h3 > h);

    CHECK(api->m_paint(doc, hdc, 0, 0, NULL) == md_ok);
    CHECK(api->m_hitTest(doc, 1, 1, &hit) == md_ok);
    MdHitResult tiny;
    tiny.m_cbSize = 4;
    CHECK(api->m_hitTest(doc, 1, 1, &tiny) == md_bad_argument);

    ::DeleteDC(hdc);
    api->m_freeDoc(doc);
}

TEST_CASE("MA6 纯文本提取：去掉标记，块之间换行，单元格之间 Tab", "[mddll]")
{
    const MdApi* api = GetMdApiTable();
    //「# 标题\n\n- **完成** 登录优化\n- [链接](https://a.com)\n\n| a | b |\n|---|---|\n| 1 | 2 |」
    const std::string source = "# \xE6\xA0\x87\xE9\xA2\x98\n\n- **\xE5\xAE\x8C\xE6\x88\x90** \xE7\x99\xBB\xE5\xBD\x95\xE4\xBC\x98\xE5\x8C\x96\n- [\xE9\x93\xBE\xE6\x8E\xA5](https://a.com)\n\n| a | b |\n|---|---|\n| 1 | 2 |";
    uint32_t needed = 0;
    REQUIRE(api->m_toPlainText(source.data(), static_cast<uint32_t>(source.size()), NULL, NULL, 0, &needed)
            == md_buffer_too_small);
    std::vector<char> buf(needed + 1, 0);
    REQUIRE(api->m_toPlainText(source.data(), static_cast<uint32_t>(source.size()), NULL, &buf[0], needed + 1, &needed)
            == md_ok);
    const std::string expected = "\xE6\xA0\x87\xE9\xA2\x98\n\xE5\xAE\x8C\xE6\x88\x90 \xE7\x99\xBB\xE5\xBD\x95\xE4\xBC\x98\xE5\x8C\x96\n\xE9\x93\xBE\xE6\x8E\xA5\na\tb\n1\t2";
    CHECK(std::string(&buf[0]) == expected);

    CHECK(api->m_toPlainText("x", 1, NULL, NULL, 0, NULL) == md_bad_argument);
    CHECK(api->m_toPlainText(NULL, 3, NULL, NULL, 0, &needed) == md_bad_argument);

    //图片前缀经解析选项传入
    MdParseOptions options;
    options.m_cbSize = sizeof(options);
    options.m_imageLinkPrefix = L"[img] ";
    options.m_flags = 0;
    const std::string img = "![a](https://x.com/a.png)";
    REQUIRE(api->m_toPlainText(img.data(), static_cast<uint32_t>(img.size()), &options, NULL, 0, &needed)
            == md_buffer_too_small);
    std::vector<char> imgBuf(needed + 1, 0);
    REQUIRE(api->m_toPlainText(img.data(), static_cast<uint32_t>(img.size()), &options, &imgBuf[0], needed + 1, &needed)
            == md_ok);
    CHECK(std::string(&imgBuf[0]) == "[img] a");
}

TEST_CASE("MA7 m_freeDoc 接受 NULL，拒绝不是本模块创建的指针并写一条警告", "[mddll]")
{
    const MdApi* api = GetMdApiTable();
    api->m_freeDoc(NULL);

    //一块与文档同样大小、校验值不对的内存：不会被当成文档释放
    std::vector<unsigned char> fake(4096, 0);
    const size_t logsBefore = g_logs.size();
    api->m_freeDoc(reinterpret_cast<MdDocument*>(&fake[0]));
    CHECK(fake[0] == 0);
    //m_init 在 MA2 里装了日志回调；单独运行本用例时没有回调，不检查日志条数
    if (logsBefore > 0)
        CHECK(g_logs.size() > logsBefore);

    MdDocInfo info;
    info.m_cbSize = sizeof(info);
    CHECK(api->m_getDocInfo(reinterpret_cast<MdDocument*>(&fake[0]), &info) == md_bad_argument);
}

TEST_CASE("MA8 命中测试与取文字配合：拖选整行后取到的就是这一行", "[mddll]")
{
    const MdApi* api = GetMdApiTable();
    MdDocument* doc = ParseOk(api, "alpha beta\ngamma");
    HDC hdc = ::CreateCompatibleDC(NULL);
    MdStyle style = TestStyle();
    int w = 0;
    int h = 0;
    REQUIRE(api->m_layout(doc, hdc, &style, kTestWidth, &w, &h) == md_ok);

    MdHitResult a;
    memset(&a, 0, sizeof(a));
    a.m_cbSize = sizeof(a);
    MdHitResult b = a;
    REQUIRE(api->m_hitTest(doc, 0, 2, &a) == md_ok);
    REQUIRE(api->m_hitTest(doc, w + 50, 2, &b) == md_ok);
    CHECK(a.m_charPos == 0);
    CHECK(GetTextVia(api, doc, a.m_charPos, b.m_charPos) == L"alpha beta");
    CHECK(a.m_kind == md_hit_text);
    CHECK(b.m_kind == md_hit_none);
    CHECK(a.m_linkIndex == -1);
    CHECK(a.m_codeBlockIndex == -1);

    ::DeleteDC(hdc);
    api->m_freeDoc(doc);
}

namespace {

//MA9：用「先查长度再取」的方式取 m_exportHtml 的结果
std::string ExportHtmlVia(const MdApi* api, const MdDocument* doc)
{
    uint32_t needed = 0;
    if (api->m_exportHtml(doc, NULL, NULL, 0, &needed) != md_buffer_too_small)
        return std::string();
    std::vector<char> buf(needed + 1, 0);
    uint32_t written = 0;
    REQUIRE(api->m_exportHtml(doc, NULL, &buf[0], needed + 1, &written) == md_ok);
    return std::string(&buf[0], written);
}

} // namespace

TEST_CASE("MA9 第五版函数表：m_setAlertTitles 设置的标题用于导出，NULL 视为未设置，多出的元素忽略，"
          "传 NULL 数组恢复为类型名", "[mddll]")
{
    const MdApi* api = GetMdApiTable();
    REQUIRE(api != NULL);
    REQUIRE(api->m_cbSize >= kMdApiV5Size);
    REQUIRE(MdApiHasField(api, offsetof(MdApi, m_setAlertTitles) + sizeof(void*)));
    REQUIRE(api->m_setAlertTitles != NULL);

    MdDocument* doc = ParseOk(api, "> [!NOTE]\n> a\n\n> [!TIP]\n> b\n\n> [!CAUTION]\n> c");

    //第二项为 NULL（技巧保持类型名），第六项超出类型个数被忽略
    const wchar_t* const titles[] = { L"Note-T", NULL, L"Important-T", L"Warning-T", L"Caution-T", L"Extra-T" };
    CHECK(api->m_setAlertTitles(titles, sizeof(titles) / sizeof(titles[0])) == md_ok);
    const std::string translated = ExportHtmlVia(api, doc);
    CHECK(translated.find(">Note-T</p>") != std::string::npos);
    CHECK(translated.find(">TIP</p>") != std::string::npos);
    CHECK(translated.find(">Caution-T</p>") != std::string::npos);
    CHECK(translated.find("Extra-T") == std::string::npos);

    //传 NULL 数组：全部恢复为类型名
    CHECK(api->m_setAlertTitles(NULL, 0) == md_ok);
    const std::string reset = ExportHtmlVia(api, doc);
    CHECK(reset.find(">NOTE</p>") != std::string::npos);
    CHECK(reset.find(">CAUTION</p>") != std::string::npos);
    CHECK(reset.find("Note-T") == std::string::npos);
    api->m_freeDoc(doc);
}
