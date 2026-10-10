/**
 *  FlamingoMarkdown.dll 排版、命中测试与绘制的单元测试，用例 ML1 ~ ML17（docs/Markdown消息方案.md 第 8 节），
 *  代码块语法着色 ML18 ~ ML22（第 12 节），下划线 ML23、警告框 ML24（第 13 节）。
 *
 *  用内存设备上下文测量文字（字体为微软雅黑与 Consolas，Windows 自带），直接调用 Core/ 下的排版、命中与绘制函数。
 *  框架 Catch2 v3，标签 [mddll]。
 *
 *  balloonwj@qq.com   2026-10-06
 */
#include "catch_amalgamated.hpp"

#include <windows.h>
#include <algorithm>
#include <string>
#include <vector>

//gdiplus.h 依赖 min / max 宏，而工程定义了 NOMINMAX，这里借用 std 版本（示例图用例要用 GDI+ 存 PNG）
namespace Gdiplus
{
    using std::min;
    using std::max;
}
#include <objidl.h>
#include <gdiplus.h>

#include "../FlamingoMarkdownApi.h"
#include "../Core/MdAlert.h"
#include "../Core/MdBuilder.h"
#include "../Core/MdCodeColor.h"
#include "../Core/MdFontCache.h"
#include "../Core/MdLayout.h"
#include "../Core/MdModel.h"
#include "../Core/MdPaint.h"
#include "../Core/MdSelection.h"
#include "../Core/MdText.h"
#include "../../Source/Common/Code/CodeLexer.h"            // ML18 ~ ML20：着色种类的取值
#include "../../Source/UI/BalloonChatWnd/ChatCodeBlock.h"  // ML19：与聊天窗代码片段的颜色表逐项比对

using namespace flamingomd;

namespace {

//测试用的正文字体与字号（与聊天气泡默认值一致）
const wchar_t* const kTestFont = L"Microsoft YaHei";
const int kTestFontPx = 14;

//常用的最大宽度（像素）
const int kWideWidth = 600;
const int kNarrowWidth = 160;

//绘制测试的位图尺寸与背景色
const int kCanvasW = 640;
const int kCanvasH = 480;
const COLORREF kCanvasBack = RGB(0xFF, 0xFF, 0xFF);

//长文档排版测试允许的最长时间（毫秒），Debug 版留足余量
const DWORD kLargeLayoutLimitMs = 4000;

//长文档测试的段落数
const int kLargeParagraphs = 400;

/**
 *  测试用的内存设备上下文，析构时释放。
 */
class MemoryDc
{
public:
    MemoryDc()
        : m_hdc(::CreateCompatibleDC(NULL))
    {
    }

    ~MemoryDc()
    {
        if (m_hdc != NULL)
            ::DeleteDC(m_hdc);
    }

    HDC Get() const
    {
        return m_hdc;
    }

private:
    HDC m_hdc;   //设备上下文
};

//测试用的样式
MdResolvedStyle TestStyle()
{
    MdStyle s;
    memset(&s, 0, sizeof(s));
    s.m_cbSize = sizeof(s);
    s.m_fontName = kTestFont;
    s.m_fontPx = kTestFontPx;
    s.m_scalePercent = 100;
    s.m_textColor = RGB(0x2A, 0x2A, 0x2A);
    s.m_mutedColor = RGB(0x8A, 0x8A, 0x8A);
    s.m_linkColor = RGB(0x2F, 0x6F, 0xD0);
    s.m_codeBackColor = RGB(0xF2, 0xF2, 0xF2);
    s.m_ruleColor = RGB(0xDD, 0xDD, 0xDD);
    s.m_tableHeaderBackColor = RGB(0xF6, 0xF6, 0xF6);
    s.m_selectionBackColor = RGB(0xCC, 0xE0, 0xF8);
    s.m_accentColor = RGB(0xFF, 0x7A, 0x45);
    MdResolvedStyle out;
    ResolveMdStyle(s, out);
    return out;
}

//解析
MdModel Parse(const std::string& source)
{
    MdModel model;
    MdBuildOptions options;
    REQUIRE(BuildMdModel(source.data(), source.size(), options, model) == md_ok);
    return model;
}

//解析并排版
void ParseAndLayout(const std::string& source, int maxWidth, MdModel& model, MdLayoutResult& layout)
{
    model = Parse(source);
    MemoryDc dc;
    LayoutMdModel(model, dc.Get(), TestStyle(), maxWidth, layout);
}

//所有可选中片段都在 [0, width] 之内
void CheckFragmentsWithin(const MdLayoutResult& layout, int width)
{
    for (size_t i = 0; i < layout.m_fragments.size(); ++i)
    {
        const MdFragment& f = layout.m_fragments[i];
        INFO("fragment " << i << " x=" << f.m_x << " w=" << f.m_width);
        CHECK(f.m_x >= 0);
        CHECK(f.m_x + f.m_width <= width);
    }
}

//统计某种装饰的个数
size_t CountDecorations(const MdLayoutResult& layout, int kind)
{
    size_t n = 0;
    for (size_t i = 0; i < layout.m_decorations.size(); ++i)
    {
        if (layout.m_decorations[i].m_kind == kind)
            ++n;
    }
    return n;
}

} // namespace

TEST_CASE("ML1 短段落收缩到内容宽度，只有一行", "[mddll]")
{
    MdModel model;
    MdLayoutResult layout;
    ParseAndLayout("hello", kWideWidth, model, layout);
    CHECK(layout.m_width < kWideWidth);
    CHECK(layout.m_width > 0);
    REQUIRE(layout.m_lines.size() == 1);
    CHECK(layout.m_height == layout.m_lines[0].m_bottom - layout.m_lines[0].m_top);
}

TEST_CASE("ML2 长段落按最大宽度折行，所有片段都不越界", "[mddll]")
{
    std::string text;
    for (int i = 0; i < 40; ++i)
        text += "word \xE4\xB8\xAD\xE6\x96\x87 ";   //word 中文
    MdModel model;
    MdLayoutResult layout;
    ParseAndLayout(text, kNarrowWidth, model, layout);
    CHECK(layout.m_width <= kNarrowWidth);
    CHECK(layout.m_lines.size() > 3);
    CheckFragmentsWithin(layout, layout.m_width);

    //行按自上而下排列，互不重叠
    for (size_t i = 1; i < layout.m_lines.size(); ++i)
        CHECK(layout.m_lines[i].m_top >= layout.m_lines[i - 1].m_bottom);
}

TEST_CASE("ML3 比整行还宽的单词在字符之间断开", "[mddll]")
{
    std::string url = "https://example.com/";
    for (int i = 0; i < 30; ++i)
        url += "abcdefghij";
    MdModel model;
    MdLayoutResult layout;
    ParseAndLayout(url, kNarrowWidth, model, layout);
    CHECK(layout.m_lines.size() > 2);
    CheckFragmentsWithin(layout, layout.m_width);

    //断开后的各段连起来仍是原文
    std::wstring joined;
    for (size_t i = 0; i < layout.m_fragments.size(); ++i)
        joined += layout.m_fragments[i].m_text;
    CHECK(joined == model.m_flatText);
}

