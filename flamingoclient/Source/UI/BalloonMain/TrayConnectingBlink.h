/**
 *  TrayConnectingBlink.h
 *
 *  托盘图标「正在连接」时亮灰交替的<u>纯逻辑状态机</u>。界面连接态为「正在连接」（掉线重连、
 *  从离线恢复在线、启动时服务器暂时连不上）时，托盘图标在亮色（在线图标）与灰色（离线图标）
 *  之间交替，告诉用户程序正在尝试连上服务器；连上之后停在亮色，转为离线之后停在灰色。
 *
 *  本文件有两个状态机：CTrayConnectingBlink 回答「此刻要不要交替、该显示哪一帧」；CTrayUnreadHold
 *  回答「未读提醒（头像闪烁、悬停的未读浮窗）此刻放不放行」—— 重连期间暂停，连上后再等
 *  kTrayUnreadResumeDelayMs 才恢复。两者都不碰 HWND、定时器与 Shell_NotifyIcon，便于被 FlamingoUnit
 *  测试工程直接断言（Tests/TrayConnectingBlinkTests.cpp）。定时器与图标切换在托盘控制器
 *  CTrayController（MainPanelTrayController.cpp）里做。
 *
 *  典型用法：
 *      balloonmain::CTrayConnectingBlink blink;
 *      //每次按连接态重取托盘图标时
 *      const balloonmain::TrayBlinkAction action = blink.OnConnUiState(connUiState);
 *      if (action == balloonmain::kTrayBlinkStart)
 *          ...启动交替定时器，显示 blink.CurrentFrame()...
 *      else if (action == balloonmain::kTrayBlinkStop)
 *          ...停掉交替定时器，图标按连接态常态显示...
 *      //交替定时器每触发一次
 *      const balloonmain::TrayBlinkFrame frame = blink.Tick();
 *
 *  balloonwj@qq.com   2026-10-03
 */
#pragma once

namespace balloonmain {

/**
 *  托盘图标的两帧。取值同时用作托盘控制器两帧图标数组的下标。
 */
enum TrayBlinkFrame
{
    kTrayBlinkFrameBright = 0,  // 亮色：在线图标（imonline.ico）；交替开始时的第一帧，也是连上之后停留的那一帧
    kTrayBlinkFrameGray   = 1,  // 灰色：离线图标（offline.ico）；交替中的另一帧，也是转为离线之后停留的那一帧
};

//两帧图标的个数，托盘控制器按它声明两帧图标数组。
const int kTrayBlinkFrameCount = 2;

/**
 *  OnConnUiState 告诉调用方该对交替定时器做什么。
 */
enum TrayBlinkAction
{
    kTrayBlinkNone  = 0,  // 交替与否没有变化：连接态没变，或在两个不交替的连接态之间切换（在线 ↔ 离线），定时器保持现状
    kTrayBlinkStart = 1,  // 刚进入「正在连接」：启动交替定时器，并立即显示 CurrentFrame()（亮色）
    kTrayBlinkStop  = 2,  // 刚离开「正在连接」：停掉交替定时器，图标按连接态常态显示（CurrentFrame() 给出亮色或灰色）
};

/**
 *  托盘图标「正在连接」亮灰交替的状态机。只有两个状态量：是否在交替、当前是哪一帧。
 *  生命周期跟随托盘控制器；初始为「不交替、停在灰色」，与进程刚启动、尚未登录时的灰色图标一致。
 */
class CTrayConnectingBlink
{
public:
    CTrayConnectingBlink();

    /**
     *  告知当前的界面连接态，据此开始或停止交替。可以重复调用：同一个连接态连续传入时，
     *  只有第一次可能返回开始或停止，其后都返回 kTrayBlinkNone。
     *    connUiState：界面连接态，取值见 OnlineCapability.h 的 ConnectionUiState；越界取值按离线处理。
     *  @return 调用方该对交替定时器做的动作，见 TrayBlinkAction。
     *  副作用：开始交替时把当前帧置为亮色；停止交替或本来就不交替时，把当前帧置为该连接态的常态帧。
     */
    TrayBlinkAction OnConnUiState(int connUiState);

