/**
 *  屏幕共享标注按画面区分的单元测试（Suite SSCV，标签 [sharecanvas]），ShareCanvasTests.cpp
 *
 *  覆盖 docs/屏幕共享与录制方案.md 第 8 节第 22 期 22e：多人同时共享时每块画面各有自己的标注。
 *    - SSCV1 编解码：样式块第 3 版带画面座位、往返不变，老样式块（16 / 26 字节）解出为默认画面；画笔位置与清空可带画面座位，
 *            老载荷不带时画笔位置为默认画面、清空为全部画面；
 *    - SSCV2 模型：按画面取笔迹与画笔位置；某作者在某块画面上的最后一笔；清空某块画面只清它、不带画面座位清空全部；
 *            本地清掉某块画面（那块画面已不在共享）；
 *    - SSCV3 会话：新画的笔迹带当前画面；橡皮、撤销、选择只作用于当前画面的笔迹；画笔位置带当前画面；
 *            发起人的清空只清当前画面；本地清掉某块画面不发出任何东西；
 *    - SSCV4 绘制：只画指定画面的笔迹；
 *    - SSCV5 按可见区域绘制（第 23 期白板）：世界比可见区域大、左上为负时，只画看得见的部分，位置按滚动换算。
 *  纯逻辑加内存位图，不建窗口。框架 Catch2 v3。
 *
 *  典型用法：FlamingoUnit.exe "[sharecanvas]"
 *
 *  balloonwj@qq.com   2026-10-05
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <string>
#include <vector>
#include "../../FlamingoMedia/Share/ShareAnnotation.h"
#include "../../FlamingoMedia/Share/ShareAnnotationRender.h"
#include "../../FlamingoMedia/Share/ShareAnnotationSession.h"
#include "../../FlamingoMedia/Share/ShareFraming.h"

using namespace screenshare;

namespace {

//发起人与一位观看者的座位号
const int kSscvHost = 0;
const int kSscvViewer = 3;
//几块画面的座位：发起人的画面、两位共享者的画面
const int kSscvCanvasA = 4;
const int kSscvCanvasB = 5;
//命中容差（归一化单位）
const int kSscvTolerance = 300;
//绘制用的位图边长（像素）
const int kSscvBitmapPx = 200;
//白色（GetPixel 的结果）
const COLORREF kSscvWhite = RGB(255, 255, 255);

//记下发出的每个包体
class SscvTransport : public ShareAudioTransport
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
AnnotationMessage sscvSent(const SscvTransport& t, size_t i)
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

//编码再解码
AnnotationMessage sscvRoundTrip(const AnnotationMessage& m)
{
    const std::string payload = ShareAnnotationCodec::encode(m);
    REQUIRE_FALSE(payload.empty());
    AnnotationMessage out;
    REQUIRE(ShareAnnotationCodec::decode(payload, out));
    return out;
}

AnnotationPoint sscvPt(int x, int y)
{
    return AnnotationPoint(static_cast<uint16_t>(x), static_cast<uint16_t>(y));
}

//以 seat 的身份在 canvas 上画一个矩形（begin 加 end）
void sscvAdd(ShareAnnotationModel& model, int seat, uint32_t id, int canvas, int x0, int y0, int x1, int y1)
{
    AnnotationMessage b;
    b.m_op = annot_op_begin;
    b.m_id = id;
    b.m_tool = annot_tool_rect;
    b.m_color = 0xE53935;
    b.m_width = 4;
    b.m_points.push_back(sscvPt(x0, y0));
    b.m_points.push_back(sscvPt(x1, y1));
    b.m_style.m_canvas = canvas;
    REQUIRE(model.Apply(seat, b, 0));
    AnnotationMessage e;
    e.m_op = annot_op_end;
    e.m_id = id;
    REQUIRE(model.Apply(seat, e, 0));
}

//会话里拖出一个两点对象
void sscvDrag(ShareAnnotationSession& s, int x0, int y0, int x1, int y1, int64_t now)
{
    s.PointerDown(sscvPt(x0, y0), kSscvTolerance, now);
    s.PointerMove(sscvPt(x1, y1), kSscvTolerance, now + 1);
    s.PointerUp(sscvPt(x1, y1), now + 2);
}

//在 canvas 那块画面的笔迹画进白底位图后，数 rc 里不是白色的像素
int sscvInkIn(const ShareAnnotationSession& s, int canvas, const RECT& rc)
{
    HDC screen = ::GetDC(NULL);
    HDC mem = ::CreateCompatibleDC(screen);
    HBITMAP bmp = ::CreateCompatibleBitmap(screen, kSscvBitmapPx, kSscvBitmapPx);
    ::ReleaseDC(NULL, screen);
    HGDIOBJ old = ::SelectObject(mem, bmp);
    RECT all = { 0, 0, kSscvBitmapPx, kSscvBitmapPx };
    ::FillRect(mem, &all, static_cast<HBRUSH>(::GetStockObject(WHITE_BRUSH)));
    DrawSessionCanvas(mem, all, s, canvas, false, 0);
    int ink = 0;
    for (int y = rc.top; y < rc.bottom; ++y)
    {
        for (int x = rc.left; x < rc.right; ++x)
        {
            if (::GetPixel(mem, x, y) != kSscvWhite)
                ++ink;
        }
    }
    ::SelectObject(mem, old);
    ::DeleteObject(bmp);
    ::DeleteDC(mem);
    return ink;
}

} // namespace

TEST_CASE("SSCV1 编解码：样式块带画面座位，老样式块为默认画面；画笔位置与清空可带画面座位", "[sharecanvas]")
{
    //开始一笔：画面座位随样式块往返
    AnnotationMessage b;
    b.m_op = annot_op_begin;
    b.m_id = 5;
    b.m_tool = annot_tool_rect;
    b.m_color = 0x1E88E5;
    b.m_width = 4;
    b.m_points.push_back(sscvPt(100, 200));
    b.m_points.push_back(sscvPt(900, 800));
    b.m_style.m_canvas = kSscvCanvasA;
    const AnnotationMessage rb = sscvRoundTrip(b);
    CHECK(rb.m_style.m_canvas == kSscvCanvasA);
    CHECK(rb.m_style == b.m_style);
    //样式比较带上画面座位
    AnnotationStyle other = b.m_style;
    other.m_canvas = kSscvCanvasB;
    CHECK_FALSE(other == b.m_style);
    CHECK(AnnotationStyle().m_canvas == kAnnotDefaultCanvas);

    //文字：样式块在载荷末尾；把它截成第 20 期的 26 字节，解出为默认画面
    AnnotationMessage t;
    t.m_op = annot_op_text;
    t.m_id = 6;
    t.m_tool = annot_tool_text;
    t.m_color = 0x000000;
    t.m_width = 2;
    t.m_points.push_back(sscvPt(300, 300));
    t.m_text = "abc";
    t.m_style.m_canvas = kSscvCanvasB;
    std::string payload = ShareAnnotationCodec::encode(t);
    REQUIRE(payload.size() > kAnnotStyleBytesV3 + 1);
    CHECK(static_cast<unsigned char>(payload[payload.size() - kAnnotStyleBytesV3 - 1]) == kAnnotStyleBytesV3);
    AnnotationMessage rt;
    REQUIRE(ShareAnnotationCodec::decode(payload, rt));
    CHECK(rt.m_style.m_canvas == kSscvCanvasB);
    payload[payload.size() - kAnnotStyleBytesV3 - 1] = static_cast<char>(kAnnotStyleBytesV2);
    payload.erase(payload.size() - 1);
    REQUIRE(ShareAnnotationCodec::decode(payload, rt));
    CHECK(rt.m_style.m_canvas == kAnnotDefaultCanvas);
    CHECK(rt.m_text == "abc");

    //画笔位置：带画面座位；老载荷（没有最后一个字节）为默认画面
    AnnotationMessage p;
    p.m_op = annot_op_pointer;
    p.m_tool = annot_tool_pen;
    p.m_color = 0x43A047;
    p.m_points.push_back(sscvPt(1000, 2000));
    p.m_canvas = kSscvCanvasA;
    CHECK(sscvRoundTrip(p).m_canvas == kSscvCanvasA);
    payload = ShareAnnotationCodec::encode(p);
    payload.erase(payload.size() - 1);
    AnnotationMessage rp;
    REQUIRE(ShareAnnotationCodec::decode(payload, rp));
    CHECK(rp.m_canvas == kAnnotDefaultCanvas);
    CHECK(rp.m_points.size() == 1);

    //清空：带画面座位；老载荷（只有操作字节）为全部画面
    AnnotationMessage c;
    c.m_op = annot_op_clear;
    c.m_canvas = kSscvCanvasB;
    CHECK(sscvRoundTrip(c).m_canvas == kSscvCanvasB);
    payload = ShareAnnotationCodec::encode(c);
    payload.erase(payload.size() - 1);
    AnnotationMessage rc;
    REQUIRE(ShareAnnotationCodec::decode(payload, rc));
    CHECK(rc.m_op == annot_op_clear);
    CHECK(rc.m_canvas == kAnnotAllCanvases);
}

TEST_CASE("SSCV2 模型：按画面取笔迹与画笔位置、某画面上的最后一笔、按画面清空、本地清掉某块画面", "[sharecanvas]")
{
    ShareAnnotationModel model;
    sscvAdd(model, kSscvHost, 1, kAnnotDefaultCanvas, 100, 100, 900, 900);
    sscvAdd(model, kSscvViewer, 2, kSscvCanvasA, 100, 100, 900, 900);
    sscvAdd(model, kSscvViewer, 3, kSscvCanvasA, 2000, 2000, 2900, 2900);
    sscvAdd(model, kSscvHost, 4, kSscvCanvasB, 100, 100, 900, 900);
    const std::vector<AnnotationStroke> onA = AnnotStrokesOnCanvas(model.Strokes(), kSscvCanvasA);
    REQUIRE(onA.size() == 2);
    CHECK(onA[0].m_id == 2);
    CHECK(onA[1].m_id == 3);
    CHECK(AnnotStrokesOnCanvas(model.Strokes(), kAnnotDefaultCanvas).size() == 1);
    CHECK(AnnotStrokesOnCanvas(model.Strokes(), 9).empty());

    //某作者在某块画面上的最后一笔
    uint32_t id = 0;
    REQUIRE(model.LastStrokeOf(kSscvViewer, kSscvCanvasA, id));
    CHECK(id == 3);
    CHECK_FALSE(model.LastStrokeOf(kSscvViewer, kSscvCanvasB, id));
    REQUIRE(model.LastStrokeOf(kSscvHost, kSscvCanvasB, id));
    CHECK(id == 4);

    //发起人清空画面 A：只清 A
    AnnotationMessage c;
    c.m_op = annot_op_clear;
    c.m_canvas = kSscvCanvasA;
    REQUIRE(model.Apply(kSscvHost, c, 0));
    REQUIRE(model.Strokes().size() == 2);
    CHECK(model.Strokes()[0].m_id == 1);
    CHECK(model.Strokes()[1].m_id == 4);
    //清空一块没有笔迹的画面：没有变化
    CHECK_FALSE(model.Apply(kSscvHost, c, 0));
    //观看者发的清空不接受
    c.m_canvas = kSscvCanvasB;
    CHECK_FALSE(model.Apply(kSscvViewer, c, 0));
    //不带画面座位（老版本）：清空全部
    c.m_canvas = kAnnotAllCanvases;
    REQUIRE(model.Apply(kSscvHost, c, 0));
    CHECK(model.Strokes().empty());

    //本地清掉某块画面（那块画面已不在共享）
    sscvAdd(model, kSscvViewer, 5, kSscvCanvasA, 100, 100, 900, 900);
    sscvAdd(model, kSscvViewer, 6, kSscvCanvasB, 100, 100, 900, 900);
    const uint64_t version = model.Version();
    CHECK(model.ClearCanvas(kSscvCanvasA));
    CHECK(model.Version() != version);
    REQUIRE(model.Strokes().size() == 1);
    CHECK(model.Strokes()[0].m_id == 6);
    CHECK_FALSE(model.ClearCanvas(kSscvCanvasA));

    //画笔位置带画面座位
    AnnotationMessage p;
    p.m_op = annot_op_pointer;
    p.m_tool = annot_tool_pen;
    p.m_points.push_back(sscvPt(500, 500));
    p.m_canvas = kSscvCanvasB;
    REQUIRE(model.Apply(kSscvViewer, p, 10));
    const std::vector<AnnotationPointer> pointers = model.Pointers();
    REQUIRE(pointers.size() == 1);
    CHECK(pointers[0].m_canvas == kSscvCanvasB);
    CHECK(AnnotPointersOnCanvas(pointers, kSscvCanvasB).size() == 1);
    CHECK(AnnotPointersOnCanvas(pointers, kSscvCanvasA).empty());
}

TEST_CASE("SSCV3 会话：新笔迹带当前画面；橡皮、撤销、选择只作用于当前画面；画笔位置带画面；清空只清当前画面", "[sharecanvas]")
{
    SscvTransport t;
    ShareAnnotationSession s;
    s.Start(&t, kSscvViewer, 100);
    CHECK(s.Canvas() == kAnnotDefaultCanvas);
    s.SetTool(annot_tool_rect);
    //同一位置在两块画面上各画一个矩形
    s.SetCanvas(kSscvCanvasA);
    CHECK(s.Canvas() == kSscvCanvasA);
    sscvDrag(s, 1000, 1000, 5000, 4000, 0);
    s.SetCanvas(kSscvCanvasB);
    sscvDrag(s, 1000, 1000, 5000, 4000, 10);
    REQUIRE(s.Model().Strokes().size() == 2);
    CHECK(s.Model().Strokes()[0].m_style.m_canvas == kSscvCanvasA);
    CHECK(s.Model().Strokes()[1].m_style.m_canvas == kSscvCanvasB);
    //发出的开始一笔也带着画面座位
    bool sawBegin = false;
    for (size_t i = 0; i < t.m_bodies.size(); ++i)
    {
        const AnnotationMessage m = sscvSent(t, i);
        if (m.m_op == annot_op_begin && m.m_style.m_canvas == kSscvCanvasA)
            sawBegin = true;
    }
    CHECK(sawBegin);

    //橡皮在画面 B 上点矩形左边：只擦掉 B 上的那个
    s.SetTool(kAnnotEraser);
    s.PointerDown(sscvPt(1000, 2500), kSscvTolerance, 20);
    s.PointerUp(sscvPt(1000, 2500), 21);
    REQUIRE(s.Model().Strokes().size() == 1);
    CHECK(s.Model().Strokes()[0].m_style.m_canvas == kSscvCanvasA);

    //撤销：画面 B 上已没有本人的笔迹，A 上那一笔不受影响；回到 A 再撤销才撤掉
    CHECK_FALSE(s.UndoOwn());
    REQUIRE(s.Model().Strokes().size() == 1);
    s.SetCanvas(kSscvCanvasA);
    CHECK(s.UndoOwn());
    CHECK(s.Model().Strokes().empty());

    //选择：两块画面同一位置各有一个矩形，在 B 上点选只选中 B 上的
    s.SetTool(annot_tool_rect);
    sscvDrag(s, 1000, 1000, 5000, 4000, 30);
    s.SetCanvas(kSscvCanvasB);
    sscvDrag(s, 1000, 1000, 5000, 4000, 40);
    REQUIRE(s.Model().Strokes().size() == 2);
    const AnnotationRef onB(kSscvViewer, s.Model().Strokes()[1].m_id);
    s.SetTool(kAnnotSelect);
    s.PointerDown(sscvPt(1000, 2500), kSscvTolerance, 50);
    s.PointerUp(sscvPt(1000, 2500), 51);
    REQUIRE(s.Selection().size() == 1);
    CHECK(s.Selection()[0] == onB);
    //换画面：选择随之取消
    s.SetCanvas(kSscvCanvasA);
    CHECK(s.Selection().empty());

    //画笔位置带当前画面
    s.SetTool(annot_tool_pen);
    const size_t before = t.m_bodies.size();
    s.PointerHover(sscvPt(7000, 7000), 1000);
    REQUIRE(t.m_bodies.size() == before + 1);
    const AnnotationMessage hover = sscvSent(t, before);
    CHECK(hover.m_op == annot_op_pointer);
    CHECK(hover.m_canvas == kSscvCanvasA);

    //发起人的清空：只清当前画面
    SscvTransport ht;
    ShareAnnotationSession h;
    h.Start(&ht, kSscvHost, 100);
    h.SetTool(annot_tool_rect);
    sscvDrag(h, 1000, 1000, 5000, 4000, 0);
    h.SetCanvas(kSscvCanvasA);
    sscvDrag(h, 1000, 1000, 5000, 4000, 10);
    REQUIRE(h.ClearAll());
    REQUIRE(h.Model().Strokes().size() == 1);
    CHECK(h.Model().Strokes()[0].m_style.m_canvas == kAnnotDefaultCanvas);
    const AnnotationMessage clear = sscvSent(ht, ht.m_bodies.size() - 1);
    CHECK(clear.m_op == annot_op_clear);
    CHECK(clear.m_canvas == kSscvCanvasA);

    //本地清掉某块画面（那块画面已不在共享）：只清它，不发出任何东西
    h.SetCanvas(kSscvCanvasB);
    sscvDrag(h, 1000, 1000, 5000, 4000, 20);
    const size_t sentBefore = ht.m_bodies.size();
    CHECK(h.ClearCanvasLocal(kSscvCanvasB));
    CHECK(ht.m_bodies.size() == sentBefore);
    REQUIRE(h.Model().Strokes().size() == 1);
    CHECK(h.Model().Strokes()[0].m_style.m_canvas == kAnnotDefaultCanvas);
    CHECK_FALSE(h.ClearCanvasLocal(kSscvCanvasB));
}

TEST_CASE("SSCV4 绘制：只画指定画面的笔迹", "[sharecanvas]")
{
    SscvTransport t;
    ShareAnnotationSession s;
    s.Start(&t, kSscvHost, 100);
    s.SetTool(annot_tool_rect);
    s.SetColor(0xE53935);
    //画面 A：左上角的矩形；画面 B：右下角的矩形
    s.SetCanvas(kSscvCanvasA);
    sscvDrag(s, 2000, 2000, 20000, 20000, 0);
    s.SetCanvas(kSscvCanvasB);
    sscvDrag(s, 40000, 40000, 60000, 60000, 10);
    //两个矩形在 200×200 位图里的范围（各留几个像素余量）
    const RECT topLeft = { 0, 0, 70, 70 };
    const RECT bottomRight = { 115, 115, 190, 190 };
    CHECK(sscvInkIn(s, kSscvCanvasA, topLeft) > 0);
    CHECK(sscvInkIn(s, kSscvCanvasA, bottomRight) == 0);
    CHECK(sscvInkIn(s, kSscvCanvasB, topLeft) == 0);
    CHECK(sscvInkIn(s, kSscvCanvasB, bottomRight) > 0);
    //没有笔迹的画面：什么也不画
    CHECK(sscvInkIn(s, 9, topLeft) == 0);
}

TEST_CASE("SSCV5 按可见区域绘制：世界比可见区域大时只画看得见的部分，位置按滚动换算，只画指定画面", "[sharecanvas]")
{
    SscvTransport t;
    ShareAnnotationSession s;
    s.Start(&t, kSscvHost, 100);
    s.SetTool(annot_tool_rect);
    s.SetColor(0xE53935);
    //白板（画面 A）：世界坐标 1/2～3/5 的矩形；另一块画面 B 在同一位置也有一个
    s.SetCanvas(kSscvCanvasA);
    sscvDrag(s, kAnnotCoordMax / 2, kAnnotCoordMax / 2, kAnnotCoordMax * 3 / 5, kAnnotCoordMax * 3 / 5, 0);
    s.SetCanvas(kSscvCanvasB);
    sscvDrag(s, kAnnotCoordMax / 2, kAnnotCoordMax / 2, kAnnotCoordMax * 3 / 5, kAnnotCoordMax * 3 / 5, 10);
    s.SetCanvas(kAnnotDefaultCanvas);
    //世界 600×600，滚到 (200, 200)：矩形在可见区域里约 (100,100)～(160,160)
    HDC screen = ::GetDC(NULL);
    HDC mem = ::CreateCompatibleDC(screen);
    HBITMAP bmp = ::CreateCompatibleBitmap(screen, kSscvBitmapPx, kSscvBitmapPx);
    ::ReleaseDC(NULL, screen);
    HGDIOBJ old = ::SelectObject(mem, bmp);
    RECT view = { 0, 0, kSscvBitmapPx, kSscvBitmapPx };
    ::FillRect(mem, &view, static_cast<HBRUSH>(::GetStockObject(WHITE_BRUSH)));
    const RECT content = { -200, -200, 400, 400 };
    DrawSessionCanvasInView(mem, view, content, s, kSscvCanvasA, 0);
    int inside = 0;
    int outside = 0;
    for (int y = 0; y < kSscvBitmapPx; ++y)
    {
        for (int x = 0; x < kSscvBitmapPx; ++x)
        {
            if (::GetPixel(mem, x, y) == kSscvWhite)
                continue;
            if (x >= 95 && x <= 165 && y >= 95 && y <= 165)
                ++inside;
            else
                ++outside;
        }
    }
    CHECK(inside > 0);
    CHECK(outside == 0);
    //没有笔迹的画面：什么也不画
    ::FillRect(mem, &view, static_cast<HBRUSH>(::GetStockObject(WHITE_BRUSH)));
    DrawSessionCanvasInView(mem, view, content, s, 9, 0);
    CHECK(::GetPixel(mem, 100, 130) == kSscvWhite);
    ::SelectObject(mem, old);
    ::DeleteObject(bmp);
    ::DeleteDC(mem);
}
