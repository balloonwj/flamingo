/**
 *  搜索候选缓存的单元测试（Suite SCC），SearchCandidateCacheTests.cpp
 *
 *  覆盖 balloonsearch::SearchCandidateCache：
 *    SCC1  首次 Sync 重算全部条目，显示名与 ResolveContactDisplayName 一致，预处理结果正确；
 *    SCC2  数据不变时再次 Sync，一条都不重算；
 *    SCC3  改一个人的备注，只重算这一条，显示名随之更新；
 *    SCC4  改昵称 / 账号 / 地区各自触发该条重算；改签名、标签、头像、在线状态不触发；
 *    SCC5  删人、加人、在头部插入一人后，Sync 结果与从零新建的缓存逐条一致；
 *    SCC6  群：首次全部重算、不变时不重算、改群名只重算一条、其它字段不触发、增删后一致；
 *    SCC7  设了备注的人保留按原始昵称预处理的结果，没设备注的不保留。
 *
 *  UTF-8 文本一律用 \x 转义写出，避免源文件编码影响；期望值用 MultiByteToWideChar
 *  独立转换，不借用被测代码里的转换函数。框架 Catch2 v3。
 *
 *  balloonwj@qq.com   2026-10-01
 */
#include "stdafx.h"

#include "catch_amalgamated.hpp"
#include "../UI/BalloonSearch/SearchCandidateCache.h"
#include "../UI/BalloonMain/ContactRemarkModel.h"

#include <string>
#include <vector>

using balloonsearch::ContactCandidate;
using balloonsearch::GroupCandidate;
using balloonsearch::SearchCandidateCache;

namespace {

// 测试用 UTF-8 文本。
const char* const kZhangSanUtf8 = "\xE5\xBC\xA0\xE4\xB8\x89";   // 张三
const char* const kLiSiUtf8     = "\xE6\x9D\x8E\xE5\x9B\x9B";   // 李四
const char* const kHangZhouUtf8 = "\xE6\x9D\xAD\xE5\xB7\x9E";   // 杭州

// 测试资料池的规模：足够覆盖首尾与中间位置，又不至于让用例变慢。
const int kPoolSize = 12;

// 资料池里联系人 userid 的起始值，避免与 0（未初始化）混淆。
const int64_t kFirstUserId = 10001;

// 群列表里群 id 的起始值。
const int64_t kFirstGroupId = 50001;

// UTF-8 → UTF-16，期望值专用，与被测代码的转换互相独立。
std::wstring Wide(const std::string& utf8)
{
    if (utf8.empty())
    {
        return std::wstring();
    }
    const int len = ::MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(), NULL, 0);
    std::wstring out((size_t)len, L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(), &out[0], len);
    return out;
}

// 造一个联系人；不参与匹配的字段也填上值，便于验证改它们不会触发重算。
net::ContactItem MakeContact(int64_t userid, const std::string& nickname, const std::string& remark,
                             const std::string& username, const std::string& address)
{
    net::ContactItem c;
    c.m_userid     = userid;
    c.m_nickname   = nickname;
    c.m_remark     = remark;
    c.m_username   = username;
    c.m_address    = address;
    c.m_signature  = "signature";
    c.m_facetype   = 0;
    c.m_presence   = 0;
    return c;
}

// 造一份资料池：中英文昵称交替，每隔几人设一个备注、空一个昵称或地区。
std::vector<net::ContactItem> MakePool()
{
    std::vector<net::ContactItem> pool;
    for (int i = 0; i < kPoolSize; ++i)
    {
        const int64_t userid = kFirstUserId + i;
        std::string nickname = (i % 2 == 0) ? std::string(kZhangSanUtf8) : ("User" + std::to_string(i));
        std::string remark;
        if (i % 4 == 1)
        {
            remark = kLiSiUtf8;    // 每四人一个设了备注
        }
        if (i == kPoolSize - 1)
        {
            nickname.clear();      // 最后一人没有昵称，显示名回落到账号
        }
        const std::string address = (i % 3 == 0) ? std::string(kHangZhouUtf8) : std::string();
        pool.push_back(MakeContact(userid, nickname, remark, "acc" + std::to_string(i), address));
    }
    return pool;
}

