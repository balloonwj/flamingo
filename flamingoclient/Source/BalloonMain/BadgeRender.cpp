/**
 *  BadgeRender.cpp
 *
 *  会话列表未读数字红点的纯几何 / 文案计算实现。仅做坐标与字符串运算，
 *  不依赖 balloonui / GDI / 窗口 —— 故与 FlamingoUnit 测试工程共用、
 *  不 include Flamingo 的 stdafx.h；在 Flamingo.vcxproj 里需标记
 *  PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-06-04
 */

#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#define WINVER          0x0601      // 目标系统下限 = Windows 7
#define _WIN32_WINNT    0x0601

#include "BadgeRender.h"

namespace balloonmain {

namespace {

// 角标上限：未读超过此值显示 "99+"，不在小红点里塞 3 位以上数字。
const int kBadgeMaxShown = 99;

// 置顶强调竖条高度百分比的下限 / 上限。低于下限等于没画；高于 100 会比行还高、
// 溢到相邻行上去。
const int kAccentMinHeightPercent = 1;
const int kAccentMaxHeightPercent = 100;

} // anonymous namespace

CString FormatBadgeText(int count)
{
    if (count <= 0)
    {
        return CString();
    }

    CString text;
    if (count > kBadgeMaxShown)
    {
        text = _T("99+");
    }
    else
    {
        text.Format(_T("%d"), count);
    }
    return text;
}

int ComputeBadgeWidth(int charCount, int diameter, int extraPerChar)
{
    // 单字符（或异常的 0 字符）即正圆，宽 = 直径；多字符按超出位数横向加宽。
    int extraChars = (charCount > 1) ? (charCount - 1) : 0;
    return diameter + extraChars * extraPerChar;
}

RECT ComputeBadgeRect(int rightEdge, int badgeWidth, int diameter, int centerY)
{
    RECT rc;
    rc.right  = rightEdge;
    rc.left   = rightEdge - badgeWidth;
    rc.top    = centerY - diameter / 2;
    rc.bottom = rc.top + diameter;
    return rc;
}

RECT ComputePinnedAccentRect(const RECT& rowRect, int barWidth, int heightPercent)
{
    // 兜底空矩形：左右、上下都取行左上角，调用方用 right <= left 判空。
    RECT rc;
    rc.left   = rowRect.left;
    rc.right  = rowRect.left;
    rc.top    = rowRect.top;
    rc.bottom = rowRect.top;

    const int rowHeight = rowRect.bottom - rowRect.top;
    if (barWidth <= 0 || rowHeight <= 0)
    {
        return rc;
    }

    // 百分比截取到 1..100：0 或负数会算出零高竖条（等于没画），超过 100 则比行还高、
    // 会溢到相邻行上去。
    int percent = heightPercent;
    if (percent < kAccentMinHeightPercent)
    {
        percent = kAccentMinHeightPercent;
    }
    if (percent > kAccentMaxHeightPercent)
    {
        percent = kAccentMaxHeightPercent;
    }

    int barHeight = rowHeight * percent / 100;
    if (barHeight < 1)
    {
        barHeight = 1;   // 行很矮时至少留 1 像素，避免算成看不见的零高矩形
    }

    // 垂直居中：上下余量对半分，奇数余量偏上 1 像素（与角标同款取整口径）。
    const int top = rowRect.top + (rowHeight - barHeight) / 2;
    rc.left   = rowRect.left;
    rc.right  = rowRect.left + barWidth;
    rc.top    = top;
    rc.bottom = top + barHeight;
    return rc;
}

} // namespace balloonmain
