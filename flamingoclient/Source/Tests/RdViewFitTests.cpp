/**
 *  远程桌面观看端等比显示与坐标换算的单元测试（标签 [rdres]），RdViewFitTests.cpp
 *  背景：2026-10-12 前远程桌面观看窗把画面拉伸铺满，两边屏幕宽高比不同时画面变形（docs/远程桌面控制权限与观看工具条方案.md
 *  第 3.2 节），改为等比显示、鼠标坐标扣除黑边。与用户确认的用例：
 *    - RES1 等比适应矩形：宽画面上下留黑边、高画面左右留黑边、宽高比相同时没有黑边、控件比画面小时也正确；
 *    - RES2 坐标扣除黑边：画面内的点换成被控端像素；黑边上的移动不发送；按下在画面内、松开在画面外时夹到边缘；
 *    - RES3 推流端关键帧策略：采集尺寸变化时立即发关键帧，走 P2P 时也发；尺寸不变时按原有规则。2026-10-14 起改为测编码器：
 *      首次喂入与尺寸变化后报告需要关键帧，发过首屏后不再报告，尺寸不变的后续帧不报告；推流循环据此当轮发出；
 *    - RES4 GDI 降级采集器：屏幕尺寸变化后报告新尺寸（取屏幕尺寸的函数换成用例里的模拟值）；
 *    - RES5 远端指针换算：主屏坐标换到画面矩形（含黑边与缩放）；指针不在被采集的屏幕上时不画。
 *  2026-10-17 远程桌面 H.264 联调发现被控端抓屏不阻塞时推流线程空转（docs/远程桌面H264与共享画质提升方案.md 8.5），补充：
 *    - RES8 GDI 降级采集器：两次拷屏之间至少间隔调用方给的等待时长（GDI 没有「画面变了」的通知，原先每次调用都立即拷屏）。
 *  RES4 会用 GDI 从桌面抓几帧小画面；RES8 把拷屏函数换成写入纯色画面，不依赖桌面；其余是纯逻辑。框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-12
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include "../../FlamingoMedia/RemoteDesktop/RdViewFit.h"
#include <vector>
#include <chrono>
#include "../../FlamingoMedia/RemoteDesktop/RdKeyframePolicy.h"
#include "../../FlamingoMedia/RemoteDesktop/ScreenFrameEncoder.h"
#include "../../FlamingoMedia/RemoteDesktop/GdiScreenCapturer.h"

using namespace remotedesktop;

namespace {

//矩形
RECT RvfRect(int l, int t, int r, int b)
{
    RECT rc = { l, t, r, b };
    return rc;
}

//点
POINT RvfPt(int x, int y)
{
    POINT pt = { x, y };
    return pt;
}

//BGRA32 每像素字节数
const int kRvfBgraBytes = 4;

/**
 *  RES3 用的一块纯色 BGRA32 画面（top-down、行间无填充）。
 *  @param pixels [出参] 像素缓冲，FrameView 指向它，须在使用 FrameView 期间保持不变
 *  @param w / h  画面尺寸
 *  @param value  每个字节的取值
 */
FrameView RvfFrame(std::vector<BYTE>& pixels, int w, int h, BYTE value)
{
    pixels.assign(static_cast<size_t>(w) * h * kRvfBgraBytes, value);
    FrameView view;
    view.m_data = &pixels[0];
    view.m_width = w;
    view.m_height = h;
    view.m_stride = w * kRvfBgraBytes;
    return view;
}

//RES4 用的模拟屏幕尺寸
SIZE g_rvfScreen = { 320, 240 };

SIZE RvfScreenProbe()
{
    return g_rvfScreen;
}

//RES8 的拷屏函数写入的纯色
COLORREF g_rvfFill = RGB(10, 20, 30);

