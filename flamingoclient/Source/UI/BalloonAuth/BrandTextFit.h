/**
 *  登录窗与注册窗品牌文字（产品名称、标语）的测量与字号适配。
 *
 *  产品名称可以在 flamingo.ini 里改成任意长度（见 App/BrandText.h），而两个窗口留给品牌
 *  文字的宽度是固定的（右侧有火烈鸟图案）。FitBrandTextSize 从设计字号开始逐级缩小，直到
 *  文字放得下；缩到下限仍放不下时返回「需要截断」，调用方绘制时加 DT_END_ELLIPSIS。
 *
 *  典型用法：
 *      BrandTextFitResult fit = FitBrandTextSize(hdc, title, L"Microsoft YaHei", 22, 13, maxW, true);
 *      DrawText(... 字号 fit.m_pixelHeight ..., DT_SINGLELINE | DT_END_ELLIPSIS);
 *
 *  字体与两个窗口绘制品牌文字时一致：按像素高度新建、半粗 / 常规两种字重、ClearType。
 *  本文件只依赖 <windows.h>，登录窗与注册窗的布局文件（两个工程共用、不使用预编译头）都能直接包含。
 *
 *  balloonwj@qq.com   2026-10-03
 */
#pragma once

#include <windows.h>

namespace balloonauth {

/**
 *  FitBrandTextSize 的结果。
 */
struct BrandTextFitResult
{
    int  m_pixelHeight;  // 采用的字号（像素高度）
    bool m_truncated;    // true：缩到下限字号仍放不下，绘制时须加 DT_END_ELLIPSIS 截断
};

/**
 *  测量单行文字在指定字体下的尺寸。
 *  @param hdc         用于测量的 DC，函数内临时选入字体、返回前恢复。
 *  @param text        文字；NULL 或空串时返回 0×0。
 *  @param fontFace    字体名，如 L"Microsoft YaHei"。
 *  @param pixelHeight 字号（像素高度），不大于 0 时返回 0×0。
 *  @param bold        true 为半粗（FW_SEMIBOLD），false 为常规。
 *  @return 宽度 cx 与行高 cy（像素）。
 */
SIZE MeasureBrandText(HDC hdc, LPCWSTR text, LPCWSTR fontFace, int pixelHeight, bool bold);

/**
 *  计算品牌文字在给定宽度内应采用的字号。
 *  @param hdc       用于测量的 DC。
 *  @param text      文字；NULL 或空串时直接返回设计字号、不截断。
 *  @param fontFace  字体名。
 *  @param desiredPx 设计字号（像素高度），文字放得下时就用它。
 *  @param minPx     允许缩小到的下限字号；大于 desiredPx 时按 desiredPx 处理（即不缩小），
 *                   小于 1 时按 1 处理。
 *  @param maxWidth  可用宽度（像素）。
 *  @param bold      字重，与绘制时一致。
 *  @return 从 desiredPx 起逐个像素缩小，返回第一个能放下的字号；到 minPx 仍放不下时
 *          返回 minPx 并把 m_truncated 置 true。
 */
BrandTextFitResult FitBrandTextSize(HDC hdc, LPCWSTR text, LPCWSTR fontFace,
                                    int desiredPx, int minPx, int maxWidth, bool bold);

} // namespace balloonauth
