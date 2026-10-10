/**
 *  CodeNotes.cpp —— 代码片段批注的整理与重新定位，见 CodeNotes.h（docs/代码片段方案.md 第 10.5 节）。
 *
 *  本 .cpp 不 include Flamingo 的 stdafx.h —— 它要被 Flamingo.vcxproj 与 FlamingoUnit.vcxproj 共用。
 *  在 Flamingo.vcxproj 里需标记 PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-10-06
 */
#include "CodeNotes.h"

#include <algorithm>
#include <map>

namespace codesnippet {

namespace {

//空格、Tab
bool IsSpaceChar(wchar_t c)
{
    return c == L' ' || c == L'\t';
}

//按 CRLF、CR、LF 拆成行，比较用：去掉每行首尾的空白
std::vector<std::wstring> SplitTrimmedLines(const std::wstring& text)
{
    std::vector<std::wstring> lines;
    std::wstring cur;
    for (size_t i = 0; i <= text.size(); ++i)
    {
        const bool end = (i == text.size());
        const wchar_t c = end ? L'\0' : text[i];
        if (end || c == L'\r' || c == L'\n')
        {
            size_t b = 0;
            size_t e = cur.size();
            while (b < e && IsSpaceChar(cur[b]))
                ++b;
            while (e > b && IsSpaceChar(cur[e - 1]))
                --e;
            lines.push_back(cur.substr(b, e - b));
            cur.clear();
            if (!end && c == L'\r' && i + 1 < text.size() && text[i + 1] == L'\n')
                ++i;
            continue;
        }
        cur += c;
    }
    return lines;
}

//只含空白的说明
bool IsBlankText(const std::wstring& s)
{
    for (size_t i = 0; i < s.size(); ++i)
    {
        if (!IsSpaceChar(s[i]) && s[i] != L'\r' && s[i] != L'\n')
            return false;
    }
    return true;
}

//按起始行、结束行、作者排序：同一行上不同作者的批注排在一起
bool NoteLess(const CodeNote& a, const CodeNote& b)
{
    if (a.m_from != b.m_from)
        return a.m_from < b.m_from;
    if (a.m_to != b.m_to)
        return a.m_to < b.m_to;
    return a.m_authorId < b.m_authorId;
}

//本人的这条批注在原消息本人的批注里有没有起止行与说明都相同的一条
bool SameNoteInList(const CodeNote& note, const std::vector<CodeNote>& list)
{
    for (size_t i = 0; i < list.size(); ++i)
    {
        if (list[i].m_from == note.m_from && list[i].m_to == note.m_to && list[i].m_text == note.m_text)
            return true;
    }
    return false;
}

/**
 *  按行做最长公共子序列，得到每个旧行对应的新行（下标从 0 起；没有对应行为 -1）。
 *  开头与末尾相同的行先直接对应，只对中间改动过的部分做比较：在编辑框里每输入一次就要重新定位一次，
 *  一次输入通常只改动一两行，这样比较的规模很小。
 *  @return 中间部分新旧行数之积超过 kCodeNotesRemapMaxLines 的平方（比较表格占用的内存过多）时返回 false，match 无效
 */
bool MatchLines(const std::vector<std::wstring>& oldLines, const std::vector<std::wstring>& newLines,
                std::vector<int>& match)
{
    const size_t oldCount = oldLines.size();
    const size_t newCount = newLines.size();
    match.assign(oldCount, -1);

    //开头相同的行
    size_t head = 0;
    while (head < oldCount && head < newCount && oldLines[head] == newLines[head])
    {
        match[head] = (int)head;
        ++head;
    }
    //末尾相同的行（不与开头已对应的行重叠）
    size_t tail = 0;
    while (tail < oldCount - head && tail < newCount - head
           && oldLines[oldCount - 1 - tail] == newLines[newCount - 1 - tail])
    {
        match[oldCount - 1 - tail] = (int)(newCount - 1 - tail);
        ++tail;
    }

    //中间部分：旧行 [head, head + n)，新行 [head, head + m)
    const size_t n = oldCount - head - tail;
    const size_t m = newCount - head - tail;
    if (n == 0 || m == 0)
        return true;
    const size_t maxCells = (size_t)kCodeNotesRemapMaxLines * (size_t)kCodeNotesRemapMaxLines;
    if (n * m > maxCells)
        return false;
    //表格：table[i][j] 为中间部分 oldLines[i..] 与 newLines[j..] 的公共子序列长度，按 (m + 1) 一行展开
    std::vector<unsigned short> table((n + 1) * (m + 1), 0);
    for (size_t i = n; i-- > 0;)
    {
        for (size_t j = m; j-- > 0;)
        {
            unsigned short& cell = table[i * (m + 1) + j];
            if (oldLines[head + i] == newLines[head + j])
                cell = (unsigned short)(table[(i + 1) * (m + 1) + j + 1] + 1);
            else
            {
                const unsigned short down = table[(i + 1) * (m + 1) + j];
                const unsigned short right = table[i * (m + 1) + j + 1];
                cell = (down > right) ? down : right;
            }
        }
    }
    size_t i = 0;
    size_t j = 0;
    while (i < n && j < m)
    {
        if (oldLines[head + i] == newLines[head + j])
        {
            match[head + i] = (int)(head + j);
            ++i;
            ++j;
        }
        else if (table[(i + 1) * (m + 1) + j] >= table[i * (m + 1) + j + 1])
            ++i;
        else
            ++j;
    }
    return true;
}

} // anonymous namespace

bool CodeNoteRangesOverlap(int from1, int to1, int from2, int to2)
{
    return from1 <= to2 && from2 <= to1;
}

std::vector<CodeNote> SanitizeCodeNotes(const std::vector<CodeNote>& notes, int lineCount)
{
    std::vector<CodeNote> valid;
    for (size_t i = 0; i < notes.size(); ++i)
    {
        CodeNote note = notes[i];
        if (note.m_from < 1 || note.m_to < note.m_from)
            continue;
        if (lineCount > 0 && note.m_to > lineCount)
            continue;
        if (IsBlankText(note.m_text))
            continue;
        if ((int)note.m_text.size() > kCodeNoteTextMaxChars)
            note.m_text = note.m_text.substr(0, (size_t)kCodeNoteTextMaxChars);
        valid.push_back(note);
    }
    std::stable_sort(valid.begin(), valid.end(), NoteLess);

    //每个作者已保留的最后一条在 out 里的下标。同一作者已保留的批注互不相交且按起始行排列，结束行也随之递增，
    //所以只要与该作者的最后一条比较
    std::map<long long, size_t> lastByAuthor;
    std::vector<CodeNote> out;
    for (size_t i = 0; i < valid.size() && (int)out.size() < kCodeNotesMax; ++i)
    {
        std::map<long long, size_t>::const_iterator it = lastByAuthor.find(valid[i].m_authorId);
        if (it != lastByAuthor.end())
        {
            const CodeNote& last = out[it->second];
            if (CodeNoteRangesOverlap(last.m_from, last.m_to, valid[i].m_from, valid[i].m_to))
                continue;
        }
        lastByAuthor[valid[i].m_authorId] = out.size();
        out.push_back(valid[i]);
    }
    return out;
}

std::vector<CodeNote> RemapCodeNotes(const std::vector<CodeNote>& notes, const std::wstring& oldText,
                                     const std::wstring& newText, int& removed)
{
    removed = 0;
    const std::vector<std::wstring> oldLines = SplitTrimmedLines(oldText);
    const std::vector<std::wstring> newLines = SplitTrimmedLines(newText);
    const int newCount = (int)newLines.size();

    //规模太大（改动过的中间部分新旧行数之积超过 kCodeNotesRemapMaxLines 的平方，比较表格占用的内存过多）：
    //不做比较，按原行号保留，越过新末行的删除
    std::vector<int> match;
    if (!MatchLines(oldLines, newLines, match))
    {
        std::vector<CodeNote> kept;
        for (size_t i = 0; i < notes.size(); ++i)
        {
            if (notes[i].m_from > newCount)
            {
                ++removed;
                continue;
            }
            CodeNote note = notes[i];
            if (note.m_to > newCount)
                note.m_to = newCount;
            kept.push_back(note);
        }
        return SanitizeCodeNotes(kept, newCount);
    }

    const int oldCount = (int)oldLines.size();
    std::vector<CodeNote> mapped;
    for (size_t k = 0; k < notes.size(); ++k)
    {
        const CodeNote& note = notes[k];
        const int from = note.m_from - 1;   //下标从 0 起
        const int to = note.m_to - 1;
        if (from < 0 || to < from || from >= oldCount)
        {
            ++removed;
            continue;
        }
        const int last = (to < oldCount) ? to : oldCount - 1;

        //首行：有对应行时跟着它走，否则从前面最近一个对应行的下一行算起（前面没有对应行时从第一行算起）
        int newFrom = 0;
        if (match[(size_t)from] >= 0)
            newFrom = match[(size_t)from];
        else
        {
            newFrom = 0;
            for (int i = from - 1; i >= 0; --i)
            {
                if (match[(size_t)i] >= 0)
                {
                    newFrom = match[(size_t)i] + 1;
                    break;
                }
            }
        }
        //末行：有对应行时跟着它走，否则到后面最近一个对应行的上一行为止（后面没有对应行时到最后一行）
        int newTo = newCount - 1;
        if (match[(size_t)last] >= 0)
            newTo = match[(size_t)last];
        else
        {
            for (int i = last + 1; i < oldCount; ++i)
            {
                if (match[(size_t)i] >= 0)
                {
                    newTo = match[(size_t)i] - 1;
                    break;
                }
            }
        }
        if (newFrom > newTo || newFrom >= newCount)
        {
            ++removed;
            continue;
        }
        mapped.push_back(CodeNote(newFrom + 1, newTo + 1, note.m_text, note.m_authorId, note.m_isNew));
    }
    return SanitizeCodeNotes(mapped, newCount);
}

int LeadingBlankLines(const std::wstring& text)
{
    int blank = 0;
    size_t i = 0;
    while (i < text.size())
    {
        size_t e = i;
        while (e < text.size() && text[e] != L'\r' && text[e] != L'\n')
            ++e;
        bool allSpace = true;
        for (size_t k = i; k < e; ++k)
        {
            if (!IsSpaceChar(text[k]))
            {
                allSpace = false;
                break;
            }
        }
        //最后一行没有换行符：它不是空白行就到此为止；是空白行也不算（整段都是空白时没有可去的「开头」）
        if (!allSpace || e == text.size())
            break;
        ++blank;
        i = e;
        if (text[i] == L'\r' && i + 1 < text.size() && text[i + 1] == L'\n')
            ++i;
        ++i;
    }
    return blank;
}

std::vector<CodeNote> ShiftCodeNotes(const std::vector<CodeNote>& notes, int delta)
{
    std::vector<CodeNote> out;
    for (size_t i = 0; i < notes.size(); ++i)
    {
        CodeNote note = notes[i];
        note.m_from += delta;
        note.m_to += delta;
        if (note.m_to < 1)
            continue;
        if (note.m_from < 1)
            note.m_from = 1;
        out.push_back(note);
    }
    return out;
}

std::vector<CodeNote> CodeNotesForSending(const std::vector<CodeNote>& notes, const std::wstring& rawText)
{
    if (notes.empty())
        return std::vector<CodeNote>();
    //去掉首尾空白之后为空的行就是只含空白的行；第一个与最后一个非空白行之间是发送的代码
    const std::vector<std::wstring> lines = SplitTrimmedLines(rawText);
    int first = -1;
    int last = -1;
    for (size_t i = 0; i < lines.size(); ++i)
    {
        if (lines[i].empty())
            continue;
        if (first < 0)
            first = (int)i;
        last = (int)i;
    }
    if (first < 0)
        return std::vector<CodeNote>();
    const int count = last - first + 1;
    std::vector<CodeNote> shifted = ShiftCodeNotes(notes, -first);
    //越过新末行的部分截掉；整条都在末尾空白行里的，由整理去掉
    for (size_t i = 0; i < shifted.size(); ++i)
    {
        if (shifted[i].m_from <= count && shifted[i].m_to > count)
            shifted[i].m_to = count;
    }
    return SanitizeCodeNotes(shifted, count);
}

int FindCodeNoteAtLine(const std::vector<CodeNote>& notes, int line)
{
    for (size_t i = 0; i < notes.size(); ++i)
    {
        if (line >= notes[i].m_from && line <= notes[i].m_to)
            return (int)i;
    }
    return -1;
}

bool CanEditCodeNote(const CodeNote& note, long long selfId)
{
    return note.m_authorId == 0 || note.m_authorId == selfId;
}

std::vector<CodeNote> MergeAnnotationRound(const std::vector<CodeNote>& original, const std::vector<CodeNote>& edited,
                                           long long selfId, int lineCount)
{
    std::vector<CodeNote> merged;
    std::vector<CodeNote> mineBefore;
    for (size_t i = 0; i < original.size(); ++i)
    {
        if (CanEditCodeNote(original[i], selfId))
        {
            mineBefore.push_back(original[i]);
            continue;
        }
        //别人的批注：原样保留，清掉上一轮的「新」
        CodeNote note = original[i];
        note.m_isNew = false;
        merged.push_back(note);
    }
    for (size_t i = 0; i < edited.size(); ++i)
    {
        if (!CanEditCodeNote(edited[i], selfId))
            continue;
        CodeNote note = edited[i];
        note.m_authorId = selfId;
        note.m_isNew = !SameNoteInList(note, mineBefore);
        merged.push_back(note);
    }
    return SanitizeCodeNotes(merged, lineCount);
}

int CountNewCodeNotes(const std::vector<CodeNote>& notes)
{
    int count = 0;
    for (size_t i = 0; i < notes.size(); ++i)
    {
        if (notes[i].m_isNew)
            ++count;
    }
    return count;
}

int FindEditableCodeNoteInRange(const std::vector<CodeNote>& notes, int from, int to, long long selfId)
{
    for (size_t i = 0; i < notes.size(); ++i)
    {
        if (CanEditCodeNote(notes[i], selfId) && CodeNoteRangesOverlap(from, to, notes[i].m_from, notes[i].m_to))
            return (int)i;
    }
    return -1;
}

std::vector<CodeNote> FillCodeNoteAuthors(const std::vector<CodeNote>& notes, long long authorId)
{
    std::vector<CodeNote> out = notes;
    for (size_t i = 0; i < out.size(); ++i)
    {
        if (out[i].m_authorId == 0)
            out[i].m_authorId = authorId;
    }
    return out;
}

} // namespace codesnippet
