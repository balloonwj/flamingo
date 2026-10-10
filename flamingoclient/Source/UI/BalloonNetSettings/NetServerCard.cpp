/**
 *  NetServerCard.cpp
 *
 *  服务器卡片的自绘实现。圆角卡片底 / 图标用 GDI+ 抗锯齿，文字用 GDI
 *  DrawText（清晰）。图片 / 文件图标复用 ChatIcons 的线性图标，聊天图标
 *  （气泡）就地绘制。
 *
 *  本 .cpp 不 include Flamingo 的 stdafx.h —— 它要被 Flamingo.vcxproj 与
 *  FlamingoUnit.vcxproj 共用。在 Flamingo.vcxproj 里需标记
 *  PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-05-22
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

#include "NetServerCard.h"
#include "NetSettingsModel.h"
#include "../BalloonChatWnd/ChatIcons.h"
#include "../../Common/I18n/I18n.h"      // TR / TRF / TR_NOOP：界面文字的多语言查找

namespace balloonnetsettings {

namespace {

// ---- 几何常量（逻辑像素）---------------------------------------------

// 以下几何常量为 design 原值的 85% 等比缩小（对话框整体缩到 85%）。
const int           kIconBox      = 34;     // 图标圆角方块边长
const int           kEdgePad      = 10;     // 卡片内左右边距
const int           kIconTextGap  = 10;     // 图标与文字列的间距
const int           kGlyphSize    = 17;     // 图标线条 glyph 尺寸
const Gdiplus::REAL kCardRadius   = 8.5f;   // 卡片圆角
const Gdiplus::REAL kIconRadius   = 7.5f;   // 图标方块圆角
const int           kStatusDotR   = 3;      // 状态圆点半径

// ---- 配色 --------------------------------------------------------------

const Gdiplus::Color kCardBgNormal  (255, 0xFF, 0xFF, 0xFF);  // 普通卡片底
const Gdiplus::Color kCardBgHover   (255, 0xFA, 0xF8, 0xF5);  // 悬停卡片底
const Gdiplus::Color kCardBgSel     (255, 0xFF, 0xF1, 0xEA);  // 选中卡片底
const Gdiplus::Color kCardBorder    (255, 0xEC, 0xEA, 0xE6);  // 普通描边
const Gdiplus::Color kCardBorderSel (255, 0xFF, 0x7A, 0x45);  // 选中描边

const COLORREF kNameColor   = RGB(0x2A, 0x2A, 0x2A);  // 服务器名
const COLORREF kSubColor    = RGB(0x9A, 0x97, 0x92);  // 副标题

// 三组服务器的图标方块底色 / glyph 色（与设计稿一致）。
const Gdiplus::Color kIconTint[kNetServerCount] =
{
    Gdiplus::Color(255, 0xFF, 0xEC, 0xE3),   // 聊天：浅橙
    Gdiplus::Color(255, 0xE4, 0xEF, 0xFB),   // 图片：浅蓝
    Gdiplus::Color(255, 0xE4, 0xF3, 0xEA),   // 文件：浅绿
};
const Gdiplus::Color kIconGlyph[kNetServerCount] =
{
    Gdiplus::Color(255, 0xFF, 0x7A, 0x45),   // 聊天：橙
    Gdiplus::Color(255, 0x3C, 0x88, 0xD8),   // 图片：蓝
    Gdiplus::Color(255, 0x2F, 0xA5, 0x6A),   // 文件：绿
};

// 连接状态的圆点 / 文字色。
COLORREF ConnColor(int state)
{
    if (state == kNetConnConnected)
    {
        return RGB(0x35, 0xB3, 0x6A);   // 已连接：绿
    }
    if (state == kNetConnConnecting)
    {
        return RGB(0xF0, 0xA0, 0x20);   // 连接中：琥珀
    }
    return RGB(0xA8, 0xA6, 0xA2);       // 未连接：灰
}

// 连接状态的文字（当前界面语言）。
LPCWSTR ConnText(int state)
{
    if (state == kNetConnConnected)
    {
        return TR(L"已连接");
    }
    if (state == kNetConnConnecting)
    {
        return TR(L"连接中");
    }
    return TR(L"未连接");
}

// 向 path 添加一个圆角矩形。
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

// 用 GDI 绘制单行文字。
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
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, i18n::UiFontName());
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

// 测量单行文字宽度。
int MeasureTextWidth(HDC hdc, LPCWSTR text, int pixelHeight)
{
    if (text == NULL || text[0] == L'\0')
    {
        return 0;
    }
    HFONT hFont = ::CreateFontW(
        -pixelHeight, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, i18n::UiFontName());
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

// 在 [ix,iy,kIconBox] 方块内绘制"聊天气泡"glyph（线性描边）。
void DrawChatBubbleGlyph(Gdiplus::Graphics& g, int ix, int iy,
                         const Gdiplus::Color& color)
{
    Gdiplus::Pen pen(color, 1.7f);
    pen.SetLineJoin(Gdiplus::LineJoinRound);
    pen.SetStartCap(Gdiplus::LineCapRound);
    pen.SetEndCap(Gdiplus::LineCapRound);

    // 气泡主体（圆角矩形）。坐标为 design 原值的 85% 等比缩小。
    const Gdiplus::REAL bx = (Gdiplus::REAL)(ix + 9);
    const Gdiplus::REAL by = (Gdiplus::REAL)(iy + 9);
    Gdiplus::GraphicsPath body;
    AddRoundRect(body, bx, by, 17.0f, 12.0f, 4.0f);
    g.DrawPath(&pen, &body);

    // 左下小尾巴。
    Gdiplus::GraphicsPath tail;
    tail.AddLine(bx + 4.0f, by + 12.0f, bx + 4.0f, by + 16.0f);
    tail.AddLine(bx + 4.0f, by + 16.0f, bx + 9.0f, by + 12.0f);
    g.DrawPath(&pen, &tail);
}

} // anonymous namespace

NetServerCard::NetServerCard()
    : m_kind(kNetServerChat)
    , m_selected(false)
    , m_port(0)
    , m_connState(kNetConnConnected)
{
}

void NetServerCard::SetServerKind(int kind)
{
    m_kind = kind;
    Invalidate();
}

void NetServerCard::SetCardSelected(bool selected)
{
    if (m_selected != selected)
    {
        m_selected = selected;
        Invalidate();
    }
}

void NetServerCard::SetEndpoint(LPCTSTR host, int port)
{
    m_host = (host != NULL) ? host : _T("");
    m_port = port;
    Invalidate();
}

void NetServerCard::SetConnState(int state)
{
    m_connState = state;
    Invalidate();
}

void NetServerCard::OnPaint(HDC hdc, const RECT& /*rcDirty*/)
{
    const int x = m_rcItem.left;
    const int y = m_rcItem.top;
    const int w = m_rcItem.right  - m_rcItem.left;
    const int h = m_rcItem.bottom - m_rcItem.top;
    if (w <= 0 || h <= 0)
    {
        return;
    }

    Gdiplus::Graphics g(hdc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

    // ---- 卡片底 + 描边 ----
    {
        Gdiplus::GraphicsPath card;
        // 描边 1px：path 内缩 1px 让描边完整落在控件内。
        AddRoundRect(card, (Gdiplus::REAL)x + 1.0f, (Gdiplus::REAL)y + 1.0f,
                     (Gdiplus::REAL)w - 2.0f, (Gdiplus::REAL)h - 2.0f,
                     kCardRadius);
        Gdiplus::Color bg = kCardBgNormal;
        if (m_selected)
        {
            bg = kCardBgSel;
        }
        else if (IsHover())
        {
            bg = kCardBgHover;
        }
        Gdiplus::SolidBrush fill(bg);
        g.FillPath(&fill, &card);
        Gdiplus::Pen border(m_selected ? kCardBorderSel : kCardBorder,
                            m_selected ? 1.6f : 1.0f);
        g.DrawPath(&border, &card);
    }

    // ---- 图标方块 ----
    const int ix = x + kEdgePad;
    const int iy = y + (h - kIconBox) / 2;
    {
        Gdiplus::GraphicsPath iconBg;
        AddRoundRect(iconBg, (Gdiplus::REAL)ix, (Gdiplus::REAL)iy,
                     (Gdiplus::REAL)kIconBox, (Gdiplus::REAL)kIconBox,
                     kIconRadius);
        const int ti = (m_kind >= 0 && m_kind < kNetServerCount)
                       ? m_kind : kNetServerChat;
        Gdiplus::SolidBrush tint(kIconTint[ti]);
        g.FillPath(&tint, &iconBg);

        // glyph：聊天就地画气泡；图片 / 文件复用 ChatIcons 线性图标。
        const Gdiplus::Color glyph = kIconGlyph[ti];
        const Gdiplus::REAL gx = (Gdiplus::REAL)(ix + (kIconBox - kGlyphSize) / 2);
        const Gdiplus::REAL gy = (Gdiplus::REAL)(iy + (kIconBox - kGlyphSize) / 2);
        if (ti == kNetServerImage)
        {
            balloonchatwnd::DrawChatIcon(g, balloonchatwnd::kChatIconImage,
                gx, gy, (Gdiplus::REAL)kGlyphSize, glyph, 1.7f);
        }
        else if (ti == kNetServerFile)
        {
            balloonchatwnd::DrawChatIcon(g, balloonchatwnd::kChatIconFile,
                gx, gy, (Gdiplus::REAL)kGlyphSize, glyph, 1.7f);
        }
        else
        {
            DrawChatBubbleGlyph(g, ix, iy, glyph);
        }
    }

    // ---- 右侧连接状态（圆点 + 文字）----
    const COLORREF connClr = ConnColor(m_connState);
    LPCWSTR        connTxt = ConnText(m_connState);
    const int connTxtW = MeasureTextWidth(hdc, connTxt, 10);
    const int statusRight = x + w - kEdgePad;
    const int statusTextX = statusRight - connTxtW;
    const int dotCx = statusTextX - 6 - kStatusDotR;
    const int dotCy = y + h / 2;
    {
        Gdiplus::SolidBrush dot(Gdiplus::Color(255,
            GetRValue(connClr), GetGValue(connClr), GetBValue(connClr)));
        g.FillEllipse(&dot,
            (Gdiplus::REAL)(dotCx - kStatusDotR),
            (Gdiplus::REAL)(dotCy - kStatusDotR),
            (Gdiplus::REAL)(kStatusDotR * 2),
            (Gdiplus::REAL)(kStatusDotR * 2));
    }
    {
        RECT stRc = { statusTextX, y, statusRight, y + h };
        DrawGdiText(hdc, connTxt, stRc, 10, false, connClr,
                    DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);
    }

    // ---- 文字列（服务器名 + 副标题）----
    const int textX = ix + kIconBox + kIconTextGap;
    const int textRight = dotCx - kStatusDotR - 8;

    // 服务器名（粗体）。文字 y 偏移 / 字号为 design 原值的 85% 等比缩小。
    RECT nameRc = { textX, y + 9, textRight, y + 26 };
    DrawGdiText(hdc, (LPCWSTR)NetServerDisplayName(m_kind), nameRc, 12, true,
                kNameColor, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    // 副标题："地址:端口"。
    CString sub;
    sub.Format(_T("%s:%d"), (LPCTSTR)m_host, m_port);
    RECT subRc = { textX, y + 26, textRight, y + 43 };
    DrawGdiText(hdc, (LPCWSTR)sub, subRc, 10, false, kSubColor,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

} // namespace balloonnetsettings
