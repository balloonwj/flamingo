/**
 *  flamingoAdmin —— 复用 UI 控件实现。
 *
 *  balloonwj@qq.com   2026.05.21
 */
#include "stdafx.h"
#include "Widgets/Widgets.h"
#include "Theme/AdminTheme.h"

#include "Controls/Feedback/DuiToolTip.h"

namespace flamingoadmin { namespace ui {

namespace theme = flamingoadmin::theme;

std::unique_ptr<balloonwjui::DuiLabel> MakeLabel(
    LPCTSTR text, int pt, bool bold, COLORREF color, UINT dtFlags)
{
    auto label = std::make_unique<balloonwjui::DuiLabel>();
    label->SetText(text);
    label->SetTextColor(color);
    label->SetFont(UiFont(pt, bold));
    label->SetTextAlign(dtFlags);
    return label;
}

// ===========================================================================
// GradientPanel
// ===========================================================================
void GradientPanel::SetGradient(COLORREF c0, COLORREF c1, COLORREF c2)
{
    m_c0 = c0;
    m_c1 = c1;
    m_c2 = c2;
}

void GradientPanel::OnPaint(HDC hdc, const RECT& rcDirty)
{
    if (!m_bVisible)
    {
        return;
    }
    FillDiagonalGradient(hdc, m_rcItem, m_c0, m_c1, m_c2);
    balloonwjui::DuiVBox::OnPaint(hdc, rcDirty);   // 再绘制子控件
}

// ===========================================================================
// LogoBadge
// ===========================================================================
void LogoBadge::OnPaint(HDC hdc, const RECT& rcDirty)
{
    if (!m_bVisible)
    {
        return;
    }
    // flamingo 主色圆角方块。
    FillRoundRect(hdc, m_rcItem, theme::kFlamingo500, theme::kRadiusMd);
    // 居中白色 "F"。
    DrawTextLine(hdc, _T("F"), m_rcItem, theme::kInk0,
                 UiFont(m_glyphPt, true),
                 DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
}

HICON CreateLogoBadgeIcon(int size)
{
    if (size <= 0)
    {
        return nullptr;
    }

    // 徽标设计基准尺寸：侧边栏中 LogoBadge 为 32x32。圆角半径与 "F" 字号都
    // 按目标尺寸相对此基准等比缩放，使任意尺寸下观感都与侧边栏徽标一致。
    const int kBadgeRefSize = 32;
    int radius  = theme::kRadiusMd * size / kBadgeRefSize;
    int glyphPt = theme::kFontSizeTitle * size / kBadgeRefSize;
    if (glyphPt < 1)
    {
        glyphPt = 1;
    }

    // 32bpp 自顶向下 ARGB DIB；CreateDIBSection 会把像素清零（初始全透明）。
    BITMAPV5HEADER header;
    ::ZeroMemory(&header, sizeof(header));
    header.bV5Size        = sizeof(BITMAPV5HEADER);
    header.bV5Width       = size;
    header.bV5Height      = -size;
    header.bV5Planes      = 1;
    header.bV5BitCount    = 32;
    header.bV5Compression = BI_BITFIELDS;
    header.bV5RedMask     = 0x00FF0000;
    header.bV5GreenMask   = 0x0000FF00;
    header.bV5BlueMask    = 0x000000FF;
    header.bV5AlphaMask   = 0xFF000000;

    HDC hdcScreen = ::GetDC(nullptr);
    void* bits = nullptr;
    HBITMAP hbmColor = ::CreateDIBSection(
        hdcScreen, reinterpret_cast<BITMAPINFO*>(&header),
        DIB_RGB_COLORS, &bits, nullptr, 0);
    ::ReleaseDC(nullptr, hdcScreen);
    if (hbmColor == nullptr)
    {
        return nullptr;
    }

    // 在 DIB 上重绘 LogoBadge：FillRoundRect 用 GDI+ 抗锯齿填充圆角方块，
    // 会写入正确的 alpha（方块内不透明、圆角外透明）；其上的白色 "F" 由
    // GDI 文字绘制，落在已不透明的像素上、只改 RGB，故 "F" 同样不透明。
    HDC hdcMem = ::CreateCompatibleDC(nullptr);
    HGDIOBJ hbmOld = ::SelectObject(hdcMem, hbmColor);
    RECT rc = { 0, 0, size, size };
    FillRoundRect(hdcMem, rc, theme::kFlamingo500, radius);
    DrawTextLine(hdcMem, _T("F"), rc, theme::kInk0, UiFont(glyphPt, true),
                 DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    ::SelectObject(hdcMem, hbmOld);
    ::DeleteDC(hdcMem);

    // 包装成图标。ICONINFO 须带掩码位图占位；32bpp alpha 图标以 alpha 通道
    // 为准、掩码不参与显示，建一张同尺寸单色位图占位即可。
    HBITMAP hbmMask = ::CreateBitmap(size, size, 1, 1, nullptr);
    ICONINFO iconInfo;
    ::ZeroMemory(&iconInfo, sizeof(iconInfo));
    iconInfo.fIcon    = TRUE;
    iconInfo.hbmColor = hbmColor;
    iconInfo.hbmMask  = hbmMask;
    HICON hIcon = ::CreateIconIndirect(&iconInfo);

    // CreateIconIndirect 已复制位图数据，原始位图可立即释放。
    ::DeleteObject(hbmColor);
    ::DeleteObject(hbmMask);
    return hIcon;
}

// ===========================================================================
// IconView
// ===========================================================================
void IconView::OnPaint(HDC hdc, const RECT& rcDirty)
{
    if (!m_bVisible)
    {
        return;
    }
    // 在控件矩形内居中放置一个 m_iconSize 见方的图标。
    const int cx = (m_rcItem.left + m_rcItem.right) / 2;
    const int cy = (m_rcItem.top + m_rcItem.bottom) / 2;
    RECT rcIcon =
    {
        cx - m_iconSize / 2, cy - m_iconSize / 2,
        cx + m_iconSize / 2, cy + m_iconSize / 2,
    };
    DrawIcon(hdc, m_icon, rcIcon, m_color);
}

// ===========================================================================
// IconButton
// ===========================================================================
IconButton::~IconButton()
{
    // 控件销毁时取消 tooltip 注册，否则管理器命中表里会留下悬空指针。
    balloonwjui::DuiToolTipMgr::Inst().Unregister(this);
}

void IconButton::SetColors(COLORREF normal, COLORREF hover, COLORREF hoverBg)
{
    m_normalClr = normal;
    m_hoverClr  = hover;
    m_hoverBg   = hoverBg;
}

void IconButton::SetTooltip(LPCTSTR text)
{
    // 向进程级 tooltip 管理器登记本控件的提示文字；宿主在 hover 变化时
    // 自动调用 DuiToolTipMgr 的 OnHover / OnLeave 弹出 / 收起提示。
    balloonwjui::DuiToolTipMgr::Inst().Register(this, text);
}

void IconButton::OnPaint(HDC hdc, const RECT& rcDirty)
{
    if (!m_bVisible)
    {
        return;
    }
    if (m_bHover)
    {
        FillRoundRect(hdc, m_rcItem, m_hoverBg, theme::kRadiusMd);
    }
    const int cx = (m_rcItem.left + m_rcItem.right) / 2;
    const int cy = (m_rcItem.top + m_rcItem.bottom) / 2;
    RECT rcIcon =
    {
        cx - m_iconSize / 2, cy - m_iconSize / 2,
        cx + m_iconSize / 2, cy + m_iconSize / 2,
    };
    DrawIcon(hdc, m_icon, rcIcon, m_bHover ? m_hoverClr : m_normalClr);
}

bool IconButton::OnMouseEnter()
{
    m_bHover = true;
    Invalidate();
    return true;
}

bool IconButton::OnMouseLeave()
{
    m_bHover = false;
    Invalidate();
    return true;
}

bool IconButton::OnLButtonUp(POINT /*pt*/, UINT /*mkFlags*/)
{
    NotifyParent(balloonwjui::DUIN_CLICK);
    return true;
}

bool IconButton::OnSetCursor(POINT /*pt*/)
{
    ::SetCursor(::LoadCursor(nullptr, IDC_HAND));
    return true;
}

// 注:FlatButton 类已迁到 balloonwjui::DuiButton + Variant + SetTextPointSize
//    + SetLeadingIcon。原实现已删除, 全部 admin 调用现在直接走 DuiButton。

}} // namespace flamingoadmin::ui
