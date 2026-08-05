/**
 *  ChatInlineTextTests.cpp —— 「文字 + 内联表情」排版引擎的单元测试（Suite CIT）
 *
 *  这套排版代码原先是 ChatBubble.cpp 里一段匿名 namespace 的私有实现，2026-08-05
 *  为了让消息记录窗也能把表情画成表情而抽成公开模块（见 docs/消息记录内容交互方案.md §4）。
 *  抽取属于"改存量渲染路径"，本文件的用例就是那次搬迁的<u>行为基线</u>：
 *  折行规则、表情占位、扁平文本字符口径、以及新加的"最多排几行"。
 *
 *  测量依赖真实字体度量，故用一个内存 DC + 固定字体（微软雅黑 14px）来跑。断言尽量
 *  写成"相对关系"（几行、谁比谁宽、区间是否连续）而不是具体像素值 —— 像素值会随
 *  系统字体版本变化，钉死它只会让用例变脆。
 *
 *  绘制（DrawInlineContent）不在本文件覆盖范围：它要求进程已初始化 GDI+ 且需要
 *  Bin/Face 下的表情 PNG，属于靠目视核对的部分。
 *
 *  balloonwj@qq.com   2026-08-05
 */
#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include "../BalloonChatWnd/ChatInlineText.h"
#include "../BalloonChat/BalloonChatMsgFormat.h"

#include <vector>

using namespace balloonchatwnd;

namespace {

// 表情格边长（像素）。与气泡的 kFaceDisplayPx 取同一个值，使"表情比文字高"这条
// 前提在用例里成立（14px 字体的行高约 19px，小于 28）。
const int kTestFaceSize = 28;

// 测量用的固定字号（像素）。取 14 —— 与气泡正文同一档。
const int kTestFontPx = 14;

/**
 *  测量用的内存 DC：自带一支固定字体并已 select 进去。
 *
 *  排版的每个入口都要求"调用前已把目标字体 select 进 hdc"，所以每个用例都需要这么
 *  一份环境；封成 RAII 免得每个用例各写一遍建 / 还原 / 销毁。
 */
class TestDc
{
public:
    TestDc()
        : m_hdc(NULL)
        , m_font(NULL)
        , m_oldFont(NULL)
    {
        m_hdc = ::CreateCompatibleDC(NULL);
        m_font = ::CreateFontW(-kTestFontPx, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                               DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                               CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                               L"Microsoft YaHei");
        if (m_hdc != NULL && m_font != NULL)
        {
            m_oldFont = (HFONT)::SelectObject(m_hdc, m_font);
        }
    }

    ~TestDc()
    {
        if (m_hdc != NULL && m_oldFont != NULL)
        {
            ::SelectObject(m_hdc, m_oldFont);
        }
        if (m_font != NULL)
        {
            ::DeleteObject(m_font);
        }
        if (m_hdc != NULL)
        {
            ::DeleteDC(m_hdc);
        }
    }

    HDC Get() const { return m_hdc; }

