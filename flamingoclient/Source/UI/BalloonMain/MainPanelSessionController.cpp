/**
 *  会话控制器的实现。整体说明见 MainPanelSessionController.h。
 *
 *  本类不是窗口，凡是需要窗口句柄的地方（提示框属主、定时器、消息投递）一律用宿主句柄
 *  m_hHost，也就是主面板；要退出进程时请求 ISessionAppHost，不自己销毁任何窗口。
 *
 *  balloonwj@qq.com   2026-08-19
 */

#include "stdafx.h"
#include "MainPanelSessionController.h"

#include "BalloonMainPanel.h"
#include "MainPanelTrayController.h"
#include "OnlineCapability.h"                            // 连接态推导 / 自动重连守卫
#include "KickoutNotice.h"                               // 被踢下线的提示文案
#include "BalloonKickoutDlg.h"                           // 贴托盘弹出的被踢下线模态提示框
#include "../BalloonChatWnd/ChatLinkDetect.h"            // 登录后灌入链接识别开关
#include "../BalloonUpdate/BalloonUpdateWindow.h"        // 静默检查查出新版本时拉起升级窗
#include "../../App/FlamingoClient.h"
#include "../../App/UIText.h"                            // g_strAppTitle
#include "../../Feature/Update/UpdateManager.h"
#include "../../net/IUSocket.h"
#include "../../net/Protocol.h"
#include "../../net/ProfileProtocolClient.h"             // net::UpdateProfileResponse
#include "../../net/AccountProtocolClient.h"             // net::ChangePresenceResponse 等
#include "../../net/CurrentUserAccount.h"                // 登录成功后校验账号取值口是否接上
#include "../../net/UserCachePath.h"                     // net::EnsureUserEssentialDirs：登录后建账号目录
#include "../../Common/Log/IULog.h"
#include "../../Common/Util/IniFile.h"
#include "../../Common/Util/EncodeUtil.h"
#include "../../resource.h"
#include "../../Feature/FolderTransfer/FolderTransferManager.h"  // 登出 / 被踢下线时停掉全部文件夹传输

//进程的隐藏属主窗口，建于 Startup.cpp。登录窗以它作属主，这样登录窗最小化时
//不会在任务栏留下一个空白按钮。
extern HWND g_hwndOwner;

