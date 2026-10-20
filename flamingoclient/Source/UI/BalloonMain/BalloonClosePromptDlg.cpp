/**
 *  BalloonClosePromptDlg.cpp —— "关闭确认"对话框实现。
 *
 *  控件树用 balloonui 现成控件程序化组合（对照 design/退出确认1.png / 退出确认2.png）：
 *  正文首行 DuiLabel 副标题；两张 ClosePromptOptionCard（自绘 DuiButton 子类，整卡
 *  可点 + 选中高亮）；底部 DuiHBox = DuiButton(StyleCheckbox 记住我的选择) + 弹性
 *  空白 + 取消 / 确认。确认流程：据选中项 + 记住勾选经 ClosePromptDecision 算决策 →
 *  写回 UserConfig 落盘 → 关本框 → 隐藏主面板（最小化）或 PostMessage(WM_CLOSE_MAINDLG)
 *  触发退出。
 *
 *  模态的实现见 OpenForClose / CloseSelf / RestoreHostEnabled：owner = 主面板保证
 *  z 序，::EnableWindow 保证主面板不可操作，恢复动作收敛在 RestoreHostEnabled 一处、
 *  由 WM_DESTROY 兜底。设计取舍写在本类头文件的说明里。
 *
 *  balloonwj@qq.com   2026-06-04
 */
#include "stdafx.h"
#include "BalloonClosePromptDlg.h"

#include <memory>

#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")

#include "../../Common/Log/IULog.h"
#include "../../App/BrandText.h"            // g_strAppTitle（标题与「退出」卡片里的产品名称）
#include "../../resource.h"                 // IDR_MAINFRAME（标题栏程序图标）
#include "../../App/FlamingoClient.h"           // CFlamingoClient::m_UserConfig / SaveUserConfig
#include "../../App/CustomMsgDef.h"             // WM_CLOSE_MAINDLG（退出时投递给主面板）
#include "MainPanelIcons.h"              // DrawLineIcon / kIconPanel / kIconExit
#include "ClosePromptDecision.h"         // DecideOnConfirm / DefaultSelection / CloseAction
#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"   // DuiVBox / DuiHBox
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../Common/I18n/I18n.h"   // TR：界面文字的多语言查找

// ---- 对话框固定尺寸（逻辑像素）-----------------------------------------
static const int kDlgWidth     = 380;   // 客户区宽
static const int kDlgHeight    = 236;   // 客户区高（按下方行高/间距算出）
static const int kDlgTitleBarH = 44;    // 标题栏高（容纳标题文字 + 程序图标 + 关闭按钮）

// ---- 控件 id（对话框内唯一；须 >= 10，避开标题栏 min/max/close 占用的 1/2/3）----
static const UINT kIdCardMinimize = 10;  // "最小化到任务栏"卡片
static const UINT kIdCardExit     = 11;  // "退出 FlamingoIM"卡片
static const UINT kIdRemember     = 12;  // "记住我的选择"复选框
static const UINT kIdCancel       = 13;  // 取消按钮
static const UINT kIdConfirm      = 14;  // 确认按钮

// ---- 布局行高 / 间距（逻辑像素）----------------------------------------
static const int kRootPadL    = 20;   // 根容器左内距
static const int kRootPadT    = 10;   // 根容器上内距
static const int kRootPadR    = 20;   // 根容器右内距
static const int kRootPadB    = 16;   // 根容器下内距
static const int kRootGap     = 10;   // 根容器子项间距
static const int kSubtitleH   = 18;   // 副标题行高
static const int kCardH       = 64;   // 选项卡片高
static const int kBottomBarH  = 34;   // 底部按钮栏高（= 按钮高）
static const int kCheckboxW   = 120;  // "记住我的选择"复选框宽
static const int kBtnWidth    = 72;   // 取消 / 确认按钮宽
static const int kBottomGap   = 10;   // 底部栏子项间距

// ---- 标题栏 --------------------------------------------------------------
// 标题栏背景色（须与 DuiFrameWindow 标题条默认填充色一致）。标题图标用 SRCCOPY
// 绘制、不做 alpha 混合，故把程序图标合成到此底色上，透明区才与标题栏一致。
static const COLORREF kTitleBarBgColor = RGB(245, 246, 250);
static const int      kTitleIconSize   = 16;   // 标题栏图标边长（与标题条绘制一致）

