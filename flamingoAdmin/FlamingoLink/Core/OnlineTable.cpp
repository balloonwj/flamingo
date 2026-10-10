/**
 *  FlamingoLink.dll —— 「在线用户」页数据模型的实现，说明见 OnlineTable.h。
 *
 *  balloonwj@qq.com   2026-10-04
 */
#include "stdafx.h"
#include "FlamingoLink/Core/OnlineTable.h"

#include <algorithm>

namespace flamingolink {

namespace {

//在线状态取值，与协议 presence_type 一致
const int kPresenceOnline = 1;
const int kPresenceAway   = 2;
const int kPresenceBusy   = 3;

//客户端类型取值，与协议 platform_type 一致
const int kClientPc      = 1;
const int kClientAndroid = 2;
const int kClientIos     = 3;
const int kClientMac     = 4;
const int kClientWeb     = 5;

//在线状态的排序名次
const int kRankOnline = 0;
const int kRankBusy   = 1;
const int kRankAway   = 2;
const int kRankOther  = 3;

//多个部门之间的分隔符
const wchar_t* const kDeptSeparator = L"、";

//按当前用户的区域设置比较两段文字（不区分大小写），中文按区域的排序规则排列
int CompareText(const CString& a, const CString& b)
{
    const int r = ::CompareStringW(LOCALE_USER_DEFAULT, NORM_IGNORECASE,
                                   a.GetString(), a.GetLength(), b.GetString(), b.GetLength());
    //CompareStringW 返回 CSTR_LESS_THAN(1) / CSTR_EQUAL(2) / CSTR_GREATER_THAN(3)，失败返回 0
    if (r == CSTR_LESS_THAN)
        return -1;
    if (r == CSTR_GREATER_THAN)
        return 1;
    return 0;
}

//排序时显示用的名字：昵称为空时用账号
const CString& DisplayName(const OnlineRow& r)
{
    return r.m_nickname.IsEmpty() ? r.m_account : r.m_nickname;
}

//表格行的排序规则：在线状态名次、名字、账号、设备类型、用户ID
bool RowLess(const OnlineRow& a, const OnlineRow& b)
{
    const int ra = OnlineTable::PresenceRank(a.m_presence);
    const int rb = OnlineTable::PresenceRank(b.m_presence);
    if (ra != rb)
        return ra < rb;

    const int byName = CompareText(DisplayName(a), DisplayName(b));
    if (byName != 0)
        return byName < 0;

    const int byAccount = CompareText(a.m_account, b.m_account);
    if (byAccount != 0)
        return byAccount < 0;

    if (a.m_clientType != b.m_clientType)
        return a.m_clientType < b.m_clientType;
    return a.m_userId < b.m_userId;
}

//text 是否包含 keyword（keyword 已转成小写）
bool ContainsLower(const CString& text, const CString& lowerKeyword)
{
    CString lower(text);
    lower.MakeLower();
    return lower.Find(lowerKeyword) >= 0;
}

} // anonymous namespace

OnlineTable::OnlineTable()
{
}

void OnlineTable::Clear()
{
    m_users.clear();
    m_rows.clear();
}

bool OnlineTable::Apply(const OnlineNotify& notify, int64_t expectedEnterprise)
{
    if (notify.m_enterpriseId != expectedEnterprise || expectedEnterprise <= 0)
        return false;

    if (notify.m_reset)
        m_users.clear();

    for (size_t i = 0; i < notify.m_users.size(); ++i)
    {
        const LinkUser& u = notify.m_users[i];
        if (u.m_devices.empty())
            m_users.erase(u.m_userId);
        else
            m_users[u.m_userId] = u;
    }

    RebuildRows();
    return true;
}

void OnlineTable::Filter(const CString& keyword, std::vector<int>& outIndex) const
{
    outIndex.clear();

    CString key(keyword);
    key.Trim();
    key.MakeLower();

    for (size_t i = 0; i < m_rows.size(); ++i)
    {
        const OnlineRow& r = m_rows[i];
        if (key.IsEmpty()
            || ContainsLower(r.m_nickname, key)
            || ContainsLower(r.m_account, key)
            || ContainsLower(r.m_depts, key))
        {
            outIndex.push_back(static_cast<int>(i));
        }
    }
}

OnlineSummary OnlineTable::Summary() const
{
    OnlineSummary s;
    s.m_users = static_cast<int>(m_users.size());
    s.m_devices = static_cast<int>(m_rows.size());

    for (size_t i = 0; i < m_rows.size(); ++i)
    {
        switch (m_rows[i].m_clientType)
        {
        //PC 客户端
        case kClientPc:
            ++s.m_pc;
            break;

        //Android 客户端
        case kClientAndroid:
            ++s.m_android;
            break;

        //iOS 客户端
        case kClientIos:
            ++s.m_ios;
            break;

        //Mac 客户端
        case kClientMac:
            ++s.m_mac;
            break;

        //Web 客户端
        case kClientWeb:
            ++s.m_web;
            break;

        //服务端下发了本版本不认识的客户端类型
        default:
            ++s.m_other;
            break;
        }
    }
    return s;
}

int OnlineTable::PresenceRank(int presence)
{
    switch (presence)
    {
    //在线排最前
    case kPresenceOnline:
        return kRankOnline;

    //勿扰其次：人在，只是不想被打扰
    case kPresenceBusy:
        return kRankBusy;

    //离开再次
    case kPresenceAway:
        return kRankAway;

    //未知状态排最后
    default:
        return kRankOther;
    }
}

CString OnlineTable::JoinDepts(const std::vector<CString>& depts)
{
    CString out;
    for (size_t i = 0; i < depts.size(); ++i)
    {
        if (i > 0)
            out += kDeptSeparator;
        out += depts[i];
    }
    return out;
}

void OnlineTable::RebuildRows()
{
    m_rows.clear();

    for (std::map<int64_t, LinkUser>::const_iterator it = m_users.begin(); it != m_users.end(); ++it)
    {
        const LinkUser& u = it->second;
        const CString depts = JoinDepts(u.m_depts);
        for (size_t k = 0; k < u.m_devices.size(); ++k)
        {
            const LinkDevice& d = u.m_devices[k];
            OnlineRow row;
            row.m_userId = u.m_userId;
            row.m_nickname = u.m_nickname;
            row.m_account = u.m_account;
            row.m_depts = depts;
            row.m_clientType = d.m_clientType;
            row.m_presence = d.m_presence;
            row.m_loginTimeMs = d.m_loginTimeMs;
            row.m_ip = d.m_ip;
            m_rows.push_back(row);
        }
    }

    std::sort(m_rows.begin(), m_rows.end(), RowLess);
}

} // namespace flamingolink
