/**
 *  ChatTextSelectionTests.cpp
 *
 *  聊天气泡"选中文字并复制"纯逻辑（BalloonChatWnd/ChatTextSelection.h）的测试，
 *  覆盖与用户确认过的范围：
 *    · 命中测试 —— 坐标到字符下标（字符中线取舍、行首行尾、首行之上末行之下、跨行累计）
 *    · 选区归一化 —— 正向 / 反向拖拽 / 空区间
 *    · 选区矩形 —— 单行 / 跨两行 / 跨三行 / 空区间
 *    · 取文本 —— 纯文字 / 跨表情 / 只覆盖表情 / 含 @ 片段
 *    · 双击选词 —— ASCII 词 / 汉字逐字 / 空白段 / 边界与空文本
 *
 *  用 Catch2 v3（amalgamated）作框架，纯内存断言，不创建窗口 / 不依赖字体度量 ——
 *  排版结果由用例直接构造，故意用整齐的 10 像素等宽字符，便于人读断言。
 *  全局 _Module 由 BalloonChatLayoutTests.cpp 统一定义，本文件只引用。
 *
 *  balloonwj@qq.com   2026-07-31
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <string>
#include <vector>

#include "../UI/BalloonChatWnd/ChatTextSelection.h"

using namespace balloonchatwnd;

namespace {

// 每个字符 10 像素宽、每行 20 像素高 —— 让断言里的坐标一眼能算。
const int kCharW = 10;
const int kLineH = 20;

// 造一段等宽文字：从全文下标 charBegin 起、count 个字符，左边界 left。
SelRun MakeRun(int charBegin, int count, int left)
{
    SelRun run;
    run.m_charBegin = charBegin;
    run.m_charCount = count;
    run.m_left      = left;
    run.m_right     = left + count * kCharW;
    for (int k = 1; k <= count; ++k)
    {
        run.m_charRights.push_back(left + k * kCharW);
    }
    return run;
}

// 造一行（行序号 lineIdx 决定 top/bottom），行内就一段等宽文字。
SelLine MakeLine(int lineIdx, int charBegin, int count, int left = 0)
{
    SelLine line;
    line.m_top    = lineIdx * kLineH;
    line.m_bottom = line.m_top + kLineH;
    line.m_runs.push_back(MakeRun(charBegin, count, left));
    return line;
}

// 造一个三行、每行 5 个字符、共 15 字符的排版结果。
SelLayout MakeThreeLineLayout()
{
    SelLayout layout;
    layout.m_lines.push_back(MakeLine(0, 0, 5));
    layout.m_lines.push_back(MakeLine(1, 5, 5));
    layout.m_lines.push_back(MakeLine(2, 10, 5));
    layout.m_totalChars = 15;
    return layout;
}

} // anonymous namespace

// =====================================================================
// SEL1-7. 命中测试（坐标 → 字符下标）
// =====================================================================

TEST_CASE("SEL1 落在字符左半边取其前、右半边取其后", "[chatsel]")
{
    SelLayout layout;
    layout.m_lines.push_back(MakeLine(0, 0, 3));   // 3 个字符，x: 0-10-20-30
    layout.m_totalChars = 3;

    REQUIRE(CharIndexFromPoint(layout, 1, 10) == 0);    // 第 0 字左半边
    REQUIRE(CharIndexFromPoint(layout, 6, 10) == 1);    // 第 0 字右半边
    REQUIRE(CharIndexFromPoint(layout, 11, 10) == 1);   // 第 1 字左半边
    REQUIRE(CharIndexFromPoint(layout, 16, 10) == 2);   // 第 1 字右半边
}

TEST_CASE("SEL2 行首之左归到该行首字符", "[chatsel]")
{
    SelLayout layout = MakeThreeLineLayout();
    // 第二行（下标 5 起），x 取负数
    REQUIRE(CharIndexFromPoint(layout, -50, kLineH + 5) == 5);
}

TEST_CASE("SEL3 行尾之右归到该行末尾，且不含换行", "[chatsel]")
{
    SelLayout layout = MakeThreeLineLayout();
    // 第一行 5 个字符（0..4），末尾位置是 5
    REQUIRE(CharIndexFromPoint(layout, 9999, 5) == 5);
}

TEST_CASE("SEL4 首行之上归 0、末行之下归全文末尾", "[chatsel]")
{
    SelLayout layout = MakeThreeLineLayout();

    REQUIRE(CharIndexFromPoint(layout, 25, -100) == 0);
    REQUIRE(CharIndexFromPoint(layout, 25, 9999) == 15);
}

TEST_CASE("SEL5 多行时点第二行中间得到的是全局下标", "[chatsel]")
{
    SelLayout layout = MakeThreeLineLayout();
    // 第二行第 2 个字符（全局下标 7）的左半边
    REQUIRE(CharIndexFromPoint(layout, 21, kLineH + 5) == 7);
}

TEST_CASE("SEL6 空排版恒返回 0，不崩", "[chatsel]")
{
    const SelLayout empty;
    REQUIRE(CharIndexFromPoint(empty, 100, 100) == 0);
}

TEST_CASE("SEL7 一行内多段时能跨段命中", "[chatsel]")
{
    // 一行两段：文字 3 字（x 0..30）+ 表情 1 格（x 30..50）
    SelLine line;
    line.m_top    = 0;
    line.m_bottom = kLineH;
    line.m_runs.push_back(MakeRun(0, 3, 0));
    SelRun face;
    face.m_charBegin = 3;
    face.m_charCount = 1;
    face.m_left      = 30;
    face.m_right     = 50;
    face.m_charRights.push_back(50);
    line.m_runs.push_back(face);

    SelLayout layout;
    layout.m_lines.push_back(line);
    layout.m_totalChars = 4;

    REQUIRE(CharIndexFromPoint(layout, 35, 5) == 3);   // 表情格左半边 → 表情之前
    REQUIRE(CharIndexFromPoint(layout, 45, 5) == 4);   // 表情格右半边 → 表情之后
}

// =====================================================================
// SEL8-10. 选区归一化
// =====================================================================

TEST_CASE("SEL8 正向拖拽得到 [anchor, caret)", "[chatsel]")
{
    int s = 0;
    int e = 0;
    NormalizeSelRange(2, 7, s, e);
    REQUIRE(s == 2);
    REQUIRE(e == 7);
}

TEST_CASE("SEL9 反向拖拽也能得到正确区间", "[chatsel]")
{
    int s = 0;
    int e = 0;
    NormalizeSelRange(7, 2, s, e);
    REQUIRE(s == 2);
    REQUIRE(e == 7);
}

TEST_CASE("SEL10 两端相等时是空区间", "[chatsel]")
{
    int s = -1;
    int e = -1;
    NormalizeSelRange(4, 4, s, e);
    REQUIRE(s == 4);
    REQUIRE(e == 4);
    REQUIRE(s == e);
}

// =====================================================================
// SEL11-14. 选区矩形
// =====================================================================

TEST_CASE("SEL11 单行选区出一个矩形，左右边界正确", "[chatsel]")
{
    SelLayout layout = MakeThreeLineLayout();

    // 第一行第 1..3 个字符（下标 1..2）
    const std::vector<SelRect> rects = BuildSelectionRects(layout, 1, 3);

    REQUIRE(rects.size() == 1);
    REQUIRE(rects[0].m_left == 10);
    REQUIRE(rects[0].m_right == 30);
    REQUIRE(rects[0].m_top == 0);
    REQUIRE(rects[0].m_bottom == kLineH);
}

TEST_CASE("SEL12 跨两行出两个矩形：首行到行尾、次行从行首起", "[chatsel]")
{
    SelLayout layout = MakeThreeLineLayout();

    // 从第一行第 3 个字符到第二行第 2 个字符（下标 3..6）
    const std::vector<SelRect> rects = BuildSelectionRects(layout, 3, 7);

    REQUIRE(rects.size() == 2);
    REQUIRE(rects[0].m_left == 30);
    REQUIRE(rects[0].m_right == 50);    // 首行一直选到行尾
    REQUIRE(rects[1].m_left == 0);      // 次行从行首起
    REQUIRE(rects[1].m_right == 20);
    REQUIRE(rects[1].m_top == kLineH);
}

TEST_CASE("SEL13 跨三行时中间那行是整行满矩形", "[chatsel]")
{
    SelLayout layout = MakeThreeLineLayout();

    const std::vector<SelRect> rects = BuildSelectionRects(layout, 2, 13);

    REQUIRE(rects.size() == 3);
    REQUIRE(rects[1].m_left == 0);
    REQUIRE(rects[1].m_right == 50);    // 中间行整行
}

TEST_CASE("SEL14 空选区不出矩形；越界自动夹取", "[chatsel]")
{
    SelLayout layout = MakeThreeLineLayout();

    REQUIRE(BuildSelectionRects(layout, 5, 5).empty());
    REQUIRE(BuildSelectionRects(layout, 7, 3).empty());   // 传反了也不出矩形

    // 越界的端点被夹到 [0, total]，仍能出矩形而不是崩
    const std::vector<SelRect> all = BuildSelectionRects(layout, -100, 9999);
    REQUIRE(all.size() == 3);
}

// =====================================================================
// SEL15-18. 取选中文本
// =====================================================================

TEST_CASE("SEL15 纯文字选区取子串", "[chatsel]")
{
    const std::wstring flat = L"hello world";
    REQUIRE(ExtractSelectedText(flat, 0, 5, L"[face]") == L"hello");
    REQUIRE(ExtractSelectedText(flat, 6, 11, L"[face]") == L"world");
}

TEST_CASE("SEL16 选区跨过内联表情时替换成可读文案", "[chatsel]")
{
    std::wstring flat = L"ab";
    flat += kSelFacePlaceholder;
    flat += L"cd";

    // 全选：ab + [表情] + cd
    REQUIRE(ExtractSelectedText(flat, 0, 5, L"[表情]") == L"ab[表情]cd");
}

TEST_CASE("SEL17 选区正好只覆盖表情时只得到文案", "[chatsel]")
{
    std::wstring flat = L"ab";
    flat += kSelFacePlaceholder;
    flat += L"cd";

    REQUIRE(ExtractSelectedText(flat, 2, 3, L"[表情]") == L"[表情]");
}

TEST_CASE("SEL18 含 @ 片段时原文照抄，空区间得到空串", "[chatsel]")
{
    const std::wstring flat = L"@张三 明天开会";

    REQUIRE(ExtractSelectedText(flat, 0, 3, L"[表情]") == L"@张三");
    REQUIRE(ExtractSelectedText(flat, 4, 4, L"[表情]").empty());
    REQUIRE(ExtractSelectedText(flat, 9, 3, L"[表情]").empty());   // 传反了
}

// =====================================================================
// SEL19-23. 双击选词
// =====================================================================

TEST_CASE("SEL19 ASCII 词中双击选中整个词", "[chatsel]")
{
    const std::wstring flat = L"hello world";
    int s = 0;
    int e = 0;

    WordRangeAt(flat, 2, s, e);      // 落在 "hello" 中间
    REQUIRE(s == 0);
    REQUIRE(e == 5);

    WordRangeAt(flat, 8, s, e);      // 落在 "world" 中间
    REQUIRE(s == 6);
    REQUIRE(e == 11);
}

TEST_CASE("SEL20 汉字上双击只选该字", "[chatsel]")
{
    const std::wstring flat = L"明天开会";
    int s = 0;
    int e = 0;

    WordRangeAt(flat, 1, s, e);
    REQUIRE(s == 1);
    REQUIRE(e == 2);
}

TEST_CASE("SEL21 空白处双击选中连续空白段", "[chatsel]")
{
    const std::wstring flat = L"ab   cd";
    int s = 0;
    int e = 0;

    WordRangeAt(flat, 3, s, e);
    REQUIRE(s == 2);
    REQUIRE(e == 5);
}

TEST_CASE("SEL22 标点与表情占位只选一个字符", "[chatsel]")
{
    std::wstring flat = L"a,";
    flat += kSelFacePlaceholder;
    int s = 0;
    int e = 0;

    WordRangeAt(flat, 1, s, e);      // 逗号
    REQUIRE(s == 1);
    REQUIRE(e == 2);

    WordRangeAt(flat, 2, s, e);      // 表情占位
    REQUIRE(s == 2);
    REQUIRE(e == 3);
}

TEST_CASE("SEL23 越界下标夹到末字符；空文本不崩", "[chatsel]")
{
    const std::wstring flat = L"abc";
    int s = -1;
    int e = -1;

    WordRangeAt(flat, 999, s, e);    // 夹到最后一个字符所在的词
    REQUIRE(s == 0);
    REQUIRE(e == 3);

    const std::wstring empty;
    WordRangeAt(empty, 0, s, e);
    REQUIRE(s == 0);
    REQUIRE(e == 0);
}
