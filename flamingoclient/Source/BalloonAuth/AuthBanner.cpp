/**
 *  AuthBanner.cpp
 *
 *  登录 / 注册窗顶部品牌 Banner + 头像区的绘制实现。
 *
 *  绘制分两层：形状（渐变 / 火烈鸟 / 头像圆角 / 状态胶囊）用 GDI+（带
 *  抗锯齿）；文字（标题 / 标语 / 头像字母 / 用户名 / "在线"）用 GDI 的
 *  DrawText —— GDI 文字经系统 ClearType 光栅化，在内存 DC 上依然清晰
 *  锐利，避免 GDI+ DrawString 发虚发糊。两层不交叉：先在作用域内用
 *  GDI+ 画完所有形状（Graphics 析构时 flush），再用 GDI 画文字。
 *
 *  本 .cpp 故意<u>不</u> include Flamingo 的 stdafx.h —— BalloonLoginLayout
 *  的自定义工厂会构造本控件，而 BalloonLoginLayout 被 Flamingo.vcxproj
 *  与 FlamingoUnit.vcxproj 两个工程共用，故本文件也随之共用，自带 ATL/WTL
 *  前置依赖。在 Flamingo.vcxproj 里本文件需标记 PrecompiledHeader=NotUsing。
 *
 *  火烈鸟路径取自 design 的 auth.jsx：viewBox 0 0 200 200，由一条
 *  "M + 8 段三次贝塞尔 + 2 段直线" 的开放路径白色描边而成。
 *
 *  balloonwj@qq.com   2026-05-21
 */

// ---- balloonui 头文件要求的 ATL/WTL 前置依赖（等价 NewChatDemo/stdafx.h）----
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#define WINVER          0x0601      // 目标系统下限 = Windows 7
#define _WIN32_WINNT    0x0601
#define _WIN32_IE       0x0601
#define _RICHEDIT_VER   0x0300      // RichEdit 3.0，WTL 10 要求的下限（窗口类仍为 RichEdit20W）
#define _WTL_NO_CSTRING
#define _WTL_NO_WTYPES

#include <atlbase.h>
#include <atlstr.h>
#include <atltypes.h>
#include <atlapp.h>

extern CAppModule _Module;

#include <atlwin.h>
#include <atlcrack.h>
#include <atlmisc.h>

#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")

#include "AuthBanner.h"
#include "../BalloonMain/MainPanelStatusMenu.h"   // GetPresenceVisual（状态胶囊文字，与主面板同源）
#include "../BalloonMain/PresenceBadge.h"         // DrawPresenceBadge（状态胶囊上的状态徽标）
#include "../net/Protocol.h"                       // presence_type（状态默认值 presence_online）

