/**
 *  自动离开控制器的实现。整体说明见 MainPanelAutoAwayController.h。
 *
 *  判定规则本身不在这里，而在 AutoAwayPolicy.h 与 KeepAwakePolicy.h 的纯函数中；本文件
 *  只负责装配输入、把判定结果落成实际动作，需要在线状态时经 ISessionStateHost 询问。
 *
 *  balloonwj@qq.com   2026-08-19
 */

#include "stdafx.h"
#include "MainPanelAutoAwayController.h"

#include "KeepAwakePolicy.h"                        // DecideKeepAwake（禁止机器休眠的判定）
#include "../../App/FlamingoClient.h"               // CFlamingoClient::m_UserConfig
#include "../../Session/UserSessionData.h"          // kMyPresenceAway / kPresenceChangeAuto
#include "../../Common/Log/IULog.h"                 // LOG_INFO / LOG_WARNING
#include "../BalloonSettings/SettingsBinding.h"     // AutoAwayDelayMinutesFromIndex（时长下拉索引→分钟）

//锁屏 / 解锁等会话事件的通知注册。只用到 WTSRegisterSessionNotification 与
//WTSUnRegisterSessionNotification 两个函数，故就地 pragma 链接，不动工程文件。
#include <wtsapi32.h>
#pragma comment(lib, "wtsapi32.lib")