TEST_CASE("ML4 代码块撑满文档宽度，「复制」按钮在右上角，长代码行折行且续行缩进", "[mddll]")
{
    std::string code = "intro text that is long enough to widen the document a little\n\n```\n";
    code += "    callSomething(argumentOne, argumentTwo, argumentThree, argumentFour, argumentFive);\n";
    code += "```";
    MdModel model;
    MdLayoutResult layout;
    ParseAndLayout(code, 300, model, layout);
    REQUIRE(layout.m_codeRects.size() == 1);
    const RECT bg = layout.m_codeRects[0];
    CHECK(bg.left == 0);
    CHECK(bg.right == layout.m_width);
    const RECT btn = layout.m_copyButtons[0];
    CHECK(btn.right <= bg.right);
    CHECK(btn.top >= bg.top);
    CHECK(btn.right - btn.left > 0);
    CHECK(btn.left > (bg.left + bg.right) / 2);

    //代码行折成多行，第二行起比第一行更靠右（续行缩进）
    std::vector<const MdLine*> codeLines;
    for (size_t i = 0; i < layout.m_lines.size(); ++i)
    {
        if (layout.m_lines[i].m_codeIndex == 0)
            codeLines.push_back(&layout.m_lines[i]);
    }
    REQUIRE(codeLines.size() >= 2);
    const MdFragment& firstFrag = layout.m_fragments[codeLines[0]->m_firstFragment];
    const MdFragment& secondFrag = layout.m_fragments[codeLines[1]->m_firstFragment];
    CHECK(secondFrag.m_x > firstFrag.m_x);
    CHECK(firstFrag.m_inCodeBlock);
    CheckFragmentsWithin(layout, layout.m_width);
}

TEST_CASE("ML5 表格在窄宽度下压缩列宽、单元格内折行，表格不越界", "[mddll]")
{
    std::string table = "| name | description |\n|---|---|\n";
    table += "| alpha | a fairly long description that must wrap inside the cell |\n";
    table += "| beta | short |";
    MdModel model;
    MdLayoutResult layout;
    ParseAndLayout(table, 220, model, layout);
    CHECK(layout.m_width <= 220);
    CheckFragmentsWithin(layout, layout.m_width);
    CHECK(CountDecorations(layout, md_deco_table_header) == 1);
    //竖线 3 条（2 列），横线 4 条（上边框加 3 行）
    size_t lines = CountDecorations(layout, md_deco_line);
    CHECK(lines == 7);

    //宽度充足时不撑满：表格只取自然宽度
    MdModel model2;
    MdLayoutResult wide;
    ParseAndLayout("| a | b |\n|---|---|\n| 1 | 2 |", kWideWidth, model2, wide);
    CHECK(wide.m_width < kWideWidth);
}

TEST_CASE("ML6 命中测试：链接、复制按钮、字符位置自左向右递增、文档之外取最近位置", "[mddll]")
{
    MdModel model;
    MdLayoutResult layout;
    ParseAndLayout("see [link](https://a.com) here\n\n```\ncode line\n```", kWideWidth, model, layout);

    //找到链接片段，命中它的中点
    const MdFragment* linkFrag = NULL;
    for (size_t i = 0; i < layout.m_fragments.size(); ++i)
    {
        if (layout.m_fragments[i].m_linkIndex == 0)
            linkFrag = &layout.m_fragments[i];
    }
    REQUIRE(linkFrag != NULL);
    MdHitInfo hit;
    HitTestMdLayout(layout, linkFrag->m_x + linkFrag->m_width / 2, linkFrag->m_top + linkFrag->m_height / 2, hit);
    CHECK(hit.m_kind == md_hit_link);
    CHECK(hit.m_linkIndex == 0);

    //「复制」按钮
    const RECT btn = layout.m_copyButtons[0];
    HitTestMdLayout(layout, (btn.left + btn.right) / 2, (btn.top + btn.bottom) / 2, hit);
    CHECK(hit.m_kind == md_hit_code_copy);
    CHECK(hit.m_codeBlockIndex == 0);

    //第一行从左到右，字符位置不减
    const MdLine& line = layout.m_lines[0];
    const int y = (line.m_top + line.m_bottom) / 2;
    int prev = -1;
    for (int x = 0; x < layout.m_width; x += 3)
    {
        HitTestMdLayout(layout, x, y, hit);
        CHECK(hit.m_charPos >= prev);
        prev = hit.m_charPos;
    }

    //文档下方与上方
    HitTestMdLayout(layout, layout.m_width - 1, layout.m_height + 100, hit);
    CHECK(hit.m_charPos == static_cast<int>(model.m_flatText.size()));
    CHECK(hit.m_kind == md_hit_none);
    HitTestMdLayout(layout, 0, -100, hit);
    CHECK(hit.m_charPos == 0);
}

TEST_CASE("ML7 按范围取文字：整篇等于渲染后文字（换行为 \\r\\n），范围顺序不限、越界截断", "[mddll]")
{
    MdModel model = Parse("# T\n\n- a\n- b\n\n| x | y |\n|---|---|\n| 1 | 2 |");
    const int len = static_cast<int>(model.m_flatText.size());
    CHECK(GetMdTextRange(model, 0, len) == ToCrLf(model.m_flatText));
    CHECK(GetMdTextRange(model, len, 0) == ToCrLf(model.m_flatText));
    CHECK(GetMdTextRange(model, -5, len + 5) == ToCrLf(model.m_flatText));
    CHECK(GetMdTextRange(model, 2, 2).empty());
    CHECK(GetMdTextRange(model, 0, len) == L"T\r\na\r\nb\r\nx\ty\r\n1\t2");
}

TEST_CASE("ML8 有序列表编号不可选中，无序列表画圆点，任务项画任务框", "[mddll]")
{
    MdModel model;
    MdLayoutResult layout;
    ParseAndLayout("1. one\n2. two\n\n- a\n  - b\n    - c\n\n- [x] done\n- [ ] todo", kWideWidth, model, layout);
    int numbers = 0;
    for (size_t i = 0; i < layout.m_fragments.size(); ++i)
    {
        const MdFragment& f = layout.m_fragments[i];
        if (f.m_flatBegin < 0)
        {
            ++numbers;
            CHECK(f.m_colorRole == md_color_muted);
        }
    }
    CHECK(numbers == 2);
    CHECK(CountDecorations(layout, md_deco_bullet_disc) == 1);
    CHECK(CountDecorations(layout, md_deco_bullet_circle) == 1);
    CHECK(CountDecorations(layout, md_deco_bullet_square) == 1);
    CHECK(CountDecorations(layout, md_deco_task_done) == 1);
    CHECK(CountDecorations(layout, md_deco_task_open) == 1);

    //编号片段不出现在任何行的片段范围里
    for (size_t i = 0; i < layout.m_lines.size(); ++i)
    {
        const MdLine& line = layout.m_lines[i];
        for (int f = line.m_firstFragment; f < line.m_firstFragment + line.m_fragmentCount; ++f)
            CHECK(layout.m_fragments[f].m_flatBegin >= 0);
    }
}

