/**
 *  GroupListOpsTests.cpp
 *
 *  「群」Tab 群列表纯操作函数（BalloonMain/GroupListOps.h）的测试：置顶的群排在最前、
 *  改某个群的置顶状态、查某个群是否置顶。
 *
 *  群 Tab 原先按服务端下发的顺序原样显示，置顶只在会话 Tab 生效。现在置顶的群要排到
 *  「我的群聊」最前面；取消置顶后要回到原来的位置，所以排序函数只给出显示顺序，
 *  不改动群列表本身。
 *
 *  用 Catch2 v3（amalgamated）作框架，纯内存断言，不创建窗口 / 不发协议。
 *
 *  balloonwj@qq.com   2026-09-30
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <vector>

#include "../UI/BalloonMain/GroupListOps.h"

using namespace balloonmain;

namespace {

// 造一个群；pinned 为本人对它的置顶状态。
net::UserGroupItem MakeGroup(int64_t groupId, int pinned)
{
    net::UserGroupItem g;
    g.m_groupid = groupId;
    g.m_pinned  = pinned;
    return g;
}

// 按显示顺序取出各群的群ID，便于整体比较。
std::vector<int64_t> OrderedIds(const std::vector<net::UserGroupItem>& groups)
{
    const std::vector<size_t> order = OrderGroupsForList(groups);
    std::vector<int64_t> ids;
    for (size_t i = 0; i < order.size(); ++i)
    {
        ids.push_back(groups[order[i]].m_groupid);
    }
    return ids;
}

} // anonymous namespace

TEST_CASE("GL1 置顶的群排在前面，两组内部各自保持原顺序", "[grouplist][grouppin]")
{
    std::vector<net::UserGroupItem> groups;
    groups.push_back(MakeGroup(11, 0));
    groups.push_back(MakeGroup(12, 1));
    groups.push_back(MakeGroup(13, 0));
    groups.push_back(MakeGroup(14, 1));
    groups.push_back(MakeGroup(15, 0));

    std::vector<int64_t> expected;
    expected.push_back(12);
    expected.push_back(14);
    expected.push_back(11);
    expected.push_back(13);
    expected.push_back(15);
    CHECK(OrderedIds(groups) == expected);

    //排序只给出显示顺序，群列表本身不动
    CHECK(groups[0].m_groupid == 11);
    CHECK(groups[1].m_groupid == 12);

    //一个都没置顶：原样
    std::vector<net::UserGroupItem> plain;
    plain.push_back(MakeGroup(21, 0));
    plain.push_back(MakeGroup(22, 0));
    std::vector<int64_t> plainExpected;
    plainExpected.push_back(21);
    plainExpected.push_back(22);
    CHECK(OrderedIds(plain) == plainExpected);

    //空列表
    std::vector<net::UserGroupItem> empty;
    CHECK(OrderGroupsForList(empty).empty());
}

TEST_CASE("GL2 改置顶状态：有变化返回 true，没变化或群不存在返回 false", "[grouplist][grouppin]")
{
    std::vector<net::UserGroupItem> groups;
    groups.push_back(MakeGroup(11, 0));
    groups.push_back(MakeGroup(12, 0));
    groups.push_back(MakeGroup(13, 0));

    CHECK(SetUserGroupPinned(groups, 13, true));
    CHECK(groups[2].m_pinned == 1);
    CHECK(IsUserGroupPinned(groups, 13));

    //本就是这个状态
    CHECK_FALSE(SetUserGroupPinned(groups, 13, true));
    //群不存在（比如刚退群）
    CHECK_FALSE(SetUserGroupPinned(groups, 99, true));
    CHECK_FALSE(IsUserGroupPinned(groups, 99));

    //置顶后排到最前；取消置顶后回到原来的位置
    std::vector<int64_t> pinnedFirst;
    pinnedFirst.push_back(13);
    pinnedFirst.push_back(11);
    pinnedFirst.push_back(12);
    CHECK(OrderedIds(groups) == pinnedFirst);

    CHECK(SetUserGroupPinned(groups, 13, false));
    CHECK_FALSE(IsUserGroupPinned(groups, 13));
    std::vector<int64_t> original;
    original.push_back(11);
    original.push_back(12);
    original.push_back(13);
    CHECK(OrderedIds(groups) == original);
}
