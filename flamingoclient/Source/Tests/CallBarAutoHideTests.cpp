/**
 *  视频通话操作条自动隐藏时序的单元测试（标签 [callbar]，docs/音视频通话-界面美化方案.md 第 4.1 节），CallBarAutoHideTests.cpp
 *  覆盖 FlamingoMedia/Call/CallBarAutoHide.h：
 *    - BAH1 静止后淡出：Reset 后静止时长之内一直完全显示，之后在淡出时长内线性降到 0；下一次变化的等待时间依次为
 *           「剩余静止时长」「0（逐帧刷新）」「-1（不再变化）」；
 *    - BAH2 鼠标一动就淡入：完全隐藏时活动，在淡入时长内从 0 升到 255，并重新起算静止时长；
 *    - BAH3 淡出途中活动：从当时的不透明度开始淡入，不会先跳到 0 或 255；
 *    - BAH4 保持显示：保持期间超过静止时长也不淡出；解除保持后从解除时刻重新起算；淡出途中进入保持会淡入回来。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-06
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include "../../FlamingoMedia/Call/CallBarAutoHide.h"

using namespace call;

namespace {

//起始时刻（毫秒，任意取一个不为 0 的值）
const int64_t kBahStart = 500000;
//淡入淡出到一半时不透明度的允许误差（整数除法的取整）
const int kBahHalfTolerance = 1;
//「一半」的不透明度
const int kBahHalfAlpha = kCallBarAlphaOpaque / 2;

} // namespace

TEST_CASE("BAH1 静止后淡出", "[callbar]")
{
    CallBarAutoHide fade;
    fade.Reset(kBahStart);
    CHECK(fade.Alpha(kBahStart) == kCallBarAlphaOpaque);
    CHECK(fade.MsUntilNextChange(kBahStart) == kCallBarIdleMs);

    const int64_t beforeIdle = kBahStart + kCallBarIdleMs - 1;
    CHECK(fade.Alpha(beforeIdle) == kCallBarAlphaOpaque);
    CHECK(fade.MsUntilNextChange(beforeIdle) == 1);

    const int64_t fadeStart = kBahStart + kCallBarIdleMs;
    const int64_t halfOut = fadeStart + kCallBarFadeOutMs / 2;
    CHECK(fade.Alpha(halfOut) >= kBahHalfAlpha - kBahHalfTolerance);
    CHECK(fade.Alpha(halfOut) <= kBahHalfAlpha + kBahHalfTolerance);
    CHECK(fade.MsUntilNextChange(halfOut) == 0);

    const int64_t hidden = fadeStart + kCallBarFadeOutMs;
    CHECK(fade.Alpha(hidden) == 0);
    CHECK(fade.MsUntilNextChange(hidden) == -1);
    CHECK(fade.Alpha(hidden + kCallBarIdleMs) == 0);
}

TEST_CASE("BAH2 鼠标一动就淡入并重新计时", "[callbar]")
{
    CallBarAutoHide fade;
    fade.Reset(kBahStart);
    const int64_t hidden = kBahStart + kCallBarIdleMs + kCallBarFadeOutMs;
    REQUIRE(fade.Alpha(hidden) == 0);

    fade.OnActivity(hidden);
    CHECK(fade.Alpha(hidden) == 0);
    CHECK(fade.MsUntilNextChange(hidden) == 0);
    const int64_t halfIn = hidden + kCallBarFadeInMs / 2;
    CHECK(fade.Alpha(halfIn) >= kBahHalfAlpha - kBahHalfTolerance);
    CHECK(fade.Alpha(halfIn) <= kBahHalfAlpha + kBahHalfTolerance);
    const int64_t shown = hidden + kCallBarFadeInMs;
    CHECK(fade.Alpha(shown) == kCallBarAlphaOpaque);
    //静止时长从活动时刻起算
    CHECK(fade.MsUntilNextChange(shown) == kCallBarIdleMs - kCallBarFadeInMs);
    CHECK(fade.Alpha(hidden + kCallBarIdleMs - 1) == kCallBarAlphaOpaque);
    CHECK(fade.Alpha(hidden + kCallBarIdleMs + kCallBarFadeOutMs) == 0);

    //完全显示时的活动只重新计时
    fade.OnActivity(shown);
    CHECK(fade.Alpha(shown) == kCallBarAlphaOpaque);
    CHECK(fade.MsUntilNextChange(shown) == kCallBarIdleMs);
}

TEST_CASE("BAH3 淡出途中活动：从当时的不透明度淡入", "[callbar]")
{
    CallBarAutoHide fade;
    fade.Reset(kBahStart);
    const int64_t halfOut = kBahStart + kCallBarIdleMs + kCallBarFadeOutMs / 2;
    const int alphaAtActivity = fade.Alpha(halfOut);
    REQUIRE(alphaAtActivity > 0);
    REQUIRE(alphaAtActivity < kCallBarAlphaOpaque);

    fade.OnActivity(halfOut);
    CHECK(fade.Alpha(halfOut) == alphaAtActivity);
    const int64_t mid = halfOut + kCallBarFadeInMs / 2;
    CHECK(fade.Alpha(mid) > alphaAtActivity);
    CHECK(fade.Alpha(mid) < kCallBarAlphaOpaque);
    CHECK(fade.Alpha(halfOut + kCallBarFadeInMs) == kCallBarAlphaOpaque);
}

TEST_CASE("BAH4 保持显示", "[callbar]")
{
    CallBarAutoHide fade;
    fade.Reset(kBahStart);

    SECTION("保持期间不淡出，解除后重新起算")
    {
        fade.SetHold(true, kBahStart);
        CHECK(fade.IsHeld());
        const int64_t late = kBahStart + kCallBarIdleMs * 2;
        CHECK(fade.Alpha(late) == kCallBarAlphaOpaque);
        CHECK(fade.MsUntilNextChange(late) == -1);

        fade.SetHold(false, late);
        CHECK_FALSE(fade.IsHeld());
        CHECK(fade.Alpha(late + kCallBarIdleMs - 1) == kCallBarAlphaOpaque);
        CHECK(fade.MsUntilNextChange(late) == kCallBarIdleMs);
        CHECK(fade.Alpha(late + kCallBarIdleMs + kCallBarFadeOutMs) == 0);
    }

    SECTION("淡出途中进入保持：淡入回来")
    {
        const int64_t halfOut = kBahStart + kCallBarIdleMs + kCallBarFadeOutMs / 2;
        const int alphaAtHold = fade.Alpha(halfOut);
        fade.SetHold(true, halfOut);
        CHECK(fade.Alpha(halfOut) == alphaAtHold);
        CHECK(fade.MsUntilNextChange(halfOut) == 0);
        CHECK(fade.Alpha(halfOut + kCallBarFadeInMs) == kCallBarAlphaOpaque);
        CHECK(fade.MsUntilNextChange(halfOut + kCallBarFadeInMs) == -1);
    }

    SECTION("状态不变时什么也不做")
    {
        fade.SetHold(false, kBahStart + kCallBarIdleMs - 1);
        CHECK(fade.MsUntilNextChange(kBahStart) == kCallBarIdleMs);
    }
}
