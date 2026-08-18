/**
 *  GroupManagePaint.h
 *
 *  群管理对话框内部共用的绘制工具 —— GDI 文字、GDI+ 圆角矩形填充 / 描边。
 *  抽出来供本目录下的按钮、导航项、各页自绘小控件统一复用，避免每个文件
 *  各抄一份 DrawGdiText / AddRoundRect。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL，
 *  并且包含 <gdiplus.h>。
 *
 *  balloonwj@qq.com   2026-06-03
 */

#pragma once

#include <gdiplus.h>

namespace balloongroupmanage {

// 字重。映射到 GDI 的 FW_* 常量。
enum GmFontWeight
{
    kGmFontNormal   = 0,   // 常规（正文）
    kGmFontSemiBold = 1,   // 半粗（小标题 / 主按钮文字）
    kGmFontBold     = 2     // 粗体（页面大标题）
};

// 用 Microsoft YaHei 在指定矩形内绘制单行 / 多行文字（GDI）。
//   hdc：        目标 DC；
//   text：       文本（nullptr / 空串不绘制）；
//   rc：         绘制矩形；
//   pixelHeight：字号（像素，<= 0 不绘制）；
//   weight：     字重，取值见 GmFontWeight；
//   color：      文字颜色；
//   dtFlags：    DrawText 对齐 / 换行标志（如 DT_LEFT | DT_VCENTER | DT_SINGLELINE）。
void GmDrawText(HDC hdc, LPCWSTR text, const RECT& rc,
                int pixelHeight, int weight, COLORREF color, UINT dtFlags);

// 向 GDI+ 路径追加一个圆角矩形（半径自动夹到不超过半宽 / 半高）。
//   path：           目标路径；
//   x / y / w / h：  矩形左上角与宽高；
//   radius：         圆角半径。
void GmAddRoundRect(Gdiplus::GraphicsPath& path,
                    Gdiplus::REAL x, Gdiplus::REAL y,
                    Gdiplus::REAL w, Gdiplus::REAL h, Gdiplus::REAL radius);

// 在 hdc 上画一个抗锯齿圆角矩形（可选填充 + 可选描边）。
//   hdc：         目标 DC；
//   rc：          矩形（设备坐标）；
//   radius：      圆角半径（像素）；
//   fill：        填充色；CLR_INVALID 表示不填充；
//   border：      描边色；CLR_INVALID 表示不描边；
//   borderWidth： 描边宽度（像素）。
void GmFillRoundRect(HDC hdc, const RECT& rc, Gdiplus::REAL radius,
                     COLORREF fill, COLORREF border, Gdiplus::REAL borderWidth);

} // namespace balloongroupmanage
