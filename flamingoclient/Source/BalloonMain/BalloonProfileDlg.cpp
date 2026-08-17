/**
 *  BalloonProfileDlg.cpp
 *
 *  「个人信息」对话框实现。
 *
 *  balloonwj@qq.com   2026-05-28
 */

#include "stdafx.h"

#include "BalloonProfileDlg.h"
#include "BalloonProfileLayout.h"
#include "../FlamingoClient.h"           // CFlamingoClient::m_UserMgr
#include "../UserMgr.h"                  // CUserMgr / CBuddyInfo
#include "../net/IUProtocolData.h"       // ONLINE_STATUS 枚举
#include "../resource.h"                 // IDR_MAINFRAME(标题栏 / 任务栏程序图标)
#include "../EncodeUtil.h"               // CString → UTF-8(发往服务端)
#include "../IULog.h"                    // LOG_INFO / LOG_ERROR
#include "../UserSessionData.h"          // FMG_MSG_REFRESH_SELF_HEADER(乐观保存后通知主面板刷 Header)
#include "../net/IUSocket.h"             // CIUSocket::SendUpdateProfile
#include "../net/ProfileProtocolClient.h" // net::UpdateProfileResponse
#include "../ClipboardText.h"            // 点击账号复制到剪贴板
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiEdit.h"   // 编辑框 GetText / SetBgColor
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiComboBox.h"   // 性别下拉 GetCurSel / SetBgColor

#include <vector>                        // CStrToUtf8 缓冲
#include <gdiplus.h>                     // 主按钮圆角橙底位图用 GDI+ 抗锯齿绘制
#pragma comment(lib, "gdiplus.lib")

namespace balloonmain {

// ---- 对话框尺寸(按设计稿 Modal width=420 严格对齐;高度收紧到内容贴合)----
// 设计稿 components.jsx Modal:width=420。原 540 高客户区里行块与底部按钮之间
// 留了约 100px 弹性空白(weight=1 spacer 撑开),观感偏松散;现把客户区高度收到
// 各固定子项之和略多一点(BalloonProfileLayout 固定项合计约 389),弹性空白只剩
// 几像素,底部按钮贴着行块,整体更紧凑。标题栏由 44 收到 36(更接近其它对话框)。
static const int kProfileWidth          = 420;
// 原 392（footer 按钮收到 30 高后整体再矮 4px）。信息行由 6 行增加到 8 行
// （新增只读的"部门""职位"），每行 kFieldRowHeight = 34，故 +68。
// 改动行数时要连带改这里，否则底部按钮会被挤出可视区。
static const int kProfileHeight         = 460;
static const int kProfileTitleBarHeight = 36;
static const int kProfileMinWidth       = 420;
static const int kProfileMinHeight      = 392;

// ---- 标题栏图标 ----
// 标题栏背景色(须与 DuiFrameWindow 标题条填充色一致)。标题图标用 SRCCOPY 绘制、
// 不做 alpha 混合,故把程序图标合成到此底色上,透明区才与标题栏融为一体。
static const COLORREF kTitleBarBgColor  = RGB(245, 246, 250);
// 标题栏图标边长(与 DuiFrameWindow 标题条绘制的 16x16 一致)。
static const int kTitleIconSize         = 16;

namespace {

// CString(UTF-16) → UTF-8 std::string。空串安全。与 BalloonChangePwdDlg /
// BalloonMainPanel 的同名 helper 同口径:发往服务端的资料字段一律走 UTF-8。
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

// GDI+ 懒初始化 token —— 沿用本项目其它 GDI+ helper(MainPanelAvatar /
// MainPanelMenuIcons)的统一模式:多次 GdiplusStartup 是安全的,本翻译单元保留
// 自己的 token,进程退出时自动回收,无须 GdiplusShutdown。
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

// 生成 side×side 的 32bpp 预乘 alpha 圆角矩形位图,内填 color、四角圆角(半径
// radius)、圆角外完全透明。用作主按钮(编辑资料 / 保存)的 9-grid 底图:配
// SetBgInsets(radius,...) 后,四角原样保留、边与中心拉伸,任意按钮尺寸都保持圆角。
// 走 GDI+ 抗锯齿(SmoothingModeAntiAlias),圆角边缘平滑无锯齿。
// 内存布局与 MainPanelAvatar::MakeMonogramAvatar 同口径:CreateDIBSection top-down
// 32bpp,Gdiplus::Bitmap 直接包住该像素 buffer、按 PARGB 写入,满足 AlphaBlend
// (AC_SRC_OVER + AC_SRC_ALPHA)对源位图预乘 alpha 的要求。返回 HBITMAP,所有权归
// 调用方,用完 DeleteObject;失败返 NULL。
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
    ::ZeroMemory(bits, (size_t)side * side * 4);   // 全透明(alpha=0,预乘下 rgb 也 0)

