/**
 *  屏幕共享标注的坐标换算与绘制，见 ShareAnnotationRender.h。
 *  第 19 期起绘制改为先用 GDI+ 把全部笔迹画进一张预乘 alpha 的图层（抗锯齿、虚线、填充、局部橡皮、激光笔淡出、
 *  标号与文字背景），再一次合成到目标上；颜色键窗口先把图层的半透明像素有序抖动成全不透明或全透明。
 *
 *  balloonwj@qq.com   2026-10-03
 */
#include "ShareAnnotationRender.h"

#include <math.h>
#include <algorithm>
#include <string>
#include <objidl.h>        // gdiplus.h 依赖 IStream 等 COM 声明
#include <gdiplus.h>
#include "ShareAnnotationGeometry.h"
#include "ShareAnnotationIcons.h"     // 网络拓扑图元的矢量图形（第 20 期）
#include "ShareAnnotationObjects.h"   // 连接线实际的两端（第 20 期）
#include "ShareAnnotationSession.h"   // kAnnotEraser（整笔橡皮的光标）、AnnotSelectionVisual（第 20 期）

#pragma comment(lib, "msimg32.lib")   //AlphaBlend（图层合成）
#pragma comment(lib, "gdiplus.lib")

namespace screenshare {

namespace {

//箭头两翼与箭杆的夹角（弧度，30 度）
const double kArrowWingAngle = 0.5235987755982988;
//荧光笔的不透明度（0~255）
const int kHighlighterAlpha = 110;
//半透明填充的不透明度（0~255）
const int kTranslucentFillAlpha = 90;
//铅笔：线宽为画笔的这一比例，颜色向白色淡化的比例，不透明度
const double kPencilWidthRatio = 0.6;
const double kPencilLighten = 0.2;
const int kPencilAlpha = 230;
//激光笔：外圈光晕的宽度是线宽的倍数与不透明度
const double kLaserGlowRatio = 2.2;
const int kLaserGlowAlpha = 60;
//两端样式的大小：线宽的倍数，且不小于下限（像素）
const double kCapPerWidth = 3.5;
const double kCapMinPx = 9.0;
//数字标号：半径是文字字号的倍数，且不小于下限（像素）；标号文字字号是半径的倍数；定位针的圆心在点上方半径的倍数处
const double kBadgeRadiusPerText = 0.8;
const double kBadgeMinRadius = 8.0;
const double kBadgeLabelRatio = 1.1;
const double kPinLift = 1.6;
//定位针尖角根部：左右各离圆心半径的这一倍数，往下离圆心半径的这一倍数
const double kPinTailHalfWidth = 0.7;
const double kPinTailDrop = 0.5;
//图层 DIB 的位深
const WORD kLayerBitsPerPixel = 32;
//空心标号、空心圆的描边宽度（像素）
const float kBadgeOutlinePx = 2.0f;
//文字背景的内边距：字号的这一比例，且不小于下限（像素）
const int kTextBgPadDivisor = 6;
const int kTextBgMinPad = 2;
//图层每像素的字节数与不透明
const int kBytesPerPixel = 4;
const int kOpaque = 255;
//4×4 有序抖动矩阵（Bayer），每格阈值 = (值 + 0.5) × 16
const int kBayer[4][4] = { { 0, 8, 2, 10 }, { 12, 4, 14, 6 }, { 3, 11, 1, 9 }, { 15, 7, 13, 5 } };
const int kBayerStep = 16;
const int kBayerSize = 4;
//没有指定字体时用的字体名（标注文字是用户内容，不跟随界面语言换字体）
const wchar_t* const kTextFace = L"Microsoft YaHei";
//第 20 期：图元名字的字号是图元边长的多少倍、最小字号（像素）、与图元的间距（像素）
const double kIconLabelRatio = 0.2;
const float kIconLabelMinPx = 10.0f;
const float kIconLabelGapPx = 2.0f;
//名字的白色描边：向四周各偏 1 像素画一遍白字，压在任何底色上都看得清
const float kLabelHaloPx = 1.0f;
//连接线上文字的字号（按内容高度的比例）、最小字号（像素）与白底的内边距（像素）
const double kConnectorLabelRatio = 0.022;
const float kConnectorLabelMinPx = 10.0f;
const float kConnectorLabelPadPx = 2.0f;
//图元细节的线宽是外框线宽的多少倍
const float kIconDetailWidthRatio = 0.7f;
//选择提示：颜色（蓝）、虚线框外扩的像素、虚线框线宽、句柄边长与描边宽、选择框填充的不透明度与边线宽
const uint32_t kSelectColor = 0x1E88E5;
const float kSelectPadPx = 4.0f;
const float kSelectLinePx = 1.5f;
const float kHandleSizePx = 8.0f;
const float kHandleBorderPx = 1.5f;
const int kBandFillAlpha = 40;
const float kBandLinePx = 1.0f;
//白色
const uint32_t kWhiteRgb = 0xFFFFFF;

//GDI+ 在本模块惰性初始化一次（进程内多次 GdiplusStartup 是安全的，进程退出时由系统回收）
void ensureGdiplus()
{
    static ULONG_PTR token = 0;
    if (token != 0)
        return;
    Gdiplus::GdiplusStartupInput input;
    Gdiplus::GdiplusStartup(&token, &input, NULL);
}

//0x00RRGGBB 加不透明度换成 GDI+ 颜色
Gdiplus::Color toColor(uint32_t rgb, int alpha)
{
    if (alpha < 0)
        alpha = 0;
    if (alpha > kOpaque)
        alpha = kOpaque;
    return Gdiplus::Color(static_cast<BYTE>(alpha), static_cast<BYTE>((rgb >> 16) & 0xFF),
                          static_cast<BYTE>((rgb >> 8) & 0xFF), static_cast<BYTE>(rgb & 0xFF));
}

//颜色向白色淡化 ratio（铅笔用）
uint32_t lighten(uint32_t rgb, double ratio)
{
    uint32_t out = 0;
    for (int shift = 0; shift <= 16; shift += 8)
    {
        const int c = static_cast<int>((rgb >> shift) & 0xFF);
        const int v = static_cast<int>(c + (kOpaque - c) * ratio);
        out |= static_cast<uint32_t>(v > kOpaque ? kOpaque : v) << shift;
    }
    return out;
}

//UTF-8 转宽字符
std::wstring utf8ToWide(const std::string& s)
{
    if (s.empty())
        return std::wstring();
    const int n = ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), NULL, 0);
    if (n <= 0)
        return std::wstring();
    std::wstring w(static_cast<size_t>(n), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), &w[0], n);
    return w;
}

//归一化坐标换成图层像素坐标（浮点）
GeoPoint toGeo(const RECT& content, const AnnotationPoint& p)
{
    const double w = static_cast<double>(content.right - content.left);
    const double h = static_cast<double>(content.bottom - content.top);
    return GeoPoint(content.left + p.m_x * w / kAnnotCoordMax, content.top + p.m_y * h / kAnnotCoordMax);
}

std::vector<Gdiplus::PointF> toPointF(const std::vector<GeoPoint>& pts)
{
    std::vector<Gdiplus::PointF> out;
    out.reserve(pts.size());
    for (size_t i = 0; i < pts.size(); ++i)
        out.push_back(Gdiplus::PointF(static_cast<Gdiplus::REAL>(pts[i].m_x), static_cast<Gdiplus::REAL>(pts[i].m_y)));
    return out;
}

//按线型设置画笔的虚线；点线用圆头，免得点画成小方块
void applyLineStyle(Gdiplus::Pen& pen, int lineStyle)
{
    const std::vector<float> dash = AnnotDashPattern(lineStyle);
    if (dash.empty())
        return;
    pen.SetDashPattern(&dash[0], static_cast<INT>(dash.size()));
    pen.SetDashCap(lineStyle == annot_line_dot ? Gdiplus::DashCapRound : Gdiplus::DashCapFlat);
}

//单位向量（a → b）；两点重合时返回 (1, 0)
GeoPoint unitDir(GeoPoint a, GeoPoint b)
{
    const double dx = b.m_x - a.m_x;
    const double dy = b.m_y - a.m_y;
    const double len = sqrt(dx * dx + dy * dy);
    if (len <= 0)
        return GeoPoint(1, 0);
    return GeoPoint(dx / len, dy / len);
}

