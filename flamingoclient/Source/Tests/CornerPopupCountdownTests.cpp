/**
 *  右下角弹窗倒计时的单元测试（Suite CPT，标签 [cornerpopup]），CornerPopupCountdownTests.cpp
 *
 *  覆盖 UI/BalloonMain/CornerPopupCountdown（docs/屏幕共享与录制方案.md 第 15 期 15g）：
 *    - CPT1 剩余整秒数向上取整，不足 0 时为 0；
 *    - CPT2 标题写法：不足一分钟写「N 秒后关闭」，一分钟及以上写「M:SS 后关闭」，来电写「N 秒后无人接听自动结束」，
 *           到时写「即将关闭」「即将自动结束」；
 *    - CPT3 倒计时：开始、到时、暂停后冻结且不到时、重新开始、停止。
 *
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-03
 */
#include "catch_amalgamated.hpp"

#include "../UI/BalloonMain/CornerPopupCountdown.h"

using namespace balloonmain;

TEST_CASE("CPT1 剩余整秒数向上取整，不足 0 时为 0", "[cornerpopup]")
{
    CHECK(CornerCountdownSeconds(58000) == 58);
    CHECK(CornerCountdownSeconds(57001) == 58);
    CHECK(CornerCountdownSeconds(1) == 1);
    CHECK(CornerCountdownSeconds(0) == 0);
    CHECK(CornerCountdownSeconds(-5) == 0);
}

TEST_CASE("CPT2 标题写法：秒、分秒、来电与到时", "[cornerpopup]")
{
    CHECK(MakeCornerCountdownTitle(L"屏幕共享", 58000, corner_countdown_close) == L"屏幕共享 · 58 秒后关闭");
    CHECK(MakeCornerCountdownTitle(L"屏幕录制", 118000, corner_countdown_close) == L"屏幕录制 · 1:58 后关闭");
    CHECK(MakeCornerCountdownTitle(L"屏幕录制", 60000, corner_countdown_close) == L"屏幕录制 · 1:00 后关闭");
    CHECK(MakeCornerCountdownTitle(L"屏幕录制", 59001, corner_countdown_close) == L"屏幕录制 · 1:00 后关闭");
    CHECK(MakeCornerCountdownTitle(L"屏幕录制", 125000, corner_countdown_close) == L"屏幕录制 · 2:05 后关闭");
    CHECK(MakeCornerCountdownTitle(L"特别关注", 6000, corner_countdown_close) == L"特别关注 · 6 秒后关闭");
    CHECK(MakeCornerCountdownTitle(L"语音通话", 42000, corner_countdown_ring) == L"语音通话 · 42 秒后无人接听自动结束");
    CHECK(MakeCornerCountdownTitle(L"视频通话", 65000, corner_countdown_ring) == L"视频通话 · 1:05 后无人接听自动结束");
    //到时：不写 0 秒
    CHECK(MakeCornerCountdownTitle(L"屏幕共享", 0, corner_countdown_close) == L"屏幕共享 · 即将关闭");
    CHECK(MakeCornerCountdownTitle(L"语音通话", -300, corner_countdown_ring) == L"语音通话 · 即将自动结束");
}

TEST_CASE("CPT3 倒计时：开始、到时、暂停后冻结、重新开始、停止", "[cornerpopup]")
{
    CornerCountdown c;
    CHECK_FALSE(c.IsRunning());
    CHECK(c.RemainingMs(1000) == 0);
    CHECK_FALSE(c.Expired(1000));

    c.Start(6000, 1000);
    CHECK(c.IsRunning());
    CHECK_FALSE(c.IsPaused());
    CHECK(c.RemainingMs(3000) == 4000);
    CHECK_FALSE(c.Expired(6999));
    CHECK(c.Expired(7000));
    CHECK(c.RemainingMs(9000) == 0);

    //暂停：剩余时间冻结，过多久都不到时
    c.Start(6000, 1000);
    c.Pause(3000);
    CHECK(c.IsPaused());
    CHECK(c.IsRunning());
    CHECK(c.RemainingMs(10000) == 4000);
    CHECK_FALSE(c.Expired(100000));

    //重新开始：取消暂停，从头倒数
    c.Start(6000, 10000);
    CHECK_FALSE(c.IsPaused());
    CHECK(c.RemainingMs(10000) == 6000);
    CHECK(c.Expired(16000));

    //停止
    c.Stop();
    CHECK_FALSE(c.IsRunning());
    CHECK(c.RemainingMs(10000) == 0);
    CHECK_FALSE(c.Expired(100000));
}
