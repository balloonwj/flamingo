/**
 *  BalloonGroupInfoDlg.cpp
 *
 *  「群资料」对话框实现。
 *
 *  balloonwj@qq.com   2026-06-08
 */

#include "stdafx.h"

#include "BalloonGroupInfoDlg.h"
#include "BalloonGroupInfoLayout.h"
#include "BalloonMainPanel.h"            // CBalloonMainPanel::SetGroupConversation / LeaveGroup(写操作回投宿主)
#include "../../App/FlamingoClient.h"            // CFlamingoClient::IsOffline
#include "../../resource.h"                  // IDR_MAINFRAME(标题栏 / 任务栏程序图标)
#include "../../Common/Util/EncodeUtil.h"                // CString → UTF-8(发往服务端)
#include "../../Common/Log/IULog.h"                     // LOG_INFO / LOG_ERROR
#include "../../net/IUSocket.h"             // CIUSocket::SendSetConversation / SendSetMemberCard / SendLeaveGroup
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiSwitch.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiEdit.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiToast.h"   // 复制后的轻提示条
#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h" // 轻提示条挂在客户区根容器（DuiVBox）上
#include "../../Common/Util/ClipboardText.h"   // 点击群名 / 群号复制到剪贴板

#include <vector>                        // CStrToUtf8 缓冲
#include <windowsx.h>                    // GET_X_LPARAM / GET_Y_LPARAM(鼠标消息取坐标)
#include <gdiplus.h>                     // 退群按钮浅红圆角底位图用 GDI+ 抗锯齿绘制
#pragma comment(lib, "gdiplus.lib")

namespace balloonmain {

// ---- 对话框尺寸(逻辑像素,对照 design/群资料.png) ----
static const int kGroupInfoWidth        = 460;
static const int kGroupInfoHeight       = 408;
static const int kGroupInfoTitleBarH    = 36;
static const int kGroupInfoMinWidth     = 460;
static const int kGroupInfoMinHeight    = 408;

// ---- 退群按钮圆角底圆角半径(与 footer 按钮 9-grid 切片一致) ----
static const int kQuitBtnRadius         = 8;

// ---- 底部按钮左侧图标边长(像素;1:1 渲染并 SetLeadingIconSize 同值) ----
static const int kFooterIconSize        = 16;

// ---- 标题栏图标 ----
static const COLORREF kTitleBarBgColor  = RGB(245, 246, 250);
static const int kTitleIconSize         = 16;

// ---- 动画的推进方式 ----
// 开关(DuiSwitch)翻转的滑动中间帧由 DuiAnimMgr 推进。本对话框不必为此挂定时器:
// DuiAnimMgr 自 2026-08-14 起自带脉冲定时器(见 balloonui/DuiAnimation.cpp 的
// StartPulse / PulseProc),有动画在跑时它自己按 16ms 发 WM_TIMER,跑完即停。

namespace {

// CString(UTF-16) → UTF-8 std::string。空串安全。与 BalloonProfileDlg 同口径:
// 发往服务端的文本一律走 UTF-8。
std::string CStrToUtf8(const CString& text)
{
    int wlen = text.GetLength();
    if (wlen <= 0)
    {
        return std::string();
    }
    std::vector<char> buf((size_t)wlen * 4 + 1, 0);
    EncodeUtil::UnicodeToUtf8(text.GetString(), buf.data(), (int)buf.size());
    return std::string(buf.data());
}

// GDI+ 懒初始化 token(沿用项目其它 GDI+ helper 模式)。
ULONG_PTR& GdipToken()
{
    static ULONG_PTR s_token = 0;
    return s_token;
}

void EnsureGdiplus()
{
    if (GdipToken() != 0)
    {
        return;
    }
    Gdiplus::GdiplusStartupInput input;
    Gdiplus::GdiplusStartup(&GdipToken(), &input, NULL);
}

// 生成 side×side 的 32bpp 预乘 alpha 圆角矩形位图(内填 color、四角圆角 radius、
// 圆角外透明),用作退群按钮 9-grid 底图。与 BalloonProfileDlg::MakeRoundRectBitmap
// 同口径(GDI+ 抗锯齿 + PARGB)。失败返 NULL,所有权归调用方。
HBITMAP MakeRoundRectBitmap(COLORREF color, int side, int radius)
{
    if (side <= 0 || radius < 0)
    {
        return NULL;
    }
    EnsureGdiplus();

    BITMAPINFO bi = {};
    bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth       = side;
    bi.bmiHeader.biHeight      = -side;     // top-down
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    void* bits = NULL;
    HBITMAP hbm = ::CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (hbm == NULL || bits == NULL)
    {
        if (hbm != NULL)
        {
            ::DeleteObject(hbm);
        }
        return NULL;
    }
    ::ZeroMemory(bits, (size_t)side * side * 4);

    const int stride = side * 4;
    Gdiplus::Bitmap bitmap(side, side, stride, PixelFormat32bppPARGB,
                           static_cast<BYTE*>(bits));
    Gdiplus::Graphics g(&bitmap);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);

