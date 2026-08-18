/**
 *  ScreenshotGeometry.h
 *
 *  截图浮层的纯几何运算：选区矩形归一化 / 夹取 / 最小尺寸判定、尺寸读数文本、
 *  操作工具栏落点计算。刻意与窗口 / GDI / 显示器 API 解耦——只做坐标运算，便于
 *  FlamingoUnit 测试工程直接断言（对应第一期用例 P1~P5），也让浮层窗口代码复用
 *  同一份逻辑而不必各写一遍。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL（用到 CString）。
 *
 *  balloonwj@qq.com   2026-06-04
 */
#pragma once

#include <windows.h>   // POINT / SIZE / RECT

namespace balloonscreenshot {

// 把一次拖拽的锚点与当前点归一成正规矩形（保证 left≤right、top≤bottom）。
// 用户可能从右下往左上拖，原始坐标 left>right / top>bottom，本函数纠正之。
//   anchor：按下时的锚点（屏幕坐标）；
//   cur：   当前鼠标点（屏幕坐标）。
//   返回：  归一后的矩形。
RECT NormalizeDragRect(POINT anchor, POINT cur);

// 把选区矩形夹进给定边界（通常是整个虚拟桌面）。保证结果各边都落在 bounds 内；
// 若选区完全在 bounds 之外，结果会退化为贴边的零宽 / 零高矩形，交由
// IsSelectionUsable 判无效。
//   r：     待夹取的矩形；
//   bounds：允许的边界矩形（屏幕坐标）。
//   返回：  夹取后的矩形。
RECT ClampRectToBounds(const RECT& r, const RECT& bounds);

// 选区是否可用：宽、高都不小于给定阈值才算数（太小的框视为误操作，不弹工具栏、
// 不可确认）。
//   r：          选区矩形；
//   minW / minH：最小宽 / 高阈值（像素，>0）。
//   返回：        宽高都 ≥ 阈值返回 true，否则 false。
bool IsSelectionUsable(const RECT& r, int minW, int minH);

// 选区尺寸读数文本，形如 "1035 × 564"（× 为 U+00D7 乘号）。
//   w / h：宽 / 高（像素）。
//   返回： 读数字符串。
CString FormatSizeReadout(int w, int h);

// 计算操作工具栏左上角落点。默认贴在选区下方、右边与选区右边对齐；下方放不下则
// 翻到选区上方；最后整体夹进 workArea 保证完全可见（条比工作区还大时贴左 / 上边缘）。
//   sel：     选区矩形（屏幕坐标）；
//   barSize： 工具栏尺寸（像素）；
//   workArea：可放置区域（屏幕坐标，一般为选区所在显示器工作区）；
//   gap：     工具栏与选区之间的间距（像素，≥0）。
//   返回：    工具栏左上角屏幕坐标。
POINT PlaceActionBar(const RECT& sel, SIZE barSize, const RECT& workArea, int gap);

// 选区命中区：判断鼠标落在选区的哪个部位，用于决定光标与拖拽行为（移动 / 缩放 /
// 新框选）。角优先于边。
enum SelHitZone
{
    kSelOutside = 0,   // 选区(含容差)之外 → 新框选
    kSelInside,        // 选区内部 → 移动整个选区
    kSelLeft,          // 左边 → 横向缩放
    kSelRight,         // 右边
    kSelTop,           // 上边 → 纵向缩放
    kSelBottom,        // 下边
    kSelTL,            // 左上角 → 双向缩放
    kSelTR,            // 右上角
    kSelBL,            // 左下角
    kSelBR,            // 右下角
};

// 判定点 pt 落在选区 sel 的哪个命中区。
//   sel：选区矩形；pt：鼠标点；tol：边 / 角命中容差（像素，>0）。
//   返回：命中区（见 SelHitZone）。
SelHitZone HitTestSelection(const RECT& sel, POINT pt, int tol);

// 把选区按 (dx,dy) 平移并夹进 bounds（保持尺寸不变；贴边时实际位移变小）。
//   sel：原选区；dx/dy：期望位移；bounds：允许范围（一般为整窗 / 虚拟桌面）。
//   返回：平移夹取后的选区。调用方可用 (返回.left - sel.left) 得到实际位移。
RECT MoveSelectionClamped(const RECT& sel, int dx, int dy, const RECT& bounds);

// 按命中区把选区相应的边按 (dx,dy) 缩放：夹进 bounds，且宽高不小于 minW×minH。
//   sel：原选区；zone：拖拽命中区（边 / 角）；dx/dy：本次位移；
//   bounds：允许范围；minW/minH：最小宽 / 高。
//   返回：缩放夹取后的选区。zone 非边 / 角时原样返回。
RECT ResizeSelection(const RECT& sel, SelHitZone zone, int dx, int dy,
                     const RECT& bounds, int minW, int minH);

} // namespace balloonscreenshot
