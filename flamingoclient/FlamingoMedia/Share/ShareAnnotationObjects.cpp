/**
 *  屏幕共享标注的对象几何，见 ShareAnnotationObjects.h。
 *
 *  命中与裁边在「等长坐标」里算：横坐标乘以宽高比，使横纵一个单位代表同样的长度（都按内容高度计）；容差、线宽按同样的
 *  比例换算。形状的轮廓复用 ShareAnnotationGeometry 的函数（它们对任何等长坐标都成立）。
 *
 *  balloonwj@qq.com   2026-10-05
 */
#include "ShareAnnotationObjects.h"

#include <math.h>
#include <stdlib.h>
#include <algorithm>
#include "ShareAnnotationGeometry.h"

namespace screenshare {

namespace {

//UTF-8 多字节字符的后续字节：最高两位为 10
const unsigned char kUtf8ContinuationMask = 0xC0;
const unsigned char kUtf8ContinuationBits = 0x80;
//ASCII 的上限（不含）
const unsigned char kAsciiLimit = 0x80;

//宽高比不大于 0 时按 1
double safeAspect(double aspect)
{
    return aspect > 0 ? aspect : 1.0;
}

//四舍五入并限制在归一化坐标范围里
int clampCoord(double v)
{
    const long r = lround(v);
    if (r < 0)
        return 0;
    return r > kAnnotCoordMax ? kAnnotCoordMax : static_cast<int>(r);
}

//字号值换成纵向的归一化单位
double emY(int fontSize)
{
    return static_cast<double>(fontSize) * kAnnotCoordMax / kAnnotTextScale;
}

//归一化坐标换成等长坐标（横坐标乘以宽高比）
GeoPoint toIso(const AnnotationPoint& p, double aspect)
{
    return GeoPoint(p.m_x * aspect, p.m_y);
}

//若干归一化坐标的点换成等长坐标
std::vector<GeoPoint> toIso(const std::vector<AnnotationPoint>& pts, double aspect)
{
    std::vector<GeoPoint> out;
    out.reserve(pts.size());
    for (size_t i = 0; i < pts.size(); ++i)
        out.push_back(toIso(pts[i], aspect));
    return out;
}

//点到线段的距离（等长坐标）
double distToSegment(const GeoPoint& p, const GeoPoint& a, const GeoPoint& b)
{
    const double dx = b.m_x - a.m_x;
    const double dy = b.m_y - a.m_y;
    const double len2 = dx * dx + dy * dy;
    double t = 0;
    if (len2 > 0)
    {
        t = ((p.m_x - a.m_x) * dx + (p.m_y - a.m_y) * dy) / len2;
        t = t < 0 ? 0 : (t > 1 ? 1 : t);
    }
    const double ex = p.m_x - (a.m_x + dx * t);
    const double ey = p.m_y - (a.m_y + dy * t);
    return sqrt(ex * ex + ey * ey);
}

//点到折线的距离；closed 时首尾也相连；只有一个点时为到这个点的距离；没有点时为极大值
double distToPath(const GeoPoint& p, const std::vector<GeoPoint>& path, bool closed)
{
    const double kFar = 1e18;
    if (path.empty())
        return kFar;
    if (path.size() == 1)
        return distToSegment(p, path[0], path[0]);
    double best = kFar;
    for (size_t i = 1; i < path.size(); ++i)
        best = (std::min)(best, distToSegment(p, path[i - 1], path[i]));
    if (closed)
        best = (std::min)(best, distToSegment(p, path.back(), path.front()));
    return best;
}

//点是否在多边形里（奇偶规则）
bool insidePolygon(const GeoPoint& p, const std::vector<GeoPoint>& poly)
{
    bool inside = false;
    const size_t n = poly.size();
    for (size_t i = 0, j = n - 1; i < n; j = i++)
    {
        const GeoPoint& a = poly[i];
        const GeoPoint& b = poly[j];
        if ((a.m_y > p.m_y) != (b.m_y > p.m_y))
        {
            const double x = (b.m_x - a.m_x) * (p.m_y - a.m_y) / (b.m_y - a.m_y) + a.m_x;
            if (p.m_x < x)
                inside = !inside;
        }
    }
    return inside;
}

//封闭形状在等长坐标里的轮廓
std::vector<GeoPoint> shapeOutlineIso(const AnnotationStroke& s, double aspect)
{
    if (s.m_points.size() < 2)
        return std::vector<GeoPoint>();
    const GeoPoint a = toIso(s.m_points.front(), aspect);
    const GeoPoint b = toIso(s.m_points.back(), aspect);
    if (s.m_tool == annot_tool_block_arrow)
        return AnnotBlockArrowOutline(s.m_style.m_variant, a, b, s.m_style.m_rotation);
    return AnnotShapeOutline(s.m_tool, a, b);
}

//线条在等长坐标里的路径：箭头、连接线按走向；曲线为平滑曲线；其余为各点连成的折线
std::vector<GeoPoint> linePathIso(const AnnotationStroke& s, const std::vector<AnnotationStroke>& all, double aspect)
{
    if (s.m_tool == annot_tool_connector)
    {
        AnnotationPoint from;
        AnnotationPoint to;
        AnnotConnectorEnds(s, all, aspect, from, to);
        return AnnotArrowPath(s.m_style.m_routing, toIso(from, aspect), toIso(to, aspect));
    }
    if (s.m_tool == annot_tool_arrow && s.m_points.size() >= 2)
        return AnnotArrowPath(s.m_style.m_routing, toIso(s.m_points.front(), aspect), toIso(s.m_points.back(), aspect));
    const std::vector<GeoPoint> pts = toIso(s.m_points, aspect);
    if (s.m_tool == annot_tool_curve)
        return AnnotSmoothCurve(pts);
    return pts;
}

//按引用找可吸附的对象；找不到或不可吸附时返回 NULL
const AnnotationStroke* findAttachable(const std::vector<AnnotationStroke>& all, const AnnotationRef& ref)
{
    if (!ref.IsSet())
        return NULL;
    for (size_t i = 0; i < all.size(); ++i)
    {
        if (all[i].m_author == ref.m_author && all[i].m_id == ref.m_id)
            return AnnotIsAttachable(all[i]) ? &all[i] : NULL;
    }
    return NULL;
}

/**
 *  从矩形中心 c 朝 toward 方向出发，求离开矩形之后再走 kAnnotConnectorGap 的点（等长坐标里算），不超过 toward。
 *  @param c、toward 归一化坐标（浮点）
 *  @param r         矩形（归一化坐标）
 */
AnnotationPoint clipOut(double cx, double cy, double tx, double ty, const AnnotRect& r, double aspect)
{
    const double dx = (tx - cx) * aspect;
    const double dy = ty - cy;
    const double len = sqrt(dx * dx + dy * dy);
    if (len <= 0)
        return AnnotationPoint(static_cast<uint16_t>(clampCoord(cx)), static_cast<uint16_t>(clampCoord(cy)));
    const double halfW = r.Width() / 2.0 * aspect;
    const double halfH = r.Height() / 2.0;
    const double kHuge = 1e18;
    const double tx1 = (dx != 0) ? halfW / fabs(dx) : kHuge;
    const double ty1 = (dy != 0) ? halfH / fabs(dy) : kHuge;
    double dist = (std::min)(tx1, ty1) * len + kAnnotConnectorGap;
    if (dist > len)
        dist = len;
    const double k = dist / len;
    return AnnotationPoint(static_cast<uint16_t>(clampCoord(cx + dx * k / aspect)),
                           static_cast<uint16_t>(clampCoord(cy + dy * k)));
}

//外接矩形向四周扩大容差（横向按归一化横向单位，纵向换算到纵向单位）
AnnotRect inflate(const AnnotRect& r, int tolerance, double aspect)
{
    const int ty = static_cast<int>(lround(tolerance * aspect));
    return AnnotRect(r.m_left - tolerance, r.m_top - ty, r.m_right + tolerance, r.m_bottom + ty);
}

//两个点张成的矩形
AnnotRect rectOf(const AnnotationPoint& a, const AnnotationPoint& b)
{
    return AnnotRect((std::min)(a.m_x, b.m_x), (std::min)(a.m_y, b.m_y), (std::max)(a.m_x, b.m_x),
                     (std::max)(a.m_y, b.m_y));
}

} // namespace

AnnotRect::AnnotRect()
    : m_left(1)
    , m_top(1)
    , m_right(0)
    , m_bottom(0)
{
}

AnnotRect::AnnotRect(int left, int top, int right, int bottom)
    : m_left(left)
    , m_top(top)
    , m_right(right)
    , m_bottom(bottom)
{
}

bool AnnotRect::IsEmpty() const
{
    return m_left > m_right || m_top > m_bottom;
}

bool AnnotRect::Contains(int x, int y) const
{
    return x >= m_left && x <= m_right && y >= m_top && y <= m_bottom;
}

bool AnnotRect::ContainsRect(const AnnotRect& r) const
{
    return !r.IsEmpty() && r.m_left >= m_left && r.m_right <= m_right && r.m_top >= m_top && r.m_bottom <= m_bottom;
}

int AnnotRect::Width() const
{
    return m_right - m_left;
}

int AnnotRect::Height() const
{
    return m_bottom - m_top;
}

AnnotationPoint AnnotRect::Center() const
{
    return AnnotationPoint(static_cast<uint16_t>((m_left + m_right) / 2), static_cast<uint16_t>((m_top + m_bottom) / 2));
}

void AnnotApproxTextSize(const std::string& utf8, int fontSize, double aspect, int& w, int& h)
{
    const double ey = emY(fontSize);
    const double ex = ey / safeAspect(aspect);
    //逐行累计字宽：ASCII 半个字宽（kAnnotApproxAsciiWidth），其余一个字宽；多字节字符只在首字节计一次
    double widest = 0;
    double line = 0;
    int lines = 1;
    for (size_t i = 0; i < utf8.size(); ++i)
    {
        const unsigned char c = static_cast<unsigned char>(utf8[i]);
        if (c == '\n')
        {
            widest = (std::max)(widest, line);
            line = 0;
            ++lines;
            continue;
        }
        if (c == '\r' || (c & kUtf8ContinuationMask) == kUtf8ContinuationBits)
            continue;
        line += (c < kAsciiLimit) ? kAnnotApproxAsciiWidth : 1.0;
    }
    widest = (std::max)(widest, line);
    w = static_cast<int>(lround(widest * ex));
    h = static_cast<int>(lround(lines * kAnnotApproxLineHeight * ey));
}

void AnnotConnectorEnds(const AnnotationStroke& c, const std::vector<AnnotationStroke>& all, double aspect,
                        AnnotationPoint& from, AnnotationPoint& to)
{
    if (c.m_points.empty())
    {
        from = AnnotationPoint();
        to = AnnotationPoint();
        return;
    }
    from = c.m_points.front();
    to = c.m_points.back();
    if (c.m_tool != annot_tool_connector)
        return;
    const double a = safeAspect(aspect);
    const AnnotationStroke* sa = findAttachable(all, c.m_style.m_attachStart);
    const AnnotationStroke* sb = findAttachable(all, c.m_style.m_attachEnd);
    //两端的出发点：吸附的取被吸附对象外接矩形的中心，否则取自己的点
    AnnotRect ra;
    AnnotRect rb;
    double fx = from.m_x;
    double fy = from.m_y;
    double tx = to.m_x;
    double ty = to.m_y;
    if (sa != NULL)
    {
        ra = AnnotObjectBounds(*sa, all, a);
        fx = (ra.m_left + ra.m_right) / 2.0;
        fy = (ra.m_top + ra.m_bottom) / 2.0;
    }
    if (sb != NULL)
    {
        rb = AnnotObjectBounds(*sb, all, a);
        tx = (rb.m_left + rb.m_right) / 2.0;
        ty = (rb.m_top + rb.m_bottom) / 2.0;
    }
    if (sa != NULL)
        from = clipOut(fx, fy, tx, ty, ra, a);
    if (sb != NULL)
        to = clipOut(tx, ty, fx, fy, rb, a);
}

AnnotRect AnnotObjectBounds(const AnnotationStroke& s, const std::vector<AnnotationStroke>& all, double aspect)
{
    if (s.m_points.empty())
        return AnnotRect();
    const double a = safeAspect(aspect);
    const AnnotationPoint& p = s.m_points[0];
    switch (s.m_tool)
    {
    //文字：左上角加近似宽高
    case annot_tool_text:
        {
            int w = 0;
            int h = 0;
            AnnotApproxTextSize(s.m_text, s.m_width, a, w, h);
            return AnnotRect(p.m_x, p.m_y, (std::min)(kAnnotCoordMax, p.m_x + w), (std::min)(kAnnotCoordMax, p.m_y + h));
        }
    //数字标号：中心加半径（与绘制一致：字号的 kAnnotBadgeRadiusRatio 倍）
    case annot_tool_badge:
        {
            const double ry = emY(s.m_width) * kAnnotBadgeRadiusRatio;
            const double rx = ry / a;
            return AnnotRect(clampCoord(p.m_x - rx), clampCoord(p.m_y - ry), clampCoord(p.m_x + rx), clampCoord(p.m_y + ry));
        }
    //连接线：按实际的两端
    case annot_tool_connector:
        {
            AnnotationPoint from;
            AnnotationPoint to;
            AnnotConnectorEnds(s, all, a, from, to);
            return rectOf(from, to);
        }
    //其余：点的范围
    default:
        break;
    }
    AnnotRect r(p.m_x, p.m_y, p.m_x, p.m_y);
    for (size_t i = 1; i < s.m_points.size(); ++i)
    {
        r.m_left = (std::min)(r.m_left, static_cast<int>(s.m_points[i].m_x));
        r.m_top = (std::min)(r.m_top, static_cast<int>(s.m_points[i].m_y));
        r.m_right = (std::max)(r.m_right, static_cast<int>(s.m_points[i].m_x));
        r.m_bottom = (std::max)(r.m_bottom, static_cast<int>(s.m_points[i].m_y));
    }
    return r;
}

bool AnnotIsSelectable(const AnnotationStroke& s)
{
    return s.m_finished && !s.m_points.empty() && s.m_tool != annot_tool_laser && s.m_tool != annot_tool_eraser_area;
}

bool AnnotIsClosedShape(int tool)
{
    switch (tool)
    {
    //矩形、椭圆与第 19 期的各种形状、块状箭头
    case annot_tool_rect:
    case annot_tool_ellipse:
    case annot_tool_round_rect:
    case annot_tool_triangle:
    case annot_tool_diamond:
    case annot_tool_pentagon:
    case annot_tool_hexagon:
    case annot_tool_star:
    case annot_tool_parallelogram:
    case annot_tool_bubble:
    case annot_tool_cloud:
    case annot_tool_block_arrow:
        return true;
    //其余不是
    default:
        return false;
    }
}

bool AnnotIsAttachable(const AnnotationStroke& s)
{
    if (!AnnotIsSelectable(s))
        return false;
    return s.m_tool == annot_tool_icon || s.m_tool == annot_tool_text || s.m_tool == annot_tool_badge ||
           AnnotIsClosedShape(s.m_tool);
}

bool AnnotIsEndpointObject(const AnnotationStroke& s)
{
    const size_t kEndpointPoints = 2;
    return s.m_points.size() == kEndpointPoints &&
           (s.m_tool == annot_tool_line || s.m_tool == annot_tool_arrow || s.m_tool == annot_tool_connector);
}

bool AnnotHitTest(const AnnotationStroke& s, const std::vector<AnnotationStroke>& all, const AnnotationPoint& pt,
                  int tolerance, double aspect, bool insideCounts)
{
    if (s.m_points.empty())
        return false;
    const double a = safeAspect(aspect);
    //图元、文字、标号：外接矩形（扩大容差）
    if (s.m_tool == annot_tool_icon || s.m_tool == annot_tool_text || s.m_tool == annot_tool_badge)
        return inflate(AnnotObjectBounds(s, all, a), tolerance, a).Contains(pt.m_x, pt.m_y);
    //等长坐标里的点、容差与半个线宽
    const GeoPoint p = toIso(pt, a);
    const double tol = tolerance * a;
    const double halfWidth = static_cast<double>(s.m_width) * kAnnotCoordMax / kAnnotWidthScale * a / 2;
    if (AnnotIsClosedShape(s.m_tool))
    {
        const std::vector<GeoPoint> outline = shapeOutlineIso(s, a);
        if (outline.empty())
            return false;
        if (insideCounts && insidePolygon(p, outline))
            return true;
        return distToPath(p, outline, true) <= tol + halfWidth;
    }
    return distToPath(p, linePathIso(s, all, a), false) <= tol + halfWidth;
}

int AnnotHitObject(const std::vector<AnnotationStroke>& all, const AnnotationPoint& pt, int tolerance, double aspect,
                   bool insideCounts)
{
    for (size_t i = all.size(); i > 0; --i)
    {
        const AnnotationStroke& s = all[i - 1];
        if (AnnotIsSelectable(s) && AnnotHitTest(s, all, pt, tolerance, aspect, insideCounts))
            return static_cast<int>(i - 1);
    }
    return -1;
}

int AnnotHitAttachable(const std::vector<AnnotationStroke>& all, const AnnotationPoint& pt, int tolerance, double aspect,
                       const AnnotationRef& exclude)
{
    const double a = safeAspect(aspect);
    for (size_t i = all.size(); i > 0; --i)
    {
        const AnnotationStroke& s = all[i - 1];
        if (!AnnotIsAttachable(s) || (s.m_author == exclude.m_author && s.m_id == exclude.m_id))
            continue;
        //吸附看外接矩形：端点落进矩形（扩大容差）即可
        if (inflate(AnnotObjectBounds(s, all, a), tolerance, a).Contains(pt.m_x, pt.m_y))
            return static_cast<int>(i - 1);
    }
    return -1;
}

std::vector<int> AnnotObjectsInRect(const std::vector<AnnotationStroke>& all, const AnnotRect& box, double aspect)
{
    std::vector<int> out;
    for (size_t i = 0; i < all.size(); ++i)
    {
        if (AnnotIsSelectable(all[i]) && box.ContainsRect(AnnotObjectBounds(all[i], all, aspect)))
            out.push_back(static_cast<int>(i));
    }
    return out;
}

std::vector<AnnotationPoint> AnnotHandlePositions(const AnnotationStroke& s, const std::vector<AnnotationStroke>& all,
                                                  double aspect)
{
    std::vector<AnnotationPoint> out;
    if (s.m_points.empty())
        return out;
    //两端可拖的对象：起点、终点（连接线按实际两端）
    if (AnnotIsEndpointObject(s))
    {
        AnnotationPoint from;
        AnnotationPoint to;
        AnnotConnectorEnds(s, all, aspect, from, to);
        out.push_back(from);
        out.push_back(to);
        return out;
    }
    //外接矩形的四角与四边中点，顺序同 AnnotHandle
    const AnnotRect r = AnnotObjectBounds(s, all, aspect);
    const uint16_t l = static_cast<uint16_t>(r.m_left);
    const uint16_t t = static_cast<uint16_t>(r.m_top);
    const uint16_t rr = static_cast<uint16_t>(r.m_right);
    const uint16_t b = static_cast<uint16_t>(r.m_bottom);
    const uint16_t cx = static_cast<uint16_t>((r.m_left + r.m_right) / 2);
    const uint16_t cy = static_cast<uint16_t>((r.m_top + r.m_bottom) / 2);
    out.push_back(AnnotationPoint(l, t));
    out.push_back(AnnotationPoint(cx, t));
    out.push_back(AnnotationPoint(rr, t));
    out.push_back(AnnotationPoint(rr, cy));
    out.push_back(AnnotationPoint(rr, b));
    out.push_back(AnnotationPoint(cx, b));
    out.push_back(AnnotationPoint(l, b));
    out.push_back(AnnotationPoint(l, cy));
    return out;
}

int AnnotHitHandle(const std::vector<AnnotationPoint>& handles, bool endpoints, const AnnotationPoint& pt, int tolerance)
{
    //取容差以内最近的一个
    int best = annot_handle_none;
    int bestDist = 0;
    for (size_t i = 0; i < handles.size(); ++i)
    {
        const int dx = abs(static_cast<int>(handles[i].m_x) - pt.m_x);
        const int dy = abs(static_cast<int>(handles[i].m_y) - pt.m_y);
        if (dx > tolerance || dy > tolerance)
            continue;
        const int d = dx + dy;
        if (best == annot_handle_none || d < bestDist)
        {
            best = static_cast<int>(i);
            bestDist = d;
        }
    }
    if (best == annot_handle_none || !endpoints)
        return best;
    return best == 0 ? annot_handle_start : annot_handle_end;
}

AnnotRect AnnotResizeRect(const AnnotRect& r, int handle, const AnnotationPoint& pt, bool keepRatio)
{
    //拖动的是哪几条边
    const bool left = (handle == annot_handle_nw || handle == annot_handle_sw || handle == annot_handle_w);
    const bool right = (handle == annot_handle_ne || handle == annot_handle_se || handle == annot_handle_e);
    const bool top = (handle == annot_handle_nw || handle == annot_handle_ne || handle == annot_handle_n);
    const bool bottom = (handle == annot_handle_sw || handle == annot_handle_se || handle == annot_handle_s);
    AnnotRect out = r;
    //拖动的边跟着点走，但不越过对边减去最小边长的位置（不翻转）
    if (left)
        out.m_left = (std::min)(static_cast<int>(pt.m_x), r.m_right - kAnnotMinObjectSize);
    if (right)
        out.m_right = (std::max)(static_cast<int>(pt.m_x), r.m_left + kAnnotMinObjectSize);
    if (top)
        out.m_top = (std::min)(static_cast<int>(pt.m_y), r.m_bottom - kAnnotMinObjectSize);
    if (bottom)
        out.m_bottom = (std::max)(static_cast<int>(pt.m_y), r.m_top + kAnnotMinObjectSize);
    //拖角并保持比例：两个方向取较大的倍数，对角不动
    const bool corner = (left || right) && (top || bottom);
    if (keepRatio && corner && r.Width() > 0 && r.Height() > 0)
    {
        const double scale = (std::max)(static_cast<double>(out.Width()) / r.Width(),
                                        static_cast<double>(out.Height()) / r.Height());
        const int w = static_cast<int>(lround(r.Width() * scale));
        const int h = static_cast<int>(lround(r.Height() * scale));
        if (left)
            out.m_left = r.m_right - w;
        else
            out.m_right = r.m_left + w;
        if (top)
            out.m_top = r.m_bottom - h;
        else
            out.m_bottom = r.m_top + h;
    }
    //限制在画面以内
    out.m_left = (std::max)(0, out.m_left);
    out.m_top = (std::max)(0, out.m_top);
    out.m_right = (std::min)(kAnnotCoordMax, out.m_right);
    out.m_bottom = (std::min)(kAnnotCoordMax, out.m_bottom);
    return out;
}

AnnotationStroke AnnotScaleObject(const AnnotationStroke& s, const AnnotRect& from, const AnnotRect& to)
{
    AnnotationStroke out = s;
    const double sx = (from.Width() > 0) ? static_cast<double>(to.Width()) / from.Width() : 1.0;
    const double sy = (from.Height() > 0) ? static_cast<double>(to.Height()) / from.Height() : 1.0;
    //各点按比例换算
    for (size_t i = 0; i < out.m_points.size(); ++i)
    {
        const double x = to.m_left + (s.m_points[i].m_x - from.m_left) * sx;
        const double y = to.m_top + (s.m_points[i].m_y - from.m_top) * sy;
        out.m_points[i] = AnnotationPoint(static_cast<uint16_t>(clampCoord(x)), static_cast<uint16_t>(clampCoord(y)));
    }
    //文字的字号按高度比例，数字标号的大小按较小的比例；限制在 1 ~ 255（一个字节）
    const int kMaxSize = 255;
    double size = s.m_width;
    if (s.m_tool == annot_tool_text)
        size = s.m_width * sy;
    else if (s.m_tool == annot_tool_badge)
        size = s.m_width * (std::min)(sx, sy);
    const long rounded = lround(size);
    out.m_width = static_cast<int>(rounded < 1 ? 1 : (rounded > kMaxSize ? kMaxSize : rounded));
    return out;
}

} // namespace screenshare