    const Gdiplus::REAL inset = 0.5f;
    const Gdiplus::REAL d = (Gdiplus::REAL)(radius * 2);
    const Gdiplus::REAL x = inset;
    const Gdiplus::REAL y = inset;
    const Gdiplus::REAL w = (Gdiplus::REAL)side - inset * 2.0f;
    const Gdiplus::REAL h = (Gdiplus::REAL)side - inset * 2.0f;

    Gdiplus::GraphicsPath path;
    if (radius > 0)
    {
        path.AddArc(x,         y,         d, d, 180.0f, 90.0f);
        path.AddArc(x + w - d, y,         d, d, 270.0f, 90.0f);
        path.AddArc(x + w - d, y + h - d, d, d,   0.0f, 90.0f);
        path.AddArc(x,         y + h - d, d, d,  90.0f, 90.0f);
        path.CloseFigure();
    }
    else
    {
        path.AddRectangle(Gdiplus::RectF(x, y, w, h));
    }

    Gdiplus::SolidBrush brush(Gdiplus::Color(255, GetRValue(color),
                                             GetGValue(color), GetBValue(color)));
    g.FillPath(&brush, &path);
    return hbm;
}

// 把 HICON 合成到 sizePx×sizePx 的位图上、底色填 bgColor,返回 HBITMAP。
// 与 BalloonProfileDlg::MakeTitleIconBitmap 同口径(标题栏 SRCCOPY 不混 alpha,
// 故图标透明区呈现底色)。所有权归调用方。
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

// 造一个 side×side 的 32bpp top-down 预乘 alpha DIBSection(全透明清零),供 GDI+ 在其上
// 画图标。bits 出参指向像素 buffer。失败返 NULL。与 MakeRoundRectBitmap 同款内存布局。
HBITMAP CreatePremultDib(int side, void** bitsOut)
{
    if (bitsOut != NULL)
    {
        *bitsOut = NULL;
    }
    if (side <= 0)
    {
        return NULL;
    }
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth       = side;
    bi.bmiHeader.biHeight      = -side;     // top-down
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    void* bits = NULL;
    HBITMAP hbm = ::CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (hbm == NULL || bits == NULL)
    {
        if (hbm != NULL)
        {
            ::DeleteObject(hbm);
        }
        return NULL;
    }
    ::ZeroMemory(bits, (size_t)side * side * 4);
    if (bitsOut != NULL)
    {
        *bitsOut = bits;
    }
    return hbm;
}

// 渲染一个 side×side 的"时钟"线描图标(圆环 + 时针 / 分针),颜色 color。用于
// "查看消息记录"按钮的左侧图标。返回 PARGB HBITMAP,所有权归调用方;失败返 NULL。
HBITMAP MakeClockIcon(int side, COLORREF color)
{
    EnsureGdiplus();
    void* bits = NULL;
    HBITMAP hbm = CreatePremultDib(side, &bits);
    if (hbm == NULL)
    {
        return NULL;
    }
    Gdiplus::Bitmap bitmap(side, side, side * 4, PixelFormat32bppPARGB,
                           static_cast<BYTE*>(bits));
    Gdiplus::Graphics g(&bitmap);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

    const Gdiplus::REAL stroke = side / 11.0f + 0.6f;   // 线宽随尺寸缩放
    Gdiplus::Pen pen(Gdiplus::Color(255, GetRValue(color), GetGValue(color),
                                    GetBValue(color)), stroke);
    pen.SetStartCap(Gdiplus::LineCapRound);
    pen.SetEndCap(Gdiplus::LineCapRound);

    // 圆环:内收半个线宽 + 1px,让 AA 边缘落在位图内。
    const Gdiplus::REAL inset = stroke / 2.0f + 1.0f;
    g.DrawEllipse(&pen, inset, inset,
                  (Gdiplus::REAL)side - inset * 2.0f,
                  (Gdiplus::REAL)side - inset * 2.0f);

    // 指针:时针朝上略偏、分针朝右偏上(约 10:10 经典表盘姿态)。
    const Gdiplus::REAL cx = side / 2.0f;
    const Gdiplus::REAL cy = side / 2.0f;
    const Gdiplus::REAL r  = side / 2.0f - inset;
    g.DrawLine(&pen, cx, cy, cx, cy - r * 0.50f);                    // 时针:朝上
    g.DrawLine(&pen, cx, cy, cx + r * 0.55f, cy - r * 0.18f);        // 分针:朝右上
    return hbm;
}

// 渲染一个 side×side 的"垃圾桶"线描图标(桶身梯形 + 桶盖 + 提手 + 两道竖纹),颜色
// color。用于"退出群聊"按钮的左侧图标(红)。返回 PARGB HBITMAP,所有权归调用方。
HBITMAP MakeTrashIcon(int side, COLORREF color)
{
    EnsureGdiplus();
    void* bits = NULL;
    HBITMAP hbm = CreatePremultDib(side, &bits);
    if (hbm == NULL)
    {
        return NULL;
    }
    Gdiplus::Bitmap bitmap(side, side, side * 4, PixelFormat32bppPARGB,
                           static_cast<BYTE*>(bits));
    Gdiplus::Graphics g(&bitmap);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

    const Gdiplus::REAL S = (Gdiplus::REAL)side;
    const Gdiplus::REAL stroke = side / 12.0f + 0.6f;
    Gdiplus::Pen pen(Gdiplus::Color(255, GetRValue(color), GetGValue(color),
                                    GetBValue(color)), stroke);
    pen.SetStartCap(Gdiplus::LineCapRound);
    pen.SetEndCap(Gdiplus::LineCapRound);
    pen.SetLineJoin(Gdiplus::LineJoinRound);

    const Gdiplus::REAL lidY = S * 0.30f;   // 桶盖横线 y
    // 桶盖横线。
    g.DrawLine(&pen, S * 0.16f, lidY, S * 0.84f, lidY);
    // 提手(盖上方的小方括号)。
    g.DrawLine(&pen, S * 0.40f, lidY, S * 0.40f, S * 0.18f);
    g.DrawLine(&pen, S * 0.40f, S * 0.18f, S * 0.60f, S * 0.18f);
    g.DrawLine(&pen, S * 0.60f, S * 0.18f, S * 0.60f, lidY);
    // 桶身梯形(上宽下窄):左边、底边、右边。
    const Gdiplus::REAL topL = S * 0.24f;
    const Gdiplus::REAL topR = S * 0.76f;
    const Gdiplus::REAL botL = S * 0.30f;
    const Gdiplus::REAL botR = S * 0.70f;
    const Gdiplus::REAL botY = S * 0.84f;
    g.DrawLine(&pen, topL, lidY, botL, botY);   // 左
    g.DrawLine(&pen, botL, botY, botR, botY);   // 底
    g.DrawLine(&pen, topR, lidY, botR, botY);   // 右
    // 两道竖纹。
    g.DrawLine(&pen, S * 0.42f, lidY + S * 0.10f, S * 0.42f, botY - S * 0.06f);
    g.DrawLine(&pen, S * 0.58f, lidY + S * 0.10f, S * 0.58f, botY - S * 0.06f);
    return hbm;
}

} // anonymous namespace

