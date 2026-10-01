/**
 *  ForwardTargetModelTests.cpp
 *
 *  转发目标选择窗的纯逻辑模型测试（标签 [forward]），见 docs/聊天消息转发方案.md §3.2、§7.2。
 *
 *    FWD-C10a  已选集合：同一目标（会话类型 + id）从不同页签勾选只算一个，再勾一次即取消；
 *              达到上限后不能再选；自己、非法 id、非单聊非群聊的会话不能选。
 *    FWD-C10b  候选过滤：空查询返回全部；名字子串、拼音首字母、账号都能命中。
 *    FWD-C10c  「最近会话」候选：只保留单聊与群聊，去掉自己、非法 id 与重复项，保持原顺序。
 *
 *  用 Catch2 v3（amalgamated 形态），纯内存断言。
 *
 *  balloonwj@qq.com   2026-10-01
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <vector>

#include "../UI/BalloonMain/ForwardTargetModel.h"
#include "../net/Protocol.h"   // conversation_single / conversation_group / conversation_service

using namespace balloonmain;

namespace {

// 本人的 userid
const int64_t kSelfId = 100001;

// 造一个候选。ctype：会话类型；cid：会话 id；name：显示名；account：账号（群为空）。
ForwardTarget MakeTarget(int ctype, int64_t cid, const wchar_t* name, const wchar_t* account)
{
    ForwardTarget t;
    t.m_ctype = ctype;
    t.m_cid = cid;
    t.m_name = name;
    t.m_account = account;
    return t;
}

} // anonymous namespace

TEST_CASE("FWD-C10a 已选集合：去重、上限、不能选自己", "[forward]")
{
    ForwardTargetSelection sel(kSelfId);
    const ForwardTarget zhang = MakeTarget(conversation_single, 100002, L"张三", L"zhangsan");

    // 在「联系人」页勾选张三
    CHECK(sel.Toggle(zhang) == kForwardToggleAdded);
    CHECK(sel.IsSelected(conversation_single, 100002));
    CHECK(sel.Selected().size() == 1);

    // 同一个人在「最近会话」页也是已选状态；在那里再勾一次即取消，不会出现两个
    const ForwardTarget zhangRecent = MakeTarget(conversation_single, 100002, L"张三", L"");
    CHECK(sel.Toggle(zhangRecent) == kForwardToggleRemoved);
    CHECK_FALSE(sel.IsSelected(conversation_single, 100002));
    CHECK(sel.Selected().empty());

    // 单聊与群聊的 id 可能数值相同，但不是同一个目标
    CHECK(sel.Toggle(zhang) == kForwardToggleAdded);
    CHECK(sel.Toggle(MakeTarget(conversation_group, 100002, L"某群", L"")) == kForwardToggleAdded);
    CHECK(sel.Selected().size() == 2);

    // 选满上限
    for (int64_t i = 0; sel.Selected().size() < kForwardMaxTargets; ++i)
    {
        CHECK(sel.Toggle(MakeTarget(conversation_group, 900001 + i, L"群", L"")) == kForwardToggleAdded);
    }
    CHECK(sel.IsFull());
    CHECK(sel.Toggle(MakeTarget(conversation_single, 100099, L"再多一个", L"")) == kForwardToggleFull);
    CHECK(sel.Selected().size() == kForwardMaxTargets);

    // 满了之后仍能取消已选的，取消后又能再选
    CHECK(sel.Toggle(zhang) == kForwardToggleRemoved);
    CHECK_FALSE(sel.IsFull());
    CHECK(sel.Toggle(MakeTarget(conversation_single, 100099, L"再多一个", L"")) == kForwardToggleAdded);

    // 右侧已选列表里点移除
    CHECK(sel.Remove(conversation_single, 100099));
    CHECK_FALSE(sel.Remove(conversation_single, 100099));

    // 已选列表保持勾选的先后顺序
    ForwardTargetSelection order(kSelfId);
    order.Toggle(MakeTarget(conversation_group, 3, L"c", L""));
    order.Toggle(MakeTarget(conversation_single, 1, L"a", L""));
    REQUIRE(order.Selected().size() == 2);
    CHECK(order.Selected()[0].m_cid == 3);
    CHECK(order.Selected()[1].m_cid == 1);

    // 不能选的：自己、非法 id、服务号会话
    ForwardTargetSelection bad(kSelfId);
    CHECK(bad.Toggle(MakeTarget(conversation_single, kSelfId, L"我", L"")) == kForwardToggleInvalid);
    CHECK(bad.Toggle(MakeTarget(conversation_single, 0, L"无效", L"")) == kForwardToggleInvalid);
    CHECK(bad.Toggle(MakeTarget(conversation_service, 5, L"服务号", L"")) == kForwardToggleInvalid);
    CHECK(bad.Selected().empty());

    // Clear 清空
    order.Clear();
    CHECK(order.Selected().empty());
}

TEST_CASE("FWD-C10b 候选过滤：名字、拼音首字母、账号都能命中", "[forward]")
{
    std::vector<ForwardTarget> all;
    all.push_back(MakeTarget(conversation_single, 100002, L"张三", L"zhangsan01"));
    all.push_back(MakeTarget(conversation_group, 900001, L"项目组", L""));
    all.push_back(MakeTarget(conversation_single, 100003, L"李四", L"lisi"));

    // 空查询（含只有空白）：全部
    CHECK(FilterForwardTargets(all, L"").size() == 3);
    CHECK(FilterForwardTargets(all, L"  ").size() == 3);

    // 名字子串
    std::vector<size_t> byName = FilterForwardTargets(all, L"项目");
    REQUIRE(byName.size() == 1);
    CHECK(byName[0] == 1);

    // 拼音首字母
    std::vector<size_t> byInitials = FilterForwardTargets(all, L"zs");
    REQUIRE(byInitials.size() == 1);
    CHECK(byInitials[0] == 0);

    // 账号
    std::vector<size_t> byAccount = FilterForwardTargets(all, L"lisi");
    REQUIRE(byAccount.size() == 1);
    CHECK(byAccount[0] == 2);

    // 都不命中
    CHECK(FilterForwardTargets(all, L"不存在").empty());
}

TEST_CASE("FWD-C10c 最近会话候选：只留单聊与群聊，去掉自己、非法 id 与重复项", "[forward]")
{
    std::vector<ForwardTarget> convs;
    convs.push_back(MakeTarget(conversation_group, 900001, L"项目组", L""));
    convs.push_back(MakeTarget(conversation_single, kSelfId, L"我", L""));
    convs.push_back(MakeTarget(conversation_single, 100002, L"张三", L""));
    convs.push_back(MakeTarget(conversation_service, 7, L"系统通知", L""));
    convs.push_back(MakeTarget(conversation_single, 0, L"无效", L""));
    convs.push_back(MakeTarget(conversation_single, 100002, L"张三（重复）", L""));
    convs.push_back(MakeTarget(conversation_single, 100003, L"李四", L""));

    const std::vector<ForwardTarget> recent = BuildRecentForwardTargets(convs, kSelfId);
    REQUIRE(recent.size() == 3);
    CHECK(recent[0].m_cid == 900001);
    CHECK(recent[1].m_cid == 100002);
    CHECK(recent[1].m_name == L"张三");
    CHECK(recent[2].m_cid == 100003);
}
