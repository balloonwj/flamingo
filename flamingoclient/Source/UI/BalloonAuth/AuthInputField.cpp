/**
 *  AuthInputField.cpp
 *
 *  圆角输入框容器的绘制与布局实现（GDI+）。
 *
 *  本 .cpp 故意<u>不</u> include Flamingo 的 stdafx.h —— BalloonLoginLayout
 *  的自定义工厂会构造本控件，而 BalloonLoginLayout 被 Flamingo.vcxproj
 *  与 FlamingoUnit.vcxproj 共用，故本文件也随之共用，自带 ATL/WTL 前置
 *  依赖。在 Flamingo.vcxproj 里本文件需标记 PrecompiledHeader=NotUsing。
 *
 *  前导图标路径取自 design 的 icons.jsx（viewBox 0 0 24 24，描边 1.6）：
 *   · 人形 user：circle(12,8,r4) + path "M4 20c1-4 4-6 8-6s7 2 8 6"
 *   · 锁形 lock：rect(5,11,14,9,r2) + path "M8 11V8a4 4 0 0 1 8 0v3"
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

#include "AuthInputField.h"

namespace balloonauth {

namespace {

// 输入框填充色 #F5F4F1（design --bg-list / Input 背景）。
const BYTE kFillR = 0xF5, kFillG = 0xF4, kFillB = 0xF1;
// 前导图标描边色 #9A9A9A（design --text-faint）。
const BYTE kIconR = 0x9A, kIconG = 0x9A, kIconB = 0x9A;

// 输入框圆角半径（逻辑像素，design Input borderRadius:10）。
const int kFieldCornerRadius = 10;

// 前导图标原始路径所在的 SVG viewBox 边长（design icons.jsx：0 0 24 24）。
const float kIconViewBox = 24.0f;
// 前导图标描边宽度（viewBox 坐标系内，design Ic strokeWidth=1.6）。
const float kIconStrokeInViewBox = 1.6f;

// 以下比例均以控件高度为基准（design Input 高 40）。
// 左侧图标区宽度（容纳图标 + 两侧留白，约 36/40）。
const float kIconGutterFactor = 0.9f;
// 图标边长（design 图标 16/40）。
const float kIconSizeFactor = 0.4f;
// 图标中心距控件左边的距离（design 12 留白 + 8 半径 = 20，20/40）。
const float kIconCenterXFactor = 0.5f;
// 内嵌控件右侧内边距。
const float kRightInsetFactor = 0.12f;

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

// 在 viewBox(0..24) 坐标系内描出人形图标（头 + 肩）。
void DrawUserIcon(Gdiplus::Graphics& g, Gdiplus::Pen& pen)
{
    // 头：circle cx12 cy8 r4 → 外接矩形 (8,4,8,8)。
    g.DrawEllipse(&pen, 8.0f, 4.0f, 8.0f, 8.0f);
    // 肩：path "M4 20 c1 -4 4 -6 8 -6 s7 2 8 6" 展开为两段三次贝塞尔。
    Gdiplus::GraphicsPath shoulders;
    shoulders.AddBezier(4.0f, 20.0f, 5.0f, 16.0f, 8.0f, 14.0f, 12.0f, 14.0f);
    shoulders.AddBezier(12.0f, 14.0f, 16.0f, 14.0f, 19.0f, 16.0f, 20.0f, 20.0f);
    g.DrawPath(&pen, &shoulders);
}

// 在 viewBox(0..24) 坐标系内描出锁形图标（锁体 + 锁梁）。
void DrawLockIcon(Gdiplus::Graphics& g, Gdiplus::Pen& pen)
{
    // 锁体：圆角矩形 rect(5,11,14,9) rx2。
    Gdiplus::GraphicsPath body;
    AddRoundRectPath(body, 5.0f, 11.0f, 14.0f, 9.0f, 2.0f);
    g.DrawPath(&pen, &body);
    // 锁梁：左竖线 + 顶部半圆弧 + 右竖线（path "M8 11V8a4 4 0 0 1 8 0V11"）。
    // AddArc(8,4,8,8,180,-180)：以 (8,4,8,8) 为外接圆、自 180° 逆时针扫
    // 180°，即从 (8,8) 经顶部到 (16,8)。
    Gdiplus::GraphicsPath shackle;
    shackle.AddLine(8.0f, 11.0f, 8.0f, 8.0f);
    shackle.AddArc(8.0f, 4.0f, 8.0f, 8.0f, 180.0f, -180.0f);
    shackle.AddLine(16.0f, 8.0f, 16.0f, 11.0f);
    g.DrawPath(&pen, &shackle);
}

} // anonymous namespace

AuthInputField::AuthInputField()
    : m_iconKind(IconNone)
{
}

void AuthInputField::OnPaint(HDC hdc, const RECT& rcDirty)
{
    const int x = m_rcItem.left;
    const int y = m_rcItem.top;
    const int w = m_rcItem.right - m_rcItem.left;
    const int h = m_rcItem.bottom - m_rcItem.top;

    if (w > 0 && h > 0)
    {
        Gdiplus::Graphics g(hdc);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

        // 圆角填充背景。
        Gdiplus::GraphicsPath fillPath;
        AddRoundRectPath(fillPath, (Gdiplus::REAL)x, (Gdiplus::REAL)y,
                         (Gdiplus::REAL)w, (Gdiplus::REAL)h,
                         (Gdiplus::REAL)kFieldCornerRadius);
        Gdiplus::SolidBrush fillBrush(Gdiplus::Color(255, kFillR, kFillG, kFillB));
        g.FillPath(&fillBrush, &fillPath);

        // 左侧前导图标。
        if (m_iconKind != IconNone)
        {
            const Gdiplus::REAL iconSize = (Gdiplus::REAL)h * kIconSizeFactor;
            const Gdiplus::REAL iconCx = (Gdiplus::REAL)x + (Gdiplus::REAL)h * kIconCenterXFactor;
            const Gdiplus::REAL iconCy = (Gdiplus::REAL)y + (Gdiplus::REAL)h / 2.0f;
            const Gdiplus::REAL scale  = iconSize / kIconViewBox;

            Gdiplus::GraphicsState state = g.Save();
            g.TranslateTransform(iconCx - iconSize / 2.0f, iconCy - iconSize / 2.0f);
            g.ScaleTransform(scale, scale);

            Gdiplus::Pen iconPen(Gdiplus::Color(255, kIconR, kIconG, kIconB),
                                 kIconStrokeInViewBox);
            iconPen.SetStartCap(Gdiplus::LineCapRound);
            iconPen.SetEndCap(Gdiplus::LineCapRound);
            iconPen.SetLineJoin(Gdiplus::LineJoinRound);

            if (m_iconKind == IconUser)
            {
                DrawUserIcon(g, iconPen);
            }
            else if (m_iconKind == IconLock)
            {
                DrawLockIcon(g, iconPen);
            }

            g.Restore(state);
        }
    }

    // 绘制内嵌输入控件 —— DuiControl 基类默认实现负责递归绘制子控件。
    DuiControl::OnPaint(hdc, rcDirty);
}

void AuthInputField::Layout(const RECT& rcAvail)
{
    m_rcItem = rcAvail;

    // 把唯一的内嵌输入控件布局在前导图标右侧、右侧留一点内边距。
    if (!m_children.empty() && m_children[0])
    {
        const int h = rcAvail.bottom - rcAvail.top;
        RECT rcChild = rcAvail;
        rcChild.left  = rcAvail.left + (int)((float)h * kIconGutterFactor);
        rcChild.right = rcAvail.right - (int)((float)h * kRightInsetFactor);
        if (rcChild.right < rcChild.left)
        {
            rcChild.right = rcChild.left;
        }
        m_children[0]->Layout(rcChild);
    }
}

} // namespace balloonauth
