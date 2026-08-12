/**
 *  KeepAwakePolicy.h
 *
 *  「禁止机器休眠」的<u>纯逻辑</u>决策。把"本次应当获取还是释放系统电源请求、是否
 *  应当再合成一次输入事件"从定时器与 Win32 API 调用中剥离出来，做成无副作用的纯函数，
 *  便于被 FlamingoUnit 直接断言（做法与同目录的 AutoAwayPolicy 一致）。
 *
 *  为什么"不休眠"与"不锁屏"需要两套机制（2026-08-12 在本机实测得出，探针脚本见
 *  tools/keepawake_probe.ps1）：
 *    · 系统电源请求（PowerCreateRequest / PowerSetRequest）能够阻止休眠与关闭显示器，
 *      但它<u>不影响输入空闲计时器</u> —— 实测持有电源请求期间，GetLastInputInfo 报出的
 *      空闲时长仍从 0 递增到 15 秒。
 *    · 屏幕保护程序与自动锁屏依据的正是该输入空闲计时器。实测：屏保超时设为 60 秒时，
 *      静置 90 秒屏保如期启动；改为每 20 秒合成一次鼠标移动事件后，100 秒内空闲时长
 *      始终不超过 15 秒，屏保一次都没有启动。
 *    因此本功能由两部分构成：电源请求（阻止休眠与关屏）+ 周期性合成输入事件
 *    （阻止屏保与锁屏）。
 *
 *  与「自动离开」互斥（2026-08-12 与用户确认）：合成输入事件会将全系统的空闲计时器
 *  重置为 0，而自动离开正是依据该计时器判定空闲，两者无法同时成立。因此本功能开启
 *  期间自动离开一律不生效（判定见 CMainDlg::BuildAutoAwayInput），设置页也会把
 *  「自动离开」相关的两行置为禁用。
 *
 *  典型用法（定时器每次触发调用一次）：
 *      balloonmain::KeepAwakeInput in;
 *      in.m_enabled        = 设置中的「禁止机器休眠」开关;
 *      in.m_loggedIn       = 是否已登录;
 *      in.m_holding        = 当前是否已持有电源请求;
 *      in.m_nowTick        = ::GetTickCount();
 *      in.m_lastNudgeTick  = 上次合成输入事件时的 tick;
 *      switch (balloonmain::DecideKeepAwake(in)) { ... }
 *
 *  balloonwj@qq.com   2026-08-12
 */

#pragma once

#include <windows.h>      // DWORD

namespace balloonmain {

/**
 *  两次合成输入事件之间的最小间隔（毫秒）。
 *
 *  驱动本策略的是 CMainDlg 中每 30 秒触发一次的定时器（AUTOAWAY_POLL_INTERVAL_MS），
 *  本值取 25 秒，意味着实际上每次触发都会合成一次输入事件。之所以保留该间隔而不是
 *  写成"每次触发必定合成"，是为了让合成频率与定时器周期解耦：将来定时器若因其它需求
 *  改为更短周期，合成频率不会随之升高。
 *
 *  取值上界受屏保与锁屏超时约束：Windows 界面上可设置的最短超时为 1 分钟，本值必须
 *  明显小于它，否则两次合成之间会留出足以触发锁屏的间隔。
 */
const DWORD kKeepAwakeNudgeIntervalMs = 25 * 1000;

/**
 *  一次判定的结论。
 */
enum KeepAwakeAction
{
    kKeepAwakeNone    = 0,   // 不执行任何动作（功能已关闭，或距上次合成尚未满一个间隔）
    kKeepAwakeAcquire = 1,   // 应当获取系统电源请求（调用方获取后需立即合成一次并记录 tick）
    kKeepAwakeRelease = 2,   // 应当释放系统电源请求（开关已关闭，或已退出登录）
    kKeepAwakeNudge   = 3,   // 已持有电源请求，且距上次合成已满一个间隔，应当再合成一次
};

/**
 *  一次判定所需的全部输入。定义为结构体而非一串参数，理由同 AutoAwayInput ——
 *  三个 bool 相邻按位置传参极易写反。
 */
struct KeepAwakeInput
{
    bool  m_enabled;         // 设置中的「禁止机器休眠」开关；关闭后应释放电源请求
    bool  m_loggedIn;        // 是否已登录。未登录时即使开关为开也不占用系统电源请求 ——
                             // 停留在登录界面的客户端没有理由阻止整台机器休眠
    bool  m_holding;         // 当前是否已持有电源请求（由调用方自行记录）
    DWORD m_nowTick;         // 当前 tick，取自 ::GetTickCount()
    DWORD m_lastNudgeTick;   // 上次合成输入事件时的 tick；从未合成过时传 0
};

/**
 *  判定本次应当执行什么动作。纯函数，无副作用，不读时钟、不访问窗口、不调用任何
 *  Win32 API。
 *
 *    input：见 KeepAwakeInput 各字段说明。
 *
 *  返回：KeepAwakeAction 之一。几条约定：
 *    · "是否应当持有"只由 m_enabled && m_loggedIn 决定；两者任一为 false 而当前持有着，
 *      返回 kKeepAwakeRelease。
 *    · 应当持有但尚未持有，返回 kKeepAwakeAcquire。调用方获取成功后应<u>立即合成一次
 *      输入事件</u>并把 m_lastNudgeTick 更新为当时的 tick，否则首次合成要等满一个间隔
 *      才会发生。
 *    · 已持有且距上次合成 >= kKeepAwakeNudgeIntervalMs，返回 kKeepAwakeNudge；不足则
 *      返回 kKeepAwakeNone。
 *    · m_lastNudgeTick 为 0（从未合成过）时一律判定为应当合成，避免开机初期 tick 数值
 *      很小时产生一次不必要的等待。
 */
int DecideKeepAwake(const KeepAwakeInput& input);

} // namespace balloonmain
