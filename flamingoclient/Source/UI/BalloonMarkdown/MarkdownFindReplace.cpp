/**
 *  MarkdownFindReplace.cpp
 *
 *  Markdown 编辑器查找与替换的实现，说明见 MarkdownFindReplace.h。
 *
 *  做法：不区分大小写时先把全文与查找词都转成小写（逐个 UTF-16 码元映射，长度与下标不变），再用 std::wstring 的
 *  find / rfind 查找；替换时下标对应回原文，替换进去的仍是原样的替换词。
 *
 *  本 .cpp 不 include stdafx.h、不依赖 ATL，与 FlamingoUnit 共用（工程里 PrecompiledHeader = NotUsing）。
 *
 *  balloonwj@qq.com   2026-10-07
 */
#include "MarkdownFindReplace.h"

#include <windows.h>

#include <algorithm>

namespace balloonmd {

namespace {

//把下标截断到 [0, len]
int ClampIndex(int pos, int len)
{
    return (std::max)(0, (std::min)(pos, len));
}

//查找用的文字：区分大小写时原样返回，否则转成小写（CharLowerBuffW 逐个码元映射，长度不变）
std::wstring Folded(const std::wstring& s, bool matchCase)
{
    if (matchCase || s.empty())
        return s;
    std::wstring lower = s;
    ::CharLowerBuffW(&lower[0], static_cast<DWORD>(lower.size()));
    return lower;
}

//由新旧全文求最小替换区间，填好结果（与 MarkdownEditCommands 的做法相同）
balloonchatwnd::MarkdownEditResult MakeResult(const std::wstring& oldText, const std::wstring& newText, int selStart,
                                              int selEnd)
{
    balloonchatwnd::MarkdownEditResult r;
    const size_t oldLen = oldText.size();
    const size_t newLen = newText.size();
    size_t prefix = 0;
    while (prefix < oldLen && prefix < newLen && oldText[prefix] == newText[prefix])
        ++prefix;
    size_t suffix = 0;
    while (suffix < oldLen - prefix && suffix < newLen - prefix
           && oldText[oldLen - 1 - suffix] == newText[newLen - 1 - suffix])
        ++suffix;
    r.m_replaceStart = static_cast<int>(prefix);
    r.m_replaceEnd = static_cast<int>(oldLen - suffix);
    r.m_replacement = newText.substr(prefix, newLen - suffix - prefix);
    r.m_selStart = selStart;
    r.m_selEnd = selEnd;
    r.m_newText = newText;
    return r;
}

/**
 *  在已转换好大小写的文字里查找，规则同 FindMarkdownText。
 *  @param hay    全文（已按需转成小写）
 *  @param needle 查找词（同上），不为空
 */
bool FindFolded(const std::wstring& hay, const std::wstring& needle, int from, bool forward, int& outStart,
                bool* wrapped)
{
    const int len = static_cast<int>(hay.size());
    const int n = static_cast<int>(needle.size());
    from = ClampIndex(from, len);
    if (wrapped != NULL)
        *wrapped = false;
    if (forward)
    {
        size_t at = hay.find(needle, static_cast<size_t>(from));
        if (at == std::wstring::npos)
        {
            //回到开头再找一遍：只找起点之前开始的匹配（与起点之后重叠的匹配上一轮已经找过）
            at = hay.find(needle);
            if (at == std::wstring::npos || static_cast<int>(at) >= from)
                return false;
            if (wrapped != NULL)
                *wrapped = true;
        }
        outStart = static_cast<int>(at);
        return true;
    }

    //向前找：终点不超过 from 的最后一个匹配
    if (from >= n)
    {
        const size_t at = hay.rfind(needle, static_cast<size_t>(from - n));
        if (at != std::wstring::npos)
        {
            outStart = static_cast<int>(at);
            return true;
        }
    }
    //回到末尾再找一遍：只找终点在 from 之后的匹配
    const size_t at = hay.rfind(needle);
    if (at == std::wstring::npos || static_cast<int>(at) + n <= from)
        return false;
    if (wrapped != NULL)
        *wrapped = true;
    outStart = static_cast<int>(at);
    return true;
}

} // namespace

int MarkdownFindKeyFor(unsigned vk, bool ctrl, bool shift, bool alt)
{
    if (alt)
        return md_find_key_none;
    //Ctrl+F、Ctrl+H（不带 Shift）
    if (ctrl && !shift)
    {
        if (vk == static_cast<unsigned>('F'))
            return md_find_key_open_find;
        if (vk == static_cast<unsigned>('H'))
            return md_find_key_open_replace;
        return md_find_key_none;
    }
    //F3、Shift+F3（不带 Ctrl）
    if (!ctrl && vk == VK_F3)
        return shift ? md_find_key_prev : md_find_key_next;
    return md_find_key_none;
}

bool FindMarkdownText(const std::wstring& text, const std::wstring& query, int from, bool forward, bool matchCase,
                      int& outStart, int& outEnd, bool* wrapped)
{
    if (wrapped != NULL)
        *wrapped = false;
    if (query.empty() || query.size() > text.size())
        return false;
    int start = 0;
    if (!FindFolded(Folded(text, matchCase), Folded(query, matchCase), from, forward, start, wrapped))
        return false;
    outStart = start;
    outEnd = start + static_cast<int>(query.size());
    return true;
}

int CountMarkdownMatches(const std::wstring& text, const std::wstring& query, bool matchCase)
{
    if (query.empty())
        return 0;
    const std::wstring hay = Folded(text, matchCase);
    const std::wstring needle = Folded(query, matchCase);
    int count = 0;
    for (size_t at = hay.find(needle); at != std::wstring::npos; at = hay.find(needle, at + needle.size()))
        ++count;
    return count;
}

int ReplaceAllMarkdownText(const std::wstring& text, const std::wstring& query, const std::wstring& replacement,
                           bool matchCase, balloonchatwnd::MarkdownEditResult& out)
{
    out = MakeResult(text, text, 0, 0);
    if (query.empty())
        return 0;
    const std::wstring hay = Folded(text, matchCase);
    const std::wstring needle = Folded(query, matchCase);
    std::wstring newText;
    newText.reserve(text.size());
    int count = 0;
    size_t copied = 0;
    for (size_t at = hay.find(needle); at != std::wstring::npos; at = hay.find(needle, at + needle.size()))
    {
        newText.append(text, copied, at - copied);
        newText += replacement;
        copied = at + needle.size();
        ++count;
    }
    if (count == 0)
        return 0;
    //光标停在最后一处替换之后
    const int caret = static_cast<int>(newText.size());
    newText.append(text, copied, std::wstring::npos);
    out = MakeResult(text, newText, caret, caret);
    return count;
}

bool ReplaceCurrentMarkdownText(const std::wstring& text, int selStart, int selEnd, const std::wstring& query,
                                const std::wstring& replacement, bool matchCase,
                                balloonchatwnd::MarkdownEditResult& out, bool* foundNext)
{
    const int len = static_cast<int>(text.size());
    const int s = ClampIndex((std::min)(selStart, selEnd), len);
    const int e = ClampIndex((std::max)(selStart, selEnd), len);
    out = MakeResult(text, text, s, e);
    if (foundNext != NULL)
        *foundNext = false;
    if (query.empty())
        return false;

    //选区正好是一个匹配：替换它，再从替换词之后找下一个
    const bool selectionMatches = (e - s == static_cast<int>(query.size()))
                                  && Folded(text.substr(s, e - s), matchCase) == Folded(query, matchCase);
    std::wstring newText = text;
    int searchFrom = s;
    if (selectionMatches)
    {
        newText = text.substr(0, s) + replacement + text.substr(e);
        searchFrom = s + static_cast<int>(replacement.size());
    }

    int start = 0;
    int end = 0;
    const bool found = FindMarkdownText(newText, query, searchFrom, true, matchCase, start, end, NULL);
    if (foundNext != NULL)
        *foundNext = found;
    if (found)
        out = MakeResult(text, newText, start, end);
    else if (selectionMatches)
        out = MakeResult(text, newText, searchFrom, searchFrom);
    return selectionMatches;
}

} // namespace balloonmd
