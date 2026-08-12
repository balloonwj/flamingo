/**
 *  AutoAwayPolicy.h
 *
 *  "电脑一段时间无操作自动切到离开"的<u>纯逻辑</u>决策。把"该不该切、该切成什么"
 *  从定时器与 Win32 空闲探测里剥出来，做成无副作用的纯函数，便于被 FlamingoUnit
 *  直接断言（做法与 OnlineCapability / MainPanelStatusMenu / PopupMenuPlacement 一致）。
 *
 *  功能约定（2026-08-12 与用户确认）：
 *    · 只有当前是<u>在线</u>时，空闲满阈值才自动切到"离开"；忙碌不动 —— 忙碌是用户
 *      刻意发出的"别打扰我"信号，降级成离开会把这个信息丢掉。
 *    · 只有<u>自动</u>切过去的离开，才在检测到有操作时自动切回在线。用户自己手动选的
 *      离开，回到电脑前也不动它。
 *    · 用户在自动离开期间手动改过状态（比如切成忙碌），"自动"这个标记就此清除，
 *      之后有操作也不会把他切回在线。这一条由调用方在状态切换的漏斗里维护
 *      （见 CMainDlg::OnChangeMyPresence 的来源标志），本模块只负责读它。
 *
 *  典型用法（定时器每跳一次）：
 *      balloonmain::AutoAwayInput in;
 *      in.m_presence    = 当前在线状态;
 *      in.m_isAutoAway  = m_bAutoAway;
 *      in.m_idleMs      = ComputeIdleMs(::GetTickCount(), lii.dwTime);
 *      in.m_thresholdMs = 设置里的分钟数 * 60 * 1000;
 *      in.m_connected   = 已登录且连接就绪;
 *      in.m_enabled     = 设置里的"自动离开"开关;
 *      switch (balloonmain::DecideAutoAway(in)) { ... }
 *
 *  balloonwj@qq.com   2026-08-12
 */

#pragma once

#include <windows.h>      // DWORD

