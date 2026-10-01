/**
 *  搜索候选缓存的实现。整体说明见 SearchCandidateCache.h。
 *
 *  本 .cpp 不 include Flamingo 的 stdafx.h —— 它要被 Flamingo.vcxproj 与
 *  FlamingoUnit.vcxproj 共用。在 Flamingo.vcxproj 里需标记 PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-10-01
 */

#include "SearchCandidateCache.h"

#include "../../Common/Util/EncodeUtil.h"
#include "../BalloonMain/ContactRemarkModel.h"   // ResolveContactDisplayName

namespace balloonsearch {

namespace {

// UTF-8 → UTF-16。与搜索下拉原先逐行现做的转换完全相同（同一个 EncodeUtil 函数、
// 同样按缓冲区转换并在第一个 NUL 处截止），保证缓存前后搜到的结果一致。
std::wstring Utf8ToWide(const std::string& utf8)
{
    if (utf8.empty())
    {
        return std::wstring();
    }
    std::vector<wchar_t> buf(utf8.size() + 1, 0);
    EncodeUtil::Utf8ToUnicode(utf8.c_str(), buf.data(), (int)buf.size());
    return std::wstring(buf.data());
}

// 联系人候选的快照与资料池里的联系人是否一致（只比参与匹配与取名的五项）。
bool ContactSnapshotMatches(const ContactCandidate& e, const net::ContactItem& c)
{
    return e.m_userid == c.m_userid
        && e.m_remark == c.m_remark
        && e.m_nickname == c.m_nickname
        && e.m_username == c.m_username
        && e.m_address == c.m_address;
}

// 按资料池里的联系人重算一条候选（覆盖 out 原有的全部内容）。
void BuildContactCandidate(const net::ContactItem& c, ContactCandidate& out)
{
    out.m_userid   = c.m_userid;
    out.m_remark   = c.m_remark;
    out.m_nickname = c.m_nickname;
    out.m_username = c.m_username;
    out.m_address  = c.m_address;

    const std::wstring nick    = Utf8ToWide(c.m_nickname);
    const std::wstring account = Utf8ToWide(c.m_username);
    const std::wstring region  = Utf8ToWide(c.m_address);

    //显示名：备注 → 昵称 → 账号 → 用户<id>，与通讯录树、会话列表同一个取名函数
    out.m_display = balloonmain::ResolveContactDisplayName(CString(Utf8ToWide(c.m_remark).c_str()),
                                                           CString(nick.c_str()),
                                                           CString(account.c_str()),
                                                           (UINT)c.m_userid);

    const std::wstring shown = (LPCWSTR)out.m_display;
    out.m_shownName   = contactsearch::PrepareName(shown);
    out.m_nickDiffers = (shown != nick);
    //只有显示名与昵称不同时才需要按昵称再匹配一次，相同时不再重复保存一份
    out.m_nickName = out.m_nickDiffers ? contactsearch::PrepareName(nick)
                                       : contactsearch::PreparedName();
    out.m_accountLower = contactsearch::ToLowerAscii(account);
    out.m_regionLower  = contactsearch::ToLowerAscii(region);
}

// 群候选的快照与群列表里的群是否一致。
bool GroupSnapshotMatches(const GroupCandidate& e, const net::UserGroupItem& g)
{
    return e.m_groupid == g.m_groupid && e.m_name == g.m_name;
}

// 按群列表里的群重算一条候选。
void BuildGroupCandidate(const net::UserGroupItem& g, GroupCandidate& out)
{
    out.m_groupid      = g.m_groupid;
    out.m_name         = g.m_name;
    out.m_preparedName = contactsearch::PrepareName(Utf8ToWide(g.m_name));
}

} // namespace

int SearchCandidateCache::SyncContacts(const std::vector<net::ContactItem>& contacts)
{
    //资料池变短：截掉多出的条目
    if (m_contacts.size() > contacts.size())
    {
        m_contacts.erase(m_contacts.begin() + contacts.size(), m_contacts.end());
    }
    m_contacts.reserve(contacts.size());

    int rebuilt = 0;
    for (size_t i = 0; i < contacts.size(); ++i)
    {
        if (i == m_contacts.size())
        {
            //资料池变长：追加一条空候选，随后按资料重算
            m_contacts.push_back(ContactCandidate());
        }
        else if (ContactSnapshotMatches(m_contacts[i], contacts[i]))
        {
            continue;   // 快照一致，沿用上次的结果
        }
        BuildContactCandidate(contacts[i], m_contacts[i]);
        ++rebuilt;
    }
    return rebuilt;
}

int SearchCandidateCache::SyncGroups(const std::vector<net::UserGroupItem>& groups)
{
    //群列表变短：截掉多出的条目
    if (m_groups.size() > groups.size())
    {
        m_groups.erase(m_groups.begin() + groups.size(), m_groups.end());
    }
    m_groups.reserve(groups.size());

    int rebuilt = 0;
    for (size_t i = 0; i < groups.size(); ++i)
    {
        if (i == m_groups.size())
        {
            //群列表变长：追加一条空候选，随后按群资料重算
            m_groups.push_back(GroupCandidate());
        }
        else if (GroupSnapshotMatches(m_groups[i], groups[i]))
        {
            continue;   // 快照一致，沿用上次的结果
        }
        BuildGroupCandidate(groups[i], m_groups[i]);
        ++rebuilt;
    }
    return rebuilt;
}

size_t SearchCandidateCache::ContactCount() const
{
    return m_contacts.size();
}

const ContactCandidate& SearchCandidateCache::ContactAt(size_t index) const
{
    return m_contacts[index];
}

size_t SearchCandidateCache::GroupCount() const
{
    return m_groups.size();
}

const GroupCandidate& SearchCandidateCache::GroupAt(size_t index) const
{
    return m_groups[index];
}

void SearchCandidateCache::Clear()
{
    m_contacts.clear();
    m_groups.clear();
}

} // namespace balloonsearch
