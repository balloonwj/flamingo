/**
 *  托盘控制器：系统托盘那只图标的全部行为集中在一个类里 —— 图标本身、悬停提示文字、
 *  新消息时的头像闪烁、自动登录时的转圈动画、左键单击、右键菜单，以及鼠标停留时弹出的
 *  未读消息浮窗。
 *
 *  本类不是窗口，它借用宿主窗口的句柄来挂托盘图标、起定时器、以及作为右键菜单的属主。
 *  宿主须把这几类消息转给本类：托盘回调消息 WM_TRAYICON_NOTIFY、WM_TIMER，以及
 *  FMG_MSG_TRAY_FLASH / FMG_MSG_TRAY_CLEAR / FMG_MSG_TRAY_MINIMIZE_TIP 三条。
 *
 *  典型用法：
 *
 *      m_trayController.Attach(m_hWnd, &m_BalloonMainPanel, &m_FMGClient, this);
 *      m_trayController.AddIcon(_T("Flamingo未登录"));
 *      ...
 *      // 宿主的 OnTimer 开头
 *      if (m_trayController.PumpTimer(nIDEvent))
 *          return;
 *
 *  balloonwj@qq.com   2026-08-18
 */

#pragma once

#include "../../App/TrayIcon.h"          // CTrayIcon（Shell_NotifyIcon 的薄封装）
#include "TrayFlashController.h"         // balloonmain::CTrayFlashController（未读会话集合与闪烁状态）
#include "TrayMsgTipPopup.h"             // CTrayMsgTipPopup（鼠标停留在托盘图标上时的未读消息浮窗）
#include "MainPanelSessionHost.h"        // balloonmain::ISessionStateHost（向宿主打听在线状态 / 请求切换状态）
#include "../../App/CustomMsgDef.h"      // WM_TRAYICON_NOTIFY（托盘图标的回调消息号）
#include "../../Session/UserSessionData.h"  // FMG_MSG_TRAY_* 三条消息号

class CBalloonMainPanel;
class CFlamingoClient;

namespace balloonmain {

/**
 *  托盘控制器。
 *
 *  生命周期与进程一致：进程启动时挂上图标（那时还没登录，显示灰色的「未登录」），
 *  登出时摘掉再挂上，退出时由 Shutdown 收尾。
 */
class CTrayController
{
public:
    CTrayController();
    ~CTrayController();

    /**
     *  绑定宿主与协作对象。须在挂图标之前调用一次。
     *
     *    hHost    宿主窗口句柄，本类不拥有。三处用途：托盘图标的回调窗口、定时器的
     *             宿主窗口、右键菜单的属主窗口。
     *    pPanel   主面板，本类不拥有。单击托盘图标打开最近未读会话时要用它。可为 NULL。
     *    pClient  网络客户端，本类不拥有。只读昵称 / 账号（悬停提示）与提示音开关。可为 NULL。
     *    pHost    宿主回调接口，本类不拥有。不可为 NULL。
     */
    void Attach(HWND hHost, CBalloonMainPanel* pPanel, CFlamingoClient* pClient, ISessionStateHost* pHost);

    /**
     *  挂上托盘图标。图标按当前连接态与在线状态现取。
     *    tip  鼠标停留在图标上时显示的提示文字（系统限制 64 个字符）。
     */
    void AddIcon(LPCTSTR tip);

    // 摘掉托盘图标。登出与退出时调用；未挂图标时调用也安全。
    void RemoveIcon();

    // 按当前连接态与在线状态重取图标并连同悬停提示一起推给系统。
    // 连接态或在线状态发生变化后调用。
    void RefreshTrayIconForStatus();

    // 只刷新悬停提示文字，不动图标。用于「图标没变、只是提示文字要跟着改」的场合。
    void RefreshTipOnly();

    // 按当前连接态与在线状态重取图标，但不推给系统。用于「随后还会再推一次」的场合，
    // 避免中间态在托盘上闪一下。
    void ReloadAppIcon();

    // 掉线重连时弹一次「正在帮您重连」的托盘气泡，同时把图标换成当前（灰色）那只。
    void ShowReconnectingBalloon();

    // 起 / 停自动登录期间的托盘图标转圈动画。手动登录不用它 —— 登录窗自己会显示
    // 「登录中」，托盘无须重复提示。
    void StartLoginAnimation();
    void StopLoginAnimation();

    /**
     *  取托盘图标在屏幕上的矩形，供别处把提示框对准图标弹出（如被踢下线的提示）。
     *    lpRect  [出参] 图标矩形，屏幕坐标。
     *  返回 TRUE 表示取到了。
     */
    BOOL GetTrayIconRect(RECT* lpRect);

    /**
     *  把定时器消息交给本类处理。
     *    nIDEvent  WM_TIMER 带来的定时器标识。
     *  返回 true 表示这个定时器归本类，宿主不必再往下判断；返回 false 表示不认识，
     *  宿主继续处理自己的定时器。
     */
    bool PumpTimer(UINT_PTR nIDEvent);

    // 退出程序时的收尾：停两个定时器、摘图标、销毁浮窗与全部图标资源。
    void Shutdown();

    // 供宿主诊断日志使用：悬停未读提示浮窗当前是否还持有窗口。
    BOOL IsMsgTipPopupWindow() const;

