/**
 *  SubtitleTextFit.cpp
 *
 *  字幕文字折行与截断的实现，说明见 SubtitleTextFit.h。
 *
 *  做法：把内容切成「单元」—— 一个空格、一个拉丁单词（连续的拉丁字母、数字与半角标点）、一个代理对，
 *  或其它任意一个字符（汉字、全角标点等）—— 逐个往当前行上加，加上后超宽就换行。当前行还没有任何
 *  内容单元（行首，或第一行只有说话人前缀）时放不下的单元按字符拆开，否则整个挪到下一行。到了最后
 *  一行还要换行时，就在这一行末尾截断并加省略号。
 *
 *  balloonwj@qq.com   2026-10-05
 */

#include "stdafx.h"

#include "SubtitleTextFit.h"

#include "../../Common/I18n/I18n.h"   // FormatPattern：把名字代入说话人模板

namespace subtitle {

namespace {

const wchar_t kHighSurrogateFirst = 0xD800;
const wchar_t kHighSurrogateLast = 0xDBFF;
const wchar_t kLowSurrogateFirst = 0xDC00;
const wchar_t kLowSurrogateLast = 0xDFFF;
//半角可打印字符的范围：空格之后到 ASCII 结束
const wchar_t kAsciiEnd = 0x80;
//能与相邻字符连成单词的非 ASCII 字母区段：拉丁字母补充与扩展、希腊字母、西里尔字母
const wchar_t kLatinExtFirst = 0x00C0;
const wchar_t kLatinExtLast = 0x024F;
const wchar_t kGreekFirst = 0x0370;
const wchar_t kGreekLast = 0x03FF;
const wchar_t kCyrillicFirst = 0x0400;
const wchar_t kCyrillicLast = 0x04FF;

bool IsHighSurrogate(wchar_t c)
{
    return c >= kHighSurrogateFirst && c <= kHighSurrogateLast;
}

bool IsLowSurrogate(wchar_t c)
{
    return c >= kLowSurrogateFirst && c <= kLowSurrogateLast;
}

bool IsSpace(wchar_t c)
{
    return c == L' ' || c == L'\t' || c == L'\r' || c == L'\n';
}

//能与相邻字符连成一个单词（折行时不拆开）的字符
bool IsWordChar(wchar_t c)
{
    if (c < kAsciiEnd)
        return !IsSpace(c) && c > L' ';
    if (c >= kLatinExtFirst && c <= kLatinExtLast)
        return true;
    if (c >= kGreekFirst && c <= kGreekLast)
        return true;
    if (c >= kCyrillicFirst && c <= kCyrillicLast)
        return true;
    return false;
}

//从 pos 起的下一个单元的长度（UTF-16 单元数，至少为 1）
size_t TokenLength(const std::wstring& s, size_t pos)
{
    const wchar_t c = s[pos];
    if (IsSpace(c))
        return 1;
    if (IsWordChar(c))
    {
        size_t end = pos + 1;
        while (end < s.size() && IsWordChar(s[end]))
            ++end;
        return end - pos;
    }
    if (IsHighSurrogate(c) && pos + 1 < s.size() && IsLowSurrogate(s[pos + 1]))
        return 2;
    return 1;
}

//从 pos 起的一个字符的长度：代理对为 2，其它为 1
size_t CharLength(const std::wstring& s, size_t pos)
{
    if (IsHighSurrogate(s[pos]) && pos + 1 < s.size() && IsLowSurrogate(s[pos + 1]))
        return 2;
    return 1;
}

//调用测量回调
int MeasureStr(MeasureTextFn measure, void* ctx, const std::wstring& s)
{
    if (s.empty())
        return 0;
    return measure(ctx, s.c_str(), (int)s.size());
}

//去掉行尾空格，但不动前 keep 个字符（说话人前缀）
void TrimTrailingSpaces(std::wstring& line, size_t keep)
{
    while (line.size() > keep && IsSpace(line[line.size() - 1]))
        line.erase(line.size() - 1);
}

//把一行截短到加上省略号也放得下，并加上省略号；前 keep 个字符（说话人前缀）不动
void Ellipsize(std::wstring& line, size_t keep, int maxWidth, MeasureTextFn measure, void* ctx)
{
    TrimTrailingSpaces(line, keep);
    const std::wstring ellipsis(1, kEllipsisChar);
    while (line.size() > keep && MeasureStr(measure, ctx, line + ellipsis) > maxWidth)
    {
        size_t cut = 1;
        //末尾是代理对时整对去掉
        if (line.size() >= keep + 2 && IsLowSurrogate(line[line.size() - 1]) &&
            IsHighSurrogate(line[line.size() - 2]))
        {
            cut = 2;
        }
        line.erase(line.size() - cut);
        TrimTrailingSpaces(line, keep);
    }
    line += ellipsis;
}

//pos 之后（含 pos）是否还有非空白字符
bool HasContentFrom(const std::wstring& s, size_t pos)
{
    for (size_t i = pos; i < s.size(); ++i)
    {
        if (!IsSpace(s[i]))
            return true;
    }
    return false;
}

} // anonymous namespace

FitResult FitSubtitleText(const std::wstring& speakerPrefix, const std::wstring& content, int maxWidthPx,
                          int maxLines, MeasureTextFn measure, void* ctx)
{
    FitResult result;
    const int maxWidth = (maxWidthPx > 0) ? maxWidthPx : 1;
    const int lines = (maxLines >= 1) ? maxLines : 1;

    //当前行的文字、其中属于说话人前缀的长度，以及当前行是否已经放了内容单元
    std::wstring cur = speakerPrefix;
    size_t curSpeaker = speakerPrefix.size();
    bool curHasContent = false;

    size_t pos = 0;
    const size_t n = content.size();
    while (pos < n)
    {
        const size_t len = TokenLength(content, pos);
        const bool space = IsSpace(content[pos]);
        //行首（还没有内容单元）的空格丢掉
        if (space && !curHasContent)
        {
            pos += len;
            continue;
        }
        const std::wstring candidate = cur + content.substr(pos, len);
        if (MeasureStr(measure, ctx, candidate) <= maxWidth)
        {
            cur = candidate;
            curHasContent = true;
            pos += len;
            continue;
        }

        //放不下。空格放不下：在这里换行，空格丢掉；后面已经没有内容时直接结束
        size_t nextPos = pos;
        if (space)
        {
            nextPos = pos + len;
            if (!HasContentFrom(content, nextPos))
            {
                pos = n;
                break;
            }
        }
        else if (!curHasContent)
        {
            //行首就放不下的单元（比整行还宽的单词、或说话人前缀之后剩余的位置不够）：按字符拆，至少放一个字符
            size_t take = CharLength(content, pos);
            while (pos + take < pos + len)
            {
                const size_t step = CharLength(content, pos + take);
                if (MeasureStr(measure, ctx, cur + content.substr(pos, take + step)) > maxWidth)
                    break;
                take += step;
            }
            cur += content.substr(pos, take);
            curHasContent = true;
            nextPos = pos + take;
            if (!HasContentFrom(content, nextPos))
            {
                pos = n;
                break;
            }
        }

        //需要换行。当前已是最后一行：截断并加省略号
        if ((int)result.m_lines.size() == lines - 1)
        {
            Ellipsize(cur, curSpeaker, maxWidth, measure, ctx);
            FitLine line;
            line.m_text = cur;
            line.m_speakerLen = (int)curSpeaker;
            result.m_lines.push_back(line);
            result.m_truncated = true;
            return result;
        }
        TrimTrailingSpaces(cur, curSpeaker);
        FitLine line;
        line.m_text = cur;
        line.m_speakerLen = (int)curSpeaker;
        result.m_lines.push_back(line);
        cur.clear();
        curSpeaker = 0;
        curHasContent = false;
        pos = nextPos;
    }

    TrimTrailingSpaces(cur, curSpeaker);
    FitLine last;
    last.m_text = cur;
    last.m_speakerLen = (int)curSpeaker;
    result.m_lines.push_back(last);
    return result;
}

FitResult FitSubtitleText(const std::wstring& originPrefix, const std::wstring& speakerPrefix,
                          const std::wstring& content, int maxWidthPx, int maxLines, MeasureTextFn measure, void* ctx)
{
    //来源标注与说话人前缀合成一段前缀交给上面的排版（两者都整体放在第一行、截断时都受保护），排完再拆成两段
    FitResult result = FitSubtitleText(originPrefix + speakerPrefix, content, maxWidthPx, maxLines, measure, ctx);
    if (originPrefix.empty() || result.m_lines.empty())
        return result;
    FitLine& first = result.m_lines[0];
    const int prefixLen = first.m_speakerLen;
    const int originLen = (int)originPrefix.size();
    first.m_originLen = (prefixLen < originLen) ? prefixLen : originLen;
    first.m_speakerLen = prefixLen - first.m_originLen;
    return result;
}

std::wstring FitSpeakerPrefix(const wchar_t* pattern, const std::wstring& name, int maxPx,
                              MeasureTextFn measure, void* ctx)
{
    const wchar_t* pat = (pattern != NULL) ? pattern : L"{0}";
    std::vector<std::wstring> args(1, name);
    const std::wstring full = i18n::FormatPattern(pat, args);
    if (MeasureStr(measure, ctx, full) <= maxPx)
        return full;

    //从尾部逐个去掉名字里的字符（代理对整对去掉），直到「截短的名字 + 省略号」套进模板后放得下
    const std::wstring ellipsis(1, kEllipsisChar);
    size_t keep = name.size();
    while (keep > 0)
    {
        --keep;
        if (keep > 0 && IsHighSurrogate(name[keep - 1]))
            --keep;
        args[0] = name.substr(0, keep) + ellipsis;
        const std::wstring candidate = i18n::FormatPattern(pat, args);
        if (MeasureStr(measure, ctx, candidate) <= maxPx)
            return candidate;
    }
    args[0] = ellipsis;
    return i18n::FormatPattern(pat, args);
}

} // namespace subtitle