CBalloonGroupInfoDlg::CBalloonGroupInfoDlg()
    : m_lpFMGClient(NULL)
    , m_pHost(NULL)
    , m_hOwnerWnd(NULL)
    , m_cardEditing(false)
    , m_cardSavePending(false)
    , m_titleIconBmp(NULL)
    , m_hTaskIconBig(NULL)
    , m_hTaskIconSmall(NULL)
    , m_quitBg(NULL)
    , m_quitBgHover(NULL)
    , m_quitBgPressed(NULL)
    , m_iconHistory(NULL)
    , m_iconQuit(NULL)
    , m_pToast(NULL)
{
}

CBalloonGroupInfoDlg::~CBalloonGroupInfoDlg()
{
    if (m_titleIconBmp != NULL)
    {
        ::DeleteObject(m_titleIconBmp);
        m_titleIconBmp = NULL;
    }
    if (m_hTaskIconBig != NULL)
    {
        ::DestroyIcon(m_hTaskIconBig);
        m_hTaskIconBig = NULL;
    }
    if (m_hTaskIconSmall != NULL)
    {
        ::DestroyIcon(m_hTaskIconSmall);
        m_hTaskIconSmall = NULL;
    }
    HBITMAP* bitmaps[] = { &m_quitBg, &m_quitBgHover, &m_quitBgPressed,
                           &m_iconHistory, &m_iconQuit };
    for (size_t i = 0; i < sizeof(bitmaps) / sizeof(bitmaps[0]); ++i)
    {
        if (*(bitmaps[i]) != NULL)
        {
            ::DeleteObject(*(bitmaps[i]));
            *(bitmaps[i]) = NULL;
        }
    }
}

