/**
 *  「禁止机器休眠」决策的单元测试（Suite KA），KeepAwakePolicyTests.cpp
 *
 *  覆盖 balloonmain::DecideKeepAwake 的完整判定表：
 *    KA1  开关关闭且未持有 —— 不执行任何动作（默认状态，多数情况下都走这条分支）；
 *    KA2  开关关闭但仍持有 —— 释放；
 *    KA3  开关开启但未登录 —— 不获取；若已持有则释放；
 *    KA4  开关开启且已登录、尚未持有 —— 获取；
 *    KA5  已持有、距上次合成不足一个间隔 —— 不执行任何动作；
 *    KA6  已持有、距上次合成恰好满一个间隔或已超过 —— 合成；
 *    KA7  从未合成过（tick 为 0）—— 立即合成，不产生一次不必要的等待；
 *    KA8  tick 回绕（开机约 49.7 天）时算出的经过时长仍然正确，不会漏掉合成。
 *
 *  框架 Catch2 v3。本套件只验证纯逻辑；Win32 API 那一层（电源请求与 SendInput）由
 *  tools/keepawake_probe.ps1 实测验收，不在此处覆盖。
 *
 *  balloonwj@qq.com   2026-08-12
 */
#include "stdafx.h"

#include "catch_amalgamated.hpp"
#include "../UI/BalloonMain/KeepAwakePolicy.h"

using balloonmain::KeepAwakeInput;
using balloonmain::kKeepAwakeNone;
using balloonmain::kKeepAwakeAcquire;
using balloonmain::kKeepAwakeRelease;
using balloonmain::kKeepAwakeNudge;
using balloonmain::kKeepAwakeNudgeIntervalMs;

namespace {

//构造一份"功能开启、已登录、已持有、刚刚合成过"的基准输入，各用例只修改自身关注的字段。
KeepAwakeInput MakeInput()
{
    KeepAwakeInput in;
    in.m_enabled       = true;
    in.m_loggedIn      = true;
    in.m_holding       = true;
    in.m_nowTick       = 1000000;
    in.m_lastNudgeTick = 1000000;
    return in;
}

} // namespace

TEST_CASE("KA1 开关关闭且未持有时不执行任何动作", "[keepawake]")
{
    KeepAwakeInput in = MakeInput();
    in.m_enabled = false;
    in.m_holding = false;

    //本功能默认关闭，多数情况下都走这条路径，它必须完全不产生动作。
    CHECK(balloonmain::DecideKeepAwake(in) == kKeepAwakeNone);
}

TEST_CASE("KA2 开关关闭后应释放电源请求", "[keepawake]")
{
    KeepAwakeInput in = MakeInput();
    in.m_enabled = false;
    in.m_holding = true;

    //用户在设置中关闭开关后，本机应恢复正常的休眠与锁屏行为，不能继续占用电源请求。
    CHECK(balloonmain::DecideKeepAwake(in) == kKeepAwakeRelease);
}

TEST_CASE("KA3 未登录时不占用系统电源请求", "[keepawake]")
{
    KeepAwakeInput in = MakeInput();
    in.m_loggedIn = false;

    //停留在登录界面（或已退出登录）的客户端没有理由阻止整台机器休眠。
    in.m_holding = false;
    CHECK(balloonmain::DecideKeepAwake(in) == kKeepAwakeNone);

    //退出登录时若仍持有，应当释放。
    in.m_holding = true;
    CHECK(balloonmain::DecideKeepAwake(in) == kKeepAwakeRelease);
}

TEST_CASE("KA4 开关开启且已登录时获取电源请求", "[keepawake]")
{
    KeepAwakeInput in = MakeInput();
    in.m_holding = false;

    CHECK(balloonmain::DecideKeepAwake(in) == kKeepAwakeAcquire);
}

TEST_CASE("KA5 距上次合成不足一个间隔时不重复合成", "[keepawake]")
{
    KeepAwakeInput in = MakeInput();
    in.m_lastNudgeTick = 1000000;
    in.m_nowTick       = in.m_lastNudgeTick + kKeepAwakeNudgeIntervalMs - 1;

    CHECK(balloonmain::DecideKeepAwake(in) == kKeepAwakeNone);
}

TEST_CASE("KA6 距上次合成满一个间隔即再次合成", "[keepawake]")
{
    KeepAwakeInput in = MakeInput();
    in.m_lastNudgeTick = 1000000;

    //边界判定用 >= ：恰好满一个间隔即应合成，不必再等一个定时器周期 —— 多等的这一个
    //周期长达 30 秒，足以让超时为 1 分钟的锁屏策略生效。
    in.m_nowTick = in.m_lastNudgeTick + kKeepAwakeNudgeIntervalMs;
    CHECK(balloonmain::DecideKeepAwake(in) == kKeepAwakeNudge);

    in.m_nowTick = in.m_lastNudgeTick + kKeepAwakeNudgeIntervalMs * 2;
    CHECK(balloonmain::DecideKeepAwake(in) == kKeepAwakeNudge);
}

TEST_CASE("KA7 从未合成过时立即合成", "[keepawake]")
{
    KeepAwakeInput in = MakeInput();
    in.m_lastNudgeTick = 0;
    in.m_nowTick       = 5;   //开机后极短时间，tick 数值很小

    //不能因为"当前 tick 减 0 尚不足一个间隔"而等待 —— 这段时间内没有任何机制阻止锁屏。
    CHECK(balloonmain::DecideKeepAwake(in) == kKeepAwakeNudge);
}

TEST_CASE("KA8 tick 回绕时仍能算出正确的经过时长", "[keepawake]")
{
    KeepAwakeInput in = MakeInput();

    //开机约 49.7 天后 GetTickCount 回绕归零。上次合成发生在回绕之前，当前已回绕：
    //无符号相减得到的仍是真实经过时长，不能因为"当前 tick 小于上次 tick"而误判。
    in.m_lastNudgeTick = 0xFFFFFFF0;                        //回绕前 16 毫秒
    in.m_nowTick       = kKeepAwakeNudgeIntervalMs;         //回绕后已超过一个间隔
    CHECK(balloonmain::DecideKeepAwake(in) == kKeepAwakeNudge);

    //同样跨越回绕，但真实经过时长尚不足一个间隔：不应合成。
    in.m_lastNudgeTick = 0xFFFFFFF0;
    in.m_nowTick       = 10;                                //真实经过时长仅 26 毫秒
    CHECK(balloonmain::DecideKeepAwake(in) == kKeepAwakeNone);
}
