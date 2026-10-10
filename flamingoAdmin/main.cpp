/**
 *  flamingoAdmin —— 程序入口。
 *
 *  flamingoAdmin 是 FlamingoIM 的企业管理控制台，用 balloonui（无 HWND DUI
 *  控件库）实现 design/FlamingoIM Enterprise Admin.html 中的设计稿。
 *
 *  本文件只负责进程初始化（DPI / OLE / GDI+ / ATL 模块）、创建主窗口
 *  AdminFrame、跑消息循环。界面构建与路由全部在 AdminFrame 内。
 *
 *  balloonwj@qq.com   2026.05.21
 */
#include "stdafx.h"
#include "resource.h"
#include "AdminFrame.h"
#include "Theme/AdminTheme.h"
#include "Widgets/Widgets.h"   // ui::CreateLogoBadgeIcon —— 主窗口图标
#include "Util/Logger.h"
#include "Util/CrashHandler.h"
#include "Util/AppConfig.h"
#include "Util/HttpWorker.h"
#include "Util/LinkBridge.h"   // FlamingoLink.dll 的卸载（退出前兜底）

#include "DuiDpi.h"

#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")

using namespace balloonwjui;

// ATL/WTL 要求的全局模块单例。stdafx.h 中以 extern 声明，定义落在此处。
CAppModule _Module;

namespace {

// ---------------------------------------------------------------------------
// 窗口尺寸常量（96 dpi 逻辑像素）
//
// 设计稿 design/FlamingoIM Enterprise Admin.html 基准帧为 1440 x 900；最小
// 尺寸取「侧栏 + 顶栏 + 内容区仍能完整呈现」的经验下限。
// ---------------------------------------------------------------------------
const int kInitialClientWidth  = 1440;   // 初始客户区宽度
const int kInitialClientHeight = 900;    // 初始客户区高度
const int kMinClientWidth      = 1100;   // 最小客户区宽度
const int kMinClientHeight     = 720;    // 最小客户区高度

// WM_KEYDOWN 的 lParam 第 30 位：按键的先前状态。为 1 表示本次按下是长按
// 触发的自动重复 —— 据此过滤重复，避免按住 F12 时调试控制台被反复切换。
const LPARAM kKeyPrevDownBit = 0x40000000;

} // anonymous namespace

