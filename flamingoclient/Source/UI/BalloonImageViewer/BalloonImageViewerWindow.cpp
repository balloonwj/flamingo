/**
 *  BalloonImageViewerWindow.cpp
 *
 *  CBalloonImageViewerWindow 的实现：建无边框暗色顶层窗口、把标题栏配成透明
 *  深色拖动条、客户区装一张 ImageCanvas。调试入口弹文件选择框挑图自检。
 *
 *  balloonwj@qq.com   2026-06-04
 */

#include "stdafx.h"
#include "BalloonImageViewerWindow.h"
#include "../../App/BrandText.h"   // g_strAppTitle（窗口标题用的产品名称）

#include <memory>
#include <shellapi.h>   // CommandLineToArgvW（解析调试入口可选图片路径）
#include <atldlgs.h>    // CFileDialog（调试入口的选图框）；单测工程的 stdafx.h 不含它
#pragma comment(lib, "shell32.lib")

#include "../../App/CustomMsgDef.h"     // WM_CLOSE_MAINDLG
#include "../../Common/Log/IULog.h"            // LOG_INFO / LOG_ERROR
#include "ImageCanvas.h"
#include "../../Common/I18n/I18n.h"   // TR：界面文字的多语言查找
#include "../../Common/Util/FileDialogFilter.h"   // filedlg::MakeFilter：文件对话框过滤串的说明文字逐段翻译

using namespace balloonwjui;

// ---- 文件作用域常量 -----------------------------------------------------

// 含标题栏的整窗初始尺寸（逻辑像素）。2026-10-02 修复 ResizeClient 多叠加系统边框的问题时由 1000×720 加上
// 修复前多叠加的 18×47（120 DPI 实测），保持修复前的窗口大小。
static const int kVwInitWidth  = 1018;
static const int kVwInitHeight = 767;

// 窗口最小尺寸（逻辑像素）。
static const int kVwMinWidth  = 480;
static const int kVwMinHeight = 360;

// 标题栏高度（逻辑像素）—— 透明深色拖动条，无可见内容。
static const int kVwTitleBarHeight = 32;

// 窗口暗色基调（与 ImageCanvas 背景一致）。
static const COLORREF kVwDarkColor = RGB(0x1E, 0x1E, 0x1E);

// 标题栏右上角关闭按钮 glyph 颜色：深色标题栏上的浅灰 X（hover 由 DuiFrameWindow
// 内部转成 Windows 红底白叉）。
static const COLORREF kVwCaptionGlyph = RGB(0xC8, 0xC8, 0xC8);

// 客户区画布控件 id（DuiFrameWindow 保留 1/2/3 给标题栏三按钮，自定义控件从
// ≥10 起编，见规约「DuiFrameWindow ctrlId 1/2/3 保留」）。
static const UINT kVwCanvasCtrlId = 10;

namespace {

// 从命令行解析 --imageviewer 之后紧跟的可选图片路径（非 "-" 开头的下一个参数）。
// 有则返回该路径，便于自动化截图核对渲染；没有返回空串（由调用方改弹文件选择框）。
// 用 Win32 CommandLineToArgvW 而非 CRT __targv —— 后者在 GUI 程序里是否填充
// 不可靠，前者总是按当前实际命令行正确切分。
CString ParseDebugImagePath()
{
    int     argc = 0;
    LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
    if (argv == NULL)
    {
        return CString();
    }

    CString result;
    for (int i = 1; i + 1 < argc; ++i)
    {
        if (::wcscmp(argv[i], L"--imageviewer") == 0)
        {
            LPCWSTR next = argv[i + 1];
            if (next != NULL && next[0] != L'-')
            {
                result = next;
                break;
            }
        }
    }
    ::LocalFree(argv);
    return result;
}

// 新建一张 size×size、纯 color 填充的 DDB 位图（调用方持有，用毕 DeleteObject）。
// 给 DuiHost::SetBgImage 当 9-grid 源（零 inset 即整张拉伸 = 纯色铺底），让透明
// 标题栏穿透显示为深色。失败返回 NULL。
HBITMAP CreateSolidColorBitmap(int size, COLORREF color)
{
    HDC     hScreen = ::GetDC(NULL);
    HDC     hMem    = ::CreateCompatibleDC(hScreen);
    HBITMAP hBmp    = ::CreateCompatibleBitmap(hScreen, size, size);
    HBITMAP hOld    = (HBITMAP)::SelectObject(hMem, hBmp);

    RECT   rc = { 0, 0, size, size };
    HBRUSH br = ::CreateSolidBrush(color);
    ::FillRect(hMem, &rc, br);
    ::DeleteObject(br);

    ::SelectObject(hMem, hOld);
    ::DeleteDC(hMem);
    ::ReleaseDC(NULL, hScreen);
    return hBmp;
}

} // anonymous namespace

