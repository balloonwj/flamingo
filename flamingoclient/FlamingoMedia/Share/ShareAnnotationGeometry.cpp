/**
 *  屏幕共享画板的几何计算，见 ShareAnnotationGeometry.h。
 *  balloonwj@qq.com   2026-10-05
 */
#include "ShareAnnotationGeometry.h"

#include <math.h>
#include <stdio.h>                 // _snwprintf_s：标号的阿拉伯数字
#include "ShareAnnotation.h"

namespace screenshare {

namespace {

const double kPi = 3.14159265358979323846;
//角度转弧度的系数
const double kDegToRad = kPi / 180.0;
//圆角矩形的圆角半径：短边的六分之一；每个圆角的采样段数
const double kRoundRectRadiusRatio = 1.0 / 6.0;
const int kRoundCornerSteps = 6;
//五角星内角顶点与外角顶点到中心的距离之比
const double kStarInnerRatio = 0.382;
//平行四边形上下两边的错开量：宽的四分之一
const double kParallelogramShift = 0.25;
//对话气泡：气泡主体占高度的比例，尖角根部的左右位置与尖角顶点的横向位置（占宽度的比例）
const double kBubbleBodyRatio = 0.78;
const double kBubbleTailLeft = 0.18;
const double kBubbleTailRight = 0.36;
const double kBubbleTailTip = 0.10;
//云形：轮廓半径随角度起伏的次数与幅度
const int kCloudBumps = 9;
const double kCloudBumpDepth = 0.14;
//弧形箭头的拱高：线段长的四分之一
const double kArcBulgeRatio = 0.25;
//两端样式的比例（以 size 为单位）：三角与燕尾的半宽、燕尾凹口深度、菱形半宽
const double kCapHalfWidth = 0.5;
const double kSwallowNotch = 0.6;
const double kDiamondHalfWidth = 0.35;
//毛笔线宽的范围（基准线宽的倍数）与平滑系数（新值所占的比例）
const double kBrushMinRatio = 0.6;
const double kBrushMaxRatio = 1.4;
const double kBrushSmooth = 0.35;
//毛笔：相邻点距离达到「基准线宽 × 该倍数」时线宽降到最细
const double kBrushFastDistance = 3.0;

//把单位方框 [0,1]×[0,1] 里的点映射到外接矩形
GeoPoint mapUnit(double ux, double uy, double l, double t, double w, double h)
{
    return GeoPoint(l + ux * w, t + uy * h);
}

//把一组点的外接矩形拉伸到目标矩形（规则多边形、云形用：先在单位圆上算，再整体贴满外接矩形）
std::vector<GeoPoint> fitToBox(const std::vector<GeoPoint>& pts, double l, double t, double w, double h)
{
    double minX = 1e300;
    double minY = 1e300;
    double maxX = -1e300;
    double maxY = -1e300;
    for (size_t i = 0; i < pts.size(); ++i)
    {
        minX = pts[i].m_x < minX ? pts[i].m_x : minX;
        minY = pts[i].m_y < minY ? pts[i].m_y : minY;
        maxX = pts[i].m_x > maxX ? pts[i].m_x : maxX;
        maxY = pts[i].m_y > maxY ? pts[i].m_y : maxY;
    }
    const double sw = maxX - minX;
    const double sh = maxY - minY;
    std::vector<GeoPoint> out;
    out.reserve(pts.size());
    for (size_t i = 0; i < pts.size(); ++i)
    {
        const double ux = sw > 0 ? (pts[i].m_x - minX) / sw : 0.5;
        const double uy = sh > 0 ? (pts[i].m_y - minY) / sh : 0.5;
        out.push_back(mapUnit(ux, uy, l, t, w, h));
    }
    return out;
}

//正多边形或星形的顶点（单位圆上，第一个顶点朝上）
std::vector<GeoPoint> regularPolygon(int vertices, double startDeg, double innerRatio)
{
    std::vector<GeoPoint> pts;
    const int count = innerRatio > 0 ? vertices * 2 : vertices;
    for (int i = 0; i < count; ++i)
    {
        const double r = (innerRatio > 0 && (i % 2) == 1) ? innerRatio : 1.0;
        const double deg = startDeg + 360.0 * i / count;
        pts.push_back(GeoPoint(r * cos(deg * kDegToRad), r * sin(deg * kDegToRad)));
    }
    return pts;
}

//往 out 里加一段圆角（四分之一圆），圆心 (cx, cy)，从 startDeg 转到 startDeg + 90
void appendCorner(std::vector<GeoPoint>& out, double cx, double cy, double rx, double ry, double startDeg)
{
    for (int i = 0; i <= kRoundCornerSteps; ++i)
    {
        const double deg = startDeg + 90.0 * i / kRoundCornerSteps;
        out.push_back(GeoPoint(cx + rx * cos(deg * kDegToRad), cy + ry * sin(deg * kDegToRad)));
    }
}

//圆角矩形的轮廓（顺时针，从左上圆角开始）
std::vector<GeoPoint> roundRect(double l, double t, double r, double b)
{
    const double w = r - l;
    const double h = b - t;
    const double radius = (w < h ? w : h) * kRoundRectRadiusRatio;
    std::vector<GeoPoint> out;
    appendCorner(out, l + radius, t + radius, radius, radius, 180);
    appendCorner(out, r - radius, t + radius, radius, radius, 270);
    appendCorner(out, r - radius, b - radius, radius, radius, 0);
    appendCorner(out, l + radius, b - radius, radius, radius, 90);
    return out;
}

//点绕中心顺时针转 deg 度（屏幕坐标 y 向下，角度为正时看上去是顺时针）
GeoPoint rotateAround(GeoPoint p, GeoPoint c, double deg)
{
    const double rad = deg * kDegToRad;
    const double dx = p.m_x - c.m_x;
    const double dy = p.m_y - c.m_y;
    return GeoPoint(c.m_x + dx * cos(rad) - dy * sin(rad), c.m_y + dx * sin(rad) + dy * cos(rad));
}

//把单位方框里的一串点映射到外接矩形
std::vector<GeoPoint> mapUnitList(const double (*unit)[2], size_t count, double l, double t, double w, double h)
{
    std::vector<GeoPoint> out;
    out.reserve(count);
    for (size_t i = 0; i < count; ++i)
        out.push_back(mapUnit(unit[i][0], unit[i][1], l, t, w, h));
    return out;
}

//U 形掉头箭头在单位方框里的轮廓：左腿向上，顶上拱过去，右腿向下，箭头朝下
std::vector<GeoPoint> uTurnUnit()
{
    //外拱、内拱的圆心与半径，右腿与箭头的位置（单位方框坐标）
    const double cx = 0.45;
    const double cy = 0.4;
    const double outerRx = 0.45;
    const double outerRy = 0.4;
    const double innerR = 0.15;
    const int steps = 12;
    std::vector<GeoPoint> pts;
    pts.push_back(GeoPoint(0, 1));
    pts.push_back(GeoPoint(0, cy));
    for (int i = 1; i <= steps; ++i)
    {
        const double deg = 180.0 + 180.0 * i / steps;
        pts.push_back(GeoPoint(cx + outerRx * cos(deg * kDegToRad), cy + outerRy * sin(deg * kDegToRad)));
    }
    pts.push_back(GeoPoint(cx + outerRx, 0.6));
    pts.push_back(GeoPoint(1.0, 0.6));
    pts.push_back(GeoPoint(0.75, 1.0));
    pts.push_back(GeoPoint(0.5, 0.6));
    pts.push_back(GeoPoint(cx + innerR, 0.6));
    for (int i = 0; i <= steps; ++i)
    {
        const double deg = 360.0 - 180.0 * i / steps;
        pts.push_back(GeoPoint(cx + innerR * cos(deg * kDegToRad), cy + innerR * sin(deg * kDegToRad)));
    }
    pts.push_back(GeoPoint(cx - innerR, 1));
    return pts;
}

//环形循环箭头在单位方框里的轮廓：大半个圆环，末端一个顺着圆周的箭头
std::vector<GeoPoint> circularUnit()
{
    const double cx = 0.5;
    const double cy = 0.5;
    const double outer = 0.42;
    const double inner = 0.26;
    const double headOuter = 0.5;
    const double headInner = 0.18;
    const double startDeg = 30;
    const double endDeg = 300;
    const double tipDeg = 340;
    const int steps = 30;
    std::vector<GeoPoint> pts;
    for (int i = 0; i <= steps; ++i)
    {
        const double deg = startDeg + (endDeg - startDeg) * i / steps;
        pts.push_back(GeoPoint(cx + outer * cos(deg * kDegToRad), cy + outer * sin(deg * kDegToRad)));
    }
    pts.push_back(GeoPoint(cx + headOuter * cos(endDeg * kDegToRad), cy + headOuter * sin(endDeg * kDegToRad)));
    const double mid = (outer + inner) / 2;
    pts.push_back(GeoPoint(cx + mid * cos(tipDeg * kDegToRad), cy + mid * sin(tipDeg * kDegToRad)));
    pts.push_back(GeoPoint(cx + headInner * cos(endDeg * kDegToRad), cy + headInner * sin(endDeg * kDegToRad)));
    for (int i = steps; i >= 0; --i)
    {
        const double deg = startDeg + (endDeg - startDeg) * i / steps;
        pts.push_back(GeoPoint(cx + inner * cos(deg * kDegToRad), cy + inner * sin(deg * kDegToRad)));
    }
    return pts;
}

//中文数字（1～9999）；超出返回空
std::wstring chineseNumber(int n)
{
    //数字与位名：一二三……九，十百千
    static const wchar_t* const kDigits[] = { L"零", L"一", L"二", L"三", L"四", L"五", L"六", L"七", L"八", L"九" };   //i18n-ignore: 中文数字序号是标号的内容，不是界面文字
    static const wchar_t* const kUnits[] = { L"", L"十", L"百", L"千" };   //i18n-ignore: 中文数字的位名，同上
    const int kMaxChinese = 9999;
    const int kTen = 10;
    if (n <= 0 || n > kMaxChinese)
        return std::wstring();
    int digits[4] = { 0, 0, 0, 0 };
    int count = 0;
    for (int v = n; v > 0; v /= kTen)
        digits[count++] = v % kTen;
    std::wstring out;
    bool pendingZero = false;
    for (int i = count - 1; i >= 0; --i)
    {
        const int d = digits[i];
        if (d == 0)
        {
            pendingZero = !out.empty();
            continue;
        }
        if (pendingZero)
        {
            out += kDigits[0];
            pendingZero = false;
        }
        //十几：「十一」而不是「一十一」（只在最高位是十位时省掉「一」）
        if (!(i == 1 && d == 1 && i == count - 1))
            out += kDigits[d];
        out += kUnits[i];
    }
    return out;
}

} // namespace

GeoPoint::GeoPoint()
    : m_x(0)
    , m_y(0)
{
}

GeoPoint::GeoPoint(double x, double y)
    : m_x(x)
    , m_y(y)
{
}

AnnotCapShape::AnnotCapShape()
    : m_closed(false)
    , m_filled(false)
    , m_inset(0)
{
}

std::vector<GeoPoint> AnnotShapeOutline(int tool, GeoPoint a, GeoPoint b)
{
    const double l = a.m_x < b.m_x ? a.m_x : b.m_x;
    const double r = a.m_x > b.m_x ? a.m_x : b.m_x;
    const double t = a.m_y < b.m_y ? a.m_y : b.m_y;
    const double bt = a.m_y > b.m_y ? a.m_y : b.m_y;
    const double w = r - l;
    const double h = bt - t;
    std::vector<GeoPoint> out;
    if (w <= 0 || h <= 0)
        return out;
    switch (tool)
    {
    //矩形：四个角
    case annot_tool_rect:
        out.push_back(GeoPoint(l, t));
        out.push_back(GeoPoint(r, t));
        out.push_back(GeoPoint(r, bt));
        out.push_back(GeoPoint(l, bt));
        break;
    //圆角矩形
    case annot_tool_round_rect:
        out = roundRect(l, t, r, bt);
        break;
    //椭圆：沿周长等角度取点
    case annot_tool_ellipse:
        for (int i = 0; i < kAnnotEllipseSegments; ++i)
        {
            const double deg = 360.0 * i / kAnnotEllipseSegments;
            out.push_back(GeoPoint(l + w / 2 + w / 2 * cos(deg * kDegToRad), t + h / 2 + h / 2 * sin(deg * kDegToRad)));
        }
        break;
    //三角形：顶点在上边中点
    case annot_tool_triangle:
        out.push_back(GeoPoint(l + w / 2, t));
        out.push_back(GeoPoint(r, bt));
        out.push_back(GeoPoint(l, bt));
        break;
    //菱形：四边中点
    case annot_tool_diamond:
        out.push_back(GeoPoint(l + w / 2, t));
        out.push_back(GeoPoint(r, t + h / 2));
        out.push_back(GeoPoint(l + w / 2, bt));
        out.push_back(GeoPoint(l, t + h / 2));
        break;
    //五边形：顶点朝上的正五边形贴满外接矩形
    case annot_tool_pentagon:
        out = fitToBox(regularPolygon(5, -90, 0), l, t, w, h);
        break;
    //六边形：上下两边水平的正六边形贴满外接矩形
    case annot_tool_hexagon:
        out = fitToBox(regularPolygon(6, 0, 0), l, t, w, h);
        break;
    //五角星：外角朝上，内外顶点交替
    case annot_tool_star:
        out = fitToBox(regularPolygon(5, -90, kStarInnerRatio), l, t, w, h);
        break;
    //平行四边形：上边右移、下边左移
    case annot_tool_parallelogram:
        out.push_back(GeoPoint(l + w * kParallelogramShift, t));
        out.push_back(GeoPoint(r, t));
        out.push_back(GeoPoint(r - w * kParallelogramShift, bt));
        out.push_back(GeoPoint(l, bt));
        break;
    //对话气泡：圆角矩形主体，左下方一个尖角
    case annot_tool_bubble:
        {
            const double bodyBottom = t + h * kBubbleBodyRatio;
            std::vector<GeoPoint> body = roundRect(l, t, r, bodyBottom);
            //圆角矩形最后一段是左下圆角（从下边到左边）；尖角插在右下圆角与左下圆角之间的下边上
            const size_t cornerPoints = static_cast<size_t>(kRoundCornerSteps + 1);
            out.assign(body.begin(), body.begin() + 3 * cornerPoints);
            out.push_back(GeoPoint(l + w * kBubbleTailRight, bodyBottom));
            out.push_back(GeoPoint(l + w * kBubbleTailTip, bt));
            out.push_back(GeoPoint(l + w * kBubbleTailLeft, bodyBottom));
            out.insert(out.end(), body.begin() + 3 * cornerPoints, body.end());
        }
        break;
    //云形：半径随角度起伏的闭合曲线，贴满外接矩形
    case annot_tool_cloud:
        {
            std::vector<GeoPoint> unit;
            for (int i = 0; i < kAnnotEllipseSegments; ++i)
            {
                const double rad = 2 * kPi * i / kAnnotEllipseSegments;
                const double radius = 1.0 - kCloudBumpDepth + kCloudBumpDepth * fabs(sin(kCloudBumps * rad / 2));
                unit.push_back(GeoPoint(radius * cos(rad), radius * sin(rad)));
            }
            out = fitToBox(unit, l, t, w, h);
        }
        break;
    //不是形状：没有轮廓
    default:
        break;
    }
    return out;
}

GeoPoint AnnotRegularCorner(GeoPoint a, GeoPoint b)
{
    const double dx = b.m_x - a.m_x;
    const double dy = b.m_y - a.m_y;
    const double side = fabs(dx) > fabs(dy) ? fabs(dx) : fabs(dy);
    return GeoPoint(a.m_x + (dx < 0 ? -side : side), a.m_y + (dy < 0 ? -side : side));
}

double AnnotRegularShapeAspect(int tool)
{
    std::vector<GeoPoint> pts;
    switch (tool)
    {
    //正三角形：顶点在上边中点，高为边长的 √3 / 2
    case annot_tool_triangle:
        return 2.0 / sqrt(3.0);
    //五边形：与 AnnotShapeOutline 相同朝向的正五边形
    case annot_tool_pentagon:
        pts = regularPolygon(5, -90, 0);
        break;
    //六边形：上下两边水平的正六边形
    case annot_tool_hexagon:
        pts = regularPolygon(6, 0, 0);
        break;
    //五角星：外接框由外角顶点决定，与正五边形相同
    case annot_tool_star:
        pts = regularPolygon(5, -90, kStarInnerRatio);
        break;
    //其余形状：正方形外接框
    default:
        return 1.0;
    }
    double minX = 1e300;
    double minY = 1e300;
    double maxX = -1e300;
    double maxY = -1e300;
    for (size_t i = 0; i < pts.size(); ++i)
    {
        minX = pts[i].m_x < minX ? pts[i].m_x : minX;
        minY = pts[i].m_y < minY ? pts[i].m_y : minY;
        maxX = pts[i].m_x > maxX ? pts[i].m_x : maxX;
        maxY = pts[i].m_y > maxY ? pts[i].m_y : maxY;
    }
    return (maxY > minY) ? (maxX - minX) / (maxY - minY) : 1.0;
}

GeoPoint AnnotConstrainDrag(int tool, GeoPoint from, GeoPoint to, double aspect, double coordMax)
{
    const bool lineLike = (tool == annot_tool_line || tool == annot_tool_arrow || tool == annot_tool_connector);
    const bool shape = (tool == annot_tool_rect || tool == annot_tool_ellipse || tool == annot_tool_round_rect ||
                        tool == annot_tool_triangle || tool == annot_tool_diamond || tool == annot_tool_pentagon ||
                        tool == annot_tool_hexagon || tool == annot_tool_star || tool == annot_tool_parallelogram ||
                        tool == annot_tool_bubble || tool == annot_tool_cloud || tool == annot_tool_block_arrow ||
                        tool == annot_tool_icon);
    if (!lineLike && !shape)
        return to;
    const double k = aspect > 0 ? aspect : 1.0;
    //等长坐标里的拖动量：横向乘以宽高比
    const double ex = (to.m_x - from.m_x) * k;
    const double ey = to.m_y - from.m_y;
    double rx = ex;
    double ry = ey;
    if (lineLike)
    {
        //方向吸附到 45 度的整数倍，长度不变
        const double step = kPi / 4;
        const double len = sqrt(ex * ex + ey * ey);
        const double angle = floor(atan2(ey, ex) / step + 0.5) * step;
        rx = len * cos(angle);
        ry = len * sin(angle);
    }
    else
    {
        //外接框按正形的宽高比：取拖动范围里较长的一边（换算到同一比例后比较），保持拖动方向
        const double ratio = AnnotRegularShapeAspect(tool);
        double w = fabs(ex);
        double h = fabs(ey);
        if (w / ratio > h)
            h = w / ratio;
        else
            w = h * ratio;
        rx = ex < 0 ? -w : w;
        ry = ey < 0 ? -h : h;
    }
    //换回归一化坐标
    double dx = rx / k;
    double dy = ry;
    //超出画面：整段按同一比例缩短，角度与正形不变
    double scale = 1.0;
    if (dx > 0 && from.m_x + dx > coordMax)
        scale = (coordMax - from.m_x) / dx < scale ? (coordMax - from.m_x) / dx : scale;
    if (dx < 0 && from.m_x + dx < 0)
        scale = (-from.m_x) / dx < scale ? (-from.m_x) / dx : scale;
    if (dy > 0 && from.m_y + dy > coordMax)
        scale = (coordMax - from.m_y) / dy < scale ? (coordMax - from.m_y) / dy : scale;
    if (dy < 0 && from.m_y + dy < 0)
        scale = (-from.m_y) / dy < scale ? (-from.m_y) / dy : scale;
    scale = scale < 0 ? 0 : scale;
    dx *= scale;
    dy *= scale;
    return GeoPoint(from.m_x + dx, from.m_y + dy);
}

std::vector<GeoPoint> AnnotArrowPath(int routing, GeoPoint from, GeoPoint to)
{
    std::vector<GeoPoint> out;
    if (routing == annot_route_elbow)
    {
        out.push_back(from);
        out.push_back(GeoPoint(to.m_x, from.m_y));
        out.push_back(to);
        return out;
    }
    if (routing == annot_route_arc)
    {
        //二次贝塞尔：控制点在中点向左侧偏两倍拱高（曲线顶点恰好偏一倍拱高）
        const double dx = to.m_x - from.m_x;
        const double dy = to.m_y - from.m_y;
        const double len = sqrt(dx * dx + dy * dy);
        GeoPoint ctrl((from.m_x + to.m_x) / 2, (from.m_y + to.m_y) / 2);
        if (len > 0)
        {
            //左法向：方向 (dx, dy) 逆时针转 90 度，屏幕坐标里是 (dy, -dx)
            const double nx = dy / len;
            const double ny = -dx / len;
            ctrl.m_x += nx * len * kArcBulgeRatio * 2;
            ctrl.m_y += ny * len * kArcBulgeRatio * 2;
        }
        for (int i = 0; i <= kAnnotArcSegments; ++i)
        {
            const double s = static_cast<double>(i) / kAnnotArcSegments;
            const double u = 1 - s;
            out.push_back(GeoPoint(u * u * from.m_x + 2 * u * s * ctrl.m_x + s * s * to.m_x,
                                   u * u * from.m_y + 2 * u * s * ctrl.m_y + s * s * to.m_y));
        }
        return out;
    }
    out.push_back(from);
    out.push_back(to);
    return out;
}

AnnotCapShape AnnotCapGeometry(int cap, GeoPoint tip, GeoPoint dir, double size)
{
    AnnotCapShape shape;
    //back：从端点往线内的方向；n：与线垂直的方向
    const GeoPoint back(-dir.m_x, -dir.m_y);
    const GeoPoint n(-dir.m_y, dir.m_x);
    switch (cap)
    {
    //实心 / 空心三角：尖在端点，底边在线内 size 处
    case annot_cap_triangle:
    case annot_cap_triangle_hollow:
        shape.m_points.push_back(tip);
        shape.m_points.push_back(GeoPoint(tip.m_x + back.m_x * size + n.m_x * size * kCapHalfWidth,
                                          tip.m_y + back.m_y * size + n.m_y * size * kCapHalfWidth));
        shape.m_points.push_back(GeoPoint(tip.m_x + back.m_x * size - n.m_x * size * kCapHalfWidth,
                                          tip.m_y + back.m_y * size - n.m_y * size * kCapHalfWidth));
        shape.m_closed = true;
        shape.m_filled = (cap == annot_cap_triangle);
        shape.m_inset = size;
        break;
    //开口 V 形：两翼与端点相连，不闭合
    case annot_cap_open:
        shape.m_points.push_back(GeoPoint(tip.m_x + back.m_x * size + n.m_x * size * kCapHalfWidth,
                                          tip.m_y + back.m_y * size + n.m_y * size * kCapHalfWidth));
        shape.m_points.push_back(tip);
        shape.m_points.push_back(GeoPoint(tip.m_x + back.m_x * size - n.m_x * size * kCapHalfWidth,
                                          tip.m_y + back.m_y * size - n.m_y * size * kCapHalfWidth));
        break;
    //燕尾：三角的底边向尖凹进去
    case annot_cap_swallowtail:
        shape.m_points.push_back(tip);
        shape.m_points.push_back(GeoPoint(tip.m_x + back.m_x * size + n.m_x * size * kCapHalfWidth,
                                          tip.m_y + back.m_y * size + n.m_y * size * kCapHalfWidth));
        shape.m_points.push_back(GeoPoint(tip.m_x + back.m_x * size * kSwallowNotch,
                                          tip.m_y + back.m_y * size * kSwallowNotch));
        shape.m_points.push_back(GeoPoint(tip.m_x + back.m_x * size - n.m_x * size * kCapHalfWidth,
                                          tip.m_y + back.m_y * size - n.m_y * size * kCapHalfWidth));
        shape.m_closed = true;
        shape.m_filled = true;
        shape.m_inset = size * kSwallowNotch;
        break;
    //实心 / 空心圆：以端点为圆心，直径为 size
    case annot_cap_circle:
    case annot_cap_circle_hollow:
        for (int i = 0; i < kAnnotCapCircleSegments; ++i)
        {
            const double rad = 2 * kPi * i / kAnnotCapCircleSegments;
            shape.m_points.push_back(GeoPoint(tip.m_x + size / 2 * cos(rad), tip.m_y + size / 2 * sin(rad)));
        }
        shape.m_closed = true;
        shape.m_filled = (cap == annot_cap_circle);
        shape.m_inset = size / 2;
        break;
    //菱形：以端点为中心
    case annot_cap_diamond:
        shape.m_points.push_back(GeoPoint(tip.m_x + dir.m_x * size / 2, tip.m_y + dir.m_y * size / 2));
        shape.m_points.push_back(GeoPoint(tip.m_x + n.m_x * size * kDiamondHalfWidth,
                                          tip.m_y + n.m_y * size * kDiamondHalfWidth));
        shape.m_points.push_back(GeoPoint(tip.m_x + back.m_x * size / 2, tip.m_y + back.m_y * size / 2));
        shape.m_points.push_back(GeoPoint(tip.m_x - n.m_x * size * kDiamondHalfWidth,
                                          tip.m_y - n.m_y * size * kDiamondHalfWidth));
        shape.m_closed = true;
        shape.m_filled = true;
        shape.m_inset = size / 2;
        break;
    //竖线：过端点、与线垂直
    case annot_cap_bar:
        shape.m_points.push_back(GeoPoint(tip.m_x + n.m_x * size / 2, tip.m_y + n.m_y * size / 2));
        shape.m_points.push_back(GeoPoint(tip.m_x - n.m_x * size / 2, tip.m_y - n.m_y * size / 2));
        break;
    //无或取值非法：不画
    default:
        break;
    }
    return shape;
}

std::vector<GeoPoint> AnnotBlockArrowOutline(int variant, GeoPoint a, GeoPoint b, int rotationDeg)
{
    const double l = a.m_x < b.m_x ? a.m_x : b.m_x;
    const double r = a.m_x > b.m_x ? a.m_x : b.m_x;
    const double t = a.m_y < b.m_y ? a.m_y : b.m_y;
    const double bt = a.m_y > b.m_y ? a.m_y : b.m_y;
    const double w = r - l;
    const double h = bt - t;
    std::vector<GeoPoint> out;
    if (w <= 0 || h <= 0)
        return out;
    //各样式在单位方框里的顶点
    static const double kRight[][2] = { { 0, 0.3 }, { 0.6, 0.3 }, { 0.6, 0 }, { 1, 0.5 }, { 0.6, 1 }, { 0.6, 0.7 },
                                        { 0, 0.7 } };
    static const double kDouble[][2] = { { 0, 0.5 }, { 0.3, 0 }, { 0.3, 0.3 }, { 0.7, 0.3 }, { 0.7, 0 }, { 1, 0.5 },
                                         { 0.7, 1 }, { 0.7, 0.7 }, { 0.3, 0.7 }, { 0.3, 1 } };
    static const double kUpDown[][2] = { { 0.5, 0 }, { 1, 0.3 }, { 0.7, 0.3 }, { 0.7, 0.7 }, { 1, 0.7 }, { 0.5, 1 },
                                         { 0, 0.7 }, { 0.3, 0.7 }, { 0.3, 0.3 }, { 0, 0.3 } };
    static const double kBent[][2] = { { 0, 1 }, { 0, 0.2 }, { 0.65, 0.2 }, { 0.65, 0 }, { 1, 0.35 }, { 0.65, 0.7 },
                                       { 0.65, 0.5 }, { 0.3, 0.5 }, { 0.3, 1 } };
    switch (variant)
    {
    //右箭头
    case annot_block_right:
        out = mapUnitList(kRight, sizeof(kRight) / sizeof(kRight[0]), l, t, w, h);
        break;
    //左右双向
    case annot_block_double:
        out = mapUnitList(kDouble, sizeof(kDouble) / sizeof(kDouble[0]), l, t, w, h);
        break;
    //上下双向
    case annot_block_up_down:
        out = mapUnitList(kUpDown, sizeof(kUpDown) / sizeof(kUpDown[0]), l, t, w, h);
        break;
    //弯折：先向上再向右
    case annot_block_bent:
        out = mapUnitList(kBent, sizeof(kBent) / sizeof(kBent[0]), l, t, w, h);
        break;
    //U 形掉头
    case annot_block_u_turn:
        {
            const std::vector<GeoPoint> unit = uTurnUnit();
            for (size_t i = 0; i < unit.size(); ++i)
                out.push_back(mapUnit(unit[i].m_x, unit[i].m_y, l, t, w, h));
        }
        break;
    //环形循环
    case annot_block_circular:
        {
            const std::vector<GeoPoint> unit = circularUnit();
            for (size_t i = 0; i < unit.size(); ++i)
                out.push_back(mapUnit(unit[i].m_x, unit[i].m_y, l, t, w, h));
        }
        break;
    //取值非法：没有轮廓
    default:
        return out;
    }
    if (rotationDeg % 360 != 0)
    {
        const GeoPoint c(l + w / 2, t + h / 2);
        for (size_t i = 0; i < out.size(); ++i)
            out[i] = rotateAround(out[i], c, rotationDeg);
    }
    return out;
}

std::vector<GeoPoint> AnnotSmoothCurve(const std::vector<GeoPoint>& pts)
{
    const size_t kMinControlPoints = 3;
    if (pts.size() < kMinControlPoints)
        return pts;
    std::vector<GeoPoint> out;
    out.reserve((pts.size() - 1) * kAnnotCurveStepsPerSegment + 1);
    for (size_t i = 0; i + 1 < pts.size(); ++i)
    {
        //Catmull-Rom：首尾段把端点重复一次当作外侧控制点
        const GeoPoint& p0 = pts[i == 0 ? 0 : i - 1];
        const GeoPoint& p1 = pts[i];
        const GeoPoint& p2 = pts[i + 1];
        const GeoPoint& p3 = pts[i + 2 < pts.size() ? i + 2 : i + 1];
        for (int s = 0; s < kAnnotCurveStepsPerSegment; ++s)
        {
            const double u = static_cast<double>(s) / kAnnotCurveStepsPerSegment;
            const double u2 = u * u;
            const double u3 = u2 * u;
            const double x = 0.5 * (2 * p1.m_x + (-p0.m_x + p2.m_x) * u + (2 * p0.m_x - 5 * p1.m_x + 4 * p2.m_x - p3.m_x) * u2 +
                                    (-p0.m_x + 3 * p1.m_x - 3 * p2.m_x + p3.m_x) * u3);
            const double y = 0.5 * (2 * p1.m_y + (-p0.m_y + p2.m_y) * u + (2 * p0.m_y - 5 * p1.m_y + 4 * p2.m_y - p3.m_y) * u2 +
                                    (-p0.m_y + 3 * p1.m_y - 3 * p2.m_y + p3.m_y) * u3);
            out.push_back(GeoPoint(x, y));
        }
    }
    out.push_back(pts.back());
    return out;
}

std::vector<float> AnnotDashPattern(int lineStyle)
{
    std::vector<float> out;
    switch (lineStyle)
    {
    //虚线：实 4、空 2（以线宽为单位）
    case annot_line_dash:
        out.push_back(4.0f);
        out.push_back(2.0f);
        break;
    //点线：实 1、空 2
    case annot_line_dot:
        out.push_back(1.0f);
        out.push_back(2.0f);
        break;
    //点划线：实 4、空 2、实 1、空 2
    case annot_line_dash_dot:
        out.push_back(4.0f);
        out.push_back(2.0f);
        out.push_back(1.0f);
        out.push_back(2.0f);
        break;
    //实线或取值非法：不用虚线
    default:
        break;
    }
    return out;
}

std::wstring AnnotBadgeLabel(int sequence, int number)
{
    if (number <= 0)
        return std::wstring();
    if (sequence == annot_seq_letter)
    {
        //双射二十六进制：1→A，26→Z，27→AA
        const int kLetters = 26;
        std::wstring out;
        for (int n = number; n > 0; n = (n - 1) / kLetters)
            out.insert(out.begin(), static_cast<wchar_t>(L'A' + (n - 1) % kLetters));
        return out;
    }
    if (sequence == annot_seq_chinese)
    {
        const std::wstring cn = chineseNumber(number);
        if (!cn.empty())
            return cn;
    }
    wchar_t buf[16] = { 0 };
    _snwprintf_s(buf, sizeof(buf) / sizeof(buf[0]), _TRUNCATE, L"%d", number);
    return buf;
}

std::vector<double> AnnotBrushWidths(const std::vector<GeoPoint>& pts, double baseWidth)
{
    std::vector<double> out;
    out.reserve(pts.size());
    double current = baseWidth;
    for (size_t i = 0; i < pts.size(); ++i)
    {
        if (i > 0)
        {
            const double dx = pts[i].m_x - pts[i - 1].m_x;
            const double dy = pts[i].m_y - pts[i - 1].m_y;
            const double dist = sqrt(dx * dx + dy * dy);
            //离得越远越细：dist 为 0 时最粗，达到基准线宽的若干倍时最细
            double speed = baseWidth > 0 ? dist / (baseWidth * kBrushFastDistance) : 1.0;
            if (speed > 1)
                speed = 1;
            const double target = baseWidth * (kBrushMaxRatio - (kBrushMaxRatio - kBrushMinRatio) * speed);
            current = current + (target - current) * kBrushSmooth;
        }
        out.push_back(current);
    }
    return out;
}

} // namespace screenshare
