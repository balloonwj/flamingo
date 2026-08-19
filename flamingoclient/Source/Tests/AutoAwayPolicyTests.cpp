/**
 *  AutoAwayPolicyTests.cpp
 *
 *  "电脑无操作自动切到离开"决策逻辑的测试。这个功能的难点全在状态机的边角上 ——
 *  什么时候该切、什么时候<u>绝不能</u>切，故用例主要围绕这几条约定：
 *    · 只有"在线"会被自动切到离开，忙碌不动；
 *    · 只有<u>自动</u>切过去的离开才会在有操作时自动切回在线，手动选的离开不动；
 *    · 未连接时一律不动作；
 *    · 关掉总开关要把自动摆布过的状态还回去。
 *  外加 tick 回绕这一条 —— 它平时看不出来，但在开机满 49.7 天那一刻会把全体用户
 *  误判成长时间空闲、集体切到离开。
 *
 *  用 Catch2 v3（amalgamated）作框架，纯内存断言，不创建窗口 / 不读时钟 / 不发协议。
 *  全局 _Module 由 BalloonChatLayoutTests.cpp 统一定义，本文件只引用。
 *
 *  balloonwj@qq.com   2026-08-12
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include "../UI/BalloonMain/AutoAwayPolicy.h"
#include "../UI/BalloonSettings/SettingsBinding.h"   // 时长下拉索引 → 分钟数
#include "../net/Protocol.h"                      // presence_type

#include <wtsapi32.h>                             // WTS_SESSION_LOCK 等会话事件状态码

using namespace balloonmain;

namespace {

// 一分钟的毫秒数 —— 用例里的阈值都按分钟写，读起来和设置页里的下拉项对得上。
const DWORD kOneMinuteMs = 60 * 1000;

// 造一份"功能开着、已连接、阈值 5 分钟"的默认输入，各用例只改自己关心的字段。
// 这样每条用例的差异一眼可见，不必在一堆相同的赋值里找那一行。
AutoAwayInput MakeInput(int presence, bool isAutoAway, DWORD idleMs)
{
    AutoAwayInput in;
    in.m_presence    = presence;
    in.m_isAutoAway  = isAutoAway;
    in.m_idleMs      = idleMs;
    in.m_thresholdMs = 5 * kOneMinuteMs;
    in.m_connected   = true;
    in.m_enabled     = true;
    return in;
}

} // anonymous namespace

// =====================================================================
// AA1-AA3. 在线 → 离开
// =====================================================================

// 已作废的在线状态取值：4 曾经是「隐身」，本产品不提供该功能（见 net/Protocol.h）。
// 老服务端仍可能推上来，故保留这些用例，确认它被当作越界值归到"离线"。
static const int kRetiredPresence = 4;
TEST_CASE("AA1 在线且空闲满阈值：自动切到离开", "[autoaway]")
{
    AutoAwayInput in = MakeInput(presence_online, false, 5 * kOneMinuteMs);
    CHECK(DecideAutoAway(in) == kAutoAwayGoAway);
}

TEST_CASE("AA2 在线但空闲不够：什么都不做", "[autoaway]")
{
    AutoAwayInput in = MakeInput(presence_online, false, 5 * kOneMinuteMs - 1);
    CHECK(DecideAutoAway(in) == kAutoAwayNone);

    in.m_idleMs = 0;
    CHECK(DecideAutoAway(in) == kAutoAwayNone);
}

TEST_CASE("AA3 阈值边界：正好满阈值就算到点", "[autoaway]")
{
    // 设置页写的是"5 分钟"，满 5 分钟就该切，不必多等一跳（判定用 >= 而非 >）。
    AutoAwayInput in = MakeInput(presence_online, false, 5 * kOneMinuteMs);
    CHECK(DecideAutoAway(in) == kAutoAwayGoAway);

    in.m_idleMs = 5 * kOneMinuteMs - 1;
    CHECK(DecideAutoAway(in) == kAutoAwayNone);
}

// =====================================================================
// AA4-AA5. 只有"在线"会被自动切走
// =====================================================================

TEST_CASE("AA4 忙碌下空闲再久也不动", "[autoaway]")
{
    // 忙碌是用户刻意发出的"别打扰我"，降级成离开会把这层意思丢掉。
    AutoAwayInput in = MakeInput(presence_busy, false, 60 * kOneMinuteMs);
    CHECK(DecideAutoAway(in) == kAutoAwayNone);
}

TEST_CASE("AA5 离线与已作废取值下空闲再久也不动", "[autoaway]")
{
    AutoAwayInput off = MakeInput(presence_offline, false, 60 * kOneMinuteMs);
    CHECK(DecideAutoAway(off) == kAutoAwayNone);

    AutoAwayInput retired = MakeInput(kRetiredPresence, false, 60 * kOneMinuteMs);
    CHECK(DecideAutoAway(retired) == kAutoAwayNone);
}

// =====================================================================
// AA6-AA8. 离开 → 在线（只对自动切过去的生效）
// =====================================================================

TEST_CASE("AA6 自动离开期间检测到操作：自动切回在线", "[autoaway]")
{
    AutoAwayInput in = MakeInput(presence_away, true, 0);
    CHECK(DecideAutoAway(in) == kAutoAwayGoOnline);
}

TEST_CASE("AA7 自动离开但人还没回来：什么都不做", "[autoaway]")
{
    AutoAwayInput in = MakeInput(presence_away, true, 30 * kOneMinuteMs);
    CHECK(DecideAutoAway(in) == kAutoAwayNone);
}

TEST_CASE("AA8 用户手动选的离开：有操作也不切回在线", "[autoaway]")
{
    // 本功能最容易做错的一条：手动选的离开必须原样留着，否则用户一碰鼠标就被
    // 拽回在线，等于剥夺了"我就想挂个离开"这个选择。
    AutoAwayInput in = MakeInput(presence_away, false, 0);
    CHECK(DecideAutoAway(in) == kAutoAwayNone);

    // 手动离开下继续空闲，同样不动（不会被再切一次离开）。
    in.m_idleMs = 60 * kOneMinuteMs;
    CHECK(DecideAutoAway(in) == kAutoAwayNone);
}

TEST_CASE("AA9 自动离开期间改成了忙碌：有操作也不切回在线", "[autoaway]")
{
    // 场景：自动离开之后，用户在手机上 / 回来顺手把状态改成了忙碌。此时调用方
    // 会把"自动"标志清掉（见会话控制器 OnChangeMyPresence 的来源标志），于是这里
    // 看到的是"忙碌 + 非自动"。即便标志因某种原因没清干净，presence 已不是离开，
    // 本函数也不会把人切回在线 —— 两道防线都验一下。
    AutoAwayInput cleared = MakeInput(presence_busy, false, 0);
    CHECK(DecideAutoAway(cleared) == kAutoAwayNone);

    AutoAwayInput staleFlag = MakeInput(presence_busy, true, 0);
    CHECK(DecideAutoAway(staleFlag) == kAutoAwayNone);
}

// =====================================================================
// AA10-AA11. 连接态与总开关
// =====================================================================

TEST_CASE("AA10 未连接时一律不动作", "[autoaway]")
{
    // 此刻 cmd_change_presence 发不出去，切了也只是本地自欺。
    AutoAwayInput idle = MakeInput(presence_online, false, 60 * kOneMinuteMs);
    idle.m_connected = false;
    CHECK(DecideAutoAway(idle) == kAutoAwayNone);

    // 连"切回在线"也不做，等连上之后用户一动键鼠自然会被切回来。
    AutoAwayInput back = MakeInput(presence_away, true, 0);
    back.m_connected = false;
    CHECK(DecideAutoAway(back) == kAutoAwayNone);
}

TEST_CASE("AA11 关掉总开关：不再自动切离开，并把自动设上的离开还回去", "[autoaway]")
{
    AutoAwayInput idle = MakeInput(presence_online, false, 60 * kOneMinuteMs);
    idle.m_enabled = false;
    CHECK(DecideAutoAway(idle) == kAutoAwayNone);

    // 用户关掉这个功能的本意是"别再自动改我的状态"，把他晾在一个自动设上的离开
    // 上最不合预期，故这里要主动切回在线。
    AutoAwayInput autoAway = MakeInput(presence_away, true, 60 * kOneMinuteMs);
    autoAway.m_enabled = false;
    CHECK(DecideAutoAway(autoAway) == kAutoAwayGoOnline);

    // 手动选的离开不受影响 —— 关开关不该动用户自己挑的状态。
    AutoAwayInput manualAway = MakeInput(presence_away, false, 60 * kOneMinuteMs);
    manualAway.m_enabled = false;
    CHECK(DecideAutoAway(manualAway) == kAutoAwayNone);
}

TEST_CASE("AA12 阈值为 0 视为未配置，按功能关闭处理", "[autoaway]")
{
    AutoAwayInput idle = MakeInput(presence_online, false, 60 * kOneMinuteMs);
    idle.m_thresholdMs = 0;
    CHECK(DecideAutoAway(idle) == kAutoAwayNone);

    // 同样要把自动设上的离开还回去，否则阈值一旦为 0，人就永远卡在离开上了。
    AutoAwayInput autoAway = MakeInput(presence_away, true, 60 * kOneMinuteMs);
    autoAway.m_thresholdMs = 0;
    CHECK(DecideAutoAway(autoAway) == kAutoAwayGoOnline);
}

// =====================================================================
// AA13-AA14. 空闲时长的计算（tick 回绕）
// =====================================================================

TEST_CASE("AA13 空闲时长：常规相减", "[autoaway][tick]")
{
    CHECK(ComputeIdleMs(10000, 4000) == 6000);
    CHECK(ComputeIdleMs(4000, 4000)  == 0);
}

TEST_CASE("AA14 空闲时长：tick 回绕后仍算得对", "[autoaway][tick]")
{
    // 开机约 49.7 天后 GetTickCount 回绕归零。最后一次输入发生在回绕前、当前 tick
    // 已经绕过去了：无符号相减天然给出正确差值。这里若写成有符号或 int64，会算出
    // 一个接近 49.7 天的巨大值，把所有人误判成长时间空闲、集体切到离开。
    const DWORD kMaxTick = 0xFFFFFFFF;

    // 回绕前 1 秒有输入，回绕后又过了 2 秒 → 实际空闲 3 秒。
    CHECK(ComputeIdleMs(2000, kMaxTick - 999) == 3000);

    // 正好在回绕点上。
    CHECK(ComputeIdleMs(0, kMaxTick) == 1);
}

TEST_CASE("AA15 最后一次输入比当前还新时按刚有操作处理", "[autoaway][tick]")
{
    // 两次取值之间恰好有人动了键鼠时会出现这种倒挂。无符号相减会得到一个接近 2^32
    // 的巨大值，若原样返回就成了"空闲 49 天"，会立刻把人切到离开。
    CHECK(ComputeIdleMs(1000, 1001) == 0);
    CHECK(ComputeIdleMs(1000, 5000) == 0);
}

// =====================================================================
// AA18-AA22. 锁屏 / 解锁等会话事件
// =====================================================================

TEST_CASE("AA18 会话事件分类：锁屏与断开算离开，解锁与恢复算回来", "[autoaway][session]")
{
    CHECK(ClassifySessionEvent(WTS_SESSION_LOCK)       == kSessionEventLeave);
    CHECK(ClassifySessionEvent(WTS_CONSOLE_DISCONNECT) == kSessionEventLeave);
    CHECK(ClassifySessionEvent(WTS_REMOTE_DISCONNECT)  == kSessionEventLeave);

    CHECK(ClassifySessionEvent(WTS_SESSION_UNLOCK)  == kSessionEventReturn);
    CHECK(ClassifySessionEvent(WTS_CONSOLE_CONNECT) == kSessionEventReturn);
    CHECK(ClassifySessionEvent(WTS_REMOTE_CONNECT)  == kSessionEventReturn);

    // 与"人在不在电脑前"无关的事件不能误判 —— 比如注销事件若被当成"离开"，
    // 会在退出流程里多发一次改状态请求。
    CHECK(ClassifySessionEvent(WTS_SESSION_LOGON)  == kSessionEventNone);
    CHECK(ClassifySessionEvent(WTS_SESSION_LOGOFF) == kSessionEventNone);
    CHECK(ClassifySessionEvent(999)                == kSessionEventNone);
}

TEST_CASE("AA19 锁屏时立刻切离开，不必等空闲满阈值", "[autoaway][session]")
{
    // 关键点：空闲时长为 0（刚敲完 Win+L），照样要切走 —— 人已经不在了。
    AutoAwayInput in = MakeInput(presence_online, false, 0);
    CHECK(DecideAutoAwayOnSessionEvent(in, kSessionEventLeave) == kAutoAwayGoAway);
}

TEST_CASE("AA20 解锁时立刻切回在线（仅限自动切过去的离开）", "[autoaway][session]")
{
    AutoAwayInput autoAway = MakeInput(presence_away, true, 30 * kOneMinuteMs);
    CHECK(DecideAutoAwayOnSessionEvent(autoAway, kSessionEventReturn) == kAutoAwayGoOnline);

    // 用户自己选的离开，解锁回来也不动 —— 与空闲轮询那条路径同一条约定。
    AutoAwayInput manualAway = MakeInput(presence_away, false, 30 * kOneMinuteMs);
    CHECK(DecideAutoAwayOnSessionEvent(manualAway, kSessionEventReturn) == kAutoAwayNone);
}

TEST_CASE("AA21 锁屏不动忙碌，也不碰未连接 / 功能关闭的情形", "[autoaway][session]")
{
    // 多出一条触发路径不能把既有约定放宽：忙碌仍然不动。
    AutoAwayInput busy = MakeInput(presence_busy, false, 0);
    CHECK(DecideAutoAwayOnSessionEvent(busy, kSessionEventLeave) == kAutoAwayNone);

    // 未连接时锁屏也不发协议。
    AutoAwayInput offline = MakeInput(presence_online, false, 0);
    offline.m_connected = false;
    CHECK(DecideAutoAwayOnSessionEvent(offline, kSessionEventLeave) == kAutoAwayNone);

    // 功能关掉后锁屏不该再自动切走。
    AutoAwayInput disabled = MakeInput(presence_online, false, 0);
    disabled.m_enabled = false;
    CHECK(DecideAutoAwayOnSessionEvent(disabled, kSessionEventLeave) == kAutoAwayNone);
}

TEST_CASE("AA22 不关心的会话事件一律不动作", "[autoaway][session]")
{
    AutoAwayInput in = MakeInput(presence_online, false, 60 * kOneMinuteMs);
    CHECK(DecideAutoAwayOnSessionEvent(in, kSessionEventNone) == kAutoAwayNone);

    // 即便当前正处于自动离开、且已经有操作，不相关的事件也不该把人切回去 ——
    // 那是空闲轮询的职责，不是会话事件的。
    AutoAwayInput autoAway = MakeInput(presence_away, true, 0);
    CHECK(DecideAutoAwayOnSessionEvent(autoAway, kSessionEventNone) == kAutoAwayNone);
}

// =====================================================================
// AA16. 等待时长下拉（索引 ↔ 分钟数）
// =====================================================================

TEST_CASE("AA16 等待时长下拉：四档 5/10/15/30 分钟", "[autoaway][settings]")
{
    CHECK(balloonsettings::AutoAwayDelayOptionCount() == 4);

    CHECK(balloonsettings::AutoAwayDelayMinutesFromIndex(balloonsettings::kAutoAwayDelay5Min)  == 5);
    CHECK(balloonsettings::AutoAwayDelayMinutesFromIndex(balloonsettings::kAutoAwayDelay10Min) == 10);
    CHECK(balloonsettings::AutoAwayDelayMinutesFromIndex(balloonsettings::kAutoAwayDelay15Min) == 15);
    CHECK(balloonsettings::AutoAwayDelayMinutesFromIndex(balloonsettings::kAutoAwayDelay30Min) == 30);
}

TEST_CASE("AA17 等待时长下拉：越界索引回落到默认 5 分钟", "[autoaway][settings]")
{
    // 配置文件被手工改坏时会读出越界值，不能让它把分钟数表读越界。
    CHECK(balloonsettings::ClampAutoAwayDelayIndex(-1) == balloonsettings::kAutoAwayDelay5Min);
    CHECK(balloonsettings::ClampAutoAwayDelayIndex(4)  == balloonsettings::kAutoAwayDelay5Min);
    CHECK(balloonsettings::ClampAutoAwayDelayIndex(99) == balloonsettings::kAutoAwayDelay5Min);

    CHECK(balloonsettings::AutoAwayDelayMinutesFromIndex(-1) == 5);
    CHECK(balloonsettings::AutoAwayDelayMinutesFromIndex(99) == 5);
}
