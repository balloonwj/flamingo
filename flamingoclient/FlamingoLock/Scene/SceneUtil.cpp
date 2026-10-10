/**
 *  SceneUtil.cpp —— 锁屏动画场景共用小工具的实现，说明见 SceneUtil.h。
 *  balloonwj@qq.com   2026-10-06
 */
#include "SceneUtil.h"

namespace flamingolock {

namespace {

const float kHueSectors = 6.0f;    // 色相环分为六段（红、黄、绿、青、蓝、品红）
const float kHueFull = 360.0f;     // 色相一整圈的度数
const float kHueSector = 60.0f;    // 每段的度数

} // namespace

D2D1_COLOR_F HslColor(float hue, float sat, float light, float alpha)
{
    float h = fmodf(hue, kHueFull);
    if (h < 0.0f)
    {
        h += kHueFull;
    }
    const float s = Clamp01F(sat);
    const float l = Clamp01F(light);
    //标准 HSL → RGB 换算：c 为色度，x 为本段内的第二分量，m 为补齐到亮度的偏移
    const float c = (1.0f - fabsf(2.0f * l - 1.0f)) * s;
    const float hp = h / kHueSector;
    const float x = c * (1.0f - fabsf(fmodf(hp, 2.0f) - 1.0f));
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    const int sector = static_cast<int>(hp) % static_cast<int>(kHueSectors);
    switch (sector)
    {
    //红到黄
    case 0:
        r = c;
        g = x;
        break;
    //黄到绿
    case 1:
        r = x;
        g = c;
        break;
    //绿到青
    case 2:
        g = c;
        b = x;
        break;
    //青到蓝
    case 3:
        g = x;
        b = c;
        break;
    //蓝到品红
    case 4:
        r = x;
        b = c;
        break;
    //品红到红
    default:
        r = c;
        b = x;
        break;
    }
    const float m = l - c / 2.0f;
    return D2D1::ColorF(r + m, g + m, b + m, Clamp01F(alpha));
}

CPathBuilder::CPathBuilder()
    : m_figureOpen(false)
{
}

CPathBuilder::~CPathBuilder()
{
    if (m_sink != NULL)
    {
        m_sink->Close();
    }
}

bool CPathBuilder::Begin(ID2D1Factory* factory)
{
    m_sink.Release();
    m_geometry.Release();
    m_figureOpen = false;
    if (factory == NULL || FAILED(factory->CreatePathGeometry(&m_geometry)))
    {
        return false;
    }
    if (FAILED(m_geometry->Open(&m_sink)))
    {
        m_geometry.Release();
        return false;
    }
    return true;
}

void CPathBuilder::MoveTo(float x, float y, bool filled)
{
    if (m_sink == NULL)
    {
        return;
    }
    if (m_figureOpen)
    {
        m_sink->EndFigure(D2D1_FIGURE_END_OPEN);
    }
    m_sink->BeginFigure(D2D1::Point2F(x, y), filled ? D2D1_FIGURE_BEGIN_FILLED : D2D1_FIGURE_BEGIN_HOLLOW);
    m_figureOpen = true;
}

void CPathBuilder::LineTo(float x, float y)
{
    if (m_sink != NULL && m_figureOpen)
    {
        m_sink->AddLine(D2D1::Point2F(x, y));
    }
}

void CPathBuilder::QuadTo(float cx, float cy, float x, float y)
{
    if (m_sink != NULL && m_figureOpen)
    {
        m_sink->AddQuadraticBezier(D2D1::QuadraticBezierSegment(D2D1::Point2F(cx, cy), D2D1::Point2F(x, y)));
    }
}

void CPathBuilder::CubicTo(float c1x, float c1y, float c2x, float c2y, float x, float y)
{
    if (m_sink != NULL && m_figureOpen)
    {
        m_sink->AddBezier(D2D1::BezierSegment(D2D1::Point2F(c1x, c1y), D2D1::Point2F(c2x, c2y), D2D1::Point2F(x, y)));
    }
}

void CPathBuilder::Close()
{
    if (m_sink != NULL && m_figureOpen)
    {
        m_sink->EndFigure(D2D1_FIGURE_END_CLOSED);
        m_figureOpen = false;
    }
}

CComPtr<ID2D1PathGeometry> CPathBuilder::Finish()
{
    CComPtr<ID2D1PathGeometry> result;
    if (m_sink == NULL)
    {
        return result;
    }
    if (m_figureOpen)
    {
        m_sink->EndFigure(D2D1_FIGURE_END_OPEN);
        m_figureOpen = false;
    }
    const HRESULT hr = m_sink->Close();
    m_sink.Release();
    if (SUCCEEDED(hr))
    {
        result = m_geometry;
    }
    m_geometry.Release();
    return result;
}

CComPtr<ID2D1StrokeStyle> MakeRoundStroke(ID2D1Factory* factory)
{
    CComPtr<ID2D1StrokeStyle> style;
    if (factory != NULL)
    {
        factory->CreateStrokeStyle(D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND,
                                                               D2D1_CAP_STYLE_ROUND, D2D1_LINE_JOIN_ROUND),
                                   NULL, 0, &style);
    }
    return style;
}

CComPtr<ID2D1RadialGradientBrush> MakeRadialBrush(ID2D1RenderTarget* rt, const D2D1_COLOR_F& inner,
                                                 const D2D1_COLOR_F& outer)
{
    CComPtr<ID2D1RadialGradientBrush> brush;
    D2D1_GRADIENT_STOP stops[2] = { { 0.0f, inner }, { 1.0f, outer } };
    CComPtr<ID2D1GradientStopCollection> collection;
    if (FAILED(rt->CreateGradientStopCollection(stops, 2, &collection)))
    {
        return brush;
    }
    rt->CreateRadialGradientBrush(D2D1::RadialGradientBrushProperties(D2D1::Point2F(0.0f, 0.0f),
                                                                      D2D1::Point2F(0.0f, 0.0f), 1.0f, 1.0f),
                                  collection, &brush);
    return brush;
}

CComPtr<ID2D1LinearGradientBrush> MakeLinearBrush(ID2D1RenderTarget* rt, const D2D1_COLOR_F& from,
                                                 const D2D1_COLOR_F& to)
{
    CComPtr<ID2D1LinearGradientBrush> brush;
    D2D1_GRADIENT_STOP stops[2] = { { 0.0f, from }, { 1.0f, to } };
    CComPtr<ID2D1GradientStopCollection> collection;
    if (FAILED(rt->CreateGradientStopCollection(stops, 2, &collection)))
    {
        return brush;
    }
    rt->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(D2D1::Point2F(0.0f, 0.0f),
                                                                      D2D1::Point2F(0.0f, 1.0f)),
                                  collection, &brush);
    return brush;
}

void DrawGlowAt(ID2D1RenderTarget* rt, ID2D1RadialGradientBrush* brush, float cx, float cy, float rx, float ry,
                float opacity)
{
    if (rt == NULL || brush == NULL || rx <= 0.0f || ry <= 0.0f || opacity <= 0.0f)
    {
        return;
    }
    brush->SetCenter(D2D1::Point2F(cx, cy));
    brush->SetGradientOriginOffset(D2D1::Point2F(0.0f, 0.0f));
    brush->SetRadiusX(rx);
    brush->SetRadiusY(ry);
    brush->SetOpacity(Clamp01F(opacity));
    rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), rx, ry), brush);
}

} // namespace flamingolock
