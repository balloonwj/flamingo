/**
 *  ImageViewerGeometry.h
 *
 *  聊天图片放大窗口的纯几何运算：旋转后尺寸、适应窗口缩放比、缩放档位夹取与步进、
 *  图片在视口内的落点夹取（含小图居中）、以光标为锚点缩放时的落点修正、缩放百分数
 *  读数。刻意与窗口 / GDI+ / 显示器 API 解耦——只做数值运算，便于 FlamingoUnit
 *  测试工程直接断言，也让看图窗口本体复用同一份逻辑而不必各写一遍。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL（用到 CString）。
 *
 *  balloonwj@qq.com   2026-06-04
 */
#pragma once

#include <windows.h>   // POINT / SIZE / RECT

namespace balloonimageviewer {

// 缩放比下限：10%。再小图片细节全糊、也超出实用范围。
constexpr double kMinZoomScale = 0.10;
// 缩放比上限：400%。再大只是看放大后的马赛克，没有意义。
constexpr double kMaxZoomScale = 4.00;
// 放大 / 缩小一档（点按钮或滚轮一格）的步进倍率：放大 ×1.25、缩小 ÷1.25。
constexpr double kZoomStepFactor = 1.25;

// 旋转后图片的像素尺寸。
//   imagePx：        原始（未旋转）图片像素尺寸；
//   rotationDegrees：旋转角，看图窗只用 0 / 90 / 180 / 270，其它值按 90 的整数倍归一。
//   返回：           90° / 270° 时宽高对调，其余原样返回。
SIZE RotatedImageSize(SIZE imagePx, int rotationDegrees);

// 计算"适应窗口"缩放比：让图片等比放进视口。
//   dispPx：      已按当前旋转处理过的图片像素尺寸；
//   viewport：    可视区像素尺寸（已扣除工具栏等占用）；
//   allowUpscale：是否允许放大小图。false 时结果夹到 ≤1.0（小图最多 100%，不放大）。
//   返回：        缩放比；任一尺寸 ≤0 时返回 1.0 作安全兜底，避免除零。
double ComputeFitScale(SIZE dispPx, SIZE viewport, bool allowUpscale);

// 把缩放比夹进 [minScale, maxScale]（要求 minScale ≤ maxScale）。
//   scale：            待夹取的缩放比；
//   minScale/maxScale：下 / 上限。
//   返回：             夹取后的缩放比。
double ClampZoom(double scale, double minScale, double maxScale);

// 在当前缩放比上放大 / 缩小一档并夹取到 [minScale, maxScale]。
//   current：          当前缩放比；
//   direction：        >0 放大、<0 缩小、0 原样；
//   stepFactor：       步进倍率（>1，一般用 kZoomStepFactor）；
//   minScale/maxScale：夹取上下限。
//   返回：             步进并夹取后的缩放比。
double SteppedZoom(double current, int direction, double stepFactor,
                   double minScale, double maxScale);

// 把图片左上角在视口内的落点夹到合理范围，避免拖出空白边：
//   · 某一维缩放后图比视口大——落点夹到 [viewport-scaled, 0]，两端不留空隙；
//   · 某一维比视口小——该维居中锁定（忽略传入值，强制 (viewport-scaled)/2）。
//   desiredTopLeft：期望的图片左上角（视口坐标，一般来自拖拽累加）；
//   scaledImg：     缩放后图片像素尺寸；
//   viewport：      可视区像素尺寸。
//   返回：          夹取 / 居中后的图片左上角。
POINT ClampImageOrigin(POINT desiredTopLeft, SIZE scaledImg, SIZE viewport);

// 以锚点为中心缩放时，算出新的图片左上角，使锚点正下方的图像像素保持不动。
// 滚轮缩放、双击在"适应↔100%"切换时用，保证视觉上"对着哪放大就从哪放大"。
//   oldTopLeft：缩放前图片左上角（视口坐标）；
//   oldScale：  缩放前缩放比（>0）；
//   newScale：  缩放后缩放比（>0）；
//   anchor：    缩放锚点（视口坐标，一般为光标位置）。
//   返回：      缩放后应放置的图片左上角（未夹取，调用方再过 ClampImageOrigin 兜底）。
POINT OriginForZoomAtAnchor(POINT oldTopLeft, double oldScale, double newScale,
                            POINT anchor);

// 缩放比的百分数读数，四舍五入到整数百分比，形如 "96%"（供底部工具栏显示）。
//   scale：缩放比（如 0.96）。
//   返回： 形如 "96%" 的字符串。
CString FormatZoomReadout(double scale);

// 在画布上按住左键拖动时该做什么。
enum CanvasDragAction
{
    kCanvasDragNone       = 0,   // 什么都不做：按在工具栏上，或移动还没超出系统拖动阈值（单击、
                                 // 双击切换缩放都落在这里，不能被当成拖动）
    kCanvasDragPanImage   = 1,   // 平移图片：图片放大到超出窗口，拖动用来查看被挡住的部分
    kCanvasDragMoveWindow = 2,   // 移动窗口：图片能完整显示、或还没有图片时，拖动画布等同于拖动
                                 // 标题栏（交给系统原生的窗口移动）
};

// 判断在画布上按住左键拖动时应当平移图片、移动窗口还是什么都不做。
//   overToolbar：按下点是否落在底部工具栏上。工具栏整块按按钮区域处理，按钮之间的空隙也算，
//                免得在工具栏上误拖动窗口。
//   pannable：   图片当前是否可平移（缩放后的图片超出视口）。没有图片时为 false。
//   pressPt：    左键按下时的位置（宿主客户区坐标）。
//   currentPt：  当前鼠标位置（与 pressPt 同一坐标系）。
//   threshold：  系统拖动阈值（SM_CXDRAG / SM_CYDRAG）。横向或纵向偏移超过它才算开始拖动，
//                与系统 DragDetect 的判定一致。
//   返回：       见 CanvasDragAction。可平移时从按下起就平移，不等阈值（与改动前的行为一致）。
CanvasDragAction ResolveCanvasDrag(bool overToolbar, bool pannable, POINT pressPt,
                                   POINT currentPt, SIZE threshold);

} // namespace balloonimageviewer