namespace balloonmain {

CSessionController::CSessionController(CFlamingoClient* pFMGClient)
    : m_hHost(NULL)
    , m_pPanel(NULL)
    , m_lpFMGClient(pFMGClient)
    , m_pAppHost(NULL)
    , m_LoginDlg(pFMGClient)
    , m_nSessionStatus(kSessionNotLogin)
    , m_bAlreadyLogin(FALSE)
    , m_bResumingOnline(false)
    , m_dwReconnectTimerId(0)
    , m_bEnableReconnect(true)
    , m_uReconnectInterval(3000)
    , m_bKickoutNoticeShowing(false)
{
    // BEGIN_MSG_MAP_EX 声明的这个标志在首次派发时会被先读后写，非窗口类必须自己置初值。
    m_bMsgHandled = FALSE;

    memset(&m_stAccountInfo, 0, sizeof(m_stAccountInfo));
}

CSessionController::~CSessionController()
{
}

void CSessionController::Attach(HWND hHost, CBalloonMainPanel* pPanel,
                                CFlamingoClient* pClient, ISessionAppHost* pAppHost)
{
    m_hHost = hHost;
    m_pPanel = pPanel;
    m_lpFMGClient = pClient;
    m_pAppHost = pAppHost;

    // 历史登录账号列表：登录窗要据它填账号下拉框、判断要不要自动登录，故在此加载。
    CString strFileName(g_szHomePath);
    strFileName += _T("Users\\LoginAccountList.dat");
    m_LoginAccountList.LoadFile(strFileName.operator LPCTSTR());
}

bool CSessionController::StartInitialLogin()
{
    return StartLogin(m_LoginAccountList.IsAutoLogin());
}

bool CSessionController::IsAutoLoginEnabledForCurrentAccount()
{
    //m_stAccountInfo.szUser 是本次登录所用的账号，登录成功时已写入历史列表；
    //尚未登录时为空串，Find 查不到，自然返回 false
    if (m_stAccountInfo.szUser[0] == _T('\0'))
    {
        return false;
    }
    LOGIN_ACCOUNT_INFO* lpAccount = m_LoginAccountList.Find(m_stAccountInfo.szUser);
    return (lpAccount != NULL && lpAccount->bAutoLogin);
}

void CSessionController::MarkLoggedInForDebugPreview()
{
    m_bAlreadyLogin = TRUE;
}

bool CSessionController::PumpTimer(UINT_PTR nIDEvent)
{
    if (m_dwReconnectTimerId == 0 || nIDEvent != (UINT_PTR)m_dwReconnectTimerId)
    {
        return false;
    }

    // 用户主动离线后必须停手。这里原先只看定时器在不在、不看登录态，于是「掉线重连
    // 过程中用户选了离线」这条路上，定时器会一直把用户连回服务器 —— 与 OnNetError 里的
    // 判断共用同一个守卫函数，避免再出现一处判了另一处没判。
    const bool userWentOffline = (m_nSessionStatus == kSessionUserGoOffline);
    if (!ShouldAutoReconnect(userWentOffline, m_bEnableReconnect))
    {
        LOG_INFO("Reconnect timer fired but reconnect is suppressed: "
            "userWentOffline=%d, enableReconnect=%d.",
            userWentOffline ? 1 : 0, m_bEnableReconnect ? 1 : 0);
        ::KillTimer(m_hHost, m_dwReconnectTimerId);
        m_dwReconnectTimerId = 0;
        return true;
    }

    //一轮重连里同一时刻只有一个登录请求在途：上一个还没有结果就不再发，也不再弹托盘气泡。
    //原先这里的标志从没被置真，每次触发都发一个请求，单次尝试比重连间隔长时请求会在发送
    //队列里越积越多（bugs.md 的 BUG-21）。
    const balloonmain::ReconnectTickAction action = m_reconnectThrottle.OnTick();
    if (action == balloonmain::kReconnectTickWait)
    {
        return true;
    }
    if (action == balloonmain::kReconnectTickResendStale)
    {
        LOG_WARNING("Reconnect: no login result after %d timer ticks, assuming the request "
            "was lost and sending a new one.", balloonmain::kReconnectResultWaitTicks);
    }

    //只有请求确实入队才进入等结果状态并弹气泡；被前置条件挡下时没有结果会回来，下一次照常再试
    if (m_lpFMGClient->Login())
    {
        m_reconnectThrottle.OnRequestQueued();
        m_pPanel->TrayController().ShowReconnectingBalloon();
    }
    return true;
}

// 初始化Top工具栏
bool CSessionController::StartLogin(BOOL bAutoLogin/* = FALSE*/)
{
    // 网络应答与推送的收件窗口是主面板：处理器要么在它自己的消息映射里，要么在挂于它
    // 派发链上的协作类里。主面板是常驻窗口，句柄在整个进程生命周期内不变，所以这里取到
    // 的一定是有效句柄，登出再登录也不必重设。
    m_lpFMGClient->SetCallBackWnd(m_pPanel->m_hWnd);

    //自动登录与手动登录走同一条路：都弹出登录窗（2026-09-30 起，见
    //docs/自动登录方案.md）。原先自动登录不弹窗、直接在后台登录，用户在登录过程中
    //没有任何办法取消（没有「取消登录」按钮，点托盘图标也找不到可显示的窗口）。
    //现在自动登录只是让登录窗显示出来之后自己点一次「登录」：界面进入「登录中」、
    //主按钮为「取消登录」；失败原因也与手动登录一样就地显示在登录窗里。
    //
    //此处原先在自动登录前同步跑一遍 v1 时代的自动升级（Updater + CUpdateDlg），那条
    //链路在 v2 改造后已经跑不通、早已删除；登录前的更新检查由登录窗自己做。

    // 尚未登录：GetConnectionUiState 为离线，托盘先挂灰色图标。
    m_pPanel->TrayController().AddIcon(_T("Flamingo未登录"));

    m_LoginDlg.m_lpFMGClient = m_lpFMGClient;
    m_LoginDlg.m_pLoginAccountList = &m_LoginAccountList;
    //默认账号为空时（进程刚启动），登录窗会预填上次登录的账号及其保存的密码，
    //自动登录要登的正是这个账号（IsAutoLogin 看的也是它）。
    m_LoginDlg.SetDefaultAccount(m_lpFMGClient->m_UserMgr.m_UserInfo.m_strAccount.c_str());
    m_LoginDlg.SetDefaultPassword(m_lpFMGClient->m_UserMgr.m_UserInfo.m_strPassword.c_str());
    m_LoginDlg.SetAutoStartLogin(bAutoLogin != FALSE);
    LOG_INFO("SessionController::StartLogin: showing the login dialog, autoStart=%d.",
             (int)(bAutoLogin != FALSE));

    //置"登录中"必须在 DoModal 之前。登录窗是模态的，且登录期间不关窗，登录结果是
    //在它的模态消息循环内部派发到 OnLoginResult 的，那里已经把状态推进到
    //kSessionLogin；若等 DoModal 返回之后再置一次"登录中"，就把登录成功
    //的状态盖了回去，此后 m_nSessionStatus 一直停在"登录中"。凡是以
    //"m_bAlreadyLogin && m_nSessionStatus == kSessionLogin" 为条件的分支
    //都会走错，2026-08-14 实测到两处后果：切换到离开 / 忙碌时不发 cmd_change_presence
    //而是误走"从离线恢复在线"分支、托盘图标也不刷新（徽标出不来）；
    //"无操作自动离开"因判定为连接未就绪而永不触发。
    //没有任何代码判断 kSessionLogining 本身，故提前置位不影响其它逻辑。
    m_nSessionStatus = kSessionLogining;

    if (m_LoginDlg.DoModal(g_hwndOwner) != IDOK)	// 显示登录对话框
    {
        m_pAppHost->RequestAppExit();
        return false;
    }
    m_LoginDlg.GetLoginAccountInfo(&m_stAccountInfo);

    // 不再显示旧的 "正在登录" 进度面板：登录进度由登录窗自己呈现。托盘的登录动画
    // 原先只服务于"不弹登录窗"的自动登录，现在两条路都有登录窗显示进度，不再需要。

    m_lpFMGClient->SetUser(m_stAccountInfo.szUser, m_stAccountInfo.szPwd);

    //已经改成同步登录了
    //m_lpFMGClient->Login();

    return true;
}

void CSessionController::ShowLoginOrMainPanel()
{
    // 选目标：登录成功且主面板存在 -> 主面板；否则（登录前 / 登录中）-> 登录窗。
    HWND hTarget = NULL;
    if (m_bAlreadyLogin && m_pPanel->IsWindow())
        hTarget = m_pPanel->m_hWnd;
    else if (m_LoginDlg.IsWindow())
        hTarget = m_LoginDlg.m_hWnd;

    LOG_INFO("ShowLoginOrMainPanel: alreadyLogin=%d, loginDlgIsWnd=%d, panelIsWnd=%d, "
        "targetIconic=%d, target=%p.",
        (int)m_bAlreadyLogin, (int)m_LoginDlg.IsWindow(), (int)m_pPanel->IsWindow(),
        (hTarget != NULL ? (int)::IsIconic(hTarget) : -1), (void*)hTarget);

    if (hTarget == NULL)
        return;

    // 关键：窗口被最小化(iconic)时只有 SW_RESTORE 能还原，SW_SHOW 对最小化窗口无效
    // （会"看上去没反应"）。主面板标题栏最小化按钮走 WM_SYSCOMMAND/SC_MINIMIZE
    // （见 balloonui DuiFrameWindow），故此处必须按是否最小化分别处理。
    if (::IsIconic(hTarget))
        ::ShowWindow(hTarget, SW_RESTORE);
    else
        ::ShowWindow(hTarget, SW_SHOW);

    ::SetForegroundWindow(hTarget);
    ::SetFocus(hTarget);
}

void CSessionController::DoExit()
{
    if (m_LoginDlg.IsWindow())
        m_LoginDlg.PostMessage(WM_CLOSE);
    else if (::IsWindow(m_hHost))
    {
        //PostMessage(WM_CLOSE);
        m_pAppHost->RequestAppExit();
    }
}

LRESULT CSessionController::OnNetError(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    LOG_INFO("SessionController::OnNetError: sessionStatus=%d, reconnectTimer=%u, "
        "enableReconnect=%d.",
        m_nSessionStatus, (unsigned int)m_dwReconnectTimerId,
        m_bEnableReconnect ? 1 : 0);

    //重连定时器已经开启，直接退出，避免重复开启
    if (m_dwReconnectTimerId != 0)
        return 0;

    m_lpFMGClient->m_UserMgr.ResetToOfflineStatus();

    ::SendMessage(m_lpFMGClient->m_UserMgr.m_hCallBackWnd, FMG_MSG_UPDATE_RECENT_LIST, 0, 0);
    ::SendMessage(m_lpFMGClient->m_UserMgr.m_hCallBackWnd, FMG_MSG_UPDATE_GROUP_LIST, 0, 0);

    //没有开启重连或者用户主动下线都不要重连（守卫与 OnTimer 的重连分支共用同一函数）。
    const bool userWentOffline = (m_nSessionStatus == kSessionUserGoOffline);
    if (balloonmain::ShouldAutoReconnect(userWentOffline, m_bEnableReconnect))
    {
        m_nSessionStatus = kSessionReconnecting;
        //新一轮重连：丢弃上一轮遗留的等待状态。例如上次重连的请求发出后用户切到了离线，
        //结果回来时不在重连状态，等待状态可能没被清掉，不重置的话本轮一个请求都发不出去
        m_reconnectThrottle.Reset();
        //SetTimer 返回 UINT_PTR，而 m_dwReconnectTimerId 声明为 DWORD。定时器标识由本处
        //以常量 RECONNECT_TIMER_ID 传入、系统原样返回，取值远小于 32 位上限，收窄不会丢数据。
        m_dwReconnectTimerId = static_cast<DWORD>(::SetTimer(m_hHost, kReconnectTimerId, m_uReconnectInterval, NULL));
    }

    //连接已断：托盘图标转灰，界面进入"正在连接"（重连中）或"离线"（用户主动下线）态，
    //聊天窗输入区据此置灰、弹出提示条。
    m_pPanel->TrayController().RefreshTrayIconForStatus();
    BroadcastConnectionState();

    return 1;
}

// v2 被踢下线推送（cmd_kickout）：账号在其他同类型设备登录（顶号）、被管理员停用、
// 服务器维护、密码已改、被本人在另一设备踢掉，都会走到这里。
//
// 流程：先断连接、压住自动重连，再贴着托盘图标弹一个<u>模态</u>提示框说明原因；用户关掉
// 提示框后走与登出相同的收尾（关所有窗口、销毁主面板、清登录态），重新弹出登录窗。
//
// 为什么是模态提示框而不是托盘气泡：气泡容易被忽略（用户反馈"效果不明显"），而被顶下线
// 是必须被看见的事 —— 尤其"异地登录"那一条还带着"密码可能已泄露"的安全提醒。
//
// 压住自动重连是这里的另一件要紧事：网络层收到推送后会关掉 socket，而关 socket 会被
// OnNetError 当成掉线、起重连定时器把客户端连回服务器重新登录，登录又会把刚在另一台机器
// 上登上去的那一端顶下来，两端来回顶号没完。
LRESULT CSessionController::OnV2Kickout(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/)
{
    const int nReason = (int)wParam;

    LOG_INFO("SessionController::OnV2Kickout: kickout notify received. reason=%d, sessionStatus=%d, "
        "reconnectTimer=%u, alreadyLogin=%d.",
        nReason, m_nSessionStatus, (unsigned int)m_dwReconnectTimerId,
        m_bAlreadyLogin ? 1 : 0);

    //【0】重入保护：提示框跑的是嵌套消息循环，期间其它消息照常分发。万一又来一条被踢
    //     下线通知（理论上不会：连接已经关了），不能再叠一个框、更不能重入下面的收尾序列。
    if (m_bKickoutNoticeShowing)
    {
        LOG_INFO("SessionController::OnV2Kickout: notice dialog is up, ignore duplicated kickout notify.");
        return 0;
    }

    //【1】先置"离线且不再重连"态。OnNetError 与重连定时器都据这个标志决定要不要连回去
    //     （见 balloonmain::ShouldAutoReconnect）。
    m_nSessionStatus = kSessionUserGoOffline;

    //【2】已经在跑的重连定时器就地停掉。掉线事件（FMG_MSG_NET_ERROR）与本消息都由网络
    //     线程投递，本消息虽然投得更早，但这里不指望它一定先被处理：万一 OnNetError 抢先
    //     把定时器起了，这一刀补上，不留"标志已置离线、定时器还在跑"的中间态。
    if (m_dwReconnectTimerId != 0)
    {
        ::KillTimer(m_hHost, m_dwReconnectTimerId);
        m_dwReconnectTimerId = 0;
        LOG_INFO("SessionController::OnV2Kickout: kicked out, running reconnect timer stopped.");
    }

    //【3】真正下线：立起网络层离线闸门、丢弃排队中的帧、断开 chat / file / img 三条连接。
    //     闸门在网络线程收到推送时已经立过一次（见 CIUSocket::HandleV2Frame 的 cmd_kickout
    //     分支），这里是幂等的补齐，同时把另外两条连接与排队数据一并收拾干净。
    m_lpFMGClient->GoOffline();

    //【4】提示框弹出期间主面板还在它背后，界面不能还写着"在线"：胶囊改离线、托盘图标转灰、
    //     聊天窗输入区置灰。这几步很快就会随主面板一起销毁，但那是用户关掉提示框<u>之后</u>
    //     的事，中间这段时间界面得是自洽的。
    if (m_pPanel->IsWindow())
    {
        m_pPanel->ApplyPresenceOptimistic(kMyPresenceOffline);
    }
    m_pPanel->TrayController().RefreshTrayIconForStatus();
    BroadcastConnectionState();

    //【5】取托盘图标位置，供提示框贴着它弹出。必须赶在下面收尾序列里的 RemoveIcon 之前
    //     取 —— 图标摘掉之后就再也问不出它在哪儿了。取不到时给空矩形，提示框自会退回
    //     桌面右下角（见 balloonmain::ComputeTrayAnchoredOrigin）。
    RECT rcTrayIcon;
    ::SetRectEmpty(&rcTrayIcon);
    if (!m_pPanel->TrayController().GetTrayIconRect(&rcTrayIcon))
    {
        ::SetRectEmpty(&rcTrayIcon);
    }

    //【6】弹模态提示框。owner 取主面板；主面板不在（已销毁 / 未创建）时退回宿主句柄。
    //     模态期间 owner 被禁用，托盘的点击也一并挡住（见 OnTrayIconNotify 里的守卫），
    //     免得用户从托盘菜单里点"登出"造成收尾序列重入。
    HWND hOwner = m_hHost;
    if (m_pPanel->IsWindow())
    {
        hOwner = m_pPanel->m_hWnd;
    }

    m_bKickoutNoticeShowing = true;
    {
        CBalloonKickoutDlg dlg;
        dlg.DoModal(hOwner, nReason, rcTrayIcon);
    }
    m_bKickoutNoticeShowing = false;

    LOG_INFO("SessionController::OnV2Kickout: notice dialog closed by user, back to the login window.");

    //【7】与登出相同的收尾：关掉所有窗口、销毁主面板、清空登录态，重新弹出登录窗。
    //     把被踢原因一并传下去 —— 只有"密码已修改"那一种才顺手清掉自动登录标志，
    //     其余原因(异地登录 / 账号停用 / 服务器维护 / 被本人从别的设备踢掉)都是被动
    //     发生的，用户没表达"以后别自动登录"的意思，不替他做主。
    ReturnToLoginWindow(balloonmain::kLoginExitKickedOut, nReason);
    return 0;
}

// 本机网络由断开恢复（CCheckNetworkStatusTask 每 3 秒探测一次本机联网状况，状态发生
// 跳变时经 CFlamingoClient::OnNetworkStatusChange 转投到这里）。
//
// 这是自动重连守卫的第三个调用点，另外两个是掉线事件(OnNetError)与重连定时器(OnTimer)。
// 原先这条路径压根不判登录态，网络一恢复就直接 GoOnline() 重新登录 —— 于是"用户选了
// 离线"和"账号在别处登录被顶下线"之后，只要网线拔一下再插上，客户端就自己连回服务器，
// 把前面那个"别再连了"的决定作废（被踢那一路还会把刚在别处登上去的那一端又顶下来）。
// 三处共用 balloonmain::ShouldAutoReconnect 之后，不会再出现一处判了另一处没判。
LRESULT CSessionController::OnNetworkResumed(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/)
{
    // 主动离线与被踢下线都会把面板状态置成 kSessionUserGoOffline，两者对
    // "要不要连回去"的答案一样，故用同一个判据。
    const bool userWentOffline = (m_nSessionStatus == kSessionUserGoOffline);

    LOG_INFO("SessionController::OnNetworkResumed: local network is back. sessionStatus=%d, "
        "userWentOffline=%d, enableReconnect=%d, alreadyLogin=%d.",
        m_nSessionStatus, userWentOffline ? 1 : 0,
        m_bEnableReconnect ? 1 : 0, m_bAlreadyLogin ? 1 : 0);

    if (!balloonmain::ShouldAutoReconnect(userWentOffline, m_bEnableReconnect))
    {
        LOG_INFO("SessionController::OnNetworkResumed: offline (or auto reconnect disabled), "
            "不自动连回服务器，等用户自己选\"在线\"。");
        return 0;
    }

    // 未登录时也不要自己连回去。这条判断只在本调用点需要：另外两个调用点（掉线事件、
    // 重连定时器）只可能在登录后触发，而"网络恢复"在停在登录窗时照样会来。
    // 不判的后果是实打实的：被顶下线回到登录界面后，账号与密码仍留在 m_UserInfo 里供
    // 登录窗预填（见 ReturnToLoginWindow），CFlamingoClient::Login 正是读这两个字段，
    // 于是网络一恢复就会拿旧账号悄悄登回去，把别处刚登上的那一端又顶下来。
    if (!m_bAlreadyLogin)
    {
        LOG_INFO("SessionController::OnNetworkResumed: not logged in (still at the login window), skip auto login.");
        return 0;
    }

    LOG_INFO("SessionController::OnNetworkResumed: not offline, log in again after the network is back.");
    m_lpFMGClient->GoOnline();
    return 0;
}

// 登录返回消息
LRESULT CSessionController::OnLoginResult(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    long nCode = (long)lParam;

    LOG_INFO("SessionController::OnLoginResult: code=%ld (0=SUCCESS 1=FAILED 2=UNREG "
        "3=PWD_ERR), sessionStatus=%d (0=NOTLOGIN 1=LOGINING 2=LOGIN "
        "3=RECONNECTING 4=USERGOOFFLINE).",
        nCode, m_nSessionStatus);

    //任何登录结果都表示在途的那个请求已经结束，清除重连节流的等待状态，下一次触发可以再发
    m_reconnectThrottle.OnResult();

    //重连不成功：等重连定时器下一次触发时再发登录请求
    if (m_nSessionStatus == kSessionReconnecting && nCode != LOGIN_SUCCESS)
    {
        return 0;
    }

    //本次结果是不是登录窗那条路发起的。判据是「登录窗还开着吗」而不是「它正在
    //登录中吗」：用户点过「取消登录」之后阶段已回到空闲，可后台线程仍会在几秒后
    //把结果投回来，那条迟到的结果照样属于登录窗，该由它按轮次号丢弃。若在这里
    //按「不归登录窗管」放行，它就会落进下面的「弹 MessageBox + StartLogin」，
    //而登录窗此刻还活着，StartLogin 再 Create 一次即撞 ATL 的 m_hWnd == 0 断言。
    //
    //「从离线恢复在线」那条路不经登录窗（主面板还开着、根本没有登录窗），
    //IsLoginWindowActive 为假，自然落到下面原有的处理里，行为不变。
    //自动登录自 2026-09-30 起也弹出登录窗，其结果同样由登录窗就地处理。
    if (m_LoginDlg.IsLoginWindowActive())
    {
        //本消息的 lParam 是结果码（函数开头已取走），wParam 是登录轮次号 ——
        //都是 CFlamingoClient::OnLoginResult 从 CLoginResult 里拆出来转发的，
        //那个对象在转发之后立即被释放，所以这里<u>拿不到也不该去碰</u>它的指针。
        const uint32_t uGeneration = (uint32_t)wParam;

        if (!m_LoginDlg.OnLoginResultArrived(nCode, uGeneration))
        {
            //过期结果：登录窗已经丢弃它，这里也不能再动主面板状态与托盘图标
            LOG_INFO("SessionController::OnLoginResult: stale result discarded by the login window.");
            return 0;
        }

        //登录失败已由登录窗就地显示，不再弹 MessageBox、也不再重建登录窗。
        //把状态复位后返回，下面那套「弹窗 + StartLogin」不再执行。
        if (nCode != LOGIN_SUCCESS)
        {
            m_pPanel->TrayController().StopLoginAnimation();
            m_pPanel->TrayController().ReloadAppIcon();
            m_bResumingOnline = false;
            m_nSessionStatus = kSessionNotLogin;
            return 0;
        }

        //登录成功：先把登录窗采集到的账号 / 密码 / 勾选项取回来，再关窗。
        //
        //这一步<u>必须</u>在这里做，不能只依赖 StartLogin 里 DoModal 返回之后的那次
        //GetLoginAccountInfo。登录窗改成「登录期间不关窗」之后，登录结果是在模态
        //消息循环<u>内部</u>派发到本函数的，此刻 DoModal 尚未返回，StartLogin 那行
        //还没执行到，m_stAccountInfo 仍是上一轮的值（本进程首次登录时为全零）。
        //下面保存历史账号一段直接用 m_stAccountInfo.szUser，取不到账号就会往
        //LoginAccountList.dat 里追加一条用户名为空的记录，并把它设成最后登录账号；
        //下次启动登录窗便预填出一个空账号。2026-08-13 实测踩过一次。
        m_LoginDlg.GetLoginAccountInfo(&m_stAccountInfo);
        m_LoginDlg.CloseAfterLoginSuccess();
    }

    m_pPanel->TrayController().StopLoginAnimation();

    // 此刻 m_bAlreadyLogin 还没置真（成功分支下面才置），GetConnectionUiState 会算成
    // "连接未就绪"、取灰色图标；登录成功分支末尾会再调一次 RefreshTrayIconForStatus
    // 换成彩色。这样登录失败时托盘自然停在灰色，无需另行处理。
    m_pPanel->TrayController().ReloadAppIcon();

    // 本次登录是不是"从离线恢复在线"发起的。下面各分支要用，取完即复位：无论成功还是
    // 失败，这一轮恢复流程到此为止。
    const bool bResuming = m_bResumingOnline;
    m_bResumingOnline = false;

    switch (nCode)
    {
    case LOGIN_SUCCESS:				// 登录成功
    {
        //守卫：登录成功之后，"当前登录账号"这个统一取值口必须真的取得到账号。
        //net::CurrentUserAccount() 读的是 CFlamingoClient::GetInstance() 登记进去的那个
        //实例，而登录信息写的是本对话框的成员 m_FMGClient。两者一旦不是同一个对象
        //（例如构造函数里那行 SetInstance 被删掉、或将来又冒出第二个 CFlamingoClient），
        //取值口就会一直返回空串或返回别人的账号，按账号分目录的缓存（ChatFile /
        //ChatImage / Avatar）随之全部失效 —— 而且不报错、不弹框，只表现为"每次都要重新
        //下载"。这类"读写不是同一个对象"的错误没有别的地方会发声，故在此校验并记日志。
        //放在本分支最前面：后面的 CreateEssentialDirectories 与各项拉取都会用到缓存目录，
        //日志要落在它们之前才好定位。
        const std::wstring strAccountFromAccessor = net::CurrentUserAccount();
        const std::wstring& strAccountFromClient  = m_lpFMGClient->m_UserMgr.m_UserInfo.m_strAccount;
        if (strAccountFromAccessor.empty() || strAccountFromAccessor != strAccountFromClient)
        {
            LOG_ERROR("login succeeded but the current-account accessor is not wired to the live "
                "CFlamingoClient: net::CurrentUserAccount()=[%s] while the logged-in account "
                "is [%s]. Every per-account cache directory (ChatFile / ChatImage / Avatar) "
                "will silently degrade to \"no cache\".",
                EncodeUtil::UnicodeToUtf8(strAccountFromAccessor).c_str(),
                EncodeUtil::UnicodeToUtf8(strAccountFromClient).c_str());
        }
        else
        {
            LOG_INFO("current-account accessor check passed, account=%s.",
                EncodeUtil::UnicodeToUtf8(strAccountFromAccessor).c_str());
        }

        if (m_dwReconnectTimerId != 0)
        {
            ::KillTimer(m_hHost, m_dwReconnectTimerId);
            m_dwReconnectTimerId = 0;
        }

        // PR7: 旧自绘 UI 不再显示。新 BalloonMainPanel 由本函数末尾
        // CreateAndShow。

        m_lpFMGClient->InitNetThreads();

        //读取重连时间间隔
        CIniFile iniFile;
        CString strIniFile;
        strIniFile.Format(_T("%sconfig\\flamingo.ini"), g_szHomePath);

        if (iniFile.ReadInt(_T("server"), _T("enablereconnect"), 1, strIniFile) != 0)
            m_bEnableReconnect = true;
        else
            m_bEnableReconnect = false;

        //如果都不重连，就没必要读取重连时间间隔了
        if (m_bEnableReconnect)
        {
            m_uReconnectInterval = iniFile.ReadInt(_T("server"), _T("reconnectinterval"), 5000, strIniFile);
            if (m_uReconnectInterval <= 0)
                m_uReconnectInterval = 5000;
        }

        CreateEssentialDirectories();

        //登录成功以后，获取服务器时间
        //m_lpFMGClient->RequestServerTime();


        //此处原先要打开 v1 接收线程的"UI 闸门"，放行登录期间缓存下来的聊天消息。
        //那个线程（CRecvMsgThread）在 v2 下收不到任何数据，2026-08-14 随 v1 清理
        //一并删除，闸门与缓存也就不存在了：v2 的消息由 CIUSocket 收到即投给代理
        //窗口，不经过任何缓存队列。

        //TODO: 这里调用时，网络线程一定都启动了吗？
        m_lpFMGClient->StartCheckNetworkStatusTask();
        //m_lpFMGClient->StartHeartbeatTask();

        // 保存登录帐号列表。没勾"记住密码"时 RecordLoginSuccess 不保存密码(已存的
        // 一并清空)。原先这里整条 memcpy，没勾时密码也照样落盘（2026-09-30 修复）。
        // m_stAccountInfo 本身不改：其中的密码之后还要用，系统设置里打开"自动登录"时
        // 由 OnSetAutoLogin 用它存下本次登录的密码。
        if (!m_LoginAccountList.RecordLoginSuccess(&m_stAccountInfo))
        {
            LOG_WARNING("login succeeded but the account could not be recorded in the history list (empty account).");
        }

        // 记下本账号的姓名(昵称) —— 登录窗下次切到这个账号时，在头像下方
        // 显示姓名而不是账号。昵称此刻已由 CFlamingoClient::OnLoginResult 从
        // 登录应答的 profile 填进 m_UserInfo（它先于本消息被调用）。
        m_LoginAccountList.SetNickName(m_stAccountInfo.szUser,
            m_lpFMGClient->m_UserMgr.m_UserInfo.m_strNickName.c_str());
        LOG_INFO(_T("save login account: account=%s, nickname=%s."),
            m_stAccountInfo.szUser, m_lpFMGClient->m_UserMgr.m_UserInfo.m_strNickName.c_str());

        m_LoginAccountList.SetLastLoginUser(m_stAccountInfo.szUser);
        SaveLoginAccountListFile();

        // 加载杂项配置
        //strFileName = m_lpFMGClient->GetPersonalFolder()+_T("MiscConfig.xml");
        //LoadMiscConfig(strFileName.c_str());

        // 注册提取消息热键(Ctrl+Alt+D)
        ::RegisterHotKey(m_hHost, kShowPanelHotKeyId, MOD_CONTROL | MOD_ALT, kShowPanelHotKey);

        //为了调试方便，调试版就不开启这个功能了- -！
//#ifndef _DEBUG
            //存储当前登录账号至本地文件
//#endif

        m_lpFMGClient->LoadUserConfig();
        // 把该账号的「自动识别链接」设置灌进进程级开关：气泡在测量阶段读它决定
        // 要不要识别，故必须在任何聊天窗打开之前设好（配置里没有这一项时默认开）。
        balloonchatwnd::SetChatLinkOptionEnabled(
            m_lpFMGClient->m_UserConfig.IsEnableLinkDetect() != FALSE);
        // 旧好友列表头像风格成员(m_nBuddyListHeadPicStyle / m_bShowBigHeadPicInSel)已随
        // 旧主窗口一并移除；对应用户配置仍保留在 UserConfig 中，Phase 2 由
        // BalloonMainPanel 接管列表样式时直接从 UserConfig 读取即可。

        // 主面板的位置由它自己维护（首次居中，之后沿用用户拖到的位置），
        // 登录成功时不再另行摆放。
        // 已无意义,但保留为无害空操作; Phase 2 由 BalloonMainPanel 自持其
        // 位置与尺寸。

        //加载最近联系人列表
        m_lpFMGClient->m_UserMgr.LoadRecentList();
        // PR7: 不再切换旧 TabCtrl 选项 —— 旧 UI 已废弃,BalloonMainPanel
        // 在 CreateAndShow 时自行决定默认 Tab。

        m_bAlreadyLogin = TRUE;

        // 登录成功后发起一次静默检查更新。默认<u>只查不下</u>：查出新版本时弹升级窗，
        // 由用户决定要不要下载 —— 背着用户拉几十 MB 不合适。想提前验证下载链路时，
        // 把 flamingo.ini 的 [app] autodownloadupdate 置 1。
        //
        // 结果经 FMG_MSG_UPDATE_RESULT 回到 OnSilentUpdateResult。这里把投递目标登记
        // 成主窗口，是因为此刻升级窗还没创建；等它创建时会把目标改登记为它自己。
        // 升级窗已经开着的话就别抢，否则正在下载的进度会投到主窗口来。
        if (!CBalloonUpdateWindow::GetInstance().IsShowing())
        {
            update::UpdateManager::GetInstance().SetNotifyTarget(m_hHost);
        }

        // 文件夹传输管理器的回投目标登记成<u>代理窗口</u>，而不是主窗口：它自己的工作
        // 线程投出来的进度 / 结果，要和网络层投来的那五条信令走同一条「代理窗 → 主窗口
        // → 主面板 → 聊天窗」通路，两路混投到不同起点只会让排查时多一种可能性。
        // 上一次登录残留的会话与保留期计时在这里一并作废 —— 按产品定稿，重试窗口只在
        // 本次登录期间有效。
        foldertransfer::FolderTransferManager::GetInstance().Reset();
        foldertransfer::FolderTransferManager::GetInstance().SetNotifyTarget(
            m_lpFMGClient->m_UserMgr.m_hProxyWnd);
        // 本机同时可进行几笔文件夹传输由 flamingo.ini 的 [app] foldermaxconcurrent 决定
        // （默认 5，发送与接收各算一笔）。每次登录都重读一次，改了配置重新登录即生效；
        // 缺失或取值非法时管理器自行回落默认值并记一条日志。
        foldertransfer::FolderTransferManager::GetInstance().LoadConfig();
        {
            CIniFile iniFile;
            CString strIniPath(g_szHomePath);
            strIniPath += _T("config\\flamingo.ini");
            const bool bAutoDownload =
                (iniFile.ReadInt(_T("app"), _T("autodownloadupdate"), 0, strIniPath) != 0);
            update::UpdateManager::GetInstance().CheckAndDownload(bAutoDownload);
        }

        m_nSessionStatus = kSessionLogin;

        // 显示主面板。窗口在进程启动时就已经建好（OnInitDialog 里的 EnsureCreated），
        // 这里只负责用本次登录用户的资料刷新 Header、再把它显示出来并置于前台。
        //
        // 本分支在"重新登录"时也会走到 —— 用户主动下线后再上线（状态菜单 / 托盘选在线）
        // 走 GoOnline → 重新登录 → 这里；断线重连成功也会到这里。这些场景下面板本来就
        // 开着，ShowAfterLogin 是幂等的，重复调用只是再刷一次 Header、再显示一次。
        // 界面数据由下面的 RequestContacts / RequestConversations / RequestUserGroups
        // 重新拉取刷新。
        m_pPanel->ShowAfterLogin();
        ::SetFocus(m_pPanel->m_hWnd);

        //本轮登录重新开始记账："收到陌生人消息就补拉通讯录"那道兜底闸门要清零。
        //掉线重连期间组织架构可能变过、cmd_org_structure_notify 也可能错过，
        //上一轮攒下的"这些人已经问过了"不该继续压着本轮的补拉。
        m_pPanel->ResetUnknownSenderPolicy();

        //同理，本地消息记录的"覆盖区间"也要作废：离线或断线期间在别的设备上删掉的消息，
        //本机收不到删除推送，之前验证为"完整"的区间可能已经不准。清空后各会话在打开
        //消息记录窗时从最新一页重新验证（见 docs/清理聊天记录方案.md §7.4）。
        m_pPanel->ResetHistoryCoverage();

        //已读上报的去重记录同样作废：断线期间发出的已读上报可能已被丢弃，
        //清空后再打开会话时会重新上报一次（见 ReadReportDedupe.h）。
        m_pPanel->ResetReadReportDedupe();

        //登录成功后拉取一次 v2 通讯录，应答经 FMG_MSG_V2_CONTACTS 异步回投，
        //在 OnV2Contacts 里填充联系人面板（让联系人树由 mock 变真实）。
        CIUSocket::GetInstance().RequestContacts();

        //登录成功后再拉取一次 v2 最近会话列表，应答经 FMG_MSG_V2_CONVERSATIONS
        //异步回投，在 OnV2Conversations 里填充"消息"页（让会话列表由 mock 变真实）。
        CIUSocket::GetInstance().RequestConversations();

        //登录成功后拉取本人加入的全部群列表（含无消息空群），应答经
        //FMG_MSG_V2_USER_GROUPS 回投，在 OnUserGroupsArrived 里填"群"Tab。
        CIUSocket::GetInstance().RequestUserGroups();

        //登录成功后拉取发给本人的平台公告（主面板 Banner 上的「消息盒子」），应答经
        //FMG_MSG_V2_ANNOUNCEMENTS 回投，在 OnAnnouncementsArrived 里落本地库并刷角标。
        //这一条走主面板而不是直接调 CIUSocket：增量游标要从主面板持有的本地公告库里取，
        //本机已有的那些不必再拉一遍。
        m_pPanel->RequestAnnouncements();

        //登录成功：托盘图标转回彩色、提示文字刷新。
        m_pPanel->TrayController().RefreshTrayIconForStatus();

        //连接已就绪：广播上线，聊天窗收起离线提示条、输入区恢复可用，其余联网入口解禁。
        //从离线恢复在线这条路走到这里，界面才算真正回到可用状态。
        BroadcastConnectionState();
    }
    break;

    case LOGIN_FAILED:				// 登录失败
    {
        //"从离线恢复在线"失败时不能走下面那套（弹提示 + StartLogin 把用户踢回登录窗）：
        //用户只是想从离线切回在线，主面板还开着、账号也还登录着，失败就该原地退回离线态，
        //让他稍后再试。ResumeOnlineFailed 负责退回并提示。
        if (bResuming)
        {
            ResumeOnlineFailed(_T("网络故障，恢复在线失败，仍保持离线状态。"));
            break;
        }

        ::MessageBox(m_hHost, _T("网络故障，登录失败！"), g_strAppTitle.c_str(), MB_OK);
        StartLogin();

        m_nSessionStatus = kSessionNotLogin;
    }
    break;

    case LOGIN_VERSION_TOO_OLD:
    {
        //服务端以 error_code_version_too_old 拒绝了登录。这个错误重试多少次都是同一个
        //结果，所以不走"弹提示 + 退回登录窗"那套，直接把用户引到升级流程上去：拉起
        //升级窗并立即检查更新。检查更新走的是登录前白名单，此刻未登录也能查、能下。
        if (bResuming)
        {
            ResumeOnlineFailed(_T("恢复在线失败：客户端版本过低，请升级后重试。"));
            break;
        }

        ::MessageBox(m_hHost, _T("当前客户端版本过低，无法登录，请先升级。"),
                   g_strAppTitle.c_str(), MB_OK | MB_ICONWARNING);
        StartLogin();
        m_nSessionStatus = kSessionNotLogin;

        //放在退回登录窗之后弹：否则升级窗会被随后出现的登录窗盖住。
        CBalloonUpdateWindow::GetInstance().ShowChecking();
    }
    break;

    case LOGIN_UNREGISTERED:
    {
        if (bResuming)
        {
            ResumeOnlineFailed(_T("恢复在线失败：该账号未注册，仍保持离线状态。"));
            break;
        }
        ::MessageBox(m_hHost, _T("用户名未注册！"), g_strAppTitle.c_str(), MB_OK);
        StartLogin();
        m_nSessionStatus = kSessionNotLogin;
    }
    break;

    case LOGIN_PASSWORD_ERROR:		// 密码错误
    {
        if (bResuming)
        {
            ResumeOnlineFailed(_T("恢复在线失败：密码错误，仍保持离线状态。"));
            break;
        }
        //服务端明确说了密码不对：把本机存下的这个密码忘掉，并取消"记住密码" /
        //"自动登录"。留着的话，勾了自动登录的用户每次开机都会被自动登一次、再被
        //弹一次"密码错误"，而在那之前他根本没有界面可以取消勾选。
        ForgetSavedPasswordForCurrentAccount();

        ::MessageBox(m_hHost, _T("密码错误！"), g_strAppTitle.c_str(), MB_OK);
        StartLogin();
        m_nSessionStatus = kSessionNotLogin;
    }
    break;

    case LOGIN_SERVER_REFUSED:
    {
        if (bResuming)
        {
            ResumeOnlineFailed(_T("恢复在线失败：服务器拒绝了登录请求，仍保持离线状态。"));
            break;
        }
        // PR7: 不再切到旧"正在登录"面板 —— 主窗口始终隐藏。
        ::MessageBox(m_hHost, _T("服务器拒绝您的登录请求！"), g_strAppTitle.c_str(), MB_OK);
        StartLogin();
        m_nSessionStatus = kSessionNotLogin;
    }
    break;

    case LOGIN_USER_CANCEL_LOGIN:	// 用户取消登录
    {
        if (bResuming)
        {
            ResumeOnlineFailed(NULL);   // 用户自己取消的，不必再弹提示
            break;
        }
        StartLogin();
        m_nSessionStatus = kSessionNotLogin;
    }
    break;
    }

    return 0;
}

// 注销返回消息
LRESULT CSessionController::OnExitForUpdate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/)
{
    LOG_INFO("SessionController::OnExitForUpdate: update package is ready, exit now and hand over to the updater.");
    //直接退出，不经 OnClose 的「退出还是最小化」询问
    m_pAppHost->RequestAppExit();
    return 0;
}