void CBalloonGroupInfoDlg::EnsureVisualResources()
{
    if (m_quitBg != NULL)
    {
        return;   // 幂等
    }
    // 退群按钮浅红圆角底:边长 = 2*radius + 4(中间留 4px stretch 区),9-grid 四角
    // 各 radius 切片保护圆角。三态渐深:normal 浅红、hover 略深、pressed 更深。
    const int side = kQuitBtnRadius * 2 + 4;
    m_quitBg        = MakeRoundRectBitmap(RGB(0xFC, 0xE9, 0xE9), side, kQuitBtnRadius);
    m_quitBgHover   = MakeRoundRectBitmap(RGB(0xF8, 0xDA, 0xDA), side, kQuitBtnRadius);
    m_quitBgPressed = MakeRoundRectBitmap(RGB(0xF2, 0xCB, 0xCB), side, kQuitBtnRadius);

    // 底部两个按钮的左侧线描图标(对照设计稿:查看消息记录=时钟、退出群聊=垃圾桶)。
    // 按显示尺寸 1:1 渲染(SetLeadingIconSize 同值),避免 AlphaBlend 缩放发糊。
    m_iconHistory = MakeClockIcon(kFooterIconSize, RGB(0x55, 0x55, 0x55));   // 中性灰
    m_iconQuit    = MakeTrashIcon(kFooterIconSize, RGB(0xE5, 0x39, 0x35));   // 危险红(同退群字色)
}

void CBalloonGroupInfoDlg::ApplyQuitButtonStyle()
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    balloonwjui::DuiButton* btnQuit = dynamic_cast<balloonwjui::DuiButton*>(
        content->FindCtrlById(kIdGroupBtnQuit));
    if (btnQuit != NULL)
    {
        // 红字(layout 已设,这里再确保一次);浅红 9-grid 底(资源就绪时);垃圾桶图标。
        btnQuit->SetTextColor(RGB(0xE5, 0x39, 0x35));
        if (m_quitBg != NULL)
        {
            btnQuit->SetBgBitmap(m_quitBg, m_quitBgHover, m_quitBgPressed, NULL);
            btnQuit->SetBgInsets(kQuitBtnRadius, kQuitBtnRadius, kQuitBtnRadius, kQuitBtnRadius);
        }
        if (m_iconQuit != NULL)
        {
            btnQuit->SetLeadingIcon(m_iconQuit);
            btnQuit->SetLeadingIconSize(kFooterIconSize);
        }
    }

    // "查看消息记录"按钮:左侧时钟图标。
    balloonwjui::DuiButton* btnHistory = dynamic_cast<balloonwjui::DuiButton*>(
        content->FindCtrlById(kIdGroupBtnHistory));
    if (btnHistory != NULL && m_iconHistory != NULL)
    {
        btnHistory->SetLeadingIcon(m_iconHistory);
        btnHistory->SetLeadingIconSize(kFooterIconSize);
    }
}

