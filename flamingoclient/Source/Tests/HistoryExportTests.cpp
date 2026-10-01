/**
 *  消息记录导出（网页格式与文件名处理）的单元测试（HEX1～HEX4），HistoryExportTests.cpp
 *
 *  覆盖：特殊字符转义、图片用相对路径引用、本地没有的图片写占位；文件名去掉非法字符、重名自动
 *  编号、按文件头补扩展名；只在有未加载的消息时才提示；网页的整体结构（标题、顺序、发送人与时间）。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-01
 */
#include "catch_amalgamated.hpp"
#include <set>
#include <string>
#include <vector>
#include "../UI/BalloonHistory/HistoryExport.h"

using namespace balloonhistory;

namespace {

HistoryExportRow TextRow(const wchar_t* time, const wchar_t* sender, const wchar_t* text)
{
    HistoryExportRow row;
    row.m_time = time;
    row.m_sender = sender;
    row.m_kind = kHistoryExportText;
    row.m_text = text;
    return row;
}

bool Contains(const std::wstring& haystack, const std::wstring& needle)
{
    return haystack.find(needle) != std::wstring::npos;
}

} // namespace

TEST_CASE("HEX1 网页会转义特殊字符，图片用相对路径引用，本地没有的图片写占位", "[historyexport]")
{
    CHECK(HtmlEscape(L"a<b>&\"c'") == L"a&lt;b&gt;&amp;&quot;c&#39;");
    CHECK(HtmlEscape(L"第一行\n第二行") == L"第一行<br>第二行");

    std::vector<HistoryExportRow> rows;
    rows.push_back(TextRow(L"2026-09-30 20:07", L"张<勇>", L"<script>alert(1)</script>"));

    HistoryExportRow img;
    img.m_time = L"2026-09-30 20:08";
    img.m_sender = L"韩晓燕";
    img.m_kind = kHistoryExportImage;
    img.m_imageRelPath = L"消息记录_files/1.png";
    rows.push_back(img);

    HistoryExportRow missing = img;
    missing.m_imageRelPath.clear();
    rows.push_back(missing);

    const std::wstring html = BuildHistoryHtml(L"消息记录", rows);
    //正文里的标签被转义，不会被浏览器当成脚本执行
    CHECK_FALSE(Contains(html, L"<script>"));
    CHECK(Contains(html, L"&lt;script&gt;alert(1)&lt;/script&gt;"));
    CHECK(Contains(html, L"张&lt;勇&gt;"));
    //图片：本地有的嵌入、没有的写占位
    CHECK(Contains(html, L"<img src=\"消息记录_files/1.png\""));
    CHECK(Contains(html, L"[图片未下载]"));
}

TEST_CASE("HEX2 文件名去掉非法字符、重名自动编号、按文件头补扩展名", "[historyexport]")
{
    CHECK(SanitizeExportFileName(L"a\\b/c:d*e?f\"g<h>i|j.png") == L"a_b_c_d_e_f_g_h_i_j.png");
    CHECK(SanitizeExportFileName(L"  .x.png. ") == L"x.png");
    CHECK(SanitizeExportFileName(L"...") == L"image");

    std::set<std::wstring> used;
    CHECK(UniqueExportFileName(L"a.png", used) == L"a.png");
    CHECK(UniqueExportFileName(L"a.png", used) == L"a (2).png");
    CHECK(UniqueExportFileName(L"A.PNG", used) == L"A (3).PNG");   //不区分大小写
    CHECK(UniqueExportFileName(L"noext", used) == L"noext");
    CHECK(UniqueExportFileName(L"noext", used) == L"noext (2)");

    CHECK(ExportFilesDirName(L"消息记录_苏沐辰.html") == L"消息记录_苏沐辰_files");
    CHECK(ExportFilesDirName(L"noext") == L"noext_files");

    const unsigned char png[] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
    const unsigned char jpg[] = { 0xFF, 0xD8, 0xFF, 0xE0 };
    const unsigned char gif[] = { 'G', 'I', 'F', '8', '9', 'a' };
    const unsigned char bmp[] = { 'B', 'M', 0, 0 };
    const unsigned char webp[] = { 'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'E', 'B', 'P' };
    const unsigned char unknown[] = { 1, 2, 3 };
    CHECK(ImageExtensionFromHeader(png, sizeof(png)) == L".png");
    CHECK(ImageExtensionFromHeader(jpg, sizeof(jpg)) == L".jpg");
    CHECK(ImageExtensionFromHeader(gif, sizeof(gif)) == L".gif");
    CHECK(ImageExtensionFromHeader(bmp, sizeof(bmp)) == L".bmp");
    CHECK(ImageExtensionFromHeader(webp, sizeof(webp)) == L".webp");
    CHECK(ImageExtensionFromHeader(unknown, sizeof(unknown)) == L".img");
    CHECK(ImageExtensionFromHeader(NULL, 0) == L".img");
}

TEST_CASE("HEX3 只在服务端还有未加载的消息时才提示", "[historyexport]")
{
    CHECK(ShouldPromptUnloadedBeforeExport(true));
    CHECK_FALSE(ShouldPromptUnloadedBeforeExport(false));
}

TEST_CASE("HEX4 网页结构：标题、按给定顺序输出、每条带发送人与时间、声明 UTF-8", "[historyexport]")
{
    std::vector<HistoryExportRow> rows;
    rows.push_back(TextRow(L"2026-09-29 08:00", L"甲", L"第一条"));
    rows.push_back(TextRow(L"2026-09-30 09:00", L"乙", L"第二条"));
    HistoryExportRow file;
    file.m_time = L"2026-09-30 10:00";
    file.m_sender = L"丙";
    file.m_kind = kHistoryExportFile;
    file.m_text = L"报告.docx";
    rows.push_back(file);

    const std::wstring html = BuildHistoryHtml(L"消息记录 · 苏沐辰", rows);
    CHECK(Contains(html, L"<meta charset=\"utf-8\">"));
    CHECK(Contains(html, L"<title>消息记录 · 苏沐辰</title>"));
    CHECK(Contains(html, L"<h1>消息记录 · 苏沐辰</h1>"));
    //顺序与给定的一致
    const size_t p1 = html.find(L"第一条");
    const size_t p2 = html.find(L"第二条");
    const size_t p3 = html.find(L"报告.docx");
    REQUIRE(p1 != std::wstring::npos);
    REQUIRE(p2 != std::wstring::npos);
    REQUIRE(p3 != std::wstring::npos);
    CHECK(p1 < p2);
    CHECK(p2 < p3);
    //发送人与时间
    CHECK(Contains(html, L"甲"));
    CHECK(Contains(html, L"2026-09-29 08:00"));
    CHECK(Contains(html, L"[文件] 报告.docx"));
}