//RES8 的拷屏函数：把抓取目标填成 g_rvfFill，不碰桌面 DC（隐藏桌面上也能运行）
BOOL RvfFillBlit(HDC memDC, HDC deskDC, int width, int height)
{
    (void)deskDC;
    RECT rc = { 0, 0, width, height };
    HBRUSH brush = ::CreateSolidBrush(g_rvfFill);
    const int filled = ::FillRect(memDC, &rc, brush);
    ::DeleteObject(brush);
    ::GdiFlush();
    return filled != 0 ? TRUE : FALSE;
}

//RES8 给采集器的等待时长（毫秒）
const UINT kRvfPaceMs = 60;
//RES8 的计时余量（毫秒）：Sleep 的精度受系统计时器影响，可能比请求的时长略短
const long long kRvfPaceSlackMs = 20;

} // namespace

TEST_CASE("RES1 aspect fit rectangle keeps the picture's aspect ratio", "[rdres]")
{
    //16:9 的画面放进 4:3 的区域：左右铺满，上下各留黑边
    RECT fit = ComputeRdFitRect(RvfRect(0, 0, 800, 600), 1920, 1080);
    CHECK(fit.left == 0);
    CHECK(fit.right == 800);
    CHECK(fit.bottom - fit.top == 450);
    CHECK(fit.top == 75);
    //4:3 的画面放进 16:9 的区域：上下铺满，左右各留黑边
    fit = ComputeRdFitRect(RvfRect(0, 0, 1600, 900), 1024, 768);
    CHECK(fit.top == 0);
    CHECK(fit.bottom == 900);
    CHECK(fit.right - fit.left == 1200);
    CHECK(fit.left == 200);
    //宽高比相同：没有黑边（区域有偏移时照样对齐）
    fit = ComputeRdFitRect(RvfRect(10, 20, 970, 560), 1920, 1080);
    CHECK(fit.left == 10);
    CHECK(fit.top == 20);
    CHECK(fit.right == 970);
    CHECK(fit.bottom == 560);
    //控件比画面小：照样等比缩小
    fit = ComputeRdFitRect(RvfRect(0, 0, 320, 320), 2560, 1440);
    CHECK(fit.right - fit.left == 320);
    CHECK(fit.bottom - fit.top == 180);
    CHECK(fit.top == 70);
    //画面尺寸未知：原样返回区域
    fit = ComputeRdFitRect(RvfRect(0, 0, 100, 100), 0, 0);
    CHECK(fit.right == 100);
    CHECK(fit.bottom == 100);
}

TEST_CASE("RES2 mouse coordinates skip the black bars and clamp drags to the picture edge", "[rdres]")
{
    const int frameW = 1920;
    const int frameH = 1080;
    //800×600 的画面控件，画面占 (0,75)~(800,525)
    const RECT fit = ComputeRdFitRect(RvfRect(0, 0, 800, 600), frameW, frameH);
    RdMouseMapper m;
    int rx = -1;
    int ry = -1;
    //画面中心换成被控端中心
    REQUIRE(m.Map(WM_MOUSEMOVE, RvfPt(400, 300), fit, frameW, frameH, rx, ry));
    CHECK(rx == 960);
    CHECK(ry == 540);
    //画面左上角
    REQUIRE(m.Map(WM_MOUSEMOVE, RvfPt(0, 75), fit, frameW, frameH, rx, ry));
    CHECK(rx == 0);
    CHECK(ry == 0);
    //黑边上的移动、按下与滚轮都不发送
    CHECK_FALSE(m.Map(WM_MOUSEMOVE, RvfPt(400, 30), fit, frameW, frameH, rx, ry));
    CHECK_FALSE(m.Map(WM_LBUTTONDOWN, RvfPt(400, 580), fit, frameW, frameH, rx, ry));
    CHECK_FALSE(m.Map(WM_MOUSEWHEEL, RvfPt(400, 580), fit, frameW, frameH, rx, ry));
    CHECK(m.ButtonsDown() == 0);
    //黑边上按下的按钮在黑边上松开：也不发送
    CHECK_FALSE(m.Map(WM_LBUTTONUP, RvfPt(400, 580), fit, frameW, frameH, rx, ry));
    //画面内按下，拖到画面外：移动与松开都夹到边缘照发
    REQUIRE(m.Map(WM_LBUTTONDOWN, RvfPt(400, 300), fit, frameW, frameH, rx, ry));
    CHECK(m.ButtonsDown() == 1);
    REQUIRE(m.Map(WM_MOUSEMOVE, RvfPt(900, 30), fit, frameW, frameH, rx, ry));
    CHECK(rx == frameW - 1);
    CHECK(ry == 0);
    REQUIRE(m.Map(WM_LBUTTONUP, RvfPt(-50, 700), fit, frameW, frameH, rx, ry));
    CHECK(rx == 0);
    CHECK(ry == frameH - 1);
    CHECK(m.ButtonsDown() == 0);
    //松开之后黑边上的移动又不发送
    CHECK_FALSE(m.Map(WM_MOUSEMOVE, RvfPt(900, 30), fit, frameW, frameH, rx, ry));
    //右键在画面内按下后失去控制：Reset 忘掉按钮，画面外的松开不再发送
    REQUIRE(m.Map(WM_RBUTTONDOWN, RvfPt(400, 300), fit, frameW, frameH, rx, ry));
    m.Reset();
    CHECK_FALSE(m.Map(WM_RBUTTONUP, RvfPt(400, 590), fit, frameW, frameH, rx, ry));
}

