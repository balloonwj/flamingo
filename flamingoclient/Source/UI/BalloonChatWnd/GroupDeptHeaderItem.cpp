/**
 *  GroupDeptHeaderItem.cpp
 *
 *  部门树部门标题行的绘制与交互实现。悬停底与折叠箭头用 GDI+ 抗锯齿，文字用 GDI DrawText。
 *
 *  本 .cpp 不 include Flamingo 的 stdafx.h —— 它要被 Flamingo.vcxproj 与 FlamingoUnit.vcxproj 共用。
 *  在 Flamingo.vcxproj 里需标记 PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-10-06
 */

// ---- balloonui 头文件要求的 ATL/WTL 前置依赖（与 GroupMemberItem.cpp 相同）----
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

#include "GroupDeptHeaderItem.h"
#include "GroupMemberList.h"          // 点击时请父列表切换折叠
#include "../../Common/I18n/I18n.h"   // UiFontName：界面字体随界面语言

namespace balloonchatwnd {

namespace {

// ---- 几何 / 配色 -------------------------------------------------------

const int kEdgePad        = 10;   // 行左内边距，与成员行相同
const int kArrowBoxPx     = 12;   // 折叠箭头所占的方框边长
const int kArrowHalfPx    = 4;    // 箭头三角形的半边长
const int kArrowGapPx     = 4;    // 箭头与部门名之间的间距
const int kNamePx         = 13;   // 部门名字号，与成员昵称相同
const int kCountGapPx     = 6;    // 部门名与人数之间的间距
const int kMaxIndentDepth = 6;    // 缩进封顶的层级：再深的部门按这一层缩进，避免名字被挤没
const int kHoverInsetXPx  = 3;    // 悬停底左右各内缩的像素，与成员行相同
const int kHoverInsetYPx  = 2;    // 悬停底上下各内缩的像素，与成员行相同

const Gdiplus::REAL kHoverRadius = 5.0f;                            // 悬停底圆角，与成员行相同
const Gdiplus::Color kHoverFill(255, 0xF2, 0xF1, 0xEE);             // 悬停底，与成员行相同
const Gdiplus::Color kArrowColor(255, 0x88, 0x88, 0x88);            // 折叠箭头
const COLORREF       kNameColor  = RGB(0x3C, 0x3C, 0x3C);           // 部门名
const COLORREF       kCountColor = RGB(0x99, 0x99, 0x99);           // 人数

// 向 path 添加一个圆角矩形
void AddRoundRect(Gdiplus::GraphicsPath& path, Gdiplus::REAL x, Gdiplus::REAL y,
                  Gdiplus::REAL w, Gdiplus::REAL h, Gdiplus::REAL radius)
{
    Gdiplus::REAL r = radius;
    if (r > w / 2.0f)
        r = w / 2.0f;
    if (r > h / 2.0f)
        r = h / 2.0f;
    const Gdiplus::REAL d = r * 2.0f;
    path.AddArc(x,         y,         d, d, 180.0f, 90.0f);
    path.AddArc(x + w - d, y,         d, d, 270.0f, 90.0f);
    path.AddArc(x + w - d, y + h - d, d, d, 0.0f,   90.0f);
    path.AddArc(x,         y + h - d, d, d, 90.0f,  90.0f);
    path.CloseFigure();
}

// 建一个界面字体（调用方负责 DeleteObject）
HFONT CreateUiFont(int pixelHeight, bool bold)
{
    return ::CreateFontW(-pixelHeight, 0, 0, 0, bold ? FW_SEMIBOLD : FW_NORMAL, FALSE, FALSE, FALSE,
                         DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                         DEFAULT_PITCH | FF_DONTCARE, i18n::UiFontName());
}

} // anonymous namespace

int GroupDeptIndentPx(int depth)
{
    if (depth <= 0)
        return 0;
    if (depth > kMaxIndentDepth)
        depth = kMaxIndentDepth;
    return depth * kGroupDeptIndentPx;
}

GroupDeptHeaderItem::GroupDeptHeaderItem()
    : m_deptId(0)
    , m_count(0)
    , m_depth(0)
    , m_collapsed(false)
    , m_searching(false)
{
}

void GroupDeptHeaderItem::SetDept(int64_t deptId, const CString& name, int count, int depth)
{
    m_deptId = deptId;
    m_name   = name;
    m_count  = count;
    m_depth  = depth;
    Invalidate();
}

void GroupDeptHeaderItem::SetCollapsed(bool collapsed)
{
    if (m_collapsed == collapsed)
        return;
    m_collapsed = collapsed;
    Invalidate();
}

void GroupDeptHeaderItem::SetSearching(bool searching)
{
    if (m_searching == searching)
        return;
    m_searching = searching;
    Invalidate();
}

bool GroupDeptHeaderItem::OnLButtonDown(POINT pt, UINT mkFlags)
{
    GroupMemberList* list = dynamic_cast<GroupMemberList*>(GetParent());
    if (list == NULL || m_searching)
    {
        return DuiControl::OnLButtonDown(pt, mkFlags);
    }
    list->ToggleDeptCollapsed(m_deptId);
    return true;
}

bool GroupDeptHeaderItem::OnMouseEnter()
{
    const bool r = DuiControl::OnMouseEnter();
    Invalidate();
    return r;
}

bool GroupDeptHeaderItem::OnMouseLeave()
{
    const bool r = DuiControl::OnMouseLeave();
    Invalidate();
    return r;
}

void GroupDeptHeaderItem::OnPaint(HDC hdc, const RECT& /*rcDirty*/)
{
    const int x = m_rcItem.left;
    const int y = m_rcItem.top;
    const int w = m_rcItem.right - m_rcItem.left;
    const int h = m_rcItem.bottom - m_rcItem.top;
    if (w <= 0 || h <= 0)
        return;

    Gdiplus::Graphics g(hdc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

    //悬停：浅灰圆角底（与成员行同一画法）
    if (IsHover())
    {
        Gdiplus::GraphicsPath hp;
        AddRoundRect(hp, (Gdiplus::REAL)(x + kHoverInsetXPx), (Gdiplus::REAL)(y + kHoverInsetYPx),
                     (Gdiplus::REAL)(w - kHoverInsetXPx * 2), (Gdiplus::REAL)(h - kHoverInsetYPx * 2),
                     kHoverRadius);
        Gdiplus::SolidBrush hb(kHoverFill);
        g.FillPath(&hb, &hp);
    }

    //折叠箭头：折叠时朝右，展开（含搜索中）时朝下
    const int boxLeft = x + kEdgePad + GroupDeptIndentPx(m_depth);
    const Gdiplus::REAL cx = (Gdiplus::REAL)boxLeft + (Gdiplus::REAL)kArrowBoxPx / 2.0f;
    const Gdiplus::REAL cy = (Gdiplus::REAL)y + (Gdiplus::REAL)h / 2.0f;
    const Gdiplus::REAL a = (Gdiplus::REAL)kArrowHalfPx;
    Gdiplus::PointF pts[3];
    if (m_collapsed && !m_searching)
    {
        pts[0] = Gdiplus::PointF(cx - a / 2.0f, cy - a);
        pts[1] = Gdiplus::PointF(cx + a / 2.0f + 1.0f, cy);
        pts[2] = Gdiplus::PointF(cx - a / 2.0f, cy + a);
    }
    else
    {
        pts[0] = Gdiplus::PointF(cx - a, cy - a / 2.0f);
        pts[1] = Gdiplus::PointF(cx + a, cy - a / 2.0f);
        pts[2] = Gdiplus::PointF(cx, cy + a / 2.0f + 1.0f);
    }
    Gdiplus::SolidBrush ab(kArrowColor);
    g.FillPolygon(&ab, pts, 3);

    //部门名（加粗）与人数（灰色）：人数紧跟在名字后面，名字过长时先截断名字
    HFONT nameFont = CreateUiFont(kNamePx, true);
    HFONT countFont = CreateUiFont(kNamePx, false);
    if (nameFont == NULL || countFont == NULL)
    {
        if (nameFont != NULL)
            ::DeleteObject(nameFont);
        if (countFont != NULL)
            ::DeleteObject(countFont);
        return;
    }
    CString countText;
    countText.Format(L"(%d)", m_count);
    HFONT oldFont = (HFONT)::SelectObject(hdc, countFont);
    SIZE countSize = { 0, 0 };
    ::GetTextExtentPoint32W(hdc, countText, countText.GetLength(), &countSize);
    ::SelectObject(hdc, nameFont);
    SIZE nameSize = { 0, 0 };
    ::GetTextExtentPoint32W(hdc, m_name, m_name.GetLength(), &nameSize);

    const int nameLeft = boxLeft + kArrowBoxPx + kArrowGapPx;
    const int right = x + w - kEdgePad;
    int nameRight = nameLeft + nameSize.cx;
    const int maxNameRight = right - kCountGapPx - countSize.cx;
    if (nameRight > maxNameRight)
        nameRight = maxNameRight;

    const int oldBk = ::SetBkMode(hdc, TRANSPARENT);
    const COLORREF oldColor = ::SetTextColor(hdc, kNameColor);
    if (nameRight > nameLeft)
    {
        RECT nameRc = { nameLeft, y, nameRight, y + h };
        ::DrawTextW(hdc, m_name, -1, &nameRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
    ::SelectObject(hdc, countFont);
    ::SetTextColor(hdc, kCountColor);
    const int countLeft = ((nameRight > nameLeft) ? nameRight : nameLeft) + kCountGapPx;
    RECT countRc = { countLeft, y, right, y + h };
    ::DrawTextW(hdc, countText, -1, &countRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    ::SetTextColor(hdc, oldColor);
    ::SetBkMode(hdc, oldBk);
    ::SelectObject(hdc, oldFont);
    ::DeleteObject(nameFont);
    ::DeleteObject(countFont);
}

} // namespace balloonchatwnd