LRESULT CSessionController::OnSilentUpdateResult(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    update::UpdateTaskResult* pResult = (update::UpdateTaskResult*)lParam;
    if (pResult == NULL)
    {
        return 0;
    }

    LOG_INFO("SessionController::OnSilentUpdateResult: transportOk=%d, code=%d, hasUpdate=%d, "
             "usable=%d, forced=%d, latest=%s.",
             pResult->m_transportOk ? 1 : 0, pResult->m_code,
             pResult->m_hasUpdate ? 1 : 0, pResult->m_packageUsable ? 1 : 0,
             pResult->m_forced ? 1 : 0, pResult->m_latestVersion.c_str());

    //只有确实有可用新版本才打扰用户。已是最新、检查超时、服务端报错这几种情况
    //都只留日志——用户并没有主动要求检查，弹个"已是最新"或"检查失败"纯属打扰。
    if (pResult->m_hasUpdate && pResult->m_packageUsable)
    {
        CBalloonUpdateWindow& w = CBalloonUpdateWindow::GetInstance();

        //用户在点击登录那一步已经看过这个版本并选了「稍后」：不再弹第二次。
        //点登录时查一次、登录成功后又静默查一次，两次拿到的是同一个版本，不拦的话
        //用户刚关掉的窗立刻又冒出来。强制升级不受此限 —— 它没有「稍后」可点。
        if (!pResult->m_forced && w.IsVersionPostponed(pResult->m_latestVersion))
        {
            LOG_INFO("SessionController::OnSilentUpdateResult: version %s was postponed by the user, no prompt this time.",
                     pResult->m_latestVersion.c_str());
            delete pResult;
            return 0;
        }

        //强制升级时把主面板一并禁用，直到升级完成 —— 否则用户把升级窗晾在一边照常
        //聊天，"强制"就名不副实了。非强制不禁用：那只是个提示，不该妨碍人干活。
        if (pResult->m_forced && m_pPanel->IsWindow())
        {
            //主面板这边没有待续的登录，关窗只需恢复可用
            w.SetGatedWindow(m_pPanel->m_hWnd,
                             CBalloonUpdateWindow::kGatedMainPanel);
        }

        //ShowResult 内部会把消息投递目标改登记为升级窗自己，后续下载进度直接投给它。
        w.ShowResult(*pResult);
    }

    delete pResult;
    return 0;
}