namespace balloonmain {

// 分钟换算成毫秒的系数（60 秒 × 1000 毫秒）。
static const DWORD kMinuteToMs = 60 * 1000;

CAutoAwayController::CAutoAwayController()
    : m_hHost(NULL)
    , m_lpFMGClient(NULL)
    , m_pHost(NULL)
    , m_bAutoAway(false)
    , m_dwLastKeepAwakeNudgeTick(0)
    , m_nTimerId(0)
    , m_bSessionNotifyRegistered(false)
{
    // BEGIN_MSG_MAP_EX 声明的这个标志在首次派发时会被先读后写，非窗口类必须自己置初值。
    m_bMsgHandled = FALSE;
}

CAutoAwayController::~CAutoAwayController()
{
    Shutdown();
}

void CAutoAwayController::Attach(HWND hHost, CFlamingoClient* pClient, ISessionStateHost* pHost)
{
    m_hHost = hHost;
    m_lpFMGClient = pClient;
    m_pHost = pHost;

    if (m_hHost == NULL || !::IsWindow(m_hHost))
    {
        LOG_ERROR("AutoAwayController::Attach: invalid host window, feature disabled.");
        return;
    }

    //轮询定时器，进程在世期间一直跑。之所以不等登录成功才起：每一跳都会先看连接态，
    //未登录 / 掉线时判定函数直接返回「什么都不做」，比在登录、掉线、重连、登出各处
    //分别起停定时器要少好几处漏改的机会。
    m_nTimerId = ::SetTimer(m_hHost, kAutoAwayTimerId, kAutoAwayPollIntervalMs, NULL);

    //订阅锁屏 / 解锁等会话事件（WM_WTSSESSION_CHANGE），让「人已经走了」这件事第一时间
    //反映到在线状态上，而不是等空闲满 5 分钟。NOTIFY_FOR_THIS_SESSION 表示只关心本会话
    //自己的事件，不关心机器上其他用户会话。注册失败不影响主流程 —— 空闲轮询那条路照常
    //兜底，只是锁屏后要多等一会儿才切走，故只记一条日志、不打断启动。
    if (::WTSRegisterSessionNotification(m_hHost, NOTIFY_FOR_THIS_SESSION))
    {
        m_bSessionNotifyRegistered = true;
    }
    else
    {
        LOG_WARNING("WTSRegisterSessionNotification failed (error %u); "
            "lock/unlock will fall back to the idle poll.",
            (unsigned int)::GetLastError());
    }

    LOG_INFO("AutoAwayController::Attach: timerId=%u, interval=%u ms, sessionNotify=%d.",
        (unsigned int)m_nTimerId, (unsigned int)kAutoAwayPollIntervalMs,
        m_bSessionNotifyRegistered ? 1 : 0);
}

void CAutoAwayController::Shutdown()
{
    if (m_nTimerId != 0 && m_hHost != NULL && ::IsWindow(m_hHost))
    {
        ::KillTimer(m_hHost, m_nTimerId);
    }
    m_nTimerId = 0;

    if (m_bSessionNotifyRegistered && m_hHost != NULL && ::IsWindow(m_hHost))
    {
        ::WTSUnRegisterSessionNotification(m_hHost);
    }
    m_bSessionNotifyRegistered = false;

    //退出前释放系统电源请求，本机恢复正常的休眠与锁屏行为。析构函数也会调到这里，
    //即便宿主忘了调用 Shutdown 也不会把电源请求留到进程结束。
    m_keepAwakeSession.Release();
    m_dwLastKeepAwakeNudgeTick = 0;
}

bool CAutoAwayController::PumpTimer(UINT_PTR nIDEvent)
{
    if (m_nTimerId == 0 || nIDEvent != m_nTimerId)
    {
        return false;
    }

    CheckAutoAway();
    //「禁止机器休眠」复用同一个定时器：它同样是 30 秒粒度的周期性处理，无需另挂一个。
    //两者互斥，同一次触发中至多只有一个会真正执行动作。
    CheckKeepAwake();
    return true;
}

void CAutoAwayController::OnPresenceChanged(int presence, int source)
{
    //维护「当前这个离开是不是自动切过去的」。
    //  · 手动改状态一律清标志 —— 这一条最关键：自动离开期间用户自己切成了忙碌，
    //    那他回到电脑前就不该被擅自切回在线。
    //  · 自动切换时，只有目标是「离开」才置位；自动切回在线要清掉。
    if (source == kPresenceChangeAuto)
    {
        m_bAutoAway = (presence == kMyPresenceAway);
    }
    else
    {
        m_bAutoAway = false;
    }
}

LRESULT CAutoAwayController::OnSessionChange(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/)
{
    // 锁屏那一刻人肯定已经不在了，不必再等空闲满阈值 —— 否则同事在随后的 5 分钟里
    // 看到的仍是"在线"，发了消息以为你马上就回。解锁则立刻切回在线（前提是这个
    // "离开"本来就是自动切过去的，用户自己选的离开不动）。
    const int kind = ClassifySessionEvent((int)wParam);

    // 无条件先输出一条。这条日志是排查「禁止机器休眠」时"是否发生过锁屏"的<u>直接证据</u>：
    // 开启该功能并静置一段时间后，日志中若始终没有出现 WTS_SESSION_LOCK（状态码 0x7），
    // 即可确认锁屏确实被阻止，无需推断。同时带上当时的功能开关状态与全系统空闲时长，
    // 使这一条日志自身即可说明上下文。
    const bool bKeepAwakeOn = (m_lpFMGClient != NULL)
                           && (m_lpFMGClient->m_UserConfig.IsEnableKeepAwake() != FALSE);
    LOG_INFO("session event: code=0x%X, kind=%d (0 ignore / 1 user left / 2 user back), "
             "keepAwake=%d, holdingPowerRequest=%d, systemIdle %u sec.",
             (unsigned int)wParam, kind,
             (int)bKeepAwakeOn,
             (int)m_keepAwakeSession.IsHolding(),
             (unsigned int)(CKeepAwakeSession::QuerySystemIdleMs() / 1000));

    if (kind == kSessionEventNone)
    {
        // 登录 / 注销 / 远程协助等与"人在不在电脑前"无关的会话事件，忽略。
        return 0;
    }

    AutoAwayInput input;
    if (!BuildAutoAwayInput(input))
    {
        return 0;
    }

    ApplyAutoAwayAction(DecideAutoAwayOnSessionEvent(input, kind), input,
                        (kind == kSessionEventLeave) ? "session-lock" : "session-unlock");
    return 0;
}

void CAutoAwayController::CheckAutoAway()
{
    AutoAwayInput input;
    if (!BuildAutoAwayInput(input))
    {
        return;
    }

    ApplyAutoAwayAction(DecideAutoAway(input), input, "idle-poll");
}

bool CAutoAwayController::BuildAutoAwayInput(AutoAwayInput& input)
{
    if (m_lpFMGClient == NULL || m_pHost == NULL)
    {
        return false;
    }

    // 每次都重新读一次配置：这样用户在系统设置里改开关 / 改时长，下一次判定就生效，
    // 设置窗不必再往这边推送通知。
    const CUserConfig& cfg = m_lpFMGClient->m_UserConfig;

    // 全系统空闲时长。GetLastInputInfo 取的是整台机器最后一次键鼠输入的时刻，跨所有
    // 程序 —— 用户在浏览器里忙活也不会被判成空闲，这正是"电脑无操作"该有的口径。
    LASTINPUTINFO lii = {};
    lii.cbSize = sizeof(lii);
    if (!::GetLastInputInfo(&lii))
    {
        // 取不到就这次不判。不记日志：定时器每 30 秒一跳，真出问题会刷屏。
        return false;
    }

    input.m_presence    = m_pHost->GetCurrentPresence();
    input.m_isAutoAway  = m_bAutoAway;
    input.m_idleMs      = ComputeIdleMs(::GetTickCount(), lii.dwTime);
    input.m_thresholdMs = (DWORD)balloonsettings::AutoAwayDelayMinutesFromIndex(
                              (int)cfg.GetAutoAwayDelayIndex()) * kMinuteToMs;
    // 连接就绪的判据与"直接发 cmd_change_presence"那条路径同源：已登录且主面板处于
    // 正常登录态，才谈得上改状态（见 ISessionStateHost::IsSessionEstablished）。
    input.m_connected   = m_pHost->IsSessionEstablished();
    // 与「禁止机器休眠」互斥（2026-08-12 与用户确认）：该功能通过周期性合成输入事件
    // 推迟锁屏，而本功能正是依据"多久没有输入"判定空闲，合成会把空闲计时器重置为 0，
    // 自动离开将永远不会触发。与其让它保持开启却不产生效果，不如在该功能开启期间明确
    // 不生效 —— 设置页也会把「自动离开」相关两行置为禁用并说明原因
    // （见 ApplyKeepAwakeExclusion）。
    // 注意此处<u>不</u>修改配置中 AutoAway 的取值：关闭禁止休眠后它原样恢复，不丢失
    // 用户此前的选择。
    input.m_enabled     = (cfg.IsEnableAutoAway() != FALSE) && (cfg.IsEnableKeepAwake() == FALSE);
    return true;
}

void CAutoAwayController::ApplyAutoAwayAction(int action, const AutoAwayInput& input,
                                              const char* pszTrigger)
{
    if (action == kAutoAwayNone)
    {
        return;
    }

    const int target = (action == kAutoAwayGoAway) ? kMyPresenceAway : kMyPresenceOnline;

    LOG_INFO("auto-away[%s]: idle %u sec (threshold %u sec), presence %d -> %d.",
        pszTrigger,
        (unsigned int)(input.m_idleMs / 1000), (unsigned int)(input.m_thresholdMs / 1000),
        input.m_presence, target);

    // 走与手动切换同一个漏斗，胶囊、托盘图标、协议下发全都自动跟上；来源标为自动，
    // 宿主据此回调 OnPresenceChanged 维护 m_bAutoAway。
    if (m_pHost != NULL)
    {
        m_pHost->ChangePresence(target, kPresenceChangeAuto);
    }
}

void CAutoAwayController::CheckKeepAwake()
{
    if (m_lpFMGClient == NULL || m_pHost == NULL)
    {
        return;
    }

    // 与自动离开一样，每次触发都重新读取配置：用户在设置中改动开关后，下一次触发
    // （最多 30 秒）即生效，设置窗口不必向此处推送通知。
    const CUserConfig& cfg = m_lpFMGClient->m_UserConfig;

    KeepAwakeInput input;
    input.m_enabled       = (cfg.IsEnableKeepAwake() != FALSE);
    // 未登录时不占用系统电源请求 —— 停留在登录界面的客户端没有理由阻止整台机器休眠。
    input.m_loggedIn      = m_pHost->IsLoggedIn();
    input.m_holding       = m_keepAwakeSession.IsHolding();
    input.m_nowTick       = ::GetTickCount();
    input.m_lastNudgeTick = m_dwLastKeepAwakeNudgeTick;

    switch (DecideKeepAwake(input))
    {
    //刚开启功能（或刚登录）：获取系统电源请求，并立即合成一次输入事件。若不立即合成，
    //首次合成要等满一个间隔才会发生，这段间隔足以让超时较短的锁屏策略生效。
    case kKeepAwakeAcquire:
        CKeepAwakeSession::LogIdlePolicySnapshot();
        if (m_keepAwakeSession.Acquire())
        {
            m_keepAwakeSession.Nudge();
            m_dwLastKeepAwakeNudgeTick = ::GetTickCount();
        }
        break;

    //开关刚被关闭，或已退出登录：释放电源请求，本机恢复正常的休眠与锁屏行为。
    case kKeepAwakeRelease:
        m_keepAwakeSession.Release();
        m_dwLastKeepAwakeNudgeTick = 0;
        break;

    //持有期间的常规处理：距上次合成已满一个间隔，再合成一次以重置输入空闲计时器。
    case kKeepAwakeNudge:
        m_keepAwakeSession.Nudge();
        m_dwLastKeepAwakeNudgeTick = ::GetTickCount();
        break;

    //功能未开启（默认状态，多数情况下都走这条分支），或距上次合成尚未满一个间隔：
    //不执行任何动作。
    case kKeepAwakeNone:
    default:
        break;
    }
}

} // namespace balloonmain
