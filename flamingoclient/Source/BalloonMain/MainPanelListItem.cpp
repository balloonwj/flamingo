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
#include "MainPanelScrollView.h"

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

bool MainPanelListItem::OnMouseWheel(POINT pt, short zDelta, UINT mkFlags)
{
    return ForwardWheelToScrollView(this, pt, zDelta, mkFlags);
}

bool MainPanelListItem::OnMouseEnter()
{
    bool r = balloonwjui::DuiButton::OnMouseEnter();   // 基类置 m_bHover=true 并通知父
    Invalidate();                                      // 基类不重绘，这里补一次让 hover 底色立刻显示
    return r;
}

bool MainPanelListItem::OnMouseLeave()
{
    bool r = balloonwjui::DuiButton::OnMouseLeave();   // 基类置 m_bHover=false 并通知父
    Invalidate();                                      // 补一次让 hover 底色立刻消失
    return r;
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
}

} // namespace balloonmain
