/**
 *  flamingoAdmin —— 复用 UI 控件集合。
 *
 *  本文件汇集若干跨界面复用的自绘控件：
 *    · SolidPanel<T> —— 给布局容器加纯色背景 + 单边边框（模板）。
 *    · GradientPanel —— 三色对角渐变背景容器（登录页左面板用）。
 *    · LogoBadge     —— 品牌徽标（圆角方块 + 白色 "F"）。
 *    · IconView      —— 纯展示图标（不可交互）。
 *    · IconButton    —— 图标按钮（可交互，点击发 DUIN_CLICK）。
 *  注:FlatButton 已迁到 balloonwjui::DuiButton + Variant + SetFont +
 *      SetLeadingIcon, admin 直接用 balloonui 的 DuiButton 即可。
 *
 *  balloonwj@qq.com   2026.05.21
 */
#pragma once

#include "DuiControl.h"
#include "DuiNotify.h"
#include "Controls/Layout/DuiLayout.h"
#include "Controls/Basic/DuiLabel.h"
#include "Widgets/UiHelpers.h"
#include "Widgets/IconRenderer.h"

#include <memory>

namespace flamingoadmin { namespace ui {

// 便捷构造一个 DuiLabel。
//   text：文字；pt：字号（磅）；bold：是否半粗；color：文字色。
//   dtFlags：DrawText 对齐标志，默认左对齐 + 垂直居中 + 单行。
std::unique_ptr<balloonwjui::DuiLabel> MakeLabel(
    LPCTSTR text, int pt, bool bold, COLORREF color,
    UINT dtFlags = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

// ===========================================================================
// SolidPanel<TLayout> —— 带纯色背景 + 可选单边边框的布局容器。
//
// TLayout 须为某个 balloonui 布局容器（DuiVBox / DuiHBox）。控件先铺背景与
// 边框，再调基类绘制子控件。模板实现放头文件。
// ===========================================================================
template <typename TLayout>
class SolidPanel : public TLayout
{
public:
    // 边标志位，供 SetEdgeBorder 组合。
    static const unsigned kEdgeLeft   = 1;   // 左边
    static const unsigned kEdgeTop    = 2;   // 上边
    static const unsigned kEdgeRight  = 4;   // 右边
    static const unsigned kEdgeBottom = 8;   // 下边

    // 设置背景填充色。不调用则不画背景（透明）。
    void SetFillColor(COLORREF color)
    {
        m_hasFill   = true;
        m_fillColor = color;
    }

    // 设置边框：在 edgeFlags 指定的边上画 1px 的 color 边线。
    //   edgeFlags：kEdgeLeft / kEdgeTop / kEdgeRight / kEdgeBottom 的按位或。
    void SetEdgeBorder(unsigned edgeFlags, COLORREF color)
    {
        m_edgeFlags = edgeFlags;
        m_edgeColor = color;
    }

    void OnPaint(HDC hdc, const RECT& rcDirty) override
    {
        if (!this->m_bVisible)
        {
            return;
        }
        const RECT& rc = this->m_rcItem;
        if (m_hasFill)
        {
            FillSolidRect(hdc, rc, m_fillColor);
        }
        if (m_edgeFlags & kEdgeLeft)
        {
            FillLine(hdc, rc.left, rc.top, 1, rc.bottom - rc.top, m_edgeColor);
        }
        if (m_edgeFlags & kEdgeTop)
        {
            FillLine(hdc, rc.left, rc.top, rc.right - rc.left, 1, m_edgeColor);
        }
        if (m_edgeFlags & kEdgeRight)
        {
            FillLine(hdc, rc.right - 1, rc.top, 1, rc.bottom - rc.top, m_edgeColor);
        }
        if (m_edgeFlags & kEdgeBottom)
        {
            FillLine(hdc, rc.left, rc.bottom - 1, rc.right - rc.left, 1, m_edgeColor);
        }
        TLayout::OnPaint(hdc, rcDirty);   // 再绘制子控件
    }

private:
    bool     m_hasFill   = false;
    COLORREF m_fillColor = RGB(255, 255, 255);
    unsigned m_edgeFlags = 0;
    COLORREF m_edgeColor = RGB(0, 0, 0);
};

// 纵向 / 横向纯色面板的便捷别名。
typedef SolidPanel<balloonwjui::DuiVBox> SolidVBox;
typedef SolidPanel<balloonwjui::DuiHBox> SolidHBox;

// ===========================================================================
// GradientPanel —— 三色对角渐变背景的纵向容器。
// 用于登录页左侧品牌面板。先铺渐变，再绘制子控件。
// ===========================================================================
class GradientPanel : public balloonwjui::DuiVBox
{
public:
    // 设置渐变三停靠色（位置固定 0 / 0.55 / 1.0，方向左上→右下）。
    void SetGradient(COLORREF c0, COLORREF c1, COLORREF c2);

    void OnPaint(HDC hdc, const RECT& rcDirty) override;

private:
    COLORREF m_c0 = RGB(255, 90, 68);
    COLORREF m_c1 = RGB(217, 58, 141);
    COLORREF m_c2 = RGB(122, 61, 153);
};

// ===========================================================================
// LogoBadge —— 品牌徽标：flamingo 主色圆角方块 + 居中白色 "F"。
// ===========================================================================
class LogoBadge : public balloonwjui::DuiControl
{
public:
    // 设置徽标内字号（磅）。默认随徽标尺寸自适应不做，固定由调用方给定。
    void SetGlyphPointSize(int pt) { m_glyphPt = pt; }

    void OnPaint(HDC hdc, const RECT& rcDirty) override;

private:
    int m_glyphPt = 12;   // 白色 "F" 字号
};

// 把 LogoBadge 的视觉（flamingo 主色圆角方块 + 居中白色 "F"）渲染成一个带
// alpha 通道的 HICON，供主窗口设置标题栏 / 任务栏 / Alt+Tab 图标 —— 使窗口
// 图标与侧边栏左上角的品牌徽标保持一致。
//   size：图标边长（像素，正方形，须 > 0）。
//   返回：新建的 HICON，调用方负责 DestroyIcon；失败返回 nullptr。
HICON CreateLogoBadgeIcon(int size);

// ===========================================================================
// IconView —— 纯展示用图标控件（不可交互）。
// ===========================================================================
class IconView : public balloonwjui::DuiControl
{
public:
    // 设置图标种类。
    void SetIcon(Icon icon) { m_icon = icon; }
    // 设置图标颜色。
    void SetColor(COLORREF color) { m_color = color; }
    // 设置图标边长（像素，在控件内居中绘制）。
    void SetIconSize(int px) { m_iconSize = px; }

    void OnPaint(HDC hdc, const RECT& rcDirty) override;

private:
    Icon     m_icon     = Icon::Dashboard;
    COLORREF m_color     = RGB(0x6f, 0x76, 0x82);
    int      m_iconSize  = 16;
};

// ===========================================================================
// IconButton —— 图标按钮：仅一个图标，hover 时有圆角底色，点击发 DUIN_CLICK。
// ===========================================================================
class IconButton : public balloonwjui::DuiControl
{
public:
    // 析构：取消 tooltip 注册，避免 DuiToolTipMgr 持有悬空指针。
    ~IconButton() override;

    // 设置图标种类。
    void SetIcon(Icon icon) { m_icon = icon; }
    // 设置图标边长（像素）。
    void SetIconSize(int px) { m_iconSize = px; }
    // 设置常态色、hover 色、hover 时的圆角底色。
    void SetColors(COLORREF normal, COLORREF hover, COLORREF hoverBg);
    // 注册鼠标悬停提示文字（基于 balloonui 的 DuiToolTipMgr，悬停约 0.5 秒弹出）。
    void SetTooltip(LPCTSTR text);

    void OnPaint(HDC hdc, const RECT& rcDirty) override;
    bool OnMouseEnter() override;
    bool OnMouseLeave() override;
    bool OnLButtonUp(POINT pt, UINT mkFlags) override;
    bool OnSetCursor(POINT pt) override;

private:
    Icon     m_icon      = Icon::Bell;
    int      m_iconSize  = 16;
    COLORREF m_normalClr = RGB(0x4a, 0x51, 0x60);
    COLORREF m_hoverClr  = RGB(0x1c, 0x22, 0x30);
    COLORREF m_hoverBg   = RGB(0xea, 0xec, 0xef);
};

}} // namespace flamingoadmin::ui
