/**
 *  屏幕共享标注的单元测试（Suite SSAN，标签 [shareannot]），ShareAnnotationTests.cpp
 *
 *  覆盖 FlamingoMedia/Share 的 ShareAnnotation（编解码与模型）、ShareAnnotationSession（本人输入与收发）与
 *  ShareAnnotationRender 的换算函数（docs/屏幕共享与录制方案.md 5.9、第 6 期）：
 *    - SSAN1  七种操作编码后解码得到原样；
 *    - SSAN2  长度不符、操作或种类非法、点数或文字超限的载荷解码失败；
 *    - SSAN3  begin / append / end 组成一笔；重复的 begin、给未知或已画完的笔追加、给别人的笔追加都不生效；
 *    - SSAN4  擦除指定一笔；按作者找最近的一笔；擦除不存在的笔返回 false；
 *    - SSAN5  清空与整笔补发只接受发起人发出的；整笔补发已有则覆盖，内容相同时不算变化；
 *    - SSAN6  收回观看者权限后不接受观看者的新操作，发起人照常，已有笔迹保留；
 *    - SSAN7  橡皮命中：折线按到各段的距离，矩形按四条边，取最上面的一笔，容差以外不命中；
 *    - SSAN8  画笔：按下发 begin，移动按时间或点数攒批发 append，松开发 end；另一端收到后得到完全相同的笔迹；
 *    - SSAN9  图形：拖动中不发送、只有本地预览，松开时发出两点的 begin 与 end；
 *    - SSAN10 橡皮、撤销本人最近一笔、清空（观看者清空不发送）；
 *    - SSAN11 发起人整笔补发后，后加入的一端得到全部笔迹；已有全部笔迹的一端没有变化；
 *    - SSAN12 观看者没有权限时按下不发送；画到一半被收回权限时这一笔照常收尾；
 *    - SSAN13 坐标换算：两个方向互换、矩形以外夹到边缘、线宽与字号换算、箭头两翼对称。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-03
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <algorithm>
#include <string>
#include <vector>

#include "../../FlamingoMedia/Share/ShareAnnotation.h"
#include "../../FlamingoMedia/Share/ShareAnnotationRender.h"
#include "../../FlamingoMedia/Share/ShareAnnotationSession.h"
#include "../../FlamingoMedia/Share/ShareFraming.h"
#include "../net/Protocol.h"

using namespace screenshare;

namespace {

//记下发出的每个包体的发送通道
class CaptureTransport : public ShareAudioTransport
{
public:
    virtual bool SendRoomBody(const std::string& body) override
    {
        m_bodies.push_back(body);
        return true;
    }

    std::vector<std::string> m_bodies;
};

//拆出包体里的帧头与标注载荷
bool splitBody(const std::string& body, ShareFrameHeader& header, std::string& payload)
{
    const char* p = NULL;
    size_t len = 0;
    if (!ShareFraming::decodeBody(body.data(), body.size(), header, p, len))
        return false;
    payload.assign(p, len);
    return true;
}

//解码第 i 个包体里的标注操作
AnnotationMessage messageAt(const CaptureTransport& t, size_t i)
{
    ShareFrameHeader header;
    std::string payload;
    AnnotationMessage m;
    REQUIRE(i < t.m_bodies.size());
    REQUIRE(splitBody(t.m_bodies[i], header, payload));
    REQUIRE(header.m_track == screenshare_track_annotation);
    REQUIRE(ShareAnnotationCodec::decode(payload, m));
    return m;
}

//把一个通道发出的全部帧交给另一端（帧头的座位号即发送者座位号，与服务端改写后一致）
void deliverAll(const CaptureTransport& from, ShareAnnotationSession& to)
{
    for (size_t i = 0; i < from.m_bodies.size(); ++i)
    {
        ShareFrameHeader header;
        std::string payload;
        REQUIRE(splitBody(from.m_bodies[i], header, payload));
        to.OnRemoteFrame(header.m_senderSeat, payload);
    }
}

AnnotationMessage makeBegin(uint32_t id, int tool, int x, int y)
{
    AnnotationMessage m;
    m.m_op = annot_op_begin;
    m.m_id = id;
    m.m_tool = tool;
    m.m_color = 0x112233;
    m.m_width = 3;
    m.m_points.push_back(AnnotationPoint(static_cast<uint16_t>(x), static_cast<uint16_t>(y)));
    return m;
}

AnnotationMessage makeAppend(uint32_t id, int x, int y)
{
    AnnotationMessage m;
    m.m_op = annot_op_append;
    m.m_id = id;
    m.m_points.push_back(AnnotationPoint(static_cast<uint16_t>(x), static_cast<uint16_t>(y)));
    return m;
}

AnnotationMessage makeEnd(uint32_t id)
{
    AnnotationMessage m;
    m.m_op = annot_op_end;
    m.m_id = id;
    return m;
}

AnnotationMessage makeErase(int author, uint32_t id)
{
    AnnotationMessage m;
    m.m_op = annot_op_erase;
    m.m_author = author;
    m.m_id = id;
    return m;
}

bool samePoints(const std::vector<AnnotationPoint>& a, const std::vector<AnnotationPoint>& b)
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i)
    {
        if (a[i].m_x != b[i].m_x || a[i].m_y != b[i].m_y)
            return false;
    }
    return true;
}

//按作者、编号排序（各端收到操作的先后不同，笔迹的叠放顺序可以不同）
bool strokeLess(const AnnotationStroke& x, const AnnotationStroke& y)
{
    if (x.m_author != y.m_author)
        return x.m_author < y.m_author;
    return x.m_id < y.m_id;
}

//两个模型的笔迹是否完全相同（作者、编号、种类、颜色、线宽、点、文字、是否画完），不比较叠放顺序
bool sameStrokes(const ShareAnnotationModel& a, const ShareAnnotationModel& b)
{
    std::vector<AnnotationStroke> sa = a.Strokes();
    std::vector<AnnotationStroke> sb = b.Strokes();
    std::sort(sa.begin(), sa.end(), strokeLess);
    std::sort(sb.begin(), sb.end(), strokeLess);
    if (sa.size() != sb.size())
        return false;
    for (size_t i = 0; i < sa.size(); ++i)
    {
        if (sa[i].m_author != sb[i].m_author || sa[i].m_id != sb[i].m_id || sa[i].m_tool != sb[i].m_tool ||
            sa[i].m_color != sb[i].m_color || sa[i].m_width != sb[i].m_width || sa[i].m_text != sb[i].m_text ||
            sa[i].m_finished != sb[i].m_finished || !samePoints(sa[i].m_points, sb[i].m_points))
            return false;
    }
    return true;
}

AnnotationPoint pt(int x, int y)
{
    return AnnotationPoint(static_cast<uint16_t>(x), static_cast<uint16_t>(y));
}

} // namespace

TEST_CASE("SSAN1 七种操作编码后解码得到原样", "[shareannot]")
{
    std::vector<AnnotationMessage> all;
    AnnotationMessage begin = makeBegin(77, annot_tool_highlighter, 10, 20);
    begin.m_points.push_back(pt(65535, 0));
    all.push_back(begin);
    AnnotationMessage append = makeAppend(77, 30, 40);
    append.m_points.push_back(pt(50, 60));
    all.push_back(append);
    all.push_back(makeEnd(77));
    AnnotationMessage text;
    text.m_op = annot_op_text;
    text.m_id = 78;
    text.m_color = 0xFFEE00;
    text.m_width = 28;
    text.m_points.push_back(pt(1000, 2000));
    text.m_text = "\xE6\xA0\x87\xE6\xB3\xA8 note";
    all.push_back(text);
    all.push_back(makeErase(3, 0xA1B2C3D4u));
    AnnotationMessage clear;
    clear.m_op = annot_op_clear;
    all.push_back(clear);
    AnnotationMessage full = makeBegin(79, annot_tool_text, 5, 6);
    full.m_op = annot_op_full;
    full.m_author = 4;
    full.m_text = "abc";
    full.m_finished = true;
    all.push_back(full);

    for (size_t i = 0; i < all.size(); ++i)
    {
        const std::string payload = ShareAnnotationCodec::encode(all[i]);
        REQUIRE_FALSE(payload.empty());
        AnnotationMessage back;
        REQUIRE(ShareAnnotationCodec::decode(payload, back));
        CHECK(back.m_op == all[i].m_op);
        CHECK(back.m_id == all[i].m_id);
        CHECK(samePoints(back.m_points, all[i].m_points));
        CHECK(back.m_text == all[i].m_text);
        if (all[i].m_op == annot_op_begin || all[i].m_op == annot_op_full)
        {
            CHECK(back.m_tool == all[i].m_tool);
            CHECK(back.m_color == all[i].m_color);
            CHECK(back.m_width == all[i].m_width);
        }
        if (all[i].m_op == annot_op_text)
        {
            CHECK(back.m_color == all[i].m_color);
            CHECK(back.m_width == all[i].m_width);
        }
        if (all[i].m_op == annot_op_erase || all[i].m_op == annot_op_full)
            CHECK(back.m_author == all[i].m_author);
        if (all[i].m_op == annot_op_full)
            CHECK(back.m_finished == all[i].m_finished);
    }
}

TEST_CASE("SSAN2 非法载荷解码失败", "[shareannot]")
{
    AnnotationMessage m;
    CHECK_FALSE(ShareAnnotationCodec::decode(std::string(), m));
    //未知操作（第 19 期起 8、9 是画笔位置与画笔离开，这里取远超当前取值的 0x7F）
    CHECK_FALSE(ShareAnnotationCodec::decode(std::string(1, '\x7F'), m));
    //截断与多余字节
    const std::string begin = ShareAnnotationCodec::encode(makeBegin(1, annot_tool_pen, 1, 1));
    CHECK_FALSE(ShareAnnotationCodec::decode(begin.substr(0, begin.size() - 1), m));
    CHECK_FALSE(ShareAnnotationCodec::decode(begin + "x", m));
    //非法种类（第 19 期起种类扩到 kAnnotToolLast，这里取远超上限的 0x7F）
    REQUIRE(begin.size() > 5);
    std::string badTool = begin;
    badTool[5] = '\x7F';
    CHECK_FALSE(ShareAnnotationCodec::decode(badTool, m));
    //点数超限：编码时截断到上限，手工改大点数字段后解码失败
    AnnotationMessage many = makeBegin(2, annot_tool_pen, 0, 0);
    for (int i = 0; i < kAnnotMaxStrokePoints + 10; ++i)
        many.m_points.push_back(pt(i, i));
    const std::string capped = ShareAnnotationCodec::encode(many);
    REQUIRE(ShareAnnotationCodec::decode(capped, m));
    CHECK(static_cast<int>(m.m_points.size()) == kAnnotMaxStrokePoints);
    //文字超限：编码时截断
    AnnotationMessage text;
    text.m_op = annot_op_text;
    text.m_id = 3;
    text.m_points.push_back(pt(1, 1));
    text.m_text.assign(static_cast<size_t>(kAnnotMaxTextBytes) + 50, 'a');
    REQUIRE(ShareAnnotationCodec::decode(ShareAnnotationCodec::encode(text), m));
    CHECK(static_cast<int>(m.m_text.size()) == kAnnotMaxTextBytes);
}

TEST_CASE("SSAN3 begin / append / end 组成一笔", "[shareannot]")
{
    ShareAnnotationModel model;
    CHECK(model.Apply(2, makeBegin(10, annot_tool_pen, 100, 100)));
    CHECK(model.Apply(2, makeAppend(10, 200, 200)));
    //重复的 begin 不生效
    CHECK_FALSE(model.Apply(2, makeBegin(10, annot_tool_pen, 5, 5)));
    //别人不能给这一笔追加（作者取发送者座位号，座位 3 没有编号 10 的笔）
    CHECK_FALSE(model.Apply(3, makeAppend(10, 300, 300)));
    CHECK(model.Apply(2, makeEnd(10)));
    //画完之后再追加不生效
    CHECK_FALSE(model.Apply(2, makeAppend(10, 400, 400)));
    //未知的笔
    CHECK_FALSE(model.Apply(2, makeAppend(99, 1, 1)));
    REQUIRE(model.Strokes().size() == 1);
    const AnnotationStroke& s = model.Strokes()[0];
    CHECK(s.m_author == 2);
    CHECK(s.m_id == 10u);
    CHECK(s.m_finished);
    REQUIRE(s.m_points.size() == 2);
    CHECK(s.m_points[1].m_x == 200);
}

TEST_CASE("SSAN4 擦除与按作者找最近的一笔", "[shareannot]")
{
    ShareAnnotationModel model;
    model.Apply(1, makeBegin(1, annot_tool_pen, 1, 1));
    model.Apply(1, makeEnd(1));
    model.Apply(1, makeBegin(2, annot_tool_pen, 2, 2));
    model.Apply(1, makeEnd(2));
    model.Apply(5, makeBegin(1, annot_tool_pen, 3, 3));
    uint32_t id = 0;
    REQUIRE(model.LastStrokeOf(1, id));
    CHECK(id == 2u);
    //任何有权限的人都能擦任何一笔
    CHECK(model.Apply(5, makeErase(1, 2)));
    REQUIRE(model.LastStrokeOf(1, id));
    CHECK(id == 1u);
    CHECK_FALSE(model.Apply(5, makeErase(1, 2)));
    CHECK(model.Strokes().size() == 2);
    CHECK_FALSE(model.LastStrokeOf(7, id));
}

TEST_CASE("SSAN5 清空与整笔补发只接受发起人的", "[shareannot]")
{
    ShareAnnotationModel model;
    model.Apply(1, makeBegin(1, annot_tool_pen, 1, 1));
    AnnotationMessage clear;
    clear.m_op = annot_op_clear;
    CHECK_FALSE(model.Apply(1, clear));
    CHECK(model.Strokes().size() == 1);
    CHECK(model.Apply(kAnnotHostSeat, clear));
    CHECK(model.Strokes().empty());

    AnnotationMessage full = makeBegin(9, annot_tool_rect, 10, 10);
    full.m_op = annot_op_full;
    full.m_author = 3;
    full.m_points.push_back(pt(20, 20));
    full.m_finished = true;
    CHECK_FALSE(model.Apply(3, full));
    CHECK(model.Apply(kAnnotHostSeat, full));
    REQUIRE(model.Strokes().size() == 1);
    CHECK(model.Strokes()[0].m_author == 3);
    //内容相同的补发不算变化
    const uint64_t version = model.Version();
    CHECK_FALSE(model.Apply(kAnnotHostSeat, full));
    CHECK(model.Version() == version);
    //内容不同则覆盖
    full.m_points[1] = pt(30, 30);
    CHECK(model.Apply(kAnnotHostSeat, full));
    CHECK(model.Strokes()[0].m_points[1].m_x == 30);
    CHECK(model.Strokes().size() == 1);
}

TEST_CASE("SSAN6 收回观看者权限后只接受发起人的新操作", "[shareannot]")
{
    ShareAnnotationModel model;
    CHECK(model.ViewersAllowed());
    model.Apply(2, makeBegin(1, annot_tool_pen, 1, 1));
    model.SetViewersAllowed(false);
    CHECK_FALSE(model.Apply(2, makeAppend(1, 2, 2)));
    CHECK_FALSE(model.Apply(2, makeBegin(2, annot_tool_pen, 1, 1)));
    CHECK_FALSE(model.Apply(2, makeErase(2, 1)));
    CHECK(model.Apply(kAnnotHostSeat, makeBegin(1, annot_tool_pen, 1, 1)));
    CHECK(model.Strokes().size() == 2);
    //发起人仍能擦观看者的笔
    CHECK(model.Apply(kAnnotHostSeat, makeErase(2, 1)));
    CHECK(model.Strokes().size() == 1);
}

TEST_CASE("SSAN7 橡皮命中", "[shareannot]")
{
    ShareAnnotationModel model;
    //折线 (1000,1000) → (5000,1000) → (5000,5000)
    AnnotationMessage line = makeBegin(1, annot_tool_pen, 1000, 1000);
    line.m_points.push_back(pt(5000, 1000));
    line.m_points.push_back(pt(5000, 5000));
    model.Apply(1, line);
    //矩形 (20000,20000)~(30000,30000)，只按四条边命中
    AnnotationMessage rect = makeBegin(2, annot_tool_rect, 20000, 20000);
    rect.m_points.push_back(pt(30000, 30000));
    model.Apply(1, rect);
    int author = -1;
    uint32_t id = 0;
    CHECK(model.HitTest(3000, 1050, 100, author, id));
    CHECK(id == 1u);
    CHECK(model.HitTest(5040, 3000, 100, author, id));
    CHECK(id == 1u);
    CHECK_FALSE(model.HitTest(3000, 1300, 100, author, id));
    CHECK(model.HitTest(25000, 29950, 100, author, id));
    CHECK(id == 2u);
    CHECK_FALSE(model.HitTest(25000, 25000, 100, author, id));
    //后画的在上面：同一处叠一笔后命中后画的
    AnnotationMessage over = makeBegin(3, annot_tool_pen, 2000, 1000);
    over.m_points.push_back(pt(4000, 1000));
    model.Apply(4, over);
    CHECK(model.HitTest(3000, 1000, 100, author, id));
    CHECK(author == 4);
    CHECK(id == 3u);
}

TEST_CASE("SSAN8 画笔攒批发送，另一端得到相同的笔迹", "[shareannot]")
{
    CaptureTransport ta;
    ShareAnnotationSession a;
    a.Start(&ta, 2, 1000);
    a.SetTool(annot_tool_pen);
    a.PointerDown(pt(100, 100), 50, 0);
    REQUIRE(ta.m_bodies.size() == 1);
    CHECK(messageAt(ta, 0).m_op == annot_op_begin);
    //40 毫秒内的移动先攒着
    a.PointerMove(pt(110, 110), 50, 10);
    a.PointerMove(pt(120, 120), 50, 20);
    CHECK(ta.m_bodies.size() == 1);
    //到 40 毫秒发一次
    a.PointerMove(pt(130, 130), 50, 41);
    REQUIRE(ta.m_bodies.size() == 2);
    AnnotationMessage ap = messageAt(ta, 1);
    CHECK(ap.m_op == annot_op_append);
    CHECK(ap.m_points.size() == 3);
    //攒够点数也发
    for (int i = 0; i < static_cast<int>(kAnnotAppendMaxPoints); ++i)
        a.PointerMove(pt(200 + i, 200), 50, 42);
    CHECK(ta.m_bodies.size() == 3);
    a.PointerUp(pt(500, 500), 43);
    const AnnotationMessage last = messageAt(ta, ta.m_bodies.size() - 1);
    CHECK(last.m_op == annot_op_end);
    REQUIRE(a.Model().Strokes().size() == 1);
    CHECK(a.Model().Strokes()[0].m_finished);
    CHECK(a.Model().Strokes()[0].m_author == 2);
    //另一端收到全部帧后笔迹完全相同
    CaptureTransport tb;
    ShareAnnotationSession b;
    b.Start(&tb, kAnnotHostSeat, 5000);
    deliverAll(ta, b);
    CHECK(sameStrokes(a.Model(), b.Model()));
    CHECK(tb.m_bodies.empty());
}

TEST_CASE("SSAN9 图形拖动中只在本地预览，松开时发出", "[shareannot]")
{
    CaptureTransport t;
    ShareAnnotationSession s;
    s.Start(&t, 1, 1);
    s.SetTool(annot_tool_arrow);
    s.PointerDown(pt(10, 10), 50, 0);
    s.PointerMove(pt(500, 500), 50, 100);
    CHECK(t.m_bodies.empty());
    AnnotationStroke preview;
    REQUIRE(s.PendingShape(preview));
    CHECK(preview.m_tool == annot_tool_arrow);
    REQUIRE(preview.m_points.size() == 2);
    CHECK(preview.m_points[1].m_x == 500);
    s.PointerUp(pt(600, 700), 200);
    CHECK_FALSE(s.PendingShape(preview));
    REQUIRE(t.m_bodies.size() == 2);
    const AnnotationMessage b = messageAt(t, 0);
    CHECK(b.m_op == annot_op_begin);
    REQUIRE(b.m_points.size() == 2);
    CHECK(b.m_points[1].m_x == 600);
    CHECK(b.m_points[1].m_y == 700);
    CHECK(messageAt(t, 1).m_op == annot_op_end);
    REQUIRE(s.Model().Strokes().size() == 1);
    CHECK(s.Model().Strokes()[0].m_tool == annot_tool_arrow);
}

TEST_CASE("SSAN10 橡皮、撤销与清空", "[shareannot]")
{
    CaptureTransport tv;
    ShareAnnotationSession viewer;
    viewer.Start(&tv, 3, 1);
    viewer.SetTool(annot_tool_pen);
    viewer.PointerDown(pt(1000, 1000), 100, 0);
    viewer.PointerMove(pt(9000, 1000), 100, 50);
    viewer.PointerUp(pt(9000, 1000), 60);
    viewer.PointerDown(pt(1000, 5000), 100, 100);
    viewer.PointerUp(pt(9000, 5000), 160);
    REQUIRE(viewer.Model().Strokes().size() == 2);
    //撤销最近一笔
    tv.m_bodies.clear();
    CHECK(viewer.UndoOwn());
    REQUIRE(tv.m_bodies.size() == 1);
    CHECK(messageAt(tv, 0).m_op == annot_op_erase);
    CHECK(viewer.Model().Strokes().size() == 1);
    //橡皮擦掉第一笔
    viewer.SetTool(kAnnotEraser);
    viewer.PointerDown(pt(5000, 1020), 100, 200);
    viewer.PointerUp(pt(5000, 1020), 210);
    CHECK(viewer.Model().Strokes().empty());
    CHECK_FALSE(viewer.UndoOwn());
    //观看者清空：不发送
    tv.m_bodies.clear();
    CHECK_FALSE(viewer.ClearAll());
    CHECK(tv.m_bodies.empty());
    //发起人清空：发送并清掉本地
    CaptureTransport th;
    ShareAnnotationSession host;
    host.Start(&th, kAnnotHostSeat, 1);
    CHECK(host.AddText(pt(10, 10), "hi"));
    CHECK(host.Model().Strokes().size() == 1);
    th.m_bodies.clear();
    CHECK(host.ClearAll());
    REQUIRE(th.m_bodies.size() == 1);
    CHECK(messageAt(th, 0).m_op == annot_op_clear);
    CHECK(host.Model().Strokes().empty());
}

TEST_CASE("SSAN11 发起人整笔补发给后加入的人", "[shareannot]")
{
    CaptureTransport th;
    CaptureTransport tv;
    ShareAnnotationSession host;
    ShareAnnotationSession viewer;
    host.Start(&th, kAnnotHostSeat, 1);
    viewer.Start(&tv, 1, 1);
    host.SetTool(annot_tool_rect);
    host.PointerDown(pt(10, 10), 50, 0);
    host.PointerUp(pt(90, 90), 10);
    viewer.SetTool(annot_tool_pen);
    viewer.PointerDown(pt(5, 5), 50, 0);
    viewer.PointerMove(pt(6, 6), 50, 100);
    viewer.PointerUp(pt(7, 7), 110);
    viewer.AddText(pt(20, 20), "x");
    deliverAll(tv, host);
    deliverAll(th, viewer);
    REQUIRE(host.Model().Strokes().size() == 3);
    CHECK(sameStrokes(host.Model(), viewer.Model()));
    //后加入的人只收到补发
    th.m_bodies.clear();
    host.SendSnapshot();
    CHECK(th.m_bodies.size() == 3);
    CaptureTransport tn;
    ShareAnnotationSession newcomer;
    newcomer.Start(&tn, 2, 1);
    deliverAll(th, newcomer);
    CHECK(sameStrokes(host.Model(), newcomer.Model()));
    //已有全部笔迹的一端收到补发没有变化
    const uint64_t version = viewer.Model().Version();
    deliverAll(th, viewer);
    CHECK(viewer.Model().Version() == version);
    //观看者调用补发不发送
    tv.m_bodies.clear();
    viewer.SendSnapshot();
    CHECK(tv.m_bodies.empty());
}

TEST_CASE("SSAN12 观看者没有标注权限", "[shareannot]")
{
    CaptureTransport t;
    ShareAnnotationSession s;
    s.Start(&t, 1, 1);
    s.SetTool(annot_tool_pen);
    s.SetViewersAllowed(false);
    CHECK_FALSE(s.CanDraw());
    s.PointerDown(pt(1, 1), 50, 0);
    s.PointerUp(pt(2, 2), 10);
    CHECK_FALSE(s.AddText(pt(1, 1), "a"));
    CHECK(t.m_bodies.empty());
    CHECK(s.Model().Strokes().empty());
    //画到一半被收回：这一笔照常收尾（发出 end）
    s.SetViewersAllowed(true);
    s.PointerDown(pt(1, 1), 50, 100);
    s.PointerMove(pt(3, 3), 50, 110);
    s.SetViewersAllowed(false);
    REQUIRE_FALSE(t.m_bodies.empty());
    CHECK(messageAt(t, t.m_bodies.size() - 1).m_op == annot_op_end);
    const size_t sent = t.m_bodies.size();
    s.PointerMove(pt(9, 9), 50, 200);
    s.PointerUp(pt(9, 9), 210);
    CHECK(t.m_bodies.size() == sent);
    //发起人不受权限影响
    ShareAnnotationSession host;
    host.Start(NULL, kAnnotHostSeat, 1);
    host.SetViewersAllowed(false);
    CHECK(host.CanDraw());
}

TEST_CASE("SSAN13 坐标换算与箭头两翼", "[shareannot]")
{
    const RECT content = { 100, 50, 1100, 550 };
    POINT p = { 600, 300 };
    AnnotationPoint a = ScreenToAnnot(content, p);
    CHECK(a.m_x == 32768);
    CHECK(a.m_y == 32768);
    POINT back = AnnotToScreen(content, a);
    CHECK(back.x == 600);
    CHECK(back.y == 300);
    //矩形以外夹到边缘
    POINT outside = { 50, 900 };
    a = ScreenToAnnot(content, outside);
    CHECK(a.m_x == 0);
    CHECK(a.m_y == kAnnotCoordMax);
    POINT corner = AnnotToScreen(content, pt(kAnnotCoordMax, 0));
    CHECK(corner.x == 1100);
    CHECK(corner.y == 50);
    //1000 宽的内容：10 像素 ≈ 655 个单位；线宽 3 → 1.5 像素取 1 以上；字号 28 → 500 高的 14 像素
    CHECK(AnnotTolerance(content, 10) == 655);
    CHECK(AnnotStrokePx(content, 4) == 2);
    CHECK(AnnotStrokePx(content, 1) >= 1);
    CHECK(AnnotTextPx(content, 28) == 14);
    CHECK(AnnotTextPx(content, 1) == kAnnotMinTextPx);
    //水平向右的箭头：两翼在箭头尖左边、上下对称
    POINT from = { 0, 100 };
    POINT to = { 200, 100 };
    POINT left;
    POINT right;
    ComputeArrowHead(from, to, 20, left, right);
    CHECK(left.x < 200);
    CHECK(right.x < 200);
    CHECK(left.x == right.x);
    CHECK(left.y + right.y == 200);
    CHECK(left.y != right.y);
}
