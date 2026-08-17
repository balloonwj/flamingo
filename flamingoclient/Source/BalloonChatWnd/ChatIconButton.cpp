/**
 *  ChatIconButton.cpp
 *
 *  单聊窗图标按钮的绘制实现。
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

#include "ChatIconButton.h"
#include "ChatIcons.h"
#include "../../../third-party/balloonui/balloonui/Controls/Feedback/DuiToolTip.h"

namespace balloonchatwnd {

namespace {

// 悬停底色（浅灰）。
const Gdiplus::Color kHoverFill(255, 0xF0, 0xEF, 0xEC);
// 图标颜色（design --text-muted #6B6B6B）。
const Gdiplus::Color kIconColor(255, 0x6B, 0x6B, 0x6B);

const Gdiplus::REAL kIconSize    = 18.0f;   // 图标显示尺寸
const Gdiplus::REAL kStrokeWidth = 2.0f;    // 图标描边宽度（viewBox 单位）
const Gdiplus::REAL kHoverRadius = 6.0f;    // 悬停底圆角

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

} // anonymous namespace

ChatIconButton::ChatIconButton()
    : m_iconKind(kChatIconMore)
{
}

ChatIconButton::~ChatIconButton()
{
    // 控件销毁前注销 tooltip，避免管理器残留死指针。
    balloonwjui::DuiToolTipMgr::Inst().Unregister(this);
}

void ChatIconButton::SetIconKind(int kind)
{
    m_iconKind = kind;
    Invalidate();
}

void ChatIconButton::OnPaint(HDC hdc, const RECT& /*rcDirty*/)
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

    // 悬停：浅灰圆角底（行内缩 2px 留呼吸位）。
    if (IsHover())
    {
        Gdiplus::GraphicsPath path;
        AddRoundRect(path, (Gdiplus::REAL)(x + 2), (Gdiplus::REAL)(y + 2),
                     (Gdiplus::REAL)(w - 4), (Gdiplus::REAL)(h - 4), kHoverRadius);
        Gdiplus::SolidBrush hover(kHoverFill);
        g.FillPath(&hover, &path);
    }

    // 图标居中。
    const Gdiplus::REAL iconX = (Gdiplus::REAL)x + ((Gdiplus::REAL)w - kIconSize) / 2.0f;
    const Gdiplus::REAL iconY = (Gdiplus::REAL)y + ((Gdiplus::REAL)h - kIconSize) / 2.0f;
    DrawChatIcon(g, m_iconKind, iconX, iconY, kIconSize, kIconColor, kStrokeWidth);
}

} // namespace balloonchatwnd
