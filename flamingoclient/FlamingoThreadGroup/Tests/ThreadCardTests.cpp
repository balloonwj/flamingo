/**
 *  帖子卡片排版的单元测试（标签 [threaddll]，docs/帖子群方案.md 第 8 节 TGD12）：
 *    - TGD12 卡片在不同宽度下的排版：没有标题、正文、图片时只有作者行与底部一行（底部也没有内容时连它也不要）；正文超过三行时截短并以「…」结尾，截短后
 *            正好不超过三行，越窄截得越多；放得下时原样显示；缩略图最多三张，其余记进「+N」；截短不拆开 4 字节字符。
 *  测量用屏幕 DC 与一个固定字号的字体，结果只与字体有关、与窗口无关。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-07
 */
#include "catch_amalgamated.hpp"

#include <windows.h>
#include <string>

#include "../UI/ThreadCardMetrics.h"

using namespace threadgroup;

namespace {

//测试用字体的字高（逻辑单位，负值表示按字符高度取），与界面默认的 9 磅字体相当
const int kTestFontHeight = -12;

//测试用的卡片内容区宽度：左栏 380 减去列表两侧外边距与卡片内边距；以及一个更窄的宽度
const int kWideWidth = 336;
const int kNarrowWidth = 200;

//一段足够长、在上面两种宽度下都超过三行的正文
const wchar_t* const kLongText =
    L"候选三个地方：西山徒步、湖边烧烤、室内密室逃脱。大家在下面留言投票，周三晚上统计。"
    L"另外请大家顺便说一下周五下午能不能早点走，需要提前跟各自的主管打招呼，场地那边也要提前两天预订。"
    L"如果有人有更好的去处也欢迎在下面补充，最终以投票结果为准。";

//屏幕 DC 与测试字体，析构时释放
class TestDc
{
public:
    TestDc()
        : m_hdc(::GetDC(NULL))
        , m_font(::CreateFontW(kTestFontHeight, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                               CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei"))
        , m_bold(::CreateFontW(kTestFontHeight, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                               CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei"))
    {
    }

    ~TestDc()
    {
        ::DeleteObject(m_font);
        ::DeleteObject(m_bold);
        ::ReleaseDC(NULL, m_hdc);
    }

    HDC   m_hdc;    //屏幕 DC
    HFONT m_font;   //正文字体
    HFONT m_bold;   //标题字体
};

//一行文字的高度
int LineHeight(const TestDc& dc)
{
    HGDIOBJ old = ::SelectObject(dc.m_hdc, dc.m_font);
    const int h = MeasureWrappedHeight(dc.m_hdc, L"A", kWideWidth);
    ::SelectObject(dc.m_hdc, old);
    return h;
}

} // namespace

TEST_CASE("TGD12 帖子卡片在不同宽度下的排版：三行截断、缩略图与「+N」", "[threaddll]")
{
    TestDc dc;
    REQUIRE(dc.m_hdc != NULL);
    REQUIRE(dc.m_font != NULL);
    const int line = LineHeight(dc);
    REQUIRE(line > 0);

    SECTION("没有标题、正文与图片：只有作者行与底部一行")
    {
        const PostCardMetrics m = MeasurePostCard(dc.m_hdc, dc.m_font, dc.m_bold, kWideWidth, L"", L"", 0, true);
        CHECK(m.m_titleHeight == 0);
        CHECK(m.m_textHeight == 0);
        CHECK(m.m_thumbCount == 0);
        CHECK(m.m_height == kCardPadY + kCardMetaHeight + kCardGap + kCardFootHeight + kCardPadY);
        CHECK(m.m_hasFoot);
    }

    SECTION("没有互动也没有留言时不留底部那一行")
    {
        const PostCardMetrics m = MeasurePostCard(dc.m_hdc, dc.m_font, dc.m_bold, kWideWidth, L"", L"", 0, false);
        CHECK_FALSE(m.m_hasFoot);
        CHECK(m.m_height == kCardPadY + kCardMetaHeight + kCardPadY);
    }

    SECTION("短正文放得下时原样显示，不截短")
    {
        const PostCardMetrics m = MeasurePostCard(dc.m_hdc, dc.m_font, dc.m_bold, kWideWidth, L"标题", L"短正文", 0, true);
        CHECK(m.m_titleHeight > 0);
        CHECK(m.m_text == L"短正文");
        CHECK_FALSE(m.m_textCut);
        CHECK(m.m_textHeight == line);
        CHECK(m.m_height == kCardPadY + kCardMetaHeight + kCardGap + m.m_titleHeight + kCardGap + line + kCardGap
                            + kCardFootHeight + kCardPadY);
    }

    SECTION("长正文截短到三行并以省略号结尾；越窄截得越多")
    {
        const PostCardMetrics wide = MeasurePostCard(dc.m_hdc, dc.m_font, dc.m_bold, kWideWidth, L"", kLongText, 0, true);
        const PostCardMetrics narrow = MeasurePostCard(dc.m_hdc, dc.m_font, dc.m_bold, kNarrowWidth, L"", kLongText, 0, true);
        CHECK(wide.m_textCut);
        CHECK(narrow.m_textCut);
        CHECK(wide.m_textHeight <= line * kCardMaxTextLines);
        CHECK(narrow.m_textHeight <= line * kCardMaxTextLines);
        //截短得刚好：正好三行，不会只剩两行
        CHECK(wide.m_textHeight == line * kCardMaxTextLines);
        CHECK(wide.m_text[wide.m_text.size() - 1] == L'\x2026');
        CHECK(narrow.m_text.size() < wide.m_text.size());
    }

    SECTION("缩略图最多三张，其余记进「+N」")
    {
        const PostCardMetrics two = MeasurePostCard(dc.m_hdc, dc.m_font, dc.m_bold, kWideWidth, L"", L"", 2, true);
        CHECK(two.m_thumbCount == 2);
        CHECK(two.m_moreCount == 0);
        const PostCardMetrics nine = MeasurePostCard(dc.m_hdc, dc.m_font, dc.m_bold, kWideWidth, L"", L"", 9, true);
        CHECK(nine.m_thumbCount == kCardMaxThumbs);
        CHECK(nine.m_moreCount == 9 - kCardMaxThumbs);
        CHECK(nine.m_height == kCardPadY + kCardMetaHeight + kCardGap + kCardThumbPx + kCardGap + kCardFootHeight + kCardPadY);
    }

    SECTION("截短不拆开 4 字节字符")
    {
        //一长串 4 字节字符（每个占两个 UTF-16 单元），截短后省略号前面不能是半个代理对
        std::wstring text;
        const wchar_t kPair[] = { 0xD83D, 0xDE00, 0 };
        const int kRepeat = 400;
        for (int i = 0; i < kRepeat; ++i)
            text += kPair;
        HGDIOBJ old = ::SelectObject(dc.m_hdc, dc.m_font);
        bool cut = false;
        const std::wstring fitted = FitTextToLines(dc.m_hdc, text, kNarrowWidth, kCardMaxTextLines, cut);
        ::SelectObject(dc.m_hdc, old);
        REQUIRE(cut);
        REQUIRE(fitted.size() >= 2);
        const wchar_t beforeEllipsis = fitted[fitted.size() - 2];
        CHECK_FALSE((beforeEllipsis >= 0xD800 && beforeEllipsis <= 0xDBFF));
        CHECK((fitted.size() - 1) % 2 == 0);
    }
}
