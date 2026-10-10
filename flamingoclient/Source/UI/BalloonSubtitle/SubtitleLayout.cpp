/**
 *  SubtitleLayout.cpp
 *
 *  字幕窗口几何与放置的实现，说明见 SubtitleLayout.h。
 *
 *  balloonwj@qq.com   2026-10-05
 */

#include "stdafx.h"

#include "SubtitleLayout.h"

#include "SubtitleState.h"                          // SubtitlePart：命中测试的结果取值
#include "../../Session/WindowPlacementPrefs.h"   // kMinVisibleWidthPx / kMinVisibleHeightPx：判断保存的位置还看不看得见

namespace subtitle {

namespace {

//96 DPI：设计尺寸的基准
const int kBaseDpi = 96;
//百分比的分母
const int kPercent = 100;

//96 DPI 下的尺寸换算成 dpi 下的物理像素
int ScaleDip(int dip, int dpi)
{
    return ::MulDiv(dip, dpi, kBaseDpi);
}

int ClampInt(int value, int lo, int hi)
{
    if (value < lo)
        return lo;
    if (value > hi)
        return hi;
    return value;
}

int MinInt(int a, int b)
{
    return (a < b) ? a : b;
}

int MaxInt(int a, int b)
{
    return (a > b) ? a : b;
}

RECT MakeRect(int left, int top, int width, int height)
{
    RECT rc = { left, top, left + width, top + height };
    return rc;
}

//两个矩形重叠部分的宽与高；不重叠时都为 0
void OverlapSize(const RECT& a, const RECT& b, int& w, int& h)
{
    w = MaxInt(0, MinInt(a.right, b.right) - MaxInt(a.left, b.left));
    h = MaxInt(0, MinInt(a.bottom, b.bottom) - MaxInt(a.top, b.top));
}

//把矩形平移到整个落在 work 里；比 work 大时先缩到 work 的大小
RECT ClampToWork(const RECT& rc, const RECT& work)
{
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;
    const int workW = work.right - work.left;
    const int workH = work.bottom - work.top;
    if (w > workW)
        w = workW;
    if (h > workH)
        h = workH;
    int left = rc.left;
    int top = rc.top;
    if (left + w > work.right)
        left = work.right - w;
    if (left < work.left)
        left = work.left;
    if (top + h > work.bottom)
        top = work.bottom - h;
    if (top < work.top)
        top = work.top;
    return MakeRect(left, top, w, h);
}

//矩形是否与 others 里的任何一个相交
bool IntersectsAny(const RECT& rc, const std::vector<RECT>& others, size_t& hit)
{
    for (size_t i = 0; i < others.size(); ++i)
    {
        RECT tmp;
        if (::IntersectRect(&tmp, &rc, &others[i]))
        {
            hit = i;
            return true;
        }
    }
    return false;
}

} // anonymous namespace

SubtitleMetrics ComputeSubtitleMetrics(int fontSizeDip, int dpi)
{
    const int d = (dpi > 0) ? dpi : kBaseDpi;
    SubtitleMetrics m;
    m.m_dpi = d;
    m.m_fontPx = ScaleDip(fontSizeDip, d);
    m.m_lineHeight = ::MulDiv(m.m_fontPx, kLineHeightPercent, kPercent);
    m.m_toolbarHeight = ScaleDip(kToolbarHeightDip, d);
    m.m_buttonSize = ScaleDip(kButtonSizeDip, d);
    m.m_buttonGap = ScaleDip(kButtonGapDip, d);
    m.m_titleGap = ScaleDip(kTitleGapDip, d);
    m.m_titleFontPx = ScaleDip(kTitleFontDip, d);
    m.m_paddingX = ScaleDip(kPaddingXDip, d);
    m.m_textTopGap = ScaleDip(kTextTopGapDip, d);
    m.m_paddingBottom = ScaleDip(kPaddingBottomDip, d);
    m.m_edgeGrip = ScaleDip(kEdgeGripDip, d);
    m.m_minWidth = ScaleDip(kMinWidthDip, d);
    m.m_cornerRadius = ScaleDip(kCornerRadiusDip, d);
    return m;
}

int SubtitleWindowHeight(const SubtitleMetrics& m, int maxLines)
{
    const int lines = ClampInt(maxLines, kMinMaxLines, kMaxMaxLines);
    return m.m_toolbarHeight + m.m_textTopGap + lines * m.m_lineHeight + m.m_paddingBottom;
}

RECT SubtitleTextRect(const SubtitleMetrics& m, int width, int maxLines)
{
    const int lines = ClampInt(maxLines, kMinMaxLines, kMaxMaxLines);
    RECT rc;
    rc.left = m.m_paddingX;
    rc.right = width - m.m_paddingX;
    rc.top = m.m_toolbarHeight + m.m_textTopGap;
    rc.bottom = rc.top + lines * m.m_lineHeight;
    return rc;
}

int DefaultSubtitleWidth(const RECT& workArea, const SubtitleMetrics& m)
{
    const int workW = workArea.right - workArea.left;
    const int maxW = MinInt(ScaleDip(kDefaultMaxWidthDip, m.m_dpi > 0 ? m.m_dpi : kBaseDpi), workW);
    const int minW = MinInt(m.m_minWidth, workW);
    return ClampInt(workW * kDefaultWidthPercent / kPercent, minW, maxW);
}

SubtitleToolbar ComputeSubtitleToolbar(const SubtitleMetrics& m, int width, int titleWidth)
{
    const int titleW = (titleWidth > 0) ? MinInt(titleWidth, width * kTitleMaxWidthPercent / kPercent) : 0;
    const int buttonsW = kToolbarButtonCount * m.m_buttonSize + (kToolbarButtonCount - 1) * m.m_buttonGap;
    const int groupW = buttonsW + ((titleW > 0) ? titleW + m.m_titleGap : 0);
    const int top = (m.m_toolbarHeight - m.m_buttonSize) / 2;
    int x = (width - groupW) / 2;

    SubtitleToolbar tb;
    tb.m_title = MakeRect(x, top, titleW, m.m_buttonSize);
    if (titleW > 0)
        x += titleW + m.m_titleGap;
    RECT* buttons[kToolbarButtonCount] = { &tb.m_settings, &tb.m_lock, &tb.m_back, &tb.m_close };
    for (int i = 0; i < kToolbarButtonCount; ++i)
    {
        *buttons[i] = MakeRect(x, top, m.m_buttonSize, m.m_buttonSize);
        x += m.m_buttonSize + m.m_buttonGap;
    }
    return tb;
}

int HitTestSubtitle(const SubtitleMetrics& m, const SubtitleToolbar& tb, int width, int height, POINT pt,
                    bool locked, bool buttonsActive, bool lockButtonVisible)
{
    if (pt.x < 0 || pt.y < 0 || pt.x >= width || pt.y >= height)
        return kPartNone;
    //锁定时只有浮出的小锁按钮可以命中，其余都算文字处（窗口此时穿透，不会真的收到点击）
    if (locked)
    {
        if (lockButtonVisible && ::PtInRect(&tb.m_lock, pt))
            return kPartLock;
        return kPartBody;
    }
    if (buttonsActive)
    {
        if (::PtInRect(&tb.m_settings, pt))
            return kPartSettings;
        if (::PtInRect(&tb.m_lock, pt))
            return kPartLock;
        if (::PtInRect(&tb.m_back, pt))
            return kPartBack;
        if (::PtInRect(&tb.m_close, pt))
            return kPartClose;
    }
    if (pt.x < m.m_edgeGrip)
        return kPartLeftEdge;
    if (pt.x >= width - m.m_edgeGrip)
        return kPartRightEdge;
    return kPartBody;
}

RECT ResolveSubtitleRect(const SubtitlePlaceInput& in)
{
    //保存过位置：与某个工作区重叠得足够多（那台显示器还在）时用它，平移到整个落在重叠最多的工作区里
    if (in.m_hasSaved && in.m_saved.m_width > 0)
    {
        const int w = MaxInt(in.m_saved.m_width, in.m_minWidth);
        const RECT rc = MakeRect(in.m_saved.m_left, in.m_saved.m_top, w, in.m_height);
        int bestArea = 0;
        size_t best = in.m_workAreas.size();
        for (size_t i = 0; i < in.m_workAreas.size(); ++i)
        {
            int ow = 0;
            int oh = 0;
            OverlapSize(rc, in.m_workAreas[i], ow, oh);
            if (ow < winplace::kMinVisibleWidthPx || oh < winplace::kMinVisibleHeightPx)
                continue;
            if (ow * oh > bestArea)
            {
                bestArea = ow * oh;
                best = i;
            }
        }
        if (best < in.m_workAreas.size())
            return ClampToWork(rc, in.m_workAreas[best]);
    }

    //默认位置：主显示器工作区底部居中，离底边留出工作区高度的 kDefaultBottomMarginPercent
    const RECT& work = in.m_primaryWork;
    const int workW = work.right - work.left;
    const int workH = work.bottom - work.top;
    const int w = MinInt(in.m_defaultWidth, workW);
    const int bottom = work.bottom - workH * kDefaultBottomMarginPercent / kPercent;
    RECT rc = MakeRect(work.left + (workW - w) / 2, bottom - in.m_height, w, in.m_height);

    //与其它字幕重叠时挪到它的上方；每挪一次最多再与另一条重叠，挪的次数不超过字幕条数
    for (size_t round = 0; round <= in.m_occupied.size(); ++round)
    {
        size_t hit = 0;
        if (!IntersectsAny(rc, in.m_occupied, hit))
            break;
        const int newBottom = in.m_occupied[hit].top - in.m_stackGap;
        rc = MakeRect(rc.left, newBottom - in.m_height, w, in.m_height);
    }
    //挪出了工作区顶部时放回工作区里（宁可重叠也不跑到屏幕外）
    if (rc.top < work.top)
        rc = MakeRect(rc.left, work.top, w, in.m_height);
    return rc;
}

RECT ClampSubtitleRect(const RECT& rc, const std::vector<RECT>& workAreas, const RECT& fallback)
{
    int bestArea = 0;
    size_t best = workAreas.size();
    for (size_t i = 0; i < workAreas.size(); ++i)
    {
        int ow = 0;
        int oh = 0;
        OverlapSize(rc, workAreas[i], ow, oh);
        if (ow * oh > bestArea)
        {
            bestArea = ow * oh;
            best = i;
        }
    }
    return ClampToWork(rc, (best < workAreas.size()) ? workAreas[best] : fallback);
}

} // namespace subtitle
