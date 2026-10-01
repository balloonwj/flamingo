/**
 *  ShakeRateLimiterTests.cpp
 *
 *  窗口抖动节流器 ShakeRateLimiter 的单元测试。锁定其核心语义：
 *    · 首次必放行；
 *    · 间隔不足最小值时拦截；
 *    · 恰好达到最小间隔时放行（"不能小于 5 秒" = 至少 5 秒，5 秒整算够）；
 *    · 被拦截不刷新计时（间隔始终从上次"成功放行"算起）；
 *    · 每次成功放行都把基准前移；
 *    · Reset 后回到"从未放行"。
 *
 *  用 Catch2 v3（amalgamated 形态）。ShakeRateLimiter 为纯逻辑、无 Win32
 *  依赖，直接构造断言即可。全局 _Module 由 BalloonChatLayoutTests.cpp 统一
 *  定义，本文件只引用。
 *
 *  balloonwj@qq.com   2026-06-03
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include "../UI/BalloonChatWnd/ShakeRateLimiter.h"

using namespace balloonchatwnd;

namespace {

// 与产品一致的最小间隔（毫秒）：窗口抖动对同一目标至少间隔 5 秒。
const unsigned long long kInterval = 5000;

// 一个远大于 0 的基准时刻，模拟 GetTickCount64 的真实量级（避免贴着 0）。
const unsigned long long kBase = 1000000;

} // anonymous namespace

TEST_CASE("S1 首次调用必放行", "[shake][ratelimit]")
{
    ShakeRateLimiter limiter;
    CHECK(limiter.TryPass(kBase, kInterval));
}

TEST_CASE("S2 间隔不足时被拦截", "[shake][ratelimit]")
{
    ShakeRateLimiter limiter;
    REQUIRE(limiter.TryPass(kBase, kInterval));            // 先放行一次

    CHECK_FALSE(limiter.TryPass(kBase + 1, kInterval));    // 1ms 后
    CHECK_FALSE(limiter.TryPass(kBase + 4999, kInterval)); // 4999ms 后，仍不足 5s
}

TEST_CASE("S3 恰好达到最小间隔放行（5 秒整算够）", "[shake][ratelimit]")
{
    ShakeRateLimiter limiter;
    REQUIRE(limiter.TryPass(kBase, kInterval));

    // 距上次正好 5000ms —— 间隔"不小于 5 秒"，应放行。
    CHECK(limiter.TryPass(kBase + 5000, kInterval));
}

TEST_CASE("S4 被拦截不刷新计时", "[shake][ratelimit]")
{
    ShakeRateLimiter limiter;
    REQUIRE(limiter.TryPass(kBase, kInterval));            // 放行 @kBase

    // 中途多次被拦截（这些不应把"上次放行时刻"往后推）。
    CHECK_FALSE(limiter.TryPass(kBase + 2000, kInterval));
    CHECK_FALSE(limiter.TryPass(kBase + 4000, kInterval));

    // 距最初那次放行满 5000ms —— 即便中间被拦过，也应放行。
    CHECK(limiter.TryPass(kBase + 5000, kInterval));
}

TEST_CASE("S5 每次成功放行都把基准前移", "[shake][ratelimit]")
{
    ShakeRateLimiter limiter;
    REQUIRE(limiter.TryPass(kBase, kInterval));            // 放行 @kBase
    REQUIRE(limiter.TryPass(kBase + 5000, kInterval));     // 放行 @kBase+5000（基准前移）

    // 距新基准（kBase+5000）才 4999ms，应被拦截。
    CHECK_FALSE(limiter.TryPass(kBase + 5000 + 4999, kInterval));
    // 距新基准满 5000ms，放行。
    CHECK(limiter.TryPass(kBase + 5000 + 5000, kInterval));
}

TEST_CASE("S6 Reset 后回到从未放行", "[shake][ratelimit]")
{
    ShakeRateLimiter limiter;
    REQUIRE(limiter.TryPass(kBase, kInterval));
    CHECK_FALSE(limiter.TryPass(kBase + 1, kInterval));    // 处于节流窗内

    limiter.Reset();
    // 复位后下一次必放行，哪怕紧接着调用。
    CHECK(limiter.TryPass(kBase + 2, kInterval));
}


// ---- 2026-09-30：接收侧间隔比发送侧短，容纳网络延迟的波动 ----
//
// 发送、接收两侧原先都是 5 秒。对方严格按 5 秒间隔发出的两次抖动，到达本端时的
// 间隔会随网络延迟上下浮动；若第二次早到一点、间隔不足 5 秒，就被接收侧静默忽略：
// 不前置、不提示、不晃动，发送方却看到"你向对方发送了窗口抖动"。

TEST_CASE("ST1 接收侧最小间隔小于发送侧", "[shake][ratelimit]")
{
    INFO("send = " << kShakeSendMinIntervalMs << " ms, recv = " << kShakeRecvMinIntervalMs << " ms");
    CHECK(kShakeRecvMinIntervalMs < kShakeSendMinIntervalMs);
}

TEST_CASE("ST2 对方按发送侧间隔发出的第二次抖动早到一点仍被接收侧放行", "[shake][ratelimit]")
{
    // 对方两次发送正好间隔 5 秒，第二次因网络波动早到 200 毫秒，本端收到的间隔为 4.8 秒。
    const unsigned long long kJitterMs = 200;
    ShakeRateLimiter recv;
    REQUIRE(recv.TryPass(kBase, kShakeRecvMinIntervalMs));
    CHECK(recv.TryPass(kBase + kShakeSendMinIntervalMs - kJitterMs, kShakeRecvMinIntervalMs));
}

TEST_CASE("ST3 接收侧最小间隔之内的第二次抖动仍被拦截", "[shake][ratelimit]")
{
    // 防刷屏的作用保留：比接收侧最小间隔还短 1 毫秒的第二次抖动照样忽略。
    ShakeRateLimiter recv;
    REQUIRE(recv.TryPass(kBase, kShakeRecvMinIntervalMs));
    CHECK_FALSE(recv.TryPass(kBase + kShakeRecvMinIntervalMs - 1, kShakeRecvMinIntervalMs));
}
