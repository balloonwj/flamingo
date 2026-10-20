/**
 *  ChatCardPaint.cpp
 *
 *  聊天卡片共用绘制函数的实现，见 ChatCardPaint.h。
 *
 *  本 .cpp 不 include Flamingo 的 stdafx.h —— 它要被 Flamingo.vcxproj 与
 *  FlamingoUnit.vcxproj 共用。在 Flamingo.vcxproj 里需标记 PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-10-14
 */

// ---- balloonui 头文件要求的 ATL/WTL 前置依赖，与同目录 ChatFileCard.cpp 相同 ----
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#define WINVER          0x0601      // 目标系统下限 = Windows 7
#define _WIN32_WINNT    0x0601
#define _WIN32_IE       0x0601
#define _RICHEDIT_VER   0x0300      // RichEdit 3.0，WTL 10 要求的下限
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

#include "ChatCardPaint.h"
#include "ChatCardStyle.h"
#include "ChatIcons.h"        // DrawChatIcon：图标块与类型标签里的线条图标
#include "ChatPaintUtil.h"    // AddRoundRect / DrawGdiText / MeasureTextWidth

namespace balloonchatwnd { namespace cardpaint {

namespace {

using chatpaint::AddRoundRect;
using chatpaint::DrawGdiText;
using chatpaint::MeasureTextWidth;

// 不透明的 GDI+ 颜色
const BYTE kOpaque = 255;

// 描 1 像素边时圆角半径的内缩量：边线画在最外一圈像素的中心上，比底板外沿向内收半个像素
const Gdiplus::REAL kHairlineInset = 0.5f;

// 把 COLORREF 换成 GDI+ 颜色
Gdiplus::Color ToGdip(COLORREF c, BYTE alpha)
{
    return Gdiplus::Color(alpha, GetRValue(c), GetGValue(c), GetBValue(c));
}

// 填一个圆角矩形
void FillRound(Gdiplus::Graphics& g, const RECT& rc, int radius, COLORREF color, BYTE alpha)
{
    Gdiplus::GraphicsPath path;
    AddRoundRect(path, (Gdiplus::REAL)rc.left, (Gdiplus::REAL)rc.top, (Gdiplus::REAL)(rc.right - rc.left),
                 (Gdiplus::REAL)(rc.bottom - rc.top), (Gdiplus::REAL)radius);
    Gdiplus::SolidBrush brush(ToGdip(color, alpha));
    g.FillPath(&brush, &path);
}

// 沿圆角矩形的最外一圈像素描 1 像素边。GDI+ 默认的像素偏移模式下像素中心落在整数坐标上，线宽 1 的笔沿
// left / top 与 right - 1 / bottom - 1 这几条线画，正好落满一列（行）像素；画在半像素上会糊成两列浅色
void StrokeRound(Gdiplus::Graphics& g, const RECT& rc, int radius, COLORREF color)
{
    Gdiplus::GraphicsPath path;
    AddRoundRect(path, (Gdiplus::REAL)rc.left, (Gdiplus::REAL)rc.top, (Gdiplus::REAL)(rc.right - rc.left - 1),
                 (Gdiplus::REAL)(rc.bottom - rc.top - 1), (Gdiplus::REAL)radius - kHairlineInset);
    Gdiplus::Pen pen(ToGdip(color, kOpaque), 1.0f);
    g.DrawPath(&pen, &path);
}

// 单行文字的绘制标志：垂直居中、不解释 & 前缀
const UINT kSingleLine = DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX;

// 底部类型标签截短时至少要放得下的宽度（按字号计的字数）：一个字加省略号，再窄就只画图标
const int kFooterLabelMinChars = 3;

} // namespace

CardChipColors CardChipColorsOf(int tone)
{
    CardChipColors c;
    switch (tone)
    {
    //进行中：绿
    case kCardChipLive:
        c.m_text = cardstyle::kChipLiveText;
        c.m_fill = cardstyle::kChipLiveFill;
        break;
    //待处理、倒计时：橙
    case kCardChipPending:
        c.m_text = cardstyle::kChipPendingText;
        c.m_fill = cardstyle::kChipPendingFill;
        break;
    //拒绝、重要、失败：红
    case kCardChipBad:
        c.m_text = cardstyle::kChipBadText;
        c.m_fill = cardstyle::kChipBadFill;
        break;
    //中性提示：蓝
    case kCardChipNeutral:
        c.m_text = cardstyle::kChipNeutralText;
        c.m_fill = cardstyle::kChipNeutralFill;
        break;
    //已结束，以及不画标签与未知取值：灰
    default:
        c.m_text = cardstyle::kChipEndedText;
        c.m_fill = cardstyle::kChipEndedFill;
        break;
    }
    return c;
}

CardButtonColors CardButtonColorsOf(int level, int state)
{
    CardButtonColors c;
    c.m_hasFill = false;
    c.m_fill = cardstyle::kCardFill;
    c.m_hasBorder = false;
    c.m_border = cardstyle::kCardBorder;
    c.m_bold = false;
    const bool disabled = (state == kCardButtonDisabled);
    const bool hover = (state == kCardButtonHover);
    switch (level)
    {
    //主要：实心橙色胶囊，白字半粗
    case kCardButtonPrimary:
        c.m_hasFill = true;
        c.m_fill = disabled ? cardstyle::kPrimaryDisabledFill
                            : (hover ? cardstyle::kPrimaryHoverFill : cardstyle::kPrimaryFill);
        c.m_text = disabled ? cardstyle::kPrimaryDisabledText : cardstyle::kPrimaryText;
        c.m_bold = true;
        break;
    //次要：只有灰色文字
    case kCardButtonSecondary:
        c.m_text = disabled ? cardstyle::kSecondaryDisabledText
                            : (hover ? cardstyle::kSecondaryHoverText : cardstyle::kSecondaryText);
        break;
    //查看（以及未知取值）：白底橙色描边胶囊
    default:
        c.m_hasFill = true;
        c.m_fill = (hover && !disabled) ? cardstyle::kViewHoverFill : cardstyle::kViewFill;
        c.m_hasBorder = true;
        c.m_border = disabled ? cardstyle::kViewDisabledBorder : cardstyle::kViewBorder;
        c.m_text = disabled ? cardstyle::kViewDisabledText : cardstyle::kViewText;
        break;
    }
    return c;
}

void PaintCardFrame(Gdiplus::Graphics& g, const RECT& rc)
{
    if (rc.right <= rc.left || rc.bottom <= rc.top)
        return;
    //先画向下错开 1 像素的淡投影，再盖上白色底板，投影只在底边下方露出一行
    RECT shadow = rc;
    ::OffsetRect(&shadow, 0, cardstyle::kCardShadowOffsetY);
    FillRound(g, shadow, cardstyle::kCardRadius, cardstyle::kCardShadow, (BYTE)cardstyle::kCardShadowAlpha);
    FillRound(g, rc, cardstyle::kCardRadius, cardstyle::kCardFill, kOpaque);
    StrokeRound(g, rc, cardstyle::kCardRadius, cardstyle::kCardBorder);
}

void PaintCardTile(Gdiplus::Graphics& g, int left, int top, const ChatCardTypeStyle& style)
{
    if (!style.m_hasTile)
        return;
    const RECT tile = { left, top, left + cardstyle::kTileSize, top + cardstyle::kTileSize };
    FillRound(g, tile, cardstyle::kTileRadius, style.m_tileFill, kOpaque);
    const int glyphInset = (cardstyle::kTileSize - cardstyle::kTileGlyphSize) / 2;
    DrawChatIcon(g, style.m_glyph, (Gdiplus::REAL)(left + glyphInset), (Gdiplus::REAL)(top + glyphInset),
                 (Gdiplus::REAL)cardstyle::kTileGlyphSize, ToGdip(style.m_glyphColor, kOpaque), cardstyle::kTileGlyphStroke);
}

int MeasureCardChipWidth(HDC hdc, const CString& text)
{
    if (text.IsEmpty())
        return 0;
    return cardstyle::kChipPadH * 2 + cardstyle::kChipDotSize + cardstyle::kChipDotGap
         + MeasureTextWidth(hdc, (LPCWSTR)text, cardstyle::kChipPx);
}

int PaintCardChip(HDC hdc, Gdiplus::Graphics& g, int left, int rowTop, int rowBottom, const CString& text, int tone)
{
    if (text.IsEmpty() || tone == kCardChipNone)
        return left;
    const CardChipColors colors = CardChipColorsOf(tone);
    const int width = MeasureCardChipWidth(hdc, text);
    const int top = rowTop + (rowBottom - rowTop - cardstyle::kChipH) / 2;
    const RECT chip = { left, top, left + width, top + cardstyle::kChipH };
    FillRound(g, chip, cardstyle::kChipRadius, colors.m_fill, kOpaque);

    const int dotLeft = left + cardstyle::kChipPadH;
    const int dotTop = top + (cardstyle::kChipH - cardstyle::kChipDotSize) / 2;
    Gdiplus::SolidBrush dot(ToGdip(colors.m_text, kOpaque));
    g.FillEllipse(&dot, dotLeft, dotTop, cardstyle::kChipDotSize, cardstyle::kChipDotSize);

    RECT textRc = chip;
    textRc.left = dotLeft + cardstyle::kChipDotSize + cardstyle::kChipDotGap;
    DrawGdiText(hdc, (LPCWSTR)text, textRc, cardstyle::kChipPx, false, colors.m_text, DT_LEFT | kSingleLine | DT_NOCLIP);
    return chip.right;
}

int MeasureCardButtonWidth(HDC hdc, const CString& text, int level)
{
    const int textW = MeasureTextWidth(hdc, (LPCWSTR)text, cardstyle::kButtonPx, level == kCardButtonPrimary);
    if (level == kCardButtonSecondary)
        return textW;
    return textW + cardstyle::kButtonPadH * 2;
}

void PaintCardButton(HDC hdc, Gdiplus::Graphics& g, const RECT& rc, const CString& text, int level, int state)
{
    const CardButtonColors colors = CardButtonColorsOf(level, state);
    if (colors.m_hasFill)
        FillRound(g, rc, cardstyle::kButtonRadius, colors.m_fill, kOpaque);
    if (colors.m_hasBorder)
        StrokeRound(g, rc, cardstyle::kButtonRadius, colors.m_border);
    DrawGdiText(hdc, (LPCWSTR)text, rc, cardstyle::kButtonPx, colors.m_bold, colors.m_text, DT_CENTER | kSingleLine | DT_NOCLIP);
}

CardFooterLayout LayoutCardFooter(const RECT& footerRc, int labelWidth, const std::vector<int>& buttonWidths)
{
    CardFooterLayout lay;
    ::SetRectEmpty(&lay.m_labelRect);
    lay.m_labelTruncated = false;
    lay.m_buttonRects.resize(buttonWidths.size());

    //按钮：从最右边一个起向左排，垂直居中
    const int btnTop = footerRc.top + ((footerRc.bottom - footerRc.top) - cardstyle::kButtonH) / 2;
    int right = footerRc.right - cardstyle::kCardPadH;
    for (size_t k = buttonWidths.size(); k > 0; --k)
    {
        const size_t i = k - 1;
        RECT& rc = lay.m_buttonRects[i];
        rc.right = right;
        rc.left = right - buttonWidths[i];
        rc.top = btnTop;
        rc.bottom = btnTop + cardstyle::kButtonH;
        right = rc.left - cardstyle::kButtonGap;
    }

    //类型标签：从左边起，最多排到最左边那个按钮左侧再留一段间距的位置，放不下就截短
    if (labelWidth <= 0)
        return lay;
    const int labelLeft = footerRc.left + cardstyle::kCardPadH;
    const int maxRight = lay.m_buttonRects.empty() ? (footerRc.right - cardstyle::kCardPadH)
                                                   : (lay.m_buttonRects[0].left - cardstyle::kFooterLabelButtonGap);
    if (maxRight <= labelLeft)
    {
        lay.m_labelTruncated = true;
        return lay;
    }
    int labelRight = labelLeft + labelWidth;
    if (labelRight > maxRight)
    {
        labelRight = maxRight;
        lay.m_labelTruncated = true;
    }
    lay.m_labelRect.left = labelLeft;
    lay.m_labelRect.top = footerRc.top;
    lay.m_labelRect.right = labelRight;
    lay.m_labelRect.bottom = footerRc.bottom;
    return lay;
}

CardFooterLayout PaintCardFooter(HDC hdc, Gdiplus::Graphics& g, const RECT& footerRc, const ChatCardTypeStyle& style,
                                 const CString& label, const std::vector<ChatCardButton>& buttons, int hoverIndex)
{
    //上方分隔线：画在底部一行上方紧邻的一行，左右各让出底板的 1 像素边框
    const RECT divider = { footerRc.left + 1, footerRc.top - cardstyle::kFooterDividerH, footerRc.right - 1, footerRc.top };
    Gdiplus::SolidBrush dividerBrush(ToGdip(cardstyle::kFooterDivider, kOpaque));
    g.FillRectangle(&dividerBrush, divider.left, divider.top, divider.right - divider.left, divider.bottom - divider.top);

    std::vector<int> widths;
    for (size_t i = 0; i < buttons.size(); ++i)
        widths.push_back(MeasureCardButtonWidth(hdc, buttons[i].m_text, buttons[i].m_level));
    const int iconPart = cardstyle::kFooterLabelIconSize + cardstyle::kFooterLabelIconGap;
    const int labelWidth = label.IsEmpty() ? 0 : iconPart + MeasureTextWidth(hdc, (LPCWSTR)label, cardstyle::kFooterLabelPx);
    const CardFooterLayout lay = LayoutCardFooter(footerRc, labelWidth, widths);

    //类型标签：图标放得下才画，文字放不下时以省略号收尾
    if (!::IsRectEmpty(&lay.m_labelRect) && lay.m_labelRect.right - lay.m_labelRect.left >= cardstyle::kFooterLabelIconSize)
    {
        const int iconTop = footerRc.top + ((footerRc.bottom - footerRc.top) - cardstyle::kFooterLabelIconSize) / 2;
        DrawChatIcon(g, style.m_glyph, (Gdiplus::REAL)lay.m_labelRect.left, (Gdiplus::REAL)iconTop,
                     (Gdiplus::REAL)cardstyle::kFooterLabelIconSize, ToGdip(style.m_labelGlyphColor, kOpaque),
                     cardstyle::kTileGlyphStroke);
        //文字放不下时以省略号收尾；剩下的宽度连一个字加省略号都放不下时只画图标，免得只露出半个字
        RECT textRc = lay.m_labelRect;
        textRc.left += iconPart;
        const int labelTextW = labelWidth - iconPart;
        const int avail = textRc.right - textRc.left;
        if (avail >= labelTextW || avail >= cardstyle::kFooterLabelPx * kFooterLabelMinChars)
            DrawGdiText(hdc, (LPCWSTR)label, textRc, cardstyle::kFooterLabelPx, false, cardstyle::kFooterLabelText,
                        DT_LEFT | kSingleLine | DT_END_ELLIPSIS);
    }

    //按钮：置灰的按钮即使悬停也按置灰画
    for (size_t i = 0; i < buttons.size(); ++i)
    {
        int state = kCardButtonNormal;
        if (!buttons[i].m_enabled)
            state = kCardButtonDisabled;
        else if ((int)i == hoverIndex)
            state = kCardButtonHover;
        PaintCardButton(hdc, g, lay.m_buttonRects[i], buttons[i].m_text, buttons[i].m_level, state);
    }
    return lay;
}

int CardFooterButtonAt(const CardFooterLayout& layout, const std::vector<ChatCardButton>& buttons, POINT pt)
{
    for (size_t i = 0; i < buttons.size() && i < layout.m_buttonRects.size(); ++i)
    {
        if (buttons[i].m_enabled && ::PtInRect(&layout.m_buttonRects[i], pt))
            return (int)i;
    }
    return -1;
}

void PaintCardDescBlock(Gdiplus::Graphics& g, const RECT& rc)
{
    if (rc.right <= rc.left || rc.bottom <= rc.top)
        return;
    FillRound(g, rc, cardstyle::kDescRadius, cardstyle::kDescFill, kOpaque);
}

void PaintCardProgress(Gdiplus::Graphics& g, const RECT& rc, int percent)
{
    const int width = rc.right - rc.left;
    if (width <= 0 || rc.bottom <= rc.top)
        return;
    const int kFullPercent = 100;   // 进度的满值
    if (percent < 0)
        percent = 0;
    if (percent > kFullPercent)
        percent = kFullPercent;
    FillRound(g, rc, cardstyle::kProgressRadius, cardstyle::kProgressTrack, kOpaque);
    const int done = width * percent / kFullPercent;
    if (done <= 0)
        return;
    RECT doneRc = rc;
    doneRc.right = rc.left + done;
    FillRound(g, doneRc, cardstyle::kProgressRadius, cardstyle::kProgressFill, kOpaque);
}

}} // namespace balloonchatwnd::cardpaint
