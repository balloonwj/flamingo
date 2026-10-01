/**
 *  AuthButtons.cpp
 *
 *  AuthPrimaryButton / AuthLinkLabel / AuthCheckBox 的绘制实现。点击 /
 *  按压 / 悬停事件逻辑全部继承自 DuiButton，本文件只覆写 OnPaint。
 *
 *  绘制分两层：形状（圆角按钮底 / 复选框方框）用 GDI+（带抗锯齿）；
 *  文字用 GDI 的 DrawText —— GDI 文字经系统 ClearType 光栅化，清晰锐利，
 *  避免 GDI+ DrawString 发虚。先在作用域内用 GDI+ 画完形状（Graphics
 *  析构时 flush），再用 GDI 画文字。
 *
 *  本 .cpp 故意<u>不</u> include Flamingo 的 stdafx.h —— BalloonLoginLayout
 *  的自定义工厂会构造这几个控件，而 BalloonLoginLayout 被 Flamingo.vcxproj
 *  与 FlamingoUnit.vcxproj 共用，故本文件也随之共用，自带 ATL/WTL 前置
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

#include "AuthButtons.h"

namespace balloonauth {

namespace {

// 主按钮圆角半径（逻辑像素，design Btn size lg 的 borderRadius=12）。
const int kButtonCornerRadius = 12;

// 主按钮填充色 #FF7A45（= 主题色 --accent）。
const COLORREF kAccentColor      = RGB(0xFF, 0x7A, 0x45);
// 主按钮悬停填充色 #FF6A30（design --accent-hover）。
const COLORREF kAccentHoverColor = RGB(0xFF, 0x6A, 0x30);

// 主按钮文字字号相对按钮高度的比例（design 14.5px / 42 ≈ 0.345）。
const float kButtonFontFactor = 0.345f;
// 链接文字字号相对控件高度的比例（design 12px / 20 = 0.6）。
const float kLinkFontFactor = 0.6f;

// 复选框方框边长相对控件高度的比例（design 方框 14 / 行高 22）。
const float kCheckBoxSideFactor = 0.66f;
// 复选框方框圆角半径相对方框边长的比例（design borderRadius 4 / 14）。
const float kCheckBoxRadiusFactor = 0.28f;
// 复选框方框与标签文字之间的间距相对控件高度的比例（design gap 6）。
const float kCheckBoxGapFactor = 0.30f;
// 复选框标签文字字号相对控件高度的比例（design 12px / 22）。
const float kCheckBoxFontFactor = 0.56f;

// 复选框未勾选时的方框边框色（design --border-strong，在白底上近似值）。
const COLORREF kCheckBorderColor = RGB(0xCF, 0xCD, 0xC9);
// 复选框标签文字色（design --text-muted #6B6B6B）。
const COLORREF kCheckTextColor   = RGB(0x6B, 0x6B, 0x6B);

// 主按钮文字色（白）。
const COLORREF kButtonTextColor  = RGB(255, 255, 255);

// 用四段 90° 圆弧 + 闭合，向 path 添加一个圆角矩形。
void AddRoundRect(Gdiplus::GraphicsPath& path,
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

// 用 GDI（而非 GDI+）绘制一段文字 —— GDI 的 DrawText 经系统 ClearType
// 光栅化，清晰锐利，避免 GDI+ DrawString 发虚。
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

} // anonymous namespace

// =======================================================================
// AuthPrimaryButton
// =======================================================================

AuthPrimaryButton::AuthPrimaryButton()
{
}

void AuthPrimaryButton::OnPaint(HDC hdc, const RECT& /*rcDirty*/)
{
    const int x = m_rcItem.left;
    const int y = m_rcItem.top;
    const int w = m_rcItem.right - m_rcItem.left;
    const int h = m_rcItem.bottom - m_rcItem.top;
    if (w <= 0 || h <= 0)
    {
        return;
    }

    // 形状层：圆角矩形实心填充（悬停时略深）。
    {
        Gdiplus::Graphics g(hdc);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

        Gdiplus::GraphicsPath path;
        AddRoundRect(path, (Gdiplus::REAL)x, (Gdiplus::REAL)y,
                     (Gdiplus::REAL)w, (Gdiplus::REAL)h,
                     (Gdiplus::REAL)kButtonCornerRadius);

        const COLORREF fill = IsHover() ? kAccentHoverColor : kAccentColor;
        Gdiplus::SolidBrush fillBrush(Gdiplus::Color(255,
            GetRValue(fill), GetGValue(fill), GetBValue(fill)));
        g.FillPath(&fillBrush, &path);
    }

    // 文字层：白色居中文字（GDI，清晰）。
    const CString text = GetText();
    DrawGdiText(hdc, (LPCWSTR)text, m_rcItem, (int)(h * kButtonFontFactor),
                false, kButtonTextColor,
                DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);
}

