/**
 *  画面按宽高比放进一块区域（居中、四周黑边）的计算，ShareAspectFit.h
 *
 *  远程桌面观看窗（ComputeRdFitRect）、屏幕共享观看窗与摄像头小窗、云端录像合成（CloudFitRect）都要把一块画面等比放进
 *  控件或格子里。2026-10-14 之前各写一份：远程桌面与云端合成比较宽高比的乘积，共享观看窗与摄像头小窗先按宽算高、
 *  超了再按高算宽，后者在整除的边界上与前者最多差 1 像素。现在统一为本函数（docs/远程桌面控制权限与观看工具条方案.md
 *  8.2 第 6 条），ComputeRdFitRect 与 CloudFitRect 保留为转调它的名字（远程桌面、云端合成的用例照常覆盖）。
 *  纯计算，单元测试经 ComputeRdFitRect 覆盖（Source/Tests/RdViewFitTests.cpp 的 RES1）。
 *
 *  典型用法：
 *      const RECT picture = screenshare::FitRectKeepAspect(clientRect, frameW, frameH);
 *      ::StretchDIBits(hdc, picture.left, picture.top, 画面宽, 画面高, ...);
 *
 *  balloonwj@qq.com   2026-10-14
 */
#pragma once

#include <windows.h>

namespace screenshare {

/**
 *  画面按宽高比放进 area 后实际所占的矩形：区域比画面宽时上下铺满、左右留黑边，否则左右铺满、上下留黑边，居中。
 *  @param area 区域（控件矩形、格子等）
 *  @param srcW 画面宽（像素）
 *  @param srcH 画面高（像素）
 *  @return 画面矩形；画面或区域的尺寸不大于 0 时返回 area 本身
 */
RECT FitRectKeepAspect(const RECT& area, int srcW, int srcH);

} // namespace screenshare
