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
 *  balloonwj@qq.com   2026-06-04
 */
#include "stdafx.h"
#include "BalloonClosePromptDlg.h"

#include <memory>

#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")

#include "../IULog.h"
#include "../resource.h"                 // IDR_MAINFRAME（标题栏程序图标）
#include "../FlamingoClient.h"           // CFlamingoClient::m_UserConfig / SaveUserConfig
#include "../CustomMsgDef.h"             // WM_CLOSE_MAINDLG（退出时投递给 CMainDlg）
#include "MainPanelIcons.h"              // DrawLineIcon / kIconPanel / kIconExit
#include "ClosePromptDecision.h"         // DecideOnConfirm / DefaultSelection / CloseAction
#include "../../balloonui/balloonui/Controls/Layout/DuiLayout.h"   // DuiVBox / DuiHBox
#include "../../balloonui/balloonui/Controls/Basic/DuiLabel.h"

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
        L"Microsoft YaHei");
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
        // owner 传 NULL：让子控件通知自路由回本对话框（与改密 / 新建群对话框同处理）。
        if (Create(NULL, CWindow::rcDefault, _T("关闭 FlamingoIM"),
                   WS_OVERLAPPEDWINDOW, 0) == NULL)
        {
            LOG_ERROR(_T("关闭确认对话框 Create 失败。"));
            return;
        }
        SetTitle(_T("关闭 FlamingoIM"));
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
        ResizeClient(kDlgWidth, kDlgHeight);
        CenterWindow(m_panelHwnd);   // 居中到主面板
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

    ShowWindow(SW_SHOW);
    UpdateWindow();
    ::SetForegroundWindow(m_hWnd);
    LOG_INFO(_T("关闭确认对话框已打开。"));
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
        subtitle->SetText(_T("请选择关闭方式"));
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
        card->SetCardContent(balloonmain::kIconPanel, _T("最小化到托盘"),
                             _T("继续在后台运行，可从托盘图标重新打开"));
        m_cardMinimize = card.get();
        root->AddChild(std::move(card), balloonwjui::DuiLayout::Hint().Fixed(kCardH));
    }

    // ---- 卡片：退出 FlamingoIM（红）----
    {
        std::unique_ptr<balloonmain::ClosePromptOptionCard> card(
            new balloonmain::ClosePromptOptionCard());
        card->SetCtrlId(kIdCardExit);
        card->SetAccent(kAccentRed);
        card->SetCardContent(balloonmain::kIconExit, _T("退出 FlamingoIM"),
                             _T("关闭应用，下次需要重新登录"));
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
        remember->SetText(_T("记住我的选择"));
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
        cancel->SetText(_T("取消"));
        bottom->AddChild(std::move(cancel), balloonwjui::DuiLayout::Hint().Fixed(kBtnWidth));

        std::unique_ptr<balloonwjui::DuiButton> confirm(new balloonwjui::DuiButton());
        confirm->SetButtonType(balloonwjui::DuiButton::StylePushButton);
        confirm->SetVariant(balloonwjui::DuiButton::Variant::Primary);   // 默认蓝，SelectAction 据选中项再调
        confirm->SetCtrlId(kIdConfirm);
        confirm->SetText(_T("确认"));
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

    if (IsWindow())
    {
        DestroyWindow();
    }

    // 执行动作：两种方式都先隐藏主面板（视觉上立即消失）；退出再通知 CMainDlg 收尾。
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
        LOG_INFO(_T("关闭确认：用户选择退出，已通知主窗口收尾。"));
    }
    else
    {
        LOG_INFO(_T("关闭确认：用户选择最小化到任务栏。"));
    }
}

// =======================================================================
// 事件处理
// =======================================================================

LRESULT CBalloonClosePromptDlg::OnCloseMsg(UINT /*uMsg*/, WPARAM /*wParam*/,
                                           LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    // 关闭(×) = 取消：仅销毁本窗口，不改配置、不执行任何动作（对象由主面板持有可复用）。
    if (IsWindow())
    {
        DestroyWindow();
    }
    return 0;
}

LRESULT CBalloonClosePromptDlg::OnKeyDownMsg(UINT /*uMsg*/, WPARAM wParam,
                                             LPARAM /*lParam*/, BOOL& bHandled)
{
    // ESC = 取消，等同关闭按钮。
    if (wParam == VK_ESCAPE)
    {
        if (IsWindow())
        {
            DestroyWindow();
        }
        return 0;
    }
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
        case kIdCardMinimize:
            SelectAction(balloonmain::kCloseActionMinimize);
            return 0;
        case kIdCardExit:
            SelectAction(balloonmain::kCloseActionExit);
            return 0;
        case kIdCancel:
            if (IsWindow())
            {
                DestroyWindow();
            }
            return 0;
        case kIdConfirm:
            DoConfirm();
            return 0;
        default:
            break;
        }
    }

    bHandled = FALSE;
    return 0;
}