    /**
     *  交替定时器触发一次：切换到另一帧。
     *  @return 切换后的帧。不在交替中时不切换，原样返回当前帧。
     */
    TrayBlinkFrame Tick();

    /**
     *  当前是否处于交替中，即最近一次传入的连接态是不是「正在连接」。
     */
    bool IsBlinking() const;

    /**
     *  当前应显示的帧：交替中为正在显示的那一帧；不交替时为最近一次连接态的常态帧。
     */
    TrayBlinkFrame CurrentFrame() const;

    /**
     *  某个连接态下不交替时应停留的帧：在线为亮色，正在连接与离线都为灰色，与 GetTrayIconFileName
     *  同一口径（只有真正在线才用彩色图标）。
     *    connUiState：界面连接态，取值见 ConnectionUiState；越界取值按离线处理。
     *  @return 常态帧。本函数无副作用。
     */
    static TrayBlinkFrame RestingFrameFor(int connUiState);

private:
    bool           m_blinking;  // 是否处于交替中；OnConnUiState 收到「正在连接」时置真，收到其它连接态时置假
    TrayBlinkFrame m_frame;     // 当前帧；交替中由 Tick 来回切换，不交替时为最近一次连接态的常态帧
};

//连上之后未读提醒恢复前的等待时长（毫秒）：重连成功后会话列表与离线期间的消息还在补拉，等这一小段
//再恢复未读闪烁与悬停的未读浮窗（2026-10-03 与用户确认的约 3 秒）
const unsigned int kTrayUnreadResumeDelayMs = 3000;

/**
 *  CTrayUnreadHold::OnConnUiState 告诉调用方该对恢复定时器做什么。
 */
enum TrayUnreadHoldAction
{
    kTrayUnreadHoldNone         = 0,  // 恢复定时器保持现状
    kTrayUnreadHoldStartResume  = 1,  // 刚从「正在连接」连上：启动 kTrayUnreadResumeDelayMs 的恢复定时器，到点调 OnResumeTimer
    kTrayUnreadHoldCancelResume = 2,  // 等待恢复期间又断开、或转为离线：停掉恢复定时器
};

/**
 *  重连期间暂停未读提醒的状态机。「未读提醒」指托盘的未读头像闪烁与悬停时弹出的未读消息浮窗；
 *  暂停期间未读本身保留，悬停显示系统提示（昵称 / 账号 / 正在连接）。
 *
 *  规则（2026-10-03 与用户确认）：进入「正在连接」即暂停；连上后不立即恢复，等
 *  kTrayUnreadResumeDelayMs 再恢复；等待期间又断开则作废这次计时，下次连上重新计时；转为离线时
 *  立即恢复 —— 离线期间收不到新消息，没有要等的。首次登录（离线 → 在线）没有暂停，不需要等待。
 *  纯逻辑，不碰 HWND 与定时器，计时器由托盘控制器起停。
 */
class CTrayUnreadHold
{
public:
    CTrayUnreadHold();

    /**
     *  告知当前的界面连接态。可以重复调用，同一个连接态连续传入时只有第一次可能返回非 None。
     *    connUiState：界面连接态，取值见 OnlineCapability.h 的 ConnectionUiState；越界取值按离线处理。
     *  @return 调用方该对恢复定时器做的动作，见 TrayUnreadHoldAction。
     */
    TrayUnreadHoldAction OnConnUiState(int connUiState);

    /**
     *  恢复定时器到点：解除暂停。没有在等待恢复时（计时已被作废、到点消息仍在队列里）什么都不做。
     */
    void OnResumeTimer();

    /**
     *  未读提醒（头像闪烁、悬停的未读浮窗）此刻是否放行。
     *  @return 放行返回 true；正在连接、或连上后还在等待恢复时返回 false。
     */
    bool AllowUnread() const;

    /**
     *  是否正在等待恢复定时器到点。
     */
    bool IsResumePending() const;

private:
    bool m_held;           // 未读提醒是否暂停中；进入「正在连接」时置真，恢复定时器到点或转为离线时置假
    bool m_resumePending;  // 是否已要求启动恢复定时器、还没到点；作废或到点时置假
};

} // namespace balloonmain