TEST_CASE("RES3 the encoder reports when a keyframe is needed, and the policy sends one at once", "[rdres]")
{
    //编码器（2026-10-14 起由它报告）：首次喂入与尺寸变化后报告需要关键帧，发过首屏后不再报告；尺寸不变的后续帧不报告
    const int w = 4;
    const int h = 2;
    const int wider = 6;
    std::vector<BYTE> pixels;
    CScreenFrameEncoder enc;
    std::vector<BYTE> out;
    CHECK_FALSE(enc.NeedsKeyframe());
    enc.Feed(RvfFrame(pixels, w, h, 0x10));
    CHECK(enc.NeedsKeyframe());
    enc.GetBitmapInfo(out);
    CHECK(enc.NeedsKeyframe());
    enc.GetFirstFrame(out);
    CHECK_FALSE(enc.NeedsKeyframe());
    //尺寸不变、内容变了：不报告，照常发增量帧
    enc.Feed(RvfFrame(pixels, w, h, 0x20));
    CHECK_FALSE(enc.NeedsKeyframe());
    enc.GetNextFrame(out, RvfPt(0, 0), 0);
    CHECK_FALSE(enc.NeedsKeyframe());
    //尺寸变了：再次报告，发过首屏后不再报告
    enc.Feed(RvfFrame(pixels, wider, h, 0x20));
    CHECK(enc.NeedsKeyframe());
    enc.GetFirstFrame(out);
    CHECK_FALSE(enc.NeedsKeyframe());

    //推流循环的判断：控制端请求、编码器报告时立即发（经 P2P 也发）；否则经中转时到了间隔才发，经 P2P 时不发
    const int every = 10;
    CHECK(RdShouldSendKeyframe(true, false, 0, every, false));
    CHECK(RdShouldSendKeyframe(false, false, 1, every, true));
    CHECK(RdShouldSendKeyframe(false, true, 1, every, true));
    CHECK_FALSE(RdShouldSendKeyframe(false, false, 100, every, false));
    CHECK_FALSE(RdShouldSendKeyframe(false, true, every - 1, every, false));
    CHECK(RdShouldSendKeyframe(false, true, every, every, false));
}

