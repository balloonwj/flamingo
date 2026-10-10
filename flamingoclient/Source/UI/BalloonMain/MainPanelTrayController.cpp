/**
 *  托盘控制器的实现。本类不是窗口：托盘图标、定时器、右键菜单都挂在宿主句柄 m_hHost
 *  （也就是主面板）上，需要在线状态 / 登录态时经 ISessionStateHost 询问。
 *
 *  balloonwj@qq.com   2026-08-18
 */

#include "stdafx.h"
#include "MainPanelTrayController.h"

#include <memory>                                   // std::unique_ptr（托盘菜单图标保活到 TrackPopup 返回）
#include <vector>

#include "BalloonMainPanel.h"                       // CBalloonMainPanel::OpenConversation
#include "MainPanelTrayMenu.h"                      // BuildTrayMenuModel / TrayMenuId（托盘菜单行模型）
#include "MainPanelMenuIcons.h"                     // CreateMenuIconImage / CreateStatusIconImage（菜单项图标渲染）
#include "MainPanelAvatar.h"                        // MakeMonogramIcon（闪烁用的发送方头像图标）
#include "OnlineCapability.h"                       // 连接态推导、托盘图标文件名与状态角标
#include "PopupMenuPlacement.h"                     // GetMenuPlacementArea / ClampMenuOriginToRect（菜单落点夹取）
#include "../../App/FlamingoClient.h"               // CFlamingoClient::m_UserMgr / m_UserConfig
#include "../../Common/Graphics/ImageEx.h"          // CImageEx（托盘菜单项图标）
#include "../../Common/Log/IULog.h"                 // LOG_INFO / LOG_WARNING
#include "../../Common/Util/EncodeUtil.h"           // UnicodeToUtf8（日志里的宽字符串路径转 UTF-8）
#include "../../Common/Util/Utils.h"                // Hootina::CPath::GetAppPath
#include "../../Session/UserSessionData.h"          // TrayFlashData / kMyPresence* / kPresenceChange*
#include "../../Feature/ScreenShare/RecordEntry.h"  // 托盘菜单「发起录制」
#include "../../Feature/ScreenShare/ScreenShareHub.h"  // 托盘菜单「发起屏幕共享」是否出现（CScreenShareHub::IsModuleReady）
#include "../../Feature/Lock/LockHost.h"     // 托盘菜单「锁屏」是否出现，以及点了之后请求锁屏
#include "../../Feature/Markdown/MarkdownDocument.h"    // IsMarkdownAvailable：托盘菜单是否出现「Markdown 编辑器」
#include "../BalloonMarkdown/MarkdownEditorManager.h"   // 托盘菜单「Markdown 编辑器」打开编辑器
#include "../../Feature/Screenshot/ScreenshotEntry.h"   // 托盘菜单「截图」的文字（全局配置里的快捷键与注册结果）
#include "../../Feature/Lock/LockSuppressPolicy.h"  // DecideDuringLock：锁屏期间不弹托盘气泡
#include "../../../../third-party/balloonui/balloonui/Controls/List/DuiMenu.h"  // balloonui 弹出菜单
#include "../../Common/I18n/I18n.h"   // TR：界面文字的多语言查找