void CBalloonGroupInfoDlg::OpenForGroup(HWND hParent, CFlamingoClient* pFMGClient,
                                        CBalloonMainPanel* pHost, const GroupInfoViewData& seed)
{
    m_lpFMGClient = pFMGClient;
    m_pHost       = pHost;
    m_hOwnerWnd   = hParent;
    m_data        = seed;

    if (IsWindow())
    {
        // 已打开:还原前置并用 seed 重刷。
        if (IsIconic())
        {
            ShowWindow(SW_RESTORE);
        }
        ::SetForegroundWindow(m_hWnd);
        balloonwjui::DuiControl* content = GetClientContent();
        if (content != NULL)
        {
            ApplyGroupInfoDataToView(content, m_data);
        }
        return;
    }

    // owner 传 NULL:成为无 owner 顶层窗口,任务栏 / Alt-Tab 有独立条目(与个人信息
    // 对话框同模式);仍用 hParent 做居中参照。
    if (Create(NULL, CWindow::rcDefault, _T("群资料"),
               WS_OVERLAPPEDWINDOW, 0) == NULL)
    {
        return;
    }

    SetTitle(_T("群资料"));
    SetTitleBarHeight(kGroupInfoTitleBarH);
    //最小化 + 关闭。本窗口是无属主的独立顶层窗口（上面 Create 的第一个参数为 NULL、
    //也没有 WS_EX_TOOLWINDOW），最小化后在任务栏上有自己的按钮，点回来即可还原；
    //再次从主面板打开时上面的 IsIconic 分支也会把它还原出来。
    //不给最大化：窗口内容是固定版式的资料卡，拉大只会留一片空白。
    SetButtons(true, false, true);
    SetMinSize(kGroupInfoMinWidth, kGroupInfoMinHeight);

    // 标题栏图标(程序图标合成到标题栏底色),只建一次跨"关闭再打开"复用。
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

    // 任务栏 / Alt-Tab 图标(balloonui 窗口类未关联类图标,须显式 WM_SETICON)。
    if (m_hTaskIconBig == NULL)
    {
        m_hTaskIconBig = AtlLoadIconImage(IDR_MAINFRAME, LR_DEFAULTCOLOR,
                                          ::GetSystemMetrics(SM_CXICON),
                                          ::GetSystemMetrics(SM_CYICON));
    }
    if (m_hTaskIconSmall == NULL)
    {
        m_hTaskIconSmall = AtlLoadIconImage(IDR_MAINFRAME, LR_DEFAULTCOLOR,
                                            ::GetSystemMetrics(SM_CXSMICON),
                                            ::GetSystemMetrics(SM_CYSMICON));
    }
    if (m_hTaskIconBig != NULL)
    {
        ::SendMessage(m_hWnd, WM_SETICON, ICON_BIG, (LPARAM)m_hTaskIconBig);
    }
    if (m_hTaskIconSmall != NULL)
    {
        ::SendMessage(m_hWnd, WM_SETICON, ICON_SMALL, (LPARAM)m_hTaskIconSmall);
    }

    // 装客户区控件树。
    std::unique_ptr<balloonwjui::DuiControl> root = BuildGroupInfoBody();
    if (!root)
    {
        return;
    }
    SetClientContent(std::move(root));

    // 退群按钮危险红视觉(浅红 9-grid + 红字)。
    EnsureVisualResources();
    ApplyQuitButtonStyle();

    // 群名片输入框在查看态是隐藏的,首次进编辑态时由 SetGroupInfoCardEditMode
    // 显示并重排。这里只统一它的底色,与是否可见无关。
    balloonwjui::DuiEdit* cardEdit = dynamic_cast<balloonwjui::DuiEdit*>(
        GetClientContent()->FindCtrlById(kIdGroupCardEdit));
    if (cardEdit != NULL)
    {
        cardEdit->SetBgColor(RGB(0xF4, 0xF3, 0xF0));   // 与全 app 输入框统一柔灰底
    }

    // 轻提示条挂在客户区根容器末尾（基类按子控件顺序绘制，末尾即最上层），布局 Fixed(0)
    // 使它不占竖直方向的空间。水平居中由 DuiToast 自己计算，距顶偏移在每次弹出前按头部资料区的
    // 实际下边重新设定（见 OnDuiNotify 中复制群名 / 群号的分支），避免压住群号那一行
    m_pToast = NULL;
    balloonwjui::DuiVBox* rootBox = dynamic_cast<balloonwjui::DuiVBox*>(GetClientContent());
    if (rootBox != NULL)
    {
        std::unique_ptr<balloonwjui::DuiToast> toast(new balloonwjui::DuiToast());
        m_pToast = toast.get();
        rootBox->AddChild(std::move(toast), balloonwjui::DuiLayout::Hint().Fixed(0));
    }

    // 注入初始数据。
    ApplyGroupInfoDataToView(GetClientContent(), m_data);

    ResizeClient(kGroupInfoWidth, kGroupInfoHeight);
    CenterWindow(hParent);
    ShowWindow(SW_SHOW);
    UpdateWindow();
    ::SetForegroundWindow(m_hWnd);
}

void CBalloonGroupInfoDlg::ApplyData(const GroupInfoViewData& data)
{
    m_data = data;
    balloonwjui::DuiControl* content = GetClientContent();
    if (content != NULL)
    {
        ApplyGroupInfoDataToView(content, m_data);
        if (IsWindow())
        {
            ::InvalidateRect(m_hWnd, NULL, FALSE);
        }
    }
}

void CBalloonGroupInfoDlg::EnterCardEdit()
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    // edit 初值设为当前名片(空名片则空串,方便直接输入)。
    balloonwjui::DuiEdit* cardEdit = dynamic_cast<balloonwjui::DuiEdit*>(
        content->FindCtrlById(kIdGroupCardEdit));
    if (cardEdit != NULL)
    {
        cardEdit->SetText(m_data.myCard);
    }

    SetGroupInfoCardEditMode(content, true);
    m_cardEditing = true;

    // 把焦点交给名片输入框并全选,方便用户直接覆盖输入。输入框改成无窗口实现之后
    // 一次 SetFocus 就够了:它既把 DUI 焦点指到本控件,又让宿主窗口把 Win32 键盘焦点
    // 拿到手(无窗口控件没有自己的窗口,按键消息要先投递到宿主才能转下来)。
    if (cardEdit != NULL)
    {
        cardEdit->SetFocus();
        cardEdit->SelectAll();
    }
    if (IsWindow())
    {
        ::InvalidateRect(m_hWnd, NULL, FALSE);
    }
}

