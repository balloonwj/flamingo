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
#include "../UI/BalloonChatWnd/ChatTextWarmup.h"           // 启动时预热聊天气泡的文字测量缓存
#include "../Common/Util/Path.h"                           // Hootina::CPath（创建 Users 目录）
#include "../Common/Graphics/GDIFactory.h"                 // CGDIFactory::Uninit（退出时释放绘图资源）
#include "../Session/UserSessionData.h"                    // g_szHomePath
#include "../Feature/FileShare/FileShareHost.h"            // 退出时关闭并卸载文件共享模块
#include "../Feature/SecretChat/SecretChatHost.h"          // 退出时结束全部密聊并卸载密聊模块
#include "../Feature/ThreadGroup/ThreadGroupHost.h"        // 退出时关闭全部帖子群窗口并卸载帖子群模块
#include "../Feature/Bot/BotHost.h"                        // 退出时关闭机器人管理窗口并卸载 FlamingoBot.dll
#include "../Feature/Caption/CaptionHost.h"                // 语音字幕：登记媒体模块，退出时在它之后卸载字幕模块
#include "../Feature/P2P/P2PHost.h"                        // 启动时加载 P2P 模块，退出时最后卸载
#include "../Feature/P2P/P2PSocketNetwork.h"               // P2P 宿主经主连接收发信令
#include "../Feature/P2P/FolderP2PRegistration.h"          // 单聊文件夹传输登记为 P2P 的业务方
#include "../Feature/Lock/LockHost.h"                       // 启动时加载锁屏模块，退出时最先关闭
#include "../UI/BalloonMain/NotificationSound.h"             // SetNotifySoundSuppressor：锁屏期间提示音不响
#include "../Feature/Markdown/MarkdownModuleHost.h"          // 启动时在后台预加载 Markdown 渲染模块
#include "../Feature/Ocr/OcrModuleHost.h"                    // 启动时在后台预加载图片文字识别模块
#include "../UI/BalloonOcr/OcrEntry.h"                       // 注册「图片文字识别」的发起实现
#include "../Feature/Screenshot/ScreenshotEntry.h"           // 启动时按全局配置注册截图快捷键
#include "../Feature/Screenshot/ScreenshotHost.h"            // 启动时加载截图模块，退出时卸载
#include "../UI/BalloonMarkdown/MarkdownEditorManager.h"     // 退出时关闭全部 Markdown 编辑器
#include "../Common/I18n/I18n.h"   // TR / TRF：界面文字的多语言查找
#include "../Feature/CloudRecord/CloudTransferRunner.h"   // 云端录像传输：加载媒体模块后登记、卸载前停止

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
        const std::wstring tip = TRF(L"初始化代理窗口失败，{0}无法启动！", g_strAppTitle);
        ::MessageBox(NULL, tip.c_str(), g_strAppTitle.c_str(), MB_OK | MB_TOPMOST);
        //ExitProcess 不会走到 WinMain 末尾的 CIULog::Uninit，先把队列里的日志写进文件
        CIULog::Flush(kIULogFlushTimeoutMs);
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

    //加载 P2P 通信模块 FlamingoP2P.dll。它的初始化不依赖登录，且另外两个 DLL 以后要从宿主取它的服务接口，
    //所以放在启动阶段、排在 FlamingoMedia 之前；加载失败只记日志，各业务继续走原来的中转。
    LoadP2PModule();

    //加载音视频与屏幕类功能模块 FlamingoMedia.dll。它的初始化不依赖登录，所以放在启动阶段；
    //加载失败只记日志，客户端照常运行，只是这类功能不可用。
    LoadMediaModule();

    //加载锁屏模块 FlamingoLock.dll（docs/锁屏方案.md）。锁屏不依赖登录，登录前的托盘菜单里也有「锁屏」，
    //所以放在启动阶段；加载失败只记日志，托盘菜单随之不显示「锁屏」，其余功能不受影响。
    LoadLockModule();

    //加载截图模块 FlamingoScreenshot.dll（docs/截图独立模块方案.md）。截图不依赖登录，托盘菜单与截图快捷键在登录前
    //就可用，所以放在启动阶段；加载失败只记日志
    LoadScreenshotModule();
    //锁屏期间 exe 自己的提示音一律不响（FlamingoMedia 的声音经宿主的 IsSoundMuted 在锁屏期间返回静音）
    balloonmain::SetNotifySoundSuppressor(&balloonlock::IsScreenLocked);

    //Markdown 消息渲染模块 FlamingoMarkdown.dll（docs/Markdown消息方案.md 第 6.1 节）：在后台线程预加载，
    //不阻塞启动；登录完成前早已就绪。DLL 不存在或加载失败只记日志，Markdown 入口不显示、消息按纯文字显示。
    {
        CString markdownDllPath(g_szHomePath);
        markdownDllPath += FLAMINGO_MARKDOWN_DLL_NAME;
        markdown::CMarkdownModuleHost::GetInstance().StartPreload(std::wstring((LPCWSTR)markdownDllPath));
    }

    //图片文字识别模块 FlamingoOcr.dll（docs/图片文字识别方案.md 第 5.2 节）：注册发起识别的实现，并在后台线程预加载 DLL、
    //查询可用性。2026-10-07 起由登录成功后提前到启动阶段，未登录时截图也能「识别文字」（docs/截图独立模块方案.md 3.7 节）；
    //DLL 不存在或加载失败只记日志，三个识别入口不显示。
    {
        balloonocr::RegisterOcrLaunchers();
        CString ocrDllPath(g_szHomePath);
        ocrDllPath += FLAMINGO_OCR_DLL_NAME;
        ocr::COcrModuleHost::GetInstance().StartPreload(std::wstring((LPCWSTR)ocrDllPath), i18n::GetLanguage());
    }

    //利用界面空闲时间预热聊天气泡的文字测量缓存，启动后第一次打开聊天窗时折行测量即可直接
    //命中缓存。分批进行，由线程定时器驱动，WM_TIMER 只在消息队列空闲时产生，不影响登录与
    //主面板的加载；约一秒内完成。见 ChatTextWarmup.h。
    balloonchatwnd::ChatTextWarmup::Inst().Start();

    //先把主面板建出来（隐藏），再走登录。
    //
    //主面板是常驻窗口：进程启动建一次，登出只隐藏、退出才销毁，整个进程生命周期内
    //HWND 不变。这样做有两个理由：
    //  · 登出后聊天窗残留、缓存串账号那类问题，根子就在「面板对象活着、窗口反复生灭」
    //    这个不对称上；常驻之后生命周期一致，容易推理。
    //  · 网络应答与推送的收件窗口就是主面板本身，这要求它在登录开始之前已经存在 ——
    //    登录结果本身就是一条网络应答。
    m_panel.EnsureCreated();

    //把本类作为进程层宿主交给主面板，主面板据此启动它持有的三个协作类（会话控制器、
    //自动离开控制器、托盘控制器）。必须在窗口建好之后调用，因为它们要在主面板窗口上
    //起定时器、挂托盘图标、订阅锁屏事件。
    m_panel.AttachAppHost(this);

    //启动命令行里带着加群链接：交给主面板，登录成功后处理（docs/邀请入群方案.md §12）
    if (!m_pendingJoinUrl.empty())
    {
        m_panel.SetPendingJoinUrl(m_pendingJoinUrl);
    }

    //锁屏开始与结束的通知投给主面板，由它暂停快捷键、静音、延后提醒类弹窗（docs/锁屏方案.md 第 4.9 节）
    balloonlock::CLockHost::GetInstance().SetNotifyWindow(m_panel.m_hWnd);

    //截图快捷键（默认 Alt+Z）：2026-10-07 起存在全局配置里，主面板一建好就注册，未登录时也能截图；登录成功后按同一份
    //配置再注册一次，登出后保持注册（docs/截图独立模块方案.md 3.5 节）。「打开主面板」与录屏两个快捷键仍在登录后注册
    balloonscreenshot::ApplyScreenshotHotkey(m_panel.m_hWnd, balloonscreenshot::LoadGlobalScreenshotPrefs().m_hotkey);

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
    debugCtx.m_pMediaModule = m_mediaLoader.GetModule();   //未加载时为 NULL，通话等迁入 DLL 的调试入口随之不生效

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

