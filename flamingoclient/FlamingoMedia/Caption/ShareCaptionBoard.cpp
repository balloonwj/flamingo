/**
 *  屏幕共享实时字幕的显示与发送规则，见 ShareCaptionBoard.h。
 *  balloonwj@qq.com   2026-10-06
 */
#include "ShareCaptionBoard.h"

#include <algorithm>

namespace screenshare {

namespace {

//每秒的毫秒数
const int64_t kMsPerSecond = 1000;

//按更新时刻排序（较早的在前）
bool UpdatedEarlier(const ShareCaptionLine& a, const ShareCaptionLine& b)
{
    if (a.m_updatedMs != b.m_updatedMs)
        return a.m_updatedMs < b.m_updatedMs;
    return a.m_seat < b.m_seat;
}

} // namespace

ShareCaptionThrottle::ShareCaptionThrottle()
    : m_lastPartialMs(-1)
{
}

bool ShareCaptionThrottle::Allow(bool final, int64_t nowMs)
{
    if (final)
        return true;
    const int64_t interval = kMsPerSecond / kShareCaptionPartialPerSec;
    if (m_lastPartialMs >= 0 && nowMs - m_lastPartialMs < interval)
        return false;
    m_lastPartialMs = nowMs;
    return true;
}

void ShareCaptionThrottle::Reset()
{
    m_lastPartialMs = -1;
}

bool ShareCaptionBoard::OnCaption(int seat, const std::wstring& speaker, const std::wstring& text, bool final,
                                  uint32_t sentenceId, int64_t nowMs)
{
    std::map<int, ShareCaptionLine>::iterator it = m_lines.find(seat);
    if (it != m_lines.end())
    {
        //乱序到达的旧句中间结果：忽略
        if (!final && sentenceId < it->second.m_sentenceId)
            return false;
        //同一句的最终结果已经到了，迟到的中间结果也忽略
        if (!final && sentenceId == it->second.m_sentenceId && !it->second.m_partial)
            return false;
    }
    if (text.empty())
    {
        //这一句被清空
        if (it == m_lines.end())
            return false;
        m_lines.erase(it);
        return true;
    }
    ShareCaptionLine line;
    line.m_seat = seat;
    line.m_speaker = speaker;
    line.m_text = text;
    line.m_partial = !final;
    line.m_sentenceId = sentenceId;
    line.m_updatedMs = nowMs;
    m_lines[seat] = line;
    return true;
}

bool ShareCaptionBoard::Expire(int64_t nowMs)
{
    bool changed = false;
    for (std::map<int, ShareCaptionLine>::iterator it = m_lines.begin(); it != m_lines.end();)
    {
        const int64_t hold = it->second.m_partial ? kShareCaptionPartialStaleMs : kShareCaptionFinalHoldMs;
        if (nowMs - it->second.m_updatedMs >= hold)
        {
            m_lines.erase(it++);
            changed = true;
            continue;
        }
        ++it;
    }
    return changed;
}

std::vector<ShareCaptionLine> ShareCaptionBoard::Visible() const
{
    std::vector<ShareCaptionLine> all;
    for (std::map<int, ShareCaptionLine>::const_iterator it = m_lines.begin(); it != m_lines.end(); ++it)
        all.push_back(it->second);
    std::sort(all.begin(), all.end(), &UpdatedEarlier);
    if (all.size() > kShareCaptionMaxSpeakers)
        all.erase(all.begin(), all.end() - static_cast<ptrdiff_t>(kShareCaptionMaxSpeakers));
    return all;
}

void ShareCaptionBoard::RemoveSeat(int seat)
{
    m_lines.erase(seat);
}

void ShareCaptionBoard::Clear()
{
    m_lines.clear();
}

bool ShareCaptionBoard::Empty() const
{
    return m_lines.empty();
}

void ShareCaptionRemoteTimes(int64_t receivedAtMs, uint32_t durationMs, int64_t& startMs, int64_t& endMs)
{
    const int64_t duration = durationMs > 0 ? static_cast<int64_t>(durationMs) : kShareCaptionMinRemoteMs;
    endMs = receivedAtMs < 0 ? 0 : receivedAtMs;
    startMs = endMs - duration;
    if (startMs < 0)
        startMs = 0;
    //太靠近开头时也保证一定时长
    if (endMs - startMs < kShareCaptionMinRemoteMs)
        endMs = startMs + kShareCaptionMinRemoteMs;
}

} // namespace screenshare
