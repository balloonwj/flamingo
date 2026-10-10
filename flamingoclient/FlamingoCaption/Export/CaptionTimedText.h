/**
 *  导出用的字幕时间轴整理（docs/语音字幕方案.md 第 8 节），CaptionTimedText.h
 *
 *  字幕轨版的 tx3g 文字轨要求样本首尾相接、覆盖整段时长（没有字幕的时段用空样本），硬字幕版要按每一帧的时刻
 *  找出该画哪条字幕。两者都从同一份「按开始时刻排序、互不重叠」的显示区间出发，本文件负责：
 *    - BuildDisplaySpans：把字幕条整理成互不重叠的显示区间（重叠时前一条截到后一条开始；文字为空、时长不足的丢弃），
 *      按需要加「说话人：」前缀；
 *    - BuildTextSamples：在显示区间之间补空样本，得到从 0 到总时长首尾相接的样本序列；
 *    - FindSpanAt：二分查找某一时刻落在哪个显示区间里。
 *  全部为纯函数，单测见 Tests/CaptionExportTests.cpp（CAPEX1～CAPEX3）。
 *
 *  典型用法：
 *      std::vector<caption::CaptionSpan> spans = caption::BuildDisplaySpans(draft.m_cues, true);
 *      std::vector<caption::CaptionSpan> samples = caption::BuildTextSamples(spans, durationMs);
 *      int idx = caption::FindSpanAt(spans, ptsMs);
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include <stdint.h>

#include <string>
#include <vector>

#include "../Model/CaptionCue.h"

namespace caption {

//一段显示区间：在 [m_startMs, m_endMs) 内显示 m_text（为空表示这段时间不显示字幕）
struct CaptionSpan
{
    int64_t      m_startMs;   //开始时刻（毫秒，含）
    int64_t      m_endMs;     //结束时刻（毫秒，不含）
    std::wstring m_speaker;   //说话人（硬字幕版按样式决定是否画出）；空样本为空
    std::wstring m_text;      //文字（不含说话人前缀）；空样本为空

    CaptionSpan()
        : m_startMs(0)
        , m_endMs(0)
    {
    }
};

/**
 *  把字幕条整理成互不重叠的显示区间。
 *  @param cues 字幕条（不要求已排序）
 *  @return 按开始时刻排序、互不重叠的区间：与后一条重叠的截到后一条开始；文字为空白、或截短后时长为 0 的丢弃；
 *          开始时刻小于 0 的按 0 处理
 */
std::vector<CaptionSpan> BuildDisplaySpans(const std::vector<CaptionCue>& cues);

/**
 *  显示区间要写进文字样本的文字。
 *  @param span        显示区间
 *  @param withSpeaker 是否加「说话人：」前缀（说话人为空时不加）
 *  @return 文字
 */
std::wstring SpanDisplayText(const CaptionSpan& span, bool withSpeaker);

/**
 *  在显示区间之间补上空样本，得到从 0 起首尾相接的样本序列（tx3g 文字轨用）。
 *  @param spans      BuildDisplaySpans 的结果
 *  @param durationMs 录像总时长（毫秒）；超出它的区间截到它为止，最后一段不到它时补一个空样本到它
 *  @return 样本序列；每个样本时长都大于 0；durationMs 不大于 0 时返回空
 */
std::vector<CaptionSpan> BuildTextSamples(const std::vector<CaptionSpan>& spans, int64_t durationMs);

/**
 *  找出某一时刻所在的显示区间。
 *  @param spans BuildDisplaySpans 的结果（已排序、互不重叠）
 *  @param ms    时刻（毫秒）
 *  @return 区间下标；不在任何区间里时返回 -1
 */
int FindSpanAt(const std::vector<CaptionSpan>& spans, int64_t ms);

} // namespace caption
