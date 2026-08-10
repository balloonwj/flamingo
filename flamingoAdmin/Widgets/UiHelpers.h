/**
 *  flamingoAdmin —— 通用绘制 / 字体辅助。
 *
 *  把「取字体、填色块、画圆角矩形、画渐变、画文本」这些各界面反复用到的
 *  GDI / GDI+ 操作收敛成一组小函数，供自绘控件（OnPaint 里）调用。
 *
 *  balloonwj@qq.com   2026.05.21
 */
#pragma once

#include <windows.h>

namespace flamingoadmin { namespace ui {

// 取一个微软雅黑 HFONT（进程内按「磅值 + 粗细」缓存复用，调用方不要 Delete）。
//   pointSize：字号（磅，按 96 dpi 换算成像素高度）。
//   bold：true 取 SemiBold(600) 字重，false 取 Normal(400)。
HFONT UiFont(int pointSize, bool bold = false);

// 用纯色填充一个矩形（普通 GDI，轴对齐无需抗锯齿）。
void FillSolidRect(HDC hdc, const RECT& rc, COLORREF color);

// 画一条 1px 粗的水平 / 垂直分隔线。
//   x, y：起点；cx, cy：宽、高（其中之一为 1 表示线宽）。
void FillLine(HDC hdc, int x, int y, int cx, int cy, COLORREF color);

// 用纯色填充一个圆角矩形（GDI+ 抗锯齿）。
//   radius：圆角半径（像素）。
void FillRoundRect(HDC hdc, const RECT& rc, COLORREF color, int radius);

// 描边一个圆角矩形（GDI+ 抗锯齿）。
//   width：描边线宽（像素）。
void StrokeRoundRect(HDC hdc, const RECT& rc, COLORREF color,
                     int radius, float width = 1.0f);

// 在 rc 内绘制单行 / 多行文本。
//   color：文字颜色。
//   font：文字字体（调用方持有所有权）。
//   dtFlags：DrawText 标志位组合（DT_LEFT / DT_CENTER / DT_VCENTER / ...）。
void DrawTextLine(HDC hdc, LPCTSTR text, const RECT& rc, COLORREF color,
                  HFONT font, UINT dtFlags);

// 用三色对角线性渐变填充 rc（从左上到右下，GDI+）。
//   c0 / c1 / c2：渐变的三个停靠色（位置固定为 0 / 0.55 / 1.0）。
void FillDiagonalGradient(HDC hdc, const RECT& rc,
                          COLORREF c0, COLORREF c1, COLORREF c2);

// 在 rc 上覆盖一层半透明色（GDI+ alpha 混合，与背景已绘内容叠加）。
// 用于弹窗遮罩这类「压暗背景」的场景。
//   color：覆盖色（RGB）。
//   alpha：不透明度，0（全透明）~ 255（全不透明）。
void FillTranslucent(HDC hdc, const RECT& rc, COLORREF color, int alpha);

}} // namespace flamingoadmin::ui
