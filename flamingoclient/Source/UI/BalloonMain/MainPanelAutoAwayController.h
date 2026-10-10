/**
 *  自动离开控制器：把「无操作自动离开」与「禁止机器休眠」这两项设置的运行期逻辑集中
 *  在一个类里。
 *
 *  两项功能共用一个 30 秒周期的定时器，且互相排斥 —— 禁止休眠靠周期性向系统合成输入
 *  事件来推迟锁屏，而自动离开正是依据「多久没有输入」判定空闲，两者同时开启时后者
 *  永远不会触发。所以它们放在一起，同一次定时器触发中至多只有一个真正执行动作。
 *
 *  判定规则本身不在本类里，而在 AutoAwayPolicy.h 与 KeepAwakePolicy.h 的纯函数中
 *  （已有单元测试覆盖，见 Tests/AutoAwayPolicyTests.cpp）。本类只负责三件事：按周期
 *  装配判定输入、把判定结果落到实际动作上、以及订阅锁屏 / 解锁事件作为第二条触发路径。
 *
 *  典型用法：由宿主窗口按值持有，窗口创建之后调一次 Attach，并在自己的消息映射里加
 *  一行 CHAIN_MSG_MAP_MEMBER，锁屏 / 解锁事件即由本类接管；WM_TIMER 则由宿主转交。
 *
 *      // 宿主的消息映射
 *      CHAIN_MSG_MAP_MEMBER(m_autoAwayController)
 *
 *      // 宿主窗口创建之后
 *      m_autoAwayController.Attach(m_hWnd, &m_FMGClient, this);
 *
 *      // 宿主的 WM_TIMER 处理里
 *      if (m_autoAwayController.PumpTimer(nIDEvent))
 *          return;
 *
 *  本类不是窗口，只是一个带消息映射的普通对象，靠 CHAIN_MSG_MAP_MEMBER 搭在宿主窗口的
 *  消息派发链上。之所以不让它自建窗口：网络应答的收件窗口全局只有一个（见
 *  CFlamingoClient::SetCallBackWnd），另开窗口就得多一次转发。
 *
 *  balloonwj@qq.com   2026-08-19
 */

#pragma once

#include "AutoAwayPolicy.h"              // balloonmain::AutoAwayInput / DecideAutoAway 等判定纯函数
#include "KeepAwakeSession.h"            // balloonmain::CKeepAwakeSession（系统电源请求持有者）
#include "MainPanelSessionHost.h"        // balloonmain::ISessionStateHost（向宿主打听在线状态 / 请求切换状态）

class CFlamingoClient;

namespace balloonmain {

// 「无操作自动离开」与「禁止机器休眠」共用的轮询定时器标识。
// 取 30 秒一跳：自动离开的最短阈值是 1 分钟，禁止休眠的合成间隔是分钟级，30 秒的
// 粒度对两者都够用，又不至于频繁唤醒进程。
const UINT_PTR kAutoAwayTimerId = 1002;

// 轮询间隔，单位毫秒。
const UINT kAutoAwayPollIntervalMs = 30 * 1000;

/**
 *  自动离开控制器。
 *
 *  生命周期与宿主窗口一致：Attach 时起定时器并订阅会话事件，Shutdown（或析构）时停掉
 *  定时器、退订会话事件、释放可能仍持有的系统电源请求。
 */
class CAutoAwayController
{
public:
    CAutoAwayController();
    ~CAutoAwayController();

    /**
     *  绑定宿主并启动。须在宿主窗口创建之后调用一次。
     *
     *    hHost    宿主窗口句柄，本类不拥有。两处用途：定时器的宿主窗口；订阅锁屏 /
     *             解锁事件（WM_WTSSESSION_CHANGE）的收件窗口。
     *    pClient  网络客户端，本类不拥有。只读用户配置里的两个开关与自动离开时长。
     *             可为 NULL，此时两项功能都按「未开启」处理。
     *    pHost    会话状态宿主接口，本类不拥有。不可为 NULL。
     */
    void Attach(HWND hHost, CFlamingoClient* pClient, ISessionStateHost* pHost);

    /**
     *  停止：杀掉定时器、退订会话事件、释放系统电源请求。
     *  宿主退出前调用；未 Attach 过时调用也安全，可重复调用。
     */
    void Shutdown();

    /**
     *  把定时器消息交给本类处理。
     *    nIDEvent  WM_TIMER 带来的定时器标识。
     *  返回 true 表示这个定时器归本类，宿主不必再往下判断；返回 false 表示不认识，
     *  宿主继续处理自己的定时器。
     */
    bool PumpTimer(UINT_PTR nIDEvent);