    const int stride = side * 4;
    Gdiplus::Bitmap bitmap(side, side, stride, PixelFormat32bppPARGB,
                           static_cast<BYTE*>(bits));
    Gdiplus::Graphics g(&bitmap);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);

    // 圆角矩形路径:四角各一段 90° 圆弧。整体内收 0.5px,让 AA 边缘落在位图内、
    // 不被四边裁掉。直径 d = radius*2。
    const Gdiplus::REAL inset = 0.5f;
    const Gdiplus::REAL d = (Gdiplus::REAL)(radius * 2);
    const Gdiplus::REAL x = inset;
    const Gdiplus::REAL y = inset;
    const Gdiplus::REAL w = (Gdiplus::REAL)side - inset * 2.0f;
    const Gdiplus::REAL h = (Gdiplus::REAL)side - inset * 2.0f;

    Gdiplus::GraphicsPath path;
    if (radius > 0)
    {
        path.AddArc(x,         y,         d, d, 180.0f, 90.0f);   // 左上
        path.AddArc(x + w - d, y,         d, d, 270.0f, 90.0f);   // 右上
        path.AddArc(x + w - d, y + h - d, d, d,   0.0f, 90.0f);   // 右下
        path.AddArc(x,         y + h - d, d, d,  90.0f, 90.0f);   // 左下
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
// DuiFrameWindow 标题栏用 SRCCOPY(不混合 alpha)绘图标,故须自带与标题栏一致的
// 底色:DrawIconEx(DI_NORMAL) 把图标按掩码 / alpha 合成到这层底色上,图标透明区
// 即呈现底色,与标题栏无缝。返回的 HBITMAP 所有权归调用方,用完 DeleteObject。
// 与 BalloonChangePwdDlg / BalloonClosePromptDlg 的同名 helper 同口径。
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

    // 把图标按掩码 + alpha 合成到已填好的底色上。
    ::DrawIconEx(mem, 0, 0, hIcon, sizePx, sizePx, 0, NULL, DI_NORMAL);

    ::SelectObject(mem, old);
    ::DeleteDC(mem);
    return hbmp;
}

} // anonymous namespace

CBalloonProfileDlg::CBalloonProfileDlg()
    : m_lpFMGClient(NULL)
    , m_hOwnerWnd(NULL)
    , m_editMode(false)
    , m_titleIconBmp(NULL)
    , m_hTaskIconBig(NULL)
    , m_hTaskIconSmall(NULL)
    , m_savePending(false)
    , m_snapGender(0)
    , m_snapBirthday(0)
{
    // m_resources 默认构造全 NULL,Lazy 由 EnsureVisualResources 填。
    // m_snapXxx 字符串默认空,首次保存前会被实际旧值覆盖。
}

CBalloonProfileDlg::~CBalloonProfileDlg()
{
    // 标题栏图标位图(SetIcon 仅借用,所有权在本类),析构释放。
    if (m_titleIconBmp != NULL)
    {
        ::DeleteObject(m_titleIconBmp);
        m_titleIconBmp = NULL;
    }

    // 任务栏 / Alt-Tab 图标(WM_SETICON 仅借用,所有权在本类),析构销毁。
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

    // 释放位图(默认构造全 NULL,DeleteObject NULL 安全)。
    HBITMAP* bitmaps[] = {
        &m_resources.hPrimaryBg,
        &m_resources.hPrimaryBgHover,
        &m_resources.hPrimaryBgPressed,
    };
    for (size_t i = 0; i < sizeof(bitmaps) / sizeof(bitmaps[0]); ++i)
    {
        if (*(bitmaps[i]) != NULL)
        {
            ::DeleteObject(*(bitmaps[i]));
            *(bitmaps[i]) = NULL;
        }
    }
}

