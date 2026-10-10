/**
 *  ConversationRowLayoutTests.cpp
 *
 *  会话行第二行排布（balloonmain::LayoutConvRowSecondLine）与按宽度截断文字
 *  （balloonmain::FitTextWithEllipsis）的单元测试，标签 [convrowlayout]
 *  （docs/消息提醒模式与消息盒子方案.md 第 13 节）：
 *    CRW1  一项徽章都没有：摘要占到时间列左端为止，未读数贴右端；
 *    CRW2  全部徽章都在、宽度充裕：从左往右依次是 ★、收藏、字幕、通话中、仅通知、提醒模式图标、未读数，
 *          间距统一，互不重叠，摘要右端紧挨第一项左边再留一个间距；
 *    CRW3  宽度逐步收窄：按「收藏 → 字幕 → 仅通知 → ★」的顺序省略，通话中、提醒模式图标、未读数始终保留；
 *    CRW4  只要还有可省略的徽章在场，摘要宽度不小于最小宽度；
 *    CRW5  摘要为空时不为它保留最小宽度，徽章不被省略；
 *    CRW6  m_summaryCap 只限制摘要，不限制右侧徽章区；
 *    CRW7  各种徽章组合、各种宽度下：每一项都在 [m_left, m_right] 之内、互不重叠、摘要不压到任何一项；
 *    CRW8  FitTextWithEllipsis：放得下原样返回；放不下补 "..." 且不超宽；连省略号都放不下返回空串；
 *          不把代理对从中间截开。
 *
 *  纯函数，不建窗口；CRW8 用屏幕 DC 与系统默认 GUI 字体测量。框架 Catch2 v3。
 *
 *  balloonwj@qq.com   2026-10-07
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <string>
#include <vector>

#include "../UI/BalloonMain/ConversationRowLayout.h"

using namespace balloonmain;

namespace {

// 测试用的第二行几何（像素）：摘要起点、右边界、时间列左端（摘要上限）。取主面板 320 宽的会话列表：
// 名字起点 = 头像 36 + 间距 6 + 展开箭头区 16 = 58，右边界 = 320 - 8。
const int kLeft = 58;
const int kRight = 312;
const int kCap = 270;

// 统一间距与摘要最小宽度，与 MainPanelTreeView.cpp 的取值相同。
const int kGap = 4;
const int kMinSummary = 48;

// 各项宽度：星标、书签、字幕徽章 12；「通话中」胶囊约 50；「仅通知」标签约 40；提醒模式图标 12；未读胶囊 16。
const int kIconW = 12;
const int kInCallW = 50;
const int kNotifyOnlyW = 40;
const int kModeIconW = 12;
const int kUnreadW = 16;

// 不画的项的左边界取值。
const int kNotDrawn = kConvRowNotDrawn;

// 按位掩码造一份输入：bit i 为 1 表示有第 i 种徽章（下标见 ConvRowMark）。
ConvRowSecondLineInput MakeInput(int left, int right, int cap, unsigned int markMask, bool modeIcon, bool unread,
                                 bool hasSummary)
{
    ConvRowSecondLineInput in;
    in.m_left = left;
    in.m_right = right;
    in.m_summaryCap = cap;
    in.m_hasSummary = hasSummary;
    in.m_minSummaryWidth = kMinSummary;
    in.m_gap = kGap;
    const int widths[conv_row_mark_count] = { kIconW, kIconW, kIconW, kInCallW, kNotifyOnlyW };
    for (int i = 0; i < conv_row_mark_count; ++i)
    {
        in.m_markWidth[i] = ((markMask >> i) & 1u) ? widths[i] : 0;
    }
    in.m_notifyIconWidth = modeIcon ? kModeIconW : 0;
    in.m_unreadWidth = unread ? kUnreadW : 0;
    return in;
}

// 全部五种徽章都在的掩码。
const unsigned int kAllMarks = (1u << conv_row_mark_count) - 1u;

// 一个已画出的项：左右边界（右边界为开区间）。
struct Span
{
    int m_left;    // 左边界
    int m_right;   // 右边界
};

// 收集排布里所有画出来的项（徽章、提醒模式图标、未读数），按从左往右的顺序。
std::vector<Span> DrawnSpans(const ConvRowSecondLineInput& in, const ConvRowSecondLineLayout& out)
{
    std::vector<Span> spans;
    for (int i = 0; i < conv_row_mark_count; ++i)
    {
        if (out.m_markLeft[i] != kNotDrawn)
        {
            Span s = { out.m_markLeft[i], out.m_markLeft[i] + in.m_markWidth[i] };
            spans.push_back(s);
        }
    }
    if (out.m_notifyIconLeft != kNotDrawn)
    {
        Span s = { out.m_notifyIconLeft, out.m_notifyIconLeft + in.m_notifyIconWidth };
        spans.push_back(s);
    }
    if (out.m_unreadLeft != kNotDrawn)
    {
        Span s = { out.m_unreadLeft, out.m_unreadLeft + in.m_unreadWidth };
        spans.push_back(s);
    }
    return spans;
}

// 摘要宽度。
int SummaryWidth(const ConvRowSecondLineLayout& out)
{
    return out.m_summaryRight - out.m_summaryLeft;
}

// 省略顺序，与 ConversationRowLayout.cpp 的 kOmitOrder 相同。
const int kExpectedOmitOrder[] =
{
    conv_row_mark_favorite, conv_row_mark_subtitle, conv_row_mark_notify_only, conv_row_mark_star,
};
const int kExpectedOmitCount = sizeof(kExpectedOmitOrder) / sizeof(kExpectedOmitOrder[0]);

} // anonymous namespace

TEST_CASE("CRW1 没有任何徽章：摘要占到时间列左端为止，未读数贴右端", "[convrowlayout]")
{
    const ConvRowSecondLineInput in = MakeInput(kLeft, kRight, kCap, 0u, false, true, true);
    const ConvRowSecondLineLayout out = LayoutConvRowSecondLine(in);
    CHECK(out.m_summaryLeft == kLeft);
    // 未读胶囊在 [296, 312)，摘要右端本可到 292，但被时间列左端 270 截住
    CHECK(out.m_unreadLeft == kRight - kUnreadW);
    CHECK(out.m_summaryRight == kCap);
    CHECK(out.m_notifyIconLeft == kNotDrawn);
    for (int i = 0; i < conv_row_mark_count; ++i)
    {
        CHECK(out.m_markLeft[i] == kNotDrawn);
        CHECK_FALSE(out.m_markOmitted[i]);
    }

    // 什么都没有：摘要直接到时间列左端
    const ConvRowSecondLineLayout bare = LayoutConvRowSecondLine(MakeInput(kLeft, kRight, kCap, 0u, false, false, true));
    CHECK(bare.m_summaryRight == kCap);
    CHECK(bare.m_unreadLeft == kNotDrawn);
}

TEST_CASE("CRW2 全部徽章都在、宽度充裕：顺序固定，间距统一，互不重叠", "[convrowlayout]")
{
    // 加宽到 600，保证一个都不省略
    const int wideRight = 600;
    const ConvRowSecondLineInput in = MakeInput(kLeft, wideRight, wideRight, kAllMarks, true, true, true);
    const ConvRowSecondLineLayout out = LayoutConvRowSecondLine(in);

    for (int i = 0; i < conv_row_mark_count; ++i)
    {
        INFO("mark " << i);
        CHECK(out.m_markLeft[i] != kNotDrawn);
        CHECK_FALSE(out.m_markOmitted[i]);
    }
    const std::vector<Span> spans = DrawnSpans(in, out);
    REQUIRE(spans.size() == (size_t)(conv_row_mark_count + 2));
    // 从左往右：★、收藏、字幕、通话中、仅通知、提醒模式图标、未读数；相邻两项恰好隔一个间距
    for (size_t i = 1; i < spans.size(); ++i)
    {
        INFO("span " << i);
        CHECK(spans[i].m_left - spans[i - 1].m_right == kGap);
    }
    CHECK(spans.back().m_right == wideRight);
    // 摘要右端 = 第一项（星标）左边再留一个间距
    CHECK(out.m_summaryRight == spans.front().m_left - kGap);
}

TEST_CASE("CRW3 宽度逐步收窄：按收藏、字幕、仅通知、星标的顺序省略，其余始终保留", "[convrowlayout]")
{
    // 右边界从宽到窄逐像素扫一遍，省略集合必须始终是省略顺序的前缀，且只增不减
    int lastOmitted = 0;
    for (int right = 600; right >= kLeft; --right)
    {
        const ConvRowSecondLineInput in = MakeInput(kLeft, right, right, kAllMarks, true, true, true);
        const ConvRowSecondLineLayout out = LayoutConvRowSecondLine(in);
        INFO("right = " << right);

        int omitted = 0;
        while (omitted < kExpectedOmitCount && out.m_markOmitted[kExpectedOmitOrder[omitted]])
        {
            ++omitted;
        }
        // 前缀之后不能再有被省略的
        for (int k = omitted; k < kExpectedOmitCount; ++k)
        {
            CHECK_FALSE(out.m_markOmitted[kExpectedOmitOrder[k]]);
        }
        CHECK(omitted >= lastOmitted);
        lastOmitted = omitted;

        // 通话中、提醒模式图标、未读数永远画
        CHECK_FALSE(out.m_markOmitted[conv_row_mark_in_call]);
        CHECK(out.m_markLeft[conv_row_mark_in_call] != kNotDrawn);
        CHECK(out.m_notifyIconLeft != kNotDrawn);
        CHECK(out.m_unreadLeft != kNotDrawn);
    }
    // 扫到最窄时四个可省略的都已省略
    CHECK(lastOmitted == kExpectedOmitCount);
}

TEST_CASE("CRW4 还有可省略的徽章在场时，摘要宽度不小于最小宽度", "[convrowlayout]")
{
    for (int right = 600; right >= kLeft; --right)
    {
        const ConvRowSecondLineInput in = MakeInput(kLeft, right, right, kAllMarks, true, true, true);
        const ConvRowSecondLineLayout out = LayoutConvRowSecondLine(in);
        bool anyOmittableLeft = false;
        for (int k = 0; k < kExpectedOmitCount; ++k)
        {
            if (out.m_markLeft[kExpectedOmitOrder[k]] != kNotDrawn)
            {
                anyOmittableLeft = true;
            }
        }
        INFO("right = " << right << ", summary width = " << SummaryWidth(out));
        if (anyOmittableLeft)
        {
            CHECK(SummaryWidth(out) >= kMinSummary);
        }
        CHECK(SummaryWidth(out) >= 0);
    }

    // 恰好放得下最小宽度时一个都不省略：全部徽章 + 图标 + 未读的总宽，加上最小摘要与一个间距
    const int clusterW = kIconW * 3 + kInCallW + kNotifyOnlyW + kModeIconW + kUnreadW + kGap * 6;
    const int exactRight = kLeft + kMinSummary + kGap + clusterW;
    const ConvRowSecondLineLayout exact =
        LayoutConvRowSecondLine(MakeInput(kLeft, exactRight, exactRight, kAllMarks, true, true, true));
    CHECK(SummaryWidth(exact) == kMinSummary);
    for (int i = 0; i < conv_row_mark_count; ++i)
    {
        CHECK_FALSE(exact.m_markOmitted[i]);
    }
    // 再窄一个像素，先省略的是收藏
    const ConvRowSecondLineLayout tight =
        LayoutConvRowSecondLine(MakeInput(kLeft, exactRight - 1, exactRight - 1, kAllMarks, true, true, true));
    CHECK(tight.m_markOmitted[conv_row_mark_favorite]);
    CHECK_FALSE(tight.m_markOmitted[conv_row_mark_subtitle]);
    CHECK_FALSE(tight.m_markOmitted[conv_row_mark_star]);
}

TEST_CASE("CRW5 摘要为空时不为它保留最小宽度，徽章不被省略", "[convrowlayout]")
{
    const int clusterW = kIconW * 3 + kInCallW + kNotifyOnlyW + kModeIconW + kUnreadW + kGap * 6;
    // 只比徽章区宽 10 像素：有摘要时要省略，没有摘要时不必
    const int right = kLeft + clusterW + 10;
    const ConvRowSecondLineLayout withText =
        LayoutConvRowSecondLine(MakeInput(kLeft, right, right, kAllMarks, true, true, true));
    const ConvRowSecondLineLayout noText =
        LayoutConvRowSecondLine(MakeInput(kLeft, right, right, kAllMarks, true, true, false));
    CHECK(withText.m_markOmitted[conv_row_mark_favorite]);
    for (int i = 0; i < conv_row_mark_count; ++i)
    {
        INFO("mark " << i);
        CHECK_FALSE(noText.m_markOmitted[i]);
        CHECK(noText.m_markLeft[i] != kNotDrawn);
    }
}

TEST_CASE("CRW6 摘要上限只限制摘要，不限制右侧徽章区", "[convrowlayout]")
{
    // 时间列左端在 200：摘要到 200 为止，徽章区照样从右端排起、可以伸到 200 右边
    const int cap = 200;
    const ConvRowSecondLineInput in = MakeInput(kLeft, kRight, cap, (1u << conv_row_mark_star) | (1u << conv_row_mark_in_call),
                                                true, true, true);
    const ConvRowSecondLineLayout out = LayoutConvRowSecondLine(in);
    CHECK(out.m_summaryRight == cap);
    CHECK(out.m_unreadLeft == kRight - kUnreadW);
    CHECK(out.m_markLeft[conv_row_mark_star] > cap);
    // 上限比起点还小时摘要宽度为 0，不为负
    const ConvRowSecondLineLayout squeezed =
        LayoutConvRowSecondLine(MakeInput(kLeft, kRight, kLeft - 10, 0u, false, true, true));
    CHECK(SummaryWidth(squeezed) == 0);
}

TEST_CASE("CRW7 各种组合与宽度下：都在范围内、互不重叠、摘要不压到任何一项", "[convrowlayout]")
{
    for (unsigned int mask = 0; mask <= kAllMarks; ++mask)
    {
        for (int flags = 0; flags < 4; ++flags)
        {
            const bool modeIcon = (flags & 1) != 0;
            const bool unread = (flags & 2) != 0;
            for (int right = kRight; right >= kLeft + 20; right -= 7)
            {
                const ConvRowSecondLineInput in = MakeInput(kLeft, right, right - 30, mask, modeIcon, unread, true);
                const ConvRowSecondLineLayout out = LayoutConvRowSecondLine(in);
                INFO("mask = " << mask << ", flags = " << flags << ", right = " << right);
                const std::vector<Span> spans = DrawnSpans(in, out);
                CHECK(out.m_summaryRight >= out.m_summaryLeft);
                for (size_t i = 0; i < spans.size(); ++i)
                {
                    // 不越过右边界；左边界只在宽度实在不够、必须保留的项挤出去时才可能小于 m_left，
                    // 这种情况下摘要宽度为 0
                    CHECK(spans[i].m_right <= right);
                    if (spans[i].m_left < kLeft)
                    {
                        CHECK(SummaryWidth(out) == 0);
                    }
                    if (i > 0)
                    {
                        CHECK(spans[i].m_left >= spans[i - 1].m_right + kGap);
                    }
                    // 摘要宽度大于 0 时不压到任何一项
                    if (SummaryWidth(out) > 0)
                    {
                        CHECK(out.m_summaryRight + kGap <= spans[i].m_left);
                    }
                }
                // 存在的徽章要么画、要么标为省略，二者必居其一
                for (int m = 0; m < conv_row_mark_count; ++m)
                {
                    const bool present = ((mask >> m) & 1u) != 0;
                    if (present)
                    {
                        CHECK((out.m_markLeft[m] != kNotDrawn) != out.m_markOmitted[m]);
                    }
                    else
                    {
                        CHECK(out.m_markLeft[m] == kNotDrawn);
                        CHECK_FALSE(out.m_markOmitted[m]);
                    }
                }
            }
        }
    }
}

namespace {

/**
 *  在作用域内借一个屏幕 DC 并选入系统默认 GUI 字体，离开时还原并释放。
 */
