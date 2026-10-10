/**
 *  GroupRoleRefreshTests.cpp
 *
 *  本人在群里的角色变了之后，客户端怎样刷新自己缓存的角色（标签 [grouprole][rolerefresh]）：
 *
 *    RC1～5 收到群成员变更推送（cmd_group_member_changed_notify）后，是否需要为刷新本人角色
 *           补拉一次群资料（grouprole::ShouldRefetchMyRoleOnMemberChange）。此前只有该群的
 *           聊天窗开着时才补拉，两个窗口都没开时本人角色的缓存要等重新登录才更新，表现为被设
 *           为管理员后主面板右键群时没有「群管理」。
 *    RC6    群聊窗成员侧栏批量更新各行记录的"本人角色"（GroupMemberList::SetMyNetRoleForAll）。
 *           本人角色到达时侧栏各行若仍是旧值，成员右键菜单里该有的管理项不会出现。
 *    JT8    收到名单里有本人的「加入」推送后补拉一次本人的群列表（grouprole::ShouldRefetchGroupListOnMemberChange，
 *           标签 [grouprole][grouplist]，docs/邀请入群方案.md 第 1 期）。此前被邀请入群后「群聊」页要重新登录才出现这个群。
 *    GC1    别人加入 / 退出 / 被移出时补拉该群资料（grouprole::ShouldRefetchGroupInfoForMemberCount），
 *           GC2 收到群资料后把人数同步进群列表（balloonmain::SetUserGroupMemberCount），标签 [grouprole][grouplist]。
 *           此前群主把人移出后，主面板「群聊」页这个群的人数要重新登录才变（2026-10-08 实测）。
 *
 *  用 Catch2 v3（amalgamated 形态）。全局 _Module 由 BalloonChatLayoutTests.cpp 统一定义。
 *
 *  balloonwj@qq.com   2026-10-01
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <memory>
#include <vector>

#include "../Session/GroupRoleRules.h"
#include "../UI/BalloonMain/GroupListOps.h"   // GC2：群列表人数的同步
#include "../UI/BalloonChatWnd/GroupMemberItem.h"
#include "../UI/BalloonChatWnd/GroupMemberList.h"

using namespace grouprole;

namespace {

// 群成员变更推送 changetype 的其它几种取值，与 net/Protocol.h 的 group_member_change_type 一致
const int kChangeJoin  = 1;   // 有人加入
const int kChangeLeave = 2;   // 有人主动退出
const int kChangeKick  = 3;   // 有人被移出
const int kChangeMute  = 5;   // 禁言状态变更
const int kChangeCard  = 6;   // 群名片变更

// 样例 userid：本人、另一位成员、群主。取值无特殊含义
const int64_t kSelf  = 5308;
const int64_t kOther = 5010;
const int64_t kOwner = 5127;

// 由若干 userid 拼一份变更名单。
std::vector<int64_t> Ids(int64_t a, int64_t b = 0)
{
    std::vector<int64_t> ids;
    ids.push_back(a);
    if (b != 0)
    {
        ids.push_back(b);
    }
    return ids;
}

} // anonymous namespace

TEST_CASE("RC1 角色变更推送的名单里有本人时，要补拉群资料", "[grouprole][rolerefresh]")
{
    CHECK(ShouldRefetchMyRoleOnMemberChange(kMemberChangeRole, Ids(kSelf), kSelf));
}

TEST_CASE("RC2 角色变更推送的名单里没有本人时，不补拉", "[grouprole][rolerefresh]")
{
    CHECK_FALSE(ShouldRefetchMyRoleOnMemberChange(kMemberChangeRole, Ids(kOther), kSelf));
    CHECK_FALSE(ShouldRefetchMyRoleOnMemberChange(kMemberChangeRole, std::vector<int64_t>(), kSelf));
}

TEST_CASE("RC3 其它类型的成员变更即使名单里有本人，也不为角色补拉", "[grouprole][rolerefresh]")
{
    const int types[] = { kChangeJoin, kChangeLeave, kChangeKick, kChangeMute, kChangeCard };
    for (size_t i = 0; i < sizeof(types) / sizeof(types[0]); ++i)
    {
        INFO("changetype=" << types[i]);
        CHECK_FALSE(ShouldRefetchMyRoleOnMemberChange(types[i], Ids(kSelf), kSelf));
    }
}

TEST_CASE("RC4 转让群主时名单是旧群主与新群主，本人是其中任何一位都要补拉", "[grouprole][rolerefresh]")
{
    //本人是新群主
    CHECK(ShouldRefetchMyRoleOnMemberChange(kMemberChangeRole, Ids(kOwner, kSelf), kSelf));
    //本人是旧群主
    CHECK(ShouldRefetchMyRoleOnMemberChange(kMemberChangeRole, Ids(kSelf, kOther), kSelf));
    //转让与本人无关
    CHECK_FALSE(ShouldRefetchMyRoleOnMemberChange(kMemberChangeRole, Ids(kOwner, kOther), kSelf));
}

TEST_CASE("RC5 未登录（本人 id 为 0）时不补拉", "[grouprole][rolerefresh]")
{
    CHECK_FALSE(ShouldRefetchMyRoleOnMemberChange(kMemberChangeRole, Ids(kSelf), 0));
    //名单里混进一个 0 也不能因此判成"有本人"
    CHECK_FALSE(ShouldRefetchMyRoleOnMemberChange(kMemberChangeRole, Ids(kOther), 0));
}

TEST_CASE("RC6 成员侧栏批量更新各行记录的本人角色", "[grouprole][rolerefresh]")
{
    balloonchatwnd::GroupMemberList list;
    const int kRows = 3;   //侧栏里的行数，取值无特殊含义
    for (int i = 0; i < kRows; ++i)
    {
        std::unique_ptr<balloonchatwnd::GroupMemberItem> item(new balloonchatwnd::GroupMemberItem());
        item->SetUserId(kOther + i);
        item->SetMyNetRole(kNetRoleMember);
        list.AddChild(std::move(item));
    }

    list.SetMyNetRoleForAll(kNetRoleOwner);

    REQUIRE(list.Children().size() == (size_t)kRows);
    for (size_t i = 0; i < list.Children().size(); ++i)
    {
        const balloonchatwnd::GroupMemberItem* item =
            dynamic_cast<const balloonchatwnd::GroupMemberItem*>(list.Children()[i].get());
        REQUIRE(item != nullptr);
        CHECK(item->MyNetRole() == kNetRoleOwner);
    }
}

TEST_CASE("JT8 本人入群时补拉群列表：名单里有本人的「加入」才补拉", "[grouprole][grouplist]")
{
    // 本人被邀请入群（或自由加群、申请经审批、建群时被拉进来）：补拉，「群聊」页才会出现这个群
    CHECK(ShouldRefetchGroupListOnMemberChange(kMemberChangeJoin, Ids(kSelf), kSelf));
    CHECK(ShouldRefetchGroupListOnMemberChange(kMemberChangeJoin, Ids(kOther, kSelf), kSelf));
    // 别人入群：本人的群列表没有变化
    CHECK_FALSE(ShouldRefetchGroupListOnMemberChange(kMemberChangeJoin, Ids(kOther), kSelf));
    // 其它类型的变更即使名单里有本人也不补拉（退群、被移出另有清理流程）
    CHECK_FALSE(ShouldRefetchGroupListOnMemberChange(kChangeLeave, Ids(kSelf), kSelf));
    CHECK_FALSE(ShouldRefetchGroupListOnMemberChange(kChangeKick, Ids(kSelf), kSelf));
    CHECK_FALSE(ShouldRefetchGroupListOnMemberChange(kMemberChangeRole, Ids(kSelf), kSelf));
    // 未登录（本人 id 为 0）不补拉
    CHECK_FALSE(ShouldRefetchGroupListOnMemberChange(kMemberChangeJoin, Ids(kSelf), 0));
    // 本端复制的取值须与协议的 changetype 一致
    CHECK(kMemberChangeJoin == kChangeJoin);
}

TEST_CASE("GC1 别人加入、退出、被移出时补拉该群资料，主面板群列表的人数靠它刷新", "[grouprole][grouplist]")
{
    // 别人入群、主动退群、被移出：补拉
    CHECK(ShouldRefetchGroupInfoForMemberCount(kChangeJoin, Ids(kOther), kSelf));
    CHECK(ShouldRefetchGroupInfoForMemberCount(kChangeLeave, Ids(kOther), kSelf));
    CHECK(ShouldRefetchGroupInfoForMemberCount(kChangeKick, Ids(kOther), kSelf));
    // 名单里有本人：本人入群另有补拉群列表（JT8），本人退群、被移出另有清理流程，这里都不补拉
    CHECK_FALSE(ShouldRefetchGroupInfoForMemberCount(kChangeJoin, Ids(kOther, kSelf), kSelf));
    CHECK_FALSE(ShouldRefetchGroupInfoForMemberCount(kChangeLeave, Ids(kSelf), kSelf));
    CHECK_FALSE(ShouldRefetchGroupInfoForMemberCount(kChangeKick, Ids(kSelf), kSelf));
    // 人数不变的变更：不补拉
    CHECK_FALSE(ShouldRefetchGroupInfoForMemberCount(kMemberChangeRole, Ids(kOther), kSelf));
    CHECK_FALSE(ShouldRefetchGroupInfoForMemberCount(kChangeMute, Ids(kOther), kSelf));
    CHECK_FALSE(ShouldRefetchGroupInfoForMemberCount(kChangeCard, Ids(kOther), kSelf));
    // 未登录（本人 id 为 0）不补拉
    CHECK_FALSE(ShouldRefetchGroupInfoForMemberCount(kChangeKick, Ids(kOther), 0));
}

TEST_CASE("GC2 收到群资料后把人数同步进群列表，人数没变时报告不需要重建", "[grouprole][grouplist]")
{
    // 两个群，人数各 6、3
    const int64_t kGroupA = 268435456;
    const int64_t kGroupB = 268435457;
    const int64_t kGroupMissing = 268435999;
    std::vector<net::UserGroupItem> groups(2);
    groups[0].m_groupid = kGroupA;
    groups[0].m_membercount = 6;
    groups[1].m_groupid = kGroupB;
    groups[1].m_membercount = 3;

    // 人数变了：更新并返回 true，别的群不动
    CHECK(balloonmain::SetUserGroupMemberCount(groups, kGroupA, 5));
    CHECK(groups[0].m_membercount == 5);
    CHECK(groups[1].m_membercount == 3);
    // 人数没变：返回 false，调用方不必重建群列表
    CHECK_FALSE(balloonmain::SetUserGroupMemberCount(groups, kGroupA, 5));
    // 不在群列表里的群、无效的人数：不改动
    CHECK_FALSE(balloonmain::SetUserGroupMemberCount(groups, kGroupMissing, 9));
    CHECK_FALSE(balloonmain::SetUserGroupMemberCount(groups, kGroupB, -1));
    CHECK(groups[1].m_membercount == 3);
}
