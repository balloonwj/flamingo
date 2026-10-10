/**
 *  锁屏动画场景共用的小工具：数值截断与插值、缓动曲线、随机数、HSL 换算颜色，以及用 Direct2D 路径几何
 *  逐段拼出曲线的构建器（对应样稿里 canvas 的 moveTo / lineTo / quadraticCurveTo / bezierCurveTo）。
 *
 *  典型用法：
 *      CPathBuilder pb;
 *      if (pb.Begin(env.m_d2d))
 *      {
 *          pb.MoveTo(0, 0, true);
 *          pb.QuadTo(10, -5, 20, 0);
 *          pb.Close();
 *          CComPtr<ID2D1PathGeometry> geo = pb.Finish();
 *      }
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include <math.h>
#include <stdlib.h>

#include "../Render/LockD2D.h"

namespace flamingolock {

const float kScenePi = 3.14159265f;      // 圆周率
const float kSceneTwoPi = 6.2831853f;    // 2π
const float kSceneRadToDeg = 57.2957795f;  // 弧度换算为度（D2D 的旋转矩阵以度为单位）

// 把 v 限制在 [lo, hi]
inline float ClampF(float v, float lo, float hi)
{
    return (v < lo) ? lo : ((v > hi) ? hi : v);
}

// 把 v 限制在 [0, 1]
inline float Clamp01F(float v)
{
    return ClampF(v, 0.0f, 1.0f);
}

// 线性插值：t 为 0 时取 a，为 1 时取 b
inline float LerpF(float a, float b, float t)
{
    return a + (b - a) * t;
}

// 三次缓出：开始快、结束慢
inline float EaseOutCubicF(float t)
{
    const float u = 1.0f - t;
    return 1.0f - u * u * u;
}

// 回弹缓出：略微越过终点再回落（标准系数 1.70158）
inline float EaseOutBackF(float t)
{
    const float c1 = 1.70158f;
    const float c3 = c1 + 1.0f;
    const float u = t - 1.0f;
    return 1.0f + c3 * u * u * u + c1 * u * u;
}

// 正弦缓入缓出
inline float EaseInOutSineF(float t)
{
    return -(cosf(kScenePi * t) - 1.0f) / 2.0f;
}

// [a, b) 之间的随机数（锁屏线程上使用 rand，线程启动时已播种）
inline float RandRangeF(float a, float b)
{
    return a + (b - a) * (static_cast<float>(rand()) / (static_cast<float>(RAND_MAX) + 1.0f));
}

// [0, n) 之间的随机整数；n 不大于 0 时返回 0
inline int RandIndex(int n)
{
    if (n <= 0)
    {
        return 0;
    }
    return static_cast<int>(RandRangeF(0.0f, static_cast<float>(n))) % n;
}

/**
 *  HSL 换算成 Direct2D 颜色（与 CSS 的 hsla 相同）。
 *  @param hue   色相，度（任意值，按 360 取模）
 *  @param sat   饱和度 0~1
 *  @param light 亮度 0~1
 *  @param alpha 不透明度 0~1
 *  @return 颜色
 */
D2D1_COLOR_F HslColor(float hue, float sat, float light, float alpha);

/**
 *  逐段拼出一个 Direct2D 路径几何。一个路径里可以有多个图形（每次 MoveTo 开始一个新图形）。
 *  只在锁屏线程上使用。
 */
class CPathBuilder
{
public:
    CPathBuilder();
    ~CPathBuilder();

    /**
     *  开始一个新路径。
     *  @param factory Direct2D 工厂（借用）
     *  @return 成功返回 true；失败时之后的调用都不起作用，Finish 返回空指针
     */
    bool Begin(ID2D1Factory* factory);

    /**
     *  开始一个新图形；上一个图形若还开着，按不闭合结束。
     *  @param x      起点横坐标
     *  @param y      起点纵坐标
     *  @param filled 图形是否参与填充（只描边的线条传 false）
     */
    void MoveTo(float x, float y, bool filled);

    // 直线到 (x, y)
    void LineTo(float x, float y);

    // 二次贝塞尔曲线：控制点 (cx, cy)，终点 (x, y)
    void QuadTo(float cx, float cy, float x, float y);

    // 三次贝塞尔曲线：控制点 (c1x, c1y)、(c2x, c2y)，终点 (x, y)
    void CubicTo(float c1x, float c1y, float c2x, float c2y, float x, float y);

    // 闭合当前图形（回到起点）
    void Close();

    /**
     *  结束路径并取出几何。
     *  @return 几何；Begin 失败或写入失败时返回空指针
     */
    CComPtr<ID2D1PathGeometry> Finish();

private:
    CPathBuilder(const CPathBuilder&);
    CPathBuilder& operator=(const CPathBuilder&);

    CComPtr<ID2D1PathGeometry> m_geometry;   // 正在构建的几何；Finish 时交出
    CComPtr<ID2D1GeometrySink> m_sink;       // 几何的写入口；Finish 时关闭并释放
    bool                       m_figureOpen; // 是否有一个图形开着（已 MoveTo、尚未结束）
};

/**
 *  创建圆头圆角的描边样式（样稿里 lineCap = 'round' 的线条）。
 *  @param factory Direct2D 工厂
 *  @return 描边样式；失败返回空指针（此时按默认样式描边）
 */
CComPtr<ID2D1StrokeStyle> MakeRoundStroke(ID2D1Factory* factory);

/**
 *  创建一个径向渐变画刷：中心处为 inner，边缘为 outer。中心与半径之后用 SetCenter / SetRadiusX / SetRadiusY 调整。
 *  @param rt    渲染目标
 *  @param inner 中心颜色
 *  @param outer 边缘颜色
 *  @return 画刷；失败返回空指针
 */
CComPtr<ID2D1RadialGradientBrush> MakeRadialBrush(ID2D1RenderTarget* rt, const D2D1_COLOR_F& inner,
                                                 const D2D1_COLOR_F& outer);

/**
 *  创建一个两色线性渐变画刷：起点为 from，终点为 to。起止点之后用 SetStartPoint / SetEndPoint 调整。
 *  @param rt   渲染目标
 *  @param from 起点颜色
 *  @param to   终点颜色
 *  @return 画刷；失败返回空指针
 */
CComPtr<ID2D1LinearGradientBrush> MakeLinearBrush(ID2D1RenderTarget* rt, const D2D1_COLOR_F& from,
                                                 const D2D1_COLOR_F& to);

/**
 *  以 (cx, cy) 为中心、半径 rx × ry 画一团径向渐变光晕（Direct2D 1.0 没有模糊特效，发光与阴影都用它近似）。
 *  @param rt      渲染目标
 *  @param brush   径向渐变画刷（MakeRadialBrush 创建，中心不透明、边缘透明）
 *  @param cx      中心横坐标
 *  @param cy      中心纵坐标
 *  @param rx      横向半径
 *  @param ry      纵向半径
 *  @param opacity 整体不透明度 0~1
 */
void DrawGlowAt(ID2D1RenderTarget* rt, ID2D1RadialGradientBrush* brush, float cx, float cy, float rx, float ry,
                float opacity);

} // namespace flamingolock
