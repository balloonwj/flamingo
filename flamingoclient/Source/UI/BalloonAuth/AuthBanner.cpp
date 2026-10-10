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
#include "../../App/BrandText.h"                      // 产品名称与标语的缺省值
#include "../BalloonMain/MainPanelStatusMenu.h"   // GetPresenceVisual（状态胶囊文字，与主面板同源）
#include "../BalloonMain/PresenceBadge.h"         // DrawPresenceBadge（状态胶囊上的状态徽标）
#include "../../net/Protocol.h"                       // presence_type（状态默认值 presence_online）
#include "../BalloonMain/AvatarGlyph.h"               // DrawCenteredGlyphGdi：头像字居中
#include "../../Common/I18n/I18n.h"   // i18n::UiFontName：当前界面语言的界面字体
#include "../../../third-party/balloonui/balloonui/DuiHost.h"                      // DuiHost：悬停提示换算屏幕坐标
#include "../../../third-party/balloonui/balloonui/Controls/Feedback/DuiToolTip.h"   // 状态胶囊的悬停提示

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
const float kPillWidthRatio      = 62.0f / 240.0f;    // 状态胶囊宽度占比（不含右端放下拉小三角的一截）
const float kPillArrowAreaRatio  = 14.0f / 240.0f;    // 状态胶囊右端放下拉小三角的一截宽度占比（2026-10-04 起）

// ---- 状态胶囊的可点击提示（2026-10-04 起）----
// 下拉小三角的宽、高与其中心距胶囊右缘的距离，各占胶囊高度的比例。
const float kPillArrowWidthFactor  = 0.34f;
const float kPillArrowHeightFactor = 0.18f;
const float kPillArrowRightFactor  = 0.42f;
// 胶囊文字右缘与小三角左缘之间的间距，占胶囊高度的比例。
const float kPillTextArrowGapFactor = 0.10f;
// 胶囊底色（黑色）的不透明度：平时、鼠标悬停时。
const BYTE kPillBgAlpha            = 12;
const BYTE kPillHoverBgAlpha       = 30;
// 胶囊细边框（黑色）的不透明度与线宽（像素）。
const BYTE kPillBorderAlpha        = 46;
const float kPillBorderWidth       = 1.0f;
// 鼠标悬停在胶囊上时的提示文字。
const wchar_t* const kPillTipText  = TR_NOOP(L"点击切换登录状态");
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
const float kFlamingoRightShiftRatio = 0.12f;   // 绘制区向右溢出控件右缘的距离 / 渐变区高度
const float kFlamingoTopShiftRatio  = 0.05f;    // 绘制区向上溢出控件上缘的距离 / 渐变区高度
const float kFlamingoPathMinX       = 52.0f;    // 火烈鸟路径最左点的 x（viewBox 内），品牌文字区以它为右界
const float kTitleFontFactor        = 0.135f;   // 标题字号 / 渐变区高度
const float kTagFontFactor          = 0.072f;   // 标语字号 / 渐变区高度
const float kInitialFontFactor      = 0.46f;    // 头像字母字号 / 头像边长

// ---- 品牌文字区（产品名称 + 标语）----

const float kBrandTextLeftRatio = 0.063f;  // 文字左边距 / 控件宽度
const float kBrandTextGapRatio  = 0.05f;   // 文字区右缘与火烈鸟最左点之间的间距 / 渐变区高度
const float kTitleMinFontRatio  = 0.6f;    // 产品名称过长时允许缩小到的字号 / 设计字号
// 标题行上沿与标语行上沿之间的距离 / 渐变区高度。取自原设计两行的相对位置（标题上沿在
// 渐变区下沿之上 0.40、标语上沿在其上 0.205，相差 0.195），两行整体上移后间距保持不变。
const float kTitleToTagRatio    = 0.195f;
// 文字区下缘与头像白环上缘之间的间距 / 控件高度：设计尺寸（高 240）下约 10 像素。
// 产品名称与标语整体放在头像上方（用户 2026-10-03 指定），与头像纵向不重叠。
const float kBrandTextAvatarGapRatio = 10.0f / 240.0f;

