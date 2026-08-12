/**
 *  AutoAwayPolicy.cpp
 *
 *  "无操作自动离开"决策的实现。全是纯函数：不读时钟、不碰窗口、不发协议 ——
 *  时钟由调用方读好当参数传进来，动作也由调用方去执行。
 *
 *  本 .cpp 不 include Flamingo 的 stdafx.h —— 它要被 Flamingo.vcxproj 与
 *  FlamingoUnit.vcxproj 共用。在 Flamingo.vcxproj 里需标记 PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-08-12
 */

#include "AutoAwayPolicy.h"
#include "../net/Protocol.h"        // presence_type（在线状态取值的单一事实来源）

#include <wtsapi32.h>               // WTS_SESSION_LOCK 等会话事件状态码（只取常量，不调 API）

namespace balloonmain {

namespace {

// tick 相减结果的"合理上界"。两个 32 位 tick 相减在无符号运算下是模 2^32 的正确差值，
// 但当最后一次输入的 tick 比"当前"还新时（两次取值之间恰好有人动了键鼠，或系统对两个
// 时钟源的更新有微小先后），差值会翻成一个接近 2^32 的巨大数。取 2^31（约 24.8 天）
// 作界：超过它的一律当"刚刚才有输入"处理。代价是"机器连续闲置 25 天以上"会被算成
// 空闲 0，这种情形下本功能不触发而已，无害。
const DWORD kMaxSaneIdleMs = 0x80000000;

} // anonymous namespace

int DecideAutoAway(const AutoAwayInput& input)
{
    // 连接没就绪就什么都别做。此刻 cmd_change_presence 发不出去，改了也只是本地自欺；
    // m_isAutoAway 原样留着，等连上之后用户一动键鼠自然会被切回在线。
    if (!input.m_connected)
    {
        return kAutoAwayNone;
    }

    // "当前正处于自动切过去的离开"—— 下面反复要用。之所以还要比对 presence，是防御
    // 标志与实际状态不同步（比如状态被别处改过而标志没跟上）：以实际状态为准。
    const bool inAutoAway = input.m_isAutoAway && (input.m_presence == presence_away);

    // 功能未启用：开关被关掉，或阈值没配出来（0 视为未配置）。此时要把本功能自动
    // 摆布过的状态还回去 —— 用户关掉它的本意是"别再自动改我的状态"，把人晾在一个
    // 自动设上的"离开"上最不合预期。
    if (!input.m_enabled || input.m_thresholdMs == 0)
    {
        if (inAutoAway)
        {
            return kAutoAwayGoOnline;
        }
        return kAutoAwayNone;
    }

    // 正处于自动离开：一旦空闲时长回落到阈值以下，说明人回来了，切回在线。
    if (inAutoAway)
    {
        if (input.m_idleMs < input.m_thresholdMs)
        {
            return kAutoAwayGoOnline;
        }
        return kAutoAwayNone;
    }

    // 剩下的只处理"在线 → 离开"这一种切换。
    // 忙碌不动：那是用户刻意发出的"别打扰我"，降级成离开会把这层意思丢掉。
    // 离线 / 隐身 / 用户手动选的离开，同样不动。
    if (input.m_presence != presence_online)
    {
        return kAutoAwayNone;
    }

    // 用 >= 而不是 > ：设置里写的是"5 分钟"，满 5 分钟就该切，不必多等一跳。
    if (input.m_idleMs >= input.m_thresholdMs)
    {
        return kAutoAwayGoAway;
    }

    return kAutoAwayNone;
}

int ClassifySessionEvent(int wtsStatusCode)
{
    switch (wtsStatusCode)
    {
    case WTS_SESSION_LOCK:
        // 用户按了 Win+L，或系统按策略自动锁屏。
        return kSessionEventLeave;
    case WTS_CONSOLE_DISCONNECT:
        // 本机会话被切走（切换用户 / 被远程接管），人不在这台机器前了。
        return kSessionEventLeave;
    case WTS_REMOTE_DISCONNECT:
        // 远程桌面断开，同样是人走了。
        return kSessionEventLeave;

    case WTS_SESSION_UNLOCK:
        // 解锁回来。
        return kSessionEventReturn;
    case WTS_CONSOLE_CONNECT:
        // 切回本机会话。
        return kSessionEventReturn;
    case WTS_REMOTE_CONNECT:
        // 远程桌面重新连上。
        return kSessionEventReturn;

    default:
        // WTS_SESSION_LOGON / LOGOFF / REMOTE_CONTROL 等：与"人在不在电脑前"无关，忽略。
        return kSessionEventNone;
    }
}

int DecideAutoAwayOnSessionEvent(const AutoAwayInput& input, int eventKind)
{
    // 把会话事件折算成空闲时长，然后复用同一个状态机 —— 这样"只有在线会被切走"、
    // "只有自动离开才会被切回"、"未连接不动作"这几条约定只有一份实现，不会因为多出
    // 一条触发路径而两边走样。
    AutoAwayInput folded = input;

    if (eventKind == kSessionEventLeave)
    {
        // 人已经离开，视同空闲已满阈值 —— 锁屏那一刻就该切走，不必再等 5 分钟。
        folded.m_idleMs = folded.m_thresholdMs;
        return DecideAutoAway(folded);
    }

    if (eventKind == kSessionEventReturn)
    {
        // 人回来了（解锁时刚敲过密码），视同刚刚有操作。
        folded.m_idleMs = 0;
        return DecideAutoAway(folded);
    }

    return kAutoAwayNone;
}

DWORD ComputeIdleMs(DWORD nowTick, DWORD lastInputTick)
{
    // 固定用 DWORD 相减：无符号运算天然处理 49.7 天的 tick 回绕（差值只要小于 49.7 天
    // 就是对的）。这里<u>不能</u>先转成有符号或 int64 —— 那样回绕当天会算出一个荒谬的
    // 巨大值，把所有人误判成长时间空闲、集体切到离开。
    const DWORD diff = nowTick - lastInputTick;

    if (diff >= kMaxSaneIdleMs)
    {
        // 最后一次输入比"当前"还新，说明刚刚才有操作。
        return 0;
    }

    return diff;
}

} // namespace balloonmain