void CBalloonGroupInfoDlg::CommitCardEdit()
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    m_cardEditing = false;

    balloonwjui::DuiEdit* cardEdit = dynamic_cast<balloonwjui::DuiEdit*>(
        content->FindCtrlById(kIdGroupCardEdit));
    CString newCard = (cardEdit != NULL) ? cardEdit->GetText() : CString();

    // 切回查看态。
    SetGroupInfoCardEditMode(content, false);

    // 无变化:仅切回,不发协议。
    if (newCard == m_data.myCard)
    {
        if (IsWindow())
        {
            ::InvalidateRect(m_hWnd, NULL, FALSE);
        }
        return;
    }

    // 乐观更新:无论在线离线,先把改后的值显示出来(用户要"点别处先变回修改后的值")。
    // 存旧值快照,服务端拒绝时由 OnCardSaveResult 回退。
    m_cardSnapshot = m_data.myCard;
    m_data.myCard  = newCard;
    ApplyGroupInfoDataToView(content, m_data);   // 查看态立即显示新值

    // 发 cmd_set_member_card,标记未决;成败由应答决定(失败回退)。离线时发送会排队
    // 但发不出去,这里仍乐观显示,下次群成员(1304)刷新会以服务端为准纠正。
    m_cardSavePending = true;
    CIUSocket::GetInstance().SendSetMemberCard(m_data.groupid, CStrToUtf8(newCard));
    LOG_INFO(_T("提交群名片修改（乐观更新已生效，等待服务端应答），group=%lld。"),
             (long long)m_data.groupid);

    if (IsWindow())
    {
        ::InvalidateRect(m_hWnd, NULL, FALSE);
    }
}

void CBalloonGroupInfoDlg::OnCardSaveResult(int code, const CString& msg)
{
    if (!m_cardSavePending)
    {
        return;   // 没有未决的改名片(可能是别处的应答),忽略
    }
    m_cardSavePending = false;

    if (code == 0)
    {
        // 成功:乐观更新已生效,无须额外动作。
        LOG_INFO(_T("群名片修改成功。"));
        return;
    }

    // 失败:从快照回退我的群名片,刷新查看态,弹提示(窗口已关时只回退数据)。
    LOG_ERROR(_T("修改群名片失败，code=%d，msg=%s。"), code, static_cast<LPCTSTR>(msg));
    m_data.myCard = m_cardSnapshot;
    balloonwjui::DuiControl* content = GetClientContent();
    if (content != NULL)
    {
        ApplyGroupInfoDataToView(content, m_data);
    }
    if (IsWindow())
    {
        ::InvalidateRect(m_hWnd, NULL, FALSE);
        CString prompt = msg.IsEmpty()
            ? CString(_T("修改群昵称失败，请稍后重试。"))
            : (CString(_T("修改群昵称失败：")) + msg);
        ::MessageBox(m_hWnd, prompt, _T("FlamingoIM"), MB_OK | MB_ICONWARNING);
    }
}

void CBalloonGroupInfoDlg::ConfirmAndLeave()
{
    CString prompt;
    CString name = m_data.groupName.IsEmpty() ? CString(_T("本群")) : m_data.groupName;
    prompt.Format(_T("确定退出群聊「%s」吗？退出后将不再接收该群消息。"),
                  static_cast<LPCTSTR>(name));
    int ret = ::MessageBox(m_hWnd, prompt, _T("退出群聊"),
                           MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2);
    if (ret != IDYES)
    {
        return;
    }

    if (m_lpFMGClient != NULL && m_lpFMGClient->IsOffline())
    {
        ::MessageBox(m_hWnd, _T("您当前处于离线状态，无法退出群聊，请上线后重试。"),
                     _T("FlamingoIM"), MB_OK | MB_ICONINFORMATION);
        return;
    }

    // 有宿主：经主面板发退群请求 + 乐观清理群列表 / 关闭群聊窗;无宿主(调试入口)退化
    // 为直接发协议。随后关闭本对话框。
    if (m_pHost != NULL)
    {
        m_pHost->LeaveGroup(m_data.groupid);
    }
    else
    {
        CIUSocket::GetInstance().SendLeaveGroup(m_data.groupid);
    }
    LOG_INFO(_T("提交退出群聊请求，group=%lld。"), (long long)m_data.groupid);

    if (IsWindow())
    {
        DestroyWindow();
    }
}

