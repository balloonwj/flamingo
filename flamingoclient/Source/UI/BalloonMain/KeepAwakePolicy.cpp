/**
 *  KeepAwakePolicy.cpp
 *
 *  「禁止机器休眠」决策的实现。全部为纯函数：不读时钟、不调用 Win32 API —— 时钟由
 *  调用方读取后作为参数传入，获取、释放、合成输入事件这些动作也由调用方执行
 *  （见 KeepAwakeSession）。
 *
 *  本 .cpp 不 include Flamingo 的 stdafx.h —— 它要被 Flamingo.vcxproj 与
 *  FlamingoUnit.vcxproj 共用。在 Flamingo.vcxproj 里需标记 PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-08-12
 */

#include "KeepAwakePolicy.h"
#include "AutoAwayPolicy.h"     // ComputeIdleMs（回绕安全的 tick 差值，两处共用一份）

namespace balloonmain {

int DecideKeepAwake(const KeepAwakeInput& input)
{
    // 是否应当持有电源请求：开关为开，且已经登录。停留在登录界面的客户端没有理由
    // 阻止整台机器休眠，因此未登录时即使开关为开也不持有。
    const bool shouldHold = (input.m_enabled && input.m_loggedIn);

    if (!shouldHold)
    {
        // 应当释放。未持有时无需任何动作 —— 本功能默认关闭，多数情况下都走这条分支。
        return input.m_holding ? kKeepAwakeRelease : kKeepAwakeNone;
    }

    if (!input.m_holding)
    {
        // 尚未持有：先获取。调用方获取成功后需立即合成一次输入事件并记录 tick，
        // 否则首次合成要等满一个间隔才会发生，这段间隔足以让超时较短的锁屏策略生效。
        return kKeepAwakeAcquire;
    }

    // 已持有，只需判断是否应当再合成一次。从未合成过（tick 为 0）时直接合成：
    // 否则开机初期 tick 数值很小的一段时间内会产生一次不必要的等待。
    if (input.m_lastNudgeTick == 0)
    {
        return kKeepAwakeNudge;
    }

    // 距上次合成经过的时长。此处复用 AutoAwayPolicy 的 ComputeIdleMs：它计算的正是
    // "从某个 tick 到当前经过了多少毫秒"，且已处理好 tick 回绕（开机约 49.7 天后归零）
    // 与"传入的 tick 晚于当前 tick"两种情形，并有专门的用例覆盖。其名称虽含 Idle，
    // 语义与此处完全一致；另写一份反而会多出一处容易出错的无符号运算。
    const DWORD elapsedMs = ComputeIdleMs(input.m_nowTick, input.m_lastNudgeTick);

    // 判定用 >= ：经过时长满一个间隔即应当合成，不必再等一个定时器周期。
    if (elapsedMs >= kKeepAwakeNudgeIntervalMs)
    {
        return kKeepAwakeNudge;
    }

    return kKeepAwakeNone;
}

} // namespace balloonmain