namespace balloonauth {

namespace {

// ---- 配色（取自 design 的 :root / login-screen.png）---------------------

// Banner 渐变起点色 #FFB793。
const BYTE kGradTopR = 0xFF, kGradTopG = 0xB7, kGradTopB = 0x93;
// Banner 渐变终点色 #FF7A45（= 主题色 --accent）。
const BYTE kGradBotR = 0xFF, kGradBotG = 0x7A, kGradBotB = 0x45;

// 火烈鸟轮廓 / 标语的白色不透明度 —— design 里约 0.85。
const BYTE kSoftWhiteAlpha = 217;   // round(0.85 * 255)

// 头像底色（主题橙 #FF7A45）。
const BYTE kAvatarFillR = 0xFF, kAvatarFillG = 0x7A, kAvatarFillB = 0x45;
// 用户名文字色（design --text #1A1A1A）。
const COLORREF kNameColor = RGB(0x1A, 0x1A, 0x1A);
// 标题白色 / 标语半透明白（GDI 不支持半透明文字，标语用浅一点的白灰）。
const COLORREF kTitleColor = RGB(255, 255, 255);
const COLORREF kTagColor   = RGB(255, 0xEC, 0xDF);   // 暖白，贴合橙色 Banner
// 头像字母色（白）。
const COLORREF kInitialColor = RGB(255, 255, 255);
// "在线"状态点的绿色 #22C55E。
const BYTE kStatusDotR = 0x22, kStatusDotG = 0xC5, kStatusDotB = 0x5E;
// 状态胶囊文字色（design --text-muted #6B6B6B）。
const COLORREF kPillTextColor = RGB(0x6B, 0x6B, 0x6B);

// ---- Banner 控件矩形内的分区比例（基准为 240 高的 Banner 控件矩形）----

const float kGradientHeightRatio = 160.0f / 240.0f;   // 顶部渐变区高度占比
const float kRingSideRatio       = 72.0f / 240.0f;    // 头像白环边长占比
const float kRingTopRatio        = 100.0f / 240.0f;   // 头像环上沿距顶占比
const float kAvatarPadRatio      = 3.0f / 240.0f;     // 环与头像之间留白占比
const float kNameTopRatio        = 180.0f / 240.0f;   // 用户名文字区上沿距顶占比
const float kNameFontFactor      = 0.062f;            // 用户名字号占控件高占比
const float kPillTopRatio        = 208.0f / 240.0f;   // 状态胶囊上沿距顶占比
const float kPillHeightRatio     = 22.0f / 240.0f;    // 状态胶囊高度占比
const float kPillWidthRatio      = 62.0f / 240.0f;    // 状态胶囊宽度占比
// 状态徽标绘制方框的边长，占胶囊高度的比例。徽标圆底只占方框的 20/24，取 0.41 时
// 设计稿尺寸（胶囊高 22）下方框约 9px、圆底约 7.5px，与主面板 Header 的胶囊一致。
// 改版前这里是半径 pillH * 0.15 的纯色圆点（直径约 6.6px），字形在那个尺寸下糊成一团。
const float kBadgeBoxRatio       = 0.41f;

// 圆角矩形圆角半径相对边长的比例（design 头像 borderRadius 22%）。
const float kRoundRectRadiusRatio = 0.22f;

// ---- 火烈鸟 ----

const float kFlamingoViewBox        = 200.0f;   // SVG viewBox 边长
const float kFlamingoStrokeInViewBox = 3.0f;    // 描边宽度（viewBox 内）
const float kFlamingoSizeFactor     = 1.15f;    // 方形绘制区 / 渐变区高度
const float kTitleFontFactor        = 0.135f;   // 标题字号 / 渐变区高度
const float kTagFontFactor          = 0.072f;   // 标语字号 / 渐变区高度
const float kInitialFontFactor      = 0.46f;    // 头像字母字号 / 头像边长

// 用四段 90° 圆弧 + 闭合，向 path 添加一个圆角矩形。
void AddRoundRectPath(Gdiplus::GraphicsPath& path,
                      Gdiplus::REAL x, Gdiplus::REAL y,
                      Gdiplus::REAL w, Gdiplus::REAL h,
                      Gdiplus::REAL radius)
{
    Gdiplus::REAL r = radius;
    if (r > w / 2.0f) { r = w / 2.0f; }
    if (r > h / 2.0f) { r = h / 2.0f; }
    const Gdiplus::REAL d = r * 2.0f;

    path.AddArc(x,         y,         d, d, 180.0f, 90.0f);
    path.AddArc(x + w - d, y,         d, d, 270.0f, 90.0f);
    path.AddArc(x + w - d, y + h - d, d, d, 0.0f,   90.0f);
    path.AddArc(x,         y + h - d, d, d, 90.0f,  90.0f);
    path.CloseFigure();
}

// 用 GDI（而非 GDI+）绘制一段文字。GDI 的 DrawText 经系统 ClearType
// 光栅化，在内存 DC 上依然清晰；GDI+ 的 DrawString 相比之下发虚。
//   hdc：       目标 DC。
//   text：      文字（宽字符）。
//   rc：        绘制矩形。
//   pixelHeight：字号（像素）。
//   bold：      true = 半粗（SemiBold），false = 常规。
//   color：     文字颜色。
//   dtFlags：   DrawText 对齐 / 裁剪标志。
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
    HFONT    hOldFont = (HFONT)::SelectObject(hdc, hFont);
    int      oldBkMode = ::SetBkMode(hdc, TRANSPARENT);
    COLORREF oldColor  = ::SetTextColor(hdc, color);