void CBalloonProfileDlg::EnsureVisualResources()
{
    if (m_resources.hPrimaryBg != NULL)
    {
        return;   // 幂等
    }
    // 主按钮圆角橙底:边长 = 2*radius + 4(中间留 4px 可 stretch 区),9-grid
    // 四角各 radius 切片保护圆角。三态渐深:normal --accent #FF7A45、hover 略深、
    // pressed 更深,给按钮交互反馈。
    const int side = kProfilePrimaryBtnRadius * 2 + 4;
    m_resources.hPrimaryBg        = MakeRoundRectBitmap(RGB(0xFF, 0x7A, 0x45),
                                                        side, kProfilePrimaryBtnRadius);
    m_resources.hPrimaryBgHover   = MakeRoundRectBitmap(RGB(0xF0, 0x6A, 0x38),
                                                        side, kProfilePrimaryBtnRadius);
    m_resources.hPrimaryBgPressed = MakeRoundRectBitmap(RGB(0xD9, 0x5A, 0x2E),
                                                        side, kProfilePrimaryBtnRadius);
}

void CBalloonProfileDlg::OpenForCurrentUser(HWND hParent, CFlamingoClient* pFMGClient)
{
    if (pFMGClient == NULL)
    {
        return;
    }
    m_lpFMGClient = pFMGClient;
    m_hOwnerWnd   = hParent;   // 主面板 HWND,乐观保存后 PostMessage 它即时刷新 Header

    if (IsWindow())
    {
        // 已打开:从最小化还原并前置。
        if (IsIconic())
        {
            ShowWindow(SW_RESTORE);
        }
        ::SetForegroundWindow(m_hWnd);
        // 已经打开的情况下重新刷一次数据,防止 m_UserInfo 已被外部更新
        // (例如未来 status 变更 / 协议推送过来的新字段)。
        RefreshFromUserInfo();
        // 昵称可能已被改过,标题栏文字一并刷新。
        UpdateTitleFromUserInfo();
        return;
    }

    // owner 传 NULL —— 让本窗口成为无 owner 的顶层窗口,从而在任务栏 / Alt-Tab
    // 里有独立条目(与聊天窗 BalloonChatWindow、改密窗 BalloonChangePwdDlg 同模式;
    // owned 窗口默认不进任务栏,这正是之前个人信息窗在任务栏看不到的原因)。
    // 仍用 hParent 做居中参照(见下方 CenterWindow(hParent))。
    if (Create(NULL, CWindow::rcDefault, _T("个人信息"),
               WS_OVERLAPPEDWINDOW, 0) == NULL)
    {
        return;
    }

    UpdateTitleFromUserInfo();         // 标题栏文字 = "用户名 - 个人信息"
    SetTitleBarHeight(kProfileTitleBarHeight);
    //最小化 + 关闭。本窗口是无属主的独立顶层窗口（上面 Create 的第一个参数为 NULL、
    //也没有 WS_EX_TOOLWINDOW），最小化后在任务栏上有自己的按钮，点回来即可还原；
    //再次从主面板头像打开时上面的 IsIconic 分支也会把它还原出来。
    //不给最大化：窗口内容是固定版式的资料卡，拉大只会留一片空白。
    SetButtons(true, false, true);
    SetMinSize(kProfileMinWidth, kProfileMinHeight);

    // 标题栏图标:程序图标 IDR_MAINFRAME 合成到标题栏底色上,只建一次,跨
    // "关闭再打开"复用;DuiFrameWindow::SetIcon 仅借用,本类析构时释放。
    if (m_titleIconBmp == NULL)
    {
        HICON hIcon = AtlLoadIconImage(IDR_MAINFRAME, LR_DEFAULTCOLOR,
                                       kTitleIconSize, kTitleIconSize);
        if (hIcon != NULL)
        {
            m_titleIconBmp = MakeTitleIconBitmap(hIcon, kTitleIconSize,
                                                 kTitleBarBgColor);
            ::DestroyIcon(hIcon);
        }
    }
    if (m_titleIconBmp != NULL)
    {
        SetIcon(m_titleIconBmp);
    }

    // 任务栏 / Alt-Tab 图标:直接用程序图标 IDR_MAINFRAME(大图标给 ICON_BIG 用于
    // Alt-Tab,小图标给 ICON_SMALL 用于任务栏 / 标题栏左上)。balloonui 窗口类注册
    // 时未关联类图标,须显式 WM_SETICON,否则任务栏条目是默认空白图标。只建一次。
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

    // 装客户区控件树。BuildProfileBody 已经把初始可见性置成"查看态"
    // (Edit 系列 / Cancel / Save 隐藏)。
    std::unique_ptr<balloonwjui::DuiControl> root = BuildProfileBody();
    if (!root)
    {
        return;
    }
    SetClientContent(std::move(root));

    // 视觉精修位图 lazy-init,只在首次 Open 时建。
    EnsureVisualResources();
    ApplyProfileButtonStyles(GetClientContent(), m_resources);

    // 5 个 DuiEdit 输入框此刻是隐藏态,进入编辑态时由 SetProfileEditMode
    // 的容器重排显示并定位。这里只统一它们的底色。
    const UINT editIds[] = {
        kIdNicknameEdit, kIdSignatureEdit, kIdBirthdayEdit,
        kIdLocationEdit, kIdEmailEdit
    };
    // 输入框柔灰底(全 app 输入框统一色 #F4F3F0,见 BalloonChangePwdDlg /
    // BalloonModifyAvatarDlg),比默认白底 + 灰边更顺眼;边框默认保留(聚焦变色反馈)。
    const COLORREF kEditBgColor = RGB(0xF4, 0xF3, 0xF0);
    for (size_t i = 0; i < sizeof(editIds) / sizeof(editIds[0]); ++i)
    {
        balloonwjui::DuiEdit* edt = dynamic_cast<balloonwjui::DuiEdit*>(
            GetClientContent()->FindCtrlById(editIds[i]));
        if (edt != NULL)
        {
            edt->SetBgColor(kEditBgColor);
        }
    }
    // 性别下拉同色,保持与输入框一致观感。
    balloonwjui::DuiComboBox* genderBox = dynamic_cast<balloonwjui::DuiComboBox*>(
        GetClientContent()->FindCtrlById(kIdGenderEdit));
    if (genderBox != NULL)
    {
        genderBox->SetBgColor(kEditBgColor);
    }

    m_editMode = false;
    RefreshFromUserInfo();

    ResizeClient(kProfileWidth, kProfileHeight);
    CenterWindow(hParent);            // 无 owner 后仍以主面板为参照居中
    ShowWindow(SW_SHOW);
    UpdateWindow();
    ::SetForegroundWindow(m_hWnd);
}