LRESULT CBalloonGroupInfoDlg::OnDuiNotify(UINT /*uMsg*/, WPARAM /*wParam*/,
                                          LPARAM lParam, BOOL& bHandled)
{
    const balloonwjui::DuiNotify* pNotify =
        reinterpret_cast<const balloonwjui::DuiNotify*>(lParam);
    if (pNotify == NULL)
    {
        bHandled = FALSE;
        return 0;
    }

    balloonwjui::DuiControl* content = GetClientContent();

    if (pNotify->code == balloonwjui::DUIN_VALUECHANGED)
    {
        // 开关翻转:从控件读最新态(不依赖 extra 语义),乐观更新 m_data 并发协议。
        if (pNotify->ctrlId == kIdGroupMuteSwitch && content != NULL)
        {
            balloonwjui::DuiSwitch* sw = dynamic_cast<balloonwjui::DuiSwitch*>(
                content->FindCtrlById(kIdGroupMuteSwitch));
            bool on = (sw != NULL) && sw->IsChecked();
            m_data.mute = on;
            // 有宿主：经主面板更新会话缓存(让消息列表即时反映)并发协议;无宿主(调试入口)
            // 退化为直接发协议。
            if (m_pHost != NULL)
            {
                m_pHost->SetGroupConversation(m_data.groupid, /*hasMute*/true, on,
                                              /*hasPinned*/false, false);
            }
            else
            {
                CIUSocket::GetInstance().SendSetConversation(
                    2 /*群聊*/, m_data.groupid, /*hasPinned*/false, false, /*hasMute*/true, on);
            }
            LOG_INFO(_T("切换群消息免打扰，group=%lld，mute=%d。"),
                     (long long)m_data.groupid, (int)on);
            return 0;
        }
        if (pNotify->ctrlId == kIdGroupPinSwitch && content != NULL)
        {
            balloonwjui::DuiSwitch* sw = dynamic_cast<balloonwjui::DuiSwitch*>(
                content->FindCtrlById(kIdGroupPinSwitch));
            bool on = (sw != NULL) && sw->IsChecked();
            m_data.pinned = on;
            if (m_pHost != NULL)
            {
                m_pHost->SetGroupConversation(m_data.groupid, /*hasMute*/false, false,
                                              /*hasPinned*/true, on);
            }
            else
            {
                CIUSocket::GetInstance().SendSetConversation(
                    2 /*群聊*/, m_data.groupid, /*hasPinned*/true, on, /*hasMute*/false, false);
            }
            LOG_INFO(_T("切换群置顶，group=%lld，pinned=%d。"),
                     (long long)m_data.groupid, (int)on);
            return 0;
        }
    }

    if (pNotify->code == balloonwjui::DUIN_CLICK)
    {
        switch (pNotify->ctrlId)
        {
        case kIdGroupCardLabel:
        case kIdGroupCardView:
            // 群名片行整行点击 → 进就地编辑。
            EnterCardEdit();
            return 0;
        case kIdGroupBtnHistory:
        {
            // 查看消息记录:交给宿主主面板打开本群的消息记录窗口(与聊天窗、右键菜单走同一个
            // OpenHistory)。群资料窗口保持打开。调试入口没有宿主时只记日志。
            int ctype = 0;
            __int64 cid = 0;
            if (GroupInfoHistoryTarget(m_data, ctype, cid) && m_pHost != NULL)
            {
                m_pHost->OpenHistory(ctype, cid);
            }
            LOG_INFO("group info dialog: open history, group=%lld, host=%d.",
                     (long long)m_data.groupid, (int)(m_pHost != NULL));
            return 0;
        }
        case kIdGroupName:
        case kIdGroupMeta:
        {
            // 点群名复制完整群名,点群号那一行复制纯数字群号;复制后在头部资料区下方轻提示
            CString text;
            if (GroupInfoCopyText(m_data, pNotify->ctrlId, text))
            {
                const bool ok = flamingo::CopyTextToClipboard((LPCTSTR)text);
                if (ok && m_pToast != NULL)
                {
                    // 偏移每次现算：窗口尺寸与 DPI 都可能在打开之后变化
                    const int topOffset = GroupInfoToastTopOffset(GetClientContent());
                    if (topOffset >= 0)
                    {
                        m_pToast->SetTopOffset(topOffset);
                    }
                    m_pToast->Show(pNotify->ctrlId == kIdGroupName ? _T("已复制群名") : _T("已复制群号"));
                }
                LOG_INFO("group info dialog: copy %s, group=%lld, length %d, ok=%d.",
                         pNotify->ctrlId == kIdGroupName ? "name" : "number",
                         (long long)m_data.groupid, text.GetLength(), (int)ok);
            }
            return 0;
        }
        case kIdGroupBtnQuit:
            ConfirmAndLeave();
            return 0;
        default:
            break;
        }
    }

    if (pNotify->code == balloonwjui::DUIN_KILLFOCUS)
    {
        // 群名片编辑框失焦 → 去抖提交:不立即提交,延后一拍(PostMessage)。用户在编辑框内
        // 重新点击会让 EDIT 短暂失焦再得焦,延后后在 OnCardCommitDeferred 里看焦点是否
        // 回到 EDIT,回到则不提交、仍留编辑态;真正离开才提交。直接提交会"点一下就退回"。
        if (pNotify->ctrlId == kIdGroupCardEdit && m_cardEditing)
        {
            if (IsWindow())
            {
                ::PostMessage(m_hWnd, WM_GROUPINFO_CARD_COMMIT, 0, 0);
            }
            return 0;
        }
    }

    bHandled = FALSE;
    return 0;
}