TEST_CASE("RES4 the GDI capturer reports the new size after the screen size changes", "[rdres]")
{
    CGdiScreenCapturer::SetScreenSizeProbeForTest(&RvfScreenProbe);
    g_rvfScreen.cx = 320;
    g_rvfScreen.cy = 240;
    {
        CGdiScreenCapturer gdi;
        REQUIRE(gdi.Init());
        CHECK(gdi.ScreenWidth() == 320);
        //隐藏桌面不是当前输入桌面，从桌面 DC 的 BitBlt 会失败（返回 Fatal）；尺寸的报告与 BitBlt 成败无关，
        //抓帧成功时（在可见桌面上运行）再核对帧本身的尺寸
        FrameView fv;
        if (gdi.CaptureFrame(0, fv) == kCaptureOk)
        {
            CHECK(fv.m_width == 320);
            CHECK(fv.m_height == 240);
        }
        //分辨率变了：抓下一帧时按新尺寸重建
        g_rvfScreen.cx = 200;
        g_rvfScreen.cy = 100;
        const CaptureResult r = gdi.CaptureFrame(0, fv);
        CHECK(gdi.ScreenWidth() == 200);
        CHECK(gdi.ScreenHeight() == 100);
        if (r == kCaptureOk)
        {
            CHECK(fv.m_width == 200);
            CHECK(fv.m_height == 100);
            CHECK(fv.m_stride == 200 * 4);
        }
    }
    CGdiScreenCapturer::SetScreenSizeProbeForTest(NULL);
}

TEST_CASE("RES8 the GDI capturer waits at least the given time between two screen copies", "[rdres]")
{
    CGdiScreenCapturer::SetScreenSizeProbeForTest(&RvfScreenProbe);
    CGdiScreenCapturer::SetScreenBlitForTest(&RvfFillBlit);
    g_rvfScreen.cx = 64;
    g_rvfScreen.cy = 48;
    {
        CGdiScreenCapturer gdi;
        REQUIRE(gdi.Init());
        FrameView fv;
        //第一次拷屏不等待，像素为写入的颜色（BGRA：蓝、绿、红）
        g_rvfFill = RGB(1, 2, 3);
        REQUIRE(gdi.CaptureFrame(kRvfPaceMs, fv) == kCaptureOk);
        CHECK(fv.m_data[0] == 3);
        CHECK(fv.m_data[1] == 2);
        CHECK(fv.m_data[2] == 1);
        //紧接着再抓两次：每次都等到距上次拷屏满 kRvfPaceMs
        const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
        CHECK(gdi.CaptureFrame(kRvfPaceMs, fv) == kCaptureOk);
        g_rvfFill = RGB(4, 5, 6);
        REQUIRE(gdi.CaptureFrame(kRvfPaceMs, fv) == kCaptureOk);
        CHECK(fv.m_data[0] == 6);
        const long long elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count();
        CHECK(elapsedMs >= 2 * static_cast<long long>(kRvfPaceMs) - kRvfPaceSlackMs);
    }
    CGdiScreenCapturer::SetScreenBlitForTest(NULL);
    CGdiScreenCapturer::SetScreenSizeProbeForTest(NULL);
}

TEST_CASE("RES5 the remote cursor is mapped into the picture and hidden when off the captured screen", "[rdres]")
{
    const int frameW = 1920;
    const int frameH = 1080;
    //800×600 的画面控件，画面占 (0,75)~(800,525)
    const RECT fit = ComputeRdFitRect(RvfRect(0, 0, 800, 600), frameW, frameH);
    POINT at;
    REQUIRE(MapRdRemoteToView(RvfPt(960, 540), frameW, frameH, fit, at));
    CHECK(at.x == 400);
    CHECK(at.y == 300);
    REQUIRE(MapRdRemoteToView(RvfPt(0, 0), frameW, frameH, fit, at));
    CHECK(at.x == 0);
    CHECK(at.y == 75);
    //指针在别的显示器上（坐标超出主屏）：不画
    CHECK_FALSE(MapRdRemoteToView(RvfPt(-10, 500), frameW, frameH, fit, at));
    CHECK_FALSE(MapRdRemoteToView(RvfPt(frameW, 500), frameW, frameH, fit, at));
    CHECK_FALSE(MapRdRemoteToView(RvfPt(100, frameH + 5), frameW, frameH, fit, at));
    //尺寸未知：不画
    CHECK_FALSE(MapRdRemoteToView(RvfPt(10, 10), 0, 0, fit, at));
}
