/**
 *  字幕编辑器时间轴的纯计算，见 CaptionTimelineMath.h。
 *  balloonwj@qq.com   2026-10-06
 */
#include "CaptionTimelineMath.h"

#include <math.h>

#include "../Model/CaptionTrack.h"   // kCaptionMinCueMs

namespace caption {

namespace {

const double kMsPerSecondF = 1000.0;
//块太窄时边缘各占块宽的几分之一
const int kEdgeFractionDen = 3;
//主刻度可选的间隔（毫秒）
const int64_t kMajorSteps[] = { 1000, 2000, 5000, 10000, 15000, 30000, 60000, 120000, 300000, 600000 };

} // namespace

int TimelineMsToX(const TimelineView& v, int64_t ms)
{
    return static_cast<int>(floor(static_cast<double>(ms - v.m_scrollMs) * v.m_pxPerSec / kMsPerSecondF + 0.5));
}

int64_t TimelineXToMs(const TimelineView& v, int x)
{
    if (v.m_pxPerSec <= 0)
        return v.m_scrollMs;
    const int64_t ms = v.m_scrollMs + static_cast<int64_t>(floor(x * kMsPerSecondF / v.m_pxPerSec + 0.5));
    return ms < 0 ? 0 : ms;
}

TimelineHit TimelineHitTest(const TimelineView& v, const std::vector<CaptionCue>& cues, int x, int y, int edgePx)
{
    TimelineHit hit;
    if (y < v.m_trackTop || y >= v.m_trackBottom)
        return hit;
    //从后往前找：重叠时取开始较晚（画在上面）的一条
    for (int i = static_cast<int>(cues.size()) - 1; i >= 0; --i)
    {
        const int left = TimelineMsToX(v, cues[i].m_startMs);
        const int right = TimelineMsToX(v, cues[i].m_endMs);
        if (x < left || x >= right)
            continue;
        int edge = edgePx;
        if ((right - left) / kEdgeFractionDen < edge)
            edge = (right - left) / kEdgeFractionDen;
        hit.m_index = i;
        if (x < left + edge)
            hit.m_kind = timeline_hit_left;
        else if (x >= right - edge)
            hit.m_kind = timeline_hit_right;
        else
            hit.m_kind = timeline_hit_body;
        return hit;
    }
    return hit;
}

bool TimelineDragTimes(int kind, const std::vector<CaptionCue>& cues, int index, int64_t deltaMs, int64_t durationMs,
                       int64_t& startMs, int64_t& endMs)
{
    if (index < 0 || index >= static_cast<int>(cues.size()))
        return false;
    const CaptionCue& c = cues[index];
    //可用范围：前一条的结束到后一条的开始（与本条重叠的前后条不限制，避免原本重叠的字幕拖不动）
    int64_t lo = 0;
    if (index > 0 && cues[index - 1].m_endMs <= c.m_startMs)
        lo = cues[index - 1].m_endMs;
    int64_t hi = (durationMs > 0) ? durationMs : INT64_MAX;
    if (index + 1 < static_cast<int>(cues.size()) && cues[index + 1].m_startMs >= c.m_endMs)
        hi = cues[index + 1].m_startMs;
    if (hi < c.m_endMs)
        hi = c.m_endMs;
    switch (kind)
    {
    //整块平移：保持时长，夹在可用范围里
    case timeline_hit_body:
    {
        const int64_t len = c.m_endMs - c.m_startMs;
        int64_t s = c.m_startMs + deltaMs;
        if (s < lo)
            s = lo;
        if (s + len > hi)
            s = hi - len;
        if (s < lo)
            s = lo;
        startMs = s;
        endMs = s + len;
        return true;
    }
    //拖左边缘：改开始，不早于可用范围、不晚于「结束减最短时长」
    case timeline_hit_left:
    {
        int64_t s = c.m_startMs + deltaMs;
        if (s < lo)
            s = lo;
        if (s > c.m_endMs - kCaptionMinCueMs)
            s = c.m_endMs - kCaptionMinCueMs;
        if (s < 0)
            s = 0;
        startMs = s;
        endMs = c.m_endMs;
        return true;
    }
    //拖右边缘：改结束，不晚于可用范围、不早于「开始加最短时长」
    case timeline_hit_right:
    {
        int64_t e = c.m_endMs + deltaMs;
        if (e > hi)
            e = hi;
        if (e < c.m_startMs + kCaptionMinCueMs)
            e = c.m_startMs + kCaptionMinCueMs;
        startMs = c.m_startMs;
        endMs = e;
        return true;
    }
    //空白处：不是拖动
    default:
        return false;
    }
}

double TimelineZoom(double pxPerSec, int steps)
{
    double v = pxPerSec * pow(kTimelineZoomStep, steps);
    if (v < kTimelineMinPxPerSec)
        v = kTimelineMinPxPerSec;
    if (v > kTimelineMaxPxPerSec)
        v = kTimelineMaxPxPerSec;
    return v;
}

int64_t TimelineClampScroll(const TimelineView& v, int64_t scrollMs, int64_t durationMs)
{
    const int64_t visibleMs = (v.m_pxPerSec > 0) ? static_cast<int64_t>(v.m_width * kMsPerSecondF / v.m_pxPerSec) : 0;
    int64_t maxScroll = durationMs - visibleMs;
    if (maxScroll < 0)
        maxScroll = 0;
    if (scrollMs > maxScroll)
        scrollMs = maxScroll;
    if (scrollMs < 0)
        scrollMs = 0;
    return scrollMs;
}

TimelineScrollBarState TimelineScrollBar(const TimelineView& v, int64_t durationMs)
{
    TimelineScrollBarState s;
    if (v.m_width <= 0 || v.m_pxPerSec <= 0 || durationMs <= 0)
        return s;
    const int contentPx = static_cast<int>(floor(static_cast<double>(durationMs) * v.m_pxPerSec / kMsPerSecondF + 0.5));
    if (contentPx <= v.m_width)
        return s;
    s.m_maxPos = contentPx - v.m_width;
    s.m_page = v.m_width;
    int pos = static_cast<int>(floor(static_cast<double>(v.m_scrollMs) * v.m_pxPerSec / kMsPerSecondF + 0.5));
    if (pos < 0)
        pos = 0;
    if (pos > s.m_maxPos)
        pos = s.m_maxPos;
    s.m_pos = pos;
    return s;
}

int64_t TimelineScrollFromBar(const TimelineView& v, int pos, int64_t durationMs)
{
    if (v.m_pxPerSec <= 0)
        return 0;
    const int64_t ms = static_cast<int64_t>(floor(pos * kMsPerSecondF / v.m_pxPerSec + 0.5));
    return TimelineClampScroll(v, ms, durationMs);
}

int64_t TimelineMajorStepMs(double pxPerSec)
{
    const size_t count = sizeof(kMajorSteps) / sizeof(kMajorSteps[0]);
    for (size_t i = 0; i < count; ++i)
    {
        if (kMajorSteps[i] * pxPerSec / kMsPerSecondF >= kTimelineMinTickPx)
            return kMajorSteps[i];
    }
    return kMajorSteps[count - 1];
}

std::vector<TimelineTick> TimelineTicks(const TimelineView& v, int64_t durationMs)
{
    std::vector<TimelineTick> out;
    if (v.m_width <= 0 || v.m_pxPerSec <= 0)
        return out;
    const int64_t major = TimelineMajorStepMs(v.m_pxPerSec);
    const int64_t minor = major / 2;
    const int64_t endMs = TimelineXToMs(v, v.m_width);
    const int64_t limit = (durationMs > 0 && durationMs < endMs) ? durationMs : endMs;
    int64_t t = (v.m_scrollMs / minor) * minor;
    for (; t <= limit; t += minor)
    {
        if (t < v.m_scrollMs)
            continue;
        TimelineTick tick;
        tick.m_ms = t;
        tick.m_x = TimelineMsToX(v, t);
        tick.m_major = (t % major) == 0;
        out.push_back(tick);
    }
    return out;
}

} // namespace caption
