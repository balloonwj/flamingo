/**
 *  MainPanelSectionHeader.cpp
 *
 *  联系人分组标题行的绘制实现。展开三角用 GDI+ 抗锯齿，文字用 GDI
 *  DrawText（清晰）。
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

#include "MainPanelSectionHeader.h"

namespace balloonmain {

namespace {

// 分组名文字色（design --text #1A1A1A）。
const COLORREF kTitleColor = RGB(0x1A, 0x1A, 0x1A);
// 右侧计数文字色（design --text-faint）。
const COLORREF kCountColor = RGB(0xA8, 0xA6, 0xA2);
// 展开三角颜色（design --text-faint）。
const Gdiplus::Color kTriangleColor(255, 0xA8, 0xA6, 0xA2);

const int kLeftPad      = 6;    // 三角距左边
const int kTriangleSize = 8;    // 三角外接尺寸
const int kTitleGap     = 6;    // 三角与分组名间距
const int kTitlePx      = 13;   // 分组名字号
const int kCountPx      = 11;   // 计数字号
const int kRightPad     = 8;    // 计数距右边

// 用 GDI 绘制文字。
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

MainPanelSectionHeader::MainPanelSectionHeader()
    : m_online(0)
    , m_total(0)
    , m_expanded(true)
{
}

void MainPanelSectionHeader::SetExpanded(bool expanded)
{
    if (m_expanded == expanded)
    {
        return;
    }
    m_expanded = expanded;
    Invalidate();
}

void MainPanelSectionHeader::SetTitle(LPCTSTR title)
{
    m_title = (title != NULL) ? title : _T("");
    Invalidate();
}

void MainPanelSectionHeader::SetCount(int online, int total)
{
    m_online = online;
    m_total  = total;
    Invalidate();
}

void MainPanelSectionHeader::OnPaint(HDC hdc, const RECT& /*rcDirty*/)
{
    const int x = m_rcItem.left;
    const int y = m_rcItem.top;
    const int w = m_rcItem.right  - m_rcItem.left;
    const int h = m_rcItem.bottom - m_rcItem.top;
    if (w <= 0 || h <= 0)
    {
        return;
    }

    // ---- GDI+：展开三角（恒展开 → 朝下）----
    {
        Gdiplus::Graphics g(hdc);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

        const Gdiplus::REAL triLeft = (Gdiplus::REAL)(x + kLeftPad);
        const Gdiplus::REAL triTop  = (Gdiplus::REAL)y + ((Gdiplus::REAL)h - kTriangleSize) / 2.0f;
        Gdiplus::PointF tri[3];
        if (m_expanded)
        {
            // 展开：实心三角朝下。
            tri[0] = Gdiplus::PointF(triLeft, triTop);
            tri[1] = Gdiplus::PointF(triLeft + kTriangleSize, triTop);
            tri[2] = Gdiplus::PointF(triLeft + kTriangleSize / 2.0f,
                                     triTop + kTriangleSize * 0.72f);
        }
        else
        {
            // 收起：实心三角朝右。
            tri[0] = Gdiplus::PointF(triLeft + 1.0f, triTop);
            tri[1] = Gdiplus::PointF(triLeft + 1.0f, triTop + kTriangleSize);
            tri[2] = Gdiplus::PointF(triLeft + 1.0f + kTriangleSize * 0.72f,
                                     triTop + kTriangleSize / 2.0f);
        }
        Gdiplus::SolidBrush brush(kTriangleColor);
        g.FillPolygon(&brush, tri, 3);
    }

    // ---- GDI 文字 ----

    // 分组名（粗体）。
    const int titleLeft = x + kLeftPad + kTriangleSize + kTitleGap;
    RECT titleRc = { titleLeft, y, x + w - 48, y + h };
    DrawGdiText(hdc, (LPCWSTR)m_title, titleRc, kTitlePx, true, kTitleColor,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    // 右侧"在线 / 总数"。
    CString count;
    count.Format(_T("%d/%d"), m_online, m_total);
    RECT countRc = { x + w - kRightPad - 64, y, x + w - kRightPad, y + h };
    DrawGdiText(hdc, (LPCWSTR)count, countRc, kCountPx, false, kCountColor,
                DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);
}

} // namespace balloonmain