    RECT r = rc;
    ::DrawTextW(hdc, text, -1, &r, dtFlags);

    ::SetTextColor(hdc, oldColor);
    ::SetBkMode(hdc, oldBkMode);
    ::SelectObject(hdc, hOldFont);
    ::DeleteObject(hFont);
}

} // anonymous namespace

AuthBanner::AuthBanner()
    : m_avatarColor(RGB(kAvatarFillR, kAvatarFillG, kAvatarFillB))   // 默认主题橙
    , m_presence(presence_online)   // 默认在线（胶囊显示绿点"在线"）
{
    ::SetRectEmpty(&m_rcPillHit);
}

void AuthBanner::SetUserName(LPCTSTR name)
{
    m_userName = (name != NULL) ? name : _T("");
    Invalidate();
}

void AuthBanner::SetPresence(int presence)
{
    m_presence = presence;   // 调用方负责随后 Invalidate 重绘
}

bool AuthBanner::OnLButtonUp(POINT pt, UINT /*mkFlags*/)
{
    // 命中状态胶囊：发通知让登录窗弹出在线状态菜单。其余区域不触发。
    if (::PtInRect(&m_rcPillHit, pt))
    {
        NotifyParent(DUIN_BANNER_STATUS_CLICK);
        return true;
    }
    return false;
}

void AuthBanner::SetAvatarColor(COLORREF color)
{
    if (m_avatarColor == color)
    {
        return;   // 颜色未变 —— 不触发多余重绘
    }
    m_avatarColor = color;
    Invalidate();
}

