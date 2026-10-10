/**
 *  屏幕共享画板工具的单元测试（Suite SSDW，标签 [sharedraw]），ShareDrawTests.cpp
 *
 *  覆盖 docs/屏幕共享与录制方案.md 第 2 节第 33～36、38 条与第 8 节第 19 期：
 *    - SSDW1 笔迹样式块：begin / text / full 带上样式块后往返不变，新种类可以编解码；老版本没有样式块的操作按默认样式解出，
 *            老箭头的终点为开口 V 形；样式块比当前版本长时多出的字节跳过，样式块不完整、种类越界时整条拒绝；
 *    - SSDW2 画笔位置：编解码；模型按座位记下别人的位置、工具与颜色，离开时删掉，超过 kAnnotPointerStaleMs 没更新的清掉；
 *            观看者没有标注权限时不接受；
 *    - SSDW3 激光笔：画完之后在 kAnnotLaserFadeMs 内淡出，过了之后从模型里删掉，不进整笔补发；其余种类不受影响；
 *    - SSDW4 模型与样式：整笔补发时样式不同也算变化；数字标号一个点即可开始；文字带加粗与背景；
 *    - SSDW5～8 几何：形状轮廓与正形、箭头走向与两端样式、块状箭头与旋转、标号文字 / 平滑曲线 / 虚线样式 / 毛笔线宽；
 *    - SSDW9～11 图层绘制：填充三种、实线与虚线、局部橡皮、箭头三角、激光笔淡出、标号与文字背景、颜色键窗口的有序抖动；
 *    - SSDW12～16 本人的标注会话：新的自由线条工具与粗细档位、激光笔淡出；两点图形带样式与 Shift 正形；折线与曲线逐点添加；
 *            数字标号自动递增与从头编号、文字样式；画笔位置的广播；
 *    - SSDW17 工具光标图像与别人画笔位置的圆点、名字标签；
 *    - SSDW18～19 工具条的下拉菜单模型：各组的项数与打勾、菜单项 id 换成命令、工具归组、按钮文字；
 *    - SSDW21 按住 Shift 的正形（2026-10-07）：16:9 的内容上，矩形、椭圆在显示上是正方形外接框，五角星、五边形、六边形、
 *            三角形各边等长；直线、箭头、连接线吸附到显示上的 45 度；拖出画面时整段按比例缩短、仍是正形；
 *    - SSDW22 拖动中按下、松开 Shift（鼠标不动）：预览的终点随之变成正形、再恢复，SetShiftDown 报告变化；
 *    - SSDW23 选择工具的光标依据（2026-10-07）：ObjectAt 在画好的矩形边上返回 true，远处返回 false。
 *  纯逻辑，不建窗口。框架 Catch2 v3。
 *
 *  典型用法：FlamingoUnit.exe "[sharedraw]"
 *
 *  balloonwj@qq.com   2026-10-05
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <math.h>
#include <algorithm>
#include <string>
#include <vector>
#include "../../FlamingoMedia/Share/ShareAnnotation.h"
#include "../../FlamingoMedia/Share/ShareAnnotationGeometry.h"
#include "../../FlamingoMedia/Share/ShareAnnotationMenus.h"
#include "../../FlamingoMedia/Share/ShareAnnotationRender.h"
#include "../../FlamingoMedia/Share/ShareAnnotationSession.h"
#include "../../FlamingoMedia/Share/ShareFraming.h"

using namespace screenshare;

namespace {

//几何比较的容差（像素）
const double kSsdwEps = 0.01;
//宽高比、边长之比的容差（归一化坐标取整到整数后的误差）
const double kSsdwRatioEps = 0.01;
//正形用例（SSDW21）里共享内容的显示尺寸（像素）：16:9
const double kSsdwViewW = 1600;
const double kSsdwViewH = 900;
//归一化坐标的最大值（kAnnotCoordMax 的浮点形式）
const double kSsdwCoordMax = static_cast<double>(kAnnotCoordMax);

//点集的外接矩形
void ssdwBounds(const std::vector<GeoPoint>& pts, double& l, double& t, double& r, double& b)
{
    l = t = 1e9;
    r = b = -1e9;
    for (size_t i = 0; i < pts.size(); ++i)
    {
        l = pts[i].m_x < l ? pts[i].m_x : l;
        t = pts[i].m_y < t ? pts[i].m_y : t;
        r = pts[i].m_x > r ? pts[i].m_x : r;
        b = pts[i].m_y > b ? pts[i].m_y : b;
    }
}

//图层里某像素的 alpha
int ssdwAlpha(const std::vector<uint8_t>& bgra, int width, int x, int y)
{
    return bgra[(static_cast<size_t>(y) * width + x) * 4 + 3];
}

//造一笔：种类、颜色、线宽与两个点（归一化坐标）
AnnotationStroke ssdwStroke(int tool, uint32_t color, int width, int x0, int y0, int x1, int y1)
{
    AnnotationStroke s;
    s.m_author = 0;
    s.m_id = 1;
    s.m_tool = tool;
    s.m_color = color;
    s.m_width = width;
    s.m_points.push_back(AnnotationPoint(static_cast<uint16_t>(x0), static_cast<uint16_t>(y0)));
    s.m_points.push_back(AnnotationPoint(static_cast<uint16_t>(x1), static_cast<uint16_t>(y1)));
    s.m_finished = true;
    return s;
}

//测试用的座位号：发起人 0、观看者 3
const int kSsdwHost = 0;
const int kSsdwViewer = 3;

//造一份各字段都不是默认值的样式
AnnotationStyle ssdwStyle()
{
    AnnotationStyle s;
    s.m_lineStyle = annot_line_dash_dot;
    s.m_startCap = annot_cap_circle_hollow;
    s.m_endCap = annot_cap_swallowtail;
    s.m_routing = annot_route_arc;
    s.m_fill = annot_fill_translucent;
    s.m_rotation = 275;
    s.m_variant = annot_block_u_turn;
    s.m_sequence = annot_seq_chinese;
    s.m_number = 4321;
    s.m_bold = true;
    s.m_textBg = annot_text_bg_fill;
    s.m_bgColor = 0x12AB34;
    return s;
}

//造一条 begin
AnnotationMessage ssdwBegin(uint32_t id, int tool)
{
    AnnotationMessage m;
    m.m_op = annot_op_begin;
    m.m_id = id;
    m.m_tool = tool;
    m.m_color = 0x1E88E5;
    m.m_width = 6;
    m.m_points.push_back(AnnotationPoint(100, 200));
    m.m_points.push_back(AnnotationPoint(3000, 4000));
    return m;
}

//编码后再解码，解码失败时让用例失败
AnnotationMessage ssdwRoundTrip(const AnnotationMessage& m)
{
    const std::string payload = ShareAnnotationCodec::encode(m);
    REQUIRE_FALSE(payload.empty());
    AnnotationMessage out;
    REQUIRE(ShareAnnotationCodec::decode(payload, out));
    return out;
}

//老版本的 begin 载荷：[1][编号 4B][种类][颜色 3B][线宽][点数 2B][点…]，没有样式块
std::string ssdwLegacyBegin(uint32_t id, int tool)
{
    std::string p;
    p.push_back(static_cast<char>(annot_op_begin));
    p.push_back(static_cast<char>((id >> 24) & 0xFF));
    p.push_back(static_cast<char>((id >> 16) & 0xFF));
    p.push_back(static_cast<char>((id >> 8) & 0xFF));
    p.push_back(static_cast<char>(id & 0xFF));
    p.push_back(static_cast<char>(tool));
    p.push_back(static_cast<char>(0xE5));
    p.push_back(static_cast<char>(0x39));
    p.push_back(static_cast<char>(0x35));
    p.push_back(static_cast<char>(4));
    p.push_back(static_cast<char>(0));
    p.push_back(static_cast<char>(2));
    const unsigned char pts[] = { 0, 10, 0, 20, 1, 0, 2, 0 };
    p.append(reinterpret_cast<const char*>(pts), sizeof(pts));
    return p;
}

} // namespace

TEST_CASE("SSDW1 笔迹样式块：往返不变，新种类可编解码；老载荷按默认样式解出，超长样式块跳过多余字节，不完整时拒绝", "[sharedraw]")
{
    //begin：块状箭头带全部样式
    AnnotationMessage begin = ssdwBegin(7, annot_tool_block_arrow);
    begin.m_style = ssdwStyle();
    AnnotationMessage got = ssdwRoundTrip(begin);
    CHECK(got.m_tool == annot_tool_block_arrow);
    CHECK(got.m_hasStyle);
    CHECK(got.m_style == ssdwStyle());

    //text：加粗、填充背景
    AnnotationMessage text;
    text.m_op = annot_op_text;
    text.m_id = 8;
    text.m_color = 0x000000;
    text.m_width = 40;
    text.m_points.push_back(AnnotationPoint(500, 600));
    text.m_text = "hello";
    text.m_style.m_bold = true;
    text.m_style.m_textBg = annot_text_bg_white;
    got = ssdwRoundTrip(text);
    CHECK(got.m_text == "hello");
    CHECK(got.m_style.m_bold);
    CHECK(got.m_style.m_textBg == annot_text_bg_white);

    //full：数字标号
    AnnotationMessage full;
    full.m_op = annot_op_full;
    full.m_author = kSsdwViewer;
    full.m_id = 9;
    full.m_tool = annot_tool_badge;
    full.m_color = 0xFB8C00;
    full.m_width = 30;
    full.m_finished = true;
    full.m_points.push_back(AnnotationPoint(1000, 1000));
    full.m_style.m_variant = annot_badge_pin;
    full.m_style.m_sequence = annot_seq_letter;
    full.m_style.m_number = 28;
    got = ssdwRoundTrip(full);
    CHECK(got.m_tool == annot_tool_badge);
    CHECK(got.m_style.m_variant == annot_badge_pin);
    CHECK(got.m_style.m_sequence == annot_seq_letter);
    CHECK(got.m_style.m_number == 28);

    //每一种新种类都能编解码（文字走 text 操作，不在 begin 里）
    for (int tool = annot_tool_pencil; tool <= kAnnotToolLast; ++tool)
    {
        INFO("tool " << tool);
        CHECK(ssdwRoundTrip(ssdwBegin(100 + tool, tool)).m_tool == tool);
    }
    //种类越界：拒绝
    AnnotationMessage bad;
    CHECK_FALSE(ShareAnnotationCodec::decode(ShareAnnotationCodec::encode(ssdwBegin(1, kAnnotToolLast + 1)), bad));

    //老版本的载荷：没有样式块，按默认样式；老箭头的终点为开口 V 形
    AnnotationMessage legacy;
    REQUIRE(ShareAnnotationCodec::decode(ssdwLegacyBegin(11, annot_tool_pen), legacy));
    CHECK_FALSE(legacy.m_hasStyle);
    CHECK(legacy.m_style == AnnotationStyle());
    REQUIRE(ShareAnnotationCodec::decode(ssdwLegacyBegin(12, annot_tool_arrow), legacy));
    CHECK(legacy.m_style.m_endCap == annot_cap_open);
    CHECK(legacy.m_style.m_startCap == annot_cap_none);

    //样式块比当前版本（第 22 期，kAnnotStyleBytesV3）长（以后的版本加了字段）：多出的字节跳过
    std::string longer = ShareAnnotationCodec::encode(begin);
    const size_t lenPos = longer.size() - kAnnotStyleBytesV3 - 1;
    REQUIRE(static_cast<unsigned char>(longer[lenPos]) == kAnnotStyleBytesV3);
    longer[lenPos] = static_cast<char>(kAnnotStyleBytesV3 + 3);
    longer.append(3, '\x7F');
    REQUIRE(ShareAnnotationCodec::decode(longer, got));
    CHECK(got.m_style == ssdwStyle());
    //样式块不完整：拒绝
    std::string truncated = ShareAnnotationCodec::encode(begin);
    truncated.resize(truncated.size() - 1);
    CHECK_FALSE(ShareAnnotationCodec::decode(truncated, got));
    //样式块长度比第 19 期的 kAnnotStyleBytes 还短：拒绝
    std::string shorter = ShareAnnotationCodec::encode(begin);
    shorter[lenPos] = static_cast<char>(kAnnotStyleBytes - 1);
    shorter.resize(lenPos + 1 + kAnnotStyleBytes - 1);
    CHECK_FALSE(ShareAnnotationCodec::decode(shorter, got));
}

TEST_CASE("SSDW2 画笔位置：编解码，模型按座位记下、离开删掉、超时清掉，没有权限时不接受", "[sharedraw]")
{
    AnnotationMessage p;
    p.m_op = annot_op_pointer;
    p.m_tool = annot_tool_marker;
    p.m_color = 0x43A047;
    p.m_points.push_back(AnnotationPoint(1234, 5678));
    AnnotationMessage got = ssdwRoundTrip(p);
    CHECK(got.m_op == annot_op_pointer);
    CHECK(got.m_tool == annot_tool_marker);
    CHECK(got.m_color == 0x43A047u);
    REQUIRE(got.m_points.size() == 1);
    CHECK(got.m_points[0].m_x == 1234);
    CHECK(got.m_points[0].m_y == 5678);
    AnnotationMessage leave;
    leave.m_op = annot_op_pointer_leave;
    CHECK(ssdwRoundTrip(leave).m_op == annot_op_pointer_leave);

    ShareAnnotationModel model;
    REQUIRE(model.Apply(kSsdwViewer, p, 1000));
    AnnotationMessage hostPtr = p;
    hostPtr.m_tool = annot_tool_pen;
    hostPtr.m_points[0] = AnnotationPoint(10, 20);
    REQUIRE(model.Apply(kSsdwHost, hostPtr, 1100));
    std::vector<AnnotationPointer> ptrs = model.Pointers();
    REQUIRE(ptrs.size() == 2);
    CHECK(ptrs[0].m_seat == kSsdwHost);
    CHECK(ptrs[0].m_tool == annot_tool_pen);
    CHECK(ptrs[1].m_seat == kSsdwViewer);
    CHECK(ptrs[1].m_pt.m_x == 1234);
    CHECK(ptrs[1].m_color == 0x43A047u);
    //同一个人再发：更新，不新增
    p.m_points[0] = AnnotationPoint(2000, 2000);
    REQUIRE(model.Apply(kSsdwViewer, p, 1200));
    ptrs = model.Pointers();
    REQUIRE(ptrs.size() == 2);
    CHECK(ptrs[1].m_pt.m_x == 2000);
    //离开：删掉
    REQUIRE(model.Apply(kSsdwViewer, leave, 1300));
    CHECK(model.Pointers().size() == 1);
    //超时：发起人的最后更新在 1100，过了 kAnnotPointerStaleMs 后清掉
    CHECK_FALSE(model.ExpireTransient(1100 + kAnnotPointerStaleMs - 1));
    CHECK(model.ExpireTransient(1100 + kAnnotPointerStaleMs));
    CHECK(model.Pointers().empty());
    //观看者没有标注权限：不接受
    model.SetViewersAllowed(false);
    CHECK_FALSE(model.Apply(kSsdwViewer, p, 5000));
    CHECK(model.Pointers().empty());
    //画笔位置不算笔迹
    CHECK(model.Strokes().empty());
}

TEST_CASE("SSDW3 激光笔：画完后淡出，淡完从模型里删掉、不进整笔补发；其余种类不受影响", "[sharedraw]")
{
    ShareAnnotationModel model;
    AnnotationMessage laser = ssdwBegin(21, annot_tool_laser);
    REQUIRE(model.Apply(kSsdwHost, laser, 1000));
    AnnotationMessage pen = ssdwBegin(22, annot_tool_pen);
    REQUIRE(model.Apply(kSsdwHost, pen, 1000));
    AnnotationMessage end;
    end.m_op = annot_op_end;
    end.m_id = 22;
    REQUIRE(model.Apply(kSsdwHost, end, 1050));
    REQUIRE(model.Strokes().size() == 2);

    //画的过程中一直是不透明
    CHECK(AnnotLaserAlpha(model.Strokes()[0], 9999) == 255);
    //整笔补发不带激光笔
    std::vector<AnnotationMessage> snap = model.Snapshot();
    REQUIRE(snap.size() == 1);
    CHECK(snap[0].m_id == 22u);

    //画完：记下时刻，之后线性淡出
    end.m_id = 21;
    REQUIRE(model.Apply(kSsdwHost, end, 2000));
    const AnnotationStroke& s = model.Strokes()[0];
    CHECK(s.m_finishedMs == 2000);
    CHECK(AnnotLaserAlpha(s, 2000) == 255);
    const int half = AnnotLaserAlpha(s, 2000 + kAnnotLaserFadeMs / 2);
    CHECK(half > 100);
    CHECK(half < 155);
    CHECK(AnnotLaserAlpha(s, 2000 + kAnnotLaserFadeMs) == 0);
    //画笔不淡出
    CHECK(AnnotLaserAlpha(model.Strokes()[1], 2000 + kAnnotLaserFadeMs) == 255);

    //淡完之前不删；淡完删掉激光笔，画笔保留
    CHECK_FALSE(model.ExpireTransient(2000 + kAnnotLaserFadeMs - 1));
    CHECK(model.ExpireTransient(2000 + kAnnotLaserFadeMs));
    REQUIRE(model.Strokes().size() == 1);
    CHECK(model.Strokes()[0].m_id == 22u);
}

TEST_CASE("SSDW4 模型与样式：整笔补发时样式不同也算变化；数字标号一个点即可；文字带加粗与背景", "[sharedraw]")
{
    ShareAnnotationModel model;
    AnnotationMessage full;
    full.m_op = annot_op_full;
    full.m_author = kSsdwViewer;
    full.m_id = 31;
    full.m_tool = annot_tool_star;
    full.m_color = 0x8E24AA;
    full.m_width = 4;
    full.m_finished = true;
    full.m_points.push_back(AnnotationPoint(10, 10));
    full.m_points.push_back(AnnotationPoint(500, 500));
    REQUIRE(model.Apply(kSsdwHost, full, 0));
    //同样内容再来一次：不算变化
    CHECK_FALSE(model.Apply(kSsdwHost, full, 0));
    //只改了填充：算变化，样式随之更新
    full.m_style.m_fill = annot_fill_solid;
    CHECK(model.Apply(kSsdwHost, full, 0));
    CHECK(model.Strokes()[0].m_style.m_fill == annot_fill_solid);

    //数字标号：一个点
    AnnotationMessage badge;
    badge.m_op = annot_op_begin;
    badge.m_id = 32;
    badge.m_tool = annot_tool_badge;
    badge.m_color = 0xE53935;
    badge.m_width = 30;
    badge.m_points.push_back(AnnotationPoint(300, 300));
    badge.m_style.m_number = 5;
    REQUIRE(model.Apply(kSsdwViewer, badge, 0));
    CHECK(model.Strokes()[1].m_style.m_number == 5);

    //文字：样式随文字保存
    AnnotationMessage text;
    text.m_op = annot_op_text;
    text.m_id = 33;
    text.m_color = 0x000000;
    text.m_width = 28;
    text.m_points.push_back(AnnotationPoint(50, 60));
    text.m_text = "abc";
    text.m_style.m_bold = true;
    text.m_style.m_textBg = annot_text_bg_fill;
    text.m_style.m_bgColor = 0xFFEB3B;
    REQUIRE(model.Apply(kSsdwViewer, text, 0));
    const AnnotationStroke& t = model.Strokes()[2];
    CHECK(t.m_style.m_bold);
    CHECK(t.m_style.m_textBg == annot_text_bg_fill);
    CHECK(t.m_style.m_bgColor == 0xFFEB3Bu);
    //整笔补发带上样式
    std::vector<AnnotationMessage> snap = model.Snapshot();
    REQUIRE(snap.size() == 3);
    CHECK(snap[2].m_style.m_bgColor == 0xFFEB3Bu);
    CHECK(snap[0].m_style.m_fill == annot_fill_solid);
}

TEST_CASE("SSDW5 形状轮廓：各形状落在外接矩形里并贴到四边，顶点数与特征点正确；按住 Shift 画正形", "[sharedraw]")
{
    const GeoPoint a(10, 20);
    const GeoPoint b(110, 80);
    const int shapes[] = { annot_tool_rect, annot_tool_round_rect, annot_tool_ellipse, annot_tool_triangle,
                           annot_tool_diamond, annot_tool_pentagon, annot_tool_hexagon, annot_tool_star,
                           annot_tool_parallelogram, annot_tool_bubble, annot_tool_cloud };
    for (size_t i = 0; i < sizeof(shapes) / sizeof(shapes[0]); ++i)
    {
        INFO("shape " << shapes[i]);
        //对角顺序反过来也一样
        const std::vector<GeoPoint> outline = AnnotShapeOutline(shapes[i], b, a);
        REQUIRE(outline.size() >= 3);
        double l = 0;
        double t = 0;
        double r = 0;
        double bt = 0;
        ssdwBounds(outline, l, t, r, bt);
        CHECK(fabs(l - 10) < 0.5);
        CHECK(fabs(t - 20) < 0.5);
        CHECK(fabs(r - 110) < 0.5);
        CHECK(fabs(bt - 80) < 0.5);
    }
    //顶点数：矩形 4、三角形 3、菱形 4、五边形 5、六边形 6、五角星 10、平行四边形 4
    CHECK(AnnotShapeOutline(annot_tool_rect, a, b).size() == 4);
    CHECK(AnnotShapeOutline(annot_tool_triangle, a, b).size() == 3);
    CHECK(AnnotShapeOutline(annot_tool_diamond, a, b).size() == 4);
    CHECK(AnnotShapeOutline(annot_tool_pentagon, a, b).size() == 5);
    CHECK(AnnotShapeOutline(annot_tool_hexagon, a, b).size() == 6);
    CHECK(AnnotShapeOutline(annot_tool_star, a, b).size() == 10);
    CHECK(AnnotShapeOutline(annot_tool_parallelogram, a, b).size() == 4);
    CHECK(AnnotShapeOutline(annot_tool_ellipse, a, b).size() == static_cast<size_t>(kAnnotEllipseSegments));
    //三角形的顶点在上边中点
    const std::vector<GeoPoint> tri = AnnotShapeOutline(annot_tool_triangle, a, b);
    CHECK(fabs(tri[0].m_x - 60) < kSsdwEps);
    CHECK(fabs(tri[0].m_y - 20) < kSsdwEps);
    //不是形状、矩形为空：空
    CHECK(AnnotShapeOutline(annot_tool_pen, a, b).empty());
    CHECK(AnnotShapeOutline(annot_tool_rect, a, a).empty());

    //正形：取较长的边，保持拖动方向
    GeoPoint c = AnnotRegularCorner(GeoPoint(10, 10), GeoPoint(50, 30));
    CHECK(fabs(c.m_x - 50) < kSsdwEps);
    CHECK(fabs(c.m_y - 50) < kSsdwEps);
    c = AnnotRegularCorner(GeoPoint(10, 10), GeoPoint(-20, 30));
    CHECK(fabs(c.m_x + 20) < kSsdwEps);
    CHECK(fabs(c.m_y - 40) < kSsdwEps);
}

TEST_CASE("SSDW6 箭头：直线两点、直角折线先横后竖、弧形向左侧拱起；两端样式的图形与缩短量", "[sharedraw]")
{
    const GeoPoint from(0, 0);
    const GeoPoint to(100, 40);
    std::vector<GeoPoint> p = AnnotArrowPath(annot_route_straight, from, to);
    REQUIRE(p.size() == 2);
    CHECK(fabs(p[1].m_x - 100) < kSsdwEps);
    p = AnnotArrowPath(annot_route_elbow, from, to);
    REQUIRE(p.size() == 3);
    CHECK(fabs(p[1].m_x - 100) < kSsdwEps);
    CHECK(fabs(p[1].m_y - 0) < kSsdwEps);
    //弧形：从 (0,0) 到 (100,0)，左侧是上方（y 为负），拱高约为线段长的四分之一
    p = AnnotArrowPath(annot_route_arc, GeoPoint(0, 0), GeoPoint(100, 0));
    REQUIRE(p.size() == static_cast<size_t>(kAnnotArcSegments + 1));
    CHECK(fabs(p.front().m_x) < kSsdwEps);
    CHECK(fabs(p.back().m_x - 100) < kSsdwEps);
    const GeoPoint mid = p[kAnnotArcSegments / 2];
    CHECK(mid.m_y < -10);
    CHECK(mid.m_y > -30);

    //两端样式：向右到达 (100,50)
    const GeoPoint tip(100, 50);
    const GeoPoint dir(1, 0);
    AnnotCapShape cap = AnnotCapGeometry(annot_cap_triangle, tip, dir, 10);
    REQUIRE(cap.m_points.size() == 3);
    CHECK(cap.m_closed);
    CHECK(cap.m_filled);
    CHECK(cap.m_inset > 0);
    //三个点都在端点左边（线内一侧），并且有一个就是端点
    bool hasTip = false;
    for (size_t i = 0; i < cap.m_points.size(); ++i)
    {
        CHECK(cap.m_points[i].m_x <= 100 + kSsdwEps);
        hasTip = hasTip || (fabs(cap.m_points[i].m_x - 100) < kSsdwEps && fabs(cap.m_points[i].m_y - 50) < kSsdwEps);
    }
    CHECK(hasTip);
    cap = AnnotCapGeometry(annot_cap_triangle_hollow, tip, dir, 10);
    CHECK(cap.m_closed);
    CHECK_FALSE(cap.m_filled);
    cap = AnnotCapGeometry(annot_cap_open, tip, dir, 10);
    CHECK_FALSE(cap.m_closed);
    CHECK(cap.m_points.size() == 3);
    cap = AnnotCapGeometry(annot_cap_swallowtail, tip, dir, 10);
    CHECK(cap.m_filled);
    CHECK(cap.m_points.size() == 4);
    //圆：以端点为圆心
    cap = AnnotCapGeometry(annot_cap_circle, tip, dir, 10);
    REQUIRE(cap.m_points.size() == static_cast<size_t>(kAnnotCapCircleSegments));
    double l = 0;
    double t = 0;
    double r = 0;
    double bt = 0;
    ssdwBounds(cap.m_points, l, t, r, bt);
    CHECK(fabs((l + r) / 2 - 100) < 0.2);
    CHECK(fabs((t + bt) / 2 - 50) < 0.2);
    CHECK(AnnotCapGeometry(annot_cap_circle_hollow, tip, dir, 10).m_filled == false);
    CHECK(AnnotCapGeometry(annot_cap_diamond, tip, dir, 10).m_points.size() == 4);
    //竖线：与线条垂直
    cap = AnnotCapGeometry(annot_cap_bar, tip, dir, 10);
    REQUIRE(cap.m_points.size() == 2);
    CHECK(fabs(cap.m_points[0].m_x - cap.m_points[1].m_x) < kSsdwEps);
    CHECK(AnnotCapGeometry(annot_cap_none, tip, dir, 10).m_points.empty());
}

TEST_CASE("SSDW7 块状箭头：六种样式都落在外接矩形里，右箭头的尖在右边中点，旋转 90 度后在下边中点", "[sharedraw]")
{
    const GeoPoint a(0, 0);
    const GeoPoint b(100, 60);
    for (int v = annot_block_right; v <= annot_block_circular; ++v)
    {
        INFO("variant " << v);
        const std::vector<GeoPoint> outline = AnnotBlockArrowOutline(v, a, b, 0);
        REQUIRE(outline.size() >= 7);
        double l = 0;
        double t = 0;
        double r = 0;
        double bt = 0;
        ssdwBounds(outline, l, t, r, bt);
        CHECK(l > -0.5);
        CHECK(t > -0.5);
        CHECK(r < 100.5);
        CHECK(bt < 60.5);
    }
    //右箭头的尖：最右边的点在右边中点
    std::vector<GeoPoint> right = AnnotBlockArrowOutline(annot_block_right, a, b, 0);
    size_t tipIndex = 0;
    for (size_t i = 1; i < right.size(); ++i)
    {
        if (right[i].m_x > right[tipIndex].m_x)
            tipIndex = i;
    }
    CHECK(fabs(right[tipIndex].m_x - 100) < kSsdwEps);
    CHECK(fabs(right[tipIndex].m_y - 30) < kSsdwEps);
    //顺时针转 90 度：尖到了中心正下方
    right = AnnotBlockArrowOutline(annot_block_right, a, b, 90);
    tipIndex = 0;
    for (size_t i = 1; i < right.size(); ++i)
    {
        if (right[i].m_y > right[tipIndex].m_y)
            tipIndex = i;
    }
    CHECK(fabs(right[tipIndex].m_x - 50) < 0.01);
    CHECK(fabs(right[tipIndex].m_y - 80) < 0.01);
    CHECK(AnnotBlockArrowOutline(99, a, b, 0).empty());
}

TEST_CASE("SSDW8 标号文字、平滑曲线、虚线样式、毛笔线宽", "[sharedraw]")
{
    CHECK(AnnotBadgeLabel(annot_seq_digit, 1) == L"1");
    CHECK(AnnotBadgeLabel(annot_seq_digit, 125) == L"125");
    CHECK(AnnotBadgeLabel(annot_seq_letter, 1) == L"A");
    CHECK(AnnotBadgeLabel(annot_seq_letter, 26) == L"Z");
    CHECK(AnnotBadgeLabel(annot_seq_letter, 27) == L"AA");
    CHECK(AnnotBadgeLabel(annot_seq_letter, 28) == L"AB");
    CHECK(AnnotBadgeLabel(annot_seq_letter, 52) == L"AZ");
    CHECK(AnnotBadgeLabel(annot_seq_letter, 53) == L"BA");
    CHECK(AnnotBadgeLabel(annot_seq_chinese, 1) == L"一");
    CHECK(AnnotBadgeLabel(annot_seq_chinese, 10) == L"十");
    CHECK(AnnotBadgeLabel(annot_seq_chinese, 11) == L"十一");
    CHECK(AnnotBadgeLabel(annot_seq_chinese, 20) == L"二十");
    CHECK(AnnotBadgeLabel(annot_seq_chinese, 99) == L"九十九");
    CHECK(AnnotBadgeLabel(annot_seq_chinese, 100) == L"一百");
    CHECK(AnnotBadgeLabel(annot_seq_chinese, 101) == L"一百零一");
    CHECK(AnnotBadgeLabel(annot_seq_chinese, 110) == L"一百一十");
    CHECK(AnnotBadgeLabel(annot_seq_chinese, 1001) == L"一千零一");
    CHECK(AnnotBadgeLabel(annot_seq_chinese, 12345) == L"12345");
    CHECK(AnnotBadgeLabel(annot_seq_digit, 0).empty());

    //平滑曲线经过每一个控制点
    std::vector<GeoPoint> ctrl;
    ctrl.push_back(GeoPoint(0, 0));
    ctrl.push_back(GeoPoint(50, 40));
    ctrl.push_back(GeoPoint(100, 0));
    ctrl.push_back(GeoPoint(150, 30));
    const std::vector<GeoPoint> curve = AnnotSmoothCurve(ctrl);
    REQUIRE(curve.size() == static_cast<size_t>((ctrl.size() - 1) * kAnnotCurveStepsPerSegment + 1));
    for (size_t i = 0; i < ctrl.size(); ++i)
    {
        const GeoPoint& c = curve[i * kAnnotCurveStepsPerSegment];
        CHECK(fabs(c.m_x - ctrl[i].m_x) < kSsdwEps);
        CHECK(fabs(c.m_y - ctrl[i].m_y) < kSsdwEps);
    }
    //少于三个点原样返回
    std::vector<GeoPoint> two(ctrl.begin(), ctrl.begin() + 2);
    CHECK(AnnotSmoothCurve(two).size() == 2);

    //虚线样式：实线为空，其余为实空交替（偶数个）
    CHECK(AnnotDashPattern(annot_line_solid).empty());
    CHECK(AnnotDashPattern(annot_line_dash).size() == 2);
    CHECK(AnnotDashPattern(annot_line_dot).size() == 2);
    CHECK(AnnotDashPattern(annot_line_dash_dot).size() == 4);
    CHECK(AnnotDashPattern(annot_line_dot)[0] < AnnotDashPattern(annot_line_dash)[0]);

    //毛笔：慢（点密）粗、快（点疏）细，都在 0.6～1.4 倍之间
    std::vector<GeoPoint> slow;
    std::vector<GeoPoint> fast;
    for (int i = 0; i < 20; ++i)
    {
        slow.push_back(GeoPoint(i * 1.0, 0));
        fast.push_back(GeoPoint(i * 40.0, 0));
    }
    const std::vector<double> ws = AnnotBrushWidths(slow, 10);
    const std::vector<double> wf = AnnotBrushWidths(fast, 10);
    REQUIRE(ws.size() == slow.size());
    REQUIRE(wf.size() == fast.size());
    CHECK(ws.back() > 11);
    CHECK(wf.back() < 9);
    for (size_t i = 0; i < ws.size(); ++i)
    {
        CHECK(ws[i] >= 6 - kSsdwEps);
        CHECK(ws[i] <= 14 + kSsdwEps);
        CHECK(wf[i] >= 6 - kSsdwEps);
        CHECK(wf[i] <= 14 + kSsdwEps);
    }
}

TEST_CASE("SSDW9 图层绘制：实心、空心、半透明填充，五角星实心时中心有颜色", "[sharedraw]")
{
    const int w = 200;
    const int h = 100;
    std::vector<uint8_t> layer;
    //实心矩形：中心不透明，颜色正确（BGRA）
    AnnotationStroke rect = ssdwStroke(annot_tool_rect, 0x2244CC, 20, 13107, 13107, 52428, 52428);
    rect.m_style.m_fill = annot_fill_solid;
    std::vector<AnnotationStroke> strokes(1, rect);
    RenderAnnotationLayer(w, h, strokes, NULL, 0, true, layer);
    REQUIRE(layer.size() == static_cast<size_t>(w * h * 4));
    CHECK(ssdwAlpha(layer, w, 100, 50) == 255);
    CHECK(layer[(50 * w + 100) * 4 + 0] == 0xCC);
    CHECK(layer[(50 * w + 100) * 4 + 2] == 0x22);
    //矩形外面透明
    CHECK(ssdwAlpha(layer, w, 5, 5) == 0);
    //空心：中心透明、边上有颜色
    strokes[0].m_style.m_fill = annot_fill_none;
    RenderAnnotationLayer(w, h, strokes, NULL, 0, true, layer);
    CHECK(ssdwAlpha(layer, w, 100, 50) == 0);
    CHECK(ssdwAlpha(layer, w, 100, 20) > 0);
    //半透明填充：中心半透明
    strokes[0].m_style.m_fill = annot_fill_translucent;
    RenderAnnotationLayer(w, h, strokes, NULL, 0, true, layer);
    CHECK(ssdwAlpha(layer, w, 100, 50) > 40);
    CHECK(ssdwAlpha(layer, w, 100, 50) < 200);
    //实心五角星：中心有颜色
    AnnotationStroke star = ssdwStroke(annot_tool_star, 0xFB8C00, 20, 13107, 6553, 52428, 58982);
    star.m_style.m_fill = annot_fill_solid;
    strokes.assign(1, star);
    RenderAnnotationLayer(w, h, strokes, NULL, 0, true, layer);
    CHECK(ssdwAlpha(layer, w, 100, 50) == 255);
    //预览（拖动中）同样画出来
    strokes.clear();
    RenderAnnotationLayer(w, h, strokes, &star, 0, true, layer);
    CHECK(ssdwAlpha(layer, w, 100, 50) == 255);
}

TEST_CASE("SSDW10 图层绘制：实线连续、虚线有间隔；局部橡皮擦掉它之前的像素；箭头的三角比线宽宽", "[sharedraw]")
{
    const int w = 200;
    const int h = 100;
    std::vector<uint8_t> layer;
    //实线：中线上一路都有颜色
    AnnotationStroke line = ssdwStroke(annot_tool_line, 0x000000, 40, 6553, 32767, 58982, 32767);
    std::vector<AnnotationStroke> strokes(1, line);
    RenderAnnotationLayer(w, h, strokes, NULL, 0, true, layer);
    bool gap = false;
    for (int x = 30; x <= 170; ++x)
        gap = gap || (ssdwAlpha(layer, w, x, 50) == 0);
    CHECK_FALSE(gap);
    //虚线：中线上有间隔
    strokes[0].m_style.m_lineStyle = annot_line_dash;
    RenderAnnotationLayer(w, h, strokes, NULL, 0, true, layer);
    gap = false;
    for (int x = 30; x <= 170; ++x)
        gap = gap || (ssdwAlpha(layer, w, x, 50) == 0);
    CHECK(gap);

    //局部橡皮：先画实心矩形，再沿中线擦一道，最后在中心画一个小实心矩形
    AnnotationStroke big = ssdwStroke(annot_tool_rect, 0x43A047, 10, 6553, 6553, 58982, 58982);
    big.m_style.m_fill = annot_fill_solid;
    AnnotationStroke eraser = ssdwStroke(annot_tool_eraser_area, 0, 100, 6553, 32767, 58982, 32767);
    AnnotationStroke inner = ssdwStroke(annot_tool_rect, 0xE53935, 10, 29490, 26214, 36044, 39321);
    inner.m_style.m_fill = annot_fill_solid;
    strokes.clear();
    strokes.push_back(big);
    strokes.push_back(eraser);
    strokes.push_back(inner);
    RenderAnnotationLayer(w, h, strokes, NULL, 0, true, layer);
    CHECK(ssdwAlpha(layer, w, 60, 50) == 0);
    CHECK(ssdwAlpha(layer, w, 60, 25) == 255);
    CHECK(ssdwAlpha(layer, w, 100, 50) == 255);

    //箭头：终点实心三角，三角处比线宽宽
    AnnotationStroke arrow = ssdwStroke(annot_tool_arrow, 0x1E88E5, 20, 6553, 32767, 58982, 32767);
    arrow.m_style.m_endCap = annot_cap_triangle;
    strokes.assign(1, arrow);
    RenderAnnotationLayer(w, h, strokes, NULL, 0, true, layer);
    CHECK(ssdwAlpha(layer, w, 173, 47) > 100);
    //线身处同样高度没有颜色
    CHECK(ssdwAlpha(layer, w, 100, 47) == 0);
}

TEST_CASE("SSDW11 图层绘制：激光笔淡出；标号与文字背景；颜色键窗口的有序抖动", "[sharedraw]")
{
    const int w = 200;
    const int h = 100;
    std::vector<uint8_t> layer;
    //激光笔：画完时刻 1000，之后按时间淡出
    AnnotationStroke laser = ssdwStroke(annot_tool_laser, 0xE53935, 40, 6553, 32767, 58982, 32767);
    laser.m_finishedMs = 1000;
    std::vector<AnnotationStroke> strokes(1, laser);
    RenderAnnotationLayer(w, h, strokes, NULL, 1000, true, layer);
    const int full = ssdwAlpha(layer, w, 100, 50);
    CHECK(full > 200);
    RenderAnnotationLayer(w, h, strokes, NULL, 1000 + kAnnotLaserFadeMs / 2, true, layer);
    const int half = ssdwAlpha(layer, w, 100, 50);
    CHECK(half > full / 2 - 40);
    CHECK(half < full / 2 + 40);
    RenderAnnotationLayer(w, h, strokes, NULL, 1000 + kAnnotLaserFadeMs, true, layer);
    CHECK(ssdwAlpha(layer, w, 100, 50) == 0);

    //实心圆标号：中心不透明，半径外透明
    AnnotationStroke badge;
    badge.m_tool = annot_tool_badge;
    badge.m_color = 0xE53935;
    badge.m_width = 30;
    badge.m_finished = true;
    badge.m_points.push_back(AnnotationPoint(32767, 32767));
    badge.m_style.m_variant = annot_badge_circle;
    badge.m_style.m_number = 7;
    strokes.assign(1, badge);
    RenderAnnotationLayer(w, h, strokes, NULL, 0, true, layer);
    CHECK(ssdwAlpha(layer, w, 100, 50) == 255);
    CHECK(ssdwAlpha(layer, w, 100 + 20, 50) == 0);

    //文字带填充背景：文字左上角外侧的内边距里是背景色
    AnnotationStroke text;
    text.m_tool = annot_tool_text;
    text.m_color = 0x000000;
    text.m_width = 200;
    text.m_finished = true;
    text.m_text = "Ab";
    text.m_points.push_back(AnnotationPoint(13107, 13107));
    text.m_style.m_textBg = annot_text_bg_fill;
    text.m_style.m_bgColor = 0xFFEB3B;
    strokes.assign(1, text);
    RenderAnnotationLayer(w, h, strokes, NULL, 0, true, layer);
    CHECK(ssdwAlpha(layer, w, 39, 19) == 255);
    CHECK(layer[(19 * w + 39) * 4 + 2] == 0xFF);
    CHECK(layer[(19 * w + 39) * 4 + 1] == 0xEB);

    //有序抖动：4×4 块全为不透明度 128 的灰色，抖动后一半全不透明、一半全透明，留下的像素去掉了预乘
    std::vector<uint8_t> block(4 * 4 * 4, 0);
    for (int i = 0; i < 16; ++i)
    {
        block[i * 4 + 0] = 64;
        block[i * 4 + 1] = 64;
        block[i * 4 + 2] = 64;
        block[i * 4 + 3] = 128;
    }
    AnnotDitherLayer(block, 4, 4);
    int opaque = 0;
    int clear = 0;
    for (int i = 0; i < 16; ++i)
    {
        if (block[i * 4 + 3] == 255)
        {
            ++opaque;
            CHECK(block[i * 4 + 0] >= 126);
            CHECK(block[i * 4 + 0] <= 129);
        }
        else if (block[i * 4 + 3] == 0 && block[i * 4 + 0] == 0)
        {
            ++clear;
        }
    }
    CHECK(opaque == 8);
    CHECK(clear == 8);
    //全不透明、全透明的像素不变
    std::vector<uint8_t> solid(4 * 4, 0);
    solid[0] = 10;
    solid[1] = 20;
    solid[2] = 30;
    solid[3] = 255;
    AnnotDitherLayer(solid, 2, 2);
    CHECK(solid[0] == 10);
    CHECK(solid[3] == 255);
    CHECK(solid[7] == 0);
}

namespace {

//记下发出的每个包体的发送通道
class SsdwTransport : public ShareAudioTransport
{
public:
    virtual bool SendRoomBody(const std::string& body) override
    {
        m_bodies.push_back(body);
        return true;
    }

    std::vector<std::string> m_bodies;   //发出的包体，按先后
};

//解出第 i 个包体里的标注操作
AnnotationMessage ssdwSent(const SsdwTransport& t, size_t i)
{
    REQUIRE(i < t.m_bodies.size());
    ShareFrameHeader header;
    const char* p = NULL;
    size_t len = 0;
    REQUIRE(ShareFraming::decodeBody(t.m_bodies[i].data(), t.m_bodies[i].size(), header, p, len));
    AnnotationMessage m;
    REQUIRE(ShareAnnotationCodec::decode(std::string(p, len), m));
    return m;
}

//归一化坐标的点
AnnotationPoint ssdwPt(int x, int y)
{
    return AnnotationPoint(static_cast<uint16_t>(x), static_cast<uint16_t>(y));
}

} // namespace

TEST_CASE("SSDW12 会话：新的自由线条工具边画边发，线宽按档位；激光笔画完后淡出，Tick 清掉", "[sharedraw]")
{
    const int tools[] = { annot_tool_pencil, annot_tool_marker, annot_tool_brush, annot_tool_laser,
                          annot_tool_eraser_area };
    for (size_t i = 0; i < sizeof(tools) / sizeof(tools[0]); ++i)
    {
        INFO("tool " << tools[i]);
        SsdwTransport t;
        ShareAnnotationSession s;
        s.Start(&t, kSsdwHost, 100);
        s.SetTool(tools[i]);
        s.PointerDown(ssdwPt(100, 100), 10, 0);
        s.PointerMove(ssdwPt(200, 200), 10, 100);
        s.PointerUp(ssdwPt(300, 300), 200);
        REQUIRE(t.m_bodies.size() >= 3);
        const AnnotationMessage begin = ssdwSent(t, 0);
        CHECK(begin.m_op == annot_op_begin);
        CHECK(begin.m_tool == tools[i]);
        CHECK(ssdwSent(t, t.m_bodies.size() - 1).m_op == annot_op_end);
        REQUIRE(s.Model().Strokes().size() == 1);
        CHECK(s.Model().Strokes()[0].m_points.size() == 3);
    }

    //粗细档位：细 < 中 < 粗；中档与第 6 期的默认值一致
    SsdwTransport t;
    ShareAnnotationSession s;
    s.Start(&t, kSsdwHost, 100);
    int widths[3] = { 0, 0, 0 };
    for (int level = annot_width_thin; level <= annot_width_thick; ++level)
    {
        s.SetWidthLevel(level);
        s.PointerDown(ssdwPt(10, 10), 10, 0);
        s.PointerUp(ssdwPt(20, 20), 10);
        widths[level] = s.Model().Strokes().back().m_width;
    }
    CHECK(widths[annot_width_thin] < widths[annot_width_medium]);
    CHECK(widths[annot_width_medium] < widths[annot_width_thick]);
    CHECK(widths[annot_width_medium] == kAnnotPenWidth);

    //激光笔：画完时刻记在模型里，淡完后 Tick 清掉
    ShareAnnotationSession laser;
    laser.Start(&t, kSsdwHost, 500);
    laser.SetTool(annot_tool_laser);
    laser.PointerDown(ssdwPt(100, 100), 10, 1000);
    laser.PointerUp(ssdwPt(500, 500), 1200);
    REQUIRE(laser.Model().Strokes().size() == 1);
    CHECK(laser.Model().Strokes()[0].m_finishedMs == 1200);
    CHECK_FALSE(laser.Tick(1200 + kAnnotLaserFadeMs - 1));
    CHECK(laser.Tick(1200 + kAnnotLaserFadeMs));
    CHECK(laser.Model().Strokes().empty());

    //别人的激光笔：带时刻收下，同样淡出
    ShareAnnotationSession other;
    other.Start(NULL, kSsdwViewer, 900);
    for (size_t i = 0; i < t.m_bodies.size(); ++i)
    {
        const AnnotationMessage m = ssdwSent(t, i);
        if (m.m_tool == annot_tool_laser || m.m_op == annot_op_end)
            other.OnRemoteFrame(kSsdwHost, ShareAnnotationCodec::encode(m), 5000);
    }
    bool hasLaser = false;
    for (size_t i = 0; i < other.Model().Strokes().size(); ++i)
        hasLaser = hasLaser || other.Model().Strokes()[i].m_tool == annot_tool_laser;
    CHECK(hasLaser);
    CHECK(other.Tick(5000 + kAnnotLaserFadeMs));
}

TEST_CASE("SSDW13 会话：直线、箭头、形状、块状箭头拖动后一次发出并带样式；按住 Shift 画正形、直线吸附 45 度", "[sharedraw]")
{
    SsdwTransport t;
    ShareAnnotationSession s;
    s.Start(&t, kSsdwHost, 100);
    //五角星，实心、点划线
    s.SetTool(annot_tool_star);
    s.SetFill(annot_fill_solid);
    s.SetLineStyle(annot_line_dash_dot);
    s.PointerDown(ssdwPt(1000, 1000), 10, 0);
    s.PointerMove(ssdwPt(5000, 3000), 10, 50);
    AnnotationStroke preview;
    REQUIRE(s.PendingShape(preview));
    CHECK(preview.m_tool == annot_tool_star);
    CHECK(preview.m_style.m_fill == annot_fill_solid);
    CHECK(t.m_bodies.empty());
    s.PointerUp(ssdwPt(5000, 3000), 100);
    REQUIRE(t.m_bodies.size() == 2);
    AnnotationMessage m = ssdwSent(t, 0);
    CHECK(m.m_tool == annot_tool_star);
    CHECK(m.m_style.m_fill == annot_fill_solid);
    CHECK(m.m_style.m_lineStyle == annot_line_dash_dot);
    REQUIRE(m.m_points.size() == 2);
    CHECK(m.m_points[1].m_x == 5000);

    //按住 Shift：五角星的外接框在显示上（横向乘以内容宽高比，会话默认 16:9）是正五角星的外接框（2026-10-07 起按宽高比换算，
    //原先在归一化坐标里取宽高相等，16:9 的内容上画出的是被拉宽的五角星）
    s.SetShiftDown(true);
    s.PointerDown(ssdwPt(1000, 1000), 10, 200);
    s.PointerUp(ssdwPt(5000, 3000), 300);
    m = ssdwSent(t, t.m_bodies.size() - 2);
    const double boxW = (m.m_points[1].m_x - m.m_points[0].m_x) * kAnnotDefaultAspect;
    const double boxH = m.m_points[1].m_y - m.m_points[0].m_y;
    REQUIRE(boxH > 0);
    CHECK(fabs(boxW / boxH - AnnotRegularShapeAspect(annot_tool_star)) < kSsdwRatioEps);

    //直线按住 Shift：吸附到水平（偏离很小时）
    s.SetTool(annot_tool_line);
    s.PointerDown(ssdwPt(1000, 1000), 10, 400);
    s.PointerUp(ssdwPt(9000, 1200), 500);
    m = ssdwSent(t, t.m_bodies.size() - 2);
    CHECK(m.m_tool == annot_tool_line);
    CHECK(m.m_points[1].m_y == 1000);
    s.SetShiftDown(false);

    //箭头：默认终点实心三角；改成双向燕尾、弧形
    s.SetTool(annot_tool_arrow);
    s.PointerDown(ssdwPt(100, 100), 10, 600);
    s.PointerUp(ssdwPt(900, 900), 700);
    m = ssdwSent(t, t.m_bodies.size() - 2);
    CHECK(m.m_style.m_endCap == annot_cap_triangle);
    CHECK(m.m_style.m_startCap == annot_cap_none);
    s.SetArrowCaps(annot_cap_swallowtail, annot_cap_swallowtail);
    s.SetRouting(annot_route_arc);
    s.PointerDown(ssdwPt(100, 100), 10, 800);
    s.PointerUp(ssdwPt(900, 900), 900);
    m = ssdwSent(t, t.m_bodies.size() - 2);
    CHECK(m.m_style.m_startCap == annot_cap_swallowtail);
    CHECK(m.m_style.m_routing == annot_route_arc);

    //块状箭头：带样式
    s.SetTool(annot_tool_block_arrow);
    s.SetBlockVariant(annot_block_circular);
    s.PointerDown(ssdwPt(100, 100), 10, 1000);
    s.PointerUp(ssdwPt(900, 900), 1100);
    m = ssdwSent(t, t.m_bodies.size() - 2);
    CHECK(m.m_tool == annot_tool_block_arrow);
    CHECK(m.m_style.m_variant == annot_block_circular);

    //起点终点重合（只是点了一下）：不发
    const size_t before = t.m_bodies.size();
    s.PointerDown(ssdwPt(100, 100), 10, 1200);
    s.PointerUp(ssdwPt(100, 100), 1300);
    CHECK(t.m_bodies.size() == before);
}

TEST_CASE("SSDW14 会话：折线、曲线逐点添加，完成时一次发出；不到两个点丢弃；切换工具时丢弃未完成的", "[sharedraw]")
{
    SsdwTransport t;
    ShareAnnotationSession s;
    s.Start(&t, kSsdwHost, 100);
    s.SetTool(annot_tool_polyline);
    s.PointerDown(ssdwPt(100, 100), 10, 0);
    s.PointerUp(ssdwPt(100, 100), 10);
    s.PointerHover(ssdwPt(500, 300), 20);
    s.PointerDown(ssdwPt(500, 300), 10, 30);
    s.PointerUp(ssdwPt(500, 300), 40);
    s.PointerDown(ssdwPt(900, 100), 10, 50);
    s.PointerUp(ssdwPt(900, 100), 60);
    //预览：已定的三个顶点加跟着光标的点
    s.PointerMove(ssdwPt(950, 400), 10, 70);
    AnnotationStroke preview;
    REQUIRE(s.PendingShape(preview));
    CHECK(preview.m_tool == annot_tool_polyline);
    CHECK(preview.m_points.size() == 4);
    //还没发出笔迹（画笔位置不算）
    for (size_t i = 0; i < t.m_bodies.size(); ++i)
        CHECK(ssdwSent(t, i).m_op == annot_op_pointer);
    const size_t before = t.m_bodies.size();
    REQUIRE(s.FinishPending(80));
    REQUIRE(t.m_bodies.size() == before + 2);
    AnnotationMessage m = ssdwSent(t, before);
    CHECK(m.m_op == annot_op_begin);
    CHECK(m.m_tool == annot_tool_polyline);
    REQUIRE(m.m_points.size() == 3);
    CHECK(m.m_points[2].m_x == 900);
    CHECK_FALSE(s.PendingShape(preview));

    //曲线：只点了一个点就完成，丢弃
    s.SetTool(annot_tool_curve);
    s.PointerDown(ssdwPt(100, 100), 10, 100);
    s.PointerUp(ssdwPt(100, 100), 110);
    CHECK_FALSE(s.FinishPending(120));
    //曲线：两个点之后切换工具，丢弃
    s.PointerDown(ssdwPt(100, 100), 10, 130);
    s.PointerUp(ssdwPt(100, 100), 140);
    s.PointerDown(ssdwPt(300, 300), 10, 150);
    s.PointerUp(ssdwPt(300, 300), 160);
    const size_t beforeSwitch = t.m_bodies.size();
    s.SetTool(annot_tool_pen);
    CHECK(t.m_bodies.size() == beforeSwitch);
    CHECK_FALSE(s.PendingShape(preview));
}

TEST_CASE("SSDW15 会话：数字标号点一下放一个、号码自动加一、可从头编号；文字带当前文字样式", "[sharedraw]")
{
    SsdwTransport t;
    ShareAnnotationSession s;
    s.Start(&t, kSsdwViewer, 100);
    s.SetTool(annot_tool_badge);
    s.SetBadgeStyle(annot_badge_pin, annot_seq_letter);
    CHECK(s.NextBadgeNumber() == 1);
    s.PointerDown(ssdwPt(100, 100), 10, 0);
    s.PointerUp(ssdwPt(100, 100), 10);
    s.PointerDown(ssdwPt(200, 200), 10, 20);
    s.PointerUp(ssdwPt(200, 200), 30);
    REQUIRE(s.Model().Strokes().size() == 2);
    CHECK(s.Model().Strokes()[0].m_style.m_number == 1);
    CHECK(s.Model().Strokes()[1].m_style.m_number == 2);
    CHECK(s.Model().Strokes()[1].m_style.m_variant == annot_badge_pin);
    CHECK(s.Model().Strokes()[1].m_style.m_sequence == annot_seq_letter);
    CHECK(s.NextBadgeNumber() == 3);
    //发出去的是一个点的 begin 加 end
    AnnotationMessage m = ssdwSent(t, 0);
    CHECK(m.m_tool == annot_tool_badge);
    CHECK(m.m_points.size() == 1);
    CHECK(ssdwSent(t, 1).m_op == annot_op_end);
    //从头编号
    s.ResetBadgeNumber();
    s.PointerDown(ssdwPt(300, 300), 10, 40);
    s.PointerUp(ssdwPt(300, 300), 50);
    CHECK(s.Model().Strokes().back().m_style.m_number == 1);

    //文字：加粗、填充背景，字号按档位
    s.SetTextStyle(true, annot_text_bg_fill, 0x80DEEA);
    s.SetWidthLevel(annot_width_thick);
    REQUIRE(s.AddText(ssdwPt(500, 500), "note"));
    const AnnotationStroke& text = s.Model().Strokes().back();
    CHECK(text.m_tool == annot_tool_text);
    CHECK(text.m_style.m_bold);
    CHECK(text.m_style.m_textBg == annot_text_bg_fill);
    CHECK(text.m_style.m_bgColor == 0x80DEEAu);
    CHECK(text.m_width > kAnnotTextSize);
}

TEST_CASE("SSDW16 会话：画笔位置按间隔与位置变化广播，离开时发一次；不进本地模型", "[sharedraw]")
{
    SsdwTransport t;
    ShareAnnotationSession s;
    s.Start(&t, kSsdwViewer, 100);
    s.SetTool(annot_tool_marker);
    s.SetColor(0x43A047);
    s.PointerHover(ssdwPt(100, 100), 1000);
    REQUIRE(t.m_bodies.size() == 1);
    AnnotationMessage m = ssdwSent(t, 0);
    CHECK(m.m_op == annot_op_pointer);
    CHECK(m.m_tool == annot_tool_marker);
    CHECK(m.m_color == 0x43A047u);
    //间隔不够：不发；位置没变：不发
    s.PointerHover(ssdwPt(200, 200), 1000 + kAnnotPointerIntervalMs - 1);
    s.PointerHover(ssdwPt(100, 100), 1000 + kAnnotPointerIntervalMs * 2);
    CHECK(t.m_bodies.size() == 1);
    s.PointerHover(ssdwPt(300, 300), 1000 + kAnnotPointerIntervalMs * 3);
    CHECK(t.m_bodies.size() == 2);
    //离开：发一次，再离开不重复发
    s.PointerLeave();
    s.PointerLeave();
    REQUIRE(t.m_bodies.size() == 3);
    CHECK(ssdwSent(t, 2).m_op == annot_op_pointer_leave);
    //本地模型里没有自己的画笔位置
    CHECK(s.Model().Pointers().empty());
    //没有标注权限：不发
    s.SetViewersAllowed(false);
    s.PointerHover(ssdwPt(900, 900), 9000);
    CHECK(t.m_bodies.size() == 3);
}

TEST_CASE("SSDW17 工具光标与别人的画笔位置：光标图像带当前颜色、热点合理；画笔位置处有圆点与名字标签", "[sharedraw]")
{
    const int size = 32;
    //画笔：热点在笔尖（左下），笔尖附近有当前颜色
    AnnotCursorImage pen = RenderToolCursor(annot_tool_pen, 0xE53935, size);
    REQUIRE(pen.m_size == size);
    REQUIRE(pen.m_bgra.size() == static_cast<size_t>(size * size * 4));
    CHECK(pen.m_hotX < size / 2);
    CHECK(pen.m_hotY > size / 2);
    bool redNearTip = false;
    for (int dy = -3; dy <= 3; ++dy)
    {
        for (int dx = -3; dx <= 3; ++dx)
        {
            const int x = pen.m_hotX + dx;
            const int y = pen.m_hotY + dy;
            if (x < 0 || y < 0 || x >= size || y >= size)
                continue;
            const uint8_t* p = &pen.m_bgra[(static_cast<size_t>(y) * size + x) * 4];
            redNearTip = redNearTip || (p[3] > 200 && p[2] > 180 && p[1] < 100);
        }
    }
    CHECK(redNearTip);
    //形状：十字，热点在中心，中心有颜色
    AnnotCursorImage cross = RenderToolCursor(annot_tool_rect, 0x1E88E5, size);
    CHECK(cross.m_hotX == size / 2);
    CHECK(cross.m_hotY == size / 2);
    CHECK(cross.m_bgra[(static_cast<size_t>(size / 2) * size + size / 2) * 4 + 3] > 0);
    //其余工具也都画得出来
    const int tools[] = { annot_tool_text, annot_tool_laser, annot_tool_badge, annot_tool_eraser_area, 100,
                          annot_tool_highlighter, annot_tool_marker };
    for (size_t i = 0; i < sizeof(tools) / sizeof(tools[0]); ++i)
    {
        INFO("tool " << tools[i]);
        AnnotCursorImage img = RenderToolCursor(tools[i], 0x000000, size);
        REQUIRE(img.m_bgra.size() == static_cast<size_t>(size * size * 4));
        int painted = 0;
        for (size_t k = 3; k < img.m_bgra.size(); k += 4)
            painted += img.m_bgra[k] > 0 ? 1 : 0;
        CHECK(painted > 10);
        CHECK(img.m_hotX >= 0);
        CHECK(img.m_hotX < size);
    }

    //别人的画笔位置：圆点在位置上，名字标签在右下方
    const int w = 300;
    const int h = 200;
    std::vector<uint8_t> layer(static_cast<size_t>(w * h * 4), 0);
    std::vector<AnnotationPointer> ptrs;
    AnnotationPointer p;
    p.m_seat = 2;
    p.m_pt = AnnotationPoint(16384, 16384);
    p.m_tool = annot_tool_pen;
    p.m_color = 0x8E24AA;
    ptrs.push_back(p);
    std::vector<std::wstring> names(1, L"Li Si");
    RenderAnnotationPointers(w, h, ptrs, names, layer);
    CHECK(ssdwAlpha(layer, w, 75, 50) > 200);
    int labelPixels = 0;
    for (int y = 52; y < 80; ++y)
    {
        for (int x = 78; x < 140; ++x)
            labelPixels += ssdwAlpha(layer, w, x, y) > 0 ? 1 : 0;
    }
    CHECK(labelPixels > 100);
    //画笔位置以外的地方不动
    CHECK(ssdwAlpha(layer, w, 250, 180) == 0);
}

namespace {

//把菜单（含子菜单）里有 id 的项摊平
void ssdwFlatten(const std::vector<AnnotMenuItem>& items, std::vector<AnnotMenuItem>& out)
{
    for (size_t i = 0; i < items.size(); ++i)
    {
        if (!items[i].m_children.empty())
            ssdwFlatten(items[i].m_children, out);
        else if (items[i].m_id != 0)
            out.push_back(items[i]);
    }
}

//统计某组菜单里换成某条命令的项数，并返回其中打勾的那项的取值（没有打勾的返回 -1）
int ssdwCount(const std::vector<AnnotMenuItem>& menu, int command, int& checkedValue)
{
    std::vector<AnnotMenuItem> flat;
    ssdwFlatten(menu, flat);
    int count = 0;
    checkedValue = -1;
    for (size_t i = 0; i < flat.size(); ++i)
    {
        AnnotMenuChoice c;
        if (!DecodeAnnotMenuChoice(flat[i].m_id, c) || c.m_command != command)
            continue;
        ++count;
        if (flat[i].m_checked)
            checkedValue = c.m_value;
    }
    return count;
}

} // namespace

TEST_CASE("SSDW18 工具条下拉菜单：各组的项数与当前项打勾", "[sharedraw]")
{
    AnnotBarState st;
    st.m_tool = annot_tool_marker;
    st.m_color = kAnnotPalette[3];
    st.m_widthLevel = annot_width_thick;
    st.m_lineStyle = annot_line_dot;
    st.m_fill = annot_fill_translucent;
    st.m_routing = annot_route_elbow;
    st.m_startCap = annot_cap_circle;
    st.m_endCap = annot_cap_diamond;
    st.m_blockVariant = annot_block_bent;
    st.m_badgeStyle = annot_badge_square;
    st.m_badgeSequence = annot_seq_chinese;
    st.m_textBold = true;
    st.m_textBg = annot_text_bg_white;
    int checked = -1;

    //画笔：六种，当前的马克笔打勾
    CHECK(ssdwCount(BuildAnnotGroupMenu(annot_group_pen, st), annot_bar_tool, checked) == 6);
    CHECK(checked == annot_tool_marker);
    //线条：三种，当前工具不在这一组，没有打勾
    CHECK(ssdwCount(BuildAnnotGroupMenu(annot_group_line, st), annot_bar_tool, checked) == 3);
    CHECK(checked == -1);
    //形状：十一种，另有一行不可点的提示
    const std::vector<AnnotMenuItem> shapes = BuildAnnotGroupMenu(annot_group_shape, st);
    CHECK(ssdwCount(shapes, annot_bar_tool, checked) == 11);
    bool hint = false;
    for (size_t i = 0; i < shapes.size(); ++i)
        hint = hint || (!shapes[i].m_enabled && !shapes[i].m_separator && !shapes[i].m_text.empty());
    CHECK(hint);
    //箭头：走向三种、起点与终点各九种、块状箭头六种，各自当前的打勾
    const std::vector<AnnotMenuItem> arrows = BuildAnnotGroupMenu(annot_group_arrow, st);
    CHECK(ssdwCount(arrows, annot_bar_routing, checked) == 3);
    CHECK(checked == annot_route_elbow);
    CHECK(ssdwCount(arrows, annot_bar_start_cap, checked) == 9);
    CHECK(checked == annot_cap_circle);
    CHECK(ssdwCount(arrows, annot_bar_end_cap, checked) == 9);
    CHECK(checked == annot_cap_diamond);
    CHECK(ssdwCount(arrows, annot_bar_block, checked) == 6);
    CHECK(checked == annot_block_bent);
    //标号：样式四种、写法三种、从头编号一项
    const std::vector<AnnotMenuItem> badges = BuildAnnotGroupMenu(annot_group_badge, st);
    CHECK(ssdwCount(badges, annot_bar_badge_style, checked) == 4);
    CHECK(checked == annot_badge_square);
    CHECK(ssdwCount(badges, annot_bar_badge_seq, checked) == 3);
    CHECK(checked == annot_seq_chinese);
    CHECK(ssdwCount(badges, annot_bar_badge_reset, checked) == 1);
    //文字：加粗一项（打勾）、背景三种。「加粗」是开关：已加粗时打勾，点它是取消加粗，所以它的取值为 0
    const std::vector<AnnotMenuItem> text = BuildAnnotGroupMenu(annot_group_text, st);
    CHECK(ssdwCount(text, annot_bar_text_bold, checked) == 1);
    CHECK(checked == 0);
    CHECK(ssdwCount(text, annot_bar_text_bg, checked) == 3);
    CHECK(checked == annot_text_bg_white);
    //橡皮：整笔与局部
    CHECK(ssdwCount(BuildAnnotGroupMenu(annot_group_eraser, st), annot_bar_tool, checked) == 2);
    //颜色：十二种色块（当前色打勾）加自定义
    const std::vector<AnnotMenuItem> colors = BuildAnnotGroupMenu(annot_group_color, st);
    CHECK(ssdwCount(colors, annot_bar_color, checked) == kAnnotPaletteSize);
    CHECK(checked == static_cast<int>(kAnnotPalette[3]));
    CHECK(ssdwCount(colors, annot_bar_custom_color, checked) == 1);
    int swatches = 0;
    for (size_t i = 0; i < colors.size(); ++i)
        swatches += colors[i].m_swatch >= 0 ? 1 : 0;
    CHECK(swatches == kAnnotPaletteSize);
    //粗细、线型、填充
    CHECK(ssdwCount(BuildAnnotGroupMenu(annot_group_width, st), annot_bar_width, checked) == 3);
    CHECK(checked == annot_width_thick);
    CHECK(ssdwCount(BuildAnnotGroupMenu(annot_group_line_style, st), annot_bar_line_style, checked) == 4);
    CHECK(checked == annot_line_dot);
    CHECK(ssdwCount(BuildAnnotGroupMenu(annot_group_fill, st), annot_bar_fill, checked) == 3);
    CHECK(checked == annot_fill_translucent);
    //组取值非法：空
    CHECK(BuildAnnotGroupMenu(kAnnotGroupCount, st).empty());
}

TEST_CASE("SSDW19 工具条：菜单项 id 互不相同且都能换成命令；工具归组；按钮文字", "[sharedraw]")
{
    AnnotBarState st;
    std::vector<UINT> ids;
    for (int g = 0; g < kAnnotGroupCount; ++g)
    {
        std::vector<AnnotMenuItem> flat;
        ssdwFlatten(BuildAnnotGroupMenu(g, st), flat);
        REQUIRE_FALSE(flat.empty());
        for (size_t i = 0; i < flat.size(); ++i)
        {
            INFO("group " << g << " id " << flat[i].m_id);
            AnnotMenuChoice c;
            CHECK(DecodeAnnotMenuChoice(flat[i].m_id, c));
            ids.push_back(flat[i].m_id);
        }
    }
    std::vector<UINT> sorted = ids;
    std::sort(sorted.begin(), sorted.end());
    CHECK(std::unique(sorted.begin(), sorted.end()) == sorted.end());
    AnnotMenuChoice none;
    CHECK_FALSE(DecodeAnnotMenuChoice(0, none));
    CHECK_FALSE(DecodeAnnotMenuChoice(99999, none));

    //工具归组
    CHECK(AnnotToolGroup(annot_tool_pencil) == annot_group_pen);
    CHECK(AnnotToolGroup(annot_tool_laser) == annot_group_pen);
    CHECK(AnnotToolGroup(annot_tool_curve) == annot_group_line);
    CHECK(AnnotToolGroup(annot_tool_arrow) == annot_group_arrow);
    CHECK(AnnotToolGroup(annot_tool_block_arrow) == annot_group_arrow);
    CHECK(AnnotToolGroup(annot_tool_star) == annot_group_shape);
    CHECK(AnnotToolGroup(annot_tool_rect) == annot_group_shape);
    CHECK(AnnotToolGroup(annot_tool_badge) == annot_group_badge);
    CHECK(AnnotToolGroup(annot_tool_text) == annot_group_text);
    CHECK(AnnotToolGroup(annot_tool_eraser_area) == annot_group_eraser);
    CHECK(AnnotToolGroup(kAnnotEraser) == annot_group_eraser);
    CHECK(AnnotToolGroup(0) == -1);

    //按钮文字：当前工具在组里写工具名，否则写组名，都带下拉符号
    st.m_tool = annot_tool_marker;
    const std::wstring pen = AnnotGroupButtonText(annot_group_pen, st);
    CHECK(pen.find(L"马克笔") == 0);
    CHECK(pen.find(L"▾") != std::wstring::npos);
    CHECK(AnnotGroupButtonText(annot_group_shape, st).find(L"形状") == 0);
    st.m_tool = annot_tool_block_arrow;
    CHECK(AnnotGroupButtonText(annot_group_arrow, st).find(L"块状箭头") == 0);
    CHECK(AnnotGroupButtonText(annot_group_color, st).find(L"颜色") == 0);
}

TEST_CASE("SSDW20 工具条：菜单命令记进状态、文字背景浅色、状态整体交给会话", "[sharedraw]")
{
    AnnotBarState st;
    AnnotMenuChoice c;
    //选工具
    c.m_command = annot_bar_tool;
    c.m_value = annot_tool_rect;
    CHECK(ApplyAnnotChoice(c, st));
    CHECK(st.m_tool == annot_tool_rect);
    //选颜色、粗细、线型、填充：工具不变
    c.m_command = annot_bar_color;
    c.m_value = 0x2196F3;
    CHECK(ApplyAnnotChoice(c, st));
    CHECK(st.m_color == 0x2196F3u);
    c.m_command = annot_bar_width;
    c.m_value = annot_width_thick;
    CHECK(ApplyAnnotChoice(c, st));
    c.m_command = annot_bar_line_style;
    c.m_value = annot_line_dash;
    CHECK(ApplyAnnotChoice(c, st));
    c.m_command = annot_bar_fill;
    c.m_value = annot_fill_translucent;
    CHECK(ApplyAnnotChoice(c, st));
    CHECK(st.m_widthLevel == annot_width_thick);
    CHECK(st.m_lineStyle == annot_line_dash);
    CHECK(st.m_fill == annot_fill_translucent);
    CHECK(st.m_tool == annot_tool_rect);
    //箭头走向与两端：切到箭头
    c.m_command = annot_bar_routing;
    c.m_value = annot_route_elbow;
    CHECK(ApplyAnnotChoice(c, st));
    CHECK(st.m_routing == annot_route_elbow);
    CHECK(st.m_tool == annot_tool_arrow);
    st.m_tool = annot_tool_pen;
    c.m_command = annot_bar_start_cap;
    c.m_value = annot_cap_circle;
    CHECK(ApplyAnnotChoice(c, st));
    c.m_command = annot_bar_end_cap;
    c.m_value = annot_cap_swallowtail;
    CHECK(ApplyAnnotChoice(c, st));
    CHECK(st.m_startCap == annot_cap_circle);
    CHECK(st.m_endCap == annot_cap_swallowtail);
    CHECK(st.m_tool == annot_tool_arrow);
    //块状箭头
    c.m_command = annot_bar_block;
    c.m_value = annot_block_u_turn;
    CHECK(ApplyAnnotChoice(c, st));
    CHECK(st.m_blockVariant == annot_block_u_turn);
    CHECK(st.m_tool == annot_tool_block_arrow);
    //数字标号
    c.m_command = annot_bar_badge_style;
    c.m_value = annot_badge_pin;
    CHECK(ApplyAnnotChoice(c, st));
    CHECK(st.m_tool == annot_tool_badge);
    st.m_tool = annot_tool_pen;
    c.m_command = annot_bar_badge_seq;
    c.m_value = annot_seq_letter;
    CHECK(ApplyAnnotChoice(c, st));
    CHECK(st.m_badgeStyle == annot_badge_pin);
    CHECK(st.m_badgeSequence == annot_seq_letter);
    CHECK(st.m_tool == annot_tool_badge);
    //文字加粗与背景
    c.m_command = annot_bar_text_bold;
    c.m_value = 1;
    CHECK(ApplyAnnotChoice(c, st));
    CHECK(st.m_textBold);
    CHECK(st.m_tool == annot_tool_text);
    c.m_command = annot_bar_text_bg;
    c.m_value = annot_text_bg_fill;
    CHECK(ApplyAnnotChoice(c, st));
    CHECK(st.m_textBg == annot_text_bg_fill);
    c.m_command = annot_bar_text_bold;
    c.m_value = 0;
    CHECK(ApplyAnnotChoice(c, st));
    CHECK_FALSE(st.m_textBold);

    //不改状态的命令
    const int commands[] = { annot_bar_undo, annot_bar_clear, annot_bar_permission, annot_bar_exit, annot_bar_badge_reset,
                             annot_bar_custom_color, 0, 999 };
    for (size_t i = 0; i < sizeof(commands) / sizeof(commands[0]); ++i)
    {
        INFO("command " << commands[i]);
        AnnotBarState before = st;
        c.m_command = commands[i];
        c.m_value = 1;
        CHECK_FALSE(ApplyAnnotChoice(c, st));
        CHECK(st.m_tool == before.m_tool);
        CHECK(st.m_color == before.m_color);
        CHECK(st.m_textBold == before.m_textBold);
    }

    //文字背景浅色：与白色 1:3 混合
    CHECK(AnnotTextBgColor(0xFF0000) == 0xFFBFBFu);
    CHECK(AnnotTextBgColor(0x000000) == 0xBFBFBFu);
    CHECK(AnnotTextBgColor(0xFFFFFF) == 0xFFFFFFu);

    //整体交给会话：画一个矩形，样式随状态
    SsdwTransport t;
    ShareAnnotationSession s;
    s.Start(&t, kSsdwViewer, 100);
    AnnotBarState rect;
    rect.m_tool = annot_tool_rect;
    rect.m_color = 0x4CAF50;
    rect.m_widthLevel = annot_width_thick;
    rect.m_lineStyle = annot_line_dot;
    rect.m_fill = annot_fill_solid;
    ApplyAnnotBarState(rect, s);
    CHECK(s.Tool() == annot_tool_rect);
    CHECK(s.Color() == 0x4CAF50u);
    CHECK(s.WidthLevel() == annot_width_thick);
    s.PointerDown(ssdwPt(100, 100), 10, 0);
    s.PointerMove(ssdwPt(300, 300), 10, 10);
    s.PointerUp(ssdwPt(300, 300), 20);
    REQUIRE(s.Model().Strokes().size() == 1);
    CHECK(s.Model().Strokes()[0].m_color == 0x4CAF50u);
    CHECK(s.Model().Strokes()[0].m_style.m_lineStyle == annot_line_dot);
    CHECK(s.Model().Strokes()[0].m_style.m_fill == annot_fill_solid);

    //箭头：走向与两端
    AnnotBarState arrow;
    arrow.m_tool = annot_tool_arrow;
    arrow.m_routing = annot_route_arc;
    arrow.m_startCap = annot_cap_diamond;
    arrow.m_endCap = annot_cap_open;
    ApplyAnnotBarState(arrow, s);
    s.PointerDown(ssdwPt(100, 400), 10, 30);
    s.PointerMove(ssdwPt(400, 400), 10, 35);
    s.PointerUp(ssdwPt(400, 400), 40);
    REQUIRE(s.Model().Strokes().size() == 2);
    CHECK(s.Model().Strokes()[1].m_style.m_routing == annot_route_arc);
    CHECK(s.Model().Strokes()[1].m_style.m_startCap == annot_cap_diamond);
    CHECK(s.Model().Strokes()[1].m_style.m_endCap == annot_cap_open);

    //文字：加粗、填充背景用当前颜色的浅色
    AnnotBarState text;
    text.m_tool = annot_tool_text;
    text.m_color = 0xFF0000;
    text.m_textBold = true;
    text.m_textBg = annot_text_bg_fill;
    ApplyAnnotBarState(text, s);
    REQUIRE(s.AddText(ssdwPt(500, 500), "note"));
    CHECK(s.Model().Strokes().back().m_style.m_bold);
    CHECK(s.Model().Strokes().back().m_style.m_textBg == annot_text_bg_fill);
    CHECK(s.Model().Strokes().back().m_style.m_bgColor == 0xFFBFBFu);
}

namespace {

//归一化坐标换成 SSDW21 显示尺寸里的像素坐标
GeoPoint ssdwToView(const GeoPoint& p)
{
    return GeoPoint(p.m_x / kSsdwCoordMax * kSsdwViewW, p.m_y / kSsdwCoordMax * kSsdwViewH);
}

//多边形（按 step 取点）相邻两点距离的最大值与最小值之比；正多边形应为 1
double ssdwEdgeSpread(const std::vector<GeoPoint>& pts, size_t step)
{
    std::vector<GeoPoint> v;
    for (size_t i = 0; i < pts.size(); i += step)
        v.push_back(pts[i]);
    double lo = 1e300;
    double hi = 0;
    for (size_t i = 0; i < v.size(); ++i)
    {
        const GeoPoint& a = v[i];
        const GeoPoint& b = v[(i + 1) % v.size()];
        const double d = sqrt((a.m_x - b.m_x) * (a.m_x - b.m_x) + (a.m_y - b.m_y) * (a.m_y - b.m_y));
        lo = d < lo ? d : lo;
        hi = d > hi ? d : hi;
    }
    return lo > 0 ? hi / lo : 1e300;
}

} // namespace

TEST_CASE("SSDW21 Shift draws regular shapes and 45 degree lines on 16:9 content", "[sharedraw]")
{
    const double aspect = kSsdwViewW / kSsdwViewH;
    const GeoPoint from(10000, 10000);
    const GeoPoint to(30000, 21000);
    //宽高比：正方形外接框的形状为 1，三角形 2/√3，五边形与五角星相同，六边形 2/√3
    CHECK(fabs(AnnotRegularShapeAspect(annot_tool_rect) - 1.0) < kSsdwEps);
    CHECK(fabs(AnnotRegularShapeAspect(annot_tool_triangle) - 2.0 / sqrt(3.0)) < kSsdwEps);
    CHECK(fabs(AnnotRegularShapeAspect(annot_tool_hexagon) - 2.0 / sqrt(3.0)) < kSsdwEps);
    CHECK(fabs(AnnotRegularShapeAspect(annot_tool_star) - AnnotRegularShapeAspect(annot_tool_pentagon)) < kSsdwEps);
    CHECK(AnnotRegularShapeAspect(annot_tool_pentagon) > 1.0);
    //矩形、椭圆：显示上宽高相等
    const int squares[] = { annot_tool_rect, annot_tool_ellipse, annot_tool_round_rect, annot_tool_diamond };
    for (size_t i = 0; i < sizeof(squares) / sizeof(squares[0]); ++i)
    {
        INFO("tool " << squares[i]);
        const GeoPoint e = ssdwToView(AnnotConstrainDrag(squares[i], from, to, aspect, kSsdwCoordMax));
        const GeoPoint s = ssdwToView(from);
        CHECK(fabs(fabs(e.m_x - s.m_x) - fabs(e.m_y - s.m_y)) < 1.0);
    }
    //正多边形：各边等长（五角星取外角顶点）
    const int polygons[] = { annot_tool_triangle, annot_tool_pentagon, annot_tool_hexagon, annot_tool_star };
    for (size_t i = 0; i < sizeof(polygons) / sizeof(polygons[0]); ++i)
    {
        INFO("tool " << polygons[i]);
        const GeoPoint e = ssdwToView(AnnotConstrainDrag(polygons[i], from, to, aspect, kSsdwCoordMax));
        const std::vector<GeoPoint> outline = AnnotShapeOutline(polygons[i], ssdwToView(from), e);
        REQUIRE_FALSE(outline.empty());
        const size_t step = (polygons[i] == annot_tool_star) ? 2 : 1;
        CHECK(ssdwEdgeSpread(outline, step) < 1.0 + kSsdwRatioEps);
    }
    //直线、箭头、连接线：显示上吸附到 45 度（拖向约 44 度的方向）
    const GeoPoint near45(from.m_x + 10000, from.m_y + 17000);
    const int lines[] = { annot_tool_line, annot_tool_arrow, annot_tool_connector };
    for (size_t i = 0; i < sizeof(lines) / sizeof(lines[0]); ++i)
    {
        INFO("tool " << lines[i]);
        const GeoPoint e = ssdwToView(AnnotConstrainDrag(lines[i], from, near45, aspect, kSsdwCoordMax));
        const GeoPoint s = ssdwToView(from);
        CHECK(fabs((e.m_x - s.m_x) - (e.m_y - s.m_y)) < 1.0);
    }
    //拖出画面：整段按比例缩短，终点在画面内，仍是正方形
    const GeoPoint corner(60000, 60000);
    const GeoPoint e = AnnotConstrainDrag(annot_tool_rect, corner, GeoPoint(kSsdwCoordMax, kSsdwCoordMax), aspect,
                                          kSsdwCoordMax);
    CHECK(e.m_x <= kSsdwCoordMax + kSsdwEps);
    CHECK(e.m_y <= kSsdwCoordMax + kSsdwEps);
    const GeoPoint ve = ssdwToView(e);
    const GeoPoint vs = ssdwToView(corner);
    CHECK(fabs((ve.m_x - vs.m_x) - (ve.m_y - vs.m_y)) < 1.0);
    //不是直线或形状的工具：原样返回
    const GeoPoint same = AnnotConstrainDrag(annot_tool_pen, from, to, aspect, kSsdwCoordMax);
    CHECK(same.m_x == to.m_x);
    CHECK(same.m_y == to.m_y);
}

TEST_CASE("SSDW22 pressing or releasing Shift while dragging updates the preview", "[sharedraw]")
{
    SsdwTransport t;
    ShareAnnotationSession s;
    s.Start(&t, kSsdwHost, 100);
    s.SetTool(annot_tool_rect);
    s.SetShiftDown(false);
    s.PointerDown(ssdwPt(1000, 1000), 10, 0);
    s.PointerMove(ssdwPt(5000, 3000), 10, 50);
    AnnotationStroke preview;
    REQUIRE(s.PendingShape(preview));
    REQUIRE(preview.m_points.size() == 2);
    CHECK(preview.m_points[1].m_x == 5000);
    CHECK(preview.m_points[1].m_y == 3000);
    //鼠标不动、按下 Shift：终点变成显示上的正方形
    CHECK(s.SetShiftDown(true));
    REQUIRE(s.PendingShape(preview));
    const double w = (preview.m_points[1].m_x - preview.m_points[0].m_x) * kAnnotDefaultAspect;
    const double h = preview.m_points[1].m_y - preview.m_points[0].m_y;
    REQUIRE(h > 0);
    CHECK(fabs(w / h - 1.0) < kSsdwRatioEps);
    //状态没变：不报告变化
    CHECK_FALSE(s.SetShiftDown(true));
    //松开 Shift：恢复鼠标所在的位置
    CHECK(s.SetShiftDown(false));
    REQUIRE(s.PendingShape(preview));
    CHECK(preview.m_points[1].m_x == 5000);
    CHECK(preview.m_points[1].m_y == 3000);
    s.PointerUp(ssdwPt(5000, 3000), 100);
    //没有在拖动：Shift 变化不报告
    CHECK_FALSE(s.SetShiftDown(true));
    s.SetShiftDown(false);
}

TEST_CASE("SSDW23 ObjectAt finds drawn objects for the select tool cursor", "[sharedraw]")
{
    SsdwTransport t;
    ShareAnnotationSession s;
    s.Start(&t, kSsdwHost, 100);
    s.SetTool(annot_tool_rect);
    s.PointerDown(ssdwPt(10000, 10000), 10, 0);
    s.PointerUp(ssdwPt(20000, 20000), 100);
    const int tolerance = 200;
    //矩形的边上有对象，远处没有
    CHECK(s.ObjectAt(ssdwPt(10000, 15000), tolerance));
    CHECK_FALSE(s.ObjectAt(ssdwPt(50000, 50000), tolerance));
}
