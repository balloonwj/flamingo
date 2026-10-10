/**
 *  导出用的字幕时间轴整理，见 CaptionTimedText.h。
 *  balloonwj@qq.com   2026-10-06
 */
#include "CaptionTimedText.h"

#include <algorithm>

#include "../Model/CaptionText.h"

namespace caption {

namespace {

//排序用：按开始时刻，相同时按结束时刻
bool SpanLess(const CaptionSpan& a, const CaptionSpan& b)
{
    if (a.m_startMs != b.m_startMs)
        return a.m_startMs < b.m_startMs;
    return a.m_endMs < b.m_endMs;
}

//说话人与文字之间的分隔（与 SRT 导出、硬字幕的前缀写法一致）
const wchar_t* const kSpeakerSeparator = L"：";

} // namespace

std::vector<CaptionSpan> BuildDisplaySpans(const std::vector<CaptionCue>& cues)
{
    std::vector<CaptionSpan> sorted;
    sorted.reserve(cues.size());
    for (size_t i = 0; i < cues.size(); ++i)
    {
        const std::wstring text = NormalizeText(cues[i].m_text);
        if (text.empty())
            continue;
        CaptionSpan s;
        s.m_startMs = cues[i].m_startMs < 0 ? 0 : cues[i].m_startMs;
        s.m_endMs = cues[i].m_endMs;
        s.m_speaker = cues[i].m_speaker;
        s.m_text = text;
        if (s.m_endMs <= s.m_startMs)
            continue;
        sorted.push_back(s);
    }
    std::stable_sort(sorted.begin(), sorted.end(), &SpanLess);
    std::vector<CaptionSpan> out;
    out.reserve(sorted.size());
    for (size_t i = 0; i < sorted.size(); ++i)
    {
        CaptionSpan s = sorted[i];
        //与后一条重叠：截到后一条开始，后一条优先显示
        if (i + 1 < sorted.size() && sorted[i + 1].m_startMs < s.m_endMs)
            s.m_endMs = sorted[i + 1].m_startMs;
        if (s.m_endMs > s.m_startMs)
            out.push_back(s);
    }
    return out;
}

std::wstring SpanDisplayText(const CaptionSpan& span, bool withSpeaker)
{
    if (!withSpeaker || span.m_speaker.empty() || span.m_text.empty())
        return span.m_text;
    return span.m_speaker + kSpeakerSeparator + span.m_text;
}

std::vector<CaptionSpan> BuildTextSamples(const std::vector<CaptionSpan>& spans, int64_t durationMs)
{
    std::vector<CaptionSpan> out;
    if (durationMs <= 0)
        return out;
    int64_t cursor = 0;
    for (size_t i = 0; i < spans.size() && cursor < durationMs; ++i)
    {
        CaptionSpan s = spans[i];
        if (s.m_startMs < cursor)
            s.m_startMs = cursor;
        if (s.m_endMs > durationMs)
            s.m_endMs = durationMs;
        if (s.m_endMs <= s.m_startMs)
            continue;
        //前面的空档补一个空样本
        if (s.m_startMs > cursor)
        {
            CaptionSpan gap;
            gap.m_startMs = cursor;
            gap.m_endMs = s.m_startMs;
            out.push_back(gap);
        }
        out.push_back(s);
        cursor = s.m_endMs;
    }
    if (cursor < durationMs)
    {
        CaptionSpan tail;
        tail.m_startMs = cursor;
        tail.m_endMs = durationMs;
        out.push_back(tail);
    }
    return out;
}

int FindSpanAt(const std::vector<CaptionSpan>& spans, int64_t ms)
{
    //找第一个开始时刻大于 ms 的区间，它前一个就是可能包含 ms 的那个
    size_t lo = 0;
    size_t hi = spans.size();
    while (lo < hi)
    {
        const size_t mid = lo + (hi - lo) / 2;
        if (spans[mid].m_startMs <= ms)
            lo = mid + 1;
        else
            hi = mid;
    }
    if (lo == 0)
        return -1;
    const size_t idx = lo - 1;
    if (ms < spans[idx].m_endMs)
        return static_cast<int>(idx);
    return -1;
}

} // namespace caption
