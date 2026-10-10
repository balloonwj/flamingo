/**
 *  TrayConnectingBlinkTests.cpp
 *
 *  托盘图标「正在连接」亮灰交替的状态机 CTrayConnectingBlink 的测试。覆盖与用户确认过的四条用例
 *  （2026-10-03）：
 *    TCB1 进入「正在连接」时要求启动定时器，第一帧是亮色；
 *    TCB2 每次计时依次在灰色、亮色之间切换；
 *    TCB3 变为「在线」时停止、停在亮色，变为「离线」时停止、停在灰色；
 *    TCB4 正在连接时未读消息闪烁暂停，连上后恢复 —— 2026-10-03 按用户要求改为「连上后再等
 *         kTrayUnreadResumeDelayMs 才恢复」，悬停时的未读浮窗一并暂停；这部分状态由 CTrayUnreadHold
 *         管理，TCB4 与 TUH1～TUH3 测的是它。
 *
 *  用 Catch2 v3（amalgamated）作框架，纯内存断言，不创建窗口、不起定时器。
 *  全局 _Module 由 BalloonChatLayoutTests.cpp 统一定义，本文件只引用。
 *
 *  balloonwj@qq.com   2026-10-03
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include "../UI/BalloonMain/TrayConnectingBlink.h"
#include "../UI/BalloonMain/OnlineCapability.h"   // kConnUiOnline / kConnUiConnecting / kConnUiOffline

using namespace balloonmain;

namespace {

//TCB2 连续计时的次数：足够看出"来回切换"而不是只切一次，取值无特殊含义
const int kTickRounds = 10;

//ConnectionUiState 之外的一个越界取值，验证按离线处理
const int kConnUiOutOfRange = 99;

} // anonymous namespace

TEST_CASE("TCB1 进入「正在连接」时要求启动定时器，第一帧是亮色", "[trayblink]")
{
    SECTION("从离线进入（启动时连不上服务器、从离线恢复在线）")
    {
        CTrayConnectingBlink blink;
        //初始停在灰色，与尚未登录时的托盘图标一致
        CHECK_FALSE(blink.IsBlinking());
        CHECK(blink.CurrentFrame() == kTrayBlinkFrameGray);

        CHECK(blink.OnConnUiState(kConnUiOffline) == kTrayBlinkNone);
        CHECK(blink.OnConnUiState(kConnUiConnecting) == kTrayBlinkStart);
        CHECK(blink.IsBlinking());
        CHECK(blink.CurrentFrame() == kTrayBlinkFrameBright);
    }

    SECTION("从在线进入（掉线重连）")
    {
        CTrayConnectingBlink blink;
        CHECK(blink.OnConnUiState(kConnUiOnline) == kTrayBlinkNone);
        CHECK(blink.OnConnUiState(kConnUiConnecting) == kTrayBlinkStart);
        CHECK(blink.CurrentFrame() == kTrayBlinkFrameBright);
    }

    SECTION("重连期间反复通知「正在连接」不会重新开始，也不会打乱当前帧")
    {
        CTrayConnectingBlink blink;
        REQUIRE(blink.OnConnUiState(kConnUiConnecting) == kTrayBlinkStart);
        const TrayBlinkFrame afterTick = blink.Tick();
        CHECK(blink.OnConnUiState(kConnUiConnecting) == kTrayBlinkNone);
        CHECK(blink.IsBlinking());
        CHECK(blink.CurrentFrame() == afterTick);
    }
}

TEST_CASE("TCB2 每次计时依次在灰色、亮色之间切换", "[trayblink]")
{
    CTrayConnectingBlink blink;
    REQUIRE(blink.OnConnUiState(kConnUiConnecting) == kTrayBlinkStart);

    //第一帧亮色，之后每次计时取反：灰、亮、灰、亮……
    TrayBlinkFrame expect = kTrayBlinkFrameBright;
    for (int i = 0; i < kTickRounds; ++i)
    {
        INFO("第 " << (i + 1) << " 次计时");
        expect = (expect == kTrayBlinkFrameBright) ? kTrayBlinkFrameGray : kTrayBlinkFrameBright;
        CHECK(blink.Tick() == expect);
        CHECK(blink.CurrentFrame() == expect);
    }

    //不在交替中时计时不改变帧（定时器停掉前可能还排着一条 WM_TIMER）
    CTrayConnectingBlink idle;
    REQUIRE(idle.OnConnUiState(kConnUiOnline) == kTrayBlinkNone);
    CHECK(idle.Tick() == kTrayBlinkFrameBright);
    CHECK(idle.Tick() == kTrayBlinkFrameBright);
}

TEST_CASE("TCB3 变为在线时停止、停在亮色，变为离线时停止、停在灰色", "[trayblink]")
{
    SECTION("连上服务器：停在亮色，不论停止前正显示哪一帧")
    {
        CTrayConnectingBlink blink;
        REQUIRE(blink.OnConnUiState(kConnUiConnecting) == kTrayBlinkStart);
        REQUIRE(blink.Tick() == kTrayBlinkFrameGray);
        CHECK(blink.OnConnUiState(kConnUiOnline) == kTrayBlinkStop);
        CHECK_FALSE(blink.IsBlinking());
        CHECK(blink.CurrentFrame() == kTrayBlinkFrameBright);
        //停止之后计时不再切换
        CHECK(blink.Tick() == kTrayBlinkFrameBright);
    }

    SECTION("转为离线（用户主动下线、恢复在线失败）：停在灰色，不论停止前正显示哪一帧")
    {
        CTrayConnectingBlink blink;
        REQUIRE(blink.OnConnUiState(kConnUiConnecting) == kTrayBlinkStart);
        REQUIRE(blink.CurrentFrame() == kTrayBlinkFrameBright);
        CHECK(blink.OnConnUiState(kConnUiOffline) == kTrayBlinkStop);
        CHECK_FALSE(blink.IsBlinking());
        CHECK(blink.CurrentFrame() == kTrayBlinkFrameGray);
        CHECK(blink.Tick() == kTrayBlinkFrameGray);
    }

    SECTION("越界的连接态按离线处理")
    {
        CTrayConnectingBlink blink;
        REQUIRE(blink.OnConnUiState(kConnUiConnecting) == kTrayBlinkStart);
        CHECK(blink.OnConnUiState(kConnUiOutOfRange) == kTrayBlinkStop);
        CHECK(blink.CurrentFrame() == kTrayBlinkFrameGray);
    }

    SECTION("在线与离线之间切换不涉及交替，但常态帧跟着连接态走")
    {
        CTrayConnectingBlink blink;
        CHECK(blink.OnConnUiState(kConnUiOnline) == kTrayBlinkNone);
        CHECK(blink.CurrentFrame() == kTrayBlinkFrameBright);
        CHECK(blink.OnConnUiState(kConnUiOffline) == kTrayBlinkNone);
        CHECK(blink.CurrentFrame() == kTrayBlinkFrameGray);
        CHECK(blink.OnConnUiState(kConnUiOnline) == kTrayBlinkNone);
        CHECK(blink.CurrentFrame() == kTrayBlinkFrameBright);
    }

    //常态帧与托盘图标文件名同一口径：只有在线是彩色
    CHECK(CTrayConnectingBlink::RestingFrameFor(kConnUiOnline) == kTrayBlinkFrameBright);
    CHECK(CTrayConnectingBlink::RestingFrameFor(kConnUiConnecting) == kTrayBlinkFrameGray);
    CHECK(CTrayConnectingBlink::RestingFrameFor(kConnUiOffline) == kTrayBlinkFrameGray);
    CHECK(CTrayConnectingBlink::RestingFrameFor(kConnUiOutOfRange) == kTrayBlinkFrameGray);
}

TEST_CASE("TCB4 正在连接时未读提醒暂停，连上后再等一会儿才恢复", "[trayblink]")
{
    CTrayUnreadHold hold;
    CHECK(hold.OnConnUiState(kConnUiOnline) == kTrayUnreadHoldNone);
    CHECK(hold.AllowUnread());

    //掉线重连：未读闪烁与悬停的未读浮窗都暂停（未读本身保留）
    CHECK(hold.OnConnUiState(kConnUiConnecting) == kTrayUnreadHoldNone);
    CHECK_FALSE(hold.AllowUnread());

    //连上：不立即恢复，要求启动恢复定时器，此时仍暂停
    CHECK(hold.OnConnUiState(kConnUiOnline) == kTrayUnreadHoldStartResume);
    CHECK(hold.IsResumePending());
    CHECK_FALSE(hold.AllowUnread());

    //恢复定时器到点：放行
    hold.OnResumeTimer();
    CHECK(hold.AllowUnread());
    CHECK_FALSE(hold.IsResumePending());
}

TEST_CASE("TUH1 等待恢复期间又断开：作废这次计时，再连上时重新计时", "[trayblink][unreadhold]")
{
    CTrayUnreadHold hold;
    REQUIRE(hold.OnConnUiState(kConnUiConnecting) == kTrayUnreadHoldNone);
    REQUIRE(hold.OnConnUiState(kConnUiOnline) == kTrayUnreadHoldStartResume);

    //还没到点又断了：停掉恢复定时器，继续暂停
    CHECK(hold.OnConnUiState(kConnUiConnecting) == kTrayUnreadHoldCancelResume);
    CHECK_FALSE(hold.IsResumePending());
    CHECK_FALSE(hold.AllowUnread());

    //被作废的那次计时如果还有一条排队的到点消息，不能把暂停解除
    hold.OnResumeTimer();
    CHECK_FALSE(hold.AllowUnread());

    //再次连上：重新计时
    CHECK(hold.OnConnUiState(kConnUiOnline) == kTrayUnreadHoldStartResume);
    hold.OnResumeTimer();
    CHECK(hold.AllowUnread());
}

TEST_CASE("TUH2 同一个连接态重复通知不会重复计时", "[trayblink][unreadhold]")
{
    CTrayUnreadHold hold;
    REQUIRE(hold.OnConnUiState(kConnUiConnecting) == kTrayUnreadHoldNone);
    CHECK(hold.OnConnUiState(kConnUiConnecting) == kTrayUnreadHoldNone);
    REQUIRE(hold.OnConnUiState(kConnUiOnline) == kTrayUnreadHoldStartResume);
    //等待恢复期间又刷新了一次（例如改了在线状态）：计时照旧，不重新开始
    CHECK(hold.OnConnUiState(kConnUiOnline) == kTrayUnreadHoldNone);
    CHECK(hold.IsResumePending());
    //首次登录（离线 → 在线）没有暂停，不需要等待
    CTrayUnreadHold fresh;
    CHECK(fresh.OnConnUiState(kConnUiOffline) == kTrayUnreadHoldNone);
    CHECK(fresh.OnConnUiState(kConnUiOnline) == kTrayUnreadHoldNone);
    CHECK(fresh.AllowUnread());
}

TEST_CASE("TUH3 转为离线时立即放行，等待中的计时一并作废", "[trayblink][unreadhold]")
{
    //离线期间本来就收不到新消息，没有什么要等的
    CTrayUnreadHold connecting;
    REQUIRE(connecting.OnConnUiState(kConnUiConnecting) == kTrayUnreadHoldNone);
    CHECK(connecting.OnConnUiState(kConnUiOffline) == kTrayUnreadHoldNone);
    CHECK(connecting.AllowUnread());

    CTrayUnreadHold waiting;
    REQUIRE(waiting.OnConnUiState(kConnUiConnecting) == kTrayUnreadHoldNone);
    REQUIRE(waiting.OnConnUiState(kConnUiOnline) == kTrayUnreadHoldStartResume);
    CHECK(waiting.OnConnUiState(kConnUiOffline) == kTrayUnreadHoldCancelResume);
    CHECK(waiting.AllowUnread());
    CHECK_FALSE(waiting.IsResumePending());

    //恢复等待时长与用户确认的约 3 秒一致
    CHECK(kTrayUnreadResumeDelayMs == 3000u);
}
