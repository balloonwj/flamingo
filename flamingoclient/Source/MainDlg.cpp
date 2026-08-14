// MainDlg.cpp : implementation of the CMainDlg class
//
/////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "resource.h"

#include "MainDlg.h"
#include "BalloonMain/NotificationSound.h"   // 通话音效：回铃 / 来电铃声（循环）与接通 / 结束 / 忙音（一次性）
#include "IniFile.h"
#include "Update/UpdateManager.h"
#include "BalloonUpdate/BalloonUpdateWindow.h"   // 静默检查查出新版本时拉起升级窗
#include "net/IUProtocolData.h"
#include "net/IUSocket.h"                 // CIUSocket::RequestContacts（登录后拉取通讯录）
#include "net/MessageProtocolClient.h"    // net::MessageNotify / net::SendMessageResponse
#include "net/RemoteDesktopProtocolClient.h" // net::RDInviteResponse / RDInviteNotify / RDRelayInfo / RDStateNotify
#include "net/CallProtocolClient.h"          // net::CallInviteResponse / CallInviteNotify / CallSignalingInfo / CallStateNotify
#include "net/FolderProtocolClient.h"        // net::FolderInviteResponse / FolderInviteNotify / FolderSessionResponse / FolderStateNotify / FolderRelayInfo
#include "FolderTransfer/FolderTransferManager.h" // foldertransfer::FolderTransferManager（文件夹传输会话生命周期）
#include "RemoteDesktop/RdRelaySelfTest.h"   // remotedesktop::RunRdRelaySelfTest（--rdrelayselftest 调试入口）
#include "Call/CallMediaSession.h"           // call::CallMediaSession（接通后中转会话）/ call::CallRelayEvent
#include "Call/CallRelaySelfTest.h"          // call::RunCallRelaySelfTest（--callrelayselftest 调试入口）
#include "Call/AudioLoopback.h"              // call::RunAudioLoopbackDebug（--callaudioloop 调试入口）
#include "Call/CameraPreviewDebug.h"         // call::RunCameraPreviewDebug（--camerapreview 调试入口）
#include "Call/CameraDisplayTest.h"          // call::RunCameraDisplayTest（--cameratest 摄像头显示自包含测试）
#include "Call/H264SelfTest.h"               // call::RunH264SelfTest（--h264selftest 调试入口）
#include "Call/VideoLoopbackDebug.h"         // call::RunVideoLoopbackDebug（--videoloop 调试入口）
#include "Call/CallVideoUiDebug.h"           // call::RunCallVideoUiDebug（--callvideoui 调试入口）
#include "RemoteDesktop/DxgiScreenCapturer.h" // remotedesktop::CDxgiScreenCapturer（--dxgicap 调试入口）
#include "RemoteDesktop/RdScreenCapturer.h"   // remotedesktop::RdScreenCapturer（--dxgipipe 闭环调试入口）
#include "net/ContactsProtocolClient.h"   // net::ContactsResult
#include "net/ConversationsProtocolClient.h" // net::ConversationsResult
#include "net/GroupProtocolClient.h"        // net::CreateGroupResponse
#include "net/GroupFileProtocolClient.h"    // net::GetGroupFilesResult / GroupFilesChangedNotify
#include "net/AnnounceProtocolClient.h"     // net::AnnouncementsResult / AnnouncementNotify / AnnounceMarkReadResult（平台公告 = 消息盒子）
#include "net/FriendProtocolClient.h"       // net::SetFriendRemarkResult（设置好友备注名应答）
#include "net/ContactTagProtocolClient.h"   // net::SetContactTagsResult（设置联系人标签应答）
#include "net/StarredProtocolClient.h"      // net::SetStarredResult（「特别关注」设置应答）
#include "net/FileTransfer.h"               // net::GroupFileUploadResult / GroupFileDownloadResult
#include "net/AccountProtocolClient.h"      // net::ChangePasswordResponse
#include "net/ProfileProtocolClient.h"      // net::UpdateProfileResponse（更新个性签名应答）
#include "net/ImageTransfer.h"              // net::ImageSendProgress / net::ImageSendResult
#include "net/FileTransfer.h"               // net::FileSendProgress / net::FileSendResult / net::FileDownload*
#include "IULog.h"
#include "UserSessionData.h"
#include "GDIFactory.h"
#include "IULog.h"
#include "Startup.h"
#include "ChatDlgCommon.h"
#include "EncodeUtil.h"
#include "UIText.h"
#include "PerformanceCounter.h"
#include "net/Msg.h"
#include <memory>                                          // std::unique_ptr（托盘菜单图标保活）
#include "ImageEx.h"                                       // CImageEx（托盘菜单项图标）
#include "BalloonMain/MainPanelTrayMenu.h"                 // BuildTrayMenuModel / TrayMenuId（托盘菜单行模型）
#include "BalloonMain/MainPanelAvatar.h"                   // MakeMonogramIcon（托盘新消息闪烁的发送方头像图标）
#include "BalloonMain/MainPanelMenuIcons.h"                // CreateMenuIconImage（托盘菜单项图标渲染）
#include "BalloonMain/MainPanelStatusMenu.h"               // GetPresenceVisual（托盘悬停提示的状态文字）
#include "BalloonMain/OnlineCapability.h"                  // 连接态推导 / 托盘图标选择 / 自动重连守卫
#include "BalloonMain/AutoAwayPolicy.h"                    // DecideAutoAway / ComputeIdleMs（无操作自动离开）
#include "BalloonMain/KeepAwakePolicy.h"                   // DecideKeepAwake（禁止机器休眠）

//锁屏 / 解锁等会话事件的通知注册。只用到 WTSRegisterSessionNotification 与
//WTSUnRegisterSessionNotification 两个函数，故就地 pragma 链接，不动工程文件。
#include <wtsapi32.h>
#pragma comment(lib, "wtsapi32.lib")
#include "BalloonSettings/SettingsBinding.h"               // AutoAwayDelayMinutesFromIndex（时长下拉索引→分钟）
#include "BalloonChatWnd/ChatLinkDetect.h"                 // SetChatLinkOptionEnabled（登录后灌入链接识别开关）
#include "BalloonMain/KickoutNotice.h"                     // GetKickoutNoticeTitle / Text（被踢下线的提示文案）
#include "BalloonMain/BalloonKickoutDlg.h"                  // CBalloonKickoutDlg（贴托盘弹出的被踢下线模态提示框）
#include "BalloonMain/PopupMenuPlacement.h"                // ClampPopupMenuToWorkArea（保证菜单在桌面内）
#include "../../third-party/balloonui/balloonui/Controls/List/DuiMenu.h"  // balloonui 弹出菜单（重建托盘右键菜单）

#pragma comment(lib, "winmm.lib")

extern HWND g_hwndOwner;

#define LOGIN_TIMER_ID			  1
#define RECVCHATMSG_TIMER_ID	  2
#define EXITAPP_TIMER_ID		  4
#define RECONNECT_TIMER_ID        5
#define AUTOAWAY_TIMER_ID         6

//"无操作自动离开"的轮询间隔（毫秒）。最短的可选阈值是 5 分钟，30 秒一跳足够 ——
//最坏情况比设定时刻晚半分钟切过去，用户无感；跳得再密只是白耗电。
#define AUTOAWAY_POLL_INTERVAL_MS 30000

//自动登录期间托盘图标动画的换帧间隔（毫秒）。六帧循环播放，200 毫秒一帧，
//整轮 1.2 秒 —— 快到能看出"正在忙"，又不至于闪得刺眼。
#define LOGIN_ICON_ANIM_INTERVAL_MS 200

//「显示 / 隐藏主面板」全局热键的主键，与注册时给的 MOD_CONTROL | MOD_ALT 一起
//构成 Ctrl+Alt+D（注册见本文件 RegisterHotKey 处，响应见 OnHotKey 的 1001 分支）。
//此前它是全局变量 g_cHotKey，定义在 v1 的字体选择框 FontSelDlg.cpp 里，取值一直是
//'D'、也没有任何界面能改它；该文件已随 v1 清理删除，故就地固化为常量。
const TCHAR kShowPanelHotKey = _T('D');

//好友项信息浮动窗口宽和高
#define BUDDYINFOFLOATWND_WIDTH   300
#define BUDDYINFOFLOATWND_HEIGHT  150

//主菜单各个子菜单索引号
enum MAIN_PANEL_MEMU
{
    MAIN_PANEL_STATUS_SUBMENU_INDEX,
    MAIN_PANEL_TRAYICON_SUBMENU_INDEX,
    MAIN_PANEL_TRAYICON_SUBMENU2_INDEX,
    MAIN_PANEL_RECENT_SUBMENU_INDEX,
    MAIN_PANEL_BUDDYLIST_SUBMENU_INDEX,
    MAIN_PANEL_GROUP_SUBMENU_INDEX,
    MAIN_PANEL_BUDDYLIST_CONTEXT_SUBMENU_INDEX,
    MAIN_PANEL_MAIN_SUBMENU_INDEX,
    MAIN_PANEL_LOCK_SUBMENU_INDEX,
    MAIN_PANEL_RECENTLIST_CONTEXT_SUBMENU_INDEX,
    MAIN_PANEL_GROUPLIST_CONTEXT_SUBMENU_INDEX,
    MAIN_PANEL_BUDDYLIST_TEAM_CONTEXT_SUBMENU_INDEX,
    MAIN_PANEL_BUDDYLIST_BLANK_CONTEXT_SUBMENU_INDEX,
    MAIN_PANEL_GROUPLIST_BLANK_CONTEXT_SUBMENU_INDEX
};


BOOL CMainDlg::PreTranslateMessage(MSG* pMsg)
{
    if (pMsg->message == WM_KEYDOWN && pMsg->wParam == VK_RETURN)
    {
        return FALSE;
    }

    return CWindow::IsDialogMessage(pMsg);
}

BOOL CMainDlg::OnIdle()
{
    return FALSE;
}

CMainDlg::CMainDlg(void) : m_LoginDlg(&m_FMGClient), m_BalloonMainPanel(&m_FMGClient),
m_BalloonChatWindow(&m_FMGClient),
m_BalloonGroupChatWindow(&m_FMGClient),
m_BalloonNetSettingsWindow(&m_FMGClient),
m_BalloonRegisterWindow(&m_FMGClient),
m_BalloonProfileWindow(&m_FMGClient),
m_BalloonContactWindow(&m_FMGClient),
m_BalloonGroupManageWindow(&m_FMGClient),
m_BalloonHistoryWindow(&m_FMGClient),
m_BalloonSystemSettingDlg(&m_FMGClient)
{
    //m_BuddyChatDlg.m_lpFMGClient = &m_FMGClient;
    m_nLastMsgType = FMG_MSG_TYPE_BUDDY;
    m_nLastSenderId = 0;
    //当前通话媒体类型默认纯语音；发起 / 接听时按本次通话类型刷新（signaling 回来时据此决定是否起视频）
    m_callMediaType = net::call_media_audio;
    m_hAppIcon = NULL;
    memset(m_hLoginIcon, 0, sizeof(m_hLoginIcon));
    m_nCurLoginIcon = 0;
    m_hMsgIcon = NULL;
    m_dwLoginTimerId = 0;
    m_dwMsgTimerId = 0;
    m_dwReconnectTimerId = 0;
    m_dwAutoAwayTimerId = 0;
    m_dwLastKeepAwakeNudgeTick = 0;

    //进程启动时还没发生过自动离开（初始状态由用户在登录窗选定）。
    m_bAutoAway = false;

    m_bAlreadySendReloginRequest = false;

    //进程启动时不处于"从离线恢复在线"的过程中。
    m_bResumingOnline = false;

    m_bEnableReconnect = true;
    m_uReconnectInterval = 3000;

    m_hDlgIcon = m_hDlgSmallIcon = NULL;
    memset(&m_stAccountInfo, 0, sizeof(m_stAccountInfo));


    m_bAlreadyLogin = FALSE;

    //进程启动时没有被踢下线提示框在显示。
    m_bKickoutNoticeShowing = false;

    m_rcTrayIconRect.SetRectEmpty();

    m_nMainPanelStatus = MAINPANEL_STATUS_NOTLOGIN;
}

CMainDlg::~CMainDlg(void)
{
}

BOOL CMainDlg::OnInitDialog(CWindow wndFocus, LPARAM lInitParam)
{
    // set icons
    m_hDlgIcon = AtlLoadIconImage(IDR_MAINFRAME, LR_DEFAULTCOLOR, ::GetSystemMetrics(SM_CXICON), ::GetSystemMetrics(SM_CYICON));
    SetIcon(m_hDlgIcon, TRUE);
    m_hDlgSmallIcon = AtlLoadIconImage(IDR_MAINFRAME, LR_DEFAULTCOLOR, ::GetSystemMetrics(SM_CXSMICON), ::GetSystemMetrics(SM_CYSMICON));
    SetIcon(m_hDlgSmallIcon, FALSE);

    // register object for message filtering and idle updates
    CMessageLoop* pLoop = _Module.GetMessageLoop();
    ATLASSERT(pLoop != NULL);
    pLoop->AddMessageFilter(this);
    pLoop->AddIdleHandler(this);

    UIAddChildWindowContainer(m_hWnd);

    //把自己的句柄登记给升级窗：安装升级包之前必须让本进程先退出（更新器按 --pid 等，
    //等不到就放弃），升级窗据此句柄请本窗口关闭。不登记的话升级只能靠用户手动退出程序。
    CBalloonUpdateWindow::GetInstance().SetHostWindow(m_hWnd);

    //去除最大化按钮
    ModifyStyle(WS_MAXIMIZEBOX, 0);
    //InitUI();

    if (!m_FMGClient.InitProxyWnd())
    {
        ::MessageBox(NULL, _T("初始化代理窗口失败，Flamingo无法启动！"), _T(""), MB_OK | MB_TOPMOST);
        ::ExitProcess(0);
        return FALSE;
    }

    // 尚未登录，GetCurrentPresence 会返回离线 —— 托盘先挂灰色图标，登录成功后由
    // OnLoginResult 里的 RefreshTrayIconForStatus 换成彩色。
    LoadAppIcon(GetConnectionUiState());
    m_TrayIcon.AddIcon(m_hWnd, WM_TRAYICON_NOTIFY, 1, m_hAppIcon, _T("Flamingo未登录"));

    //"无操作自动离开"的轮询定时器，进程在世期间一直跑。之所以不等登录成功才起：
    //每一跳都会先看连接态，未登录 / 掉线时 DecideAutoAway 直接返回"什么都不做"，
    //比在登录、掉线、重连、登出各处分别起停定时器要少好几处漏改的机会。
    m_dwAutoAwayTimerId = SetTimer(AUTOAWAY_TIMER_ID, AUTOAWAY_POLL_INTERVAL_MS, NULL);

    //订阅锁屏 / 解锁等会话事件（WM_WTSSESSION_CHANGE），让"人已经走了"这件事第一时间
    //反映到在线状态上，而不是傻等空闲满 5 分钟。NOTIFY_FOR_THIS_SESSION 表示只关心本
    //会话自己的事件，不关心机器上其他用户会话。注册失败不影响主流程 —— 空闲轮询那条
    //路照常兜底，只是锁屏后要多等一会儿才切走，故只记一条日志、不打断启动。
    if (!::WTSRegisterSessionNotification(m_hWnd, NOTIFY_FOR_THIS_SESSION))
    {
        LOG_WARNING("WTSRegisterSessionNotification failed (error %u); "
            "lock/unlock will fall back to the idle poll.",
            (unsigned int)::GetLastError());
    }

    //创建Users目录
    CString strAppPath(g_szHomePath);
    CString strUsersDirectory(strAppPath);
    strUsersDirectory += _T("Users");
    if (!Hootina::CPath::IsDirectoryExist(strUsersDirectory))
        Hootina::CPath::CreateDirectory(strUsersDirectory);

    // 加载系统表情列表
    tstring strFileName = Hootina::CPath::GetAppPath() + _T("Face\\FaceConfig.xml");
    m_FaceList.LoadConfigFile(strFileName.c_str());

    // 加载登录帐号列表
    strFileName = Hootina::CPath::GetAppPath() + _T("Users\\LoginAccountList.dat");
    m_LoginAccountList.LoadFile(strFileName.c_str());

#ifdef _DEBUG
    // 调试入口：命令行带 --mainpanel 时跳过登录，直接用 mock 数据弹出
    // 新版 balloonui 主面板，便于开发期截图核对主面板视觉。正式发布
    // 构建（非 _DEBUG）不含此分支。
    if (::_tcsstr(::GetCommandLine(), _T("--mainpanel")) != NULL)
    {
        m_BalloonMainPanel.CreateAndShow(m_hWnd);

        // 再带 --connecting / --offline 时把对应连接态推给面板，便于截图核对
        // "掉线重连中"与"用户主动离线"两种状态下的胶囊文字与头像置灰。不带则
        // 保持默认的在线态（彩色头像 + "在线"胶囊）。
        if (::_tcsstr(::GetCommandLine(), _T("--connecting")) != NULL)
        {
            m_BalloonMainPanel.ApplyConnectionState((int)balloonmain::kConnUiConnecting);
        }
        else if (::_tcsstr(::GetCommandLine(), _T("--offline")) != NULL)
        {
            m_BalloonMainPanel.ApplyConnectionState((int)balloonmain::kConnUiOffline);
        }

        ShowWindow(SW_HIDE);
        return TRUE;
    }
    // 调试入口：命令行带 --chatwindow 时跳过登录，直接弹出新版气泡式
    // 单聊窗口（mock 数据），便于开发期截图核对单聊窗视觉。
    if (::_tcsstr(::GetCommandLine(), _T("--chatwindow")) != NULL)
    {
        m_BalloonChatWindow.CreateAndShow(m_hWnd);
        ShowWindow(SW_HIDE);
        return TRUE;
    }
    // 调试入口：命令行带 --groupchatwindow 时跳过登录，直接弹出新版气泡式
    // 群聊窗口（mock 数据），便于开发期截图核对群聊窗视觉。
    if (::_tcsstr(::GetCommandLine(), _T("--groupchatwindow")) != NULL)
    {
        m_BalloonGroupChatWindow.CreateAndShow(m_hWnd);
        ShowWindow(SW_HIDE);
        return TRUE;
    }

    // 调试入口：命令行带 --history 时跳过登录，直接弹出新版历史消息记录窗口
    //（mock 数据），便于开发期核对消息记录窗视觉。
    if (::_tcsstr(::GetCommandLine(), _T("--history")) != NULL)
    {
        m_BalloonHistoryWindow.CreateAndShow(m_hWnd);
        ShowWindow(SW_HIDE);
        return TRUE;
    }
    // 调试入口：命令行带 --netsettings 时跳过登录，直接弹出新版网络设置
    // 窗口（读写真实 flamingo.ini），便于开发期核对网络设置窗视觉与
    // 三组服务器切换。
    if (::_tcsstr(::GetCommandLine(), _T("--netsettings")) != NULL)
    {
        m_BalloonNetSettingsWindow.CreateAndShow(m_hWnd);
        ShowWindow(SW_HIDE);
        return TRUE;
    }
    // 调试入口：命令行带 --register 时跳过登录，直接弹出新版注册对话框，
    // 便于开发期核对注册窗（三步流程）视觉。
    if (::_tcsstr(::GetCommandLine(), _T("--register")) != NULL)
    {
        m_BalloonRegisterWindow.CreateAndShow(m_hWnd);
        ShowWindow(SW_HIDE);
        return TRUE;
    }
    // 调试入口：命令行带 --profile 时跳过登录，直接弹出新版个人信息
    // 对话框（mock 数据），便于开发期核对个人信息窗视觉与查看/编辑切换。
    if (::_tcsstr(::GetCommandLine(), _T("--profile")) != NULL)
    {
        m_BalloonProfileWindow.CreateAndShow(m_hWnd);
        ShowWindow(SW_HIDE);
        return TRUE;
    }
    // 调试入口：命令行带 --contact 时跳过登录，直接弹出新版联系人资料
    // 对话框（mock 数据），便于开发期核对联系人窗视觉。
    if (::_tcsstr(::GetCommandLine(), _T("--contact")) != NULL)
    {
        m_BalloonContactWindow.CreateAndShow(m_hWnd);
        ShowWindow(SW_HIDE);
        return TRUE;
    }
    // 调试入口：命令行带 --groupmanage 时跳过登录，直接弹出新版群管理
    // 窗口（mock 数据），便于开发期核对群管理窗视觉与各页切换。
    if (::_tcsstr(::GetCommandLine(), _T("--groupmanage")) != NULL)
    {
        m_BalloonGroupManageWindow.CreateAndShow(m_hWnd);
        ShowWindow(SW_HIDE);
        return TRUE;
    }
    // 调试入口：命令行带 --syssettings 时跳过登录，直接弹出新版系统设置
    // 对话框（配置取默认值），便于开发期核对系统设置窗视觉与三页切换。
    if (::_tcsstr(::GetCommandLine(), _T("--syssettings")) != NULL)
    {
        m_BalloonSystemSettingDlg.OpenSettings(m_hWnd);
        ShowWindow(SW_HIDE);
        return TRUE;
    }
    // 调试入口：命令行带 --about 时跳过登录，直接弹出新版关于对话框，
    // 便于开发期核对关于窗视觉（品牌区 app 图标 + 标题栏进程 id + 五个功能行）。
    if (::_tcsstr(::GetCommandLine(), _T("--about")) != NULL)
    {
        m_BalloonAboutDlg.OpenAbout(m_hWnd);
        ShowWindow(SW_HIDE);
        return TRUE;
    }
    // 调试入口：命令行带 --updatewindow 时跳过登录，直接弹出升级窗，便于开发期核对
    // 它的视觉与四种形态。默认呈现"发现新版本"；再带 --forced 看强制升级形态（隐藏
    // 「稍后」、去掉关闭按钮），带 --latest 看"已是最新"，带 --failed 看检查失败。
    // 这里喂的是构造出来的结果，不连服务器，故不受联调环境影响。
    if (::_tcsstr(::GetCommandLine(), _T("--updatewindow")) != NULL)
    {
        update::UpdateTaskResult mock;
        mock.m_valid       = true;
        mock.m_transportOk = true;
        mock.m_code        = 0;

        if (::_tcsstr(::GetCommandLine(), _T("--failed")) != NULL)
        {
            mock.m_transportOk = false;
            mock.m_errmsg      = "check-update timeout";
        }
        else if (::_tcsstr(::GetCommandLine(), _T("--latest")) != NULL)
        {
            mock.m_hasUpdate     = false;
            mock.m_latestVersion = "3.2.1";
        }
        else
        {
            mock.m_hasUpdate     = true;
            mock.m_packageUsable = true;
            mock.m_latestVersion = "3.3.0";
            mock.m_size          = 38219776;
            mock.m_releaseNote   = "1. 新增群接龙，支持在群里发起接龙并统计参与人；\r\n"
                                   "2. 修复了聊天记录搜索在消息量较大时偶尔卡顿的问题；\r\n"
                                   "3. 优化了文件传输的断点续传逻辑，弱网下更稳定；\r\n"
                                   "4. 修复若干已知问题。";
            mock.m_forced = (::_tcsstr(::GetCommandLine(), _T("--forced")) != NULL);

            //--emptynote：把更新说明置空，用来复现"发布时没填说明"那条路径 ——
            //说明区整块隐藏后，下面各控件的位置会变，容易暴露重排相关的问题。
            if (::_tcsstr(::GetCommandLine(), _T("--emptynote")) != NULL)
            {
                mock.m_releaseNote.clear();
            }
        }

        CBalloonUpdateWindow::GetInstance().ShowResult(mock);

        //再带 --progress 时用假数据把下载进度演一遍，便于目检进度条与进度文字。
        //走的是与真实下载相同的消息通路，观感一致。
        if (::_tcsstr(::GetCommandLine(), _T("--progress")) != NULL && mock.m_packageUsable)
        {
            CBalloonUpdateWindow::GetInstance().StartMockProgress(mock.m_size);
        }

        ShowWindow(SW_HIDE);
        return TRUE;
    }
    // 调试入口：命令行带 --kickout 时跳过登录，直接弹出"被踢下线"提示框，便于开发期
    // 核对提示框视觉与落点（贴着托盘图标弹出）。原因默认取"异地登录"，可再带
    // --reason=N 指定别的原因（N 取值见 BalloonMain/KickoutNotice.h 的 KickoutReasonValue），
    // 用来核对各条文案的换行与窗口高度。关掉提示框即退出程序，不走回登录界面那条路。
    if (::_tcsstr(::GetCommandLine(), _T("--kickout")) != NULL)
    {
        int nReason = balloonmain::kKickoutReasonOtherDevice;
        LPCTSTR pszReason = ::_tcsstr(::GetCommandLine(), _T("--reason="));
        if (pszReason != NULL)
        {
            nReason = ::_ttoi(pszReason + _tcslen(_T("--reason=")));
        }

        // 托盘图标此刻已经挂上（本函数前面 AddIcon 过），取得到它的位置，落点与实战一致。
        RECT rcTrayIcon;
        ::SetRectEmpty(&rcTrayIcon);
        if (!m_TrayIcon.GetTrayIconRect(&rcTrayIcon))
        {
            ::SetRectEmpty(&rcTrayIcon);
        }

        ShowWindow(SW_HIDE);

        CBalloonKickoutDlg dlg;
        dlg.DoModal(m_hWnd, nReason, rcTrayIcon);

        CloseDialog(IDOK);
        return TRUE;
    }
    // 调试入口：命令行带 --groupinfo 时跳过登录，直接弹出新版群资料对话框
    // （假数据，对照 design/群资料.png），便于开发期核对版式、开关拨动、群名片
    // 整行点击就地编辑等交互。
    if (::_tcsstr(::GetCommandLine(), _T("--groupinfo")) != NULL)
    {
        balloonmain::GroupInfoViewData seed;
        seed.groupid     = 700001;
        seed.groupName   = _T("FlamingoIM 项目组");
        seed.memberCount = 28;
        seed.ownerName   = _T("谢知行");
        seed.announcement= _T("v3.2 版本即将发布，全员准备。");
        seed.myCard      = _T("林知夏");
        seed.mute        = false;
        seed.pinned      = true;
        // 调试入口无宿主主面板：pHost 传 NULL，开关 / 退群退化为直接发协议、不更新缓存。
        m_BalloonGroupInfoDlg.OpenForGroup(m_hWnd, &m_FMGClient, NULL, seed);
        ShowWindow(SW_HIDE);
        return TRUE;
    }
    // 调试入口：命令行带 --rddlg 时跳过登录，直接弹出远程桌面会话提示对话框（假数据），
    // 便于核对它贴桌面右下角的落点、置顶效果与标题栏按钮，不必真的发起一次远程桌面。
    // 默认弹被控端那一路（"XXX 请求远程控制你的电脑"，拒绝 / 接受两个按钮）；
    // 再带 --calling 则弹发起方那一路（"正在呼叫 XXX…"，只有取消按钮）。
    // 两路的显示收尾都走 ShowAtWorkAreaCorner，位置与置顶行为一致。
    if (::_tcsstr(::GetCommandLine(), _T("--rddlg")) != NULL)
    {
        const bool bCalling = (::_tcsstr(::GetCommandLine(), _T("--calling")) != NULL);
        if (bCalling)
        {
            m_rdDlg.OpenCalling(m_hWnd, _T("林知夏"), 700002);
        }
        else
        {
            m_rdDlg.OpenAuth(m_hWnd, _T("林知夏"), std::string("mock-rd-session"), 700002);
        }
        ShowWindow(SW_HIDE);
        return TRUE;
    }
    // 调试入口：命令行带 --imageviewer 时跳过登录，直接弹出新版聊天图片放大
    // 看图窗（弹文件选择框挑一张本地图片），便于开发期核对看图窗视觉与缩放 /
    // 旋转 / 平移交互。
    if (::_tcsstr(::GetCommandLine(), _T("--imageviewer")) != NULL)
    {
        m_BalloonImageViewerWindow.CreateAndShow(m_hWnd);
        ShowWindow(SW_HIDE);
        return TRUE;
    }
    // 调试入口：命令行带 --rdpselftest 时跳过登录，直接弹出远程桌面引擎自环
    // 测试窗口（抓本机屏→编码→解码→绘制），便于开发期目视确认引擎闭环出画面。
    if (::_tcsstr(::GetCommandLine(), _T("--rdpselftest")) != NULL)
    {
        m_RdSelfTestWindow.CreateAndShow(m_hWnd);
        ShowWindow(SW_HIDE);
        return TRUE;
    }
    // 调试入口：命令行带 --rdrelayselftest 时跳过登录，进程内开两条 relay 连接连
    // imgserver 中转端口(20003)，互发字节验证「连接→握手→双向透传」整条中转通路。
    // 需 imgserver 在跑、且其 relaytokenkey 为 flamingo-relay-token-secret（见 imgserver.conf）。
    if (::_tcsstr(::GetCommandLine(), _T("--rdrelayselftest")) != NULL)
    {
        bool selfTestOk = false;
        std::string report = remotedesktop::RunRdRelaySelfTest(
            "127.0.0.1", 20003, "flamingo-relay-token-secret", selfTestOk);
        CString text(CA2W(report.c_str(), CP_UTF8));
        ::MessageBox(m_hWnd, text, _T("远程桌面中转自环自测"),
                     MB_OK | (selfTestOk ? MB_ICONINFORMATION : MB_ICONWARNING));
        ShowWindow(SW_HIDE);
        return TRUE;
    }
    // 调试入口：命令行带 --callrelayselftest 时跳过登录，进程内开两条 relay 连接（主叫 /
    // 被叫，同 sessionid 相反 role）连 imgserver 中转端口(20003)，双向互发测试帧验证
    // 「连接→握手→透传」整条通话中转通路。需 imgserver 在跑、且其 relaytokenkey 为
    // flamingo-relay-token-secret（见 imgserver.conf）。
    if (::_tcsstr(::GetCommandLine(), _T("--callrelayselftest")) != NULL)
    {
        bool selfTestOk = false;
        std::string report = call::RunCallRelaySelfTest(
            "127.0.0.1", 20003, "flamingo-relay-token-secret", selfTestOk);
        CString text(CA2W(report.c_str(), CP_UTF8));
        ::MessageBox(m_hWnd, text, _T("通话中转自环自测"),
                     MB_OK | (selfTestOk ? MB_ICONINFORMATION : MB_ICONWARNING));
        ShowWindow(SW_HIDE);
        return TRUE;
    }
    // 调试入口：命令行带 --callaudioloop 时跳过登录，做本地音频回环自测：WASAPI 麦克风采集
    // → Opus 编码 → 解码 → WASAPI 扬声器播放（对着麦克风说话应能听到自己）。弹框运行期间
    // 回环持续运行，点确定停止。需默认麦克风/扬声器可用且采集率为 Opus 支持率（常 48kHz）。
    if (::_tcsstr(::GetCommandLine(), _T("--callaudioloop")) != NULL)
    {
        call::RunAudioLoopbackDebug();
        ShowWindow(SW_HIDE);
        return TRUE;
    }
    // 调试入口：命令行带 --camerapreview 时跳过登录，打开默认摄像头，把采集到的画面实时
    // 显示在预览窗口里（验证 MF 摄像头采集 + NV12→BGRA 显示这层）。关窗即停。需有摄像头。
    if (::_tcsstr(::GetCommandLine(), _T("--camerapreview")) != NULL)
    {
        call::RunCameraPreviewDebug();
        ShowWindow(SW_HIDE);
        return TRUE;
    }
    // 调试入口：命令行带 --cameratest 时跳过登录，运行自包含的摄像头显示测试——全程打日志到
    // E:\flamingo\cameratest.log，逐步定位「摄像头数据显示不出来」卡在哪一步（初始化/枚举/打开/
    // 协商/取帧/绘制）。弹窗显示画面，关窗弹统计框。
    if (::_tcsstr(::GetCommandLine(), _T("--cameratest")) != NULL)
    {
        call::RunCameraDisplayTest();
        ShowWindow(SW_HIDE);
        return TRUE;
    }
    // 调试入口：命令行带 --h264selftest 时跳过登录，做 H.264 编解码自测：合成 NV12 灰度
    // 渐变 → MF H.264 编码 → 解码 → 首帧落 BMP（E:\flamingo\h264_selftest_decoded.bmp）。
    // 不需要摄像头，验证 MF H.264 编解码 round-trip。
    if (::_tcsstr(::GetCommandLine(), _T("--h264selftest")) != NULL)
    {
        call::RunH264SelfTest();
        ShowWindow(SW_HIDE);
        return TRUE;
    }
    // 调试入口：命令行带 --videoloop 时跳过登录，做本地视频回环：摄像头采集 → H.264 编码 →
    // 回灌解码 → 首个解码帧落 BMP（E:\flamingo\videoloop_decoded.bmp），验证整条视频管线。
    if (::_tcsstr(::GetCommandLine(), _T("--videoloop")) != NULL)
    {
        call::RunVideoLoopbackDebug();
        ShowWindow(SW_HIDE);
        return TRUE;
    }
    // 调试入口：命令行带 --callvideoui 时跳过登录，以"视频通话中"形态打开通话窗，并用本机
    // 摄像头回环填充画面（本地预览=原始采集，远端区=自采→编码→解码后的画面），便于单机目检
    // 通话窗的视频布局与画面渲染（无需服务器 / 第二台机器）。关窗即停。
    if (::_tcsstr(::GetCommandLine(), _T("--callvideoui")) != NULL)
    {
        call::RunCallVideoUiDebug();
        ShowWindow(SW_HIDE);
        return TRUE;
    }
    // 调试入口：命令行带 --rdbar 时跳过登录，直接在屏幕顶部弹出被控端受控提示条，
    // 便于开发期目视核对其外观 / 布局 / 结束按钮（点"结束控制"会关掉提示条）。
    if (::_tcsstr(::GetCommandLine(), _T("--rdbar")) != NULL)
    {
        m_rdControlledBar.Show(L"张三", m_hWnd);
        ShowWindow(SW_HIDE);
        return TRUE;
    }
    // 调试入口：命令行带 --rdtoolbar 时跳过登录，直接在主显示器顶部弹出控制端全屏
    // 工具条，便于目视核对其外观 / 布局 / 两个按钮（实际运行时它只在观看窗全屏态出现）。
    if (::_tcsstr(::GetCommandLine(), _T("--rdtoolbar")) != NULL)
    {
        //调试期单弹一个工具条实例看外观；用静态对象使其在函数返回后仍存活。命令消息传
        //WM_NULL（点了按钮也只是给本窗口投递一条无副作用的空消息）。
        static CRdViewerToolbar s_dbgToolbar;
        HMONITOR mon = ::MonitorFromWindow(m_hWnd, MONITOR_DEFAULTTOPRIMARY);
        s_dbgToolbar.ShowOnMonitor(mon, m_hWnd, WM_NULL);
        ShowWindow(SW_HIDE);
        return TRUE;
    }
    // 调试入口：命令行带 --dxgicap 时跳过登录，用 DXGI Desktop Duplication 抓一帧屏幕
    // 存成 exe 同目录的 dxgicap.bmp，弹框报结果。用于在本机显卡上验证 DuplicateOutput
    // 能否跑通（不同显卡 / 虚拟机 / RDP 下行为差异大，先探路）。
    if (::_tcsstr(::GetCommandLine(), _T("--dxgicap")) != NULL)
    {
        wchar_t path[MAX_PATH] = { 0 };
        ::GetModuleFileNameW(NULL, path, MAX_PATH);
        wchar_t* slash = wcsrchr(path, L'\\');
        if (slash != NULL)
            wcscpy(slash + 1, L"dxgicap.bmp");
        else
            wcscpy(path, L"dxgicap.bmp");

        std::wstring msg;
        bool ok = remotedesktop::CDxgiScreenCapturer::DebugCaptureToBmp(path, msg);
        ::MessageBoxW(m_hWnd, msg.c_str(), L"DXGI 抓屏自测",
                      MB_OK | (ok ? MB_ICONINFORMATION : MB_ICONWARNING));
        ShowWindow(SW_HIDE);
        return TRUE;
    }
    // 调试入口：命令行带 --dxgipipegdi 时强制走 GDI 降级后端跑同一闭环，存 dxgipipegdi.bmp，
    // 用于本地验证 GDI 抓取（增量3 写的降级后端，此前只跑过 DXGI 主路径）出图正确。
    // 须在 --dxgipipe 之前判断（--dxgipipegdi 含 "--dxgipipe" 子串）。
    if (::_tcsstr(::GetCommandLine(), _T("--dxgipipegdi")) != NULL)
    {
        wchar_t path[MAX_PATH] = { 0 };
        ::GetModuleFileNameW(NULL, path, MAX_PATH);
        wchar_t* slash = wcsrchr(path, L'\\');
        if (slash != NULL)
            wcscpy(slash + 1, L"dxgipipegdi.bmp");
        else
            wcscpy(path, L"dxgipipegdi.bmp");

        std::wstring msg;
        bool ok = remotedesktop::RdScreenCapturer::DebugPipelineToBmp(path, msg, true);   // 强制 GDI 后端
        ::MessageBoxW(m_hWnd, msg.c_str(), L"GDI 降级抓屏链路闭环自测",
                      MB_OK | (ok ? MB_ICONINFORMATION : MB_ICONWARNING));
        ShowWindow(SW_HIDE);
        return TRUE;
    }
    // 调试入口：命令行带 --dxgipipe 时跳过登录，本地走通整条抓屏链路（DXGI/GDI 抓 →
    // 16 位编码 → DIFF → 解码 → 绘制）并把还原画面存成 exe 同目录的 dxgipipe.bmp，
    // 不连服务器即可肉眼确认编码链路出图正确（方向 / 颜色 / 光标）。后端走 DXGI 还是
    // 降级 GDI 看客户端日志。
    if (::_tcsstr(::GetCommandLine(), _T("--dxgipipe")) != NULL)
    {
        wchar_t path[MAX_PATH] = { 0 };
        ::GetModuleFileNameW(NULL, path, MAX_PATH);
        wchar_t* slash = wcsrchr(path, L'\\');
        if (slash != NULL)
            wcscpy(slash + 1, L"dxgipipe.bmp");
        else
            wcscpy(path, L"dxgipipe.bmp");

        std::wstring msg;
        bool ok = remotedesktop::RdScreenCapturer::DebugPipelineToBmp(path, msg);
        ::MessageBoxW(m_hWnd, msg.c_str(), L"DXGI 抓屏链路闭环自测",
                      MB_OK | (ok ? MB_ICONINFORMATION : MB_ICONWARNING));
        ShowWindow(SW_HIDE);
        return TRUE;
    }
#endif

    //在这个函数里面显示登录对话框
    //如果登录对话框不是走正常登录,则主对话框也不应该创建出来
    if (!StartLogin(m_LoginAccountList.IsAutoLogin()))
        return TRUE;

    //TODO: 根据账户名设置用户头像
    UINT nUTalkUin = _tcstoul(m_stAccountInfo.szUser, NULL, 10);

    // PR7: CMainDlg 主窗口不再显示。
    // 旧自绘 UI shell 已废弃,CMainDlg 退居幕后只作 IM 消息中枢
    // (收发消息 / 托盘 / 重连 / 管理聊天窗) 。登录成功后由 OnLoginResult
    // 创建并显示 BalloonMainPanel,后者成为唯一可见主面板。
    ShowWindow(SW_HIDE);
    ModifyStyleEx(WS_EX_APPWINDOW, 0);



    return TRUE;
}