void CBalloonProfileDlg::RefreshFromUserInfo()
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL || m_lpFMGClient == NULL)
    {
        return;
    }
    const CBuddyInfo& me = m_lpFMGClient->m_UserMgr.m_UserInfo;

    ProfileDataInput info;
    info.userid    = me.m_uUserID;
    info.account   = CString(me.m_strAccount.c_str());
    info.nickname  = CString(me.m_strNickName.c_str());
    info.signature = CString(me.m_strSign.c_str());
    info.gender    = (int)me.m_nGender;
    info.birthday  = me.m_nBirthday;
    info.address   = CString(me.m_strAddress.c_str());
    info.email     = CString(me.m_strEmail.c_str());
    // 部门 / 职位不在 m_UserInfo 里，由主面板经 SetOrgInfo 灌进来（见其注释）。
    info.dept      = m_dept;
    info.position  = m_position;

    ApplyProfileDataToView(content, info);
}

void CBalloonProfileDlg::SetOrgInfo(const CString& dept, const CString& position)
{
    m_dept     = dept;
    m_position = position;

    // 窗口已经开着时立刻反映出来（例如通讯录刷新后主面板重新灌了一次）；
    // 还没创建时只存下来，等 OpenForCurrentUser 装填 UI 时自然用上。
    if (IsWindow())
    {
        RefreshFromUserInfo();
    }
}

void CBalloonProfileDlg::UpdateTitleFromUserInfo()
{
    if (m_lpFMGClient == NULL)
    {
        return;
    }
    const CBuddyInfo& me = m_lpFMGClient->m_UserMgr.m_UserInfo;

    // 用户名优先取昵称,昵称为空时退回账号(与顶部 Banner / RefreshFromUserInfo
    // 同口径)。
    CString nick(me.m_strNickName.c_str());
    CString account(me.m_strAccount.c_str());
    CString userName = nick.IsEmpty() ? account : nick;

    // 标题栏文字 = "用户名 - 个人信息"。
    CString title;
    title.Format(_T("%s - 个人信息"), static_cast<LPCTSTR>(userName));
    SetTitle(title);
}

