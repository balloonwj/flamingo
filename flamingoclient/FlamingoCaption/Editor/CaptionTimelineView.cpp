/**
 *  字幕编辑器的时间轴，见 CaptionTimelineView.h。
 *  balloonwj@qq.com   2026-10-06
 */
#include "stdafx.h"

#include "CaptionTimelineView.h"

#include <stdlib.h>

#include <memory>

#include "CaptionEditorModel.h"   //FormatEditTime：刻度文字
#include "../../Source/Common/I18n/I18n.h"   //UiFontName

namespace caption {

namespace {

//每秒的毫秒数（像素与时间换算）
const double kMsPerSecF = 1000.0;
//刻度行高（像素）；字幕块行与上下边的间距
const int kRulerH = 22;
const int kTrackPad = 6;
//主刻度、次刻度的长度
const int kMajorTickH = 8;
const int kMinorTickH = 4;
//刻度文字与刻度线的间距、字号
const int kTickTextGap = 3;
const int kTickFontPx = 11;
//字幕块文字字号与左右内边距
const int kCueFontPx = 12;
const int kCueTextPad = 4;
//边缘判定宽度、开始拖动的移动阈值（像素）
const int kEdgePx = 6;
const int kDragThresholdPx = 3;
//播放线宽、选中边框宽
const int kPlayheadW = 2;
const int kSelectedBorderW = 2;
//滚轮一格滚动的像素
const int kWheelScrollPx = 80;
//EnsureVisible 时目标放在可见范围的几分之一处
const int kEnsureFractionDen = 4;
//颜色
const COLORREF kBg = RGB(246, 247, 249);
const COLORREF kRulerBg = RGB(236, 238, 242);
const COLORREF kTickColor = RGB(150, 154, 162);
const COLORREF kTickText = RGB(100, 104, 112);
const COLORREF kCueFill = RGB(120, 160, 230);
const COLORREF kCueEditedFill = RGB(236, 170, 90);
const COLORREF kCueText = RGB(255, 255, 255);
const COLORREF kCueSelectedBorder = RGB(20, 50, 120);
const COLORREF kPlayhead = RGB(230, 60, 60);
const COLORREF kEndShade = RGB(225, 227, 232);

//WHEEL_DELTA 的整数倍换成格数
int WheelSteps(short zDelta)
{
    return zDelta / WHEEL_DELTA != 0 ? zDelta / WHEEL_DELTA : (zDelta > 0 ? 1 : -1);
}

HFONT MakeFont(int px)
{
    return ::CreateFontW(-px, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                         CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, i18n::UiFontName());
}

} // namespace

CaptionTimelineView::CaptionTimelineView()
    : m_durationMs(0)
    , m_positionMs(0)
    , m_selected(-1)
    , m_scrollMs(0)
    , m_pxPerSec(kTimelineDefaultPxPerSec)
    , m_pressed(false)
    , m_dragging(false)
    , m_dragKind(timeline_hit_none)
    , m_dragIndex(-1)
    , m_pressX(0)
    , m_dragStart(0)
    , m_dragEnd(0)
    , m_hasResult(false)
    , m_resultIndex(-1)
    , m_resultStart(0)
    , m_resultEnd(0)
    , m_scrollBar(NULL)
{
    //横向滚动条：浮在底部，内容超过一屏时才显示；滚轮一格与本控件自己的滚轮步长相同
    std::unique_ptr<balloonwjui::DuiScrollBar> bar(new balloonwjui::DuiScrollBar(true));
    bar->SetCtrlId(kScrollBarCtrlId);
    bar->SetLineSize(kWheelScrollPx);
    bar->SetOnScroll(&CaptionTimelineView::OnScrollBar, this);
    bar->SetVisible(false);
    m_scrollBar = bar.get();
    AddChild(std::move(bar));
}

void CaptionTimelineView::Layout(const RECT& rcAvail)
{
    //滚动条浮在内容之上、贴着下缘，内容仍按整个矩形排版（UI 约定第 1 条）
    RECT band = { rcAvail.left, rcAvail.bottom - balloonwjui::DuiScrollBar::kOverlayBandPx, rcAvail.right,
                  rcAvail.bottom };
    if (band.top < rcAvail.top)
        band.top = rcAvail.top;
    m_scrollBar->SetRect(band);
    syncScrollBar(false);
}

void CaptionTimelineView::syncScrollBar(bool show)
{
    const TimelineScrollBarState s = TimelineScrollBar(view(), m_durationMs);
    if (s.m_maxPos <= 0)
    {
        //内容放得下：不显示，也不拦截鼠标
        m_scrollBar->SetRange(0, 0);
        m_scrollBar->SetVisible(false);
        return;
    }
    m_scrollBar->SetVisible(true);
    m_scrollBar->SetRange(0, s.m_maxPos);
    m_scrollBar->SetPage(s.m_page);
    m_scrollBar->SetPos(s.m_pos, false);
    if (show)
        m_scrollBar->TriggerShow();
}

void CaptionTimelineView::OnScrollBar(void* user, int pos)
{
    CaptionTimelineView* self = static_cast<CaptionTimelineView*>(user);
    if (self == NULL)
        return;
    self->m_scrollMs = TimelineScrollFromBar(self->view(), pos, self->m_durationMs);
    self->Invalidate();
}

TimelineView CaptionTimelineView::view() const
{
    TimelineView v;
    v.m_scrollMs = m_scrollMs;
    v.m_pxPerSec = m_pxPerSec;
    const RECT rc = GetRect();
    v.m_width = rc.right - rc.left;
    v.m_trackTop = kRulerH + kTrackPad;
    v.m_trackBottom = (rc.bottom - rc.top) - kTrackPad;
    return v;
}

POINT CaptionTimelineView::local(POINT pt) const
{
    const RECT rc = GetRect();
    POINT p = { pt.x - rc.left, pt.y - rc.top };
    return p;
}

void CaptionTimelineView::SetCues(const std::vector<CaptionCue>& cues)
{
    m_cues = cues;
    if (m_selected >= static_cast<int>(m_cues.size()))
        m_selected = -1;
    Invalidate();
}

void CaptionTimelineView::SetDuration(int64_t durationMs)
{
    m_durationMs = durationMs;
    m_scrollMs = TimelineClampScroll(view(), m_scrollMs, m_durationMs);
    syncScrollBar(false);
    Invalidate();
}

void CaptionTimelineView::SetPosition(int64_t ms, bool followPage)
{
    if (ms == m_positionMs)
        return;
    m_positionMs = ms;
    if (followPage)
    {
        const TimelineView v = view();
        const int x = TimelineMsToX(v, ms);
        //走出右边：翻一页，让播放位置回到左边缘
        if (x >= v.m_width || x < 0)
        {
            m_scrollMs = TimelineClampScroll(v, ms, m_durationMs);
            syncScrollBar(false);
        }
    }
    Invalidate();
}

void CaptionTimelineView::SetSelected(int index)
{
    if (index == m_selected)
        return;
    m_selected = index;
    Invalidate();
}

void CaptionTimelineView::EnsureVisible(int64_t ms)
{
    const TimelineView v = view();
    const int x = TimelineMsToX(v, ms);
    if (x >= 0 && x < v.m_width)
        return;
    const int64_t visibleMs = (v.m_pxPerSec > 0) ? static_cast<int64_t>(v.m_width * kMsPerSecF / v.m_pxPerSec) : 0;
    m_scrollMs = TimelineClampScroll(v, ms - visibleMs / kEnsureFractionDen, m_durationMs);
    syncScrollBar(false);
    Invalidate();
}

bool CaptionTimelineView::DragResult(int& index, int64_t& startMs, int64_t& endMs) const
{
    if (!m_hasResult)
        return false;
    index = m_resultIndex;
    startMs = m_resultStart;
    endMs = m_resultEnd;
    return true;
}

void CaptionTimelineView::OnPaint(HDC hdc, const RECT& rcDirty)
{
    const RECT rc = GetRect();
    const TimelineView v = view();
    //宿主不替控件裁剪：内容只画在本控件矩形内，否则右边那块字幕会画到旁边的字幕列表上（2026-10-06）
    const int saved = ::SaveDC(hdc);
    ::IntersectClipRect(hdc, rc.left, rc.top, rc.right, rc.bottom);
    HBRUSH bg = ::CreateSolidBrush(kBg);
    ::FillRect(hdc, &rc, bg);
    ::DeleteObject(bg);
    //录像结束之后的部分画浅灰
    if (m_durationMs > 0)
    {
        const int endX = TimelineMsToX(v, m_durationMs);
        if (endX < v.m_width)
        {
            RECT shade = { rc.left + (endX > 0 ? endX : 0), rc.top + kRulerH, rc.right, rc.bottom };
            HBRUSH b = ::CreateSolidBrush(kEndShade);
            ::FillRect(hdc, &shade, b);
            ::DeleteObject(b);
        }
    }
    //刻度行
    RECT ruler = { rc.left, rc.top, rc.right, rc.top + kRulerH };
    HBRUSH rb = ::CreateSolidBrush(kRulerBg);
    ::FillRect(hdc, &ruler, rb);
    ::DeleteObject(rb);
    ::SetBkMode(hdc, TRANSPARENT);
    HFONT tickFont = MakeFont(kTickFontPx);
    HGDIOBJ oldFont = ::SelectObject(hdc, tickFont);
    HPEN tickPen = ::CreatePen(PS_SOLID, 1, kTickColor);
    HGDIOBJ oldPen = ::SelectObject(hdc, tickPen);
    ::SetTextColor(hdc, kTickText);
    const std::vector<TimelineTick> ticks = TimelineTicks(v, m_durationMs);
    for (size_t i = 0; i < ticks.size(); ++i)
    {
        const int x = rc.left + ticks[i].m_x;
        const int len = ticks[i].m_major ? kMajorTickH : kMinorTickH;
        ::MoveToEx(hdc, x, rc.top + kRulerH - len, NULL);
        ::LineTo(hdc, x, rc.top + kRulerH);
        if (ticks[i].m_major)
        {
            const std::wstring label = FormatEditTime(ticks[i].m_ms);
            RECT tr = { x + kTickTextGap, rc.top, x + kTimelineMinTickPx, rc.top + kRulerH - kMinorTickH };
            ::DrawTextW(hdc, label.c_str(), -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }
    }
    ::SelectObject(hdc, oldPen);
    ::DeleteObject(tickPen);
    //字幕块
    HFONT cueFont = MakeFont(kCueFontPx);
    ::SelectObject(hdc, cueFont);
    ::SetTextColor(hdc, kCueText);
    for (size_t i = 0; i < m_cues.size(); ++i)
    {
        int64_t s = m_cues[i].m_startMs;
        int64_t e = m_cues[i].m_endMs;
        if (m_dragging && static_cast<int>(i) == m_dragIndex)
        {
            s = m_dragStart;
            e = m_dragEnd;
        }
        const int left = TimelineMsToX(v, s);
        const int right = TimelineMsToX(v, e);
        if (right < 0 || left >= v.m_width)
            continue;
        RECT block = { rc.left + left, rc.top + v.m_trackTop, rc.left + right, rc.top + v.m_trackBottom };
        HBRUSH fill = ::CreateSolidBrush(m_cues[i].m_edited ? kCueEditedFill : kCueFill);
        ::FillRect(hdc, &block, fill);
        ::DeleteObject(fill);
        if (static_cast<int>(i) == m_selected)
        {
            HBRUSH border = ::CreateSolidBrush(kCueSelectedBorder);
            for (int k = 0; k < kSelectedBorderW; ++k)
            {
                RECT f = { block.left + k, block.top + k, block.right - k, block.bottom - k };
                ::FrameRect(hdc, &f, border);
            }
            ::DeleteObject(border);
        }
        RECT text = { block.left + kCueTextPad, block.top, block.right - kCueTextPad, block.bottom };
        if (text.right > text.left)
            ::DrawTextW(hdc, CueListMainText(m_cues[i]).c_str(), -1, &text,
                        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    }
    ::SelectObject(hdc, oldFont);
    ::DeleteObject(cueFont);
    ::DeleteObject(tickFont);
    //播放位置
    const int px = TimelineMsToX(v, m_positionMs);
    if (px >= 0 && px < v.m_width)
    {
        RECT line = { rc.left + px, rc.top, rc.left + px + kPlayheadW, rc.bottom };
        HBRUSH pb = ::CreateSolidBrush(kPlayhead);
        ::FillRect(hdc, &line, pb);
        ::DeleteObject(pb);
    }
    ::RestoreDC(hdc, saved);
    //子控件（横向滚动条）画在最上面
    balloonwjui::DuiControl::OnPaint(hdc, rcDirty);
}

bool CaptionTimelineView::OnLButtonDown(POINT pt, UINT mkFlags)
{
    (void)mkFlags;
    const POINT p = local(pt);
    const TimelineHit hit = TimelineHitTest(view(), m_cues, p.x, p.y, kEdgePx);
    m_pressed = true;
    m_dragging = false;
    m_pressX = p.x;
    m_dragKind = hit.m_kind;
    m_dragIndex = hit.m_index;
    Capture();
    if (hit.m_index >= 0)
    {
        m_dragStart = m_cues[hit.m_index].m_startMs;
        m_dragEnd = m_cues[hit.m_index].m_endMs;
        m_selected = hit.m_index;
        Invalidate();
        NotifyParent(DUIN_TIMELINE_SELECT, static_cast<LPARAM>(hit.m_index));
    }
    else
    {
        NotifyParent(DUIN_TIMELINE_SEEK, static_cast<LPARAM>(TimelineXToMs(view(), p.x)));
    }
    return true;
}

bool CaptionTimelineView::OnMouseMove(POINT pt, UINT mkFlags)
{
    (void)mkFlags;
    if (!m_pressed)
        return false;
    const POINT p = local(pt);
    if (m_dragIndex < 0)
    {
        //按住空白处拖动：连续跳转（拖动播放位置）
        NotifyParent(DUIN_TIMELINE_SEEK, static_cast<LPARAM>(TimelineXToMs(view(), p.x)));
        return true;
    }
    if (!m_dragging && abs(p.x - m_pressX) < kDragThresholdPx)
        return true;
    m_dragging = true;
    const int64_t deltaMs = static_cast<int64_t>((p.x - m_pressX) * kMsPerSecF / m_pxPerSec);
    int64_t s = 0;
    int64_t e = 0;
    if (TimelineDragTimes(m_dragKind, m_cues, m_dragIndex, deltaMs, m_durationMs, s, e))
    {
        m_dragStart = s;
        m_dragEnd = e;
        Invalidate();
    }
    return true;
}

bool CaptionTimelineView::OnLButtonUp(POINT pt, UINT mkFlags)
{
    (void)pt;
    (void)mkFlags;
    if (!m_pressed)
        return false;
    m_pressed = false;
    ReleaseCapture();
    if (m_dragging && m_dragIndex >= 0 && m_dragIndex < static_cast<int>(m_cues.size()))
    {
        m_dragging = false;
        const CaptionCue& c = m_cues[m_dragIndex];
        if (m_dragStart != c.m_startMs || m_dragEnd != c.m_endMs)
        {
            m_hasResult = true;
            m_resultIndex = m_dragIndex;
            m_resultStart = m_dragStart;
            m_resultEnd = m_dragEnd;
            NotifyParent(DUIN_TIMELINE_DRAGGED);
        }
        Invalidate();
    }
    m_dragging = false;
    return true;
}

bool CaptionTimelineView::OnMouseWheel(POINT pt, short zDelta, UINT mkFlags)
{
    const TimelineView v = view();
    const int steps = WheelSteps(zDelta);
    if ((mkFlags & MK_CONTROL) != 0)
    {
        //以鼠标位置为中心缩放：鼠标下的时刻保持不动
        const POINT p = local(pt);
        const int64_t anchor = TimelineXToMs(v, p.x);
        m_pxPerSec = TimelineZoom(m_pxPerSec, steps);
        TimelineView nv = view();
        const int64_t scroll = anchor - static_cast<int64_t>(p.x * kMsPerSecF / m_pxPerSec);
        m_scrollMs = TimelineClampScroll(nv, scroll, m_durationMs);
    }
    else
    {
        const int64_t deltaMs = static_cast<int64_t>(-steps * kWheelScrollPx * kMsPerSecF / m_pxPerSec);
        m_scrollMs = TimelineClampScroll(v, m_scrollMs + deltaMs, m_durationMs);
    }
    //滚动或缩放时滚动条淡入，停止操作后淡出
    syncScrollBar(true);
    Invalidate();
    return true;
}

bool CaptionTimelineView::OnSetCursor(POINT pt)
{
    const POINT p = local(pt);
    const TimelineHit hit = TimelineHitTest(view(), m_cues, p.x, p.y, kEdgePx);
    LPCWSTR cursor = IDC_ARROW;
    if (hit.m_kind == timeline_hit_left || hit.m_kind == timeline_hit_right)
        cursor = IDC_SIZEWE;
    else if (hit.m_kind == timeline_hit_body)
        cursor = IDC_SIZEALL;
    ::SetCursor(::LoadCursor(NULL, cursor));
    return true;
}

} // namespace caption
