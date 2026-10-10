/**
 *  NotifyManageModel.cpp
 *
 *  批量管理窗口「消息提醒管理」的纯逻辑实现。Flamingo.vcxproj 与 FlamingoUnit.vcxproj 两个工程
 *  逐字共用；在 Flamingo.vcxproj 里本文件需标记 PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-10-03
 */

#include "NotifyManageModel.h"
#include "../../net/Protocol.h"   // conversation_single / notify_mode_block / notify_mode_starred

#include <wctype.h>   // towlower：搜索不区分大小写

namespace balloonnotifymode {

namespace {

// 转成小写（只处理字母，中文不受影响）
std::wstring ToLower(const std::wstring& s)
{
    std::wstring out(s);
    for (size_t i = 0; i < out.size(); ++i)
    {
        out[i] = (wchar_t)towlower(out[i]);
    }
    return out;
}

// 把一个来源里匹配的行追加到 out，跳过 seen 里已有的会话；到上限时置 truncated 并返回 false
bool AppendMatches(const std::vector<ManageItem>& src, const std::wstring& filter,
                   std::set<std::pair<int, int64_t> >& seen,
                   std::vector<ManageItem>& out, bool& truncated)
{
    for (size_t i = 0; i < src.size(); ++i)
    {
        const ManageItem& item = src[i];
        if (!MatchesFilter(item, filter))
            continue;
        if (!seen.insert(std::make_pair(item.m_ctype, item.m_cid)).second)
            continue;
        if (out.size() >= kNotifyManageMaxRows)
        {
            truncated = true;
            return false;
        }
        out.push_back(item);
    }
    return true;
}

} // anonymous namespace

bool MatchesFilter(const ManageItem& item, const std::wstring& filter)
{
    if (filter.empty())
        return true;
    const std::wstring key = ToLower(filter);
    if (ToLower(item.m_name).find(key) != std::wstring::npos)
        return true;
    return !item.m_extra.empty() && ToLower(item.m_extra).find(key) != std::wstring::npos;
}

std::vector<ManageItem> BuildVisibleItems(const ManagePools& pools, int ctype,
                                          const std::wstring& filter, bool& truncated)
{
    truncated = false;
    std::vector<ManageItem> out;
    std::set<std::pair<int, int64_t> > seen;
    switch (ctype)
    {
    //群聊页：我加入的全部群
    case conversation_group:
        AppendMatches(pools.m_groups, filter, seen, out, truncated);
        break;

    //单聊页：聊过的人与设置过模式的人；有搜索词时再接上通讯录里匹配的同事
    case conversation_single:
        if (AppendMatches(pools.m_singleBase, filter, seen, out, truncated) && !filter.empty())
            AppendMatches(pools.m_contacts, filter, seen, out, truncated);
        break;

    //不认识的页签：什么也不列
    default:
        break;
    }
    return out;
}

void ManageSelection::Toggle(int ctype, int64_t cid)
{
    SetChecked(ctype, cid, !IsChecked(ctype, cid));
}

void ManageSelection::SetChecked(int ctype, int64_t cid, bool checked)
{
    if (checked)
        m_checked.insert(std::make_pair(ctype, cid));
    else
        m_checked.erase(std::make_pair(ctype, cid));
}

bool ManageSelection::IsChecked(int ctype, int64_t cid) const
{
    return m_checked.count(std::make_pair(ctype, cid)) != 0;
}

void ManageSelection::SetAll(const std::vector<ManageItem>& items, bool checked)
{
    for (size_t i = 0; i < items.size(); ++i)
    {
        SetChecked(items[i].m_ctype, items[i].m_cid, checked);
    }
}

int ManageSelection::StateOf(const std::vector<ManageItem>& items) const
{
    size_t checked = 0;
    for (size_t i = 0; i < items.size(); ++i)
    {
        if (IsChecked(items[i].m_ctype, items[i].m_cid))
            ++checked;
    }
    if (checked == 0)
        return kManageCheckNone;
    return (checked == items.size()) ? kManageCheckAll : kManageCheckSome;
}

int ManageSelection::CountOf(int ctype) const
{
    int n = 0;
    for (std::set<std::pair<int, int64_t> >::const_iterator it = m_checked.begin(); it != m_checked.end(); ++it)
    {
        if (it->first == ctype)
            ++n;
    }
    return n;
}

void ManageSelection::Clear()
{
    m_checked.clear();
}

std::vector<int64_t> ManageSelection::CheckedIds(int ctype) const
{
    //set 按 (ctype, cid) 排序，同一 ctype 内即按 cid 升序
    std::vector<int64_t> ids;
    for (std::set<std::pair<int, int64_t> >::const_iterator it = m_checked.begin(); it != m_checked.end(); ++it)
    {
        if (it->first == ctype)
            ids.push_back(it->second);
    }
    return ids;
}

std::vector<net::NotifyModeEntry> BuildApplyEntries(const ManageSelection& sel, int ctype, int targetMode,
                                                    const balloonmain::NotifyModeModel& modes, int& skipped)
{
    skipped = 0;
    std::vector<net::NotifyModeEntry> entries;
    const std::vector<int64_t> ids = sel.CheckedIds(ctype);
    for (size_t i = 0; i < ids.size(); ++i)
    {
        //模式没变的不必提交；单聊没有屏蔽，也没有「只提醒 @我和关注的人」
        if (modes.GetMode(ctype, ids[i]) == targetMode
            || (ctype == conversation_single
                && (targetMode == notify_mode_block || targetMode == notify_mode_mention_follow)))
        {
            ++skipped;
            continue;
        }
        net::NotifyModeEntry e;
        e.m_ctype = ctype;
        e.m_cid   = ids[i];
        e.m_mode  = targetMode;
        entries.push_back(e);
    }
    return entries;
}

bool WouldExceedStarredLimit(const balloonmain::NotifyModeModel& modes,
                             const std::vector<net::NotifyModeEntry>& entries)
{
    //在一份拷贝上按条目改完再数，改掉原有特别关注的条目也一并算进去
    balloonmain::NotifyModeModel after = modes;
    after.ApplyEntries(entries);
    return after.CountMode(notify_mode_starred) > balloonmain::kNotifyModeStarredMax;
}

} // namespace balloonnotifymode