namespace balloonmain {

/**
 *  一次判定的结论。
 */
enum AutoAwayAction
{
    kAutoAwayNone     = 0,   // 什么都不做（绝大多数跳都落在这里）
    kAutoAwayGoAway   = 1,   // 自动切到"离开"：当前在线且已空闲满阈值
    kAutoAwayGoOnline = 2,   // 自动切回"在线"：此前是自动切过去的离开，现在检测到有操作
};

/**
 *  一次判定所需的全部输入。做成结构体而非一长串参数，是为了让调用点看得出每个值
 *  的含义 —— 五个参数里有三个是 bool，按位置传极易写反。
 */
struct AutoAwayInput
{
    int   m_presence;      // 当前在线状态（v2 presence_type，见 net/Protocol.h）
    bool  m_isAutoAway;    // 当前这个"离开"是否为本功能自动切过去的；用户手动选的离开为 false
    DWORD m_idleMs;        // 距最后一次键鼠操作的时长（毫秒），全系统口径，见 ComputeIdleMs
    DWORD m_thresholdMs;   // 触发阈值（毫秒），来自设置里的时长下拉；0 视为未配置、不触发
    bool  m_connected;     // 连接是否就绪（已登录且不在重连中）；false 时一律不动作
    bool  m_enabled;       // 设置里的"自动离开"总开关；关掉后不再自动切离开，
                           // 且若此刻正处于自动离开，会被切回在线（见 DecideAutoAway 说明）
};

/**
 *  判定这一跳该做什么。纯函数，无副作用，不读时钟、不碰窗口、不发协议。
 *
 *    input：见 AutoAwayInput 各字段说明。
 *
 *  返回：AutoAwayAction 之一。几条容易忽略的约定：
 *    · <u>未连接时一律返回 kAutoAwayNone</u>，连"切回在线"也不做 —— 此刻发不出
 *      cmd_change_presence，切了也只是本地自欺。m_isAutoAway 保持不变，等连上之后
 *      用户一动键鼠自然会被切回来。
 *    · <u>关掉总开关时，若正处于自动离开会返回 kAutoAwayGoOnline</u>。这是刻意的：
 *      用户关掉这个功能的本意是"别再自动摆布我的状态"，把他晾在一个自动设上的
 *      "离开"上最不合预期。
 *    · 阈值判定用 >= —— "满 5 分钟"就算到点，不必多等一跳。
 *    · m_thresholdMs 为 0 视为未配置，不触发切离开（但仍会执行上面的关开关兜底）。
 */
int DecideAutoAway(const AutoAwayInput& input);

/**
 *  会话事件的语义分类。锁屏、切换用户、远程桌面断开这些事件都由 Windows 的
 *  WM_WTSSESSION_CHANGE 一条消息带来，状态码却有七八种，故先归成"人走了 / 人回来了 /
 *  不关心"三类，绘制策略只认这三类。
 */
enum SessionEventKind
{
    kSessionEventNone   = 0,   // 与"人在不在电脑前"无关的事件，忽略
    kSessionEventLeave  = 1,   // 人离开了：锁屏、切换到别的用户、远程桌面断开
    kSessionEventReturn = 2,   // 人回来了：解锁、切回本会话、远程桌面重新连上
};

/**
 *  把 WM_WTSSESSION_CHANGE 的状态码归类。
 *
 *    wtsStatusCode：消息的 wParam，取值见 <wtsapi32.h> 的 WTS_* 常量
 *                   （WTS_SESSION_LOCK / WTS_SESSION_UNLOCK 等）。
 *
 *  返回：SessionEventKind 之一；未登记的状态码一律归到 kSessionEventNone。
 *
 *  之所以把"切换用户 / 远程桌面断开"也算作离开：这几种情形下人同样不在这台机器前，
 *  与锁屏没有区别。反之重新连上就算回来。
 */
int ClassifySessionEvent(int wtsStatusCode);

/**
 *  会话事件发生时的判定 —— 锁屏那一刻人肯定已经不在了，不必再等空闲满阈值。
 *
 *    input：    与 DecideAutoAway 同义；其中 m_idleMs <u>不参与</u>判定（会话事件本身
 *               已经说明了人在不在），其余字段照常生效。
 *    eventKind：会话事件类别，取值见 SessionEventKind，一般由 ClassifySessionEvent 得出。
 *
 *  返回：AutoAwayAction 之一。实现上把事件折算成空闲时长后<u>复用 DecideAutoAway</u>
 *        （离开事件视同已空闲满阈值，回来事件视同刚刚有操作），因此"只有在线会被切走"、
 *        "只有自动离开才会被切回"、"未连接不动作"、"开关关掉要把状态还回去"这几条约定
 *        自动继承，不会因为多出一条触发路径而两边走样。
 */
int DecideAutoAwayOnSessionEvent(const AutoAwayInput& input, int eventKind);

/**
 *  算出距最后一次键鼠操作过了多久（毫秒）。
 *
 *    nowTick：       当前 tick，取自 ::GetTickCount()。
 *    lastInputTick： 最后一次输入的 tick，取自 ::GetLastInputInfo 的 LASTINPUTINFO::dwTime。
 *
 *  返回：两者之差（毫秒）。
 *
 *  单独抽出来是为了把 <u>tick 回绕</u>这件事测到：两个值都是 32 位无符号 tick，
 *  开机约 49.7 天后回绕归零。直接相减在无符号运算下<u>依然正确</u>（差值只要小于
 *  49.7 天就对），但写成有符号或先转 int64 反而会算出一个荒谬的巨大值，进而在回绕
 *  当天把所有人误判成空闲、集体切到离开。故此处固定用 DWORD 相减，并有专门的用例守着。
 *
 *  另注：lastInputTick 晚于 nowTick（两次取值之间恰好有输入）时，无符号相减会得到
 *  一个接近 2^32 的巨大值。故本函数对这种情况返回 0 —— 刚刚才有输入，空闲当然是 0。
 */
DWORD ComputeIdleMs(DWORD nowTick, DWORD lastInputTick);

} // namespace balloonmain
