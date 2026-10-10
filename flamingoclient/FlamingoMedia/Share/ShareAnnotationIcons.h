/**
 *  屏幕共享标注的网络拓扑图元（第 20 期，docs/屏幕共享与录制方案.md 第 2 节第 37 条），ShareAnnotationIcons.h
 *
 *  十一种图元（电脑、笔记本、服务器、路由器、交换机、防火墙、无线 AP、互联网、数据库、手机、打印机）都是矢量图：每种由若干
 *  简单图形组成（折线、多边形、椭圆、圆角矩形），坐标在边长为 1 的单位方框里（左上为 0、右下为 1，向下为正）。绘制时等比
 *  放进图元的外接矩形、居中，描边用图元的颜色与线宽，「浅色填充」用同一颜色的低不透明度，「实心」用同一颜色填满。
 *  本文件只有纯函数，单元测试见 Source/Tests/ShareObjectTests.cpp（SSOB10）。
 *
 *  典型用法：
 *      std::vector<AnnotIconPrim> prims = AnnotIconPrimitives(annot_icon_router);
 *      for (...) 把 prims[i].m_points 换算到外接矩形里再画;
 *
 *  balloonwj@qq.com   2026-10-05
 */
#pragma once

#include <vector>
#include "ShareAnnotationGeometry.h"

namespace screenshare {

//图元里一个简单图形的种类
enum AnnotIconPrimKind
{
    annot_prim_polyline   = 0,   //折线（不闭合）：m_points 为各点
    annot_prim_polygon    = 1,   //多边形（闭合，首尾不重复）：m_points 为各顶点
    annot_prim_ellipse    = 2,   //椭圆：m_points 为外接矩形的左上、右下两点
    annot_prim_round_rect = 3,   //圆角矩形：m_points 为左上、右下两点，圆角半径为 m_radius
};

//图元里一个简单图形的填充方式
enum AnnotIconFill
{
    annot_icon_fill_none  = 0,   //只描边
    annot_icon_fill_tint  = 1,   //浅色填充再描边
    annot_icon_fill_solid = 2,   //实心填满，不描边（指示灯、按钮等小细节）
};

//图元里的一个简单图形
struct AnnotIconPrim
{
    int                   m_kind;     //种类，取值见 AnnotIconPrimKind
    std::vector<GeoPoint> m_points;   //点（单位方框里的坐标）
    int                   m_fill;     //填充方式，取值见 AnnotIconFill；折线只描边
    double                m_radius;   //圆角矩形的圆角半径（单位方框里的长度），其余种类为 0

    AnnotIconPrim();
};

//「浅色填充」的不透明度（0~255）
const int kAnnotIconTintAlpha = 48;

/**
 *  某种图元的矢量图形，按绘制的先后排列（后画的在上面）。
 *  @param icon 图元种类，取值见 AnnotationIcon
 *  @return 简单图形；种类非法时返回空
 */
std::vector<AnnotIconPrim> AnnotIconPrimitives(int icon);

} // namespace screenshare