LRESULT CBalloonGroupInfoDlg::OnLButtonDown(UINT /*uMsg*/, WPARAM /*wParam*/,
                                            LPARAM lParam, BOOL& bHandled)
{
    // 群名片编辑态下点在输入框之外 —— 把 DUI 焦点从输入框上摘掉,由此触发
    // DUIN_KILLFOCUS → 去抖提交,达成"点别处自动退出编辑并保存"。
    //
    // 这条路径换过一次触发方式。输入框还是内嵌真 Win32 EDIT 子窗口的时候,靠的是
    // 「把键盘焦点移到宿主窗口 → 子窗口失焦 → DUIN_KILLFOCUS」;那时点在子窗口上的
    // 鼠标消息由系统直送该子窗口、不经宿主,所以宿主收到本消息就等于"点在输入框
    // 之外",不必判落点。输入框改成无窗口实现之后这两条前提都不成立了:键盘焦点本来
    // 就在宿主窗口上,再调一次 ::SetFocus(m_hWnd) 不产生任何焦点变化(故那一句已删除);
    // 而点在输入框上的鼠标消息同样会到达宿主。于是改为在这里自己判落点,只有确实点
    // 在输入框之外时才摘焦点。
    if (m_cardEditing)
    {
        balloonwjui::DuiControl* content = GetClientContent();
        balloonwjui::DuiEdit* cardEdit = (content != NULL)
            ? dynamic_cast<balloonwjui::DuiEdit*>(content->FindCtrlById(kIdGroupCardEdit))
            : NULL;
        // 只在输入框当前确实持有 DUI 焦点时才动焦点:焦点已经不在它身上时,
        // DUIN_KILLFOCUS 早已发过、提交也已经投递过,这里再摘一次只会给别的控件
        // 发出一条无意义的失焦通知。
        if (cardEdit != NULL && cardEdit->IsFocused())
        {
            // 控件矩形与本消息的鼠标坐标同为宿主窗口客户区坐标,可直接比对。
            POINT pt;
            pt.x = GET_X_LPARAM(lParam);
            pt.y = GET_Y_LPARAM(lParam);
            RECT rcEdit = cardEdit->GetRect();
            if (!::PtInRect(&rcEdit, pt))
            {
                // 摘掉 DUI 焦点即走原有的那条提交链:输入框收到失焦 → 发出
                // DUIN_KILLFOCUS → 本窗口投递 WM_GROUPINFO_CARD_COMMIT → 去抖后提交。
                // 这里刻意不直接调 CommitCardEdit:提交会切回查看态并重排整棵树,而本次
                // 点击随后还要交给 DuiHost 做命中派发,就地提交会让它按提交后的新布局去
                // 命中控件,点到本不该点的东西上。
                SetDuiFocus(NULL);
            }
        }
    }
    bHandled = FALSE;   // 继续交给 DuiFrameWindow/DuiHost 正常处理这次点击
    return 0;
}

LRESULT CBalloonGroupInfoDlg::OnCardCommitDeferred(UINT /*uMsg*/, WPARAM /*wParam*/,
                                                   LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    if (!m_cardEditing)
    {
        return 0;   // 已不在编辑态(例如已被别处提交),无须处理
    }
    // 焦点又回到了群名片输入框(用户在输入框内重新点击导致的短暂失焦→得焦) → 仍在
    // 编辑,跳过提交。否则焦点真正离开了,执行提交。
    balloonwjui::DuiControl* content = GetClientContent();
    if (content != NULL)
    {
        balloonwjui::DuiEdit* cardEdit = dynamic_cast<balloonwjui::DuiEdit*>(
            content->FindCtrlById(kIdGroupCardEdit));
        if (cardEdit != NULL && cardEdit->IsFocused())
        {
            return 0;   // 焦点回到输入框,仍在编辑
        }
    }
    CommitCardEdit();
    return 0;
}

} // namespace balloonmain
