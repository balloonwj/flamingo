/**
 *  TypingIndicatorTests.cpp
 *
 *  「对方正在输入」两个状态机的单元测试：发送侧节流器 TypingSendThrottle
 *  与接收侧状态 TypingPeerState。锁定与用户确认过的这几条行为：
 *    · 从空输入框敲第一个字立刻通告一次 state=1；
 *    · 持续打字在续发间隔内不重复发包，满间隔则续发一次；
 *    · 停手满闲置时长发一次 state=0，且不重复发；
 *    · 内容被删光立即发 state=0；
 *    · 消息发出后立即发 state=0 且状态复位（下次打字重新走"立刻发"）；
 *    · 接收侧收到 state=1 后若再无后续包，到期自动收起提示。
 *
 *  用 Catch2 v3（amalgamated 形态）。两个类都是纯逻辑、无 Win32 依赖，直接
 *  构造断言即可。全局 _Module 由 BalloonChatLayoutTests.cpp 统一定义，本文件
 *  只引用。
 *
 *  balloonwj@qq.com   2026-08-05
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include "../BalloonChatWnd/TypingIndicator.h"

using namespace balloonchatwnd;

namespace {

// 一个远大于 0 的基准时刻，模拟 GetTickCount64 的真实量级（避免贴着 0）。
const unsigned long long kBase = 1000000;

} // anonymous namespace

//==================== 发送侧 TypingSendThrottle ====================

TEST_CASE("T1 从空输入框敲第一个字立刻通告正在输入", "[typing]")
{
    TypingSendThrottle throttle;
    CHECK(throttle.OnInputChanged(kBase, false) == typing_action_send_typing);
    CHECK(throttle.IsActive());
}

TEST_CASE("T2 续发间隔内连续打字不重复发包", "[typing]")
{
    TypingSendThrottle throttle;
    REQUIRE(throttle.OnInputChanged(kBase, false) == typing_action_send_typing);

    // 间隔内的每一次按键都不该产生新包（否则一句话就是几十个包）
    CHECK(throttle.OnInputChanged(kBase + 1, false) == typing_action_none);
    CHECK(throttle.OnInputChanged(kBase + 1000, false) == typing_action_none);
    CHECK(throttle.OnInputChanged(kBase + kTypingRepeatMs - 1, false) == typing_action_none);
}

TEST_CASE("T3 持续打字满续发间隔则续发一次", "[typing]")
{
    TypingSendThrottle throttle;
    REQUIRE(throttle.OnInputChanged(kBase, false) == typing_action_send_typing);
    REQUIRE(throttle.OnInputChanged(kBase + 1000, false) == typing_action_none);

    // 距上次发包正好满间隔：续发，让对端那边的提示不至于过期消失
    CHECK(throttle.OnInputChanged(kBase + kTypingRepeatMs, false) == typing_action_send_typing);
    // 续发后基准前移，下一轮重新计时
    CHECK(throttle.OnInputChanged(kBase + kTypingRepeatMs + 1, false) == typing_action_none);
    CHECK(throttle.OnInputChanged(kBase + kTypingRepeatMs * 2, false) == typing_action_send_typing);
}

TEST_CASE("T4 停手满闲置时长发一次停止且不重复", "[typing]")
{
    TypingSendThrottle throttle;
    REQUIRE(throttle.OnInputChanged(kBase, false) == typing_action_send_typing);

    // 还没到闲置时长，tick 不产生动作
    CHECK(throttle.OnTick(kBase + kTypingIdleMs - 1) == typing_action_none);
    CHECK(throttle.IsActive());

    // 满闲置时长：收尾
    CHECK(throttle.OnTick(kBase + kTypingIdleMs) == typing_action_send_stop);
    CHECK_FALSE(throttle.IsActive());

    // 已经收尾，后续 tick 不该再发第二个 0 包
    CHECK(throttle.OnTick(kBase + kTypingIdleMs + 1) == typing_action_none);
    CHECK(throttle.OnTick(kBase + kTypingIdleMs * 10) == typing_action_none);
}

TEST_CASE("T5 闲置计时从最后一次输入算起", "[typing]")
{
    TypingSendThrottle throttle;
    REQUIRE(throttle.OnInputChanged(kBase, false) == typing_action_send_typing);
    // 中途又敲了一下（这次在续发间隔内、不发包，但要刷新"最后输入时刻"）
    REQUIRE(throttle.OnInputChanged(kBase + 2000, false) == typing_action_none);

    // 距首次输入已满闲置时长，但距最后一次输入还差 2 秒 —— 不该收尾
    CHECK(throttle.OnTick(kBase + kTypingIdleMs) == typing_action_none);
    CHECK(throttle.IsActive());

    // 距最后一次输入满了才收尾
    CHECK(throttle.OnTick(kBase + 2000 + kTypingIdleMs) == typing_action_send_stop);
}

TEST_CASE("T6 内容被删光立即发停止", "[typing]")
{
    TypingSendThrottle throttle;
    REQUIRE(throttle.OnInputChanged(kBase, false) == typing_action_send_typing);

    // 用户把输入框内容全删了：立即收尾，不等闲置定时器
    CHECK(throttle.OnInputChanged(kBase + 500, true) == typing_action_send_stop);
    CHECK_FALSE(throttle.IsActive());

    // 空输入框上继续敲退格之类不产生包
    CHECK(throttle.OnInputChanged(kBase + 600, true) == typing_action_none);
}

TEST_CASE("T7 消息发出后收尾并复位，下次打字重新立刻通告", "[typing]")
{
    TypingSendThrottle throttle;
    REQUIRE(throttle.OnInputChanged(kBase, false) == typing_action_send_typing);

    // 用户按了发送：消息本身就说明"输入结束了"，立即收尾
    CHECK(throttle.OnStop() == typing_action_send_stop);
    CHECK_FALSE(throttle.IsActive());
    // 重复收尾不再发包
    CHECK(throttle.OnStop() == typing_action_none);

    // 紧接着敲下一句：即便距上次发包还不到续发间隔，也应立刻通告
    CHECK(throttle.OnInputChanged(kBase + 100, false) == typing_action_send_typing);
}

TEST_CASE("T8 Reset 静默复位不产生动作", "[typing]")
{
    TypingSendThrottle throttle;
    REQUIRE(throttle.OnInputChanged(kBase, false) == typing_action_send_typing);

    // 连接已断之类的场合：发 0 包也送不到，静默复位即可
    throttle.Reset();
    CHECK_FALSE(throttle.IsActive());
    CHECK(throttle.OnStop() == typing_action_none);

    // 复位后重新打字仍走"立刻通告"
    CHECK(throttle.OnInputChanged(kBase + 100, false) == typing_action_send_typing);
}

//==================== 接收侧 TypingPeerState ====================

TEST_CASE("T9 收到输入中即显示，重复收到不再报变化", "[typing]")
{
    TypingPeerState state;
    CHECK(state.OnNotify(kBase, 1));            // 首次：显示状态变了，要重绘
    CHECK(state.IsTyping());

    CHECK_FALSE(state.OnNotify(kBase + 100, 1)); // 已在显示：不必重绘
    CHECK(state.IsTyping());
}

TEST_CASE("T10 收到停止立即收起", "[typing]")
{
    TypingPeerState state;
    REQUIRE(state.OnNotify(kBase, 1));

    CHECK(state.OnNotify(kBase + 100, 0));
    CHECK_FALSE(state.IsTyping());

    // 没在显示时又收到停止：状态没变，不用重绘
    CHECK_FALSE(state.OnNotify(kBase + 200, 0));
}

TEST_CASE("T11 对端不再发包时到期自动收起", "[typing]")
{
    TypingPeerState state;
    REQUIRE(state.OnNotify(kBase, 1));

    // 未到期：继续显示
    CHECK_FALSE(state.OnTick(kBase + kTypingExpireMs - 1));
    CHECK(state.IsTyping());

    // 到期：对端多半已掉线，自动收起（这是"不可靠提示"的兜底）
    CHECK(state.OnTick(kBase + kTypingExpireMs));
    CHECK_FALSE(state.IsTyping());

    // 收起之后不再重复报变化
    CHECK_FALSE(state.OnTick(kBase + kTypingExpireMs + 1));
}

TEST_CASE("T12 续发包把过期时刻往后推", "[typing]")
{
    TypingPeerState state;
    REQUIRE(state.OnNotify(kBase, 1));

    // 对端还在打字、续发了一包
    REQUIRE_FALSE(state.OnNotify(kBase + kTypingRepeatMs, 1));

    // 按首包算本该到期的时刻，此刻不应收起
    CHECK_FALSE(state.OnTick(kBase + kTypingExpireMs));
    CHECK(state.IsTyping());

    // 按续发包重新计的到期时刻才收起
    CHECK(state.OnTick(kBase + kTypingRepeatMs + kTypingExpireMs));
    CHECK_FALSE(state.IsTyping());
}

TEST_CASE("T13 收到正式消息时清掉提示", "[typing]")
{
    TypingPeerState state;
    REQUIRE(state.OnNotify(kBase, 1));

    // 对方的消息都到了，"正在输入"自然该消失
    CHECK(state.Clear());
    CHECK_FALSE(state.IsTyping());
    // 没在显示时清理不报变化
    CHECK_FALSE(state.Clear());
}
