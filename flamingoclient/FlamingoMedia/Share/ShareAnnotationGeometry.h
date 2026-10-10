/**
 *  屏幕共享画板的几何计算（第 19 期，docs/屏幕共享与录制方案.md 第 2 节第 34～36、38 条），ShareAnnotationGeometry.h
 *
 *  绘制、命中测试共用的纯函数，全部用像素坐标的浮点点（GeoPoint），不调用任何绘图 API：
 *    - 形状的轮廓：矩形、圆角矩形、椭圆、三角形、菱形、五边形、六边形、五角星、平行四边形、对话气泡、云形；按住 Shift 画正形；
 *    - 箭头走向（直线、直角折线、弧形）与两端样式（实心 / 空心三角、开口 V 形、燕尾、实心 / 空心圆、菱形、竖线）；
 *    - 块状箭头（右、左右、上下、弯折、U 形掉头、环形循环），可旋转；
 *    - 穿过各点的平滑曲线、虚线样式、数字标号的文字（1 2 3 / A B C / 一 二 三）、毛笔随运笔速度变化的线宽。
 *  单元测试见 Source/Tests/ShareDrawTests.cpp（标签 [sharedraw]）。
 *
 *  典型用法：
 *      std::vector<GeoPoint> outline = AnnotShapeOutline(annot_tool_star, GeoPoint(10, 10), GeoPoint(110, 90));
 *      AnnotCapShape cap = AnnotCapGeometry(annot_cap_triangle, tip, dir, 12.0);
 *
 *  balloonwj@qq.com   2026-10-05
 */
#pragma once

#include <string>
#include <vector>

namespace screenshare {

//像素坐标的点（浮点，便于旋转与插值）
struct GeoPoint
{
    double m_x;   //横坐标，像素
    double m_y;   //纵坐标，像素（向下为正）

    GeoPoint();
    GeoPoint(double x, double y);
};

//平滑曲线每段采样的点数（两个控制点之间）
const int kAnnotCurveStepsPerSegment = 12;
//椭圆、云形轮廓的采样点数
const int kAnnotEllipseSegments = 72;
//端点圆形的采样点数
const int kAnnotCapCircleSegments = 24;
//弧形箭头的采样段数
const int kAnnotArcSegments = 24;

/**
 *  形状的轮廓多边形（闭合，首尾不重复）。
 *  @param tool 形状种类：annot_tool_rect、annot_tool_round_rect、annot_tool_ellipse、annot_tool_triangle、annot_tool_diamond、
 *              annot_tool_pentagon、annot_tool_hexagon、annot_tool_star、annot_tool_parallelogram、annot_tool_bubble、
 *              annot_tool_cloud
 *  @param a、b 外接矩形的两个对角（顺序任意）
 *  @return 轮廓点，外接矩形与 a、b 张成的矩形一致；不是形状种类或矩形为空时返回空
 */
std::vector<GeoPoint> AnnotShapeOutline(int tool, GeoPoint a, GeoPoint b);

/**
 *  正形（按住 Shift）：以 a 为起点，把 b 调成宽高相等的对角（取较长的一边，保持拖动的方向）。
 *  @return 调整后的对角
 */
GeoPoint AnnotRegularCorner(GeoPoint a, GeoPoint b);

/**
 *  形状画成「正形」时外接矩形应有的宽高比（宽 ÷ 高，显示上的像素比例，2026-10-07）：矩形、圆角矩形、椭圆、菱形、平行四边形、
 *  对话气泡、云形、块状箭头、图元为 1（正方形外接框）；三角形为正三角形的外接框（宽 ÷ 高 = 2 / √3）；五边形、五角星、
 *  六边形为对应正多边形（AnnotShapeOutline 里的朝向）的外接框。外接框按这个比例时，AnnotShapeOutline 贴满外接框后就是正形。
 *  @param tool 形状种类
 *  @return 宽高比；不是形状时为 1
 */
double AnnotRegularShapeAspect(int tool);

/**
 *  按住 Shift 拖动时的终点约束（2026-10-07，纯函数）：在「等长坐标」（横坐标乘以内容宽高比，使横竖一个单位在屏幕上一样长）里计算，
 *  再换回归一化坐标：
 *    - 直线、箭头、连接线：方向吸附到 45 度的整数倍，长度不变；
 *    - 各种形状：外接矩形按 AnnotRegularShapeAspect 取正形（取拖动范围里较长的一边，保持拖动方向）；
 *  终点超出 0 ~ coordMax 时，整段拖动按同一比例缩短，保持角度与正形，不单独截断某一个方向。
 *  @param tool     工具种类（AnnotationTool）；不是上面两类时原样返回 to
 *  @param from     起点（归一化坐标）
 *  @param to       鼠标当前位置（归一化坐标）
 *  @param aspect   共享内容显示的宽高比（宽 ÷ 高）；不大于 0 时按 1 处理
 *  @param coordMax 归一化坐标的最大值（kAnnotCoordMax）
 *  @return 约束后的终点（归一化坐标，未取整）
 */
GeoPoint AnnotConstrainDrag(int tool, GeoPoint from, GeoPoint to, double aspect, double coordMax);

/**
 *  箭头走向的路径。
 *  @param routing 取值见 AnnotationRouting：直线为两点；直角折线为三点（先横后竖）；弧形为 kAnnotArcSegments + 1 个点，
 *                 向线段左侧（按从 from 到 to 的方向）拱起，拱高为线段长的四分之一
 *  @return 路径点，首点为 from、尾点为 to
 */
std::vector<GeoPoint> AnnotArrowPath(int routing, GeoPoint from, GeoPoint to);

//线条一端的图形
struct AnnotCapShape
{
    std::vector<GeoPoint> m_points;   //图形的点：闭合图形为多边形（首尾不重复），不闭合时为折线
    bool                  m_closed;   //是否闭合
    bool                  m_filled;   //是否填充（实心三角、燕尾、实心圆、菱形）
    double                m_inset;    //线条在这一端要缩短的长度（像素），免得粗线头伸进箭头或空心图形里

