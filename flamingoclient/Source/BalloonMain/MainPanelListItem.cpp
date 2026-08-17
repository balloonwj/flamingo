/**
 *  MainPanelListItem.cpp
 *
 *  主面板通用列表行的绘制实现。形状（悬停底 / 头像圆角 / 状态点 / 角标）
 *  用 GDI+ 抗锯齿，文字用 GDI DrawText（清晰）。
 *
 *  本 .cpp 不 include Flamingo 的 stdafx.h —— 它要被 Flamingo.vcxproj 与
 *  FlamingoUnit.vcxproj 共用。在 Flamingo.vcxproj 里需标记
 *  PrecompiledHeader=NotUsing。
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
#define _RICHEDIT_VER   0x0200
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
#pragma comment(lib, "msimg32.lib")   // AlphaBlend（画自定义图头像）

#include "MainPanelListItem.h"
#include "MainPanelMockData.h"
#include "PresenceBadge.h"          // DrawPresenceBadge（头像右下角的状态徽标）

namespace balloonmain {

namespace {

// ---- 配色 --------------------------------------------------------------

// 名称文字色 —— 比 design --text(#1A1A1A) 略浅，避免视觉过黑过重。
const COLORREF kNameColor = RGB(0x3C, 0x3C, 0x3C);
// 搜索命中高亮黄底（淡黄，承托深色名称文字，对应 design 搜索框命中色）。
const COLORREF kHighlightFill = RGB(0xFF, 0xF1, 0x76);
// 副行文字色（design --text-muted #6B6B6B）。
const COLORREF kSubColor  = RGB(0x6B, 0x6B, 0x6B);
// 右上角时间色（design --text-faint）。
const COLORREF kTimeColor = RGB(0xA8, 0xA6, 0xA2);
// 未读角标红 #FF4D4F。
const Gdiplus::Color kBadgeRed(255, 0xFF, 0x4D, 0x4F);
// 状态点白色描边（与底色拉开）。
const Gdiplus::Color kStatusRing(255, 255, 255, 255);

// ---- 几何常量 ----------------------------------------------------------

// 头像边长与圆角取自类内公开常量 —— 调用方生成真图位图时用的是同两个值，
// 在此复述一份数字会让两边慢慢对不上。
const int           kAvatarSize    = MainPanelListItem::kAvatarSizePx;
const int           kAvatarLeft    = 8;     // 头像距行左边
const int           kAvatarTextGap = 10;    // 头像与文字列间距
const Gdiplus::REAL kAvatarRadius  = (Gdiplus::REAL)MainPanelListItem::kAvatarRadiusPx;
// 状态徽标的绘制方框边长。徽标圆底只占方框的 20/24，故 9 对应约 7.5px 的圆点 ——
// 与改版前 7px 的纯色小圆点接近，但字形（时钟 / 横杠 / 空心环）能看出来了。
const Gdiplus::REAL kStatusDotSize = 9.0f;
const Gdiplus::REAL kStatusRingW   = 2.0f;  // 状态点白描边宽度
// 徽标圆底在绘制方框中所占的比例（viewBox 24 里半径 10 的圆，直径 20/24）。
// 白描边要贴着圆底外扩，故需要知道这个比例。
const Gdiplus::REAL kBadgeDiscRatio = 20.0f / 24.0f;
const int           kNamePx        = 12;    // 名称字号
const int           kSubPx         = 10;    // 副行字号
const int           kRightPx       = 11;    // 右侧文字（时间 / 成员数）字号
const Gdiplus::REAL kBadgeDiameter = 16.0f; // 角标直径
const int           kRowRightPad   = 10;    // 行右内距

// 名称 / 副行右界与行右内距之间的呼吸位（逻辑像素），免得文字贴着行边缘。
const int kNameRightGap       = 4;
// 名称 / 副行右界与未读角标之间的间距（逻辑像素）。
const int kBadgeTextGap       = 6;
// 第一行右侧文字与其左侧名称之间的最小间距（逻辑像素）。两者同处第一行，靠得太近
// 会读成一串，故留出这一段空白。
const int kRightTextGap       = 8;
// 量不到右侧文字宽度（取字体失败）时的兜底预留宽（逻辑像素）。取值参照本控件改造前
// 硬编码的右侧文字框宽度，足够放下 "12 人" / "昨天" 这类短文字。
const int kRightTextFallbackW = 70;
// 单行行（没有副行）里右侧文字距行顶的距离与文字框高度（逻辑像素）。这种行没有第二行
// 可作参照，右侧文字沿用"贴着行右上角"的老位置。
const int kRightTextTop       = 8;
const int kRightTextHeight    = 16;

// ---- 悬停态「定位」小按钮 ----------------------------------------------
//
// 只在打开了 SetLocateButtonVisible 的行上出现（目前是搜索结果里的联系人行），
// 且仅当鼠标悬停于该行时才绘制；其余时候完全不占位。

// 图标绘制方框的边长（逻辑像素）。取 18 与行内其它次要元素（未读角标 16）量级相当：
// 再大会与 36 见方的头像抢视觉重量，再小则难以点中。
const int kLocateIconSize    = 18;
// 图标绘制方框的右边界与行右内距 kRowRightPad 之间的间距（逻辑像素）。0 表示图标
// 右边界正好落在行右内距那条线上，与未读角标、右侧文字的右对齐口径一致。
const int kLocateIconRightGap = 0;
// 鼠标落在图标上时，垫在图标下方的圆底相对图标方框每边外扩的距离（逻辑像素）。
// 外扩 4 后圆底直径 26，与常见工具栏图标按钮的触发区尺寸相当。
const int kLocateHotPad      = 4;
// 图标常态描边色（中灰）。刻意弱于名称文字色 kNameColor，避免抢走主行的注意力。
const COLORREF kLocateIconColor    = RGB(0x8C, 0x8C, 0x8C);
// 鼠标正落在图标上时的描边色（深灰）。与常态之间拉开明显的明度差，表示"这里可以点"。
const COLORREF kLocateIconHotColor = RGB(0x40, 0x40, 0x40);
// 鼠标正落在图标上时垫在图标下方的浅色圆底。比搜索结果行的悬停底色
// （SearchDropdown 里的 RGB(0xE9,0xEC,0xF1)）再深一档，才能在已经变色的行底上看出来。
const Gdiplus::Color kLocateHotFill(255, 0xD6, 0xDB, 0xE3);
// 图标描边宽度，以 viewBox 24 为基准（与 MainPanelIcons 里其它线性图标同口径）。
const Gdiplus::REAL kLocateStrokeWidth = 2.0f;
// 图标的 viewBox 基准边长（design icons.jsx 的图标统一用 0 0 24 24）。
const Gdiplus::REAL kLocateViewBox     = 24.0f;

// ---- 地图定位针在 viewBox 24 里的几何 ----
// 针头圆心。x 取正中；y 取 9.5 而非 12 —— 针身要向下伸出一段尖角，圆心上移后
// 整枚针在方框内才是上下居中的。
const Gdiplus::REAL kPinHeadCx     = 12.0f;
const Gdiplus::REAL kPinHeadCy     = 9.5f;
// 针头圆半径。5.5 是在 18 像素目标尺寸下实测的下限附近：再小针孔就糊进圆环里了。
const Gdiplus::REAL kPinHeadR      = 5.5f;
// 针尖端点的 y 坐标（落在针头圆心正下方）。20.5 使整枚针在 viewBox 上下各留约 3.5
// 的余量，与本文件其它图标的留白一致。
const Gdiplus::REAL kPinTipY       = 20.5f;
// 针身两条斜边与针头圆的相接点相对圆心的偏移量（圆上 45° 方位，左下 / 右下各一个）。
// 数值即 kPinHeadR * cos(45°) ≈ 3.889，取 3.9 便于阅读。
const Gdiplus::REAL kPinSideOffset = 3.9f;
// 针头正中那个实心小圆（针孔）的半径。太小在 18 像素下看不见，太大会把针头填满、
// 看着像个实心水滴而不是定位针。
const Gdiplus::REAL kPinHoleR      = 1.9f;

// 在 (x, y) 起、边长 size 的方形区域内描边绘制一枚地图定位针（圆形针头 + 下方尖角
// + 针头正中的实心小圆）。画法与 MainPanelIcons::DrawLineIcon 同口径：先把 viewBox 24
// 映射到目标区域，再用圆头圆角的 Pen 描边。
//
// 之所以不把它加进 MainPanelIcons 的 IconKind：那份图标表是主面板 Tab 栏与各菜单
// 共用的公共资源，本图标目前只有搜索结果行一个使用方，先就地画；将来出现第二个
// 使用方再归并过去。
void DrawLocatePin(Gdiplus::Graphics& g,
                   Gdiplus::REAL x, Gdiplus::REAL y, Gdiplus::REAL size,
                   const Gdiplus::Color& color)
{
    if (size <= 0.0f)
    {
        return;
    }

    Gdiplus::GraphicsState st = g.Save();
    g.TranslateTransform(x, y);
    g.ScaleTransform(size / kLocateViewBox, size / kLocateViewBox);

    Gdiplus::Pen pen(color, kLocateStrokeWidth);
    pen.SetStartCap(Gdiplus::LineCapRound);
    pen.SetEndCap(Gdiplus::LineCapRound);
    pen.SetLineJoin(Gdiplus::LineJoinRound);

    // 针头圆。
    g.DrawEllipse(&pen, kPinHeadCx - kPinHeadR, kPinHeadCy - kPinHeadR,
                  kPinHeadR * 2.0f, kPinHeadR * 2.0f);

    // 针身：自针头圆左下 45° 处斜下到针尖，再斜上回到圆右下 45° 处。
    {
        Gdiplus::GraphicsPath tip;
        tip.AddLine(kPinHeadCx - kPinSideOffset, kPinHeadCy + kPinSideOffset,
                    kPinHeadCx, kPinTipY);
        tip.AddLine(kPinHeadCx, kPinTipY,
                    kPinHeadCx + kPinSideOffset, kPinHeadCy + kPinSideOffset);
        g.DrawPath(&pen, &tip);
    }

    // 针孔：针头正中的实心小圆，有了它才一眼能认出这是定位针。
    {
        Gdiplus::SolidBrush brush(color);
        g.FillEllipse(&brush, kPinHeadCx - kPinHoleR, kPinHeadCy - kPinHoleR,
                      kPinHoleR * 2.0f, kPinHoleR * 2.0f);
    }

    g.Restore(st);
}

// 用四段 90° 圆弧 + 闭合，向 path 添加一个圆角矩形。
void AddRoundRect(Gdiplus::GraphicsPath& path,
                  Gdiplus::REAL x, Gdiplus::REAL y,
                  Gdiplus::REAL w, Gdiplus::REAL h, Gdiplus::REAL radius)
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

// 用与 DrawGdiText 完全相同的字体，量一段文字的渲染宽度（逻辑像素）。用来给第一行
// 右侧的文字预留位置，使左侧名称的省略号截在它左边、两段字不叠在一起。
// 取不到字体或文字为空时返回 0，调用方据此走兜底宽。
int MeasureGdiTextWidth(HDC hdc, LPCWSTR text, int pixelHeight, bool bold)
{
    if (text == NULL || text[0] == L'\0' || pixelHeight <= 0)
    {
        return 0;
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
        return 0;
    }
    HFONT hOldFont = (HFONT)::SelectObject(hdc, hFont);
    SIZE sz = { 0, 0 };
    ::GetTextExtentPoint32W(hdc, text, ::lstrlenW(text), &sz);
    ::SelectObject(hdc, hOldFont);
    ::DeleteObject(hFont);
    return sz.cx;
}

// 按底色明度选头像文字色：浅底用深字、深底用白字（服务号头像底色偏浅）。
COLORREF AvatarTextColor(COLORREF bg)
{
    int lum = (GetRValue(bg) * 299 + GetGValue(bg) * 587
               + GetBValue(bg) * 114) / 1000;
    return (lum > 150) ? RGB(0x4A, 0x4A, 0x4A) : RGB(255, 255, 255);
}

} // anonymous namespace

MainPanelListItem::MainPanelListItem()
    : m_avatarColor(RGB(0xFF, 0x7A, 0x45))
    , m_avatarBitmap(NULL)
    , m_nameHiStart(-1)
    , m_nameHiLen(0)
    , m_showStatus(false)
    , m_status(kMockOffline)
    , m_badgeCount(0)
    , m_hoverFill(RGB(0xF4, 0xF3, 0xF0))   // design --hover 浅灰（默认，与主面板列表一致）
    , m_showLocate(false)                  // 默认不提供「定位」小按钮
    , m_locateHot(false)
    , m_locatePressed(false)
{
}

MainPanelListItem::~MainPanelListItem()
{
    if (m_avatarBitmap != NULL)
    {
        ::DeleteObject(m_avatarBitmap);
        m_avatarBitmap = NULL;
    }
}

void MainPanelListItem::SetAvatar(LPCTSTR text, COLORREF color)
{
    m_avatarText  = (text != NULL) ? text : _T("");
    m_avatarColor = color;
    Invalidate();
}

void MainPanelListItem::SetOwnedAvatarBitmap(HBITMAP hbm)
{
    if (m_avatarBitmap == hbm)
    {
        return;   //同一张图重复设，什么都不做（尤其别把它删了又留着指针）
    }
    if (m_avatarBitmap != NULL)
    {
        ::DeleteObject(m_avatarBitmap);
    }
    m_avatarBitmap = hbm;   //接管所有权
    Invalidate();
}

void MainPanelListItem::SetName(LPCTSTR name)
{
    m_name = (name != NULL) ? name : _T("");
    Invalidate();
}

void MainPanelListItem::SetNameHighlight(int start, int len)
{
    if (len <= 0 || start < 0)
    {
        m_nameHiStart = -1;
        m_nameHiLen   = 0;
    }
    else
    {
        m_nameHiStart = start;
        m_nameHiLen   = len;
    }
    Invalidate();
}

void MainPanelListItem::SetSubText(LPCTSTR sub)
{
    m_subText = (sub != NULL) ? sub : _T("");
    Invalidate();
}

void MainPanelListItem::SetStatus(bool show, int status)
{
    m_showStatus = show;
    m_status     = status;
    Invalidate();
}

void MainPanelListItem::SetRightText(LPCTSTR text)
{
    m_rightText = (text != NULL) ? text : _T("");
    Invalidate();
}

void MainPanelListItem::SetBadgeCount(int count)
{
    m_badgeCount = (count > 0) ? count : 0;
    Invalidate();
}

void MainPanelListItem::SetHoverFill(COLORREF c)
{
    m_hoverFill = c;
    Invalidate();
}

void MainPanelListItem::SetLocateButtonVisible(bool show)
{
    if (m_showLocate == show)
    {
        return;
    }
    m_showLocate = show;
    if (!m_showLocate)
    {
        //关掉按钮时一并复位命中态与按下态，免得下次再打开时残留上一轮的状态。
        m_locateHot = false;
        m_locatePressed = false;
    }
    Invalidate();
}

RECT MainPanelListItem::LocateIconRect() const
{
    RECT rc = { 0, 0, 0, 0 };

    const int h = m_rcItem.bottom - m_rcItem.top;
    if (h <= 0)
    {
        return rc;   //尚未布局（或行高为 0），没有可用矩形
    }

    rc.right  = m_rcItem.right - kRowRightPad - kLocateIconRightGap;
    rc.left   = rc.right - kLocateIconSize;
    rc.top    = m_rcItem.top + (h - kLocateIconSize) / 2;   //垂直居中
    rc.bottom = rc.top + kLocateIconSize;
    return rc;
}

bool MainPanelListItem::OnMouseEnter()
{
    bool r = balloonwjui::DuiButton::OnMouseEnter();   // 基类置 m_bHover=true 并通知父
    // 刚进入本行时先按"鼠标不在图标上"起步 —— 紧随其后的 OnMouseMove 会立刻把真实
    // 落点算出来，不会停在错误状态上。
    m_locateHot = false;
    Invalidate();                                      // 基类不重绘，这里补一次让 hover 底色立刻显示
    return r;
}

bool MainPanelListItem::OnMouseLeave()
{
    bool r = balloonwjui::DuiButton::OnMouseLeave();   // 基类置 m_bHover=false 并通知父
    // 鼠标已离开本行，图标随 hover 底色一起消失，命中态必须跟着复位；否则下次
    // 移进来时会先闪一帧加深色的图标。
    m_locateHot = false;
    // 按下态一并复位：按住左键拖出本行时不会有松开事件传到这里，不复位的话它会
    // 一直留着，让下一次真正的点击被误判成定位。
    m_locatePressed = false;
    Invalidate();                                      // 补一次让 hover 底色立刻消失
    return r;
}

bool MainPanelListItem::OnMouseMove(POINT pt, UINT mkFlags)
{
    bool r = balloonwjui::DuiButton::OnMouseMove(pt, mkFlags);

    // 只在"鼠标是否落在定位图标上"这个状态发生翻转时才重绘。鼠标在行内移动是高频
    // 事件，每次都 Invalidate 会让整张下拉不停刷新。
    bool hot = false;
    if (m_showLocate)
    {
        RECT rcIcon = LocateIconRect();
        if (rcIcon.right > rcIcon.left)
        {
            hot = (::PtInRect(&rcIcon, pt) != FALSE);
        }
    }
    if (hot != m_locateHot)
    {
        m_locateHot = hot;
        Invalidate();
    }

    return r;
}

bool MainPanelListItem::OnLButtonDown(POINT pt, UINT mkFlags)
{
    // 记下这一次按下的落点在不在「定位」图标内。只判松开点是不够的：在行的空白处
    // 按下、拖到图标上再松开，本该算一次普通的行点击（打开会话），只判松开点会把
    // 它当成定位。
    m_locatePressed = false;
    if (m_showLocate)
    {
        RECT rcIcon = LocateIconRect();
        if (rcIcon.right > rcIcon.left && ::PtInRect(&rcIcon, pt))
        {
            m_locatePressed = true;
        }
    }

    return balloonwjui::DuiButton::OnLButtonDown(pt, mkFlags);
}

bool MainPanelListItem::OnLButtonUp(POINT pt, UINT mkFlags)
{
    // 按下与松开的落点都在「定位」图标内：本次点击只表示"去通讯录里定位这个人"，
    // 绝不能再走基类那条 DUIN_CLICK 通路 —— 否则宿主会既定位又打开会话。
    //
    // 拦截手法是把基类调用改派到一个<u>落在本行之外</u>的坐标上：DuiButton::OnLButtonUp
    // 只有在"按下时在按钮内、松开时也在按钮内"两个条件同时成立时才发 DUIN_CLICK
    // （见 DuiButton.h 的行为约定），松开点在外就走它自己的取消路径。这样按下态、
    // 鼠标捕获、重绘等收尾工作仍由基类完成，本类不必去碰基类的私有成员，也就无需
    // 改动 balloonui 库。
    const bool pressedOnIcon = m_locatePressed;
    m_locatePressed = false;   // 一次点击用完即复位，不跨事件保留

    if (m_showLocate && pressedOnIcon)
    {
        RECT rcIcon = LocateIconRect();
        if (rcIcon.right > rcIcon.left && ::PtInRect(&rcIcon, pt))
        {
            POINT ptOutside;
            ptOutside.x = m_rcItem.left - 1;
            ptOutside.y = m_rcItem.top - 1;
            balloonwjui::DuiButton::OnLButtonUp(ptOutside, mkFlags);

            NotifyParent(DUIN_LISTITEM_LOCATE);
            return true;
        }
    }

    return balloonwjui::DuiButton::OnLButtonUp(pt, mkFlags);
}

void MainPanelListItem::OnPaint(HDC hdc, const RECT& /*rcDirty*/)
{
    const int x = m_rcItem.left;
    const int y = m_rcItem.top;
    const int w = m_rcItem.right  - m_rcItem.left;
    const int h = m_rcItem.bottom - m_rcItem.top;
    if (w <= 0 || h <= 0)
    {
        return;
    }

    const int avX = x + kAvatarLeft;
    const int avY = y + (h - kAvatarSize) / 2;
    const int textLeft = avX + kAvatarSize + kAvatarTextGap;

    // ---- GDI+ 形状层 ----
    {
        Gdiplus::Graphics g(hdc);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

        // 悬停底（行内缩 3px，留出圆角呼吸位）。底色取 m_hoverFill（可经
        // SetHoverFill 调整，默认 design --hover 浅灰）。
        if (IsHover())
        {
            Gdiplus::GraphicsPath hover;
            AddRoundRect(hover, (Gdiplus::REAL)(x + 3), (Gdiplus::REAL)(y + 1),
                         (Gdiplus::REAL)(w - 6), (Gdiplus::REAL)(h - 2), 6.0f);
            Gdiplus::SolidBrush hb(Gdiplus::Color(255,
                GetRValue(m_hoverFill), GetGValue(m_hoverFill), GetBValue(m_hoverFill)));
            g.FillPath(&hb, &hover);
        }

        // 头像圆角底。有真图时不画底色方块 —— 真图本身就是一张裁好的圆角方图，
        // 稍后在 GDI 层用 AlphaBlend 盖上去（同 MainPanelHeader 的处理）。
        if (m_avatarBitmap == NULL)
        {
            Gdiplus::GraphicsPath avatar;
            AddRoundRect(avatar, (Gdiplus::REAL)avX, (Gdiplus::REAL)avY,
                         (Gdiplus::REAL)kAvatarSize, (Gdiplus::REAL)kAvatarSize,
                         kAvatarRadius);
            Gdiplus::SolidBrush ab(Gdiplus::Color(255,
                GetRValue(m_avatarColor), GetGValue(m_avatarColor),
                GetBValue(m_avatarColor)));
            g.FillPath(&ab, &avatar);
        }

        // 头像状态点（离线不画）。
        if (m_showStatus && m_status != kMockOffline)
        {
            const Gdiplus::REAL dotCx = (Gdiplus::REAL)(avX + kAvatarSize) - kStatusDotSize / 2.0f - 1.0f;
            const Gdiplus::REAL dotCy = (Gdiplus::REAL)(avY + kAvatarSize) - kStatusDotSize / 2.0f - 1.0f;
            // 先垫一圈白描边，把徽标与头像本身的颜色隔开（否则深色头像上的徽标会糊进去）。
            // 白圈按徽标<u>圆底</u>的半径外扩，而不是按绘制方框 —— 方框比圆底大一圈，
            // 照方框外扩会让白圈离徽标太远、看着像单独的一个白点。
            const Gdiplus::REAL discR = kStatusDotSize * kBadgeDiscRatio / 2.0f;
            const Gdiplus::REAL ringR = discR + kStatusRingW;
            Gdiplus::SolidBrush ring(kStatusRing);
            g.FillEllipse(&ring, dotCx - ringR, dotCy - ringR, ringR * 2.0f, ringR * 2.0f);

            // 徽标本体：字形与配色都取自共用映射，与通讯录列表里同一个人身上的那枚一致。
            DrawPresenceBadge(g, MockPresenceFromStatus(m_status), false,
                              dotCx - kStatusDotSize / 2.0f,
                              dotCy - kStatusDotSize / 2.0f,
                              kStatusDotSize);
        }

        // 未读角标。
        if (m_badgeCount > 0)
        {
            const Gdiplus::REAL bx = (Gdiplus::REAL)(x + w - kRowRightPad) - kBadgeDiameter;
            const Gdiplus::REAL by = (Gdiplus::REAL)y + (Gdiplus::REAL)(h) / 2.0f - kBadgeDiameter / 2.0f;
            Gdiplus::SolidBrush bb(kBadgeRed);
            g.FillEllipse(&bb, bx, by, kBadgeDiameter, kBadgeDiameter);
        }
    }

    // ---- GDI 位图层：真实头像 ----
    //
    // 真图盖在圆角底的位置上（底色方块此时没画）。位图本身已按行内头像边长裁成
    // 圆角方形，故这里等尺寸贴上即可；尺寸对不上时按位图实际大小拉伸，不裁切。
    if (m_avatarBitmap != NULL)
    {
        BITMAP bm = {};
        ::GetObject(m_avatarBitmap, sizeof(bm), &bm);
        if (bm.bmWidth > 0 && bm.bmHeight > 0)
        {
            HDC     mem = ::CreateCompatibleDC(hdc);
            HGDIOBJ old = ::SelectObject(mem, m_avatarBitmap);
            BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
            ::AlphaBlend(hdc, avX, avY, kAvatarSize, kAvatarSize,
                         mem, 0, 0, bm.bmWidth, bm.bmHeight, bf);
            ::SelectObject(mem, old);
            ::DeleteDC(mem);
        }
    }

    // ---- GDI 文字层 ----

    // 头像首字 / emoji（有真图时不画 —— 那张图就是头像本身）。
    if (m_avatarBitmap == NULL && !m_avatarText.IsEmpty())
    {
        RECT avRc = { avX, avY, avX + kAvatarSize, avY + kAvatarSize };
        DrawGdiText(hdc, (LPCWSTR)m_avatarText, avRc, 16, true,
                    AvatarTextColor(m_avatarColor),
                    DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);
    }

    // 名称 + 副行（整体垂直居中）。
    const bool hasSub = !m_subText.IsEmpty();
    const int  blockH = hasSub ? 33 : 18;
    const int  blockTop = y + (h - blockH) / 2;

    // 第一行右侧文字（时间 / 群成员数）先量宽 —— 它与名称同处第一行，名称的右界要
    // 为它让位，否则长名称会一直画到它底下、两段字叠在一起。
    int rightTextW = 0;
    if (!m_rightText.IsEmpty())
    {
        rightTextW = MeasureGdiTextWidth(hdc, (LPCWSTR)m_rightText, kRightPx, false);
        if (rightTextW <= 0)
        {
            rightTextW = kRightTextFallbackW;
        }
    }

    // 名称 / 副行右界：默认留 kNameRightGap；有未读角标时再让出角标宽度，避免长文字
    // 钻到角标下面（角标垂直居中靠右绘制）。无角标的行（如搜索结果）右界不变。
    int textRight = x + w - kRowRightPad - kNameRightGap;
    if (m_badgeCount > 0)
    {
        textRight = x + w - kRowRightPad - (int)kBadgeDiameter - kBadgeTextGap;
    }

    // 名称还要再为右侧文字让位。副行（第二行）不受影响 —— 它在右侧文字的下一行，
    // 底下那一整条横向空间都归它，群号这类短文字更没必要跟着缩。
    int nameRight = textRight;
    if (rightTextW > 0)
    {
        const int limit = x + w - kRowRightPad - rightTextW - kRightTextGap;
        if (limit < nameRight)
        {
            nameRight = limit;
        }
    }

    RECT nameRc = { textLeft, blockTop, nameRight, blockTop + 18 };

    // 搜索命中高亮：在名称对应子串处画黄底（用与名称完全相同的字体测量子串
    // 的左偏移与宽度，保证高亮框与文字对齐），再照常画名称文字盖在其上。
    if (m_nameHiStart >= 0 && m_nameHiLen > 0 && m_nameHiStart < m_name.GetLength())
    {
        int hiStart = m_nameHiStart;
        int hiLen   = m_nameHiLen;
        if (hiStart + hiLen > m_name.GetLength())
        {
            hiLen = m_name.GetLength() - hiStart;
        }

        HFONT hMeasure = ::CreateFontW(
            -kNamePx, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");
        if (hMeasure != NULL)
        {
            HFONT hOld = (HFONT)::SelectObject(hdc, hMeasure);
            SIZE szPre = { 0, 0 };
            SIZE szHi  = { 0, 0 };
            ::GetTextExtentPoint32W(hdc, (LPCWSTR)m_name, hiStart, &szPre);
            ::GetTextExtentPoint32W(hdc, ((LPCWSTR)m_name) + hiStart, hiLen, &szHi);
            ::SelectObject(hdc, hOld);
            ::DeleteObject(hMeasure);

            int hiLeft  = textLeft + szPre.cx;
            int hiRight = hiLeft + szHi.cx;
            if (hiRight > nameRight)
            {
                hiRight = nameRight;   // 不越过名称区右界（与省略号区一致）
            }
            if (hiRight > hiLeft)
            {
                RECT hl = { hiLeft, blockTop + 1, hiRight, blockTop + 17 };
                HBRUSH hb = ::CreateSolidBrush(kHighlightFill);
                ::FillRect(hdc, &hl, hb);
                ::DeleteObject(hb);
            }
        }
    }

    // 名称用常规字重（非半粗）—— 半粗 + 深色会显得过黑过重。
    DrawGdiText(hdc, (LPCWSTR)m_name, nameRc, kNamePx, false, kNameColor,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    if (hasSub)
    {
        RECT subRc = { textLeft, blockTop + 17, textRight, blockTop + 33 };
        DrawGdiText(hdc, (LPCWSTR)m_subText, subRc, kSubPx, false, kSubColor,
                    DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }

    // 第一行右侧文字（会话行的时间 / 群行的成员数）。两行的行里与名称同高 —— 名称在上、
    // 副行在下时，右侧文字要和名称处在同一条水平线上才不显得飘在两行中间的缝里；单行的
    // 行没有第二行作参照，沿用贴着行右上角的老位置。
    if (!m_rightText.IsEmpty())
    {
        RECT rightRc;
        rightRc.right = x + w - kRowRightPad;
        rightRc.left  = rightRc.right - rightTextW;
        if (hasSub)
        {
            rightRc.top    = nameRc.top;
            rightRc.bottom = nameRc.bottom;
        }
        else
        {
            rightRc.top    = y + kRightTextTop;
            rightRc.bottom = rightRc.top + kRightTextHeight;
        }
        DrawGdiText(hdc, (LPCWSTR)m_rightText, rightRc, kRightPx, false, kTimeColor,
                    DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);
    }

    // 角标数字。
    if (m_badgeCount > 0)
    {
        CString badgeStr;
        badgeStr.Format(_T("%d"), m_badgeCount);
        const int bx = x + w - kRowRightPad - (int)kBadgeDiameter;
        const int by = y + h / 2 - (int)kBadgeDiameter / 2;
        RECT badgeRc = { bx, by, bx + (int)kBadgeDiameter, by + (int)kBadgeDiameter };
        DrawGdiText(hdc, (LPCWSTR)badgeStr, badgeRc, 10, true, RGB(255, 255, 255),
                    DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);
    }

    // ---- 悬停态「定位」小按钮 ----
    //
    // 画在最后，浮在行内其它元素之上。只有本行打开了该按钮、且鼠标此刻正悬停在本行
    // 内时才画：其余时候完全不绘制、也不占位（名称与副行的右界不因它而收缩 —— 打开
    // 该按钮的搜索结果联系人行既没有未读角标也没有右侧文字，行右侧本就是空白）。
    if (m_showLocate && IsHover())
    {
        RECT rcIcon = LocateIconRect();
        if (rcIcon.right > rcIcon.left)
        {
            Gdiplus::Graphics gi(hdc);
            gi.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

            // 鼠标正落在图标上：先垫一层浅色圆底作为可点击反馈。
            if (m_locateHot)
            {
                const Gdiplus::REAL hotR = (Gdiplus::REAL)kLocateIconSize / 2.0f
                                           + (Gdiplus::REAL)kLocateHotPad;
                const Gdiplus::REAL hotCx = (Gdiplus::REAL)(rcIcon.left + rcIcon.right) / 2.0f;
                const Gdiplus::REAL hotCy = (Gdiplus::REAL)(rcIcon.top + rcIcon.bottom) / 2.0f;
                Gdiplus::SolidBrush hotBrush(kLocateHotFill);
                gi.FillEllipse(&hotBrush, hotCx - hotR, hotCy - hotR,
                               hotR * 2.0f, hotR * 2.0f);
            }

            // 图标本体。鼠标落在它上面时画深一档，与浅色圆底一起构成"这里可以点"的提示。
            const COLORREF pinColor = m_locateHot ? kLocateIconHotColor : kLocateIconColor;
            Gdiplus::Color pinArgb(255, GetRValue(pinColor), GetGValue(pinColor),
                                   GetBValue(pinColor));
            DrawLocatePin(gi, (Gdiplus::REAL)rcIcon.left, (Gdiplus::REAL)rcIcon.top,
                          (Gdiplus::REAL)kLocateIconSize, pinArgb);
        }
    }
}

} // namespace balloonmain
