/**
 *  进程外壳：把「一个 Flamingo 进程从启动到退出」这件事收在一个类里 —— 网络客户端的
 *  创建与销毁、必需目录的建立、主面板的创建与绑定、命令行调试入口的执行、发起首次登录，
 *  以及退出时的收尾。
 *
 *  本类不是窗口：进程里所有需要收件窗口的事（网络应答与推送、托盘回调、退出请求）都
 *  投给主面板，由它的消息映射与挂在它派发链上的协作类处理，本类只管进程本身的生死。
 *
 *  典型用法（见 Startup.cpp 的 Run）：
 *
 *      balloonapp::CAppShell shell;
 *      int nRet = 0;
 *      if (shell.Startup(g_hwndOwner))
 *          nRet = theLoop.Run();
 *      shell.Shutdown();
 *
 *  balloonwj@qq.com   2026-08-19
 */

#pragma once

#include "FlamingoClient.h"
#include "../UI/BalloonMain/BalloonMainPanel.h"
#include "../UI/BalloonMain/MainPanelSessionController.h"   // balloonmain::ISessionAppHost（本类实现它）
#include "DebugEntryPoints.h"                               // 命令行调试入口（本类按值持有）
#include "MediaModuleLoader.h"                              // FlamingoMedia.dll 加载器（本类按值持有）
#include "MediaHostAdapter.h"                               // 交给 FlamingoMedia.dll 的宿主接口（本类按值持有）

namespace balloonapp {

/**
 *  进程外壳。整个进程只应存在一个，由 Run 在栈上创建。
 */
class CAppShell : public balloonmain::ISessionAppHost
{
public:
    CAppShell();
    ~CAppShell();

    /**
     *  启动：初始化网络层、建必需目录、创建主面板并绑定各协作类、执行命令行调试入口、
     *  发起首次登录。
     *
     *    hOwnerWindow  进程的隐藏属主窗口（见 Startup.cpp 的 CreateOwnerWindow）。
     *                  登录窗以它作属主，这样登录窗最小化时不会在任务栏留下空白按钮。
     *
     *  返回 true 表示应当进入消息循环；返回 false 表示不必进入（命令行调试入口要求
     *  立即退出，或用户直接关掉了登录窗）。无论返回什么，调用方都要调一次 Shutdown。
     */
    bool Startup(HWND hOwnerWindow);

    /**
     *  记下启动命令行里的加群链接（docs/邀请入群方案.md §12），Startup 建好主面板后交给它，登录成功后处理。
     *  须在 Startup 之前调用；传空串表示没有。
     *    url：链接文本，如 flamingo://joingroup?code=abcdefghjkmn。
     */
    void SetPendingJoinUrl(const std::wstring& url);

    /**
     *  收尾：销毁全部窗口、落盘用户配置与最近联系人列表、反初始化网络层与绘图资源。
     *  须在消息循环返回之后调用一次；未 Startup 过时调用也安全，可重复调用。
     */
    void Shutdown();

    // ---- balloonmain::ISessionAppHost 的实现 ----

    /**
     *  请求退出整个进程：向本线程投 WM_QUIT，消息循环随之返回，真正的收尾在 Shutdown 里做。
     *
     *  之所以不在这里直接销毁窗口：本方法多半是在主面板的窗口过程里被调用的（会话控制器
     *  挂在主面板的消息派发链上），而收尾第一件事就是销毁主面板 —— 在一个窗口自己的窗口
     *  过程里把它自己销毁掉，返回后 ATL 还会去读已经作废的 m_hWnd，触发断言。
     */
    virtual void RequestAppExit();

private:
    //加载并初始化 FlamingoMedia.dll。加载或初始化失败只记日志，不影响进程启动。
    void LoadMediaModule();

    //关闭并卸载 FlamingoMedia.dll。未加载时调用是安全的。
    void UnloadMediaModule();

    //加载并初始化 FlamingoP2P.dll（经 balloonp2p::CP2PHost）。加载或初始化失败只记日志，不影响进程启动，
    //各业务继续走原来的中转。须在 LoadMediaModule 之前调用：另外两个 DLL 以后要从宿主取它的服务接口。
    void LoadP2PModule();

    //加载并初始化 FlamingoLock.dll（经 balloonlock::CLockHost）。加载或初始化失败只记日志，不影响进程启动，
    //托盘菜单随之不显示「锁屏」。
    void LoadLockModule();

    //加载并初始化 FlamingoScreenshot.dll（经 balloonscreenshot::CScreenshotHost，docs/截图独立模块方案.md 3.3 节）。
    //未登录时也要能截图，所以放在启动阶段；加载或初始化失败只记日志，不影响进程启动。
    void LoadScreenshotModule();

    CFlamingoClient m_FMGClient;                //网络客户端，进程内唯一的一个，本类拥有
    CBalloonMainPanel m_panel;                  //主面板，进程启动即建好并隐藏，是真正的主窗口

    //命令行调试入口，以及它们用到的那批预览窗口。只在 _DEBUG 构建中真正做事。
    CDebugEntryRunner m_debugEntries;

    //退出请求是否已经提出过。WM_QUIT 投出之后，仍在栈上的登录窗模态循环会随之返回，
    //其调用方按「用户关掉了登录窗」再请求一次退出；用本标志把第二次及以后的挡掉。
    bool m_bExitRequested;

    //Shutdown 是否已经执行过。Startup 失败与正常结束都会走到它，故做成可重复调用。
    bool m_bShutdownDone;

    //启动命令行里的加群链接（docs/邀请入群方案.md §12）；没有时为空。Startup 建好主面板后交给它
    std::wstring m_pendingJoinUrl;

    //FlamingoMedia.dll 的宿主接口实现。须比 DLL 活得久：Startup 里交给模块，Shutdown 里先关模块再随本类析构。
    CMediaHostAdapter m_mediaHost;

    //FlamingoMedia.dll 加载器。Startup 时加载（失败则音视频类功能不可用），Shutdown 时关闭并卸载。
    CMediaModuleLoader m_mediaLoader;
};

} // namespace balloonapp