TEST_CASE("ML9 一级标题比正文行高大，下方有细线；引用块有竖条，文字用次要颜色", "[mddll]")
{
    MdModel model;
    MdLayoutResult layout;
    ParseAndLayout("# Title\n\nbody\n\n> quoted", kWideWidth, model, layout);
    REQUIRE(layout.m_lines.size() == 3);
    const int headingH = layout.m_lines[0].m_bottom - layout.m_lines[0].m_top;
    const int bodyH = layout.m_lines[1].m_bottom - layout.m_lines[1].m_top;
    CHECK(headingH > bodyH);
    CHECK(CountDecorations(layout, md_deco_line) == 2);   //标题细线与引用竖条

    const MdLine& quoteLine = layout.m_lines[2];
    const MdFragment& qf = layout.m_fragments[quoteLine.m_firstFragment];
    CHECK(qf.m_colorRole == md_color_muted);
    CHECK(qf.m_x > 0);
}

TEST_CASE("ML10 空文档高度为 0、没有行；极窄宽度不会死循环", "[mddll]")
{
    MdModel model;
    MdLayoutResult layout;
    ParseAndLayout("", kWideWidth, model, layout);
    CHECK(layout.m_height == 0);
    CHECK(layout.m_lines.empty());

    MdModel model2;
    MdLayoutResult narrow;
    ParseAndLayout("abc \xE4\xB8\xAD\xE6\x96\x87 | a | b |\n\n```\nxyz\n```\n\n| a | b |\n|---|---|\n| 1 | 2 |", 1, model2, narrow);
    CHECK(narrow.m_width >= 1);
    CHECK(narrow.m_height > 0);
}

TEST_CASE("ML11 逐字断行时不拆开代理对（emoji）", "[mddll]")
{
    std::string text;
    for (int i = 0; i < 30; ++i)
        text += "\xF0\x9F\x98\x80";   //U+1F600
    MdModel model;
    MdLayoutResult layout;
    ParseAndLayout(text, 40, model, layout);
    CHECK(layout.m_lines.size() > 1);
    for (size_t i = 0; i < layout.m_fragments.size(); ++i)
    {
        const std::wstring& t = layout.m_fragments[i].m_text;
        REQUIRE_FALSE(t.empty());
        CHECK_FALSE(IsLowSurrogate(t[0]));
        CHECK_FALSE(IsHighSurrogate(t[t.size() - 1]));
    }
}

TEST_CASE("ML12 行内代码的片段带左右内边距，正文片段没有", "[mddll]")
{
    MdModel model;
    MdLayoutResult layout;
    ParseAndLayout("run `make all` now", kWideWidth, model, layout);
    bool sawCode = false;
    for (size_t i = 0; i < layout.m_fragments.size(); ++i)
    {
        const MdFragment& f = layout.m_fragments[i];
        if ((f.m_style & md_style_code) != 0)
        {
            sawCode = true;
            CHECK(f.m_padLeft > 0);
            CHECK(f.m_padRight > 0);
            CHECK_FALSE(f.m_inCodeBlock);
        }
        else
        {
            CHECK(f.m_padLeft == 0);
            CHECK(f.m_padRight == 0);
        }
    }
    CHECK(sawCode);
}