    /**
     *  通知本类：本人的在线状态刚刚被切换过。
     *
     *  必须由「切换在线状态」的唯一漏斗调用一次，本类据此维护「当前这个离开是不是
     *  自动切过去的」—— 这个标志决定了检测到键鼠操作时该不该把用户自动切回在线。
     *    presence  切换后的在线状态（kMyPresenceOnline 等）。
     *    source    本次切换的来源，取值见 Session/UserSessionData.h 的
     *              PresenceChangeSource（0=用户手动，1=本功能自动切的）。
     */
    void OnPresenceChanged(int presence, int source);

    BEGIN_MSG_MAP_EX(CAutoAwayController)
        //锁屏 / 解锁 / 切换用户等会话事件：锁屏立刻切离开，解锁切回在线
        MESSAGE_HANDLER_EX(WM_WTSSESSION_CHANGE, OnSessionChange)
    END_MSG_MAP()

private:
    // 锁屏 / 解锁等会话事件（wParam 为 WTS_* 状态码）。这是自动离开的第二条触发路径 ——
    // 锁屏那一刻人肯定已经不在了，不必再等空闲满阈值。
    LRESULT OnSessionChange(UINT uMsg, WPARAM wParam, LPARAM lParam);

    // 执行一次「无操作自动离开」判定：装配输入、交给判定函数、按结果动作。
    void CheckAutoAway();

    // 执行一次「禁止机器休眠」判定：读取设置与当前持有状态，交给判定函数，按结果获取
    // 或释放系统电源请求、或向系统合成一次输入事件。
    void CheckKeepAwake();

    /**
     *  装配一次「自动离开」判定所需的输入（当前状态、空闲时长、阈值、连接态、开关）。
     *  空闲轮询与锁屏事件两条触发路径共用它，保证两边看到的是同一份口径。
     *    input  出参，装配结果。
     *  返回：取不到系统空闲时长时返回 false（此时 input 内容无意义，调用方应放弃本次判定）。
     */
    bool BuildAutoAwayInput(AutoAwayInput& input);

    /**
     *  执行一次「自动离开」判定的结果：该切就打日志并请求宿主切换在线状态，
     *  kAutoAwayNone 时什么都不做。
     *    action      判定结果，取值见 AutoAwayPolicy.h 的 AutoAwayAction。
     *    input       本次判定的输入，仅用于打日志（空闲时长 / 阈值 / 原状态）。
     *    pszTrigger  触发来源的英文短标签，只进日志，便于事后区分是空闲轮询还是锁屏引起的。
     */
    void ApplyAutoAwayAction(int action, const AutoAwayInput& input, const char* pszTrigger);

    HWND m_hHost;                       // 宿主窗口句柄，借用不拥有；定时器宿主与会话事件收件窗口
    CFlamingoClient* m_lpFMGClient;     // 网络客户端，借用不拥有；只读用户配置
    ISessionStateHost* m_pHost;         // 会话状态宿主接口，借用不拥有

    // 当前这个「离开」是否为本功能自动切过去的。
    //
    // 只有它为 true 时，检测到键鼠操作才会把用户自动切回在线；用户自己手动选的离开
    // 不会被改动。任何一次手动改状态都会把它清掉（见 OnPresenceChanged），这样
    // 「自动离开期间用户手动切成忙碌」之后回到电脑前也不会被擅自切回在线。
    // 不持久化：每次启动都由用户在登录窗选定初始状态，自动标记从头算起。
    bool m_bAutoAway;

    // 「禁止机器休眠」的系统电源请求持有者。功能关闭时它不持有任何资源，析构时会
    // 自动释放。只在主线程访问（定时器与消息处理），见 KeepAwakeSession.h 的说明。
    CKeepAwakeSession m_keepAwakeSession;

    // 上次向系统合成输入事件时的 tick（::GetTickCount()）；0 表示本次持有期间尚未
    // 合成过。与 m_keepAwakeSession 一同构成 KeepAwakePolicy 判定所需的状态。
    DWORD m_dwLastKeepAwakeNudgeTick;

    // 轮询定时器标识；0 表示定时器未启动（Attach 之前，或 Shutdown 之后）。
    UINT_PTR m_nTimerId;

    // 是否已经成功订阅过会话事件。Shutdown 时据它决定要不要退订 —— 订阅失败时退订
    // 没有意义，且会在日志里留下无谓的失败记录。
    bool m_bSessionNotifyRegistered;

    // 消息映射用的「本条消息是否已处理」标志。BEGIN_MSG_MAP_EX 会自行声明它，
    // 但不会初始化；本类不是窗口，首次派发时它会被先读后写，故须在构造函数里置初值。
    // 声明由宏生成，此处只是说明，不重复声明。
};

} // namespace balloonmain
