/**
 *  屏幕共享标注对象模型与网络拓扑的单元测试（Suite SSOB，标签 [shareobj]），ShareObjectTests.cpp
 *
 *  覆盖 docs/屏幕共享与录制方案.md 第 2 节第 37 条与第 8 节第 20 期：
 *    - SSOB1 编解码：第 20 期样式块带两端吸附对象、往返不变，第 19 期 16 字节的样式块仍可解出（吸附为空）；修改、移动两个新操作
 *            往返不变，移动的对象个数为 0 或超过上限时拒绝；图元与连接线两个新种类可以开始一笔；
 *    - SSOB2 模型的修改：替换颜色、线宽、点、文字与样式，种类不变；没画完、不存在、激光笔、内容相同的修改不生效；
 *            观看者没有权限时不接受；
 *    - SSOB3 模型的移动：多个对象一起移动；每个对象各自把位移限制在画面以内；不存在或没画完的对象跳过；
 *    - SSOB4 连接线：被吸附的对象擦除后那一端改为不吸附、停在它的中心；移动连接线时，吸附对象不在同一批里的那一端脱开，
 *            在同一批里的保持吸附；整笔补发带上吸附；
 *    - SSOB5 对象几何：外接矩形、文字近似尺寸、连接线两端从被吸附对象中心出发裁到边外、点选（重叠取上面的，橡皮只认边线）、
 *            框选、可吸附的对象；
 *    - SSOB6 单选时的句柄、按句柄缩放（不翻转、最小边长、按住 Shift 保持比例）、把对象缩放到新矩形（文字字号、标号大小）；
 *    - SSOB7 会话的选择工具：点选、拖动移动按间隔发出、框选、Shift 加减选、删除、别人删掉后从选择里去掉、换工具取消选择；
 *    - SSOB8 会话：拖句柄缩放松开时发一次修改；拖连接线的一端落到图元上吸附、落到空白处脱开；
 *    - SSOB9 会话：改文字（文字、图元名字、标号号码，文字改空即删除）、改选中对象的样式（只改用得上的对象）；
 *    - SSOB10 网络拓扑图元的矢量图形：十一种都有、坐标在单位方框里、彼此不同；
 *    - SSOB11 图层绘制：图元按矢量图画（浅色填充）、名字在下方；连接线按吸附的两端画；选中框、句柄与选择框；
 *    - SSOB12 会话：点一下放默认大小的图元（靠边时整个留在画面里）、拖出指定大小；连接线两端落在图元上吸附（同一个图元只吸附起点）、
 *            走向可设；橡皮点在图元里面擦掉图元、点在连接线上擦掉连接线；
 *    - SSOB13 工具条：选择与拓扑两组的菜单、组的顺序、按钮文字、图元与连接线走向命令；选择工具下样式类命令改到选中对象上。
 *  纯逻辑，不建窗口。框架 Catch2 v3。
 *
 *  另有隐藏用例 SSOBX（标签 [shareobj-snap]）把一张网络拓扑效果图存到仓库根的 snapshots\share20_topology.png，只在手动指定时运行；
 *  SSOBY（标签 [sharebar-snap]）在主显示器左上角短暂显示标注工具条，只截它本身（PrintWindow），存成 snapshots\share19_toolbar*.png。
 *
 *  典型用法：FlamingoUnit.exe "[shareobj]"
 *
 *  balloonwj@qq.com   2026-10-05
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <objidl.h>        // gdiplus.h 依赖 IStream 等 COM 声明
#include <gdiplus.h>       // 效果图用例（SSOBX）把图层存成 PNG
#include <stdlib.h>
#include <string>
#include <vector>
#include "../../FlamingoMedia/Share/ShareAnnotation.h"
#include "../../FlamingoMedia/Share/ShareAnnotationBar.h"
#include "../../FlamingoMedia/Share/ShareAnnotationIcons.h"
#include "../../FlamingoMedia/Share/ShareAnnotationMenus.h"
#include "../../FlamingoMedia/Share/ShareAnnotationObjects.h"
#include "../../FlamingoMedia/Share/ShareAnnotationRender.h"
#include "../../FlamingoMedia/Share/ShareAnnotationSession.h"
#include "../../FlamingoMedia/Share/ShareFraming.h"

using namespace screenshare;

namespace {

//发起人与一位观看者的座位号
const int kSsobHost = 0;
const int kSsobViewer = 3;

//编码再解码
AnnotationMessage ssobRoundTrip(const AnnotationMessage& m)
{
    const std::string payload = ShareAnnotationCodec::encode(m);
    REQUIRE_FALSE(payload.empty());
    AnnotationMessage out;
    REQUIRE(ShareAnnotationCodec::decode(payload, out));
    return out;
}

//以 seat 的身份画一个两点的对象（begin 加 end）
void ssobAdd(ShareAnnotationModel& model, int seat, uint32_t id, int tool, int x0, int y0, int x1, int y1,
             const AnnotationStyle& style = AnnotationStyle())
{
    AnnotationMessage b;
    b.m_op = annot_op_begin;
    b.m_id = id;
    b.m_tool = tool;
    b.m_color = 0x1E88E5;
    b.m_width = 4;
    b.m_points.push_back(AnnotationPoint(static_cast<uint16_t>(x0), static_cast<uint16_t>(y0)));
    b.m_points.push_back(AnnotationPoint(static_cast<uint16_t>(x1), static_cast<uint16_t>(y1)));
    b.m_style = style;
    REQUIRE(model.Apply(seat, b, 0));
    AnnotationMessage e;
    e.m_op = annot_op_end;
    e.m_id = id;
    REQUIRE(model.Apply(seat, e, 0));
}

//移动操作
AnnotationMessage ssobMove(int dx, int dy)
{
    AnnotationMessage m;
    m.m_op = annot_op_move;
    m.m_dx = dx;
    m.m_dy = dy;
    return m;
}

} // namespace

TEST_CASE("SSOB1 编解码：第 20 期样式块带吸附、老样式块仍可解；修改与移动往返不变；新种类", "[shareobj]")
{
    //连接线：起点吸附 (2, 77)，终点不吸附
    AnnotationMessage b;
    b.m_op = annot_op_begin;
    b.m_id = 41;
    b.m_tool = annot_tool_connector;
    b.m_color = 0x43A047;
    b.m_width = 4;
    b.m_points.push_back(AnnotationPoint(100, 200));
    b.m_points.push_back(AnnotationPoint(9000, 8000));
    b.m_style.m_endCap = annot_cap_triangle;
    b.m_style.m_attachStart = AnnotationRef(2, 77);
    const AnnotationMessage rb = ssobRoundTrip(b);
    CHECK(rb.m_tool == annot_tool_connector);
    CHECK(rb.m_style == b.m_style);
    CHECK(rb.m_style.m_attachStart == AnnotationRef(2, 77));
    //样式比较带上吸附：只差吸附也算不同
    AnnotationStyle other = b.m_style;
    other.m_attachEnd = AnnotationRef(4, 1);
    CHECK_FALSE(other == b.m_style);
    CHECK_FALSE(rb.m_style.m_attachEnd.IsSet());

    //第 19 期的样式块（16 字节）：吸附为空，其余照常
    std::string payload = ShareAnnotationCodec::encode(b);
    //当前版本的样式块为第 22 期的 kAnnotStyleBytesV3，截成 16 字节
    REQUIRE(payload.size() > kAnnotStyleBytesV3 + 1);
    const size_t styleAt = payload.size() - (kAnnotStyleBytesV3 + 1);
    CHECK(static_cast<uint8_t>(payload[styleAt]) == kAnnotStyleBytesV3);
    std::string legacy = payload.substr(0, payload.size() - (kAnnotStyleBytesV3 - kAnnotStyleBytes));
    legacy[styleAt] = static_cast<char>(kAnnotStyleBytes);
    AnnotationMessage rl;
    REQUIRE(ShareAnnotationCodec::decode(legacy, rl));
    CHECK(rl.m_style.m_endCap == annot_cap_triangle);
    CHECK_FALSE(rl.m_style.m_attachStart.IsSet());
    CHECK_FALSE(rl.m_style.m_attachEnd.IsSet());

    //图元
    AnnotationMessage icon;
    icon.m_op = annot_op_begin;
    icon.m_id = 42;
    icon.m_tool = annot_tool_icon;
    icon.m_color = 0x546E7A;
    icon.m_width = 4;
    icon.m_points.push_back(AnnotationPoint(1000, 1000));
    icon.m_points.push_back(AnnotationPoint(4000, 5000));
    icon.m_style.m_variant = annot_icon_router;
    CHECK(ssobRoundTrip(icon).m_style.m_variant == annot_icon_router);

    //修改：作者、编号、颜色、线宽、点、文字、样式
    AnnotationMessage u;
    u.m_op = annot_op_update;
    u.m_author = kSsobViewer;
    u.m_id = 9;
    u.m_color = 0xFB8C00;
    u.m_width = 8;
    u.m_points.push_back(AnnotationPoint(10, 20));
    u.m_points.push_back(AnnotationPoint(30, 40));
    u.m_points.push_back(AnnotationPoint(50, 60));
    u.m_text = "PC-1";
    u.m_style.m_variant = annot_icon_server;
    u.m_style.m_attachEnd = AnnotationRef(0, 5);
    const AnnotationMessage ru = ssobRoundTrip(u);
    CHECK(ru.m_op == annot_op_update);
    CHECK(ru.m_author == kSsobViewer);
    CHECK(ru.m_id == 9u);
    CHECK(ru.m_color == 0xFB8C00u);
    CHECK(ru.m_width == 8);
    REQUIRE(ru.m_points.size() == 3);
    CHECK(ru.m_points[2].m_x == 50);
    CHECK(ru.m_text == "PC-1");
    CHECK(ru.m_style == u.m_style);

    //移动：对象列表与有符号的位移
    AnnotationMessage mv = ssobMove(-1200, 300);
    mv.m_targets.push_back(AnnotationRef(0, 5));
    mv.m_targets.push_back(AnnotationRef(3, 9));
    const AnnotationMessage rm = ssobRoundTrip(mv);
    CHECK(rm.m_op == annot_op_move);
    REQUIRE(rm.m_targets.size() == 2);
    CHECK(rm.m_targets[1] == AnnotationRef(3, 9));
    CHECK(rm.m_dx == -1200);
    CHECK(rm.m_dy == 300);

    //对象个数为 0 或超过上限：编码返回空串，解码拒绝
    AnnotationMessage none = ssobMove(1, 1);
    CHECK(ShareAnnotationCodec::encode(none).empty());
    AnnotationMessage many = ssobMove(1, 1);
    for (int i = 0; i <= kAnnotMaxMoveTargets; ++i)
        many.m_targets.push_back(AnnotationRef(0, static_cast<uint32_t>(i + 1)));
    CHECK(ShareAnnotationCodec::encode(many).empty());
    std::string zero;
    zero.push_back(static_cast<char>(annot_op_move));
    zero.push_back(0);
    zero.append(4, '\0');
    AnnotationMessage rz;
    CHECK_FALSE(ShareAnnotationCodec::decode(zero, rz));
}

TEST_CASE("SSOB2 模型的修改：整体替换、种类不变；无效修改不生效；没有权限不接受", "[shareobj]")
{
    ShareAnnotationModel model;
    ssobAdd(model, kSsobHost, 1, annot_tool_rect, 1000, 1000, 5000, 4000);
    const uint64_t v0 = model.Version();

    //观看者改发起人的矩形：颜色、线宽、点、样式；消息里的种类不算数
    AnnotationMessage u;
    u.m_op = annot_op_update;
    u.m_author = kSsobHost;
    u.m_id = 1;
    u.m_tool = annot_tool_ellipse;
    u.m_color = 0xE53935;
    u.m_width = 8;
    u.m_points.push_back(AnnotationPoint(2000, 2000));
    u.m_points.push_back(AnnotationPoint(6000, 5000));
    u.m_style.m_fill = annot_fill_solid;
    CHECK(model.Apply(kSsobViewer, u, 0));
    CHECK(model.Version() > v0);
    const AnnotationStroke* r = model.Find(AnnotationRef(kSsobHost, 1));
    REQUIRE(r != NULL);
    CHECK(r->m_tool == annot_tool_rect);
    CHECK(r->m_author == kSsobHost);
    CHECK(r->m_color == 0xE53935u);
    CHECK(r->m_width == 8);
    CHECK(r->m_points[1].m_x == 6000);
    CHECK(r->m_style.m_fill == annot_fill_solid);
    //内容相同：不算变化
    CHECK_FALSE(model.Apply(kSsobViewer, u, 0));
    //不存在
    AnnotationMessage missing = u;
    missing.m_id = 99;
    CHECK_FALSE(model.Apply(kSsobViewer, missing, 0));
    //点为空：无效
    AnnotationMessage empty = u;
    empty.m_points.clear();
    empty.m_color = 0x000000;
    CHECK_FALSE(model.Apply(kSsobViewer, empty, 0));

    //没画完的一笔不能改
    AnnotationMessage b;
    b.m_op = annot_op_begin;
    b.m_id = 2;
    b.m_tool = annot_tool_pen;
    b.m_color = 0x000000;
    b.m_width = 4;
    b.m_points.push_back(AnnotationPoint(10, 10));
    REQUIRE(model.Apply(kSsobViewer, b, 0));
    AnnotationMessage u2 = u;
    u2.m_author = kSsobViewer;
    u2.m_id = 2;
    CHECK_FALSE(model.Apply(kSsobViewer, u2, 0));

    //激光笔不能改
    AnnotationMessage lb = b;
    lb.m_id = 3;
    lb.m_tool = annot_tool_laser;
    REQUIRE(model.Apply(kSsobViewer, lb, 100));
    AnnotationMessage le;
    le.m_op = annot_op_end;
    le.m_id = 3;
    REQUIRE(model.Apply(kSsobViewer, le, 100));
    AnnotationMessage u3 = u;
    u3.m_author = kSsobViewer;
    u3.m_id = 3;
    CHECK_FALSE(model.Apply(kSsobViewer, u3, 100));

    //文字可以改文字
    AnnotationMessage t;
    t.m_op = annot_op_text;
    t.m_id = 4;
    t.m_color = 0x000000;
    t.m_width = 28;
    t.m_points.push_back(AnnotationPoint(300, 300));
    t.m_text = "old";
    REQUIRE(model.Apply(kSsobHost, t, 0));
    AnnotationMessage ut;
    ut.m_op = annot_op_update;
    ut.m_author = kSsobHost;
    ut.m_id = 4;
    ut.m_color = 0x000000;
    ut.m_width = 28;
    ut.m_points.push_back(AnnotationPoint(300, 300));
    ut.m_text = "new";
    CHECK(model.Apply(kSsobViewer, ut, 0));
    CHECK(model.Find(AnnotationRef(kSsobHost, 4))->m_text == "new");

    //收回权限后观看者的修改不接受，发起人的照常
    model.SetViewersAllowed(false);
    AnnotationMessage u4 = u;
    u4.m_color = 0x8E24AA;
    CHECK_FALSE(model.Apply(kSsobViewer, u4, 0));
    CHECK(model.Apply(kSsobHost, u4, 0));
    CHECK(model.Find(AnnotationRef(kSsobHost, 1))->m_color == 0x8E24AAu);
}

TEST_CASE("SSOB3 模型的移动：多个对象一起动，各自限制在画面以内，跳过不存在与没画完的", "[shareobj]")
{
    ShareAnnotationModel model;
    ssobAdd(model, kSsobHost, 1, annot_tool_rect, 1000, 1000, 5000, 4000);
    ssobAdd(model, kSsobViewer, 7, annot_tool_ellipse, 60000, 20000, 65000, 30000);

    //两个一起右移 2000、上移 500：椭圆右边只剩 535，限制为 535
    AnnotationMessage mv = ssobMove(2000, -500);
    mv.m_targets.push_back(AnnotationRef(kSsobHost, 1));
    mv.m_targets.push_back(AnnotationRef(kSsobViewer, 7));
    mv.m_targets.push_back(AnnotationRef(kSsobViewer, 99));
    CHECK(model.Apply(kSsobViewer, mv, 0));
    const AnnotationStroke* r = model.Find(AnnotationRef(kSsobHost, 1));
    REQUIRE(r != NULL);
    CHECK(r->m_points[0].m_x == 3000);
    CHECK(r->m_points[0].m_y == 500);
    CHECK(r->m_points[1].m_x == 7000);
    CHECK(r->m_points[1].m_y == 3500);
    const AnnotationStroke* e = model.Find(AnnotationRef(kSsobViewer, 7));
    REQUIRE(e != NULL);
    CHECK(e->m_points[1].m_x == 65535);
    CHECK(e->m_points[0].m_x == 60535);
    CHECK(e->m_points[0].m_y == 19500);

    //上移超过上边：矩形顶边停在 0
    AnnotationMessage up = ssobMove(0, -20000);
    up.m_targets.push_back(AnnotationRef(kSsobHost, 1));
    CHECK(model.Apply(kSsobHost, up, 0));
    CHECK(model.Find(AnnotationRef(kSsobHost, 1))->m_points[0].m_y == 0);
    CHECK(model.Find(AnnotationRef(kSsobHost, 1))->m_points[1].m_y == 3000);
    //已经贴边再往上：没有变化
    CHECK_FALSE(model.Apply(kSsobHost, up, 0));
    //位移为 0、目标全都不存在：没有变化
    AnnotationMessage still = ssobMove(0, 0);
    still.m_targets.push_back(AnnotationRef(kSsobHost, 1));
    CHECK_FALSE(model.Apply(kSsobHost, still, 0));
    AnnotationMessage ghost = ssobMove(10, 10);
    ghost.m_targets.push_back(AnnotationRef(5, 5));
    CHECK_FALSE(model.Apply(kSsobHost, ghost, 0));

    //没画完的一笔不动
    AnnotationMessage b;
    b.m_op = annot_op_begin;
    b.m_id = 8;
    b.m_tool = annot_tool_pen;
    b.m_color = 0x000000;
    b.m_width = 4;
    b.m_points.push_back(AnnotationPoint(100, 100));
    REQUIRE(model.Apply(kSsobViewer, b, 0));
    AnnotationMessage pend = ssobMove(10, 10);
    pend.m_targets.push_back(AnnotationRef(kSsobViewer, 8));
    CHECK_FALSE(model.Apply(kSsobViewer, pend, 0));
    CHECK(model.Find(AnnotationRef(kSsobViewer, 8))->m_points[0].m_x == 100);

    //没有权限时观看者的移动不接受
    model.SetViewersAllowed(false);
    AnnotationMessage deny = ssobMove(10, 10);
    deny.m_targets.push_back(AnnotationRef(kSsobHost, 1));
    CHECK_FALSE(model.Apply(kSsobViewer, deny, 0));
}

TEST_CASE("SSOB4 连接线：被吸附对象擦除后脱开、移动时按是否同批决定脱开；整笔补发带吸附", "[shareobj]")
{
    ShareAnnotationModel model;
    //三个图元：A (1000,1000)-(3000,3000)、B (10000,1000)-(12000,3000)、E (20000,1000)-(22000,3000)
    AnnotationStyle iconStyle;
    iconStyle.m_variant = annot_icon_computer;
    ssobAdd(model, kSsobHost, 1, annot_tool_icon, 1000, 1000, 3000, 3000, iconStyle);
    ssobAdd(model, kSsobHost, 2, annot_tool_icon, 10000, 1000, 12000, 3000, iconStyle);
    ssobAdd(model, kSsobHost, 3, annot_tool_icon, 20000, 1000, 22000, 3000, iconStyle);
    //连接线 C：A → B；连接线 D：B → E
    AnnotationStyle c;
    c.m_attachStart = AnnotationRef(kSsobHost, 1);
    c.m_attachEnd = AnnotationRef(kSsobHost, 2);
    ssobAdd(model, kSsobViewer, 10, annot_tool_connector, 2000, 2000, 11000, 2000, c);
    AnnotationStyle d;
    d.m_attachStart = AnnotationRef(kSsobHost, 2);
    d.m_attachEnd = AnnotationRef(kSsobHost, 3);
    ssobAdd(model, kSsobViewer, 11, annot_tool_connector, 11000, 2000, 21000, 2000, d);

    //只移动图元 B：连接线照旧吸附，自身的点不变
    AnnotationMessage mb = ssobMove(0, 4000);
    mb.m_targets.push_back(AnnotationRef(kSsobHost, 2));
    CHECK(model.Apply(kSsobHost, mb, 0));
    const AnnotationStroke* cs = model.Find(AnnotationRef(kSsobViewer, 10));
    REQUIRE(cs != NULL);
    CHECK(cs->m_style.m_attachEnd == AnnotationRef(kSsobHost, 2));
    CHECK(cs->m_points[1].m_y == 2000);

    //擦除图元 A：C 的起点脱开，停在 A 的中心 (2000, 2000)
    AnnotationMessage er;
    er.m_op = annot_op_erase;
    er.m_author = kSsobHost;
    er.m_id = 1;
    CHECK(model.Apply(kSsobHost, er, 0));
    cs = model.Find(AnnotationRef(kSsobViewer, 10));
    REQUIRE(cs != NULL);
    CHECK_FALSE(cs->m_style.m_attachStart.IsSet());
    CHECK(cs->m_points[0].m_x == 2000);
    CHECK(cs->m_points[0].m_y == 2000);
    CHECK(cs->m_style.m_attachEnd == AnnotationRef(kSsobHost, 2));

    //B 与 D 一起下移 1000：D 的起点（吸附 B）保持吸附；终点（吸附 E，E 不在这一批）脱开，停在 E 的中心加位移
    AnnotationMessage md = ssobMove(0, 1000);
    md.m_targets.push_back(AnnotationRef(kSsobHost, 2));
    md.m_targets.push_back(AnnotationRef(kSsobViewer, 11));
    CHECK(model.Apply(kSsobViewer, md, 0));
    const AnnotationStroke* ds = model.Find(AnnotationRef(kSsobViewer, 11));
    REQUIRE(ds != NULL);
    CHECK(ds->m_style.m_attachStart == AnnotationRef(kSsobHost, 2));
    CHECK_FALSE(ds->m_style.m_attachEnd.IsSet());
    CHECK(ds->m_points[1].m_x == 21000);
    CHECK(ds->m_points[1].m_y == 3000);

    //整笔补发带上吸附
    const std::vector<AnnotationMessage> snap = model.Snapshot();
    bool found = false;
    for (size_t i = 0; i < snap.size(); ++i)
    {
        if (snap[i].m_author == kSsobViewer && snap[i].m_id == 11)
        {
            found = true;
            const AnnotationMessage rs = ssobRoundTrip(snap[i]);
            CHECK(rs.m_style.m_attachStart == AnnotationRef(kSsobHost, 2));
        }
    }
    CHECK(found);
}

namespace {

//记下发出的每个包体的发送通道
class SsobTransport : public ShareAudioTransport
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
AnnotationMessage ssobSent(const SsobTransport& t, size_t i)
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
AnnotationPoint ssobPt(int x, int y)
{
    return AnnotationPoint(static_cast<uint16_t>(x), static_cast<uint16_t>(y));
}

//一个已画完的对象
AnnotationStroke ssobObj(int author, uint32_t id, int tool, int x0, int y0, int x1, int y1)
{
    AnnotationStroke s;
    s.m_author = author;
    s.m_id = id;
    s.m_tool = tool;
    s.m_color = 0x000000;
    s.m_width = 4;
    s.m_points.push_back(ssobPt(x0, y0));
    s.m_points.push_back(ssobPt(x1, y1));
    s.m_finished = true;
    return s;
}

//矩形各边与期望值之差都不超过 tol
bool ssobNear(const AnnotRect& r, int l, int t, int rr, int b, int tol)
{
    return abs(r.m_left - l) <= tol && abs(r.m_top - t) <= tol && abs(r.m_right - rr) <= tol && abs(r.m_bottom - b) <= tol;
}

//会话里用当前工具从 a 拖到 b
void ssobDrag(ShareAnnotationSession& s, int x0, int y0, int x1, int y1, int64_t now)
{
    s.PointerDown(ssobPt(x0, y0), 100, now);
    s.PointerMove(ssobPt(x1, y1), 100, now + 1);
    s.PointerUp(ssobPt(x1, y1), now + 2);
}

//以 seat 的身份经 OnRemoteFrame 加一个两点对象；text 不为空时再发一条修改带上文字
void ssobRemoteAdd(ShareAnnotationSession& s, int seat, uint32_t id, int tool, int x0, int y0, int x1, int y1,
                   const AnnotationStyle& style, const std::string& text = std::string())
{
    AnnotationMessage b;
    b.m_op = annot_op_begin;
    b.m_id = id;
    b.m_tool = tool;
    b.m_color = 0x546E7A;
    b.m_width = 4;
    b.m_points.push_back(ssobPt(x0, y0));
    b.m_points.push_back(ssobPt(x1, y1));
    b.m_style = style;
    REQUIRE(s.OnRemoteFrame(seat, ShareAnnotationCodec::encode(b), 0));
    AnnotationMessage e;
    e.m_op = annot_op_end;
    e.m_id = id;
    REQUIRE(s.OnRemoteFrame(seat, ShareAnnotationCodec::encode(e), 0));
    if (!text.empty())
    {
        AnnotationMessage u;
        u.m_op = annot_op_update;
        u.m_author = seat;
        u.m_id = id;
        u.m_color = b.m_color;
        u.m_width = b.m_width;
        u.m_points = b.m_points;
        u.m_text = text;
        u.m_style = style;
        REQUIRE(s.OnRemoteFrame(seat, ShareAnnotationCodec::encode(u), 0));
    }
}

//宽高比：宽是高的两倍
const double kSsobAspect = 2.0;

} // namespace

TEST_CASE("SSOB5 对象几何：外接矩形、文字近似尺寸、连接线两端裁边、点选与框选、可吸附", "[shareobj]")
{
    //文字近似尺寸：字号 100、宽高比 2，ASCII 半个字宽、中文一个字宽，行高 1.3 倍（"ab" 加 UTF-8 的「中」）
    int w = 0;
    int h = 0;
    AnnotApproxTextSize("ab\xE4\xB8\xAD", 100, kSsobAspect, w, h);
    CHECK(abs(w - 6881) <= 2);
    CHECK(abs(h - 8520) <= 2);
    AnnotApproxTextSize("a\nbb", 100, kSsobAspect, w, h);
    CHECK(abs(w - 3604) <= 2);
    CHECK(abs(h - 17039) <= 2);

    std::vector<AnnotationStroke> all;
    all.push_back(ssobObj(0, 1, annot_tool_rect, 1000, 1000, 5000, 4000));       //0 矩形
    all.push_back(ssobObj(0, 2, annot_tool_ellipse, 2000, 2000, 4000, 3000));    //1 椭圆（在矩形上面）
    AnnotationStroke pen = ssobObj(0, 3, annot_tool_pen, 10000, 10000, 12000, 10000);
    pen.m_points.push_back(ssobPt(12000, 12000));
    all.push_back(pen);                                                           //2 画笔
    AnnotationStroke text = ssobObj(0, 4, annot_tool_text, 20000, 20000, 0, 0);
    text.m_points.resize(1);
    text.m_text = "hello";
    text.m_width = 28;
    all.push_back(text);                                                          //3 文字
    all.push_back(ssobObj(0, 5, annot_tool_laser, 49000, 49000, 51000, 51000));  //4 激光笔
    all.push_back(ssobObj(0, 6, annot_tool_eraser_area, 51000, 51000, 53000, 53000)); //5 局部橡皮
    AnnotationStroke pending = ssobObj(0, 7, annot_tool_pen, 30000, 30000, 31000, 31000);
    pending.m_finished = false;
    all.push_back(pending);                                                       //6 没画完的画笔
    AnnotationStroke icon = ssobObj(0, 8, annot_tool_icon, 44000, 46000, 40000, 40000);
    icon.m_style.m_variant = annot_icon_server;
    all.push_back(icon);                                                          //7 图元（两点反着给）
    AnnotationStroke badge = ssobObj(0, 9, annot_tool_badge, 30000, 50000, 0, 0);
    badge.m_points.resize(1);
    badge.m_width = 32;
    all.push_back(badge);                                                         //8 数字标号

    //外接矩形
    CHECK(ssobNear(AnnotObjectBounds(all[0], all, kSsobAspect), 1000, 1000, 5000, 4000, 0));
    CHECK(ssobNear(AnnotObjectBounds(all[2], all, kSsobAspect), 10000, 10000, 12000, 12000, 0));
    CHECK(ssobNear(AnnotObjectBounds(all[7], all, kSsobAspect), 40000, 40000, 44000, 46000, 0));
    AnnotApproxTextSize("hello", 28, kSsobAspect, w, h);
    CHECK(ssobNear(AnnotObjectBounds(all[3], all, kSsobAspect), 20000, 20000, 20000 + w, 20000 + h, 0));
    CHECK(ssobNear(AnnotObjectBounds(all[8], all, kSsobAspect), 30000 - 839, 50000 - 1678, 30000 + 839, 50000 + 1678, 2));

    //可选、可吸附、两端可拖
    CHECK(AnnotIsSelectable(all[0]));
    CHECK_FALSE(AnnotIsSelectable(all[4]));
    CHECK_FALSE(AnnotIsSelectable(all[5]));
    CHECK_FALSE(AnnotIsSelectable(all[6]));
    CHECK(AnnotIsAttachable(all[7]));
    CHECK(AnnotIsAttachable(all[1]));
    CHECK(AnnotIsAttachable(all[3]));
    CHECK_FALSE(AnnotIsAttachable(all[2]));
    CHECK(AnnotIsEndpointObject(ssobObj(0, 50, annot_tool_line, 0, 0, 10, 10)));
    CHECK(AnnotIsEndpointObject(ssobObj(0, 51, annot_tool_arrow, 0, 0, 10, 10)));
    CHECK(AnnotIsEndpointObject(ssobObj(0, 52, annot_tool_connector, 0, 0, 10, 10)));
    CHECK_FALSE(AnnotIsEndpointObject(all[0]));
    CHECK_FALSE(AnnotIsAttachable(ssobObj(0, 52, annot_tool_connector, 0, 0, 10, 10)));

    //点选：重叠时取上面的；封闭形状点在里面算；橡皮只认边线
    CHECK(AnnotHitObject(all, ssobPt(3000, 2500), 100, kSsobAspect, true) == 1);
    CHECK(AnnotHitObject(all, ssobPt(3000, 2500), 100, kSsobAspect, false) == -1);
    CHECK(AnnotHitObject(all, ssobPt(1050, 2500), 100, kSsobAspect, false) == 0);
    CHECK(AnnotHitObject(all, ssobPt(4500, 3500), 100, kSsobAspect, true) == 0);
    CHECK(AnnotHitObject(all, ssobPt(11000, 10050), 100, kSsobAspect, true) == 2);
    CHECK(AnnotHitObject(all, ssobPt(11000, 11000), 100, kSsobAspect, true) == -1);
    CHECK(AnnotHitObject(all, ssobPt(20500, 20500), 100, kSsobAspect, true) == 3);
    CHECK(AnnotHitObject(all, ssobPt(50000, 50000), 100, kSsobAspect, true) == -1);
    CHECK(AnnotHitObject(all, ssobPt(52000, 52000), 100, kSsobAspect, true) == -1);
    CHECK(AnnotHitObject(all, ssobPt(30500, 30500), 100, kSsobAspect, true) == -1);
    CHECK(AnnotHitObject(all, ssobPt(42000, 43000), 100, kSsobAspect, true) == 7);
    CHECK(AnnotHitObject(all, ssobPt(42000, 43000), 100, kSsobAspect, false) == 7);
    CHECK(AnnotHitObject(all, ssobPt(30000, 50000), 100, kSsobAspect, false) == 8);

    //框选：完整落在框里的
    std::vector<int> in = AnnotObjectsInRect(all, AnnotRect(0, 0, 6000, 6000), kSsobAspect);
    REQUIRE(in.size() == 2);
    CHECK(in[0] == 0);
    CHECK(in[1] == 1);
    in = AnnotObjectsInRect(all, AnnotRect(9000, 9000, 13000, 13000), kSsobAspect);
    REQUIRE(in.size() == 1);
    CHECK(in[0] == 2);
    CHECK(AnnotObjectsInRect(all, AnnotRect(41000, 41000, 43000, 43000), kSsobAspect).empty());

    //可吸附：图元、椭圆可以，画笔不行，排除自己
    CHECK(AnnotHitAttachable(all, ssobPt(42000, 43000), 100, kSsobAspect, AnnotationRef()) == 7);
    CHECK(AnnotHitAttachable(all, ssobPt(42000, 43000), 100, kSsobAspect, AnnotationRef(0, 8)) == -1);
    CHECK(AnnotHitAttachable(all, ssobPt(11000, 10050), 100, kSsobAspect, AnnotationRef()) == -1);
    CHECK(AnnotHitAttachable(all, ssobPt(3000, 2500), 100, kSsobAspect, AnnotationRef()) == 1);

    //连接线两端：从被吸附图元的中心出发，裁到边外 kAnnotConnectorGap（按高度计；横向除以宽高比）
    std::vector<AnnotationStroke> net;
    net.push_back(ssobObj(0, 1, annot_tool_icon, 1000, 20000, 3000, 22000));    //A 中心 (2000, 21000)
    net.push_back(ssobObj(0, 2, annot_tool_icon, 11000, 20000, 13000, 22000));  //B 中心 (12000, 21000)
    net.push_back(ssobObj(0, 3, annot_tool_icon, 1000, 40000, 3000, 44000));    //C 中心 (2000, 42000)
    AnnotationStroke ab = ssobObj(3, 10, annot_tool_connector, 0, 0, 100, 100);
    ab.m_style.m_attachStart = AnnotationRef(0, 1);
    ab.m_style.m_attachEnd = AnnotationRef(0, 2);
    net.push_back(ab);
    AnnotationStroke ac = ssobObj(3, 11, annot_tool_connector, 0, 0, 100, 100);
    ac.m_style.m_attachStart = AnnotationRef(0, 1);
    ac.m_style.m_attachEnd = AnnotationRef(0, 3);
    net.push_back(ac);
    AnnotationPoint from;
    AnnotationPoint to;
    AnnotConnectorEnds(net[3], net, kSsobAspect, from, to);
    CHECK(abs(from.m_x - 3060) <= 2);
    CHECK(from.m_y == 21000);
    CHECK(abs(to.m_x - 10940) <= 2);
    CHECK(to.m_y == 21000);
    AnnotConnectorEnds(net[4], net, kSsobAspect, from, to);
    CHECK(from.m_x == 2000);
    CHECK(abs(from.m_y - 22120) <= 2);
    CHECK(abs(to.m_y - 39880) <= 2);
    //连接线的外接矩形按实际两端
    CHECK(ssobNear(AnnotObjectBounds(net[3], net, kSsobAspect), 3060, 21000, 10940, 21000, 2));
    //没有吸附、被吸附的对象不存在：用自己的点
    AnnotationStroke loose = ssobObj(3, 12, annot_tool_connector, 500, 600, 700, 800);
    loose.m_style.m_attachEnd = AnnotationRef(9, 99);
    AnnotConnectorEnds(loose, net, kSsobAspect, from, to);
    CHECK(from.m_x == 500);
    CHECK(from.m_y == 600);
    CHECK(to.m_x == 700);
    CHECK(to.m_y == 800);
}

TEST_CASE("SSOB6 句柄、按句柄缩放与把对象缩放到新矩形", "[shareobj]")
{
    std::vector<AnnotationStroke> all;
    all.push_back(ssobObj(0, 1, annot_tool_rect, 1000, 1000, 5000, 4000));
    all.push_back(ssobObj(0, 2, annot_tool_line, 100, 100, 900, 900));

    //外接矩形的八个句柄，顺序同 AnnotHandle
    const std::vector<AnnotationPoint> hs = AnnotHandlePositions(all[0], all, kSsobAspect);
    REQUIRE(hs.size() == static_cast<size_t>(kAnnotBoxHandleCount));
    const int expect[kAnnotBoxHandleCount][2] = { { 1000, 1000 }, { 3000, 1000 }, { 5000, 1000 }, { 5000, 2500 },
                                                  { 5000, 4000 }, { 3000, 4000 }, { 1000, 4000 }, { 1000, 2500 } };
    for (int i = 0; i < kAnnotBoxHandleCount; ++i)
    {
        INFO("handle " << i);
        CHECK(hs[i].m_x == expect[i][0]);
        CHECK(hs[i].m_y == expect[i][1]);
    }
    CHECK(AnnotHitHandle(hs, false, ssobPt(5040, 3960), 100) == annot_handle_se);
    CHECK(AnnotHitHandle(hs, false, ssobPt(3000, 1000), 100) == annot_handle_n);
    CHECK(AnnotHitHandle(hs, false, ssobPt(3000, 2500), 100) == annot_handle_none);
    //直线：两端
    const std::vector<AnnotationPoint> es = AnnotHandlePositions(all[1], all, kSsobAspect);
    REQUIRE(es.size() == 2);
    CHECK(es[0].m_x == 100);
    CHECK(es[1].m_x == 900);
    CHECK(AnnotHitHandle(es, true, ssobPt(890, 905), 100) == annot_handle_end);
    CHECK(AnnotHitHandle(es, true, ssobPt(110, 90), 100) == annot_handle_start);
    CHECK(AnnotHitHandle(es, true, ssobPt(500, 500), 100) == annot_handle_none);

    //按句柄缩放
    const AnnotRect r(1000, 1000, 5000, 4000);
    CHECK(ssobNear(AnnotResizeRect(r, annot_handle_se, ssobPt(7000, 6000), false), 1000, 1000, 7000, 6000, 0));
    CHECK(ssobNear(AnnotResizeRect(r, annot_handle_e, ssobPt(8000, 123), false), 1000, 1000, 8000, 4000, 0));
    CHECK(ssobNear(AnnotResizeRect(r, annot_handle_n, ssobPt(123, 500), false), 1000, 500, 5000, 4000, 0));
    //拖过对边：不翻转，留最小边长
    CHECK(ssobNear(AnnotResizeRect(r, annot_handle_nw, ssobPt(6000, 5000), false), 5000 - kAnnotMinObjectSize,
                   4000 - kAnnotMinObjectSize, 5000, 4000, 0));
    //保持比例：取两个方向里较大的倍数
    CHECK(ssobNear(AnnotResizeRect(r, annot_handle_se, ssobPt(9000, 5000), true), 1000, 1000, 9000, 7000, 0));
    //边上的句柄不管比例
    CHECK(ssobNear(AnnotResizeRect(r, annot_handle_e, ssobPt(9000, 5000), true), 1000, 1000, 9000, 4000, 0));

    //把对象缩放到新矩形
    const AnnotRect big(1000, 1000, 9000, 7000);
    const AnnotationStroke rs = AnnotScaleObject(all[0], r, big);
    CHECK(rs.m_points[0].m_x == 1000);
    CHECK(rs.m_points[1].m_x == 9000);
    CHECK(rs.m_points[1].m_y == 7000);
    CHECK(rs.m_width == all[0].m_width);
    CHECK(rs.m_id == 1u);
    AnnotationStroke pen = ssobObj(0, 3, annot_tool_pen, 1000, 1000, 5000, 4000);
    pen.m_points.insert(pen.m_points.begin() + 1, ssobPt(3000, 2500));
    const AnnotationStroke ps = AnnotScaleObject(pen, r, big);
    CHECK(ps.m_points[1].m_x == 5000);
    CHECK(ps.m_points[1].m_y == 4000);
    //文字：左上角不动、字号按高度比例，不超过 255
    AnnotationStroke text = ssobObj(0, 4, annot_tool_text, 10000, 10000, 0, 0);
    text.m_points.resize(1);
    text.m_text = "ab";
    text.m_width = 100;
    const AnnotRect tr(10000, 10000, 16881, 18519);
    CHECK(AnnotScaleObject(text, tr, AnnotRect(10000, 10000, 16881, 27038)).m_width == 200);
    CHECK(AnnotScaleObject(text, tr, AnnotRect(10000, 10000, 16881, 35557)).m_width == 255);
    CHECK(AnnotScaleObject(text, tr, AnnotRect(10000, 10000, 16881, 27038)).m_points[0].m_x == 10000);
    //数字标号：中心换算，大小按较小的比例
    AnnotationStroke badge = ssobObj(0, 5, annot_tool_badge, 30000, 30000, 0, 0);
    badge.m_points.resize(1);
    badge.m_width = 32;
    const AnnotRect br(29161, 28322, 30839, 31678);
    const AnnotationStroke bs = AnnotScaleObject(badge, br, AnnotRect(29161, 28322, 31678, 33356));
    CHECK(bs.m_width == 48);
    CHECK(abs(bs.m_points[0].m_x - 30419) <= 2);
    CHECK(abs(bs.m_points[0].m_y - 30839) <= 2);
}

TEST_CASE("SSOB7 会话的选择工具：点选、拖动移动（按间隔发出）、框选、Shift 加减选、删除、别人删掉后去掉", "[shareobj]")
{
    SsobTransport t;
    ShareAnnotationSession s;
    s.Start(&t, kSsobHost, 100);
    s.SetContentAspect(kSsobAspect);
    s.SetTool(annot_tool_rect);
    ssobDrag(s, 1000, 1000, 5000, 4000, 0);
    s.SetTool(annot_tool_ellipse);
    ssobDrag(s, 20000, 20000, 24000, 23000, 10);
    REQUIRE(s.Model().Strokes().size() == 2);
    const AnnotationRef a(kSsobHost, s.Model().Strokes()[0].m_id);
    const AnnotationRef b(kSsobHost, s.Model().Strokes()[1].m_id);

    s.SetTool(kAnnotSelect);
    size_t sent = t.m_bodies.size();
    //点中矩形：选中，单选有八个句柄
    s.PointerDown(ssobPt(3000, 2500), 100, 1000);
    REQUIRE(s.Selection().size() == 1);
    CHECK(s.Selection()[0] == a);
    AnnotSelectionVisual v = s.SelectionVisual();
    REQUIRE(v.m_boxes.size() == 1);
    CHECK(ssobNear(v.m_boxes[0], 1000, 1000, 5000, 4000, 0));
    CHECK(v.m_handles.size() == static_cast<size_t>(kAnnotBoxHandleCount));
    CHECK_FALSE(v.m_endpointHandles);
    //拖动：不到间隔先不发，到了间隔发一次，松开时把剩下的发出
    s.PointerMove(ssobPt(3500, 2500), 100, 1010);
    CHECK(t.m_bodies.size() == sent);
    s.PointerMove(ssobPt(4000, 3000), 100, 1050);
    REQUIRE(t.m_bodies.size() == sent + 1);
    AnnotationMessage m1 = ssobSent(t, sent);
    CHECK(m1.m_op == annot_op_move);
    REQUIRE(m1.m_targets.size() == 1);
    CHECK(m1.m_targets[0] == a);
    CHECK(m1.m_dx == 1000);
    CHECK(m1.m_dy == 500);
    s.PointerUp(ssobPt(4200, 3000), 1060);
    REQUIRE(t.m_bodies.size() == sent + 2);
    AnnotationMessage m2 = ssobSent(t, sent + 1);
    CHECK(m2.m_dx == 200);
    CHECK(m2.m_dy == 0);
    const AnnotationStroke* ra = s.Model().Find(a);
    REQUIRE(ra != NULL);
    CHECK(ra->m_points[0].m_x == 2200);
    CHECK(ra->m_points[0].m_y == 1500);
    CHECK(ra->m_points[1].m_x == 6200);

    //点空白：取消选择，拖出选择框框住两个
    s.PointerDown(ssobPt(60000, 60000), 100, 2000);
    CHECK(s.Selection().empty());
    s.PointerMove(ssobPt(100, 100), 100, 2010);
    v = s.SelectionVisual();
    CHECK(v.m_hasBand);
    CHECK(ssobNear(v.m_band, 100, 100, 60000, 60000, 0));
    s.PointerUp(ssobPt(100, 100), 2020);
    REQUIRE(s.Selection().size() == 2);
    CHECK_FALSE(s.SelectionVisual().m_hasBand);
    CHECK(s.SelectionVisual().m_handles.empty());

    //按住已选中的椭圆拖：两个一起动
    sent = t.m_bodies.size();
    s.PointerDown(ssobPt(22000, 21500), 100, 3000);
    s.PointerMove(ssobPt(22000, 22500), 100, 3100);
    s.PointerUp(ssobPt(22000, 22500), 3110);
    REQUIRE(t.m_bodies.size() == sent + 1);
    CHECK(ssobSent(t, sent).m_targets.size() == 2);
    CHECK(ssobSent(t, sent).m_dy == 1000);
    CHECK(s.Model().Find(b)->m_points[0].m_y == 21000);

    //按住 Shift 点矩形：从选择里去掉；只点不拖不发任何操作
    sent = t.m_bodies.size();
    s.SetShiftDown(true);
    s.PointerDown(ssobPt(4000, 3000), 100, 4000);
    s.PointerUp(ssobPt(4000, 3000), 4010);
    s.SetShiftDown(false);
    REQUIRE(s.Selection().size() == 1);
    CHECK(s.Selection()[0] == b);
    CHECK(t.m_bodies.size() == sent);

    //删除选中的：发擦除、选择清空
    CHECK(s.DeleteSelection());
    REQUIRE(t.m_bodies.size() == sent + 1);
    CHECK(ssobSent(t, sent).m_op == annot_op_erase);
    CHECK(s.Selection().empty());
    CHECK(s.Model().Find(b) == NULL);
    CHECK_FALSE(s.DeleteSelection());

    //再选中矩形，别人把它删了：从选择里去掉
    s.PointerDown(ssobPt(4000, 3000), 100, 5000);
    s.PointerUp(ssobPt(4000, 3000), 5010);
    REQUIRE(s.Selection().size() == 1);
    AnnotationMessage er;
    er.m_op = annot_op_erase;
    er.m_author = a.m_author;
    er.m_id = a.m_id;
    CHECK(s.OnRemoteFrame(kSsobViewer, ShareAnnotationCodec::encode(er), 5020));
    CHECK(s.Selection().empty());

    //换别的工具：取消选择
    s.SetTool(annot_tool_rect);
    ssobDrag(s, 1000, 1000, 2000, 2000, 6000);
    s.SetTool(kAnnotSelect);
    s.PointerDown(ssobPt(1500, 1500), 100, 7000);
    s.PointerUp(ssobPt(1500, 1500), 7010);
    REQUIRE(s.Selection().size() == 1);
    s.SetTool(annot_tool_pen);
    CHECK(s.Selection().empty());
}

TEST_CASE("SSOB8 会话：拖句柄缩放松开时发一次修改；拖连接线的一端落到图元上就吸附", "[shareobj]")
{
    SsobTransport t;
    ShareAnnotationSession s;
    s.Start(&t, kSsobHost, 100);
    s.SetContentAspect(kSsobAspect);
    s.SetTool(annot_tool_rect);
    ssobDrag(s, 1000, 1000, 5000, 4000, 0);
    const AnnotationRef a(kSsobHost, s.Model().Strokes()[0].m_id);
    s.SetTool(kAnnotSelect);
    s.PointerDown(ssobPt(3000, 2500), 100, 100);
    s.PointerUp(ssobPt(3000, 2500), 110);
    REQUIRE(s.Selection().size() == 1);

    //拖右下角句柄：拖动中只改本地，松开时发一条修改
    const size_t sent = t.m_bodies.size();
    s.PointerDown(ssobPt(5000, 4000), 100, 200);
    s.PointerMove(ssobPt(7000, 6000), 100, 300);
    CHECK(t.m_bodies.size() == sent);
    CHECK(s.Model().Find(a)->m_points[1].m_x == 7000);
    s.PointerUp(ssobPt(9000, 7000), 400);
    REQUIRE(t.m_bodies.size() == sent + 1);
    AnnotationMessage u = ssobSent(t, sent);
    CHECK(u.m_op == annot_op_update);
    CHECK(u.m_author == a.m_author);
    CHECK(u.m_id == a.m_id);
    REQUIRE(u.m_points.size() == 2);
    CHECK(u.m_points[0].m_x == 1000);
    CHECK(u.m_points[1].m_x == 9000);
    CHECK(u.m_points[1].m_y == 7000);
    //按住 Shift 拖角：保持比例（8000×6000 拖到 16000×8000，取较大的 2 倍）
    s.SetShiftDown(true);
    s.PointerDown(ssobPt(9000, 7000), 100, 500);
    s.PointerUp(ssobPt(17000, 9000), 600);
    s.SetShiftDown(false);
    CHECK(s.Model().Find(a)->m_points[1].m_x == 17000);
    CHECK(s.Model().Find(a)->m_points[1].m_y == 13000);

    //别人画的图元与连接线
    AnnotationStyle iconStyle;
    iconStyle.m_variant = annot_icon_switch;
    ssobRemoteAdd(s, kSsobViewer, 30, annot_tool_icon, 21000, 29000, 23000, 33000, iconStyle);
    ssobRemoteAdd(s, kSsobViewer, 31, annot_tool_connector, 2000, 30000, 8000, 30000, AnnotationStyle());
    const AnnotationRef icon(kSsobViewer, 30);
    const AnnotationRef conn(kSsobViewer, 31);
    //点中连接线：两端句柄
    s.PointerDown(ssobPt(5000, 30000), 100, 700);
    s.PointerUp(ssobPt(5000, 30000), 710);
    REQUIRE(s.Selection().size() == 1);
    CHECK(s.Selection()[0] == conn);
    AnnotSelectionVisual v = s.SelectionVisual();
    CHECK(v.m_endpointHandles);
    REQUIRE(v.m_handles.size() == 2);
    //拖终点到图元上：吸附
    const size_t sent2 = t.m_bodies.size();
    s.PointerDown(ssobPt(8000, 30000), 100, 800);
    s.PointerMove(ssobPt(15000, 30500), 100, 810);
    s.PointerUp(ssobPt(22000, 31000), 820);
    REQUIRE(t.m_bodies.size() == sent2 + 1);
    AnnotationMessage cu = ssobSent(t, sent2);
    CHECK(cu.m_op == annot_op_update);
    CHECK(cu.m_style.m_attachEnd == icon);
    CHECK_FALSE(cu.m_style.m_attachStart.IsSet());
    const AnnotationStroke* c = s.Model().Find(conn);
    REQUIRE(c != NULL);
    CHECK(c->m_style.m_attachEnd == icon);
    //再把终点（现在在图元左边上）拖到空白处：脱开
    v = s.SelectionVisual();
    REQUIRE(v.m_handles.size() == 2);
    s.PointerDown(v.m_handles[1], 100, 900);
    s.PointerUp(ssobPt(20000, 50000), 910);
    c = s.Model().Find(conn);
    REQUIRE(c != NULL);
    CHECK_FALSE(c->m_style.m_attachEnd.IsSet());
    CHECK(c->m_points.back().m_x == 20000);
    CHECK(c->m_points.back().m_y == 50000);
}

TEST_CASE("SSOB9 会话：改文字（文字、图元名字、标号号码）、改选中对象的样式", "[shareobj]")
{
    SsobTransport t;
    ShareAnnotationSession s;
    s.Start(&t, kSsobHost, 100);
    s.SetContentAspect(kSsobAspect);
    s.SetTool(annot_tool_text);
    REQUIRE(s.AddText(ssobPt(20000, 20000), "hello"));
    const AnnotationRef text(kSsobHost, s.Model().Strokes().back().m_id);
    s.SetTool(annot_tool_badge);
    s.PointerDown(ssobPt(40000, 40000), 100, 0);
    s.PointerUp(ssobPt(40000, 40000), 10);
    const AnnotationRef badge(kSsobHost, s.Model().Strokes().back().m_id);
    AnnotationStyle iconStyle;
    iconStyle.m_variant = annot_icon_computer;
    ssobRemoteAdd(s, kSsobViewer, 30, annot_tool_icon, 50000, 10000, 54000, 14000, iconStyle, "PC");
    const AnnotationRef icon(kSsobViewer, 30);

    //能改文字的对象
    AnnotationRef ref;
    std::string current;
    REQUIRE(s.EditableAt(ssobPt(20300, 20300), 100, ref, current));
    CHECK(ref == text);
    CHECK(current == "hello");
    REQUIRE(s.EditableAt(ssobPt(52000, 12000), 100, ref, current));
    CHECK(ref == icon);
    CHECK(current == "PC");
    REQUIRE(s.EditableAt(ssobPt(40000, 40000), 100, ref, current));
    CHECK(ref == badge);
    CHECK(current == "1");
    CHECK_FALSE(s.EditableAt(ssobPt(60000, 60000), 100, ref, current));

    //改文字：发修改；内容相同不发
    size_t sent = t.m_bodies.size();
    CHECK(s.SetObjectText(text, "world"));
    REQUIRE(t.m_bodies.size() == sent + 1);
    CHECK(ssobSent(t, sent).m_op == annot_op_update);
    CHECK(s.Model().Find(text)->m_text == "world");
    CHECK_FALSE(s.SetObjectText(text, "world"));
    //图元名字
    CHECK(s.SetObjectText(icon, "Core-SW"));
    CHECK(s.Model().Find(icon)->m_text == "Core-SW");
    //标号：文字当号码，不是 1~65535 的数字不改
    CHECK(s.SetObjectText(badge, "7"));
    CHECK(s.Model().Find(badge)->m_style.m_number == 7);
    CHECK_FALSE(s.SetObjectText(badge, "x"));
    CHECK_FALSE(s.SetObjectText(badge, "0"));
    CHECK(s.Model().Find(badge)->m_style.m_number == 7);
    //文字改成空：删掉
    sent = t.m_bodies.size();
    CHECK(s.SetObjectText(text, ""));
    REQUIRE(t.m_bodies.size() == sent + 1);
    CHECK(ssobSent(t, sent).m_op == annot_op_erase);
    CHECK(s.Model().Find(text) == NULL);

    //改样式：没有选中对象时不改
    CHECK_FALSE(s.RestyleSelection(annot_restyle_color, 0x00FF00));
    //画一个矩形与一笔画笔，框选两个
    s.SetTool(annot_tool_rect);
    ssobDrag(s, 1000, 1000, 5000, 4000, 100);
    s.SetTool(annot_tool_pen);
    ssobDrag(s, 2000, 5000, 4000, 6000, 200);
    const AnnotationRef rect(kSsobHost, s.Model().Strokes()[s.Model().Strokes().size() - 2].m_id);
    const AnnotationRef pen(kSsobHost, s.Model().Strokes().back().m_id);
    s.SetTool(kAnnotSelect);
    s.PointerDown(ssobPt(500, 500), 100, 300);
    s.PointerMove(ssobPt(8000, 8000), 100, 310);
    s.PointerUp(ssobPt(8000, 8000), 320);
    REQUIRE(s.Selection().size() == 2);
    //颜色：两个都改
    sent = t.m_bodies.size();
    CHECK(s.RestyleSelection(annot_restyle_color, 0x00FF00));
    CHECK(t.m_bodies.size() == sent + 2);
    CHECK(s.Model().Find(rect)->m_color == 0x00FF00u);
    CHECK(s.Model().Find(pen)->m_color == 0x00FF00u);
    //同样的颜色再来一次：没有变化
    CHECK_FALSE(s.RestyleSelection(annot_restyle_color, 0x00FF00));
    //填充、线型只对矩形
    sent = t.m_bodies.size();
    CHECK(s.RestyleSelection(annot_restyle_fill, annot_fill_solid));
    CHECK(s.RestyleSelection(annot_restyle_line_style, annot_line_dash));
    CHECK(t.m_bodies.size() == sent + 2);
    CHECK(s.Model().Find(rect)->m_style.m_fill == annot_fill_solid);
    CHECK(s.Model().Find(rect)->m_style.m_lineStyle == annot_line_dash);
    CHECK(s.Model().Find(pen)->m_style.m_lineStyle == annot_line_solid);
    //粗细：按各自种类的档位
    CHECK(s.RestyleSelection(annot_restyle_width, annot_width_thick));
    CHECK(s.Model().Find(rect)->m_width == 8);
    CHECK(s.Model().Find(pen)->m_width == 8);
    //图元种类只对图元：选中的里没有图元，不改
    CHECK_FALSE(s.RestyleSelection(annot_restyle_icon, annot_icon_router));
    s.PointerDown(ssobPt(52000, 12000), 100, 400);
    s.PointerUp(ssobPt(52000, 12000), 410);
    REQUIRE(s.Selection().size() == 1);
    CHECK(s.RestyleSelection(annot_restyle_icon, annot_icon_router));
    CHECK(s.Model().Find(icon)->m_style.m_variant == annot_icon_router);
}

namespace {

//图层上 (x, y) 周围 r 像素以内最大的不透明度
int ssobAlphaNear(const std::vector<uint8_t>& bgra, int width, int height, int x, int y, int r)
{
    const int kBpp = 4;
    int best = 0;
    for (int yy = y - r; yy <= y + r; ++yy)
    {
        for (int xx = x - r; xx <= x + r; ++xx)
        {
            if (xx < 0 || yy < 0 || xx >= width || yy >= height)
                continue;
            const int a = bgra[(static_cast<size_t>(yy) * width + xx) * kBpp + 3];
            best = a > best ? a : best;
        }
    }
    return best;
}

//图层上 (x, y) 处的一个通道（0 蓝、1 绿、2 红、3 不透明度）
int ssobChannel(const std::vector<uint8_t>& bgra, int width, int x, int y, int c)
{
    const int kBpp = 4;
    return bgra[(static_cast<size_t>(y) * width + x) * kBpp + c];
}

//像素换成归一化坐标（图层宽 w、高 h）
int ssobNx(int px, int w)
{
    return px * kAnnotCoordMax / w;
}

int ssobNy(int py, int h)
{
    return py * kAnnotCoordMax / h;
}

} // namespace

TEST_CASE("SSOB10 网络拓扑图元：十一种都有矢量图形、坐标在单位方框里、彼此不同；非法种类为空", "[shareobj]")
{
    std::vector<double> signatures;
    for (int icon = 0; icon < kAnnotIconCount; ++icon)
    {
        INFO("icon " << icon);
        const std::vector<AnnotIconPrim> prims = AnnotIconPrimitives(icon);
        CHECK(prims.size() >= 2);
        double sig = static_cast<double>(prims.size());
        for (size_t i = 0; i < prims.size(); ++i)
        {
            CHECK_FALSE(prims[i].m_points.empty());
            if (prims[i].m_kind == annot_prim_ellipse || prims[i].m_kind == annot_prim_round_rect)
                CHECK(prims[i].m_points.size() == 2);
            if (prims[i].m_kind == annot_prim_polygon)
                CHECK(prims[i].m_points.size() >= 3);
            for (size_t k = 0; k < prims[i].m_points.size(); ++k)
            {
                CHECK(prims[i].m_points[k].m_x >= 0.0);
                CHECK(prims[i].m_points[k].m_x <= 1.0);
                CHECK(prims[i].m_points[k].m_y >= 0.0);
                CHECK(prims[i].m_points[k].m_y <= 1.0);
                sig += prims[i].m_points[k].m_x * (k + 1) + prims[i].m_points[k].m_y * (i + 2);
            }
        }
        for (size_t j = 0; j < signatures.size(); ++j)
            CHECK(signatures[j] != sig);
        signatures.push_back(sig);
    }
    CHECK(AnnotIconPrimitives(-1).empty());
    CHECK(AnnotIconPrimitives(kAnnotIconCount).empty());
}

TEST_CASE("SSOB11 图层绘制：图元按矢量图画、名字在下方；连接线按吸附的两端画；选择提示", "[shareobj]")
{
    const int w = 400;
    const int h = 200;
    std::vector<AnnotationStroke> all;
    //电脑图元：像素 (40,20)-(120,100)，名字「PC」
    AnnotationStroke icon = ssobObj(0, 1, annot_tool_icon, ssobNx(40, w), ssobNy(20, h), ssobNx(120, w), ssobNy(100, h));
    icon.m_style.m_variant = annot_icon_computer;
    icon.m_width = 10;
    icon.m_color = 0x1E88E5;
    icon.m_text = "PC";
    all.push_back(icon);
    std::vector<uint8_t> bgra;
    RenderAnnotationLayer(w, h, all, NULL, 0, true, bgra);
    //显示器上边的中点（单位方框 (0.5, 0.06)）有描边
    CHECK(ssobAlphaNear(bgra, w, h, 80, 25, 1) > 0);
    //显示器里面是浅色填充：半透明
    const int tint = ssobChannel(bgra, w, 80, 48, 3);
    CHECK(tint > 0);
    CHECK(tint < 128);
    //外接矩形的左上角没有画（不是两点连线）
    CHECK(ssobAlphaNear(bgra, w, h, 41, 21, 1) == 0);
    //名字在下方
    bool label = false;
    for (int y = 101; y < 130 && !label; ++y)
    {
        for (int x = 66; x < 94; ++x)
        {
            if (ssobChannel(bgra, w, x, y, 3) > 0)
            {
                label = true;
                break;
            }
        }
    }
    CHECK(label);

    //连接线：自己的点在左上角，吸附在两个图元上，按图元之间的连线画
    std::vector<AnnotationStroke> net;
    net.push_back(ssobObj(0, 1, annot_tool_icon, ssobNx(20, w), ssobNy(80, h), ssobNx(60, w), ssobNy(120, h)));
    net.push_back(ssobObj(0, 2, annot_tool_icon, ssobNx(300, w), ssobNy(80, h), ssobNx(340, w), ssobNy(120, h)));
    AnnotationStroke conn = ssobObj(3, 9, annot_tool_connector, 0, 0, 100, 100);
    conn.m_width = 10;
    conn.m_style.m_attachStart = AnnotationRef(0, 1);
    conn.m_style.m_attachEnd = AnnotationRef(0, 2);
    net.push_back(conn);
    RenderAnnotationLayer(w, h, net, NULL, 0, true, bgra);
    CHECK(ssobAlphaNear(bgra, w, h, 180, 100, 1) > 0);
    CHECK(ssobAlphaNear(bgra, w, h, 250, 100, 1) > 0);
    //连接线在两个图元之间，图元之外上方没有线
    CHECK(ssobAlphaNear(bgra, w, h, 180, 60, 2) == 0);

    //选择提示：选中框、八个句柄、选择框
    std::vector<uint8_t> layer(static_cast<size_t>(w) * h * 4, 0);
    AnnotSelectionVisual v;
    const AnnotRect box(ssobNx(100, w), ssobNy(50, h), ssobNx(200, w), ssobNy(150, h));
    v.m_boxes.push_back(box);
    v.m_handles = AnnotHandlePositions(ssobObj(0, 1, annot_tool_rect, box.m_left, box.m_top, box.m_right, box.m_bottom),
                                       std::vector<AnnotationStroke>(), static_cast<double>(w) / h);
    v.m_hasBand = true;
    v.m_band = AnnotRect(ssobNx(250, w), ssobNy(20, h), ssobNx(350, w), ssobNy(80, h));
    RenderAnnotationSelection(w, h, v, layer);
    //右下角句柄：不透明
    CHECK(ssobChannel(layer, w, 200, 150, 3) == 255);
    //选中框的上边（外扩几个像素，是虚线）：这一段里有画到的像素
    bool edge = false;
    for (int x = 120; x < 180 && !edge; ++x)
    {
        for (int y = 40; y < 50; ++y)
        {
            if (ssobChannel(layer, w, x, y, 3) > 0)
            {
                edge = true;
                break;
            }
        }
    }
    CHECK(edge);
    //选中框里面不填充
    CHECK(ssobChannel(layer, w, 150, 100, 3) == 0);
    //选择框里面：浅蓝半透明
    const int band = ssobChannel(layer, w, 300, 50, 3);
    CHECK(band > 0);
    CHECK(band < 128);
    CHECK(ssobChannel(layer, w, 300, 50, 0) > ssobChannel(layer, w, 300, 50, 2));
}


TEST_CASE("SSOB12 会话：点一下放默认大小的图元、拖出指定大小；连接线两端落在图元上就吸附；橡皮擦图元与连接线", "[shareobj]")
{
    SsobTransport t;
    ShareAnnotationSession s;
    s.Start(&t, kSsobHost, 100);
    s.SetContentAspect(kSsobAspect);

    //点一下：以按下处为中心放一个默认大小的图元（纵向 kAnnotIconDefaultSize，横向按宽高比换算成正方形）
    s.SetTool(annot_tool_icon);
    s.SetIconKind(annot_icon_router);
    size_t sent = t.m_bodies.size();
    s.PointerDown(ssobPt(30000, 30000), 100, 0);
    s.PointerUp(ssobPt(30000, 30000), 10);
    REQUIRE(t.m_bodies.size() == sent + 2);
    AnnotationMessage b = ssobSent(t, sent);
    CHECK(b.m_op == annot_op_begin);
    CHECK(b.m_tool == annot_tool_icon);
    CHECK(b.m_style.m_variant == annot_icon_router);
    REQUIRE(b.m_points.size() == 2);
    const int halfY = kAnnotIconDefaultSize / 2;
    const int halfX = static_cast<int>(kAnnotIconDefaultSize / kSsobAspect / 2);
    CHECK(abs(b.m_points[0].m_x - (30000 - halfX)) <= 1);
    CHECK(abs(b.m_points[0].m_y - (30000 - halfY)) <= 1);
    CHECK(abs(b.m_points[1].m_x - (30000 + halfX)) <= 1);
    CHECK(abs(b.m_points[1].m_y - (30000 + halfY)) <= 1);
    CHECK(ssobSent(t, sent + 1).m_op == annot_op_end);
    const AnnotationRef routerRef(kSsobHost, b.m_id);
    //拖动的距离太小：也按点一下处理
    s.SetIconKind(annot_icon_server);
    s.PointerDown(ssobPt(5000, 5000), 100, 20);
    s.PointerMove(ssobPt(5050, 5050), 100, 21);
    s.PointerUp(ssobPt(5050, 5050), 22);
    const AnnotationStroke& tiny = s.Model().Strokes().back();
    CHECK(tiny.m_style.m_variant == annot_icon_server);
    CHECK(abs(static_cast<int>(tiny.m_points[1].m_y) - tiny.m_points[0].m_y - kAnnotIconDefaultSize) <= 1);
    //拖出指定大小
    s.SetIconKind(annot_icon_database);
    ssobDrag(s, 10000, 40000, 18000, 56000, 30);
    const AnnotationStroke& dragged = s.Model().Strokes().back();
    CHECK(dragged.m_points[0].m_x == 10000);
    CHECK(dragged.m_points[1].m_y == 56000);
    const AnnotationRef dbRef(kSsobHost, dragged.m_id);
    //默认大小的图元靠近边缘：整个限制在画面以内
    s.PointerDown(ssobPt(100, 100), 100, 40);
    s.PointerUp(ssobPt(100, 100), 41);
    const AnnotationStroke& corner = s.Model().Strokes().back();
    CHECK(corner.m_points[0].m_x == 0);
    CHECK(corner.m_points[0].m_y == 0);
    CHECK(abs(static_cast<int>(corner.m_points[1].m_y) - kAnnotIconDefaultSize) <= 1);

    //连接线：从路由器拖到数据库，两端吸附
    s.SetTool(annot_tool_connector);
    sent = t.m_bodies.size();
    ssobDrag(s, 30000, 30000, 14000, 48000, 50);
    REQUIRE(t.m_bodies.size() == sent + 2);
    AnnotationMessage cb = ssobSent(t, sent);
    CHECK(cb.m_tool == annot_tool_connector);
    CHECK(cb.m_style.m_attachStart == routerRef);
    CHECK(cb.m_style.m_attachEnd == dbRef);
    CHECK(cb.m_style.m_routing == annot_route_straight);
    const AnnotationRef connRef(kSsobHost, cb.m_id);
    //走向可设
    s.SetConnectorRouting(annot_route_elbow);
    ssobDrag(s, 60000, 10000, 60000, 20000, 60);
    AnnotationStroke loose = s.Model().Strokes().back();
    CHECK(loose.m_tool == annot_tool_connector);
    CHECK(loose.m_style.m_routing == annot_route_elbow);
    CHECK_FALSE(loose.m_style.m_attachStart.IsSet());
    CHECK_FALSE(loose.m_style.m_attachEnd.IsSet());
    //两端落在同一个图元上：只吸附起点
    ssobDrag(s, 29800, 29800, 30200, 30200, 70);
    AnnotationStroke same = s.Model().Strokes().back();
    CHECK(same.m_style.m_attachStart == routerRef);
    CHECK_FALSE(same.m_style.m_attachEnd.IsSet());
    //只点一下：不发
    sent = t.m_bodies.size();
    s.PointerDown(ssobPt(40000, 10000), 100, 80);
    s.PointerUp(ssobPt(40000, 10000), 81);
    CHECK(t.m_bodies.size() == sent);

    //橡皮：点在图元里面就擦掉图元，吸附在它上面的连接线脱开
    s.SetTool(kAnnotEraser);
    s.PointerDown(ssobPt(14000, 48000), 100, 90);
    s.PointerUp(ssobPt(14000, 48000), 91);
    CHECK(s.Model().Find(dbRef) == NULL);
    const AnnotationStroke* conn = s.Model().Find(connRef);
    REQUIRE(conn != NULL);
    CHECK_FALSE(conn->m_style.m_attachEnd.IsSet());
    //点在连接线上：擦掉连接线
    s.PointerDown(ssobPt(60000, 15000), 100, 100);
    s.PointerUp(ssobPt(60000, 15000), 101);
    for (size_t i = 0; i < s.Model().Strokes().size(); ++i)
        CHECK_FALSE((s.Model().Strokes()[i].m_tool == annot_tool_connector && s.Model().Strokes()[i].m_points[0].m_x == 60000));
}


TEST_CASE("SSOB13 工具条：选择与拓扑两组的菜单、组的顺序、按钮文字；选择工具下样式命令改到选中对象上", "[shareobj]")
{
    //组的顺序：十三组各出现一次，选择在最前，拓扑紧跟形状，颜色起是样式组
    std::vector<int> seen(kAnnotGroupCount, 0);
    for (int i = 0; i < kAnnotGroupCount; ++i)
    {
        REQUIRE(kAnnotGroupOrder[i] >= 0);
        REQUIRE(kAnnotGroupOrder[i] < kAnnotGroupCount);
        ++seen[kAnnotGroupOrder[i]];
    }
    for (int g = 0; g < kAnnotGroupCount; ++g)
        CHECK(seen[g] == 1);
    CHECK(kAnnotGroupOrder[0] == annot_group_select);
    for (int i = 1; i < kAnnotGroupCount; ++i)
    {
        if (kAnnotGroupOrder[i] == annot_group_topology)
            CHECK(kAnnotGroupOrder[i - 1] == annot_group_shape);
    }

    AnnotBarState st;
    //选择组：「选择对象」与「删除选中的对象」，选择工具时打勾
    std::vector<AnnotMenuItem> sel = BuildAnnotGroupMenu(annot_group_select, st);
    bool hasSelect = false;
    bool hasDelete = false;
    for (size_t i = 0; i < sel.size(); ++i)
    {
        AnnotMenuChoice c;
        if (sel[i].m_separator || !sel[i].m_enabled || !DecodeAnnotMenuChoice(sel[i].m_id, c))
            continue;
        if (c.m_command == annot_bar_tool && c.m_value == kAnnotSelect)
        {
            hasSelect = true;
            CHECK_FALSE(sel[i].m_checked);
        }
        if (c.m_command == annot_bar_delete)
            hasDelete = true;
    }
    CHECK(hasSelect);
    CHECK(hasDelete);
    st.m_tool = kAnnotSelect;
    sel = BuildAnnotGroupMenu(annot_group_select, st);
    bool checkedSelect = false;
    for (size_t i = 0; i < sel.size(); ++i)
    {
        AnnotMenuChoice c;
        if (DecodeAnnotMenuChoice(sel[i].m_id, c) && c.m_command == annot_bar_tool && c.m_value == kAnnotSelect)
            checkedSelect = sel[i].m_checked;
    }
    CHECK(checkedSelect);

    //拓扑组：十一种图元、连接线、连接线走向子菜单；当前图元打勾
    st.m_tool = annot_tool_icon;
    st.m_iconKind = annot_icon_server;
    const std::vector<AnnotMenuItem> topo = BuildAnnotGroupMenu(annot_group_topology, st);
    int icons = 0;
    int checkedIcon = -1;
    bool hasConnector = false;
    int routings = 0;
    for (size_t i = 0; i < topo.size(); ++i)
    {
        AnnotMenuChoice c;
        if (!topo[i].m_children.empty())
        {
            for (size_t k = 0; k < topo[i].m_children.size(); ++k)
            {
                if (DecodeAnnotMenuChoice(topo[i].m_children[k].m_id, c) && c.m_command == annot_bar_conn_routing)
                    ++routings;
            }
            continue;
        }
        if (!DecodeAnnotMenuChoice(topo[i].m_id, c))
            continue;
        if (c.m_command == annot_bar_icon)
        {
            ++icons;
            if (topo[i].m_checked)
                checkedIcon = c.m_value;
        }
        if (c.m_command == annot_bar_tool && c.m_value == annot_tool_connector)
            hasConnector = true;
    }
    CHECK(icons == kAnnotIconCount);
    CHECK(checkedIcon == annot_icon_server);
    CHECK(hasConnector);
    CHECK(routings == 3);

    //按钮文字与工具归组
    CHECK(AnnotGroupButtonText(annot_group_topology, st).find(L"服务器") == 0);
    st.m_tool = annot_tool_connector;
    CHECK(AnnotGroupButtonText(annot_group_topology, st).find(L"连接线") == 0);
    st.m_tool = annot_tool_pen;
    CHECK(AnnotGroupButtonText(annot_group_topology, st).find(L"拓扑") == 0);
    CHECK(AnnotGroupButtonText(annot_group_select, st).find(L"选择") == 0);
    CHECK(AnnotToolGroup(kAnnotSelect) == annot_group_select);
    CHECK(AnnotToolGroup(annot_tool_icon) == annot_group_topology);
    CHECK(AnnotToolGroup(annot_tool_connector) == annot_group_topology);

    //命令记进状态：图元切到图元工具，连接线走向切到连接线工具；删除不改状态
    AnnotMenuChoice c;
    c.m_command = annot_bar_icon;
    c.m_value = annot_icon_firewall;
    CHECK(ApplyAnnotChoice(c, st));
    CHECK(st.m_tool == annot_tool_icon);
    CHECK(st.m_iconKind == annot_icon_firewall);
    c.m_command = annot_bar_conn_routing;
    c.m_value = annot_route_elbow;
    CHECK(ApplyAnnotChoice(c, st));
    CHECK(st.m_tool == annot_tool_connector);
    CHECK(st.m_connRouting == annot_route_elbow);
    c.m_command = annot_bar_delete;
    c.m_value = 0;
    CHECK_FALSE(ApplyAnnotChoice(c, st));

    //状态整体交给会话：图元种类与连接线走向
    ShareAnnotationSession s;
    ApplyAnnotBarState(st, s);
    CHECK(s.IconKind() == annot_icon_firewall);
    CHECK(s.Tool() == annot_tool_connector);

    //选择工具下：样式类命令改到选中对象上，工具保持为选择
    st.m_tool = kAnnotSelect;
    int attribute = -1;
    int value = -1;
    c.m_command = annot_bar_color;
    c.m_value = 0x43A047;
    CHECK(ApplyAnnotChoiceInSelectMode(c, st, attribute, value));
    CHECK(attribute == annot_restyle_color);
    CHECK(value == 0x43A047);
    CHECK(st.m_color == 0x43A047u);
    CHECK(st.m_tool == kAnnotSelect);
    c.m_command = annot_bar_end_cap;
    c.m_value = annot_cap_diamond;
    CHECK(ApplyAnnotChoiceInSelectMode(c, st, attribute, value));
    CHECK(attribute == annot_restyle_end_cap);
    CHECK(value == annot_cap_diamond);
    CHECK(st.m_endCap == annot_cap_diamond);
    CHECK(st.m_tool == kAnnotSelect);
    c.m_command = annot_bar_conn_routing;
    c.m_value = annot_route_arc;
    CHECK(ApplyAnnotChoiceInSelectMode(c, st, attribute, value));
    CHECK(attribute == annot_restyle_routing);
    CHECK(st.m_tool == kAnnotSelect);
    c.m_command = annot_bar_icon;
    c.m_value = annot_icon_ap;
    CHECK(ApplyAnnotChoiceInSelectMode(c, st, attribute, value));
    CHECK(attribute == annot_restyle_icon);
    CHECK(value == annot_icon_ap);
    CHECK(st.m_tool == kAnnotSelect);
    c.m_command = annot_bar_text_bold;
    c.m_value = 1;
    CHECK(ApplyAnnotChoiceInSelectMode(c, st, attribute, value));
    CHECK(attribute == annot_restyle_text_bold);
    //选工具、删除、撤销：不是样式类命令
    c.m_command = annot_bar_tool;
    c.m_value = annot_tool_rect;
    CHECK_FALSE(ApplyAnnotChoiceInSelectMode(c, st, attribute, value));
    CHECK(st.m_tool == kAnnotSelect);
    c.m_command = annot_bar_delete;
    CHECK_FALSE(ApplyAnnotChoiceInSelectMode(c, st, attribute, value));
    c.m_command = annot_bar_undo;
    CHECK_FALSE(ApplyAnnotChoiceInSelectMode(c, st, attribute, value));
}

namespace {

//PNG 编码器的 CLSID（GDI+ 内置编码器的固定值）
const CLSID kSsobPngEncoder = { 0x557CF406, 0x1A04, 0x11D3, { 0x9A, 0x73, 0x00, 0x00, 0xF8, 0x1E, 0xF3, 0x2E } };

//把预乘 alpha 的图层合成到白底上存成 PNG
bool ssobSavePng(const wchar_t* path, const std::vector<uint8_t>& bgra, int w, int h)
{
    const int kBpp = 4;
    const int kMax = 255;
    std::vector<uint8_t> rgb(bgra.size());
    for (size_t i = 0; i + 3 < bgra.size(); i += kBpp)
    {
        const int a = bgra[i + 3];
        for (int c = 0; c < 3; ++c)
            rgb[i + c] = static_cast<uint8_t>(bgra[i + c] + (kMax - a));
        rgb[i + 3] = static_cast<uint8_t>(kMax);
    }
    Gdiplus::GdiplusStartupInput input;
    ULONG_PTR token = 0;
    Gdiplus::GdiplusStartup(&token, &input, NULL);
    bool ok = false;
    {
        Gdiplus::Bitmap bmp(w, h, w * kBpp, PixelFormat32bppARGB, &rgb[0]);
        ok = (bmp.Save(path, &kSsobPngEncoder, NULL) == Gdiplus::Ok);
    }
    Gdiplus::GdiplusShutdown(token);
    return ok;
}

} // namespace

TEST_CASE("SSOBX 网络拓扑效果图：十一种图元加名字、两条连接线与一个选中框，存到 snapshots", "[.][shareobj-snap]")
{
    const int w = 1100;
    const int h = 420;
    const int cell = 100;
    const int gap = 0;
    const char* const names[kAnnotIconCount] = { "PC-01", "Laptop", "Server", "Router", "Switch", "Firewall", "AP",
                                                 "Internet", "DB", "Phone", "Printer" };
    std::vector<AnnotationStroke> all;
    for (int i = 0; i < kAnnotIconCount; ++i)
    {
        const int x0 = 20 + i * (cell + gap) - (i > 5 ? 0 : 0);
        AnnotationStroke s = ssobObj(0, static_cast<uint32_t>(i + 1), annot_tool_icon, ssobNx(x0, w), ssobNy(40, h),
                                     ssobNx(x0 + cell - 30, w), ssobNy(40 + cell - 30, h));
        s.m_style.m_variant = i;
        s.m_color = 0x37474F;
        s.m_width = 4;
        s.m_text = names[i];
        all.push_back(s);
    }
    //第二排：交换机连两台电脑与一台服务器，连接线带文字与箭头
    const int row = 250;
    AnnotationStroke sw = ssobObj(0, 20, annot_tool_icon, ssobNx(480, w), ssobNy(row, h), ssobNx(560, w), ssobNy(row + 80, h));
    sw.m_style.m_variant = annot_icon_switch;
    sw.m_color = 0x1E88E5;
    sw.m_text = "Core-SW";
    all.push_back(sw);
    AnnotationStroke pc = ssobObj(0, 21, annot_tool_icon, ssobNx(160, w), ssobNy(row, h), ssobNx(240, w), ssobNy(row + 80, h));
    pc.m_style.m_variant = annot_icon_computer;
    pc.m_color = 0x43A047;
    pc.m_text = "PC-02";
    all.push_back(pc);
    AnnotationStroke sv = ssobObj(0, 22, annot_tool_icon, ssobNx(820, w), ssobNy(row, h), ssobNx(900, w), ssobNy(row + 80, h));
    sv.m_style.m_variant = annot_icon_server;
    sv.m_color = 0xE53935;
    sv.m_text = "Web";
    all.push_back(sv);
    AnnotationStroke c1 = ssobObj(0, 30, annot_tool_connector, 0, 0, 1, 1);
    c1.m_style.m_attachStart = AnnotationRef(0, 21);
    c1.m_style.m_attachEnd = AnnotationRef(0, 20);
    c1.m_color = 0x37474F;
    c1.m_text = "1G";
    all.push_back(c1);
    AnnotationStroke c2 = ssobObj(0, 31, annot_tool_connector, 0, 0, 1, 1);
    c2.m_style.m_attachStart = AnnotationRef(0, 20);
    c2.m_style.m_attachEnd = AnnotationRef(0, 22);
    c2.m_style.m_endCap = annot_cap_triangle;
    c2.m_style.m_lineStyle = annot_line_dash;
    c2.m_color = 0x37474F;
    c2.m_text = "10G";
    all.push_back(c2);
    std::vector<uint8_t> bgra;
    RenderAnnotationLayer(w, h, all, NULL, 0, true, bgra);
    //选中服务器：选中框与八个句柄
    AnnotSelectionVisual v;
    v.m_boxes.push_back(AnnotObjectBounds(sv, all, static_cast<double>(w) / h));
    v.m_handles = AnnotHandlePositions(sv, all, static_cast<double>(w) / h);
    RenderAnnotationSelection(w, h, v, bgra);
    CHECK(ssobSavePng(L"E:\\flamingo\\snapshots\\share20_topology.png", bgra, w, h));
}

namespace {

//让本线程的消息跑一会儿（工具条窗口排版、绘制）
void ssobPump(DWORD ms)
{
    const ULONGLONG until = ::GetTickCount64() + ms;
    while (::GetTickCount64() < until)
    {
        MSG msg;
        while (::PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
        {
            ::TranslateMessage(&msg);
            ::DispatchMessageW(&msg);
        }
        ::Sleep(10);
    }
}

//只截一个窗口本身（PrintWindow，不碰屏幕上的其他内容），存成 PNG
bool ssobSnapWindow(HWND hwnd, const wchar_t* path)
{
    RECT rc;
    if (!::GetWindowRect(hwnd, &rc))
        return false;
    const int w = rc.right - rc.left;
    const int h = rc.bottom - rc.top;
    if (w <= 0 || h <= 0)
        return false;
    HDC screen = ::GetDC(NULL);
    HDC mem = ::CreateCompatibleDC(screen);
    BITMAPINFO bi;
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = NULL;
    HBITMAP dib = ::CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    ::ReleaseDC(NULL, screen);
    if (dib == NULL)
    {
        ::DeleteDC(mem);
        return false;
    }
    HGDIOBJ old = ::SelectObject(mem, dib);
    //PW_RENDERFULLCONTENT：分层窗口也能截到
    const UINT kRenderFullContent = 2;
    const BOOL printed = ::PrintWindow(hwnd, mem, kRenderFullContent);
    std::vector<uint8_t> bgra(static_cast<const uint8_t*>(bits), static_cast<const uint8_t*>(bits) + static_cast<size_t>(w) * h * 4);
    ::SelectObject(mem, old);
    ::DeleteObject(dib);
    ::DeleteDC(mem);
    return printed && ssobSavePng(path, bgra, w, h);
}

} // namespace

TEST_CASE("SSOBY 标注工具条效果图：观看者与发起人两种，各截一张存到 snapshots", "[.][sharebar-snap]")
{
    //工具条的命令投给一个只收消息的窗口（不处理）
    HWND notify = ::CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, NULL, NULL);
    REQUIRE(notify != NULL);
    //摆在主显示器左上角：参照矩形下方居中
    const RECT anchor = { 0, 0, 1400, 60 };
    {
        CShareAnnotationBar bar;
        bar.SetState(AnnotBarState());
        bar.Show(anchor, notify, false);
        REQUIRE(bar.IsWindow());
        ssobPump(400);
        CHECK(ssobSnapWindow(bar.m_hWnd, L"E:\\flamingo\\snapshots\\share19_toolbar.png"));
        bar.Hide();
    }
    {
        //发起人的工具条排除在屏幕采集之外，PrintWindow 截出来是黑的：调试版开关临时关掉这一排除（只在本进程）
        _putenv_s("FLAMINGO_SHARE_NO_EXCLUDE", "1");
        CShareAnnotationBar bar;
        bar.SetState(AnnotBarState());
        bar.Show(anchor, notify, true);
        _putenv_s("FLAMINGO_SHARE_NO_EXCLUDE", "");
        REQUIRE(bar.IsWindow());
        ssobPump(400);
        CHECK(ssobSnapWindow(bar.m_hWnd, L"E:\\flamingo\\snapshots\\share19_toolbar_host.png"));
        bar.Hide();
    }
    ::DestroyWindow(notify);
}
