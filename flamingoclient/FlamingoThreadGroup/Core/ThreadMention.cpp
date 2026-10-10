/**
 *  帖子群正文里 @ 片段的重扫与显示换算，见 ThreadMention.h。
 *  balloonwj@qq.com   2026-10-08
 */
#include "ThreadMention.h"

#include "ThreadText.h"
#include "ThreadMembers.h"   // ThreadMember：MentionPickList 的条目
#include "../../Source/UI/BalloonChatWnd/GroupMentionModel.h"   // RescanMentions / ConvertSpansForRender

namespace threadgroup {

namespace {

//线上位置换算表里「这个位置不能作为片段边界」的标记（落在表情标签或「//」转义的中间）
const int kNotBoundary = -1;

/**
 *  线上正文每个 UTF-16 下标（含末尾之后的位置）对应的显示字符位置，规则与 ParseWire 相同：
 *  表情标签整体算 1 个字符（不认识的表情算 unknownFaceChars 个），「//」转义算 1 个字符，其余每个下标算 1 个字符。
 */
std::vector<int> BuildWireToDisplayMap(const std::wstring& wire, const std::set<int>& knownFaceIds, int unknownFaceChars)
{
    const size_t n = wire.size();
    std::vector<int> map(n + 1, kNotBoundary);
    int disp = 0;
    size_t i = 0;
    while (i < n)
    {
        if (wire[i] == L'/')
        {
            int faceId = 0;
            size_t next = 0;
            //「//」只有紧跟一个表情标签时才是转义：两个 '/' 显示成一个，后面的标签按普通文字显示
            if (i + 1 < n && wire[i + 1] == L'/' && ParseFaceTagAt(wire, i + 1, faceId, next))
            {
                map[i] = disp;
                disp += 1;
                i += 2;
                continue;
            }
            if (ParseFaceTagAt(wire, i, faceId, next))
            {
                map[i] = disp;
                disp += (knownFaceIds.count(faceId) > 0) ? 1 : unknownFaceChars;
                i = next;
                continue;
            }
        }
        map[i] = disp;
        disp += 1;
        ++i;
    }
    map[n] = disp;
    return map;
}

} // namespace

std::vector<ThreadAtSpan> RescanAtSpans(const std::wstring& wire, const std::vector<ThreadAtCandidate>& candidates)
{
    std::vector<balloonchatwnd::MentionCandidate> cands;
    for (size_t i = 0; i < candidates.size(); ++i)
    {
        //userid 为 0 的是「@所有人」，与普通群的重扫一样参与匹配
        if (candidates[i].m_userId < kThreadAtAllUserId || candidates[i].m_name.empty())
            continue;
        balloonchatwnd::MentionCandidate c;
        c.m_uid = candidates[i].m_userId;
        c.m_displayName = WideToUtf8(candidates[i].m_name);
        cands.push_back(c);
    }
    std::vector<ThreadAtSpan> out;
    if (cands.empty())
        return out;
    const std::vector<balloonchatwnd::MentionSpan> spans = balloonchatwnd::RescanMentions(WideToUtf8(wire), cands);
    for (size_t i = 0; i < spans.size(); ++i)
    {
        ThreadAtSpan s;
        s.m_userId = spans[i].m_uid;
        s.m_off = spans[i].m_off;
        s.m_len = spans[i].m_len;
        out.push_back(s);
    }
    return out;
}

std::vector<ThreadAtCandidate> CandidatesFromSpans(const std::wstring& wire, const std::vector<ThreadAtSpan>& at)
{
    std::vector<ThreadAtCandidate> out;
    const std::string utf8 = WideToUtf8(wire);
    for (size_t i = 0; i < at.size(); ++i)
    {
        const ThreadAtSpan& s = at[i];
        //'@' 之后至少要有一个字节的名字；userid 为 0 的「@所有人」同样取出，编辑帖子时才能保留
        if (s.m_userId < kThreadAtAllUserId || s.m_off < 0 || s.m_len <= 1
            || static_cast<size_t>(s.m_off + s.m_len) > utf8.size())
            continue;
        if (utf8[static_cast<size_t>(s.m_off)] != '@')
            continue;
        ThreadAtCandidate cand;
        cand.m_userId = s.m_userId;
        cand.m_name = Utf8ToWide(utf8.substr(static_cast<size_t>(s.m_off) + 1, static_cast<size_t>(s.m_len) - 1));
        if (!cand.m_name.empty())
            out.push_back(cand);
    }
    return out;
}

bool WireRangeToDisplay(const std::wstring& wire, int wireStart, int wireEnd, const std::set<int>& knownFaceIds,
                        int unknownFaceChars, int& dispStart, int& dispEnd)
{
    dispStart = 0;
    dispEnd = 0;
    if (wireStart < 0 || wireEnd <= wireStart || static_cast<size_t>(wireEnd) > wire.size())
        return false;
    const std::vector<int> map = BuildWireToDisplayMap(wire, knownFaceIds, unknownFaceChars);
    if (map[static_cast<size_t>(wireStart)] == kNotBoundary || map[static_cast<size_t>(wireEnd)] == kNotBoundary)
        return false;
    dispStart = map[static_cast<size_t>(wireStart)];
    dispEnd = map[static_cast<size_t>(wireEnd)];
    return dispEnd > dispStart;
}

std::vector<ThreadAtDisplay> AtSpansToDisplay(const std::wstring& wire, const std::vector<ThreadAtSpan>& at,
                                              int64_t myUserId, const std::set<int>& knownFaceIds, int unknownFaceChars)
{
    std::vector<ThreadAtDisplay> out;
    if (at.empty() || wire.empty())
        return out;
    //字节位置先换算成线上正文的 UTF-16 下标（同时标好是不是 @ 到本人），再换算成显示位置
    std::vector<balloonchatwnd::MentionSpan> spans;
    for (size_t i = 0; i < at.size(); ++i)
    {
        balloonchatwnd::MentionSpan s;
        s.m_uid = at[i].m_userId;
        s.m_off = at[i].m_off;
        s.m_len = at[i].m_len;
        spans.push_back(s);
    }
    const std::vector<balloonchatwnd::RenderSpan> render =
        balloonchatwnd::ConvertSpansForRender(WideToUtf8(wire), spans, myUserId);
    const std::vector<int> map = BuildWireToDisplayMap(wire, knownFaceIds, unknownFaceChars);
    for (size_t i = 0; i < render.size(); ++i)
    {
        const int start = render[i].m_off;
        const int end = render[i].m_off + render[i].m_len;
        if (start < 0 || end <= start || static_cast<size_t>(end) >= map.size())
            continue;
        const int ds = map[static_cast<size_t>(start)];
        const int de = map[static_cast<size_t>(end)];
        if (ds == kNotBoundary || de == kNotBoundary || de <= ds)
            continue;
        ThreadAtDisplay d;
        d.m_start = ds;
        d.m_end = de;
        d.m_isMe = render[i].m_isMe;
        d.m_userId = render[i].m_uid;
        out.push_back(d);
    }
    return out;
}

std::wstring AtAllName()
{
    return Utf8ToWide(balloonchatwnd::kMentionAllNameUtf8);
}

bool HasAtAll(const std::vector<ThreadAtSpan>& at)
{
    for (size_t i = 0; i < at.size(); ++i)
    {
        if (at[i].m_userId == kThreadAtAllUserId)
            return true;
    }
    return false;
}

bool NeedsSpaceBeforeAt(int caretPos, wchar_t prevChar)
{
    if (caretPos <= 0)
        return false;
    return prevChar != L' ' && prevChar != L'\t' && prevChar != L'\r' && prevChar != L'\n';
}

int64_t AtUserAtChar(const std::vector<ThreadAtDisplay>& ranges, int charIndex)
{
    for (size_t i = 0; i < ranges.size(); ++i)
    {
        const ThreadAtDisplay& r = ranges[i];
        if (charIndex < r.m_start || charIndex >= r.m_end)
            continue;
        //「@所有人」没有具体的人，不弹菜单
        return (r.m_userId > 0) ? r.m_userId : 0;
    }
    return 0;
}

std::vector<ThreadMember> MentionPickList(const std::vector<ThreadMember>& order, bool canMentionAll)
{
    std::vector<ThreadMember> list;
    if (canMentionAll)
    {
        ThreadMember all;
        all.m_userId = kThreadAtAllUserId;
        all.m_name = AtAllName();
        list.push_back(all);
    }
    list.insert(list.end(), order.begin(), order.end());
    return list;
}

} // namespace threadgroup