// v2 更新资料应答（当前用于个性签名）：交主面板据 code 保留乐观更新或回退提示
LRESULT CSessionController::OnV2UpdateProfileResult(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //lParam 为 IUSocket 接收线程堆分配的 net::UpdateProfileResponse，处理后须 delete。
    net::UpdateProfileResponse* pResp = (net::UpdateProfileResponse*)lParam;
    if (pResp == NULL)
        return 0;
    m_pPanel->OnUpdateProfileResult(*pResp);
    delete pResp;

    //资料更新走的是"乐观更新 + 失败回退"（见 CBalloonProfileDlg::OnSaveResult），
    //上面这一路处理完之后 m_UserInfo 里的昵称就是最终生效的值。若它和历史登录
    //列表里存的姓名对不上（用户刚改了昵称），写回并落盘，使下次在登录窗切到本
    //账号时头像下方显示的是新姓名。没变则不写，避免每次改签名都动一次文件。
    SyncNickNameToLoginAccountList();
    return 0;
}

// 把当前登录账号的昵称同步进历史登录账号列表（有变化才写盘）
void CSessionController::SyncNickNameToLoginAccountList()
{
    LOGIN_ACCOUNT_INFO* lpAccount = m_LoginAccountList.Find(m_stAccountInfo.szUser);
    if (lpAccount == NULL)
        return;

    const tstring& strNickName = m_lpFMGClient->m_UserMgr.m_UserInfo.m_strNickName;
    if (strNickName == lpAccount->szNickName)
        return;

    LOG_INFO(_T("nickname changed, update login account list: account=%s, nickname=%s."),
        m_stAccountInfo.szUser, strNickName.c_str());
    m_LoginAccountList.SetNickName(m_stAccountInfo.szUser, strNickName.c_str());
    SaveLoginAccountListFile();
}