    BEGIN_MSG_MAP_EX(CTrayController)
        //托盘图标的鼠标事件（左键单击、右键弹菜单、悬停、离开）
        MESSAGE_HANDLER_EX(WM_TRAYICON_NOTIFY, OnTrayIconNotify)
        //有新消息：把该会话记进未读集合并开始闪烁发送方头像
        MESSAGE_HANDLER_EX(FMG_MSG_TRAY_FLASH, OnV2TrayFlash)
        //某个会话已读：从未读集合移除，集合空了就停止闪烁
        MESSAGE_HANDLER_EX(FMG_MSG_TRAY_CLEAR, OnV2TrayClear)
        //主面板首次最小化隐藏到托盘：弹一次气泡告诉用户程序还在托盘里
        MESSAGE_HANDLER_EX(FMG_MSG_TRAY_MINIMIZE_TIP, OnTrayMinimizeTip)
    END_MSG_MAP()

    // ---- 以下四个由上面的消息映射派发进来 ----

    // 托盘图标的回调消息（WM_TRAYICON_NOTIFY）：lParam 是鼠标事件。
    LRESULT OnTrayIconNotify(UINT uMsg, WPARAM wParam, LPARAM lParam);

    // 有新消息：把该会话记进未读集合并开始闪烁发送方头像。
    // lParam 为堆上的 balloonmain::TrayFlashData*，处理后由本方法 delete。
    LRESULT OnV2TrayFlash(UINT uMsg, WPARAM wParam, LPARAM lParam);

    // 某个会话已读：从未读集合移除，集合空了就停止闪烁。
    // wParam=会话类型，lParam=堆上的 long long* 会话 id，处理后由本方法 delete。
    LRESULT OnV2TrayClear(UINT uMsg, WPARAM wParam, LPARAM lParam);

    // 主面板首次最小化隐藏到托盘：弹一次气泡告诉用户程序还在托盘里。
    LRESULT OnTrayMinimizeTip(UINT uMsg, WPARAM wParam, LPARAM lParam);

private:
    // 悬停提示文字：第一行昵称、第二行账号、第三行在线状态与连接态。
    CString BuildTrayTipText();

    // 只把悬停提示文字推给系统（图标沿用当前那只）。
    void UpdateTrayIconTip();

    // 按连接态取图标文件、叠加在线状态角标，结果存进 m_hAppIcon。
    //   nConnUiState  连接态（balloonmain::kConnUi*）。
    // 返回 TRUE 表示取到了图标。
    BOOL LoadAppIcon(int nConnUiState);
    void DestroyAppIcon();

    // 自动登录动画的六帧图标。
    BOOL LoadLoginIcon();
    void DestroyLoginIcon();

    // 按未读集合里最近那条消息的发送方，生成闪烁用的头像图标存进 m_hMsgIcon。
    void UpdateTrayFlashIcon();

    // 停止闪烁：停定时器、释放头像图标、把图标与提示恢复成常态。
    void StopTrayFlash();

    // 闪烁定时器的一次触发：在应用图标与发送方头像之间切换一帧。
    void OnFlashTimerTick();

    // 提示音开关的读取与翻转（翻转后立即落盘）。
    bool IsMuted() const;
    void ToggleMute();

    // 托盘图标上的四种鼠标操作。
    void OnTrayIcon_LButtunUp();
    void OnTrayIcon_RButtunUp();
    void OnTrayIcon_MouseHover();
    void OnTrayIcon_MouseLeave();

    // 未读消息浮窗里点了某条会话时的回调。浮窗的回调是 C 风格函数指针，故用静态函数
    // 做跳板，userdata 传的是 this。
    static void OnTrayPopupPickConv(void* userdata, int ctype, long long cid);
    void OpenConvFromTrayPopup(int ctype, long long cid);

    HWND m_hHost;                       // 宿主窗口句柄，借用不拥有；托盘回调窗口、定时器宿主、右键菜单属主
    CBalloonMainPanel* m_pPanel;        // 主面板，借用不拥有；单击托盘打开会话时用
    CFlamingoClient* m_lpFMGClient;     // 网络客户端，借用不拥有；只读昵称 / 账号与提示音开关
    ISessionStateHost* m_pHost;                 // 宿主回调接口，借用不拥有

    CTrayIcon m_TrayIcon;               // 托盘图标本身，对 Shell_NotifyIcon 的封装
    HICON m_hAppIcon;                   // 常态图标：按连接态取底图、叠在线状态角标；由 LoadAppIcon 生成、DestroyAppIcon 释放
    HICON m_hLoginIcon[6];              // 自动登录动画的六帧图标；由 LoadLoginIcon 生成、DestroyLoginIcon 释放
    int m_nCurLoginIcon;                // 登录动画当前播到第几帧，取值 0~5
    HICON m_hMsgIcon;                   // 新消息闪烁用的发送方头像图标；每轮闪烁开始时重建、停闪时释放
    DWORD m_dwLoginTimerId;             // 登录动画定时器标识；0 表示动画没在跑
    DWORD m_dwMsgTimerId;               // 新消息闪烁定时器标识；0 表示没在闪
    CTrayFlashController m_trayFlash;   // 未读会话集合与闪烁状态机：哪些会话未读、最近一条是哪个
    bool m_bTrayFlashTickLogged;        // 本轮闪烁是否已经打过「首次 tick」那条诊断日志；每轮开始时复位
    CTrayMsgTipPopup m_trayMsgTipPopup; // 鼠标停留在托盘图标上时弹出的未读消息浮窗
    CRect m_rcTrayIconRect;             // 最近一次取到的托盘图标屏幕矩形，浮窗据它定位
};

} // namespace balloonmain