//把路径的一端沿线缩短 inset（像素）；这一段比 inset 还短时不缩
void shortenEnd(std::vector<GeoPoint>& path, bool atStart, double inset)
{
    if (path.size() < 2 || inset <= 0)
        return;
    GeoPoint& end = atStart ? path.front() : path.back();
    const GeoPoint& inner = atStart ? path[1] : path[path.size() - 2];
    const double dx = inner.m_x - end.m_x;
    const double dy = inner.m_y - end.m_y;
    const double len = sqrt(dx * dx + dy * dy);
    if (len <= inset)
        return;
    end.m_x += dx / len * inset;
    end.m_y += dy / len * inset;
}

//画线条一端的图形
void drawCap(Gdiplus::Graphics& g, const AnnotCapShape& cap, const Gdiplus::Color& color, float widthPx)
{
    if (cap.m_points.empty())
        return;
    const std::vector<Gdiplus::PointF> pts = toPointF(cap.m_points);
    Gdiplus::Pen pen(color, widthPx);
    pen.SetLineJoin(Gdiplus::LineJoinRound);
    pen.SetStartCap(Gdiplus::LineCapRound);
    pen.SetEndCap(Gdiplus::LineCapRound);
    if (cap.m_closed && cap.m_filled)
    {
        Gdiplus::SolidBrush brush(color);
        g.FillPolygon(&brush, &pts[0], static_cast<INT>(pts.size()));
        return;
    }
    if (cap.m_closed)
        g.DrawPolygon(&pen, &pts[0], static_cast<INT>(pts.size()));
    else
        g.DrawLines(&pen, &pts[0], static_cast<INT>(pts.size()));
}

//画一条带两端样式的路径（直线、折线、曲线、箭头）
void drawPathWithCaps(Gdiplus::Graphics& g, std::vector<GeoPoint> path, const AnnotationStyle& style,
                      const Gdiplus::Color& color, float widthPx)
{
    if (path.size() < 2)
        return;
    double capSize = widthPx * kCapPerWidth;
    if (capSize < kCapMinPx)
        capSize = kCapMinPx;
    //两端的方向：从相邻点指向端点
    const GeoPoint startTip = path.front();
    const GeoPoint endTip = path.back();
    const AnnotCapShape startCap = AnnotCapGeometry(style.m_startCap, startTip, unitDir(path[1], startTip), capSize);
    const AnnotCapShape endCap = AnnotCapGeometry(style.m_endCap, endTip, unitDir(path[path.size() - 2], endTip),
                                                  capSize);
    shortenEnd(path, true, startCap.m_inset);
    shortenEnd(path, false, endCap.m_inset);
    const std::vector<Gdiplus::PointF> pts = toPointF(path);
    Gdiplus::Pen pen(color, widthPx);
    pen.SetLineJoin(Gdiplus::LineJoinRound);
    pen.SetStartCap(Gdiplus::LineCapRound);
    pen.SetEndCap(Gdiplus::LineCapRound);
    applyLineStyle(pen, style.m_lineStyle);
    g.DrawLines(&pen, &pts[0], static_cast<INT>(pts.size()));
    drawCap(g, startCap, color, widthPx);
    drawCap(g, endCap, color, widthPx);
}

//画一个闭合轮廓：按填充方式先填充再描边
void drawOutline(Gdiplus::Graphics& g, const std::vector<GeoPoint>& outline, const AnnotationStyle& style,
                 uint32_t rgb, int alphaScale, float widthPx)
{
    if (outline.size() < 3)
        return;
    const std::vector<Gdiplus::PointF> pts = toPointF(outline);
    if (style.m_fill == annot_fill_solid || style.m_fill == annot_fill_translucent)
    {
        const int fillAlpha = (style.m_fill == annot_fill_solid ? kOpaque : kTranslucentFillAlpha) * alphaScale / kOpaque;
        Gdiplus::SolidBrush brush(toColor(rgb, fillAlpha));
        g.FillPolygon(&brush, &pts[0], static_cast<INT>(pts.size()));
    }
    Gdiplus::Pen pen(toColor(rgb, alphaScale), widthPx);
    pen.SetLineJoin(Gdiplus::LineJoinRound);
    applyLineStyle(pen, style.m_lineStyle);
    g.DrawPolygon(&pen, &pts[0], static_cast<INT>(pts.size()));
}

//画一笔自由线条（画笔、铅笔、马克笔、荧光笔、激光笔、毛笔、局部橡皮）
void drawFreehand(Gdiplus::Graphics& g, const AnnotationStroke& s, const std::vector<GeoPoint>& pts, float widthPx,
                  int alphaScale)
{
    if (pts.empty())
        return;
    std::vector<GeoPoint> path = pts;
    //只有一个点：复制一个，画成圆点
    if (path.size() == 1)
        path.push_back(GeoPoint(path[0].m_x + 0.01, path[0].m_y));
    const std::vector<Gdiplus::PointF> fpts = toPointF(path);
    switch (s.m_tool)
    {
    //局部橡皮：把它之前画的像素擦成透明
    case annot_tool_eraser_area:
        {
            Gdiplus::Pen pen(Gdiplus::Color(0, 0, 0, 0), widthPx);
            pen.SetLineJoin(Gdiplus::LineJoinRound);
            pen.SetStartCap(Gdiplus::LineCapRound);
            pen.SetEndCap(Gdiplus::LineCapRound);
            const Gdiplus::CompositingMode old = g.GetCompositingMode();
            g.SetCompositingMode(Gdiplus::CompositingModeSourceCopy);
            g.DrawLines(&pen, &fpts[0], static_cast<INT>(fpts.size()));
            g.SetCompositingMode(old);
        }
        return;
    //毛笔：每段按两端点的线宽画，快细慢粗
    case annot_tool_brush:
        {
            const std::vector<double> widths = AnnotBrushWidths(path, widthPx);
            for (size_t i = 1; i < path.size(); ++i)
            {
                Gdiplus::Pen pen(toColor(s.m_color, alphaScale),
                                 static_cast<Gdiplus::REAL>((widths[i - 1] + widths[i]) / 2));
                pen.SetStartCap(Gdiplus::LineCapRound);
                pen.SetEndCap(Gdiplus::LineCapRound);
                g.DrawLine(&pen, fpts[i - 1], fpts[i]);
            }
        }
        return;
    //激光笔：先画一圈半透明的光晕，再画实心的芯
    case annot_tool_laser:
        {
            Gdiplus::Pen glow(toColor(s.m_color, kLaserGlowAlpha * alphaScale / kOpaque),
                              static_cast<Gdiplus::REAL>(widthPx * kLaserGlowRatio));
            glow.SetLineJoin(Gdiplus::LineJoinRound);
            glow.SetStartCap(Gdiplus::LineCapRound);
            glow.SetEndCap(Gdiplus::LineCapRound);
            g.DrawLines(&glow, &fpts[0], static_cast<INT>(fpts.size()));
            Gdiplus::Pen core(toColor(s.m_color, alphaScale), widthPx);
            core.SetLineJoin(Gdiplus::LineJoinRound);
            core.SetStartCap(Gdiplus::LineCapRound);
            core.SetEndCap(Gdiplus::LineCapRound);
            g.DrawLines(&core, &fpts[0], static_cast<INT>(fpts.size()));
        }
        return;
    //其余：画笔、铅笔、马克笔、荧光笔
    default:
        break;
    }
    uint32_t rgb = s.m_color;
    int alpha = alphaScale;
    float w = widthPx;
    Gdiplus::LineCap cap = Gdiplus::LineCapRound;
    if (s.m_tool == annot_tool_pencil)
    {
        rgb = lighten(rgb, kPencilLighten);
        alpha = kPencilAlpha * alphaScale / kOpaque;
        w = static_cast<float>(widthPx * kPencilWidthRatio);
        if (w < 1.0f)
            w = 1.0f;
    }
    else if (s.m_tool == annot_tool_highlighter)
    {
        alpha = kHighlighterAlpha * alphaScale / kOpaque;
    }
    else if (s.m_tool == annot_tool_marker)
    {
        cap = Gdiplus::LineCapSquare;
    }
    Gdiplus::Pen pen(toColor(rgb, alpha), w);
    pen.SetLineJoin(Gdiplus::LineJoinRound);
    pen.SetStartCap(cap);
    pen.SetEndCap(cap);
    g.DrawLines(&pen, &fpts[0], static_cast<INT>(fpts.size()));
}