    // 本 DC 当前字体下，一行内容有多高（排版用的行高取它与表情格边长的较大者）。
    int LineHeight() const
    {
        TEXTMETRICW tm;
        ::ZeroMemory(&tm, sizeof(tm));
        ::GetTextMetricsW(m_hdc, &tm);
        const int textH = tm.tmHeight;
        return (textH > kTestFaceSize) ? textH : kTestFaceSize;
    }

private:
    HDC   m_hdc;
    HFONT m_font;
    HFONT m_oldFont;
};

// 数一数放置项一共占了几行（放置项按文档顺序产出，top 变了就是换行）。
int CountLines(const std::vector<InlinePlaced>& placed)
{
    int lines = 0;
    int lastTop = -1;
    for (size_t i = 0; i < placed.size(); ++i)
    {
        if (lines == 0 || placed[i].rc.top != lastTop)
        {
            ++lines;
            lastTop = placed[i].rc.top;
        }
    }
    return lines;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// CIT1-CIT3  基本排版：单行、折行、表情格
// ---------------------------------------------------------------------------

TEST_CASE("CIT1 纯文字放得下时排成一行", "[chatinline]")
{
    TestDc dc;
    REQUIRE(dc.Get() != NULL);

    std::vector<balloonchat::ChatSegment> segs;
    segs.push_back(balloonchat::ChatSegment::Text(_T("你好")));

    std::vector<InlinePlaced> placed;
    SIZE sz = LayoutInlineContent(dc.Get(), segs, 400, kTestFaceSize, &placed);

    CHECK(CountLines(placed) == 1);
    CHECK(sz.cy == dc.LineHeight());
    CHECK(sz.cx > 0);
    CHECK(sz.cx <= 400);
}

TEST_CASE("CIT2 放不下时折行，可用宽度越窄行数越多", "[chatinline]")
{
    TestDc dc;
    REQUIRE(dc.Get() != NULL);

    std::vector<balloonchat::ChatSegment> segs;
    segs.push_back(balloonchat::ChatSegment::Text(
        _T("这是一段用来测试自动折行的中文文本内容需要足够长才能折出好几行来")));

    std::vector<InlinePlaced> wide;
    SIZE szWide = LayoutInlineContent(dc.Get(), segs, 400, kTestFaceSize, &wide);
    std::vector<InlinePlaced> narrow;
    SIZE szNarrow = LayoutInlineContent(dc.Get(), segs, 100, kTestFaceSize, &narrow);

    CHECK(CountLines(narrow) > CountLines(wide));
    CHECK(szNarrow.cy > szWide.cy);
    //折行不会把内容画到可用宽度之外。
    CHECK(szNarrow.cx <= 100);
    CHECK(szWide.cx <= 400);
    //高度恒为行高的整数倍。
    CHECK(szNarrow.cy % dc.LineHeight() == 0);
}

TEST_CASE("CIT3 表情排成一个方格，在扁平文本里占一个字符", "[chatinline]")
{
    TestDc dc;
    REQUIRE(dc.Get() != NULL);

    std::vector<balloonchat::ChatSegment> segs;
    segs.push_back(balloonchat::ChatSegment::Text(_T("你好")));
    segs.push_back(balloonchat::ChatSegment::Face(5));
    segs.push_back(balloonchat::ChatSegment::Text(_T("再见")));

    std::vector<InlinePlaced> placed;
    LayoutInlineContent(dc.Get(), segs, 400, kTestFaceSize, &placed);

    int faceCount = 0;
    for (size_t i = 0; i < placed.size(); ++i)
    {
        if (!placed[i].isFace)
        {
            continue;
        }
        ++faceCount;
        CHECK(placed[i].faceId == 5);
        //表情格是正方形，边长就是传入的 faceSize。
        CHECK((placed[i].rc.right - placed[i].rc.left) == kTestFaceSize);
        CHECK((placed[i].rc.bottom - placed[i].rc.top) == kTestFaceSize);
        //在扁平文本里恒占一个字符位（U+FFFC 占位）。
        CHECK(placed[i].charCount == 1);
        //夹在"你好"(2 字)之后，故下标为 2。
        CHECK(placed[i].charBegin == 2);
    }
    CHECK(faceCount == 1);
}

// ---------------------------------------------------------------------------
// CIT4-CIT5  扁平文本与"是否含表情"
// ---------------------------------------------------------------------------

TEST_CASE("CIT4 扁平文本里表情为 U+FFFC，长度等于文字数加表情数", "[chatinline]")
{
    std::vector<balloonchat::ChatSegment> segs;
    segs.push_back(balloonchat::ChatSegment::Text(_T("你好")));
    segs.push_back(balloonchat::ChatSegment::Face(5));
    segs.push_back(balloonchat::ChatSegment::Text(_T("再见")));
    segs.push_back(balloonchat::ChatSegment::Face(9));

    std::wstring flat = BuildFlatText(segs);

    REQUIRE(flat.size() == 6);                 // 2 + 1 + 2 + 1
    CHECK(flat[0] == L'你');
    CHECK(flat[2] == kSelFacePlaceholder);
    CHECK(flat[3] == L'再');
    CHECK(flat[5] == kSelFacePlaceholder);

    //纯文字时扁平文本就是原文。
    std::vector<balloonchat::ChatSegment> plain;
    plain.push_back(balloonchat::ChatSegment::Text(_T("abc")));
    CHECK(BuildFlatText(plain) == std::wstring(L"abc"));

    //空段序列产出空串。
    std::vector<balloonchat::ChatSegment> none;
    CHECK(BuildFlatText(none).empty());
}

TEST_CASE("CIT5 MessageTextHasFace 认得出正文里的表情标记", "[chatinline]")
{
    CHECK_FALSE(MessageTextHasFace(_T("")));
    CHECK_FALSE(MessageTextHasFace(_T("明天见")));
    CHECK(MessageTextHasFace(_T("明天见 /f[\"5\"]")));
    CHECK(MessageTextHasFace(_T("/f[\"0\"]开头就是表情")));
}

// ---------------------------------------------------------------------------
// CIT6-CIT7  行数上限（消息记录窗折叠态用；气泡侧不限行，行为须与抽取前一致）
// ---------------------------------------------------------------------------

TEST_CASE("CIT6 行数上限：排满即停并回报还有剩余", "[chatinline]")
{
    TestDc dc;
    REQUIRE(dc.Get() != NULL);

    std::vector<balloonchat::ChatSegment> segs;
    segs.push_back(balloonchat::ChatSegment::Text(
        _T("这是一段很长很长的中文文本用来把内容撑到远远超过五行以便验证行数上限"
           "确实生效并且高度被夹到恰好等于上限行数不会凭空多出一行空白来")));

    //先不限行，确认这段内容本来远超 5 行。
    std::vector<InlinePlaced> full;
    bool fullTruncated = true;
    SIZE szFull = LayoutInlineContent(dc.Get(), segs, 120, kTestFaceSize, &full,
                                      0, &fullTruncated);
    REQUIRE(CountLines(full) > 5);
    CHECK_FALSE(fullTruncated);   //不限行时恒为 false

    //限 5 行。
    const int kMaxLines = 5;
    std::vector<InlinePlaced> clipped;
    bool truncated = false;
    SIZE szClipped = LayoutInlineContent(dc.Get(), segs, 120, kTestFaceSize, &clipped,
                                         kMaxLines, &truncated);
    CHECK(truncated);
    CHECK(CountLines(clipped) == kMaxLines);
    //高度恰为 5 行，不多不少 —— 多一行就会在折叠态下留出一条空白。
    CHECK(szClipped.cy == kMaxLines * dc.LineHeight());
    CHECK(szClipped.cy < szFull.cy);
    //截断只是少排了后面的内容，已排出来的那部分与不限行时逐项一致。
    REQUIRE(clipped.size() <= full.size());
    for (size_t i = 0; i < clipped.size(); ++i)
    {
        CHECK(clipped[i].charBegin == full[i].charBegin);
        CHECK(clipped[i].rc.top == full[i].rc.top);
        CHECK(clipped[i].rc.left == full[i].rc.left);
    }
}

TEST_CASE("CIT7 内容不够上限时不算截断，与不限行结果完全一致", "[chatinline]")
{
    TestDc dc;
    REQUIRE(dc.Get() != NULL);

    std::vector<balloonchat::ChatSegment> segs;
    segs.push_back(balloonchat::ChatSegment::Text(_T("短短一句")));

    std::vector<InlinePlaced> unlimited;
    SIZE szUnlimited = LayoutInlineContent(dc.Get(), segs, 400, kTestFaceSize, &unlimited);

    std::vector<InlinePlaced> limited;
    bool truncated = true;
    SIZE szLimited = LayoutInlineContent(dc.Get(), segs, 400, kTestFaceSize, &limited,
                                         5, &truncated);

    CHECK_FALSE(truncated);
    CHECK(szLimited.cx == szUnlimited.cx);
    CHECK(szLimited.cy == szUnlimited.cy);
    REQUIRE(limited.size() == unlimited.size());

    //这条是抽取那次搬迁的回归哨兵：气泡侧传 0（不限行），排版结果必须与从前一模一样。
    std::vector<InlinePlaced> zeroLimit;
    LayoutInlineContent(dc.Get(), segs, 400, kTestFaceSize, &zeroLimit, 0, NULL);
    REQUIRE(zeroLimit.size() == unlimited.size());
    for (size_t i = 0; i < zeroLimit.size(); ++i)
    {
        CHECK(zeroLimit[i].rc.left == unlimited[i].rc.left);
        CHECK(zeroLimit[i].rc.top == unlimited[i].rc.top);
        CHECK(zeroLimit[i].charBegin == unlimited[i].charBegin);
        CHECK(zeroLimit[i].charCount == unlimited[i].charCount);
    }
}

// ---------------------------------------------------------------------------
// CIT8-CIT9  折行的两处边角：超长词元、显式换行
// ---------------------------------------------------------------------------

TEST_CASE("CIT8 中间没有空格的超长串在字符间断开，不画出可用宽度之外", "[chatinline]")
{
    TestDc dc;
    REQUIRE(dc.Get() != NULL);

    //长网址这类连续 ASCII 串会被当成一个不可分割的词元，词间折行救不了它。
    std::vector<balloonchat::ChatSegment> segs;
    segs.push_back(balloonchat::ChatSegment::Text(
        _T("https://example.com/a/very/long/path/that/never/breaks/anywhere/at/all")));

    const int kMaxW = 120;
    std::vector<InlinePlaced> placed;
    SIZE sz = LayoutInlineContent(dc.Get(), segs, kMaxW, kTestFaceSize, &placed);

    CHECK(CountLines(placed) > 1);
    CHECK(sz.cx <= kMaxW);
    //每一片都在可用宽度之内。
    for (size_t i = 0; i < placed.size(); ++i)
    {
        CHECK(placed[i].rc.right <= kMaxW);
    }
    //切碎之后各片的字符区间仍然首尾相接、不重不漏 —— 选中复制靠的就是这个。
    int expectBegin = 0;
    for (size_t i = 0; i < placed.size(); ++i)
    {
        CHECK(placed[i].charBegin == expectBegin);
        expectBegin += placed[i].charCount;
    }
}

TEST_CASE("CIT9 显式换行各占一行，且不打乱字符下标", "[chatinline]")
{
    TestDc dc;
    REQUIRE(dc.Get() != NULL);

    std::vector<balloonchat::ChatSegment> segs;
    segs.push_back(balloonchat::ChatSegment::Text(_T("第一行\n第二行\n第三行")));

    std::vector<InlinePlaced> placed;
    SIZE sz = LayoutInlineContent(dc.Get(), segs, 400, kTestFaceSize, &placed);

    CHECK(CountLines(placed) == 3);
    CHECK(sz.cy == 3 * dc.LineHeight());

    //换行符本身不产出放置项，但要推进字符游标，否则后面的下标整体前移。
    //"第一行"(3) + "\n"(1) = 4，故第二行首项的 charBegin 为 4。
    REQUIRE(placed.size() >= 2);
    CHECK(placed[0].charBegin == 0);
    bool foundSecondLine = false;
    for (size_t i = 0; i < placed.size(); ++i)
    {
        if (placed[i].rc.top == dc.LineHeight())
        {
            CHECK(placed[i].charBegin == 4);
            foundSecondLine = true;
            break;
        }
    }
    CHECK(foundSecondLine);
}

// ---------------------------------------------------------------------------
// CIT10  排版结果 → 选区模型
// ---------------------------------------------------------------------------

TEST_CASE("CIT10 选区模型按行切分，逐字符边界单调递增", "[chatinline]")
{
    TestDc dc;
    REQUIRE(dc.Get() != NULL);

    std::vector<balloonchat::ChatSegment> segs;
    segs.push_back(balloonchat::ChatSegment::Text(_T("你好")));
    segs.push_back(balloonchat::ChatSegment::Face(5));
    segs.push_back(balloonchat::ChatSegment::Text(_T("世界")));

    std::vector<InlinePlaced> placed;
    LayoutInlineContent(dc.Get(), segs, 400, kTestFaceSize, &placed);

    const std::wstring flat = BuildFlatText(segs);
    SelLayout layout;
    BuildSelLayoutFromPlaced(dc.Get(), placed, (int)flat.size(), layout);

    CHECK(layout.m_totalChars == (int)flat.size());
    REQUIRE(layout.m_lines.size() == 1);   //这点内容一行放得下

    const SelLine& line = layout.m_lines[0];
    CHECK(line.m_bottom > line.m_top);
    REQUIRE(!line.m_runs.empty());

    //每一段的逐字符右边界数目须与该段字符数一致，且严格递增 —— 否则命中测试会
    //把好几个字符都算到同一个位置上，选中时"只能整段整段地选"。
    for (size_t i = 0; i < line.m_runs.size(); ++i)
    {
        const SelRun& run = line.m_runs[i];
        CHECK((int)run.m_charRights.size() == run.m_charCount);
        for (size_t k = 1; k < run.m_charRights.size(); ++k)
        {
            CHECK(run.m_charRights[k] >= run.m_charRights[k - 1]);
        }
    }
}
