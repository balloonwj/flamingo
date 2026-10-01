/**
 *  ChatSmoothScrollTests.cpp
 *
 *  消息列表平滑滚动的测试（标签 [smoothscroll]）。
 *
 *  覆盖：
 *    SS1 滚轮 delta 按比例换算成像素：一格 64 像素，delta=30 为 16 像素
 *    SS2 连续滚动时在当前目标上累加，而不是从当前位置重算
 *    SS3 目标夹紧在可滚动范围内；到顶 / 到底后反向滚立即生效
 *    SS4 动画开始时位置不变、到时长正好到达目标、中间单调前进且先快后慢
 *    SS5 滚到底、定位到某条消息、追加新消息都会取消进行中的动画
 *    SS6 动画进行中列表被销毁，之后动画管理器再推进也不会出问题
 *    SS7 列表头部插入内容（向上翻页）时，进行中的动画随之平移，视口内容不跳
 *    SS8 接进 ChatMessageList 之后：一格滚轮最终滚 64 像素，四次 delta=30 合计 64 像素
 *    SS9 滚到底之后还没重排时（位置还是哨兵值），第一格滚轮照样生效
 *
 *  SS1~SS4 直接测纯逻辑的 ChatSmoothScroll，时间由用例给定，结果与机器快慢无关。
 *  SS5~SS8 测 ChatMessageList，用 AdvanceSmoothScroll 把动画直接推到结束再断言。
 *  全局 _Module 由 BalloonChatLayoutTests.cpp 统一定义，本文件只引用。
 *
 *  balloonwj@qq.com   2026-09-30
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <memory>

#include "../UI/BalloonChatWnd/ChatSmoothScroll.h"
#include "../UI/BalloonChatWnd/ChatBubble.h"
#include "../UI/BalloonChatWnd/ChatMessageList.h"
#include "../UI/BalloonChatWnd/ChatMockData.h"
#include "../Common/Util/PerfStopWatch.h"
#include "../../../third-party/balloonui/balloonui/DuiAnimation.h"   // SS6：手动推进动画管理器

using namespace balloonchatwnd;

namespace {

// 一格滚轮对应的像素数，与 ChatMessageList.cpp 的 kWheelStep 一致。
const int kStep = 64;

// 一格滚轮的 delta（Windows 约定值），以及触控板一次常见的小幅 delta。
const int kNotch      = WHEEL_DELTA;
const int kSmallDelta = 30;

// 纯逻辑用例里的可滚动范围与起始位置（像素）。起始位置取在中间，上下都有余地。
const int kMaxScroll = 2000;
const int kStartPos  = 1000;

// 纯逻辑用例的起始时刻（毫秒，任意值）。
const double kT0 = 10000.0;

// 列表用例的尺寸（像素）与消息条数：条数足以撑出远大于视口的内容。
const int kListW    = 400;
const int kListH    = 300;
const int kMsgCount = 30;

// 把动画推到结束用的时间余量：远大于动画时长，推进一次必然走完。
const double kFarFutureMs = 100000.0;

// 造一个装满消息、排好版并停在底部的列表。
void FillList(ChatMessageList& list)
{
    for (int i = 0; i < kMsgCount; ++i)
    {
        list.AppendMessage(MakeTextMessage(i % 2 == 0,
            _T("这是一条用来撑出滚动范围的消息，长度足够折成两行，好让内容远远超出视口高度。")));
    }
    RECT rc = { 0, 0, kListW, kListH };
    list.ForceLayout(rc);
}

// 把列表的平滑滚动推到结束。
void FinishSmooth(ChatMessageList& list)
{
    list.AdvanceSmoothScroll(PerfNowMs() + kFarFutureMs);
}

} // anonymous namespace

// =====================================================================
// SS1~SS4：纯逻辑
// =====================================================================

TEST_CASE("SS1 滚轮 delta 按比例换算成像素", "[smoothscroll]")
{
    CHECK(ChatSmoothScroll::WheelDeltaToPixels(kNotch, kStep) == Catch::Approx(64.0));
    CHECK(ChatSmoothScroll::WheelDeltaToPixels(kSmallDelta, kStep) == Catch::Approx(16.0));
    CHECK(ChatSmoothScroll::WheelDeltaToPixels(-kNotch, kStep) == Catch::Approx(-64.0));
    CHECK(ChatSmoothScroll::WheelDeltaToPixels(2 * kNotch, kStep) == Catch::Approx(128.0));

    //四次小幅滚动合计等于一格
    ChatSmoothScroll s;
    int pos = kStartPos;
    for (int i = 0; i < 4; ++i)
    {
        REQUIRE(s.OnWheel(pos, kMaxScroll, kSmallDelta, kStep, kT0 + i));
    }
    CHECK(s.Target() == Catch::Approx(kStartPos - 64.0));
}

TEST_CASE("SS2 连续滚动时在当前目标上累加", "[smoothscroll]")
{
    ChatSmoothScroll s;
    REQUIRE(s.OnWheel(kStartPos, kMaxScroll, kNotch, kStep, kT0));
    //动画才走了一小段，位置还没到目标
    const int midPos = s.Advance(kT0 + 20.0);
    REQUIRE(midPos > kStartPos - 64);
    REQUIRE(midPos < kStartPos);

    //第二格在第一格的目标上再加一格，而不是在当前位置上加
    REQUIRE(s.OnWheel(midPos, kMaxScroll, kNotch, kStep, kT0 + 20.0));
    CHECK(s.Target() == Catch::Approx(kStartPos - 128.0));
    CHECK(s.Advance(kT0 + 20.0 + kChatSmoothScrollDurationMs) == kStartPos - 128);
}

TEST_CASE("SS3 目标夹紧在可滚动范围内，到边界后反向滚立即生效", "[smoothscroll]")
{
    SECTION("已经在顶部时向上滚没有效果")
    {
        ChatSmoothScroll s;
        CHECK_FALSE(s.OnWheel(0, kMaxScroll, kNotch, kStep, kT0));
        CHECK_FALSE(s.IsActive());
    }

    SECTION("已经在底部时向下滚没有效果")
    {
        ChatSmoothScroll s;
        CHECK_FALSE(s.OnWheel(kMaxScroll, kMaxScroll, -kNotch, kStep, kT0));
        CHECK_FALSE(s.IsActive());
    }

    SECTION("靠近顶部时目标停在 0，反向滚的第一格立即生效")
    {
        const int nearTop = 30;
        ChatSmoothScroll s;
        //连滚三格：目标只到 0，不会记成 -162
        REQUIRE(s.OnWheel(nearTop, kMaxScroll, kNotch, kStep, kT0));
        s.OnWheel(nearTop, kMaxScroll, kNotch, kStep, kT0 + 1.0);
        s.OnWheel(nearTop, kMaxScroll, kNotch, kStep, kT0 + 2.0);
        CHECK(s.Target() == Catch::Approx(0.0));

        //立即反向滚一格：目标是 64，没有先抵消一段越界量
        REQUIRE(s.OnWheel(nearTop, kMaxScroll, -kNotch, kStep, kT0 + 3.0));
        CHECK(s.Target() == Catch::Approx(64.0));
    }

    SECTION("靠近底部时目标停在最大值")
    {
        ChatSmoothScroll s;
        REQUIRE(s.OnWheel(kMaxScroll - 10, kMaxScroll, -kNotch, kStep, kT0));
        CHECK(s.Target() == Catch::Approx((double)kMaxScroll));
    }
}

TEST_CASE("SS4 动画按时间推进：起点不动、终点到位、中间单调且先快后慢", "[smoothscroll]")
{
    ChatSmoothScroll s;
    REQUIRE(s.OnWheel(kStartPos, kMaxScroll, -2 * kNotch, kStep, kT0));
    const int target = kStartPos + 128;

    //开始时刻位置不变
    CHECK(s.Advance(kT0) == kStartPos);
    CHECK(s.IsActive());

    //中间逐帧单调前进
    int last = kStartPos;
    const double kFrameMs = 16.0;
    for (double t = kT0 + kFrameMs; t < kT0 + kChatSmoothScrollDurationMs; t += kFrameMs)
    {
        const int pos = s.Advance(t);
        CHECK(pos >= last);
        CHECK(pos <= target);
        last = pos;
    }

    //先快后慢：走到一半时间时，已经走完了一半以上的路程
    ChatSmoothScroll half;
    REQUIRE(half.OnWheel(kStartPos, kMaxScroll, -2 * kNotch, kStep, kT0));
    CHECK(half.Advance(kT0 + kChatSmoothScrollDurationMs / 2.0) > kStartPos + 64);

    //到时长正好到达目标，动画结束
    CHECK(s.Advance(kT0 + kChatSmoothScrollDurationMs) == target);
    CHECK_FALSE(s.IsActive());
    //结束后再推进，位置不再变化
    CHECK(s.Advance(kT0 + 10 * kChatSmoothScrollDurationMs) == target);
}

// =====================================================================
// SS5~SS8：接进 ChatMessageList 之后
// =====================================================================

TEST_CASE("SS5 直接改滚动位置的操作会取消进行中的动画", "[smoothscroll]")
{
    const POINT pt = { kListW / 2, kListH / 2 };

    SECTION("滚到底")
    {
        ChatMessageList list;
        FillList(list);
        const int bottom = list.ScrollY();
        list.OnMouseWheel(pt, (short)kNotch, 0);
        REQUIRE(list.IsSmoothScrolling());

        list.ScrollToBottom();
        CHECK_FALSE(list.IsSmoothScrolling());
        RECT rc = { 0, 0, kListW, kListH };
        list.ForceLayout(rc);
        //动画已被取消，之后再推进也不会把位置拉离底部
        FinishSmooth(list);
        CHECK(list.ScrollY() == bottom);
    }

    SECTION("追加新消息")
    {
        ChatMessageList list;
        FillList(list);
        list.OnMouseWheel(pt, (short)kNotch, 0);
        REQUIRE(list.IsSmoothScrolling());

        list.AppendMessage(MakeTextMessage(true, _T("新消息")));
        CHECK_FALSE(list.IsSmoothScrolling());
    }

    SECTION("定位到某条消息")
    {
        ChatMessageList list;
        for (int i = 0; i < kMsgCount; ++i)
        {
            ChatMessage m = MakeTextMessage(i % 2 == 0,
                _T("这是一条用来撑出滚动范围的消息，长度足够折成两行，好让内容远远超出视口高度。"));
            m.msgid = 700 + i;
            list.AppendMessage(m);
        }
        RECT rc = { 0, 0, kListW, kListH };
        list.ForceLayout(rc);
        list.OnMouseWheel(pt, (short)kNotch, 0);
        REQUIRE(list.IsSmoothScrolling());

        REQUIRE(list.ScrollToMsgid(705));
        CHECK_FALSE(list.IsSmoothScrolling());
    }
}

TEST_CASE("SS6 动画进行中列表被销毁，动画管理器再推进也不出问题", "[smoothscroll]")
{
    const POINT pt = { kListW / 2, kListH / 2 };
    {
        std::unique_ptr<ChatMessageList> list(new ChatMessageList());
        FillList(*list);
        list->OnMouseWheel(pt, (short)kNotch, 0);
        REQUIRE(list->IsSmoothScrolling());
    }
    //列表已经销毁，管理器里仍挂着它的逐帧驱动：推进一次、再推进到结束，都不能访问已销毁的列表
    balloonwjui::DuiAnimMgr::Inst().TickAll(::GetTickCount());
    balloonwjui::DuiAnimMgr::Inst().TickAll(::GetTickCount() + 10 * kChatSmoothScrollDurationMs);
    SUCCEED("no access to the destroyed list");
}

TEST_CASE("SS7 向上翻页插入内容时进行中的动画随之平移", "[smoothscroll]")
{
    SECTION("纯逻辑：起点与目标一起平移")
    {
        ChatSmoothScroll s;
        REQUIRE(s.OnWheel(kStartPos, kMaxScroll, kNotch, kStep, kT0));
        s.Shift(500);
        CHECK(s.Target() == Catch::Approx(kStartPos - 64.0 + 500.0));
        //平移之后，开始时刻的位置也随之平移
        CHECK(s.Advance(kT0) == kStartPos + 500);
    }

    SECTION("列表：翻页批次结束后，动画终点仍对着原来要看的那段内容")
    {
        const POINT pt = { kListW / 2, kListH / 2 };
        ChatMessageList list;
        FillList(list);
        list.OnMouseWheel(pt, (short)kNotch, 0);
        REQUIRE(list.IsSmoothScrolling());
        const int heightBefore = list.ContentHeight();
        const int posBefore    = list.ScrollY();

        list.BeginPrependBatch();
        for (int i = 0; i < 5; ++i)
        {
            list.AppendMessage(MakeTextMessage(false, _T("更早的一条消息")));
        }
        list.EndPrependBatch();
        const int added = list.ContentHeight() - heightBefore;
        REQUIRE(added > 0);

        FinishSmooth(list);
        //终点 = 原位置 - 一格 + 头部新增的高度
        CHECK(list.ScrollY() == posBefore - kStep + added);
    }
}

TEST_CASE("SS9 滚到底之后，第一格滚轮照样生效", "[smoothscroll]")
{
    //打开聊天窗渲染完历史后，最后一步是 ScrollToBottom。它原先只把位置设成一个很大的
    //哨兵值，要等下一次重排才归到真实的最大值；实测这时滚第一格，目标从哨兵值算起、
    //夹紧后正好是最大值，画面不动，第一格被吞掉了。2026-10-01 起 ScrollToBottom 当场
    //重排（哨兵值还会让滚动条滑块画到可视区之外），位置直接就是真实的最大值，
    //本用例改为核对这一点，并照旧确认第一格滚轮生效。
    const POINT pt = { kListW / 2, kListH / 2 };
    ChatMessageList list;
    FillList(list);
    const int maxScroll = list.ContentHeight() - list.ViewHeight();
    REQUIRE(maxScroll > kStep);

    list.ScrollToBottom();
    REQUIRE(list.ScrollY() == maxScroll);

    list.OnMouseWheel(pt, (short)kNotch, 0);
    FinishSmooth(list);
    CHECK(list.ScrollY() == maxScroll - kStep);
}

TEST_CASE("SS8 列表上一格滚轮最终滚 64 像素，四次小幅滚动合计 64 像素", "[smoothscroll]")
{
    const POINT pt = { kListW / 2, kListH / 2 };

    SECTION("一格")
    {
        ChatMessageList list;
        FillList(list);
        const int bottom = list.ScrollY();
        REQUIRE(bottom > kStep);
        list.OnMouseWheel(pt, (short)kNotch, 0);
        FinishSmooth(list);
        CHECK(list.ScrollY() == bottom - kStep);
        CHECK_FALSE(list.IsSmoothScrolling());
    }

    SECTION("触控板的四次小幅滚动")
    {
        ChatMessageList list;
        FillList(list);
        const int bottom = list.ScrollY();
        for (int i = 0; i < 4; ++i)
        {
            list.OnMouseWheel(pt, (short)kSmallDelta, 0);
        }
        FinishSmooth(list);
        CHECK(list.ScrollY() == bottom - kStep);
    }
}