// ---- 强调色（卡片选中态）-----------------------------------------------
static const COLORREF kAccentBlue = RGB(0x2D, 0x6C, 0xDF);   // 最小化卡：品牌蓝
static const COLORREF kAccentRed  = RGB(0xE5, 0x48, 0x4D);   // 退出卡：危险红

// ---- 卡片配色 ----------------------------------------------------------
static const COLORREF kCardBgNormal   = RGB(0xF7, 0xF8, 0xFA);  // 默认卡底（浅灰）
static const COLORREF kCardBgHover    = RGB(0xEF, 0xF1, 0xF4);  // 悬停卡底（略深灰）
static const COLORREF kCardBorderIdle = RGB(0xEC, 0xED, 0xF0);  // 未选卡描边（极浅灰）
static const COLORREF kCardTitleColor = RGB(0x2A, 0x2A, 0x2A);  // 标题深灰（未选）
static const COLORREF kCardSubColor   = RGB(0x8A, 0x8A, 0x8A);  // 副标题灰
static const COLORREF kCardIconColor  = RGB(0x8A, 0x8A, 0x8A);  // 图标灰（未选）
static const COLORREF kRadioRingIdle  = RGB(0xC2, 0xC6, 0xCC);  // 单选圆环灰（未选）

// 选中态淡底：在白底上按强调色调出极淡的同色系底，使整卡高亮但不刺眼。
static const COLORREF kCardBgSelBlue  = RGB(0xEE, 0xF4, 0xFE);  // 蓝选中底
static const COLORREF kCardBgSelRed   = RGB(0xFD, 0xEC, 0xEC);  // 红选中底

// ---- 卡片几何（逻辑像素，基于 kCardH=64）-------------------------------
static const int           kCardRadius   = 10;   // 卡片圆角
static const int           kCardIconLeft = 16;   // 图标距卡左边
static const Gdiplus::REAL kCardIconSize = 22.0f;// 图标边长
static const int           kCardTextLeft = 52;   // 文字列距卡左边（图标 + 间距后）
static const int           kCardRadioR   = 9;    // 单选圆环半径
static const int           kCardRadioPadR= 16;   // 单选圆环距卡右边
static const int           kTitlePx      = 14;   // 标题字号
static const int           kSubPx        = 11;   // 副标题字号

// =======================================================================
// 文件作用域辅助
// =======================================================================