void CBalloonProfileDlg::SwitchEditMode(bool edit)
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    m_editMode = edit;

    // 可见性翻转 + 受影响容器重排都交给 SetProfileEditMode(纯 DUI、可单测)。
    // 它内部对每行 hbox / footer hbox 直接 ForceLayout,绕开 SetRect 矩形相等提前
    // 返回的坑,保证刚可见的编辑框 / Cancel/Save 拿到正确矩形。
    SetProfileEditMode(content, edit);

    if (IsWindow())
    {
        ::InvalidateRect(m_hWnd, NULL, FALSE);
    }
}

void CBalloonProfileDlg::DoSaveProfile()
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL || m_lpFMGClient == NULL)
    {
        return;
    }

    // 离线禁止提交(与主面板签名编辑一致):弹提示、留在编辑态。
    if (m_lpFMGClient->IsOffline())
    {
        ::MessageBox(m_hWnd, _T("您当前处于离线状态，无法保存资料，请上线后重试。"),
                     _T("FlamingoIM"), MB_OK | MB_ICONINFORMATION);
        return;
    }

    // 读 6 个编辑控件当前值。
    balloonwjui::DuiEdit* nickEdit = dynamic_cast<balloonwjui::DuiEdit*>(
        content->FindCtrlById(kIdNicknameEdit));
    balloonwjui::DuiEdit* signEdit = dynamic_cast<balloonwjui::DuiEdit*>(
        content->FindCtrlById(kIdSignatureEdit));
    balloonwjui::DuiEdit* bdayEdit = dynamic_cast<balloonwjui::DuiEdit*>(
        content->FindCtrlById(kIdBirthdayEdit));
    balloonwjui::DuiEdit* addrEdit = dynamic_cast<balloonwjui::DuiEdit*>(
        content->FindCtrlById(kIdLocationEdit));
    balloonwjui::DuiEdit* mailEdit = dynamic_cast<balloonwjui::DuiEdit*>(
        content->FindCtrlById(kIdEmailEdit));
    balloonwjui::DuiComboBox* genderBox = dynamic_cast<balloonwjui::DuiComboBox*>(
        content->FindCtrlById(kIdGenderEdit));

    CString nickname  = (nickEdit != NULL) ? nickEdit->GetText() : CString();
    CString signature = (signEdit != NULL) ? signEdit->GetText() : CString();
    CString bdayText  = (bdayEdit != NULL) ? bdayEdit->GetText() : CString();
    CString address   = (addrEdit != NULL) ? addrEdit->GetText() : CString();
    CString email     = (mailEdit != NULL) ? mailEdit->GetText() : CString();
    // GetCurSel:0 男 / 1 女(ApplyProfileDataToView 按此顺序 AddString)。
    int gender = (genderBox != NULL && genderBox->GetCurSel() == 1) ? 1 : 0;

    // 生日:解析 "YYYY-MM-DD"。-1=格式非法,弹提示、聚焦该框、留在编辑态。
    long birthday = ParseProfileBirthday(bdayText);
    if (birthday < 0)
    {
        ::MessageBox(m_hWnd, _T("生日格式应为 YYYY-MM-DD，请检查后重试。"),
                     _T("FlamingoIM"), MB_OK | MB_ICONINFORMATION);
        if (bdayEdit != NULL)
        {
            bdayEdit->SetFocus();
        }
        return;
    }

    CBuddyInfo& me = m_lpFMGClient->m_UserMgr.m_UserInfo;

    // 存旧值快照(服务端拒绝时回退用)。
    m_snapNickname  = CString(me.m_strNickName.c_str());
    m_snapSignature = CString(me.m_strSign.c_str());
    m_snapGender    = (int)me.m_nGender;
    m_snapBirthday  = me.m_nBirthday;
    m_snapAddress   = CString(me.m_strAddress.c_str());
    m_snapEmail     = CString(me.m_strEmail.c_str());

    // 乐观更新:写入本地用户信息(m_strXxx 为宽字符 tstring,直接用 CString 缓冲)。
    me.m_strNickName = nickname.GetString();
    me.m_strSign     = signature.GetString();
    me.m_nGender     = gender;
    me.m_nBirthday   = birthday;
    me.m_strAddress  = address.GetString();
    me.m_strEmail    = email.GetString();

    // 查看态文字立即反映新值,并切回查看态。
    RefreshFromUserInfo();
    SwitchEditMode(false);

    // 通知主面板按已更新的 m_UserInfo 立即刷新本人 Header(昵称/签名/头像),不必等
    // 服务端应答 —— 与主面板签名原地编辑"即时刷新"的体验一致。失败时由 OnSaveResult
    // 回退 m_UserInfo,应答路径会再刷一次还原。
    if (m_hOwnerWnd != NULL && ::IsWindow(m_hOwnerWnd))
    {
        ::PostMessage(m_hOwnerWnd, FMG_MSG_REFRESH_SELF_HEADER, 0, 0);
    }

    // 编码 UTF-8 发 cmd_update_profile;应答经主面板 OnUpdateProfileResult →
    // 本类 OnSaveResult 回投(失败回退)。
    m_savePending = true;
    CIUSocket::GetInstance().SendUpdateProfile(
        CStrToUtf8(nickname), CStrToUtf8(signature), gender, birthday,
        CStrToUtf8(address), CStrToUtf8(email));
    LOG_INFO(_T("提交个人资料更新（乐观更新已生效，等待服务端应答）。"));
}