void AuthBanner::OnPaint(HDC hdc, const RECT& /*rcDirty*/)
{
    const int left   = m_rcItem.left;
    const int top    = m_rcItem.top;
    const int rectW  = m_rcItem.right - m_rcItem.left;
    const int rectH  = m_rcItem.bottom - m_rcItem.top;
    if (rectW <= 0 || rectH <= 0)
    {
        return;
    }

    // ---- 几何量（形状层与文字层共用）----
    const float gradH = (float)rectH * kGradientHeightRatio;

    const float ringSide = (float)rectH * kRingSideRatio;
    const float centerX  = (float)left + (float)rectW / 2.0f;
    const float ringX    = centerX - ringSide / 2.0f;
    const float ringY    = (float)top + (float)rectH * kRingTopRatio;
    const float pad      = (float)rectH * kAvatarPadRatio;
    const float avX      = ringX + pad;
    const float avY      = ringY + pad;
    const float avSide   = ringSide - pad * 2.0f;

    const float pillW = (float)rectH * kPillWidthRatio;
    const float pillH = (float)rectH * kPillHeightRatio;
    const float pillX = centerX - pillW / 2.0f;
    const float pillY = (float)top + (float)rectH * kPillTopRatio;
    const float badgeBox = pillH * kBadgeBoxRatio;      // 状态徽标绘制方框边长
    const float dotCx    = pillX + pillH * 0.46f;       // 徽标中心 X（胶囊左内缘处）
    const float dotCy    = pillY + pillH / 2.0f;        // 徽标中心 Y（胶囊垂直居中）

    // ====================================================================
    // 第一层：GDI+ 形状（作用域结束时 Graphics 析构并 flush）
    // ====================================================================
    {
        Gdiplus::Graphics g(hdc);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

        // 1. 135° 线性渐变背景（仅顶部渐变区）。
        Gdiplus::RectF gradRc((Gdiplus::REAL)left, (Gdiplus::REAL)top,
                              (Gdiplus::REAL)rectW, gradH);
        Gdiplus::LinearGradientBrush gradient(
            gradRc,
            Gdiplus::Color(255, kGradTopR, kGradTopG, kGradTopB),
            Gdiplus::Color(255, kGradBotR, kGradBotG, kGradBotB),
            135.0f);
        g.FillRectangle(&gradient, gradRc);

        // 2. 火烈鸟轮廓。
        const Gdiplus::REAL flamSide  = gradH * kFlamingoSizeFactor;
        const Gdiplus::REAL flamX     = (Gdiplus::REAL)m_rcItem.right - flamSide
                                      + gradH * 0.12f;
        const Gdiplus::REAL flamY     = (Gdiplus::REAL)top - gradH * 0.05f;
        const Gdiplus::REAL flamScale = flamSide / kFlamingoViewBox;

        Gdiplus::GraphicsState flamState = g.Save();
        g.TranslateTransform(flamX, flamY);
        g.ScaleTransform(flamScale, flamScale);

        Gdiplus::GraphicsPath flamingo;
        flamingo.AddBezier(120.0f, 30.0f, 138.0f, 30.0f, 148.0f, 42.0f, 148.0f, 58.0f);
        flamingo.AddBezier(148.0f, 58.0f, 148.0f, 71.0f, 141.0f, 80.0f, 130.0f, 84.0f);
        flamingo.AddBezier(130.0f, 84.0f, 123.0f, 86.0f, 119.0f, 91.0f, 119.0f, 98.0f);
        flamingo.AddLine  (119.0f, 98.0f, 119.0f, 105.0f);
        flamingo.AddBezier(119.0f, 105.0f, 119.0f, 137.0f, 105.0f, 161.0f, 77.0f, 172.0f);
        flamingo.AddBezier(77.0f, 172.0f, 63.0f, 178.0f, 52.0f, 169.0f, 56.0f, 155.0f);
        flamingo.AddBezier(56.0f, 155.0f, 60.0f, 137.0f, 74.0f, 129.0f, 84.0f, 122.0f);
        flamingo.AddBezier(84.0f, 122.0f, 94.0f, 115.0f, 98.0f, 108.0f, 98.0f, 97.0f);
        flamingo.AddLine  (98.0f, 97.0f, 98.0f, 90.0f);
        flamingo.AddBezier(98.0f, 90.0f, 80.0f, 86.0f, 63.0f, 76.0f, 63.0f, 55.0f);

        Gdiplus::Pen flamingoPen(
            Gdiplus::Color(kSoftWhiteAlpha, 255, 255, 255), kFlamingoStrokeInViewBox);
        flamingoPen.SetStartCap(Gdiplus::LineCapRound);
        flamingoPen.SetEndCap(Gdiplus::LineCapRound);
        flamingoPen.SetLineJoin(Gdiplus::LineJoinRound);
        g.DrawPath(&flamingoPen, &flamingo);
        g.Restore(flamState);

        // 3. 头像：投影 + 白色圆环 + 橙色圆角底。
        const Gdiplus::REAL ringRadius = ringSide * kRoundRectRadiusRatio;
        const Gdiplus::REAL avRadius   = avSide * kRoundRectRadiusRatio;

        Gdiplus::GraphicsPath shadowPath;
        AddRoundRectPath(shadowPath, ringX, ringY + pad * 2.0f,
                         ringSide, ringSide, ringRadius);
        Gdiplus::SolidBrush shadowBrush(Gdiplus::Color(46, 0, 0, 0));
        g.FillPath(&shadowBrush, &shadowPath);

        Gdiplus::GraphicsPath ringPath;
        AddRoundRectPath(ringPath, ringX, ringY, ringSide, ringSide, ringRadius);
        Gdiplus::SolidBrush ringBrush(Gdiplus::Color(255, 255, 255, 255));
        g.FillPath(&ringBrush, &ringPath);

        Gdiplus::GraphicsPath avatarPath;
        AddRoundRectPath(avatarPath, avX, avY, avSide, avSide, avRadius);
        // 头像方块底色取自 m_avatarColor（登录窗按账号设置；默认主题橙）。
        Gdiplus::SolidBrush avatarBrush(
            Gdiplus::Color(255, GetRValue(m_avatarColor),
                           GetGValue(m_avatarColor), GetBValue(m_avatarColor)));
        g.FillPath(&avatarBrush, &avatarPath);

        // 4. 状态胶囊底 + 状态徽标（形状与颜色随当前在线状态：在线实心圆 / 离开时钟 /
        //    忙碌横杠 / 离线空心环，与主面板、列表、菜单四处同源）。
        Gdiplus::GraphicsPath pillPath;
        AddRoundRectPath(pillPath, pillX, pillY, pillW, pillH, pillH / 2.0f);
        Gdiplus::SolidBrush pillBgBrush(Gdiplus::Color(12, 0, 0, 0));
        g.FillPath(&pillBgBrush, &pillPath);

        // 登录窗只表达用户挑选的状态，不涉及掉线重连那套连接态，故直接按 m_presence 画。
        balloonmain::DrawPresenceBadge(g, m_presence, false,
                                       dotCx - badgeBox / 2.0f,
                                       dotCy - badgeBox / 2.0f,
                                       badgeBox);
    }

    // ====================================================================
    // 第二层：GDI 文字（清晰锐利）
    // ====================================================================

    // 标题 / 标语 —— 贴渐变区左下角，左对齐。
    const int textX     = left + (int)((float)rectW * 0.063f);
    const float gradBot = (float)top + gradH;
    const int titlePx   = (int)(gradH * kTitleFontFactor);
    const int tagPx     = (int)(gradH * kTagFontFactor);

    RECT titleRc = { textX, (int)(gradBot - gradH * 0.40f),
                     left + rectW, (int)gradBot };
    DrawGdiText(hdc, L"FlamingoIM", titleRc, titlePx, true, kTitleColor,
                DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOCLIP);

    RECT tagRc = { textX, (int)(gradBot - gradH * 0.205f),
                   left + rectW, (int)gradBot };
    DrawGdiText(hdc, L"让沟通像火烈鸟一样优雅", tagRc, tagPx, false, kTagColor,
                DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOCLIP);

    // 头像内白色首字母。
    if (!m_userName.IsEmpty())
    {
        const CString initial = m_userName.Left(1);
        RECT avatarRc = { (int)avX, (int)avY,
                          (int)(avX + avSide), (int)(avY + avSide) };
        DrawGdiText(hdc, (LPCWSTR)initial, avatarRc,
                    (int)(avSide * kInitialFontFactor), true, kInitialColor,
                    DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);
    }

    // 用户名。
    if (!m_userName.IsEmpty())
    {
        RECT nameRc = { left, (int)((float)top + (float)rectH * kNameTopRatio),
                        left + rectW,
                        (int)((float)top + (float)rectH * (kNameTopRatio + 0.12f)) };
        DrawGdiText(hdc, (LPCWSTR)m_userName, nameRc,
                    (int)((float)rectH * kNameFontFactor), true, kNameColor,
                    DT_CENTER | DT_TOP | DT_SINGLELINE | DT_NOCLIP);
    }

    // 状态胶囊文字（徽标右侧），随当前在线状态变化（在线 / 离开 / 忙碌）。
    RECT pillTextRc = { (int)(dotCx + badgeBox / 2.0f), (int)pillY,
                        (int)(pillX + pillW - pillH * 0.18f), (int)(pillY + pillH) };
    DrawGdiText(hdc, balloonmain::GetPresenceVisual(m_presence).text, pillTextRc,
                (int)(pillH * 0.52f), false,
                kPillTextColor, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);

    // 缓存状态胶囊命中矩形（供 OnLButtonUp 判定 + 登录窗锚定状态菜单）。
    ::SetRect(&m_rcPillHit, (int)pillX, (int)pillY,
              (int)(pillX + pillW), (int)(pillY + pillH));
}

} // namespace balloonauth
