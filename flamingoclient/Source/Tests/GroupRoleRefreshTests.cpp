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
