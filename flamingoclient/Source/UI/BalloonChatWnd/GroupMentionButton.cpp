/**
 *  GroupMentionButton.cpp
 *
 *  「@提醒」按钮的绘制实现。白底圆角 + 描边用 GDI+ 抗锯齿，文字用 GDI
 *  DrawText（清晰）。
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

#include "GroupMentionButton.h"
#include "../../../third-party/balloonui/balloonui/Controls/Feedback/DuiToolTip.h"
#include "../../Common/I18n/I18n.h"   // TR：界面文字的多语言查找

namespace balloonchatwnd {

namespace {

// ---- 配色 / 几何 -------------------------------------------------------

const Gdiplus::Color kFill      (255, 255, 255, 255);     // 白底
const Gdiplus::Color kFillHover (255, 0xF4, 0xF3, 0xF0);  // 悬停浅灰底
const Gdiplus::Color kBorder    (255, 0xCF, 0xCD, 0xC8);  // 1px 描边
const COLORREF       kTextColor = RGB(0x7A, 0x7A, 0x7A);  // 文字（muted）

const Gdiplus::REAL kRadius    = 6.0f;    // 按钮圆角
const Gdiplus::REAL kMaxHeight = 24.0f;   // 按钮主体高
const int           kTextPx    = 11;      // 文字字号

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

} // anonymous namespace

GroupMentionButton::GroupMentionButton()
{
    SetText(TR(L"@提醒"));
}

GroupMentionButton::~GroupMentionButton()
{
    // 控件销毁前注销 tooltip，避免管理器残留死指针。
    balloonwjui::DuiToolTipMgr::Inst().Unregister(this);
}

void GroupMentionButton::OnPaint(HDC hdc, const RECT& /*rcDirty*/)
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

    // ---- GDI+ 形状层：白底圆角 + 1px 描边 ----
    {
        Gdiplus::Graphics g(hdc);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

        // 描边 1px：path 内缩 0.5px 让描边完整落在控件内。
        Gdiplus::GraphicsPath path;
        AddRoundRect(path, (Gdiplus::REAL)x + 0.5f, btnY + 0.5f,
                     (Gdiplus::REAL)w - 1.0f, btnH - 1.0f, kRadius);
        Gdiplus::SolidBrush fill(IsHover() ? kFillHover : kFill);
        g.FillPath(&fill, &path);
        Gdiplus::Pen border(kBorder, 1.0f);
        g.DrawPath(&border, &path);
    }

    // ---- GDI 文字层 ----
    RECT textRc = { x, (int)btnY, x + w, (int)(btnY + btnH) };
    DrawGdiText(hdc, (LPCWSTR)GetText(), textRc, kTextPx, kTextColor,
                DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);
}

} // namespace balloonchatwnd
