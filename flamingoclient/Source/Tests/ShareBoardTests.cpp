/**
 *  屏幕共享白板的单元测试（Suite SSWV，标签 [shareboard]），ShareBoardTests.cpp
 *
 *  覆盖 docs/屏幕共享与录制方案.md 第 2 节第 31、32、39、40 条与第 8 节第 23 期的纯逻辑：
 *    - SSWV1 「白板视野」操作（12）：编解码往返；模型按画面记下背景与可见区域（连同发送者），不进笔迹、不进整笔补发；
 *            本地清掉那块画面时一并清掉；
 *    - SSWV2 共享白板的世界：宽高比固定 16:9、画布就是整个世界；缩放（以光标处为中心、限制范围、背景线跟着缩放）；
 *            可见区域变大变小时按比例重算世界、保持缩放与相对位置；
 *    - SSWV3 「跟随创建者」：一方取出可见区域（按世界归一化），另一方按它缩放并滚动，看到的是同一块；
 *    - SSWV4 另存为图片的渲染：白板白底、网格按世界缩放、笔迹画上去、全不透明；画面加笔迹的合成，没有笔迹处保持原样；
 *    - SSWV5 存成 PNG（读回来尺寸与像素一致，目录不存在时失败）与默认文件名；
 *    - SSWV6 会话发出白板视野（先应用到本地、编码正确）；按窗口取它所在显示器的共享源（屏幕当白板）。
 *  纯逻辑加内存位图与临时文件，不建窗口。框架 Catch2 v3。
 *
 *  典型用法：FlamingoUnit.exe "[shareboard]"
 *
 *  balloonwj@qq.com   2026-10-05
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <objidl.h>        // gdiplus.h 依赖 IStream 等 COM 声明
#include <gdiplus.h>       // SSWV5 读回 PNG
#include <stdlib.h>
#include <string>
#include <vector>
#include "../../FlamingoMedia/Share/ShareAnnotation.h"
#include "../../FlamingoMedia/Share/ShareAnnotationSession.h"
#include "../../FlamingoMedia/Share/ShareFraming.h"
#include "../../FlamingoMedia/Share/ShareHostStreamer.h"
#include "../../FlamingoMedia/Share/ShareImageExport.h"
#include "../../FlamingoMedia/Share/ShareSources.h"
#include "../../FlamingoMedia/Share/ShareWhiteboard.h"

using namespace screenshare;

namespace {

//白板的画面座位、建立者与另一位成员的座位
const int kSswvBoard = 6;
const int kSswvOwner = 2;
const int kSswvOther = 3;
//允许的取整误差（归一化单位）
const int kSswvSlack = 200;

//编码再解码
AnnotationMessage sswvRoundTrip(const AnnotationMessage& m)
{
    const std::string payload = ShareAnnotationCodec::encode(m);
    REQUIRE_FALSE(payload.empty());
    AnnotationMessage out;
    REQUIRE(ShareAnnotationCodec::decode(payload, out));
    return out;
}

//导出用的图片尺寸（像素）：网格间距为 32 × 1920 / 3840 = 16
const int kSswvImgW = 1920;
const int kSswvImgH = 1080;
//导出用的笔迹：红色、线宽 8（约 7.7 像素）、对角为 (1/4, 1/4) 与 (3/4, 3/4) 的矩形
const uint32_t kSswvRed = 0xE53935;
const int kSswvStrokeWidth = 8;
const int kSswvQuarter = kAnnotCoordMax / 4;

AnnotationStroke sswvRect()
{
    AnnotationStroke s;
    s.m_author = kSswvOwner;
    s.m_id = 1;
    s.m_tool = annot_tool_rect;
    s.m_color = kSswvRed;
    s.m_width = kSswvStrokeWidth;
    s.m_finished = true;
    s.m_points.push_back(AnnotationPoint(static_cast<uint16_t>(kSswvQuarter), static_cast<uint16_t>(kSswvQuarter)));
    s.m_points.push_back(AnnotationPoint(static_cast<uint16_t>(3 * kSswvQuarter), static_cast<uint16_t>(3 * kSswvQuarter)));
    return s;
}

//图片里某个像素（BGRA）
const uint8_t* sswvPixel(const std::vector<uint8_t>& img, int width, int x, int y)
{
    return &img[(static_cast<size_t>(y) * width + x) * 4];
}

//一条白板视野操作
AnnotationMessage sswvBoard(int canvas, int background, int left, int top, int width)
{
    AnnotationMessage m;
    m.m_op = annot_op_board;
    m.m_canvas = canvas;
    m.m_board.m_background = background;
    m.m_board.m_left = left;
    m.m_board.m_top = top;
    m.m_board.m_width = width;
    return m;
}

} // namespace

TEST_CASE("SSWV1 白板视野操作：编解码往返；模型按画面记下（带发送者），不进笔迹与补发；本地清掉画面时一并清掉", "[shareboard]")
{
    const AnnotationMessage m = sswvBoard(kSswvBoard, wb_bg_grid, 1200, 3400, 30000);
    const AnnotationMessage r = sswvRoundTrip(m);
    CHECK(r.m_op == annot_op_board);
    CHECK(r.m_canvas == kSswvBoard);
    CHECK(r.m_board.m_background == wb_bg_grid);
    CHECK(r.m_board.m_left == 1200);
    CHECK(r.m_board.m_top == 3400);
    CHECK(r.m_board.m_width == 30000);
    //载荷长度：操作 1、画面 1、背景 1、左 2、上 2、宽 2
    CHECK(ShareAnnotationCodec::encode(m).size() == 9);
    //宽为 0 的视野不合法
    AnnotationMessage bad = sswvBoard(kSswvBoard, wb_bg_plain, 0, 0, 0);
    AnnotationMessage out;
    CHECK_FALSE(ShareAnnotationCodec::decode(ShareAnnotationCodec::encode(bad), out));

    ShareAnnotationModel model;
    AnnotationBoardView v;
    CHECK_FALSE(model.BoardView(kSswvBoard, v));
    const uint64_t version = model.Version();
    REQUIRE(model.Apply(kSswvOwner, m, 100));
    REQUIRE(model.BoardView(kSswvBoard, v));
    CHECK(v.m_background == wb_bg_grid);
    CHECK(v.m_width == 30000);
    CHECK(v.m_senderSeat == kSswvOwner);
    CHECK(v.m_updatedMs == 100);
    //不进笔迹、不进补发；版本号照样变（界面据此重画背景）
    CHECK(model.Strokes().empty());
    CHECK(model.Snapshot().empty());
    CHECK(model.Version() != version);
    //后到的覆盖先到的
    REQUIRE(model.Apply(kSswvOther, sswvBoard(kSswvBoard, wb_bg_lines, 0, 0, 65535), 200));
    REQUIRE(model.BoardView(kSswvBoard, v));
    CHECK(v.m_background == wb_bg_lines);
    CHECK(v.m_senderSeat == kSswvOther);
    //别的画面没有
    CHECK_FALSE(model.BoardView(kSswvBoard + 1, v));
    //本地清掉这块画面：视野一并清掉
    model.ClearCanvas(kSswvBoard);
    CHECK_FALSE(model.BoardView(kSswvBoard, v));
}

TEST_CASE("SSWV2 共享白板的世界：16:9、画布即世界；缩放以光标处为中心、有范围、背景线跟着缩放；可见区域变化时按比例重算", "[shareboard]")
{
    ShareWhiteboardView v;
    v.InitShared(1000, 700);
    CHECK(v.IsShared());
    CHECK(v.Zoom() == Catch::Approx(1.0));
    CHECK(v.WorldWidth() == 2000);
    CHECK(v.WorldHeight() == 1125);
    CHECK(v.CanvasWidth() == v.WorldWidth());
    CHECK(v.CanvasHeight() == v.WorldHeight());
    //画布就是世界：拖边不会改变
    v.DragEdge(wb_edge_corner, 500, 500);
    CHECK(v.CanvasWidth() == v.WorldWidth());
    //网格线间距：世界 2000 宽时为 32 × 2000 / 3840 ≈ 17 像素
    std::vector<int> xs;
    std::vector<int> ys;
    v.BackgroundLines(wb_bg_grid, xs, ys);
    REQUIRE(xs.size() >= 2);
    CHECK(abs((xs[1] - xs[0]) - 17) <= 1);

    //放大一倍，以 (400, 300) 为中心：那一点下面的世界位置不变
    const AnnotationPoint before = v.ViewToAnnot(400, 300);
    REQUIRE(v.SetZoom(2.0, 400, 300));
    CHECK(v.Zoom() == Catch::Approx(2.0));
    CHECK(v.WorldWidth() == 4000);
    CHECK(v.CanvasWidth() == 4000);
    const AnnotationPoint after = v.ViewToAnnot(400, 300);
    CHECK(abs(static_cast<int>(after.m_x) - before.m_x) <= kSswvSlack);
    CHECK(abs(static_cast<int>(after.m_y) - before.m_y) <= kSswvSlack);
    //网格线跟着放大
    v.BackgroundLines(wb_bg_grid, xs, ys);
    REQUIRE(xs.size() >= 2);
    CHECK(abs((xs[1] - xs[0]) - 33) <= 1);
    //范围：不超过上限、不低于下限；不变时返回 false
    v.SetZoom(100.0, 0, 0);
    CHECK(v.Zoom() == Catch::Approx(kWhiteboardMaxZoom));
    CHECK_FALSE(v.SetZoom(kWhiteboardMaxZoom, 0, 0));
    v.SetZoom(0.01, 0, 0);
    CHECK(v.Zoom() == Catch::Approx(kWhiteboardMinZoom));
    //缩到比可见区域还小：不滚动
    CHECK(v.ScrollX() == 0);
    CHECK(v.ScrollY() == 0);

    //可见区域变宽一倍：世界按比例变宽，缩放与相对位置保持
    ShareWhiteboardView w;
    w.InitShared(1000, 700);
    w.SetZoom(2.0, 0, 0);
    w.ScrollBy(1000, 200);
    int left0 = 0;
    int top0 = 0;
    int width0 = 0;
    w.VisibleRegion(left0, top0, width0);
    w.SetViewSize(2000, 1400);
    CHECK(w.Zoom() == Catch::Approx(2.0));
    CHECK(w.WorldWidth() == 8000);
    int left1 = 0;
    int top1 = 0;
    int width1 = 0;
    w.VisibleRegion(left1, top1, width1);
    CHECK(abs(left1 - left0) <= kSswvSlack);
    CHECK(abs(top1 - top0) <= kSswvSlack);
    CHECK(abs(width1 - width0) <= kSswvSlack);

    //本机白板（第 21 期）照旧：世界是可见区域的两倍，不缩放
    ShareWhiteboardView local;
    local.Init(800, 600);
    CHECK_FALSE(local.IsShared());
    CHECK(local.WorldWidth() == 1600);
    CHECK(local.WorldHeight() == 1200);
    CHECK(local.Zoom() == Catch::Approx(1.0));
}

TEST_CASE("SSWV3 跟随创建者：取出的可见区域交给另一方，另一方缩放并滚动后看到的是同一块", "[shareboard]")
{
    //创建者：可见区域 1200×675，放大 1.5 倍后滚到右下
    ShareWhiteboardView creator;
    creator.InitShared(1200, 675);
    creator.SetZoom(1.5, 0, 0);
    creator.ScrollBy(900, 500);
    int left = 0;
    int top = 0;
    int width = 0;
    creator.VisibleRegion(left, top, width);
    CHECK(width > 0);
    CHECK(width < kAnnotCoordMax);
    //跟随者：可见区域只有 600×338
    ShareWhiteboardView follower;
    follower.InitShared(600, 338);
    follower.ShowRegion(left, top, width);
    int fl = 0;
    int ft = 0;
    int fw = 0;
    follower.VisibleRegion(fl, ft, fw);
    CHECK(abs(fl - left) <= kSswvSlack);
    CHECK(abs(ft - top) <= kSswvSlack);
    CHECK(abs(fw - width) <= kSswvSlack);
    //两边可见区域左上角对应同一个世界位置
    const AnnotationPoint a = creator.ViewToAnnot(0, 0);
    const AnnotationPoint b = follower.ViewToAnnot(0, 0);
    CHECK(abs(static_cast<int>(a.m_x) - b.m_x) <= kSswvSlack);
    CHECK(abs(static_cast<int>(a.m_y) - b.m_y) <= kSswvSlack);
    //宽为 0 的区域不处理
    const double zoom = follower.Zoom();
    follower.ShowRegion(0, 0, 0);
    CHECK(follower.Zoom() == Catch::Approx(zoom));
}

TEST_CASE("SSWV4 另存为图片的渲染：白板白底、网格按世界缩放、笔迹、全不透明；画面加笔迹，没有笔迹处保持原样", "[shareboard]")
{
    const std::vector<AnnotationStroke> strokes(1, sswvRect());
    std::vector<uint8_t> img;
    RenderBoardImage(kSswvImgW, kSswvImgH, wb_bg_grid, strokes, img);
    REQUIRE(img.size() == static_cast<size_t>(kSswvImgW) * kSswvImgH * 4);
    //网格线在 16 的倍数上，两线之间是白底
    const uint8_t* grid = sswvPixel(img, kSswvImgW, 16, 5);
    const uint8_t* blank = sswvPixel(img, kSswvImgW, 8, 5);
    CHECK(blank[0] == 255);
    CHECK(blank[1] == 255);
    CHECK(blank[2] == 255);
    CHECK((grid[0] != 255 || grid[1] != 255 || grid[2] != 255));
    //矩形左边（x = 480）上是红色
    const uint8_t* edge = sswvPixel(img, kSswvImgW, 480, 540);
    CHECK(edge[2] > 200);
    CHECK(edge[1] < 120);
    //全不透明
    CHECK(blank[3] == 255);
    CHECK(edge[3] == 255);
    //纯白背景：没有网格
    RenderBoardImage(kSswvImgW, kSswvImgH, wb_bg_plain, strokes, img);
    const uint8_t* plain = sswvPixel(img, kSswvImgW, 16, 5);
    CHECK(plain[0] == 255);
    CHECK(plain[1] == 255);
    CHECK(plain[2] == 255);
    //尺寸不合法：空
    RenderBoardImage(0, kSswvImgH, wb_bg_plain, strokes, img);
    CHECK(img.empty());

    //画面：纯蓝，合成之后没有笔迹处仍是蓝色，矩形边上是红色
    std::vector<uint8_t> frame(static_cast<size_t>(kSswvImgW) * kSswvImgH * 4, 0);
    for (size_t i = 0; i < frame.size(); i += 4)
    {
        frame[i] = 255;
        frame[i + 3] = 255;
    }
    std::vector<uint8_t> out;
    RenderScreenImage(frame, kSswvImgW, kSswvImgH, strokes, out);
    REQUIRE(out.size() == frame.size());
    const uint8_t* keep = sswvPixel(out, kSswvImgW, 100, 100);
    CHECK(keep[0] == 255);
    CHECK(keep[1] == 0);
    CHECK(keep[2] == 0);
    const uint8_t* line = sswvPixel(out, kSswvImgW, 480, 540);
    CHECK(line[2] > 200);
    CHECK(line[0] < 120);
    CHECK(line[3] == 255);
    //画面大小与尺寸不符：空
    RenderScreenImage(std::vector<uint8_t>(16, 0), kSswvImgW, kSswvImgH, strokes, out);
    CHECK(out.empty());
}

TEST_CASE("SSWV5 存成 PNG：读回来尺寸与像素一致，目录不存在时失败；默认文件名", "[shareboard]")
{
    const std::vector<AnnotationStroke> strokes(1, sswvRect());
    std::vector<uint8_t> img;
    RenderBoardImage(kSswvImgW, kSswvImgH, wb_bg_grid, strokes, img);
    REQUIRE_FALSE(img.empty());
    wchar_t dir[MAX_PATH] = { 0 };
    REQUIRE(::GetTempPathW(MAX_PATH, dir) > 0);
    const std::wstring path = std::wstring(dir) + L"flamingo_sswv5_" + std::to_wstring(::GetCurrentProcessId()) + L".png";
    REQUIRE(SaveBgraAsPng(path, img, kSswvImgW, kSswvImgH));
    //读回来
    ULONG_PTR token = 0;
    Gdiplus::GdiplusStartupInput input;
    Gdiplus::GdiplusStartup(&token, &input, NULL);
    {
        Gdiplus::Bitmap bmp(path.c_str());
        REQUIRE(bmp.GetLastStatus() == Gdiplus::Ok);
        CHECK(static_cast<int>(bmp.GetWidth()) == kSswvImgW);
        CHECK(static_cast<int>(bmp.GetHeight()) == kSswvImgH);
        Gdiplus::Color c;
        REQUIRE(bmp.GetPixel(480, 540, &c) == Gdiplus::Ok);
        CHECK(c.GetR() > 200);
        CHECK(c.GetG() < 120);
        REQUIRE(bmp.GetPixel(8, 5, &c) == Gdiplus::Ok);
        CHECK(c.GetR() == 255);
        CHECK(c.GetA() == 255);
    }
    ::DeleteFileW(path.c_str());
    //目录不存在：失败
    CHECK_FALSE(SaveBgraAsPng(std::wstring(dir) + L"no_such_dir_sswv5\a.png", img, kSswvImgW, kSswvImgH));
    //尺寸与数据不符：失败
    CHECK_FALSE(SaveBgraAsPng(path, std::vector<uint8_t>(4, 0), kSswvImgW, kSswvImgH));

    //默认文件名（单测里没有加载语言包，前缀为简体中文原文）
    SYSTEMTIME t;
    memset(&t, 0, sizeof(t));
    t.wYear = 2026;
    t.wMonth = 10;
    t.wDay = 5;
    t.wHour = 15;
    t.wMinute = 30;
    t.wSecond = 12;
    CHECK(MakeAnnotExportFileName(true, t) == L"白板_20261005_153012.png");
    CHECK(MakeAnnotExportFileName(false, t) == L"屏幕标注_20261005_153012.png");
}

namespace {

//记下发出的每个包体
class SswvTransport : public ShareAudioTransport
{
public:
    virtual bool SendRoomBody(const std::string& body) override
    {
        m_bodies.push_back(body);
        return true;
    }

    std::vector<std::string> m_bodies;   //发出的包体，按先后
};

} // namespace

TEST_CASE("SSWV6 会话发出白板视野；按窗口取它所在显示器的共享源", "[shareboard]")
{
    SswvTransport t;
    ShareAnnotationSession s;
    //没开始：不发
    AnnotationBoardView v;
    v.m_background = wb_bg_lines;
    v.m_left = 100;
    v.m_top = 200;
    v.m_width = 40000;
    CHECK_FALSE(s.SendBoardView(kSswvBoard, v));
    s.Start(&t, kSswvOwner, 100);
    REQUIRE(s.SendBoardView(kSswvBoard, v));
    REQUIRE(t.m_bodies.size() == 1);
    ShareFrameHeader header;
    const char* p = NULL;
    size_t len = 0;
    REQUIRE(ShareFraming::decodeBody(t.m_bodies[0].data(), t.m_bodies[0].size(), header, p, len));
    AnnotationMessage m;
    REQUIRE(ShareAnnotationCodec::decode(std::string(p, len), m));
    CHECK(m.m_op == annot_op_board);
    CHECK(m.m_canvas == kSswvBoard);
    CHECK(m.m_board.m_background == wb_bg_lines);
    CHECK(m.m_board.m_width == 40000);
    //先应用到本地
    AnnotationBoardView local;
    REQUIRE(s.Model().BoardView(kSswvBoard, local));
    CHECK(local.m_senderSeat == kSswvOwner);
    CHECK(local.m_left == 100);
    //宽不合法：不发
    v.m_width = 0;
    CHECK_FALSE(s.SendBoardView(kSswvBoard, v));
    CHECK(t.m_bodies.size() == 1);

    //桌面窗口在主显示器上
    ShareSourceSpec spec;
    REQUIRE(MonitorSourceForWindow(::GetDesktopWindow(), spec));
    CHECK(spec.m_kind == share_source_monitor);
    MONITORINFO mi;
    memset(&mi, 0, sizeof(mi));
    mi.cbSize = sizeof(mi);
    REQUIRE(::GetMonitorInfo(::MonitorFromWindow(::GetDesktopWindow(), MONITOR_DEFAULTTOPRIMARY), &mi));
    CHECK(::EqualRect(&spec.m_monitorRect, &mi.rcMonitor));
    //不是窗口：找不到
    CHECK_FALSE(MonitorSourceForWindow(NULL, spec));
}
