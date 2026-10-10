/**
 *  点赞与表情回应汇总运算的实现，见 ThreadReactions.h。
 *  balloonwj@qq.com   2026-10-07
 */
#include "ThreadReactions.h"

#include <algorithm>

namespace threadgroup {

namespace {

//显示顺序：「赞」在最前，其余按互动编号升序
bool ReactionOrder(const ThreadReaction& a, const ThreadReaction& b)
{
    if (a.m_reaction == kReactionLike || b.m_reaction == kReactionLike)
        return a.m_reaction == kReactionLike && b.m_reaction != kReactionLike;
    return a.m_reaction < b.m_reaction;
}

//找某种互动所在的下标；没有返回 -1
int FindReaction(const std::vector<ThreadReaction>& list, int reaction)
{
    for (size_t i = 0; i < list.size(); ++i)
    {
        if (list[i].m_reaction == reaction)
            return static_cast<int>(i);
    }
    return -1;
}

} // namespace

void ToggleReaction(std::vector<ThreadReaction>& list, int reaction, bool on)
{
    const int idx = FindReaction(list, reaction);
    if (on)
    {
        if (idx < 0)
        {
            ThreadReaction r;
            r.m_reaction = reaction;
            r.m_count = 1;
            r.m_mine = true;
            list.push_back(r);
        }
        else if (!list[idx].m_mine)
        {
            list[idx].m_mine = true;
            ++list[idx].m_count;
        }
    }
    else if (idx >= 0 && list[idx].m_mine)
    {
        list[idx].m_mine = false;
        --list[idx].m_count;
    }
    SortReactions(list);
}

bool HasMyReaction(const std::vector<ThreadReaction>& list, int reaction)
{
    const int idx = FindReaction(list, reaction);
    return idx >= 0 && list[idx].m_mine;
}

int ReactionCount(const std::vector<ThreadReaction>& list, int reaction)
{
    const int idx = FindReaction(list, reaction);
    return idx >= 0 ? list[idx].m_count : 0;
}

void ApplyReactionNotify(std::vector<ThreadReaction>& list, const std::vector<ThreadReaction>& summary, int reaction,
                         bool on, bool operatorIsMe)
{
    std::vector<ThreadReaction> merged = summary;
    for (size_t i = 0; i < merged.size(); ++i)
    {
        if (operatorIsMe && merged[i].m_reaction == reaction)
            merged[i].m_mine = on;
        else
            merged[i].m_mine = HasMyReaction(list, merged[i].m_reaction);
    }
    list.swap(merged);
    SortReactions(list);
}

void SortReactions(std::vector<ThreadReaction>& list)
{
    std::vector<ThreadReaction> kept;
    for (size_t i = 0; i < list.size(); ++i)
    {
        if (list[i].m_count > 0)
            kept.push_back(list[i]);
    }
    std::stable_sort(kept.begin(), kept.end(), ReactionOrder);
    list.swap(kept);
}

void ReactorListSplit(int listed, int total, int maxNames, int& shown, int& rest)
{
    shown = listed < maxNames ? listed : maxNames;
    if (shown < 0)
        shown = 0;
    //总人数比列出的名字多时写「等 N 人」，N 为总人数
    rest = (total > shown) ? total : 0;
}

} // namespace threadgroup