// 造一份群列表。
std::vector<net::UserGroupItem> MakeGroups()
{
    std::vector<net::UserGroupItem> groups;
    for (int i = 0; i < kPoolSize; ++i)
    {
        net::UserGroupItem g;
        g.m_groupid     = kFirstGroupId + i;
        g.m_name        = (i % 2 == 0) ? (std::string(kZhangSanUtf8) + std::to_string(i)) : ("Group" + std::to_string(i));
        g.m_membercount = i + 1;
        groups.push_back(g);
    }
    return groups;
}

// 按 ResolveContactDisplayName 的口径算出某联系人的显示名。
CString ExpectedDisplay(const net::ContactItem& c)
{
    return balloonmain::ResolveContactDisplayName(CString(Wide(c.m_remark).c_str()),
                                                  CString(Wide(c.m_nickname).c_str()),
                                                  CString(Wide(c.m_username).c_str()),
                                                  (UINT)c.m_userid);
}

// 两份预处理结果是否完全相同。
bool SameName(const contactsearch::PreparedName& a, const contactsearch::PreparedName& b)
{
    return a.m_lower == b.m_lower && a.m_initials == b.m_initials && a.m_syllables == b.m_syllables;
}

// 判断一条联系人候选是否与它对应的联系人完全吻合（快照与全部预处理结果）。
bool CandidateMatchesContact(const ContactCandidate& e, const net::ContactItem& c)
{
    const CString display = ExpectedDisplay(c);
    const std::wstring shown = (LPCWSTR)display;
    const std::wstring nick  = Wide(c.m_nickname);
    const bool nickDiffers   = (shown != nick);

    const bool nickOk = nickDiffers
        ? SameName(e.m_nickName, contactsearch::PrepareName(nick))
        : SameName(e.m_nickName, contactsearch::PreparedName());

    return e.m_userid == c.m_userid
        && e.m_remark == c.m_remark
        && e.m_nickname == c.m_nickname
        && e.m_username == c.m_username
        && e.m_address == c.m_address
        && e.m_display == display
        && SameName(e.m_shownName, contactsearch::PrepareName(shown))
        && e.m_nickDiffers == nickDiffers
        && nickOk
        && e.m_accountLower == contactsearch::ToLowerAscii(Wide(c.m_username))
        && e.m_regionLower == contactsearch::ToLowerAscii(Wide(c.m_address));
}

// 统计缓存里与资料池对不上的条目数；条数不同时把多出或缺少的部分也计入。
int CountContactMismatches(const SearchCandidateCache& cache, const std::vector<net::ContactItem>& pool)
{
    int mismatches = 0;
    const size_t n = (cache.ContactCount() < pool.size()) ? cache.ContactCount() : pool.size();
    for (size_t i = 0; i < n; ++i)
    {
        if (!CandidateMatchesContact(cache.ContactAt(i), pool[i]))
        {
            ++mismatches;
        }
    }
    mismatches += (int)((cache.ContactCount() > pool.size())
                        ? cache.ContactCount() - pool.size()
                        : pool.size() - cache.ContactCount());
    return mismatches;
}

// 统计群候选与群列表对不上的条目数，规则同 CountContactMismatches。
int CountGroupMismatches(const SearchCandidateCache& cache, const std::vector<net::UserGroupItem>& groups)
{
    int mismatches = 0;
    const size_t n = (cache.GroupCount() < groups.size()) ? cache.GroupCount() : groups.size();
    for (size_t i = 0; i < n; ++i)
    {
        const GroupCandidate& e = cache.GroupAt(i);
        const bool ok = e.m_groupid == groups[i].m_groupid
                     && e.m_name == groups[i].m_name
                     && SameName(e.m_preparedName, contactsearch::PrepareName(Wide(groups[i].m_name)));
        if (!ok)
        {
            ++mismatches;
        }
    }
    mismatches += (int)((cache.GroupCount() > groups.size())
                        ? cache.GroupCount() - groups.size()
                        : groups.size() - cache.GroupCount());
    return mismatches;
}

} // namespace

