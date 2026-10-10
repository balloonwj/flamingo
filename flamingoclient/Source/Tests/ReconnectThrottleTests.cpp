/**
 *  断线重连登录请求节流的单元测试（Suite RT），ReconnectThrottleTests.cpp
 *
 *  背景见 UI/BalloonMain/ReconnectThrottle.h：重连定时器原先每次触发都
 *  发一个登录请求，不管上一个有没有结果，请求可能在发送队列里越积越多。覆盖：
 *    RT1 一轮重连的第一次触发会发出请求；
 *    RT2 等结果期间，后续几次触发都不发；
 *    RT3 收到失败结果后，下一次触发重新发出；
 *    RT4 收到成功结果后清空状态，下一轮重连的第一次触发照常发出；
 *    RT5 迟到的结果没有清掉等待状态时，开始新一轮重连会把它重置；
 *    RT6 连续 kReconnectResultWaitTicks 次触发都没等到结果，下一次照常重发；
 *    RT7 请求没能入队（前置条件不满足）时不进入等待状态，下一次触发照常尝试。
 *
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-01
 */
#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include "../UI/BalloonMain/ReconnectThrottle.h"

using balloonmain::ReconnectThrottle;
using balloonmain::kReconnectResultWaitTicks;
using balloonmain::kReconnectTickResendStale;
using balloonmain::kReconnectTickSend;
using balloonmain::kReconnectTickWait;

namespace {

// 模拟一次"定时器触发并成功入队"：本次应当发出，发出后进入等待状态。
void SendAndQueue(ReconnectThrottle& t)
{
    REQUIRE(t.OnTick() == kReconnectTickSend);
    t.OnRequestQueued();
}

} // namespace

TEST_CASE("RT1 一轮重连的第一次触发会发出请求", "[reconnect]")
{
    ReconnectThrottle t;
    CHECK_FALSE(t.IsWaiting());
    CHECK(t.OnTick() == kReconnectTickSend);
}

TEST_CASE("RT2 等结果期间后续触发都不发", "[reconnect]")
{
    ReconnectThrottle t;
    SendAndQueue(t);
    CHECK(t.IsWaiting());

    const int kTicksWhileWaiting = 3;   // 少于兜底次数，都应当等待
    for (int i = 0; i < kTicksWhileWaiting; ++i)
    {
        CHECK(t.OnTick() == kReconnectTickWait);
    }
    CHECK(t.IsWaiting());
}

TEST_CASE("RT3 收到失败结果后下一次触发重新发出", "[reconnect]")
{
    ReconnectThrottle t;
    SendAndQueue(t);
    REQUIRE(t.OnTick() == kReconnectTickWait);

    t.OnResult();   // 登录失败的结果回来了
    CHECK_FALSE(t.IsWaiting());
    CHECK(t.OnTick() == kReconnectTickSend);
}

TEST_CASE("RT4 收到成功结果后下一轮重连照常发出", "[reconnect]")
{
    ReconnectThrottle t;
    SendAndQueue(t);
    t.OnResult();   // 登录成功，本轮结束（定时器由调用方停掉）
    CHECK_FALSE(t.IsWaiting());

    //过一阵又掉线，开始新一轮
    t.Reset();
    CHECK(t.OnTick() == kReconnectTickSend);
}

TEST_CASE("RT5 新一轮重连会重置上一轮遗留的等待状态", "[reconnect]")
{
    //请求发出后用户切到离线，结果回来时不在重连状态，等待状态没有被清掉
    ReconnectThrottle t;
    SendAndQueue(t);
    REQUIRE(t.IsWaiting());

    //再次掉线、开始新一轮：第一次触发必须照常发出，不能被遗留的等待状态挡住
    t.Reset();
    CHECK_FALSE(t.IsWaiting());
    CHECK(t.OnTick() == kReconnectTickSend);
}

TEST_CASE("RT6 久等无结果时照常重发", "[reconnect]")
{
    ReconnectThrottle t;
    SendAndQueue(t);

    //连续 kReconnectResultWaitTicks 次触发都在等
    for (int i = 0; i < kReconnectResultWaitTicks; ++i)
    {
        CHECK(t.OnTick() == kReconnectTickWait);
    }
    //再下一次判定为请求已丢失，重新发出，并退出等待状态
    CHECK(t.OnTick() == kReconnectTickResendStale);
    CHECK_FALSE(t.IsWaiting());

    //重发的那个请求入队后，重新开始计数
    t.OnRequestQueued();
    CHECK(t.OnTick() == kReconnectTickWait);
}

TEST_CASE("RT7 请求没能入队时下一次触发照常尝试", "[reconnect]")
{
    ReconnectThrottle t;
    //本次该发，但 Login() 没能入队（缺账号或密码等），调用方不调 OnRequestQueued
    REQUIRE(t.OnTick() == kReconnectTickSend);
    CHECK_FALSE(t.IsWaiting());
    CHECK(t.OnTick() == kReconnectTickSend);
}