// 清掉某账号在本机存下的"自动登录"标志并立即落盘
void CSessionController::ClearAutoLoginFlag(LPCTSTR lpszAccount)
{
    if (lpszAccount == NULL || lpszAccount[0] == _T('\0'))
    {
        LOG_INFO("SessionController::ClearAutoLoginFlag: account is empty, skipped.");
        return;
    }

    LOGIN_ACCOUNT_INFO* lpAccount = m_LoginAccountList.Find(lpszAccount);
    if (lpAccount == NULL)
    {
        //该账号还没在本机成功登录过（历史列表里没有它），本来就没有标志可清。
        LOG_INFO("SessionController::ClearAutoLoginFlag: account is not in the history list, skipped. account=%s.",
            EncodeUtil::UnicodeToUtf8(std::wstring(lpszAccount)).c_str());
        return;
    }

    if (!lpAccount->bAutoLogin)
    {
        return;   //本就没勾自动登录，不必白写一次盘
    }

    lpAccount->bAutoLogin = FALSE;
    SaveLoginAccountListFile();
    LOG_INFO("SessionController::ClearAutoLoginFlag: auto-login flag cleared. account=%s.",
             EncodeUtil::UnicodeToUtf8(std::wstring(lpszAccount)).c_str());
}

// 系统设置「自动登录」开关：wParam = 目标状态（1 开 / 0 关），lParam 未用，无堆负载
LRESULT CSessionController::OnSetAutoLogin(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/)
{
    const BOOL bEnable = (wParam != 0) ? TRUE : FALSE;
    if (m_stAccountInfo.szUser[0] == _T('\0'))
    {
        //设置窗只在登录成功后才能打开，正常流程不会走到这里
        LOG_WARNING("SessionController::OnSetAutoLogin: no logged-in account, ignored. enable=%d.", bEnable);
        return 0;
    }

    //打开时传入本次登录所用的密码：自动登录要靠已存的密码完成，
    //而本次登录若没勾"记住密码"，列表里存的密码可能是空的
    if (!m_LoginAccountList.ApplyAutoLoginSetting(m_stAccountInfo.szUser, bEnable, m_stAccountInfo.szPwd))
    {
        LOG_WARNING("SessionController::OnSetAutoLogin: account is not in the history list, ignored. account=%s, enable=%d.",
            EncodeUtil::UnicodeToUtf8(std::wstring(m_stAccountInfo.szUser)).c_str(), bEnable);
        return 0;
    }

    //m_stAccountInfo 也要同步：每次登录成功（包括断线自动重连成功）时，OnLoginResult 会按它
    //（经 RecordLoginSuccess）更新历史列表里该账号的密码与两个勾选项。只改列表不改它的话，
    //下一次重连就会把这里的修改恢复成旧值
    m_stAccountInfo.bAutoLogin = bEnable;
    if (bEnable)
    {
        m_stAccountInfo.bRememberPwd = TRUE;
    }

    SaveLoginAccountListFile();
    LOG_INFO("SessionController::OnSetAutoLogin: auto-login turned %s from settings. account=%s.",
        bEnable ? "on" : "off", EncodeUtil::UnicodeToUtf8(std::wstring(m_stAccountInfo.szUser)).c_str());
    return 0;
}

