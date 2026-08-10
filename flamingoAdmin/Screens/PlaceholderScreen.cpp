/**
 *  flamingoAdmin —— 占位界面实现。
 *
 *  balloonwj@qq.com   2026.05.21
 */
#include "stdafx.h"
#include "Screens/PlaceholderScreen.h"
#include "Widgets/UiHelpers.h"
#include "Theme/AdminTheme.h"

namespace flamingoadmin {

// 本文件在 namespace flamingoadmin 内，theme:: 直接解析到 flamingoadmin::theme。

namespace {

// ---- 占位界面布局常量（96 dpi 逻辑像素）----
const int kIconBoxSize  = 72;   // 图标方块边长
const int kIconBoxRound = 18;   // 图标方块圆角
const int kGlyphSize    = 32;   // 方块内图标边长
const int kTitleHeight  = 30;   // 标题行高
const int kSubHeight    = 22;   // 副标题行高
const int kGapBoxTitle  = 16;   // 方块与标题的间距
const int kGapTitleSub  = 4;    // 标题与副标题的间距

} // anonymous namespace

void PlaceholderScreen::SetContent(ui::Icon icon, LPCTSTR title, LPCTSTR subtitle)
{
    m_icon     = icon;
    m_title    = title;
    m_subtitle = subtitle;
    Invalidate();
}

void PlaceholderScreen::OnPaint(HDC hdc, const RECT& /*rcDirty*/)
{
    if (!m_bVisible)
    {
        return;
    }

    // 整块内容（方块 + 标题 + 副标题）的总高度，用于在控件内垂直居中。
    const int blockHeight = kIconBoxSize + kGapBoxTitle + kTitleHeight
                            + kGapTitleSub + kSubHeight;
    const int centerX = (m_rcItem.left + m_rcItem.right) / 2;
    const int centerY = (m_rcItem.top + m_rcItem.bottom) / 2;
    int y = centerY - blockHeight / 2;

    // 图标方块。
    RECT rcBox =
    {
        centerX - kIconBoxSize / 2, y,
        centerX + kIconBoxSize / 2, y + kIconBoxSize,
    };
    ui::FillRoundRect(hdc, rcBox, theme::kInk50, kIconBoxRound);
    RECT rcGlyph =
    {
        centerX - kGlyphSize / 2, y + (kIconBoxSize - kGlyphSize) / 2,
        centerX + kGlyphSize / 2, y + (kIconBoxSize + kGlyphSize) / 2,
    };
    ui::DrawIcon(hdc, m_icon, rcGlyph, theme::kInk300);
    y += kIconBoxSize + kGapBoxTitle;

    // 标题。
    RECT rcTitle = { m_rcItem.left, y, m_rcItem.right, y + kTitleHeight };
    ui::DrawTextLine(hdc, m_title, rcTitle, theme::kInk600,
                     ui::UiFont(theme::kFontSizeHeading, true),
                     DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    y += kTitleHeight + kGapTitleSub;

    // 副标题。
    RECT rcSub = { m_rcItem.left, y, m_rcItem.right, y + kSubHeight };
    ui::DrawTextLine(hdc, m_subtitle, rcSub, theme::kInk400,
                     ui::UiFont(theme::kFontSizeNormal, false),
                     DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
}

} // namespace flamingoadmin