int WINAPI _tWinMain(HINSTANCE hInstance, HINSTANCE, LPTSTR, int nCmdShow)
{
    // 声明 Per-Monitor DPI v2 感知；必须在创建任何窗口之前调用。
    DuiDpi::OptInPerMonitorV2();

    // OLE 初始化：富文本控件依赖之。
    ::OleInitialize(NULL);
    // ICC_LISTVIEW_CLASSES：注册 ListView 通用控件类，调试控制台窗口需要它。
    AtlInitCommonControls(ICC_BAR_CLASSES | ICC_LISTVIEW_CLASSES);
    _Module.Init(NULL, hInstance);

    // 初始化文件日志器，关键路径日志从此可用。
    flamingoadmin::Logger::Init();

    // 载入应用配置（服务器地址等）。须在构建登录界面之前，登录页要读它。
    flamingoadmin::AppConfig::Instance().Load();

    // 安装崩溃捕获：一旦崩溃，把符号化调用栈写入日志。
    flamingoadmin::InstallCrashHandler();

    // 启动 GDI+：自绘控件用它绘制圆角、渐变与矢量图标。
    Gdiplus::GdiplusStartupInput gdiplusInput;
    ULONG_PTR gdiplusToken = 0;
    Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusInput, nullptr);

    int exitCode = 0;
    {
        flamingoadmin::AdminFrame frame;
        frame.SetButtons(true, true, true);
        frame.SetMinSize(kMinClientWidth, kMinClientHeight);
        frame.SetTitleBarHeight(flamingoadmin::theme::kTitleBarHeight);

        // WS_CLIPCHILDREN：企业详情页的「在线用户」内容是 FlamingoLink.dll 建的子窗口，主窗口重绘时
        // 跳过子窗口所占的区域，避免先画一遍主窗口内容再被子窗口盖住造成闪烁（2026-10-04）。
        if (!frame.Create(NULL, CWindow::rcDefault,
                          _T("FlamingoIM Enterprise Admin"),
                          WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, 0))
        {
            FA_LOGE(_T("主窗口创建失败，程序退出"));
            Gdiplus::GdiplusShutdown(gdiplusToken);
            _Module.Term();
            ::OleUninitialize();
            return 0;
        }

        // 标题文字必须在 Create 之后设置。
        frame.SetTitle(_T("FlamingoIM Enterprise Admin"));

        // 应用程序图标：用侧边栏品牌徽标（flamingo 圆角方块 + 白色 "F"）实时
        // 渲染，使自绘标题栏左上角 icon、任务栏 / Alt-Tab 图标都与窗口左上角
        // 的徽标一致。大图标用于任务栏 / Alt-Tab，小图标用于自绘标题栏。
        HICON hIconBig = flamingoadmin::ui::CreateLogoBadgeIcon(
                             ::GetSystemMetrics(SM_CXICON));
        HICON hIconSmall = flamingoadmin::ui::CreateLogoBadgeIcon(
                               ::GetSystemMetrics(SM_CXSMICON));
        if (hIconBig != nullptr)
        {
            ::SendMessage(frame.m_hWnd, WM_SETICON, ICON_BIG,
                          reinterpret_cast<LPARAM>(hIconBig));
            ICONINFO iconInfo = {};
            if (::GetIconInfo(hIconBig, &iconInfo))
            {
                if (iconInfo.hbmMask != nullptr)
                {
                    ::DeleteObject(iconInfo.hbmMask);
                }
                // balloonui 自绘标题栏左上角的 icon 取此 HBITMAP。
                frame.SetIcon(iconInfo.hbmColor);
            }
        }
        if (hIconSmall != nullptr)
        {
            ::SendMessage(frame.m_hWnd, WM_SETICON, ICON_SMALL,
                          reinterpret_cast<LPARAM>(hIconSmall));
        }

        // 装入初始界面（登录界面）。须在 Create 之后调用。
        frame.InitContent();

        frame.ResizeClient(kInitialClientWidth, kInitialClientHeight);
        frame.CenterWindow();

        // 启动 HTTP 工作线程：登录等所有联网请求都经它异步发送。
        flamingoadmin::HttpWorker::Instance().Start();

        frame.ShowWindow(nCmdShow);
        frame.UpdateWindow();
        FA_LOGI(_T("主窗口已显示（%d x %d）"),
                kInitialClientWidth, kInitialClientHeight);

        MSG msg;
        while (::GetMessage(&msg, NULL, 0, 0))
        {
            // F12：切换调试控制台。F12 被系统保留给调试器，无法用 RegisterHotKey
            // 注册为热键，故在消息循环里拦截 —— 这样无论键盘焦点落在主窗口
            // 还是哪个子窗口上都能响应。过滤长按自动重复。
            if (msg.message == WM_KEYDOWN && msg.wParam == VK_F12
                && (msg.lParam & kKeyPrevDownBit) == 0)
            {
                frame.ToggleDebugConsole();
                continue;
            }
            // Esc：主窗口里有叠层弹窗时关闭它（等同点关闭按钮）。在派发之前拦截，这样无论
            // 焦点落在弹窗里的哪个输入框上都生效；独立顶层的对话框各自处理 Esc，不经这里。
            if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE
                && (msg.lParam & kKeyPrevDownBit) == 0
                && ::GetAncestor(msg.hwnd, GA_ROOT) == frame.m_hWnd
                && frame.HandleEscapeKey())
            {
                continue;
            }
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
        }
        exitCode = static_cast<int>(msg.wParam);

        // 停止 HTTP 工作线程并 join，确保进程退出前线程已干净收回。
        flamingoadmin::HttpWorker::Instance().Stop();

        // FlamingoLink.dll 通常已在主窗口销毁时卸载（AdminFrame::OnDestroy）；这里再调一次，
        // 确保主窗口创建后中途异常退出时 DLL 的连接线程也被停掉。重复调用安全。
        flamingoadmin::LinkBridge::Instance().Unload();
    }

    Gdiplus::GdiplusShutdown(gdiplusToken);
    _Module.Term();
    ::OleUninitialize();
    return exitCode;
}