// =======================================================================
// AuthLinkLabel
// =======================================================================

AuthLinkLabel::AuthLinkLabel()
    : m_linkColor(kAccentColor)
{
}

int AuthLinkLabel::FontPx() const
{
    const int h = m_rcItem.bottom - m_rcItem.top;
    return (h > 0) ? (int)(h * kLinkFontFactor) : 0;
}

void AuthLinkLabel::OnPaint(HDC hdc, const RECT& /*rcDirty*/)
{
    const int h = m_rcItem.bottom - m_rcItem.top;
    if (h <= 0)
    {
        return;
    }
    // 链接只有文字，无形状层 —— 直接用 GDI 画。
    const CString text = GetText();
    DrawGdiText(hdc, (LPCWSTR)text, m_rcItem, FontPx(),
                false, m_linkColor,
                DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);
}

// =======================================================================
// AuthCheckBox
// =======================================================================

AuthCheckBox::AuthCheckBox()
{
    // 设为 Checkbox 风格 —— 复用 DuiButton 的点击 toggle + DUIN_VALUECHANGED。
    SetButtonType(balloonwjui::DuiButton::StyleCheckbox);
}

void AuthCheckBox::OnPaint(HDC hdc, const RECT& /*rcDirty*/)
{
    const int x = m_rcItem.left;
    const int y = m_rcItem.top;
    const int w = m_rcItem.right - m_rcItem.left;
    const int h = m_rcItem.bottom - m_rcItem.top;
    if (w <= 0 || h <= 0)
    {
        return;
    }

    const float boxSide = (float)h * kCheckBoxSideFactor;
    const float boxX    = (float)x;
    const float boxY    = (float)y + ((float)h - boxSide) / 2.0f;

    // 形状层：勾选方框（勾选时橙底 + 白勾，未勾选时白底 + 浅灰边框）。
    {
        Gdiplus::Graphics g(hdc);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

        Gdiplus::GraphicsPath boxPath;
        AddRoundRect(boxPath, boxX, boxY, boxSide, boxSide,
                     boxSide * kCheckBoxRadiusFactor);

        if (IsChecked())
        {
            Gdiplus::SolidBrush fill(Gdiplus::Color(255,
                GetRValue(kAccentColor), GetGValue(kAccentColor),
                GetBValue(kAccentColor)));
            g.FillPath(&fill, &boxPath);

            // 对勾路径取自 design icons.jsx 的 check（viewBox 24）。
            Gdiplus::GraphicsState st = g.Save();
            g.TranslateTransform(boxX, boxY);
            g.ScaleTransform(boxSide / 24.0f, boxSide / 24.0f);
            Gdiplus::Pen checkPen(Gdiplus::Color(255, 255, 255, 255), 3.0f);
            checkPen.SetStartCap(Gdiplus::LineCapRound);
            checkPen.SetEndCap(Gdiplus::LineCapRound);
            checkPen.SetLineJoin(Gdiplus::LineJoinRound);
            Gdiplus::GraphicsPath check;
            check.AddLine(5.0f, 12.5f, 9.5f, 17.0f);
            check.AddLine(9.5f, 17.0f, 20.0f, 7.0f);
            g.DrawPath(&checkPen, &check);
            g.Restore(st);
        }
        else
        {
            Gdiplus::SolidBrush fill(Gdiplus::Color(255, 255, 255, 255));
            g.FillPath(&fill, &boxPath);
            Gdiplus::Pen border(Gdiplus::Color(255,
                GetRValue(kCheckBorderColor), GetGValue(kCheckBorderColor),
                GetBValue(kCheckBorderColor)), 1.2f);
            g.DrawPath(&border, &boxPath);
        }
    }

    // 文字层：标签文字（GDI，清晰），方框右侧、垂直居中。
    const CString text = GetText();
    if (!text.IsEmpty())
    {
        RECT textRc = { (int)(boxX + boxSide + (float)h * kCheckBoxGapFactor),
                        y, x + w, y + h };
        DrawGdiText(hdc, (LPCWSTR)text, textRc, (int)((float)h * kCheckBoxFontFactor),
                    false, kCheckTextColor,
                    DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);
    }
}

} // namespace balloonauth