void CMainDlg::OnSysCommand(UINT nID, CPoint pt)
{
    if (nID == SC_MINIMIZE)
    {
        ShowWindow(SW_HIDE);
        return;
    }

    SetMsgHandled(FALSE);
}

void CMainDlg::OnTimer(UINT_PTR nIDEvent)
{
    m_TrayIcon.OnTimer(nIDEvent);

    if (nIDEvent == m_dwMsgTimerId)		// 来消息闪烁动画
    {
        static BOOL bSwitch = FALSE;

        if (m_dwLoginTimerId != NULL)	// 未登录成功
        {
            // 登录动画定时器还在，说明本轮登录尚未收到应答。此时直接返回会让托盘
            // 一声不响地不闪，记一笔，免得排查时以为是闪烁逻辑本身坏了。
            if (!m_bTrayFlashTickLogged)
            {
                m_bTrayFlashTickLogged = true;
                LOG_WARNING("托盘闪烁 tick 被跳过：登录动画定时器仍在（loginTimerId=%u）。",
                            (UINT)m_dwLoginTimerId);
            }
            return;
        }

        // 两个相位在"应用图标"与"发送方头像"之间来回切。灭的那半拍原先传的是
        // NULL —— 各版本 Windows 对 NIM_MODIFY 带空图标的处理并不一致，有的会
        // 原样留着上一个图标不动，那样看起来就是根本没闪。换成始终给一个有效
        // 图标，任何版本上都能看出两幅画面在交替。
        // 头像图标万一没生成出来（m_hMsgIcon 为空），两个相位都退回应用图标 ——
        // 宁可不闪，也不要把托盘图标闪成空白。
        bSwitch = !bSwitch;
        BOOL bOk = FALSE;
        if (bSwitch || m_hMsgIcon == NULL)
            bOk = m_TrayIcon.ModifyIcon(m_hAppIcon, _T(""));
        else
            bOk = m_TrayIcon.ModifyIcon(m_hMsgIcon, _T(""));

        // 每轮闪烁只在第一次 tick 记一条：确认定时器确实在跑、Shell_NotifyIcon
        // 是否成功、以及那一刻用的头像图标是不是空的（空图标会闪成"什么都没有"）。
        if (!m_bTrayFlashTickLogged)
        {
            m_bTrayFlashTickLogged = true;
            LOG_INFO("托盘闪烁首次 tick: ModifyIcon 返回=%d, 头像图标=%p, "
                     "集合内会话数=%d。",
                     bOk ? 1 : 0, m_hMsgIcon, m_trayFlash.GetConversationCount());
        }
    }
    else if (nIDEvent == m_dwLoginTimerId)	// 登录动画
    {
        m_TrayIcon.ModifyIcon(m_hLoginIcon[m_nCurLoginIcon], _T(""));
        m_nCurLoginIcon++;
        if (m_nCurLoginIcon >= 6)
            m_nCurLoginIcon = 0;
    }
    else if (nIDEvent == m_dwReconnectTimerId)
    {
        // 用户主动离线后必须停手。这里原先只看定时器在不在、不看登录态，于是"掉线
        // 重连过程中用户选了离线"这条路上，定时器会一直把用户连回服务器 —— 与
        // OnNetError 里的判断共用同一个守卫函数，避免再出现一处判了另一处没判。
        const bool userWentOffline = (m_nMainPanelStatus == MAINPANEL_STATUS_USERGOOFFLINE);
        if (!balloonmain::ShouldAutoReconnect(userWentOffline, m_bEnableReconnect))
        {
            LOG_INFO("重连定时器触发但不重连：userWentOffline=%d, enableReconnect=%d。",
                userWentOffline ? 1 : 0, m_bEnableReconnect ? 1 : 0);
            KillTimer(m_dwReconnectTimerId);
            m_dwReconnectTimerId = 0;
            return;
        }

        if (!m_bAlreadySendReloginRequest)
        {
            // 重连期间托盘按当前状态取图标（此刻连接已断，图标为灰），并弹一次气泡提示。
            LoadAppIcon(GetConnectionUiState());
            m_TrayIcon.ModifyIcon(m_hAppIcon, BuildTrayTipText(), 1, TRUE,
                _T("温馨提示"), _T("您已经掉线，正在帮您重连......"), 3000);

            m_FMGClient.Login();
        }
    }
    else if (nIDEvent == m_dwAutoAwayTimerId)
    {
        CheckAutoAway();
        // 「禁止机器休眠」复用同一个定时器：它同样是 30 秒粒度的周期性处理，无需另挂一个。
        // 两者互斥，同一次触发中至多只有一个会真正执行动作。
        CheckKeepAwake();
    }
}

void CMainDlg::CheckKeepAwake()
{
    // 与自动离开一样，每次触发都重新读取配置：用户在设置中改动开关后，下一次触发
    // （最多 30 秒）即生效，设置窗口不必向此处推送通知。
    const CUserConfig& cfg = m_FMGClient.m_UserConfig;

    balloonmain::KeepAwakeInput input;
    input.m_enabled       = (cfg.IsEnableKeepAwake() != FALSE);
    // 未登录时不占用系统电源请求 —— 停留在登录界面的客户端没有理由阻止整台机器休眠。
    input.m_loggedIn      = m_bAlreadyLogin;
    input.m_holding       = m_KeepAwakeSession.IsHolding();
    input.m_nowTick       = ::GetTickCount();
    input.m_lastNudgeTick = m_dwLastKeepAwakeNudgeTick;

    switch (balloonmain::DecideKeepAwake(input))
    {
    //刚开启功能（或刚登录）：获取系统电源请求，并立即合成一次输入事件。若不立即合成，
    //首次合成要等满一个间隔才会发生，这段间隔足以让超时较短的锁屏策略生效。
    case balloonmain::kKeepAwakeAcquire:
        balloonmain::CKeepAwakeSession::LogIdlePolicySnapshot();
        if (m_KeepAwakeSession.Acquire())
        {
            m_KeepAwakeSession.Nudge();
            m_dwLastKeepAwakeNudgeTick = ::GetTickCount();
        }
        break;

    //开关刚被关闭，或已退出登录：释放电源请求，本机恢复正常的休眠与锁屏行为。
    case balloonmain::kKeepAwakeRelease:
        m_KeepAwakeSession.Release();
        m_dwLastKeepAwakeNudgeTick = 0;
        break;

    //持有期间的常规处理：距上次合成已满一个间隔，再合成一次以重置输入空闲计时器。
    case balloonmain::kKeepAwakeNudge:
        m_KeepAwakeSession.Nudge();
        m_dwLastKeepAwakeNudgeTick = ::GetTickCount();
        break;

    //功能未开启（默认状态，多数情况下都走这条分支），或距上次合成尚未满一个间隔：
    //不执行任何动作。
    case balloonmain::kKeepAwakeNone:
    default:
        break;
    }
}

