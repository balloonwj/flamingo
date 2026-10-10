/**
 *  录制中的摄像头画中画的单元测试（标签 [recordpip]），RecordPipTests.cpp
 *  背景：2026-10-05 用户要求录制中可以打开摄像头，屏幕上出现可拖动的摄像头小窗，录像里的画中画跟着小窗的位置走
 *  （所见即所得；录单个窗口时小窗拖出这个窗口就贴在录像的边上）。
 *  覆盖：
 *    - RPIP1 小窗位置换算：在被录内容里时按比例；超出右边、下边时贴边；比被录内容还大时缩到同宽同高；空矩形不自定义；
 *      默认位置在被录内容右下角、宽为五分之一、16:9；
 *    - RPIP2 合成器按摆放画画中画：自定义摆放时画在对应位置（原来的右下角不再有画中画），不自定义时仍在右下角；
 *    - RPIP3 摄像头小窗：显示后整个窗口都能拖动（命中测试为标题栏）；位置变了回调接收方；画出来源的画面；关闭后不再显示。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-05
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <vector>

#include "../../FlamingoMedia/Record/RecordCameraWindow.h"
#include "../../FlamingoMedia/Record/RecordFrameComposer.h"
#include "../../FlamingoMedia/Record/RecordPolicy.h"

namespace {

//被录内容的屏幕矩形：1000 × 500，左上角在 (100, 200)
const RECT kRpipRecorded = { 100, 200, 1100, 700 };
//比例比较的误差
const double kRpipEps = 1e-6;
//输出画面尺寸（16:9）与画中画样例尺寸
const int kRpipFrameW = 1280;
const int kRpipFrameH = 720;
const int kRpipCamW = 64;
const int kRpipCamH = 36;
//每像素字节数
const int kRpipBpp = 4;
//摄像头小窗的测试位置与挪动后的位置（屏幕坐标）
const RECT kRpipWindowRect = { 60, 60, 300, 195 };
const int kRpipMovedX = 80;
const int kRpipMovedY = 90;

//纯色 BGRA 画面
std::vector<uint8_t> RpipSolid(int w, int h, uint8_t b, uint8_t g, uint8_t r)
{
    std::vector<uint8_t> px(static_cast<size_t>(w) * h * kRpipBpp, 0);
    for (int i = 0; i < w * h; ++i)
    {
        px[i * kRpipBpp] = b;
        px[i * kRpipBpp + 1] = g;
        px[i * kRpipBpp + 2] = r;
        px[i * kRpipBpp + 3] = 255;
    }
    return px;
}

//合成结果在 (x, y) 处是否偏红
bool RpipIsRed(const screenrecord::RecordFrameComposer& c, int x, int y)
{
    const uint8_t* p = c.Pixels() + static_cast<size_t>(y) * c.Stride() + static_cast<size_t>(x) * kRpipBpp;
    return p[2] > 200 && p[1] < 60 && p[0] < 60;
}

//假摄像头：给一张纯绿的画面
class RpipFakeCamera : public screenrecord::IRecordVideoSource
{
public:
    virtual bool GetRecordFrame(std::vector<uint8_t>& bgra, int& width, int& height, uint64_t& serial) override
    {
        bgra = RpipSolid(kRpipCamW, kRpipCamH, 0, 200, 0);
        width = kRpipCamW;
        height = kRpipCamH;
        serial = 1;
        return true;
    }
};

//记下位置变化的接收方
class RpipSink : public screenrecord::IRecordCameraWindowSink
{
public:
    RpipSink()
        : m_calls(0)
    {
        memset(&m_last, 0, sizeof(m_last));
    }
    virtual void OnCameraWindowMoved(const RECT& screenRect) override
    {
        ++m_calls;
        m_last = screenRect;
    }
    int  m_calls;   //回调次数
    RECT m_last;    //最近一次的矩形
};

void RpipPump()
{
    MSG msg;
    for (int i = 0; i < 4; ++i)
    {
        while (::PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
        {
            ::TranslateMessage(&msg);
            ::DispatchMessageW(&msg);
        }
        ::Sleep(static_cast<DWORD>(screenrecord::kRecordCameraFrameMs));
    }
}

} // namespace

TEST_CASE("RPIP1 camera window position maps into the recording", "[recordpip]")
{
    //在被录内容里：左上 (300, 300)、200 × 100 → 比例 (0.2, 0.2, 0.2, 0.2)
    const RECT inside = { 300, 300, 500, 400 };
    screenrecord::RecordPipPlacement p = screenrecord::MapPipWindowToContent(inside, kRpipRecorded);
    CHECK(p.m_custom);
    CHECK(p.m_x == Catch::Approx(0.2).margin(kRpipEps));
    CHECK(p.m_y == Catch::Approx(0.2).margin(kRpipEps));
    CHECK(p.m_w == Catch::Approx(0.2).margin(kRpipEps));
    CHECK(p.m_h == Catch::Approx(0.2).margin(kRpipEps));
    //拖出右下角：贴到右边与下边
    const RECT outside = { 1200, 800, 1400, 900 };
    p = screenrecord::MapPipWindowToContent(outside, kRpipRecorded);
    CHECK(p.m_custom);
    CHECK(p.m_x + p.m_w == Catch::Approx(1.0).margin(kRpipEps));
    CHECK(p.m_y + p.m_h == Catch::Approx(1.0).margin(kRpipEps));
    CHECK(p.m_w == Catch::Approx(0.2).margin(kRpipEps));
    //拖到左上外面：贴到左边与上边
    const RECT leftTop = { 0, 0, 200, 100 };
    p = screenrecord::MapPipWindowToContent(leftTop, kRpipRecorded);
    CHECK(p.m_x == Catch::Approx(0.0).margin(kRpipEps));
    CHECK(p.m_y == Catch::Approx(0.0).margin(kRpipEps));
    //比被录内容还大：缩到同宽同高
    const RECT huge = { 0, 0, 3000, 2000 };
    p = screenrecord::MapPipWindowToContent(huge, kRpipRecorded);
    CHECK(p.m_w == Catch::Approx(1.0).margin(kRpipEps));
    CHECK(p.m_h == Catch::Approx(1.0).margin(kRpipEps));
    CHECK(p.m_x == Catch::Approx(0.0).margin(kRpipEps));
    //空矩形：不自定义
    const RECT empty = { 0, 0, 0, 0 };
    CHECK_FALSE(screenrecord::MapPipWindowToContent(empty, kRpipRecorded).m_custom);
    CHECK_FALSE(screenrecord::MapPipWindowToContent(inside, empty).m_custom);
    //默认位置：右下角、宽为 1/5、16:9，完整落在被录内容里
    const RECT def = screenrecord::DefaultPipWindowRect(kRpipRecorded);
    CHECK(def.right - def.left == (kRpipRecorded.right - kRpipRecorded.left) / screenrecord::kRecordPipWidthDivisor);
    CHECK((def.bottom - def.top) * 16 == Catch::Approx((def.right - def.left) * 9).margin(16));
    CHECK(def.right <= kRpipRecorded.right);
    CHECK(def.bottom <= kRpipRecorded.bottom);
    CHECK(def.left > (kRpipRecorded.left + kRpipRecorded.right) / 2);
    CHECK(def.top > (kRpipRecorded.top + kRpipRecorded.bottom) / 2);
}

TEST_CASE("RPIP2 composer draws the picture-in-picture where it is placed", "[recordpip]")
{
    screenrecord::RecordFrameComposer composer;
    REQUIRE(composer.Init(kRpipFrameW, kRpipFrameH));
    const std::vector<uint8_t> src = RpipSolid(kRpipFrameW, kRpipFrameH, 0, 0, 0);
    const std::vector<uint8_t> cam = RpipSolid(kRpipCamW, kRpipCamH, 0, 0, 255);
    //默认：右下角
    composer.Compose(&src[0], kRpipFrameW, kRpipFrameH, kRpipFrameW * kRpipBpp, NULL, &cam[0], kRpipCamW, kRpipCamH);
    const RECT corner = screenrecord::ComputePipRect(kRpipFrameW, kRpipFrameH);
    const int cornerX = (corner.left + corner.right) / 2;
    const int cornerY = (corner.top + corner.bottom) / 2;
    CHECK(RpipIsRed(composer, cornerX, cornerY));
    //自定义：左上角 (0.1, 0.1)、大小 0.25 × 0.25
    screenrecord::RecordPipPlacement p;
    p.m_custom = true;
    p.m_x = 0.1;
    p.m_y = 0.1;
    p.m_w = 0.25;
    p.m_h = 0.25;
    composer.SetPipPlacement(p);
    composer.Compose(&src[0], kRpipFrameW, kRpipFrameH, kRpipFrameW * kRpipBpp, NULL, &cam[0], kRpipCamW, kRpipCamH);
    const RECT placed = screenrecord::PlacePipRect(composer.ContentRect(), p, kRpipFrameW, kRpipFrameH);
    CHECK(placed.left == static_cast<LONG>(kRpipFrameW * 0.1));
    CHECK(placed.top == static_cast<LONG>(kRpipFrameH * 0.1));
    CHECK(RpipIsRed(composer, (placed.left + placed.right) / 2, (placed.top + placed.bottom) / 2));
    CHECK_FALSE(RpipIsRed(composer, cornerX, cornerY));
}

TEST_CASE("RPIP3 camera window can be dragged and reports its moves", "[recordpip]")
{
    RpipFakeCamera camera;
    RpipSink sink;
    screenrecord::CRecordCameraWindow wnd;
    REQUIRE(wnd.Show(kRpipWindowRect, &camera, &sink));
    CHECK(wnd.IsShown());
    //整个窗口都能拖动
    const RECT rc = wnd.ScreenRect();
    const LPARAM center = MAKELPARAM((rc.left + rc.right) / 2, (rc.top + rc.bottom) / 2);
    CHECK(wnd.SendMessage(WM_NCHITTEST, 0, center) == HTCAPTION);
    //挪动：回调接收方
    const int before = sink.m_calls;
    wnd.SetWindowPos(NULL, kRpipMovedX, kRpipMovedY, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    RpipPump();
    CHECK(sink.m_calls > before);
    CHECK(sink.m_last.left == kRpipMovedX);
    CHECK(sink.m_last.top == kRpipMovedY);
    //关闭
    wnd.Hide();
    CHECK_FALSE(wnd.IsShown());
}
