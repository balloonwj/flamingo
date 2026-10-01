/**
 *  BalloonKickoutDlg.cpp —— "被踢下线"提示框实现。
 *
 *  控件树用 balloonui 现成控件程序化组合：根 DuiVBox（正文 DuiLabel 自动换行 +
 *  底部 DuiHBox 弹性空白 + "确定" DuiButton）。窗口高度按正文实测行高算出，故换成
 *  更长的原因文案也不会被截断。模态循环照 CBalloonLoginDlg::DoModal 的做法手写。
 *
 *  balloonwj@qq.com   2026-08-04
 */

#include "stdafx.h"
#include "BalloonKickoutDlg.h"

#include <memory>

#include "../../Common/Log/IULog.h"
#include "../../resource.h"                 // IDR_MAINFRAME（标题栏程序图标）
#include "KickoutNotice.h"               // GetKickoutNoticeTitle / GetKickoutNoticeText
#include "PopupMenuPlacement.h"          // PlaceTrayAnchoredWindow（贴托盘落点）
#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"   // DuiVBox / DuiHBox
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"

// ---- 对话框固定尺寸（逻辑像素）-----------------------------------------
static const int kDlgWidth     = 360;   // 客户区宽（正文按此宽度折行）
static const int kDlgTitleBarH = 44;    // 标题栏高（与关闭确认框一致，容纳标题 + 图标 + 关闭按钮）

// ---- 控件 id（对话框内唯一；须 >= 10，避开标题栏 min/max/close 占用的 1/2/3）----
static const UINT kIdOk = 10;           // "确定"按钮

// ---- 布局行高 / 间距（逻辑像素）----------------------------------------
static const int kRootPadL   = 20;   // 根容器左内距
static const int kRootPadT   = 16;   // 根容器上内距
static const int kRootPadR   = 20;   // 根容器右内距
static const int kRootPadB   = 16;   // 根容器下内距
static const int kRootGap    = 14;   // 正文与按钮行之间的间距
static const int kBottomBarH = 32;   // 底部按钮行高（= 按钮高）
static const int kBtnWidth   = 88;   // "确定"按钮宽
static const int kTextMinH   = 48;   // 正文区最小高度：文案再短窗口也不至于扁成一条

// ---- 配色 --------------------------------------------------------------
static const COLORREF kBodyBgColor  = RGB(255, 255, 255);      // 正文区白底
static const COLORREF kBodyTextColor = RGB(0x2A, 0x2A, 0x2A);  // 正文深灰（与关闭确认框标题同色）

// ---- 标题栏 ------------------------------------------------------------
// 标题栏背景色须与 DuiFrameWindow 标题条默认填充色一致：标题图标以 SRCCOPY 绘制、
// 不做 alpha 混合，故先把程序图标合成到此底色上，透明区才与标题栏浑然一体。
static const COLORREF kTitleBarBgColor = RGB(245, 246, 250);
static const int      kTitleIconSize   = 16;   // 标题栏图标边长

namespace {

// 把图标合成到给定底色上，得到标题栏用的位图。返回的 HBITMAP 由调用方释放。
// （与 BalloonClosePromptDlg.cpp 里同名辅助同一做法：DuiFrameWindow::SetIcon 只认
// 位图且不做 alpha 混合，直接给带透明区的图标会出现黑边。）
HBITMAP MakeTitleIconBitmap(HICON hIcon, int sizePx, COLORREF bgColor)
{
    if (hIcon == NULL || sizePx <= 0)
    {
        return NULL;
    }

    HDC     screen = ::GetDC(NULL);
    HDC     mem    = ::CreateCompatibleDC(screen);
    HBITMAP hbmp   = ::CreateCompatibleBitmap(screen, sizePx, sizePx);
    ::ReleaseDC(NULL, screen);
    if (mem == NULL || hbmp == NULL)
    {
        if (mem != NULL)
        {
            ::DeleteDC(mem);
        }
        if (hbmp != NULL)
        {
            ::DeleteObject(hbmp);
        }
        return NULL;
    }

    HGDIOBJ old = ::SelectObject(mem, hbmp);

    RECT   rc  = { 0, 0, sizePx, sizePx };
    HBRUSH hbr = ::CreateSolidBrush(bgColor);
    ::FillRect(mem, &rc, hbr);
    ::DeleteObject(hbr);

    ::DrawIconEx(mem, 0, 0, hIcon, sizePx, sizePx, 0, NULL, DI_NORMAL);

    ::SelectObject(mem, old);
    ::DeleteDC(mem);
    return hbmp;
}

} // anonymous namespace

