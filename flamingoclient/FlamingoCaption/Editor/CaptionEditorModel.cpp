/**
 *  字幕编辑器的纯逻辑，见 CaptionEditorModel.h。
 *  balloonwj@qq.com   2026-10-06
 */
#include "CaptionEditorModel.h"

#include <windows.h>
#include <stdio.h>
#include <wchar.h>

#include "../Model/CaptionFiles.h"
#include "../Model/CaptionText.h"

namespace caption {

namespace {

//时间换算
const int64_t kMsPerSecond = 1000;
const int64_t kMsPerTenth = 100;
const int64_t kSecondsPerMinute = 60;
const int64_t kMinutesPerHour = 60;
const int64_t kMsPerMinute = kMsPerSecond * kSecondsPerMinute;
const int64_t kMsPerHour = kMsPerMinute * kMinutesPerHour;
//时间文字的缓冲区长度（字符）
const int kTimeTextChars = 32;
//解析时刻：最多三段（时:分:秒），小数最多三位（毫秒）
const int kMaxTimeParts = 3;
const int kMaxFractionDigits = 3;
//分、秒的上限（有多段时）
const int64_t kMaxMinuteOrSecond = 59;
//说话人与文字之间的分隔（与导出、绘制一致）
const wchar_t kFullColon = 0xFF1A;

} // namespace

CaptionEditorModel::CaptionEditorModel()
    : m_undo(kCaptionEditorUndoLimit)
    , m_selectedId(0)
    , m_dirty(false)
{
}

bool CaptionEditorModel::Load(const std::wstring& videoPath, std::string* error)
{
    if (error != NULL)
        error->clear();
    CaptionDraft draft;
    const std::wstring draftPath = CaptionDraftIo::DraftPathFor(videoPath);
    bool ok = true;
    if (::GetFileAttributesW(draftPath.c_str()) != INVALID_FILE_ATTRIBUTES)
    {
        std::string why;
        if (!CaptionDraftIo::LoadFile(draftPath, draft, &why))
        {
            ok = false;
            if (error != NULL)
                *error = why;
            draft = CaptionDraft();
        }
    }
    if (draft.m_videoName.empty())
        draft.m_videoName = FileNameOf(videoPath);
    Reset(videoPath, draft);
    return ok;
}

void CaptionEditorModel::Reset(const std::wstring& videoPath, const CaptionDraft& draft)
{
    m_videoPath = videoPath;
    m_draft = draft;
    m_track.Assign(draft.m_cues);
    m_track.SetNextId(draft.m_nextId);
    m_draft.m_cues.clear();
    m_undo.Clear();
    m_selectedId = 0;
    m_dirty = false;
}

bool CaptionEditorModel::Save(std::string* error)
{
    const CaptionDraft draft = ToDraft();
    if (!CaptionDraftIo::SaveFile(CaptionDraftIo::DraftPathFor(m_videoPath), draft))
    {
        if (error != NULL)
            *error = "save draft failed";
        return false;
    }
    m_dirty = false;
    return true;
}

CaptionDraft CaptionEditorModel::ToDraft() const
{
    CaptionDraft draft = m_draft;
    draft.m_cues = m_track.Cues();
    draft.m_nextId = m_track.NextId();
    return draft;
}

const std::wstring& CaptionEditorModel::VideoPath() const
{
    return m_videoPath;
}

const CaptionTrack& CaptionEditorModel::Track() const
{
    return m_track;
}

bool CaptionEditorModel::IsDirty() const
{
    return m_dirty;
}

int CaptionEditorModel::Selected() const
{
    if (m_selectedId == 0)
        return -1;
    return m_track.IndexOfId(m_selectedId);
}

int64_t CaptionEditorModel::SelectedId() const
{
    return Selected() >= 0 ? m_selectedId : 0;
}

void CaptionEditorModel::Select(int index)
{
    if (index < 0 || index >= static_cast<int>(m_track.Count()))
        m_selectedId = 0;
    else
        m_selectedId = m_track.At(static_cast<size_t>(index)).m_id;
}

void CaptionEditorModel::pushUndo()
{
    m_undo.Push(m_track.Cues());
}

void CaptionEditorModel::touched(int64_t selectId)
{
    m_dirty = true;
    m_selectedId = (selectId != 0 && m_track.IndexOfId(selectId) >= 0) ? selectId : 0;
}

bool CaptionEditorModel::EditText(int index, const std::wstring& text)
{
    if (index < 0 || index >= static_cast<int>(m_track.Count()))
        return false;
    const std::wstring normalized = NormalizeText(text);
    if (normalized.empty() || normalized == m_track.At(static_cast<size_t>(index)).m_text)
        return false;
    const int64_t id = m_track.At(static_cast<size_t>(index)).m_id;
    pushUndo();
    m_track.SetText(static_cast<size_t>(index), normalized);
    touched(id);
    return true;
}

bool CaptionEditorModel::EditSpeaker(int index, const std::wstring& speaker)
{
    if (index < 0 || index >= static_cast<int>(m_track.Count()))
        return false;
    const std::wstring normalized = NormalizeText(speaker);
    if (normalized == m_track.At(static_cast<size_t>(index)).m_speaker)
        return false;
    const int64_t id = m_track.At(static_cast<size_t>(index)).m_id;
    pushUndo();
    m_track.SetSpeaker(static_cast<size_t>(index), normalized);
    touched(id);
    return true;
}

bool CaptionEditorModel::EditTimes(int index, int64_t startMs, int64_t endMs)
{
    if (index < 0 || index >= static_cast<int>(m_track.Count()))
        return false;
    const CaptionCue& cue = m_track.At(static_cast<size_t>(index));
    if (cue.m_startMs == startMs && cue.m_endMs == endMs)
        return false;
    if (startMs < 0 || endMs - startMs < kCaptionMinCueMs)
        return false;
    const int64_t id = cue.m_id;
    pushUndo();
    m_track.SetTimes(static_cast<size_t>(index), startMs, endMs);
    touched(id);
    return true;
}

bool CaptionEditorModel::Split(int index, size_t charPos)
{
    if (index < 0 || index >= static_cast<int>(m_track.Count()))
        return false;
    //先在副本上试拆：失败时不压栈
    CaptionTrack probe = m_track;
    int64_t newId = 0;
    if (!probe.Split(static_cast<size_t>(index), charPos, &newId))
        return false;
    pushUndo();
    m_track = probe;
    touched(newId);
    return true;
}

bool CaptionEditorModel::MergeWithNext(int index)
{
    if (index < 0 || index + 1 >= static_cast<int>(m_track.Count()))
        return false;
    const int64_t id = m_track.At(static_cast<size_t>(index)).m_id;
    pushUndo();
    m_track.MergeWithNext(static_cast<size_t>(index));
    touched(id);
    return true;
}

bool CaptionEditorModel::Insert(int64_t atMs, const std::wstring& placeholder)
{
    const int64_t start = atMs < 0 ? 0 : atMs;
    int64_t end = start + kCaptionInsertDefaultMs;
    //碰到后一条（开始时刻晚于 start 的第一条）时截到它开始
    for (size_t i = 0; i < m_track.Count(); ++i)
    {
        const CaptionCue& c = m_track.At(i);
        if (c.m_startMs > start && c.m_startMs < end)
        {
            end = c.m_startMs;
            break;
        }
    }
    if (end - start < kCaptionMinCueMs)
        return false;
    CaptionCue cue;
    cue.m_startMs = start;
    cue.m_endMs = end;
    cue.m_text = NormalizeText(placeholder);
    cue.m_source = caption_source_manual;
    cue.m_edited = true;
    if (cue.m_text.empty())
        return false;
    pushUndo();
    const int64_t id = m_track.Add(cue);
    touched(id);
    return true;
}

bool CaptionEditorModel::Remove(int index)
{
    if (index < 0 || index >= static_cast<int>(m_track.Count()))
        return false;
    pushUndo();
    m_track.Remove(static_cast<size_t>(index));
    int64_t next = 0;
    if (static_cast<size_t>(index) < m_track.Count())
        next = m_track.At(static_cast<size_t>(index)).m_id;
    else if (index > 0)
        next = m_track.At(static_cast<size_t>(index) - 1).m_id;
    touched(next);
    return true;
}

bool CaptionEditorModel::ShiftAll(int64_t deltaMs)
{
    if (deltaMs == 0 || m_track.Count() == 0)
        return false;
    const int64_t id = m_selectedId;
    pushUndo();
    m_track.ShiftAll(deltaMs);
    touched(id);
    return true;
}

int CaptionEditorModel::ReplaceAll(const std::wstring& needle, const std::wstring& replacement, bool matchCase)
{
    if (needle.empty())
        return 0;
    CaptionTrack probe = m_track;
    const int count = probe.ReplaceAll(needle, replacement, matchCase);
    if (count <= 0)
        return 0;
    const int64_t id = m_selectedId;
    pushUndo();
    m_track = probe;
    touched(id);
    return count;
}

int CaptionEditorModel::RenameSpeaker(const std::wstring& from, const std::wstring& to)
{
    const std::wstring target = NormalizeText(to);
    if (from == target)
        return 0;
    CaptionTrack probe = m_track;
    const int count = probe.RenameSpeaker(from, target);
    if (count <= 0)
        return 0;
    const int64_t id = m_selectedId;
    pushUndo();
    m_track = probe;
    touched(id);
    return count;
}

void CaptionEditorModel::ReplaceAllCues(const std::vector<CaptionCue>& cues)
{
    pushUndo();
    m_track.Assign(cues);
    touched(0);
}

int CaptionEditorModel::FindNext(const std::wstring& needle, bool matchCase)
{
    if (needle.empty() || m_track.Count() == 0)
        return -1;
    const int sel = Selected();
    const size_t from = (sel >= 0) ? static_cast<size_t>(sel + 1) % m_track.Count() : 0;
    const int found = m_track.FindNext(needle, from, matchCase);
    if (found >= 0)
        Select(found);
    return found;
}

bool CaptionEditorModel::Undo()
{
    std::vector<CaptionCue> current = m_track.Cues();
    const int64_t nextId = m_track.NextId();
    if (!m_undo.Undo(current))
        return false;
    m_track.Assign(current);
    m_track.SetNextId(nextId);
    touched(m_selectedId);
    return true;
}

bool CaptionEditorModel::Redo()
{
    std::vector<CaptionCue> current = m_track.Cues();
    const int64_t nextId = m_track.NextId();
    if (!m_undo.Redo(current))
        return false;
    m_track.Assign(current);
    m_track.SetNextId(nextId);
    touched(m_selectedId);
    return true;
}

bool CaptionEditorModel::CanUndo() const
{
    return m_undo.CanUndo();
}

bool CaptionEditorModel::CanRedo() const
{
    return m_undo.CanRedo();
}

const CaptionStyle& CaptionEditorModel::Style() const
{
    return m_draft.m_style;
}

void CaptionEditorModel::SetStyle(const CaptionStyle& style)
{
    m_draft.m_style = style;
    m_dirty = true;
}

const CaptionPip& CaptionEditorModel::Pip() const
{
    return m_draft.m_pip;
}

void CaptionEditorModel::AddExport(int kind, const std::wstring& name)
{
    for (size_t i = 0; i < m_draft.m_exports.size(); ++i)
    {
        if (m_draft.m_exports[i].m_kind == kind && m_draft.m_exports[i].m_name == name)
            return;
    }
    CaptionExport e;
    e.m_kind = kind;
    e.m_name = name;
    m_draft.m_exports.push_back(e);
    m_dirty = true;
}

std::vector<std::wstring> CaptionEditorModel::Speakers() const
{
    std::vector<std::wstring> out;
    for (size_t i = 0; i < m_track.Count(); ++i)
    {
        const std::wstring& s = m_track.At(i).m_speaker;
        if (s.empty())
            continue;
        bool seen = false;
        for (size_t k = 0; k < out.size() && !seen; ++k)
            seen = (out[k] == s);
        if (!seen)
            out.push_back(s);
    }
    return out;
}

std::wstring FormatEditTime(int64_t ms)
{
    if (ms < 0)
        ms = 0;
    const int64_t hours = ms / kMsPerHour;
    const int64_t minutes = (ms % kMsPerHour) / kMsPerMinute;
    const int64_t seconds = (ms % kMsPerMinute) / kMsPerSecond;
    const int64_t tenths = (ms % kMsPerSecond) / kMsPerTenth;
    wchar_t buf[kTimeTextChars] = { 0 };
    if (hours > 0)
        _snwprintf_s(buf, kTimeTextChars, _TRUNCATE, L"%lld:%02lld:%02lld.%lld", static_cast<long long>(hours),
                     static_cast<long long>(minutes), static_cast<long long>(seconds), static_cast<long long>(tenths));
    else
        _snwprintf_s(buf, kTimeTextChars, _TRUNCATE, L"%02lld:%02lld.%lld", static_cast<long long>(minutes),
                     static_cast<long long>(seconds), static_cast<long long>(tenths));
    return buf;
}

bool ParseEditTime(const std::wstring& text, int64_t& ms)
{
    //去掉首尾空白
    size_t b = 0;
    size_t e = text.size();
    while (b < e && iswspace(text[b]))
        ++b;
    while (e > b && iswspace(text[e - 1]))
        --e;
    if (b == e)
        return false;
    const std::wstring s = text.substr(b, e - b);
    //小数部分
    int64_t fractionMs = 0;
    std::wstring whole = s;
    const size_t dot = s.find(L'.');
    if (dot != std::wstring::npos)
    {
        const std::wstring frac = s.substr(dot + 1);
        whole = s.substr(0, dot);
        if (frac.empty() || frac.size() > static_cast<size_t>(kMaxFractionDigits))
            return false;
        int64_t scale = kMsPerSecond;
        for (size_t i = 0; i < frac.size(); ++i)
        {
            if (frac[i] < L'0' || frac[i] > L'9')
                return false;
            scale /= 10;
            fractionMs += (frac[i] - L'0') * scale;
        }
    }
    //冒号分段
    std::vector<int64_t> parts;
    int64_t cur = 0;
    bool hasDigit = false;
    for (size_t i = 0; i <= whole.size(); ++i)
    {
        if (i == whole.size() || whole[i] == L':')
        {
            if (!hasDigit)
                return false;
            parts.push_back(cur);
            cur = 0;
            hasDigit = false;
            continue;
        }
        if (whole[i] < L'0' || whole[i] > L'9')
            return false;
        cur = cur * 10 + (whole[i] - L'0');
        hasDigit = true;
    }
    if (parts.empty() || parts.size() > static_cast<size_t>(kMaxTimeParts))
        return false;
    int64_t total = 0;
    if (parts.size() == 1)
    {
        total = parts[0] * kMsPerSecond;
    }
    else
    {
        //除第一段外的分、秒不超过 59
        for (size_t i = 1; i < parts.size(); ++i)
        {
            if (parts[i] > kMaxMinuteOrSecond)
                return false;
        }
        if (parts.size() == 2)
            total = parts[0] * kMsPerMinute + parts[1] * kMsPerSecond;
        else
            total = parts[0] * kMsPerHour + parts[1] * kMsPerMinute + parts[2] * kMsPerSecond;
    }
    ms = total + fractionMs;
    return true;
}

std::wstring CueListMainText(const CaptionCue& cue)
{
    if (cue.m_speaker.empty())
        return cue.m_text;
    return cue.m_speaker + kFullColon + cue.m_text;
}

std::wstring CueListSubText(const CaptionCue& cue, const std::wstring& editedMark)
{
    std::wstring s = FormatEditTime(cue.m_startMs) + L" – " + FormatEditTime(cue.m_endMs);
    if (cue.m_edited && !editedMark.empty())
        s += L" · " + editedMark;
    return s;
}

} // namespace caption
