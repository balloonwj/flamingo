/**
 *  语音字幕的字幕条切分、自动替换表与识别结果装配，实现见 CaptionSegmenter.h。
 *
 *  balloonwj@qq.com   2026-10-06
 */
#include "CaptionSegmenter.h"

#include <algorithm>

#include "CaptionJson.h"
#include "CaptionText.h"
#include "CaptionTrack.h"

namespace caption {

namespace {

//断开位置的搜索窗口：以「目标宽度」为 100，窗口从 60 到 130（超过每段上限时以上限为准）
const int kBreakWindowLowPercent = 60;
const int kBreakWindowHighPercent = 130;
const int kPercent = 100;
//结束时刻缺失时，按每个显示宽度单位 100 毫秒估算这句话的时长
const int64_t kEstimatedMsPerUnit = 100;
//UTF-16 低位代理的范围：断开位置不能落在代理对中间
const wchar_t kLowSurrogateFirst = 0xDC00;
const wchar_t kLowSurrogateLast = 0xDFFF;

//断开位置的优先级：数字越大越优先
enum BreakRank
{
    break_rank_none     = 0,   //不能在这里断开
    break_rank_space    = 1,   //空格处
    break_rank_mid      = 2,   //句中停顿标点之后
    break_rank_sentence = 3,   //句末标点之后
};

bool IsAsciiAlnum(wchar_t ch)
{
    return (ch >= L'0' && ch <= L'9') || (ch >= L'a' && ch <= L'z') || (ch >= L'A' && ch <= L'Z');
}

bool IsLowSurrogate(wchar_t ch)
{
    return ch >= kLowSurrogateFirst && ch <= kLowSurrogateLast;
}

int CharUnits(wchar_t ch)
{
    if (ch == L'\n' || IsLowSurrogate(ch))
        return 0;
    return IsWideChar(ch) ? kWideCharUnits : kNarrowCharUnits;
}

int64_t CeilDiv(int64_t a, int64_t b)
{
    return (a + b - 1) / b;
}

//在第 j 个字符之前（即 text[j-1] 之后）断开的优先级
int RankAt(const std::wstring& text, size_t j)
{
    const wchar_t prev = text[j - 1];
    const wchar_t next = j < text.size() ? text[j] : 0;
    //半角标点后面紧跟字母数字时（如 3.5、a.b）不算断开处
    const bool asciiPunctGlued = prev < 0x80 && IsAsciiAlnum(next);
    if (IsSentenceEndPunct(prev) && !asciiPunctGlued)
        return break_rank_sentence;
    if (IsBreakPunct(prev) && !asciiPunctGlued)
        return break_rank_mid;
    if (prev == L' ' || prev == L'\n')
        return break_rank_space;
    return break_rank_none;
}

//从 from 起，累计宽度首次达到 units 时的下标（不含该下标的字符之后），至多为 text.size()
size_t IndexAtUnits(const std::vector<int>& cum, size_t from, int units)
{
    size_t j = from + 1;
    while (j < cum.size() - 1 && cum[j] - cum[from] < units)
        ++j;
    return j;
}

//规则按 m_from 长度从长到短排序
bool LongerFromFirst(const CaptionReplaceRule* a, const CaptionReplaceRule* b)
{
    return a->m_from.size() > b->m_from.size();
}

bool EqualsIgnoreAsciiCase(const std::wstring& a, const std::wstring& b)
{
    if (a.size() != b.size())
        return false;
    return FindInText(a, b, 0, false, false) == 0;
}

} // namespace

CaptionSegmentRules::CaptionSegmentRules()
    : m_maxLineUnits(kCaptionMaxLineUnits)
    , m_maxLines(kCaptionMaxLines)
    , m_maxCueMs(kCaptionMaxCueMs)
    , m_minCueMs(kCaptionMinSegmentMs)
{
}

CaptionReplaceRule::CaptionReplaceRule()
    : m_matchCase(false)
    , m_wholeWord(true)
{
}

bool CaptionReplaceTable::Add(const CaptionReplaceRule& rule)
{
    if (rule.m_from.empty())
        return false;
    for (size_t i = 0; i < m_rules.size(); ++i)
    {
        if (EqualsIgnoreAsciiCase(m_rules[i].m_from, rule.m_from))
        {
            m_rules[i] = rule;
            return true;
        }
    }
    m_rules.push_back(rule);
    return true;
}

bool CaptionReplaceTable::Remove(const std::wstring& from)
{
    for (size_t i = 0; i < m_rules.size(); ++i)
    {
        if (EqualsIgnoreAsciiCase(m_rules[i].m_from, from))
        {
            m_rules.erase(m_rules.begin() + static_cast<ptrdiff_t>(i));
            return true;
        }
    }
    return false;
}

const std::vector<CaptionReplaceRule>& CaptionReplaceTable::Rules() const
{
    return m_rules;
}

std::wstring CaptionReplaceTable::Apply(const std::wstring& text, int* count) const
{
    std::vector<const CaptionReplaceRule*> order;
    for (size_t i = 0; i < m_rules.size(); ++i)
        order.push_back(&m_rules[i]);
    std::stable_sort(order.begin(), order.end(), LongerFromFirst);
    std::wstring out = text;
    int total = 0;
    for (size_t i = 0; i < order.size(); ++i)
    {
        int n = 0;
        out = ReplaceInText(out, order[i]->m_from, order[i]->m_to, order[i]->m_matchCase, order[i]->m_wholeWord, &n);
        total += n;
    }
    if (count != NULL)
        *count = total;
    return out;
}

std::string CaptionReplaceTable::ToJson() const
{
    Json::Value arr(Json::arrayValue);
    for (size_t i = 0; i < m_rules.size(); ++i)
    {
        Json::Value r(Json::objectValue);
        r["from"] = WideToUtf8(m_rules[i].m_from);
        r["to"] = WideToUtf8(m_rules[i].m_to);
        r["matchcase"] = m_rules[i].m_matchCase ? 1 : 0;
        r["wholeword"] = m_rules[i].m_wholeWord ? 1 : 0;
        arr.append(r);
    }
    return WriteJson(arr, false);
}

bool CaptionReplaceTable::FromJson(const std::string& json)
{
    Json::Value root;
    if (!ParseJson(json, root) || !root.isArray())
        return false;
    std::vector<CaptionReplaceRule> rules;
    for (Json::ArrayIndex i = 0; i < root.size(); ++i)
    {
        const Json::Value& r = root[i];
        if (!r.isObject())
            return false;
        CaptionReplaceRule rule;
        rule.m_from = Utf8ToWide(JsonString(r["from"]));
        rule.m_to = Utf8ToWide(JsonString(r["to"]));
        rule.m_matchCase = JsonInt64(r["matchcase"], 0) != 0;
        rule.m_wholeWord = JsonInt64(r["wholeword"], 1) != 0;
        if (!rule.m_from.empty())
            rules.push_back(rule);
    }
    m_rules = rules;
    return true;
}

std::vector<std::wstring> CaptionSegmenter::SplitText(const std::wstring& text, int maxUnits, int pieces)
{
    std::vector<std::wstring> out;
    const size_t n = text.size();
    if (n == 0 || maxUnits <= 0)
        return out;
    std::vector<int> cum(n + 1, 0);
    for (size_t i = 0; i < n; ++i)
        cum[i + 1] = cum[i] + CharUnits(text[i]);

    size_t pos = 0;
    int64_t planned = pieces > 0 ? pieces : 1;
    while (pos < n)
    {
        const int remaining = cum[n] - cum[pos];
        if (remaining <= 0)
            break;
        int64_t need = CeilDiv(remaining, maxUnits);
        if (planned > need)
            need = planned;
        if (need <= 1)
        {
            const std::wstring last = NormalizeText(text.substr(pos));
            if (!last.empty())
                out.push_back(last);
            break;
        }
        const int target = static_cast<int>(CeilDiv(remaining, need));

        //每段的硬上限：宽度不超过 maxUnits 的最远位置（至少前进一个字符）
        size_t hard = pos + 1;
        while (hard < n && cum[hard + 1] - cum[pos] <= maxUnits)
            ++hard;
        size_t lo = IndexAtUnits(cum, pos, std::max(1, target * kBreakWindowLowPercent / kPercent));
        size_t hi = IndexAtUnits(cum, pos, target * kBreakWindowHighPercent / kPercent);
        if (hi > hard)
            hi = hard;
        if (lo > hi)
            lo = hi;

        //在窗口里找优先级最高、位置最靠后的断开处
        size_t end = 0;
        int bestRank = break_rank_none;
        for (size_t j = hi; j >= lo && j > pos; --j)
        {
            const int rank = RankAt(text, j);
            if (rank > bestRank)
            {
                bestRank = rank;
                end = j;
            }
        }
        if (bestRank == break_rank_none)
        {
            end = IndexAtUnits(cum, pos, target);
            if (end > hard)
                end = hard;
            //不拆开英文单词：落在单词中间时退回到词首，把整个单词留给下一段；
            //这一段从头到这里都是同一个单词（超长单词）时，才往后推到词尾，最多推到硬上限
            if (end < n && IsAsciiAlnum(text[end - 1]) && IsAsciiAlnum(text[end]))
            {
                size_t ws = end;
                while (ws > pos && IsAsciiAlnum(text[ws - 1]))
                    --ws;
                if (ws > pos)
                {
                    end = ws;
                }
                else
                {
                    size_t we = end;
                    while (we < hard && IsAsciiAlnum(text[we]))
                        ++we;
                    end = we;
                }
            }
        }
        if (end < n && IsLowSurrogate(text[end]))
            ++end;

        const std::wstring piece = NormalizeText(text.substr(pos, end - pos));
        if (!piece.empty())
            out.push_back(piece);
        pos = end;
        planned = need - 1;
    }
    return out;
}

namespace {

//超过最长时长的段再细分的最大递归层数
const int kMaxResplitDepth = 3;

//把 [startMs, endMs) 这一段时间里的文字切开并分配时间；某段分到的时长仍超过上限时对它再细分
void SplitRange(const std::wstring& text, int64_t startMs, int64_t endMs, const CaptionSegmentRules& rules,
                int depth, std::vector<CaptionCue>& out, const std::wstring& speaker, int source)
{
    const int64_t duration = endMs - startMs;
    const int maxUnits = rules.m_maxLineUnits * rules.m_maxLines;
    int64_t pieces = rules.m_maxCueMs > 0 ? CeilDiv(duration, rules.m_maxCueMs) : 1;
    if (rules.m_minCueMs > 0 && pieces > 1 && duration / pieces < rules.m_minCueMs)
        pieces = std::max<int64_t>(1, duration / rules.m_minCueMs);
    //一行以内的短句不按时长切分（如中间停顿较长的「好的」），只按宽度切
    if (DisplayUnits(text) <= rules.m_maxLineUnits)
        pieces = 1;
    const std::vector<std::wstring> parts = CaptionSegmenter::SplitText(text, maxUnits, static_cast<int>(pieces));

    int partsUnits = 0;
    for (size_t i = 0; i < parts.size(); ++i)
        partsUnits += std::max(1, DisplayUnits(parts[i]));
    int before = 0;
    for (size_t i = 0; i < parts.size(); ++i)
    {
        const int units = std::max(1, DisplayUnits(parts[i]));
        const int64_t s = startMs + duration * before / partsUnits;
        before += units;
        const int64_t e = (i + 1 == parts.size()) ? endMs : startMs + duration * before / partsUnits;
        if (e <= s)
            continue;
        //这一段分到的时长仍超过上限、且能再分（不止一个字符）时再细分一次
        if (rules.m_maxCueMs > 0 && e - s > rules.m_maxCueMs && depth < kMaxResplitDepth && parts.size() > 1
            && parts[i].size() > 1)
        {
            SplitRange(parts[i], s, e, rules, depth + 1, out, speaker, source);
            continue;
        }
        CaptionCue c;
        c.m_startMs = s;
        c.m_endMs = e;
        c.m_speaker = speaker;
        c.m_text = parts[i];
        c.m_source = source;
        c.m_edited = false;
        out.push_back(c);
    }
}

} // namespace

std::vector<CaptionCue> CaptionSegmenter::Split(const std::wstring& text, int64_t startMs, int64_t endMs,
                                                const std::wstring& speaker, int source,
                                                const CaptionSegmentRules& rules)
{
    std::vector<CaptionCue> cues;
    const std::wstring t = NormalizeText(text);
    if (t.empty())
        return cues;
    const int totalUnits = DisplayUnits(t);
    if (endMs <= startMs)
        endMs = startMs + std::max(1, totalUnits) * kEstimatedMsPerUnit;
    SplitRange(t, startMs, endMs, rules, 0, cues, speaker, source);
    return cues;
}

CaptionAssembler::CaptionAssembler(const CaptionSegmentRules& rules, const CaptionReplaceTable* replace)
    : m_rules(rules)
    , m_replace(replace)
{
}

int CaptionAssembler::OnFinal(CaptionTrack& track, const std::wstring& text, int64_t startMs, int64_t endMs,
                              const std::wstring& speaker, int source)
{
    std::wstring t = NormalizeText(text);
    if (m_replace != NULL)
        t = m_replace->Apply(t, NULL);
    const std::wstring key = std::to_wstring(static_cast<long long>(source)) + L"|" + speaker;

    //行首标点：并回同一路的上一条（间隔不太长时），否则丢弃
    const size_t lead = LeadingPunctLength(t);
    if (lead > 0)
    {
        std::wstring punct;
        for (size_t i = 0; i < lead; ++i)
        {
            if (t[i] != L' ')
                punct.push_back(t[i]);
        }
        std::map<std::wstring, int64_t>::const_iterator it = m_lastCue.find(key);
        if (it != m_lastCue.end())
        {
            const int idx = track.IndexOfId(it->second);
            if (idx >= 0 && startMs - track.At(static_cast<size_t>(idx)).m_endMs <= kCaptionPunctMergeGapMs)
                track.AppendText(static_cast<size_t>(idx), punct);
        }
        t = NormalizeText(t.substr(lead));
    }
    if (t.empty())
        return 0;

    const std::vector<CaptionCue> cues = CaptionSegmenter::Split(t, startMs, endMs, speaker, source, m_rules);
    int added = 0;
    for (size_t i = 0; i < cues.size(); ++i)
    {
        const int64_t id = track.Add(cues[i]);
        if (id != 0)
        {
            m_lastCue[key] = id;
            ++added;
        }
    }
    return added;
}

void CaptionAssembler::Reset()
{
    m_lastCue.clear();
}

} // namespace caption
