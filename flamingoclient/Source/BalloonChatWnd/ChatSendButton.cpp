/**
 *  ChatSendButton.cpp
 *
 *  单聊窗「发送」按钮的绘制实现。橙色圆角底用 GDI+ 抗锯齿，纸飞机图标
 *  走 ChatIcons，文字用 GDI DrawText。
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

#include "ChatSendButton.h"
#include "ChatIcons.h"
#include "../../../third-party/balloonui/balloonui/Controls/Feedback/DuiToolTip.h"

namespace balloonchatwnd {

namespace {

// 橙色填充 / 悬停色。
const Gdiplus::Color kFill     (255, 0xFF, 0x7A, 0x45);
const Gdiplus::Color kFillHover(255, 0xFF, 0x6A, 0x30);
// 禁用态（离线时聊天窗对本按钮 SetEnabled(false)）的灰底与浅灰前景。基类 DuiButton
// 只管不再响应点击，外观得由本类自绘区分，否则离线时按钮看着仍是可点的橙色。
const Gdiplus::Color kFillDisabled(255, 0xE5, 0xE7, 0xEB);
const Gdiplus::Color kIconDisabled(255, 0x9C, 0xA3, 0xAF);
const COLORREF       kTextDisabled = RGB(0x9C, 0xA3, 0xAF);
// 图标 / 文字白色。
const Gdiplus::Color kIconWhite(255, 255, 255, 255);
const COLORREF       kTextWhite = RGB(255, 255, 255);

const Gdiplus::REAL kRadius      = 8.0f;    // 按钮圆角
const Gdiplus::REAL kMaxHeight   = 30.0f;   // 按钮主体最大高
const Gdiplus::REAL kIconSize    = 14.0f;   // 纸飞机图标尺寸
const Gdiplus::REAL kIconTextGap = 5.0f;    // 图标与文字间距
const int           kTextPx      = 13;      // 文字字号

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

// 用 GDI 绘制文字。
void DrawGdiText(HDC hdc, LPCWSTR text, const RECT& rc,
                 int pixelHeight, COLORREF color, UINT dtFlags)
{
    if (text == NULL || text[0] == L'\0' || pixelHeight <= 0)
    {
        return;
    }
    HFONT hFont = ::CreateFontW(
        -pixelHeight, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");
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
int MeasureTextWidth(HDC hdc, LPCWSTR text, int pixelHeight)
{
    if (text == NULL || text[0] == L'\0')
    {
        return 0;
    }
    HFONT hFont = ::CreateFontW(
        -pixelHeight, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");
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

ChatSendButton::ChatSendButton()
{
    SetText(_T("发送"));
}

ChatSendButton::~ChatSendButton()
{
    // 控件销毁前注销 tooltip，避免管理器残留死指针。
    balloonwjui::DuiToolTipMgr::Inst().Unregister(this);
}

void ChatSendButton::OnPaint(HDC hdc, const RECT& /*rcDirty*/)
{
    const int x = m_rcItem.left;
    const int y = m_rcItem.top;
    const int w = m_rcItem.right  - m_rcItem.left;
    const int h = m_rcItem.bottom - m_rcItem.top;
    if (w <= 0 || h <= 0)
    {
        return;
    }

    // 按钮主体高度上限 kMaxHeight，垂直居中。
    Gdiplus::REAL btnH = (Gdiplus::REAL)h;
    if (btnH > kMaxHeight) { btnH = kMaxHeight; }
    const Gdiplus::REAL btnY = (Gdiplus::REAL)y + ((Gdiplus::REAL)h - btnH) / 2.0f;

    // 右侧让出一个「▼」下拉区，左侧主体区放图标 + 文字。
    const int dropLeft  = x + w - kSendBtnDropdownZoneW;   // 下拉区左边界（=主体区右边界）
    const int contentW  = dropLeft - x;                    // 主体区宽度

    const CString text = GetText();
    const int textW = MeasureTextWidth(hdc, (LPCWSTR)text, kTextPx);

    // 图标 + 文字作为整体，在「主体区」内水平居中（不含右侧下拉区）。
    const Gdiplus::REAL groupW = kIconSize + kIconTextGap + (Gdiplus::REAL)textW;
    const Gdiplus::REAL groupX = (Gdiplus::REAL)x + ((Gdiplus::REAL)contentW - groupW) / 2.0f;
    const Gdiplus::REAL iconY  = btnY + (btnH - kIconSize) / 2.0f;

    // ---- GDI+ 形状层：橙色圆角底（整宽）+ 纸飞机图标 + 分隔线 + ▼ ----
    {
        Gdiplus::Graphics g(hdc);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

        // 禁用态（离线时由聊天窗 SetEnabled(false) 置入）画成灰底：基类 DuiButton 只
        // 负责不再响应点击，外观由本类的自绘决定 —— 不在这里区分的话，按钮离线时看着
        // 仍是可点的橙色，用户会反复去点。
        Gdiplus::GraphicsPath path;
        AddRoundRect(path, (Gdiplus::REAL)x, btnY, (Gdiplus::REAL)w, btnH, kRadius);
        Gdiplus::Color fillColor = kFill;
        if (!IsEnabled())
        {
            fillColor = kFillDisabled;
        }
        else if (IsHover())
        {
            fillColor = kFillHover;
        }
        Gdiplus::SolidBrush fill(fillColor);
        g.FillPath(&fill, &path);

        // 禁用态下图标与文字用浅灰，与灰底保持足够对比但明显"不可用"。
        const Gdiplus::Color fgColor = IsEnabled() ? kIconWhite : kIconDisabled;
        DrawChatIcon(g, kChatIconSend, groupX, iconY, kIconSize, fgColor, 2.0f);

        // 主体区与下拉区之间的竖分隔线（半透明白，上下各内缩一点）。
        Gdiplus::Pen divider(Gdiplus::Color(110, 255, 255, 255), 1.0f);
        g.DrawLine(&divider,
                   (Gdiplus::REAL)dropLeft, btnY + 6.0f,
                   (Gdiplus::REAL)dropLeft, btnY + btnH - 6.0f);

        // 下拉区里居中画一个白色下三角 ▼。
        const Gdiplus::REAL triCx = (Gdiplus::REAL)dropLeft + kSendBtnDropdownZoneW / 2.0f;
        const Gdiplus::REAL triCy = btnY + btnH / 2.0f;
        Gdiplus::PointF tri[3] =
        {
            Gdiplus::PointF(triCx - 4.0f, triCy - 2.0f),
            Gdiplus::PointF(triCx + 4.0f, triCy - 2.0f),
            Gdiplus::PointF(triCx,        triCy + 3.0f)
        };
        Gdiplus::SolidBrush triBrush(fgColor);
        g.FillPolygon(&triBrush, tri, 3);
    }

    // ---- GDI 文字层（限定在主体区内）----
    RECT textRc = { (int)(groupX + kIconSize + kIconTextGap), (int)btnY,
                    dropLeft, (int)(btnY + btnH) };
    DrawGdiText(hdc, (LPCWSTR)text, textRc, kTextPx,
                IsEnabled() ? kTextWhite : kTextDisabled,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);
}

bool ChatSendButton::InDropdownZone(POINT pt) const
{
    // 下拉区 = 控件最右侧 kSendBtnDropdownZoneW 宽的竖条。
    return pt.x >= (m_rcItem.right - kSendBtnDropdownZoneW);
}

bool ChatSendButton::OnLButtonDown(POINT pt, UINT mkFlags)
{
    if (InDropdownZone(pt))
    {
        // 点「▼」下拉区：发下拉通知给父窗口弹菜单，不进入按钮按下态、也不发送。
        NotifyParent(DUIN_SENDBTN_DROPDOWN);
        return true;
    }
    // 点主体：交基类按普通按钮处理（按下态 + 抬起时发 DUIN_CLICK → 发送）。
    return balloonwjui::DuiButton::OnLButtonDown(pt, mkFlags);
}

} // namespace balloonchatwnd