class ScreenFontDc
{
public:
    ScreenFontDc()
        : m_dc(::GetDC(NULL))
        , m_old(NULL)
    {
        if (m_dc != NULL)
        {
            m_old = ::SelectObject(m_dc, ::GetStockObject(DEFAULT_GUI_FONT));
        }
    }

    ~ScreenFontDc()
    {
        if (m_dc != NULL)
        {
            ::SelectObject(m_dc, m_old);
            ::ReleaseDC(NULL, m_dc);
        }
    }

    HDC Get() const
    {
        return m_dc;
    }

    // 一段文字在当前字体下的宽度，像素。
    int Width(const std::wstring& text) const
    {
        SIZE sz = { 0, 0 };
        ::GetTextExtentPoint32W(m_dc, text.c_str(), (int)text.size(), &sz);
        return sz.cx;
    }

private:
    ScreenFontDc(const ScreenFontDc&);
    ScreenFontDc& operator=(const ScreenFontDc&);

    HDC     m_dc;    // 屏幕 DC，析构时释放
    HGDIOBJ m_old;   // 选入字体前的原字体，析构时还原
};

// 判断 s 是否以 suffix 结尾。
bool EndsWith(const std::wstring& s, const std::wstring& suffix)
{
    return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

} // anonymous namespace

