/**
 *  OverlayScrollBarGeometry.h
 *
 *  主面板三个列表面板那条"悬浮式（overlay）自动隐藏滚动条"的几何计算。
 *
 *  为什么单独抽一个模块：滚动条要能用鼠标抓住拖动，就必须同时算出两个矩形
 *  —— 画出来的滑块矩形，以及比滑块略宽、供鼠标命中的"命中带"。这两个矩形
 *  还要一起给窗口的缩放边框让位（见下），逻辑不算短，且是纯坐标运算，抽出来
 *  便于 FlamingoUnit 直接断言。
 *
 *  为什么要给缩放边框让位：主面板是 balloonui 的 DuiFrameWindow，它在
 *  WM_NCCALCSIZE 里把客户区铺满整个窗口矩形，又在 WM_NCHITTEST 里把最外侧
 *  GetBorderPx() 个逻辑像素判成缩放边框（HTRIGHT 等）。列表面板是铺满窗口宽度
 *  的，滚动条若贴着视口右缘画，整条都落在那道缩放边框里 —— 鼠标靠近先变成
 *  左右双向箭头，按下去被系统当成拖窗口边框，事件根本传不到 DUI 树，滑块因此
 *  抓不住。故本模块把滑块与命中带整体往里挪 edgeInsetPx 个像素，让它们完整
 *  落在客户区可点击的范围内，窗口右边框的缩放能力保持不变。
 *
 *  本模块只做坐标运算，不碰窗口 / GDI，与 FlamingoUnit 测试工程共用，
 *  不 include stdafx.h（PCH 设 NotUsing）。
 *
 *  balloonwj@qq.com   2026-08-05
 */

#pragma once

#include <windows.h>      // RECT / MulDiv

namespace balloonmain {

// =======================================================================
// 几何常量（逻辑像素）
// =======================================================================

// 滑块宽度。窄条观感，对齐现代 overlay 滚动条。
const int kOverlayBarWidth = 5;

// 滑块右缘与"可用右缘"之间的间隙。可用右缘 = 视口右缘再让开窗口缩放边框，
// 即 rcView.right - edgeInsetPx。
const int kOverlayBarMargin = 2;

// 滑块最小高度。内容极长时按"视口 / 内容"比例算出来的滑块会细到几像素、
// 鼠标根本瞄不准，故设一个下限。
const int kOverlayBarMinThumb = 32;

// 命中带在滑块左侧额外外扩的宽度。滑块本身只有 kOverlayBarWidth 像素宽，
// 直接拿它当命中区太难点中；往左扩一段，使命中带总宽为
// kOverlayBarHitPad + kOverlayBarWidth + kOverlayBarMargin。
const int kOverlayBarHitPad = 4;

// 计算 DPI 缩放时的基准 DPI（Windows 的 96 = 100% 缩放）。
const int kOverlayBarBaseDpi = 96;

/**
 *  一条 overlay 滚动条的几何结果。
 */
struct OverlayScrollBarMetrics
{
    // 内容是否溢出视口（即是否需要滚动条）。false 时另外两个矩形无意义，
    // 调用方既不该画滑块，也不该认领命中带 —— 否则不需要滚动的列表右侧
    // 会平白多出一条吃掉点击的死区。
    bool m_visible;

    // 滑块矩形（host 客户区坐标）。
    RECT m_rcThumb;

    // 可抓握的命中带（host 客户区坐标）。横向覆盖滑块并向左外扩，纵向盖满
    // 整个视口（这样鼠标在轨道任意高度上悬停都能唤出滚动条）。右缘顶到
    // "视口右缘 - edgeInsetPx"为止，不会侵入窗口缩放边框。
    RECT m_rcHit;
};

/**
 *  算出一条 overlay 滚动条的滑块矩形与命中带。
 *
 *    rcView：       滚动视口矩形（host 客户区坐标）。
 *    contentHeight：内容总高度（像素）。小于等于视口高度即视为不需要滚动条。
 *    scrollPos：    当前滚动位置（像素，0 = 顶部）。超出 [0, 内容高-视口高]
 *                   时会被夹回该范围，不会算出跑到视口外的滑块。
 *    edgeInsetPx：  右侧要给窗口缩放边框让出的宽度（物理像素，>= 0；负值按 0
 *                   处理）。取值一般来自 ComputeResizeBorderInsetPx。
 *
 *  返回：几何结果；视口无效（宽或高 <= 0）或内容未溢出时 m_visible 为 false。
 */
OverlayScrollBarMetrics ComputeOverlayScrollBar(const RECT& rcView,
                                                int contentHeight,
                                                int scrollPos,
                                                int edgeInsetPx);

/**
 *  由"滑块顶边所在的 y 坐标"反算滚动位置 —— 拖动滑块时用。
 *
 *  与 ComputeOverlayScrollBar 是一对互逆运算：前者把 scrollPos 映射到滑块位置，
 *  本函数把用户拖到的滑块位置映射回 scrollPos。
 *
 *    rcView：       滚动视口矩形（host 客户区坐标）。
 *    contentHeight：内容总高度（像素）。
 *    thumbHeight：  滑块高度（像素），取自 ComputeOverlayScrollBar 的结果。
 *    thumbTop：     滑块顶边期望落到的 y 坐标（host 客户区坐标）。超出轨道
 *                   上下界时会被夹住。
 *
 *  返回：夹在 [0, 内容高 - 视口高] 之间的滚动位置；内容未溢出、视口无效、
 *        或滑块与视口等高（轨道长度为 0，无处可拖）时返回 0。
 */
int OverlayScrollPosFromThumbTop(const RECT& rcView,
                                 int contentHeight,
                                 int thumbHeight,
                                 int thumbTop);

/**
 *  算滚动条要给窗口缩放边框让出的宽度。
 *
 *  与 DuiFrameWindow::OnNcHitTestMsg 同一口径：边框宽度是 96-dpi 逻辑像素，
 *  运行时按 GetDpi()/96 缩放成物理像素。布局矩形用的是物理像素，故这里也要
 *  缩放，否则高 DPI 下让出来的宽度不够、滚动条又被边框压住一截。
 *
 *    borderPx： 窗口缩放边框宽度（96-dpi 逻辑像素，取自
 *               DuiFrameWindow::GetBorderPx()）。<= 0 表示没有缩放边框。
 *    dpi：      当前显示器 DPI（取自 DuiHost::GetDpi()）。<= 0 时按 96 处理
 *               —— 窗口还没建好时 GetDpi 可能取不到有效值。
 *    resizable：窗口是否可缩放（DuiFrameWindow::IsResizable()）。不可缩放时
 *               根本没有缩放边框，不必让路。
 *
 *  返回：要让出的宽度（物理像素，>= 0）。
 */
int ComputeResizeBorderInsetPx(int borderPx, int dpi, bool resizable);

} // namespace balloonmain
