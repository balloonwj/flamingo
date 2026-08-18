/**
 *  GroupMemberOrderTests.cpp
 *
 *  群成员侧栏显示顺序（BalloonChatWnd/GroupMemberOrder.h）的测试。
 *
 *  规则：群主排最前、其次管理员、最后普通成员，同一角色内保持服务端返回的原有
 *  顺序。稳定性是重点 —— 群成员应答会在开窗、成员变更等多个时机重复到达，同角色
 *  内的次序若每次都可能变，用户眼里就是"列表自己在跳"。
 *
 *  用 Catch2 v3（amalgamated），纯内存断言，不创建窗口。
 *  全局 _Module 由 BalloonChatLayoutTests.cpp 统一定义，本文件只引用。
 *
 *  balloonwj@qq.com   2026-08-03
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <vector>

#include "../UI/BalloonChatWnd/GroupMemberOrder.h"
#include "../net/Protocol.h"   // group_member_role：确认本模块用的数值与协议一致

using namespace balloonchatwnd;

namespace {

// 角色值取自协议枚举，避免测试里写裸数字与产品代码脱节。
const int kMember = group_role_member;
const int kAdmin  = group_role_admin;
const int kOwner  = group_role_owner;

// 把下标序列拼成便于阅读的字符串，断言失败时一眼看出实际顺序。
std::string ToText(const std::vector<std::size_t>& v)
{
    std::string s;
    for (std::size_t i = 0; i < v.size(); ++i)
    {
        if (i > 0)
        {
            s += ",";
        }
        s += std::to_string((unsigned long long)v[i]);
    }
    return s;
}

} // anonymous namespace

TEST_CASE("ORDER1 群主排最前、管理员次之、普通成员最后", "[memberorder]")
{
    std::vector<int> roles;
    roles.push_back(kMember);   // 0
    roles.push_back(kOwner);    // 1
    roles.push_back(kMember);   // 2
    roles.push_back(kAdmin);    // 3

    const std::vector<std::size_t> order = OrderMemberIndicesByRole(roles);

    INFO("实际顺序 = " << ToText(order));
    REQUIRE(order.size() == 4);
    CHECK(order[0] == 1);   // 群主
    CHECK(order[1] == 3);   // 管理员
    CHECK(order[2] == 0);   // 普通成员，按原序
    CHECK(order[3] == 2);
}

TEST_CASE("ORDER2 同一角色内保持原有顺序", "[memberorder]")
{
    // 三个管理员、三个普通成员交替给出：同角色内必须严格按输入先后排列。
    std::vector<int> roles;
    roles.push_back(kAdmin);    // 0
    roles.push_back(kMember);   // 1
    roles.push_back(kAdmin);    // 2
    roles.push_back(kMember);   // 3
    roles.push_back(kAdmin);    // 4
    roles.push_back(kMember);   // 5

    const std::vector<std::size_t> order = OrderMemberIndicesByRole(roles);

    INFO("实际顺序 = " << ToText(order));
    REQUIRE(order.size() == 6);
    CHECK(order[0] == 0);
    CHECK(order[1] == 2);
    CHECK(order[2] == 4);
    CHECK(order[3] == 1);
    CHECK(order[4] == 3);
    CHECK(order[5] == 5);
}

TEST_CASE("ORDER3 重复排同一份输入结果不变（稳定，列表不会跳）", "[memberorder]")
{
    std::vector<int> roles;
    roles.push_back(kMember);
    roles.push_back(kAdmin);
    roles.push_back(kOwner);
    roles.push_back(kMember);
    roles.push_back(kAdmin);

    const std::vector<std::size_t> a = OrderMemberIndicesByRole(roles);
    const std::vector<std::size_t> b = OrderMemberIndicesByRole(roles);

    REQUIRE(a.size() == b.size());
    for (std::size_t i = 0; i < a.size(); ++i)
    {
        INFO("位置 " << i);
        CHECK(a[i] == b[i]);
    }
}

TEST_CASE("ORDER4 一个人都不丢：输出是输入下标的一个排列", "[memberorder]")
{
    std::vector<int> roles;
    roles.push_back(kOwner);
    roles.push_back(kMember);
    roles.push_back(kAdmin);
    roles.push_back(kMember);
    roles.push_back(kOwner);   // 防御性：数据异常出现两个群主也不该丢人

    const std::vector<std::size_t> order = OrderMemberIndicesByRole(roles);

    REQUIRE(order.size() == roles.size());
    std::vector<bool> seen(roles.size(), false);
    for (std::size_t i = 0; i < order.size(); ++i)
    {
        REQUIRE(order[i] < roles.size());
        CHECK_FALSE(seen[order[i]]);   // 不重复
        seen[order[i]] = true;
    }
    for (std::size_t i = 0; i < seen.size(); ++i)
    {
        INFO("原下标 " << i);
        CHECK(seen[i]);                // 不遗漏
    }
}

TEST_CASE("ORDER5 未知角色当普通成员排在最后但不丢弃", "[memberorder]")
{
    // 服务端将来加了新角色时，客户端不该让那个人从成员列表里凭空消失。
    const int kUnknownRole = 99;

    std::vector<int> roles;
    roles.push_back(kUnknownRole);   // 0
    roles.push_back(kOwner);         // 1
    roles.push_back(kMember);        // 2

    const std::vector<std::size_t> order = OrderMemberIndicesByRole(roles);

    INFO("实际顺序 = " << ToText(order));
    REQUIRE(order.size() == 3);
    CHECK(order[0] == 1);   // 群主仍在最前
    // 未知角色与普通成员同档，按原序：未知(0) 在 普通(2) 之前。
    CHECK(order[1] == 0);
    CHECK(order[2] == 2);
}

TEST_CASE("ORDER6 空输入返回空", "[memberorder]")
{
    const std::vector<std::size_t> order = OrderMemberIndicesByRole(std::vector<int>());
    CHECK(order.empty());
}
