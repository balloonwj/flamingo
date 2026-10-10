/**
 *  ConversationRowLayout.h
 *
 *  主面板「最近会话列表」一行第二行（摘要那一行）的排布计算，以及按像素宽度截断文字并补省略号。
 *  两者都不依赖任何控件，供 MainPanelTreeView 绘制会话行时调用，也供单元测试直接断言。
 *
 *  第二行从左往右是：摘要 | ★ 特别关注、收藏书签、字幕徽章、「通话中」胶囊、「仅通知」标签 |
 *  提醒模式图标 | 未读数胶囊或小红点。右侧这一组整体贴行右端排列，摘要占剩下的宽度并保留一个
 *  最小宽度；宽度不够时按「收藏 → 字幕 → 仅通知 → ★」的顺序依次省略徽章，「通话中」胶囊、
 *  提醒模式图标与未读数始终保留（docs/消息提醒模式与消息盒子方案.md 第 13 节）。
 *
 *  典型用法：
 *      balloonmain::ConvRowSecondLineInput in;
 *      in.m_left  = 摘要起点;
 *      in.m_right = 行右端减去右内边距;
 *      in.m_summaryCap = 摘要不能越过的右边界;
 *      in.m_markWidth[balloonmain::conv_row_mark_star] = 12;
 *      ...
 *      const balloonmain::ConvRowSecondLineLayout out = balloonmain::LayoutConvRowSecondLine(in);
 *
 *  balloonwj@qq.com   2026-10-07
 */

#pragma once

#include <windows.h>

#include <climits>
#include <string>

namespace balloonmain {

// 排布结果里不画的项（不存在或被省略）的左边界取值。取 INT_MIN 而不是 -1：宽度极窄时必须保留的项可能被挤到
// 摘要起点左边，算出的左边界本身就可能是 -1 这样的负数，不能与"不画"混淆。
const int kConvRowNotDrawn = INT_MIN;

/**
 *  会话行第二行右侧徽章区里的徽章种类。取值即从左往右的排列顺序，也是数组下标。
 */
enum ConvRowMark
{
    //「特别关注」金色星标：这个会话的提醒模式是特别关注时出现
    conv_row_mark_star = 0,
    //「我的收藏」玫红书签：这个联系人或群被收藏时出现
    conv_row_mark_favorite = 1,
    //「字幕模式」徽章：这个会话的桌面字幕开着时出现
    conv_row_mark_subtitle = 2,
    //「通话中」绿色胶囊：本人正在与这个会话通话时出现，宽度不够时也不省略
    conv_row_mark_in_call = 3,
    //「仅通知」灰色标签：群开启了仅通知模式时出现（只有群会话会有）
    conv_row_mark_notify_only = 4,
    // 徽章种类数，不是徽章，只用作数组长度与循环上界
    conv_row_mark_count = 5
};

/**
 *  LayoutConvRowSecondLine 的输入：一行第二行的可用范围与各项内容的宽度。坐标与宽度的单位都是
 *  像素，坐标为控件所在宿主的客户区坐标（与绘制同一坐标系）。宽度为 0 表示这一项不存在。
 */
struct ConvRowSecondLineInput
{
    int  m_left;                                // 摘要的起点（与第一行名字的起点相同）
    int  m_right;                               // 第二行内容的右边界（行右端减去右内边距），未读数贴着它
    int  m_summaryCap;                          // 摘要文字不能越过的右边界（树控件给左侧文字留的列宽右端），
                                                // 只限制摘要，不限制右侧徽章区
    bool m_hasSummary;                          // 摘要是否有文字；没有文字时不为它保留最小宽度
    int  m_minSummaryWidth;                     // 有文字时摘要至少保留的宽度；徽章区要挤占它时先省略徽章
    int  m_gap;                                 // 统一间距：摘要与徽章区之间、相邻两项之间都是这么宽
    int  m_markWidth[conv_row_mark_count];      // 各徽章的宽度，下标见 ConvRowMark；0 表示这一行没有该徽章
    int  m_notifyIconWidth;                     // 提醒模式图标的宽度；0 表示没有（允许提醒、特别关注）
    int  m_unreadWidth;                         // 未读数胶囊或小红点的宽度；0 表示没有

    ConvRowSecondLineInput();
};

/**
 *  LayoutConvRowSecondLine 的输出。不画的项（不存在或被省略）左边界为 kConvRowNotDrawn。
 */
struct ConvRowSecondLineLayout
{
    int  m_summaryLeft;                         // 摘要矩形左边界（等于输入的 m_left）
    int  m_summaryRight;                        // 摘要矩形右边界；不小于 m_summaryLeft，宽度可能为 0
    int  m_markLeft[conv_row_mark_count];       // 各徽章的左边界；kConvRowNotDrawn 表示不画
    bool m_markOmitted[conv_row_mark_count];    // 该徽章存在、但因宽度不够被省略
    int  m_notifyIconLeft;                      // 提醒模式图标的左边界；kConvRowNotDrawn 表示没有
    int  m_unreadLeft;                          // 未读数胶囊或小红点的左边界；kConvRowNotDrawn 表示没有

    ConvRowSecondLineLayout();
};

/**
 *  计算会话行第二行的排布。
 *
 *    in：可用范围与各项宽度，见 ConvRowSecondLineInput。
 *
 *  规则：
 *    · 右侧一组贴 m_right 从右往左排：未读数、提醒模式图标、「仅通知」、「通话中」、字幕、收藏、★，
 *      相邻两项之间隔 m_gap；从左往右读即为 ★、收藏、字幕、通话中、仅通知、提醒模式图标、未读数。
 *    · 摘要占 [m_left, min(徽章区左端 - m_gap, m_summaryCap)]；徽章区为空时右端取 min(m_right, m_summaryCap)。
 *    · 摘要有文字、且宽度小于 m_minSummaryWidth 时，按「收藏 → 字幕 → 仅通知 → ★」的顺序逐个省略徽章，
 *      直到摘要宽度够了或可省略的都已省略；「通话中」、提醒模式图标、未读数从不省略。
 *    · 省略后剩余的徽章仍保持原来的相对顺序，间距不变，不留空位。
 *
 *  返回：排布结果。任何两项的矩形都不重叠，摘要也不与任何一项重叠。
 */
ConvRowSecondLineLayout LayoutConvRowSecondLine(const ConvRowSecondLineInput& in);

/**
 *  把一段文字截断到不超过 maxWidth 像素宽：放得下时原样返回；放不下时截掉末尾并补上 "..."
 *  （与 DrawText 的 DT_END_ELLIPSIS 用同一个省略号，界面里各处的省略写法一致）。按 hdc 当前选入的
 *  字体测量，调用方须先选好字体。不会把一个 UTF-16 代理对从中间截开。
 *
 *    hdc：     测量用的设备上下文，只读其当前字体，不在上面绘制。
 *    text：    原文。
 *    maxWidth：允许的最大宽度，像素。
 *
 *  返回：截断后的文字；maxWidth 连省略号都放不下时返回空串。
 */
std::wstring FitTextWithEllipsis(HDC hdc, const std::wstring& text, int maxWidth);

} // namespace balloonmain
