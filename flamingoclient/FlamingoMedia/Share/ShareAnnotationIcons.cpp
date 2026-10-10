/**
 *  屏幕共享标注的网络拓扑图元，见 ShareAnnotationIcons.h。
 *
 *  每种图元的坐标都在单位方框（0~1）里手工设计，数值即图形在方框里的相对位置，不再逐个命名为常量；
 *  共用的比例（圆角半径、弧线的采样段数等）在文件开头命名。
 *  balloonwj@qq.com   2026-10-05
 */
#include "ShareAnnotationIcons.h"

#include <math.h>
#include "ShareAnnotation.h"

namespace screenshare {

namespace {

//圆周率
const double kPi = 3.14159265358979323846;
//常用的圆角半径（单位方框里的长度）
const double kSmallRadius = 0.04;
const double kMediumRadius = 0.08;
//弧线的采样段数
const int kArcSteps = 16;

//两点张成的图形（椭圆、圆角矩形）
AnnotIconPrim box(int kind, double x0, double y0, double x1, double y1, int fill, double radius)
{
    AnnotIconPrim p;
    p.m_kind = kind;
    p.m_points.push_back(GeoPoint(x0, y0));
    p.m_points.push_back(GeoPoint(x1, y1));
    p.m_fill = fill;
    p.m_radius = radius;
    return p;
}

//圆角矩形
AnnotIconPrim roundRect(double x0, double y0, double x1, double y1, int fill, double radius)
{
    return box(annot_prim_round_rect, x0, y0, x1, y1, fill, radius);
}

//椭圆
AnnotIconPrim ellipse(double x0, double y0, double x1, double y1, int fill)
{
    return box(annot_prim_ellipse, x0, y0, x1, y1, fill, 0);
}

//矩形（四个顶点的多边形）
AnnotIconPrim rect(double x0, double y0, double x1, double y1, int fill)
{
    AnnotIconPrim p;
    p.m_kind = annot_prim_polygon;
    p.m_points.push_back(GeoPoint(x0, y0));
    p.m_points.push_back(GeoPoint(x1, y0));
    p.m_points.push_back(GeoPoint(x1, y1));
    p.m_points.push_back(GeoPoint(x0, y1));
    p.m_fill = fill;
    return p;
}

//一条线段
AnnotIconPrim line(double x0, double y0, double x1, double y1)
{
    AnnotIconPrim p;
    p.m_kind = annot_prim_polyline;
    p.m_points.push_back(GeoPoint(x0, y0));
    p.m_points.push_back(GeoPoint(x1, y1));
    return p;
}

//三个点的折线（箭头的两翼）
AnnotIconPrim polyline3(double x0, double y0, double x1, double y1, double x2, double y2)
{
    AnnotIconPrim p;
    p.m_kind = annot_prim_polyline;
    p.m_points.push_back(GeoPoint(x0, y0));
    p.m_points.push_back(GeoPoint(x1, y1));
    p.m_points.push_back(GeoPoint(x2, y2));
    return p;
}

/**
 *  椭圆上的一段弧（折线），角度按弧度、向下为正（0 指向右，kPi / 2 指向下）。
 *  @param cx、cy 圆心
 *  @param rx、ry 半径
 *  @param from、to 起止角度
 */
std::vector<GeoPoint> arc(double cx, double cy, double rx, double ry, double from, double to)
{
    std::vector<GeoPoint> pts;
    for (int i = 0; i <= kArcSteps; ++i)
    {
        const double t = from + (to - from) * i / kArcSteps;
        pts.push_back(GeoPoint(cx + rx * cos(t), cy + ry * sin(t)));
    }
    return pts;
}

//弧线图形
AnnotIconPrim arcLine(double cx, double cy, double rx, double ry, double from, double to)
{
    AnnotIconPrim p;
    p.m_kind = annot_prim_polyline;
    p.m_points = arc(cx, cy, rx, ry, from, to);
    return p;
}

//电脑：显示器、屏幕、支架、底座
std::vector<AnnotIconPrim> computer()
{
    std::vector<AnnotIconPrim> v;
    v.push_back(roundRect(0.08, 0.06, 0.92, 0.66, annot_icon_fill_tint, kSmallRadius));
    v.push_back(rect(0.15, 0.13, 0.85, 0.58, annot_icon_fill_none));
    AnnotIconPrim stand;
    stand.m_kind = annot_prim_polygon;
    stand.m_points.push_back(GeoPoint(0.43, 0.66));
    stand.m_points.push_back(GeoPoint(0.57, 0.66));
    stand.m_points.push_back(GeoPoint(0.61, 0.84));
    stand.m_points.push_back(GeoPoint(0.39, 0.84));
    stand.m_fill = annot_icon_fill_tint;
    v.push_back(stand);
    v.push_back(roundRect(0.26, 0.84, 0.74, 0.92, annot_icon_fill_solid, kSmallRadius));
    return v;
}

//笔记本：屏幕、屏幕内框、键盘底座、触控板
std::vector<AnnotIconPrim> laptop()
{
    std::vector<AnnotIconPrim> v;
    v.push_back(roundRect(0.17, 0.12, 0.83, 0.64, annot_icon_fill_tint, kSmallRadius));
    v.push_back(rect(0.23, 0.18, 0.77, 0.58, annot_icon_fill_none));
    AnnotIconPrim base;
    base.m_kind = annot_prim_polygon;
    base.m_points.push_back(GeoPoint(0.10, 0.68));
    base.m_points.push_back(GeoPoint(0.90, 0.68));
    base.m_points.push_back(GeoPoint(0.98, 0.82));
    base.m_points.push_back(GeoPoint(0.02, 0.82));
    base.m_fill = annot_icon_fill_tint;
    v.push_back(base);
    v.push_back(line(0.42, 0.75, 0.58, 0.75));
    return v;
}

//服务器：机柜、三层分隔、指示灯、通风栅
std::vector<AnnotIconPrim> server()
{
    std::vector<AnnotIconPrim> v;
    v.push_back(roundRect(0.22, 0.04, 0.78, 0.96, annot_icon_fill_tint, kSmallRadius));
    v.push_back(line(0.22, 0.34, 0.78, 0.34));
    v.push_back(line(0.22, 0.64, 0.78, 0.64));
    const double rows[] = { 0.19, 0.49, 0.80 };
    for (size_t i = 0; i < sizeof(rows) / sizeof(rows[0]); ++i)
    {
        v.push_back(ellipse(0.29, rows[i] - 0.035, 0.36, rows[i] + 0.035, annot_icon_fill_solid));
        v.push_back(line(0.46, rows[i], 0.70, rows[i]));
    }
    return v;
}

//路由器：机身、顶面、两根天线、顶面上的四向箭头
std::vector<AnnotIconPrim> router()
{
    std::vector<AnnotIconPrim> v;
    v.push_back(roundRect(0.06, 0.42, 0.94, 0.78, annot_icon_fill_tint, kMediumRadius));
    v.push_back(ellipse(0.06, 0.30, 0.94, 0.54, annot_icon_fill_tint));
    v.push_back(line(0.22, 0.36, 0.22, 0.08));
    v.push_back(line(0.78, 0.36, 0.78, 0.08));
    //向左右的箭头
    v.push_back(line(0.30, 0.42, 0.70, 0.42));
    v.push_back(polyline3(0.36, 0.38, 0.30, 0.42, 0.36, 0.46));
    v.push_back(polyline3(0.64, 0.38, 0.70, 0.42, 0.64, 0.46));
    //机身前面的指示灯
    v.push_back(ellipse(0.20, 0.62, 0.26, 0.68, annot_icon_fill_solid));
    v.push_back(ellipse(0.32, 0.62, 0.38, 0.68, annot_icon_fill_solid));
    return v;
}

//交换机：扁平机身、一排网口、上方两个反向的箭头
std::vector<AnnotIconPrim> networkSwitch()
{
    std::vector<AnnotIconPrim> v;
    v.push_back(roundRect(0.04, 0.42, 0.96, 0.74, annot_icon_fill_tint, kSmallRadius));
    const double ports[] = { 0.12, 0.25, 0.38, 0.51, 0.64, 0.77 };
    for (size_t i = 0; i < sizeof(ports) / sizeof(ports[0]); ++i)
        v.push_back(rect(ports[i], 0.56, ports[i] + 0.09, 0.66, annot_icon_fill_solid));
    v.push_back(line(0.26, 0.30, 0.74, 0.30));
    v.push_back(polyline3(0.66, 0.24, 0.74, 0.30, 0.66, 0.36));
    v.push_back(line(0.74, 0.14, 0.26, 0.14));
    v.push_back(polyline3(0.34, 0.08, 0.26, 0.14, 0.34, 0.20));
    return v;
}

//防火墙：一面砖墙，四行砖错缝
std::vector<AnnotIconPrim> firewall()
{
    std::vector<AnnotIconPrim> v;
    v.push_back(rect(0.08, 0.16, 0.92, 0.84, annot_icon_fill_tint));
    const double rows[] = { 0.33, 0.50, 0.67 };
    for (size_t i = 0; i < sizeof(rows) / sizeof(rows[0]); ++i)
        v.push_back(line(0.08, rows[i], 0.92, rows[i]));
    //奇数行的竖缝在三分点，偶数行的在四分点
    v.push_back(line(0.36, 0.16, 0.36, 0.33));
    v.push_back(line(0.64, 0.16, 0.64, 0.33));
    v.push_back(line(0.22, 0.33, 0.22, 0.50));
    v.push_back(line(0.50, 0.33, 0.50, 0.50));
    v.push_back(line(0.78, 0.33, 0.78, 0.50));
    v.push_back(line(0.36, 0.50, 0.36, 0.67));
    v.push_back(line(0.64, 0.50, 0.64, 0.67));
    v.push_back(line(0.22, 0.67, 0.22, 0.84));
    v.push_back(line(0.50, 0.67, 0.50, 0.84));
    v.push_back(line(0.78, 0.67, 0.78, 0.84));
    return v;
}

//无线 AP：底座、指示灯、上方三道信号弧
std::vector<AnnotIconPrim> wirelessAp()
{
    std::vector<AnnotIconPrim> v;
    v.push_back(roundRect(0.16, 0.66, 0.84, 0.88, annot_icon_fill_tint, kMediumRadius));
    v.push_back(ellipse(0.46, 0.74, 0.54, 0.80, annot_icon_fill_solid));
    //信号弧：圆心在底座上方，朝上张开 90 度
    const double cx = 0.5;
    const double cy = 0.60;
    const double radii[] = { 0.14, 0.27, 0.40 };
    for (size_t i = 0; i < sizeof(radii) / sizeof(radii[0]); ++i)
        v.push_back(arcLine(cx, cy, radii[i], radii[i], -0.75 * kPi, -0.25 * kPi));
    return v;
}

//互联网：一朵云
std::vector<AnnotIconPrim> internet()
{
    std::vector<AnnotIconPrim> v;
    AnnotIconPrim cloud;
    cloud.m_kind = annot_prim_polygon;
    cloud.m_points = AnnotShapeOutline(annot_tool_cloud, GeoPoint(0.04, 0.20), GeoPoint(0.96, 0.80));
    cloud.m_fill = annot_icon_fill_tint;
    v.push_back(cloud);
    //云里的经纬线，表示「网络」
    v.push_back(ellipse(0.36, 0.34, 0.64, 0.70, annot_icon_fill_none));
    v.push_back(line(0.36, 0.52, 0.64, 0.52));
    v.push_back(line(0.50, 0.34, 0.50, 0.70));
    return v;
}

//数据库：圆柱（顶面、柱身、两道腰线）
std::vector<AnnotIconPrim> database()
{
    std::vector<AnnotIconPrim> v;
    const double left = 0.16;
    const double right = 0.84;
    const double cx = 0.5;
    const double rx = 0.34;
    const double ry = 0.10;
    const double top = 0.16;
    const double bottom = 0.84;
    //柱身：左右两条边加底部的半个椭圆
    AnnotIconPrim body;
    body.m_kind = annot_prim_polygon;
    body.m_points.push_back(GeoPoint(right, top));
    body.m_points.push_back(GeoPoint(left, top));
    const std::vector<GeoPoint> lower = arc(cx, bottom, rx, ry, kPi, 0);
    body.m_points.insert(body.m_points.end(), lower.begin(), lower.end());
    body.m_fill = annot_icon_fill_tint;
    v.push_back(body);
    v.push_back(ellipse(left, top - ry, right, top + ry, annot_icon_fill_tint));
    v.push_back(arcLine(cx, 0.39, rx, ry, kPi, 0));
    v.push_back(arcLine(cx, 0.62, rx, ry, kPi, 0));
    return v;
}

//手机：机身、屏幕、听筒、按键
std::vector<AnnotIconPrim> phone()
{
    std::vector<AnnotIconPrim> v;
    v.push_back(roundRect(0.30, 0.04, 0.70, 0.96, annot_icon_fill_tint, kMediumRadius));
    v.push_back(rect(0.35, 0.14, 0.65, 0.80, annot_icon_fill_none));
    v.push_back(line(0.45, 0.09, 0.55, 0.09));
    v.push_back(ellipse(0.47, 0.84, 0.53, 0.90, annot_icon_fill_solid));
    return v;
}

//打印机：进纸、机身、出纸、纸上的字行、指示灯
std::vector<AnnotIconPrim> printer()
{
    std::vector<AnnotIconPrim> v;
    v.push_back(rect(0.26, 0.08, 0.74, 0.38, annot_icon_fill_none));
    v.push_back(roundRect(0.06, 0.36, 0.94, 0.74, annot_icon_fill_tint, kSmallRadius));
    v.push_back(rect(0.26, 0.62, 0.74, 0.94, annot_icon_fill_none));
    v.push_back(line(0.34, 0.74, 0.66, 0.74));
    v.push_back(line(0.34, 0.84, 0.60, 0.84));
    v.push_back(ellipse(0.80, 0.44, 0.86, 0.50, annot_icon_fill_solid));
    return v;
}

} // namespace

AnnotIconPrim::AnnotIconPrim()
    : m_kind(annot_prim_polyline)
    , m_fill(annot_icon_fill_none)
    , m_radius(0)
{
}

std::vector<AnnotIconPrim> AnnotIconPrimitives(int icon)
{
    switch (icon)
    {
    //电脑
    case annot_icon_computer:
        return computer();
    //笔记本
    case annot_icon_laptop:
        return laptop();
    //服务器
    case annot_icon_server:
        return server();
    //路由器
    case annot_icon_router:
        return router();
    //交换机
    case annot_icon_switch:
        return networkSwitch();
    //防火墙
    case annot_icon_firewall:
        return firewall();
    //无线 AP
    case annot_icon_ap:
        return wirelessAp();
    //互联网
    case annot_icon_internet:
        return internet();
    //数据库
    case annot_icon_database:
        return database();
    //手机
    case annot_icon_phone:
        return phone();
    //打印机
    case annot_icon_printer:
        return printer();
    //非法种类
    default:
        return std::vector<AnnotIconPrim>();
    }
}

} // namespace screenshare