// Banner 上所有文字使用的字体：当前界面语言的界面字体（多语言第 3e 步）。品牌文字的测量（FitBrandTextSize）
// 也用它，两者须一致。
const wchar_t* BannerFontFace()
{
    return i18n::UiFontName();
}

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
        BannerFontFace());
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
    , m_title(brandtext::kDefaultAppTitle)
    , m_slogan(brandtext::kDefaultSlogan)
    , m_avatarGlyphSet(false)
    , m_pillHover(false)
{
    ::SetRectEmpty(&m_rcPillHit);
    ::SetRectEmpty(&m_rcAvatar);
}

AuthBanner::~AuthBanner()
{
}

void AuthBanner::SetAvatar(LPCTSTR glyph, COLORREF color, LPCWSTR photoPath)
{
    m_avatarGlyph = (glyph != NULL) ? glyph : _T("");
    m_avatarGlyphSet = true;
    m_avatarColor = color;
    const std::wstring path = (photoPath != NULL) ? photoPath : L"";
    if (path != m_avatarPhotoPath)
    {
        m_avatarPhotoPath = path;
        m_avatarPhoto.reset();
        if (!path.empty())
        {
            // 从文件加载后复制一份放在内存里，随即释放文件：照片是主面板的头像缓存文件，不能一直占着
            std::unique_ptr<Gdiplus::Bitmap> file(Gdiplus::Bitmap::FromFile(path.c_str()));
            if (file && file->GetLastStatus() == Gdiplus::Ok && file->GetWidth() > 0 && file->GetHeight() > 0)
            {
                m_avatarPhoto.reset(file->Clone(0, 0, (INT)file->GetWidth(), (INT)file->GetHeight(),
                                                PixelFormat32bppARGB));
                if (m_avatarPhoto && m_avatarPhoto->GetLastStatus() != Gdiplus::Ok)
                    m_avatarPhoto.reset();
            }
        }
    }
    Invalidate();
}

bool AuthBanner::HasAvatarPhoto() const
{
    return m_avatarPhoto != nullptr;
}

void AuthBanner::SetTitleText(LPCTSTR title)
{
    m_title = (title != NULL && title[0] != _T('\0')) ? title : brandtext::kDefaultAppTitle;
    Invalidate();
}

void AuthBanner::SetSloganText(LPCTSTR slogan)
{
    m_slogan = (slogan != NULL) ? slogan : _T("");
    Invalidate();
}

RECT AuthBanner::ComputeBrandTextArea(const RECT& rcItem)
{
    RECT area = { 0, 0, 0, 0 };
    const int rectW = rcItem.right - rcItem.left;
    const int rectH = rcItem.bottom - rcItem.top;
    if (rectW <= 0 || rectH <= 0)
    {
        return area;
    }

    // 火烈鸟绘制区的位置与 OnPaint 中的计算一致，路径最左点即图案的最左端
    const float gradH        = (float)rectH * kGradientHeightRatio;
    const float flamSide     = gradH * kFlamingoSizeFactor;
    const float flamX        = (float)rcItem.right - flamSide + gradH * kFlamingoRightShiftRatio;
    const float flamingoLeft = flamX + kFlamingoPathMinX * flamSide / kFlamingoViewBox;

    // 文字整体放在头像白环上方：下缘在白环上缘之上留出间距。两者纵向不重叠，横向就只需
    // 避开右侧的火烈鸟图案。
    const float ringTop = (float)rcItem.top + (float)rectH * kRingTopRatio;

    area.left   = rcItem.left + (int)((float)rectW * kBrandTextLeftRatio);
    area.right  = (int)(flamingoLeft - gradH * kBrandTextGapRatio);
    area.top    = rcItem.top;
    area.bottom = (int)(ringTop - (float)rectH * kBrandTextAvatarGapRatio);
    if (area.right < area.left)
    {
        area.right = area.left;
    }
    if (area.bottom < area.top)
    {
        area.bottom = area.top;
    }
    return area;
}

