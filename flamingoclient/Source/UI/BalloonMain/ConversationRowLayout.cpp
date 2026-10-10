/**
 *  ConversationRowLayout.cpp
 *
 *  会话行第二行的排布计算与按宽度截断文字。说明见 ConversationRowLayout.h。
 *
 *  本 .cpp 不 include Flamingo 的 stdafx.h —— 它要被 Flamingo.vcxproj 与 FlamingoUnit.vcxproj 共用。
 *  在 Flamingo.vcxproj 里需标记 PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-10-07
 */

#include <windows.h>

#include <string>

#include "ConversationRowLayout.h"

namespace balloonmain {

namespace {

// 宽度不够时省略徽章的先后顺序：先省收藏，再省字幕，再省「仅通知」，最后省特别关注星标。
// 「通话中」胶囊不在表里，永远保留（用户 2026-10-07 定的优先级）。
const int kOmitOrder[] =
{
    conv_row_mark_favorite,
    conv_row_mark_subtitle,
    conv_row_mark_notify_only,
    conv_row_mark_star,
};
const int kOmitOrderCount = sizeof(kOmitOrder) / sizeof(kOmitOrder[0]);

// 截断时补在末尾的省略号，与 DrawText 的 DT_END_ELLIPSIS 所用的相同。
const wchar_t kEllipsis[] = L"...";

// 右侧一组（保留下来的徽章 + 提醒模式图标 + 未读数）连同其间的间距一共多宽，单位像素；一项都没有时为 0。
//   in：     输入。
//   kept：   各徽章是否保留，下标见 ConvRowMark。
int ClusterWidth(const ConvRowSecondLineInput& in, const bool kept[conv_row_mark_count])
{
    int width = 0;
    int items = 0;
    for (int i = 0; i < conv_row_mark_count; ++i)
    {
        if (kept[i])
        {
            width += in.m_markWidth[i];
            ++items;
        }
    }
    if (in.m_notifyIconWidth > 0)
    {
        width += in.m_notifyIconWidth;
        ++items;
    }
    if (in.m_unreadWidth > 0)
    {
        width += in.m_unreadWidth;
        ++items;
    }
    if (items > 1)
    {
        width += in.m_gap * (items - 1);
    }
    return width;
}

// 给定右侧一组的宽度，摘要矩形的右边界：徽章区左端再留一个间距，且不越过 m_summaryCap，也不小于 m_left。
int SummaryRightFor(const ConvRowSecondLineInput& in, int clusterWidth)
{
    int right = in.m_right;
    if (clusterWidth > 0)
    {
        right = in.m_right - clusterWidth - in.m_gap;
    }
    if (right > in.m_summaryCap)
    {
        right = in.m_summaryCap;
    }
    if (right < in.m_left)
    {
        right = in.m_left;
    }
    return right;
}

} // anonymous namespace

ConvRowSecondLineInput::ConvRowSecondLineInput()
    : m_left(0)
    , m_right(0)
    , m_summaryCap(0)
    , m_hasSummary(false)
    , m_minSummaryWidth(0)
    , m_gap(0)
    , m_notifyIconWidth(0)
    , m_unreadWidth(0)
{
    for (int i = 0; i < conv_row_mark_count; ++i)
    {
        m_markWidth[i] = 0;
    }
}

ConvRowSecondLineLayout::ConvRowSecondLineLayout()
    : m_summaryLeft(0)
    , m_summaryRight(0)
    , m_notifyIconLeft(kConvRowNotDrawn)
    , m_unreadLeft(kConvRowNotDrawn)
{
    for (int i = 0; i < conv_row_mark_count; ++i)
    {
        m_markLeft[i] = kConvRowNotDrawn;
        m_markOmitted[i] = false;
    }
}

ConvRowSecondLineLayout LayoutConvRowSecondLine(const ConvRowSecondLineInput& in)
{
    ConvRowSecondLineLayout out;

    // 先假定存在的徽章全部保留
    bool kept[conv_row_mark_count];
    for (int i = 0; i < conv_row_mark_count; ++i)
    {
        kept[i] = (in.m_markWidth[i] > 0);
    }

    // 摘要有文字时要保住最小宽度：放不下就按省略顺序逐个去掉徽章，每去掉一个重算一次
    const int needSummary = in.m_hasSummary ? in.m_minSummaryWidth : 0;
    int omitIndex = 0;
    while (SummaryRightFor(in, ClusterWidth(in, kept)) - in.m_left < needSummary
           && omitIndex < kOmitOrderCount)
    {
        const int mark = kOmitOrder[omitIndex];
        ++omitIndex;
        if (kept[mark])
        {
            kept[mark] = false;
            out.m_markOmitted[mark] = true;
        }
    }

    out.m_summaryLeft  = in.m_left;
    out.m_summaryRight = SummaryRightFor(in, ClusterWidth(in, kept));

    // 右侧一组从右往左摆：未读数贴右端，再往左是提醒模式图标，再往左是保留下来的徽章（逆序）
    int cursor = in.m_right;   // 下一项的右边界
    if (in.m_unreadWidth > 0)
    {
        out.m_unreadLeft = cursor - in.m_unreadWidth;
        cursor = out.m_unreadLeft - in.m_gap;
    }
    if (in.m_notifyIconWidth > 0)
    {
        out.m_notifyIconLeft = cursor - in.m_notifyIconWidth;
        cursor = out.m_notifyIconLeft - in.m_gap;
    }
    for (int i = conv_row_mark_count - 1; i >= 0; --i)
    {
        if (!kept[i])
        {
            continue;
        }
        out.m_markLeft[i] = cursor - in.m_markWidth[i];
        cursor = out.m_markLeft[i] - in.m_gap;
    }
    return out;
}

std::wstring FitTextWithEllipsis(HDC hdc, const std::wstring& text, int maxWidth)
{
    if (text.empty() || maxWidth <= 0)
    {
        return std::wstring();
    }

    SIZE full = { 0, 0 };
    ::GetTextExtentPoint32W(hdc, text.c_str(), (int)text.size(), &full);
    if (full.cx <= maxWidth)
    {
        return text;
    }

    SIZE ellipsis = { 0, 0 };
    const int ellipsisLen = (int)(sizeof(kEllipsis) / sizeof(kEllipsis[0])) - 1;
    ::GetTextExtentPoint32W(hdc, kEllipsis, ellipsisLen, &ellipsis);
    const int avail = maxWidth - ellipsis.cx;
    if (avail <= 0)
    {
        return std::wstring();
    }

    // 求出在 avail 像素内放得下的字符个数；GetTextExtentExPoint 按逐字累加的宽度判断，与 DrawText 一致
    int fit = 0;
    SIZE dummy = { 0, 0 };
    if (!::GetTextExtentExPointW(hdc, text.c_str(), (int)text.size(), avail, &fit, NULL, &dummy))
    {
        return std::wstring();
    }
    // 不把代理对从中间截开：最后一个保留的字符若是高位代理，连它一起去掉
    if (fit > 0 && IS_HIGH_SURROGATE(text[fit - 1]))
    {
        --fit;
    }
    if (fit <= 0)
    {
        return std::wstring(kEllipsis);
    }
    return text.substr(0, (size_t)fit) + kEllipsis;
}

} // namespace balloonmain