TEST_CASE("SCC1 首次 Sync 重算全部条目且结果正确", "[searchcache]")
{
    const std::vector<net::ContactItem> pool = MakePool();
    SearchCandidateCache cache;

    CHECK(cache.SyncContacts(pool) == kPoolSize);
    REQUIRE(cache.ContactCount() == pool.size());
    CHECK(CountContactMismatches(cache, pool) == 0);

    // 抽查一条设了备注的：显示名是"真名（备注）"的形态，与通讯录树同一口径
    CHECK(cache.ContactAt(1).m_display == ExpectedDisplay(pool[1]));
}

TEST_CASE("SCC2 数据不变时再次 Sync 不重算", "[searchcache]")
{
    const std::vector<net::ContactItem> pool = MakePool();
    SearchCandidateCache cache;
    cache.SyncContacts(pool);
    REQUIRE(cache.ContactCount() == pool.size());

    CHECK(cache.SyncContacts(pool) == 0);
    CHECK(CountContactMismatches(cache, pool) == 0);
}

TEST_CASE("SCC3 改一个人的备注只重算这一条", "[searchcache]")
{
    std::vector<net::ContactItem> pool = MakePool();
    SearchCandidateCache cache;
    cache.SyncContacts(pool);
    REQUIRE(cache.ContactCount() == pool.size());

    const size_t target = 2;   // 原本没有备注的一人
    const CString before = cache.ContactAt(target).m_display;
    pool[target].m_remark = kLiSiUtf8;

    CHECK(cache.SyncContacts(pool) == 1);
    CHECK(CountContactMismatches(cache, pool) == 0);
    CHECK(cache.ContactAt(target).m_display != before);
    CHECK(cache.ContactAt(target).m_display == ExpectedDisplay(pool[target]));
}

TEST_CASE("SCC4 只有参与匹配的字段变化才触发重算", "[searchcache]")
{
    std::vector<net::ContactItem> pool = MakePool();
    SearchCandidateCache cache;
    cache.SyncContacts(pool);
    REQUIRE(cache.ContactCount() == pool.size());

    // 参与匹配的三项，各改一个人，各自只重算一条
    pool[3].m_nickname = "Renamed";
    CHECK(cache.SyncContacts(pool) == 1);
    pool[4].m_username = "newaccount";
    CHECK(cache.SyncContacts(pool) == 1);
    pool[5].m_address = kHangZhouUtf8;
    CHECK(cache.SyncContacts(pool) == 1);
    CHECK(CountContactMismatches(cache, pool) == 0);

    // 不参与匹配的字段：签名、标签、头像、在线状态，改了也不重算
    pool[6].m_signature  = "changed signature";
    pool[6].m_tags.push_back("tag");
    pool[7].m_facetype   = 2;
    pool[7].m_customface = "fileid-123";
    pool[8].m_presence   = 1;
    CHECK(cache.SyncContacts(pool) == 0);
    CHECK(CountContactMismatches(cache, pool) == 0);
}

