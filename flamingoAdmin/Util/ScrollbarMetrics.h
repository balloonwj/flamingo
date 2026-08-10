/**
 *  flamingoAdmin —— 自绘悬浮滚动条的几何计算（纯函数）。
 *
 *  供 ComposeModal 投放目标选择器等「手写自绘可滚动区域」计算悬浮滚动条
 *  滑块的位置与尺寸，以及拖动时由滑块位置反算滚动偏移。所有函数均为纯
 *  函数，只依赖 RECT / int，不引用任何 balloonui 类型，因此可被
 *  flamingoAdminTests 直接编入单测（测试工程不链接 balloonui）。
 *
 *  典型用法：
 *      RECT thumb = ThumbRect(track, contentH, viewH, scroll, kMinThumb);
 *      int  scr   = ScrollOffsetFromThumbTop(track, thumbH, newTop,
 *                                            contentH, viewH);
 *
 *  balloonwj@qq.com   2026-05-22
 */
#pragma once

#include <windows.h>

namespace flamingoadmin {

// 判断内容是否溢出可视区（即是否需要滚动条）。
//   contentH：内容总高（像素）。
//   viewH：可视区高度（像素）。
// 返回 true 表示内容放不下、需要滚动条。
bool ScrollbarNeeded(int contentH, int viewH);

// 把滚动偏移夹取到合法区间 [0, max]，max = contentH - viewH（不溢出则为 0）。
//   scroll：待夹取的滚动偏移。
//   contentH / viewH：内容总高 / 可视高（像素）。
// 返回夹取后的滚动偏移。
int ClampScrollOffset(int scroll, int contentH, int viewH);

// 计算悬浮滚动条滑块的矩形。
//   track：滚动条轨道矩形（自绘区右缘内侧的竖条，host-client 坐标）。
//   contentH / viewH：内容总高 / 可视高（像素）。
//   scroll：当前滚动偏移（内部按 [0,max] 夹取）。
//   minThumb：滑块最小高度（像素），防止内容极多时滑块过小难以拖动。
// 返回滑块矩形；内容不溢出时返回空矩形 {0,0,0,0}。
RECT ThumbRect(const RECT& track, int contentH, int viewH,
               int scroll, int minThumb);

// 拖动滑块时，由滑块期望的新顶部 y 反算对应的滚动偏移。
//   track：轨道矩形。
//   thumbH：滑块高度（像素）。
//   thumbTop：拖动后滑块期望的顶部 y（host-client 坐标）。
//   contentH / viewH：内容总高 / 可视高（像素）。
// 返回对应的滚动偏移（已夹取到 [0,max]）。
int ScrollOffsetFromThumbTop(const RECT& track, int thumbH, int thumbTop,
                             int contentH, int viewH);

// 计算悬浮滚动条「淡入→驻留→淡出」时间线在进度 t01 处的不透明度。
//   t01：动画整体进度，取值 [0,1]。
//   fromAlpha：动画起始不透明度（连续滚动时≈1，使淡入段为 1→1、不闪）。
//   fadeInFrac：淡入段占整条时间线的比例，取值 (0,1)。
//   holdEndFrac：驻留段结束处占整条时间线的比例，须 >= fadeInFrac。
// 返回该处不透明度 [0,1]：t01 < fadeInFrac 为淡入段（fromAlpha→1）；
// [fadeInFrac, holdEndFrac) 为驻留段（恒 1）；其后为淡出段（1→0）。
float FadeAlphaAt(double t01, float fromAlpha,
                  double fadeInFrac, double holdEndFrac);

} // namespace flamingoadmin