namespace balloonmain {

// 托盘图标 id。AddIcon / ModifyIcon / RemoveIcon 各处必须一致；本程序只挂一只图标。
static const UINT kTrayIconId = 1;

// 两个定时器的标识。它们挂在宿主窗口上，取值须与宿主自己的定时器错开 —— 宿主当前用
// 5 作断线重连、6 作「无操作自动离开」，主面板用 0xB001 作滚动条动画。
// 1 原为自动登录的六帧转圈动画（没有调用方，2026-10-03 删除），现由正在连接的亮灰交替沿用。
static const UINT_PTR kConnectingBlinkTimerId = 1;
static const UINT_PTR kFlashTimerId           = 2;
// 重连成功后恢复未读提醒的一次性定时器，时长见 balloonmain::kTrayUnreadResumeDelayMs。
// CTrayIcon 自己的悬停检测定时器用 990、未读浮窗的隐藏定时器用 991，均不冲突。
static const UINT_PTR kUnreadResumeTimerId    = 3;

// 正在连接时托盘图标亮灰交替的间隔（毫秒），即每 0.5 秒换一帧、一秒一个来回（与用户确认的约
// 500 毫秒）。交替期间未读消息的头像闪烁暂停，两者不会同时出现，间隔不必与之错开。
static const UINT kConnectingBlinkIntervalMs = 500;

// 托盘菜单图标尺寸 / 颜色 —— 与主面板左下角系统菜单同口径，保证两处 DuiMenu 视觉统一。
// DuiMenu 以源矩形 (0,0,16,16) 采样图标，故尺寸取 16。
static const int      kTrayMenuIconSize  = 16;                     // 图标位图边长（像素）
static const COLORREF kTrayMenuIconColor = RGB(0x5B, 0x5B, 0x5B);  // 图标描边色（深灰）

// 「已最小化到托盘」气泡提示的停留时长（毫秒）。比掉线提示（3 秒）略长一点，因为这条
// 提示要让用户看清「去托盘找它」这句话。Vista 及以后系统会按自己的策略统一控制实际
// 停留时间，这个值只作参考。
static const UINT kTrayMinimizeTipTimeoutMs = 5000;

CTrayController::CTrayController()
    : m_hHost(NULL)
    , m_pPanel(NULL)
    , m_lpFMGClient(NULL)
    , m_pHost(NULL)
    , m_hAppIcon(NULL)
    , m_hMsgIcon(NULL)
    , m_dwBlinkTimerId(0)
    , m_dwUnreadResumeTimerId(0)
    , m_dwMsgTimerId(0)
    , m_bTrayFlashTickLogged(false)
{
    for (int i = 0; i < kTrayBlinkFrameCount; ++i)
        m_hBlinkIcon[i] = NULL;
    m_rcTrayIconRect.SetRectEmpty();
    // BEGIN_MSG_MAP_EX 声明的这个标志在首次派发时会被先读后写，非窗口类必须自己置初值。
    m_bMsgHandled = FALSE;
}

CTrayController::~CTrayController()
{
    Shutdown();
}

void CTrayController::Attach(HWND hHost, CBalloonMainPanel* pPanel,
                             CFlamingoClient* pClient, ISessionStateHost* pHost)
{
    m_hHost = hHost;
    m_pPanel = pPanel;
    m_lpFMGClient = pClient;
    m_pHost = pHost;
}

void CTrayController::AddIcon(LPCTSTR tip)
{
    LoadAppIcon(m_pHost->GetConnectionUiState());
    m_TrayIcon.AddIcon(m_hHost, WM_TRAYICON_NOTIFY, kTrayIconId, m_hAppIcon, tip);
}

void CTrayController::RemoveIcon()
{
    m_TrayIcon.RemoveIcon();
}

void CTrayController::RefreshTipOnly()
{
    m_TrayIcon.ModifyTip(TrayTipForCurrentState());
}

void CTrayController::ReloadAppIcon()
{
    LoadAppIcon(m_pHost->GetConnectionUiState());
}

void CTrayController::ShowReconnectingBalloon()
{
    // 重连期间托盘按当前状态取图标（此刻连接已断，正在亮灰交替，取的是当前那一帧），并弹一次气泡提示。
    // 锁屏期间只换图标、不弹气泡（气泡带系统提示音，见 docs/锁屏方案.md 第 3.7 节）
    LoadAppIcon(m_pHost->GetConnectionUiState());
    const BOOL bPopupBalloon =
        (balloonlock::DecideDuringLock(balloonlock::kLockEvtTrayBalloon, balloonlock::IsScreenLocked())
         == balloonlock::kLockActNormal) ? TRUE : FALSE;
    m_TrayIcon.ModifyIcon(m_hAppIcon, TrayTipForCurrentState(), kTrayIconId, bPopupBalloon,
        TR(L"温馨提示"), TR(L"您已经掉线，正在帮您重连......"), 3000);
}

BOOL CTrayController::GetTrayIconRect(RECT* lpRect)
{
    return m_TrayIcon.GetTrayIconRect(lpRect);
}

BOOL CTrayController::IsMsgTipPopupWindow() const
{
    return m_trayMsgTipPopup.IsWindow();
}

bool CTrayController::IsMuted() const
{
    if (m_lpFMGClient == NULL)
        return false;
    return m_lpFMGClient->m_UserConfig.IsEnableMute() ? true : false;
}

void CTrayController::ToggleMute()
{
    if (m_lpFMGClient == NULL)
        return;
    // 托盘右键「关闭所有声音」：翻转静音位。它与系统设置里的「提示音」开关写的是同一个
    // 标志，收消息 / 好友上线的提示音都据此门控。改完立刻持久化到该账号的 .ini，与系统
    // 设置页一致，否则重启后会按配置文件里的旧值复位。
    m_lpFMGClient->m_UserConfig.EnableMute(!m_lpFMGClient->m_UserConfig.IsEnableMute());
    m_lpFMGClient->SaveUserConfig();
}

bool CTrayController::PumpTimer(UINT_PTR nIDEvent)
{
    // CTrayIcon 自己有一个用于检测鼠标停留 / 离开的定时器，它挂在同一个宿主窗口上，
    // 每次 WM_TIMER 都要先喂给它，不能只在自己认领的定时器里喂。
    m_TrayIcon.OnTimer(nIDEvent);

    if (nIDEvent == m_dwMsgTimerId && m_dwMsgTimerId != 0)
    {
        OnFlashTimerTick();
        return true;
    }
    if (nIDEvent == m_dwBlinkTimerId && m_dwBlinkTimerId != 0)
    {
        OnConnectingBlinkTick();
        return true;
    }
    if (nIDEvent == m_dwUnreadResumeTimerId && m_dwUnreadResumeTimerId != 0)
    {
        OnUnreadResumeTick();
        return true;
    }
    return false;
}

void CTrayController::OnConnectingBlinkTick()
{
    const TrayBlinkFrame frame = m_connBlink.Tick();
    HICON hFrame = m_hBlinkIcon[frame];
    if (hFrame == NULL)
    {
        // LoadBlinkIcons 失败时已记过错误日志，这里不再逐次刷日志，保持当前图标
        return;
    }
    // 提示文字传 NULL：只换图标，悬停提示保持「正在连接」那三行不变
    m_TrayIcon.ModifyIcon(hFrame, NULL);
}

void CTrayController::OnFlashTimerTick()
{
    static BOOL bSwitch = FALSE;

    if (!m_unreadHold.AllowUnread())
    {
        // 重连期间（以及连上后的等待期里）未读提醒暂停：托盘此刻在亮灰交替或刚停下，未读闪烁的
        // 定时器照常运行、只是每次都跳过，恢复定时器到点后自然继续。只记一条，免得排查时以为是
        // 闪烁逻辑本身坏了。
        if (!m_bTrayFlashTickLogged)
        {
            m_bTrayFlashTickLogged = true;
            LOG_INFO("Tray flash tick paused: unread reminders are held while reconnecting "
                     "(blinkTimerId=%u, resumeTimerId=%u).", (UINT)m_dwBlinkTimerId,
                     (UINT)m_dwUnreadResumeTimerId);
        }
        return;
    }

    // 两个相位在「应用图标」与「发送方头像」之间来回切。灭的那半拍原先传的是 NULL ——
    // 各版本 Windows 对 NIM_MODIFY 带空图标的处理并不一致，有的会原样留着上一个图标
    // 不动，那样看起来就是根本没闪。换成始终给一个有效图标，任何版本上都能看出两幅
    // 画面在交替。
    // 头像图标万一没生成出来（m_hMsgIcon 为空），两个相位都退回应用图标 —— 宁可不闪，
    // 也不要把托盘图标闪成空白。
    // 提示文字有意传空串而不是 NULL：CTrayIcon::ModifyIcon 只要提示指针非空就带上 NIF_TIP，
    // 空串会把系统的悬停提示清空。闪烁期间悬停托盘由自绘的未读浮窗（OnTrayIcon_MouseHover）
    // 接管，系统提示若仍在，两者会同时出现；停闪时 StopTrayFlash 经 UpdateTrayIconTip 恢复提示。
    bSwitch = !bSwitch;
    BOOL bOk = FALSE;
    if (bSwitch || m_hMsgIcon == NULL)
        bOk = m_TrayIcon.ModifyIcon(m_hAppIcon, _T(""));
    else
        bOk = m_TrayIcon.ModifyIcon(m_hMsgIcon, _T(""));

    // 每轮闪烁只在第一次 tick 记一条：确认定时器确实在跑、Shell_NotifyIcon 是否成功、
    // 以及那一刻用的头像图标是不是空的（空图标会闪成「什么都没有」）。
    if (!m_bTrayFlashTickLogged)
    {
        m_bTrayFlashTickLogged = true;
        LOG_INFO("Tray flash first tick: ModifyIcon returned=%d, avatarIcon=%p, "
                 "conversations=%d.",
                 bOk ? 1 : 0, m_hMsgIcon, m_trayFlash.GetConversationCount());
    }
}

void CTrayController::Shutdown()
{
    StopConnectingBlinkTimer();
    StopUnreadResumeTimer();

    if (m_dwMsgTimerId != 0)
    {
        ::KillTimer(m_hHost, m_dwMsgTimerId);
        m_dwMsgTimerId = 0;
    }

    m_TrayIcon.RemoveIcon();

    // 悬停未读提示浮窗平时只 HidePopup 不销毁，是退出时最容易残留、触发 ATL 析构断言
    // 的那个，这里显式销毁。
    if (m_trayMsgTipPopup.IsWindow())
        m_trayMsgTipPopup.DestroyWindow();

    DestroyAppIcon();

    if (m_hMsgIcon != NULL)
    {
        ::DestroyIcon(m_hMsgIcon);
        m_hMsgIcon = NULL;
    }
}

//======================== 托盘图标资源：按连接态与在线状态取图标，以及正在连接时亮灰交替的两帧 ========================

BOOL CTrayController::LoadAppIcon(int nConnUiState)
{
    DestroyAppIcon();

    // 先据连接态开始或停止亮灰交替，并暂停或准备恢复未读提醒。各处在连接态变化后都会经这里
    // 重取图标，这两件事因此不必在每个调用点各写一遍。
    SyncConnectingBlink(nConnUiState);
    SyncUnreadHold(nConnUiState);
    if (m_connBlink.IsBlinking())
    {
        // 交替中：常态图标就是当前那一帧（复制一份，m_hAppIcon 与两帧图标各自释放），
        // 调用方随后推给系统的正是这一帧，与交替定时器的下一次切换衔接。正在连接不叠状态角标。
        HICON hFrame = m_hBlinkIcon[m_connBlink.CurrentFrame()];
        if (hFrame != NULL)
        {
            m_hAppIcon = ::CopyIcon(hFrame);
        }
        if (m_hAppIcon != NULL)
        {
            return TRUE;
        }
        // 两帧图标没有加载成功：退回下面的常规取法（灰色图标），托盘至少不会没有图标
        LOG_WARNING("TrayController::LoadAppIcon: blink frame icon unavailable, frame=%d, "
                    "falling back to the plain icon.", (int)m_connBlink.CurrentFrame());
    }

    // 图标文件名由 OnlineCapability 决定（真正在线才彩色，离线与连接未就绪都灰），
    // 与"离线时哪些功能可用"同一个判定口径；日后为离开 / 忙碌各配一只图标时只改那一处。
    tstring strFileName = Hootina::CPath::GetAppPath() + _T("Image\\");
    strFileName += balloonmain::GetTrayIconFileName(nConnUiState);

    const int nIconSize = ::GetSystemMetrics(SM_CXSMICON);
    m_hAppIcon = AtlLoadIconImage(strFileName.c_str(), LR_DEFAULTCOLOR | LR_LOADFROMFILE,
        nIconSize, ::GetSystemMetrics(SM_CYSMICON));
    if (m_hAppIcon == NULL)
    {
        //格式串用窄字符（日志文件编码统一），宽字符路径先转成 UTF-8 再按 %s 打印。
        //不能用 %ls：安装目录含中文时格式化会失败，整条日志被丢弃，偏偏是最需要它的时候
        LOG_ERROR("TrayController::LoadAppIcon: failed to load tray icon file: %s.",
                  EncodeUtil::UnicodeToUtf8(strFileName).c_str());
        return FALSE;
    }

    // 离开 / 忙碌在底图右下角叠一枚状态徽标 —— 这两个状态与在线共用同一张彩色底图，
    // 不叠加的话托盘上根本看不出自己正处于哪个状态。叠不叠、叠哪个由
    // GetTrayOverlayPresence 判定（在线不叠、离线与连接未就绪不叠）。
    const int nPresence = m_pHost->GetCurrentPresence();
    const int nOverlay = balloonmain::GetTrayOverlayPresence(nPresence, nConnUiState);
    LOG_INFO("TrayController::LoadAppIcon: connUiState=%d, presence=%d, overlay=%d "
             "(-1 = no badge).", nConnUiState, nPresence, nOverlay);
    if (nOverlay != balloonmain::kNoTrayOverlay)
    {
        HICON hOverlaid = balloonmain::MakePresenceOverlayIcon(m_hAppIcon, nOverlay, nIconSize);
        if (hOverlaid != NULL)
        {
            // 合成成功才换手：合成失败（GDI 资源不足等）就照常用底图，
            // 托盘少一枚徽标总好过没有图标。
            ::DestroyIcon(m_hAppIcon);
            m_hAppIcon = hOverlaid;
        }
        else
        {
            LOG_WARNING("TrayController::LoadAppIcon: failed to compose presence badge, "
                        "falling back to the plain icon. presence=%d.", nOverlay);
        }
    }
    return TRUE;
}

void CTrayController::DestroyAppIcon()
{
    if (m_hAppIcon != NULL)
    {
        ::DestroyIcon(m_hAppIcon);
        m_hAppIcon = NULL;
    }
}

void CTrayController::SyncConnectingBlink(int nConnUiState)
{
    const TrayBlinkAction action = m_connBlink.OnConnUiState(nConnUiState);
    if (action == kTrayBlinkStart)
    {
        LoadBlinkIcons();
        //::SetTimer 返回 UINT_PTR，而 m_dwBlinkTimerId 声明为 DWORD。定时器标识由本处以常量
        //kConnectingBlinkTimerId 传入、系统原样返回，取值远小于 32 位上限，收窄不会丢数据。
        m_dwBlinkTimerId = static_cast<DWORD>(
            ::SetTimer(m_hHost, kConnectingBlinkTimerId, kConnectingBlinkIntervalMs, NULL));
        LOG_INFO("TrayController::SyncConnectingBlink: connecting, tray icon blink started, "
                 "timerId=%u, interval=%u ms, unreadFlashTimerId=%u.",
                 (UINT)m_dwBlinkTimerId, kConnectingBlinkIntervalMs, (UINT)m_dwMsgTimerId);
    }
    else if (action == kTrayBlinkStop)
    {
        StopConnectingBlinkTimer();
        // 未读闪烁的定时器在交替期间一直在跑、只是每次都跳过，停止交替后自然恢复；
        // 复位首次 tick 日志标志，恢复后的第一次闪烁照常记一条
        m_bTrayFlashTickLogged = false;
        LOG_INFO("TrayController::SyncConnectingBlink: connUiState=%d, tray icon blink stopped, "
                 "resting frame=%d (0=bright 1=gray), unreadFlashTimerId=%u.",
                 nConnUiState, (int)m_connBlink.CurrentFrame(), (UINT)m_dwMsgTimerId);
    }
}

void CTrayController::SyncUnreadHold(int nConnUiState)
{
    const bool wasAllowed = m_unreadHold.AllowUnread();
    const TrayUnreadHoldAction action = m_unreadHold.OnConnUiState(nConnUiState);
    if (action == kTrayUnreadHoldStartResume)
    {
        //::SetTimer 返回 UINT_PTR，而 m_dwUnreadResumeTimerId 声明为 DWORD。定时器标识由本处以常量
        //kUnreadResumeTimerId 传入、系统原样返回，取值远小于 32 位上限，收窄不会丢数据。
        m_dwUnreadResumeTimerId = static_cast<DWORD>(
            ::SetTimer(m_hHost, kUnreadResumeTimerId, kTrayUnreadResumeDelayMs, NULL));
        LOG_INFO("TrayController::SyncUnreadHold: connection ready, unread reminders resume in %u ms, "
                 "timerId=%u.", kTrayUnreadResumeDelayMs, (UINT)m_dwUnreadResumeTimerId);
    }
    else if (action == kTrayUnreadHoldCancelResume)
    {
        StopUnreadResumeTimer();
        LOG_INFO("TrayController::SyncUnreadHold: connUiState=%d, pending unread resume cancelled, "
                 "allowUnread=%d.", nConnUiState, m_unreadHold.AllowUnread() ? 1 : 0);
    }

    // 刚进入暂停：已经弹出的未读浮窗收起来，悬停改为显示系统提示
    if (wasAllowed && !m_unreadHold.AllowUnread())
    {
        if (m_trayMsgTipPopup.IsWindow())
        {
            m_trayMsgTipPopup.HidePopup();
        }
        LOG_INFO("TrayController::SyncUnreadHold: reconnecting, unread reminders held, "
                 "unreadConversations=%d.", m_trayFlash.GetConversationCount());
    }
}

void CTrayController::StopUnreadResumeTimer()
{
    if (m_dwUnreadResumeTimerId != 0)
    {
        ::KillTimer(m_hHost, m_dwUnreadResumeTimerId);
        m_dwUnreadResumeTimerId = 0;
    }
}

void CTrayController::OnUnreadResumeTick()
{
    // 一次性定时器：到点即停
    StopUnreadResumeTimer();
    m_unreadHold.OnResumeTimer();
    // 恢复后的第一次未读闪烁照常记一条日志
    m_bTrayFlashTickLogged = false;
    // 有未读时系统提示让位给未读浮窗
    RefreshTipOnly();
    LOG_INFO("TrayController::OnUnreadResumeTick: unread reminders resumed, allowUnread=%d, "
             "unreadConversations=%d.", m_unreadHold.AllowUnread() ? 1 : 0,
             m_trayFlash.GetConversationCount());
}

void CTrayController::LoadBlinkIcons()
{
    DestroyBlinkIcons();

    // 两帧就是在线与离线两只托盘图标，文件名取 GetTrayIconFileName 的结果，与常态图标同源
    const int frameConnUi[kTrayBlinkFrameCount] = { kConnUiOnline, kConnUiOffline };
    const int nIconW = ::GetSystemMetrics(SM_CXSMICON);
    const int nIconH = ::GetSystemMetrics(SM_CYSMICON);
    for (int i = 0; i < kTrayBlinkFrameCount; ++i)
    {
        tstring strFileName = Hootina::CPath::GetAppPath() + _T("Image\\");
        strFileName += balloonmain::GetTrayIconFileName(frameConnUi[i]);
        m_hBlinkIcon[i] = AtlLoadIconImage(strFileName.c_str(), LR_DEFAULTCOLOR | LR_LOADFROMFILE,
            nIconW, nIconH);
        if (m_hBlinkIcon[i] == NULL)
        {
            LOG_ERROR("TrayController::LoadBlinkIcons: failed to load blink frame %d icon: %s.",
                      i, EncodeUtil::UnicodeToUtf8(strFileName).c_str());
        }
    }
}

void CTrayController::DestroyBlinkIcons()
{
    for (int i = 0; i < kTrayBlinkFrameCount; ++i)
    {
        if (m_hBlinkIcon[i] != NULL)
        {
            ::DestroyIcon(m_hBlinkIcon[i]);
            m_hBlinkIcon[i] = NULL;
        }
    }
}

void CTrayController::StopConnectingBlinkTimer()
{
    if (m_dwBlinkTimerId != 0)
    {
        ::KillTimer(m_hHost, m_dwBlinkTimerId);
        m_dwBlinkTimerId = 0;
    }
    DestroyBlinkIcons();
}

//======================== 悬停提示文字与图标刷新 ========================

CString CTrayController::BuildTrayTipText()
{
    // 第三行的状态文字与主面板胶囊三处同源。原先这里把状态写死成"在线"，用户切到
    // 离开 / 忙碌 / 离线后悬停托盘看到的仍是"在线"。
    //
    // 走 GetConnectionPresenceVisual 而<u>不是</u>只看 m_pHost->GetCurrentPresence()：后者是
    // "用户选了什么状态"，掉线时用户并没有改状态，于是它照旧给"在线"，而上一行取
    // 图标的 m_pHost->GetConnectionUiState() 已经按连接态转灰了 —— 同一次 ModifyIcon 里
    // 图标和文字各说各话，正是"图标灰了、提示还写着在线"的由来。
    LPCTSTR pszNickName = _T("");
    LPCTSTR pszAccount  = _T("");
    if (m_lpFMGClient != NULL)
    {
        pszNickName = m_lpFMGClient->m_UserMgr.m_UserInfo.m_strNickName.c_str();
        pszAccount  = m_lpFMGClient->m_UserMgr.m_UserInfo.m_strAccount.c_str();
    }

    CString strIconInfo;
    strIconInfo.Format(_T("%s\r\n%s\r\n%s"),
        pszNickName,
        pszAccount,
        balloonmain::GetConnectionPresenceVisual(m_pHost->GetCurrentPresence(),
                                                 m_pHost->GetConnectionUiState()).text);
    return strIconInfo;
}

CString CTrayController::TrayTipForCurrentState()
{
    // 有未读时悬停弹的是未读消息浮窗，系统提示留空，两者不会叠在一起出现。原先只有未读闪烁的
    // 每一拍传空串，「正在连接」期间闪烁暂停、各处刷新又把三行提示写了回去，悬停时就两个都出来了
    if (!balloonmain::ShouldShowTraySystemTip(m_trayFlash, m_unreadHold.AllowUnread()))
    {
        return CString();
    }
    return BuildTrayTipText();
}

void CTrayController::UpdateTrayIconTip()
{
    m_TrayIcon.ModifyIcon(m_hAppIcon, TrayTipForCurrentState());
}

void CTrayController::RefreshTrayIconForStatus()
{
    // 图标与提示都按 v2 在线状态（GetCurrentPresence）取，与托盘菜单标出的"当前项"、
    // 主面板状态胶囊三处同源。
    //
    // 注意这里<u>不再</u>读 v1 的 m_FMGClient.GetStatus()：v2 改状态并不更新那个字段，
    // 只有掉线时的 ResetToOfflineStatus 会把它置成 STATUS_OFFLINE，据它取图标会取错。
    //
    // 提示文字统一走 TrayTipForCurrentState（没有未读时为昵称 / 账号 / 状态三行，有未读时为空），
    // 与其它几处刷新托盘提示的地方保持同一种格式 —— 原先这里用的是"昵称（状态）"单行格式，
    // 两种格式会互相覆盖，用户看到的提示取决于最后是谁刷的。
    LoadAppIcon(m_pHost->GetConnectionUiState());
    m_TrayIcon.ModifyIcon(m_hAppIcon, TrayTipForCurrentState());
}

//======================== 新消息闪烁 ========================

void CTrayController::UpdateTrayFlashIcon()
{
    // 先销毁旧图标，避免每次重建泄漏 HICON。
    if (m_hMsgIcon != NULL)
    {
        ::DestroyIcon(m_hMsgIcon);
        m_hMsgIcon = NULL;
    }

    // 取"最近一条"未读会话；集合为空（无未读）时 m_hMsgIcon 保持空，OnTimer 不会闪。
    balloonmain::TrayFlashEntry target;
    if (!m_trayFlash.GetTarget(target))
    {
        LOG_INFO("Rebuild tray flash avatar: unread set is empty, no icon generated.");
        return;
    }

    // 按系统小图标尺寸生成 monogram 头像图标（首字 + 底色，与会话列表头像同口径）。
    int nIconSize = ::GetSystemMetrics(SM_CXSMICON);
    m_hMsgIcon = balloonmain::MakeMonogramIcon(target.m_avatarText.c_str(),
                                               target.m_avatarColor, nIconSize);

    // 图标生成失败时 m_hMsgIcon 为空，OnTimer 两个分支都变成"设空图标"，看起来
    // 就是完全不闪，所以这里把结果记下来。
    LOG_INFO("Rebuild tray flash avatar: ctype=%d, cid=%lld, size=%d, icon=%p.",
             target.m_ctype, (long long)target.m_cid, nIconSize, m_hMsgIcon);
}

void CTrayController::StopTrayFlash()
{
    // 杀掉闪烁定时器。
    if (m_dwMsgTimerId != 0)
    {
        ::KillTimer(m_hHost, m_dwMsgTimerId);
        m_dwMsgTimerId = 0;
    }

    // 销毁闪烁头像图标。
    if (m_hMsgIcon != NULL)
    {
        ::DestroyIcon(m_hMsgIcon);
        m_hMsgIcon = NULL;
    }

    // 恢复默认托盘图标（带"昵称 / 账号 / 当前状态"提示），与 OnDelMsgSender / OnCancelFlash 同口径。
    UpdateTrayIconTip();
}

//======================== 托盘图标上的鼠标操作 ========================

void CTrayController::OnTrayIcon_LButtunUp()
{
    // v2：托盘正在闪新消息头像 —— 单击打开"最近一条"未读会话。开窗 + 清未读交给
    // 主面板 OpenConversation 完成，它内部 MarkConversationRead 会回投 FMG_MSG_TRAY_CLEAR，
    // 由 OnV2TrayClear 统一停闪 / 切目标 / 恢复默认托盘图标，故本分支不再自行恢复图标、直接返回。
    if (m_trayFlash.IsFlashing())
    {
        balloonmain::TrayFlashEntry target;
        if (m_trayFlash.GetTarget(target) && m_pPanel != NULL && m_pPanel->IsWindow())
            m_pPanel->OpenConversation(target.m_ctype, target.m_cid);
        return;
    }
    else
    {
        // 托盘左键单击默认行为：唤出登录窗(登录前) / 主面板(登录后)，含最小化还原。
        m_pHost->ShowLoginOrMainPanel();
    }

    //先将任务栏图标恢复成正常状态
    UpdateTrayIconTip();
}

void CTrayController::OnTrayIcon_RButtunUp()
{

    // 右键弹菜单前先收起悬停未读提示浮窗（否则要等它自己的轮询定时器才消失）。
    m_trayMsgTipPopup.HidePopup();

    // 选托盘菜单形态：未登录 / 已登录。锁定功能已随旧主窗口移除，不再有"锁定"形态。
    balloonmain::TrayMenuKind kind = balloonmain::kTrayBeforeLogin;
    if (m_pHost->IsLoggedIn())
        kind = balloonmain::kTrayNormal;

    // 按行模型装配 DuiMenu。图标用 CreateMenuIconImage 即时渲染，存进局部 icons
    // 数组保活到 TrackPopup 返回（TrackPopup 同步阻塞，局部即可）—— DuiMenu 只
    // 借用图标指针、不持有所有权。
    //
    // 当前在线状态传给行模型，由它把对应那一项标成"当前"（带外环的圆点 +
    // "（当前）"后缀），否则四个状态项长得一模一样，用户看不出自己现在是哪个。
    //
    // 连接态也一并传进去，由行模型按 OnlineCapability 那套规则决定当前项显示什么文字、
    // 四个状态项能不能点。本函数<u>不再自己判断</u>：原先这里只把 presence 交给行模型，
    // 于是掉线时主面板胶囊已经显示"正在连接"、托盘菜单里却仍标着"在线（当前）"，
    // 而且四项照样可点 —— 点了只是把改状态的帧发进一条断掉的连接，界面先按"已改"
    // 渲染，与服务端实际状态不符。
    bool bMuted = IsMuted();
    int  nCurrentPresence = m_pHost->GetCurrentPresence();
    int  nConnUiState = m_pHost->GetConnectionUiState();
    // 「截图」一项登录前后都有，文字带上当前生效的快捷键（注册失败或停用时只写「截图」）；shotLabel 须活到菜单用完
    const CString shotLabel = balloonscreenshot::TrayScreenshotLabel(
        balloonscreenshot::LoadGlobalScreenshotPrefs().m_hotkey,
        balloonscreenshot::GetScreenshotHotkeyState() == balloonscreenshot::kShotHotkeyRegistered);
    std::vector<balloonmain::TrayMenuEntry> model =
        balloonmain::BuildTrayMenuModel(kind, bMuted, nCurrentPresence, nConnUiState,
                                        balloonscreenshare::CurrentRecordMenuState(),
                                        balloonscreenshare::CScreenShareHub::GetInstance().IsModuleReady(),
                                        balloonlock::CLockHost::GetInstance().IsModuleReady(),
                                        markdown::IsMarkdownAvailable(),
                                        //截图模块不可用时不传文字，菜单里就没有「截图」（docs/截图独立模块方案.md 3.3 节）
                                        balloonscreenshot::IsScreenshotAvailable() ? (LPCWSTR)shotLabel : NULL);

    balloonwjui::DuiMenu menu;
    std::vector<std::unique_ptr<CImageEx> > icons;
    icons.reserve(model.size());

    for (size_t i = 0; i < model.size(); ++i)
    {
        const balloonmain::TrayMenuEntry& e = model[i];
        if (e.isSeparator)
        {
            menu.AppendSeparator();
            continue;
        }

        CImageEx* icon = NULL;
        if (e.dotColor != 0)
        {
            // 在线状态项：画该状态的徽标(在线实心圆/离开时钟/忙碌横杠/离线空心环)，
            // 字形与颜色都取自 PresenceBadge 的共用映射，与主面板状态菜单观感一致；
            // 当前所处的那一项额外套一圈外环。
            icon = balloonmain::CreateStatusIconImage(
                e.presence, e.isCurrent, kTrayMenuIconSize);
        }
        else if (e.iconKind >= 0)
        {
            // 其余功能项：普通线描图标，默认单色。
            icon = balloonmain::CreateMenuIconImage(
                e.iconKind, kTrayMenuIconColor, kTrayMenuIconSize);
        }
        icons.push_back(std::unique_ptr<CImageEx>(icon));   // icon 可能为 NULL，AppendItem 容忍
        menu.AppendItem(e.id, e.label, icon);

        if (!e.enabled)
            menu.SetEnabled(e.id, false);
    }

    // 在光标处弹出。DuiMenu 的内部 popup 窗口自己抓前台 / 焦点来检测点击外部
    // 关闭，故无需老 CSkinMenu 那套 SetForegroundWindow + WM_NULL 的托盘菜单
    // hack；owner 传宿主窗口（菜单关掉后焦点回这里）。
    //
    // 托盘图标在任务栏，光标通常贴着屏幕底 / 右边缘；DuiMenu 不自动翻转，必须
    // 先把落点夹进显示器工作区，否则菜单会落到桌面外看不见（见 PopupMenuPlacement.h）。
    CPoint pt;
    GetCursorPos(&pt);
    POINT anchor = { pt.x, pt.y };
    SIZE  menuSize = menu.MeasureSize();
    RECT  area = balloonmain::GetMenuPlacementArea(anchor);          // 已排除任务栏并留缝
    POINT pos = balloonmain::ClampMenuOriginToRect(anchor, menuSize, area);

    // 关键路径日志：记录托盘菜单的光标、菜单尺寸、可放置区与最终落点，便于排查
    // "被任务栏遮挡 / 越界"类问题（例如任务栏自动隐藏会让工作区不排除任务栏）。
    // 同时记下当前在线状态与连接态 —— 排查"菜单标错当前项 / 四项该不该灰"时要用。
    LOG_INFO("TrayController::OnTrayIcon_RButtunUp: cursor=(%d,%d), menu=%dx%d, "
             "area=(%d,%d,%d,%d), popup=(%d,%d), bottom=%d, presence=%d, "
             "connUiState=%d (0=online 1=connecting 2=offline).",
        anchor.x, anchor.y, (int)menuSize.cx, (int)menuSize.cy,
        (int)area.left, (int)area.top, (int)area.right, (int)area.bottom,
        pos.x, pos.y, pos.y + (int)menuSize.cy, nCurrentPresence, nConnUiState);

    UINT chosen = menu.TrackPopup(pos.x, pos.y, m_hHost);   // 同步阻塞

    // 先按行模型过一遍选中项，两种情况就地返回，都不进下面的分发：
    //   · 该行被标为不可点（连接未就绪时的四个状态项）—— DuiMenu 本身已不会把灰显项
    //     交回来，这里是幂等的兜底，免得哪天菜单控件的行为变了就把一个明明拦住的
    //     操作放了出去；
    //   · 选中的就是当前所处的状态 —— 什么都不做，避免重复发包、无谓的断开或重连
    //     （与主面板状态菜单的处理一致，见 CBalloonMainPanel::ShowStatusMenu）。
    for (size_t i = 0; i < model.size(); ++i)
    {
        if (model[i].isSeparator || model[i].id != chosen)
        {
            continue;
        }

        if (!model[i].enabled)
        {
            LOG_INFO("TrayController::OnTrayIcon_RButtunUp: item %u is disabled in the current "
                     "connection state (connUiState=%d), ignored.",
                (unsigned int)chosen, nConnUiState);
            return;
        }

        if (model[i].isCurrent)
        {
            LOG_INFO("TrayController::OnTrayIcon_RButtunUp: item %u is already the current "
                     "presence (presence=%d), nothing to do.",
                (unsigned int)chosen, nCurrentPresence);
            return;
        }
    }

    // 按选中项分发 —— 直接复用现成的 OnMenu_* 命令处理函数（这些函数对菜单命令
    // 只用 nID，uNotifyCode / wndCtl 传 0 / NULL 即可）。
    switch (chosen)
    {
    // 四个在线状态统一走 v2 在线状态切换（OnChangeMyPresence）：与主面板状态菜单同一套
    // 行为——离线=真正断开(GoOffline)，在线/离开/忙碌=改状态(在线态发 cmd_change_presence、
    // 离线态走 GoOnline 带上目标状态)。wParam 取 v2 presence(MyPresenceValue 与 presence_type 同值)。
    case balloonmain::kTrayMenuOnline:
        m_pHost->ChangePresence(kMyPresenceOnline, kPresenceChangeManual);
        break;

    case balloonmain::kTrayMenuAway:
        m_pHost->ChangePresence(kMyPresenceAway, kPresenceChangeManual);
        break;

    case balloonmain::kTrayMenuBusy:
        m_pHost->ChangePresence(kMyPresenceBusy, kPresenceChangeManual);
        break;

    case balloonmain::kTrayMenuOffline:
        m_pHost->ChangePresence(kMyPresenceOffline, kPresenceChangeManual);
        break;

    case balloonmain::kTrayMenuMute:
        ToggleMute();
        break;

    case balloonmain::kTrayMenuOpenPanel:
        m_pHost->ShowLoginOrMainPanel();
        break;

    // 「发起屏幕共享」：交给主面板打开选会话窗口，选好后打开那个会话的聊天窗并弹出共享内容选择窗
    case balloonmain::kTrayMenuScreenShare:
        if (m_pPanel != NULL && m_pPanel->IsWindow())
            m_pPanel->OpenStartScreenSharePicker();
        break;

    // 「锁屏」：由 FlamingoLock.dll 在它自己的线程上弹出设置临时密码的窗口，本调用立即返回
    case balloonmain::kTrayMenuLockScreen:
        balloonlock::CLockHost::GetInstance().RequestLock();
        break;

    // 「截图」：登录前后都可用，交给主面板等菜单从屏幕上消失后再发起不绑定会话的截图（docs/截图独立模块方案.md 3.6 节）
    case balloonmain::kTrayMenuScreenshot:
        if (m_pPanel != NULL && m_pPanel->IsWindow())
            m_pPanel->RequestTrayScreenshot();
        break;

    // 「Markdown 编辑器」：打开一个空白编辑器，登录前后都可用（docs/Markdown消息方案.md 第 11.3 节）
    case balloonmain::kTrayMenuMarkdownEditor:
        balloonmd::CMarkdownEditorManager::GetInstance().OpenEditor(std::string(), NULL);
        break;

    // 「发起录制 / 停止录制」：由 FlamingoMedia.dll 开始独立录屏，录制中则停止并保存
    case balloonmain::kTrayMenuRecord:
        balloonscreenshare::ToggleRecording();
        break;

    case balloonmain::kTrayMenuLogout:
        // 登出不能在这里同步做：它要销毁主面板并进入登录窗的模态循环，而此刻还站在
        // 托盘消息处理 + TrackPopup 返回后的调用栈上。投递给自己，等消息循环下一轮
        // 在干净的栈上执行（见 OnLogout）。
        m_pHost->RequestLogout();
        break;

    case balloonmain::kTrayMenuExit:
        m_pHost->RequestExit();
        break;

    case 0:
    default:
        break;   // 未选中（失焦 / ESC）
    }
}

void CTrayController::OnTrayIcon_MouseHover()
{
    // 仅在托盘正闪新消息时弹出未读提示列表（无未读不弹）。重连期间与连上后的等待期里也不弹
    // （见 CTrayUnreadHold），此时悬停显示的是系统提示。
    if (!m_trayFlash.IsFlashing() || !m_unreadHold.AllowUnread())
        return;

    // 托盘图标定位：现代 Windows 上读其它进程托盘工具栏取图标矩形并不可靠（曾导致
    // 弹窗跑到屏幕左上角），改用悬停瞬间的光标位置作锚点 —— 光标此刻正压在托盘图标上，
    // 即等于"托盘图标处"。以光标为中心取一个小框：既作落点锚点（弹窗对准它正上方、
    // 紧挨任务栏），也作自动隐藏的命中判断区。
    POINT pt;
    ::GetCursorPos(&pt);
    const int kTrayAnchorHalf = 24;   // 锚点小框半边长（像素）
    CRect rcTrayIcon(pt.x - kTrayAnchorHalf, pt.y - kTrayAnchorHalf,
                     pt.x + kTrayAnchorHalf, pt.y + kTrayAnchorHalf);
    m_rcTrayIconRect = rcTrayIcon;

    // 注册行点击回调（幂等），按托盘闪烁集合在托盘上方弹出。浮窗自管自动隐藏。
    m_trayMsgTipPopup.SetPickCallback(&CTrayController::OnTrayPopupPickConv, this);
    m_trayMsgTipPopup.ShowForEntries(m_trayFlash.GetEntries(), m_rcTrayIconRect);
}

void CTrayController::OnTrayIcon_MouseLeave()
{
    // 新版未读提示浮窗自带"光标离开弹窗 ∪ 托盘则隐藏"的轮询，这里无需处理 —— 若在此
    // 主动隐藏，反而会在光标从托盘移到弹窗上的瞬间把它收起，导致点不到。故留空。
}

// 托盘悬停未读提示行点击回调跳板：转交本对话框打开会话。
void CTrayController::OnTrayPopupPickConv(void* userdata, int ctype, long long cid)
{
    CTrayController* pSelf = (CTrayController*)userdata;
    if (pSelf != NULL)
        pSelf->OpenConvFromTrayPopup(ctype, cid);
}

void CTrayController::OpenConvFromTrayPopup(int ctype, long long cid)
{
    // 转交主面板打开会话；OpenConversation 内部清未读并回投 FMG_MSG_TRAY_CLEAR，
    // 由 OnV2TrayClear 把该会话移出托盘闪烁集合、必要时停闪。
    if (m_pPanel != NULL && m_pPanel->IsWindow())
        m_pPanel->OpenConversation(ctype, (int64_t)cid);
}

//======================== 由宿主转发进来的消息 ========================

LRESULT CTrayController::OnTrayIconNotify(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    // wParam为uID, lParam为鼠标消息
    m_TrayIcon.OnTrayIconNotify(wParam, lParam);


    UINT uID = (UINT)wParam;
    UINT uIconMsg = (UINT)lParam;

    // 被踢下线提示框正在显示时，托盘的点击一律不受理：那个框是模态的，它跑的嵌套消息
    // 循环照常分发托盘消息，若放行，用户从托盘右键菜单里点"登出"会重入回登录界面的收尾
    // 序列（销毁主面板 + 再进一层登录窗模态循环），栈就叠乱了。
    if (m_pHost->IsKickoutNoticeShowing())
    {
        return 0;
    }

    switch (uIconMsg)
    {
    case WM_LBUTTONUP:
        OnTrayIcon_LButtunUp();
        break;
    case WM_LBUTTONDBLCLK:
        // 双击托盘图标：直接唤出登录窗(登录前) / 主面板(登录后)，含最小化还原。
        m_pHost->ShowLoginOrMainPanel();
        break;
    case WM_RBUTTONUP:
        OnTrayIcon_RButtunUp();
        break;
    case WM_MOUSEHOVER:
        OnTrayIcon_MouseHover();
        break;
    case WM_MOUSELEAVE:
        OnTrayIcon_MouseLeave();
        break;
    }
    return 0;
}

// v2 有应在托盘闪烁的新消息：把发送方会话记入 m_trayFlash，重建当前最近一条的
// 头像图标，并（必要时）启动闪烁定时器。lParam 为主面板堆分配的 TrayFlashData。
LRESULT CTrayController::OnV2TrayFlash(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    TrayFlashData* pData = (TrayFlashData*)lParam;
    if (pData == NULL)
        return 0;

    const int nCtype = pData->ctype;
    const long long nCid = (long long)pData->cid;

    m_trayFlash.PushUnread(pData->ctype, pData->cid, pData->displayName,
                           pData->preview, pData->avatarText, pData->avatarColor);
    delete pData;

    // 重建"最近一条"会话的 monogram 头像图标，供 OnTimer 闪烁切换使用。
    UpdateTrayFlashIcon();

    // 有了未读就立刻清空系统提示，不等第一次闪烁计时：那之前悬停会同时出现系统提示与未读浮窗
    RefreshTipOnly();

    // 闪烁定时器未起则起一个（复用收消息闪烁定时器 id）。已在闪则沿用，避免重复 SetTimer。
    const bool bTimerAlreadyOn = (0 != m_dwMsgTimerId);
    if (!bTimerAlreadyOn)
    {
        // 闪烁快慢跟随系统的文本光标闪烁速度，但那个值必须过一遍 ClampTrayFlashInterval：
        // 用户把系统的"文本光标闪烁"关掉时 ::GetCaretBlinkTime() 返回 INFINITE，原样
        // 交给 SetTimer 会被夹成约 24.8 天，托盘就一次都不闪、且毫无提示。
        const UINT uBlink = ::GetCaretBlinkTime();
        const UINT uInterval = balloonmain::ClampTrayFlashInterval(uBlink);
        //SetTimer 返回 UINT_PTR，而 m_dwMsgTimerId 声明为 DWORD。定时器标识由本处以常量
        //RECVCHATMSG_TIMER_ID 传入、系统原样返回，取值远小于 32 位上限，收窄不会丢数据。
        m_dwMsgTimerId = static_cast<DWORD>(::SetTimer(m_hHost, kFlashTimerId, uInterval, NULL));
        LOG_INFO("Tray flash timer started: timerId=%u, caretBlink=%u ms, interval=%u ms.",
                 (UINT)m_dwMsgTimerId, uBlink, uInterval);
    }

    // 首次 tick 打一条日志用（每次重新起闪都复位），确认定时器真的在跑。
    m_bTrayFlashTickLogged = false;

    LOG_INFO("Tray flash requested: ctype=%d, cid=%lld, conversations=%d, avatarIcon=%p, "
             "timerAlreadyOn=%d, connectingBlink=%d.",
             nCtype, nCid, m_trayFlash.GetConversationCount(), m_hMsgIcon,
             bTimerAlreadyOn ? 1 : 0, m_connBlink.IsBlinking() ? 1 : 0);

    return 0;
}

// v2 某会话已读（被打开 / 在列表里点开）：从 m_trayFlash 移除该会话。若仍有别的未读，
// 目标可能切到次新，重建头像图标继续闪；若再无未读，则停闪并恢复默认托盘图标。
LRESULT CTrayController::OnV2TrayClear(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam)
{
    int        nCtype = (int)wParam;
    long long* pCid   = (long long*)lParam;
    if (pCid == NULL)
        return 0;

    long long nCid = *pCid;
    delete pCid;

    bool bStillFlashing = m_trayFlash.ClearConversation(nCtype, (int64_t)nCid);

    // 停闪是"该闪没闪"最常见的来源之一（某处误判会话已读就会把刚起的闪掐掉），
    // 故清除动作也记一笔，日志里能看清是谁把它清的、清完还剩几个。
    LOG_INFO("Tray flash cleared: ctype=%d, cid=%lld, stillFlashing=%d.",
             nCtype, (long long)nCid, bStillFlashing ? 1 : 0);

    if (bStillFlashing)
        UpdateTrayFlashIcon();      // 目标可能已切换，重建图标继续闪
    else
        StopTrayFlash();            // 再无未读：停闪并恢复默认图标

    return 0;
}

// 主面板最小化隐藏到托盘：弹一次气泡告诉用户主面板去哪儿了、怎么唤回。
// "该账号是否已经提示过"由主面板据 CUserConfig 判定并落盘，本处收到即弹。
// 用 ShowBalloonTip 而非 ModifyIcon：只弹气泡不动图标，托盘正在闪新消息头像时
// 也不会打断闪烁。
LRESULT CTrayController::OnTrayMinimizeTip(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/)
{
    // 锁屏期间不弹（锁屏前一刻最小化主面板时，这条消息可能在锁屏之后才处理）
    if (balloonlock::DecideDuringLock(balloonlock::kLockEvtTrayBalloon, balloonlock::IsScreenLocked())
        != balloonlock::kLockActNormal)
    {
        LOG_INFO("Tray balloon tip skipped: screen is locked.");
        return 0;
    }
    // 提示里的产品名称取 flamingo.ini 的 [ui] apptitle
    CString strTip;
    strTip.Format(TR(L"%s 已最小化到托盘，单击托盘图标可重新打开主面板。"), g_strAppTitle.c_str());
    m_TrayIcon.ShowBalloonTip(TR(L"温馨提示"), strTip, kTrayMinimizeTipTimeoutMs);

    LOG_INFO("Tray balloon tip shown: main panel minimized to tray.");
    return 0;
}

} // namespace balloonmain