// 忘掉当前账号在本机存下的密码，并取消"记住密码" / "自动登录"
void CSessionController::ForgetSavedPasswordForCurrentAccount()
{
    const tstring& strAccount = m_lpFMGClient->m_UserMgr.m_UserInfo.m_strAccount;
    if (strAccount.empty())
    {
        LOG_INFO("SessionController::ForgetSavedPasswordForCurrentAccount: account is empty, skipped.");
        return;
    }

    LOGIN_ACCOUNT_INFO* lpAccount = m_LoginAccountList.Find(strAccount.c_str());
    if (lpAccount == NULL)
    {
        LOG_INFO("SessionController::ForgetSavedPasswordForCurrentAccount: account is not in the history list, skipped. account=%s.",
            EncodeUtil::UnicodeToUtf8(strAccount).c_str());
        return;
    }

    lpAccount->szPwd[0]     = _T('\0');
    lpAccount->bRememberPwd = FALSE;
    lpAccount->bAutoLogin   = FALSE;
    SaveLoginAccountListFile();
    LOG_INFO("SessionController::ForgetSavedPasswordForCurrentAccount: saved password cleared, remember-password and auto-login turned off. account=%s.",
        EncodeUtil::UnicodeToUtf8(strAccount).c_str());
}

// 把历史登录账号列表写回磁盘
void CSessionController::SaveLoginAccountListFile()
{
    tstring strFileName = Hootina::CPath::GetAppPath() + _T("Users\\LoginAccountList.dat");
    if (!m_LoginAccountList.SaveFile(strFileName.c_str()))
    {
        LOG_ERROR(_T("save login account list failed, file=%s."), strFileName.c_str());
    }
}

// 切换我的在线状态（主面板状态菜单触发）。wParam = 目标 v2 presence（见 MyPresenceValue，
// 与 net/Protocol.h presence_type 同值）。据当前连接 / 登录态分三路处理。
LRESULT CSessionController::OnChangeMyPresence(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam)
{
    int presence = (int)wParam;
    //lParam 是本次切换的来源：0（kPresenceChangeManual）= 用户手动选的，
    //1（kPresenceChangeAuto）= "无操作自动离开"功能切的。主面板状态菜单与托盘菜单
    //两个既有入口传的都是 0，语义天然正确，无需改动。
    const bool bAuto = ((int)lParam == kPresenceChangeAuto);

    LOG_INFO("SessionController::OnChangeMyPresence: target presence=%d, sessionStatus=%d, alreadyLogin=%d, auto=%d.",
        presence, m_nSessionStatus, m_bAlreadyLogin ? 1 : 0, bAuto ? 1 : 0);

    //通知自动离开控制器：本次切换的目标状态与来源。它据此维护"当前这个离开是不是
    //自动切过去的"——只有自动切过去的离开，才会在检测到键鼠操作时被自动切回在线。
    m_pPanel->NotifyMyPresenceChanged(presence, (int)lParam);

    //先把主面板的状态胶囊乐观更新成目标状态。这一步必须放在这里而不是各个发起点：
    //切换在线状态有两个入口（主面板状态菜单、系统托盘右键菜单），两者都汇到本函数，
    //只有放在这里，从托盘切状态时胶囊才会跟着变。
    //（原先这步写在 CBalloonMainPanel::ShowStatusMenu 里，托盘那条路走不到，表现为
    // "从托盘切了状态，主面板上的状态没变"。）
    if (m_pPanel->IsWindow())
    {
        m_pPanel->ApplyPresenceOptimistic(presence);
    }

    //托盘悬停提示的第三行也跟着刷新，否则切完状态去悬停托盘看到的还是旧状态。
    //这里用 ModifyTip 而不是 UpdateTrayIconTip：前者只换提示文字、不动图标，托盘正在
    //闪新消息头像时刷新提示不会把闪烁打断。
    m_pPanel->TrayController().RefreshTipOnly();

    if (presence == kMyPresenceOffline)
    {
        //【1】先置"用户主动下线"态，再断连接。顺序不能反：OnNetError 据这个标志决定
        //     要不要起自动重连，而关 socket 会引发掉线事件；虽然该事件目前是 PostMessage
        //     投递（处理时本函数已返回），但依赖投递方式是脆的 —— 与 OnLogout 的写法对齐。
        m_nSessionStatus = kSessionUserGoOffline;

        //【2】已经在跑的重连定时器必须就地停掉。这是"主动离线"最容易漏的一环：
        //     OnTimer 的重连分支只看定时器在不在，不看登录态，掉线重连过程中选离线的话，
        //     定时器会继续把用户连回服务器（见 OnTimer 里的 ShouldAutoReconnect 守卫）。
        if (m_dwReconnectTimerId != 0)
        {
            ::KillTimer(m_hHost, m_dwReconnectTimerId);
            m_dwReconnectTimerId = 0;
            LOG_INFO("SessionController::OnChangeMyPresence: user went offline, running reconnect timer stopped.");
        }

        //【3】真正下线：立起网络层离线闸门、丢弃排队中的帧、断开 chat/file/img 三条连接。
        m_lpFMGClient->GoOffline();

        //【4】托盘图标换成灰色的离线图标，提示文字同步。
        m_pPanel->TrayController().RefreshTrayIconForStatus();

        //【5】广播离线态：聊天窗输入区置灰、顶部弹出离线提示条，其余需要联网的入口一并拦住。
        BroadcastConnectionState();

        LOG_INFO("SessionController::OnChangeMyPresence: entered the user-went-offline state.");
        return 0;
    }

    //目标为在线 / 离开 / 勿扰。
    if (m_bAlreadyLogin && m_nSessionStatus == kSessionLogin)
    {
        //已登录且连接正常：直接发 cmd_change_presence 改状态，服务端向好友广播。
        //SendChangePresence 内部会把期望在线状态记入 m_nRecordOnlineStatus，供后续重连沿用。
        CIUSocket::GetInstance().SendChangePresence(presence);
        //状态变了，托盘图标与提示同步（在线三态目前共用彩色图标，但提示文字要换）。
        m_pPanel->TrayController().RefreshTrayIconForStatus();
    }
    else
    {
        //当前离线 / 未登录 / 重连中：先记下期望在线状态，再重新上线 —— CFlamingoClient::Login
        //会读取它作为登录 presence（见 FlamingoClient.cpp 的 Login）。
        CIUSocket::GetInstance().SetRecordOnlineStatus(presence);

        //标记"这次登录是从离线恢复在线"。登录失败时 OnLoginResult 据此决定退回离线态、
        //而不是像首次登录失败那样弹出登录窗把用户踢回登录界面（见 OnLoginResult）。
        m_bResumingOnline = true;

        //此刻连接尚未建立，界面进入"正在连接"中间态：提示条改成正在连接、各项功能仍然
        //拦住（胶囊已经乐观显示成在线，但消息这会儿发出去也到不了服务器）。
        BroadcastConnectionState();

        LOG_INFO("SessionController::OnChangeMyPresence: resuming online, logging in again, target presence=%d.",
            presence);
        m_lpFMGClient->GoOnline();
    }
    return 0;
}

