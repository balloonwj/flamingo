/**
 *  多语言排版的小工具：译文比中文长时，按文字宽度「只增不减」地加宽固定宽度的控件（多语言第 4 期）。
 *
 *  只在非简体中文界面生效：简体中文的文字按原设计都放得下，布局与改动之前完全相同。
 *  文字宽度用当前界面语言的界面字体（i18n::UiFontName）量出，与控件实际绘制用的字体一致。
 *
 *  只有头文件，用到的模块直接包含即可，不必登记进工程文件。典型用法（建好控件树之后）：
 *      i18n::GrowFixedWidthToText(root->FindCtrlById(kIdSend), TR(L"发消息"), kButtonTextPx, true, kButtonPadPx);
 *
 *  balloonwj@qq.com   2026-10-04
 */
#pragma once

#include <windows.h>
#include <string.h>

#include "I18n.h"
#include "../../../third-party/balloonui/balloonui/DuiControl.h"
#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"
#include "../../../third-party/balloonui/balloonui/DuiDpi.h"
#include "../../../third-party/balloonui/balloonui/DuiTheme.h"

namespace i18n {

/**
 *  用当前界面字体量一段单行文字的宽度。
 *  @param text        文字；为 NULL 或空串时返回 0
 *  @param pixelHeight 字号（像素，与控件绘制时 CreateFont 的高度相同）
 *  @param bold        是否按半粗体（FW_SEMIBOLD）量
 *  @return 文字宽度，单位像素
 */
inline int MeasureUiTextWidth(LPCWSTR text, int pixelHeight, bool bold)
{
    if (text == NULL || text[0] == L'\0' || pixelHeight <= 0)
    {
        return 0;
    }
    HDC hdc = ::GetDC(NULL);
    if (hdc == NULL)
    {
        return 0;
    }
    HFONT font = ::CreateFontW(-pixelHeight, 0, 0, 0, bold ? FW_SEMIBOLD : FW_NORMAL, FALSE, FALSE, FALSE,
                               DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                               DEFAULT_PITCH | FF_DONTCARE, UiFontName());
    HFONT oldFont = (font != NULL) ? (HFONT)::SelectObject(hdc, font) : NULL;
    SIZE size = { 0, 0 };
    ::GetTextExtentPoint32W(hdc, text, (int)wcslen(text), &size);
    if (font != NULL)
    {
        ::SelectObject(hdc, oldFont);
        ::DeleteObject(font);
    }
    ::ReleaseDC(NULL, hdc);
    return size.cx;
}

/**
 *  当前界面字体一行文字的高度（TEXTMETRIC::tmHeight，含字体自带的上下留白）。
 *  不同字体在同一字号下行高不同（如 Segoe UI 比微软雅黑高），按设计行高居中绘制时，行高不够会切掉字母的下伸部分。
 *  @param pixelHeight 字号（像素，与控件绘制时 CreateFont 的高度相同）
 *  @param bold        是否按半粗体（FW_SEMIBOLD）量
 *  @return 行高，单位像素；取不到屏幕 DC 时返回 0
 */
inline int MeasureUiLineHeight(int pixelHeight, bool bold)
{
    if (pixelHeight <= 0)
    {
        return 0;
    }
    HDC hdc = ::GetDC(NULL);
    if (hdc == NULL)
    {
        return 0;
    }
    HFONT font = ::CreateFontW(-pixelHeight, 0, 0, 0, bold ? FW_SEMIBOLD : FW_NORMAL, FALSE, FALSE, FALSE,
                               DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                               DEFAULT_PITCH | FF_DONTCARE, UiFontName());
    HFONT oldFont = (font != NULL) ? (HFONT)::SelectObject(hdc, font) : NULL;
    TEXTMETRICW tm;
    memset(&tm, 0, sizeof(tm));
    ::GetTextMetricsW(hdc, &tm);
    if (font != NULL)
    {
        ::SelectObject(hdc, oldFont);
        ::DeleteObject(font);
    }
    ::ReleaseDC(NULL, hdc);
    return tm.tmHeight;
}

/**
 *  按当前界面字体的行高「只增不减」地取行高：非简体中文界面下设计行高放不下一行文字时返回字体行高，
 *  否则返回设计行高。简体中文界面恒为设计行高。
 *  @param designHeight 原设计行高（像素）
 *  @param pixelHeight  文字字号（像素）
 *  @param bold         文字是否半粗体
 *  @return 行高（像素），不小于 designHeight
 */
inline int LineHeightForUiFont(int designHeight, int pixelHeight, bool bold)
{
    if (IsSourceLanguage())
    {
        return designHeight;
    }
    const int need = MeasureUiLineHeight(pixelHeight, bold);
    return (need > designHeight) ? need : designHeight;
}

/**
 *  把磅值字号换算成指定 DPI 下的像素字高，与 balloonui 的 DuiResMgr 建字体时的换算相同
 *  （lfHeight = -MulDiv(pt, dpi, 72)）。
 *  @param pointSize 字号（磅）
 *  @param dpi       显示器 DPI（96 为 100% 缩放）
 *  @return 字高（像素）
 */
inline int PointToPixelHeightForDpi(int pointSize, int dpi)
{
    const int kPointsPerInch = 72;   // 1 英寸 = 72 磅
    return ::MulDiv(pointSize, dpi, kPointsPerInch);
}

// MaxMonitorDpi 枚举显示器的回调：data 指向当前已知的最大 DPI，取本显示器中心点所在显示器的 DPI 与之比较
inline BOOL CALLBACK MaxMonitorDpiProc(HMONITOR /*monitor*/, HDC /*hdc*/, LPRECT rc, LPARAM data)
{
    int* maxDpi = reinterpret_cast<int*>(data);
    if (rc != NULL && maxDpi != NULL)
    {
        POINT center;
        center.x = rc->left + (rc->right - rc->left) / 2;
        center.y = rc->top + (rc->bottom - rc->top) / 2;
        const int dpi = balloonwjui::DuiDpi::GetDpiForPoint(center);
        if (dpi > *maxDpi)
        {
            *maxDpi = dpi;
        }
    }
    return TRUE;
}

/**
 *  本机所有显示器中最高的 DPI，不低于系统 DPI。
 *  背景（2026-10-07，据实证见 Tests/I18nTests.cpp 的 I18）：进程按显示器感知 DPI，balloonui 控件绘制文字时按窗口所在显示器的
 *  DPI 取字体（DuiControl::GetDefaultFont），而控件尺寸不随 DPI 缩放。系统 DPI 在登录时确定，窗口放在 DPI 更高的显示器上、
 *  或改了缩放比例却没有重新登录时，按系统 DPI 量出的宽度放不下实际绘制的文字（英文「Restore default」在系统 96、
 *  显示器 120 时，按钮 95 像素、文字加留白要 117 像素）。按最高的 DPI 量，窗口放在任何一块显示器上都放得下。
 *  @return DPI 数值（96 / 120 / 144 / 192 等）
 */
inline int MaxMonitorDpi()
{
    int maxDpi = balloonwjui::DuiDpi::GetSystemDpi();
    ::EnumDisplayMonitors(NULL, NULL, MaxMonitorDpiProc, reinterpret_cast<LPARAM>(&maxDpi));
    return maxDpi;
}

/**
 *  把磅值字号换算成像素字高，用于量 SetTextPointSize 设定字号的控件上的文字、据此定控件尺寸。
 *  按本机所有显示器中最高的 DPI 换算（原因见 MaxMonitorDpi），量出的尺寸在任何一块显示器上都放得下文字。
 *  只用于测量，不要用它建字体：控件绘制用的字体由 balloonui 按窗口所在显示器的 DPI 建。
 *  @param pointSize 字号（磅）
 *  @return 字高（像素）
 */
inline int PointToPixelHeight(int pointSize)
{
    return PointToPixelHeightForDpi(pointSize, MaxMonitorDpi());
}

/**
 *  按文字宽度「只增不减」地取宽度：非简体中文界面下文字加留白比设计宽度更宽时返回前者，否则返回设计宽度。
 *  用于建控件之前先算宽度的场合（如工具条要先按各按钮宽度算出整条宽度）。
 *  @param text        文字（已取译文）；为 NULL 或空串时返回设计宽度
 *  @param designWidth 原设计宽度（像素）
 *  @param pixelHeight 文字字号（像素）
 *  @param bold        文字是否半粗体
 *  @param extraPx     文字宽度之外还要留的宽度合计（像素），如左右内边距、图标宽度
 *  @return 宽度（像素），不小于 designWidth
 */
inline int WidthForText(LPCWSTR text, int designWidth, int pixelHeight, bool bold, int extraPx)
{
    if (IsSourceLanguage())
    {
        return designWidth;
    }
    const int need = MeasureUiTextWidth(text, pixelHeight, bold) + extraPx;
    return (need > designWidth) ? need : designWidth;
}

/**
 *  balloonui 默认字体（控件未设字体时使用，字号取 DuiTheme 的默认字号）在指定 DPI 下的像素字高。
 *  @param dpi 显示器 DPI（96 为 100% 缩放）
 *  @return 字高（像素）
 */
inline int DefaultFontPixelHeightForDpi(int dpi)
{
    return PointToPixelHeightForDpi(balloonwjui::DuiTheme::Inst().GetDefaultFontPt(), dpi);
}

/**
 *  balloonui 默认字体的像素字高，用于测量：按本机所有显示器中最高的 DPI 换算（见 PointToPixelHeight）。
 *  @return 字高（像素）
 */
inline int DefaultFontPixelHeight()
{
    return DefaultFontPixelHeightForDpi(MaxMonitorDpi());
}

// balloonui 按钮未设字体时文字左右留白合计（像素）：按钮文字居中，左右各留 8 像素
const int kDefaultButtonTextPadPx = 16;

/**
 *  按指定显示器 DPI 量文字，「只增不减」地取默认字体按钮的宽度。其余同 DefaultButtonWidthForText。
 *  @param text        按钮文字（已取译文）
 *  @param designWidth 原设计宽度（像素）
 *  @param dpi         显示器 DPI（96 为 100% 缩放）
 *  @return 按钮宽度（像素），不小于 designWidth；简体中文界面恒为 designWidth
 */
inline int DefaultButtonWidthForTextAtDpi(LPCWSTR text, int designWidth, int dpi)
{
    return WidthForText(text, designWidth, DefaultFontPixelHeightForDpi(dpi), false, kDefaultButtonTextPadPx);
}

/**
 *  balloonui 按钮（DuiButton）未设字体、使用默认字体时，按文字宽度「只增不减」地取按钮宽度。
 *  默认字体的字号取 DuiTheme 的默认字号，与 DuiButton 绘制时用的默认字体一致；按本机所有显示器中最高的 DPI 量
 *  （见 MaxMonitorDpi），窗口放在任何一块显示器上都放得下。
 *  @param text        按钮文字（已取译文）
 *  @param designWidth 原设计宽度（像素）
 *  @return 按钮宽度（像素），不小于 designWidth；简体中文界面恒为 designWidth
 */
inline int DefaultButtonWidthForText(LPCWSTR text, int designWidth)
{
    return DefaultButtonWidthForTextAtDpi(text, designWidth, MaxMonitorDpi());
}

/**
 *  会在两种文字之间切换的按钮（如「打开麦克风 / 关闭麦克风」）：取两种文字中较宽的一个定按钮宽度，
 *  切换文字时按钮宽度不变。其余同 DefaultButtonWidthForText。
 *  @param textA       第一种文字（已取译文）
 *  @param textB       第二种文字（已取译文）
 *  @param designWidth 原设计宽度（像素）
 *  @return 按钮宽度（像素），不小于 designWidth
 */
inline int DefaultButtonWidthForTexts(LPCWSTR textA, LPCWSTR textB, int designWidth)
{
    const int widthA = DefaultButtonWidthForText(textA, designWidth);
    const int widthB = DefaultButtonWidthForText(textB, designWidth);
    return (widthA > widthB) ? widthA : widthB;
}

/**
 *  非简体中文界面下，若控件的固定宽度放不下文字，就把它加宽到「文字宽度 + 留白」；放得下或为简体中文时不动。
 *  控件须是某个 DuiLayout（DuiHBox / DuiVBox）的直接子控件，且主轴是固定宽度（XML 的 fixedWidth）。
 *  @param ctrl        要加宽的控件，可为 NULL（直接返回）。所有权不转移
 *  @param text        控件上的文字（已取译文）
 *  @param pixelHeight 文字字号（像素）
 *  @param bold        文字是否半粗体
 *  @param extraPx     文字宽度之外还要留的宽度合计（像素），如左右内边距、图标宽度
 *  @return 加宽了返回 true
 */
inline bool GrowFixedWidthToText(balloonwjui::DuiControl* ctrl, LPCWSTR text, int pixelHeight, bool bold, int extraPx)
{
    if (ctrl == NULL || IsSourceLanguage())
    {
        return false;
    }
    balloonwjui::DuiLayout* parent = dynamic_cast<balloonwjui::DuiLayout*>(ctrl->GetParent());
    if (parent == NULL)
    {
        return false;
    }
    balloonwjui::DuiLayout::Hint hint = parent->GetHint(ctrl);
    if (hint.fixedMain <= 0)
    {
        return false;
    }
    const int need = WidthForText(text, hint.fixedMain, pixelHeight, bold, extraPx);
    if (need <= hint.fixedMain)
    {
        return false;
    }
    hint.fixedMain = need;
    parent->SetHint(ctrl, hint);
    return true;
}

} // namespace i18n
