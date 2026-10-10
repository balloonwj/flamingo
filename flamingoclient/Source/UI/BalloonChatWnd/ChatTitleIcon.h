/**
 *  ChatTitleIcon.h
 *
 *  聊天窗（单聊 / 群聊）标题栏图标的共用生成 helper。把对端 / 群名的 monogram
 *  画成一个"圆形头像"位图设到标题栏，并可转成 HICON 设到任务栏 / Alt-Tab。
 *  原先这套代码只在单聊窗里 file-local，群聊窗也要用，故抽到此处共用。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 <windows.h>。
 *
 *  balloonwj@qq.com   2026-06-02
 */
#pragma once

#include <windows.h>

namespace balloonchatwnd {

// 标题栏不透明态背景色（与 balloonui DuiFrameTitleBar 一致，见 DuiFrameWindow.cpp
// 标题条 FillRect 的 RGB(245,246,250)）。圆形头像图标的圆外区域填成此色，与标题栏
// 融为一体、视觉上是圆形且无黑边。
const COLORREF kChatTitleBarBg = RGB(245, 246, 250);

/**
 *  生成标题栏用的"圆形" monogram 头像位图：整块填标题栏底色，其上画 AA 圆
 *  （头像底色）+ 居中首字。DuiFrameWindow 用 StretchBlt 画标题栏图标、忽略 alpha
 *  只看 RGB，故圆外区域填成标题栏底色，与标题栏融为一体。失败返回 NULL。
 *  位图所有权归调用方（用完 DeleteObject）。
 *    glyph：首字；avatarColor：圆的底色；barBg：标题栏背景色；sizePx：边长。
 */
HBITMAP MakeRoundTitleIcon(LPCTSTR glyph, COLORREF avatarColor,
                           COLORREF barBg, int sizePx);

/**
 *  生成标题栏用的"圆形"真图头像位图：整块填标题栏底色，其上把本地图片裁成圆形绘制。
 *  与 MakeRoundTitleIcon 同理（DuiFrameWindow StretchBlt 标题栏图标忽略 alpha 只看 RGB），
 *  故圆外区域填标题栏底色融为一体、无黑边。用于 facetype=2 真图头像的标题栏图标。
 *  失败（图片解码失败等）返回 NULL，调用方可回退到 MakeRoundTitleIcon。
 *  位图所有权归调用方（用完 DeleteObject）。
 *    imagePath：本地图片完整路径；barBg：标题栏背景色；sizePx：边长。
 *    gray：     是否把图片画成灰度（对方离线时为 true，2026-10-03）。只有圆内的图片变灰，
 *               圆外仍是 barBg 原色，标题栏上看不出方形的边。灰度权重与 MakeGrayColor 相同（Rec.601）。
 */
HBITMAP MakeRoundTitleIconFromImage(LPCWSTR imagePath, COLORREF barBg, int sizePx,
                                    bool gray = false);

/**
 *  把一个 32bpp 头像位图转成 HICON（用于任务栏 / Alt-Tab）。失败返回 NULL。
 *  调用方负责对返回的 HICON 调 DestroyIcon。
 */
HICON IconFromBitmap(HBITMAP hbmColor);

} // namespace balloonchatwnd
