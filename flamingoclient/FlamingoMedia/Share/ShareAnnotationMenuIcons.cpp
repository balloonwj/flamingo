/**
 *  标注工具条的效果预览图标，说明见 ShareAnnotationMenuIcons.h。
 *
 *  样例笔迹的坐标按图标方框的比例给出（0 为左 / 上边，1 为右 / 下边），四周留出约八分之一的边；线宽按图标边长换成
 *  AnnotationStroke::m_width（像素 = m_width × 边长 ÷ kAnnotWidthScale），文字与数字标号的字号同理（kAnnotTextScale）。
 *
 *  balloonwj@qq.com   2026-10-05
 */
#include "ShareAnnotationMenuIcons.h"

#include <math.h>
#include <string.h>
#include <string>
#include <vector>
#include <objidl.h>    // gdiplus.h 依赖 IStream 等 COM 声明
#include <gdiplus.h>
#include "ShareAnnotation.h"
#include "ShareAnnotationRender.h"    // RenderAnnotationLayer：样例笔迹用标注的绘制代码画
#include "ShareAnnotationSession.h"   // kAnnotEraser、kAnnotSelect、粗细档位

#pragma comment(lib, "gdiplus.lib")

namespace screenshare {

namespace {

//每像素字节数（BGRA）与不透明
const int kBpp = 4;
const int kOpaque = 255;

//样例颜色：当前颜色太浅（亮度高于该值，0~255）时改用深灰，否则在白色菜单底上看不见
const int kLightLuma = 200;
const uint32_t kFallbackInk = 0x505050;
//亮度权重（ITU-R BT.601，千分比）
const int kLumaR = 299;
const int kLumaG = 587;
const int kLumaB = 114;
const int kLumaTotal = 1000;
//示意图形（橡皮、光标、删除等）用的深灰与浅色
const uint32_t kGlyphInk = 0x505050;
const uint32_t kGlyphPaper = 0xFFFFFF;
//橡皮示意图形上半截的粉色
const uint32_t kEraserPink = 0xF48FB1;
//示意图形的描边线宽（像素）
const float kGlyphPenPx = 1.0f;
//垃圾桶、回转箭头这类细节较多的示意图形用的描边线宽（像素）
const float kGlyphBoldPenPx = 1.2f;
//自定义颜色示意图形的四个扇区颜色：红、黄、绿、蓝
const uint32_t kWheelColors[] = { 0xE53935, 0xFDD835, 0x43A047, 0x1E88E5 };
const int kWheelSectors = 4;
const float kFullCircleDeg = 360.0f;

//选中时的底色与边框（与 balloonui 主色一致的蓝）
const uint32_t kCheckedFill = 0xDCE8FB;
const uint32_t kCheckedBorder = 0x2D6CDF;
//选中底的圆角半径与边框线宽（像素）
const float kCheckedRadius = 3.0f;
const float kCheckedBorderPx = 1.0f;

//样例四周留的边（占边长的比例）与常用位置
const double kMargin = 0.14;
const double kFar = 1.0 - kMargin;
const double kMid = 0.5;

//各类样例的线宽（像素）
const double kPencilPx = 1.0;
const double kPenPx = 2.0;
const double kMarkerPx = 3.0;
const double kHighlighterPx = 6.0;
const double kBrushPx = 3.0;
const double kLaserPx = 2.0;
const double kShapePx = 1.0;
const double kArrowPx = 1.0;
const double kLineStylePx = 2.0;
//粗细三档样例的线宽（像素）：细、中、粗
const double kWidthLevelPx[] = { 1.0, 2.0, 4.0 };
//文字样例的字号（像素）与数字标号的字号（像素，标号圆的半径按字号换算）
const double kTextPx = 12.0;
const double kBadgeTextPx = 8.0;
//文字样例左上角的位置（占边长的比例）
const double kTextX = 0.16;
const double kTextY = 0.0;
//文字背景样例的字号（像素，按放大 kBadgeSupersample 倍的画布计）与左上角：缩小后白底框四周露出浅灰底
const double kTextBgSamplePx = 12.0;
const double kTextBgX = 0.3;
const double kTextBgY = 0.2;
//序号写法样例的字号（像素）：两个字要放进图标宽度
const double kSequenceTextPx = 8.0;

//波浪线样例的点数与振幅（占边长的比例）、周期数
const int kWavePoints = 16;
const double kWaveAmplitude = 0.2;
const double kWaveCycles = 1.0;
const double kTwoPi = 6.283185307179586;
//毛笔样例：点的间距按这个比例忽疏忽密，绘制代码按运笔快慢画出粗细变化
const double kBrushSpacingSwing = 0.6;

//连接线样例两端小方块的边长（占边长的比例）
const double kNodeSide = 0.22;
//数字标号的最小半径是 8 像素，16 像素的图标放不下（定位针还要把圆向上抬），先在这么多倍大的画布上画再缩小
const int kBadgeSupersample = 2;
//文字背景样例下面垫的浅灰底（白底在白色菜单上才看得出来）与它离图标边缘的距离（像素）
const uint32_t kTextBackdrop = 0xE4E6EA;
const int kTextBackdropInset = 2;

//GDI+ 在本模块惰性初始化一次（进程内多次 GdiplusStartup 是安全的，进程退出时由系统回收）
void ensureGdiplus()
{
    static ULONG_PTR token = 0;
    if (token != 0)
        return;
    Gdiplus::GdiplusStartupInput input;
    Gdiplus::GdiplusStartup(&token, &input, NULL);
}

Gdiplus::Color toColor(uint32_t rgb, int alpha)
{
    return Gdiplus::Color(static_cast<BYTE>(alpha), static_cast<BYTE>((rgb >> 16) & 0xFF),
                          static_cast<BYTE>((rgb >> 8) & 0xFF), static_cast<BYTE>(rgb & 0xFF));
}

//样例用的颜色：当前颜色太浅时换成深灰
uint32_t sampleColor(uint32_t rgb)
{
    const int r = static_cast<int>((rgb >> 16) & 0xFF);
    const int g = static_cast<int>((rgb >> 8) & 0xFF);
    const int b = static_cast<int>(rgb & 0xFF);
    const int luma = (r * kLumaR + g * kLumaG + b * kLumaB) / kLumaTotal;
    return (luma > kLightLuma) ? kFallbackInk : rgb;
}

//图标方框里按比例给出的点
AnnotationPoint at(double fx, double fy)
{
    const double x = fx < 0 ? 0 : (fx > 1 ? 1 : fx);
    const double y = fy < 0 ? 0 : (fy > 1 ? 1 : fy);
    return AnnotationPoint(static_cast<uint16_t>(lround(x * kAnnotCoordMax)),
                           static_cast<uint16_t>(lround(y * kAnnotCoordMax)));
}

//线宽像素换成 m_width（绘制时按图标宽度换回像素）
int widthFor(double px, int size)
{
    const long w = lround(px * kAnnotWidthScale / size);
    return w < 1 ? 1 : static_cast<int>(w);
}

//字号像素换成 m_width（文字与数字标号）
int textSizeFor(double px, int size)
{
    const long w = lround(px * kAnnotTextScale / size);
    return w < 1 ? 1 : static_cast<int>(w);
}

AnnotationStroke makeStroke(int tool, uint32_t color, int width)
{
    AnnotationStroke s;
    s.m_tool = tool;
    s.m_color = color;
    s.m_width = width;
    //激光笔没画完时不淡出，样例按全不透明画
    s.m_finished = false;
    return s;
}

//两点笔迹（形状、箭头、直线等）：从 (x0, y0) 到 (x1, y1)
AnnotationStroke makeTwoPoint(int tool, uint32_t color, int width, double x0, double y0, double x1, double y1)
{
    AnnotationStroke s = makeStroke(tool, color, width);
    s.m_points.push_back(at(x0, y0));
    s.m_points.push_back(at(x1, y1));
    return s;
}

//波浪形的手绘笔迹（画笔组）；brushSpacing 为真时点距忽疏忽密，毛笔据此画出粗细变化
AnnotationStroke makeWave(int tool, uint32_t color, int width, bool brushSpacing)
{
    AnnotationStroke s = makeStroke(tool, color, width);
    double t = 0;
    for (int i = 0; i < kWavePoints; ++i)
    {
        const double x = kMargin + (kFar - kMargin) * t;
        const double y = kMid + kWaveAmplitude * sin(kTwoPi * kWaveCycles * t);
        s.m_points.push_back(at(x, y));
        double step = 1.0 / (kWavePoints - 1);
        if (brushSpacing)
            step *= 1.0 + kBrushSpacingSwing * sin(kTwoPi * t);
        t += step;
        if (t > 1)
            t = 1;
    }
    return s;
}

//预乘透明度的 src 叠到 dst 上（同尺寸、同行宽）
void blendOver(uint8_t* dst, int dstStride, const std::vector<uint8_t>& src, int size)
{
    for (int y = 0; y < size; ++y)
    {
        uint8_t* d = dst + static_cast<size_t>(y) * dstStride;
        const uint8_t* s = &src[static_cast<size_t>(y) * size * kBpp];
        for (int x = 0; x < size; ++x)
        {
            const int a = s[x * kBpp + 3];
            if (a == 0)
                continue;
            for (int c = 0; c < kBpp; ++c)
            {
                const int v = s[x * kBpp + c] + d[x * kBpp + c] * (kOpaque - a) / kOpaque;
                d[x * kBpp + c] = static_cast<uint8_t>(v > kOpaque ? kOpaque : v);
            }
        }
    }
}

//用标注的绘制代码把样例笔迹画进一张透明图层
std::vector<uint8_t> renderStrokes(const std::vector<AnnotationStroke>& strokes, int size)
{
    std::vector<uint8_t> layer;
    RenderAnnotationLayer(size, size, strokes, NULL, 0, true, layer);
    return layer;
}

//先在 scale 倍大的画布上画样例笔迹，再高质量缩小到 size（笔迹是按 size × scale 构造的）
std::vector<uint8_t> renderStrokesScaled(const std::vector<AnnotationStroke>& strokes, int size, int scale)
{
    if (scale <= 1)
        return renderStrokes(strokes, size);
    const int big = size * scale;
    std::vector<uint8_t> large = renderStrokes(strokes, big);
    std::vector<uint8_t> out(static_cast<size_t>(size) * size * kBpp, 0);
    if (large.empty())
        return out;
    ensureGdiplus();
    {
        Gdiplus::Bitmap src(big, big, big * kBpp, PixelFormat32bppPARGB, &large[0]);
        Gdiplus::Bitmap dst(size, size, size * kBpp, PixelFormat32bppPARGB, &out[0]);
        Gdiplus::Graphics g(&dst);
        g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
        g.SetCompositingMode(Gdiplus::CompositingModeSourceCopy);
        g.DrawImage(&src, Gdiplus::Rect(0, 0, size, size), 0, 0, big, big, Gdiplus::UnitPixel);
    }
    return out;
}

//文字背景样例下面的浅灰底：四周各留 kTextBackdropInset 像素，选中时的浅蓝底与边框仍看得见
void paintTextBackdrop(uint8_t* bgra, int stride, int size)
{
    for (int y = kTextBackdropInset; y < size - kTextBackdropInset; ++y)
    {
        uint8_t* row = bgra + static_cast<size_t>(y) * stride;
        for (int x = kTextBackdropInset; x < size - kTextBackdropInset; ++x)
        {
            row[x * kBpp + 0] = static_cast<uint8_t>(kTextBackdrop & 0xFF);
            row[x * kBpp + 1] = static_cast<uint8_t>((kTextBackdrop >> 8) & 0xFF);
            row[x * kBpp + 2] = static_cast<uint8_t>((kTextBackdrop >> 16) & 0xFF);
            row[x * kBpp + 3] = kOpaque;
        }
    }
}

//示意图形的绘制函数：在 size × size 的 GDI+ 画布上画
typedef void (*GlyphPainter)(Gdiplus::Graphics& g, float size);

std::vector<uint8_t> renderGlyph(GlyphPainter painter, int size)
{
    std::vector<uint8_t> layer(static_cast<size_t>(size) * size * kBpp, 0);
    ensureGdiplus();
    Gdiplus::Bitmap bmp(size, size, size * kBpp, PixelFormat32bppPARGB, &layer[0]);
    Gdiplus::Graphics g(&bmp);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    painter(g, static_cast<float>(size));
    return layer;
}

//橡皮本体：斜放 45 度，上半截粉色、下半截白色，中心在 (cx, cy)，尺寸按 scale 缩放
void drawEraserBody(Gdiplus::Graphics& g, float s, float cx, float cy, float scale)
{
    const float kBodyW = 0.42f;    //宽占边长的比例
    const float kBodyH = 0.78f;    //长占边长的比例
    const float kTiltDeg = 45.0f;  //倾斜角度
    Gdiplus::Matrix m;
    m.Translate(cx, cy);
    m.Rotate(kTiltDeg);
    g.SetTransform(&m);
    const float w = s * kBodyW * scale;
    const float h = s * kBodyH * scale;
    Gdiplus::SolidBrush pink(toColor(kEraserPink, kOpaque));
    Gdiplus::SolidBrush paper(toColor(kGlyphPaper, kOpaque));
    Gdiplus::Pen ink(toColor(kGlyphInk, kOpaque), kGlyphPenPx);
    g.FillRectangle(&pink, -w / 2, -h / 2, w, h / 2);
    g.FillRectangle(&paper, -w / 2, 0.0f, w, h / 2);
    g.DrawRectangle(&ink, -w / 2, -h / 2, w, h);
    g.ResetTransform();
}

//整笔擦除：居中的橡皮
void paintEraser(Gdiplus::Graphics& g, float s)
{
    drawEraserBody(g, s, s / 2, s / 2, 1.0f);
}

//局部擦除：小一号的橡皮在右上，左下拖一道点线表示擦过的路径
void paintAreaEraser(Gdiplus::Graphics& g, float s)
{
    const float kTrailY = 0.82f;    //点线的纵向位置（占边长的比例）
    const float kTrailX0 = 0.06f;   //点线起点
    const float kTrailX1 = 0.56f;   //点线终点
    const float kBodyX = 0.66f;     //橡皮中心
    const float kBodyY = 0.42f;
    const float kShrink = 0.72f;    //橡皮缩小的比例
    Gdiplus::Pen trail(toColor(kGlyphInk, kOpaque), kGlyphPenPx);
    trail.SetDashStyle(Gdiplus::DashStyleDot);
    g.DrawLine(&trail, s * kTrailX0, s * kTrailY, s * kTrailX1, s * kTrailY);
    drawEraserBody(g, s, s * kBodyX, s * kBodyY, kShrink);
}

//选择对象：箭头光标（白色描边的深色箭头）
void paintCursor(Gdiplus::Graphics& g, float s)
{
    const Gdiplus::PointF arrow[] = {
        Gdiplus::PointF(s * 0.22f, s * 0.08f), Gdiplus::PointF(s * 0.22f, s * 0.84f),
        Gdiplus::PointF(s * 0.40f, s * 0.66f), Gdiplus::PointF(s * 0.54f, s * 0.94f),
        Gdiplus::PointF(s * 0.66f, s * 0.88f), Gdiplus::PointF(s * 0.52f, s * 0.60f),
        Gdiplus::PointF(s * 0.78f, s * 0.60f) };
    const int count = static_cast<int>(sizeof(arrow) / sizeof(arrow[0]));
    Gdiplus::SolidBrush ink(toColor(kGlyphInk, kOpaque));
    Gdiplus::Pen halo(toColor(kGlyphPaper, kOpaque), kGlyphPenPx);
    g.FillPolygon(&ink, arrow, count);
    g.DrawPolygon(&halo, arrow, count);
}

//删除选中的对象：垃圾桶
void paintTrash(Gdiplus::Graphics& g, float s)
{
    Gdiplus::Pen ink(toColor(kGlyphInk, kOpaque), kGlyphBoldPenPx);
    //桶身（上宽下窄）、桶盖、提手、两道竖纹
    const Gdiplus::PointF body[] = { Gdiplus::PointF(s * 0.24f, s * 0.30f), Gdiplus::PointF(s * 0.76f, s * 0.30f),
                                     Gdiplus::PointF(s * 0.70f, s * 0.90f), Gdiplus::PointF(s * 0.30f, s * 0.90f) };
    g.DrawPolygon(&ink, body, static_cast<int>(sizeof(body) / sizeof(body[0])));
    g.DrawLine(&ink, s * 0.14f, s * 0.22f, s * 0.86f, s * 0.22f);
    g.DrawLine(&ink, s * 0.40f, s * 0.22f, s * 0.42f, s * 0.10f);
    g.DrawLine(&ink, s * 0.42f, s * 0.10f, s * 0.58f, s * 0.10f);
    g.DrawLine(&ink, s * 0.58f, s * 0.10f, s * 0.60f, s * 0.22f);
    g.DrawLine(&ink, s * 0.43f, s * 0.42f, s * 0.44f, s * 0.80f);
    g.DrawLine(&ink, s * 0.57f, s * 0.42f, s * 0.56f, s * 0.80f);
}

//自定义颜色：四色圆盘
void paintColorWheel(Gdiplus::Graphics& g, float s)
{
    const float inset = s * 0.12f;
    const float d = s - 2 * inset;
    const float sweep = kFullCircleDeg / kWheelSectors;
    for (int i = 0; i < kWheelSectors; ++i)
    {
        Gdiplus::SolidBrush b(toColor(kWheelColors[i], kOpaque));
        g.FillPie(&b, inset, inset, d, d, sweep * i, sweep);
    }
    Gdiplus::Pen ink(toColor(kGlyphInk, kOpaque), kGlyphPenPx);
    g.DrawEllipse(&ink, inset, inset, d, d);
}

//从头编号：逆时针的回转箭头，中间一个「1」
void paintReset(Gdiplus::Graphics& g, float s)
{
    const float inset = s * 0.14f;
    const float d = s - 2 * inset;
    const float kArcStartDeg = -60.0f;
    const float kArcSweepDeg = 290.0f;
    Gdiplus::Pen ink(toColor(kGlyphInk, kOpaque), kGlyphBoldPenPx);
    g.DrawArc(&ink, inset, inset, d, d, kArcStartDeg, kArcSweepDeg);
    //箭头头部：画在圆弧起点（右上方）
    const Gdiplus::PointF head[] = { Gdiplus::PointF(s * 0.66f, s * 0.04f), Gdiplus::PointF(s * 0.88f, s * 0.20f),
                                     Gdiplus::PointF(s * 0.62f, s * 0.30f) };
    Gdiplus::SolidBrush fill(toColor(kGlyphInk, kOpaque));
    g.FillPolygon(&fill, head, static_cast<int>(sizeof(head) / sizeof(head[0])));
    Gdiplus::FontFamily family(L"Arial");
    const float kDigitPx = s * 0.5f;
    Gdiplus::Font font(&family, kDigitPx, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::StringFormat center;
    center.SetAlignment(Gdiplus::StringAlignmentCenter);
    center.SetLineAlignment(Gdiplus::StringAlignmentCenter);
    g.DrawString(L"1", -1, &font, Gdiplus::RectF(0, 0, s, s), &center, &fill);
}

//收起工具条：向左的双箭头
void paintCollapse(Gdiplus::Graphics& g, float s)
{
    const float kTop = 0.24f;      //箭头上端（占边长的比例）
    const float kBottom = 0.76f;   //箭头下端
    const float kTipA = 0.22f;     //第一个箭头的尖
    const float kTailA = 0.48f;    //第一个箭头的尾
    const float kShift = 0.28f;    //第二个箭头向右错开的距离
    const float kPenPx = 1.6f;     //箭头线宽（像素）
    Gdiplus::Pen ink(toColor(kGlyphInk, kOpaque), kPenPx);
    ink.SetLineJoin(Gdiplus::LineJoinRound);
    ink.SetStartCap(Gdiplus::LineCapRound);
    ink.SetEndCap(Gdiplus::LineCapRound);
    for (int i = 0; i < 2; ++i)
    {
        const float dx = kShift * i;
        const Gdiplus::PointF v[] = { Gdiplus::PointF(s * (kTailA + dx), s * kTop),
                                      Gdiplus::PointF(s * (kTipA + dx), s * 0.5f),
                                      Gdiplus::PointF(s * (kTailA + dx), s * kBottom) };
        g.DrawLines(&ink, v, static_cast<int>(sizeof(v) / sizeof(v[0])));
    }
}

//颜色项：当前颜色的圆角色块（菜单里颜色项另用 ShareMouseEffects 的色块，这里给分组按钮等其它场合）
void fillSwatch(uint8_t* bgra, int stride, int size, uint32_t rgb)
{
    std::vector<uint8_t> layer(static_cast<size_t>(size) * size * kBpp, 0);
    ensureGdiplus();
    {
        Gdiplus::Bitmap bmp(size, size, size * kBpp, PixelFormat32bppPARGB, &layer[0]);
        Gdiplus::Graphics g(&bmp);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        Gdiplus::SolidBrush b(toColor(rgb, kOpaque));
        Gdiplus::Pen ink(toColor(kGlyphInk, kOpaque), kGlyphPenPx);
        const float inset = size * 0.16f;
        g.FillEllipse(&b, inset, inset, size - 2 * inset, size - 2 * inset);
        g.DrawEllipse(&ink, inset, inset, size - 2 * inset, size - 2 * inset);
    }
    blendOver(bgra, stride, layer, size);
}

//选中底：浅蓝圆角底加主色边框
void paintCheckedBack(uint8_t* bgra, int stride, int size)
{
    std::vector<uint8_t> layer(static_cast<size_t>(size) * size * kBpp, 0);
    ensureGdiplus();
    {
        Gdiplus::Bitmap bmp(size, size, size * kBpp, PixelFormat32bppPARGB, &layer[0]);
        Gdiplus::Graphics g(&bmp);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        Gdiplus::GraphicsPath path;
        const float x = kCheckedBorderPx / 2;
        const float w = size - kCheckedBorderPx;
        const float r2 = kCheckedRadius * 2;
        path.AddArc(x, x, r2, r2, 180.0f, 90.0f);
        path.AddArc(x + w - r2, x, r2, r2, 270.0f, 90.0f);
        path.AddArc(x + w - r2, x + w - r2, r2, r2, 0.0f, 90.0f);
        path.AddArc(x, x + w - r2, r2, r2, 90.0f, 90.0f);
        path.CloseFigure();
        Gdiplus::SolidBrush fill(toColor(kCheckedFill, kOpaque));
        Gdiplus::Pen border(toColor(kCheckedBorder, kOpaque), kCheckedBorderPx);
        g.FillPath(&fill, &path);
        g.DrawPath(&border, &path);
    }
    blendOver(bgra, stride, layer, size);
}

//两端各一个小方块（连接线样例的两个节点）
void appendNodes(std::vector<AnnotationStroke>& out, uint32_t color, int size)
{
    out.push_back(makeTwoPoint(annot_tool_rect, color, widthFor(kShapePx, size), 0.04, 1 - 0.04 - kNodeSide,
                               0.04 + kNodeSide, 1 - 0.04));
    out.push_back(makeTwoPoint(annot_tool_rect, color, widthFor(kShapePx, size), 1 - 0.04 - kNodeSide, 0.04,
                               1 - 0.04, 0.04 + kNodeSide));
}

//连接线样例：两个节点之间按走向连线，终点按当前终点样式
AnnotationStroke makeConnector(uint32_t color, int size, int routing, const AnnotBarState& state)
{
    const double inner = 0.04 + kNodeSide;
    AnnotationStroke s = makeTwoPoint(annot_tool_connector, color, widthFor(kArrowPx, size), inner, 1 - inner,
                                      1 - inner, inner);
    s.m_style.m_routing = routing;
    s.m_style.m_startCap = annot_cap_none;
    s.m_style.m_endCap = state.m_endCap;
    return s;
}

//文字样例（一个字母）：字号 px，左上角在 (fx, fy)
AnnotationStroke makeText(uint32_t color, int size, const char* text, bool bold, int bg, double px, double fx,
                          double fy)
{
    AnnotationStroke s = makeStroke(annot_tool_text, color, textSizeFor(px, size));
    s.m_points.push_back(at(fx, fy));
    s.m_text = text;
    s.m_style.m_bold = bold;
    s.m_style.m_textBg = bg;
    s.m_style.m_bgColor = AnnotTextBgColor(color);
    return s;
}

//数字标号样例
AnnotationStroke makeBadge(uint32_t color, int size, int variant, int sequence)
{
    AnnotationStroke s = makeStroke(annot_tool_badge, color, textSizeFor(kBadgeTextPx, size));
    //定位针的圆在点的上方，点放在接近底边处
    s.m_points.push_back(at(kMid, variant == annot_badge_pin ? 0.9 : kMid));
    s.m_style.m_variant = variant;
    s.m_style.m_sequence = sequence;
    s.m_style.m_number = 1;
    return s;
}

//序号写法样例：头两个序号的字样
AnnotationStroke makeSequenceText(uint32_t color, int size, int sequence)
{
    AnnotationStroke s = makeStroke(annot_tool_text, color, textSizeFor(kSequenceTextPx, size));
    s.m_points.push_back(at(0.02, 0.14));
    s.m_style.m_bold = true;
    switch (sequence)
    {
    //A B C
    case annot_seq_letter:
        s.m_text = "AB";
        break;
    //一 二 三
    case annot_seq_chinese:
        s.m_text = u8"一二";   //i18n-ignore: 序号写法的示意字形，不是界面文字
        break;
    //1 2 3
    default:
        s.m_text = "12";
        break;
    }
    return s;
}

//画笔组、线条组、形状组等工具的样例
bool toolStrokes(int tool, uint32_t color, int size, const AnnotBarState& state, std::vector<AnnotationStroke>& out)
{
    switch (tool)
    {
    //铅笔：细
    case annot_tool_pencil:
        out.push_back(makeWave(tool, color, widthFor(kPencilPx, size), false));
        return true;
    //钢笔：中等粗细
    case annot_tool_pen:
        out.push_back(makeWave(tool, color, widthFor(kPenPx, size), false));
        return true;
    //马克笔：粗、方头
    case annot_tool_marker:
        out.push_back(makeWave(tool, color, widthFor(kMarkerPx, size), false));
        return true;
    //荧光笔：更宽、半透明
    case annot_tool_highlighter:
        out.push_back(makeWave(tool, color, widthFor(kHighlighterPx, size), false));
        return true;
    //毛笔：点距忽疏忽密，画出粗细变化
    case annot_tool_brush:
        out.push_back(makeWave(tool, color, widthFor(kBrushPx, size), true));
        return true;
    //激光笔：发亮的笔迹
    case annot_tool_laser:
        out.push_back(makeWave(tool, color, widthFor(kLaserPx, size), false));
        return true;
    //直线
    case annot_tool_line:
        out.push_back(makeTwoPoint(tool, color, widthFor(kPenPx, size), kMargin, kFar, kFar, kMargin));
        return true;
    //折线：四个顶点，锯齿形
    case annot_tool_polyline:
        {
            AnnotationStroke s = makeStroke(tool, color, widthFor(kPenPx, size));
            s.m_points.push_back(at(kMargin, 0.78));
            s.m_points.push_back(at(0.38, 0.26));
            s.m_points.push_back(at(0.62, 0.74));
            s.m_points.push_back(at(kFar, 0.22));
            out.push_back(s);
        }
        return true;
    //曲线：穿过三个点的平滑弧，与锯齿形的折线一眼能分开
    case annot_tool_curve:
        {
            AnnotationStroke s = makeStroke(tool, color, widthFor(kPenPx, size));
            s.m_points.push_back(at(kMargin, 0.8));
            s.m_points.push_back(at(kMid, 0.2));
            s.m_points.push_back(at(kFar, 0.8));
            out.push_back(s);
        }
        return true;
    //各种形状：画在留了边的方框里，空心
    case annot_tool_rect:
    case annot_tool_round_rect:
    case annot_tool_ellipse:
    case annot_tool_triangle:
    case annot_tool_diamond:
    case annot_tool_pentagon:
    case annot_tool_hexagon:
    case annot_tool_star:
    case annot_tool_parallelogram:
    case annot_tool_bubble:
    case annot_tool_cloud:
        out.push_back(makeTwoPoint(tool, color, widthFor(kShapePx, size), 0.08, 0.12, 0.92, 0.88));
        return true;
    //文字：一个字母 A
    case annot_tool_text:
        out.push_back(makeText(color, size, "A", state.m_textBold, annot_text_bg_none, kTextPx, kTextX, kTextY));
        return true;
    //数字标号：按当前样式（size 由调用方按放大后的画布给出，见 kBadgeSupersample）
    case annot_tool_badge:
        out.push_back(makeBadge(color, size, state.m_badgeStyle, state.m_badgeSequence));
        return true;
    //箭头：按当前走向与两端样式
    case annot_tool_arrow:
        {
            AnnotationStroke s = makeTwoPoint(tool, color, widthFor(kArrowPx, size), kMargin, kFar, kFar, kMargin);
            s.m_style.m_routing = state.m_routing;
            s.m_style.m_startCap = state.m_startCap;
            s.m_style.m_endCap = state.m_endCap;
            out.push_back(s);
        }
        return true;
    //块状箭头：按当前样式，实心
    case annot_tool_block_arrow:
        {
            AnnotationStroke s = makeTwoPoint(tool, color, widthFor(kShapePx, size), 0.06, 0.18, 0.94, 0.82);
            s.m_style.m_variant = state.m_blockVariant;
            s.m_style.m_fill = annot_fill_solid;
            out.push_back(s);
        }
        return true;
    //网络拓扑图元：按当前种类
    case annot_tool_icon:
        {
            AnnotationStroke s = makeTwoPoint(tool, color, widthFor(kShapePx, size), 0.04, 0.04, 0.96, 0.96);
            s.m_style.m_variant = state.m_iconKind;
            out.push_back(s);
        }
        return true;
    //连接线：两个节点之间按当前走向连线
    case annot_tool_connector:
        appendNodes(out, color, size);
        out.push_back(makeConnector(color, size, state.m_connRouting, state));
        return true;
    //其余（局部橡皮、整笔橡皮、选择）由调用方画示意图形
    default:
        return false;
    }
}

//工具命令里不是笔迹、要画示意图形的几种
GlyphPainter toolGlyph(int tool)
{
    if (tool == kAnnotEraser)
        return paintEraser;
    if (tool == annot_tool_eraser_area)
        return paintAreaEraser;
    if (tool == kAnnotSelect)
        return paintCursor;
    return NULL;
}

//工具命令的取值是否有图标
bool toolHasIcon(int tool)
{
    if (toolGlyph(tool) != NULL)
        return true;
    return tool >= annot_tool_pen && tool <= kAnnotToolLast;
}

} // namespace

bool AnnotChoiceHasIcon(const AnnotMenuChoice& choice)
{
    switch (choice.m_command)
    {
    //选工具：画笔、线条、形状、文字、标号、箭头、图元、连接线、两种橡皮与选择
    case annot_bar_tool:
        return toolHasIcon(choice.m_value);
    //选颜色与自定义颜色
    case annot_bar_color:
    case annot_bar_custom_color:
    //粗细、线型、填充
    case annot_bar_width:
    case annot_bar_line_style:
    case annot_bar_fill:
    //箭头走向、两端样式、块状箭头
    case annot_bar_routing:
    case annot_bar_start_cap:
    case annot_bar_end_cap:
    case annot_bar_block:
    //数字标号的样式、序号写法、从头编号
    case annot_bar_badge_style:
    case annot_bar_badge_seq:
    case annot_bar_badge_reset:
    //文字加粗与背景
    case annot_bar_text_bold:
    case annot_bar_text_bg:
    //拓扑图元、连接线走向、删除选中的对象
    case annot_bar_icon:
    case annot_bar_conn_routing:
    case annot_bar_delete:
        return true;
    //撤销、清空、权限、退出、保存等工具条按钮命令与非法命令
    default:
        return false;
    }
}

bool RenderAnnotChoiceIcon(const AnnotMenuChoice& choice, const AnnotBarState& state, bool checked, int size,
                           uint8_t* bgra, int stride)
{
    if (!AnnotChoiceHasIcon(choice) || size <= 0 || bgra == NULL || stride < size * kBpp)
        return false;
    for (int y = 0; y < size; ++y)
        memset(bgra + static_cast<size_t>(y) * stride, 0, static_cast<size_t>(size) * kBpp);
    if (checked)
        paintCheckedBack(bgra, stride, size);
    const uint32_t color = sampleColor(state.m_color);
    std::vector<AnnotationStroke> strokes;
    GlyphPainter glyph = NULL;
    //样例笔迹在几倍大的画布上画（数字标号放大画再缩小）
    int scale = 1;
    switch (choice.m_command)
    {
    //选工具（数字标号工具放大画）
    case annot_bar_tool:
        glyph = toolGlyph(choice.m_value);
        if (choice.m_value == annot_tool_badge)
            scale = kBadgeSupersample;
        if (glyph == NULL)
            toolStrokes(choice.m_value, color, size * scale, state, strokes);
        break;
    //选颜色：那种颜色的圆形色块
    case annot_bar_color:
        fillSwatch(bgra, stride, size, static_cast<uint32_t>(choice.m_value));
        return true;
    //自定义颜色：四色圆盘
    case annot_bar_custom_color:
        glyph = paintColorWheel;
        break;
    //粗细：一道横线，按档位的线宽
    case annot_bar_width:
        {
            const int level = (choice.m_value < annot_width_thin || choice.m_value > annot_width_thick)
                                  ? annot_width_medium : choice.m_value;
            strokes.push_back(makeTwoPoint(annot_tool_line, color, widthFor(kWidthLevelPx[level], size), kMargin, kMid,
                                           kFar, kMid));
        }
        break;
    //线型：一道横线，按线型
    case annot_bar_line_style:
        {
            AnnotationStroke s = makeTwoPoint(annot_tool_line, color, widthFor(kLineStylePx, size), 0.02, kMid, 0.98,
                                              kMid);
            s.m_style.m_lineStyle = choice.m_value;
            strokes.push_back(s);
        }
        break;
    //填充：一个矩形，按填充方式
    case annot_bar_fill:
        {
            AnnotationStroke s = makeTwoPoint(annot_tool_rect, color, widthFor(kShapePx, size), 0.12, 0.2, 0.88, 0.8);
            s.m_style.m_fill = choice.m_value;
            strokes.push_back(s);
        }
        break;
    //箭头走向：按这种走向画箭头，两端按当前样式
    case annot_bar_routing:
        {
            AnnotBarState st = state;
            st.m_routing = choice.m_value;
            toolStrokes(annot_tool_arrow, color, size, st, strokes);
        }
        break;
    //起点样式：一道横线，只有起点（左端）带这种样式
    case annot_bar_start_cap:
    //终点样式：一道横线，只有终点（右端）带这种样式
    case annot_bar_end_cap:
        {
            AnnotationStroke s = makeTwoPoint(annot_tool_arrow, color, widthFor(kArrowPx, size), 0.2, kMid, 0.8,
                                              kMid);
            s.m_style.m_startCap = (choice.m_command == annot_bar_start_cap) ? choice.m_value : annot_cap_none;
            s.m_style.m_endCap = (choice.m_command == annot_bar_end_cap) ? choice.m_value : annot_cap_none;
            strokes.push_back(s);
        }
        break;
    //块状箭头：按这种样式
    case annot_bar_block:
        {
            AnnotBarState st = state;
            st.m_blockVariant = choice.m_value;
            toolStrokes(annot_tool_block_arrow, color, size, st, strokes);
        }
        break;
    //数字标号样式：按这种样式画「1」号，放大画再缩小
    case annot_bar_badge_style:
        scale = kBadgeSupersample;
        strokes.push_back(makeBadge(color, size * scale, choice.m_value, state.m_badgeSequence));
        break;
    //序号写法：直接写出头两个序号（12、AB、一二），与标号样式的图标区分开
    case annot_bar_badge_seq:
        strokes.push_back(makeSequenceText(color, size, choice.m_value));
        break;
    //从头编号：回转箭头
    case annot_bar_badge_reset:
        glyph = paintReset;
        break;
    //加粗：粗体的 B
    case annot_bar_text_bold:
        strokes.push_back(makeText(color, size, "B", true, annot_text_bg_none, kTextPx, kTextX, kTextY));
        break;
    //文字背景：字母 A，按这种背景；下面垫浅灰底，白底才看得出来。放大画再缩小，背景框才不会占满整个图标
    case annot_bar_text_bg:
        paintTextBackdrop(bgra, stride, size);
        scale = kBadgeSupersample;
        strokes.push_back(makeText(color, size * scale, "A", state.m_textBold, choice.m_value, kTextBgSamplePx,
                                   kTextBgX, kTextBgY));
        break;
    //拓扑图元：画这种图元
    case annot_bar_icon:
        {
            AnnotBarState st = state;
            st.m_iconKind = choice.m_value;
            toolStrokes(annot_tool_icon, color, size, st, strokes);
        }
        break;
    //连接线走向：两个节点之间按这种走向连线
    case annot_bar_conn_routing:
        appendNodes(strokes, color, size);
        strokes.push_back(makeConnector(color, size, choice.m_value, state));
        break;
    //删除选中的对象：垃圾桶
    case annot_bar_delete:
        glyph = paintTrash;
        break;
    //其余命令在 AnnotChoiceHasIcon 里已排除
    default:
        break;
    }
    if (glyph != NULL)
        blendOver(bgra, stride, renderGlyph(glyph, size), size);
    else if (!strokes.empty())
        blendOver(bgra, stride, renderStrokesScaled(strokes, size, scale), size);
    return true;
}

bool AnnotMenuItemIconChoice(const AnnotMenuItem& item, AnnotMenuChoice& out)
{
    if (item.m_separator || !item.m_enabled)
        return false;
    //子菜单父项：用打勾的那一项，没有打勾的取第一项
    if (!item.m_children.empty())
    {
        const AnnotMenuItem* pick = NULL;
        for (size_t i = 0; i < item.m_children.size(); ++i)
        {
            const AnnotMenuItem& c = item.m_children[i];
            if (c.m_separator || !c.m_enabled || c.m_id == 0)
                continue;
            if (pick == NULL || c.m_checked)
                pick = &c;
            if (c.m_checked)
                break;
        }
        return pick != NULL && AnnotMenuItemIconChoice(*pick, out);
    }
    AnnotMenuChoice c;
    if (item.m_id == 0 || !DecodeAnnotMenuChoice(item.m_id, c) || !AnnotChoiceHasIcon(c))
        return false;
    //颜色项的取值是常用色的下标，换成颜色本身
    if (c.m_command == annot_bar_color && c.m_value >= 0 && c.m_value < kAnnotPaletteSize)
        c.m_value = static_cast<int>(kAnnotPalette[c.m_value]);
    out = c;
    return true;
}

bool RenderAnnotCollapseGlyph(int size, uint8_t* bgra, int stride)
{
    if (size <= 0 || bgra == NULL || stride < size * kBpp)
        return false;
    for (int y = 0; y < size; ++y)
        memset(bgra + static_cast<size_t>(y) * stride, 0, static_cast<size_t>(size) * kBpp);
    blendOver(bgra, stride, renderGlyph(paintCollapse, size), size);
    return true;
}

bool AnnotGroupButtonChoice(int group, const AnnotBarState& state, AnnotMenuChoice& out)
{
    AnnotMenuChoice c;
    c.m_command = annot_bar_tool;
    const bool inGroup = (AnnotToolGroup(state.m_tool) == group);
    switch (group)
    {
    //画笔组：当前工具或钢笔
    case annot_group_pen:
        c.m_value = inGroup ? state.m_tool : annot_tool_pen;
        break;
    //线条组：当前工具或直线
    case annot_group_line:
        c.m_value = inGroup ? state.m_tool : annot_tool_line;
        break;
    //箭头组：在用块状箭头时为块状箭头的样式，否则为当前走向的箭头
    case annot_group_arrow:
        if (state.m_tool == annot_tool_block_arrow)
        {
            c.m_command = annot_bar_block;
            c.m_value = state.m_blockVariant;
        }
        else
        {
            c.m_command = annot_bar_routing;
            c.m_value = state.m_routing;
        }
        break;
    //形状组：当前工具或矩形
    case annot_group_shape:
        c.m_value = inGroup ? state.m_tool : annot_tool_rect;
        break;
    //数字标号组：当前样式
    case annot_group_badge:
        c.m_command = annot_bar_badge_style;
        c.m_value = state.m_badgeStyle;
        break;
    //文字组：字母 A
    case annot_group_text:
        c.m_value = annot_tool_text;
        break;
    //橡皮组：当前工具或整笔擦除
    case annot_group_eraser:
        c.m_value = inGroup ? state.m_tool : kAnnotEraser;
        break;
    //粗细、线型、填充：当前取值
    case annot_group_width:
        c.m_command = annot_bar_width;
        c.m_value = state.m_widthLevel;
        break;
    case annot_group_line_style:
        c.m_command = annot_bar_line_style;
        c.m_value = state.m_lineStyle;
        break;
    case annot_group_fill:
        c.m_command = annot_bar_fill;
        c.m_value = state.m_fill;
        break;
    //选择组：箭头光标
    case annot_group_select:
        c.m_value = kAnnotSelect;
        break;
    //拓扑组：在用连接线时为连接线，否则为当前图元
    case annot_group_topology:
        if (state.m_tool == annot_tool_connector)
        {
            c.m_value = annot_tool_connector;
        }
        else
        {
            c.m_command = annot_bar_icon;
            c.m_value = state.m_iconKind;
        }
        break;
    //颜色组按钮前面是色块；非法组没有图标
    default:
        return false;
    }
    out = c;
    return true;
}

} // namespace screenshare