// =======================================================================
// 构造 / 析构
// =======================================================================

CBalloonKickoutDlg::CBalloonKickoutDlg()
    : m_modalRunning(false)
    , m_modalResult(IDOK)
    , m_titleIconBmp(NULL)
{
}

CBalloonKickoutDlg::~CBalloonKickoutDlg()
{
    if (m_titleIconBmp != NULL)
    {
        ::DeleteObject(m_titleIconBmp);
        m_titleIconBmp = NULL;
    }
}

// =======================================================================
// 模态显示
// =======================================================================

INT_PTR CBalloonKickoutDlg::DoModal(HWND hWndParent, int reason, const RECT& trayIconRect)
{
    m_modalResult  = IDOK;
    m_modalRunning = true;

    // 创建无边框 DUI 顶层窗口。WS_THICKFRAME（含在 WS_OVERLAPPEDWINDOW 里）须保留——
    // DuiFrameWindow 靠 WM_NCCALCSIZE 抠掉系统 chrome，但需要它维持窗口动画。
    //
    // WS_EX_TOPMOST 在<u>建窗时</u>就带上：置顶是本框的硬要求，建窗即生效最稳妥，
    // 不必指望后面某一次 SetWindowPos 一定跑到（后面 PlaceNearTray、ShowWindow 之后
    // 还各有一道重申，再加上 WM_WINDOWPOSCHANGING 兜底，三重保险见下）。
    if (Create(hWndParent, CWindow::rcDefault, balloonmain::GetKickoutNoticeTitle(),
               WS_OVERLAPPEDWINDOW, WS_EX_TOPMOST) == NULL)
    {
        LOG_ERROR(_T("被踢下线提示框 Create 失败。"));
        m_modalRunning = false;
        return IDOK;   // 窗口没建起来也不能卡住调用方，照常让它继续回登录界面
    }

    ConfigureFrame();

    // BuildUi 给的是<u>正文区</u>该有的高度，而 ResizeClient 设的是整个客户区 —— 后者
    // 把自绘标题栏也算在内（DuiFrameWindow 的 DUI 根 = 标题栏 + 客户内容，见其头文件
    // "host 的客户区 = 整个窗口"）。漏加这一段的后果是正文区被挤掉一个标题栏的高度，
    // 底部"确定"按钮直接被窗口下边缘切掉半截。
    const int nClientHeight = kDlgTitleBarH + BuildUi(reason);

    // 最小尺寸同步抬到实际高度：SetMinSize 定的下限会在 WM_GETMINMAXINFO 里生效，
    // 若低于这里要设的高度就形同虚设，若高于则会把窗口顶大 —— 两者取同一个值最省心。
    SetMinSize(kDlgWidth, nClientHeight);
    ResizeClient(kDlgWidth, nClientHeight);
    PlaceNearTray(trayIconRect);

    ShowWindow(SW_SHOW);
    UpdateWindow();

    // 显示之后再重申一次置顶并提到最前。ShowWindow / 随后的激活都可能重排 z 序，
    // 这一道保证"刚弹出来那一刻"就在最上层；被顶下线时用户往往正在别的程序里忙，
    // 提示框要是压在窗口堆下面，就又变回上一版托盘气泡那种看不见的效果了。
    SetWindowPos(HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    BringWindowToTop();
    ::SetForegroundWindow(m_hWnd);

    // 模态化：禁用 owner，跑独立消息循环，直到 EndModal 把 m_modalRunning 置 false。
    // 与 CBalloonLoginDlg::DoModal 同一套写法。
    if (::IsWindow(hWndParent))
    {
        ::EnableWindow(hWndParent, FALSE);
    }

    MSG msg;
    while (::GetMessage(&msg, NULL, 0, 0))
    {
        ::TranslateMessage(&msg);
        ::DispatchMessage(&msg);
        if (!m_modalRunning)
        {
            break;
        }
    }

    if (::IsWindow(hWndParent))
    {
        // 先恢复 owner 再销毁本窗口：顺序反了的话，本窗口销毁时焦点会落到别的进程窗口上。
        ::EnableWindow(hWndParent, TRUE);
        ::SetForegroundWindow(hWndParent);
    }

    if (IsWindow())
    {
        DestroyWindow();
    }

    return m_modalResult;
}

void CBalloonKickoutDlg::EndModal(INT_PTR result)
{
    m_modalResult  = result;
    m_modalRunning = false;
}

// =======================================================================
// 外观与布局
// =======================================================================

void CBalloonKickoutDlg::ConfigureFrame()
{
    SetTitle(balloonmain::GetKickoutNoticeTitle());
    SetTitleBarHeight(kDlgTitleBarH);
    SetButtons(false, false, true);   // 只留关闭按钮：本框没有最小化 / 最大化的意义
    SetResizable(false);
    //最小尺寸等 BuildUi 把正文高度量出来之后再设（见 DoModal），这里不预设。

    // 标题栏图标用程序图标 IDR_MAINFRAME。SetIcon 只借用位图，故本类持有、析构时释放。
    if (m_titleIconBmp == NULL)
    {
        HICON hIcon = AtlLoadIconImage(IDR_MAINFRAME, LR_DEFAULTCOLOR,
                                       kTitleIconSize, kTitleIconSize);
        if (hIcon != NULL)
        {
            m_titleIconBmp = MakeTitleIconBitmap(hIcon, kTitleIconSize, kTitleBarBgColor);
            ::DestroyIcon(hIcon);
        }
    }
    if (m_titleIconBmp != NULL)
    {
        SetIcon(m_titleIconBmp);
    }
}

int CBalloonKickoutDlg::BuildUi(int reason)
{
    std::unique_ptr<balloonwjui::DuiVBox> root(new balloonwjui::DuiVBox());
    root->SetPadding(kRootPadL, kRootPadT, kRootPadR, kRootPadB);
    root->SetGap(kRootGap);
    root->SetBgColor(kBodyBgColor);

    // ---- 正文：按原因取文案，自动换行 ----
    const int nTextWidth = kDlgWidth - kRootPadL - kRootPadR;
    int       nTextHeight = kTextMinH;
    {
        std::unique_ptr<balloonwjui::DuiLabel> body(new balloonwjui::DuiLabel());
        body->SetText(balloonmain::GetKickoutNoticeText(reason));
        body->SetTextColor(kBodyTextColor);
        body->SetWordWrap(true);
        body->SetTextAlign(DT_LEFT | DT_TOP | DT_WORDBREAK);

        // 实测这段文字在正文宽度下要占多高，窗口高度据此算 —— 文案换成更长的一条
        // （如"账号被管理员停用"那句）也不会被截掉半行。
        const int nMeasured = body->MeasureHeight(nTextWidth);
        if (nMeasured > nTextHeight)
        {
            nTextHeight = nMeasured;
        }

        // 正文用<u>弹性</u>占位而不是固定高度：客户区最终高度受 DPI 缩放、最小尺寸约束
        // 等因素影响，未必与上面算出的值分毫不差。给正文 Weight(1)，多出来的余量就被
        // 顶部这块（文字左上对齐）悄悄吸收，底部按钮始终贴着下内边距，不会一会儿悬空
        // 一会儿被切掉。
        root->AddChild(std::move(body), balloonwjui::DuiLayout::Hint().Weight(1));
    }

    // ---- 底部：弹性空白 + "确定" ----
    {
        std::unique_ptr<balloonwjui::DuiHBox> bottom(new balloonwjui::DuiHBox());

        std::unique_ptr<balloonwjui::DuiControl> spacer(new balloonwjui::DuiControl());
        bottom->AddChild(std::move(spacer), balloonwjui::DuiLayout::Hint().Weight(1));

        std::unique_ptr<balloonwjui::DuiButton> ok(new balloonwjui::DuiButton());
        ok->SetButtonType(balloonwjui::DuiButton::StylePushButton);
        ok->SetVariant(balloonwjui::DuiButton::Variant::Primary);
        ok->SetCtrlId(kIdOk);
        ok->SetText(_T("确定"));
        bottom->AddChild(std::move(ok), balloonwjui::DuiLayout::Hint().Fixed(kBtnWidth));

        root->AddChild(std::move(bottom), balloonwjui::DuiLayout::Hint().Fixed(kBottomBarH));
    }

    SetClientContent(std::move(root));

    return kRootPadT + nTextHeight + kRootGap + kBottomBarH + kRootPadB;
}

void CBalloonKickoutDlg::PlaceNearTray(const RECT& trayIconRect)
{
    RECT rcWnd = { 0 };
    GetWindowRect(&rcWnd);

    SIZE size;
    size.cx = rcWnd.right - rcWnd.left;
    size.cy = rcWnd.bottom - rcWnd.top;

    const POINT pt = balloonmain::PlaceTrayAnchoredWindow(trayIconRect, size);

    LOG_INFO("KickoutDlg::PlaceNearTray: tray=(%ld,%ld,%ld,%ld), size=(%ld,%ld), origin=(%ld,%ld).",
             trayIconRect.left, trayIconRect.top, trayIconRect.right, trayIconRect.bottom,
             size.cx, size.cy, pt.x, pt.y);

    // 同一次调用里把落点与 z 序一起定了：HWND_TOPMOST 让本框压在所有窗口之上。
    // 置顶是必需的 —— 被顶下线时用户多半正在别的程序里忙，主面板可能还最小化着；
    // 提示框若混在窗口堆里，就退化成上一版托盘气泡那种"看不见"的效果了。
    // 本调用在 ShowWindow 之前，故窗口从第一帧起就是置顶的，不会先闪一下再跳到最前。
    SetWindowPos(HWND_TOPMOST, pt.x, pt.y, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
}

// =======================================================================
// 消息处理
// =======================================================================

LRESULT CBalloonKickoutDlg::OnDuiNotify(UINT /*uMsg*/, WPARAM /*wParam*/,
                                        LPARAM lParam, BOOL& bHandled)
{
    const balloonwjui::DuiNotify* pNotify =
        reinterpret_cast<const balloonwjui::DuiNotify*>(lParam);
    if (pNotify == NULL)
    {
        bHandled = FALSE;
        return 0;
    }

    if (pNotify->code == balloonwjui::DUIN_CLICK && pNotify->ctrlId == kIdOk)
    {
        EndModal(IDOK);
        return 0;
    }

    bHandled = FALSE;
    return 0;
}

LRESULT CBalloonKickoutDlg::OnCloseMsg(UINT /*uMsg*/, WPARAM /*wParam*/,
                                       LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    // 点标题栏关闭按钮与点"确定"等价：本框只是告知，没有"取消"语义。
    EndModal(IDOK);
    return 0;
}

LRESULT CBalloonKickoutDlg::OnKeyDownMsg(UINT /*uMsg*/, WPARAM wParam,
                                         LPARAM /*lParam*/, BOOL& bHandled)
{
    if (wParam == VK_ESCAPE || wParam == VK_RETURN)
    {
        EndModal(IDOK);
        return 0;
    }

    bHandled = FALSE;
    return 0;
}

LRESULT CBalloonKickoutDlg::OnWindowPosChangingMsg(UINT /*uMsg*/, WPARAM /*wParam*/,
                                                   LPARAM lParam, BOOL& bHandled)
{
    WINDOWPOS* pPos = (WINDOWPOS*)lParam;

    // 只在这次变更确实要动 z 序时才改（带 SWP_NOZORDER 的纯移动 / 缩放不必插手）。
    if (pPos != NULL && (pPos->flags & SWP_NOZORDER) == 0)
    {
        pPos->hwndInsertAfter = HWND_TOPMOST;
    }

    // 不截断：基类还要据本消息更新自身状态。
    bHandled = FALSE;
    return 0;
}