void CAppShell::SetPendingJoinUrl(const std::wstring& url)
{
    m_pendingJoinUrl = url;
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

    //预热若还没做完（启动后一秒内就退出），先停掉它的定时器
    balloonchatwnd::ChatTextWarmup::Inst().Stop();

    //最先关闭锁屏模块：锁屏中退出（如系统注销）时先结束锁屏线程、摘除键盘钩子、销毁锁屏窗口
    balloonlock::CLockHost::GetInstance().Shutdown();

    //关闭并卸载截图模块：截图界面是模态的，退出时不会有截图在进行；它不依赖网络层与主面板，放在前面卸载
    balloonscreenshot::CScreenshotHost::GetInstance().Shutdown();

    //关闭全部 Markdown 编辑器：用户主动退出时已在 DoExit 里询问过未保存的文档；它们不属于主面板，要在这里销毁
    balloonmd::CMarkdownEditorManager::GetInstance().CloseAll();

    //接着关闭音视频模块：它收尾进行中的会话时可能还要经主连接发信令、关闭自己的窗口，
    //必须赶在主面板销毁与网络层反初始化之前
    UnloadMediaModule();

    //关闭并卸载语音字幕模块：音视频模块持有它的识别流，必须在音视频模块关闭之后；字幕编辑器是它的窗口，要赶在主面板销毁之前
    ballooncaption::CCaptionHost::GetInstance().Shutdown();

    //关闭并卸载文件共享模块，理由同上：它要销毁自己的窗口，必须赶在主面板销毁与日志收尾之前
    balloonfileshare::CFileShareHost::GetInstance().Shutdown();

    //关闭并卸载单聊密聊模块：它结束会话时还要经主连接发 2607 通知对方、清零密钥，必须赶在网络层反初始化之前；
    //它的回调目标是主面板，也要赶在主面板销毁之前
    balloonsecret::CSecretChatHost::GetInstance().Shutdown();

    //关闭并卸载帖子群模块：它要销毁自己的帖子群窗口、经主连接撤销「正在查看」，必须赶在主面板销毁与网络层反初始化之前
    balloonthread::CThreadGroupHost::GetInstance().Shutdown();

    //关闭并卸载机器人管理界面模块：先撤销帧回调与界面入口，再销毁它的窗口；回调目标是主面板，必须赶在主面板销毁、
    //网络层反初始化之前
    balloonbot::CBotHost::GetInstance().Shutdown();

    //最后关闭并卸载 P2P 模块：另外两个 DLL 以后会持有它的服务接口，必须在它们卸载之后；
    //它结束链路时可能还要经主连接发 2305，所以仍要赶在网络层反初始化之前。先注销文件夹传输与单文件直传的业务方
    balloonp2p::UnregisterFolderP2P();
    balloonp2p::UnregisterDirectFileP2P();
    balloonp2p::CP2PHost::GetInstance().Shutdown();

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

void CAppShell::LoadMediaModule()
{
    //g_szHomePath 是程序目录，以反斜杠结尾；DLL 与 exe 放在同一目录
    CString dllPath(g_szHomePath);
    dllPath += FLAMINGO_MEDIA_DLL_NAME;
    //宿主能力依赖网络客户端（用户号、静音设置）与主面板（通话提示），交给模块之前先登记
    m_mediaHost.Attach(&m_FMGClient, &m_panel);
    MediaLoadResult result = m_mediaLoader.Load(dllPath, flamingomedia::kApiVersion);
    if (result != media_load_ok)
    {
        LOG_WARNING("media module unavailable: %s, api version %u.", CMediaModuleLoader::DescribeResult(result),
                    flamingomedia::kApiVersion);
        return;
    }

    //初始化参数里的程序目录不带结尾反斜杠
    CString appDir(g_szHomePath);
    appDir.TrimRight(_T('\\'));
    flamingomedia::MediaInitParams params;
    params.m_cbSize = sizeof(params);
    params.m_appDir = appDir;
    if (!m_mediaLoader.GetModule()->Init(&m_mediaHost, &params))
    {
        LOG_ERROR("media module loaded but Init failed, unloading it.");
        m_mediaLoader.Unload();
        return;
    }
    //主面板据此把主连接转交来的音视频类信令交给模块
    m_panel.SetMediaModule(m_mediaLoader.GetModule());
    //字幕宿主借用媒体宿主的界面能力（主窗口、翻译、选择会话发送文件），并在字幕可用状态变化时通知媒体模块
    ballooncaption::CCaptionHost::GetInstance().SetMediaHost(&m_mediaHost);
    ballooncaption::CCaptionHost::GetInstance().SetMediaModule(m_mediaLoader.GetModule());
    //云端录像文件的上传与下载（第 27 期）：进度与结束回报给媒体模块
    ballooncloud::CCloudTransferRunner::GetInstance().SetMediaModule(m_mediaLoader.GetModule());
    LOG_INFO("media module loaded, api version %u.", m_mediaLoader.GetModule()->GetApiVersion());
}

void CAppShell::LoadP2PModule()
{
    //g_szHomePath 是程序目录，以反斜杠结尾；DLL 与 exe 放在同一目录。传完整路径，不依赖系统的 DLL 搜索顺序
    CString dllPath(g_szHomePath);
    dllPath += FLAMINGO_P2P_DLL_NAME;
    //初始化参数里的程序目录不带结尾反斜杠
    CString appDir(g_szHomePath);
    appDir.TrimRight(_T('\\'));
    //加载失败的原因由宿主记日志，这里不必再记
    if (!balloonp2p::CP2PHost::GetInstance().Load(dllPath, appDir, &balloonp2p::CP2PSocketNetwork::GetInstance()))
        return;
    //单聊文件夹传输登记为用途 folder 的业务方：要在第一次登录之前登记，2302 建好的链路才不会因为没人认领被丢掉
    balloonp2p::RegisterFolderP2P(balloonp2p::CP2PHost::GetInstance().GetService());
    //单聊单文件直传登记为用途 file 的业务方（2026-10-04 起）：2301 报到了 file，服务端才判定双方能直传
    balloonp2p::RegisterDirectFileP2P(balloonp2p::CP2PHost::GetInstance().GetService());
}

void CAppShell::LoadLockModule()
{
    //g_szHomePath 是程序目录，以反斜杠结尾；DLL 与 exe 放在同一目录。传完整路径，不依赖系统的 DLL 搜索顺序
    CString dllPath(g_szHomePath);
    dllPath += FLAMINGO_LOCK_DLL_NAME;
    //初始化参数里的程序目录不带结尾反斜杠
    CString appDir(g_szHomePath);
    appDir.TrimRight(_T('\\'));
    //上次选择的锁屏画面记在 config\flamingo.ini 的 [lock] 节（登录前也要能读）
    CString iniPath(g_szHomePath);
    iniPath += _T("config\\flamingo.ini");
    //加载失败的原因由宿主记日志，这里不必再记
    balloonlock::CLockHost::GetInstance().Load(dllPath, appDir, iniPath);
}

void CAppShell::LoadScreenshotModule()
{
    //g_szHomePath 是程序目录，以反斜杠结尾；DLL 与 exe 放在同一目录。传完整路径，不依赖系统的 DLL 搜索顺序
    CString dllPath(g_szHomePath);
    dllPath += FLAMINGO_SCREENSHOT_DLL_NAME;
    //初始化参数里的程序目录不带结尾反斜杠
    CString appDir(g_szHomePath);
    appDir.TrimRight(_T('\\'));
    //加载失败的原因由宿主记日志，这里不必再记
    balloonscreenshot::CScreenshotHost::GetInstance().Load(dllPath, appDir);
}

void CAppShell::UnloadMediaModule()
{
    if (!m_mediaLoader.IsLoaded())
    {
        return;
    }
    //先解除主面板与字幕宿主的登记，此后转交来的信令与字幕状态变化不再交给即将卸载的模块
    m_panel.SetMediaModule(NULL);
    ballooncaption::CCaptionHost::GetInstance().SetMediaModule(NULL);
    //先停掉进行中的云端录像传输：之后不再回报给即将卸载的模块
    ballooncloud::CCloudTransferRunner::GetInstance().Shutdown();
    m_mediaLoader.GetModule()->Shutdown();
    m_mediaLoader.Unload();
    LOG_INFO("media module unloaded.");
}

} // namespace balloonapp