//画数字标号
void drawBadge(Gdiplus::Graphics& g, const RECT& content, const AnnotationStroke& s, int alphaScale)
{
    const GeoPoint at = toGeo(content, s.m_points[0]);
    double radius = AnnotTextPx(content, s.m_width) * kBadgeRadiusPerText;
    if (radius < kBadgeMinRadius)
        radius = kBadgeMinRadius;
    GeoPoint center = at;
    const Gdiplus::Color color = toColor(s.m_color, alphaScale);
    Gdiplus::SolidBrush brush(color);
    bool hollow = false;
    switch (s.m_style.m_variant)
    {
    //方块
    case annot_badge_square:
        g.FillRectangle(&brush, static_cast<Gdiplus::REAL>(center.m_x - radius),
                        static_cast<Gdiplus::REAL>(center.m_y - radius), static_cast<Gdiplus::REAL>(radius * 2),
                        static_cast<Gdiplus::REAL>(radius * 2));
        break;
    //定位针：圆在点上方，下面一个尖角指向点
    case annot_badge_pin:
        {
            center.m_y -= radius * kPinLift;
            Gdiplus::PointF tail[] = {
                Gdiplus::PointF(static_cast<Gdiplus::REAL>(center.m_x - radius * kPinTailHalfWidth),
                                static_cast<Gdiplus::REAL>(center.m_y + radius * kPinTailDrop)),
                Gdiplus::PointF(static_cast<Gdiplus::REAL>(at.m_x), static_cast<Gdiplus::REAL>(at.m_y)),
                Gdiplus::PointF(static_cast<Gdiplus::REAL>(center.m_x + radius * kPinTailHalfWidth),
                                static_cast<Gdiplus::REAL>(center.m_y + radius * kPinTailDrop)) };
            g.FillPolygon(&brush, tail, static_cast<INT>(sizeof(tail) / sizeof(tail[0])));
            g.FillEllipse(&brush, static_cast<Gdiplus::REAL>(center.m_x - radius),
                          static_cast<Gdiplus::REAL>(center.m_y - radius), static_cast<Gdiplus::REAL>(radius * 2),
                          static_cast<Gdiplus::REAL>(radius * 2));
        }
        break;
    //空心圆：白底、彩色描边与文字
    case annot_badge_circle_hollow:
        {
            hollow = true;
            Gdiplus::SolidBrush white(Gdiplus::Color(static_cast<BYTE>(alphaScale), 255, 255, 255));
            g.FillEllipse(&white, static_cast<Gdiplus::REAL>(center.m_x - radius),
                          static_cast<Gdiplus::REAL>(center.m_y - radius), static_cast<Gdiplus::REAL>(radius * 2),
                          static_cast<Gdiplus::REAL>(radius * 2));
            Gdiplus::Pen pen(color, kBadgeOutlinePx);
            g.DrawEllipse(&pen, static_cast<Gdiplus::REAL>(center.m_x - radius),
                          static_cast<Gdiplus::REAL>(center.m_y - radius), static_cast<Gdiplus::REAL>(radius * 2),
                          static_cast<Gdiplus::REAL>(radius * 2));
        }
        break;
    //实心圆（默认）
    default:
        g.FillEllipse(&brush, static_cast<Gdiplus::REAL>(center.m_x - radius),
                      static_cast<Gdiplus::REAL>(center.m_y - radius), static_cast<Gdiplus::REAL>(radius * 2),
                      static_cast<Gdiplus::REAL>(radius * 2));
        break;
    }
    const std::wstring label = AnnotBadgeLabel(s.m_style.m_sequence, s.m_style.m_number);
    if (label.empty())
        return;
    //字多时缩小，免得溢出圆
    double fontPx = radius * kBadgeLabelRatio;
    if (label.size() > 1)
        fontPx = fontPx * 2 / (label.size() + 1);
    Gdiplus::FontFamily family(kTextFace);
    Gdiplus::Font font(&family, static_cast<Gdiplus::REAL>(fontPx), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::StringFormat format;
    format.SetAlignment(Gdiplus::StringAlignmentCenter);
    format.SetLineAlignment(Gdiplus::StringAlignmentCenter);
    Gdiplus::SolidBrush textBrush(hollow ? color : Gdiplus::Color(static_cast<BYTE>(alphaScale), 255, 255, 255));
    const Gdiplus::RectF box(static_cast<Gdiplus::REAL>(center.m_x - radius), static_cast<Gdiplus::REAL>(center.m_y - radius),
                             static_cast<Gdiplus::REAL>(radius * 2), static_cast<Gdiplus::REAL>(radius * 2));
    g.DrawString(label.c_str(), -1, &font, box, &format, &textBrush);
}

//画一段文字（加粗、背景）
void drawText(Gdiplus::Graphics& g, const RECT& content, const AnnotationStroke& s, int alphaScale)
{
    const std::wstring text = utf8ToWide(s.m_text);
    if (text.empty())
        return;
    const GeoPoint at = toGeo(content, s.m_points[0]);
    const int px = AnnotTextPx(content, s.m_width);
    Gdiplus::FontFamily family(kTextFace);
    Gdiplus::Font font(&family, static_cast<Gdiplus::REAL>(px),
                       s.m_style.m_bold ? Gdiplus::FontStyleBold : Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
    const Gdiplus::PointF origin(static_cast<Gdiplus::REAL>(at.m_x), static_cast<Gdiplus::REAL>(at.m_y));
    if (s.m_style.m_textBg == annot_text_bg_white || s.m_style.m_textBg == annot_text_bg_fill)
    {
        Gdiplus::RectF bounds;
        g.MeasureString(text.c_str(), -1, &font, origin, &bounds);
        int pad = px / kTextBgPadDivisor;
        if (pad < kTextBgMinPad)
            pad = kTextBgMinPad;
        const uint32_t bg = (s.m_style.m_textBg == annot_text_bg_white) ? 0xFFFFFF : s.m_style.m_bgColor;
        Gdiplus::SolidBrush bgBrush(toColor(bg, alphaScale));
        g.FillRectangle(&bgBrush, bounds.X - pad, bounds.Y - pad, bounds.Width + 2 * pad, bounds.Height + 2 * pad);
    }
    Gdiplus::SolidBrush brush(toColor(s.m_color, alphaScale));
    g.DrawString(text.c_str(), -1, &font, origin, &brush);
}

//在 origin 处画一行带白色描边的文字（图元名字）；centerX 为真时 origin.X 是文字的水平中心
void drawHaloText(Gdiplus::Graphics& g, const std::wstring& text, const Gdiplus::Font& font, Gdiplus::PointF origin,
                  bool centerX, uint32_t rgb, int alphaScale)
{
    if (text.empty())
        return;
    if (centerX)
    {
        Gdiplus::RectF bounds;
        g.MeasureString(text.c_str(), -1, &font, origin, &bounds);
        origin.X -= bounds.Width / 2;
    }
    Gdiplus::SolidBrush halo(toColor(kWhiteRgb, alphaScale));
    //四个方向各偏一点画白字：左、右、上、下
    const int kHaloDirections = 4;
    const float offsets[kHaloDirections][2] = { { -kLabelHaloPx, 0 }, { kLabelHaloPx, 0 }, { 0, -kLabelHaloPx },
                                                { 0, kLabelHaloPx } };
    for (int i = 0; i < kHaloDirections; ++i)
        g.DrawString(text.c_str(), -1, &font, Gdiplus::PointF(origin.X + offsets[i][0], origin.Y + offsets[i][1]), &halo);
    Gdiplus::SolidBrush brush(toColor(rgb, alphaScale));
    g.DrawString(text.c_str(), -1, &font, origin, &brush);
}

//画图元里的一个简单图形（已换算成像素坐标）
void drawIconPrim(Gdiplus::Graphics& g, const AnnotIconPrim& p, const std::vector<GeoPoint>& pts, Gdiplus::Pen& pen,
                  Gdiplus::Brush* fill)
{
    const bool outline = (p.m_fill != annot_icon_fill_solid);
    switch (p.m_kind)
    {
    //折线：只描边
    case annot_prim_polyline:
        if (pts.size() >= 2)
        {
            const std::vector<Gdiplus::PointF> f = toPointF(pts);
            g.DrawLines(&pen, &f[0], static_cast<INT>(f.size()));
        }
        break;
    //多边形
    case annot_prim_polygon:
        if (pts.size() >= 3)
        {
            const std::vector<Gdiplus::PointF> f = toPointF(pts);
            if (fill != NULL)
                g.FillPolygon(fill, &f[0], static_cast<INT>(f.size()));
            if (outline)
                g.DrawPolygon(&pen, &f[0], static_cast<INT>(f.size()));
        }
        break;
    //椭圆：两点张成
    case annot_prim_ellipse:
        if (pts.size() == 2)
        {
            const float x = static_cast<float>((std::min)(pts[0].m_x, pts[1].m_x));
            const float y = static_cast<float>((std::min)(pts[0].m_y, pts[1].m_y));
            const float w = static_cast<float>(fabs(pts[1].m_x - pts[0].m_x));
            const float h = static_cast<float>(fabs(pts[1].m_y - pts[0].m_y));
            if (fill != NULL)
                g.FillEllipse(fill, x, y, w, h);
            if (outline)
                g.DrawEllipse(&pen, x, y, w, h);
        }
        break;
    //圆角矩形：复用形状轮廓
    case annot_prim_round_rect:
        if (pts.size() == 2)
        {
            const std::vector<GeoPoint> rr = AnnotShapeOutline(annot_tool_round_rect, pts[0], pts[1]);
            if (rr.size() >= 3)
            {
                const std::vector<Gdiplus::PointF> f = toPointF(rr);
                if (fill != NULL)
                    g.FillPolygon(fill, &f[0], static_cast<INT>(f.size()));
                if (outline)
                    g.DrawPolygon(&pen, &f[0], static_cast<INT>(f.size()));
            }
        }
        break;
    //未知图形：不画
    default:
        break;
    }
}

//画一个网络拓扑图元（第 20 期）：矢量图等比放进外接矩形、居中，名字写在下方
void drawIcon(Gdiplus::Graphics& g, const RECT& content, const AnnotationStroke& s, int alphaScale, float widthPx)
{
    if (s.m_points.size() < 2)
        return;
    const GeoPoint a = toGeo(content, s.m_points.front());
    const GeoPoint b = toGeo(content, s.m_points.back());
    const double left = (std::min)(a.m_x, b.m_x);
    const double top = (std::min)(a.m_y, b.m_y);
    const double w = fabs(b.m_x - a.m_x);
    const double h = fabs(b.m_y - a.m_y);
    const double side = (std::min)(w, h);
    if (side <= 0)
        return;
    //单位方框换算到外接矩形里居中的正方形
    const double ox = left + (w - side) / 2;
    const double oy = top + (h - side) / 2;
    const std::vector<AnnotIconPrim> prims = AnnotIconPrimitives(s.m_style.m_variant);
    const Gdiplus::Color line = toColor(s.m_color, alphaScale);
    Gdiplus::SolidBrush tintBrush(toColor(s.m_color, kAnnotIconTintAlpha * alphaScale / kOpaque));
    Gdiplus::SolidBrush solidBrush(line);
    for (size_t i = 0; i < prims.size(); ++i)
    {
        const AnnotIconPrim& p = prims[i];
        std::vector<GeoPoint> pts;
        for (size_t k = 0; k < p.m_points.size(); ++k)
            pts.push_back(GeoPoint(ox + p.m_points[k].m_x * side, oy + p.m_points[k].m_y * side));
        //外框（第一个图形）用整线宽，细节用细一些的线
        const float pw = (i == 0) ? widthPx : (std::max)(1.0f, widthPx * kIconDetailWidthRatio);
        Gdiplus::Pen pen(line, pw);
        pen.SetLineJoin(Gdiplus::LineJoinRound);
        pen.SetStartCap(Gdiplus::LineCapRound);
        pen.SetEndCap(Gdiplus::LineCapRound);
        Gdiplus::Brush* fill = NULL;
        if (p.m_fill == annot_icon_fill_tint)
            fill = &tintBrush;
        else if (p.m_fill == annot_icon_fill_solid)
            fill = &solidBrush;
        drawIconPrim(g, p, pts, pen, fill);
    }
    //名字：在外接矩形下方居中
    const std::wstring name = utf8ToWide(s.m_text);
    if (name.empty())
        return;
    const float px = (std::max)(kIconLabelMinPx, static_cast<float>(side * kIconLabelRatio));
    Gdiplus::FontFamily family(kTextFace);
    Gdiplus::Font font(&family, px, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
    const Gdiplus::PointF origin(static_cast<float>(left + w / 2), static_cast<float>(top + h + kIconLabelGapPx));
    drawHaloText(g, name, font, origin, true, s.m_color, alphaScale);
}

//路径按长度的中点
GeoPoint pathMidpoint(const std::vector<GeoPoint>& path)
{
    double total = 0;
    for (size_t i = 1; i < path.size(); ++i)
        total += hypot(path[i].m_x - path[i - 1].m_x, path[i].m_y - path[i - 1].m_y);
    double walked = 0;
    for (size_t i = 1; i < path.size(); ++i)
    {
        const double seg = hypot(path[i].m_x - path[i - 1].m_x, path[i].m_y - path[i - 1].m_y);
        if (seg > 0 && walked + seg >= total / 2)
        {
            const double t = (total / 2 - walked) / seg;
            return GeoPoint(path[i - 1].m_x + (path[i].m_x - path[i - 1].m_x) * t,
                            path[i - 1].m_y + (path[i].m_y - path[i - 1].m_y) * t);
        }
        walked += seg;
    }
    return path.empty() ? GeoPoint() : path.front();
}

//画一条连接线（第 20 期）：按吸附算出实际的两端，走向、线型与两端样式同箭头；线上文字写在路径中点、白底
void drawConnector(Gdiplus::Graphics& g, const RECT& content, const AnnotationStroke& s,
                   const std::vector<AnnotationStroke>& all, int alphaScale, float widthPx)
{
    const double cw = static_cast<double>(content.right - content.left);
    const double ch = static_cast<double>(content.bottom - content.top);
    if (cw <= 0 || ch <= 0)
        return;
    AnnotationPoint from;
    AnnotationPoint to;
    AnnotConnectorEnds(s, all, cw / ch, from, to);
    const std::vector<GeoPoint> path = AnnotArrowPath(s.m_style.m_routing, toGeo(content, from), toGeo(content, to));
    drawPathWithCaps(g, path, s.m_style, toColor(s.m_color, alphaScale), widthPx);
    const std::wstring label = utf8ToWide(s.m_text);
    if (label.empty() || path.size() < 2)
        return;
    const GeoPoint mid = pathMidpoint(path);
    const float px = (std::max)(kConnectorLabelMinPx, static_cast<float>(ch * kConnectorLabelRatio));
    Gdiplus::FontFamily family(kTextFace);
    Gdiplus::Font font(&family, px, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
    Gdiplus::RectF bounds;
    g.MeasureString(label.c_str(), -1, &font, Gdiplus::PointF(0, 0), &bounds);
    const Gdiplus::PointF origin(static_cast<float>(mid.m_x - bounds.Width / 2),
                                 static_cast<float>(mid.m_y - bounds.Height / 2));
    Gdiplus::SolidBrush bg(toColor(kWhiteRgb, alphaScale));
    g.FillRectangle(&bg, origin.X - kConnectorLabelPadPx, origin.Y - kConnectorLabelPadPx,
                    bounds.Width + 2 * kConnectorLabelPadPx, bounds.Height + 2 * kConnectorLabelPadPx);
    Gdiplus::SolidBrush brush(toColor(s.m_color, alphaScale));
    g.DrawString(label.c_str(), -1, &font, origin, &brush);
}

//把一笔画进图层；all 为全部笔迹（连接线据此找被吸附的对象）
void renderStroke(Gdiplus::Graphics& g, const RECT& content, const AnnotationStroke& s,
                  const std::vector<AnnotationStroke>& all, int64_t nowMs)
{
    if (s.m_points.empty())
        return;
    const int alphaScale = AnnotLaserAlpha(s, nowMs);
    if (alphaScale <= 0)
        return;
    const float widthPx = AnnotStrokeWidthPx(content, s.m_width);
    std::vector<GeoPoint> pts;
    pts.reserve(s.m_points.size());
    for (size_t i = 0; i < s.m_points.size(); ++i)
        pts.push_back(toGeo(content, s.m_points[i]));
    const GeoPoint a = pts[0];
    const GeoPoint b = pts.size() > 1 ? pts[1] : pts[0];
    switch (s.m_tool)
    {
    //文字
    case annot_tool_text:
        drawText(g, content, s, alphaScale);
        break;
    //数字标号
    case annot_tool_badge:
        drawBadge(g, content, s, alphaScale);
        break;
    //箭头：按走向生成路径，两端按样式
    case annot_tool_arrow:
        drawPathWithCaps(g, AnnotArrowPath(s.m_style.m_routing, a, b), s.m_style, toColor(s.m_color, alphaScale),
                         widthPx);
        break;
    //直线、折线：两端按样式
    case annot_tool_line:
    case annot_tool_polyline:
        drawPathWithCaps(g, pts, s.m_style, toColor(s.m_color, alphaScale), widthPx);
        break;
    //曲线：穿过各控制点的平滑曲线
    case annot_tool_curve:
        drawPathWithCaps(g, AnnotSmoothCurve(pts), s.m_style, toColor(s.m_color, alphaScale), widthPx);
        break;
    //块状箭头
    case annot_tool_block_arrow:
        drawOutline(g, AnnotBlockArrowOutline(s.m_style.m_variant, a, b, s.m_style.m_rotation), s.m_style, s.m_color,
                    alphaScale, widthPx);
        break;
    //网络拓扑图元（第 20 期）
    case annot_tool_icon:
        drawIcon(g, content, s, alphaScale, widthPx);
        break;
    //连接线（第 20 期）
    case annot_tool_connector:
        drawConnector(g, content, s, all, alphaScale, widthPx);
        break;
    //各种形状
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
        drawOutline(g, AnnotShapeOutline(s.m_tool, a, b), s.m_style, s.m_color, alphaScale, widthPx);
        break;
    //自由线条
    default:
        drawFreehand(g, s, pts, widthPx, alphaScale);
        break;
    }
}

} // namespace

AnnotationPoint ScreenToAnnot(const RECT& content, POINT pt)
{
    const int w = content.right - content.left;
    const int h = content.bottom - content.top;
    if (w <= 0 || h <= 0)
        return AnnotationPoint();
    int64_t x = pt.x - content.left;
    int64_t y = pt.y - content.top;
    if (x < 0)
        x = 0;
    if (x > w)
        x = w;
    if (y < 0)
        y = 0;
    if (y > h)
        y = h;
    //四舍五入
    const int64_t nx = (x * kAnnotCoordMax + w / 2) / w;
    const int64_t ny = (y * kAnnotCoordMax + h / 2) / h;
    return AnnotationPoint(static_cast<uint16_t>(nx), static_cast<uint16_t>(ny));
}

POINT AnnotToScreen(const RECT& content, const AnnotationPoint& pt)
{
    const int64_t w = content.right - content.left;
    const int64_t h = content.bottom - content.top;
    POINT p;
    p.x = content.left + static_cast<LONG>((pt.m_x * w + kAnnotCoordMax / 2) / kAnnotCoordMax);
    p.y = content.top + static_cast<LONG>((pt.m_y * h + kAnnotCoordMax / 2) / kAnnotCoordMax);
    return p;
}

int AnnotTolerance(const RECT& content, int px)
{
    const int w = content.right - content.left;
    if (w <= 0)
        return 1;
    const int64_t t = static_cast<int64_t>(px) * kAnnotCoordMax / w;
    return t < 1 ? 1 : static_cast<int>(t);
}

int AnnotStrokePx(const RECT& content, int width)
{
    const int w = content.right - content.left;
    const int64_t px = static_cast<int64_t>(width) * w / kAnnotWidthScale;
    return px < 1 ? 1 : static_cast<int>(px);
}

float AnnotStrokeWidthPx(const RECT& content, int width)
{
    const int w = content.right - content.left;
    const float px = static_cast<float>(width) * w / kAnnotWidthScale;
    return px < 1.0f ? 1.0f : px;
}

int AnnotTextPx(const RECT& content, int size)
{
    const int h = content.bottom - content.top;
    const int64_t px = static_cast<int64_t>(size) * h / kAnnotTextScale;
    return px < kAnnotMinTextPx ? kAnnotMinTextPx : static_cast<int>(px);
}

void ComputeArrowHead(POINT from, POINT to, int headLen, POINT& left, POINT& right)
{
    //箭杆方向反过来，左右各转 30 度
    const double angle = atan2(static_cast<double>(from.y - to.y), static_cast<double>(from.x - to.x));
    left.x = to.x + static_cast<LONG>(lround(headLen * cos(angle + kArrowWingAngle)));
    left.y = to.y + static_cast<LONG>(lround(headLen * sin(angle + kArrowWingAngle)));
    right.x = to.x + static_cast<LONG>(lround(headLen * cos(angle - kArrowWingAngle)));
    right.y = to.y + static_cast<LONG>(lround(headLen * sin(angle - kArrowWingAngle)));
}

void RenderAnnotationLayer(int width, int height, const std::vector<AnnotationStroke>& strokes,
                           const AnnotationStroke* pending, int64_t nowMs, bool antialias, std::vector<uint8_t>& bgra)
{
    const int w = width > 0 ? width : 0;
    const int h = height > 0 ? height : 0;
    bgra.assign(static_cast<size_t>(w) * h * kBytesPerPixel, 0);
    if (w == 0 || h == 0)
        return;
    ensureGdiplus();
    Gdiplus::Bitmap bmp(w, h, w * kBytesPerPixel, PixelFormat32bppPARGB, &bgra[0]);
    Gdiplus::Graphics g(&bmp);
    g.SetSmoothingMode(antialias ? Gdiplus::SmoothingModeAntiAlias : Gdiplus::SmoothingModeNone);
    g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    g.SetTextRenderingHint(antialias ? Gdiplus::TextRenderingHintAntiAlias
                                     : Gdiplus::TextRenderingHintSingleBitPerPixelGridFit);
    const RECT content = { 0, 0, w, h };
    for (size_t i = 0; i < strokes.size(); ++i)
        renderStroke(g, content, strokes[i], strokes, nowMs);
    if (pending != NULL)
        renderStroke(g, content, *pending, strokes, nowMs);
}

void AnnotDitherLayer(std::vector<uint8_t>& bgra, int width, int height)
{
    if (width <= 0 || height <= 0 || bgra.size() < static_cast<size_t>(width) * height * kBytesPerPixel)
        return;
    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            uint8_t* p = &bgra[(static_cast<size_t>(y) * width + x) * kBytesPerPixel];
            const int a = p[3];
            if (a == 0 || a == kOpaque)
                continue;
            const int threshold = kBayer[y % kBayerSize][x % kBayerSize] * kBayerStep + kBayerStep / 2;
            if (a > threshold)
            {
                //留下：去掉预乘，换成全不透明
                for (int c = 0; c < 3; ++c)
                {
                    const int v = p[c] * kOpaque / a;
                    p[c] = static_cast<uint8_t>(v > kOpaque ? kOpaque : v);
                }
                p[3] = kOpaque;
            }
            else
            {
                p[0] = 0;
                p[1] = 0;
                p[2] = 0;
                p[3] = 0;
            }
        }
    }
}

namespace {

//把图层合成到 hdc 的 content 矩形上；颜色键窗口先抖动（显示不了半透明）
void blendLayer(HDC hdc, const RECT& content, std::vector<uint8_t>& layer, bool colorKeyTarget)
{
    const int w = content.right - content.left;
    const int h = content.bottom - content.top;
    if (w <= 0 || h <= 0 || layer.size() < static_cast<size_t>(w) * h * kBytesPerPixel)
        return;
    if (colorKeyTarget)
        AnnotDitherLayer(layer, w, h);
    BITMAPINFO bi;
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = kLayerBitsPerPixel;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = NULL;
    HDC mem = ::CreateCompatibleDC(hdc);
    HBITMAP dib = ::CreateDIBSection(hdc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (mem == NULL || dib == NULL || bits == NULL)
    {
        if (dib != NULL)
            ::DeleteObject(dib);
        if (mem != NULL)
            ::DeleteDC(mem);
        return;
    }
    memcpy(bits, &layer[0], static_cast<size_t>(w) * h * kBytesPerPixel);
    HGDIOBJ old = ::SelectObject(mem, dib);
    BLENDFUNCTION blend = { AC_SRC_OVER, 0, kOpaque, AC_SRC_ALPHA };
    ::AlphaBlend(hdc, content.left, content.top, w, h, mem, 0, 0, w, h, blend);
    ::SelectObject(mem, old);
    ::DeleteObject(dib);
    ::DeleteDC(mem);
}

} // namespace

void DrawAnnotations(HDC hdc, const RECT& content, const std::vector<AnnotationStroke>& strokes,
                     const AnnotationStroke* pending, bool colorKeyTarget, int64_t nowMs)
{
    const int w = content.right - content.left;
    const int h = content.bottom - content.top;
    if (w <= 0 || h <= 0 || (strokes.empty() && pending == NULL))
        return;
    std::vector<uint8_t> layer;
    RenderAnnotationLayer(w, h, strokes, pending, nowMs, true, layer);
    blendLayer(hdc, content, layer, colorKeyTarget);
}

void DrawAnnotations(HDC hdc, const RECT& content, const std::vector<AnnotationStroke>& strokes,
                     const AnnotationStroke* pending, bool colorKeyTarget)
{
    DrawAnnotations(hdc, content, strokes, pending, colorKeyTarget, static_cast<int64_t>(::GetTickCount64()));
}

AnnotCursorImage::AnnotCursorImage()
    : m_size(0)
    , m_hotX(0)
    , m_hotY(0)
{
}

namespace {

//光标图像的最小边长（像素）
const int kMinCursorSize = 16;
//光标上的深色墨（笔杆、十字、I 形）与描边白色
const Gdiplus::Color kCursorInk(255, 50, 50, 50);
const Gdiplus::Color kCursorHalo(255, 255, 255, 255);
//橡皮块的颜色（粉）
const Gdiplus::Color kEraserPink(255, 244, 143, 177);
//别人画笔位置的圆点半径、白边宽（像素），名字标签离圆点的偏移、内边距、字号（像素）与底色不透明度
const float kPointerDotRadius = 5.0f;
const float kPointerDotHalo = 1.5f;
const float kPointerLabelOffset = 8.0f;
const float kPointerLabelPadX = 4.0f;
const float kPointerLabelPadY = 2.0f;
const float kPointerLabelFontPx = 12.0f;
const int kPointerLabelAlpha = 230;

//斜放的笔：笔杆从右上到左下，笔尖（当前颜色）在左下角；broad 为粗头笔（荧光笔、马克笔）
void drawPenCursor(Gdiplus::Graphics& g, float s, const Gdiplus::Color& color, bool broad, int& hotX, int& hotY)
{
    const float bodyW = broad ? s * 0.24f : s * 0.16f;
    const Gdiplus::PointF top(s * 0.88f, s * 0.12f);
    const Gdiplus::PointF neck(s * 0.32f, s * 0.68f);
    const Gdiplus::PointF tip(s * 0.1f, s * 0.9f);
    //白边让光标在深色背景上也看得见
    Gdiplus::Pen halo(kCursorHalo, bodyW + 2.0f);
    halo.SetStartCap(Gdiplus::LineCapRound);
    halo.SetEndCap(Gdiplus::LineCapRound);
    g.DrawLine(&halo, top, neck);
    Gdiplus::Pen body(kCursorInk, bodyW);
    body.SetStartCap(Gdiplus::LineCapRound);
    body.SetEndCap(Gdiplus::LineCapFlat);
    g.DrawLine(&body, top, neck);
    //笔尖：从笔杆末端收到尖上的三角
    const float half = bodyW / 2;
    const Gdiplus::PointF nib[] = { Gdiplus::PointF(neck.X - half * 0.7f, neck.Y - half * 0.7f),
                                    Gdiplus::PointF(neck.X + half * 0.7f, neck.Y + half * 0.7f), tip };
    Gdiplus::Pen nibHalo(kCursorHalo, 1.5f);
    g.DrawPolygon(&nibHalo, nib, 3);
    Gdiplus::SolidBrush nibBrush(color);
    g.FillPolygon(&nibBrush, nib, 3);
    hotX = static_cast<int>(tip.X);
    hotY = static_cast<int>(tip.Y);
}

//十字：中心一个当前颜色的小方块
void drawCrossCursor(Gdiplus::Graphics& g, float s, const Gdiplus::Color& color, int& hotX, int& hotY)
{
    const float c = s / 2;
    const float arm = s * 0.4f;
    Gdiplus::Pen halo(kCursorHalo, 3.0f);
    g.DrawLine(&halo, c - arm, c, c + arm, c);
    g.DrawLine(&halo, c, c - arm, c, c + arm);
    Gdiplus::Pen ink(kCursorInk, 1.0f);
    g.DrawLine(&ink, c - arm, c, c + arm, c);
    g.DrawLine(&ink, c, c - arm, c, c + arm);
    Gdiplus::SolidBrush dot(color);
    g.FillRectangle(&dot, c - 2.0f, c - 2.0f, 4.0f, 4.0f);
    hotX = static_cast<int>(c);
    hotY = static_cast<int>(c);
}

} // namespace

AnnotCursorImage RenderToolCursor(int tool, uint32_t color, int sizePx)
{
    AnnotCursorImage img;
    img.m_size = sizePx < kMinCursorSize ? kMinCursorSize : sizePx;
    img.m_bgra.assign(static_cast<size_t>(img.m_size) * img.m_size * kBytesPerPixel, 0);
    ensureGdiplus();
    Gdiplus::Bitmap bmp(img.m_size, img.m_size, img.m_size * kBytesPerPixel, PixelFormat32bppPARGB, &img.m_bgra[0]);
    Gdiplus::Graphics g(&bmp);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    const float s = static_cast<float>(img.m_size);
    const Gdiplus::Color c = toColor(color, kOpaque);
    switch (tool)
    {
    //画笔、铅笔、毛笔：细笔
    case annot_tool_pen:
    case annot_tool_pencil:
    case annot_tool_brush:
        drawPenCursor(g, s, c, false, img.m_hotX, img.m_hotY);
        break;
    //荧光笔、马克笔：粗头笔
    case annot_tool_highlighter:
    case annot_tool_marker:
        drawPenCursor(g, s, c, true, img.m_hotX, img.m_hotY);
        break;
    //激光笔：中间一个发亮的圆点
    case annot_tool_laser:
        {
            const float r = s * 0.18f;
            Gdiplus::SolidBrush glow(toColor(color, kLaserGlowAlpha * 2));
            g.FillEllipse(&glow, s / 2 - r * 1.8f, s / 2 - r * 1.8f, r * 3.6f, r * 3.6f);
            Gdiplus::SolidBrush core(c);
            g.FillEllipse(&core, s / 2 - r, s / 2 - r, r * 2, r * 2);
            img.m_hotX = img.m_size / 2;
            img.m_hotY = img.m_size / 2;
        }
        break;
    //文字：I 形
    case annot_tool_text:
        {
            const float x = s / 2;
            Gdiplus::Pen halo(kCursorHalo, 3.0f);
            Gdiplus::Pen ink(kCursorInk, 1.5f);
            const float t = s * 0.15f;
            const float b = s * 0.85f;
            const float serif = s * 0.12f;
            g.DrawLine(&halo, x, t, x, b);
            g.DrawLine(&ink, x, t, x, b);
            g.DrawLine(&halo, x - serif, t, x + serif, t);
            g.DrawLine(&ink, x - serif, t, x + serif, t);
            g.DrawLine(&halo, x - serif, b, x + serif, b);
            g.DrawLine(&ink, x - serif, b, x + serif, b);
            img.m_hotX = img.m_size / 2;
            img.m_hotY = img.m_size / 2;
        }
        break;
    //整笔橡皮、局部橡皮：斜放的橡皮块，热点在左下
    case annot_tool_eraser_area:
    case kAnnotEraser:
        {
            const Gdiplus::PointF block[] = { Gdiplus::PointF(s * 0.15f, s * 0.7f), Gdiplus::PointF(s * 0.55f, s * 0.3f),
                                              Gdiplus::PointF(s * 0.85f, s * 0.6f), Gdiplus::PointF(s * 0.45f, s * 1.0f - 1) };
            Gdiplus::SolidBrush pink(kEraserPink);
            g.FillPolygon(&pink, block, 4);
            Gdiplus::Pen ink(kCursorInk, 1.5f);
            g.DrawPolygon(&ink, block, 4);
            img.m_hotX = static_cast<int>(s * 0.2f);
            img.m_hotY = static_cast<int>(s * 0.75f);
        }
        break;
    //数字标号：当前颜色的小圆
    case annot_tool_badge:
        {
            const float r = s * 0.3f;
            Gdiplus::SolidBrush halo(kCursorHalo);
            g.FillEllipse(&halo, s / 2 - r - 1, s / 2 - r - 1, r * 2 + 2, r * 2 + 2);
            Gdiplus::SolidBrush fill(c);
            g.FillEllipse(&fill, s / 2 - r, s / 2 - r, r * 2, r * 2);
            img.m_hotX = img.m_size / 2;
            img.m_hotY = img.m_size / 2;
        }
        break;
    //直线、折线、曲线、箭头、形状、块状箭头与其余：十字
    default:
        drawCrossCursor(g, s, c, img.m_hotX, img.m_hotY);
        break;
    }
    return img;
}

HCURSOR CreateAnnotationCursor(int tool, uint32_t color)
{
    int size = ::GetSystemMetrics(SM_CXCURSOR);
    const AnnotCursorImage img = RenderToolCursor(tool, color, size);
    size = img.m_size;
    BITMAPV5HEADER bi;
    memset(&bi, 0, sizeof(bi));
    bi.bV5Size = sizeof(bi);
    bi.bV5Width = size;
    bi.bV5Height = -size;
    bi.bV5Planes = 1;
    bi.bV5BitCount = kLayerBitsPerPixel;
    bi.bV5Compression = BI_RGB;
    void* bits = NULL;
    HDC screen = ::GetDC(NULL);
    HBITMAP color32 = ::CreateDIBSection(screen, reinterpret_cast<BITMAPINFO*>(&bi), DIB_RGB_COLORS, &bits, NULL, 0);
    ::ReleaseDC(NULL, screen);
    //单色掩码全为 0：透明度完全由 32 位彩色位图的 alpha 决定
    HBITMAP mask = ::CreateBitmap(size, size, 1, 1, NULL);
    HCURSOR cursor = NULL;
    if (color32 != NULL && bits != NULL && mask != NULL && !img.m_bgra.empty())
    {
        memcpy(bits, &img.m_bgra[0], img.m_bgra.size());
        ICONINFO ii;
        memset(&ii, 0, sizeof(ii));
        ii.fIcon = FALSE;
        ii.xHotspot = static_cast<DWORD>(img.m_hotX);
        ii.yHotspot = static_cast<DWORD>(img.m_hotY);
        ii.hbmMask = mask;
        ii.hbmColor = color32;
        cursor = ::CreateIconIndirect(&ii);
    }
    if (color32 != NULL)
        ::DeleteObject(color32);
    if (mask != NULL)
        ::DeleteObject(mask);
    return cursor;
}

namespace {

//把别人的画笔位置与名字画进图层（第 19 期）；content 为整个画面在图层里的矩形（第 23 期起可以比图层大、左上为负）
void renderPointersIn(Gdiplus::Graphics& g, const RECT& content, const std::vector<AnnotationPointer>& pointers,
                      const std::vector<std::wstring>& names)
{
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAlias);
    Gdiplus::FontFamily family(kTextFace);
    Gdiplus::Font font(&family, kPointerLabelFontPx, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
    for (size_t i = 0; i < pointers.size(); ++i)
    {
        const AnnotationPointer& p = pointers[i];
        const GeoPoint at = toGeo(content, p.m_pt);
        const float x = static_cast<float>(at.m_x);
        const float y = static_cast<float>(at.m_y);
        //圆点：白边加当前颜色
        Gdiplus::SolidBrush halo(kCursorHalo);
        const float hr = kPointerDotRadius + kPointerDotHalo;
        g.FillEllipse(&halo, x - hr, y - hr, hr * 2, hr * 2);
        Gdiplus::SolidBrush dot(toColor(p.m_color, kOpaque));
        g.FillEllipse(&dot, x - kPointerDotRadius, y - kPointerDotRadius, kPointerDotRadius * 2, kPointerDotRadius * 2);
        //名字标签：同色底、白字，在圆点右下方
        if (i >= names.size() || names[i].empty())
            continue;
        const Gdiplus::PointF origin(x + kPointerLabelOffset + kPointerLabelPadX, y + kPointerLabelOffset + kPointerLabelPadY);
        Gdiplus::RectF bounds;
        g.MeasureString(names[i].c_str(), -1, &font, origin, &bounds);
        Gdiplus::SolidBrush bg(toColor(p.m_color, kPointerLabelAlpha));
        g.FillRectangle(&bg, bounds.X - kPointerLabelPadX, bounds.Y - kPointerLabelPadY,
                        bounds.Width + kPointerLabelPadX * 2, bounds.Height + kPointerLabelPadY * 2);
        Gdiplus::SolidBrush white(kCursorHalo);
        g.DrawString(names[i].c_str(), -1, &font, origin, &white);
    }
}

} // namespace

void RenderAnnotationPointers(int width, int height, const std::vector<AnnotationPointer>& pointers,
                              const std::vector<std::wstring>& names, std::vector<uint8_t>& bgra)
{
    if (width <= 0 || height <= 0 || pointers.empty() ||
        bgra.size() < static_cast<size_t>(width) * height * kBytesPerPixel)
        return;
    ensureGdiplus();
    Gdiplus::Bitmap bmp(width, height, width * kBytesPerPixel, PixelFormat32bppPARGB, &bgra[0]);
    Gdiplus::Graphics g(&bmp);
    const RECT content = { 0, 0, width, height };
    renderPointersIn(g, content, pointers, names);
}

void DrawAnnotationsWithPointers(HDC hdc, const RECT& content, const std::vector<AnnotationStroke>& strokes,
                                 const AnnotationStroke* pending, bool colorKeyTarget, int64_t nowMs,
                                 const std::vector<AnnotationPointer>& pointers, const std::vector<std::wstring>& names)
{
    const int w = content.right - content.left;
    const int h = content.bottom - content.top;
    if (w <= 0 || h <= 0 || (strokes.empty() && pending == NULL && pointers.empty()))
        return;
    std::vector<uint8_t> layer;
    RenderAnnotationLayer(w, h, strokes, pending, nowMs, true, layer);
    RenderAnnotationPointers(w, h, pointers, names, layer);
    blendLayer(hdc, content, layer, colorKeyTarget);
}

namespace {

//归一化矩形换成图层上的像素矩形（左上、宽高）
Gdiplus::RectF toPixelRect(const RECT& content, const AnnotRect& r)
{
    const GeoPoint a = toGeo(content, AnnotationPoint(static_cast<uint16_t>(r.m_left), static_cast<uint16_t>(r.m_top)));
    const GeoPoint b = toGeo(content, AnnotationPoint(static_cast<uint16_t>(r.m_right), static_cast<uint16_t>(r.m_bottom)));
    return Gdiplus::RectF(static_cast<float>(a.m_x), static_cast<float>(a.m_y), static_cast<float>(b.m_x - a.m_x),
                          static_cast<float>(b.m_y - a.m_y));
}

} // namespace

namespace {

//把选择提示画到 g 上；content 为共享内容（或白板的世界）在 g 上的矩形
void drawSelection(Gdiplus::Graphics& g, const RECT& content, const AnnotSelectionVisual& v)
{
    const Gdiplus::Color blue = toColor(kSelectColor, kOpaque);
    //选中框：外接矩形外扩几个像素的虚线框
    Gdiplus::Pen dashed(blue, kSelectLinePx);
    dashed.SetDashStyle(Gdiplus::DashStyleDash);
    for (size_t i = 0; i < v.m_boxes.size(); ++i)
    {
        const Gdiplus::RectF r = toPixelRect(content, v.m_boxes[i]);
        g.DrawRectangle(&dashed, r.X - kSelectPadPx, r.Y - kSelectPadPx, r.Width + 2 * kSelectPadPx,
                        r.Height + 2 * kSelectPadPx);
    }
    //句柄：外接矩形的为白底蓝边小方块，直线类两端的为小圆
    Gdiplus::SolidBrush white(toColor(kWhiteRgb, kOpaque));
    Gdiplus::Pen border(blue, kHandleBorderPx);
    const float half = kHandleSizePx / 2;
    for (size_t i = 0; i < v.m_handles.size(); ++i)
    {
        const GeoPoint c = toGeo(content, v.m_handles[i]);
        const float x = static_cast<float>(c.m_x) - half;
        const float y = static_cast<float>(c.m_y) - half;
        if (v.m_endpointHandles)
        {
            g.FillEllipse(&white, x, y, kHandleSizePx, kHandleSizePx);
            g.DrawEllipse(&border, x, y, kHandleSizePx, kHandleSizePx);
        }
        else
        {
            g.FillRectangle(&white, x, y, kHandleSizePx, kHandleSizePx);
            g.DrawRectangle(&border, x, y, kHandleSizePx, kHandleSizePx);
        }
    }
    //选择框：浅蓝半透明填充加蓝边
    if (v.m_hasBand && !v.m_band.IsEmpty())
    {
        const Gdiplus::RectF r = toPixelRect(content, v.m_band);
        Gdiplus::SolidBrush fill(toColor(kSelectColor, kBandFillAlpha));
        g.FillRectangle(&fill, r);
        Gdiplus::Pen pen(blue, kBandLinePx);
        g.DrawRectangle(&pen, r);
    }
}

//是否有要画的选择提示
bool hasSelectionVisual(const AnnotSelectionVisual& v)
{
    return !v.m_boxes.empty() || !v.m_handles.empty() || v.m_hasBand;
}

} // namespace

void RenderAnnotationSelection(int width, int height, const AnnotSelectionVisual& v, std::vector<uint8_t>& bgra)
{
    if (width <= 0 || height <= 0 || bgra.size() < static_cast<size_t>(width) * height * kBytesPerPixel)
        return;
    if (!hasSelectionVisual(v))
        return;
    ensureGdiplus();
    Gdiplus::Bitmap bmp(width, height, width * kBytesPerPixel, PixelFormat32bppPARGB, &bgra[0]);
    Gdiplus::Graphics g(&bmp);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    const RECT content = { 0, 0, width, height };
    drawSelection(g, content, v);
}

void RenderAnnotationView(int viewW, int viewH, const RECT& content, const std::vector<AnnotationStroke>& strokes,
                          const AnnotationStroke* pending, int64_t nowMs, bool antialias,
                          const AnnotSelectionVisual* selection, std::vector<uint8_t>& bgra)
{
    const int w = viewW > 0 ? viewW : 0;
    const int h = viewH > 0 ? viewH : 0;
    bgra.assign(static_cast<size_t>(w) * h * kBytesPerPixel, 0);
    if (w == 0 || h == 0 || content.right <= content.left || content.bottom <= content.top)
        return;
    ensureGdiplus();
    //图层只有可见区域那么大；笔迹按 content（可以比图层大、左上为负）换算，超出图层的部分由 GDI+ 裁掉
    Gdiplus::Bitmap bmp(w, h, w * kBytesPerPixel, PixelFormat32bppPARGB, &bgra[0]);
    Gdiplus::Graphics g(&bmp);
    g.SetSmoothingMode(antialias ? Gdiplus::SmoothingModeAntiAlias : Gdiplus::SmoothingModeNone);
    g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    g.SetTextRenderingHint(antialias ? Gdiplus::TextRenderingHintAntiAlias
                                     : Gdiplus::TextRenderingHintSingleBitPerPixelGridFit);
    for (size_t i = 0; i < strokes.size(); ++i)
        renderStroke(g, content, strokes[i], strokes, nowMs);
    if (pending != NULL)
        renderStroke(g, content, *pending, strokes, nowMs);
    if (selection != NULL && hasSelectionVisual(*selection))
    {
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        drawSelection(g, content, *selection);
    }
}

void DrawAnnotationsInView(HDC hdc, const RECT& view, const RECT& content, const std::vector<AnnotationStroke>& strokes,
                           const AnnotationStroke* pending, int64_t nowMs, const AnnotSelectionVisual* selection)
{
    const int w = view.right - view.left;
    const int h = view.bottom - view.top;
    const bool hasSelection = (selection != NULL && hasSelectionVisual(*selection));
    if (w <= 0 || h <= 0 || (strokes.empty() && pending == NULL && !hasSelection))
        return;
    //content 换成相对可见区域左上角的坐标
    RECT local = content;
    ::OffsetRect(&local, -view.left, -view.top);
    std::vector<uint8_t> layer;
    RenderAnnotationView(w, h, local, strokes, pending, nowMs, true, selection, layer);
    blendLayer(hdc, view, layer, false);
}

void DrawAnnotationsWithSelection(HDC hdc, const RECT& content, const std::vector<AnnotationStroke>& strokes,
                                  const AnnotationStroke* pending, bool colorKeyTarget, int64_t nowMs,
                                  const std::vector<AnnotationPointer>& pointers, const std::vector<std::wstring>& names,
                                  const AnnotSelectionVisual* selection)
{
    const int w = content.right - content.left;
    const int h = content.bottom - content.top;
    const bool hasSelection = (selection != NULL && (!selection->m_boxes.empty() || selection->m_hasBand));
    if (w <= 0 || h <= 0 || (strokes.empty() && pending == NULL && pointers.empty() && !hasSelection))
        return;
    std::vector<uint8_t> layer;
    RenderAnnotationLayer(w, h, strokes, pending, nowMs, true, layer);
    RenderAnnotationPointers(w, h, pointers, names, layer);
    if (selection != NULL)
        RenderAnnotationSelection(w, h, *selection, layer);
    blendLayer(hdc, content, layer, colorKeyTarget);
}

void DrawSessionCanvas(HDC hdc, const RECT& content, const ShareAnnotationSession& session, int canvas,
                       bool colorKeyTarget, int64_t nowMs)
{
    const std::vector<AnnotationStroke> strokes = AnnotStrokesOnCanvas(session.Model().Strokes(), canvas);
    //别人的画笔位置与名字画在笔迹上面（第 19 期），只画光标在这块画面上的
    const std::vector<AnnotationPointer> pointers = AnnotPointersOnCanvas(session.Model().Pointers(), canvas);
    std::vector<std::wstring> names;
    for (size_t i = 0; i < pointers.size(); ++i)
        names.push_back(session.SeatName(pointers[i].m_seat));
    //本人正在画的预览与选中框只属于本人当前画面
    const bool mine = (session.Canvas() == canvas);
    AnnotationStroke pending;
    const bool hasPending = mine && session.PendingShape(pending);
    AnnotSelectionVisual selection;
    if (mine)
        selection = session.SelectionVisual();
    DrawAnnotationsWithSelection(hdc, content, strokes, hasPending ? &pending : NULL, colorKeyTarget, nowMs, pointers,
                                 names, &selection);
}

void DrawSessionCanvasInView(HDC hdc, const RECT& view, const RECT& content, const ShareAnnotationSession& session,
                             int canvas, int64_t nowMs)
{
    const int w = view.right - view.left;
    const int h = view.bottom - view.top;
    if (w <= 0 || h <= 0 || content.right <= content.left || content.bottom <= content.top)
        return;
    const std::vector<AnnotationStroke> strokes = AnnotStrokesOnCanvas(session.Model().Strokes(), canvas);
    const std::vector<AnnotationPointer> pointers = AnnotPointersOnCanvas(session.Model().Pointers(), canvas);
    //本人正在画的预览与选中框只属于本人当前画面
    const bool mine = (session.Canvas() == canvas);
    AnnotationStroke pending;
    const bool hasPending = mine && session.PendingShape(pending);
    AnnotSelectionVisual selection;
    if (mine)
        selection = session.SelectionVisual();
    const bool hasSelection = hasSelectionVisual(selection);
    if (strokes.empty() && !hasPending && !hasSelection && pointers.empty())
        return;
    //图层只有可见区域那么大，content 换成相对可见区域左上角的坐标
    RECT local = content;
    ::OffsetRect(&local, -view.left, -view.top);
    std::vector<uint8_t> layer;
    RenderAnnotationView(w, h, local, strokes, hasPending ? &pending : NULL, nowMs, true, hasSelection ? &selection : NULL,
                         layer);
    //别人的画笔位置与名字画在笔迹上面（第 19 期）
    if (!pointers.empty() && !layer.empty())
    {
        std::vector<std::wstring> names;
        for (size_t i = 0; i < pointers.size(); ++i)
            names.push_back(session.SeatName(pointers[i].m_seat));
        Gdiplus::Bitmap bmp(w, h, w * kBytesPerPixel, PixelFormat32bppPARGB, &layer[0]);
        Gdiplus::Graphics g(&bmp);
        renderPointersIn(g, local, pointers, names);
    }
    blendLayer(hdc, view, layer, false);
}

} // namespace screenshare