// =======================================================================
// 构造 / 析构
// =======================================================================

CBalloonImageViewerWindow::CBalloonImageViewerWindow()
    : m_pCanvas(NULL)
    , m_hMainDlg(NULL)
    , m_navigator(NULL)
    , m_hBgBitmap(NULL)
{
}

CBalloonImageViewerWindow::~CBalloonImageViewerWindow()
{
    // 客户区控件树与 HWND 由 DuiFrameWindow 基类析构兜底。背景位图是 caller-owned
    //（SetBgImage 不接管），由本类释放。
    if (m_hBgBitmap != NULL)
    {
        ::DeleteObject(m_hBgBitmap);
        m_hBgBitmap = NULL;
    }
}

// =======================================================================
// 创建 / 显示
// =======================================================================

bool CBalloonImageViewerWindow::BuildWindow(HWND hWndParent)
{
    if (Create(hWndParent, CWindow::rcDefault, g_strAppTitle.c_str(),
               WS_OVERLAPPEDWINDOW, 0) == NULL)
    {
        LOG_ERROR(_T("看图窗口 Create 失败。"));
        return false;
    }

    // 标题栏：透明深色拖动条 —— 右上角保留最小化 / 最大化 / 关闭三按钮。标题文字
    // 颜色设成与背景同色使其在窗体上不可见（任务栏标题不受影响），保持顶部清爽。
    SetTitle(TR(L"图片查看"));
    SetTitleBarHeight(kVwTitleBarHeight);
    SetTitleBarTransparent(true);
    SetTitleTextColor(kVwDarkColor);
    SetButtons(true, true, true);            // 最小化 / 最大化 / 关闭
    SetCaptionGlyphColor(kVwCaptionGlyph);   // 深色条上用浅灰 glyph（关闭 hover 转 Win 红）
    SetResizable(true);
    SetMinSize(kVwMinWidth, kVwMinHeight);
    // 标题栏左侧的程序图标：标题栏透明，透出的是深色背景，底色用 kVwDarkColor；同时设好任务栏图标
    SetIcon(m_programIcons.TitleBitmap(balloonmain::kProgramTitleIconSize, kVwDarkColor));
    m_programIcons.ApplyTaskIcons(m_hWnd);

    // 深色背景：用一张纯深色位图铺满（零 inset 整张拉伸），让透明标题栏区域与
    // 画布之外的区域都是深色。位图本类持有、析构释放。
    m_hBgBitmap = CreateSolidColorBitmap(8, kVwDarkColor);
    if (m_hBgBitmap != NULL)
    {
        RECT zeroInsets = { 0, 0, 0, 0 };
        SetBgImage(m_hBgBitmap, zeroInsets);
    }

    InitUi();

    ResizeClient(kVwInitWidth, kVwInitHeight);
    CenterWindow();
    ShowWindow(SW_SHOW);
    UpdateWindow();
    ::SetForegroundWindow(m_hWnd);
    return true;
}

void CBalloonImageViewerWindow::InitUi()
{
    std::unique_ptr<balloonimageviewer::ImageCanvas> canvas(
        new balloonimageviewer::ImageCanvas());
    canvas->SetCtrlId(kVwCanvasCtrlId);
    m_pCanvas = canvas.get();
    SetClientContent(std::unique_ptr<DuiControl>(canvas.release()));

    LOG_INFO(_T("看图窗客户区初始化完成。"));
}