// 登出（托盘菜单 / 主面板系统菜单触发，经 FMG_MSG_LOGOUT 投递而来）。
//
// 与"离线"(OnChangeMyPresence 的 kMyPresenceOffline)的区别：离线只关三条连接、主面板
// 留在屏幕上，随时可以再上线；登出还要关掉所有子窗口、销毁主面板并重新弹出登录窗，
// 等于回到程序刚启动的状态。整条序列与"账号在别处登录被踢下线"(OnSelfStatusChange)
// 走的是同一套动作，区别仅在于这里由用户主动发起、且不弹那条被迫下线的通知。
//
// 本处理器<u>必须</u>由 PostMessage 投递进来（见 FMG_MSG_LOGOUT 的定义）：它要销毁主面板、
// 并在 StartLogin 内进入登录窗的模态循环，不能跑在菜单 TrackPopup 返回后的调用栈上，
// 否则等于在消息处理函数里嵌套模态循环并顺手销毁消息来源窗口。
LRESULT CSessionController::OnLogout(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/)
{
    LOG_INFO("SessionController::OnLogout: logout requested. sessionStatus=%d, alreadyLogin=%d.",
        m_nSessionStatus, m_bAlreadyLogin ? 1 : 0);

    //二次确认：登出会断开连接并关掉所有聊天窗，误点代价不小。确认框挂在主面板上；
    //主面板尚未创建或已销毁时退回挂到宿主句柄。
    HWND hOwner = m_hHost;
    if (m_pPanel->IsWindow())
        hOwner = m_pPanel->m_hWnd;

    if (::MessageBox(hOwner,
                     _T("确定要登出当前账号吗？\r\n登出后将断开连接并关闭所有聊天窗口。"),
                     g_strAppTitle.c_str(),
                     MB_YESNO | MB_ICONQUESTION) != IDYES)
    {
        LOG_INFO("SessionController::OnLogout: cancelled by the user in the confirm dialog, logout aborted.");
        return 0;
    }

    //收尾与"被踢下线"完全相同，共用同一个序列（见 ReturnToLoginWindow）。区别只在于
    //这里是用户主动发起的 —— 点"登出"就是明确表达了"我不想自动登进去"，故要清掉本机
    //的自动登录标志；被踢下线那条路径则按原因区分。
    ReturnToLoginWindow(balloonmain::kLoginExitUserLogout,
                        balloonmain::kKickoutReasonUnknown);
    return 0;
}

// 断开连接、关掉所有窗口、清空登录态，并重新弹出登录窗 —— 用户主动登出（OnLogout）与
// 账号在别处登录被顶下线（OnV2Kickout）共用的收尾序列。
//
// 两条路径此前各写了一份几乎相同的代码，改一处漏一处的风险很实在（v1 时期就漏过复位
// m_bAlreadyLogin，导致回到登录界面后托盘右键弹的还是已登录那套菜单），故合并到这里。
//
// 调用方须保证自己跑在一个干净的消息调用栈上（由 PostMessage 投递而来）：本函数要销毁
// 主面板、并在 StartLogin 内进入登录窗的模态循环，不能跑在菜单 TrackPopup 返回后的栈上。
void CSessionController::ReturnToLoginWindow(int exitKind, int kickoutReason)
{
    //先置"用户主动下线"态再关连接，压制 OnNetError 里的自动重连 —— 否则关 socket 会被
    //当成掉线触发重连（见 OnNetError 对 kSessionUserGoOffline 的判断）。
    m_nSessionStatus = kSessionUserGoOffline;

    //断开 chat / file / img 三条连接，再关掉所有聊天窗等子窗口。
    //（被踢下线那条路径已经 GoOffline 过一次，这里再来一次是幂等的。）
    //
    //这里<u>不能</u>调 m_lpFMGClient->Uninit()：那是<u>退出进程</u>时才做的整体拆除，在"回登录
    //界面"这条路上调它，下一次登录就再也登不进去了。两个原因，各自都足以致命：
    //  · Uninit 头一件事是 DestroyProxyWnd()，把进程级的消息代理窗口销毁、m_hProxyWnd
    //    置 NULL。而登录线程正是把登录结果 PostMessage 给这个句柄的（见
    //    CBalloonLoginDlg::LoginThreadProc）。窗口没了，登录其实已经成功、服务器也回了
    //    code=0，但结果消息投不到 UI，界面就一直卡在"登录中"，用户看到的就是"登不上"。
    //  · Uninit 还会 Stop + Join 掉收发消息线程，而它们基于 CThread —— Start() 只在线程
    //    指针为空时才创建，Join() 之后指针并不置空、m_bStop 也仍是 true（见 Thread.cpp）。
    //    也就是说这套线程<u>停掉就再也起不来</u>，后续 InitNetThreads() 是个空操作。
    //
    //正确做法就是这里的写法：只断连接、不拆线程。收发线程在连接关闭期间自然空转
    //（收线程见 IsClosed 就短睡，发线程阻塞在条件变量上），下次登录 Connect 成功后
    //它们照常接着干活，与"掉线 / 主动离线后再上线"走的是同一条路。
    m_lpFMGClient->GoOffline();

    //文件夹传输的会话、清单与断点续传进度都只在本次登录期间有效，登出 / 被踢下线时
    //一并作废：停掉还在跑的收发状态机、回收其工作线程，再清空重试保留期。放在关子窗口
    //之前做 —— 状态机的进度回调是投给代理窗口再转到聊天窗的，窗口先没了会白投一轮。
    foldertransfer::FolderTransferManager::GetInstance().Reset();


    //把主面板复位到"未登录"状态，然后隐藏它。主面板是常驻窗口，登出<u>不销毁</u>，
    //下次登录复用同一个窗口（见 OnInitDialog 里 EnsureCreated 处的说明）。
    //
    //复位这一步不能省：面板对象跨越了登出边界，它持有的子窗口与各项缓存不会自行消失。
    //  · 单聊窗、群聊窗等都是 Create(NULL, ...) 建出来的独立顶层窗口，既不是主面板的
    //    子窗口、也没有把主面板设为 owner，隐藏或销毁主面板都不会连带关掉它们；不复位
    //    的话用户登出后这些窗口仍留在屏幕上，还带着上一个账号的聊天内容。
    //  · 通讯录、会话、群资料等缓存都以 userid / groupid 为键，换账号登录后键的含义
    //    完全变了，不清就会串账号。
    //以上两点于 2026-08-18 修复。
    //
    //两步的先后顺序不能颠倒：ResetForLogout 要按控件 id 现查三棵树来清空节点，
    //窗口隐藏本身不影响查找，但把顺序写死在这里可以避免日后改成销毁时踩坑。
    m_pPanel->ResetForLogout();
    m_pPanel->HideForLogout();

    //账号与密码要在登录窗里回填（登出 / 被踢后两者都保留），而紧接着的 ClearUserInfo /
    //Reset 会把 m_UserInfo 整个清空，故先留存、清完再写回 —— StartLogin 正是从这两个
    //字段取登录窗的默认账号与默认密码。
    tstring strAccount  = m_lpFMGClient->m_UserMgr.m_UserInfo.m_strAccount;
    tstring strPassword = m_lpFMGClient->m_UserMgr.m_UserInfo.m_strPassword;

    m_lpFMGClient->m_UserMgr.ClearUserInfo();
    m_lpFMGClient->m_UserMgr.m_UserInfo.Reset();
    m_lpFMGClient->m_UserMgr.m_UserInfo.m_strAccount  = strAccount;
    m_lpFMGClient->m_UserMgr.m_UserInfo.m_strPassword = strPassword;

    //回到未登录态。m_bAlreadyLogin 必须复位：托盘菜单据它选形态，不复位的话回到登录界面
    //后托盘右键仍会弹出"在线 / 离线 / 登出"那套已登录菜单。
    m_bAlreadyLogin    = FALSE;
    m_nSessionStatus = kSessionNotLogin;

    //按退出方式决定要不要顺手关掉"自动登录"（口径见 balloonmain::ShouldClearAutoLoginOnExit）。
    //必须在这里清、而且要立刻落盘：不清的话，用户点了"登出"、本次进程内确实回到了登录窗，
    //可下次启动照样被自动登进去——而自动登录一旦成功就直接进主面板，他再也没有机会看到
    //登录窗去取消那个勾选，等于绕不出来。
    //账号取上面留存下来的 strAccount：紧邻的 ClearUserInfo 已经把 m_UserInfo 清过一遍了。
    if (balloonmain::ShouldClearAutoLoginOnExit(exitKind, kickoutReason))
    {
        ClearAutoLoginFlag(strAccount.c_str());
    }

    //托盘图标先摘掉，StartLogin(FALSE) 内部会重新挂上"Flamingo未登录"那只。
    m_pPanel->TrayController().RemoveIcon();

    LOG_INFO("SessionController::ReturnToLoginWindow: connection closed and main panel reset, showing the login window again.");

    //重新走登录流程（登录窗为模态）。用户直接关掉登录窗时 StartLogin 返回 false 并已
    //在内部 CloseDialog 退出程序，此处无需再处理其返回值。
    StartLogin(FALSE);
}