void CBalloonProfileDlg::OnSaveResult(const net::UpdateProfileResponse& resp)
{
    if (!m_savePending)
    {
        return;   // 没有未决保存(可能是签名编辑路径的应答),忽略
    }
    m_savePending = false;

    if (resp.code == 0)
    {
        // 成功:乐观更新已生效,无需额外动作。
        LOG_INFO(_T("个人资料保存成功。"));
        return;
    }

    // 失败:从快照回退本地用户信息,刷新查看态文字,弹提示(窗口已关则只回退数据)。
    LOG_ERROR("save profile failed, code=%d, msg=%s.", resp.code, resp.msg.c_str());
    if (m_lpFMGClient != NULL)
    {
        CBuddyInfo& me = m_lpFMGClient->m_UserMgr.m_UserInfo;
        me.m_strNickName = m_snapNickname.GetString();
        me.m_strSign     = m_snapSignature.GetString();
        me.m_nGender     = m_snapGender;
        me.m_nBirthday   = m_snapBirthday;
        me.m_strAddress  = m_snapAddress.GetString();
        me.m_strEmail    = m_snapEmail.GetString();
    }
    if (IsWindow())
    {
        RefreshFromUserInfo();
        ::MessageBox(m_hWnd, _T("保存资料失败，请稍后重试。"),
                     _T("FlamingoIM"), MB_OK | MB_ICONWARNING);
    }
}

LRESULT CBalloonProfileDlg::OnDuiNotify(UINT /*uMsg*/, WPARAM /*wParam*/,
                                        LPARAM lParam, BOOL& bHandled)
{
    const balloonwjui::DuiNotify* pNotify =
        reinterpret_cast<const balloonwjui::DuiNotify*>(lParam);

    if (pNotify != NULL && pNotify->code == balloonwjui::DUIN_CLICK)
    {
        switch (pNotify->ctrlId)
        {
        case kIdBtnClose:
            // 关闭对话框 —— DuiFrameWindow 自带标题栏 ✕ 同样走这条路径。
            if (IsWindow())
            {
                DestroyWindow();
            }
            return 0;
        case kIdBtnEdit:
            // 切到编辑态。
            SwitchEditMode(true);
            return 0;
        case kIdBtnCancel:
            // 放弃编辑:重新从 m_UserInfo 加载值覆盖编辑态控件,再切回
            // 查看态。这样编辑态留下的临时输入不会污染下次打开编辑。
            RefreshFromUserInfo();
            SwitchEditMode(false);
            return 0;
        case kIdBtnSave:
            // 读编辑值、校验生日格式、乐观写入 m_UserInfo 并发 cmd_update_profile。
            // 切回查看态在 DoSaveProfile 内部完成(校验不过则留在编辑态)。
            DoSaveProfile();
            return 0;
        case kIdProfileAccount:
            // 点击账号(ModeLink)→ 把登录账号名复制到剪贴板。复制裸账号(不带"账号 "
            // 前缀),与登录账号一致,方便粘贴。
            if (m_lpFMGClient != NULL)
            {
                flamingo::CopyTextToClipboard(
                    m_lpFMGClient->m_UserMgr.m_UserInfo.m_strAccount.c_str());
            }
            return 0;
        default:
            break;
        }
    }

    bHandled = FALSE;
    return 0;
}

} // namespace balloonmain
