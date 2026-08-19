/**
 *  进程外壳的实现。整体说明见 AppShell.h。
 *
 *  退出的做法：RequestAppExit 只投 WM_QUIT，真正的收尾放在消息循环返回之后的 Shutdown 里。
 *  这样收尾不会跑在任何窗口的窗口过程中，也就不会出现「在窗口自己的过程里销毁它自己」。
 *
 *  balloonwj@qq.com   2026-08-19
 */

#include "stdafx.h"
#include "AppShell.h"

#include "../UI/BalloonUpdate/BalloonUpdateWindow.h"       // 升级窗要登记一个宿主窗口，安装前据它请本进程退出
#include "../Common/Log/IULog.h"
#include "../Common/Util/Path.h"                           // Hootina::CPath（创建 Users 目录）
#include "../Common/Graphics/GDIFactory.h"                 // CGDIFactory::Uninit（退出时释放绘图资源）
#include "../Session/UserSessionData.h"                    // g_szHomePath

namespace balloonapp {

CAppShell::CAppShell()
    : m_panel(&m_FMGClient)
    , m_debugEntries(&m_FMGClient)
    , m_bExitRequested(false)
    , m_bShutdownDone(false)
{
    //把本类持有的这个 CFlamingoClient 登记成「进程内正在使用的那一个」，
    //net::CurrentUserAccount() 等取全局登录态的地方据此找到它。
    //
    //放在构造函数体的第一行而不是 Startup：m_FMGClient 在成员声明顺序里排在其余成员
    //之前，进入本函数体时它已经构造完毕，取地址是安全的，且该地址在本类整个生命周期内
    //不变；与析构函数里的注销严格配对。
    CFlamingoClient::SetInstance(&m_FMGClient);
}

CAppShell::~CAppShell()
{
    Shutdown();

    //注销登记，避免 m_FMGClient 随本类一同销毁之后 GetInstance 还返回一个野指针。
    CFlamingoClient::SetInstance(NULL);
}

bool CAppShell::Startup(HWND hOwnerWindow)
{
    if (!m_FMGClient.InitProxyWnd())
    {
        ::MessageBox(NULL, _T("初始化代理窗口失败，Flamingo无法启动！"), _T(""), MB_OK | MB_TOPMOST);
        ::ExitProcess(0);
        return false;
    }

    //创建 Users 目录：历史登录账号列表就存在它下面。
    CString strUsersDirectory(g_szHomePath);
    strUsersDirectory += _T("Users");
    if (!Hootina::CPath::IsDirectoryExist(strUsersDirectory))
    {
        Hootina::CPath::CreateDirectory(strUsersDirectory);
    }

    //先把主面板建出来（隐藏），再走登录。
    //
    //主面板是常驻窗口：进程启动建一次，登出只隐藏、退出才销毁，整个进程生命周期内
    //HWND 不变。这样做有两个理由：
    //  · 登出后聊天窗残留、缓存串账号那类问题，根子就在「面板对象活着、窗口反复生灭」
    //    这个不对称上（见 bugs.md 的 BUG-14）；常驻之后生命周期一致，容易推理。
    //  · 网络应答与推送的收件窗口就是主面板本身，这要求它在登录开始之前已经存在 ——
    //    登录结果本身就是一条网络应答。
    m_panel.EnsureCreated();

    //把本类作为进程层宿主交给主面板，主面板据此启动它持有的三个协作类（会话控制器、
    //自动离开控制器、托盘控制器）。必须在窗口建好之后调用，因为它们要在主面板窗口上
    //起定时器、挂托盘图标、订阅锁屏事件。
    m_panel.AttachAppHost(this);

    //把主面板的句柄登记给升级窗：安装升级包之前必须让本进程先退出（更新器按 --pid 等，
    //等不到就放弃），升级窗据此句柄投 FMG_MSG_EXIT_FOR_UPDATE 请程序退出，那条消息的
    //处理器在主面板持有的会话控制器里。不登记的话升级只能靠用户手动退出程序。
    CBalloonUpdateWindow::GetInstance().SetHostWindow(m_panel.m_hWnd);

#ifdef _DEBUG
    //命令行调试入口（--mainpanel / --chatwindow / --dxgipipe 等三十来个开关），实现在
    //DebugEntryPoints.cpp；命中任何一个就不再往下走正常的登录流程。正式发布构建里
    //TryRun 恒返回「未命中」。
    DebugEntryContext debugCtx;
    debugCtx.m_hHost = m_panel.m_hWnd;
    debugCtx.m_pPanel = &m_panel;
    debugCtx.m_pTray = &m_panel.TrayController();

    const DebugEntryResult debugResult = m_debugEntries.TryRun(debugCtx);
    if (debugCtx.m_bFakeLoggedIn)
    {
        //--mainpanel 模拟「已经登录」，托盘右键菜单的形态据这个标志选。
        m_panel.SessionController().MarkLoggedInForDebugPreview();
    }
    if (debugResult == kDebugEntryRequestExit)
    {
        //该分支要求立即退出（目前只有 --kickout）。
        return false;
    }
    if (debugResult == kDebugEntryHandled)
    {
        //其余调试入口把窗口开着，进程留在消息循环里，靠托盘菜单或关窗退出。
        return true;
    }
#else
    UNREFERENCED_PARAMETER(hOwnerWindow);
#endif

    //弹出登录窗（勾了自动登录就直接登）。返回 false 表示用户直接把登录窗关掉了，
    //没有走正常登录流程。
    return m_panel.SessionController().StartInitialLogin();
}

void CAppShell::RequestAppExit()
{
    if (m_bExitRequested)
    {
        LOG_INFO("AppShell::RequestAppExit: exit already requested, ignore this one.");
        return;
    }
    m_bExitRequested = true;

    LOG_INFO("AppShell::RequestAppExit: WM_QUIT posted, the message loop will return.");
    ::PostQuitMessage(0);
}

void CAppShell::Shutdown()
{
    if (m_bShutdownDone)
    {
        return;
    }
    m_bShutdownDone = true;

    //诊断日志：记录收尾时各窗口对象是否仍持有 HWND。退出若崩，看本行即可判断是主面板、
    //托盘浮窗还是某个调试入口的预览窗口没收干净。
    LOG_INFO("AppShell::Shutdown: trayMsgTip=%d panel=%d debugPreviewWindows=%d.",
        (int)m_panel.TrayController().IsMsgTipPopupWindow(),
        (int)m_panel.IsWindow(),
        m_debugEntries.CountAlivePreviewWindows());

    //窗口对象都是 ATL 窗口类（balloonui 的 DuiHost / DuiFrameWindow 派生），对象析构时
    //仍持有 HWND 会触发 ATL 的断言（atlwin.h 的「Object deleted before window was
    //destroyed」），表现为退出崩溃，故在此显式销毁。
    if (m_panel.IsWindow())
    {
        m_panel.DestroyWindow();
    }
    m_debugEntries.DestroyPreviewWindows();

    //本次会话的界面偏好落盘（头像选择），随后存最近联系人列表。
    m_FMGClient.m_UserConfig.SetFaceID(m_FMGClient.m_UserMgr.m_UserInfo.m_nFace);
    m_FMGClient.m_UserConfig.SetCustomFace(m_FMGClient.m_UserMgr.m_UserInfo.m_strCustomFace.c_str());
    m_FMGClient.SaveUserConfig();
    m_FMGClient.m_UserMgr.StoreRecentList();

    m_FMGClient.Uninit();

    CGDIFactory::Uninit();
}

} // namespace balloonapp
