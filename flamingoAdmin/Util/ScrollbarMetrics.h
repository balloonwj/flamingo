/**
 *  flamingoAdmin —— 自绘悬浮滚动条的几何计算（纯函数）。
 *
 *  供 OverlayScrollbar 等「手写自绘可滚动区域」判断是否需要滚动条、把滚动
 *  偏移夹取到合法区间。所有函数均为纯函数，只依赖 int，不引用任何 balloonui
 *  类型，因此可被 flamingoAdminTests 直接编入单测（测试工程不链接 balloonui）。
 *
 *  2026-10-04 起滑块矩形、拖动反算与淡入淡出时间线改由 balloonui DuiScrollBar
 *  计算（见 OverlayScrollbar.h），原先的 ThumbRect / ScrollOffsetFromThumbTop /
 *  FadeAlphaAt 随之删除。
 *
 *  典型用法：
 *      if (ScrollbarNeeded(contentH, viewH)) { ... }
 *      scroll = ClampScrollOffset(scroll, contentH, viewH);
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

} // namespace flamingoadmin