void AuthBanner::ComputeBrandTextRows(HDC hdc, const RECT& rcItem, LPCWSTR title, LPCWSTR slogan,
                                      RECT& titleRc, RECT& sloganRc)
{
    const RECT area   = ComputeBrandTextArea(rcItem);
    const float gradH = (float)(rcItem.bottom - rcItem.top) * kGradientHeightRatio;
    const int titlePx = (int)(gradH * kTitleFontFactor);
    const int tagPx   = (int)(gradH * kTagFontFactor);

    // 行高都按设计字号测量：产品名称被缩小字号时行框高度不变、文字贴框底绘制，
    // 与下方标语的间距保持不变
    const int titleLineH = MeasureBrandText(hdc, title, BannerFontFace(), titlePx, true).cy;
    const bool hasSlogan = (slogan != NULL && slogan[0] != L'\0');
    const int sloganLineH = hasSlogan ? MeasureBrandText(hdc, slogan, BannerFontFace(), tagPx, false).cy : 0;

    // 标语贴文字区下缘；不显示标语时为高度 0 的矩形
    sloganRc.left   = area.left;
    sloganRc.right  = area.right;
    sloganRc.bottom = area.bottom;
    sloganRc.top    = area.bottom - sloganLineH;

    // 标题在标语之上，两行上沿相距 kTitleToTagRatio；没有标语时标题下移、贴文字区下缘
    titleRc.left  = area.left;
    titleRc.right = area.right;
    if (hasSlogan)
    {
        titleRc.top = sloganRc.top - (int)(gradH * kTitleToTagRatio);
    }
    else
    {
        titleRc.top = area.bottom - titleLineH;
    }
    if (titleRc.top < area.top)
    {
        titleRc.top = area.top;
    }
    titleRc.bottom = titleRc.top + titleLineH;
}

