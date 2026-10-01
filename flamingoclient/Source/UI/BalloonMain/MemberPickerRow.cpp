/**
 *  MemberPickerRow.cpp
 *
 *  候选树行内容的绘制实现。
 *
 *  本 .cpp 不 include Flamingo 的 stdafx.h —— 它要被 Flamingo.vcxproj 与
 *  FlamingoUnit.vcxproj 共用。在 Flamingo.vcxproj 里需标记
 *  PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-08-03
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

#include "MemberPickerRow.h"
#include "MemberPickerTreeModel.h"   // MemberPickerCheckState

namespace balloonmain {

namespace {

// ---- 几何常量（逻辑像素）--------------------------------------------------

// 复选框条带宽度：16px 的方框 + 左右各 4px 呼吸位。点在这一竖条内算切换勾选。
const int kCheckStripW = 24;
// 复选框方框边长。
const int kCheckBoxSize = 16;
// 方框圆角半径 —— 略带圆角，比直角柔和，与全局按钮观感一致。
const Gdiplus::REAL kCheckBoxRadius = 3.0f;
// 头像边长与它左右的间距。
const int kAvatarSize = 32;
const int kAvatarGap  = 8;
// 头像圆角（32 的 25%，与主面板列表行同口径）。
const Gdiplus::REAL kAvatarRadius = 8.0f;
// 姓名字号与头像内字号。
const int kNamePx   = 12;
const int kAvatarPx = 14;

// ---- 配色 ----------------------------------------------------------------

// 复选框未选时的边框色（浅灰）与底色（白）。
const Gdiplus::Color kBoxBorder(255, 0xC4, 0xC7, 0xCE);
const Gdiplus::Color kBoxFill(255, 255, 255, 255);
// 选中 / 半选时的强调色（品牌橙，与头像调色板第 0 槽同一个橙）。
const Gdiplus::Color kBoxAccent(255, 0xFF, 0x7A, 0x45);
// 勾与半选方块的颜色（白，压在强调色上）。
const Gdiplus::Color kBoxMark(255, 255, 255, 255);
// 姓名文字色，与主面板列表行同口径。
const COLORREF kNameColor = RGB(0x3C, 0x3C, 0x3C);

// 向 path 添加一个圆角矩形（四段 90° 圆弧 + 闭合）。半径会被 clamp 到不超过
// 宽 / 高的一半。
void AddRoundRect(Gdiplus::GraphicsPath& path,
                  Gdiplus::REAL x, Gdiplus::REAL y,
                  Gdiplus::REAL w, Gdiplus::REAL h, Gdiplus::REAL radius)
{
    Gdiplus::REAL r = radius;
    if (r > w / 2.0f)
    {
        r = w / 2.0f;
    }
    if (r > h / 2.0f)
    {
        r = h / 2.0f;
    }
    const Gdiplus::REAL d = r * 2.0f;
    path.AddArc(x,         y,         d, d, 180.0f, 90.0f);
    path.AddArc(x + w - d, y,         d, d, 270.0f, 90.0f);
    path.AddArc(x + w - d, y + h - d, d, d, 0.0f,   90.0f);
    path.AddArc(x,         y + h - d, d, d, 90.0f,  90.0f);
    path.CloseFigure();
}

// 建一支本行绘制用的字体（微软雅黑，ClearType）。调用方负责 DeleteObject。
HFONT CreateRowFont(int pixelHeight, bool bold)
{
    return ::CreateFontW(
        -pixelHeight, 0, 0, 0,
        bold ? FW_SEMIBOLD : FW_NORMAL,
        FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
        L"Microsoft YaHei");
}

// 用 GDI DrawText 画一段文字（经系统 ClearType 光栅化，比 GDI+ 清晰）。
void DrawGdiText(HDC hdc, LPCWSTR text, const RECT& rc,
                 int pixelHeight, bool bold, COLORREF color, UINT dtFlags)
{
    if (text == NULL || text[0] == L'\0' || pixelHeight <= 0)
    {
        return;
    }
    HFONT hFont = CreateRowFont(pixelHeight, bold);
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

// 按底色明度选头像文字色：浅底用深字、深底用白字。
COLORREF AvatarTextColor(COLORREF bg)
{
    const int lum = (GetRValue(bg) * 299 + GetGValue(bg) * 587
                     + GetBValue(bg) * 114) / 1000;
    return (lum > 150) ? RGB(0x4A, 0x4A, 0x4A) : RGB(255, 255, 255);
}

} // anonymous namespace

MemberPickerRow::MemberPickerRow()
    : m_avatarColor(RGB(0xFF, 0x7A, 0x45))
    , m_checkState(kPickerUnchecked)
    , m_checkVisible(true)
{
}

void MemberPickerRow::SetCheckVisible(bool visible)
{
    if (m_checkVisible == visible)
    {
        return;
    }
    m_checkVisible = visible;
    Invalidate();
}

int MemberPickerRow::CheckboxStripWidth()
{
    return kCheckStripW;
}

void MemberPickerRow::SetRowData(LPCTSTR name, LPCTSTR avatarText, COLORREF avatarColor)
{
    m_name        = (name != NULL) ? name : _T("");
    m_avatarText  = (avatarText != NULL) ? avatarText : _T("");
    m_avatarColor = avatarColor;
    Invalidate();
}

void MemberPickerRow::SetCheckState(int state)
{
    if (m_checkState == state)
    {
        return;
    }
    m_checkState = state;
    Invalidate();
}

void MemberPickerRow::OnPaint(HDC hdc, const RECT& /*rcDirty*/)
{
    if (!IsVisible())
    {
        return;
    }

    const int x = m_rcItem.left;
    const int y = m_rcItem.top;
    const int w = m_rcItem.right  - m_rcItem.left;
    const int h = m_rcItem.bottom - m_rcItem.top;
    if (w <= 0 || h <= 0)
    {
        return;
    }

    const bool hasAvatar = !m_avatarText.IsEmpty();

    // ---- GDI+ 形状层：复选框 + 头像底 ----
    {
        Gdiplus::Graphics g(hdc);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

        // 复选框：条带内水平居中、行内垂直居中。
        const Gdiplus::REAL boxX = (Gdiplus::REAL)(x + (kCheckStripW - kCheckBoxSize) / 2);
        const Gdiplus::REAL boxY = (Gdiplus::REAL)(y + (h - kCheckBoxSize) / 2);
        Gdiplus::GraphicsPath box;
        AddRoundRect(box, boxX, boxY,
                     (Gdiplus::REAL)kCheckBoxSize, (Gdiplus::REAL)kCheckBoxSize,
                     kCheckBoxRadius);

        if (!m_checkVisible)
        {
            // 不可勾选的行（如转发选择窗里的部门行）：条带留空，不画复选框。
        }
        else if (m_checkState == kPickerUnchecked)
        {
            // 未选：白底 + 浅灰细边。
            Gdiplus::SolidBrush fill(kBoxFill);
            g.FillPath(&fill, &box);
            Gdiplus::Pen pen(kBoxBorder, 1.0f);
            g.DrawPath(&pen, &box);
        }
        else
        {
            // 已选 / 半选：整块填强调色，再在上面画白色的勾或方块。
            Gdiplus::SolidBrush fill(kBoxAccent);
            g.FillPath(&fill, &box);

            if (m_checkState == kPickerChecked)
            {
                // 勾：两笔折线，端点按方框比例取，缩放后形状稳定。
                Gdiplus::Pen mark(kBoxMark, 2.0f);
                mark.SetStartCap(Gdiplus::LineCapRound);
                mark.SetEndCap(Gdiplus::LineCapRound);
                const Gdiplus::REAL s = (Gdiplus::REAL)kCheckBoxSize;
                Gdiplus::PointF pts[3];
                pts[0] = Gdiplus::PointF(boxX + s * 0.24f, boxY + s * 0.52f);
                pts[1] = Gdiplus::PointF(boxX + s * 0.44f, boxY + s * 0.72f);
                pts[2] = Gdiplus::PointF(boxX + s * 0.78f, boxY + s * 0.30f);
                g.DrawLines(&mark, pts, 3);
            }
            else
            {
                // 半选：中间一条白色横杠（比"勾"更能表达"只选了一部分"）。
                const Gdiplus::REAL s = (Gdiplus::REAL)kCheckBoxSize;
                Gdiplus::SolidBrush mark(kBoxMark);
                g.FillRectangle(&mark,
                                boxX + s * 0.24f, boxY + s * 0.44f,
                                s * 0.52f, s * 0.12f + 1.0f);
            }
        }

        // 头像圆角底（部门行不画头像）。
        if (hasAvatar)
        {
            const Gdiplus::REAL avX = (Gdiplus::REAL)(x + kCheckStripW + kAvatarGap);
            const Gdiplus::REAL avY = (Gdiplus::REAL)(y + (h - kAvatarSize) / 2);
            Gdiplus::GraphicsPath avatar;
            AddRoundRect(avatar, avX, avY,
                         (Gdiplus::REAL)kAvatarSize, (Gdiplus::REAL)kAvatarSize,
                         kAvatarRadius);
            Gdiplus::SolidBrush ab(Gdiplus::Color(255,
                GetRValue(m_avatarColor), GetGValue(m_avatarColor),
                GetBValue(m_avatarColor)));
            g.FillPath(&ab, &avatar);
        }
    }

    // ---- GDI 文字层 ----

    if (hasAvatar)
    {
        const int avX = x + kCheckStripW + kAvatarGap;
        const int avY = y + (h - kAvatarSize) / 2;
        RECT avRc = { avX, avY, avX + kAvatarSize, avY + kAvatarSize };
        DrawGdiText(hdc, (LPCWSTR)m_avatarText, avRc, kAvatarPx, true,
                    AvatarTextColor(m_avatarColor),
                    DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);
    }

    // 姓名：头像右侧（无头像时紧跟复选框），一路用满到行右边缘，单行、
    // 放不下打省略号。
    int nameLeft = x + kCheckStripW + kAvatarGap;
    if (hasAvatar)
    {
        nameLeft += kAvatarSize + kAvatarGap;
    }
    RECT nameRc = { nameLeft, y, x + w, y + h };
    DrawGdiText(hdc, (LPCWSTR)m_name, nameRc, kNamePx, false, kNameColor,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

} // namespace balloonmain