    AnnotCapShape();
};

/**
 *  线条一端的样式图形。
 *  @param cap  样式，取值见 AnnotationCap
 *  @param tip  端点
 *  @param dir  线条到达端点时的方向（单位向量，从线内指向端点外侧）
 *  @param size 图形大小（像素），通常为线宽的若干倍
 *  @return 图形；annot_cap_none 或取值非法时点为空
 */
AnnotCapShape AnnotCapGeometry(int cap, GeoPoint tip, GeoPoint dir, double size);

/**
 *  块状箭头的轮廓多边形。
 *  @param variant     样式，取值见 AnnotationBlockArrow
 *  @param a、b        外接矩形的两个对角（旋转之前）
 *  @param rotationDeg 绕外接矩形中心顺时针旋转的角度（度）
 *  @return 轮廓点；样式非法或矩形为空时返回空
 */
std::vector<GeoPoint> AnnotBlockArrowOutline(int variant, GeoPoint a, GeoPoint b, int rotationDeg);

/**
 *  穿过各点的平滑曲线（Catmull-Rom 样条采样），曲线种类与折线种类的预览共用。
 *  @param pts 控制点；少于 3 个时原样返回
 *  @return 采样点，经过每一个控制点（首尾相同）
 */
std::vector<GeoPoint> AnnotSmoothCurve(const std::vector<GeoPoint>& pts);

/**
 *  线型的虚线模式：实线段、空白段交替的长度，以线宽为单位（交给绘图库的虚线设置）。
 *  @param lineStyle 取值见 AnnotationLineStyle
 *  @return 模式；实线或取值非法时为空
 */
std::vector<float> AnnotDashPattern(int lineStyle);

/**
 *  数字标号上显示的文字。
 *  @param sequence 写法，取值见 AnnotationSequence：阿拉伯数字；字母（26 之后为 AA、AB…）；中文数字（10000 以内，
 *                  如十一、二十、一百零一，更大时用阿拉伯数字）
 *  @param number   号码；不大于 0 时返回空串
 */
std::wstring AnnotBadgeLabel(int sequence, int number);

/**
 *  毛笔每个点的线宽：相邻点离得越远（运笔越快）越细，越近越粗，并与前一点平滑过渡。
 *  @param pts       笔迹的点（像素）
 *  @param baseWidth 基准线宽（像素）
 *  @return 与 pts 一一对应的线宽，在 0.6～1.4 倍基准线宽之间
 */
std::vector<double> AnnotBrushWidths(const std::vector<GeoPoint>& pts, double baseWidth);

} // namespace screenshare