namespace {

// COLORREF → 不透明 Gdiplus::Color。
Gdiplus::Color GdiColor(COLORREF c)
{
    return Gdiplus::Color(255, GetRValue(c), GetGValue(c), GetBValue(c));
}

// 用四段 90° 圆弧 + 闭合，向 path 添加一个圆角矩形。
void AddRoundRect(Gdiplus::GraphicsPath& path,
                  Gdiplus::REAL x, Gdiplus::REAL y,
                  Gdiplus::REAL w, Gdiplus::REAL h, Gdiplus::REAL radius)
{
    Gdiplus::REAL r = radius;
    if (r > w / 2.0f)
    {
        r = w / 2.0f;
    }
    if (r > h / 2.0f)
    {
        r = h / 2.0f;
    }
    const Gdiplus::REAL d = r * 2.0f;
    path.AddArc(x,         y,         d, d, 180.0f, 90.0f);
    path.AddArc(x + w - d, y,         d, d, 270.0f, 90.0f);
    path.AddArc(x + w - d, y + h - d, d, d, 0.0f,   90.0f);
    path.AddArc(x,         y + h - d, d, d, 90.0f,  90.0f);
    path.CloseFigure();
}

// 用 GDI 绘制文字 —— DrawText 经系统 ClearType 光栅化，清晰锐利。
void DrawGdiText(HDC hdc, LPCWSTR text, const RECT& rc,
                 int pixelHeight, bool bold, COLORREF color, UINT dtFlags)
{
    if (text == NULL || text[0] == L'\0' || pixelHeight <= 0)
    {
        return;
    }
    HFONT hFont = ::CreateFontW(
        -pixelHeight, 0, 0, 0,
        bold ? FW_SEMIBOLD : FW_NORMAL,
        FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
        i18n::UiFontName());
    if (hFont == NULL)
    {
        return;
    }
    HFONT    hOldFont  = (HFONT)::SelectObject(hdc, hFont);
    int      oldBkMode = ::SetBkMode(hdc, TRANSPARENT);
    COLORREF oldColor  = ::SetTextColor(hdc, color);
    RECT r = rc;
    ::DrawTextW(hdc, text, -1, &r, dtFlags);
    ::SetTextColor(hdc, oldColor);
    ::SetBkMode(hdc, oldBkMode);
    ::SelectObject(hdc, hOldFont);
    ::DeleteObject(hFont);
}

// 把 HICON 合成到 sizePx×sizePx 的位图上、底色填 bgColor，返回 HBITMAP。
// DuiFrameWindow 标题栏用 SRCCOPY（不混合 alpha）绘图标，故须自带与标题栏一致
// 的底色：DrawIconEx(DI_NORMAL) 会把图标按其掩码/alpha 合成到这层底色上，透明区
// 即呈现底色。返回的 HBITMAP 所有权归调用方，用完 DeleteObject。
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
// ClosePromptOptionCard —— 关闭方式卡片（自绘 DuiButton 子类）
// =======================================================================

namespace balloonmain {

ClosePromptOptionCard::ClosePromptOptionCard()
    : m_iconKind(-1)
    , m_accent(kAccentBlue)
    , m_selected(false)
{
}

void ClosePromptOptionCard::SetCardContent(int iconKind, LPCTSTR title, LPCTSTR subtitle)
{
    m_iconKind = iconKind;
    m_title    = (title != NULL) ? title : _T("");
    m_subtitle = (subtitle != NULL) ? subtitle : _T("");
    Invalidate();
}

void ClosePromptOptionCard::SetAccent(COLORREF accent)
{
    m_accent = accent;
    Invalidate();
}

void ClosePromptOptionCard::SetSelected(bool selected)
{
    if (m_selected == selected)
    {
        return;
    }
    m_selected = selected;
    Invalidate();
}

bool ClosePromptOptionCard::OnMouseEnter()
{
    bool r = balloonwjui::DuiButton::OnMouseEnter();   // 基类置 m_bHover=true
    Invalidate();                                      // 基类不重绘，补一次让 hover 底色立刻显示
    return r;
}

bool ClosePromptOptionCard::OnMouseLeave()
{
    bool r = balloonwjui::DuiButton::OnMouseLeave();   // 基类置 m_bHover=false
    Invalidate();                                      // 补一次让 hover 底色立刻消失
    return r;
}

void ClosePromptOptionCard::OnPaint(HDC hdc, const RECT& /*rcDirty*/)
{
    const int left   = m_rcItem.left;
    const int top    = m_rcItem.top;
    const int right  = m_rcItem.right;
    const int bottom = m_rcItem.bottom;
    const int w      = right - left;
    const int h      = bottom - top;
    if (w <= 0 || h <= 0)
    {
        return;
    }

    Gdiplus::Graphics g(hdc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

    // ---- 卡片底 + 描边（按选中 / 悬停 / 默认三态）----
    // 描边走 AA，故内缩 1px 让线宽完整落在矩形内、不被宿主裁剪。
    Gdiplus::GraphicsPath card;
    AddRoundRect(card, (Gdiplus::REAL)(left + 1), (Gdiplus::REAL)(top + 1),
                 (Gdiplus::REAL)(w - 2), (Gdiplus::REAL)(h - 2), (Gdiplus::REAL)kCardRadius);

    COLORREF bgColor;
    if (m_selected)
    {
        bgColor = (m_accent == kAccentRed) ? kCardBgSelRed : kCardBgSelBlue;
    }
    else if (IsHover())
    {
        bgColor = kCardBgHover;
    }
    else
    {
        bgColor = kCardBgNormal;
    }
    Gdiplus::SolidBrush bg(GdiColor(bgColor));
    g.FillPath(&bg, &card);

    if (m_selected)
    {
        Gdiplus::Pen border(GdiColor(m_accent), 1.6f);
        g.DrawPath(&border, &card);
    }
    else
    {
        Gdiplus::Pen border(GdiColor(kCardBorderIdle), 1.0f);
        g.DrawPath(&border, &card);
    }

    // ---- 左侧线性图标 ----
    if (m_iconKind >= 0)
    {
        Gdiplus::REAL ix = (Gdiplus::REAL)(left + kCardIconLeft);
        Gdiplus::REAL iy = (Gdiplus::REAL)top + ((Gdiplus::REAL)h - kCardIconSize) / 2.0f;
        Gdiplus::Color iconColor = m_selected ? GdiColor(m_accent) : GdiColor(kCardIconColor);
        balloonmain::DrawLineIcon(g, m_iconKind, ix, iy, kCardIconSize, iconColor, 2.0f);
    }

    // ---- 右侧单选圆环 / 圆点 ----
    {
        const Gdiplus::REAL cx = (Gdiplus::REAL)(right - kCardRadioPadR - kCardRadioR);
        const Gdiplus::REAL cy = (Gdiplus::REAL)top + (Gdiplus::REAL)h / 2.0f;
        const Gdiplus::REAL r  = (Gdiplus::REAL)kCardRadioR;
        if (m_selected)
        {
            Gdiplus::Pen ring(GdiColor(m_accent), 2.0f);
            g.DrawEllipse(&ring, cx - r, cy - r, r * 2.0f, r * 2.0f);
            Gdiplus::SolidBrush dot(GdiColor(m_accent));
            const Gdiplus::REAL dr = r - 4.0f;   // 内部实心点半径
            g.FillEllipse(&dot, cx - dr, cy - dr, dr * 2.0f, dr * 2.0f);
        }
        else
        {
            Gdiplus::Pen ring(GdiColor(kRadioRingIdle), 1.5f);
            g.DrawEllipse(&ring, cx - r, cy - r, r * 2.0f, r * 2.0f);
        }
    }

    // ---- 标题 + 副标题（GDI 文字，清晰）----
    const int textLeft  = left + kCardTextLeft;
    const int textRight = right - kCardRadioPadR - kCardRadioR * 2 - 12;   // 给单选点留位

    COLORREF titleColor = m_selected ? m_accent : kCardTitleColor;
    RECT rcTitle = { textLeft, top + 12, textRight, top + 33 };
    DrawGdiText(hdc, m_title, rcTitle, kTitlePx, true, titleColor,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    RECT rcSub = { textLeft, top + 33, textRight, top + 54 };
    DrawGdiText(hdc, m_subtitle, rcSub, kSubPx, false, kCardSubColor,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

} // namespace balloonmain

// =======================================================================
// CBalloonClosePromptDlg —— 构造 / 析构
// =======================================================================

CBalloonClosePromptDlg::CBalloonClosePromptDlg()
    : m_lpFMGClient(NULL)
    , m_panelHwnd(NULL)
    , m_mainDlgHwnd(NULL)
    , m_cardMinimize(NULL)
    , m_cardExit(NULL)
    , m_remember(NULL)
    , m_confirm(NULL)
    , m_selectedAction(balloonmain::kCloseActionMinimize)
    , m_built(false)
    , m_titleIconBmp(NULL)
{
}

CBalloonClosePromptDlg::~CBalloonClosePromptDlg()
{
    if (m_titleIconBmp != NULL)
    {
        ::DeleteObject(m_titleIconBmp);
        m_titleIconBmp = NULL;
    }
}

// =======================================================================
// 打开
// =======================================================================

void CBalloonClosePromptDlg::OpenForClose(HWND ownerPanel, HWND hMainDlg, CFlamingoClient* client)
{
    m_panelHwnd   = ownerPanel;
    m_mainDlgHwnd = hMainDlg;
    m_lpFMGClient = client;

    if (!IsWindow())
    {
        // owner 传主面板：owned 窗口在 z 序上恒在 owner 之上，用户再怎么点主面板都
        // 压不住本框；主面板被销毁时本框也会被系统一并销毁，不会留下孤立窗口。
        //
        // 曾经这里传的是 NULL，理由写作"让子控件通知自路由回本对话框"。2026-10-14 起
        // DuiHost::SendNotify 只把 WS_CHILD 宿主的控件通知发给父窗口，有无所有者，本框控件的
        // 通知都发回本框（开发约定文档 UI 约定第 11 条），不需要为此去掉所有者。
        // 在此之前 SendNotify 用 ::GetParent(m_hWnd) 找投递目标，本框带 owner 时通知同样回到
        // 本框：WS_OVERLAPPEDWINDOW 的顶层窗口即便带 owner，::GetParent 也返回 NULL（owner 须用
        // ::GetWindow(GW_OWNER) 取），Tests/ClosePromptDecisionTests.cpp 的 CPD15 验证的就是这一
        // Win32 语义。工程内 CBalloonKickoutDlg 与 CBalloonRegisterWindow 也一直是带 owner 创建。
        // 标题「关闭 <产品名称>」，产品名称取 flamingo.ini 的 [ui] apptitle
        const std::wstring caption = TRF(L"关闭 {0}", g_strAppTitle);
        if (Create(ownerPanel, CWindow::rcDefault, caption.c_str(),
                   WS_OVERLAPPEDWINDOW, 0) == NULL)
        {
            LOG_ERROR("ClosePromptDlg: create window failed.");
            return;
        }
        SetTitle(caption.c_str());
        SetTitleBarHeight(kDlgTitleBarH);
        SetButtons(false, false, true);   // 只留关闭按钮
        SetResizable(false);
        SetMinSize(kDlgWidth, kDlgHeight + kDlgTitleBarH);

        // 标题栏图标：用程序图标 IDR_MAINFRAME。合成到标题栏底色上的位图只建一次，
        // 跨"关闭再打开"复用；DuiFrameWindow::SetIcon 仅借用，本类析构时释放。
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

        m_built = false;
        BuildUiOnce();
        //kDlgHeight 是标题栏以下的内容高度，整窗高度要加上标题栏（DuiFrameWindow::ResizeClient 设的是整窗）
        ResizeClient(kDlgWidth, kDlgHeight + kDlgTitleBarH);

        // 居中到主面板。主面板若处于最小化或隐藏状态（例如从任务栏 / 托盘一侧触发
        // 关闭），ATL 的 CenterWindow 会自动改为按显示器工作区居中（见 atlwin.h 里
        // "don't center against invisible or minimized windows" 一段），故本框在
        // 这些情形下也落在屏幕中央、能被看见，不会跑到最小化窗口的离屏坐标上。
        CenterWindow(m_panelHwnd);
    }

    // 默认选中项：由当前"无提示时是否退出"偏好推出（首次配置为最小化，与设计稿一致）。
    BOOL exitWhenClose = (m_lpFMGClient != NULL)
                       ? m_lpFMGClient->m_UserConfig.IsEnableExitWhenCloseMainDlg()
                       : FALSE;
    SelectAction(balloonmain::DefaultSelection(exitWhenClose != FALSE));

    // "记住我的选择"每次打开复位为未勾选。
    if (m_remember != NULL)
    {
        m_remember->SetCheck(false, false);
    }

    // 模态化：把主面板置为不可操作。放在 ShowWindow 之前，使"主面板可点"与"本框
    // 已弹出"之间不留哪怕一帧的空隙。是否真的要禁用由 ClosePromptHostGuard 判定
    // （主面板句柄无效、或已被别的窗口禁用时不接管），恢复由 RestoreHostEnabled 负责。
    const bool hostIsWindow  = (m_panelHwnd != NULL) && (::IsWindow(m_panelHwnd) != FALSE);
    const bool hostIsEnabled = hostIsWindow && (::IsWindowEnabled(m_panelHwnd) != FALSE);
    if (m_hostGuard.OnOpen(hostIsWindow, hostIsEnabled))
    {
        ::EnableWindow(m_panelHwnd, FALSE);
    }

    ShowWindow(SW_SHOW);
    UpdateWindow();
    ::SetForegroundWindow(m_hWnd);
    LOG_INFO("ClosePromptDlg: opened (owner=0x%p, owner disabled=%d).",
             m_panelHwnd, (int)m_hostGuard.IsHostDisabled());
}

// =======================================================================
// 关闭收尾与宿主窗恢复
// =======================================================================

void CBalloonClosePromptDlg::CloseSelf()
{
    // 顺序不能颠倒：销毁一个处于激活态的窗口时，系统把激活权交回它的 owner，
    // 而 owner 必须已经是可操作的，否则激活权会落到别的进程窗口上。工程内
    // CBalloonKickoutDlg / CBalloonLoginDlg 的模态收尾也是先恢复 owner 再销毁。
    RestoreHostEnabled();

    if (IsWindow())
    {
        DestroyWindow();
    }
}

void CBalloonClosePromptDlg::RestoreHostEnabled()
{
    const bool hostIsWindow = (m_panelHwnd != NULL) && (::IsWindow(m_panelHwnd) != FALSE);
    if (!m_hostGuard.OnClose(hostIsWindow))
    {
        return;
    }

    ::EnableWindow(m_panelHwnd, TRUE);

    // 主面板仍然摆在桌面上时把激活权还给它；已隐藏或已最小化（确认"最小化到托盘"
    // 之后即属此列）就不必抢焦点，交由系统按常规规则处理。
    if (::IsWindowVisible(m_panelHwnd) && !::IsIconic(m_panelHwnd))
    {
        ::SetForegroundWindow(m_panelHwnd);
    }

    LOG_INFO("ClosePromptDlg: host panel re-enabled (owner=0x%p).", m_panelHwnd);
}

// =======================================================================
// 骨架构建（一次）
// =======================================================================

void CBalloonClosePromptDlg::BuildUiOnce()
{
    if (m_built)
    {
        return;
    }

    std::unique_ptr<balloonwjui::DuiVBox> root(new balloonwjui::DuiVBox());
    root->SetPadding(kRootPadL, kRootPadT, kRootPadR, kRootPadB);
    root->SetGap(kRootGap);
    root->SetBgColor(RGB(255, 255, 255));   // 白色卡底

    // ---- 副标题（设计稿表头副标题，因复用标题栏放到正文首行）----
    {
        std::unique_ptr<balloonwjui::DuiLabel> subtitle(new balloonwjui::DuiLabel());
        subtitle->SetText(TR(L"请选择关闭方式"));
        subtitle->SetTextColor(kCardSubColor);
        subtitle->SetTextAlign(DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        root->AddChild(std::move(subtitle), balloonwjui::DuiLayout::Hint().Fixed(kSubtitleH));
    }

    // ---- 卡片：最小化到托盘（蓝）----
    {
        std::unique_ptr<balloonmain::ClosePromptOptionCard> card(
            new balloonmain::ClosePromptOptionCard());
        card->SetCtrlId(kIdCardMinimize);
        card->SetAccent(kAccentBlue);
        card->SetCardContent(balloonmain::kIconPanel, TR(L"最小化到托盘"),
                             TR(L"继续在后台运行，可从托盘图标重新打开"));
        m_cardMinimize = card.get();
        root->AddChild(std::move(card), balloonwjui::DuiLayout::Hint().Fixed(kCardH));
    }

    // ---- 卡片：退出 <产品名称>（红）----
    {
        std::unique_ptr<balloonmain::ClosePromptOptionCard> card(
            new balloonmain::ClosePromptOptionCard());
        card->SetCtrlId(kIdCardExit);
        card->SetAccent(kAccentRed);
        // 产品名称过长时卡片标题以省略号截断（卡片绘制带 DT_END_ELLIPSIS）
        const std::wstring exitTitle = TRF(L"退出 {0}", g_strAppTitle);
        card->SetCardContent(balloonmain::kIconExit, exitTitle.c_str(),
                             TR(L"关闭应用，下次需要重新登录"));
        m_cardExit = card.get();
        root->AddChild(std::move(card), balloonwjui::DuiLayout::Hint().Fixed(kCardH));
    }

    // ---- 底部栏：记住我的选择 + 弹性空白 + 取消 + 确认 ----
    {
        std::unique_ptr<balloonwjui::DuiHBox> bottom(new balloonwjui::DuiHBox());
        bottom->SetGap(kBottomGap);

        std::unique_ptr<balloonwjui::DuiButton> remember(new balloonwjui::DuiButton());
        remember->SetButtonType(balloonwjui::DuiButton::StyleCheckbox);
        remember->SetVariant(balloonwjui::DuiButton::Variant::Ghost);   // 只画小勾选框，不铺整格浅底
        remember->SetText(TR(L"记住我的选择"));
        remember->SetTextAlign(DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        remember->SetCtrlId(kIdRemember);
        remember->SetCheck(false, false);
        m_remember = remember.get();
        bottom->AddChild(std::move(remember), balloonwjui::DuiLayout::Hint().Fixed(kCheckboxW));

        std::unique_ptr<balloonwjui::DuiControl> spacer(new balloonwjui::DuiControl());
        bottom->AddChild(std::move(spacer), balloonwjui::DuiLayout::Hint().Weight(1));

        std::unique_ptr<balloonwjui::DuiButton> cancel(new balloonwjui::DuiButton());
        cancel->SetButtonType(balloonwjui::DuiButton::StylePushButton);
        cancel->SetVariant(balloonwjui::DuiButton::Variant::Default);
        cancel->SetCtrlId(kIdCancel);
        cancel->SetText(TR(L"取消"));
        bottom->AddChild(std::move(cancel), balloonwjui::DuiLayout::Hint().Fixed(kBtnWidth));

        std::unique_ptr<balloonwjui::DuiButton> confirm(new balloonwjui::DuiButton());
        confirm->SetButtonType(balloonwjui::DuiButton::StylePushButton);
        confirm->SetVariant(balloonwjui::DuiButton::Variant::Primary);   // 默认蓝，SelectAction 据选中项再调
        confirm->SetCtrlId(kIdConfirm);
        confirm->SetText(TR(L"确认"));
        m_confirm = confirm.get();
        bottom->AddChild(std::move(confirm), balloonwjui::DuiLayout::Hint().Fixed(kBtnWidth));

        root->AddChild(std::move(bottom), balloonwjui::DuiLayout::Hint().Fixed(kBottomBarH));
    }

    SetClientContent(std::move(root));
    m_built = true;
}

// =======================================================================
// 选中 / 确认
// =======================================================================

void CBalloonClosePromptDlg::SelectAction(int action)
{
    m_selectedAction = action;

    if (m_cardMinimize != NULL)
    {
        m_cardMinimize->SetSelected(action == balloonmain::kCloseActionMinimize);
    }
    if (m_cardExit != NULL)
    {
        m_cardExit->SetSelected(action == balloonmain::kCloseActionExit);
    }

    // 确认按钮颜色跟随选中项：退出→危险红，最小化→品牌蓝。
    if (m_confirm != NULL)
    {
        m_confirm->SetVariant(action == balloonmain::kCloseActionExit
                              ? balloonwjui::DuiButton::Variant::Danger
                              : balloonwjui::DuiButton::Variant::Primary);
    }
}

void CBalloonClosePromptDlg::DoConfirm()
{
    bool remember = (m_remember != NULL) && m_remember->IsChecked();
    balloonmain::CloseDecision d = balloonmain::DecideOnConfirm(m_selectedAction, remember);

    // 写回偏好并立即落盘，使"记住我的选择"在最小化（进程不退出）情形下也持久化。
    if (m_lpFMGClient != NULL)
    {
        m_lpFMGClient->m_UserConfig.EnableExitPrompt(d.enablePrompt ? TRUE : FALSE);
        m_lpFMGClient->m_UserConfig.EnableExitWhenCloseMainDlg(d.exitWhenClose ? TRUE : FALSE);
        m_lpFMGClient->SaveUserConfig();
    }

    // 先取本地副本，DestroyWindow 后本对象成员仍有效但窗口已销毁。
    HWND panel   = m_panelHwnd;
    HWND mainDlg = m_mainDlgHwnd;
    int  action  = d.action;

    // 关闭收尾统一走 CloseSelf：它先把主面板恢复为可操作，再销毁本窗口。下面对
    // 主面板的隐藏 / 退出动作必须排在恢复之后 —— 一个禁用状态下的窗口被隐藏，
    // 下次显示出来仍是禁用的，用户就再也点不动它了。
    CloseSelf();

    // 执行动作：两种方式都先隐藏主面板（视觉上立即消失）；退出再请求进程收尾。
    if (panel != NULL && ::IsWindow(panel))
    {
        ::ShowWindow(panel, SW_HIDE);
    }
    if (action == balloonmain::kCloseActionExit)
    {
        if (mainDlg != NULL && ::IsWindow(mainDlg))
        {
            ::PostMessage(mainDlg, WM_CLOSE_MAINDLG, 0, 0);
        }
        LOG_INFO("ClosePromptDlg: user chose exit, WM_CLOSE_MAINDLG posted to main dialog.");
    }
    else
    {
        LOG_INFO("ClosePromptDlg: user chose minimize to tray.");
    }
}

// =======================================================================
// 事件处理
// =======================================================================

LRESULT CBalloonClosePromptDlg::OnCloseMsg(UINT /*uMsg*/, WPARAM /*wParam*/,
                                           LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    // 关闭(×) = 取消：恢复主面板后销毁本窗口，不改配置、不执行任何动作
    //（本对象由主面板持有，下次还能再打开）。
    CloseSelf();
    return 0;
}

LRESULT CBalloonClosePromptDlg::OnKeyDownMsg(UINT /*uMsg*/, WPARAM wParam,
                                             LPARAM /*lParam*/, BOOL& bHandled)
{
    // ESC = 取消，等同关闭按钮。
    if (wParam == VK_ESCAPE)
    {
        CloseSelf();
        return 0;
    }
    bHandled = FALSE;
    return 0;
}

LRESULT CBalloonClosePromptDlg::OnDestroyMsg(UINT /*uMsg*/, WPARAM /*wParam*/,
                                             LPARAM /*lParam*/, BOOL& bHandled)
{
    // 恢复主面板可操作状态的兜底。本框无论因何被销毁都会走到这里：用户确认、取消、
    // ESC、标题栏关闭按钮（这几条已在 CloseSelf 里恢复过，此处因幂等而不重复执行），
    // 以及不经 CloseSelf 的两条 —— CBalloonMainPanel 析构时直接 DestroyWindow 掉本框，
    // 以及主面板窗口销毁时系统连带销毁作为 owned 窗口的本框。漏掉任何一条，主面板
    // 都会永久停在不可操作状态。
    RestoreHostEnabled();

    // 不消费本消息：DuiFrameWindow::OnFrameDestroyMsg 还要在这里释放控件树。
    bHandled = FALSE;
    return 0;
}

LRESULT CBalloonClosePromptDlg::OnDuiNotify(UINT /*uMsg*/, WPARAM /*wParam*/,
                                            LPARAM lParam, BOOL& bHandled)
{
    const balloonwjui::DuiNotify* pNotify =
        reinterpret_cast<const balloonwjui::DuiNotify*>(lParam);
    if (pNotify == NULL)
    {
        bHandled = FALSE;
        return 0;
    }

    if (pNotify->code == balloonwjui::DUIN_CLICK)
    {
        switch (pNotify->ctrlId)
        {
        //点"最小化到托盘"卡片：整卡可点，仅切换选中态，不立即执行动作。
        case kIdCardMinimize:
            SelectAction(balloonmain::kCloseActionMinimize);
            return 0;

        //点"退出 FlamingoIM"卡片：同上，仅切换选中态。
        case kIdCardExit:
            SelectAction(balloonmain::kCloseActionExit);
            return 0;

        //点"取消"：不改配置、不执行动作，恢复主面板后关掉本框。
        case kIdCancel:
            CloseSelf();
            return 0;

        //点"确认"：按选中项执行最小化或退出，见 DoConfirm。
        case kIdConfirm:
            DoConfirm();
            return 0;

        //其余控件（如"记住我的选择"复选框）的点击本处不处理，交由下方
        //bHandled = FALSE 让消息继续沿消息映射交给基类。
        default:
            break;
        }
    }

    bHandled = FALSE;
    return 0;
}
