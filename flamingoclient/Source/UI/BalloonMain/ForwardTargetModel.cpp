/**
 *  转发目标选择窗纯逻辑模型的实现，见 ForwardTargetModel.h。
 *  balloonwj@qq.com   2026-10-01
 */
#include "ForwardTargetModel.h"

#include "../../net/Protocol.h"            // conversation_single / conversation_group
#include "../BalloonSearch/ContactMatch.h" // contactsearch::Match（与主面板搜索同一套匹配规则）

namespace balloonmain {

namespace {

//去掉首尾空白（空格、制表符、换行）
std::wstring TrimQuery(const std::wstring& s)
{
    const wchar_t* const kSpaces = L" \t\r\n";
    const std::wstring::size_type first = s.find_first_not_of(kSpaces);
    if (first == std::wstring::npos)
    {
        return std::wstring();
    }
    const std::wstring::size_type last = s.find_last_not_of(kSpaces);
    return s.substr(first, last - first + 1);
}

//判断会话类型是否可以作为转发目标
bool IsForwardableConvType(int ctype)
{
    return ctype == conversation_single || ctype == conversation_group;
}

} // anonymous namespace

ForwardTargetSelection::ForwardTargetSelection(int64_t selfUserId)
    : m_selfUserId(selfUserId)
{
}

ForwardToggleResult ForwardTargetSelection::Toggle(const ForwardTarget& target)
{
    const int index = IndexOf(target.m_ctype, target.m_cid);
    if (index >= 0)
    {
        m_selected.erase(m_selected.begin() + index);
        return kForwardToggleRemoved;
    }
    if (!IsSelectable(target))
    {
        return kForwardToggleInvalid;
    }
    if (IsFull())
    {
        return kForwardToggleFull;
    }
    m_selected.push_back(target);
    return kForwardToggleAdded;
}

bool ForwardTargetSelection::Remove(int ctype, int64_t cid)
{
    const int index = IndexOf(ctype, cid);
    if (index < 0)
    {
        return false;
    }
    m_selected.erase(m_selected.begin() + index);
    return true;
}

bool ForwardTargetSelection::IsSelected(int ctype, int64_t cid) const
{
    return IndexOf(ctype, cid) >= 0;
}

bool ForwardTargetSelection::IsFull() const
{
    return m_selected.size() >= kForwardMaxTargets;
}

const std::vector<ForwardTarget>& ForwardTargetSelection::Selected() const
{
    return m_selected;
}

void ForwardTargetSelection::Clear()
{
    m_selected.clear();
}

bool ForwardTargetSelection::IsSelectable(const ForwardTarget& target) const
{
    if (!IsForwardableConvType(target.m_ctype) || target.m_cid <= 0)
    {
        return false;
    }
    //转发给自己服务端会以 error_code_bad_request 拒绝
    return !(target.m_ctype == conversation_single && target.m_cid == m_selfUserId);
}

int ForwardTargetSelection::IndexOf(int ctype, int64_t cid) const
{
    for (size_t i = 0; i < m_selected.size(); ++i)
    {
        if (m_selected[i].m_ctype == ctype && m_selected[i].m_cid == cid)
        {
            return (int)i;
        }
    }
    return -1;
}

std::vector<size_t> FilterForwardTargets(const std::vector<ForwardTarget>& candidates,
                                         const std::wstring& query)
{
    std::vector<size_t> hits;
    const std::wstring q = TrimQuery(query);
    for (size_t i = 0; i < candidates.size(); ++i)
    {
        //空查询不过滤；地区一项转发目标没有，传空串
        if (q.empty() || contactsearch::Match(q, candidates[i].m_name,
                                              candidates[i].m_account, std::wstring()).m_matched)
        {
            hits.push_back(i);
        }
    }
    return hits;
}

std::vector<ForwardTarget> BuildRecentForwardTargets(const std::vector<ForwardTarget>& conversations,
                                                     int64_t selfUserId)
{
    //能否作为目标的判断与 ForwardTargetSelection::IsSelectable 口径一致；这里整理的是
    //全部候选，不受选择上限约束，故单独逐项比较。会话条数不过几百，逐项判重的开销可以忽略
    std::vector<ForwardTarget> out;
    for (size_t i = 0; i < conversations.size(); ++i)
    {
        const ForwardTarget& t = conversations[i];
        if (!IsForwardableConvType(t.m_ctype) || t.m_cid <= 0)
        {
            continue;
        }
        if (t.m_ctype == conversation_single && t.m_cid == selfUserId)
        {
            continue;
        }

        bool duplicated = false;
        for (size_t k = 0; k < out.size(); ++k)
        {
            if (out[k].m_ctype == t.m_ctype && out[k].m_cid == t.m_cid)
            {
                duplicated = true;
                break;
            }
        }
        if (!duplicated)
        {
            out.push_back(t);
        }
    }
    return out;
}

} // namespace balloonmain
