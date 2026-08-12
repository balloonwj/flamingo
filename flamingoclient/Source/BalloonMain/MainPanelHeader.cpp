/**
 *  MainPanelHeader.cpp
 *
 *  主面板渐变 Banner 的绘制实现。形状（渐变 / 头像 / 状态胶囊 / 通知盒）
 *  用 GDI+ 抗锯齿；文字用 GDI DrawText（系统 ClearType 光栅化，清晰
 *  锐利，避免 GDI+ DrawString 发虚）。两层不交叉：先在作用域内用 GDI+
 *  画完形状（Graphics 析构 flush），再用 GDI 画文字。
 *
 *  本 .cpp 故意<u>不</u> include Flamingo 的 stdafx.h —— 它要被
 *  Flamingo.vcxproj 与 FlamingoUnit.vcxproj 共用，故自带 ATL/WTL 前置
 *  依赖。在 Flamingo.vcxproj 里本文件需标记 PrecompiledHeader=NotUsing。
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
#pragma comment(lib, "msimg32.lib")   // AlphaBlend（画真实头像位图）

#include "MainPanelHeader.h"
#include "MainPanelMockData.h"
#include "MainPanelStatusMenu.h"    // GetPresenceVisual（状态胶囊文字/圆点色映射）
#include "PresenceBadge.h"          // DrawPresenceBadge（状态胶囊上的状态徽标）
#include "OnlineCapability.h"       // GetConnectionPresenceVisual / GetConnectionBadgePresence /
                                    // ShouldGrayAvatar（按连接态渲染）
#include "AvatarGray.h"             // MakeGrayColor / MakeGrayAvatarBitmap（离线头像置灰）
#include "../net/Protocol.h"        // presence_type（状态默认值 presence_online）

namespace balloonmain {

namespace {

// ---- 配色（取自 design main-panel.jsx 的 Banner 区）--------------------

// 渐变三段色：橙 #FF7A45 → 粉 #FF4D8D → 紫 #6E5BE5（135° 线性渐变）。
const Gdiplus::Color kGradStop0(255, 0xFF, 0x7A, 0x45);
const Gdiplus::Color kGradStop1(255, 0xFF, 0x4D, 0x8D);
const Gdiplus::Color kGradStop2(255, 0x6E, 0x5B, 0xE5);

// 头像外圈半透明白底（design rgba(255,255,255,.25)）。
const Gdiplus::Color kAvatarRing(64, 255, 255, 255);
// 状态胶囊半透明白底 / 描边（design bg .18 / border .3）。
const Gdiplus::Color kPillFill(46, 255, 255, 255);
const Gdiplus::Color kPillBorder(77, 255, 255, 255);
// 通知盒半透明黑底（design rgba(0,0,0,.15)）。
const Gdiplus::Color kBellBoxFill(38, 0, 0, 0);
// 状态胶囊圆点颜色不再写死（原 kStatusGreen 已移除）：改由 GetPresenceVisual 按当前
// 在线状态返回（绿/橙/红/灰），见 MainPanelStatusMenu.h。
// 未读角标：黄底 #FFE066 + 深棕字 #5A3700。
const Gdiplus::Color kBadgeFill(255, 0xFF, 0xE0, 0x66);

// 文字色。
const COLORREF kNameColor   = RGB(255, 255, 255);          // 昵称：纯白
const COLORREF kPillText    = RGB(255, 255, 255);          // 状态胶囊文字：白
const COLORREF kSoftWhite   = RGB(255, 0xEC, 0xDF);        // 签名 / 通知：暖白
const COLORREF kSignHint    = RGB(255, 0xD2, 0xBE);        // 签名为空时的占位提示：更淡的暖白
const COLORREF kBadgeText   = RGB(0x5A, 0x37, 0x00);       // 角标文字：深棕
const COLORREF kInitialText = RGB(255, 255, 255);          // 头像字母：白

// ---- 几何常量（逻辑像素，对照 design 的 padding:14/16/10、Banner 高 108）

const int kPadLeft       = 16;   // 左内距
const int kPadTop        = 14;   // 上内距
const int kAvatarSize    = 50;   // 头像边长
const int kAvatarRingPad = 2;    // 头像外圈留白
const int kAvatarTextGap = 12;   // 头像与右侧文字列的间距
const int kBellBoxSize   = 24;   // 通知盒边长
const int kBellBoxBottom = 10;   // 通知盒距 Banner 底边

const Gdiplus::REAL kAvatarRadius = 6.0f;   // 头像圆角
const Gdiplus::REAL kRingRadius   = 8.0f;   // 头像外圈圆角
const Gdiplus::REAL kBellRadius   = 6.0f;   // 通知盒圆角

// 状态胶囊右侧的下拉箭头（"˅"），提示胶囊可点击弹出状态选择菜单。
const int           kPillChevronW       = 7;     // 箭头宽（像素）
const int           kPillChevronH       = 4;     // 箭头高（像素）
const int           kPillTextChevronGap = 5;     // 胶囊文字与下拉箭头之间的间距（像素）
const Gdiplus::REAL kPillChevronPenW    = 1.4f;  // 箭头描边宽（像素）

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

// 用 GDI（而非 GDI+）绘制一段文字 —— DrawText 经系统 ClearType 光栅化，
// 清晰锐利，避免 GDI+ DrawString 发虚。
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

// 测量一段文字的像素宽度（用于状态胶囊按文字宽度自适应）。
int MeasureTextWidth(HDC hdc, LPCWSTR text, int pixelHeight, bool bold)
{
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
    ::GetTextExtentPoint32W(hdc, text, (int)::wcslen(text), &sz);
    ::SelectObject(hdc, hOldFont);
    ::DeleteObject(hFont);
    return sz.cx;
}

} // anonymous namespace

MainPanelHeader::MainPanelHeader()
    : m_hasUserData(false)
    , m_avatarColor(RGB(0xFF, 0x7A, 0x45))
    , m_avatarBitmap(NULL)
    , m_avatarBitmapGray(NULL)
    , m_unreadTotal(0)
    , m_announceUnread(0)           // 未读公告数默认 0：登录后由主面板从本地公告库注入
    , m_presence(presence_online)   // 默认在线（未注入数据时胶囊显示"在线"绿点）
    , m_connUiState(kConnUiOnline)  // 默认在线态（设计期 Mock 渲染不该一上来就是灰的）
{
    ::SetRectEmpty(&m_rcAvatarHit);
    ::SetRectEmpty(&m_rcNameHit);
    ::SetRectEmpty(&m_rcSignHit);
    ::SetRectEmpty(&m_rcStatusHit);
    ::SetRectEmpty(&m_rcBellHit);
}

MainPanelHeader::~MainPanelHeader()
{
    if (m_avatarBitmap != NULL)
    {
        ::DeleteObject(m_avatarBitmap);
        m_avatarBitmap = NULL;
    }
    if (m_avatarBitmapGray != NULL)
    {
        ::DeleteObject(m_avatarBitmapGray);
        m_avatarBitmapGray = NULL;
    }
}

void MainPanelHeader::SetAvatarBitmap(HBITMAP hbm)
{
    if (m_avatarBitmap == hbm)
    {
        return;
    }
    if (m_avatarBitmap != NULL)
    {
        ::DeleteObject(m_avatarBitmap);
    }
    m_avatarBitmap = hbm;   // 接管所有权

    // 换了头像，旧的灰度副本就作废了；下次需要置灰时再按新图重新生成。
    if (m_avatarBitmapGray != NULL)
    {
        ::DeleteObject(m_avatarBitmapGray);
        m_avatarBitmapGray = NULL;
    }

    Invalidate();
}

void MainPanelHeader::SetUserData(const CString& nickname,
                                  const CString& account,
                                  const CString& signature,
                                  const CString& avatarText,
                                  COLORREF       avatarColor,
                                  int            unreadTotal,
                                  int            presence)
{
    m_hasUserData = true;
    m_nickname    = nickname;
    m_account     = account;
    m_signature   = signature;
    m_avatarText  = avatarText;
    m_avatarColor = avatarColor;
    m_unreadTotal = unreadTotal;
    m_presence    = presence;
}

bool MainPanelHeader::OnLButtonUp(POINT pt, UINT /*mkFlags*/)
{
    // 在抬起时分派（与 DuiControl 点击语义一致）。命中矩形均由 OnPaint 按实际绘制
    // 位置缓存。判定顺序：先状态胶囊，再签名，最后头像/昵称（胶囊套在昵称下方、与
    // 头像/昵称命中区不重叠，先判它即可）。

    // 命中状态胶囊：发 DUIN_HEADER_STATUS_CLICK，触发"弹出在线状态选择菜单"。
    if (::PtInRect(&m_rcStatusHit, pt))
    {
        NotifyParent(DUIN_HEADER_STATUS_CLICK);
        return true;
    }

    // 命中签名区：发 DUIN_HEADER_SIGN_CLICK，触发"个性签名就地编辑"。
    if (::PtInRect(&m_rcSignHit, pt))
    {
        NotifyParent(DUIN_HEADER_SIGN_CLICK);
        return true;
    }

    // 命中头像格：发 DUIN_HEADER_AVATAR_CLICK，路由到 OpenModifyAvatarDlg（改头像）。
    if (::PtInRect(&m_rcAvatarHit, pt))
    {
        NotifyParent(DUIN_HEADER_AVATAR_CLICK);
        return true;
    }

    // 命中昵称：发 DUIN_CLICK，CBalloonMainPanel::OnDuiNotify 按 ctrlId(kIdHeader)
    // 路由到 OpenProfileDlg（打开个人信息窗）。头像→改头像、昵称→看资料，二者分开。
    if (::PtInRect(&m_rcNameHit, pt))
    {
        NotifyParent(balloonwjui::DUIN_CLICK);
        return true;
    }

    // 命中底部"消息盒子"行（铃铛盒 + 文字 + 角标）：发 DUIN_HEADER_BELL_CLICK，
    // 路由到"打开消息盒子窗口"。放在最后判：这一行在 Banner 底部，与上面几块不重叠。
    if (::PtInRect(&m_rcBellHit, pt))
    {
        NotifyParent(DUIN_HEADER_BELL_CLICK);
        return true;
    }

    // 空白渐变区：不触发，留给窗口拖动。
    return false;
}

