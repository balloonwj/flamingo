/**
 *  语音字幕的字幕轨与撤销栈，实现见 CaptionTrack.h。
 *
 *  balloonwj@qq.com   2026-10-06
 */
#include "CaptionTrack.h"

#include <algorithm>

#include "CaptionText.h"

namespace caption {

namespace {

//第一个分配出去的编号
const int64_t kFirstCueId = 1;

//排序规则：开始时刻早的在前，相同时编号小的在前
bool CueLess(const CaptionCue& a, const CaptionCue& b)
{
    if (a.m_startMs != b.m_startMs)
        return a.m_startMs < b.m_startMs;
    return a.m_id < b.m_id;
}

} // namespace

CaptionCue::CaptionCue()
    : m_id(0)
    , m_startMs(0)
    , m_endMs(0)
    , m_source(caption_source_manual)
    , m_edited(false)
{
}

CaptionTrack::CaptionTrack()
    : m_nextId(kFirstCueId)
{
}

size_t CaptionTrack::Count() const
{
    return m_cues.size();
}

const CaptionCue& CaptionTrack::At(size_t index) const
{
    return m_cues[index];
}

const std::vector<CaptionCue>& CaptionTrack::Cues() const
{
    return m_cues;
}

void CaptionTrack::Assign(const std::vector<CaptionCue>& cues)
{
    m_cues = cues;
    int64_t maxId = 0;
    for (size_t i = 0; i < m_cues.size(); ++i)
    {
        if (m_cues[i].m_id > maxId)
            maxId = m_cues[i].m_id;
    }
    if (m_nextId <= maxId)
        m_nextId = maxId + 1;
    for (size_t i = 0; i < m_cues.size(); ++i)
    {
        if (m_cues[i].m_id == 0)
            m_cues[i].m_id = m_nextId++;
    }
    SortByStart();
}

void CaptionTrack::Clear()
{
    m_cues.clear();
}

int64_t CaptionTrack::Add(const CaptionCue& cue)
{
    if (cue.m_startMs < 0 || cue.m_endMs <= cue.m_startMs)
        return 0;
    CaptionCue c = cue;
    if (c.m_id == 0)
        c.m_id = m_nextId++;
    else if (c.m_id >= m_nextId)
        m_nextId = c.m_id + 1;
    std::vector<CaptionCue>::iterator pos = std::upper_bound(m_cues.begin(), m_cues.end(), c, CueLess);
    m_cues.insert(pos, c);
    return c.m_id;
}

int CaptionTrack::IndexOfId(int64_t id) const
{
    for (size_t i = 0; i < m_cues.size(); ++i)
    {
        if (m_cues[i].m_id == id)
            return static_cast<int>(i);
    }
    return -1;
}

bool CaptionTrack::Remove(size_t index)
{
    if (index >= m_cues.size())
        return false;
    m_cues.erase(m_cues.begin() + static_cast<ptrdiff_t>(index));
    return true;
}

bool CaptionTrack::SetText(size_t index, const std::wstring& text)
{
    if (index >= m_cues.size())
        return false;
    m_cues[index].m_text = NormalizeText(text);
    m_cues[index].m_edited = true;
    return true;
}

bool CaptionTrack::SetSpeaker(size_t index, const std::wstring& speaker)
{
    if (index >= m_cues.size())
        return false;
    m_cues[index].m_speaker = speaker;
    m_cues[index].m_edited = true;
    return true;
}

bool CaptionTrack::AppendText(size_t index, const std::wstring& suffix)
{
    if (index >= m_cues.size())
        return false;
    m_cues[index].m_text += suffix;
    return true;
}

bool CaptionTrack::SetTimes(size_t index, int64_t startMs, int64_t endMs)
{
    if (index >= m_cues.size() || startMs < 0 || endMs - startMs < kCaptionMinCueMs)
        return false;
    m_cues[index].m_startMs = startMs;
    m_cues[index].m_endMs = endMs;
    m_cues[index].m_edited = true;
    SortByStart();
    return true;
}

bool CaptionTrack::Split(size_t index, size_t charPos, int64_t* newId)
{
    if (index >= m_cues.size())
        return false;
    const CaptionCue& src = m_cues[index];
    if (charPos == 0 || charPos >= src.m_text.size())
        return false;
    const std::wstring left = NormalizeText(src.m_text.substr(0, charPos));
    const std::wstring right = NormalizeText(src.m_text.substr(charPos));
    if (left.empty() || right.empty())
        return false;
    const int64_t unitsLeft = DisplayUnits(left);
    const int64_t unitsRight = DisplayUnits(right);
    const int64_t duration = src.m_endMs - src.m_startMs;
    const int64_t splitMs = src.m_startMs + duration * unitsLeft / (unitsLeft + unitsRight);
    if (splitMs - src.m_startMs < kCaptionMinCueMs || src.m_endMs - splitMs < kCaptionMinCueMs)
        return false;

    CaptionCue second = src;
    second.m_id = m_nextId++;
    second.m_startMs = splitMs;
    second.m_text = right;
    second.m_edited = true;
    m_cues[index].m_endMs = splitMs;
    m_cues[index].m_text = left;
    m_cues[index].m_edited = true;
    m_cues.insert(m_cues.begin() + static_cast<ptrdiff_t>(index) + 1, second);
    SortByStart();
    if (newId != NULL)
        *newId = second.m_id;
    return true;
}

bool CaptionTrack::MergeWithNext(size_t index)
{
    if (index + 1 >= m_cues.size())
        return false;
    CaptionCue& a = m_cues[index];
    const CaptionCue& b = m_cues[index + 1];
    a.m_text = JoinText(a.m_text, b.m_text);
    if (b.m_endMs > a.m_endMs)
        a.m_endMs = b.m_endMs;
    a.m_edited = true;
    m_cues.erase(m_cues.begin() + static_cast<ptrdiff_t>(index) + 1);
    return true;
}

void CaptionTrack::ShiftAll(int64_t deltaMs)
{
    for (size_t i = 0; i < m_cues.size(); ++i)
    {
        CaptionCue& c = m_cues[i];
        c.m_startMs += deltaMs;
        c.m_endMs += deltaMs;
        if (c.m_startMs < 0)
            c.m_startMs = 0;
        if (c.m_endMs - c.m_startMs < kCaptionMinCueMs)
            c.m_endMs = c.m_startMs + kCaptionMinCueMs;
        c.m_edited = true;
    }
    SortByStart();
}

int CaptionTrack::ReplaceAll(const std::wstring& needle, const std::wstring& replacement, bool matchCase)
{
    int total = 0;
    for (size_t i = 0; i < m_cues.size(); ++i)
    {
        int n = 0;
        const std::wstring replaced = ReplaceInText(m_cues[i].m_text, needle, replacement, matchCase, false, &n);
        if (n > 0)
        {
            m_cues[i].m_text = replaced;
            m_cues[i].m_edited = true;
            total += n;
        }
    }
    return total;
}

int CaptionTrack::RenameSpeaker(const std::wstring& from, const std::wstring& to)
{
    int n = 0;
    for (size_t i = 0; i < m_cues.size(); ++i)
    {
        if (m_cues[i].m_speaker == from)
        {
            m_cues[i].m_speaker = to;
            m_cues[i].m_edited = true;
            ++n;
        }
    }
    return n;
}

int CaptionTrack::FindNext(const std::wstring& needle, size_t fromIndex, bool matchCase) const
{
    const size_t n = m_cues.size();
    if (n == 0 || needle.empty())
        return -1;
    for (size_t k = 0; k < n; ++k)
    {
        const size_t i = (fromIndex + k) % n;
        if (FindInText(m_cues[i].m_text, needle, 0, matchCase, false) != std::wstring::npos)
            return static_cast<int>(i);
    }
    return -1;
}

int CaptionTrack::IndexAtTime(int64_t ms) const
{
    int found = -1;
    for (size_t i = 0; i < m_cues.size(); ++i)
    {
        if (m_cues[i].m_startMs > ms)
            break;
        if (ms < m_cues[i].m_endMs)
            found = static_cast<int>(i);
    }
    return found;
}

int64_t CaptionTrack::NextId() const
{
    return m_nextId;
}

void CaptionTrack::SetNextId(int64_t nextId)
{
    int64_t maxId = 0;
    for (size_t i = 0; i < m_cues.size(); ++i)
    {
        if (m_cues[i].m_id > maxId)
            maxId = m_cues[i].m_id;
    }
    m_nextId = nextId > maxId ? nextId : maxId + 1;
}

void CaptionTrack::SortByStart()
{
    std::stable_sort(m_cues.begin(), m_cues.end(), CueLess);
}

CaptionUndoStack::CaptionUndoStack(size_t limit)
    : m_limit(limit > 0 ? limit : 1)
{
}

void CaptionUndoStack::Push(const std::vector<CaptionCue>& before)
{
    m_undo.push_back(before);
    if (m_undo.size() > m_limit)
        m_undo.erase(m_undo.begin());
    m_redo.clear();
}

bool CaptionUndoStack::CanUndo() const
{
    return !m_undo.empty();
}

bool CaptionUndoStack::CanRedo() const
{
    return !m_redo.empty();
}

bool CaptionUndoStack::Undo(std::vector<CaptionCue>& current)
{
    if (m_undo.empty())
        return false;
    m_redo.push_back(current);
    current = m_undo.back();
    m_undo.pop_back();
    return true;
}

bool CaptionUndoStack::Redo(std::vector<CaptionCue>& current)
{
    if (m_redo.empty())
        return false;
    m_undo.push_back(current);
    current = m_redo.back();
    m_redo.pop_back();
    return true;
}

void CaptionUndoStack::Clear()
{
    m_undo.clear();
    m_redo.clear();
}

} // namespace caption
