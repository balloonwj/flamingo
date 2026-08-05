/**
 *  HistoryContentViewTests.cpp —— 「消息记录」多行内容块的单元测试（Suite HCV）
 *
 *  被测的是 HistoryContentView 的<u>测量</u>那一半：折叠态高度恰好夹到 5 行、展开态
 *  按内容长、含表情的行按 28px 行高算、以及"要不要画展开链接"的判据。列表行高就是
 *  照 MeasureHeight 定的，量错一像素整屏都会歪，所以这部分必须有用例钉住。
 *
 *  绘制（OnPaint）不在本文件覆盖范围：它要求进程已初始化 GDI+、且要能从 Bin/Face
 *  读到表情 PNG，属于靠截图目视核对的部分（见 docs/消息记录内容交互方案.md §8）。
 *
 *  断言尽量写成"相对关系 + 行高的整数倍"而不是具体像素值 —— 像素值会随系统字体
 *  版本变化，钉死它只会让用例变脆。
 *
 *  balloonwj@qq.com   2026-08-05
 */
#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include "../BalloonHistory/HistoryContentView.h"
#include "../BalloonChatWnd/ChatFaceTag.h"

#include <vector>

using namespace balloonhistory;

namespace {

// 与 HistoryContentView 默认字号一致（像素）。用例里不改字号，故两边必须对齐，
// 否则算出来的"一行有多高"跟被测对象量的不是一回事。
const int kTestFontPx = kHistoryContentDefaultFontPx;

/**
 *  量"一行有多高"用的内存 DC：自带一支与 HistoryContentView 参数完全相同的字体
 *  并已 select 进去。
 *
 *  为什么用例需要它 —— 被测对象的行高来自真实字体度量（`max(字体行高, 表情格边长)`），
 *  在用例里没法凭空写死。这里按同一套参数建同一支字体、量出 TEXTMETRIC，用例的
 *  期望值才能表达成"恰好 N 行"而不是某个具体像素数。
 *  封成 RAII 是免得每个用例各写一遍建 / 还原 / 销毁。
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

    // 纯文字内容的行高：正文里没有表情时，排版引擎的行高就是字体行高
    // （HistoryContentView 此时给引擎传的表情格边长是 0）。
    int TextLineHeight() const
    {
        TEXTMETRICW tm;
        ::ZeroMemory(&tm, sizeof(tm));
        ::GetTextMetricsW(m_hdc, &tm);
        return tm.tmHeight;
    }

    // 含表情的内容的行高：排版引擎取 max(字体行高, 表情格边长)。
    int FaceLineHeight() const
    {
        const int textH = TextLineHeight();
        return (textH > balloonchatwnd::kFaceDisplayPx) ? textH
                                                        : balloonchatwnd::kFaceDisplayPx;
    }

private:
    HDC   m_hdc;
    HFONT m_font;
    HFONT m_oldFont;
};

// 造一段 count 个汉字的正文。用汉字而不是英文：汉字之间处处可折行，行数只取决于
// 可用宽度，用例才好预估；连续英文字母会被当成一个不可分割的词元走另一条排版分支。
CString MakeHanText(int count)
{
    CString text;
    for (int i = 0; i < count; ++i)
    {
        text += _T("字");
    }
    return text;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// HCV1-HCV4  折叠 / 展开态的高度与「展开」链接的出现条件
// ---------------------------------------------------------------------------

TEST_CASE("HCV1 短文本只占一行，不需要展开链接", "[historycontent]")
{
    TestDc dc;
    REQUIRE(dc.Get() != NULL);

    HistoryContentView view;
    view.SetContent(_T("短短一句"));

    const int kAvailWidth = 400;   //宽到这句话一行就放得下
    CHECK(view.MeasureHeight(kAvailWidth) == dc.TextLineHeight());
    CHECK_FALSE(view.NeedsExpandLink(kAvailWidth));
    //默认是折叠态。
    CHECK_FALSE(view.IsExpanded());
}

TEST_CASE("HCV2 长文本折叠态恰好夹到 5 行，并要求画展开链接", "[historycontent]")
{
    TestDc dc;
    REQUIRE(dc.Get() != NULL);

    HistoryContentView view;
    //200 个汉字，在 200px 宽里怎么排都远远超过 5 行。
    view.SetContent(MakeHanText(200));

    const int kAvailWidth = 200;
    //高度必须<u>恰好</u>是 5 行 —— 多一行就会在折叠态下方留出一条空白。
    CHECK(view.MeasureHeight(kAvailWidth)
          == kHistoryCollapsedMaxLines * dc.TextLineHeight());
    CHECK(view.NeedsExpandLink(kAvailWidth));
}

TEST_CASE("HCV3 同一段长文本展开后明显更高，收起后回到 5 行", "[historycontent]")
{
    TestDc dc;
    REQUIRE(dc.Get() != NULL);

    HistoryContentView view;
    view.SetContent(MakeHanText(200));

    const int kAvailWidth = 200;
    const int collapsedH = view.MeasureHeight(kAvailWidth);

    view.SetExpanded(true);
    REQUIRE(view.IsExpanded());
    const int expandedH = view.MeasureHeight(kAvailWidth);

    CHECK(expandedH > collapsedH);
    //展开后高度仍是行高的整数倍，且行数确实多于折叠上限。
    CHECK(expandedH % dc.TextLineHeight() == 0);
    CHECK(expandedH / dc.TextLineHeight() > kHistoryCollapsedMaxLines);

    //展开之后链接仍要在（否则"收起"就点不着了）。
    CHECK(view.NeedsExpandLink(kAvailWidth));

    //收起回到原来的高度 —— 这一条同时钉住测量缓存的键里带了折叠态，
    //否则切换回来会拿到展开态那个高度。
    view.SetExpanded(false);
    CHECK(view.MeasureHeight(kAvailWidth) == collapsedH);
}

TEST_CASE("HCV4 不足 5 行的内容，折叠态与展开态一样高", "[historycontent]")
{
    TestDc dc;
    REQUIRE(dc.Get() != NULL);

    HistoryContentView view;
    //40 个汉字在 400px 宽里排下来只有两行上下，够不着 5 行的上限。
    view.SetContent(MakeHanText(40));

    const int kAvailWidth = 400;
    const int collapsedH = view.MeasureHeight(kAvailWidth);
    REQUIRE(collapsedH > 0);
    REQUIRE(collapsedH < kHistoryCollapsedMaxLines * dc.TextLineHeight());

    view.SetExpanded(true);
    CHECK(view.MeasureHeight(kAvailWidth) == collapsedH);
    //内容本来就是全的，不该画「展开」链接。
    CHECK_FALSE(view.NeedsExpandLink(kAvailWidth));
}

// ---------------------------------------------------------------------------
// HCV5  含内联表情时的行高
// ---------------------------------------------------------------------------

TEST_CASE("HCV5 含表情的一行按表情格边长算，比纯文字的一行更高", "[historycontent]")
{
    TestDc dc;
    REQUIRE(dc.Get() != NULL);
    //本用例的前提：28px 的表情格确实比 14px 字体的行高要高。前提不成立时下面
    //"更高"的断言就没有意义了，故先钉住它。
    REQUIRE(dc.FaceLineHeight() > dc.TextLineHeight());

    const int kAvailWidth = 400;

    HistoryContentView plain;
    plain.SetContent(_T("你好世界"));

    HistoryContentView withFace;
    //正文里的表情写成 /f["<id>"]，与发送管线的编码口径一致。
    withFace.SetContent(_T("你好/f[\"5\"]世界"));

    const int plainH = plain.MeasureHeight(kAvailWidth);
    const int faceH  = withFace.MeasureHeight(kAvailWidth);

    CHECK(plainH == dc.TextLineHeight());
    CHECK(faceH == dc.FaceLineHeight());
    //同样是一行，含表情的那行更高。
    CHECK(faceH > plainH);
}

// ---------------------------------------------------------------------------
// HCV6  可用宽度对行数与「展开」链接的影响
// ---------------------------------------------------------------------------

TEST_CASE("HCV6 宽度越窄行数越多，折叠态仍夹在 5 行但开始需要展开链接", "[historycontent]")
{
    TestDc dc;
    REQUIRE(dc.Get() != NULL);

    HistoryContentView view;
    //60 个汉字：宽的时候三行上下，窄的时候十行开外。
    view.SetContent(MakeHanText(60));

    const int kWideWidth   = 400;   //宽到 5 行以内排得完
    const int kNarrowWidth = 80;    //窄到远远超过 5 行

    const int wideH = view.MeasureHeight(kWideWidth);
    REQUIRE(wideH < kHistoryCollapsedMaxLines * dc.TextLineHeight());
    CHECK_FALSE(view.NeedsExpandLink(kWideWidth));

    const int narrowH = view.MeasureHeight(kNarrowWidth);
    //折叠态无论多窄都只画 5 行。
    CHECK(narrowH == kHistoryCollapsedMaxLines * dc.TextLineHeight());
    CHECK(narrowH > wideH);
    //同一段内容，只是变窄了，就需要展开链接了。
    CHECK(view.NeedsExpandLink(kNarrowWidth));
}

// ---------------------------------------------------------------------------
// HCV7  空正文
// ---------------------------------------------------------------------------

TEST_CASE("HCV7 空正文有一行的下限高度，不为 0", "[historycontent]")
{
    TestDc dc;
    REQUIRE(dc.Get() != NULL);

    HistoryContentView view;
    view.SetContent(_T(""));

    const int kAvailWidth = 400;
    const int h = view.MeasureHeight(kAvailWidth);
    //高度不能是 0：那一行会整个塌掉，界面上看到的是一条什么都没有的空行。
    CHECK(h > 0);
    CHECK(h == dc.TextLineHeight());
    CHECK_FALSE(view.NeedsExpandLink(kAvailWidth));

    //可用宽度非法（<= 0）时按 1 处理，不崩、也仍然有下限高度。
    CHECK(view.MeasureHeight(0) > 0);
    CHECK(view.MeasureHeight(-100) > 0);
}