void MainPanelHeader::OnPaint(HDC hdc, const RECT& /*rcDirty*/)
{
    const int left = m_rcItem.left;
    const int top  = m_rcItem.top;
    const int w    = m_rcItem.right  - m_rcItem.left;
    const int h    = m_rcItem.bottom - m_rcItem.top;
    if (w <= 0 || h <= 0)
    {
        return;
    }

    // 数据来源:已注入则用注入值,否则回退 Mock。回退时仍引用 MockMe,以保持
    // 单元测试 / 设计期截图核对场景与原行为一致。
    const balloonmain::MockContact& me = balloonmain::MockMe();
    const CString  effName       = m_hasUserData
                                      ? (m_nickname.IsEmpty() ? m_account : m_nickname)
                                      : me.name;
    const CString  effSignature  = m_hasUserData ? m_signature  : me.signature;
    const CString  effAvatarText = m_hasUserData ? m_avatarText : me.avatarText;
    const COLORREF effAvatarClr  = m_hasUserData ? m_avatarColor : me.avatarColor;
    // 底部那一行显示的是<u>未读公告</u>数，不是会话未读数：这一行就是「消息盒子」的入口，
    // 而消息盒子装的是平台公告（见 docs/消息盒子接入方案.md）。注入模式下取
    // SetAnnounceUnread 推进来的值；Mock 模式（设计期截图核对 / 单元测试）仍用 mock 值，
    // 好让 --mainpanel 调试入口能看到带角标的样子。
    const int      effUnread     = m_hasUserData ? m_announceUnread
                                                  : balloonmain::MockUnreadTotal();
    // 在线状态：已注入用 m_presence，否则按"在线"渲染（与 Mock 场景一致）。
    const int      effPresence   = m_hasUserData ? m_presence : presence_online;

    // 头像是否置灰：口径与托盘图标同源（见 OnlineCapability.h 的 ShouldGrayAvatar）。
    // 掉线重连中与用户主动离线都置灰，只有真正在线才是彩色。
    const bool     bGrayAvatar   = balloonmain::ShouldGrayAvatar(m_connUiState);
    const COLORREF drawAvatarClr = bGrayAvatar ? balloonmain::MakeGrayColor(effAvatarClr)
                                               : effAvatarClr;

    // ---- 几何量（形状层与文字层共用）----
    const int ringX   = left + kPadLeft;
    const int ringY   = top + kPadTop;
    const int ringSz  = kAvatarSize + kAvatarRingPad * 2;
    const int avX     = ringX + kAvatarRingPad;
    const int avY     = ringY + kAvatarRingPad;
    const int textX   = ringX + ringSz + kAvatarTextGap;   // 右侧文字列左边界

    const int nameY   = top + 12;                          // 昵称行顶
    const int pillY   = nameY + 22;                        // 状态胶囊顶
    const int pillH   = 18;
    const int signY   = pillY + pillH + 5;                 // 签名行顶

    const int bellY   = top + h - kBellBoxBottom - kBellBoxSize;  // 通知盒顶
    const int bellX   = left + kPadLeft;

    // 状态胶囊文字 + 圆点色。这里走的是<u>连接态</u>而不是只看 effPresence：掉线时
    // 用户选的仍是"在线"，照它渲染就会出现"托盘图标已经灰了、胶囊还写着在线"
    // （见 OnlineCapability.h 的 GetConnectionPresenceVisual）。
    const balloonmain::PresenceVisual pillVisual =
        balloonmain::GetConnectionPresenceVisual(effPresence, m_connUiState);
    const wchar_t* pillStatusText = pillVisual.text;
    const int pillTextPx = 12;
    int pillTextW = MeasureTextWidth(hdc, pillStatusText, pillTextPx, false);
    // 状态徽标的绘制方框边长。徽标圆底只占方框的 20/24，故 9 对应约 7.5px 的圆点
    // （改版前是 6px 的纯色圆点）。之所以要放大：6px 下时钟指针与横杠糊成一团，
    // 看不出字形，这一枚就与列表 / 菜单里的徽标不像同一套东西了。
    const int pillDotD = 9;
    // 左padding(9) + 点 + 点字间距(5) + 字 + 文字与箭头间距 + 下拉箭头 + 右padding(9)
    const int pillW = 9 + pillDotD + 5 + pillTextW
                      + kPillTextChevronGap + kPillChevronW + 9;
    // 下拉箭头左缘 X（host 客户区坐标），文字之后。
    const int pillChevronX = textX + 9 + pillDotD + 5 + pillTextW + kPillTextChevronGap;

    // ====================================================================
    // 第一层：GDI+ 形状
    // ====================================================================
    {
        Gdiplus::Graphics g(hdc);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

        // 1. 橙→粉→紫 三段线性渐变背景（135°，约等于 GDI+ 45°）。
        Gdiplus::RectF bgRc((Gdiplus::REAL)left, (Gdiplus::REAL)top,
                            (Gdiplus::REAL)w, (Gdiplus::REAL)h);
        Gdiplus::LinearGradientBrush gradient(
            bgRc, kGradStop0, kGradStop2, 45.0f);
        Gdiplus::Color gradColors[3] = { kGradStop0, kGradStop1, kGradStop2 };
        Gdiplus::REAL  gradPos[3]    = { 0.0f, 0.5f, 1.0f };
        gradient.SetInterpolationColors(gradColors, gradPos, 3);
        g.FillRectangle(&gradient, bgRc);

        // 2. 头像外圈（半透明白）+ 头像圆角底。
        Gdiplus::GraphicsPath ringPath;
        AddRoundRectPath(ringPath, (Gdiplus::REAL)ringX, (Gdiplus::REAL)ringY,
                         (Gdiplus::REAL)ringSz, (Gdiplus::REAL)ringSz, kRingRadius);
        Gdiplus::SolidBrush ringBrush(kAvatarRing);
        g.FillPath(&ringBrush, &ringPath);

        // 有真实头像位图时不画底色方块（稍后用 AlphaBlend 画圆角真图）；否则画底色。
        if (m_avatarBitmap == NULL)
        {
            Gdiplus::GraphicsPath avatarPath;
            AddRoundRectPath(avatarPath, (Gdiplus::REAL)avX, (Gdiplus::REAL)avY,
                             (Gdiplus::REAL)kAvatarSize, (Gdiplus::REAL)kAvatarSize,
                             kAvatarRadius);
            Gdiplus::SolidBrush avatarBrush(Gdiplus::Color(255,
                GetRValue(drawAvatarClr), GetGValue(drawAvatarClr),
                GetBValue(drawAvatarClr)));
            g.FillPath(&avatarBrush, &avatarPath);
        }

        // 3. 状态胶囊底 + 描边 + 状态点（颜色随在线状态）+ 右侧下拉箭头。
        Gdiplus::GraphicsPath pillPath;
        AddRoundRectPath(pillPath, (Gdiplus::REAL)textX, (Gdiplus::REAL)pillY,
                         (Gdiplus::REAL)pillW, (Gdiplus::REAL)pillH,
                         (Gdiplus::REAL)pillH / 2.0f);
        Gdiplus::SolidBrush pillBrush(kPillFill);
        g.FillPath(&pillBrush, &pillPath);
        Gdiplus::Pen pillPen(kPillBorder, 1.0f);
        g.DrawPath(&pillPen, &pillPath);

        // 状态徽标：字形与颜色都由"该画哪个状态"决定，与列表、菜单里的徽标同源。
        // 注意取的是 GetConnectionBadgePresence 而非 m_presence —— 掉线重连期间用户
        // 选的仍是"在线"，但胶囊文字已经是"正在连接"、点是灰的，形状必须跟着走灰环，
        // 否则会出现"灰点画成在线实心圆"的矛盾。
        balloonmain::DrawPresenceBadge(
            g,
            balloonmain::GetConnectionBadgePresence(effPresence, m_connUiState),
            false,
            (Gdiplus::REAL)(textX + 9),
            (Gdiplus::REAL)(pillY + (pillH - pillDotD) / 2),
            (Gdiplus::REAL)pillDotD);

        // 下拉箭头 "˅"：两段短线在文字右侧画一个朝下的折线，颜色同胶囊文字（白）。
        const Gdiplus::REAL chevCx = (Gdiplus::REAL)pillChevronX + kPillChevronW / 2.0f;
        const Gdiplus::REAL chevTop = (Gdiplus::REAL)(pillY + (pillH - kPillChevronH) / 2);
        Gdiplus::Pen chevPen(Gdiplus::Color(255,
            GetRValue(kPillText), GetGValue(kPillText), GetBValue(kPillText)),
            kPillChevronPenW);
        chevPen.SetStartCap(Gdiplus::LineCapRound);
        chevPen.SetEndCap(Gdiplus::LineCapRound);
        chevPen.SetLineJoin(Gdiplus::LineJoinRound);
        g.DrawLine(&chevPen,
                   (Gdiplus::REAL)pillChevronX, chevTop,
                   chevCx, chevTop + kPillChevronH);
        g.DrawLine(&chevPen,
                   chevCx, chevTop + kPillChevronH,
                   (Gdiplus::REAL)(pillChevronX + kPillChevronW), chevTop);

        // 4. 通知盒（半透明黑圆角）+ 铃铛 + 未读角标。
        Gdiplus::GraphicsPath bellBoxPath;
        AddRoundRectPath(bellBoxPath, (Gdiplus::REAL)bellX, (Gdiplus::REAL)bellY,
                         (Gdiplus::REAL)kBellBoxSize, (Gdiplus::REAL)kBellBoxSize,
                         kBellRadius);
        Gdiplus::SolidBrush bellBoxBrush(kBellBoxFill);
        g.FillPath(&bellBoxBrush, &bellBoxPath);

        // 铃铛图形 —— 取自 design icons.jsx 的 bell（viewBox 24），白色填充。
        {
            Gdiplus::GraphicsState st = g.Save();
            const Gdiplus::REAL bellIcon = 14.0f;   // 图标显示尺寸
            g.TranslateTransform(
                (Gdiplus::REAL)bellX + (kBellBoxSize - bellIcon) / 2.0f,
                (Gdiplus::REAL)bellY + (kBellBoxSize - bellIcon) / 2.0f);
            g.ScaleTransform(bellIcon / 24.0f, bellIcon / 24.0f);

            Gdiplus::GraphicsPath bell;
            bell.AddLine(6.0f, 16.0f, 6.0f, 11.0f);
            bell.AddArc(6.0f, 5.0f, 12.0f, 12.0f, 180.0f, 180.0f);  // 顶部半圆穹顶
            bell.AddLine(18.0f, 11.0f, 18.0f, 16.0f);
            bell.AddLine(18.0f, 16.0f, 19.5f, 18.0f);
            bell.AddLine(19.5f, 18.0f, 4.5f, 18.0f);
            bell.CloseFigure();
            Gdiplus::SolidBrush bellBrush(Gdiplus::Color(255, 255, 255, 255));
            g.FillPath(&bellBrush, &bell);
            // 铃舌：底部小半圆。
            Gdiplus::GraphicsPath clapper;
            clapper.AddArc(10.0f, 18.0f, 4.0f, 4.0f, 0.0f, 180.0f);
            g.FillPath(&bellBrush, &clapper);
            g.Restore(st);
        }

        // 未读角标（黄底，叠在通知盒右上角）。
        const int unread = effUnread;
        if (unread > 0)
        {
            const Gdiplus::REAL badgeD = 15.0f;
            const Gdiplus::REAL badgeX = (Gdiplus::REAL)(bellX + kBellBoxSize) - badgeD + 3.0f;
            const Gdiplus::REAL badgeY = (Gdiplus::REAL)bellY - 3.0f;
            Gdiplus::SolidBrush badgeBrush(kBadgeFill);
            g.FillEllipse(&badgeBrush, badgeX, badgeY, badgeD, badgeD);
            Gdiplus::Pen badgeBorder(Gdiplus::Color(255, 255, 255, 255), 1.5f);
            g.DrawEllipse(&badgeBorder, badgeX, badgeY, badgeD, badgeD);
        }
    }

    // 有真实头像位图：在 GDI+ 块结束后用 AlphaBlend 画圆角真图（位图自带圆角 + 角外
    // 透明，方向正确，与联系人树头像同款画法）。画了真图就不再画字形。
    if (m_avatarBitmap != NULL)
    {
        // 离线 / 重连中要画灰度副本。副本按需生成一次后缓存起来 —— 整幅位图逐像素
        // 灰化不该每次重绘都做一遍；换头像时 SetAvatarBitmap 会把缓存作废。
        // 生成失败（非 32bpp、内存不足等）时 hbmDraw 退回原图，宁可没灰掉也别画空白。
        HBITMAP hbmDraw = m_avatarBitmap;
        if (bGrayAvatar)
        {
            if (m_avatarBitmapGray == NULL)
            {
                m_avatarBitmapGray = balloonmain::MakeGrayAvatarBitmap(m_avatarBitmap);
            }
            if (m_avatarBitmapGray != NULL)
            {
                hbmDraw = m_avatarBitmapGray;
            }
        }

        BITMAP bm = {};
        ::GetObject(hbmDraw, sizeof(bm), &bm);
        if (bm.bmWidth > 0 && bm.bmHeight > 0)
        {
            HDC     mem = ::CreateCompatibleDC(hdc);
            HGDIOBJ old = ::SelectObject(mem, hbmDraw);
            BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
            ::AlphaBlend(hdc, avX, avY, kAvatarSize, kAvatarSize,
                         mem, 0, 0, bm.bmWidth, bm.bmHeight, bf);
            ::SelectObject(mem, old);
            ::DeleteDC(mem);
        }
    }

    // ====================================================================
    // 第二层：GDI 文字（清晰锐利）
    // ====================================================================

    // 头像首字母（仅在没有真实头像位图时画）。
    if (m_avatarBitmap == NULL && !effAvatarText.IsEmpty())
    {
        RECT avRc = { avX, avY, avX + kAvatarSize, avY + kAvatarSize };
        DrawGdiText(hdc, (LPCWSTR)effAvatarText, avRc, 23, true, kInitialText,
                    DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);
    }

    // 昵称。
    RECT nameRc = { textX, nameY, left + w - 8, nameY + 20 };
    DrawGdiText(hdc, (LPCWSTR)effName, nameRc, 15, true, kNameColor,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);

    // 状态胶囊文字（状态点右侧、下拉箭头左侧）。
    RECT pillTextRc = { textX + 9 + pillDotD + 5, pillY,
                        textX + 9 + pillDotD + 5 + pillTextW, pillY + pillH };
    DrawGdiText(hdc, pillStatusText, pillTextRc, pillTextPx, false, kPillText,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);

    // 个性签名（空时画浅色占位提示，引导点击就地添加；占位与真签名都可点）。
    const bool    signIsHint = effSignature.IsEmpty();
    const CString signDisplay = signIsHint ? CString(_T("点击添加个性签名")) : effSignature;
    {
        RECT sigRc = { textX, signY, left + w - 12, signY + 18 };
        DrawGdiText(hdc, (LPCWSTR)signDisplay, sigRc, 12, false,
                    signIsHint ? kSignHint : kSoftWhite,
                    DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }

    // ---- 缓存"头像 / 昵称 / 签名 / 状态胶囊"命中矩形（供 OnLButtonUp 判定）----
    // 头像：取外圈矩形（视觉上的可点头像）。
    ::SetRect(&m_rcAvatarHit, ringX, ringY, ringX + ringSz, ringY + ringSz);
    // 状态胶囊：整颗胶囊（含点 + 文字 + 下拉箭头）都可点，点击弹出状态选择菜单。
    ::SetRect(&m_rcStatusHit, textX, pillY, textX + pillW, pillY + pillH);
    // 昵称：按实测文字宽度框定（避免点到右侧空白渐变区也触发）。
    {
        int nameW = MeasureTextWidth(hdc, (LPCWSTR)effName, 15, true);
        ::SetRect(&m_rcNameHit, textX, nameY, textX + nameW, nameY + 20);
    }
    // 签名命中矩形：真签名与占位提示都可点（占位点击 = 就地添加签名），按实测
    // 文字宽度框定，并不超过绘制右界。
    {
        int signW = MeasureTextWidth(hdc, (LPCWSTR)signDisplay, 12, false);
        int signRight = textX + signW;
        if (signRight > left + w - 12)
        {
            signRight = left + w - 12;
        }
        ::SetRect(&m_rcSignHit, textX, signY, signRight, signY + 18);
    }

    // 底部通知行文字。
    const int unread = effUnread;
    CString notice;
    if (unread > 0)
    {
        notice.Format(_T("你有 %d 条新公告"), unread);
    }
    else
    {
        notice = _T("消息盒子");
    }
    RECT noticeRc = { bellX + kBellBoxSize + 8, bellY,
                      left + w - 8, bellY + kBellBoxSize };
    DrawGdiText(hdc, (LPCWSTR)notice, noticeRc, 12, false, kSoftWhite,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);

    // 角标数字。
    RECT badgeRc = { bellX + kBellBoxSize - 12, bellY - 3,
                     bellX + kBellBoxSize + 3, bellY + 12 };
    if (unread > 0)
    {
        CString badgeStr;
        badgeStr.Format(_T("%d"), unread);
        DrawGdiText(hdc, (LPCWSTR)badgeStr, badgeRc, 9, true, kBadgeText,
                    DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);
    }

    // ---- 缓存"消息盒子"行的命中矩形（供窗口拖动区把这一行让开）----
    // 通知盒本身是定宽方块，右侧文字按实测宽度框定 —— 若直接取 noticeRc 的右界
    // （一路顶到 Banner 右边距），文字之后那一大片空白渐变区就白白被划走、按住
    // 拖不动窗口了。角标叠在通知盒右上角、会溢出方块 3 个像素，一并并进来。
    {
        int noticeW = MeasureTextWidth(hdc, (LPCWSTR)notice, 12, false);
        int noticeRight = noticeRc.left + noticeW;
        if (noticeRight > noticeRc.right)
        {
            noticeRight = noticeRc.right;
        }
        ::SetRect(&m_rcBellHit, bellX, bellY, noticeRight, bellY + kBellBoxSize);
        if (unread > 0)
        {
            ::UnionRect(&m_rcBellHit, &m_rcBellHit, &badgeRc);
        }
    }
}

} // namespace balloonmain