TEST_CASE("CRW8 FitTextWithEllipsis：放得下原样返回，放不下补省略号且不超宽", "[convrowlayout]")
{
    ScreenFontDc dc;
    REQUIRE(dc.Get() != NULL);

    const std::wstring text = L"晚上一起吃饭吗？我在公司楼下等你，记得带伞";
    const int fullW = dc.Width(text);
    REQUIRE(fullW > 0);

    // 放得下：原样返回
    CHECK(FitTextWithEllipsis(dc.Get(), text, fullW) == text);
    CHECK(FitTextWithEllipsis(dc.Get(), text, fullW + 50) == text);

    // 放不下：以 "..." 结尾，宽度不超过上限，且是原文的前缀加省略号
    const int half = fullW / 2;
    const std::wstring cut = FitTextWithEllipsis(dc.Get(), text, half);
    INFO("cut width = " << dc.Width(cut) << ", limit = " << half);
    REQUIRE(EndsWith(cut, L"..."));
    CHECK(dc.Width(cut) <= half);
    const std::wstring head = cut.substr(0, cut.size() - 3);
    CHECK(text.compare(0, head.size(), head) == 0);
    CHECK_FALSE(head.empty());

    // 连省略号都放不下：空串；上限为 0 或原文为空：空串
    CHECK(FitTextWithEllipsis(dc.Get(), text, 1).empty());
    CHECK(FitTextWithEllipsis(dc.Get(), text, 0).empty());
    CHECK(FitTextWithEllipsis(dc.Get(), std::wstring(), 100).empty());

    // 不把代理对从中间截开：在每个可能的宽度上，结果里都没有落单的高位代理
    std::wstring emoji = L"ab";
    emoji += (wchar_t)0xD83D;   // U+1F600 的高位代理
    emoji += (wchar_t)0xDE00;   // U+1F600 的低位代理
    emoji += L"cdefghijklmn";
    const int emojiW = dc.Width(emoji);
    for (int w = 1; w <= emojiW; ++w)
    {
        const std::wstring r = FitTextWithEllipsis(dc.Get(), emoji, w);
        for (size_t i = 0; i < r.size(); ++i)
        {
            if (IS_HIGH_SURROGATE(r[i]))
            {
                INFO("width = " << w);
                REQUIRE(i + 1 < r.size());
                CHECK(IS_LOW_SURROGATE(r[i + 1]));
            }
        }
    }
}
