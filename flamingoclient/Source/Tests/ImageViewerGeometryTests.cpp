/**
 *  ImageViewerGeometryTests.cpp
 *
 *  聊天图片放大窗口的纯几何运算测试（增量 1）：旋转后尺寸、适应窗口缩放比
 *  （只缩不放）、缩放档位夹取与步进、图片落点夹取与小图居中、锚点缩放落点、
 *  缩放百分数读数。对应方案用例 V1~V6。另有 IVD1～IVD5：在画布上按住拖动时，按图片能否
 *  平移、是否移出系统拖动阈值、是否按在工具栏上，决定平移图片、移动窗口还是什么都不做。窗口本体 / GDI+ 绘制 / 键鼠交互只能
 *  目视核对，不在本文件覆盖范围内。
 *
 *  用 Catch2 v3（amalgamated）。全局 _Module 由 BalloonChatLayoutTests.cpp
 *  统一定义，本文件只引用。
 *
 *  balloonwj@qq.com   2026-06-04
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include "../UI/BalloonImageViewer/ImageViewerGeometry.h"

using namespace balloonimageviewer;

namespace {

SIZE MakeSize(int cx, int cy)
{
    SIZE s;
    s.cx = cx;
    s.cy = cy;
    return s;
}

POINT MakePoint(int x, int y)
{
    POINT p;
    p.x = x;
    p.y = y;
    return p;
}

} // anonymous namespace

// =====================================================================
// V1. 旋转后尺寸：90 / 270 宽高对调，0 / 180 原样，负角 / 超 360 归一
// =====================================================================

TEST_CASE("V1 0 度原样", "[imgviewer][geom]")
{
    SIZE s = RotatedImageSize(MakeSize(1600, 900), 0);
    CHECK(s.cx == 1600);
    CHECK(s.cy == 900);
}

TEST_CASE("V1 90 度宽高对调", "[imgviewer][geom]")
{
    SIZE s = RotatedImageSize(MakeSize(1600, 900), 90);
    CHECK(s.cx == 900);
    CHECK(s.cy == 1600);
}

TEST_CASE("V1 180 度原样", "[imgviewer][geom]")
{
    SIZE s = RotatedImageSize(MakeSize(1600, 900), 180);
    CHECK(s.cx == 1600);
    CHECK(s.cy == 900);
}

TEST_CASE("V1 270 度宽高对调", "[imgviewer][geom]")
{
    SIZE s = RotatedImageSize(MakeSize(1600, 900), 270);
    CHECK(s.cx == 900);
    CHECK(s.cy == 1600);
}

TEST_CASE("V1 负角归一为 270（对调）", "[imgviewer][geom]")
{
    SIZE s = RotatedImageSize(MakeSize(1600, 900), -90);
    CHECK(s.cx == 900);
    CHECK(s.cy == 1600);
}

TEST_CASE("V1 超 360 归一为 90（对调）", "[imgviewer][geom]")
{
    SIZE s = RotatedImageSize(MakeSize(1600, 900), 450);
    CHECK(s.cx == 900);
    CHECK(s.cy == 1600);
}

// =====================================================================
// V2. 适应窗口缩放比：横图 / 竖图取较小比；小图只缩不放（≤100%）；退化兜底
// =====================================================================

TEST_CASE("V2 横向大图取较小比缩进", "[imgviewer][geom]")
{
    // 4000x2000 进 800x600：sx=0.2、sy=0.3，取 0.2。
    double s = ComputeFitScale(MakeSize(4000, 2000), MakeSize(800, 600), false);
    CHECK(s == Catch::Approx(0.2));
}

TEST_CASE("V2 纵向大图取较小比缩进", "[imgviewer][geom]")
{
    // 2000x4000 进 800x600：sx=0.4、sy=0.15，取 0.15。
    double s = ComputeFitScale(MakeSize(2000, 4000), MakeSize(800, 600), false);
    CHECK(s == Catch::Approx(0.15));
}

TEST_CASE("V2 小图只缩不放夹到 100%", "[imgviewer][geom]")
{
    // 100x80 进 800x600 本可放大到 7.5 倍，但 allowUpscale=false 夹到 1.0。
    double s = ComputeFitScale(MakeSize(100, 80), MakeSize(800, 600), false);
    CHECK(s == Catch::Approx(1.0));
}

TEST_CASE("V2 允许放大时小图按真实适应比放大", "[imgviewer][geom]")
{
    // 同上但 allowUpscale=true：min(8.0, 7.5)=7.5。
    double s = ComputeFitScale(MakeSize(100, 80), MakeSize(800, 600), true);
    CHECK(s == Catch::Approx(7.5));
}

TEST_CASE("V2 退化尺寸返回 1.0 兜底", "[imgviewer][geom]")
{
    CHECK(ComputeFitScale(MakeSize(0, 100), MakeSize(800, 600), false) == Catch::Approx(1.0));
    CHECK(ComputeFitScale(MakeSize(100, 100), MakeSize(0, 600), true) == Catch::Approx(1.0));
}

// =====================================================================
// V3. 缩放夹取与步进
// =====================================================================

TEST_CASE("V3 ClampZoom 夹到上下限与区间内原样", "[imgviewer][geom]")
{
    CHECK(ClampZoom(5.0, kMinZoomScale, kMaxZoomScale) == Catch::Approx(kMaxZoomScale));
    CHECK(ClampZoom(0.05, kMinZoomScale, kMaxZoomScale) == Catch::Approx(kMinZoomScale));
    CHECK(ClampZoom(1.0, kMinZoomScale, kMaxZoomScale) == Catch::Approx(1.0));
}

TEST_CASE("V3 SteppedZoom 放大 / 缩小一档", "[imgviewer][geom]")
{
    CHECK(SteppedZoom(1.0, 1, kZoomStepFactor, kMinZoomScale, kMaxZoomScale)
          == Catch::Approx(1.25));
    CHECK(SteppedZoom(1.0, -1, kZoomStepFactor, kMinZoomScale, kMaxZoomScale)
          == Catch::Approx(0.8));
}

TEST_CASE("V3 SteppedZoom 步进撞上 / 下限被夹住", "[imgviewer][geom]")
{
    // 3.5 ×1.25 = 4.375，夹到上限 4.0。
    CHECK(SteppedZoom(3.5, 1, kZoomStepFactor, kMinZoomScale, kMaxZoomScale)
          == Catch::Approx(kMaxZoomScale));
    // 0.12 ÷1.25 = 0.096，夹到下限 0.10。
    CHECK(SteppedZoom(0.12, -1, kZoomStepFactor, kMinZoomScale, kMaxZoomScale)
          == Catch::Approx(kMinZoomScale));
}

TEST_CASE("V3 SteppedZoom direction 为 0 原样", "[imgviewer][geom]")
{
    CHECK(SteppedZoom(1.3, 0, kZoomStepFactor, kMinZoomScale, kMaxZoomScale)
          == Catch::Approx(1.3));
}

// =====================================================================
// V4. 图片落点夹取与小图居中
// =====================================================================

TEST_CASE("V4 大图在可平移区间内原样", "[imgviewer][geom]")
{
    POINT p = ClampImageOrigin(MakePoint(-100, -100), MakeSize(1600, 1200), MakeSize(800, 600));
    CHECK(p.x == -100);
    CHECK(p.y == -100);
}

TEST_CASE("V4 大图向右下越界夹到末端", "[imgviewer][geom]")
{
    // x 下限 800-1600=-800，y 下限 600-1200=-600。
    POINT p = ClampImageOrigin(MakePoint(-2000, -2000), MakeSize(1600, 1200), MakeSize(800, 600));
    CHECK(p.x == -800);
    CHECK(p.y == -600);
}

TEST_CASE("V4 大图向左上越界（正向）夹到 0", "[imgviewer][geom]")
{
    POINT p = ClampImageOrigin(MakePoint(100, 50), MakeSize(1600, 1200), MakeSize(800, 600));
    CHECK(p.x == 0);
    CHECK(p.y == 0);
}

TEST_CASE("V4 小图两维居中锁定（忽略传入落点）", "[imgviewer][geom]")
{
    // (800-400)/2=200，(600-300)/2=150，与传入的 (50,50) 无关。
    POINT p = ClampImageOrigin(MakePoint(50, 50), MakeSize(400, 300), MakeSize(800, 600));
    CHECK(p.x == 200);
    CHECK(p.y == 150);
}

TEST_CASE("V4 一维大可平移、一维小居中", "[imgviewer][geom]")
{
    // x 维 1600≥800：在 [-800,0] 内 -100 原样；y 维 300<600：居中 (600-300)/2=150。
    POINT p = ClampImageOrigin(MakePoint(-100, 999), MakeSize(1600, 300), MakeSize(800, 600));
    CHECK(p.x == -100);
    CHECK(p.y == 150);
}

// =====================================================================
// V5. 锚点缩放落点：锚点下的像素保持不动
// =====================================================================

TEST_CASE("V5 放大 2 倍对着 (100,100) 原点移到 (-100,-100)", "[imgviewer][geom]")
{
    POINT p = OriginForZoomAtAnchor(MakePoint(0, 0), 1.0, 2.0, MakePoint(100, 100));
    CHECK(p.x == -100);
    CHECK(p.y == -100);
}

TEST_CASE("V5 缩小到一半对着 (100,100) 原点移到 (50,50)", "[imgviewer][geom]")
{
    POINT p = OriginForZoomAtAnchor(MakePoint(0, 0), 1.0, 0.5, MakePoint(100, 100));
    CHECK(p.x == 50);
    CHECK(p.y == 50);
}

TEST_CASE("V5 锚点正落在左上角时原点不动", "[imgviewer][geom]")
{
    POINT p = OriginForZoomAtAnchor(MakePoint(10, 20), 1.0, 3.0, MakePoint(10, 20));
    CHECK(p.x == 10);
    CHECK(p.y == 20);
}

TEST_CASE("V5 非零起始落点按比例缩放", "[imgviewer][geom]")
{
    // ratio=4/2=2；x=200-(200-(-50))*2=-300；y=200-(200-(-30))*2=-260。
    POINT p = OriginForZoomAtAnchor(MakePoint(-50, -30), 2.0, 4.0, MakePoint(200, 200));
    CHECK(p.x == -300);
    CHECK(p.y == -260);
}

TEST_CASE("V5 oldScale 非法时退回原落点", "[imgviewer][geom]")
{
    POINT p = OriginForZoomAtAnchor(MakePoint(7, 8), 0.0, 2.0, MakePoint(1, 2));
    CHECK(p.x == 7);
    CHECK(p.y == 8);
}

// =====================================================================
// V6. 缩放百分数读数
// =====================================================================

TEST_CASE("V6 百分数读数四舍五入到整数", "[imgviewer][geom]")
{
    CHECK(FormatZoomReadout(0.96) == CString(_T("96%")));
    CHECK(FormatZoomReadout(1.0) == CString(_T("100%")));
    CHECK(FormatZoomReadout(0.2) == CString(_T("20%")));
    CHECK(FormatZoomReadout(1.5) == CString(_T("150%")));
    CHECK(FormatZoomReadout(0.333) == CString(_T("33%")));
    CHECK(FormatZoomReadout(4.0) == CString(_T("400%")));
}

// ---- 拖动画布：平移图片还是移动窗口（用例 IVD1～IVD5）---------------------

namespace {

// 测试用的系统拖动阈值（像素），与 Windows 默认的 SM_CXDRAG / SM_CYDRAG 一致
const int kTestDragThreshold = 4;
// 按下点（宿主客户区坐标）
const int kTestPressX = 200;
const int kTestPressY = 150;

SIZE DragThreshold()
{
    return MakeSize(kTestDragThreshold, kTestDragThreshold);
}

} // namespace

TEST_CASE("IVD1 图片能完整显示时，拖过阈值就移动窗口", "[imgviewer][drag]")
{
    const POINT press = MakePoint(kTestPressX, kTestPressY);
    // 横向、纵向各自超过阈值都算
    CHECK(ResolveCanvasDrag(false, false, press,
                            MakePoint(kTestPressX + kTestDragThreshold + 1, kTestPressY),
                            DragThreshold()) == kCanvasDragMoveWindow);
    CHECK(ResolveCanvasDrag(false, false, press,
                            MakePoint(kTestPressX, kTestPressY - kTestDragThreshold - 1),
                            DragThreshold()) == kCanvasDragMoveWindow);
}

TEST_CASE("IVD2 图片放大到超出窗口时，拖动仍是平移图片", "[imgviewer][drag]")
{
    const POINT press = MakePoint(kTestPressX, kTestPressY);
    // 平移从按下就开始，不等阈值（与改动前一致）
    CHECK(ResolveCanvasDrag(false, true, press, press, DragThreshold()) == kCanvasDragPanImage);
    CHECK(ResolveCanvasDrag(false, true, press,
                            MakePoint(kTestPressX + kTestDragThreshold * 10, kTestPressY),
                            DragThreshold()) == kCanvasDragPanImage);
}

TEST_CASE("IVD3 按下后没移出阈值：既不移动窗口也不平移，单击与双击不受影响", "[imgviewer][drag]")
{
    const POINT press = MakePoint(kTestPressX, kTestPressY);
    CHECK(ResolveCanvasDrag(false, false, press, press, DragThreshold()) == kCanvasDragNone);
    // 恰好等于阈值仍不算移出
    CHECK(ResolveCanvasDrag(false, false, press,
                            MakePoint(kTestPressX + kTestDragThreshold, kTestPressY + kTestDragThreshold),
                            DragThreshold()) == kCanvasDragNone);
}

TEST_CASE("IVD4 还没有加载图片时，拖动移动窗口", "[imgviewer][drag]")
{
    const POINT press = MakePoint(kTestPressX, kTestPressY);
    // 没有图片时谈不上平移，pannable 恒为 false，结论与图片能完整显示时相同
    CHECK(ResolveCanvasDrag(false, false, press,
                            MakePoint(kTestPressX + kTestDragThreshold + 1, kTestPressY),
                            DragThreshold()) == kCanvasDragMoveWindow);
}

TEST_CASE("IVD5 按在底部工具栏上时不移动窗口", "[imgviewer][drag]")
{
    const POINT press = MakePoint(kTestPressX, kTestPressY);
    const POINT distant = MakePoint(kTestPressX + kTestDragThreshold * 10, kTestPressY);
    CHECK(ResolveCanvasDrag(true, false, press, distant, DragThreshold()) == kCanvasDragNone);
    CHECK(ResolveCanvasDrag(true, true, press, distant, DragThreshold()) == kCanvasDragNone);
}