// v2 修改在线状态应答：交主面板据 code 保留乐观更新的胶囊或回退提示
LRESULT CSessionController::OnV2ChangePresenceResult(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //lParam 为 IUSocket 接收线程堆分配的 net::ChangePresenceResponse，处理后须 delete。
    net::ChangePresenceResponse* pResp = (net::ChangePresenceResponse*)lParam;
    if (pResp == NULL)
        return 0;
    m_pPanel->OnChangePresenceResult(*pResp);
    delete pResp;
    return 0;
}

// v2 好友在线状态变更推送：交主面板更新通讯录缓存并实时刷新联系人树该好友的状态点
LRESULT CSessionController::OnV2PresenceChanged(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //lParam 为 IUSocket 接收线程堆分配的 net::PresenceChangedNotify，处理后须 delete。
    net::PresenceChangedNotify* pNotify = (net::PresenceChangedNotify*)lParam;
    if (pNotify == NULL)
        return 0;
    m_pPanel->OnPresenceChanged(pNotify->m_userid, pNotify->m_presence);
    delete pNotify;
    return 0;
}

bool CSessionController::IsServerConnected() const
{
    // 两个条件缺一不可：socket 还连着（IsClosed 取反），且已经登录成功。只连上没登录
    // 时业务帧发过去也会被服务端拒掉，对用户而言同样是"还不能用"。
    if (!m_bAlreadyLogin)
    {
        return false;
    }
    return !CIUSocket::GetInstance().IsClosed();
}

void CSessionController::ResumeOnlineFailed(LPCTSTR lpszReason)
{
    LOG_INFO("SessionController::ResumeOnlineFailed: resuming online failed, falling back to offline.");

    // 连接已经断了（登录没成功），这里把上层状态一并拨回离线，等同于用户从没点过
    // "恢复在线"：压制自动重连、重新立起网络层的离线闸门、丢弃排队中的帧。
    m_nSessionStatus = kSessionUserGoOffline;
    CIUSocket::GetInstance().SetUserOffline(true);
    CIUSocket::GetInstance().ClearPendingSendData();

    // 状态胶囊退回离线 —— 点"在线"时它已经被乐观更新成在线了，不退回的话会出现
    // "胶囊显示在线、实际离线且不会重连"的自相矛盾。
    if (m_pPanel->IsWindow())
    {
        m_pPanel->ApplyPresenceOptimistic(kMyPresenceOffline);
    }

    // 托盘图标保持灰色、提示文字回到离线，界面门禁回到离线态（提示条重新变成
    // "点此恢复在线"，而不是停在"正在连接"）。
    m_pPanel->TrayController().RefreshTrayIconForStatus();
    BroadcastConnectionState();

    if (lpszReason == NULL)
    {
        return;   // 用户自己取消的，不必再弹提示
    }

    // 提示框挂在主面板上；主面板不在时退回挂到宿主句柄。
    HWND hOwner = m_hHost;
    if (m_pPanel->IsWindow())
    {
        hOwner = m_pPanel->m_hWnd;
    }
    ::MessageBox(hOwner, lpszReason, g_strAppTitle.c_str(), MB_OK | MB_ICONWARNING);
}

int CSessionController::GetConnectionUiState()
{
    return (int)balloonmain::ComputeConnectionUiState(GetCurrentPresence(), IsServerConnected());
}

void CSessionController::BroadcastConnectionState()
{
    const int state = GetConnectionUiState();

    LOG_INFO("SessionController::BroadcastConnectionState: presence=%d, connected=%d, uiState=%d "
        "(0=在线 1=正在连接 2=离线)。",
        GetCurrentPresence(), IsServerConnected() ? 1 : 0, state);

    // 主面板负责把状态转发给它持有的各个聊天窗（单聊 / 群聊），并按需禁用自己这边
    // 需要联网的入口（加好友、建群、改资料等）。
    if (m_pPanel->IsWindow())
    {
        m_pPanel->ApplyConnectionState(state);
    }
}

int CSessionController::GetCurrentPresence()
{
    // 主面板在时以状态胶囊为准 —— 托盘菜单标记的当前项、托盘悬停提示的状态文字，
    // 与用户在主面板上看到的胶囊必然是同一个值。
    if (m_pPanel->IsWindow())
    {
        return m_pPanel->GetPresence();
    }

    // 主面板不在（未登录 / 登出后已销毁），或用户主动点了"离线"：都按"离线"呈现。
    if (!m_bAlreadyLogin || m_nSessionStatus == kSessionUserGoOffline)
    {
        return kMyPresenceOffline;
    }

    // 其余情形（含登录中 / 重连中）退回期望在线状态。
    return CIUSocket::GetInstance().GetRecordOnlineStatus();
}

void CSessionController::CreateEssentialDirectories()
{
    //账号目录建在数据根目录（g_szDataRoot）下，与账号配置、最近联系人列表、本地消息库的
    //读写位置一致。2026-10-01 之前这里按程序目录（g_szHomePath）建：用户改过数据目录后，
    //数据根目录下的账号目录要等第一个头像下载落地时才被顺带建出来，在那之前本地消息库
    //打不开，账号配置也写不进去（WritePrivateProfileString 不会自动建父目录）。
    const std::wstring& account = m_lpFMGClient->m_UserMgr.m_UserInfo.m_strAccount;
    if (net::EnsureUserEssentialDirs(g_szDataRoot, account))
    {
        LOG_INFO("Essential user directories ready under data root, account=%s.",
                 EncodeUtil::UnicodeToUtf8(account).c_str());
    }
    else
    {
        LOG_ERROR("Failed to create essential user directories under data root [%s], account=%s.",
                  EncodeUtil::UnicodeToUtf8(std::wstring(g_szDataRoot)).c_str(),
                  EncodeUtil::UnicodeToUtf8(account).c_str());
    }
}

bool CSessionController::IsLoggedIn()
{
    return m_bAlreadyLogin ? true : false;
}

bool CSessionController::IsSessionEstablished()
{
    // 与「直接下发 cmd_change_presence」那条路径同源（见 OnChangeMyPresence）：
    // 已登录成功、且主面板处于正常登录态（非登录中 / 重连中 / 主动离线），
    // 才谈得上向服务端改状态。
    return m_bAlreadyLogin && m_nSessionStatus == kSessionLogin;
}

bool CSessionController::IsKickoutNoticeShowing()
{
    return m_bKickoutNoticeShowing;
}

void CSessionController::ChangePresence(int presence, int source)
{
    OnChangeMyPresence(0, (WPARAM)presence, (LPARAM)source);
}

void CSessionController::RequestLogout()
{
    // 必须 PostMessage 而不是直接调：登出要销毁主面板并进入登录窗的模态循环，
    // 不能跑在托盘菜单 TrackPopup 返回后的调用栈上（见 FMG_MSG_LOGOUT 的定义）。
    ::PostMessage(m_hHost, FMG_MSG_LOGOUT, 0, 0);
}

void CSessionController::RequestExit()
{
    DoExit();
}

} // namespace balloonmain