bool CMainDlg::BuildAutoAwayInput(balloonmain::AutoAwayInput& input)
{
    // 每次都重新读一次配置：这样用户在系统设置里改开关 / 改时长，下一次判定就生效，
    // 设置窗不必再往这边推送通知。
    const CUserConfig& cfg = m_FMGClient.m_UserConfig;

    // 全系统空闲时长。GetLastInputInfo 取的是整台机器最后一次键鼠输入的时刻，跨所有
    // 程序 —— 用户在浏览器里忙活也不会被判成空闲，这正是"电脑无操作"该有的口径。
    LASTINPUTINFO lii = {};
    lii.cbSize = sizeof(lii);
    if (!::GetLastInputInfo(&lii))
    {
        // 取不到就这次不判。不记日志：定时器每 30 秒一跳，真出问题会刷屏。
        return false;
    }

    input.m_presence    = GetCurrentPresence();
    input.m_isAutoAway  = m_bAutoAway;
    input.m_idleMs      = balloonmain::ComputeIdleMs(::GetTickCount(), lii.dwTime);
    input.m_thresholdMs = (DWORD)balloonsettings::AutoAwayDelayMinutesFromIndex(
                              (int)cfg.GetAutoAwayDelayIndex()) * 60 * 1000;
    // 连接就绪的判据与"直接发 cmd_change_presence"那条路径同源（见 OnChangeMyPresence）：
    // 已登录且主面板处于正常登录态，才谈得上改状态。
    input.m_connected   = (m_bAlreadyLogin && m_nMainPanelStatus == MAINPANEL_STATUS_LOGIN);
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

void CMainDlg::ApplyAutoAwayAction(int action, const balloonmain::AutoAwayInput& input,
                                   const char* pszTrigger)
{
    if (action == balloonmain::kAutoAwayNone)
    {
        return;
    }

    const int target = (action == balloonmain::kAutoAwayGoAway)
                     ? kMyPresenceAway
                     : kMyPresenceOnline;

    LOG_INFO("auto-away[%s]: idle %u sec (threshold %u sec), presence %d -> %d.",
        pszTrigger,
        (unsigned int)(input.m_idleMs / 1000), (unsigned int)(input.m_thresholdMs / 1000),
        input.m_presence, target);

    // 走与手动切换同一个漏斗，胶囊、托盘图标、协议下发全都自动跟上；lParam 标明这次
    // 是自动切换，OnChangeMyPresence 据此维护 m_bAutoAway。
    OnChangeMyPresence(0, (WPARAM)target, (LPARAM)kPresenceChangeAuto);
}

void CMainDlg::CheckAutoAway()
{
    balloonmain::AutoAwayInput input;
    if (!BuildAutoAwayInput(input))
    {
        return;
    }

    ApplyAutoAwayAction(balloonmain::DecideAutoAway(input), input, "idle-poll");
}

LRESULT CMainDlg::OnSessionChange(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/)
{
    // 锁屏那一刻人肯定已经不在了，不必再等空闲满阈值 —— 否则同事在随后的 5 分钟里
    // 看到的仍是"在线"，发了消息以为你马上就回。解锁则立刻切回在线（前提是这个
    // "离开"本来就是自动切过去的，用户自己选的离开不动）。
    const int kind = balloonmain::ClassifySessionEvent((int)wParam);

    // 无条件先输出一条。这条日志是排查「禁止机器休眠」时"是否发生过锁屏"的<u>直接证据</u>：
    // 开启该功能并静置一段时间后，日志中若始终没有出现 WTS_SESSION_LOCK（状态码 0x7），
    // 即可确认锁屏确实被阻止，无需推断。同时带上当时的功能开关状态与全系统空闲时长，
    // 使这一条日志自身即可说明上下文。
    LOG_INFO("session event: code=0x%X, kind=%d (0 ignore / 1 user left / 2 user back), "
             "keepAwake=%d, holdingPowerRequest=%d, systemIdle %u sec.",
             (unsigned int)wParam, kind,
             (int)(m_FMGClient.m_UserConfig.IsEnableKeepAwake() != FALSE),
             (int)m_KeepAwakeSession.IsHolding(),
             (unsigned int)(balloonmain::CKeepAwakeSession::QuerySystemIdleMs() / 1000));

    if (kind == balloonmain::kSessionEventNone)
    {
        // 登录 / 注销 / 远程协助等与"人在不在电脑前"无关的会话事件，忽略。
        return 0;
    }

    balloonmain::AutoAwayInput input;
    if (!BuildAutoAwayInput(input))
    {
        return 0;
    }

    ApplyAutoAwayAction(balloonmain::DecideAutoAwayOnSessionEvent(input, kind), input,
                        (kind == balloonmain::kSessionEventLeave) ? "session-lock" : "session-unlock");
    return 0;
}

void CMainDlg::OnSize(UINT nType, CSize size)
{
    // CMainDlg 主窗口已隐藏、退居幕后只作消息中枢；原先这里对旧自绘主面板各控件的
    // DeferWindowPos 布局代码已随旧控件一并移除。保持 SetMsgHandled(FALSE) 走默认处理。
    SetMsgHandled(FALSE);
}

void CMainDlg::OnHotKey(int nHotKeyID, UINT uModifiers, UINT uVirtKey)
{
    switch (nHotKeyID)
    {
    case 1001:
    {
        // PR7: 热键 Ctrl+Alt+D 切换主面板可见性。CMainDlg 主窗口始终隐藏,
        // 改判 BalloonMainPanel 可见性,等价地切换该窗口。
        if (m_BalloonMainPanel.IsWindow()
            && ::IsWindowVisible(m_BalloonMainPanel.m_hWnd))
        {
            ::ShowWindow(m_BalloonMainPanel.m_hWnd, SW_HIDE);
        }
        else
        {
            OnTrayIconNotify(WM_TRAYICON_NOTIFY, NULL, WM_LBUTTONUP);
        }
    }
    break;
    }
}

void CMainDlg::OnClose()
{
    //本窗口是常态隐藏的消息中枢，用户看不见它，正常流程里也没有任何代码给它发
    //WM_CLOSE —— 主面板点关闭走的是自己的关闭确认框（CBalloonClosePromptDlg），
    //决定退出时投的是 WM_CLOSE_MAINDLG。所以走到这里只剩系统关机、任务管理器
    //「结束任务」这类外部来源，此时直接退出即可。
    //
    //这里原先会弹「退出程序还是最小化到托盘？」（老的 CClosePromptDlg，SkinLib 版），
    //已于 2026-08-12 连同那个类一起删除：对一个不可见的窗口问"要不要最小化"本就说不通，
    //而且它真弹出来过一次 —— 升级流程曾用 WM_CLOSE 请本窗口退出，结果被这个框拦住，
    //更新器等不到主进程退出、30 秒后放弃，升级白做一场。升级现在走
    //FMG_MSG_EXIT_FOR_UPDATE，与本函数无关，但这个框留着迟早还会绊到别人。
    CloseDialog(IDOK);
}

void CMainDlg::OnDestroy()
{
    CRect rcWindow;
    GetWindowRect(&rcWindow);
    m_FMGClient.m_UserConfig.SetMainDlgX(rcWindow.left);
    m_FMGClient.m_UserConfig.SetMainDlgY(rcWindow.top);
    m_FMGClient.m_UserConfig.SetMainDlgWidth(rcWindow.Width());
    m_FMGClient.m_UserConfig.SetMainDlgHeight(rcWindow.Height());

    m_FMGClient.m_UserConfig.SetFaceID(m_FMGClient.m_UserMgr.m_UserInfo.m_nFace);
    m_FMGClient.m_UserConfig.SetCustomFace(m_FMGClient.m_UserMgr.m_UserInfo.m_strCustomFace.c_str());
    m_FMGClient.SaveUserConfig();

    //存储最近联系人列表
    m_FMGClient.m_UserMgr.StoreRecentList();


    m_FMGClient.Uninit();

    // 旧自绘 UI shell 已移除：原先这里的 UninitUI() 与 m_SkinMenu.DestroyMenu()
    // 随旧主窗口控件一并删除，CMainDlg 退居幕后只作消息中枢，无旧 UI 可销毁。
    //m_TrayIcon.RemoveIcon();
    //if (m_BuddyChatDlg.IsWindow())
    //	m_BuddyChatDlg.DestroyWindow();
    //if (m_GroupChatDlg.IsWindow())
    //	m_GroupChatDlg.DestroyWindow();

    DestroyAppIcon();
    DestroyLoginIcon();

    if (m_hMsgIcon != NULL)
    {
        ::DestroyIcon(m_hMsgIcon);
        m_hMsgIcon = NULL;
    }

    if (m_hDlgIcon != NULL)
    {
        ::DestroyIcon(m_hDlgIcon);
        m_hDlgIcon = NULL;
    }

    if (m_hDlgSmallIcon != NULL)
    {
        ::DestroyIcon(m_hDlgSmallIcon);
        m_hDlgSmallIcon = NULL;
    }

    ::UnregisterHotKey(m_hWnd, 1001);	// 反注册提取消息热键

    // 保存杂项配置
    //tstring strFileName = m_FMGClient.GetPersonalFolder()+_T("MiscConfig.xml");
    //SaveMiscConfig(strFileName.c_str());

    CMessageLoop* pLoop = _Module.GetMessageLoop();
    ATLASSERT(pLoop != NULL);
    pLoop->RemoveMessageFilter(this);
    pLoop->RemoveIdleHandler(this);

    CGDIFactory::Uninit();
}

void CMainDlg::CloseDialog(int nVal)
{
    //此处原先有一道「您还有文件正在传输，确认要退出」的拦截，判据是 v1 单聊窗
    //（CBuddyChatDlg）里是否有传输中的文件。v1 聊天窗已随本次清理删除，那个判据
    //恒为假、拦截早已不起作用，故一并移除。
    //若要为 v2 的文件 / 文件夹传输恢复这道拦截，应改为向 FolderTransfer 的传输管理
    //对象询问，而不是复活这段代码。

    if (m_dwLoginTimerId != NULL)
    {
        KillTimer(m_dwLoginTimerId);
        m_dwLoginTimerId = NULL;
    }
    if (m_dwMsgTimerId != NULL)
    {
        KillTimer(m_dwMsgTimerId);
        m_dwMsgTimerId = NULL;
    }
    // 退出前把系统电源请求还给系统。进程结束时系统也会回收，此处显式释放一次，
    // 让"哪个程序在阻止休眠"这件事在日志上有始有终。
    m_KeepAwakeSession.Release();
    m_dwLastKeepAwakeNudgeTick = 0;

    if (m_dwAutoAwayTimerId != 0)
    {
        KillTimer(m_dwAutoAwayTimerId);
        m_dwAutoAwayTimerId = 0;
    }

    //退订会话通知。必须赶在窗口销毁之前 —— 系统按 HWND 记录订阅方，窗口没了还留着
    //订阅，事件会投到一个已经不存在的窗口上。
    ::WTSUnRegisterSessionNotification(m_hWnd);




    m_TrayIcon.RemoveIcon();
    m_CascadeWinManager.Clear();

    // 退出前显式销毁 CMainDlg 持有的所有 ATL 窗口对象成员。balloonui 的 DuiHost /
    // DuiFrameWindow 都是 CWindowImpl<...> 派生（见 DuiHost.h），CDialogImpl 同理；
    // 它们多是独立顶层窗口，DestroyWindow(CMainDlg) 不会连带销毁。若不在此销毁，
    // dlgMain 析构时这些成员仍持有有效 HWND，会触发 ATL CWindowImplRoot 的析构断言
    // （atlwin.h:3238 "Object deleted before window was destroyed"），表现为退出崩溃。
    // 所有退出路径都汇到本函数（OnMenu_Exit / OnClose / 面板关闭→OnCloseDlg），在此
    // 统一收尾。注意 m_trayMsgTipPopup（托盘悬停未读提示浮窗）平时只 HidePopup 不
    // 销毁，是登录后右键托盘退出时最容易残留触发断言的那个。
    //
    // 诊断日志：记录退出时各窗口对象是否仍持有 HWND（1=有窗口）。若退出仍崩，看本行
    // 即可定位残留的是哪个窗口；若某项为 1 而本函数又没有对应销毁，即为漏网者。
    LOG_INFO("CloseDialog teardown alive: trayMsgTip=%d panel=%d "
        "remoteDesktop=%d chatWin=%d groupChatWin=%d netSet=%d reg=%d profile=%d contact=%d "
        "groupMgr=%d sysSet=%d imgViewer=%d.",
        (int)m_trayMsgTipPopup.IsWindow(), (int)m_BalloonMainPanel.IsWindow(),
        (int)m_BalloonChatWindow.IsWindow(), (int)m_BalloonGroupChatWindow.IsWindow(),
        (int)m_BalloonNetSettingsWindow.IsWindow(), (int)m_BalloonRegisterWindow.IsWindow(),
        (int)m_BalloonProfileWindow.IsWindow(), (int)m_BalloonContactWindow.IsWindow(),
        (int)m_BalloonGroupManageWindow.IsWindow(), (int)m_BalloonSystemSettingDlg.IsWindow(),
        (int)m_BalloonImageViewerWindow.IsWindow());

    if (m_trayMsgTipPopup.IsWindow())
        m_trayMsgTipPopup.DestroyWindow();
    if (m_BalloonMainPanel.IsWindow())
        m_BalloonMainPanel.DestroyWindow();
    if (m_BalloonChatWindow.IsWindow())
        m_BalloonChatWindow.DestroyWindow();
    if (m_BalloonGroupChatWindow.IsWindow())
        m_BalloonGroupChatWindow.DestroyWindow();
    if (m_BalloonNetSettingsWindow.IsWindow())
        m_BalloonNetSettingsWindow.DestroyWindow();
    if (m_BalloonRegisterWindow.IsWindow())
        m_BalloonRegisterWindow.DestroyWindow();
    if (m_BalloonProfileWindow.IsWindow())
        m_BalloonProfileWindow.DestroyWindow();
    if (m_BalloonContactWindow.IsWindow())
        m_BalloonContactWindow.DestroyWindow();
    if (m_BalloonGroupManageWindow.IsWindow())
        m_BalloonGroupManageWindow.DestroyWindow();
    if (m_BalloonSystemSettingDlg.IsWindow())
        m_BalloonSystemSettingDlg.DestroyWindow();
    if (m_BalloonGroupInfoDlg.IsWindow())
        m_BalloonGroupInfoDlg.DestroyWindow();
    if (m_BalloonImageViewerWindow.IsWindow())
        m_BalloonImageViewerWindow.DestroyWindow();
    //ShowWindow(SW_HIDE);

    //if (m_FMGClient.GetStatus() != STATUS_OFFLINE)
    //{
    //	m_FMGClient.Logout();
    //}
    //else
    //{
    DestroyWindow();
    ::PostQuitMessage(nVal);
    //}
}

// 初始化Top工具栏
bool CMainDlg::StartLogin(BOOL bAutoLogin/* = FALSE*/)
{
    m_FMGClient.SetCallBackWnd(m_hWnd);
    if (bAutoLogin)
    {
        //此处原先在自动登录前同步跑一遍 v1 时代的自动升级（Updater + CUpdateDlg），
        //并用 #ifndef _DEBUG 把它挡在 Release 才生效。那条链路在 v2 改造后已经跑不通，
        //现已整体删除。检查更新改为登录成功后异步进行，见本文件 OnLoginResult 里的
        //静默检查，以及 Source/Update、Source/BalloonUpdate。

        BOOL bRet = m_LoginAccountList.GetLastLoginAccountInfo(&m_stAccountInfo);
        if (!bRet)
            return true;

        m_LoginDlg.SetLoginAccountInfo(&m_stAccountInfo);

        //置"登录中"必须在真正发起登录之前，理由见下面手动登录分支里的说明。
        //自动登录这条路本身没有时序问题（DoLogin 起线程即返回，结果稍后才到），
        //提前置位只是让两条路的写法一致。
        m_nMainPanelStatus = MAINPANEL_STATUS_LOGINING;
        m_LoginDlg.DoLogin();
    }
    else
    {
        ShowWindow(SW_HIDE);
        // 尚未登录：GetConnectionUiState 为离线，托盘先挂灰色图标。
        LoadAppIcon(GetConnectionUiState());
        m_TrayIcon.AddIcon(m_hWnd, WM_TRAYICON_NOTIFY, 1, m_hAppIcon, _T("Flamingo未登录"));

        //ShowWindow(SW_HIDE);
        //ShowWindow(SW_SHOW);

        m_LoginDlg.m_lpFMGClient = &m_FMGClient;
        m_LoginDlg.m_pLoginAccountList = &m_LoginAccountList;
        m_LoginDlg.SetDefaultAccount(m_FMGClient.m_UserMgr.m_UserInfo.m_strAccount.c_str());
        m_LoginDlg.SetDefaultPassword(m_FMGClient.m_UserMgr.m_UserInfo.m_strPassword.c_str());

        //置"登录中"必须在 DoModal 之前。登录窗是模态的，且登录期间不关窗，登录结果是
        //在它的模态消息循环内部派发到 OnLoginResult 的，那里已经把状态推进到
        //MAINPANEL_STATUS_LOGIN；若等 DoModal 返回之后再置一次"登录中"，就把登录成功
        //的状态盖了回去，此后 m_nMainPanelStatus 一直停在"登录中"。凡是以
        //"m_bAlreadyLogin && m_nMainPanelStatus == MAINPANEL_STATUS_LOGIN" 为条件的分支
        //都会走错，2026-08-14 实测到两处后果：切换到离开 / 忙碌时不发 cmd_change_presence
        //而是误走"从离线恢复在线"分支、托盘图标也不刷新（徽标出不来）；
        //"无操作自动离开"因判定为连接未就绪而永不触发。
        //没有任何代码判断 MAINPANEL_STATUS_LOGINING 本身，故提前置位不影响其它逻辑。
        m_nMainPanelStatus = MAINPANEL_STATUS_LOGINING;

        if (m_LoginDlg.DoModal(g_hwndOwner) != IDOK)	// 显示登录对话框
        {
            CloseDialog(IDOK);
            return false;
        }
        m_LoginDlg.GetLoginAccountInfo(&m_stAccountInfo);
    }

    // PR7: 不再显示旧的 "正在登录" 进度面板。CMainDlg 主窗口始终隐藏。
    //
    //托盘图标的登录动画只在自动登录这条路上启动，原因是两条路的时序不同：
    //  自动登录：上面的 DoLogin 起一个后台线程就返回，登录结果稍后经消息队列
    //            投递到 OnLoginResult，那里的 KillTimer 能正常停掉动画。
    //  手动登录：登录窗是模态的，且登录期间不关窗，登录结果是在 DoModal 的模态
    //            消息循环内部派发到 OnLoginResult 的（见该函数里的详细说明）。
    //            结果到达时本函数还停在 DoModal 那一行，动画尚未启动，
    //            OnLoginResult 里的 KillTimer 因而杀了个空；等 DoModal 返回、
    //            执行到这里，动画反倒在登录成功之后才开始，并且再没有人停它。
    //            表现为登录成功后托盘图标持续闪烁 —— Loading_1/3/5.ico 与
    //            imonline.ico 是同一张图，Loading_2/4/6.ico 与 offline.ico 是
    //            同一张图，所以看上去就是在线与离线两个图标每 200 毫秒交替。
    //            手动登录本来就有登录窗自己显示"登录中"，托盘无须重复提示。
    if (bAutoLogin)
    {
        LoadLoginIcon();
        m_dwLoginTimerId = SetTimer(LOGIN_TIMER_ID, LOGIN_ICON_ANIM_INTERVAL_MS, NULL);
        LOG_INFO("MainDlg::StartLogin: auto-login, tray login animation started, timerId=%u.",
                 (unsigned)m_dwLoginTimerId);
    }

    m_FMGClient.SetUser(m_stAccountInfo.szUser, m_stAccountInfo.szPwd);

    //已经改成同步登录了
    //m_FMGClient.Login();

    return true;
}

LRESULT CMainDlg::OnTrayIconNotify(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    // wParam为uID, lParam为鼠标消息
    m_TrayIcon.OnTrayIconNotify(wParam, lParam);


    UINT uID = (UINT)wParam;
    UINT uIconMsg = (UINT)lParam;

    // 被踢下线提示框正在显示时，托盘的点击一律不受理：那个框是模态的，它跑的嵌套消息
    // 循环照常分发托盘消息，若放行，用户从托盘右键菜单里点"登出"会重入回登录界面的收尾
    // 序列（销毁主面板 + 再进一层登录窗模态循环），栈就叠乱了。
    if (m_bKickoutNoticeShowing)
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
        ShowLoginOrMainPanel();
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

void CMainDlg::OnMenu_ShowMainPanel(UINT uNotifyCode, int nID, CWindow wndCtl)
{
    // 托盘菜单"打开主面板"：登录前唤登录窗、登录后唤主面板（与托盘单/双击同逻辑）。
    ShowLoginOrMainPanel();
}

void CMainDlg::ShowLoginOrMainPanel()
{
    // 选目标：登录成功且主面板存在 -> 主面板；否则（登录前 / 登录中）-> 登录窗。
    HWND hTarget = NULL;
    if (m_bAlreadyLogin && m_BalloonMainPanel.IsWindow())
        hTarget = m_BalloonMainPanel.m_hWnd;
    else if (m_LoginDlg.IsWindow())
        hTarget = m_LoginDlg.m_hWnd;

    LOG_INFO("ShowLoginOrMainPanel: alreadyLogin=%d, loginDlgIsWnd=%d, panelIsWnd=%d, "
        "targetIconic=%d, target=%p.",
        (int)m_bAlreadyLogin, (int)m_LoginDlg.IsWindow(), (int)m_BalloonMainPanel.IsWindow(),
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

void CMainDlg::OnMenu_Exit(UINT uNotifyCode, int nID, CWindow wndCtl)
{
    if (m_LoginDlg.IsWindow())
        m_LoginDlg.PostMessage(WM_CLOSE);
    else if (IsWindow())
    {
        //PostMessage(WM_CLOSE);
        CloseDialog(IDOK);
    }
}

void CMainDlg::OnMenu_Mute(UINT uNotifyCode, int nID, CWindow wndCtl)
{
    // 托盘右键"关闭所有声音"：翻转静音位。它与系统设置"提示音"开关写的是同一个 m_bMute，
    // 收消息 / 好友上线的提示音都据此门控。改完立刻持久化到该账号的 .cfg，和系统设置页一致，
    // 重启后保持（否则切了静音、重启又会按配置文件里的旧值复位）。
    m_FMGClient.m_UserConfig.EnableMute(!m_FMGClient.m_UserConfig.IsEnableMute());
    m_FMGClient.SaveUserConfig();
}

// 网络设置 —— 登录后主面板系统菜单"网络设置"项经 WM_COMMAND 转发到此，
// 复用新版网络设置窗（读写真实 flamingo.ini，与登录前 --netsettings 入口同一窗口）。
void CMainDlg::OnMenu_NetSettings(UINT uNotifyCode, int nID, CWindow wndCtl)
{
    // 已经开着就前置，不重复 Create —— ATL 的 CWindowImpl::Create 断言 m_hWnd 为空，
    // 对同一个成员窗口再建一次会在 Debug 下断言、Release 下丢掉旧句柄。
    if (m_BalloonNetSettingsWindow.IsWindow())
    {
        m_BalloonNetSettingsWindow.ShowWindow(SW_SHOW);
        ::SetForegroundWindow(m_BalloonNetSettingsWindow.m_hWnd);
        return;
    }

    // hMainDlg 传 NULL：关闭网络设置窗只销毁它自己，不退出程序。
    // 该参数只为 --netsettings 调试入口而设 —— 那条路径下本窗口是唯一可见窗口，
    // 关闭它就该结束进程，于是窗口在 WM_CLOSE 里往 hMainDlg 投 WM_CLOSE_MAINDLG
    // （见 CBalloonNetSettingsWindow::OnCloseMsg）。登录后从主面板系统菜单打开时
    // 必须传 NULL，否则关掉这个小窗会把整个程序一起带走。与主面板开群管理窗 /
    // 资料窗的口径一致（见 CBalloonMainPanel 里的 CreateAndShow(NULL)）。
    m_BalloonNetSettingsWindow.CreateAndShow(NULL);
}


void CMainDlg::OnMainMenu_About(UINT uNotifyCode, int nID, CWindow wndCtl)
{

}

// “取消”按钮(取消登录)
void CMainDlg::OnMenu_Status(UINT uNotifyCode, int nID, CWindow wndCtl)
{
    long nNewStatus = GetStatusFromMenuID(nID);
    //如果需要更改的目标状态与当前状态一样，不处理
    if (nNewStatus == m_FMGClient.GetStatus())
        return;


    //m_FMGClient.ChangeStatus(nNewStatus);

    if (nNewStatus == STATUS_OFFLINE)
    {
        m_FMGClient.GoOffline();
        //重连标志清零
        m_nMainPanelStatus = MAINPANEL_STATUS_USERGOOFFLINE;
    }
    else if (nNewStatus == STATUS_ONLINE)
        m_FMGClient.GoOnline();

    //StatusMenuBtn_SetIconPic(m_btnStatus, nNewStatus);	
}

LRESULT CMainDlg::OnNetError(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    LOG_INFO("MainDlg::OnNetError: panelStatus=%d, reconnectTimer=%u, "
        "enableReconnect=%d.",
        m_nMainPanelStatus, (unsigned int)m_dwReconnectTimerId,
        m_bEnableReconnect ? 1 : 0);

    //重连定时器已经开启，直接退出，避免重复开启
    if (m_dwReconnectTimerId != 0)
        return 0;

    m_FMGClient.m_UserMgr.ResetToOfflineStatus();

    ::SendMessage(m_FMGClient.m_UserMgr.m_hCallBackWnd, FMG_MSG_UPDATE_BUDDY_INFO, 0, (LPARAM)(m_FMGClient.m_UserMgr.m_UserInfo.m_uUserID));
    ::SendMessage(m_FMGClient.m_UserMgr.m_hCallBackWnd, FMG_MSG_UPDATE_RECENT_LIST, 0, 0);
    ::SendMessage(m_FMGClient.m_UserMgr.m_hCallBackWnd, FMG_MSG_UPDATE_GROUP_LIST, 0, 0);

    //没有开启重连或者用户主动下线都不要重连（守卫与 OnTimer 的重连分支共用同一函数）。
    const bool userWentOffline = (m_nMainPanelStatus == MAINPANEL_STATUS_USERGOOFFLINE);
    if (balloonmain::ShouldAutoReconnect(userWentOffline, m_bEnableReconnect))
    {
        m_nMainPanelStatus = MAINPANEL_STATUS_RECONNECTING;
        m_dwReconnectTimerId = SetTimer(RECONNECT_TIMER_ID, m_uReconnectInterval, NULL);
    }

    //连接已断：托盘图标转灰，界面进入"正在连接"（重连中）或"离线"（用户主动下线）态，
    //聊天窗输入区据此置灰、弹出提示条。
    RefreshTrayIconForStatus();
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
LRESULT CMainDlg::OnV2Kickout(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/)
{
    const int nReason = (int)wParam;

    LOG_INFO("MainDlg::OnV2Kickout: 收到被踢下线推送。reason=%d, panelStatus=%d, "
        "reconnectTimer=%u, alreadyLogin=%d.",
        nReason, m_nMainPanelStatus, (unsigned int)m_dwReconnectTimerId,
        m_bAlreadyLogin ? 1 : 0);

    //【0】重入保护：提示框跑的是嵌套消息循环，期间其它消息照常分发。万一又来一条被踢
    //     下线通知（理论上不会：连接已经关了），不能再叠一个框、更不能重入下面的收尾序列。
    if (m_bKickoutNoticeShowing)
    {
        LOG_INFO("MainDlg::OnV2Kickout: 提示框正在显示，忽略重复的被踢下线通知。");
        return 0;
    }

    //【1】先置"离线且不再重连"态。OnNetError 与重连定时器都据这个标志决定要不要连回去
    //     （见 balloonmain::ShouldAutoReconnect）。
    m_nMainPanelStatus = MAINPANEL_STATUS_USERGOOFFLINE;

    //【2】已经在跑的重连定时器就地停掉。掉线事件（FMG_MSG_NET_ERROR）与本消息都由网络
    //     线程投递，本消息虽然投得更早，但这里不指望它一定先被处理：万一 OnNetError 抢先
    //     把定时器起了，这一刀补上，不留"标志已置离线、定时器还在跑"的中间态。
    if (m_dwReconnectTimerId != 0)
    {
        KillTimer(m_dwReconnectTimerId);
        m_dwReconnectTimerId = 0;
        LOG_INFO("MainDlg::OnV2Kickout: 被踢下线，已停掉正在运行的重连定时器。");
    }

    //【3】真正下线：立起网络层离线闸门、丢弃排队中的帧、断开 chat / file / img 三条连接。
    //     闸门在网络线程收到推送时已经立过一次（见 CIUSocket::HandleV2Frame 的 cmd_kickout
    //     分支），这里是幂等的补齐，同时把另外两条连接与排队数据一并收拾干净。
    m_FMGClient.GoOffline();

    //【4】提示框弹出期间主面板还在它背后，界面不能还写着"在线"：胶囊改离线、托盘图标转灰、
    //     聊天窗输入区置灰。这几步很快就会随主面板一起销毁，但那是用户关掉提示框<u>之后</u>
    //     的事，中间这段时间界面得是自洽的。
    if (m_BalloonMainPanel.IsWindow())
    {
        m_BalloonMainPanel.ApplyPresenceOptimistic(kMyPresenceOffline);
    }
    RefreshTrayIconForStatus();
    BroadcastConnectionState();

    //【5】取托盘图标位置，供提示框贴着它弹出。必须赶在下面收尾序列里的 RemoveIcon 之前
    //     取 —— 图标摘掉之后就再也问不出它在哪儿了。取不到时给空矩形，提示框自会退回
    //     桌面右下角（见 balloonmain::ComputeTrayAnchoredOrigin）。
    RECT rcTrayIcon;
    ::SetRectEmpty(&rcTrayIcon);
    if (!m_TrayIcon.GetTrayIconRect(&rcTrayIcon))
    {
        ::SetRectEmpty(&rcTrayIcon);
    }

    //【6】弹模态提示框。owner 取主面板；主面板不在（已销毁 / 未创建）时退回隐藏的 CMainDlg。
    //     模态期间 owner 被禁用，托盘的点击也一并挡住（见 OnTrayIconNotify 里的守卫），
    //     免得用户从托盘菜单里点"登出"造成收尾序列重入。
    HWND hOwner = m_hWnd;
    if (m_BalloonMainPanel.IsWindow())
    {
        hOwner = m_BalloonMainPanel.m_hWnd;
    }

    m_bKickoutNoticeShowing = true;
    {
        CBalloonKickoutDlg dlg;
        dlg.DoModal(hOwner, nReason, rcTrayIcon);
    }
    m_bKickoutNoticeShowing = false;

    LOG_INFO("MainDlg::OnV2Kickout: 用户已关闭提示框，回到登录界面。");

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
LRESULT CMainDlg::OnNetworkResumed(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/)
{
    // 主动离线与被踢下线都会把面板状态置成 MAINPANEL_STATUS_USERGOOFFLINE，两者对
    // "要不要连回去"的答案一样，故用同一个判据。
    const bool userWentOffline = (m_nMainPanelStatus == MAINPANEL_STATUS_USERGOOFFLINE);

    LOG_INFO("MainDlg::OnNetworkResumed: 本机网络已恢复。panelStatus=%d, "
        "userWentOffline=%d, enableReconnect=%d, alreadyLogin=%d.",
        m_nMainPanelStatus, userWentOffline ? 1 : 0,
        m_bEnableReconnect ? 1 : 0, m_bAlreadyLogin ? 1 : 0);

    if (!balloonmain::ShouldAutoReconnect(userWentOffline, m_bEnableReconnect))
    {
        LOG_INFO("MainDlg::OnNetworkResumed: 处于离线态（或配置关闭了自动重连），"
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
        LOG_INFO("MainDlg::OnNetworkResumed: 当前未登录（停在登录界面），不自动登录。");
        return 0;
    }

    LOG_INFO("MainDlg::OnNetworkResumed: 非离线态，网络恢复后重新登录。");
    m_FMGClient.GoOnline();
    return 0;
}

// 登录返回消息
LRESULT CMainDlg::OnLoginResult(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    long nCode = (long)lParam;

    LOG_INFO("MainDlg::OnLoginResult: code=%ld (0=SUCCESS 1=FAILED 2=UNREG "
        "3=PWD_ERR), panelStatus=%d (0=NOTLOGIN 1=LOGINING 2=LOGIN "
        "3=RECONNECTING 4=USERGOOFFLINE).",
        nCode, m_nMainPanelStatus);

    //重连不成功，清除以发送登录请求包标识，再次发送重连登录数据包
    if (m_nMainPanelStatus == MAINPANEL_STATUS_RECONNECTING && nCode != LOGIN_SUCCESS)
    {
        m_bAlreadySendReloginRequest = false;
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
    if (m_LoginDlg.IsLoginWindowActive())
    {
        //本消息的 lParam 是结果码（函数开头已取走），wParam 是登录轮次号 ——
        //都是 CFlamingoClient::OnLoginResult 从 CLoginResult 里拆出来转发的，
        //那个对象在转发之后立即被释放，所以这里<u>拿不到也不该去碰</u>它的指针。
        const uint32_t uGeneration = (uint32_t)wParam;

        if (!m_LoginDlg.OnLoginResultArrived(nCode, uGeneration))
        {
            //过期结果：登录窗已经丢弃它，这里也不能再动主面板状态与托盘图标
            LOG_INFO("MainDlg::OnLoginResult: stale result discarded by the login window.");
            return 0;
        }

        //登录失败已由登录窗就地显示，不再弹 MessageBox、也不再重建登录窗。
        //把状态复位后返回，下面那套「弹窗 + StartLogin」不再执行。
        if (nCode != LOGIN_SUCCESS)
        {
            KillTimer(m_dwLoginTimerId);
            m_dwLoginTimerId = NULL;
            LoadAppIcon(GetConnectionUiState());
            m_bResumingOnline = false;
            m_nMainPanelStatus = MAINPANEL_STATUS_NOTLOGIN;
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

    KillTimer(m_dwLoginTimerId);
    m_dwLoginTimerId = NULL;

    // 此刻 m_bAlreadyLogin 还没置真（成功分支下面才置），GetConnectionUiState 会算成
    // "连接未就绪"、取灰色图标；登录成功分支末尾会再调一次 RefreshTrayIconForStatus
    // 换成彩色。这样登录失败时托盘自然停在灰色，无需另行处理。
    LoadAppIcon(GetConnectionUiState());

    // 本次登录是不是"从离线恢复在线"发起的。下面各分支要用，取完即复位：无论成功还是
    // 失败，这一轮恢复流程到此为止。
    const bool bResuming = m_bResumingOnline;
    m_bResumingOnline = false;

    switch (nCode)
    {
    case LOGIN_SUCCESS:				// 登录成功
    {
        if (m_dwReconnectTimerId != 0)
        {
            KillTimer(m_dwReconnectTimerId);
            m_dwReconnectTimerId = 0;
        }

        // PR7: 旧自绘 UI 不再显示。新 BalloonMainPanel 由本函数末尾
        // CreateAndShow,CMainDlg 主窗口保持隐藏。

        m_FMGClient.InitNetThreads();

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
        //m_FMGClient.RequestServerTime();


        //此处原先要打开 v1 接收线程的"UI 闸门"，放行登录期间缓存下来的聊天消息。
        //那个线程（CRecvMsgThread）在 v2 下收不到任何数据，2026-08-14 随 v1 清理
        //一并删除，闸门与缓存也就不存在了：v2 的消息由 CIUSocket 收到即投给代理
        //窗口，不经过任何缓存队列。

        //TODO: 这里调用时，网络线程一定都启动了吗？
        m_FMGClient.StartCheckNetworkStatusTask();
        //m_FMGClient.StartHeartbeatTask();

        // 保存登录帐号列表
        LOGIN_ACCOUNT_INFO* lpAccount = m_LoginAccountList.Find(m_stAccountInfo.szUser);
        if (lpAccount != NULL)
            memcpy(lpAccount, &m_stAccountInfo, sizeof(LOGIN_ACCOUNT_INFO));
        else
            m_LoginAccountList.Add(m_stAccountInfo.szUser, m_stAccountInfo.szPwd,
                m_stAccountInfo.nStatus, m_stAccountInfo.bRememberPwd, m_stAccountInfo.bAutoLogin);

        // 记下本账号的姓名(昵称) —— 登录窗下次切到这个账号时，在头像下方
        // 显示姓名而不是账号。昵称此刻已由 CFlamingoClient::OnLoginResult 从
        // 登录应答的 profile 填进 m_UserInfo（它先于本消息被调用）。
        //
        // 必须放在上面的 memcpy 之后：m_stAccountInfo 是登录窗采集来的，只有
        // 账号 / 密码 / 勾选项，没有姓名，直接 memcpy 会把已存下的姓名冲掉。
        m_LoginAccountList.SetNickName(m_stAccountInfo.szUser,
            m_FMGClient.m_UserMgr.m_UserInfo.m_strNickName.c_str());
        LOG_INFO(_T("save login account: account=%s, nickname=%s."),
            m_stAccountInfo.szUser, m_FMGClient.m_UserMgr.m_UserInfo.m_strNickName.c_str());

        m_LoginAccountList.SetLastLoginUser(m_stAccountInfo.szUser);
        SaveLoginAccountListFile();

        // 加载杂项配置
        //strFileName = m_FMGClient.GetPersonalFolder()+_T("MiscConfig.xml");
        //LoadMiscConfig(strFileName.c_str());

        // 注册提取消息热键(Ctrl+Alt+D)
        ::RegisterHotKey(m_hWnd, 1001, MOD_CONTROL | MOD_ALT, kShowPanelHotKey);

        //为了调试方便，调试版就不开启这个功能了- -！
//#ifndef _DEBUG
            //存储当前登录账号至本地文件
            //SaveCurrentLogonUserToFile();
//#endif

        m_FMGClient.LoadUserConfig();
        // 把该账号的「自动识别链接」设置灌进进程级开关：气泡在测量阶段读它决定
        // 要不要识别，故必须在任何聊天窗打开之前设好（配置里没有这一项时默认开）。
        balloonchatwnd::SetChatLinkOptionEnabled(
            m_FMGClient.m_UserConfig.IsEnableLinkDetect() != FALSE);
        // 旧好友列表头像风格成员(m_nBuddyListHeadPicStyle / m_bShowBigHeadPicInSel)已随
        // 旧主窗口一并移除；对应用户配置仍保留在 UserConfig 中，Phase 2 由
        // BalloonMainPanel 接管列表样式时直接从 UserConfig 读取即可。

        // PR7: 不再 SetWindowPos 还原 CMainDlg 窗口位置 —— 主窗口始终隐藏。
        // 窗口位置/尺寸的持久化(OnDestroy 中写回 UserConfig)对 CMainDlg
        // 已无意义,但保留为无害空操作; Phase 2 由 BalloonMainPanel 自持其
        // 位置与尺寸。

        //加载最近联系人列表
        m_FMGClient.m_UserMgr.LoadRecentList();
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
            update::UpdateManager::GetInstance().SetNotifyTarget(m_hWnd);
        }

        // 文件夹传输管理器的回投目标登记成<u>代理窗口</u>，而不是主窗口：它自己的工作
        // 线程投出来的进度 / 结果，要和网络层投来的那五条信令走同一条「代理窗 → 主窗口
        // → 主面板 → 聊天窗」通路，两路混投到不同起点只会让排查时多一种可能性。
        // 上一次登录残留的会话与保留期计时在这里一并作废 —— 按产品定稿，重试窗口只在
        // 本次登录期间有效。
        foldertransfer::FolderTransferManager::GetInstance().Reset();
        foldertransfer::FolderTransferManager::GetInstance().SetNotifyTarget(
            m_FMGClient.m_UserMgr.m_hProxyWnd);
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

        m_nMainPanelStatus = MAINPANEL_STATUS_LOGIN;

        //登录成功也清除该标志位，以便下次重连
        m_bAlreadySendReloginRequest = false;

        // 显示新版 balloonui 主面板。CMainDlg 主窗口本就隐藏,故 CreateAndShow
        // 后直接把焦点 / 前台窗口指向 BalloonMainPanel。
        // 注意:本分支在"重新登录"时也会走到 —— 用户主动下线后再上线(状态菜单/托盘选
        // 在线)走 GoOnline → 重新登录 → 这里;断线重连成功也会到这里。这些场景下主面板
        // 早已创建且未销毁(去离线只关 socket、不销毁窗口),<u>不能再 CreateAndShow</u>:
        // CWindowImplBaseT::Create 断言 m_hWnd==NULL,二次创建会触发断言崩溃。故仅在面板
        // 尚未创建时创建;已存在则只重新显示 / 前置,数据由下面 RequestContacts /
        // RequestConversations / RequestUserGroups 重新拉取刷新。
        if (!m_BalloonMainPanel.IsWindow())
        {
            m_BalloonMainPanel.CreateAndShow(m_hWnd);
        }
        if (m_BalloonMainPanel.IsWindow())
        {
            ::ShowWindow(m_BalloonMainPanel.m_hWnd, SW_SHOW);
            ::SetForegroundWindow(m_BalloonMainPanel.m_hWnd);
            ::SetFocus(m_BalloonMainPanel.m_hWnd);
        }

        //本轮登录重新开始记账："收到陌生人消息就补拉通讯录"那道兜底闸门要清零。
        //掉线重连期间组织架构可能变过、cmd_org_structure_notify 也可能错过，
        //上一轮攒下的"这些人已经问过了"不该继续压着本轮的补拉。
        m_BalloonMainPanel.ResetUnknownSenderPolicy();

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
        m_BalloonMainPanel.RequestAnnouncements();

        //登录成功：托盘图标转回彩色、提示文字刷新。
        RefreshTrayIconForStatus();

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

        MessageBox(_T("网络故障，登录失败！"), g_strAppTitle.c_str(), MB_OK);
        StartLogin();

        m_nMainPanelStatus = MAINPANEL_STATUS_NOTLOGIN;
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

        MessageBox(_T("当前客户端版本过低，无法登录，请先升级。"),
                   g_strAppTitle.c_str(), MB_OK | MB_ICONWARNING);
        StartLogin();
        m_nMainPanelStatus = MAINPANEL_STATUS_NOTLOGIN;

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
        MessageBox(_T("用户名未注册！"), g_strAppTitle.c_str(), MB_OK);
        StartLogin();
        m_nMainPanelStatus = MAINPANEL_STATUS_NOTLOGIN;
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

        MessageBox(_T("密码错误！"), g_strAppTitle.c_str(), MB_OK);
        StartLogin();
        m_nMainPanelStatus = MAINPANEL_STATUS_NOTLOGIN;
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
        MessageBox(_T("服务器拒绝您的登录请求！"), g_strAppTitle.c_str(), MB_OK);
        StartLogin();
        m_nMainPanelStatus = MAINPANEL_STATUS_NOTLOGIN;
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
        m_nMainPanelStatus = MAINPANEL_STATUS_NOTLOGIN;
    }
    break;
    }

    return 0;
}

// 注销返回消息
LRESULT CMainDlg::OnExitForUpdate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/)
{
    LOG_INFO("MainDlg::OnExitForUpdate: 升级包已就绪，直接退出程序交给更新器。");
    //直接退出，不经 OnClose 的「退出还是最小化」询问
    CloseDialog(IDOK);
    return 0;
}

LRESULT CMainDlg::OnSilentUpdateResult(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    update::UpdateTaskResult* pResult = (update::UpdateTaskResult*)lParam;
    if (pResult == NULL)
    {
        return 0;
    }

    LOG_INFO("MainDlg::OnSilentUpdateResult: transportOk=%d, code=%d, hasUpdate=%d, "
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
            LOG_INFO("MainDlg::OnSilentUpdateResult: 版本 %s 用户已选稍后，不再提示。",
                     pResult->m_latestVersion.c_str());
            delete pResult;
            return 0;
        }

        //强制升级时把主面板一并禁用，直到升级完成 —— 否则用户把升级窗晾在一边照常
        //聊天，"强制"就名不副实了。非强制不禁用：那只是个提示，不该妨碍人干活。
        if (pResult->m_forced && m_BalloonMainPanel.IsWindow())
        {
            //主面板这边没有待续的登录，关窗只需恢复可用
            w.SetGatedWindow(m_BalloonMainPanel.m_hWnd,
                             CBalloonUpdateWindow::kGatedMainPanel);
        }

        //ShowResult 内部会把消息投递目标改登记为升级窗自己，后续下载进度直接投给它。
        w.ShowResult(*pResult);
    }

    delete pResult;
    return 0;
}

LRESULT CMainDlg::OnLogoutResult(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    CloseDialog(IDOK);
    return 0;
}

// 更新用户信息
LRESULT CMainDlg::OnUpdateUserInfo(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    //CBuddyInfoResult* lpBuddyInfoResult = (CBuddyInfoResult*)lParam;
    //if (NULL == lpBuddyInfoResult)
    //	return 0;
    ////m_FMGClient.OnUpdateBuddyInfo(lpBuddyInfoResult);
    //delete lpBuddyInfoResult;

    return 0;
}

// 更新群列表消息
LRESULT CMainDlg::OnUpdateGroupList(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    //BOOL bSuccess = (BOOL)lParam;
    //if (!bSuccess)
    //	return 0;

    // 旧自绘群列表控件 m_GroupListCtrl 已废弃，不再填充，原 UpdateGroupTreeCtrl() 调用已移除。
    // #20:转发到 BalloonMainPanel,刷新群 Tab。
    m_BalloonMainPanel.RefreshGroups();

    return 0;
}

// 更新最近联系人列表消息
LRESULT CMainDlg::OnUpdateRecentList(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    // 旧自绘最近联系人控件 m_RecentListCtrl 已废弃，不再填充，原 UpdateRecentTreeCtrl() 调用已移除。
    // #20:转发到 BalloonMainPanel,刷新会话 Tab。
    m_BalloonMainPanel.RefreshMessages();

    return 0;
}

// 好友消息
LRESULT CMainDlg::OnV2Contacts(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //lParam 为 IUSocket 接收线程堆分配的 net::ContactsResult，处理后须 delete。
    net::ContactsResult* pResult = (net::ContactsResult*)lParam;
    if (pResult == NULL)
        return 0;
    if (pResult->m_code == 0)
        m_BalloonMainPanel.OnContactsArrived(*pResult);
    else
        LOG_ERROR("v2 contacts response error, code=%d, msg=%s.",
                  pResult->m_code, pResult->m_msg.c_str());
    delete pResult;
    return 0;
}

// v2 最近会话列表应答：填充"消息"页
LRESULT CMainDlg::OnV2Conversations(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //lParam 为 IUSocket 接收线程堆分配的 net::ConversationsResult，处理后须 delete。
    net::ConversationsResult* pResult = (net::ConversationsResult*)lParam;
    if (pResult == NULL)
        return 0;
    if (pResult->m_code == 0)
        m_BalloonMainPanel.OnConversationsArrived(*pResult);
    else
        LOG_ERROR("v2 conversations response error, code=%d, msg=%s.",
                  pResult->m_code, pResult->m_msg.c_str());
    delete pResult;
    return 0;
}

// v2 建群应答：交主面板提示建群结果（成功关闭对话框并提示；失败提示错误）
LRESULT CMainDlg::OnV2CreateGroupResult(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //lParam 为 IUSocket 接收线程堆分配的 net::CreateGroupResponse，处理后须 delete。
    net::CreateGroupResponse* pResp = (net::CreateGroupResponse*)lParam;
    if (pResp == NULL)
        return 0;
    m_BalloonMainPanel.OnCreateGroupResult(*pResp);
    delete pResp;
    return 0;
}

// v2 修改密码应答：转发到主面板（再转发给改密对话框提示结果）
LRESULT CMainDlg::OnV2ChangePwdResult(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //lParam 为 IUSocket 接收线程堆分配的 net::ChangePasswordResponse，处理后须 delete。
    net::ChangePasswordResponse* pResp = (net::ChangePasswordResponse*)lParam;
    if (pResp == NULL)
        return 0;
    m_BalloonMainPanel.OnChangePwdResult(*pResp);
    delete pResp;
    return 0;
}

// v2 更新资料应答（当前用于个性签名）：交主面板据 code 保留乐观更新或回退提示
LRESULT CMainDlg::OnV2UpdateProfileResult(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //lParam 为 IUSocket 接收线程堆分配的 net::UpdateProfileResponse，处理后须 delete。
    net::UpdateProfileResponse* pResp = (net::UpdateProfileResponse*)lParam;
    if (pResp == NULL)
        return 0;
    m_BalloonMainPanel.OnUpdateProfileResult(*pResp);
    delete pResp;

    //资料更新走的是"乐观更新 + 失败回退"（见 CBalloonProfileDlg::OnSaveResult），
    //上面这一路处理完之后 m_UserInfo 里的昵称就是最终生效的值。若它和历史登录
    //列表里存的姓名对不上（用户刚改了昵称），写回并落盘，使下次在登录窗切到本
    //账号时头像下方显示的是新姓名。没变则不写，避免每次改签名都动一次文件。
    SyncNickNameToLoginAccountList();
    return 0;
}

// 把当前登录账号的昵称同步进历史登录账号列表（有变化才写盘）
void CMainDlg::SyncNickNameToLoginAccountList()
{
    LOGIN_ACCOUNT_INFO* lpAccount = m_LoginAccountList.Find(m_stAccountInfo.szUser);
    if (lpAccount == NULL)
        return;

    const tstring& strNickName = m_FMGClient.m_UserMgr.m_UserInfo.m_strNickName;
    if (strNickName == lpAccount->szNickName)
        return;

    LOG_INFO(_T("nickname changed, update login account list: account=%s, nickname=%s."),
        m_stAccountInfo.szUser, strNickName.c_str());
    m_LoginAccountList.SetNickName(m_stAccountInfo.szUser, strNickName.c_str());
    SaveLoginAccountListFile();
}

// 清掉某账号在本机存下的"自动登录"标志并立即落盘
void CMainDlg::ClearAutoLoginFlag(LPCTSTR lpszAccount)
{
    if (lpszAccount == NULL || lpszAccount[0] == _T('\0'))
    {
        LOG_INFO("MainDlg::ClearAutoLoginFlag: 账号为空，跳过。");
        return;
    }

    LOGIN_ACCOUNT_INFO* lpAccount = m_LoginAccountList.Find(lpszAccount);
    if (lpAccount == NULL)
    {
        //该账号还没在本机成功登录过（历史列表里没有它），本来就没有标志可清。
        LOG_INFO(_T("MainDlg::ClearAutoLoginFlag: 账号不在历史列表里，跳过。account=%s."),
            lpszAccount);
        return;
    }

    if (!lpAccount->bAutoLogin)
    {
        return;   //本就没勾自动登录，不必白写一次盘
    }

    lpAccount->bAutoLogin = FALSE;
    SaveLoginAccountListFile();
    LOG_INFO(_T("MainDlg::ClearAutoLoginFlag: 已清除自动登录标志。account=%s."), lpszAccount);
}

// 忘掉当前账号在本机存下的密码，并取消"记住密码" / "自动登录"
void CMainDlg::ForgetSavedPasswordForCurrentAccount()
{
    const tstring& strAccount = m_FMGClient.m_UserMgr.m_UserInfo.m_strAccount;
    if (strAccount.empty())
    {
        LOG_INFO("MainDlg::ForgetSavedPasswordForCurrentAccount: 账号为空，跳过。");
        return;
    }

    LOGIN_ACCOUNT_INFO* lpAccount = m_LoginAccountList.Find(strAccount.c_str());
    if (lpAccount == NULL)
    {
        LOG_INFO(_T("MainDlg::ForgetSavedPasswordForCurrentAccount: 账号不在历史列表里，跳过。account=%s."),
            strAccount.c_str());
        return;
    }

    lpAccount->szPwd[0]     = _T('\0');
    lpAccount->bRememberPwd = FALSE;
    lpAccount->bAutoLogin   = FALSE;
    SaveLoginAccountListFile();
    LOG_INFO(_T("MainDlg::ForgetSavedPasswordForCurrentAccount: 已清除保存的密码并取消记住密码 / 自动登录。account=%s."),
        strAccount.c_str());
}

// 把历史登录账号列表写回磁盘
void CMainDlg::SaveLoginAccountListFile()
{
    tstring strFileName = Hootina::CPath::GetAppPath() + _T("Users\\LoginAccountList.dat");
    if (!m_LoginAccountList.SaveFile(strFileName.c_str()))
    {
        LOG_ERROR(_T("save login account list failed, file=%s."), strFileName.c_str());
    }
}

// v2 修改头像应答：交修改头像对话框据 code 提示成功/失败，成功则刷新本端头像
LRESULT CMainDlg::OnV2ChangeAvatarResult(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //lParam 为 IUSocket 接收线程堆分配的 net::UpdateProfileResponse（修改头像应答体为
    //通用 {code,msg}，复用此结构承载），处理后须 delete。
    net::UpdateProfileResponse* pResp = (net::UpdateProfileResponse*)lParam;
    if (pResp == NULL)
        return 0;
    m_BalloonMainPanel.OnChangeAvatarResult(*pResp);
    delete pResp;
    return 0;
}

// v2 头像图片上传结果：交修改头像对话框（成功则接着发 cmd_change_avatar；失败提示）
LRESULT CMainDlg::OnV2AvatarUploadResult(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //lParam 为 ImageTransfer 工作线程堆分配的 net::AvatarUploadResult，处理后须 delete。
    net::AvatarUploadResult* pResult = (net::AvatarUploadResult*)lParam;
    if (pResult == NULL)
        return 0;
    m_BalloonMainPanel.OnAvatarUploadResult(*pResult);
    delete pResult;
    return 0;
}

// v2 头像图片下载结果：交主面板按 fileid 刷新对应联系人 / 会话节点头像
LRESULT CMainDlg::OnV2AvatarDownloadResult(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //lParam 为 ImageTransfer 工作线程堆分配的 net::AvatarDownloadResult，处理后须 delete。
    net::AvatarDownloadResult* pResult = (net::AvatarDownloadResult*)lParam;
    if (pResult == NULL)
        return 0;
    m_BalloonMainPanel.OnAvatarDownloadResult(*pResult);
    delete pResult;
    return 0;
}

// v2 联系人资料变更推送：交主面板更新通讯录缓存里该 userid 的头像并实时重画
LRESULT CMainDlg::OnV2UserInfoChanged(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //lParam 为 IUSocket 接收线程堆分配的 net::UserInfoChangedNotify，处理后须 delete。
    net::UserInfoChangedNotify* pNotify = (net::UserInfoChangedNotify*)lParam;
    if (pNotify == NULL)
        return 0;
    m_BalloonMainPanel.OnUserInfoChanged(*pNotify);
    delete pNotify;
    return 0;
}

// v2 组织架构变更推送：交主面板重新拉一次通讯录。企业里新入职 / 新注册的同事就是
// 靠这条推送在已登录的客户端上"冒"出来的 —— 通讯录本身只在登录时拉过一次。
LRESULT CMainDlg::OnV2OrgStructureChanged(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //lParam 为 IUSocket 接收线程堆分配的 net::OrgStructureChangedNotify，处理后须 delete。
    net::OrgStructureChangedNotify* pNotify = (net::OrgStructureChangedNotify*)lParam;
    if (pNotify == NULL)
        return 0;
    m_BalloonMainPanel.OnOrgStructureChanged(*pNotify);
    delete pNotify;
    return 0;
}

// 切换我的在线状态（主面板状态菜单触发）。wParam = 目标 v2 presence（见 MyPresenceValue，
// 与 net/Protocol.h presence_type 同值）。据当前连接 / 登录态分三路处理。
LRESULT CMainDlg::OnChangeMyPresence(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam)
{
    int presence = (int)wParam;
    //lParam 是本次切换的来源：0（kPresenceChangeManual）= 用户手动选的，
    //1（kPresenceChangeAuto）= "无操作自动离开"功能切的。主面板状态菜单与托盘菜单
    //两个既有入口传的都是 0，语义天然正确，无需改动。
    const bool bAuto = ((int)lParam == kPresenceChangeAuto);

    LOG_INFO("MainDlg::OnChangeMyPresence: target presence=%d, panelStatus=%d, alreadyLogin=%d, auto=%d.",
        presence, m_nMainPanelStatus, m_bAlreadyLogin ? 1 : 0, bAuto ? 1 : 0);

    //维护"当前这个离开是不是自动切过去的"。
    //  · 手动改状态一律清标志 —— 这一条最关键：自动离开期间用户自己切成了忙碌，
    //    那他回到电脑前就不该被擅自切回在线。
    //  · 自动切换时，只有目标是"离开"才置位；自动切回在线要清掉。
    if (bAuto)
    {
        m_bAutoAway = (presence == kMyPresenceAway);
    }
    else
    {
        m_bAutoAway = false;
    }

    //先把主面板的状态胶囊乐观更新成目标状态。这一步必须放在这里而不是各个发起点：
    //切换在线状态有两个入口（主面板状态菜单、系统托盘右键菜单），两者都汇到本函数，
    //只有放在这里，从托盘切状态时胶囊才会跟着变。
    //（原先这步写在 CBalloonMainPanel::ShowStatusMenu 里，托盘那条路走不到，表现为
    // "从托盘切了状态，主面板上的状态没变"。）
    if (m_BalloonMainPanel.IsWindow())
    {
        m_BalloonMainPanel.ApplyPresenceOptimistic(presence);
    }

    //托盘悬停提示的第三行也跟着刷新，否则切完状态去悬停托盘看到的还是旧状态。
    //这里用 ModifyTip 而不是 UpdateTrayIconTip：前者只换提示文字、不动图标，托盘正在
    //闪新消息头像时刷新提示不会把闪烁打断。
    m_TrayIcon.ModifyTip(BuildTrayTipText());

    if (presence == kMyPresenceOffline)
    {
        //【1】先置"用户主动下线"态，再断连接。顺序不能反：OnNetError 据这个标志决定
        //     要不要起自动重连，而关 socket 会引发掉线事件；虽然该事件目前是 PostMessage
        //     投递（处理时本函数已返回），但依赖投递方式是脆的 —— 与 OnLogout 的写法对齐。
        m_nMainPanelStatus = MAINPANEL_STATUS_USERGOOFFLINE;

        //【2】已经在跑的重连定时器必须就地停掉。这是"主动离线"最容易漏的一环：
        //     OnTimer 的重连分支只看定时器在不在，不看登录态，掉线重连过程中选离线的话，
        //     定时器会继续把用户连回服务器（见 OnTimer 里的 ShouldAutoReconnect 守卫）。
        if (m_dwReconnectTimerId != 0)
        {
            KillTimer(m_dwReconnectTimerId);
            m_dwReconnectTimerId = 0;
            LOG_INFO("MainDlg::OnChangeMyPresence: 用户主动离线，已停掉正在运行的重连定时器。");
        }

        //【3】真正下线：立起网络层离线闸门、丢弃排队中的帧、断开 chat/file/img 三条连接。
        m_FMGClient.GoOffline();

        //【4】托盘图标换成灰色的离线图标，提示文字同步。
        RefreshTrayIconForStatus();

        //【5】广播离线态：聊天窗输入区置灰、顶部弹出离线提示条，其余需要联网的入口一并拦住。
        BroadcastConnectionState();

        LOG_INFO("MainDlg::OnChangeMyPresence: 已进入主动离线态。");
        return 0;
    }

    //目标为在线 / 离开 / 勿扰。
    if (m_bAlreadyLogin && m_nMainPanelStatus == MAINPANEL_STATUS_LOGIN)
    {
        //已登录且连接正常：直接发 cmd_change_presence 改状态，服务端向好友广播。
        //SendChangePresence 内部会把期望在线状态记入 m_nRecordOnlineStatus，供后续重连沿用。
        CIUSocket::GetInstance().SendChangePresence(presence);
        //状态变了，托盘图标与提示同步（在线三态目前共用彩色图标，但提示文字要换）。
        RefreshTrayIconForStatus();
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

        LOG_INFO("MainDlg::OnChangeMyPresence: 从离线恢复在线，重新登录中，目标 presence=%d。",
            presence);
        m_FMGClient.GoOnline();
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
LRESULT CMainDlg::OnLogout(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/)
{
    LOG_INFO("MainDlg::OnLogout: 收到登出请求。panelStatus=%d, alreadyLogin=%d.",
        m_nMainPanelStatus, m_bAlreadyLogin ? 1 : 0);

    //二次确认：登出会断开连接并关掉所有聊天窗，误点代价不小。确认框挂在主面板上；
    //主面板尚未创建或已销毁时退回挂到隐藏的 CMainDlg。
    HWND hOwner = m_hWnd;
    if (m_BalloonMainPanel.IsWindow())
        hOwner = m_BalloonMainPanel.m_hWnd;

    if (::MessageBox(hOwner,
                     _T("确定要登出当前账号吗？\r\n登出后将断开连接并关闭所有聊天窗口。"),
                     g_strAppTitle.c_str(),
                     MB_YESNO | MB_ICONQUESTION) != IDYES)
    {
        LOG_INFO("MainDlg::OnLogout: 用户在确认框取消，登出中止。");
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
void CMainDlg::ReturnToLoginWindow(int exitKind, int kickoutReason)
{
    //先置"用户主动下线"态再关连接，压制 OnNetError 里的自动重连 —— 否则关 socket 会被
    //当成掉线触发重连（见 OnNetError 对 MAINPANEL_STATUS_USERGOOFFLINE 的判断）。
    m_nMainPanelStatus = MAINPANEL_STATUS_USERGOOFFLINE;

    //断开 chat / file / img 三条连接，再关掉所有聊天窗等子窗口。
    //（被踢下线那条路径已经 GoOffline 过一次，这里再来一次是幂等的。）
    //
    //这里<u>不能</u>调 m_FMGClient.Uninit()：那是<u>退出进程</u>时才做的整体拆除，在"回登录
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
    m_FMGClient.GoOffline();

    //文件夹传输的会话、清单与断点续传进度都只在本次登录期间有效，登出 / 被踢下线时
    //一并作废：停掉还在跑的收发状态机、回收其工作线程，再清空重试保留期。放在关子窗口
    //之前做 —— 状态机的进度回调是投给代理窗口再转到聊天窗的，窗口先没了会白投一轮。
    foldertransfer::FolderTransferManager::GetInstance().Reset();


    //销毁主面板，避免它残留在屏幕上。下面 StartLogin 重新走登录流程，登录成功后由
    //OnLoginResult 重新 CreateAndShow 一个新的主面板。
    if (m_BalloonMainPanel.IsWindow())
        m_BalloonMainPanel.DestroyWindow();

    //账号与密码要在登录窗里回填（登出 / 被踢后两者都保留），而紧接着的 ClearUserInfo /
    //Reset 会把 m_UserInfo 整个清空，故先留存、清完再写回 —— StartLogin 正是从这两个
    //字段取登录窗的默认账号与默认密码。
    tstring strAccount  = m_FMGClient.m_UserMgr.m_UserInfo.m_strAccount;
    tstring strPassword = m_FMGClient.m_UserMgr.m_UserInfo.m_strPassword;

    m_FMGClient.m_UserMgr.ClearUserInfo();
    m_FMGClient.m_UserMgr.m_UserInfo.Reset();
    m_FMGClient.m_UserMgr.m_UserInfo.m_strAccount  = strAccount;
    m_FMGClient.m_UserMgr.m_UserInfo.m_strPassword = strPassword;

    //回到未登录态。m_bAlreadyLogin 必须复位：托盘菜单据它选形态，不复位的话回到登录界面
    //后托盘右键仍会弹出"在线 / 离线 / 登出"那套已登录菜单。
    m_bAlreadyLogin    = FALSE;
    m_nMainPanelStatus = MAINPANEL_STATUS_NOTLOGIN;

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
    m_TrayIcon.RemoveIcon();

    LOG_INFO("MainDlg::ReturnToLoginWindow: 连接已断开、主面板已销毁，重新弹出登录窗。");

    //重新走登录流程（登录窗为模态）。用户直接关掉登录窗时 StartLogin 返回 false 并已
    //在内部 CloseDialog 退出程序，此处无需再处理其返回值。
    StartLogin(FALSE);
}

// v2 撤回消息应答：只处理失败分支。撤回成功时界面改动统一等服务端随后推来的
// cmd_message_recalled_notify 再做（见 OnV2MessageRecalled），这样本端、对端与多端
// 走的是同一条路径，结果必然一致，也避免应答与推送各改一次造成重复处理。
LRESULT CMainDlg::OnV2RecallResult(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //lParam 为 IUSocket 接收线程堆分配的 net::RecallMessageResponse，处理后须 delete。
    net::RecallMessageResponse* pResp = (net::RecallMessageResponse*)lParam;
    if (pResp == NULL)
        return 0;

    if (pResp->m_code != 0)
    {
        //按服务端返回的 error_code 给出具体原因，别让用户面对一句笼统的"撤回失败"。
        LPCTSTR pszTip = _T("撤回失败，请稍后重试。");
        if (pResp->m_code == kRecallErrTimeout)
            pszTip = _T("该消息发出已超过 2 分钟，无法撤回。");
        else if (pResp->m_code == kRecallErrNotAllowed)
            pszTip = _T("你没有权限撤回这条消息。");
        else if (pResp->m_code == kRecallErrMessageNotExist)
            pszTip = _T("该消息不存在或已被撤回。");

        LOG_ERROR("v2 recall message failed, code=%d, msg=%s.", pResp->m_code, pResp->m_msg.c_str());

        //提示框挂在主面板上；主面板不在时退回隐藏的 CMainDlg。
        HWND hOwner = m_BalloonMainPanel.IsWindow() ? m_BalloonMainPanel.m_hWnd : m_hWnd;
        ::MessageBox(hOwner, pszTip, g_strAppTitle.c_str(), MB_OK | MB_ICONINFORMATION);
    }

    delete pResp;
    return 0;
}

// v2 某条消息被撤回的推送：转交主面板统一处理（把对应气泡改成提示行、刷新会话列表
// 预览、把本地消息库该条标记为已撤回）。撤回者本人也会收到本推送。
LRESULT CMainDlg::OnV2MessageRecalled(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //lParam 为 IUSocket 接收线程堆分配的 net::MessageRecalledNotify，处理后须 delete。
    net::MessageRecalledNotify* pNotify = (net::MessageRecalledNotify*)lParam;
    if (pNotify == NULL)
        return 0;

    m_BalloonMainPanel.OnMessageRecalled(*pNotify);
    delete pNotify;
    return 0;
}

// v2 Pin 消息应答：与撤回同口径 —— 成功时什么都不做（界面改动等 cmd_message_pinned_notify
// 推送），只在失败时据 code 给出具体原因。
LRESULT CMainDlg::OnV2PinResult(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //lParam 为 IUSocket 接收线程堆分配的 net::PinMessageResponse，处理后须 delete。
    net::PinMessageResponse* pResp = (net::PinMessageResponse*)lParam;
    if (pResp == NULL)
        return 0;

    if (pResp->m_code != 0)
    {
        //按服务端返回的 error_code 给出具体原因，别让用户面对一句笼统的"操作失败"。
        //文案要分清"钉"与"取消"两个方向 —— 同一个失败码在两个方向下读起来完全不同。
        const bool bPinning = (pResp->m_pin == 1);
        LPCTSTR pszTip = bPinning ? _T("Pin 失败，请稍后重试。") : _T("取消 Pin 失败，请稍后重试。");
        if (pResp->m_code == kPinErrNotAllowed)
            pszTip = _T("只有钉住这条消息的人和群主 / 管理员才能取消它。");
        else if (pResp->m_code == kPinErrMessageNotExist)
            pszTip = _T("该消息不存在或已被撤回。");
        else if (pResp->m_code == kPinErrNotGroupMember)
            pszTip = _T("你已不在该群聊中。");

        LOG_ERROR("v2 pin message failed, code=%d, msg=%s, ctype=%d, cid=%lld, msgid=%lld, pin=%d.",
                  pResp->m_code, pResp->m_msg.c_str(), pResp->m_ctype,
                  (long long)pResp->m_cid, (long long)pResp->m_msgid, pResp->m_pin);

        //提示框挂在主面板上；主面板不在时退回隐藏的 CMainDlg。
        HWND hOwner = m_BalloonMainPanel.IsWindow() ? m_BalloonMainPanel.m_hWnd : m_hWnd;
        ::MessageBox(hOwner, pszTip, g_strAppTitle.c_str(), MB_OK | MB_ICONINFORMATION);
    }

    delete pResp;
    return 0;
}

// v2 某条消息被 Pin / 取消 Pin 的推送：转交主面板统一处理（落本地消息库标记、刷新
// 消息记录窗；本地没有这条消息时再向服务端补拉一次消息体）。操作者本人也会收到本推送。
LRESULT CMainDlg::OnV2MessagePinned(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //lParam 为 IUSocket 接收线程堆分配的 net::MessagePinnedNotify，处理后须 delete。
    net::MessagePinnedNotify* pNotify = (net::MessagePinnedNotify*)lParam;
    if (pNotify == NULL)
        return 0;

    m_BalloonMainPanel.OnMessagePinned(*pNotify);
    delete pNotify;
    return 0;
}

// v2 被 Pin 消息全集应答：转交主面板把本地库缺的消息补齐，并与这一份对账。
LRESULT CMainDlg::OnV2PinnedMessages(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //lParam 为 IUSocket 接收线程堆分配的 net::PinnedMessagesResult，处理后须 delete。
    net::PinnedMessagesResult* pResult = (net::PinnedMessagesResult*)lParam;
    if (pResult == NULL)
        return 0;

    m_BalloonMainPanel.OnPinnedMessagesArrived(*pResult);
    delete pResult;
    return 0;
}

// v2 平台公告列表应答：交主面板逐条落本地库 announce.db 并刷新铃铛角标。
// 注意与群公告的 OnV2GroupAnnouncements 不是一回事——那个属于某个群，这个属于整个平台。
LRESULT CMainDlg::OnV2Announcements(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //lParam 为 IUSocket 接收线程堆分配的 net::AnnouncementsResult，处理后须 delete。
    net::AnnouncementsResult* pResult = (net::AnnouncementsResult*)lParam;
    if (pResult == NULL)
        return 0;

    m_BalloonMainPanel.OnAnnouncementsArrived(*pResult);
    delete pResult;
    return 0;
}

// v2 平台公告新发布 / 被撤回推送：交主面板落本地库、刷铃铛角标、刷消息盒子窗。
LRESULT CMainDlg::OnV2AnnouncementNotify(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //lParam 为 IUSocket 接收线程堆分配的 net::AnnouncementNotify，处理后须 delete。
    net::AnnouncementNotify* pNotify = (net::AnnouncementNotify*)lParam;
    if (pNotify == NULL)
        return 0;

    m_BalloonMainPanel.OnAnnouncementNotify(*pNotify);
    delete pNotify;
    return 0;
}

// v2 平台公告已读上报应答：成功时界面不需要动（已读在发起时就乐观落了本地库），
// 交主面板仅在失败时留痕，便于日后排查「读过的公告在别的设备上仍显示未读」。
LRESULT CMainDlg::OnV2AnnMarkReadResult(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //lParam 为 IUSocket 接收线程堆分配的 net::AnnounceMarkReadResult，处理后须 delete。
    net::AnnounceMarkReadResult* pResult = (net::AnnounceMarkReadResult*)lParam;
    if (pResult == NULL)
        return 0;

    m_BalloonMainPanel.OnAnnounceMarkReadResult(*pResult);
    delete pResult;
    return 0;
}

// v2 修改在线状态应答：交主面板据 code 保留乐观更新的胶囊或回退提示
LRESULT CMainDlg::OnV2ChangePresenceResult(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //lParam 为 IUSocket 接收线程堆分配的 net::ChangePresenceResponse，处理后须 delete。
    net::ChangePresenceResponse* pResp = (net::ChangePresenceResponse*)lParam;
    if (pResp == NULL)
        return 0;
    m_BalloonMainPanel.OnChangePresenceResult(*pResp);
    delete pResp;
    return 0;
}

// v2 好友在线状态变更推送：交主面板更新通讯录缓存并实时刷新联系人树该好友的状态点
LRESULT CMainDlg::OnV2PresenceChanged(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //lParam 为 IUSocket 接收线程堆分配的 net::PresenceChangedNotify，处理后须 delete。
    net::PresenceChangedNotify* pNotify = (net::PresenceChangedNotify*)lParam;
    if (pNotify == NULL)
        return 0;
    m_BalloonMainPanel.OnPresenceChanged(pNotify->m_userid, pNotify->m_presence);
    delete pNotify;
    return 0;
}

// v2 会话属性多端同步推送：交主面板按新状态刷新会话列表（置顶 / 免打扰 / 删除）
LRESULT CMainDlg::OnV2ConversationChanged(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //lParam 为 IUSocket 接收线程堆分配的 net::ConversationChangedNotify，处理后须 delete。
    net::ConversationChangedNotify* pNotify = (net::ConversationChangedNotify*)lParam;
    if (pNotify == NULL)
        return 0;
    m_BalloonMainPanel.OnConversationChanged(*pNotify);
    delete pNotify;
    return 0;
}

// v2 群管理写操作应答：全部转交主面板，由它按 wParam 里的协议号决定失败提示文案
LRESULT CMainDlg::OnV2GroupOpResult(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam)
{
    //lParam 为 IUSocket 接收线程堆分配的 net::GroupOpResult，处理后须 delete。
    net::GroupOpResult* pResult = (net::GroupOpResult*)lParam;
    if (pResult == NULL)
        return 0;
    m_BalloonMainPanel.OnGroupOpResult((int)wParam, *pResult);
    delete pResult;
    return 0;
}

// v2 待审批加群申请列表应答：交主面板填进群管理窗口的审批页
LRESULT CMainDlg::OnV2GroupJoinRequests(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    net::GroupJoinRequestsResult* pResult = (net::GroupJoinRequestsResult*)lParam;
    if (pResult == NULL)
        return 0;
    m_BalloonMainPanel.OnGroupJoinRequests(*pResult);
    delete pResult;
    return 0;
}

// v2 群敏感词库应答：交主面板填进群权限页的词库管理
LRESULT CMainDlg::OnV2SensitiveWords(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    net::SensitiveWordsResult* pResult = (net::SensitiveWordsResult*)lParam;
    if (pResult == NULL)
        return 0;
    m_BalloonMainPanel.OnSensitiveWords(*pResult);
    delete pResult;
    return 0;
}

// v2 群公告列表应答：交主面板分给群管理窗的公告页与群聊窗的公告边栏
LRESULT CMainDlg::OnV2GroupAnnouncements(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    net::GroupAnnouncementsResult* pResult = (net::GroupAnnouncementsResult*)lParam;
    if (pResult == NULL)
        return 0;
    m_BalloonMainPanel.OnGroupAnnouncements(*pResult);
    delete pResult;
    return 0;
}

// v2 群公告变更推送：交主面板刷新公告界面；新发布时还要弹一次提示
LRESULT CMainDlg::OnV2AnnouncementChanged(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    net::GroupAnnouncementNotify* pNotify = (net::GroupAnnouncementNotify*)lParam;
    if (pNotify == NULL)
        return 0;
    m_BalloonMainPanel.OnAnnouncementChanged(*pNotify);
    delete pNotify;
    return 0;
}

// v2 群成员变动推送：交主面板刷新群管理窗口、群聊窗成员边栏与会话列表
LRESULT CMainDlg::OnV2GroupMemberChanged(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    net::GroupMemberChangedNotify* pNotify = (net::GroupMemberChangedNotify*)lParam;
    if (pNotify == NULL)
        return 0;
    m_BalloonMainPanel.OnGroupMemberChanged(*pNotify);
    delete pNotify;
    return 0;
}

// v2 群资料变更推送：交主面板刷新群名与各页；群已解散时关掉相关窗口
LRESULT CMainDlg::OnV2GroupInfoChanged(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    net::GroupInfoChangedNotify* pNotify = (net::GroupInfoChangedNotify*)lParam;
    if (pNotify == NULL)
        return 0;
    m_BalloonMainPanel.OnGroupInfoChanged(*pNotify);
    delete pNotify;
    return 0;
}

// v2 有人申请加群推送：交主面板让审批入口出现待处理红点
LRESULT CMainDlg::OnV2JoinGroupRequest(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    net::GroupJoinRequestItem* pNotify = (net::GroupJoinRequestItem*)lParam;
    if (pNotify == NULL)
        return 0;
    m_BalloonMainPanel.OnJoinGroupRequestNotify(*pNotify);
    delete pNotify;
    return 0;
}

// v2 群资料应答：交主面板写群信息缓存并刷新群名
LRESULT CMainDlg::OnV2GroupInfo(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //lParam 为 IUSocket 接收线程堆分配的 net::GroupInfoResult，处理后须 delete。
    net::GroupInfoResult* pResult = (net::GroupInfoResult*)lParam;
    if (pResult == NULL)
        return 0;
    if (pResult->m_code == 0)
        m_BalloonMainPanel.OnGroupInfoArrived(*pResult);
    else
        LOG_ERROR("v2 group-info response error, code=%d, msg=%s.",
                  pResult->m_code, pResult->m_msg.c_str());
    delete pResult;
    return 0;
}

// v2 群成员应答：交主面板路由到对应群聊窗填成员侧栏
LRESULT CMainDlg::OnV2GroupMembers(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //lParam 为 IUSocket 接收线程堆分配的 net::GroupMembersResult，处理后须 delete。
    net::GroupMembersResult* pResult = (net::GroupMembersResult*)lParam;
    if (pResult == NULL)
        return 0;
    if (pResult->m_code == 0)
        m_BalloonMainPanel.OnGroupMembersArrived(*pResult);
    else
        LOG_ERROR("v2 group-members response error, code=%d, msg=%s.",
                  pResult->m_code, pResult->m_msg.c_str());
    delete pResult;
    return 0;
}

// v2 群写操作通用应答（置顶/免打扰/改群名片/退群）：wParam=code，lParam=堆上 std::string* msg。
// 改群名片(SET_MEMBER_CARD)交群资料对话框处理（成功无动作；失败从快照回退我的群名片 +
// 弹提示，这才是用户要的"修改失败再还原"）。其余两个（置顶/免打扰、退群）乐观更新已在
// 发起端生效，这里只记日志、失败给通用提示（应答不带 id，无法精确回滚，下次同步会纠正）。
LRESULT CMainDlg::OnV2SetFavoriteResult(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    net::SetFavoriteResult* pResult = (net::SetFavoriteResult*)lParam;
    if (pResult == NULL)
    {
        return 0;
    }

    LOG_INFO("MainDlg::OnV2SetFavoriteResult: code=%d, ctype=%d, cid=%lld, favorite=%d.",
        pResult->m_code, pResult->m_ctype, (long long)pResult->m_cid, pResult->m_favorite);

    //转交主面板：成功什么都不用做（乐观更新已经生效），失败要回滚并提示。
    if (m_BalloonMainPanel.IsWindow())
    {
        m_BalloonMainPanel.OnSetFavoriteResult(*pResult);
    }

    delete pResult;
    return 0;
}

LRESULT CMainDlg::OnV2SetStarredResult(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    net::SetStarredResult* pResult = (net::SetStarredResult*)lParam;
    if (pResult == NULL)
    {
        return 0;
    }

    LOG_INFO("MainDlg::OnV2SetStarredResult: code=%d, target=%lld, starred=%d.",
        pResult->m_code, (long long)pResult->m_target, pResult->m_starred);

    //转交主面板：成功什么都不用做（乐观更新已经生效），失败要回滚并提示。
    if (m_BalloonMainPanel.IsWindow())
    {
        m_BalloonMainPanel.OnSetStarredResult(*pResult);
    }

    delete pResult;
    return 0;
}

LRESULT CMainDlg::OnV2SetRemarkResult(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    net::SetFriendRemarkResult* pResult = (net::SetFriendRemarkResult*)lParam;
    if (pResult == NULL)
    {
        return 0;
    }

    //备注名可能含用户隐私，日志只记结果码与目标，不记内容。
    LOG_INFO("MainDlg::OnV2SetRemarkResult: code=%d, userid=%lld.",
        pResult->m_code, (long long)pResult->m_userid);

    //转交主面板：成功什么都不用做（乐观更新已经生效），失败要回滚并提示。
    if (m_BalloonMainPanel.IsWindow())
    {
        m_BalloonMainPanel.OnSetFriendRemarkResult(*pResult);
    }

    delete pResult;
    return 0;
}

LRESULT CMainDlg::OnV2SetTagsResult(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    net::SetContactTagsResult* pResult = (net::SetContactTagsResult*)lParam;
    if (pResult == NULL)
    {
        return 0;
    }

    //标签可能含用户隐私，日志只记结果码、目标与条数，不记内容。
    LOG_INFO("MainDlg::OnV2SetTagsResult: code=%d, userid=%lld, tagcount=%d.",
        pResult->m_code, (long long)pResult->m_userid, (int)pResult->m_tags.size());

    //转交主面板：它拿应答回带的列表覆盖本地缓存并刷新资料窗。
    if (m_BalloonMainPanel.IsWindow())
    {
        m_BalloonMainPanel.OnSetContactTagsResult(*pResult);
    }

    delete pResult;
    return 0;
}

LRESULT CMainDlg::OnV2GroupWriteResult(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    int code = (int)wParam;
    std::string* pMsg = (std::string*)lParam;
    std::string msg = (pMsg != NULL) ? *pMsg : std::string();
    delete pMsg;

    // 改群名片：转交群资料对话框做"成功保持 / 失败回退"。
    if (uMsg == FMG_MSG_V2_SET_MEMBER_CARD_RESULT)
    {
        CString wmsg;
        if (!msg.empty())
        {
            int n = ::MultiByteToWideChar(CP_UTF8, 0, msg.c_str(), -1, NULL, 0);
            if (n > 0)
            {
                ::MultiByteToWideChar(CP_UTF8, 0, msg.c_str(), -1, wmsg.GetBuffer(n), n);
                wmsg.ReleaseBuffer();
            }
        }
        m_BalloonMainPanel.OnGroupCardSaveResult(code, wmsg);
        return 0;
    }

    if (code == 0)
    {
        LOG_INFO("v2 group write-op ok, uiMsg=%u.", uMsg);
        return 0;
    }

    LOG_ERROR("v2 group write-op failed, uiMsg=%u, code=%d, msg=%s.", uMsg, code, msg.c_str());
    ::MessageBox(m_hWnd, _T("操作未能完成，请稍后重试。"), _T("FlamingoIM"),
                 MB_OK | MB_ICONWARNING);
    return 0;
}

LRESULT CMainDlg::OnV2GroupFiles(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //lParam 为 IUSocket 接收线程堆分配的 net::GetGroupFilesResult，处理后须 delete。
    //无论成功失败都交主面板按 groupid 路由到群聊窗（失败时群聊窗据 code 收尾本次拉取）。
    net::GetGroupFilesResult* pResult = (net::GetGroupFilesResult*)lParam;
    if (pResult == NULL)
        return 0;
    m_BalloonMainPanel.OnGroupFilesArrived(*pResult);
    delete pResult;
    return 0;
}

LRESULT CMainDlg::OnV2GroupFilesChanged(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //lParam 为 IUSocket 接收线程堆分配的 net::GroupFilesChangedNotify，处理后须 delete。
    net::GroupFilesChangedNotify* pNotify = (net::GroupFilesChangedNotify*)lParam;
    if (pNotify == NULL)
        return 0;
    m_BalloonMainPanel.OnGroupFilesChanged(*pNotify);
    delete pNotify;
    return 0;
}

LRESULT CMainDlg::OnV2GroupFileUploadResult(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //lParam 为 FileTransfer 工作线程堆分配的 net::GroupFileUploadResult，处理后须 delete。
    net::GroupFileUploadResult* pResult = (net::GroupFileUploadResult*)lParam;
    if (pResult == NULL)
        return 0;
    m_BalloonMainPanel.OnGroupFileUploadResult(*pResult);
    delete pResult;
    return 0;
}

LRESULT CMainDlg::OnV2GroupFileDownloadResult(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //lParam 为 FileTransfer 工作线程堆分配的 net::GroupFileDownloadResult，处理后须 delete。
    net::GroupFileDownloadResult* pResult = (net::GroupFileDownloadResult*)lParam;
    if (pResult == NULL)
        return 0;
    m_BalloonMainPanel.OnGroupFileDownloadResult(*pResult);
    delete pResult;
    return 0;
}

LRESULT CMainDlg::OnV2GroupFileUploadProgress(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //lParam 为 FileTransfer 工作线程堆分配的 net::GroupFileUploadProgress，处理后须 delete。
    net::GroupFileUploadProgress* pProg = (net::GroupFileUploadProgress*)lParam;
    if (pProg == NULL)
        return 0;
    m_BalloonMainPanel.OnGroupFileUploadProgress(*pProg);
    delete pProg;
    return 0;
}

LRESULT CMainDlg::OnV2GroupFileDownloadProgress(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //lParam 为 FileTransfer 工作线程堆分配的 net::GroupFileDownloadProgress，处理后须 delete。
    net::GroupFileDownloadProgress* pProg = (net::GroupFileDownloadProgress*)lParam;
    if (pProg == NULL)
        return 0;
    m_BalloonMainPanel.OnGroupFileDownloadProgress(*pProg);
    delete pProg;
    return 0;
}

// v2 我的群列表应答：交主面板填"群"Tab（含无消息空群）
LRESULT CMainDlg::OnV2UserGroups(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //lParam 为 IUSocket 接收线程堆分配的 net::UserGroupsResult，处理后须 delete。
    net::UserGroupsResult* pResult = (net::UserGroupsResult*)lParam;
    if (pResult == NULL)
        return 0;
    if (pResult->m_code == 0)
        m_BalloonMainPanel.OnUserGroupsArrived(*pResult);
    else
        LOG_ERROR("v2 user-groups response error, code=%d, msg=%s.",
                  pResult->m_code, pResult->m_msg.c_str());
    delete pResult;
    return 0;
}

// v2 历史消息应答：交主面板按 (ctype,cid) 路由到对应聊天窗渲染历史
LRESULT CMainDlg::OnV2SyncMessages(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //lParam 为 IUSocket 接收线程堆分配的 net::SyncMessagesResult，处理后须 delete。
    net::SyncMessagesResult* pResult = (net::SyncMessagesResult*)lParam;
    if (pResult == NULL)
        return 0;
    if (pResult->m_code == 0)
        m_BalloonMainPanel.OnSyncMessagesArrived(*pResult);
    else
        LOG_ERROR("v2 sync-messages response error, code=%d.", pResult->m_code);
    delete pResult;
    return 0;
}

LRESULT CMainDlg::OnV2RecvChatMsg(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //lParam 为堆分配的 net::MessageNotify，路由到对应单聊窗后须 delete。
    net::MessageNotify* pNotify = (net::MessageNotify*)lParam;
    if (pNotify == NULL)
        return 0;
    m_BalloonMainPanel.OnRecvChatMessage(*pNotify);
    delete pNotify;
    return 0;
}

// v2 有应在托盘闪烁的新消息：把发送方会话记入 m_trayFlash，重建当前最近一条的
// 头像图标，并（必要时）启动闪烁定时器。lParam 为主面板堆分配的 TrayFlashData。
LRESULT CMainDlg::OnV2TrayFlash(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
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

    // 闪烁定时器未起则起一个（复用收消息闪烁定时器 id）。已在闪则沿用，避免重复 SetTimer。
    const bool bTimerAlreadyOn = (0 != m_dwMsgTimerId);
    if (!bTimerAlreadyOn)
    {
        // 闪烁快慢跟随系统的文本光标闪烁速度，但那个值必须过一遍 ClampTrayFlashInterval：
        // 用户把系统的"文本光标闪烁"关掉时 ::GetCaretBlinkTime() 返回 INFINITE，原样
        // 交给 SetTimer 会被夹成约 24.8 天，托盘就一次都不闪、且毫无提示。
        const UINT uBlink = ::GetCaretBlinkTime();
        const UINT uInterval = balloonmain::ClampTrayFlashInterval(uBlink);
        m_dwMsgTimerId = SetTimer(RECVCHATMSG_TIMER_ID, uInterval, NULL);
        LOG_INFO("托盘闪烁定时器已启动: timerId=%u, 系统光标闪烁=%u ms, 实际间隔=%u ms。",
                 (UINT)m_dwMsgTimerId, uBlink, uInterval);
    }

    // 首次 tick 打一条日志用（每次重新起闪都复位），确认定时器真的在跑。
    m_bTrayFlashTickLogged = false;

    LOG_INFO("收到托盘闪烁请求: ctype=%d, cid=%lld, 集合内会话数=%d, 图标=%p, "
             "定时器原本就在跑=%d, 登录动画定时器=%u。",
             nCtype, nCid, m_trayFlash.GetConversationCount(), m_hMsgIcon,
             bTimerAlreadyOn ? 1 : 0, (UINT)m_dwLoginTimerId);

    return 0;
}

// v2 某会话已读（被打开 / 在列表里点开）：从 m_trayFlash 移除该会话。若仍有别的未读，
// 目标可能切到次新，重建头像图标继续闪；若再无未读，则停闪并恢复默认托盘图标。
LRESULT CMainDlg::OnV2TrayClear(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam)
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
    LOG_INFO("托盘清除会话: ctype=%d, cid=%lld, 清除后仍有未读=%d。",
             nCtype, (long long)nCid, bStillFlashing ? 1 : 0);

    if (bStillFlashing)
        UpdateTrayFlashIcon();      // 目标可能已切换，重建图标继续闪
    else
        StopTrayFlash();            // 再无未读：停闪并恢复默认图标

    return 0;
}

// "已最小化到托盘"气泡提示的停留时长（毫秒）。比掉线提示（3 秒）略长一点，
// 因为这条提示要让用户看清"去托盘找它"这句话。Vista 及以后系统会按自己的
// 策略统一控制实际停留时间，这个值只作参考。
static const UINT kTrayMinimizeTipTimeoutMs = 5000;

// 主面板最小化隐藏到托盘：弹一次气泡告诉用户主面板去哪儿了、怎么唤回。
// "该账号是否已经提示过"由主面板据 CUserConfig 判定并落盘，本处收到即弹。
// 用 ShowBalloonTip 而非 ModifyIcon：只弹气泡不动图标，托盘正在闪新消息头像时
// 也不会打断闪烁。
LRESULT CMainDlg::OnTrayMinimizeTip(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/)
{
    m_TrayIcon.ShowBalloonTip(_T("温馨提示"),
        _T("FlamingoIM 已最小化到托盘，单击托盘图标可重新打开主面板。"),
        kTrayMinimizeTipTimeoutMs);

    LOG_INFO("Tray balloon tip shown: main panel minimized to tray.");
    return 0;
}

LRESULT CMainDlg::OnCallStartInvite(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam)
{
    //单聊窗"语音通话"按钮发起：wParam=对端 userid，lParam=堆 std::wstring* peerName。
    //发起纯语音呼叫并弹"呼叫中"对话框；会话 id 待邀请应答回来后回填。
    int64_t peerId = (int64_t)(UINT)wParam;
    std::wstring* pName = (std::wstring*)lParam;
    CString peerName = (pName != NULL) ? pName->c_str() : _T("");
    if (pName != NULL)
        delete pName;

    if (peerId == 0)
        return 0;

    //本端发起纯语音呼叫：记下本次通话媒体类型，供接通后 signaling 决定中转会话是否起视频
    m_callMediaType = net::call_media_audio;
    CIUSocket::GetInstance().SendCallInvite(peerId, net::call_media_audio);
    //告知对话框本次是纯语音（标题栏与提示文字据此措辞；也清掉上一通视频通话留下的标志）
    m_callDlg.SetVideoCall(false);
    m_callDlg.OpenCalling(NULL, peerName, peerId);
    return 0;
}

LRESULT CMainDlg::OnCallStartVideoInvite(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam)
{
    //单聊窗"视频通话"按钮发起：wParam=对端 userid，lParam=堆 std::wstring* peerName。
    //发起音视频呼叫并弹"呼叫中"对话框；接通后 signaling 据 m_callMediaType 起视频引擎。
    int64_t peerId = (int64_t)(UINT)wParam;
    std::wstring* pName = (std::wstring*)lParam;
    CString peerName = (pName != NULL) ? pName->c_str() : _T("");
    if (pName != NULL)
        delete pName;

    if (peerId == 0)
        return 0;

    //本端发起视频呼叫：记下媒体类型为视频，发 cmd_call_invite(media=video)
    m_callMediaType = net::call_media_video;
    CIUSocket::GetInstance().SendCallInvite(peerId, net::call_media_video);
    //告知对话框本次是视频通话：呼叫中的标题栏与提示文字据此写成"视频通话"
    m_callDlg.SetVideoCall(true);
    m_callDlg.OpenCalling(NULL, peerName, peerId);
    return 0;
}

LRESULT CMainDlg::OnV2CallInviteAck(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //邀请应答：成功把会话 id 回填给"呼叫中"对话框（取消时要用）；失败则关对话框并提示。
    net::CallInviteResponse* pResp = (net::CallInviteResponse*)lParam;
    if (pResp == NULL)
        return 0;

    bool bMuted = m_FMGClient.m_UserConfig.IsEnableMute() ? true : false;

    if (pResp->m_code == 0)
    {
        m_callDlg.SetSessionId(pResp->m_sessionId);
        //服务端已受理呼叫、对方开始响铃：本端起回铃音，循环到接通 / 被拒 / 超时 / 取消。
        //
        //只在呼叫窗还在时才起：铃声的停止统一挂在通话窗销毁上（见 BalloonCallDlg::OnDestroyMsg），
        //窗口已经没了还起铃声，就再没有任何路径会去停它。用户在 ack 回来之前就关掉呼叫窗时
        //会走到这里 —— 那时 sessionId 还没回填，关窗连取消信令都发不出去。
        if (m_callDlg.IsWindow())
            balloonmain::StartLoopSound(balloonmain::kNotifySoundCallOutgoing, bMuted);
        else
            LOG_INFO("v2 call invite ack arrived after dialog closed, skip ringback.");
    }
    else
    {
        //呼叫没起来：停掉可能已经起的回铃，播一声忙音。
        balloonmain::StopLoopSound();
        balloonmain::PlayNotifySound(balloonmain::kNotifySoundCallBusy, bMuted);
        m_callDlg.CloseDlg();
        ::MessageBox(m_hWnd, _T("对方当前无法接听（不在线或正在通话中）。"),
                     _T("语音通话"), MB_OK | MB_ICONINFORMATION);
    }
    delete pResp;
    return 0;
}

LRESULT CMainDlg::OnV2CallInviteNotify(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //被叫收到来电：弹来电对话框（接听 / 拒绝由对话框直接发 cmd_call_response）。
    //昵称以 UTF-8 传来，转成宽字符再显示。
    net::CallInviteNotify* pNotify = (net::CallInviteNotify*)lParam;
    if (pNotify == NULL)
        return 0;

    //记下来电的媒体类型：被叫接通后 signaling 据此决定中转会话是否起视频
    m_callMediaType = pNotify->m_media;
    CString fromName(CA2W(pNotify->m_fromName.c_str(), CP_UTF8));
    //告知对话框本次来电是视频还是纯语音：被叫在接听前就能从提示文字看出对方约的是视频通话
    //（视频通话接听后会打开摄像头）
    m_callDlg.SetVideoCall(pNotify->m_media == net::call_media_video);
    m_callDlg.OpenIncoming(NULL, fromName, pNotify->m_sessionId, pNotify->m_fromId);
    //来电铃声：循环到接听 / 拒绝 / 对方取消 / 超时。
    balloonmain::StartLoopSound(balloonmain::kNotifySoundCallIncoming,
                                m_FMGClient.m_UserConfig.IsEnableMute() ? true : false);
    delete pNotify;
    return 0;
}

LRESULT CMainDlg::OnV2CallSignaling(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //中转参数下发：双方接通。切到"通话中"形态，并据下发的中转地址 / 令牌 / 角色启动
    //中转会话连上 imgserver 中转通道。第一期中转会话在通道上收发测试帧验证字节透传，
    //连通 / 透传校验 / 失败 / 断开等结果经 FMG_MSG_CALL_RELAY_EVENT 回报；第二期此处
    //承载真实音频采集 / 编码 / 播放管线。
    net::CallSignalingInfo* pInfo = (net::CallSignalingInfo*)lParam;
    if (pInfo == NULL)
        return 0;

    //据本次通话媒体类型决定中转会话是否起视频（视频通话才开摄像头采集 + H.264 编解码 + 视频解码线程）
    bool enableVideo = (m_callMediaType == net::call_media_video);

    //视频通话：先告知通话窗用视频布局（须在 OpenConnected 前），再把通话窗作为视频帧显示去向
    //注入中转会话（远端 / 本端帧解出后经它画到通话窗）。
    m_callDlg.SetVideoCall(enableVideo);
    //接通：停掉回铃 / 来电铃声，播一声接通提示。
    balloonmain::StopLoopSound();
    balloonmain::PlayNotifySound(balloonmain::kNotifySoundCallConnected,
                                 m_FMGClient.m_UserConfig.IsEnableMute() ? true : false);
    m_callDlg.OpenConnected(NULL);
    if (enableVideo)
        m_callMedia.SetVideoDisplaySink(m_callDlg.GetVideoDisplaySink());

    m_callMedia.Start(pInfo->m_relayIp, pInfo->m_relayPort, pInfo->m_token,
                      pInfo->m_role, pInfo->m_sessionId, m_hWnd, FMG_MSG_CALL_RELAY_EVENT,
                      enableVideo);

    delete pInfo;
    return 0;
}

// 通话窗关窗时请求停掉媒体会话。
//
// 为什么需要这条消息：通话窗自己不持有 CallMediaSession（持有者是 CMainDlg），用户点
// 通话窗标题栏关闭按钮时它只发挂断信令 + 销毁自身，媒体会话仍在跑 —— 采集线程不停，
// 摄像头灯就一直亮着。正常挂断另有一条路径（服务端回推 ended，OnV2CallStateNotify 里
// 停会话），但那依赖推送按时到达；网络异常或推送延迟时就漏掉了。本消息把"关窗即停采集"
// 变成不依赖网络的确定行为。
//
// CallMediaSession::Stop 自身幂等（m_running.exchange 保证），故与 ended 推送那条路径
// 重复调用无害。
LRESULT CMainDlg::OnCallStopMedia(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/)
{
    LOG_INFO("MainDlg::OnCallStopMedia: 通话窗关窗，停掉媒体会话释放摄像头 / 麦克风。");
    //关窗兜底：铃声停不下来比多调一次糟得多，这里再停一次（无声在播时是空操作）。
    balloonmain::StopLoopSound();
    m_callMedia.Stop();
    return 0;
}

// v2 切换通话媒体类型的应答：只处理失败分支。成功时界面与引擎改动统一等服务端随后
// 推来的 cmd_call_media_changed_notify 再做（见 OnV2CallMediaChanged），这样本端与对端
// 走同一条路径，媒体类型必然一致。
LRESULT CMainDlg::OnV2CallSwitchMediaResult(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //lParam 为 IUSocket 接收线程堆分配的 net::CallSwitchMediaResponse，处理后须 delete。
    net::CallSwitchMediaResponse* pResp = (net::CallSwitchMediaResponse*)lParam;
    if (pResp == NULL)
        return 0;

    if (pResp->m_code != 0)
    {
        //按服务端返回的 error_code 给出具体原因，别让用户面对一句笼统的"切换失败"。
        LPCTSTR pszTip = _T("切换失败，请稍后重试。");
        if (pResp->m_code == kCallErrNotExist)
            pszTip = _T("通话已结束，无法切换。");
        else if (pResp->m_code == kCallErrNotMember)
            pszTip = _T("你不是本次通话的参与方。");
        else if (pResp->m_code == kCallErrStateInvalid)
            pszTip = _T("通话尚未接通，接通后才能切换。");

        LOG_ERROR("v2 call switch media failed, code=%d, msg=%s.",
                  pResp->m_code, pResp->m_msg.c_str());

        HWND hOwner = m_callDlg.IsWindow() ? m_callDlg.m_hWnd : m_hWnd;
        ::MessageBox(hOwner, pszTip, g_strAppTitle.c_str(), MB_OK | MB_ICONINFORMATION);
    }

    delete pResp;
    return 0;
}

// v2 通话媒体类型已变更的推送：双方（含发起切换者本人）都会收到，本端据此落地。
//
// 按已定交互模型（见 docs/音视频通话-媒体切换方案.md 第 3.1 节）：升级到视频时，
// <u>只有发起方自动打开摄像头</u>，另一方进入视频界面但摄像头保持关闭，由其自己决定
// 何时开启；降级回语音时双方摄像头都关。
LRESULT CMainDlg::OnV2CallMediaChanged(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //lParam 为 IUSocket 接收线程堆分配的 net::CallMediaChangedNotify，处理后须 delete。
    net::CallMediaChangedNotify* pNotify = (net::CallMediaChangedNotify*)lParam;
    if (pNotify == NULL)
        return 0;

    //推送可能晚于通话结束到达（如切换在途时对端挂断）：此时通话窗已关，直接丢弃，
    //不能去改一个已经不存在的窗口。挂断优先于切换。
    if (!m_callDlg.IsWindow() || m_callDlg.GetSessionId() != pNotify->m_sessionId)
    {
        LOG_INFO("v2 call media changed ignored (dialog closed or session mismatch), session=%s.",
                 pNotify->m_sessionId.c_str());
        delete pNotify;
        return 0;
    }

    bool bVideo  = (pNotify->m_media == net::call_media_video);
    UINT selfId  = m_FMGClient.m_UserMgr.m_UserInfo.m_uUserID;
    bool bIsSelf = (selfId != 0 && (UINT)pNotify->m_operator == selfId);
    //升级到视频时只有发起方自动开摄像头；降级回语音时两端都不开。
    bool bOpenCamera = bVideo && bIsSelf;

    LOG_INFO("v2 call media changed: session=%s, media=%d, operator=%lld, isSelf=%d, openCamera=%d.",
             pNotify->m_sessionId.c_str(), pNotify->m_media,
             (long long)pNotify->m_operator, bIsSelf ? 1 : 0, bOpenCamera ? 1 : 0);

    //先切引擎再切界面：引擎启停可能失败（摄像头被占用），据其结果决定界面呈现，
    //避免出现"界面已是视频态、实际没有画面且无提示"的不一致。
    bool bEngineOk = m_callMedia.EnableVideo(bVideo && bOpenCamera);
    if (bVideo && bOpenCamera && !bEngineOk)
    {
        //摄像头拿不到：仍进入视频界面（对端可能已在推流），但本端摄像头保持关闭并提示。
        LOG_ERROR("v2 call switch to video: local camera unavailable, keep camera off.");
        bOpenCamera = false;
    }

    m_callDlg.ApplyMediaMode(bVideo, bOpenCamera);

    delete pNotify;
    return 0;
}

LRESULT CMainDlg::OnV2CallStateNotify(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //通话状态推送：接通态由 signaling 驱动；其余状态关对话框并据 state 提示。
    net::CallStateNotify* pState = (net::CallStateNotify*)lParam;
    if (pState == NULL)
        return 0;

    // 接通态在下面的分支里不做收尾，但铃声必须停 —— 原先只在 OnV2CallSignaling 里停，
    // 而那条路径并非每次接通都会走到（实测日志里收到 state=connected 却没有 signaling），
    // 于是回铃 / 来电铃声在接通后仍在响。这里无条件先停一次，最可靠。
    balloonmain::StopLoopSound();

    if (pState->m_state != net::call_state_connected)
    {
        //通话非接通态（结束 / 拒绝 / 取消 / 离线 / 超时 / 忙）：先停中转会话再关对话框。
        //本端挂断也走这里——挂断按钮发 cmd_call_hangup，服务端回推 state(已结束)。
        //
        //关窗前先把对端 userid 与通话时长取出来：CloseDlg 会销毁窗口，之后这两个值就没了。

        UINT    peerId       = (UINT)m_callDlg.GetPeerUserId();
        CString durationText = m_callDlg.GetCallDurationText();

        m_callMedia.Stop();
        m_callDlg.CloseDlg();

        //提示不再用 MessageBox 打断用户，改为在与对端的聊天窗里留一条居中系统提示 ——
        //通话结果本就属于两人之间的会话记录，留在聊天里可回溯，也不必点一下"确定"。
        CString tip;
        switch (pState->m_state)
        {
        case net::call_state_rejected:
            tip = _T("对方拒绝了通话");
            break;
        case net::call_state_canceled:
            tip = _T("对方已取消通话");
            break;
        case net::call_state_peer_offline:
            tip = _T("对方不在线");
            break;
        case net::call_state_timeout:
            tip = _T("对方无应答");
            break;
        case net::call_state_busy:
            tip = _T("对方正在通话中");
            break;
        case net::call_state_ended:
            //正常结束：接通过就带上时长（"通话结束 05:23"），没接通过只写"通话结束"。
            tip = _T("通话结束");
            if (!durationText.IsEmpty())
            {
                tip += _T(" ");
                tip += durationText;
            }
            break;
        default:
            break;
        }

        //一次性音效：正常结束播挂断音，其余异常结束（被拒 / 无应答 / 忙 / 离线 / 取消）播忙音。
        bool bMutedNow = m_FMGClient.m_UserConfig.IsEnableMute() ? true : false;
        if (pState->m_state == net::call_state_ended)
            balloonmain::PlayNotifySound(balloonmain::kNotifySoundCallEnded, bMutedNow);
        else
            balloonmain::PlayNotifySound(balloonmain::kNotifySoundCallBusy, bMutedNow);

        if (!tip.IsEmpty())
        {
            LOG_INFO("通话结束提示写入聊天窗: peer=%u, state=%d.", peerId, pState->m_state);
            m_BalloonMainPanel.AppendCallTipToChat(peerId, tip);
        }
    }
    delete pState;
    return 0;
}

LRESULT CMainDlg::OnCallRelayEvent(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam)
{
    //通话中转会话事件（来自 call::CallMediaSession 接收线程，已 PostMessage 转投本 UI 线程）。
    //第一期主要用于日志与端到端联调核对：接通态 UI 由 signaling 驱动，这里不改 UI、也不弹窗
    //打扰；中转失败 / 断开仅记日志（信令侧仍由挂断 / 状态推送收尾）。第二期可据此驱动通话窗
    //的媒体状态指示。
    int eventCode = (int)wParam;
    switch (eventCode)
    {
    case call::call_relay_connected:
        //本端已连上中转并握手成功、发出首帧测试帧
        LOG_INFO("Call relay connected: local channel ready.");
        break;
    case call::call_relay_test_ok:
        //收到对端测试帧且逐字节校验一致：字节透传端到端验证通过
        LOG_INFO("Call relay transport verified: test frame seq=%u.", (unsigned)lParam);
        break;
    case call::call_relay_failed:
        //中转连接 / 握手失败：通道未建立
        LOG_ERROR("Call relay connect/handshake failed.");
        break;
    case call::call_relay_closed:
        //中转异常断开：通话期间通道中断
        LOG_INFO("Call relay channel closed unexpectedly.");
        break;
    case call::call_relay_camera_unavailable:
        //本端摄像头不可用（没有设备 / 打不开 / 采集中途被别的程序抢走）：通话不中断，
        //只在通话窗本端那一路画面上给一句提示，免得用户对着黑屏猜。lParam 是失败原因。
        LOG_ERROR("Call local camera unavailable, reason=%d.", (int)lParam);
        m_callDlg.SetCameraUnavailable((int)lParam);
        break;
    default:
        break;
    }
    return 0;
}

LRESULT CMainDlg::OnRdStartInvite(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam)
{
    //聊天窗显示器按钮发起：wParam=对端 userid，lParam=堆 std::wstring* peerName。
    //协调器统一发 cmd_rd_invite 并弹"呼叫中"对话框；会话 id 待邀请应答回来后回填。
    int64_t peerId = (int64_t)(UINT)wParam;
    std::wstring* pName = (std::wstring*)lParam;
    CString peerName = (pName != NULL) ? pName->c_str() : _T("");
    if (pName != NULL)
        delete pName;

    if (peerId == 0)
        return 0;

    CIUSocket::GetInstance().SendRemoteDesktopInvite(peerId);
    m_rdDlg.OpenCalling(NULL, peerName, peerId);
    return 0;
}

LRESULT CMainDlg::OnRdControlledEndByUser(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/)
{
    //被控端用户点受控提示条"结束控制"：主动发 cmd_rd_close 结束会话（服务端会广播
    //cmd_rd_state_notify 给控制端收尾），随后停本端推流、关受控条、清会话 id。与服务端
    //推来的 state_notify 收尾走同一组动作，重复调用幂等。
    //远程桌面会话收尾是关键路径，留一条日志便于排查
    LOG_INFO("远程桌面：被控端用户主动结束控制，sessionId=%s。", m_rdControlledSessionId.c_str());
    if (!m_rdControlledSessionId.empty())
        CIUSocket::GetInstance().SendRemoteDesktopClose(m_rdControlledSessionId);
    m_rdStreamer.Stop();
    m_rdControlledBar.Hide();
    m_rdControlledSessionId.clear();
    return 0;
}

LRESULT CMainDlg::OnV2RdInviteAck(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //邀请应答：成功把会话 id 回填给"呼叫中"对话框（取消时要用）；失败则关对话框并提示。
    net::RDInviteResponse* pResp = (net::RDInviteResponse*)lParam;
    if (pResp == NULL)
        return 0;

    if (pResp->m_code == 0)
    {
        m_rdDlg.SetSessionId(pResp->m_sessionId);
    }
    else
    {
        m_rdDlg.CloseDlg();
        ::MessageBox(m_hWnd, _T("对方当前无法接受远程桌面请求。"),
                     _T("远程桌面"), MB_OK | MB_ICONINFORMATION);
    }
    delete pResp;
    return 0;
}

LRESULT CMainDlg::OnV2RdInviteNotify(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //被控端收到邀请：弹授权对话框（接受 / 拒绝由对话框直接发 cmd_rd_response）。
    //昵称以 UTF-8 传来，转成宽字符再显示。
    net::RDInviteNotify* pNotify = (net::RDInviteNotify*)lParam;
    if (pNotify == NULL)
        return 0;

    CString fromName(CA2W(pNotify->m_fromName.c_str(), CP_UTF8));
    //记下控制方昵称：若本端接受授权、随后成为被控端，受控提示条用它显示"正在被 XXX 控制"
    m_rdControlledPeerName = (LPCTSTR)fromName;
    m_rdDlg.OpenAuth(NULL, fromName, pNotify->m_sessionId, pNotify->m_fromId);
    delete pNotify;
    return 0;
}

LRESULT CMainDlg::OnV2RdRelayInfo(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //中转参数下发：双方建立成功。关呼叫 / 授权对话框，按本端角色启动：被控端连中转
    //抓屏推流，控制端开观看窗连中转收画面显示。
    net::RDRelayInfo* pInfo = (net::RDRelayInfo*)lParam;
    if (pInfo == NULL)
        return 0;

    m_rdDlg.CloseDlg();

    if (pInfo->m_role == net::rd_role_controlled)
    {
        //本端=被控端：先停上次残留，再连中转抓屏推流
        m_rdStreamer.Stop();
        if (!m_rdStreamer.Start(pInfo->m_relayIp, pInfo->m_relayPort, pInfo->m_token))
        {
            ::MessageBox(m_hWnd, _T("启动屏幕推流失败。"), _T("远程桌面"),
                         MB_OK | MB_ICONWARNING);
        }
        else
        {
            //推流已起：记下会话 id（被控方主动结束时发 cmd_rd_close 用），并在屏幕顶部
            //弹出受控提示条，让被控方随时知情、可随手终止远程控制
            m_rdControlledSessionId = pInfo->m_sessionId;
            m_rdControlledBar.Show(m_rdControlledPeerName, m_hWnd);
        }
    }
    else if (pInfo->m_role == net::rd_role_controller)
    {
        //本端=控制端：先关上次残留，再开观看窗连中转收画面
        m_rdViewer.CloseViewer();
        m_rdViewer.Start(pInfo->m_relayIp, pInfo->m_relayPort, pInfo->m_token, pInfo->m_sessionId);
    }

    delete pInfo;
    return 0;
}

LRESULT CMainDlg::OnV2RdStateNotify(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //状态推送：关对话框并据 state 提示（拒绝 / 取消 / 对方离线 / 超时 / 断开）。
    net::RDStateNotify* pState = (net::RDStateNotify*)lParam;
    if (pState == NULL)
        return 0;

    //会话结束：关呼叫 / 授权对话框，停被控端推流并关受控提示条、关控制端观看窗
    //（远程桌面会话状态收尾是关键路径，留一条日志）
    LOG_INFO("远程桌面：会话状态收尾，state=%d。", pState->m_state);
    m_rdDlg.CloseDlg();
    m_rdStreamer.Stop();
    m_rdControlledBar.Hide();
    m_rdControlledSessionId.clear();
    m_rdViewer.CloseViewer();

    //据状态提示；disconnected（正常断开）静默，避免与观看窗 / 推流器自身的断开提示重复
    LPCTSTR tip = NULL;
    switch (pState->m_state)
    {
    case net::rd_state_rejected:
        tip = _T("对方拒绝了远程桌面请求。");
        break;
    case net::rd_state_canceled:
        tip = _T("对方取消了远程桌面请求。");
        break;
    case net::rd_state_peer_offline:
        tip = _T("对方当前不在线。");
        break;
    case net::rd_state_timeout:
        tip = _T("远程桌面请求超时。");
        break;
    default:
        break;
    }
    if (tip != NULL)
        ::MessageBox(m_hWnd, tip, _T("远程桌面"), MB_OK | MB_ICONINFORMATION);

    delete pState;
    return 0;
}

LRESULT CMainDlg::OnV2SendChatAck(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //lParam 为堆分配的 net::SendMessageResponse，处理后须 delete。
    net::SendMessageResponse* pResp = (net::SendMessageResponse*)lParam;
    if (pResp == NULL)
        return 0;
    m_BalloonMainPanel.OnSendChatAck(*pResp);
    delete pResp;
    return 0;
}

LRESULT CMainDlg::OnV2ImageSendProgress(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //lParam 为堆分配的 net::ImageSendProgress，路由后须 delete。
    net::ImageSendProgress* pProg = (net::ImageSendProgress*)lParam;
    if (pProg == NULL)
        return 0;
    m_BalloonMainPanel.OnImageSendProgress(*pProg);
    delete pProg;
    return 0;
}

LRESULT CMainDlg::OnV2ImageSendResult(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //lParam 为堆分配的 net::ImageSendResult，路由后须 delete。
    net::ImageSendResult* pResult = (net::ImageSendResult*)lParam;
    if (pResult == NULL)
        return 0;
    m_BalloonMainPanel.OnImageSendResult(*pResult);
    delete pResult;
    return 0;
}

LRESULT CMainDlg::OnV2ImageDownloadResult(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //lParam 为堆分配的 net::ImageDownloadResult，路由后须 delete。
    net::ImageDownloadResult* pResult = (net::ImageDownloadResult*)lParam;
    if (pResult == NULL)
        return 0;
    m_BalloonMainPanel.OnImageDownloadResult(*pResult);
    delete pResult;
    return 0;
}

LRESULT CMainDlg::OnV2FileSendProgress(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //lParam 为堆分配的 net::FileSendProgress，路由后须 delete。
    net::FileSendProgress* pProg = (net::FileSendProgress*)lParam;
    if (pProg == NULL)
        return 0;
    m_BalloonMainPanel.OnFileSendProgress(*pProg);
    delete pProg;
    return 0;
}

LRESULT CMainDlg::OnV2FileSendResult(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //lParam 为堆分配的 net::FileSendResult，路由后须 delete。
    net::FileSendResult* pResult = (net::FileSendResult*)lParam;
    if (pResult == NULL)
        return 0;
    m_BalloonMainPanel.OnFileSendResult(*pResult);
    delete pResult;
    return 0;
}

LRESULT CMainDlg::OnV2FileDownloadProgress(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //lParam 为堆分配的 net::FileDownloadProgress，路由后须 delete。
    net::FileDownloadProgress* pProg = (net::FileDownloadProgress*)lParam;
    if (pProg == NULL)
        return 0;
    m_BalloonMainPanel.OnFileDownloadProgress(*pProg);
    delete pProg;
    return 0;
}

LRESULT CMainDlg::OnV2FileDownloadResult(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //lParam 为堆分配的 net::FileDownloadResult，路由后须 delete。
    net::FileDownloadResult* pResult = (net::FileDownloadResult*)lParam;
    if (pResult == NULL)
        return 0;
    m_BalloonMainPanel.OnFileDownloadResult(*pResult);
    delete pResult;
    return 0;
}

LRESULT CMainDlg::OnV2FileTransferAck(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //lParam 为堆分配的 net::FileTransferAckNotify，路由后须 delete。
    net::FileTransferAckNotify* pNotify = (net::FileTransferAckNotify*)lParam;
    if (pNotify == NULL)
        return 0;
    //这一环原先没有日志，出问题时无法判断消息是断在窗口消息映射还是断在主面板，
    //补一条使整条链路（收帧 → 转发 → 主面板 → 聊天窗）每一跳都可追。
    LOG_INFO("路由对方收取文件状态: msgid=%lld, state=%d.",
             (long long)pNotify->m_msgid, pNotify->m_state);
    m_BalloonMainPanel.OnFileTransferAck(*pNotify);
    delete pNotify;
    return 0;
}

// =======================================================================
// v2 单聊文件夹传输（cmd_folder_* 1810~1817）
//
// 每个处理器都做同样的三件事，顺序固定不可颠倒：
//   1. 先交 foldertransfer::FolderTransferManager 处理会话生命周期（记会话 id、起 /
//      停状态机、发后续信令）——界面能显示什么，取决于管理器判定的结果；
//   2. 再交主面板路由到对应聊天窗，更新那条文件夹气泡；
//   3. 最后 delete 堆负载。
// =======================================================================

LRESULT CMainDlg::OnV2FolderInviteAck(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam)
{
    //wParam 为本次邀请请求所用的帧 seq，lParam 为堆分配的 net::FolderInviteResponse，
    //处理后须 delete。
    net::FolderInviteResponse* pResp = (net::FolderInviteResponse*)lParam;
    if (pResp == NULL)
        return 0;

    //管理器按帧 seq 找到发起这笔邀请的那一条记录（应答包体里没有任何可对回的字段，而本机
    //可能有多笔传输同时在等应答），据应答记下服务端分配的会话 id（成功）或就地放开名额
    //（失败），并回填一份"界面要用的定位信息"——气泡此刻还挂着本地占位 id，得靠它换成
    //真会话 id。
    foldertransfer::FolderBindNotice bind;
    foldertransfer::FolderTransferManager::GetInstance().OnInviteAck(
        (uint32_t)wParam, *pResp, bind);
    m_BalloonMainPanel.OnFolderBound(bind);

    delete pResp;
    return 0;
}

LRESULT CMainDlg::OnV2FolderInviteNotify(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //lParam 为堆分配的 net::FolderInviteNotify，处理后须 delete。
    net::FolderInviteNotify* pNotify = (net::FolderInviteNotify*)lParam;
    if (pNotify == NULL)
        return 0;

    //本机已有一笔文件夹传输在跑时管理器会就地回一个拒绝并返回 false，此时不必出气泡——
    //出了反而让用户以为还能点「接收」。
    if (foldertransfer::FolderTransferManager::GetInstance().OnInviteNotify(*pNotify))
    {
        LOG_INFO("Route folder invite to chat window, session=%s, from=%lld.",
                 pNotify->m_sessionId.c_str(), (long long)pNotify->m_fromId);
        m_BalloonMainPanel.OnFolderInviteNotify(*pNotify);
    }

    delete pNotify;
    return 0;
}

LRESULT CMainDlg::OnV2FolderSessionAck(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam)
{
    //wParam 为发出的那条命令的协议号（1812 / 1813 / 1814 / 1817），
    //lParam 为堆分配的 net::FolderSessionResponse，处理后须 delete。
    net::FolderSessionResponse* pResp = (net::FolderSessionResponse*)lParam;
    if (pResp == NULL)
        return 0;

    foldertransfer::FolderTransferManager::GetInstance().OnSessionAck((int)wParam, *pResp);

    delete pResp;
    return 0;
}

LRESULT CMainDlg::OnV2FolderStateNotify(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //lParam 为堆分配的 net::FolderStateNotify，处理后须 delete。
    net::FolderStateNotify* pNotify = (net::FolderStateNotify*)lParam;
    if (pNotify == NULL)
        return 0;

    foldertransfer::FolderTransferManager::GetInstance().OnStateNotify(*pNotify);
    m_BalloonMainPanel.OnFolderStateNotify(*pNotify);

    delete pNotify;
    return 0;
}

LRESULT CMainDlg::OnV2FolderRelayInfo(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //lParam 为堆分配的 net::FolderRelayInfo，处理后须 delete。
    net::FolderRelayInfo* pInfo = (net::FolderRelayInfo*)lParam;
    if (pInfo == NULL)
        return 0;

    //中转参数一到就建通道开传，界面上气泡随之转成「传输中」。
    foldertransfer::FolderTransferManager::GetInstance().OnRelayInfo(*pInfo);
    m_BalloonMainPanel.OnFolderTransferStarted(pInfo->m_sessionId);

    delete pInfo;
    return 0;
}

LRESULT CMainDlg::OnV2FolderScanned(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //lParam 为堆分配的 foldertransfer::FolderScanNotice，处理后须 delete。
    foldertransfer::FolderScanNotice* pNotice = (foldertransfer::FolderScanNotice*)lParam;
    if (pNotice == NULL)
        return 0;

    //扫描是本端自己的事，管理器已经在工作线程上处理完了（失败时也已放开名额），
    //这里只负责把结果画到气泡上。
    m_BalloonMainPanel.OnFolderScanned(*pNotice);

    delete pNotice;
    return 0;
}

LRESULT CMainDlg::OnV2FolderSendProgress(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //lParam 为堆分配的 foldertransfer::FolderProgressNotice，处理后须 delete。
    foldertransfer::FolderProgressNotice* pProg = (foldertransfer::FolderProgressNotice*)lParam;
    if (pProg == NULL)
        return 0;

    m_BalloonMainPanel.OnFolderProgress(*pProg);

    delete pProg;
    return 0;
}

LRESULT CMainDlg::OnV2FolderSendResult(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //lParam 为堆分配的 foldertransfer::FolderResultNotice，处理后须 delete。
    foldertransfer::FolderResultNotice* pResult = (foldertransfer::FolderResultNotice*)lParam;
    if (pResult == NULL)
        return 0;

    //收尾必须在 UI 线程做：FinishSend 内部要等状态机的工作线程退出，在状态机自己的
    //回调线程里调等于让它 join 自己（见 IFolderSendSessionHandler 的注释）。
    foldertransfer::FolderTransferManager::GetInstance().FinishSend(*pResult);
    m_BalloonMainPanel.OnFolderResult(*pResult);

    delete pResult;
    return 0;
}

LRESULT CMainDlg::OnV2FolderRecvProgress(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //lParam 为堆分配的 foldertransfer::FolderProgressNotice，处理后须 delete。
    foldertransfer::FolderProgressNotice* pProg = (foldertransfer::FolderProgressNotice*)lParam;
    if (pProg == NULL)
        return 0;

    m_BalloonMainPanel.OnFolderProgress(*pProg);

    delete pProg;
    return 0;
}

LRESULT CMainDlg::OnV2FolderRecvResult(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //lParam 为堆分配的 foldertransfer::FolderResultNotice，处理后须 delete。
    foldertransfer::FolderResultNotice* pResult = (foldertransfer::FolderResultNotice*)lParam;
    if (pResult == NULL)
        return 0;

    //理由同 OnV2FolderSendResult：FinishRecv 会等接收会话的工作线程退出。
    foldertransfer::FolderTransferManager::GetInstance().FinishRecv(*pResult);
    m_BalloonMainPanel.OnFolderResult(*pResult);

    delete pResult;
    return 0;
}

LRESULT CMainDlg::OnV2TypingNotify(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //lParam 为堆分配的 net::TypingNotify，路由后须 delete。
    net::TypingNotify* pNotify = (net::TypingNotify*)lParam;
    if (pNotify == NULL)
        return 0;
    //与文件收取状态同理：这一跳留一条日志，出问题时能判断消息断在哪一层。
    LOG_INFO("路由对端正在输入状态: cid=%lld, state=%d.",
             (long long)pNotify->m_cid, pNotify->m_state);
    m_BalloonMainPanel.OnTypingNotify(*pNotify);
    delete pNotify;
    return 0;
}

// 好友状态改变消息
LRESULT CMainDlg::OnStatusChangeMsg(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    UINT nUTalkUin = (UINT)lParam;
    if (0 == nUTalkUin)
        return 0;

    // 企业版说明：原先这里从 v1 好友列表里取该账号的在线状态，只有"由离线变在线"才放
    // 上线提示音；v1 好友列表已删除，无从判断新旧状态，故本消息不再放提示音。
    // 联系人在线状态改由 v2 推送（cmd_presence_changed_notify → FMG_MSG_V2_PRESENCE_CHANGED）
    // 驱动 BalloonMainPanel 刷新状态点。
    //
    // 旧自绘好友列表控件也已移除，原先这里对 m_BuddyListCtrl 的分组在线人数 / 头像 /
    // 在线状态更新已一并删除。
    return 0;
}

// 被踢下线消息
LRESULT CMainDlg::OnKickMsg(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    LPCTSTR lpszReason = _T("被踢下线！");
    MessageBox(lpszReason, _T("提示"), MB_OK);
    PostMessage(WM_CLOSE);
    return 0;
}

// 群系统消息
LRESULT CMainDlg::OnSysGroupMsg(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    //UINT nGroupCode = (UINT)lParam;
    //if (0 == nGroupCode)
    //	return 0;

    //tstring strFileName = Hootina::CPath::GetAppPath() + _T("Sound\\system.wav");	// 播放来系统消息提示音
    //::sndPlaySound(strFileName.c_str(), SND_ASYNC);

    //UpdateMsgIcon();


    //if (NULL == m_dwMsgTimerId)
    //	m_dwMsgTimerId = SetTimer(2, ::GetCaretBlinkTime(), NULL);

    return 0;
}

// 更新好友号码
LRESULT CMainDlg::OnUpdateBuddyNumber(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    UINT nUTalkUin = (UINT)lParam;
    if (0 == nUTalkUin)
        return 0;


    return 0;
}

// 更新群成员号码
LRESULT CMainDlg::OnUpdateGMemberNumber(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    UINT nGroupCode = (UINT)wParam;
    UINT nUTalkUin = (UINT)lParam;
    if (0 == nGroupCode || 0 == nUTalkUin)
        return 0;


    return 0;
}

// 更新群号码
LRESULT CMainDlg::OnUpdateGroupNumber(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    UINT nGroupCode = (UINT)lParam;
    if (0 == nGroupCode)
        return 0;


    return 0;
}

// 更新好友个性签名
LRESULT CMainDlg::OnUpdateBuddySign(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    UINT nUTalkUin = lParam;
    if (0 == nUTalkUin)
        return 0;

    CBuddyInfo* lpBuddyInfo = m_FMGClient.GetUserInfo(nUTalkUin);
    if (lpBuddyInfo == NULL)
        return 0;

    // 自己的个性签名展示已迁到 BalloonMainPanel（旧自绘 m_btnSign 已移除）；这里只在
    // 更新的是好友（非自己）时通知相关聊天窗 / 信息窗，让其同步显示新签名。
    // 旧好友列表控件 m_BuddyListCtrl 的签名更新已一并删除。
    if (m_FMGClient.m_UserMgr.m_UserInfo.m_uUserID != nUTalkUin)
    {
    }

    return 0;
}

// 更新群成员个性签名
LRESULT CMainDlg::OnUpdateGMemberSign(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    UINT nGroupCode = (UINT)wParam;
    UINT nUTalkUin = (UINT)lParam;
    if (0 == nGroupCode || 0 == nUTalkUin)
        return 0;


    return 0;
}

// 更新用户信息
LRESULT CMainDlg::OnUpdateBuddyInfo(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    //    UINT nUTalkUin = lParam;
    //    if (0 == nUTalkUin)
    //        return 0;
    //
    //    CBuddyInfo* lpBuddyInfo = m_FMGClient.GetUserInfo(nUTalkUin);
    //    if (lpBuddyInfo == NULL)	// 更新用户昵称
    //        return 0;
    //    if (lpBuddyInfo->m_uUserID == m_FMGClient.m_UserMgr.m_UserInfo.m_uUserID)
    //    {
    //        //如果昵称为空，则显示账户名
    //        if (lpBuddyInfo->m_strNickName.empty())
    //            m_staNickName.SetWindowText(lpBuddyInfo->m_strAccount.c_str());
    //        else
    //            m_staNickName.SetWindowText(lpBuddyInfo->m_strNickName.c_str());
    //
    //        //m_staNickName.Invalidate(FALSE);
    //
    //        long nStatus = m_FMGClient.GetStatus();
    //        LoadAppIcon(nStatus);
    //        CString strIconInfo;
    //        if (nStatus == STATUS_OFFLINE)
    //            strIconInfo.Format(_T("%s\r\n%s\r\n离线"), m_FMGClient.m_UserMgr.m_UserInfo.m_strNickName.c_str(), m_FMGClient.m_UserMgr.m_UserInfo.m_strAccount.c_str());
    //        else
    //            strIconInfo.Format(_T("%s\r\n%s\r\n在线"), m_FMGClient.m_UserMgr.m_UserInfo.m_strNickName.c_str(), m_FMGClient.m_UserMgr.m_UserInfo.m_strAccount.c_str());
    //
    //        m_TrayIcon.ModifyIcon(m_hAppIcon, strIconInfo);
    //    }
    //    else
    //    {
    //        int nTeamIndex, nIndex;	// 获取好友项索引
    //        m_BuddyListCtrl.GetItemIndexByID(nUTalkUin, nTeamIndex, nIndex);
    //        m_BuddyListCtrl.SetBuddyItemMarkName(nTeamIndex, nIndex, lpBuddyInfo->m_strMarkName.c_str());
    //        m_BuddyListCtrl.SetBuddyItemNickName(nTeamIndex, nIndex, lpBuddyInfo->m_strNickName.c_str());
    //        m_BuddyListCtrl.Invalidate(FALSE);
    //    }
    //
    //
    //    //同时更新头像信息 
    //    PostMessage(FMG_MSG_UPDATE_BUDDY_NUMBER, 0, (LPARAM)nUTalkUin);
    //    PostMessage(FMG_MSG_UPDATE_BUDDY_HEADPIC, 0, (LPARAM)nUTalkUin);
    //    PostMessage(FMG_MSG_UPDATE_BUDDY_SIGN, 0, (LPARAM)nUTalkUin);
    //
    //    //通知群聊窗口更新
    //    for (const auto& iter : m_FMGClient.m_UserMgr.m_GroupList.m_arrGroupInfo)
    //    {
    //        if (iter->IsMember(nUTalkUin))
    //            PostMessage(FMG_MSG_UPDATE_GROUP_INFO, nUTalkUin, 0);
    //    }
    //    // 通知好友信息窗口更新
    //
    //#ifdef _DEBUG
    //    static long i = 0;
    //    AtlTrace(_T("Update Buddy Info: %d.\n"), i);
    //    i++;
    //#endif
    return 0;
}

// 更新群成员信息
LRESULT CMainDlg::OnUpdateGMemberInfo(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    UINT nGroupCode = (UINT)wParam;
    UINT nUTalkUin = (UINT)lParam;
    if (0 == nGroupCode || 0 == nUTalkUin)
        return 0;


    return 0;
}

// 更新群信息
LRESULT CMainDlg::OnUpdateGroupInfo(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    UINT uAccountID = (UINT)wParam;

    //	CGroupInfo* pGroupInfo = NULL;
    //	CBuddyInfo* pBuddyInfo = NULL;
    //	std::map<UINT, CGroupChatDlg*>::iterator iterGroupChatDlg;
    //	UINT nGroupCode = 0;
    //	for(const auto& iter : m_FMGClient.m_UserMgr.m_GroupList.m_arrGroupInfo)
    //	{
    //		pGroupInfo = iter;
    //		if(pGroupInfo == NULL)
    //			continue;
    //
    //		pBuddyInfo = pGroupInfo->GetMemberByUin(uAccountID);
    //		if(pBuddyInfo == NULL)
    //			continue;
    //		
    //		nGroupCode = pGroupInfo->m_nGroupCode;
    //		iterGroupChatDlg = m_mapGroupChatDlg.find(nGroupCode);
    //		if(iterGroupChatDlg != m_mapGroupChatDlg.end())
    //		{
    //#ifndef _DEBUG
    //			if(iterGroupChatDlg->second->IsWindow())
    //			{
    //#endif
    //				iterGroupChatDlg->second->OnUpdateGroupInfo();
    //#ifndef _DEBUG
    //			}
    //#endif
    //		}
    //		
    //	}

    return 0;
}

// HTREEITEM CMainDlg::FindTreeItemByUTalkUin(CTreeViewCtrl& treeCtrl, UINT nUTalkUin)
// {
// 	HTREEITEM hItem = treeCtrl.GetRootItem();
// 	while (hItem != NULL)
// 	{
// 		UINT nUTalkUin2 = (UINT)treeCtrl.GetItemData(hItem);
// 		if (nUTalkUin2 == nUTalkUin)
// 			return hItem;
// 
// 		HTREEITEM hFindItem = FindTreeItemByUTalkUin2(treeCtrl, hItem, nUTalkUin);
// 		if (hFindItem != NULL)
// 			return hFindItem;
// 
// 		hItem = treeCtrl.GetNextItem(hItem, TVGN_NEXT);
// 	}
// 	return NULL;
// }
// 
// HTREEITEM CMainDlg::FindTreeItemByUTalkUin2(CTreeViewCtrl& treeCtrl, HTREEITEM hItem, UINT nUTalkUin)
// {
// 	if (treeCtrl.ItemHasChildren(hItem))
// 	{
// 		HTREEITEM hChildItem = treeCtrl.GetChildItem(hItem);
// 
// 		while (hChildItem != NULL)
// 		{
// 			UINT nUTalkUin2 = (UINT)treeCtrl.GetItemData(hItem);
// 			if (nUTalkUin2 == nUTalkUin)
// 				return hItem;
// 
// 			HTREEITEM hFindItem = FindTreeItemByUTalkUin2(treeCtrl, hChildItem, nUTalkUin);
// 			if (hFindItem != NULL)
// 				return hFindItem;
// 
// 			hChildItem = treeCtrl.GetNextItem(hChildItem, TVGN_NEXT);
// 		}
// 	}
// 
// 	UINT nUTalkUin2 = (UINT)treeCtrl.GetItemData(hItem);
// 	return  (nUTalkUin2 == nUTalkUin) ? hItem : NULL;
// }

// 更新好友头像图片
LRESULT CMainDlg::OnUpdateBuddyHeadPic(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    UINT nUTalkUin = (UINT)lParam;
    if (nUTalkUin == 0)
        return 0;

    CBuddyInfo* lpBuddyInfo = m_FMGClient.GetUserInfo(nUTalkUin);
    if (lpBuddyInfo == NULL)
        return 0;

    //更新自己的头像（已迁到 BalloonMainPanel 头部，旧自绘 m_picHead 已移除）；
    //好友 / 最近联系人列表头像随旧控件一并移除，新版面板由联系人 / 会话刷新统一处理。
    if (m_FMGClient.m_UserMgr.m_UserInfo.m_uUserID == nUTalkUin)
        return 0;


    return 0;
}

// 更新群成员头像图片
LRESULT CMainDlg::OnUpdateGMemberHeadPic(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    UINT nGroupCode = (UINT)wParam;
    UINT nUTalkUin = (UINT)lParam;


    return 0;
}

// 更新群头像图片
LRESULT CMainDlg::OnUpdateGroupHeadPic(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    UINT nGroupCode = (UINT)wParam;

    // 旧自绘群列表 / 最近联系人列表头像更新已随旧控件移除；群头像在新版面板由群列表 /
    // 会话刷新统一处理。这里只通知已打开的群聊窗同步头像。

    return 0;
}

// 改变在线状态返回消息
LRESULT CMainDlg::OnChangeStatusResult(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    BOOL bSuccess = (BOOL)wParam;
    long nNewStatus = lParam;
    if (!bSuccess)
    {
        MessageBox(_T("改变在线状态失败！"));
        return 0;
    }
    //状态改变成功：托盘图标随新状态更新。
    RefreshTrayIconForStatus();
    return 0;
}

LRESULT CMainDlg::OnShowOrCloseDlg(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    UINT nGroupCode = (UINT)wParam;
    UINT nUTalkUin = (UINT)lParam;

    if (nUTalkUin == m_FMGClient.m_UserMgr.m_UserInfo.m_uUserID)
    {
        PostMessage(WM_COMMAND, (WPARAM)ID_PIC_HEAD, 0);
        return 1;
    }

    switch (uMsg)
    {
    case WM_SHOW_SYSGROUPCHATDLG:
        ShowSysGroupChatDlg(nGroupCode, TRUE);
        break;
    case WM_CLOSE_SYSGROUPCHATDLG:
        ShowSysGroupChatDlg(nGroupCode, FALSE);
        break;
    }
    return 0;
}

LRESULT CMainDlg::OnCloseDlg(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    CloseDialog(IDOK);
    return (LRESULT)1;
}

LRESULT CMainDlg::OnDelMsgSender(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    long nType = (long)wParam;
    UINT nSenderId = (UINT)lParam;


    // 旧自绘好友 / 群列表的头像灭闪已随旧控件移除；仍转发刷新最近联系人（会话）列表。
    if (nType == FMG_MSG_TYPE_BUDDY || nType == FMG_MSG_TYPE_GROUP)
    {
        //更新最近联系人中新消息的数目
        SendMessage(FMG_MSG_UPDATE_RECENT_LIST, 0, 0);
    }

    UpdateMsgIcon();

    //TODO: 以下代码放到UpdateMsgIcon中去
    CMessageList* lpMsgList = m_FMGClient.GetMessageList();
    if (lpMsgList != NULL && lpMsgList->GetMsgSenderCount() <= 0)
    {
        KillTimer(m_dwMsgTimerId);
        m_dwMsgTimerId = NULL;
        m_nLastMsgType = FMG_MSG_TYPE_BUDDY;
        m_nLastSenderId = 0;

        UpdateTrayIconTip();
    }

    return 0;
}

LRESULT CMainDlg::OnCancelFlash(UINT uMsg, WPARAM wParam, LPARAM lParam)
{

    KillTimer(m_dwMsgTimerId);
    m_dwMsgTimerId = NULL;
    //先将任务栏图标恢复成正常状态
    UpdateTrayIconTip();
    return 0;
}

void CMainDlg::ShowSysGroupChatDlg(UINT nGroupCode, BOOL bShow)
{
    /*CString strText, strGroupName, strAdminName, strAdminUin, strMsg;
    UINT nGroupNumber;

    CGroupList* lpGroupList = m_FMGClient.GetGroupList();
    CMessageList* lpMsgList = m_FMGClient.GetMessageList();
    if (NULL == lpGroupList || NULL == lpMsgList)
        return;

    CGroupInfo* lpGroupInfo = lpGroupList->GetGroupByCode(nGroupCode);
    CMessageSender* lpMsgSender = lpMsgList->GetMsgSender(long_SYSGROUP, nGroupCode);
    if (NULL == lpMsgSender)
        return;

    if (lpGroupInfo != NULL)
        strGroupName = lpGroupInfo->m_strName.c_str();

    int nMsgCnt = lpMsgSender->GetMsgCount();
    for (int i = 0; i < nMsgCnt; i++)
    {
        CSysGroupMessage* lpSysGroupMsg = lpMsgSender->GetSysGroupMsg(i);
        if (lpSysGroupMsg != NULL)
        {
            strAdminName = lpSysGroupMsg->m_strAdminNickName.c_str();
            strAdminUin = lpSysGroupMsg->m_strAdminUin.c_str();
            strMsg = lpSysGroupMsg->m_strMsg.c_str();
            nGroupNumber = lpSysGroupMsg->m_nGroupNumber;

            if (lpSysGroupMsg->m_strSubType == _T("group_request_join_agree"))
            {
                LPCTSTR lpFmt = _T("管理员已同意您加入群%s(%u)");
                strText.Format(lpFmt, strGroupName, nGroupNumber);
                MessageBox(strText, _T("提示"), MB_OK);
            }
            else if (lpSysGroupMsg->m_strSubType == _T("group_request_join_deny"))
            {
                LPCTSTR lpFmt = _T("管理员已拒绝您加入群%s(%u)。拒绝理由：%s");
                strText.Format(lpFmt, strGroupName, nGroupNumber, strMsg);
                MessageBox(strText, _T("提示"), MB_OK);
            }
            else if (lpSysGroupMsg->m_strSubType == _T("group_leave"))
            {
                LPCTSTR lpFmt = _T("%s(%u)管理员%s(%s)已将您移除出该群。");
                strText.Format(lpFmt, strGroupName, nGroupNumber, strAdminName, strAdminUin);
                MessageBox(strText, _T("提示"), MB_OK);
            }
        }
    }*/
    //lpMsgList->DelMsgSender(long_SYSGROUP, nGroupCode);
    //OnDelMsgSender(NULL, long_SYSGROUP, nGroupCode);
}

void CMainDlg::OnTrayIcon_LButtunUp()
{
    // v2：托盘正在闪新消息头像 —— 单击打开"最近一条"未读会话。开窗 + 清未读交给
    // 主面板 OpenConversation 完成，它内部 MarkConversationRead 会回投 FMG_MSG_TRAY_CLEAR，
    // 由 OnV2TrayClear 统一停闪 / 切目标 / 恢复默认托盘图标，故本分支不再自行恢复图标、直接返回。
    if (m_trayFlash.IsFlashing())
    {
        balloonmain::TrayFlashEntry target;
        if (m_trayFlash.GetTarget(target) && m_BalloonMainPanel.IsWindow())
            m_BalloonMainPanel.OpenConversation(target.m_ctype, target.m_cid);
        return;
    }
    else
    {
        // 托盘左键单击默认行为：唤出登录窗(登录前) / 主面板(登录后)，含最小化还原。
        ShowLoginOrMainPanel();
    }

    //先将任务栏图标恢复成正常状态
    UpdateTrayIconTip();
}

// 托盘菜单图标尺寸 / 颜色 —— 与左下角汉堡系统菜单（CBalloonMainPanel）同口径，
// 保证两处 DuiMenu 视觉统一。DuiMenu 以源矩形 (0,0,16,16) 采样图标，故尺寸取 16。
static const int      kTrayMenuIconSize  = 16;                     // 图标位图边长（像素）
static const COLORREF kTrayMenuIconColor = RGB(0x5B, 0x5B, 0x5B);  // 图标描边色（深灰）

bool CMainDlg::IsServerConnected() const
{
    // 两个条件缺一不可：socket 还连着（IsClosed 取反），且已经登录成功。只连上没登录
    // 时业务帧发过去也会被服务端拒掉，对用户而言同样是"还不能用"。
    if (!m_bAlreadyLogin)
    {
        return false;
    }
    return !CIUSocket::GetInstance().IsClosed();
}

void CMainDlg::ResumeOnlineFailed(LPCTSTR lpszReason)
{
    LOG_INFO("MainDlg::ResumeOnlineFailed: 恢复在线失败，退回离线态。");

    // 连接已经断了（登录没成功），这里把上层状态一并拨回离线，等同于用户从没点过
    // "恢复在线"：压制自动重连、重新立起网络层的离线闸门、丢弃排队中的帧。
    m_nMainPanelStatus = MAINPANEL_STATUS_USERGOOFFLINE;
    CIUSocket::GetInstance().SetUserOffline(true);
    CIUSocket::GetInstance().ClearPendingSendData();

    // 状态胶囊退回离线 —— 点"在线"时它已经被乐观更新成在线了，不退回的话会出现
    // "胶囊显示在线、实际离线且不会重连"的自相矛盾。
    if (m_BalloonMainPanel.IsWindow())
    {
        m_BalloonMainPanel.ApplyPresenceOptimistic(kMyPresenceOffline);
    }

    // 托盘图标保持灰色、提示文字回到离线，界面门禁回到离线态（提示条重新变成
    // "点此恢复在线"，而不是停在"正在连接"）。
    RefreshTrayIconForStatus();
    BroadcastConnectionState();

    if (lpszReason == NULL)
    {
        return;   // 用户自己取消的，不必再弹提示
    }

    // 提示框挂在主面板上；主面板不在时退回挂到隐藏的 CMainDlg。
    HWND hOwner = m_hWnd;
    if (m_BalloonMainPanel.IsWindow())
    {
        hOwner = m_BalloonMainPanel.m_hWnd;
    }
    ::MessageBox(hOwner, lpszReason, g_strAppTitle.c_str(), MB_OK | MB_ICONWARNING);
}

int CMainDlg::GetConnectionUiState()
{
    return (int)balloonmain::ComputeConnectionUiState(GetCurrentPresence(), IsServerConnected());
}

void CMainDlg::BroadcastConnectionState()
{
    const int state = GetConnectionUiState();

    LOG_INFO("MainDlg::BroadcastConnectionState: presence=%d, connected=%d, uiState=%d "
        "(0=在线 1=正在连接 2=离线)。",
        GetCurrentPresence(), IsServerConnected() ? 1 : 0, state);

    // 主面板负责把状态转发给它持有的各个聊天窗（单聊 / 群聊），并按需禁用自己这边
    // 需要联网的入口（加好友、建群、改资料等）。
    if (m_BalloonMainPanel.IsWindow())
    {
        m_BalloonMainPanel.ApplyConnectionState(state);
    }
}

CString CMainDlg::BuildTrayTipText()
{
    // 第三行的状态文字与主面板胶囊三处同源。原先这里把状态写死成"在线"，用户切到
    // 离开 / 忙碌 / 离线后悬停托盘看到的仍是"在线"。
    //
    // 走 GetConnectionPresenceVisual 而<u>不是</u>只看 GetCurrentPresence()：后者是
    // "用户选了什么状态"，掉线时用户并没有改状态，于是它照旧给"在线"，而上一行取
    // 图标的 GetConnectionUiState() 已经按连接态转灰了 —— 同一次 ModifyIcon 里
    // 图标和文字各说各话，正是"图标灰了、提示还写着在线"的由来。
    CString strIconInfo;
    strIconInfo.Format(_T("%s\r\n%s\r\n%s"),
        m_FMGClient.m_UserMgr.m_UserInfo.m_strNickName.c_str(),
        m_FMGClient.m_UserMgr.m_UserInfo.m_strAccount.c_str(),
        balloonmain::GetConnectionPresenceVisual(GetCurrentPresence(),
                                                 GetConnectionUiState()).text);
    return strIconInfo;
}

void CMainDlg::UpdateTrayIconTip()
{
    m_TrayIcon.ModifyIcon(m_hAppIcon, BuildTrayTipText());
}

int CMainDlg::GetCurrentPresence()
{
    // 主面板在时以状态胶囊为准 —— 托盘菜单标记的当前项、托盘悬停提示的状态文字，
    // 与用户在主面板上看到的胶囊必然是同一个值。
    if (m_BalloonMainPanel.IsWindow())
    {
        return m_BalloonMainPanel.GetPresence();
    }

    // 主面板不在（未登录 / 登出后已销毁），或用户主动点了"离线"：都按"离线"呈现。
    if (!m_bAlreadyLogin || m_nMainPanelStatus == MAINPANEL_STATUS_USERGOOFFLINE)
    {
        return kMyPresenceOffline;
    }

    // 其余情形（含登录中 / 重连中）退回期望在线状态。
    return CIUSocket::GetInstance().GetRecordOnlineStatus();
}

void CMainDlg::OnTrayIcon_RButtunUp()
{

    // 右键弹菜单前先收起悬停未读提示浮窗（否则要等它自己的轮询定时器才消失）。
    m_trayMsgTipPopup.HidePopup();

    // 选托盘菜单形态：未登录 / 已登录。锁定功能已随旧主窗口移除，不再有"锁定"形态。
    balloonmain::TrayMenuKind kind = balloonmain::kTrayBeforeLogin;
    if (m_bAlreadyLogin)
        kind = balloonmain::kTrayNormal;

    // 按行模型装配 DuiMenu。图标用 CreateMenuIconImage 即时渲染，存进局部 icons
    // 数组保活到 TrackPopup 返回（TrackPopup 同步阻塞，局部即可）—— DuiMenu 只
    // 借用图标指针、不持有所有权。
    //
    // 当前在线状态传给行模型，由它把对应那一项标成"当前"（带外环的圆点 +
    // "（当前）"后缀），否则四个状态项长得一模一样，用户看不出自己现在是哪个。
    bool bMuted = m_FMGClient.m_UserConfig.IsEnableMute() ? true : false;
    int  nCurrentPresence = GetCurrentPresence();
    std::vector<balloonmain::TrayMenuEntry> model =
        balloonmain::BuildTrayMenuModel(kind, bMuted, nCurrentPresence);

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
    // hack；owner 仍传隐藏的 CMainDlg（菜单关掉后焦点回这里）。
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
    LOG_INFO(_T("托盘菜单落点：光标(%d,%d) 菜单(%dx%d) 可放置区(%d,%d,%d,%d) 弹出(%d,%d) 底边=%d")
             _T(" 当前状态presence=%d"),
        anchor.x, anchor.y, (int)menuSize.cx, (int)menuSize.cy,
        (int)area.left, (int)area.top, (int)area.right, (int)area.bottom,
        pos.x, pos.y, pos.y + (int)menuSize.cy, nCurrentPresence);

    UINT chosen = menu.TrackPopup(pos.x, pos.y, m_hWnd);   // 同步阻塞

    // 选中的就是当前所处的状态：什么都不做，避免重复发包、无谓的断开或重连。
    // 与主面板状态菜单的处理一致（见 CBalloonMainPanel::ShowStatusMenu）。
    for (size_t i = 0; i < model.size(); ++i)
    {
        if (model[i].id == chosen && model[i].isCurrent)
        {
            LOG_INFO(_T("托盘菜单：选中的就是当前状态(presence=%d)，不做处理。"),
                nCurrentPresence);
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
        OnChangeMyPresence(0, (WPARAM)kMyPresenceOnline, (LPARAM)kPresenceChangeManual);
        break;

    case balloonmain::kTrayMenuAway:
        OnChangeMyPresence(0, (WPARAM)kMyPresenceAway, (LPARAM)kPresenceChangeManual);
        break;

    case balloonmain::kTrayMenuBusy:
        OnChangeMyPresence(0, (WPARAM)kMyPresenceBusy, (LPARAM)kPresenceChangeManual);
        break;

    case balloonmain::kTrayMenuOffline:
        OnChangeMyPresence(0, (WPARAM)kMyPresenceOffline, (LPARAM)kPresenceChangeManual);
        break;

    case balloonmain::kTrayMenuMute:
        OnMenu_Mute(0, 0, NULL);
        break;

    case balloonmain::kTrayMenuOpenPanel:
        OnMenu_ShowMainPanel(0, 0, NULL);
        break;

    case balloonmain::kTrayMenuLogout:
        // 登出不能在这里同步做：它要销毁主面板并进入登录窗的模态循环，而此刻还站在
        // 托盘消息处理 + TrackPopup 返回后的调用栈上。投递给自己，等消息循环下一轮
        // 在干净的栈上执行（见 OnLogout）。
        PostMessage(FMG_MSG_LOGOUT, 0, 0);
        break;

    case balloonmain::kTrayMenuExit:
        OnMenu_Exit(0, 0, NULL);
        break;

    case 0:
    default:
        break;   // 未选中（失焦 / ESC）
    }
}

void CMainDlg::OnTrayIcon_MouseHover()
{
    // 仅在托盘正闪新消息时弹出未读提示列表（无未读不弹）。
    if (!m_trayFlash.IsFlashing())
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
    m_trayMsgTipPopup.SetPickCallback(&CMainDlg::OnTrayPopupPickConv, this);
    m_trayMsgTipPopup.ShowForEntries(m_trayFlash.GetEntries(), m_rcTrayIconRect);
}

void CMainDlg::OnTrayIcon_MouseLeave()
{
    // 新版未读提示浮窗自带"光标离开弹窗 ∪ 托盘则隐藏"的轮询，这里无需处理 —— 若在此
    // 主动隐藏，反而会在光标从托盘移到弹窗上的瞬间把它收起，导致点不到。故留空。
}

// 托盘悬停未读提示行点击回调跳板：转交本对话框打开会话。
void CMainDlg::OnTrayPopupPickConv(void* userdata, int ctype, long long cid)
{
    CMainDlg* pSelf = (CMainDlg*)userdata;
    if (pSelf != NULL)
        pSelf->OpenConvFromTrayPopup(ctype, cid);
}

void CMainDlg::OpenConvFromTrayPopup(int ctype, long long cid)
{
    // 转交主面板打开会话；OpenConversation 内部清未读并回投 FMG_MSG_TRAY_CLEAR，
    // 由 OnV2TrayClear 把该会话移出托盘闪烁集合、必要时停闪。
    if (m_BalloonMainPanel.IsWindow())
        m_BalloonMainPanel.OpenConversation(ctype, (int64_t)cid);
}

BOOL CMainDlg::LoadAppIcon(int nConnUiState)
{
    DestroyAppIcon();

    // 图标文件名由 OnlineCapability 决定（真正在线才彩色，离线与连接未就绪都灰），
    // 与"离线时哪些功能可用"同一个判定口径；日后为离开 / 忙碌各配一只图标时只改那一处。
    tstring strFileName = Hootina::CPath::GetAppPath() + _T("Image\\");
    strFileName += balloonmain::GetTrayIconFileName(nConnUiState);

    const int nIconSize = ::GetSystemMetrics(SM_CXSMICON);
    m_hAppIcon = AtlLoadIconImage(strFileName.c_str(), LR_DEFAULTCOLOR | LR_LOADFROMFILE,
        nIconSize, ::GetSystemMetrics(SM_CYSMICON));
    if (m_hAppIcon == NULL)
    {
        //格式串用窄字符（日志文件编码统一），路径是宽字符，故占位符写 %ls
        LOG_ERROR("MainDlg::LoadAppIcon: failed to load tray icon file: %ls.",
                  strFileName.c_str());
        return FALSE;
    }

    // 离开 / 忙碌在底图右下角叠一枚状态徽标 —— 这两个状态与在线共用同一张彩色底图，
    // 不叠加的话托盘上根本看不出自己正处于哪个状态。叠不叠、叠哪个由
    // GetTrayOverlayPresence 判定（在线不叠、离线与连接未就绪不叠）。
    const int nPresence = GetCurrentPresence();
    const int nOverlay = balloonmain::GetTrayOverlayPresence(nPresence, nConnUiState);
    LOG_INFO("MainDlg::LoadAppIcon: connUiState=%d, presence=%d, overlay=%d "
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
            LOG_WARNING("MainDlg::LoadAppIcon: failed to compose presence badge, "
                        "falling back to the plain icon. presence=%d.", nOverlay);
        }
    }
    return TRUE;
}

void CMainDlg::RefreshTrayIconForStatus()
{
    // 图标与提示都按 v2 在线状态（GetCurrentPresence）取，与托盘菜单标出的"当前项"、
    // 主面板状态胶囊三处同源。
    //
    // 注意这里<u>不再</u>读 v1 的 m_FMGClient.GetStatus()：v2 改状态并不更新那个字段，
    // 只有掉线时的 ResetToOfflineStatus 会把它置成 STATUS_OFFLINE，据它取图标会取错。
    //
    // 提示文字统一走 BuildTrayTipText（昵称 / 账号 / 状态三行），与其它几处刷新托盘
    // 提示的地方保持同一种格式 —— 原先这里用的是"昵称（状态）"单行格式，两种格式会
    // 互相覆盖，用户看到的提示取决于最后是谁刷的。
    LoadAppIcon(GetConnectionUiState());
    m_TrayIcon.ModifyIcon(m_hAppIcon, BuildTrayTipText());
}

void CMainDlg::DestroyAppIcon()
{
    if (m_hAppIcon != NULL)
    {
        ::DestroyIcon(m_hAppIcon);
        m_hAppIcon = NULL;
    }
}

BOOL CMainDlg::LoadLoginIcon()
{
    DestroyLoginIcon();

    CString strFileName;
    for (int i = 0; i < 6; i++)
    {
        strFileName.Format(_T("%sImage\\Loading_%d.ico"), Hootina::CPath::GetAppPath().c_str(), i + 1);
        m_hLoginIcon[i] = AtlLoadIconImage((LPCTSTR)strFileName, LR_DEFAULTCOLOR | LR_LOADFROMFILE,
            ::GetSystemMetrics(SM_CXSMICON), ::GetSystemMetrics(SM_CYSMICON));
    }

    return TRUE;
}

void CMainDlg::DestroyLoginIcon()
{
    for (int i = 0; i < 6; i++)
    {
        if (m_hLoginIcon[i] != NULL)
        {
            ::DestroyIcon(m_hLoginIcon[i]);
            m_hLoginIcon[i] = NULL;
        }
    }
}

void CMainDlg::UpdateTrayFlashIcon()
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
        LOG_INFO("重建托盘闪烁头像：集合为空，不生成图标。");
        return;
    }

    // 按系统小图标尺寸生成 monogram 头像图标（首字 + 底色，与会话列表头像同口径）。
    int nIconSize = ::GetSystemMetrics(SM_CXSMICON);
    m_hMsgIcon = balloonmain::MakeMonogramIcon(target.m_avatarText.c_str(),
                                               target.m_avatarColor, nIconSize);

    // 图标生成失败时 m_hMsgIcon 为空，OnTimer 两个分支都变成"设空图标"，看起来
    // 就是完全不闪，所以这里把结果记下来。
    LOG_INFO("重建托盘闪烁头像: ctype=%d, cid=%lld, 尺寸=%d, 图标=%p。",
             target.m_ctype, (long long)target.m_cid, nIconSize, m_hMsgIcon);
}

void CMainDlg::StopTrayFlash()
{
    // 杀掉闪烁定时器。
    if (m_dwMsgTimerId != NULL)
    {
        KillTimer(m_dwMsgTimerId);
        m_dwMsgTimerId = NULL;
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

void CMainDlg::UpdateMsgIcon()
{
    CMessageList* lpMsgList = m_FMGClient.GetMessageList();
    if (NULL == lpMsgList)
        return;

    CMessageSender* lpMsgSender = lpMsgList->GetLastMsgSender();
    if (NULL == lpMsgSender)
        return;

    long nMsgType = lpMsgSender->GetMsgType();
    UINT nSenderId = lpMsgSender->GetSenderId();
    UINT nGroupCode = lpMsgSender->GetGroupCode();

    if (m_nLastMsgType != nMsgType || m_nLastSenderId != nSenderId)
    {
        m_nLastMsgType = nMsgType;
        m_nLastSenderId = nSenderId;

        if (m_hMsgIcon != NULL)
        {
            ::DestroyIcon(m_hMsgIcon);
            m_hMsgIcon = NULL;
        }

        CString strHeadPicFileName;

        if (FMG_MSG_TYPE_BUDDY == nMsgType)
        {
            //strHeadPicFileName = GetHeadPicFullName(0, nSenderId);
            strHeadPicFileName.Format(_T("%sUsers\\%s\\UserThumb\\%d.png"), g_szHomePath, m_FMGClient.m_UserMgr.m_UserInfo.m_strAccount.c_str(), nSenderId);
            if (!Hootina::CPath::IsFileExist(strHeadPicFileName))
                strHeadPicFileName.Format(_T("%sImage\\UserThumbs\\%d.png"), g_szHomePath, m_FMGClient.m_UserMgr.GetFaceID(nSenderId));
        }
        else if (FMG_MSG_TYPE_GROUP == nMsgType)
            strHeadPicFileName = GetHeadPicFullName(nSenderId, 0);
        else if (FMG_MSG_TYPE_SESS == nMsgType)
            strHeadPicFileName = GetHeadPicFullName(nGroupCode, nSenderId);
        // 		else if (long_SYSGROUP == nMsgType)
        // 			strHeadPicFileName = Hootina::CPath::GetAppPath() + _T("Image\\DefSysHeadPic.ico");

        m_hMsgIcon = ExtractIcon(strHeadPicFileName);
        if (NULL == m_hMsgIcon)
        {
            if (FMG_MSG_TYPE_BUDDY == nMsgType || FMG_MSG_TYPE_SESS == nMsgType)
                m_hMsgIcon = AtlLoadIconImage(IDI_BUDDY, LR_DEFAULTCOLOR, ::GetSystemMetrics(SM_CXSMICON), ::GetSystemMetrics(SM_CYSMICON));
            else if (FMG_MSG_TYPE_GROUP == nMsgType)
                m_hMsgIcon = AtlLoadIconImage(IDI_GROUPCHATDLG_16, LR_DEFAULTCOLOR, ::GetSystemMetrics(SM_CXSMICON), ::GetSystemMetrics(SM_CYSMICON));
            //else if (long_SYSGROUP == nMsgType)
            //	m_hMsgIcon = AtlLoadIconImage(IDI_SYS, LR_DEFAULTCOLOR, ::GetSystemMetrics(SM_CXSMICON), ::GetSystemMetrics(SM_CYSMICON));
        }
    }
}

CString CMainDlg::GetHeadPicFullName(UINT nGroupCode, UINT nUTalkUin)
{


    //UINT nGroupNum, nUTalkNum;
    //GetNumber(nGroupCode, nUTalkUin, nGroupNum, nUTalkNum);

    //if (nGroupCode != 0 && nUTalkUin != 0)	// 群成员
    //	return m_FMGClient.GetSessHeadPicFullName(nUTalkNum).c_str();
    //else if (nGroupCode != 0)			// 群
    //	return m_FMGClient.GetGroupHeadPicFullName(nGroupNum).c_str();
    //else								// 好友
    //	return m_FMGClient.GetBuddyHeadPicFullName(nUTalkUin).c_str();

    return _T("");
}

void CMainDlg::GetNumber(UINT nGroupCode, UINT nUTalkUin, UINT& nGroupNum, UINT& nUTalkNum)
{
    /*nGroupNum = nUTalkNum = 0;

    if (nGroupCode != 0 && nUTalkUin != 0)
    {
        CGroupList* lpGroupList = m_FMGClient.GetGroupList();
        if (lpGroupList != NULL)
        {
            CBuddyInfo* lpBuddyInfo = lpGroupList->GetGroupMemberByCode(nGroupCode, nUTalkUin);
            if (lpBuddyInfo != NULL)
                nUTalkNum = lpBuddyInfo->m_nUTalkNum;
        }
    }
    else if (nGroupCode != 0)
    {
        CGroupList* lpGroupList = m_FMGClient.GetGroupList();
        if (lpGroupList != NULL)
        {
            CGroupInfo* lpGroupInfo = lpGroupList->GetGroupByCode(nGroupCode);
            if (lpGroupInfo != NULL)
                nGroupNum = lpGroupInfo->m_nGroupNumber;
        }
    }
    else if (nUTalkUin != 0)
    {
        CBuddyList* lpBuddyList = m_FMGClient.GetBuddyList();
        if (lpBuddyList != NULL)
        {
            CBuddyInfo* lpBuddyInfo = lpBuddyList->GetBuddy(nUTalkUin);
            if (lpBuddyInfo != NULL)
                nUTalkNum = lpBuddyInfo->m_nUTalkNum;
        }
    }*/
}

HICON CMainDlg::ExtractIcon(LPCTSTR lpszFileName)
{
    if (NULL == lpszFileName || NULL == *lpszFileName)
        return NULL;

    int cx = 16, cy = 16;
    HBITMAP hBmp = NULL;

    Gdiplus::Bitmap imgHead(lpszFileName);
    if (imgHead.GetLastStatus() != Gdiplus::Ok)
        return NULL;

    if (imgHead.GetWidth() != cx || imgHead.GetHeight() != cy)
    {
        Gdiplus::Bitmap* pThumbnail = (Gdiplus::Bitmap*)imgHead.GetThumbnailImage(cx, cy, NULL, NULL);
        if (pThumbnail != NULL)
        {
            pThumbnail->GetHBITMAP(Gdiplus::Color(255, 255, 255), &hBmp);
            delete pThumbnail;
        }
    }
    else
    {
        imgHead.GetHBITMAP(Gdiplus::Color(255, 255, 255), &hBmp);
    }

    if (NULL == hBmp)
        return NULL;

    HICON hIcon = NULL;

    CImageList imgList;
    BOOL bRet = imgList.Create(cx, cy, TRUE | ILC_COLOR32, 1, 1);
    if (bRet)
    {
        imgList.SetBkColor(RGB(255, 255, 255));
        imgList.Add(hBmp);
        hIcon = imgList.ExtractIcon(0);
    }
    ::DeleteObject(hBmp);
    imgList.Destroy();

    return hIcon;
}

// 从菜单ID获取对应的UTalk_STATUS
long CMainDlg::GetStatusFromMenuID(int nMenuID)
{
    switch (nMenuID)
    {
    case ID_MENU_IMONLINE:
        return STATUS_ONLINE;
    case ID_MENU_IMOFFLINE:
        return STATUS_OFFLINE;
    default:
        return STATUS_OFFLINE;
    }
}

void CMainDlg::SaveCurrentLogonUserToFile()
{
    CIniFile iniFile;
    CString strIniFile;
    strIniFile.Format(_T("%sconfig\\flamingo.ini"), g_szHomePath);
    CString strAccountList;
    iniFile.ReadString(_T("LogonUserList"), _T("AccountName"), _T(""), strAccountList.GetBuffer(256), 256, strIniFile);
    strAccountList.ReleaseBuffer();
    strAccountList += m_FMGClient.m_UserMgr.m_UserInfo.m_strAccount.c_str();
    strAccountList += _T(";");
    iniFile.WriteString(_T("LogonUserList"), _T("AccountName"), strAccountList, strIniFile);
}

void CMainDlg::DeleteCurrentUserFromFile()
{
    CIniFile iniFile;
    CString strIniFile;
    strIniFile.Format(_T("%sconfig\\flamingo.ini"), g_szHomePath);
    CString strAccountList;
    iniFile.ReadString(_T("LogonUserList"), _T("AccountName"), _T(""), strAccountList.GetBuffer(256), 256, strIniFile);
    strAccountList.ReleaseBuffer();

    CString strCurrentAccount;
    strCurrentAccount.Format(_T("%s;"), m_FMGClient.m_UserMgr.m_UserInfo.m_strAccount.c_str());
    strAccountList.Replace(strCurrentAccount, _T(""));
    iniFile.WriteString(_T("LogonUserList"), _T("AccountName"), strAccountList, strIniFile);
}

void CMainDlg::CreateEssentialDirectories()
{
    CString strAppPath(g_szHomePath);

    CString strUsersDirectory(strAppPath);
    strUsersDirectory += _T("Users");
    if (!Hootina::CPath::IsDirectoryExist(strUsersDirectory))
        Hootina::CPath::CreateDirectory(strUsersDirectory);

    CString strCurrentUserDirectory;
    strCurrentUserDirectory.Format(_T("%s\\%s"), strUsersDirectory, m_FMGClient.m_UserMgr.m_UserInfo.m_strAccount.c_str());

    if (!Hootina::CPath::IsDirectoryExist(strCurrentUserDirectory))
        Hootina::CPath::CreateDirectory(strCurrentUserDirectory);

    CString strChatImagesDirectory;
    strChatImagesDirectory.Format(_T("%s\\ChatImage\\"), strCurrentUserDirectory);
    if (!Hootina::CPath::IsDirectoryExist(strChatImagesDirectory))
        Hootina::CPath::CreateDirectory(strChatImagesDirectory);

    CString strUserThumbDirectory;
    strUserThumbDirectory.Format(_T("%s\\UserThumb\\"), strCurrentUserDirectory);
    if (!Hootina::CPath::IsDirectoryExist(strUserThumbDirectory))
        Hootina::CPath::CreateDirectory(strUserThumbDirectory);

}