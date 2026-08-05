/**
 *  TagChip.cpp
 *
 *  标签胶囊的自绘实现。圆角胶囊底走 GDI+ 抗锯齿，文字与「×」走 GDI
 *  DrawText（更清晰）。各项几何尺寸严格照设计稿 dialogs.jsx 的标签胶囊：
 *  高 24、左右内边距 10、accent-soft 底、accent 字、字号 11.5。
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

#include "TagChip.h"

namespace ballooncontact {

namespace {

// ---- 几何常量（逻辑像素，照设计稿 dialogs.jsx 标签胶囊）-----------------

const int kChipPadX     = 10;   // 胶囊左右内边距（设计稿 padding:'0 10px'）
const int kChipTextGap  = 4;    // 文字与「×」之间的间距（设计稿 gap:4）
const int kChipFontSize = 12;   // 胶囊文字字号（设计稿 11.5，取整 12）
const int kCloseGlyph   = 12;   // 「×」字形的标称宽度（设计稿 fontSize:14 的 ×，
                                // 这里用 12 像素近似，便于热区计算）

// ---- 配色（accent / accent-soft，照主题色板）---------------------------

// 胶囊底色 accent-soft = #FFE6D6 = RGB(255,230,214)。
const Gdiplus::Color kChipBg(255, 0xFF, 0xE6, 0xD6);

// 胶囊文字 / 「×」颜色 accent = #FF7A45 = RGB(255,122,69)。
const COLORREF       kChipFg = RGB(0xFF, 0x7A, 0x45);

// ---- 公共绘制小工具 ----------------------------------------------------

// 用指定字号创建一个微软雅黑字体。
HFONT CreateYaHeiFont(int pixelHeight)
{
    return ::CreateFontW(
        -pixelHeight, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");
}

// 测量单行文字宽度（像素）。text 为空返回 0。
int MeasureTextWidth(HDC hdc, LPCWSTR text, int pixelHeight)
{
    if (hdc == NULL || text == NULL || text[0] == L'\0')
    {
        return 0;
    }
    HFONT hFont = CreateYaHeiFont(pixelHeight);
    if (hFont == NULL)
    {
        return 0;
    }
    HFONT hOldFont = (HFONT)::SelectObject(hdc, hFont);
    SIZE  sz = { 0, 0 };
    ::GetTextExtentPoint32W(hdc, text, (int)::wcslen(text), &sz);
    ::SelectObject(hdc, hOldFont);
    ::DeleteObject(hFont);
    return sz.cx;
}

// 在矩形内用 GDI 绘制单行文字。
void DrawGdiText(HDC hdc, LPCWSTR text, const RECT& rc,
                 int pixelHeight, COLORREF color, UINT dtFlags)
{
    if (text == NULL || text[0] == L'\0' || pixelHeight <= 0)
    {
        return;
    }
    HFONT hFont = CreateYaHeiFont(pixelHeight);
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

TagChip::TagChip()
{
}

void TagChip::SetTagText(LPCTSTR text)
{
    m_text = (text != NULL) ? text : _T("");
    Invalidate();
}

int TagChip::CalcDesiredWidth(HDC hdcMeasure) const
{
    // 理想宽度 = 左内边距 + 文字宽 + 文字与×间距 + ×字形宽 + 右内边距。
    const int textW  = MeasureTextWidth(hdcMeasure, (LPCWSTR)m_text, kChipFontSize);
    return kChipPadX + textW + kChipTextGap + kCloseGlyph + kChipPadX;
}

RECT TagChip::DeleteZoneRect() const
{
    // 「×」热区 = 胶囊右侧、宽度 = ×字形宽 + 右内边距、整高。
    RECT empty = { 0, 0, 0, 0 };
    const int w = m_rcItem.right - m_rcItem.left;
    const int h = m_rcItem.bottom - m_rcItem.top;
    if (w <= 0 || h <= 0)
    {
        return empty;
    }
    RECT zone;
    zone.right  = m_rcItem.right;
    zone.left   = m_rcItem.right - (kCloseGlyph + kChipPadX);
    zone.top    = m_rcItem.top;
    zone.bottom = m_rcItem.bottom;
    // 热区不应超过整个胶囊。
    if (zone.left < m_rcItem.left)
    {
        zone.left = m_rcItem.left;
    }
    return zone;
}

void TagChip::OnPaint(HDC hdc, const RECT& /*rcDirty*/)
{
    const int x = m_rcItem.left;
    const int y = m_rcItem.top;
    const int w = m_rcItem.right  - m_rcItem.left;
    const int h = m_rcItem.bottom - m_rcItem.top;
    if (w <= 0 || h <= 0)
    {
        return;
    }

    // ---- 全圆角胶囊底 ----
    {
        Gdiplus::Graphics g(hdc);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

        // 全圆角：半径 = 高度 / 2（设计稿 borderRadius:999）。
        const Gdiplus::REAL r = (Gdiplus::REAL)h / 2.0f;
        const Gdiplus::REAL d = r * 2.0f;
        Gdiplus::GraphicsPath path;
        path.AddArc((Gdiplus::REAL)x,             (Gdiplus::REAL)y,
                    d, d, 90.0f, 180.0f);
        path.AddArc((Gdiplus::REAL)x + w - d,     (Gdiplus::REAL)y,
                    d, d, 270.0f, 180.0f);
        path.CloseFigure();

        Gdiplus::SolidBrush bg(kChipBg);
        g.FillPath(&bg, &path);
    }

    // ---- 标签文字（左对齐，留出右侧「×」位置）----
    RECT textRc;
    textRc.left   = x + kChipPadX;
    textRc.top    = y;
    textRc.right  = x + w - kChipPadX - kCloseGlyph - kChipTextGap;
    textRc.bottom = y + h;
    DrawGdiText(hdc, (LPCWSTR)m_text, textRc, kChipFontSize, kChipFg,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX
                | DT_END_ELLIPSIS);

    // ---- 删除「×」----
    RECT closeRc = DeleteZoneRect();
    DrawGdiText(hdc, L"×", closeRc, kChipFontSize + 2, kChipFg,
                DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
}

bool TagChip::OnLButtonDown(POINT /*pt*/, UINT /*mkFlags*/)
{
    // 消费按下事件，保证 host 把随后的 OnLButtonUp 仍派发给本控件。
    return true;
}

bool TagChip::OnLButtonUp(POINT pt, UINT /*mkFlags*/)
{
    // 只有点在「×」热区内才视为"删除本标签"。
    RECT zone = DeleteZoneRect();
    if (::PtInRect(&zone, pt))
    {
        // 向父窗口冒 DUIN_CLICK —— 父窗口据本控件 ctrlId 删除对应标签。
        // DUIN_CLICK 定义在 balloonwjui 命名空间，本文件在 ballooncontact
        // 命名空间下，须显式限定。
        NotifyParent(balloonwjui::DUIN_CLICK);
        return true;
    }
    // 点在文字区：不发通知，但仍消费事件（胶囊整体没有其它点击语义）。
    return true;
}

} // namespace ballooncontact
