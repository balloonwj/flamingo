/**
 *  GroupMemberScrollTests.cpp
 *
 *  群聊窗成员侧栏的滚动，以及它与聊天消息区共用的细滚动条几何（标签 [groupchat][memberscroll]）：
 *
 *    SC1 滑块几何：内容没有超出时没有滑块；超出时高度按比例、不短于 36，到顶贴上沿、到底贴下沿，
 *        宽 5、离右缘 2。
 *    SC2 滚动位置收进 [0, 内容高 - 可视高]，内容不超出时恒为 0。
 *    SC3 拖动：由滑块上沿反推滚动位置，与滑块几何互逆，误差不超过 1 像素。
 *    SC4 成员数超过边栏高度时滚动一段，每一行都按滚动量上移。
 *    SC5 滚轮每格滚 3 行，到顶 / 到底后不再继续；内容没有超出时不处理。
 *    SC6 重新填充成员时保持原滚动位置，行数变少超出范围时收回到新的底部。
 *
 *  用 Catch2 v3（amalgamated 形态）。全局 _Module 由 BalloonChatLayoutTests.cpp 统一定义。
 *
 *  balloonwj@qq.com   2026-10-01
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <memory>

#include "../UI/BalloonChatWnd/ChatScrollThumb.h"
#include "../UI/BalloonChatWnd/GroupMemberItem.h"
#include "../UI/BalloonChatWnd/GroupMemberList.h"

using namespace balloonchatwnd;

namespace {

// 侧栏宽度与可视高度（像素）：可视高度恰好容纳 10 行成员
const int kListW = 240;
const int kVisibleRows = 10;
const int kListH = kVisibleRows * kGroupMemberRowHeight;

// 造一个装着 rows 行成员的侧栏，并摆到 (0, 0, kListW, kListH)。
std::unique_ptr<GroupMemberList> MakeList(int rows)
{
    std::unique_ptr<GroupMemberList> list(new GroupMemberList());
    for (int i = 0; i < rows; ++i)
    {
        std::unique_ptr<GroupMemberItem> item(new GroupMemberItem());
        item->SetUserId(1000 + i);
        list->AddChild(std::move(item));
    }
    RECT rc = { 0, 0, kListW, kListH };
    list->SetRect(rc);
    return list;
}

// 侧栏里第 i 行的上沿。
int RowTop(GroupMemberList& list, size_t i)
{
    return list.Children()[i]->GetRect().top;
}

// 把侧栏的成员行全部摘掉，换成 rows 行新成员，再按原矩形强制重新排版（与群聊窗重填成员的做法相同）。
void Refill(GroupMemberList& list, int rows)
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
    for (int i = 0; i < rows; ++i)
    {
        std::unique_ptr<GroupMemberItem> item(new GroupMemberItem());
        item->SetUserId(2000 + i);
        list.AddChild(std::move(item));
    }
    list.ForceLayout(list.GetRect());
}

} // anonymous namespace

TEST_CASE("SC1 滑块几何：没超出时没有滑块，超出时按比例、贴两端、宽 5 离右缘 2", "[groupchat][memberscroll]")
{
    const RECT view = { 0, 0, kListW, kListH };

    //内容恰好等于可视高：没有滑块
    RECT t = ComputeChatScrollThumb(view, kListH, kListH, 0);
    CHECK(::IsRectEmpty(&t));

    //内容是可视高的两倍：滑块高为可视高的一半
    const int content = kListH * 2;
    t = ComputeChatScrollThumb(view, kListH, content, 0);
    CHECK(t.bottom - t.top == kListH / 2);
    CHECK(t.top == view.top);                                        //到顶贴上沿
    CHECK(t.right - t.left == kChatScrollBarWidth);
    CHECK(t.right == view.right - kChatScrollBarMargin);

    t = ComputeChatScrollThumb(view, kListH, content, content - kListH);
    CHECK(t.bottom == view.bottom);                                  //到底贴下沿

    //内容极长：滑块不短于 36
    t = ComputeChatScrollThumb(view, kListH, kListH * 100, 0);
    CHECK(t.bottom - t.top == kChatScrollMinThumb);
}

TEST_CASE("SC2 滚动位置收进合法范围，内容不超出时恒为 0", "[groupchat][memberscroll]")
{
    const int content = kListH * 3;
    CHECK(ClampChatScrollY(-10, kListH, content) == 0);
    CHECK(ClampChatScrollY(100, kListH, content) == 100);
    CHECK(ClampChatScrollY(content, kListH, content) == content - kListH);
    CHECK(ClampChatScrollY(50, kListH, kListH) == 0);
    CHECK(ClampChatScrollY(50, kListH, kListH / 2) == 0);
}

TEST_CASE("SC3 拖动：由滑块上沿反推滚动位置，与滑块几何互逆", "[groupchat][memberscroll]")
{
    const RECT view = { 0, 0, kListW, kListH };
    const int content = kListH * 3 + 17;   //不是整倍数，检验取整
    const int maxScroll = content - kListH;
    const int kStep = 7;   //逐个检验的滚动位置间隔（像素），取值无特殊含义
    for (int y = 0; y <= maxScroll; y += kStep)
    {
        INFO("scrollY=" << y);
        const RECT t = ComputeChatScrollThumb(view, kListH, content, y);
        const int back = ChatScrollYFromThumbTop(t.top, view, kListH, content);
        //滑块位置是取整后的像素，反推回来的滚动位置与原值之差不超过一个像素对应的滚动量
        const int thumbH = t.bottom - t.top;
        const int perPixel = maxScroll / (kListH - thumbH) + 1;
        CHECK(abs(back - y) <= perPixel);
    }
    //超出轨道两端按两端算
    CHECK(ChatScrollYFromThumbTop(-100, view, kListH, content) == 0);
    CHECK(ChatScrollYFromThumbTop(kListH * 10, view, kListH, content) == maxScroll);
}

TEST_CASE("SC4 成员多于可视行数时滚动一段，每一行按滚动量上移", "[groupchat][memberscroll]")
{
    const int rows = 20;
    std::unique_ptr<GroupMemberList> list = MakeList(rows);
    CHECK(list->ContentHeight() == rows * kGroupMemberRowHeight);
    CHECK(RowTop(*list, 0) == 0);

    const int kScroll = 57;   //滚动量（像素），不是行高的整倍数
    list->SetScrollY(kScroll);
    CHECK(list->ScrollY() == kScroll);
    for (size_t i = 0; i < list->Children().size(); ++i)
    {
        INFO("row=" << i);
        CHECK(RowTop(*list, i) == (int)i * kGroupMemberRowHeight - kScroll);
    }
}

TEST_CASE("SC5 滚轮每格滚 3 行，到顶到底后不再继续，内容不超出时不处理", "[groupchat][memberscroll]")
{
    std::unique_ptr<GroupMemberList> list = MakeList(20);
    const int maxScroll = list->ContentHeight() - kListH;
    POINT pt = { kListW / 2, kListH / 2 };

    CHECK(list->OnMouseWheel(pt, -WHEEL_DELTA, 0));   //向下滚一格
    CHECK(list->ScrollY() == kGroupMemberWheelRows * kGroupMemberRowHeight);

    for (int i = 0; i < 10; ++i)
    {
        list->OnMouseWheel(pt, -WHEEL_DELTA, 0);
    }
    CHECK(list->ScrollY() == maxScroll);              //到底后不再继续

    for (int i = 0; i < 10; ++i)
    {
        list->OnMouseWheel(pt, WHEEL_DELTA, 0);
    }
    CHECK(list->ScrollY() == 0);                      //到顶后不再继续

    //成员不多于可视行数：不处理，交给外层
    std::unique_ptr<GroupMemberList> shortList = MakeList(kVisibleRows);
    CHECK_FALSE(shortList->OnMouseWheel(pt, -WHEEL_DELTA, 0));
    CHECK(shortList->ScrollY() == 0);
}

TEST_CASE("SC6 重新填充成员时保持滚动位置，超出新范围时收回到新的底部", "[groupchat][memberscroll]")
{
    std::unique_ptr<GroupMemberList> list = MakeList(20);

    //行数不变（例如角色变更后重拉成员）：位置不变
    const int kScroll = 50;   //滚动量（像素），取值无特殊含义
    list->SetScrollY(kScroll);
    Refill(*list, 20);
    CHECK(list->ScrollY() == kScroll);
    CHECK(RowTop(*list, 0) == -kScroll);

    //滚到底后成员变少：收回到新的底部
    list->SetScrollY(list->ContentHeight());
    Refill(*list, 12);
    CHECK(list->ScrollY() == 12 * kGroupMemberRowHeight - kListH);

    //成员少到不再超出：回到顶部
    Refill(*list, 5);
    CHECK(list->ScrollY() == 0);
}

TEST_CASE("SC7 设了右侧让位宽度后，成员行与滑块都收在让位之内，滑块在那里能抓住", "[groupchat][memberscroll]")
{
    std::unique_ptr<GroupMemberList> list = MakeList(20);
    const int kInset = 9;   //让位宽度（像素）：缩放边框在 100% 缩放下的宽度量级
    list->SetRightInset(kInset);

    //成员行右缘让出 kInset
    CHECK(list->Children()[0]->GetRect().right == kListW - kInset);

    //滑块右缘距控件右缘 kInset + 边距
    const RECT view = { 0, 0, kListW - kInset, kListH };
    const RECT thumb = ComputeChatScrollThumb(view, kListH, list->ContentHeight(), 0);
    CHECK(thumb.right == kListW - kInset - kChatScrollBarMargin);

    //在滑块上按下开始拖动（窗口里这里已不在缩放边框内），拖到底后抬起
    POINT down = { (thumb.left + thumb.right) / 2, thumb.top + 2 };
    CHECK(list->OnLButtonDown(down, MK_LBUTTON));
    POINT move = { down.x, kListH * 2 };
    CHECK(list->OnMouseMove(move, MK_LBUTTON));
    CHECK(list->ScrollY() == list->ContentHeight() - kListH);
    CHECK(list->OnLButtonUp(move, 0));

    //让位宽度之外的那一条（原先滑块所在、窗口里属于缩放边框的位置）不再算命中带
    POINT outer = { kListW - kChatScrollBarMargin - 1, kListH / 2 };
    CHECK_FALSE(HitChatScrollBand(outer, thumb, view));
}
