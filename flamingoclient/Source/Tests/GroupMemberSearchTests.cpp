/**
 *  GroupMemberSearchTests.cpp
 *
 *  群聊窗右侧成员侧栏的搜索过滤（标签 [groupchat][membersearch]）：
 *
 *    MS1 匹配规则：名字子串、拼音首字母、全拼命中，英文不区分大小写，首尾空白忽略，空关键词全部命中。
 *    MS2 过滤后只有命中的行可见，从顶部起连续排列、中间不留空行；没命中的行矩形为空；
 *        内容高度 = 可见行数 × 行高。
 *    MS3 清空关键词后所有行恢复显示，顺序与过滤前相同。
 *    MS4 已经滚到下面时输入关键词，滚动位置回到顶部；命中的行一屏放得下时内容不超出（不显示滑块）。
 *    MS5 保留关键词期间成员列表重建，重建后再按关键词过滤，仍只显示命中的行。
 *    MS6 有关键词且一个都没命中时 HasNoMatches() 为真；关键词为空（或只有空白）时为假。
 *    MS7 被隐藏的行照常接收在线状态更新，清空关键词后显示的是最新状态。
 *
 *  用 Catch2 v3（amalgamated 形态）。全局 _Module 由 BalloonChatLayoutTests.cpp 统一定义。
 *
 *  balloonwj@qq.com   2026-10-01
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <memory>
#include <vector>

#include "../UI/BalloonChatWnd/GroupMemberItem.h"
#include "../UI/BalloonChatWnd/GroupMemberList.h"

using namespace balloonchatwnd;

namespace {

// 侧栏宽度与可视高度（像素）：可视高度恰好容纳 10 行成员
const int kListW = 240;
const int kVisibleRows = 10;
const int kListH = kVisibleRows * kGroupMemberRowHeight;

// 成员行的 userid 起点，取值无特殊含义
const int64_t kFirstUserId = 1000;

// 群角色取值（与 grouprole::NetGroupRole 一致）：0 普通成员、1 管理员、2 群主
const int kRoleMember = 0;
const int kRoleAdmin = 1;
const int kRoleOwner = 2;

// 一名测试成员：名字与角色
struct SampleMember
{
    const wchar_t* m_name;   // 显示的名字
    int            m_role;   // 角色，取值见上面三个常量
};

// 按群主 → 管理员 → 普通成员排好序的一组成员，与群聊窗填充侧栏后的顺序一致。
// 其中两人姓苏，用来验证过滤后的行连续排列
const SampleMember kSampleMembers[] =
{
    { L"张勇",   kRoleOwner },
    { L"冯杰英", kRoleAdmin },
    { L"邱文",   kRoleMember },
    { L"苏沐辰", kRoleMember },
    { L"刘雪军", kRoleMember },
    { L"苏健",   kRoleMember },
    { L"Alice",  kRoleMember },
};
const size_t kSampleCount = sizeof(kSampleMembers) / sizeof(kSampleMembers[0]);

// 造一个成员行。
std::unique_ptr<GroupMemberItem> MakeItem(int64_t userid, const wchar_t* name, int role)
{
    std::unique_ptr<GroupMemberItem> item(new GroupMemberItem());
    item->SetUserId(userid);
    item->SetMember(CString(name), CString(name).Left(1), RGB(0x40, 0x80, 0xC0), role);
    return item;
}

// 造一个装着 kSampleMembers 的侧栏，并摆到 (0, 0, kListW, kListH)。
std::unique_ptr<GroupMemberList> MakeSampleList()
{
    std::unique_ptr<GroupMemberList> list(new GroupMemberList());
    for (size_t i = 0; i < kSampleCount; ++i)
    {
        list->AddChild(MakeItem(kFirstUserId + (int64_t)i, kSampleMembers[i].m_name,
                                kSampleMembers[i].m_role));
    }
    RECT rc = { 0, 0, kListW, kListH };
    list->SetRect(rc);
    return list;
}

// 侧栏里第 i 行。
GroupMemberItem* ItemAt(GroupMemberList& list, size_t i)
{
    return dynamic_cast<GroupMemberItem*>(list.Children()[i].get());
}

// 侧栏里可见的成员行的名字，按显示顺序。
std::vector<CString> VisibleNames(GroupMemberList& list)
{
    std::vector<CString> names;
    for (size_t i = 0; i < list.Children().size(); ++i)
    {
        GroupMemberItem* item = ItemAt(list, i);
        if (item != nullptr && item->IsVisible())
        {
            names.push_back(item->Name());
        }
    }
    return names;
}

// 控件矩形是否有面积。
bool HasArea(const balloonwjui::DuiControl* c)
{
    const RECT rc = c->GetRect();
    return rc.right > rc.left && rc.bottom > rc.top;
}

// 把侧栏的成员行全部摘掉，换成 names 里的新成员（都按普通成员），再按原矩形强制重新排版，
// 与群聊窗收到成员列表后重建侧栏的做法相同。
void Refill(GroupMemberList& list, const std::vector<const wchar_t*>& names)
{
    std::vector<balloonwjui::DuiControl*> old;
    for (size_t i = 0; i < list.Children().size(); ++i)
    {
        old.push_back(list.Children()[i].get());
    }
    for (size_t i = 0; i < old.size(); ++i)
    {
        list.RemoveChild(old[i]);
    }
    for (size_t i = 0; i < names.size(); ++i)
    {
        list.AddChild(MakeItem(kFirstUserId + (int64_t)i, names[i], kRoleMember));
    }
    list.ForceLayout(list.GetRect());
}

} // anonymous namespace

TEST_CASE("MS1 成员搜索的匹配规则：子串、拼音首字母、全拼，不区分大小写，忽略首尾空白", "[groupchat][membersearch]")
{
    const CString su(L"苏沐辰");
    CHECK(GroupMemberNameMatches(su, L"苏沐"));         //名字子串
    CHECK(GroupMemberNameMatches(su, L"smc"));          //拼音首字母
    CHECK(GroupMemberNameMatches(su, L"SMC"));          //英文不区分大小写
    CHECK(GroupMemberNameMatches(su, L"sumuchen"));     //全拼
    CHECK(GroupMemberNameMatches(su, L"  苏沐  "));     //首尾空白忽略
    CHECK(GroupMemberNameMatches(su, L""));             //空关键词全部命中
    CHECK(GroupMemberNameMatches(su, L"   "));          //只有空白等同空关键词
    CHECK(GroupMemberNameMatches(CString(L"Alice"), L"ALI"));
    CHECK_FALSE(GroupMemberNameMatches(su, L"冯杰"));
    CHECK_FALSE(GroupMemberNameMatches(su, L"不存在的人"));
}

TEST_CASE("MS2 过滤后命中的行从顶部起连续排列，没命中的行隐藏且矩形为空", "[groupchat][membersearch]")
{
    std::unique_ptr<GroupMemberList> list = MakeSampleList();
    list->SetFilterKeyword(L"苏");

    const std::vector<CString> names = VisibleNames(*list);
    REQUIRE(names.size() == 2);
    CHECK(names[0] == CString(L"苏沐辰"));
    CHECK(names[1] == CString(L"苏健"));
    CHECK(list->VisibleRowCount() == 2);
    CHECK(list->ContentHeight() == 2 * kGroupMemberRowHeight);

    //可见的两行紧挨着排在最上面，中间不留被隐藏的行的空位
    int expectTop = 0;
    for (size_t i = 0; i < list->Children().size(); ++i)
    {
        GroupMemberItem* item = ItemAt(*list, i);
        REQUIRE(item != nullptr);
        if (item->IsVisible())
        {
            CHECK(item->GetRect().top == expectTop);
            CHECK(item->GetRect().bottom == expectTop + kGroupMemberRowHeight);
            expectTop += kGroupMemberRowHeight;
        }
        else
        {
            //隐藏的行矩形为空，鼠标点不到
            CHECK_FALSE(HasArea(item));
        }
    }
}

TEST_CASE("MS3 清空关键词后所有行恢复显示，顺序与过滤前相同", "[groupchat][membersearch]")
{
    std::unique_ptr<GroupMemberList> list = MakeSampleList();
    list->SetFilterKeyword(L"苏");
    REQUIRE(list->VisibleRowCount() == 2);

    list->SetFilterKeyword(L"");
    REQUIRE(list->VisibleRowCount() == (int)kSampleCount);
    CHECK(list->ContentHeight() == (int)kSampleCount * kGroupMemberRowHeight);
    for (size_t i = 0; i < kSampleCount; ++i)
    {
        GroupMemberItem* item = ItemAt(*list, i);
        REQUIRE(item != nullptr);
        CHECK(item->IsVisible());
        CHECK(item->Name() == CString(kSampleMembers[i].m_name));
        CHECK(item->GetRect().top == (int)i * kGroupMemberRowHeight);
    }
}

TEST_CASE("MS4 滚到下面时输入关键词，滚动位置回到顶部，命中的行放得下时内容不超出", "[groupchat][membersearch]")
{
    //30 名成员，其中只有第 25 名姓苏
    const int kRows = 30;
    const int kSuIndex = 25;
    std::vector<const wchar_t*> names;
    for (int i = 0; i < kRows; ++i)
    {
        names.push_back(i == kSuIndex ? L"苏沐辰" : L"张勇");
    }
    std::unique_ptr<GroupMemberList> list(new GroupMemberList());
    RECT rc = { 0, 0, kListW, kListH };
    list->SetRect(rc);
    Refill(*list, names);

    //先滚到底
    list->SetScrollY(kRows * kGroupMemberRowHeight);
    REQUIRE(list->ScrollY() > 0);

    list->SetFilterKeyword(L"smc");
    CHECK(list->ScrollY() == 0);
    CHECK(list->VisibleRowCount() == 1);
    //内容不超出可视高度，滚动条滑块不会画出来
    CHECK(list->ContentHeight() <= kListH);
    GroupMemberItem* su = ItemAt(*list, (size_t)kSuIndex);
    REQUIRE(su != nullptr);
    CHECK(su->IsVisible());
    CHECK(su->GetRect().top == 0);
}

TEST_CASE("MS5 保留关键词期间成员列表重建，重建后仍只显示命中的行", "[groupchat][membersearch]")
{
    std::unique_ptr<GroupMemberList> list = MakeSampleList();
    list->SetFilterKeyword(L"苏");
    REQUIRE(list->VisibleRowCount() == 2);

    //成员变动推送：列表整体重建，新成员里多了一位姓苏的
    std::vector<const wchar_t*> names;
    names.push_back(L"张勇");
    names.push_back(L"苏沐辰");
    names.push_back(L"苏晴");
    names.push_back(L"苏健");
    names.push_back(L"刘雪军");
    Refill(*list, names);
    list->ReapplyFilter();

    CHECK(list->FilterKeyword() == CString(L"苏"));
    const std::vector<CString> visible = VisibleNames(*list);
    REQUIRE(visible.size() == 3);
    CHECK(visible[0] == CString(L"苏沐辰"));
    CHECK(visible[1] == CString(L"苏晴"));
    CHECK(visible[2] == CString(L"苏健"));
    CHECK(list->ContentHeight() == 3 * kGroupMemberRowHeight);
}

TEST_CASE("MS6 有关键词且一个都没命中时 HasNoMatches 为真，关键词为空时为假", "[groupchat][membersearch]")
{
    std::unique_ptr<GroupMemberList> list = MakeSampleList();
    CHECK_FALSE(list->HasNoMatches());

    list->SetFilterKeyword(L"不存在的人");
    CHECK(list->HasNoMatches());
    CHECK(list->VisibleRowCount() == 0);
    CHECK(list->ContentHeight() == 0);

    list->SetFilterKeyword(L"苏");
    CHECK_FALSE(list->HasNoMatches());

    //只有空白等同空关键词：全部显示，不算"没有命中"
    list->SetFilterKeyword(L"   ");
    CHECK_FALSE(list->HasNoMatches());
    CHECK(list->VisibleRowCount() == (int)kSampleCount);

    //成员还没到齐（侧栏为空）且没有关键词时，不显示"没有找到匹配的成员"
    GroupMemberList empty;
    CHECK_FALSE(empty.HasNoMatches());
}

TEST_CASE("MS7 被隐藏的行照常接收在线状态更新，清空关键词后显示最新状态", "[groupchat][membersearch]")
{
    //在线状态取值同协议 presence_type：0 离线、1 在线
    const int kPresenceOffline = 0;
    const int kPresenceOnline = 1;

    std::unique_ptr<GroupMemberList> list = MakeSampleList();
    REQUIRE(list->SetMemberPresence(kFirstUserId, kPresenceOffline));

    //「苏」把第一行的张勇隐藏
    list->SetFilterKeyword(L"苏");
    GroupMemberItem* zhang = ItemAt(*list, 0);
    REQUIRE(zhang != nullptr);
    REQUIRE_FALSE(zhang->IsVisible());

    //隐藏期间张勇上线
    CHECK(list->SetMemberPresence(kFirstUserId, kPresenceOnline));

    list->SetFilterKeyword(L"");
    CHECK(zhang->IsVisible());
    CHECK(zhang->Presence() == kPresenceOnline);
}