BrandTextFitResult AuthBanner::FitTitle(HDC hdc, const RECT& rcItem, LPCWSTR title)
{
    const RECT area   = ComputeBrandTextArea(rcItem);
    const float gradH = (float)(rcItem.bottom - rcItem.top) * kGradientHeightRatio;
    const int desired = (int)(gradH * kTitleFontFactor);
    const int minPx   = (int)((float)desired * kTitleMinFontRatio);
    return FitBrandTextSize(hdc, title, BannerFontFace(), desired, minPx, area.right - area.left, true);
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

bool AuthBanner::OnMouseMove(POINT pt, UINT mkFlags)
{
    SetPillHover(::PtInRect(&m_rcPillHit, pt) != FALSE, pt);
    return DuiControl::OnMouseMove(pt, mkFlags);
}

bool AuthBanner::OnMouseLeave()
{
    const POINT none = { 0, 0 };
    SetPillHover(false, none);
    return DuiControl::OnMouseLeave();
}

bool AuthBanner::OnSetCursor(POINT pt)
{
    // 状态胶囊可以点：手形光标
    if (::PtInRect(&m_rcPillHit, pt))
    {
        ::SetCursor(::LoadCursor(NULL, IDC_HAND));
        return true;
    }
    return DuiControl::OnSetCursor(pt);
}

void AuthBanner::SetPillHover(bool hover, POINT pt)
{
    if (hover == m_pillHover)
    {
        return;
    }
    m_pillHover = hover;
    balloonwjui::DuiToolTipMgr& tips = balloonwjui::DuiToolTipMgr::Inst();
    if (hover)
    {
        // 提示只在胶囊上登记：本控件是整块 Banner，按控件登记会在整块 Banner 上都弹提示。
        // 宿主只在鼠标进入本控件时通知提示管理器开始计时，进入胶囊时由这里补一次通知。
        tips.Register(this, TR(kPillTipText));
        balloonwjui::DuiHost* host = GetHost();
        if (host != NULL && ::IsWindow(host->m_hWnd))
        {
            POINT screen = pt;
            ::ClientToScreen(host->m_hWnd, &screen);
            tips.OnHover(this, screen);
        }
    }
    else
    {
        tips.OnLeave(this);
        tips.Unregister(this);
    }
    Invalidate();
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

    // 胶囊宽度含右端放下拉小三角的一截（2026-10-04 起）
    const float pillW = (float)rectH * (kPillWidthRatio + kPillArrowAreaRatio);
    const float pillH = (float)rectH * kPillHeightRatio;
    const float pillX = centerX - pillW / 2.0f;
    const float pillY = (float)top + (float)rectH * kPillTopRatio;
    const float badgeBox = pillH * kBadgeBoxRatio;      // 状态徽标绘制方框边长
    const float dotCx    = pillX + pillH * 0.46f;       // 徽标中心 X（胶囊左内缘处）
    const float dotCy    = pillY + pillH / 2.0f;        // 徽标中心 Y（胶囊垂直居中）
    const float arrowCx  = pillX + pillW - pillH * kPillArrowRightFactor;   // 下拉小三角中心 X
    const float arrowW   = pillH * kPillArrowWidthFactor;                   // 下拉小三角宽
    const float arrowH   = pillH * kPillArrowHeightFactor;                  // 下拉小三角高

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
                                      + gradH * kFlamingoRightShiftRatio;
        const Gdiplus::REAL flamY     = (Gdiplus::REAL)top - gradH * kFlamingoTopShiftRatio;
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
        // 外圈与底色都按像素格对齐地填充，与头像字、照片头像对齐（见 AvatarGlyph.h）；只对齐底色会让外圈一侧厚、一侧薄
        balloonmain::FillAvatarBackground(g, ringBrush, ringPath);

        Gdiplus::GraphicsPath avatarPath;
        AddRoundRectPath(avatarPath, avX, avY, avSide, avSide, avRadius);
        if (m_avatarPhoto)
        {
            // 照片：等比缩放到铺满圆角方块（长边多出的部分居中裁掉），按圆角路径填充
            const Gdiplus::REAL photoW = (Gdiplus::REAL)m_avatarPhoto->GetWidth();
            const Gdiplus::REAL photoH = (Gdiplus::REAL)m_avatarPhoto->GetHeight();
            const Gdiplus::REAL scale = (avSide / photoW > avSide / photoH) ? (avSide / photoW) : (avSide / photoH);
            Gdiplus::TextureBrush photoBrush(m_avatarPhoto.get(), Gdiplus::WrapModeClamp);
            photoBrush.TranslateTransform(avX + (avSide - photoW * scale) / 2.0f, avY + (avSide - photoH * scale) / 2.0f);
            photoBrush.ScaleTransform(scale, scale);
            balloonmain::FillAvatarBackground(g, photoBrush, avatarPath);
        }
        else
        {
            // 头像方块底色取自 m_avatarColor（登录窗按账号设置；默认主题橙）。
            Gdiplus::SolidBrush avatarBrush(
                Gdiplus::Color(255, GetRValue(m_avatarColor),
                               GetGValue(m_avatarColor), GetBValue(m_avatarColor)));
            balloonmain::FillAvatarBackground(g, avatarBrush, avatarPath);
        }

        // 4. 状态胶囊底 + 状态徽标（形状与颜色随当前在线状态：在线实心圆 / 离开时钟 /
        //    忙碌横杠 / 离线空心环，与主面板、列表、菜单四处同源）。
        Gdiplus::GraphicsPath pillPath;
        AddRoundRectPath(pillPath, pillX, pillY, pillW, pillH, pillH / 2.0f);
        // 底色：鼠标悬停时加深，提示可以点
        Gdiplus::SolidBrush pillBgBrush(Gdiplus::Color(m_pillHover ? kPillHoverBgAlpha : kPillBgAlpha, 0, 0, 0));
        g.FillPath(&pillBgBrush, &pillPath);
        // 细边框与右端的下拉小三角：看起来像一个可以点开的下拉按钮
        Gdiplus::Pen pillBorder(Gdiplus::Color(kPillBorderAlpha, 0, 0, 0), kPillBorderWidth);
        g.DrawPath(&pillBorder, &pillPath);
        const Gdiplus::PointF arrow[3] = {
            Gdiplus::PointF(arrowCx - arrowW / 2.0f, dotCy - arrowH / 2.0f),
            Gdiplus::PointF(arrowCx + arrowW / 2.0f, dotCy - arrowH / 2.0f),
            Gdiplus::PointF(arrowCx, dotCy + arrowH / 2.0f)
        };
        Gdiplus::SolidBrush arrowBrush(
            Gdiplus::Color(255, GetRValue(kPillTextColor), GetGValue(kPillTextColor), GetBValue(kPillTextColor)));
        g.FillPolygon(&arrowBrush, arrow, 3);

        // 登录窗只表达用户挑选的状态，不涉及掉线重连那套连接态，故直接按 m_presence 画。
        balloonmain::DrawPresenceBadge(g, m_presence, false,
                                       dotCx - badgeBox / 2.0f,
                                       dotCy - badgeBox / 2.0f,
                                       badgeBox);
    }

    // ====================================================================
    // 第二层：GDI 文字（清晰锐利）
    // ====================================================================

    // 产品名称 / 标语 —— 两行整体放在头像上方、靠左对齐，右侧止于火烈鸟图案之前
    // （行矩形的计算见 ComputeBrandTextRows）。
    const int tagPx = (int)(gradH * kTagFontFactor);
    RECT titleRc;
    RECT tagRc;
    ComputeBrandTextRows(hdc, m_rcItem, m_title, m_slogan, titleRc, tagRc);

    // 产品名称放不下时先缩小字号（FitTitle），缩到下限仍放不下则以省略号截断；文字贴行框底
    // 绘制，缩小字号后仍紧贴下方的标语。
    const BrandTextFitResult titleFit = FitTitle(hdc, m_rcItem, m_title);
    DrawGdiText(hdc, m_title, titleRc, titleFit.m_pixelHeight, true, kTitleColor,
                DT_LEFT | DT_BOTTOM | DT_SINGLELINE | DT_NOCLIP | DT_NOPREFIX | DT_END_ELLIPSIS);

    // 标语不缩小字号，放不下直接以省略号截断；为空时不绘制（DrawGdiText 对空串直接返回）。
    DrawGdiText(hdc, m_slogan, tagRc, tagPx, false, kTagColor,
                DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOCLIP | DT_NOPREFIX | DT_END_ELLIPSIS);

    // 头像方块的矩形：缓存下来供测试取色
    ::SetRect(&m_rcAvatar, (int)avX, (int)avY, (int)(avX + avSide), (int)(avY + avSide));

    // 头像内白色字形：画照片时不画；调用过 SetAvatar 时取它设的字形，否则取用户名首字。
    const CString initial = m_avatarGlyphSet ? m_avatarGlyph : m_userName.Left(1);
    if (!m_avatarPhoto && !initial.IsEmpty())
    {
        RECT avatarRc = { (int)avX, (int)avY,
                          (int)(avX + avSide), (int)(avY + avSide) };
        // 按字形笔画外框居中（见 AvatarGlyph.h），DrawText 按单元格居中会让字偏低
        balloonmain::DrawCenteredGlyphGdi(hdc, (LPCWSTR)initial, avatarRc,
                                          (int)(avSide * kInitialFontFactor), true, kInitialColor);
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
    // 文字在徽标与下拉小三角之间居中
    RECT pillTextRc = { (int)(dotCx + badgeBox / 2.0f), (int)pillY,
                        (int)(arrowCx - arrowW / 2.0f - pillH * kPillTextArrowGapFactor), (int)(pillY + pillH) };
    DrawGdiText(hdc, balloonmain::GetPresenceVisual(m_presence).text, pillTextRc,
                (int)(pillH * 0.52f), false,
                kPillTextColor, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);

    // 缓存状态胶囊命中矩形（供 OnLButtonUp 判定 + 登录窗锚定状态菜单）。
    ::SetRect(&m_rcPillHit, (int)pillX, (int)pillY,
              (int)(pillX + pillW), (int)(pillY + pillH));
}

} // namespace balloonauth
