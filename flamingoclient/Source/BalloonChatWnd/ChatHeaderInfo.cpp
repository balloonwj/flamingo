/**
 *  ChatHeaderInfo.cpp
 *
 *  聊天头部信息块的绘制实现。状态胶囊用 GDI+ 抗锯齿，文字用 GDI
 *  DrawText（清晰）。数据取自 ChatMockData 的 ChatPartner()。
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

#include "ChatHeaderInfo.h"
#include "ChatMockData.h"
#include "../net/Protocol.h"        // presence_type（把对方真实 presence 映射到状态点颜色/文字）

namespace balloonchatwnd {

namespace {

// 把 v2 在线状态（presence_type）映射到本头部用的 ChatStatus（两套枚举值不同：v2
// 是 0离线/1在线/2离开/3勿扰/4隐身，ChatStatus 是 0在线/1离开/2忙碌/4离线）。
// 隐身与其它未知值按离线处理（对外显示离线）。
int ChatStatusFromPresence(int presence)
{
    switch (presence)
    {
    case presence_online:
        return kChatOnline;
    case presence_away:
        return kChatAway;
    case presence_busy:
        return kChatBusy;
    default:
        return kChatOffline;
    }
}

// ---- 配色 --------------------------------------------------------------

const COLORREF kNameColor = RGB(0x2A, 0x2A, 0x2A);   // 昵称
const COLORREF kSigColor  = RGB(0x8A, 0x8A, 0x8A);   // 签名（text-muted）
const COLORREF kPillText  = RGB(0x5C, 0x6B, 0x5F);   // 状态胶囊文字
// 状态胶囊浅底。
const Gdiplus::Color kPillFill(255, 0xF0, 0xF3, 0xF0);
// "正在输入…"用的主题橙（与 ChatBubble.cpp 的 kAccent 同色），状态点与文字都用它 ——
// 与在线绿 / 离开橙那一档明显区分，用户一眼能看出这是临时状态而非在线状态。
const COLORREF kTypingColor = RGB(0xFF, 0x7A, 0x45);
// "正在输入…"的胶囊文字。末尾用省略号表示动作仍在进行。
const wchar_t* const kTypingText = L"正在输入…";

// ---- 几何 --------------------------------------------------------------

const int kNamePx     = 15;   // 昵称字号
const int kSigPx      = 12;   // 签名字号
const int kPillTextPx = 11;   // 状态胶囊文字字号
const int kPillHeight = 18;   // 状态胶囊高度
const int kStatusDot  = 6;    // 状态点直径

// 取在线状态对应的状态点颜色。
COLORREF StatusColor(int status)
{
    switch (status)
    {
    case kChatOnline: return RGB(0x22, 0xC5, 0x5E);   // 在线 绿
    case kChatAway:   return RGB(0xF5, 0x9E, 0x0B);   // 离开 橙
    case kChatBusy:   return RGB(0xEF, 0x44, 0x44);   // 忙碌 红
    default:          return RGB(0x94, 0xA3, 0xB8);   // 离线 灰
    }
}

// 取在线状态对应的文字。
LPCWSTR StatusText(int status)
{
    switch (status)
    {
    case kChatOnline: return L"在线";
    case kChatAway:   return L"离开";
    case kChatBusy:   return L"忙碌";
    default:          return L"离线";
    }
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

// 测量一段文字的像素宽度。
int MeasureTextWidth(HDC hdc, LPCWSTR text, int pixelHeight, bool bold)
{
    if (text == NULL || text[0] == L'\0')
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
    ::GetTextExtentPoint32W(hdc, text, (int)::wcslen(text), &sz);
    ::SelectObject(hdc, hOldFont);
    ::DeleteObject(hFont);
    return sz.cx;
}

} // anonymous namespace

ChatHeaderInfo::ChatHeaderInfo()
{
}

void ChatHeaderInfo::SetPresence(int presence)
{
    m_presence    = presence;
    m_hasRealPeer = true;
}

void ChatHeaderInfo::SetSignature(const CString& signature)
{
    m_signature   = signature;
    m_hasRealPeer = true;
}

void ChatHeaderInfo::SetTyping(bool typing)
{
    m_typing = typing;
}

void ChatHeaderInfo::OnPaint(HDC hdc, const RECT& /*rcDirty*/)
{
    const int x = m_rcItem.left;
    const int y = m_rcItem.top;
    const int w = m_rcItem.right  - m_rcItem.left;
    const int h = m_rcItem.bottom - m_rcItem.top;
    if (w <= 0 || h <= 0)
    {
        return;
    }

    const ChatPeer& peer = ChatPartner();

    // 数据来源：已注入真实对端数据则用真实 presence / 签名；否则回退 mock（调试入口 /
    // 未绑定对端时）。真实 presence 是 v2 presence_type，先映射到本头部的 ChatStatus。
    const int     drawStatus = m_hasRealPeer ? ChatStatusFromPresence(m_presence) : peer.status;
    const CString drawSig    = m_hasRealPeer ? m_signature : peer.signature;

    // 昵称 + 签名整体垂直居中：昵称行 + 签名行。
    const int nameTop = y + (h - 38) / 2;       // 昵称行顶
    const int sigTop  = nameTop + 21;           // 签名行顶

    // ---- 状态胶囊 ----
    // 注：对方昵称已移到窗口标题栏（标题文字 + 头像图标）显示，故此处不再
    // 重复画昵称；状态胶囊左对齐到信息块左侧。
    // 对方正在输入时，这枚胶囊改说"正在输入…"并整体换成主题色 —— 输入是个转瞬即逝
    // 的状态，压过在线 / 离开那一档显示，输入停止后自然恢复。
    LPCWSTR statusText = m_typing ? kTypingText : StatusText(drawStatus);
    const COLORREF statusTextColor = m_typing ? kTypingColor : kPillText;
    const int statusTextW = MeasureTextWidth(hdc, statusText, kPillTextPx, false);
    const int pillW = 8 + kStatusDot + 5 + statusTextW + 8;
    const int pillX = x;
    const int pillY = nameTop + (20 - kPillHeight) / 2;

    {
        Gdiplus::Graphics g(hdc);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

        Gdiplus::GraphicsPath pill;
        const Gdiplus::REAL pr = (Gdiplus::REAL)kPillHeight / 2.0f;
        const Gdiplus::REAL d  = pr * 2.0f;
        pill.AddArc((Gdiplus::REAL)pillX, (Gdiplus::REAL)pillY, d, d, 90.0f, 180.0f);
        pill.AddArc((Gdiplus::REAL)(pillX + pillW) - d, (Gdiplus::REAL)pillY,
                    d, d, 270.0f, 180.0f);
        pill.CloseFigure();
        Gdiplus::SolidBrush pillBrush(kPillFill);
        g.FillPath(&pillBrush, &pill);

        COLORREF dotC = m_typing ? kTypingColor : StatusColor(drawStatus);
        Gdiplus::SolidBrush dotBrush(Gdiplus::Color(255,
            GetRValue(dotC), GetGValue(dotC), GetBValue(dotC)));
        g.FillEllipse(&dotBrush,
                      (Gdiplus::REAL)(pillX + 8),
                      (Gdiplus::REAL)(pillY + (kPillHeight - kStatusDot) / 2),
                      (Gdiplus::REAL)kStatusDot, (Gdiplus::REAL)kStatusDot);
    }

    RECT pillTextRc = { pillX + 8 + kStatusDot + 5, pillY,
                        pillX + pillW, pillY + kPillHeight };
    DrawGdiText(hdc, statusText, pillTextRc, kPillTextPx, false, statusTextColor,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);

    // ---- 签名 ----
    if (!drawSig.IsEmpty())
    {
        RECT sigRc = { x, sigTop, x + w, sigTop + 17 };
        DrawGdiText(hdc, (LPCWSTR)drawSig, sigRc, kSigPx, false, kSigColor,
                    DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
}

bool ChatHeaderInfo::OnLButtonDown(POINT /*pt*/, UINT /*mkFlags*/)
{
    // 整块可点：发 DUIN_CLICK（ctrlId=kIdHeaderInfo），由单聊窗路由到主面板
    // 打开对方资料窗。
    NotifyParent(balloonwjui::DUIN_CLICK);
    return true;
}

} // namespace balloonchatwnd
