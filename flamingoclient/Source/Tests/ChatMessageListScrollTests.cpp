/**
 *  ChatMessageListScrollTests.cpp
 *
 *  聊天消息区「滚到底部」之后的滚动状态与滚动条滑块（标签 [chatlist][scrollbottom]）：
 *
 *    SB1 逐条追加一屏以上的消息后，再像聊天窗渲染完首屏历史那样调一次 ScrollToBottom：
 *        滚动位置恰为「内容高度 - 可视高度」，滑块落在可视区内并贴着可视区底部。
 *    SB2 只逐条追加、不额外调用 ScrollToBottom：同样停在底部，滑块可见。
 *
 *  背景：ScrollToBottom 原先只把滚动位置置为一个足够大的占位值、等下一次排版再夹紧；聊天窗在
 *  首屏历史渲染完之后调用它，之后并没有排版，滚动位置一直停在占位值上，滑块按占位值算到了几百万
 *  像素之外，表现为刚打开聊天窗时消息区没有滚动条，滚动一下才出现。
 *
 *  用 Catch2 v3（amalgamated 形态）。全局 _Module 由 BalloonChatLayoutTests.cpp 统一定义。
 *
 *  balloonwj@qq.com   2026-10-01
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include "../UI/BalloonChatWnd/ChatMessageList.h"
#include "../UI/BalloonChatWnd/ChatMockData.h"

using namespace balloonchatwnd;

namespace {

// 消息区尺寸（像素），取单聊窗默认大小下消息区的实际尺寸量级
const int kViewW = 620;
const int kViewH = 420;

// 追加的消息条数：足以超出一屏
const int kMessageCount = 30;

// 造一个摆好位置的消息区，逐条追加 kMessageCount 条双方交替的文字消息（与聊天窗渲染首屏历史的做法相同）。
void FillList(ChatMessageList& list)
{
    const RECT view = { 0, 0, kViewW, kViewH };
    list.ForceLayout(view);
    for (int i = 0; i < kMessageCount; ++i)
    {
        list.AppendMessage(MakeTextMessage((i % 2) == 0, _T("这是一条用来把消息区撑出滚动条的测试消息")));
    }
}

// 检查消息区停在底部、滑块可见且贴着可视区底部。
void CheckAtBottomWithThumb(const ChatMessageList& list)
{
    REQUIRE(list.ViewHeight() > 0);
    //前提：内容确实超出了可视区
    REQUIRE(list.ContentHeight() > list.ViewHeight());
    CHECK(list.ScrollY() == list.ContentHeight() - list.ViewHeight());

    const RECT thumb = list.ScrollThumbRect();
    INFO("scroll " << list.ScrollY() << ", thumb " << thumb.left << "," << thumb.top << ","
         << thumb.right << "," << thumb.bottom);
    CHECK(thumb.bottom > thumb.top);
    CHECK(thumb.top >= 0);
    CHECK(thumb.bottom == kViewH);
}

} // anonymous namespace

TEST_CASE("SB1 追加完消息后再调 ScrollToBottom：滚动位置夹紧到底部，滑块在可视区内", "[chatlist][scrollbottom]")
{
    ChatMessageList list;
    FillList(list);
    //聊天窗渲染完首屏历史后会再调一次（BalloonChatWindow / BalloonGroupChatWindow），之后不再排版
    list.ScrollToBottom();
    CheckAtBottomWithThumb(list);
}

TEST_CASE("SB2 只逐条追加消息：停在底部，滑块在可视区内", "[chatlist][scrollbottom]")
{
    ChatMessageList list;
    FillList(list);
    CheckAtBottomWithThumb(list);
}