TEST_CASE("SCC5 增删与错位后与从零新建的缓存一致", "[searchcache]")
{
    std::vector<net::ContactItem> pool = MakePool();
    SearchCandidateCache cache;
    cache.SyncContacts(pool);
    REQUIRE(cache.ContactCount() == pool.size());

    // 删掉末尾两人
    pool.pop_back();
    pool.pop_back();
    cache.SyncContacts(pool);
    CHECK(cache.ContactCount() == pool.size());
    CHECK(CountContactMismatches(cache, pool) == 0);

    // 追加三人
    for (int i = 0; i < 3; ++i)
    {
        pool.push_back(MakeContact(kFirstUserId + kPoolSize + i, "Added" + std::to_string(i), "",
                                   "added" + std::to_string(i), ""));
    }
    cache.SyncContacts(pool);
    CHECK(cache.ContactCount() == pool.size());
    CHECK(CountContactMismatches(cache, pool) == 0);

    // 在头部插入一人：后面的人整体后移一位，每个位置上的人都变了
    pool.insert(pool.begin(), MakeContact(kFirstUserId - 1, kLiSiUtf8, "", "front", ""));
    cache.SyncContacts(pool);
    CHECK(cache.ContactCount() == pool.size());
    CHECK(CountContactMismatches(cache, pool) == 0);

    // 与从零新建的缓存逐条比对
    SearchCandidateCache fresh;
    fresh.SyncContacts(pool);
    REQUIRE(fresh.ContactCount() == cache.ContactCount());
    int diff = 0;
    for (size_t i = 0; i < pool.size(); ++i)
    {
        const ContactCandidate& a = cache.ContactAt(i);
        const ContactCandidate& b = fresh.ContactAt(i);
        const bool same = a.m_userid == b.m_userid
                       && a.m_display == b.m_display
                       && SameName(a.m_shownName, b.m_shownName)
                       && a.m_nickDiffers == b.m_nickDiffers
                       && SameName(a.m_nickName, b.m_nickName)
                       && a.m_accountLower == b.m_accountLower
                       && a.m_regionLower == b.m_regionLower;
        if (!same)
        {
            ++diff;
        }
    }
    CHECK(diff == 0);
}

TEST_CASE("SCC6 群候选：首次重算、不变不重算、改名重算、增删后一致", "[searchcache]")
{
    std::vector<net::UserGroupItem> groups = MakeGroups();
    SearchCandidateCache cache;

    CHECK(cache.SyncGroups(groups) == kPoolSize);
    REQUIRE(cache.GroupCount() == groups.size());
    CHECK(CountGroupMismatches(cache, groups) == 0);

    CHECK(cache.SyncGroups(groups) == 0);

    // 改群名只重算一条
    groups[3].m_name = kHangZhouUtf8;
    CHECK(cache.SyncGroups(groups) == 1);
    CHECK(CountGroupMismatches(cache, groups) == 0);

    // 成员数、头像、置顶不参与匹配，改了不重算
    groups[4].m_membercount = 99;
    groups[5].m_facetype    = 2;
    groups[5].m_customface  = "fileid-456";
    groups[6].m_pinned      = 1;
    CHECK(cache.SyncGroups(groups) == 0);

    // 删一个、在头部插一个
    groups.erase(groups.begin() + 2);
    net::UserGroupItem front;
    front.m_groupid = kFirstGroupId - 1;
    front.m_name    = kLiSiUtf8;
    groups.insert(groups.begin(), front);
    cache.SyncGroups(groups);
    CHECK(cache.GroupCount() == groups.size());
    CHECK(CountGroupMismatches(cache, groups) == 0);

    // 联系人与群互不影响：只同步了群，联系人候选仍为空
    CHECK(cache.ContactCount() == 0);
}

TEST_CASE("SCC7 设了备注的人保留按原始昵称预处理的结果", "[searchcache]")
{
    std::vector<net::ContactItem> pool;
    pool.push_back(MakeContact(kFirstUserId, kZhangSanUtf8, kLiSiUtf8, "withremark", ""));
    pool.push_back(MakeContact(kFirstUserId + 1, kZhangSanUtf8, "", "noremark", ""));

    SearchCandidateCache cache;
    cache.SyncContacts(pool);
    REQUIRE(cache.ContactCount() == pool.size());

    // 设了备注：显示名是"真名（备注）"，与昵称不同，按昵称另存一份
    const ContactCandidate& withRemark = cache.ContactAt(0);
    CHECK(withRemark.m_nickDiffers);
    CHECK(SameName(withRemark.m_nickName, contactsearch::PrepareName(Wide(kZhangSanUtf8))));
    CHECK(withRemark.m_nickName.m_initials == "zs");

    // 没设备注：显示名就是昵称，不另存
    const ContactCandidate& noRemark = cache.ContactAt(1);
    CHECK_FALSE(noRemark.m_nickDiffers);
    CHECK(noRemark.m_nickName.m_lower.empty());
    CHECK(noRemark.m_nickName.m_syllables.empty());
    CHECK(noRemark.m_shownName.m_initials == "zs");
}