TEST_CASE("ML13 绘制：画出了文字与底色；裁剪矩形在文档之外时什么也不画", "[mddll]")
{
    MdModel model;
    MdLayoutResult layout;
    const std::string source = "# T\n\ntext **bold** [l](https://a.com)\n\n```\ncode\n```\n\n- [x] t\n\n| a | b |\n|---|---|\n| 1 | 2 |";

    BITMAPINFO bmi;
    memset(&bmi, 0, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = kCanvasW;
    bmi.bmiHeader.biHeight = -kCanvasH;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    void* bits = NULL;
    HBITMAP bmp = ::CreateDIBSection(NULL, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
    REQUIRE(bmp != NULL);
    MemoryDc dc;
    HGDIOBJ oldBmp = ::SelectObject(dc.Get(), bmp);
    model = Parse(source);
    const MdResolvedStyle style = TestStyle();
    LayoutMdModel(model, dc.Get(), style, kWideWidth, layout);
    REQUIRE(layout.m_height < kCanvasH);

    RECT all = { 0, 0, kCanvasW, kCanvasH };
    HBRUSH back = ::CreateSolidBrush(kCanvasBack);
    ::FillRect(dc.Get(), &all, back);

    //裁剪在文档下方：一个像素都不该变
    MdPaintParams outside;
    outside.m_hasClip = true;
    RECT clip = { 0, kCanvasH - 4, kCanvasW, kCanvasH };
    outside.m_clip = clip;
    PaintMdLayout(layout, style, dc.Get(), 0, 0, outside);
    ::GdiFlush();
    const DWORD* px = static_cast<const DWORD*>(bits);
    size_t changed = 0;
    for (int i = 0; i < kCanvasW * kCanvasH; ++i)
    {
        if ((px[i] & 0x00FFFFFF) != 0x00FFFFFF)
            ++changed;
    }
    CHECK(changed == 0);

    //正常绘制并带选区：有大量像素被画上
    MdPaintParams params;
    params.m_selBegin = 0;
    params.m_selEnd = 3;
    params.m_hoverCodeBlock = 0;
    PaintMdLayout(layout, style, dc.Get(), 0, 0, params);
    ::GdiFlush();
    changed = 0;
    for (int i = 0; i < kCanvasW * kCanvasH; ++i)
    {
        if ((px[i] & 0x00FFFFFF) != 0x00FFFFFF)
            ++changed;
    }
    CHECK(changed > 1000);

    //调用前后设备上下文里的字体不变
    HGDIOBJ fontBefore = ::GetCurrentObject(dc.Get(), OBJ_FONT);
    PaintMdLayout(layout, style, dc.Get(), 0, 0, params);
    CHECK(::GetCurrentObject(dc.Get(), OBJ_FONT) == fontBefore);

    ::DeleteObject(back);
    ::SelectObject(dc.Get(), oldBmp);
    ::DeleteObject(bmp);
}

TEST_CASE("ML14 长文档在限定时间内排完", "[mddll]")
{
    std::string source;
    for (int i = 0; i < kLargeParagraphs; ++i)
        source += "Paragraph with **bold**, *italic*, `code` and \xE4\xB8\xAD\xE6\x96\x87 text that wraps a few times.\n\n";
    MdModel model = Parse(source);
    MemoryDc dc;
    MdLayoutResult layout;
    const DWORD start = ::GetTickCount();
    LayoutMdModel(model, dc.Get(), TestStyle(), 360, layout);
    CHECK(::GetTickCount() - start < kLargeLayoutLimitMs);
    CHECK(layout.m_lines.size() >= static_cast<size_t>(kLargeParagraphs));
}

namespace {

//示例图的宽度（像素）与左右、上下留白；高度按排版结果
const int kShotWidth = 460;
const int kShotPad = 12;

//取 GDI+ 的 PNG 编码器
bool FindPngEncoder(CLSID& clsid)
{
    UINT count = 0;
    UINT bytes = 0;
    Gdiplus::GetImageEncodersSize(&count, &bytes);
    if (bytes == 0)
        return false;
    std::vector<BYTE> buf(bytes);
    Gdiplus::ImageCodecInfo* codecs = reinterpret_cast<Gdiplus::ImageCodecInfo*>(&buf[0]);
    Gdiplus::GetImageEncoders(count, bytes, codecs);
    for (UINT i = 0; i < count; ++i)
    {
        if (wcscmp(codecs[i].MimeType, L"image/png") == 0)
        {
            clsid = codecs[i].Clsid;
            return true;
        }
    }
    return false;
}

//把一份文档按给定底色画成 PNG；代码底色、线条色、表头底色按气泡的配色（与 ChatMarkdown.cpp 一致）
void RenderShot(const std::string& source, COLORREF back, COLORREF codeBack, COLORREF rule, COLORREF header,
                const std::wstring& path)
{
    MdModel model = Parse(source);
    MemoryDc measure;
    MdLayoutResult layout;
    MdResolvedStyle style = TestStyle();
    style.m_codeBackColor = codeBack;
    style.m_ruleColor = rule;
    style.m_tableHeaderBackColor = header;
    LayoutMdModel(model, measure.Get(), style, kShotWidth - kShotPad * 2, layout);
    const int h = layout.m_height + kShotPad * 2;

    BITMAPINFO bmi;
    memset(&bmi, 0, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = kShotWidth;
    bmi.bmiHeader.biHeight = -h;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    void* bits = NULL;
    HBITMAP bmp = ::CreateDIBSection(NULL, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
    REQUIRE(bmp != NULL);
    MemoryDc dc;
    HGDIOBJ old = ::SelectObject(dc.Get(), bmp);
    RECT all = { 0, 0, kShotWidth, h };
    HBRUSH brush = ::CreateSolidBrush(back);
    ::FillRect(dc.Get(), &all, brush);
    ::DeleteObject(brush);
    MdPaintParams params;
    params.m_hoverCodeBlock = 0;
    params.m_selBegin = 2;
    params.m_selEnd = 6;
    PaintMdLayout(layout, style, dc.Get(), kShotPad, kShotPad, params);
    ::GdiFlush();
    ::SelectObject(dc.Get(), old);

    CLSID png;
    REQUIRE(FindPngEncoder(png));
    Gdiplus::Bitmap image(bmp, NULL);
    CHECK(image.Save(path.c_str(), &png, NULL) == Gdiplus::Ok);
    ::DeleteObject(bmp);
}

} // namespace

TEST_CASE("MLX 把示例文档画成 PNG 供人工核对（默认不运行，输出到环境变量 MD_SHOT_DIR 所指目录）", "[.mdshot]")
{
    REQUIRE(EnsureMdGdiplus());
    wchar_t dir[MAX_PATH] = { 0 };
    if (::GetEnvironmentVariableW(L"MD_SHOT_DIR", dir, MAX_PATH) == 0)
        ::GetTempPathW(MAX_PATH, dir);
    std::wstring base(dir);
    if (!base.empty() && base[base.size() - 1] != L'\\')
        base += L'\\';

    //「## 周报」「- **完成** 登录优化」等示例（UTF-8 字节写出，不依赖源文件编码）
    const std::string sample =
        "## \xE5\x91\xA8\xE6\x8A\xA5 Weekly\n\n"
        "\xE6\x9C\xAC\xE5\x91\xA8\xE5\xAE\x8C\xE6\x88\x90 **\xE7\x99\xBB\xE5\xBD\x95\xE4\xBC\x98\xE5\x8C\x96**\xEF\xBC\x8C"
        "*\xE6\x96\x9C\xE4\xBD\x93*\xE3\x80\x81~~\xE5\x88\xA0\xE9\x99\xA4~~\xE3\x80\x81`inline code` \xE5\x92\x8C "
        "[\xE9\x93\xBE\xE6\x8E\xA5](https://example.com)\xE3\x80\x82\n"
        "\xE7\xAC\xAC\xE4\xBA\x8C\xE8\xA1\x8C\xEF\xBC\x88\xE5\x8D\x95\xE4\xB8\xAA\xE6\x8D\xA2\xE8\xA1\x8C\xEF\xBC\x89\n\n"
        "1. \xE7\xAC\xAC\xE4\xB8\x80\xE9\xA1\xB9\n2. \xE7\xAC\xAC\xE4\xBA\x8C\xE9\xA1\xB9\n   - \xE5\xB5\x8C\xE5\xA5\x97\n\n"
        "- [x] \xE5\xB7\xB2\xE5\xAE\x8C\xE6\x88\x90\n- [ ] \xE5\xBE\x85\xE5\x8A\x9E\n\n"
        "> \xE5\xBC\x95\xE7\x94\xA8\xE4\xB8\x80\xE6\xAE\xB5\xE8\xAF\x9D\n\n"
        "```cpp\nint main()\n{\n    return 0; // \xE6\xB3\xA8\xE9\x87\x8A\n}\n```\n\n"
        "| \xE5\x90\x8D\xE7\xA7\xB0 | \xE6\x95\xB0\xE9\x87\x8F | \xE5\xA4\x87\xE6\xB3\xA8 |\n|:--|--:|:-:|\n"
        "| \xE8\x8B\xB9\xE6\x9E\x9C | 3 | ok |\n| \xE9\xA6\x99\xE8\x95\x89 | 12 | \xE5\xBE\x88\xE9\x95\xBF\xE7\x9A\x84\xE4\xB8\x80\xE6\xAE\xB5\xE5\xA4\x87\xE6\xB3\xA8\xE6\x96\x87\xE5\xAD\x97 |\n\n"
        "---\n\n![logo](https://example.com/a.png)";
    RenderShot(sample, RGB(0xF3, 0xF2, 0xEF), RGB(0xE6, 0xE4, 0xDF), RGB(0xD3, 0xD0, 0xC9), RGB(0xEB, 0xE9, 0xE4),
               base + L"md_shot_other.png");
    RenderShot(sample, RGB(0xFF, 0xE6, 0xD8), RGB(0xF9, 0xD5, 0xC2), RGB(0xF0, 0xBE, 0xA4), RGB(0xFC, 0xDC, 0xCB),
               base + L"md_shot_self.png");
}

TEST_CASE("ML16 代码行的行首缩进保留，折行后的续行不带行首空格", "[mddll]")
{
    MdModel model;
    MdLayoutResult layout;
    ParseAndLayout("```\n    indented();\nflat();\n```", kWideWidth, model, layout);
    std::vector<std::wstring> lineTexts;
    for (size_t i = 0; i < layout.m_lines.size(); ++i)
    {
        const MdLine& line = layout.m_lines[i];
        std::wstring text;
        for (int f = line.m_firstFragment; f < line.m_firstFragment + line.m_fragmentCount; ++f)
            text += layout.m_fragments[f].m_text;
        lineTexts.push_back(text);
    }
    REQUIRE(lineTexts.size() == 2);
    CHECK(lineTexts[0] == L"    indented();");
    CHECK(lineTexts[1] == L"flat();");

    //普通段落行首的空格照旧不显示（md4c 已去掉段落开头的空格，这里核对折行续行）
    MdModel model2;
    MdLayoutResult narrow;
    ParseAndLayout("aaaa bbbb cccc dddd eeee ffff gggg", 60, model2, narrow);
    for (size_t i = 0; i < narrow.m_lines.size(); ++i)
    {
        const MdLine& line = narrow.m_lines[i];
        if (line.m_fragmentCount > 0)
            CHECK(narrow.m_fragments[line.m_firstFragment].m_text[0] != L' ');
    }
}

TEST_CASE("ML15 字体缓存按描述去重，反复排版不增加字体个数", "[mddll]")
{
    MdModel model;
    MdLayoutResult layout;
    ParseAndLayout("**a** *b* `c` # d", kWideWidth, model, layout);
    const size_t before = MdFontCache::Instance().Count();
    for (int i = 0; i < 20; ++i)
        ParseAndLayout("**a** *b* `c` # d", kWideWidth, model, layout);
    CHECK(MdFontCache::Instance().Count() == before);
}

namespace {

//按给定的结构大小与排版选项生成测试样式；颜色与宽度无关，不填
MdResolvedStyle TestStyleWithFlags(uint32_t cbSize, uint32_t layoutFlags)
{
    MdStyle s;
    memset(&s, 0, sizeof(s));
    s.m_cbSize = cbSize;
    s.m_fontName = kTestFont;
    s.m_fontPx = kTestFontPx;
    s.m_layoutFlags = layoutFlags;
    MdResolvedStyle out;
    ResolveMdStyle(s, out);
    return out;
}

} // namespace

TEST_CASE("ML17 撑满宽度：设了 md_layout_fill_width 时文档宽度等于最大宽度，旧结构大小时不读该选项", "[mddll]")
{
    const MdModel model = Parse("hello\n\n```\nx\n```");
    MemoryDc dc;

    //默认收缩到内容宽度（聊天气泡的行为，ML1）
    MdLayoutResult shrink;
    LayoutMdModel(model, dc.Get(), TestStyleWithFlags(sizeof(MdStyle), 0), kWideWidth, shrink);
    CHECK(shrink.m_width < kWideWidth);

    //撑满：文档宽度等于最大宽度，代码块底色随之铺满
    MdLayoutResult fill;
    LayoutMdModel(model, dc.Get(), TestStyleWithFlags(sizeof(MdStyle), md_layout_fill_width), kWideWidth, fill);
    CHECK(fill.m_width == kWideWidth);
    REQUIRE(fill.m_codeRects.size() == 1);
    REQUIRE(shrink.m_codeRects.size() == 1);
    CHECK((fill.m_codeRects[0].right - fill.m_codeRects[0].left)
          > (shrink.m_codeRects[0].right - shrink.m_codeRects[0].left));

    //旧宿主编译时的结构里没有 m_layoutFlags（m_cbSize 为第一版大小）：该位置即使有值也不读，仍按内容收缩
    MdLayoutResult old;
    LayoutMdModel(model, dc.Get(), TestStyleWithFlags(kMdStyleMinSize, md_layout_fill_width), kWideWidth, old);
    CHECK(old.m_width == shrink.m_width);

    //两种样式不相等，ApiLayout 不会把一种的排版缓存当成另一种的结果
    CHECK_FALSE(TestStyleWithFlags(sizeof(MdStyle), 0) == TestStyleWithFlags(sizeof(MdStyle), md_layout_fill_width));
    CHECK(kMdStyleV2Size > kMdStyleMinSize);
}

namespace {

//ML18 ~ ML22 的示例代码：关键字、数字、注释、字符串俱全
const char* const kCppSample = "```cpp\nint main() { return 42; } // done\nconst char* s = \"hi\";\n```";

//同一段代码写成不着色的代码块（语言名 text）
const char* const kTextSample = "```text\nint main() { return 42; } // done\nconst char* s = \"hi\";\n```";

//ML22 判断像素颜色的阈值：测试样式的正文为 0x2A2A2A，代码底色与画布都是浅色，不着色时每个像素的每个通道都
//不低于 0x2A；「两个通道都低于 kInkDarkChannel」只可能来自着色的文字（关键字蓝 0000FF、注释绿 008000）
const DWORD kInkDarkChannel = 0x20;
const DWORD kInkBrightChannel = 0x60;

//命中测试扫描的步长与左右多扫的范围（像素）
const int kHitSweepStep = 2;
const int kHitSweepMargin = 4;

//第一个代码块
const MdBlock& FirstCodeBlock(const MdModel& model)
{
    REQUIRE(!model.m_codeBlocks.empty());
    return model.m_blocks[static_cast<size_t>(model.m_codeBlocks[0])];
}

//文字恰为 text 的代码片段；没有时返回 NULL
const MdFragment* FindCodeFragment(const MdLayoutResult& layout, const std::wstring& text)
{
    for (size_t i = 0; i < layout.m_fragments.size(); ++i)
    {
        const MdFragment& f = layout.m_fragments[i];
        if (f.m_inCodeBlock && f.m_text == text)
            return &f;
    }
    return NULL;
}

//第一个代码块各行的文字（每行的片段依次拼接）
std::vector<std::wstring> CodeLineTexts(const MdLayoutResult& layout)
{
    std::vector<std::wstring> texts;
    for (size_t li = 0; li < layout.m_lines.size(); ++li)
    {
        const MdLine& line = layout.m_lines[li];
        if (line.m_codeIndex != 0)
            continue;
        std::wstring joined;
        for (int k = 0; k < line.m_fragmentCount; ++k)
            joined += layout.m_fragments[static_cast<size_t>(line.m_firstFragment + k)].m_text;
        texts.push_back(joined);
    }
    return texts;
}

//第一个代码块的每个片段：着色种类与逐字符算出的 kinds 一致（kinds 为空表示应当全不着色）；同一行的片段首尾相接
void CheckCodeFragments(const MdModel& model, const MdLayoutResult& layout, const std::vector<unsigned char>& kinds)
{
    const MdBlock& block = FirstCodeBlock(model);
    for (size_t li = 0; li < layout.m_lines.size(); ++li)
    {
        const MdLine& line = layout.m_lines[li];
        if (line.m_codeIndex != 0)
            continue;
        for (int k = 0; k < line.m_fragmentCount; ++k)
        {
            const MdFragment& f = layout.m_fragments[static_cast<size_t>(line.m_firstFragment + k)];
            INFO("line " << li << " fragment " << k);
            const int offset = f.m_flatBegin - block.m_codeFlatBegin;
            REQUIRE(offset >= 0);
            for (size_t c = 0; c < f.m_text.size(); ++c)
            {
                const int expected = kinds.empty() ? kMdCodeTokenPlain : kinds[static_cast<size_t>(offset) + c];
                CHECK(f.m_tokenKind == expected);
            }
            if (k > 0)
            {
                const MdFragment& prev = layout.m_fragments[static_cast<size_t>(line.m_firstFragment + k - 1)];
                CHECK(f.m_x == prev.m_x + prev.m_width);
            }
        }
    }
}

//在白底位图上排版并绘制 source（不带选区、鼠标不在代码块上），像素按自上而下存进 pixels（每个为 0x00RRGGBB）
void PaintSourceToPixels(const std::string& source, MdModel& model, MdLayoutResult& layout, std::vector<DWORD>& pixels)
{
    BITMAPINFO bmi;
    memset(&bmi, 0, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = kCanvasW;
    bmi.bmiHeader.biHeight = -kCanvasH;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    void* bits = NULL;
    HBITMAP bmp = ::CreateDIBSection(NULL, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
    REQUIRE(bmp != NULL);
    MemoryDc dc;
    HGDIOBJ oldBmp = ::SelectObject(dc.Get(), bmp);
    model = Parse(source);
    const MdResolvedStyle style = TestStyle();
    LayoutMdModel(model, dc.Get(), style, kWideWidth, layout);

    RECT all = { 0, 0, kCanvasW, kCanvasH };
    HBRUSH back = ::CreateSolidBrush(kCanvasBack);
    ::FillRect(dc.Get(), &all, back);
    ::DeleteObject(back);
    MdPaintParams params;
    PaintMdLayout(layout, style, dc.Get(), 0, 0, params);
    ::GdiFlush();

    const DWORD* px = static_cast<const DWORD*>(bits);
    pixels.assign(px, px + kCanvasW * kCanvasH);
    ::SelectObject(dc.Get(), oldBmp);
    ::DeleteObject(bmp);
}

//像素是不是关键字的蓝色笔画：红、绿两通道都很暗，蓝通道亮
bool IsBlueInk(DWORD px)
{
    const DWORD r = (px >> 16) & 0xFF;
    const DWORD g = (px >> 8) & 0xFF;
    const DWORD b = px & 0xFF;
    return r < kInkDarkChannel && g < kInkDarkChannel && b > kInkBrightChannel;
}

//像素是不是注释的绿色笔画：红、蓝两通道都很暗，绿通道亮
bool IsGreenInk(DWORD px)
{
    const DWORD r = (px >> 16) & 0xFF;
    const DWORD g = (px >> 8) & 0xFF;
    const DWORD b = px & 0xFF;
    return r < kInkDarkChannel && b < kInkDarkChannel && g > kInkBrightChannel;
}

//矩形 [left, right) × [top, bottom) 内满足 test 的像素个数（矩形先截到画布之内）
size_t CountPixels(const std::vector<DWORD>& pixels, int left, int top, int right, int bottom, bool (*test)(DWORD))
{
    left = (std::max)(left, 0);
    top = (std::max)(top, 0);
    right = (std::min)(right, kCanvasW);
    bottom = (std::min)(bottom, kCanvasH);
    size_t n = 0;
    for (int y = top; y < bottom; ++y)
    {
        for (int x = left; x < right; ++x)
        {
            if (test(pixels[static_cast<size_t>(y) * kCanvasW + x]))
                ++n;
        }
    }
    return n;
}

} // namespace

TEST_CASE("ML18 写了语言名的代码块按语法着色：关键字、数字、注释、字符串各成片段，片段首尾相接、拼回原行", "[mddll]")
{
    MdModel model;
    MdLayoutResult layout;
    ParseAndLayout(kCppSample, kWideWidth, model, layout);
    const MdBlock& block = FirstCodeBlock(model);
    std::vector<unsigned char> kinds;
    REQUIRE(ComputeMdCodeTokenKinds(block.m_codeLang, block.m_code, kinds));
    REQUIRE(kinds.size() == block.m_code.size());
    CheckCodeFragments(model, layout, kinds);

    //典型的词各自成为片段，带对应的着色种类
    const MdFragment* keyword = FindCodeFragment(layout, L"return");
    REQUIRE(keyword != NULL);
    CHECK(keyword->m_tokenKind == codesnippet::code_token_keyword);
    const MdFragment* number = FindCodeFragment(layout, L"42");
    REQUIRE(number != NULL);
    CHECK(number->m_tokenKind == codesnippet::code_token_number);
    const MdFragment* comment = FindCodeFragment(layout, L"// done");
    REQUIRE(comment != NULL);
    CHECK(comment->m_tokenKind == codesnippet::code_token_comment);
    const MdFragment* str = FindCodeFragment(layout, L"\"hi\"");
    REQUIRE(str != NULL);
    CHECK(str->m_tokenKind == codesnippet::code_token_string);

    //两行代码各自拼回原文
    const std::vector<std::wstring> lines = CodeLineTexts(layout);
    REQUIRE(lines.size() == 2);
    CHECK(lines[0] == L"int main() { return 42; } // done");
    CHECK(lines[1] == L"const char* s = \"hi\";");
}

TEST_CASE("ML19 没写语言名、语言名认不出或为 text 时不着色；语言名的大小写与别名可识别；颜色表与聊天窗代码片段一致",
          "[mddll]")
{
    const char* const plainSources[] = { "```\nint x = 1;\n```", "```foo\nint x = 1;\n```", "```text\nint x = 1;\n```" };
    for (size_t i = 0; i < sizeof(plainSources) / sizeof(plainSources[0]); ++i)
    {
        INFO("source " << i);
        MdModel model;
        MdLayoutResult layout;
        ParseAndLayout(plainSources[i], kWideWidth, model, layout);
        CheckCodeFragments(model, layout, std::vector<unsigned char>());
        //不着色时一行代码（同一种字体）只有一个片段
        const std::vector<std::wstring> lines = CodeLineTexts(layout);
        REQUIRE(lines.size() == 1);
        CHECK(lines[0] == L"int x = 1;");
        CHECK(FindCodeFragment(layout, L"int x = 1;") != NULL);
    }

    //语言名：大小写不限，别名可识别；没写、认不出、text / plain、代码为空时不着色
    std::vector<unsigned char> kinds;
    const std::wstring code = L"return 1;";
    CHECK(ComputeMdCodeTokenKinds(L"C++", code, kinds));
    CHECK(ComputeMdCodeTokenKinds(L"Cpp", code, kinds));
    CHECK(ComputeMdCodeTokenKinds(L"py", code, kinds));
    CHECK(ComputeMdCodeTokenKinds(L"TS", code, kinds));
    CHECK(kinds.size() == code.size());
    CHECK_FALSE(ComputeMdCodeTokenKinds(L"", code, kinds));
    CHECK(kinds.empty());
    CHECK_FALSE(ComputeMdCodeTokenKinds(L"foo", code, kinds));
    CHECK_FALSE(ComputeMdCodeTokenKinds(L"text", code, kinds));
    CHECK_FALSE(ComputeMdCodeTokenKinds(L"plain", code, kinds));
    CHECK_FALSE(ComputeMdCodeTokenKinds(L"cpp", L"", kinds));

    //颜色表与聊天窗代码片段（ChatCodeBlock.cpp）逐项相同；越界按普通文字
    for (int kind = 0; kind < codesnippet::code_token_kind_count; ++kind)
    {
        INFO("kind " << kind);
        CHECK(MdCodeTokenColor(kind) == static_cast<uint32_t>(balloonchatwnd::CodeTokenColor(kind)));
    }
    CHECK(MdCodeTokenColor(-1) == MdCodeTokenColor(kMdCodeTokenPlain));
    CHECK(MdCodeTokenColor(codesnippet::code_token_kind_count) == MdCodeTokenColor(kMdCodeTokenPlain));
    CHECK(MdCodeTokenColorHex(codesnippet::code_token_keyword) == "0000FF");
    CHECK(MdCodeTokenColorHex(codesnippet::code_token_string) == "A31515");
}

TEST_CASE("ML20 着色不改变折行：窄宽度下行数与每行的位置、起止都与不着色时相同，折到下一行的字符串仍是字符串色",
          "[mddll]")
{
    std::string body = "const char* s = \"";
    for (int i = 0; i < 8; ++i)
        body += "alpha beta gamma ";
    body += "\";";
    MdModel colored;
    MdLayoutResult coloredLayout;
    ParseAndLayout("```cpp\n" + body + "\n```", kNarrowWidth, colored, coloredLayout);
    MdModel plain;
    MdLayoutResult plainLayout;
    ParseAndLayout("```text\n" + body + "\n```", kNarrowWidth, plain, plainLayout);

    REQUIRE(coloredLayout.m_lines.size() > 3);
    REQUIRE(coloredLayout.m_lines.size() == plainLayout.m_lines.size());
    for (size_t i = 0; i < coloredLayout.m_lines.size(); ++i)
    {
        INFO("line " << i);
        const MdLine& a = coloredLayout.m_lines[i];
        const MdLine& b = plainLayout.m_lines[i];
        CHECK(a.m_top == b.m_top);
        CHECK(a.m_bottom == b.m_bottom);
        CHECK(a.m_left == b.m_left);
        CHECK(a.m_right == b.m_right);
        CHECK(a.m_flatBegin == b.m_flatBegin);
        CHECK(a.m_flatEnd == b.m_flatEnd);
    }
    CHECK(coloredLayout.m_width == plainLayout.m_width);
    CHECK(coloredLayout.m_height == plainLayout.m_height);

    const MdBlock& block = FirstCodeBlock(colored);
    std::vector<unsigned char> kinds;
    REQUIRE(ComputeMdCodeTokenKinds(block.m_codeLang, block.m_code, kinds));
    CheckCodeFragments(colored, coloredLayout, kinds);
    CheckCodeFragments(plain, plainLayout, std::vector<unsigned char>());

    //第二行起（字符串折下来的部分）有字符串色的片段
    bool stringOnLaterLine = false;
    for (size_t li = 1; li < coloredLayout.m_lines.size(); ++li)
    {
        const MdLine& line = coloredLayout.m_lines[li];
        for (int k = 0; k < line.m_fragmentCount; ++k)
        {
            if (coloredLayout.m_fragments[static_cast<size_t>(line.m_firstFragment + k)].m_tokenKind
                == codesnippet::code_token_string)
                stringOnLaterLine = true;
        }
    }
    CHECK(stringOnLaterLine);
}

TEST_CASE("ML21 着色后命中测试与按范围取文字的结果与不着色时相同", "[mddll]")
{
    MdModel colored;
    MdLayoutResult coloredLayout;
    ParseAndLayout(kCppSample, kWideWidth, colored, coloredLayout);
    MdModel plain;
    MdLayoutResult plainLayout;
    ParseAndLayout(kTextSample, kWideWidth, plain, plainLayout);
    REQUIRE(colored.m_flatText == plain.m_flatText);
    REQUIRE(coloredLayout.m_lines.size() == plainLayout.m_lines.size());
    CHECK(coloredLayout.m_fragments.size() > plainLayout.m_fragments.size());

    //逐行从左到右扫过：每一点命中的字符位置与种类都相同
    for (size_t li = 0; li < coloredLayout.m_lines.size(); ++li)
    {
        const MdLine& line = coloredLayout.m_lines[li];
        const int y = (line.m_top + line.m_bottom) / 2;
        for (int x = -kHitSweepMargin; x < coloredLayout.m_width + kHitSweepMargin; x += kHitSweepStep)
        {
            INFO("line " << li << " x " << x);
            MdHitInfo a;
            MdHitInfo b;
            HitTestMdLayout(coloredLayout, x, y, a);
            HitTestMdLayout(plainLayout, x, y, b);
            CHECK(a.m_charPos == b.m_charPos);
            CHECK(a.m_kind == b.m_kind);
        }
    }

    //按范围取文字（跨着色片段的一段）
    CHECK(GetMdTextRange(colored, 4, 20) == GetMdTextRange(plain, 4, 20));
    CHECK(GetMdTextRange(colored, 4, 20) == L"main() { return ");
}

TEST_CASE("ML22 绘制：关键字画成蓝色、注释画成绿色；不着色时整张图里没有这两种颜色", "[mddll]")
{
    MdModel colored;
    MdLayoutResult coloredLayout;
    std::vector<DWORD> coloredPixels;
    PaintSourceToPixels(kCppSample, colored, coloredLayout, coloredPixels);
    MdModel plain;
    MdLayoutResult plainLayout;
    std::vector<DWORD> plainPixels;
    PaintSourceToPixels(kTextSample, plain, plainLayout, plainPixels);
    REQUIRE(coloredLayout.m_height < kCanvasH);

    const MdFragment* keyword = FindCodeFragment(coloredLayout, L"return");
    const MdFragment* comment = FindCodeFragment(coloredLayout, L"// done");
    REQUIRE(keyword != NULL);
    REQUIRE(comment != NULL);
    CHECK(CountPixels(coloredPixels, keyword->m_x, keyword->m_top, keyword->m_x + keyword->m_width,
                      keyword->m_top + keyword->m_height, IsBlueInk) > 0);
    CHECK(CountPixels(coloredPixels, comment->m_x, comment->m_top, comment->m_x + comment->m_width,
                      comment->m_top + comment->m_height, IsGreenInk) > 0);

    //对照：不着色时一个这样的像素都没有
    CHECK(CountPixels(plainPixels, 0, 0, kCanvasW, kCanvasH, IsBlueInk) == 0);
    CHECK(CountPixels(plainPixels, 0, 0, kCanvasW, kCanvasH, IsGreenInk) == 0);
}

namespace {

//ML23：深色像素的亮度上限（正文 0x2A2A2A，画布白色），以及一行算作「横线」时深色像素至少占片段宽度的比例（百分数）
const DWORD kDarkLumaLimit = 0x80;
const int kLineCoveragePercent = 80;

//文字恰为 text 的片段（不论是否在代码块里）；没有时返回 NULL
const MdFragment* FindFragment(const MdLayoutResult& layout, const std::wstring& text)
{
    for (size_t i = 0; i < layout.m_fragments.size(); ++i)
    {
        if (layout.m_fragments[i].m_text == text)
            return &layout.m_fragments[i];
    }
    return NULL;
}

//片段基线（含）到片段底边之间，是否有一行深色像素铺满片段宽度的 kLineCoveragePercent 以上（即画出了下划线）
bool HasLineBelowBaseline(const std::vector<DWORD>& pixels, const MdFragment& f)
{
    const int left = f.m_x + f.m_padLeft;
    const int right = f.m_x + f.m_width - f.m_padRight;
    if (right <= left)
        return false;
    for (int y = f.m_baseline; y < f.m_top + f.m_height && y < kCanvasH; ++y)
    {
        int dark = 0;
        for (int x = left; x < right && x < kCanvasW; ++x)
        {
            const DWORD px = pixels[static_cast<size_t>(y) * kCanvasW + x];
            const DWORD luma = (((px >> 16) & 0xFF) + ((px >> 8) & 0xFF) + (px & 0xFF)) / 3;
            if (luma < kDarkLumaLimit)
                ++dark;
        }
        if (dark * 100 >= (right - left) * kLineCoveragePercent)
            return true;
    }
    return false;
}

} // namespace

TEST_CASE("ML23 <u>…</u> 里的文字画出下划线；同一行不带下划线的文字没有", "[mddll]")
{
    MdModel model;
    MdLayoutResult layout;
    std::vector<DWORD> pixels;
    PaintSourceToPixels("<u>under</u> plain", model, layout, pixels);
    const MdFragment* under = FindFragment(layout, L"under");
    const MdFragment* plain = FindFragment(layout, L" plain");
    REQUIRE(under != NULL);
    REQUIRE(plain != NULL);
    CHECK((under->m_style & md_style_underline) != 0);
    CHECK(HasLineBelowBaseline(pixels, *under));
    CHECK_FALSE(HasLineBelowBaseline(pixels, *plain));
}

namespace {

//ML24：DIB 像素（0x00RRGGBB）与 COLORREF（0x00BBGGRR）比较是否同色
bool SameColor(DWORD px, uint32_t colorRef)
{
    const DWORD rgb = (static_cast<DWORD>(GetRValue(colorRef)) << 16) | (static_cast<DWORD>(GetGValue(colorRef)) << 8)
                      | GetBValue(colorRef);
    return (px & 0x00FFFFFFu) == rgb;
}

//ML24：某种装饰第一次出现的下标；没有时返回装饰个数
size_t FirstDecoration(const MdLayoutResult& layout, int kind)
{
    for (size_t i = 0; i < layout.m_decorations.size(); ++i)
    {
        if (layout.m_decorations[i].m_kind == kind)
            return i;
    }
    return layout.m_decorations.size();
}

//ML24：离边缘的距离（像素），取底色与竖条的颜色时避开边界上的抗锯齿
const int kProbeInset = 1;

} // namespace

TEST_CASE("ML24 警告框：底色盖住整块且排在框内装饰之前，竖条与图标各一个；标题片段不可选中、不在任何行里、"
          "取类型的颜色，正文用正文颜色；框里的列表与代码块照常排版；宿主设置的标题生效；画出底色与竖条的颜色", "[mddll]")
{
    SetMdAlertTitles(NULL, 0);
    MdModel model;
    MdLayoutResult layout;
    std::vector<DWORD> pixels;
    PaintSourceToPixels("> [!WARNING]\n> body\n>\n> - item\n>\n> ```\n> code\n> ```", model, layout, pixels);
    REQUIRE(layout.m_height < kCanvasH);
    CHECK(model.m_flatText == L"body\nitem\ncode");

    //装饰：底色排在代码块底色之前（先画），三种警告框装饰各一个，都带类型
    CHECK(CountDecorations(layout, md_deco_alert_back) == 1);
    CHECK(CountDecorations(layout, md_deco_alert_bar) == 1);
    CHECK(CountDecorations(layout, md_deco_alert_icon) == 1);
    const size_t back = FirstDecoration(layout, md_deco_alert_back);
    const size_t code = FirstDecoration(layout, md_deco_code_block);
    REQUIRE(back < layout.m_decorations.size());
    REQUIRE(code < layout.m_decorations.size());
    CHECK(back < code);
    const MdDecoration& b = layout.m_decorations[back];
    CHECK(b.m_alertType == md_alert_warning);
    CHECK(b.m_rect.left == 0);
    CHECK(b.m_rect.top == 0);
    CHECK(b.m_rect.right == layout.m_width);
    CHECK(b.m_rect.bottom == layout.m_height);
    const MdDecoration& icon = layout.m_decorations[FirstDecoration(layout, md_deco_alert_icon)];
    CHECK(icon.m_alertType == md_alert_warning);
    CheckFragmentsWithin(layout, layout.m_width);

    //标题：没有设置译文时为类型名；不可选中，不属于任何行，颜色取类型；图标在它左边
    const MdFragment* title = FindFragment(layout, L"WARNING");
    REQUIRE(title != NULL);
    CHECK(title->m_flatBegin == -1);
    CHECK(title->m_colorRole == md_color_alert_first + md_alert_warning - 1);
    CHECK(icon.m_rect.right <= title->m_x);
    const int titleIndex = static_cast<int>(title - &layout.m_fragments[0]);
    for (size_t i = 0; i < layout.m_lines.size(); ++i)
    {
        const MdLine& line = layout.m_lines[i];
        CHECK((titleIndex < line.m_firstFragment || titleIndex >= line.m_firstFragment + line.m_fragmentCount));
    }

    //正文：在标题行下方，用正文颜色（普通引用块里是次要文字颜色）
    const MdFragment* body = FindFragment(layout, L"body");
    REQUIRE(body != NULL);
    CHECK(body->m_colorRole == md_color_text);
    CHECK(body->m_top >= title->m_top + title->m_height);

    //框里的列表与代码块照常排版：列表有项目符号、列表项文字在符号右边，代码块在框内、四周留出内边距
    CHECK(CountDecorations(layout, md_deco_bullet_disc) == 1);
    const MdFragment* item = FindFragment(layout, L"item");
    REQUIRE(item != NULL);
    const MdDecoration& bullet = layout.m_decorations[FirstDecoration(layout, md_deco_bullet_disc)];
    CHECK(bullet.m_rect.right <= item->m_x);
    CHECK(item->m_top >= body->m_top + body->m_height);
    const MdDecoration& codeBack = layout.m_decorations[code];
    CHECK(codeBack.m_rect.left > b.m_rect.left);
    CHECK(codeBack.m_rect.right < b.m_rect.right);
    CHECK(codeBack.m_rect.top > item->m_top);
    CHECK(codeBack.m_rect.bottom < b.m_rect.bottom);

    //绘制：右下角的内边距里是底色，左侧竖条是类型的主色
    const size_t corner = static_cast<size_t>(layout.m_height - 1 - kProbeInset) * kCanvasW
                          + static_cast<size_t>(layout.m_width - 1 - kProbeInset);
    CHECK(SameColor(pixels[corner], MdAlertBackColor(md_alert_warning)));
    const size_t bar = static_cast<size_t>(layout.m_height / 2) * kCanvasW + kProbeInset;
    CHECK(SameColor(pixels[bar], MdAlertColor(md_alert_warning)));

    //宿主设置译文后标题随之改变，文档宽度放得下标题
    const wchar_t* const titles[] = { L"\x63D0\x793A", L"\x6280\x5DE7", L"\x91CD\x8981", L"\x8B66\x544A",
                                      L"\x6CE8\x610F\xFF1A\x8FD9\x662F\x4E00\x4E2A\x5F88\x957F\x7684\x6807\x9898" };
    SetMdAlertTitles(titles, sizeof(titles) / sizeof(titles[0]));
    MdModel translated;
    MdLayoutResult translatedLayout;
    ParseAndLayout("> [!CAUTION]", kWideWidth, translated, translatedLayout);
    SetMdAlertTitles(NULL, 0);
    const MdFragment* longTitle = FindFragment(translatedLayout, titles[md_alert_caution - 1]);
    REQUIRE(longTitle != NULL);
    CHECK(longTitle->m_colorRole == md_color_alert_first + md_alert_caution - 1);
    CheckFragmentsWithin(translatedLayout, translatedLayout.m_width);
    CHECK(translatedLayout.m_lines.empty());
}