void CBalloonImageViewerWindow::CreateAndShow(HWND hMainDlg)
{
    // 调试入口（命令行 --imageviewer）：无父窗口、非模态。
    m_hMainDlg = hMainDlg;
    if (!BuildWindow(NULL))
    {
        return;
    }

    // 优先用命令行 --imageviewer 之后紧跟的图片路径（便于自动化截图核对渲染）；
    // 未提供则弹文件选择框人工挑图。取消则保留空画布（只剩深色背景），按 Esc 关闭。
    CString cmdPath = ParseDebugImagePath();
    if (!cmdPath.IsEmpty())
    {
        if (m_pCanvas != NULL && !m_pCanvas->LoadImageFromFile(cmdPath))
        {
            LOG_ERROR(_T("看图窗加载命令行指定图片失败。"));
        }
    }
    else
    {
        //文件类型过滤串：说明文字按界面语言翻译后再拼成带 \0 的过滤串，须活到对话框关闭
        const std::wstring fileFilter = filedlg::MakeFilter(TR(L"图片文件"), filedlg::kImagePatterns, TR(L"所有文件"), L"*.*");
        CFileDialog dlg(TRUE, NULL, NULL,
                        OFN_FILEMUSTEXIST | OFN_HIDEREADONLY | OFN_EXPLORER,
                        fileFilter.c_str(),
                        m_hWnd);
        if (dlg.DoModal() == IDOK && m_pCanvas != NULL)
        {
            if (!m_pCanvas->LoadImageFromFile(dlg.m_szFileName))
            {
                LOG_ERROR(_T("看图窗加载所选图片失败。"));
            }
        }
    }
    LOG_INFO(_T("看图窗口已创建并显示（调试入口）。"));
}

void CBalloonImageViewerWindow::OpenShared(HWND hOwner, LPCTSTR imagePath)
{
    // 进程内单例：首次调用时构造，之后复用同一个看图窗。m_hMainDlg 保持 NULL，
    // 故关闭只销毁本窗、不退应用。非 UI 线程不应调用本函数（看图窗须在 UI 线程）。
    OpenSharedWithNavigator(hOwner, imagePath, NULL);
}

void CBalloonImageViewerWindow::OpenSharedWithNavigator(HWND hOwner, LPCTSTR imagePath, IImageViewerNavigator* navigator)
{
    // 进程内单例：首次调用时构造，之后复用同一个看图窗。m_hMainDlg 保持 NULL，
    // 故关闭只销毁本窗、不退应用。非 UI 线程不应调用本函数（看图窗须在 UI 线程）。
    // 每次打开都重设翻页回调：聊天图片（不带回调）打开后，← / → 不再翻文件共享的图片
    static CBalloonImageViewerWindow s_sharedViewer;
    s_sharedViewer.SetNavigator(navigator);
    s_sharedViewer.ShowImage(hOwner, imagePath);
}

void CBalloonImageViewerWindow::SetNavigator(IImageViewerNavigator* navigator)
{
    m_navigator = navigator;
}

void CBalloonImageViewerWindow::ShowImage(HWND hOwner, LPCTSTR imagePath)
{
    // 正式使用：窗口未创建则先建，已创建则复用换图。
    if (!IsWindow())
    {
        if (!BuildWindow(hOwner))
        {
            return;
        }
    }
    if (m_pCanvas != NULL && imagePath != NULL)
    {
        m_pCanvas->LoadImageFromFile(imagePath);
    }
    if (IsWindow())
    {
        ShowWindow(SW_SHOW);
        ::SetForegroundWindow(m_hWnd);
    }
}

// =======================================================================
// 消息处理
// =======================================================================

LRESULT CBalloonImageViewerWindow::OnKeyDownMsg(UINT /*uMsg*/, WPARAM wParam,
                                                LPARAM /*lParam*/, BOOL& bHandled)
{
    if (wParam == VK_ESCAPE)
    {
        // Esc 关闭看图窗。
        PostMessage(WM_CLOSE);
        return 0;
    }
    // ← / →：有翻页回调时交给它（文件共享的图片预览翻到上一张 / 下一张）
    if ((wParam == VK_LEFT || wParam == VK_RIGHT) && m_navigator != NULL)
    {
        m_navigator->OnImageViewerNavigate(wParam == VK_LEFT ? -1 : 1);
        return 0;
    }
    bHandled = FALSE;   // 其余键交回基类链
    return 0;
}

LRESULT CBalloonImageViewerWindow::OnCloseMsg(UINT /*uMsg*/, WPARAM /*wParam*/,
                                              LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    // 调试入口下，本窗口是唯一可见窗口，关闭即请求退出程序；
    // 正式使用下 m_hMainDlg 为 NULL，仅销毁本窗口。
    if (m_hMainDlg != NULL && ::IsWindow(m_hMainDlg))
    {
        ::PostMessage(m_hMainDlg, WM_CLOSE_MAINDLG, 0, 0);
    }
    // 关闭之后不再翻页
    m_navigator = NULL;
    if (IsWindow())
    {
        DestroyWindow();
    }
    return 0;
}
