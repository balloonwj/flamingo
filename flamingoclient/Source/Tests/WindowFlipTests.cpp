/**
 *  登录窗与注册窗之间「翻转」过渡的测试（标签 [authflip]，2026-10-07）：
 *    - WF1 每帧几何的两端：进度 0 恰为起始窗口矩形（不压扁、不压暗、画起始画面），进度 1 恰为目标窗口矩形
 *    - WF2 中途：转到 90 度时宽度为 0、压暗最深；高度在两窗之间单调过渡；梯形远端的竖边较短，正向前半程在右、
 *          后半程在左，反向与之镜像；所有顶点都在过渡画布之内
 *    - WF3 缓动曲线与进度：两端与中点、单调、对称、越界截断；按时间算进度
 *    - WF4 目标窗口摆放：上边缘对齐、水平居中；超出工作区时挪回工作区之内
 *    - WF5 软件绘制：进度 0 时画布与起始画面逐像素相同、其余透明；中途梯形之外透明、之内压暗且不透明；
 *          转到侧面时整块透明；画面为空时整块透明
 *    - WF6 截图：隐藏着的 DuiFrameWindow 也能截到当前画面，改了内容后再截得到新画面（守住 WindowFlipTransition.h
 *          所说的「对窗口直接发 WM_PAINT、从后台缓冲取画面」这一做法）
 *    - WF7 直接切换（不播放翻转）：目标窗口先显示、起始窗口后隐藏，任一步之后至少一个窗口可见
 *    - WF8 翻转切换：步骤依次为盖上过渡窗口、隐藏起始窗口、显示目标窗口、销毁过渡窗口，任一步之后至少一个窗口
 *          （含过渡窗口）可见；逐帧进度单调、最后一帧进度为 1；结束后过渡窗口已销毁
 *    - WFS 效果图（标签 [.][snapshot][authflip]，默认不运行）：用真实的登录窗、注册窗布局播放一次正向与反向翻转，
 *          设了环境变量 FLAMINGO_SNAPSHOT_PATH（一个目录）时把进度约 0.25 与 0.75 的两帧存成 PNG，供人工核对。
 *  建窗口的用例把窗口放在屏幕之外，按仓库约定在不可见桌面里运行。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-07
 */
#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <objidl.h>    //gdiplus.h 依赖 IStream 等 COM 声明
#include <gdiplus.h>
#include <math.h>
#include <memory>
#include <string>
#include <vector>

#include "../UI/BalloonAuth/WindowFlipFrame.h"
#include "../UI/BalloonAuth/WindowFlipTransition.h"
#include "../UI/BalloonAuth/BalloonLoginLayout.h"
#include "../UI/BalloonRegister/RegisterLayout.h"
#include "../UI/BalloonRegister/RegisterModel.h"
#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiFrameWindow.h"
#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"

#pragma comment(lib, "gdiplus.lib")

using namespace balloonauth;

namespace {

const float kWfEps = 0.01f;                 //浮点比较容差（像素）
const int kWfOffscreenX = -6000;            //建窗口的用例把窗口放在屏幕之外的横坐标
const int kWfOffscreenY = 200;              //纵坐标
const int kWfLoginW = 380;                  //与登录窗相同的宽
const int kWfLoginH = 565;                  //与登录窗相同的高
const int kWfRegisterH = 642;               //与注册窗相同的高
const int kWfTitleBarH = 28;                //测试窗口的标题栏高
const COLORREF kWfColorA = RGB(10, 200, 30);    //测试窗口 A 的底色
const COLORREF kWfColorB = RGB(220, 40, 90);    //测试窗口 B 的底色
const COLORREF kWfColorC = RGB(30, 60, 250);    //WF6 改色之后的底色
const uint32_t kWfAlphaMask = 0xFF000000u;  //预乘 BGRA 的 alpha 字节
const int kWfSamples = 100;                 //曲线取样点数
const DWORD kWfPumpMs = 100;                //建窗口后处理消息的时长（毫秒）
const wchar_t* const kWfFlipClass = L"FlamingoWindowFlip";   //过渡窗口的窗口类名（与 WindowFlipTransition.cpp 一致）
const wchar_t* const kWfShotEnv = L"FLAMINGO_SNAPSHOT_PATH";  //WFS 效果图的输出目录

RECT MakeRc(int left, int top, int width, int height)
{
    RECT rc;
    rc.left = left;
    rc.top = top;
    rc.right = left + width;
    rc.bottom = top + height;
    return rc;
}

//COLORREF 转成不透明的预乘 BGRA 像素
uint32_t ToPixel(COLORREF c)
{
    return kWfAlphaMask | (static_cast<uint32_t>(GetRValue(c)) << 16) | (static_cast<uint32_t>(GetGValue(c)) << 8) |
           static_cast<uint32_t>(GetBValue(c));
}

void PumpMessages(DWORD ms)
{
    const DWORD start = ::GetTickCount();
    while (::GetTickCount() - start < ms)
    {
        MSG msg;
        while (::PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
        {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
        }
        ::Sleep(10);
    }
}

//一个用作测试的 DuiFrameWindow：标题栏下面整块是一种颜色
class CWfWindow : public balloonwjui::DuiFrameWindow
{
public:
    CWfWindow()
        : m_body(NULL)
    {
    }

    //建窗口（隐藏），尺寸 width × height，左上角在 (x, y)
    bool Build(int x, int y, int width, int height, COLORREF color)
    {
        if (Create(NULL, CWindow::rcDefault, L"wf", WS_OVERLAPPEDWINDOW, 0) == NULL)
            return false;
        SetTitleBarHeight(kWfTitleBarH);
        SetButtons(true, false, true);
        std::unique_ptr<balloonwjui::DuiVBox> body(new balloonwjui::DuiVBox());
        body->SetBgColor(color);
        m_body = body.get();
        SetClientContent(std::move(body));
        ResizeClient(width, height);
        SetWindowPos(NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        return true;
    }

    //换底色
    void SetColor(COLORREF color)
    {
        if (m_body != NULL)
        {
            m_body->SetBgColor(color);
            m_body->Invalidate();
        }
    }

private:
    balloonwjui::DuiVBox* m_body;   //标题栏下面的整块内容（借用，控件树持有）
};

//记录切换的每一步与每一帧，并在每一步之后检查「至少一个窗口可见」
class CWfObserver : public IFlipObserver
{
public:
    CWfObserver(HWND a, HWND b)
        : m_a(a)
        , m_b(b)
        , m_allVisibleOk(true)
        , m_framesMonotonic(true)
        , m_lastT(-1.0f)
        , m_frameCount(0)
    {
    }

    void OnFlipStep(FlipStep step) override
    {
        m_steps.push_back(step);
        HWND flip = ::FindWindowW(kWfFlipClass, NULL);
        const bool flipVisible = (flip != NULL && ::IsWindowVisible(flip));
        m_flipVisibleAtStep.push_back(flipVisible);
        m_aVisibleAtStep.push_back(::IsWindowVisible(m_a) != FALSE);
        m_bVisibleAtStep.push_back(::IsWindowVisible(m_b) != FALSE);
        if (!flipVisible && !::IsWindowVisible(m_a) && !::IsWindowVisible(m_b))
            m_allVisibleOk = false;
    }

    void OnFlipFrame(float t, const FlipBitmap& canvas) override
    {
        if (t < m_lastT)
            m_framesMonotonic = false;
        m_lastT = t;
        ++m_frameCount;
        (void)canvas;
    }

    HWND m_a;                               //起始窗口
    HWND m_b;                               //目标窗口
    std::vector<FlipStep> m_steps;          //依次收到的步骤
    std::vector<bool> m_flipVisibleAtStep;  //每一步之后过渡窗口是否可见
    std::vector<bool> m_aVisibleAtStep;     //每一步之后起始窗口是否可见
    std::vector<bool> m_bVisibleAtStep;     //每一步之后目标窗口是否可见
    bool m_allVisibleOk;                    //每一步之后是否都至少有一个窗口可见
    bool m_framesMonotonic;                 //逐帧进度是否单调不减
    float m_lastT;                          //最后一帧的进度
    int m_frameCount;                       //收到的帧数
};

//一张 w × h、每个像素各不相同的不透明画面
FlipBitmap MakeTestImage(int w, int h)
{
    FlipBitmap img;
    img.m_width = w;
    img.m_height = h;
    img.m_pixels.resize(static_cast<size_t>(w) * h);
    for (int y = 0; y < h; ++y)
    {
        for (int x = 0; x < w; ++x)
            img.m_pixels[static_cast<size_t>(y) * w + x] = kWfAlphaMask | (static_cast<uint32_t>(y) << 8) | static_cast<uint32_t>(x);
    }
    return img;
}

} // namespace

TEST_CASE("WF1 进度 0 恰为起始窗口矩形，进度 1 恰为目标窗口矩形", "[authflip]")
{
    const RECT from = MakeRc(100, 100, kWfLoginW, kWfLoginH);
    const RECT to = MakeRc(100, 100, kWfLoginW, kWfRegisterH);
    const RECT canvas = FlipCanvasRect(from, to);
    CHECK(canvas.left == 100);
    CHECK(canvas.top == 100);
    CHECK(canvas.right == 100 + kWfLoginW);
    CHECK(canvas.bottom == 100 + kWfRegisterH);

    const FlipFrame f0 = ComputeFlipFrame(from, to, 0.0f, kFlipForward);
    CHECK_FALSE(f0.m_showTarget);
    CHECK(fabsf(f0.m_left - 0.0f) < kWfEps);
    CHECK(fabsf(f0.m_right - kWfLoginW) < kWfEps);
    CHECK(fabsf(f0.m_leftTop) < kWfEps);
    CHECK(fabsf(f0.m_rightTop) < kWfEps);
    CHECK(fabsf(f0.m_leftBottom - kWfLoginH) < kWfEps);
    CHECK(fabsf(f0.m_rightBottom - kWfLoginH) < kWfEps);
    CHECK(fabsf(f0.m_shade) < kWfEps);

    const FlipFrame f1 = ComputeFlipFrame(from, to, 1.0f, kFlipForward);
    CHECK(f1.m_showTarget);
    CHECK(fabsf(f1.m_left - 0.0f) < kWfEps);
    CHECK(fabsf(f1.m_right - kWfLoginW) < kWfEps);
    CHECK(fabsf(f1.m_leftBottom - kWfRegisterH) < kWfEps);
    CHECK(fabsf(f1.m_rightBottom - kWfRegisterH) < kWfEps);
    CHECK(fabsf(f1.m_leftTop) < kWfEps);
    CHECK(fabsf(f1.m_shade) < kWfEps);

    //反向（注册窗 → 登录窗）两端同理
    const FlipFrame b1 = ComputeFlipFrame(to, from, 1.0f, kFlipBackward);
    CHECK(b1.m_showTarget);
    CHECK(fabsf(b1.m_leftBottom - kWfLoginH) < kWfEps);
}

TEST_CASE("WF2 转到 90 度时宽度为 0；高度单调过渡；梯形远端的竖边较短且方向互为镜像", "[authflip]")
{
    const RECT from = MakeRc(100, 100, kWfLoginW, kWfLoginH);
    const RECT to = MakeRc(100, 100, kWfLoginW, kWfRegisterH);
    const RECT canvas = FlipCanvasRect(from, to);
    const float cw = static_cast<float>(canvas.right - canvas.left);
    const float ch = static_cast<float>(canvas.bottom - canvas.top);

    const FlipFrame mid = ComputeFlipFrame(from, to, 0.5f, kFlipForward);
    CHECK(mid.m_right - mid.m_left < 1.0f);
    CHECK(fabsf(mid.m_shade - kFlipMaxShade) < kWfEps);
    CHECK(mid.m_showTarget);

    //近端竖边的高度（较长的那条）在 565 与 642 之间单调不减
    float lastH = 0.0f;
    for (int i = 0; i <= kWfSamples; ++i)
    {
        const float t = static_cast<float>(i) / kWfSamples;
        INFO("t = " << t);
        const FlipFrame f = ComputeFlipFrame(from, to, t, kFlipForward);
        const float lh = f.m_leftBottom - f.m_leftTop;
        const float rh = f.m_rightBottom - f.m_rightTop;
        const float h = (lh > rh) ? lh : rh;
        CHECK(h >= lastH - kWfEps);
        CHECK(h >= kWfLoginH - kWfEps);
        CHECK(h <= kWfRegisterH + kWfEps);
        lastH = h;
        //所有顶点都在画布之内
        CHECK(f.m_left >= -kWfEps);
        CHECK(f.m_right <= cw + kWfEps);
        CHECK(f.m_leftTop >= -kWfEps);
        CHECK(f.m_rightTop >= -kWfEps);
        CHECK(f.m_leftBottom <= ch + kWfEps);
        CHECK(f.m_rightBottom <= ch + kWfEps);
        CHECK(f.m_left <= f.m_right + kWfEps);
        //上边缘对齐：近端竖边的上端始终在第 0 行
        CHECK(fabsf((lh > rh ? f.m_leftTop : f.m_rightTop)) < kWfEps);
    }

    //正向：前半程右竖边短，后半程左竖边短
    const FlipFrame fq = ComputeFlipFrame(from, to, 0.25f, kFlipForward);
    CHECK(fq.m_rightBottom - fq.m_rightTop < fq.m_leftBottom - fq.m_leftTop - 1.0f);
    CHECK_FALSE(fq.m_showTarget);
    const FlipFrame f3 = ComputeFlipFrame(from, to, 0.75f, kFlipForward);
    CHECK(f3.m_leftBottom - f3.m_leftTop < f3.m_rightBottom - f3.m_rightTop - 1.0f);
    CHECK(f3.m_showTarget);
    //反向与之镜像
    const FlipFrame bq = ComputeFlipFrame(to, from, 0.25f, kFlipBackward);
    CHECK(bq.m_leftBottom - bq.m_leftTop < bq.m_rightBottom - bq.m_rightTop - 1.0f);
    const FlipFrame b3 = ComputeFlipFrame(to, from, 0.75f, kFlipBackward);
    CHECK(b3.m_rightBottom - b3.m_rightTop < b3.m_leftBottom - b3.m_leftTop - 1.0f);
    //压扁以画面的竖直中线为轴：左右两条竖边关于中线对称
    CHECK(fabsf((fq.m_left + fq.m_right) / 2.0f - kWfLoginW / 2.0f) < kWfEps);
}

TEST_CASE("WF3 缓动曲线与按时间算进度", "[authflip]")
{
    CHECK(fabsf(FlipEase(0.0f)) < kWfEps);
    CHECK(fabsf(FlipEase(1.0f) - 1.0f) < kWfEps);
    CHECK(fabsf(FlipEase(0.5f) - 0.5f) < kWfEps);
    CHECK(fabsf(FlipEase(-1.0f)) < kWfEps);
    CHECK(fabsf(FlipEase(2.0f) - 1.0f) < kWfEps);
    float last = -1.0f;
    for (int i = 0; i <= kWfSamples; ++i)
    {
        const float t = static_cast<float>(i) / kWfSamples;
        const float e = FlipEase(t);
        CHECK(e >= last);
        CHECK(fabsf(FlipEase(1.0f - t) - (1.0f - e)) < kWfEps);
        last = e;
    }
    CHECK(kFlipDurationMs == 350u);
    CHECK(fabsf(FlipProgress(0, kFlipDurationMs)) < kWfEps);
    CHECK(fabsf(FlipProgress(175, kFlipDurationMs) - 0.5f) < kWfEps);
    CHECK(fabsf(FlipProgress(350, kFlipDurationMs) - 1.0f) < kWfEps);
    CHECK(fabsf(FlipProgress(10000, kFlipDurationMs) - 1.0f) < kWfEps);
    CHECK(fabsf(FlipProgress(5, 0) - 1.0f) < kWfEps);
}

TEST_CASE("WF4 目标窗口上边缘对齐、水平居中，超出工作区时挪回来", "[authflip]")
{
    const RECT work = MakeRc(0, 0, 1920, 1040);
    //常规：与登录窗同宽，上边缘对齐、水平居中
    const RECT login = MakeRc(770, 200, kWfLoginW, kWfLoginH);
    const RECT r = AlignTopCenter(login, kWfLoginW, kWfRegisterH, work);
    CHECK(r.left == 770);
    CHECK(r.top == 200);
    CHECK(r.right - r.left == kWfLoginW);
    CHECK(r.bottom - r.top == kWfRegisterH);
    //宽度不同时水平中心相同
    const RECT wide = AlignTopCenter(login, 500, kWfRegisterH, work);
    CHECK((wide.left + wide.right) / 2 == (login.left + login.right) / 2);
    //登录窗靠近工作区底边：注册窗更高，往上挪到底边贴着工作区
    const RECT low = MakeRc(770, 600, kWfLoginW, kWfLoginH);
    const RECT r2 = AlignTopCenter(low, kWfLoginW, kWfRegisterH, work);
    CHECK(r2.bottom == work.bottom);
    CHECK(r2.left == 770);
    //贴着右边与左边
    const RECT right = AlignTopCenter(MakeRc(1700, 100, kWfLoginW, kWfLoginH), 500, kWfRegisterH, work);
    CHECK(right.right == work.right);
    const RECT left = AlignTopCenter(MakeRc(-200, 100, kWfLoginW, kWfLoginH), 500, kWfRegisterH, work);
    CHECK(left.left == work.left);
    //比工作区还大：以左上角为准
    const RECT huge = AlignTopCenter(login, 3000, 2000, work);
    CHECK(huge.left == work.left);
    CHECK(huge.top == work.top);
}

TEST_CASE("WF5 软件绘制：进度 0 逐像素相同，中途梯形之外透明、之内压暗，转到侧面与空画面整块透明", "[authflip]")
{
    const int w = 40;
    const int h = 30;
    const int h2 = 50;
    const RECT from = MakeRc(0, 0, w, h);
    const RECT to = MakeRc(0, 0, w, h2);
    const FlipBitmap img = MakeTestImage(w, h);
    FlipBitmap canvas;
    canvas.m_width = w;
    canvas.m_height = h2;

    //进度 0：与起始画面逐像素相同，起始窗口以下的部分透明
    RenderFlipFrame(img, ComputeFlipFrame(from, to, 0.0f, kFlipForward), canvas);
    REQUIRE(canvas.m_pixels.size() == static_cast<size_t>(w) * h2);
    int mismatch = 0;
    for (int y = 0; y < h2; ++y)
    {
        for (int x = 0; x < w; ++x)
        {
            const uint32_t expect = (y < h) ? img.m_pixels[static_cast<size_t>(y) * w + x] : 0u;
            if (canvas.m_pixels[static_cast<size_t>(y) * w + x] != expect)
                ++mismatch;
        }
    }
    CHECK(mismatch == 0);

    //进度 0.25：梯形左右之外透明；中间一列中间一行不透明且被压暗
    const FlipFrame q = ComputeFlipFrame(from, to, 0.25f, kFlipForward);
    RenderFlipFrame(img, q, canvas);
    const int leftOut = static_cast<int>(floorf(q.m_left)) - 1;
    const int rightOut = static_cast<int>(ceilf(q.m_right));
    REQUIRE(leftOut >= 0);
    REQUIRE(rightOut < w);
    for (int y = 0; y < h2; ++y)
    {
        CHECK(canvas.m_pixels[static_cast<size_t>(y) * w + leftOut] == 0u);
        CHECK(canvas.m_pixels[static_cast<size_t>(y) * w + rightOut] == 0u);
    }
    const int cx = w / 2;
    const int cy = static_cast<int>((q.m_leftTop + q.m_leftBottom) / 2.0f);
    const uint32_t p = canvas.m_pixels[static_cast<size_t>(cy) * w + cx];
    CHECK((p & kWfAlphaMask) == kWfAlphaMask);
    //梯形里取到的是某个源像素按 (1 - 压暗) 缩放后的值：绿色分量（源像素的行号）不超过源画面的最大行号
    CHECK(((p >> 8) & 0xFFu) <= static_cast<uint32_t>(h - 1));
    //远端（右侧）竖边比近端短：最上一行里，右边最靠外那一列只被梯形盖住一小部分（大半透明），
    //左边最靠外那一列几乎完全被盖住
    const int topRow = static_cast<int>(floorf(q.m_leftTop));
    const int rightCol = static_cast<int>(floorf(q.m_right)) - 1;
    const int leftCol = static_cast<int>(ceilf(q.m_left));
    const uint32_t alphaRight = canvas.m_pixels[static_cast<size_t>(topRow) * w + rightCol] >> 24;
    const uint32_t alphaLeft = canvas.m_pixels[static_cast<size_t>(topRow) * w + leftCol] >> 24;
    CHECK(alphaRight < 128u);
    CHECK(alphaLeft > 200u);

    //转到侧面（进度 0.5）：整块透明
    RenderFlipFrame(img, ComputeFlipFrame(from, to, 0.5f, kFlipForward), canvas);
    bool allClear = true;
    for (size_t i = 0; i < canvas.m_pixels.size(); ++i)
    {
        if (canvas.m_pixels[i] != 0u)
            allClear = false;
    }
    CHECK(allClear);

    //空画面：整块透明，不越界
    FlipBitmap empty;
    RenderFlipFrame(empty, ComputeFlipFrame(from, to, 0.0f, kFlipForward), canvas);
    CHECK(canvas.m_pixels.size() == static_cast<size_t>(w) * h2);
    bool emptyClear = true;
    for (size_t i = 0; i < canvas.m_pixels.size(); ++i)
    {
        if (canvas.m_pixels[i] != 0u)
            emptyClear = false;
    }
    CHECK(emptyClear);
}

TEST_CASE("WF6 隐藏着的 DuiFrameWindow 也能截到当前画面，改了内容后再截得到新画面", "[authflip]")
{
    CWfWindow wnd;
    REQUIRE(wnd.Build(kWfOffscreenX, kWfOffscreenY, kWfLoginW, kWfLoginH, kWfColorA));
    REQUIRE_FALSE(wnd.IsWindowVisible());
    FlipBitmap img;
    REQUIRE(CaptureDuiHostImage(wnd, img));
    CHECK(img.m_width == kWfLoginW);
    CHECK(img.m_height == kWfLoginH);
    const size_t center = static_cast<size_t>(kWfLoginH / 2) * kWfLoginW + kWfLoginW / 2;
    CHECK(img.m_pixels[center] == ToPixel(kWfColorA));
    //标题栏（不透明标题栏的底色）与内容不同色
    CHECK(img.m_pixels[static_cast<size_t>(kWfTitleBarH / 2) * kWfLoginW + kWfLoginW / 2] != ToPixel(kWfColorA));

    //隐藏期间改了内容：再截得到新画面
    wnd.SetColor(kWfColorC);
    FlipBitmap img2;
    REQUIRE(CaptureDuiHostImage(wnd, img2));
    CHECK(img2.m_pixels[center] == ToPixel(kWfColorC));

    //显示出来之后照样能截
    wnd.ShowWindow(SW_SHOWNOACTIVATE);
    PumpMessages(kWfPumpMs);
    FlipBitmap img3;
    REQUIRE(CaptureDuiHostImage(wnd, img3));
    CHECK(img3.m_pixels[center] == ToPixel(kWfColorC));
    wnd.DestroyWindow();

    //窗口已销毁：截图失败
    FlipBitmap img4;
    CHECK_FALSE(CaptureDuiHostImage(wnd, img4));
}

TEST_CASE("WF7 直接切换：目标窗口先显示、起始窗口后隐藏，任一步之后至少一个窗口可见", "[authflip]")
{
    CWfWindow a;
    CWfWindow b;
    REQUIRE(a.Build(kWfOffscreenX, kWfOffscreenY, kWfLoginW, kWfLoginH, kWfColorA));
    REQUIRE(b.Build(kWfOffscreenX, kWfOffscreenY, kWfLoginW, kWfRegisterH, kWfColorB));
    a.ShowWindow(SW_SHOW);
    PumpMessages(kWfPumpMs);
    CWfObserver obs(a.m_hWnd, b.m_hWnd);
    CHECK_FALSE(SwitchDuiWindows(a, b, kFlipForward, false, &obs));
    REQUIRE(obs.m_steps.size() == 3u);
    CHECK(obs.m_steps[0] == kFlipStepTargetShown);
    CHECK(obs.m_steps[1] == kFlipStepSourceHidden);
    CHECK(obs.m_steps[2] == kFlipStepFinished);
    CHECK(obs.m_allVisibleOk);
    CHECK(obs.m_frameCount == 0);
    CHECK_FALSE(a.IsWindowVisible());
    CHECK(b.IsWindowVisible());

    //起始窗口本来就不可见时同样直接切换（即使允许播放翻转）
    CWfObserver back(b.m_hWnd, a.m_hWnd);
    b.ShowWindow(SW_HIDE);
    CHECK_FALSE(SwitchDuiWindows(b, a, kFlipBackward, true, &back));
    CHECK(a.IsWindowVisible());
    CHECK(back.m_frameCount == 0);
    a.DestroyWindow();
    b.DestroyWindow();
}

TEST_CASE("WF8 翻转切换：四个步骤依次发生，任一步之后至少一个窗口可见，最后一帧进度为 1", "[authflip]")
{
    CWfWindow a;
    CWfWindow b;
    REQUIRE(a.Build(kWfOffscreenX, kWfOffscreenY, kWfLoginW, kWfLoginH, kWfColorA));
    REQUIRE(b.Build(kWfOffscreenX, kWfOffscreenY, kWfLoginW, kWfRegisterH, kWfColorB));
    a.ShowWindow(SW_SHOW);
    PumpMessages(kWfPumpMs);
    REQUIRE(a.IsWindowVisible());

    CWfObserver obs(a.m_hWnd, b.m_hWnd);
    const bool played = SwitchDuiWindows(a, b, kFlipForward, true, &obs);
    CHECK(played);
    REQUIRE(obs.m_steps.size() == 4u);
    CHECK(obs.m_steps[0] == kFlipStepCovered);
    CHECK(obs.m_steps[1] == kFlipStepSourceHidden);
    CHECK(obs.m_steps[2] == kFlipStepTargetShown);
    CHECK(obs.m_steps[3] == kFlipStepFinished);
    //盖上过渡窗口时起始窗口还在；隐藏起始窗口后由过渡窗口撑着；显示目标窗口时过渡窗口还在；最后只剩目标窗口
    CHECK(obs.m_flipVisibleAtStep[0]);
    CHECK(obs.m_aVisibleAtStep[0]);
    CHECK(obs.m_flipVisibleAtStep[1]);
    CHECK_FALSE(obs.m_aVisibleAtStep[1]);
    CHECK(obs.m_bVisibleAtStep[2]);
    CHECK_FALSE(obs.m_flipVisibleAtStep[3]);
    CHECK(obs.m_allVisibleOk);
    CHECK(obs.m_framesMonotonic);
    CHECK(obs.m_frameCount >= 2);
    CHECK(fabsf(obs.m_lastT - 1.0f) < kWfEps);
    CHECK(::FindWindowW(kWfFlipClass, NULL) == NULL);
    CHECK_FALSE(a.IsWindowVisible());
    CHECK(b.IsWindowVisible());

    //反向翻回来
    CWfObserver back(b.m_hWnd, a.m_hWnd);
    CHECK(SwitchDuiWindows(b, a, kFlipBackward, true, &back));
    CHECK(back.m_allVisibleOk);
    CHECK(a.IsWindowVisible());
    CHECK_FALSE(b.IsWindowVisible());
    a.DestroyWindow();
    b.DestroyWindow();
}

namespace {

//WF9：按发生的先后记录观察者的步骤与开关 DWM 窗口动画的调用
std::vector<std::string> g_wf9Events;
//WF9：当前这次切换的目标窗口，用来区分开关的是不是它
HWND g_wf9Target = NULL;

//WF9 替换进去的开关函数：只记录，不真的调用 DWM
void Wf9Switch(HWND hwnd, bool disabled)
{
    g_wf9Events.push_back(std::string(disabled ? "off" : "on") + (hwnd == g_wf9Target ? ":target" : ":other"));
}

//WF9 的观察者：把步骤写进同一个记录
class CWf9Observer : public IFlipObserver
{
public:
    void OnFlipStep(FlipStep step) override
    {
        switch (step)
        {
        //过渡窗口已盖上
        case kFlipStepCovered:
            g_wf9Events.push_back("covered");
            break;
        //起始窗口已隐藏
        case kFlipStepSourceHidden:
            g_wf9Events.push_back("source_hidden");
            break;
        //目标窗口已显示并激活
        case kFlipStepTargetShown:
            g_wf9Events.push_back("target_shown");
            break;
        //切换完成
        case kFlipStepFinished:
            g_wf9Events.push_back("finished");
            break;
        //不会出现的取值
        default:
            g_wf9Events.push_back("unknown");
            break;
        }
    }

    void OnFlipFrame(float /*t*/, const FlipBitmap& /*canvas*/) override
    {
    }
};

} // namespace

TEST_CASE("WF9 翻转切换：盖上过渡窗口之前关掉目标窗口的 DWM 窗口动画，目标窗口显示后立即恢复；直接切换不改动画设置",
          "[authflip]")
{
    //目标窗口第一次显示时系统播放的「打开」动画会让翻转结束时闪一下（2026-10-07 可见桌面逐帧截屏实证，见 WFC）
    CWfWindow a;
    CWfWindow b;
    REQUIRE(a.Build(kWfOffscreenX, kWfOffscreenY, kWfLoginW, kWfLoginH, kWfColorA));
    REQUIRE(b.Build(kWfOffscreenX, kWfOffscreenY, kWfLoginW, kWfRegisterH, kWfColorB));
    a.ShowWindow(SW_SHOW);
    PumpMessages(kWfPumpMs);

    const FlipTransitionSwitchFn original = SetFlipTransitionSwitchForTest(&Wf9Switch);
    CHECK(original != NULL);
    CWf9Observer obs;
    g_wf9Events.clear();
    g_wf9Target = b.m_hWnd;
    CHECK(SwitchDuiWindows(a, b, kFlipForward, true, &obs));
    std::vector<std::string> expected;
    expected.push_back("off:target");
    expected.push_back("covered");
    expected.push_back("source_hidden");
    expected.push_back("on:target");
    expected.push_back("target_shown");
    expected.push_back("finished");
    CHECK(g_wf9Events == expected);

    //直接切换：没有过渡窗口，系统动画照常播放，不去改它
    g_wf9Events.clear();
    g_wf9Target = a.m_hWnd;
    CHECK_FALSE(SwitchDuiWindows(b, a, kFlipBackward, false, &obs));
    std::vector<std::string> direct;
    direct.push_back("target_shown");
    direct.push_back("source_hidden");
    direct.push_back("finished");
    CHECK(g_wf9Events == direct);

    //传 NULL 恢复默认实现，返回的是刚才替换进去的函数
    CHECK(SetFlipTransitionSwitchForTest(NULL) == &Wf9Switch);
    CHECK(SetFlipTransitionSwitchForTest(NULL) == original);
    g_wf9Target = NULL;
    a.DestroyWindow();
    b.DestroyWindow();
}

// =====================================================================
// WFS —— 效果图（默认不运行）
// =====================================================================

namespace {

//在 GDI+ 已注册的编码器里找 PNG 编码器
bool WfPngEncoder(CLSID& out)
{
    UINT count = 0;
    UINT bytes = 0;
    if (Gdiplus::GetImageEncodersSize(&count, &bytes) != Gdiplus::Ok || bytes == 0)
        return false;
    std::vector<BYTE> buf(bytes);
    Gdiplus::ImageCodecInfo* codecs = reinterpret_cast<Gdiplus::ImageCodecInfo*>(&buf[0]);
    if (Gdiplus::GetImageEncoders(count, bytes, codecs) != Gdiplus::Ok)
        return false;
    for (UINT i = 0; i < count; ++i)
    {
        if (wcscmp(codecs[i].MimeType, L"image/png") == 0)
        {
            out = codecs[i].Clsid;
            return true;
        }
    }
    return false;
}

//把一帧画面存成 PNG（铺在深灰底上，便于看出透明区域）
bool WfSavePng(const FlipBitmap& canvas, const std::wstring& path)
{
    const uint32_t kBackdrop = 0xFF404040u;   //深灰底
    std::vector<uint32_t> composed(canvas.m_pixels.size());
    for (size_t i = 0; i < composed.size(); ++i)
    {
        const uint32_t s = canvas.m_pixels[i];
        const uint32_t a = s >> 24;
        uint32_t out = kWfAlphaMask;
        for (int shift = 0; shift < 24; shift += 8)
        {
            const uint32_t sc = (s >> shift) & 0xFFu;
            const uint32_t bc = (kBackdrop >> shift) & 0xFFu;
            out |= ((sc + bc * (255u - a) / 255u) & 0xFFu) << shift;
        }
        composed[i] = out;
    }
    CLSID png;
    if (composed.empty() || !WfPngEncoder(png))
        return false;
    Gdiplus::Bitmap bmp(canvas.m_width, canvas.m_height, canvas.m_width * 4, PixelFormat32bppARGB,
                        reinterpret_cast<BYTE*>(&composed[0]));
    return bmp.Save(path.c_str(), &png, NULL) == Gdiplus::Ok;
}

//WFS 用：把进度最接近 0.25 与 0.75 的两帧各存一份
class CWfShotObserver : public IFlipObserver
{
public:
    explicit CWfShotObserver(const std::wstring& prefix)
        : m_prefix(prefix)
        , m_best25(1.0f)
        , m_best75(1.0f)
    {
    }

    void OnFlipStep(FlipStep step) override
    {
        (void)step;
    }

    void OnFlipFrame(float t, const FlipBitmap& canvas) override
    {
        const float kQuarter = 0.25f;
        const float kThreeQuarters = 0.75f;
        if (fabsf(t - kQuarter) < m_best25)
        {
            m_best25 = fabsf(t - kQuarter);
            m_frame25 = canvas;
        }
        if (fabsf(t - kThreeQuarters) < m_best75)
        {
            m_best75 = fabsf(t - kThreeQuarters);
            m_frame75 = canvas;
        }
    }

    std::wstring m_prefix;   //文件名前缀（含目录）
    float m_best25;          //已存那一帧与 0.25 的差
    float m_best75;          //已存那一帧与 0.75 的差
    FlipBitmap m_frame25;    //进度最接近 0.25 的一帧
    FlipBitmap m_frame75;    //进度最接近 0.75 的一帧
};

//按真实登录窗、注册窗的外观配置一个测试窗口：透明标题栏、白色按钮、背景图
class CWfAuthWindow : public balloonwjui::DuiFrameWindow
{
public:
    CWfAuthWindow()
        : m_bg(NULL)
    {
    }

    ~CWfAuthWindow()
    {
        if (IsWindow())
            DestroyWindow();
        if (m_bg != NULL)
            ::DeleteObject(m_bg);
    }

    bool Build(bool login, int x, int y)
    {
        if (Create(NULL, CWindow::rcDefault, L"wfs", WS_OVERLAPPEDWINDOW, 0) == NULL)
            return false;
        SetTitle(_T(""));
        SetTitleBarHeight(login ? kLoginTitleBarHeight : balloonregister::kRegWindowTitleBarH);
        SetTitleBarTransparent(true);
        SetCaptionGlyphColor(RGB(255, 255, 255));
        SetButtons(true, false, true);
        if (login)
        {
            m_bg = CreateLoginFrameBgBitmap();
            if (m_bg != NULL)
                SetBgImage(m_bg, LoginFrameBgInsets());
            SetClientContent(BuildLoginBody());
            ResizeClient(kLoginWindowWidth, kLoginWindowHeight);
        }
        else
        {
            //注册窗的背景图在 BalloonRegisterWindow.cpp 里（文件内函数），这里画一个近似的：上 160 像素橙色、其余白色
            const int kBannerH = 160;
            HDC screen = ::GetDC(NULL);
            m_bg = ::CreateCompatibleBitmap(screen, balloonregister::kRegWindowWidth, balloonregister::kRegWindowHeight);
            HDC mem = ::CreateCompatibleDC(screen);
            HGDIOBJ old = ::SelectObject(mem, m_bg);
            RECT all = { 0, 0, balloonregister::kRegWindowWidth, balloonregister::kRegWindowHeight };
            HBRUSH white = ::CreateSolidBrush(RGB(255, 255, 255));
            ::FillRect(mem, &all, white);
            ::DeleteObject(white);
            RECT banner = { 0, 0, balloonregister::kRegWindowWidth, kBannerH };
            HBRUSH orange = ::CreateSolidBrush(RGB(0xFF, 0x98, 0x6A));
            ::FillRect(mem, &banner, orange);
            ::DeleteObject(orange);
            ::SelectObject(mem, old);
            ::DeleteDC(mem);
            ::ReleaseDC(NULL, screen);
            RECT insets = { 1, kBannerH, 1, 12 };
            SetBgImage(m_bg, insets);
            SetClientContent(balloonregister::BuildRegisterBody(balloonregister::kRegStepFillInfo));
            ResizeClient(balloonregister::kRegWindowWidth, balloonregister::kRegWindowHeight);
        }
        SetWindowPos(NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        return true;
    }

private:
    HBITMAP m_bg;   //背景图，本对象持有
};

} // namespace

TEST_CASE("WFS 翻转效果图：真实布局的登录窗与注册窗，存进度约 0.25 与 0.75 的帧", "[.][snapshot][authflip]")
{
    wchar_t dir[MAX_PATH] = { 0 };
    const bool save = ::GetEnvironmentVariableW(kWfShotEnv, dir, MAX_PATH) > 0;
    Gdiplus::GdiplusStartupInput input;
    ULONG_PTR token = 0;
    REQUIRE(Gdiplus::GdiplusStartup(&token, &input, NULL) == Gdiplus::Ok);
    {
        CWfAuthWindow login;
        CWfAuthWindow reg;
        REQUIRE(login.Build(true, 200, 60));
        RECT rcLogin;
        login.GetWindowRect(&rcLogin);
        RECT rcReg;
        REQUIRE(reg.Build(false, 0, 0));
        reg.GetWindowRect(&rcReg);
        const RECT work = MakeRc(0, 0, 1920, 1040);
        const RECT target = AlignTopCenter(rcLogin, rcReg.right - rcReg.left, rcReg.bottom - rcReg.top, work);
        reg.SetWindowPos(NULL, target.left, target.top, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        login.ShowWindow(SW_SHOW);
        PumpMessages(kWfPumpMs * 3);

        CWfShotObserver forward(std::wstring(dir) + L"\\flip_forward");
        CHECK(SwitchDuiWindows(login, reg, kFlipForward, true, &forward));
        PumpMessages(kWfPumpMs * 3);
        CWfShotObserver backward(std::wstring(dir) + L"\\flip_backward");
        CHECK(SwitchDuiWindows(reg, login, kFlipBackward, true, &backward));
        CHECK(login.IsWindowVisible());
        if (save)
        {
            CHECK(WfSavePng(forward.m_frame25, forward.m_prefix + L"_25.png"));
            CHECK(WfSavePng(forward.m_frame75, forward.m_prefix + L"_75.png"));
            CHECK(WfSavePng(backward.m_frame25, backward.m_prefix + L"_25.png"));
            CHECK(WfSavePng(backward.m_frame75, backward.m_prefix + L"_75.png"));
        }
    }
    //刻意不调 GdiplusShutdown：产品代码按「初始化一次、从不关闭」使用 GDI+，测试中途关掉会让后续用例访问已释放的
    //GDI+（与 BalloonLoginLayoutTests.cpp 同类用例的做法相同）
    (void)token;
}

// =====================================================================
// WFC 实验（2026-10-07，默认不运行，只在用户的可见桌面上手动运行）：翻转结束时界面闪一下的原因
// =====================================================================
//
// 用户反馈登录窗与注册窗翻转完成后界面会闪一下。读代码得到两种推测：A 系统给重新显示的目标窗口播了「打开」动画
// （代码里没有关掉 DWM 的窗口动画，本机又开着窗口动画），而过渡窗口在同一时刻就被销毁；B 目标窗口的新画面还没被
// 桌面合成，过渡窗口就先没了。DWM 动画只在可见桌面上播放，不可见桌面上看不到，所以这个实验要在可见桌面上运行。
// 做法：后台线程每次等桌面合成完成（DwmFlush）后截一次屏（带 CAPTUREBLT，分层的过渡窗口也截得到），从翻转开始截到
// 结束后约 0.6 秒。每组新建一对窗口、切到前台后做一次翻转，以最后稳定的一帧为基准，列出「目标窗口已显示」前后每一帧
// 与基准的平均差异，差异最大的几帧存成 PNG（目录取环境变量 FLAMINGO_FLIPCAP_DIR）。各轮的做法与结论见用例里的注释：
// 闪烁来自目标窗口第一次显示时的 DWM 打开动画（淡入加放大，约 300 毫秒），与过渡窗口自身的动画无关。

#include <algorithm>
#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")
#include "TestDpiAwareScope.h"

namespace {

const wchar_t* const kWfcDirEnv = L"FLAMINGO_FLIPCAP_DIR";   //实验输出目录的环境变量
const wchar_t* const kWfcOverlayClass = L"FlamingoWindowFlip"; //过渡窗口的窗口类名（WindowFlipTransition.cpp）
const DWORD kWfcDwmTransitionsForceDisabled = 3;              //DWMWA_TRANSITIONS_FORCEDISABLED 的取值
const size_t kWfcMaxFrames = 200;                             //每种做法最多保留的帧数
const DWORD kWfcTailMs = 600;                                 //翻转函数返回之后继续截屏的时长（毫秒）
const DWORD kWfcSettleMs = 800;                               //每种做法之间等窗口稳定的时长（毫秒）
const int kWfcReportFromMs = -60;                             //输出差异序列时，相对「目标窗口已显示」的起点（毫秒）
const int kWfcReportToMs = 450;                               //输出差异序列时的终点（毫秒）
const double kWfcFlickerDiff = 2.0;                           //与基准的平均差异超过它（0～255）就算一帧「不一样」
const int kWfcSavePeaks = 3;                                  //每种做法存几张差异最大的帧
const uint32_t kWfcChannelMask = 0xFF;                        //取一个颜色分量
const double kWfcMsPerSecond = 1000.0;                        //秒换算毫秒

//一帧截屏：相对实验起点的时间与像素（0xAARRGGBB，自上而下）
struct WfcFrame
{
    double     m_ms;      //截下这一帧时距实验起点的毫秒数
    FlipBitmap m_image;   //截下的屏幕区域
};

//后台截屏线程的参数与结果
struct WfcCapture
{
    RECT                  m_rect;       //截屏的屏幕区域
    LARGE_INTEGER         m_freq;       //QueryPerformanceCounter 的频率
    LARGE_INTEGER         m_start;      //实验起点
    volatile LONG         m_stop;       //置 1 时线程退出
    std::vector<WfcFrame> m_frames;     //截下的帧（线程结束之前只有截屏线程读写）

    WfcCapture()
        : m_stop(0)
    {
        ::SetRectEmpty(&m_rect);
        ::QueryPerformanceFrequency(&m_freq);
        ::QueryPerformanceCounter(&m_start);
    }

    //距实验起点的毫秒数
    double NowMs() const
    {
        LARGE_INTEGER now;
        ::QueryPerformanceCounter(&now);
        return static_cast<double>(now.QuadPart - m_start.QuadPart) * kWfcMsPerSecond
             / static_cast<double>(m_freq.QuadPart);
    }
};

//截屏线程：每次桌面合成完成后截一次屏，直到被要求停止或帧数到上限
DWORD WINAPI WfcCaptureThread(LPVOID param)
{
    //截屏线程同样按显示器感知 DPI，屏幕坐标与建窗口的线程一致
    TestDpiAwareScope dpiAware;
    WfcCapture* cap = static_cast<WfcCapture*>(param);
    const int w = cap->m_rect.right - cap->m_rect.left;
    const int h = cap->m_rect.bottom - cap->m_rect.top;
    HDC screen = ::GetDC(NULL);
    HDC mem = ::CreateCompatibleDC(screen);
    BITMAPINFO bi;
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = NULL;
    HBITMAP dib = ::CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    HGDIOBJ old = ::SelectObject(mem, dib);
    while (::InterlockedCompareExchange(&cap->m_stop, 0, 0) == 0 && cap->m_frames.size() < kWfcMaxFrames)
    {
        ::DwmFlush();
        ::BitBlt(mem, 0, 0, w, h, screen, cap->m_rect.left, cap->m_rect.top, SRCCOPY | CAPTUREBLT);
        ::GdiFlush();
        WfcFrame f;
        f.m_ms = cap->NowMs();
        f.m_image.m_width = w;
        f.m_image.m_height = h;
        const uint32_t* p = static_cast<const uint32_t*>(bits);
        f.m_image.m_pixels.assign(p, p + static_cast<size_t>(w) * static_cast<size_t>(h));
        for (size_t i = 0; i < f.m_image.m_pixels.size(); ++i)
            f.m_image.m_pixels[i] |= 0xFF000000u;
        cap->m_frames.push_back(f);
    }
    ::SelectObject(mem, old);
    ::DeleteObject(dib);
    ::DeleteDC(mem);
    ::ReleaseDC(NULL, screen);
    return 0;
}

//两帧的平均差异：三个颜色分量差的绝对值的平均（0～255）；尺寸不同时返回 -1
double WfcMeanDiff(const FlipBitmap& a, const FlipBitmap& b)
{
    if (a.m_pixels.size() != b.m_pixels.size() || a.m_pixels.empty())
        return -1.0;
    double sum = 0.0;
    for (size_t i = 0; i < a.m_pixels.size(); ++i)
    {
        const uint32_t x = a.m_pixels[i];
        const uint32_t y = b.m_pixels[i];
        for (int shift = 0; shift <= 16; shift += 8)
        {
            const int cx = (int)((x >> shift) & kWfcChannelMask);
            const int cy = (int)((y >> shift) & kWfcChannelMask);
            sum += (cx > cy) ? (cx - cy) : (cy - cx);
        }
    }
    return sum / (static_cast<double>(a.m_pixels.size()) * 3.0);
}

//一种做法的选项
struct WfcOptions
{
    WfcOptions()
        : m_disableWindowTransitions(false)
        , m_disableOverlayTransitions(false)
        , m_flushBeforeEnd(false)
        , m_restoreTargetAfterShow(false)
        , m_saveSequence(false)
        , m_dir(kFlipForward)
    {
    }

    bool m_disableWindowTransitions;    //翻转之前就关掉起始与目标两个窗口的 DWM 动画，翻转之后不恢复
    bool m_disableOverlayTransitions;   //过渡窗口盖上之后关掉它的 DWM 动画
    bool m_flushBeforeEnd;              //「目标窗口已显示」之后先等一次桌面合成，再让过渡窗口销毁
    bool m_restoreTargetAfterShow;      //模拟修法：过渡窗口盖上时关掉目标窗口的 DWM 动画，目标窗口一显示就恢复
    bool m_saveSequence;                //把差异序列范围内的每一帧都存成 PNG
    FlipDirection m_dir;                //翻转方向（只影响画面，与 DWM 动画无关）
};

//实验用观察者：记下「目标窗口已显示」的时刻，并按选项在各步骤上开关 DWM 动画
class CWfcObserver : public IFlipObserver
{
public:
    CWfcObserver(const WfcCapture* cap, const WfcOptions& opts, HWND target)
        : m_cap(cap)
        , m_opts(opts)
        , m_target(target)
        , m_shownMs(0.0)
        , m_finishedMs(0.0)
    {
    }

    void OnFlipStep(FlipStep step) override
    {
        if (step == kFlipStepCovered && m_opts.m_disableOverlayTransitions)
        {
            HWND overlay = ::FindWindowW(kWfcOverlayClass, NULL);
            if (overlay != NULL)
                SetTransitionsDisabled(overlay, true);
        }
        if (step == kFlipStepCovered && m_opts.m_restoreTargetAfterShow)
            SetTransitionsDisabled(m_target, true);
        if (step == kFlipStepTargetShown)
        {
            m_shownMs = m_cap->NowMs();
            if (m_opts.m_restoreTargetAfterShow)
                SetTransitionsDisabled(m_target, false);
            if (m_opts.m_flushBeforeEnd)
                ::DwmFlush();
        }
        if (step == kFlipStepFinished)
            m_finishedMs = m_cap->NowMs();
    }

    void OnFlipFrame(float /*t*/, const FlipBitmap& /*canvas*/) override
    {
    }

    static void SetTransitionsDisabled(HWND hwnd, bool disabled)
    {
        BOOL value = disabled ? TRUE : FALSE;
        ::DwmSetWindowAttribute(hwnd, kWfcDwmTransitionsForceDisabled, &value, sizeof(value));
    }

    const WfcCapture* m_cap;   //截屏线程的结果（取时间用，不拥有）
    WfcOptions m_opts;         //本次做法的选项
    HWND m_target;             //目标窗口
    double m_shownMs;          //「目标窗口已显示」的时刻（毫秒，相对实验起点）
    double m_finishedMs;       //切换结束（过渡窗口已销毁）的时刻
};

//把窗口切到前台。单测程序由后台进程启动，系统不允许它直接调用 SetForegroundWindow 抢前台，窗口会被压在别的
//窗口后面；这里临时把本线程的输入状态挂到当前前台窗口的线程上再切换，与真实场景一致（用户在登录窗里点「注册账号」，
//客户端本来就在前台）。返回切换后前台窗口是否就是 hwnd
bool WfcBringToFront(HWND hwnd)
{
    HWND fg = ::GetForegroundWindow();
    const DWORD fgThread = (fg != NULL) ? ::GetWindowThreadProcessId(fg, NULL) : 0;
    const DWORD self = ::GetCurrentThreadId();
    const bool attach = (fgThread != 0 && fgThread != self) ? (::AttachThreadInput(self, fgThread, TRUE) != FALSE) : false;
    ::SetWindowPos(hwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    ::SetForegroundWindow(hwnd);
    if (attach)
        ::AttachThreadInput(self, fgThread, FALSE);
    return ::GetForegroundWindow() == hwnd;
}

//跑一种做法：从显示着的 from 翻到 to，返回「目标窗口已显示」之后与基准差异较大的帧数，并把差异序列写进用例输出、
//峰值帧存成 PNG；结束后直接切回 from
int WfcRunVariant(const char* name, CWfAuthWindow& from, CWfAuthWindow& to, const WfcOptions& opts,
                  const std::wstring& outDir)
{
    CWfcObserver::SetTransitionsDisabled(to.m_hWnd, opts.m_disableWindowTransitions);
    CWfcObserver::SetTransitionsDisabled(from.m_hWnd, opts.m_disableWindowTransitions);

    WfcCapture cap;
    RECT a;
    RECT b;
    from.GetWindowRect(&a);
    to.GetWindowRect(&b);
    ::UnionRect(&cap.m_rect, &a, &b);
    CWfcObserver observer(&cap, opts, to.m_hWnd);
    HANDLE thread = ::CreateThread(NULL, 0, WfcCaptureThread, &cap, 0, NULL);
    const bool fromFront = WfcBringToFront(from.m_hWnd);
    PumpMessages(kWfPumpMs);
    SwitchDuiWindows(from, to, opts.m_dir, true, &observer);
    const bool toFront = (::GetForegroundWindow() == to.m_hWnd);
    PumpMessages(kWfcTailMs);
    ::InterlockedExchange(&cap.m_stop, 1);
    ::WaitForSingleObject(thread, INFINITE);
    ::CloseHandle(thread);
    if (cap.m_frames.size() < 2)
    {
        WARN(name << ": too few frames captured (" << cap.m_frames.size() << ")");
        return -1;
    }

    const FlipBitmap& ref = cap.m_frames.back().m_image;
    const std::wstring wname(name, name + strlen(name));
    std::string line;
    int differing = 0;
    std::vector<std::pair<double, size_t> > peaks;
    for (size_t i = 0; i < cap.m_frames.size(); ++i)
    {
        const double rel = cap.m_frames[i].m_ms - observer.m_shownMs;
        if (rel < kWfcReportFromMs || rel > kWfcReportToMs)
            continue;
        const double d = WfcMeanDiff(cap.m_frames[i].m_image, ref);
        char buf[48];
        sprintf_s(buf, "%+.0fms:%.1f ", rel, d);
        line += buf;
        if (rel >= 0.0 && d > kWfcFlickerDiff)
            ++differing;
        if (rel >= 0.0)
            peaks.push_back(std::make_pair(d, i));
        if (opts.m_saveSequence && !outDir.empty())
        {
            wchar_t file[MAX_PATH];
            swprintf_s(file, L"%s\\%s_seq%02u_%+.0fms_d%.0f.png", outDir.c_str(), wname.c_str(), (unsigned)i, rel, d);
            WfSavePng(cap.m_frames[i].m_image, file);
        }
    }
    WARN(name << ": source foreground before flip " << fromFront << ", target foreground after flip " << toFront
         << ", frames " << cap.m_frames.size() << ", target shown at " << (int)observer.m_shownMs
         << " ms, finished at " << (int)observer.m_finishedMs << " ms, frames after shown differing > "
         << kWfcFlickerDiff << ": " << differing << "\n  " << line);
    if (!outDir.empty())
    {
        std::sort(peaks.begin(), peaks.end());
        for (int k = 0; k < kWfcSavePeaks && !peaks.empty(); ++k)
        {
            const std::pair<double, size_t> p = peaks.back();
            peaks.pop_back();
            wchar_t file[MAX_PATH];
            swprintf_s(file, L"%s\\%s_peak%d_%+.0fms_d%.0f.png", outDir.c_str(), wname.c_str(), k,
                       cap.m_frames[p.second].m_ms - observer.m_shownMs, p.first);
            WfSavePng(cap.m_frames[p.second].m_image, file);
        }
        WfSavePng(ref, outDir + L"\\" + wname + L"_final.png");
    }
    //回到初始状态：直接切回 from，等窗口稳定
    SwitchDuiWindows(to, from, kFlipBackward, false, NULL);
    PumpMessages(kWfcSettleMs);
    return differing;
}

//新建一对与真实登录窗、注册窗相同的窗口，摆到主显示器工作区中央，显示登录窗并切到前台后跑一种做法。
//backward 为 false 时测「登录窗 → 注册窗」（注册窗第一次显示，与真实客户端每次新建注册窗相同）；为 true 时先不带动画
//切到注册窗，再测「注册窗 → 登录窗」（登录窗此前显示过，与真实客户端翻回登录窗相同）。secondFlip 为 true 时在同一对
//窗口上再测一次（名称加后缀 _second_flip）。返回第一次测量里目标窗口显示之后与基准差异较大的帧数；窗口在函数返回前销毁
int WfcRunFresh(const char* name, const WfcOptions& opts, bool backward, bool secondFlip, const std::wstring& outDir)
{
    CWfAuthWindow login;
    CWfAuthWindow reg;
    if (!login.Build(true, 0, 0) || !reg.Build(false, 0, 0))
        return -1;
    //注册窗上边缘与登录窗对齐、水平居中（与真实窗口相同）
    RECT work;
    ::SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    RECT rcLogin;
    login.GetWindowRect(&rcLogin);
    const int lw = rcLogin.right - rcLogin.left;
    const int lh = rcLogin.bottom - rcLogin.top;
    login.SetWindowPos(NULL, (work.left + work.right - lw) / 2, (work.top + work.bottom - lh) / 2, 0, 0,
                       SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    login.GetWindowRect(&rcLogin);
    RECT rcReg;
    reg.GetWindowRect(&rcReg);
    const RECT target = AlignTopCenter(rcLogin, rcReg.right - rcReg.left, rcReg.bottom - rcReg.top, work);
    reg.SetWindowPos(NULL, target.left, target.top, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    login.ShowWindow(SW_SHOW);
    WfcBringToFront(login.m_hWnd);
    PumpMessages(kWfcSettleMs);

    CWfAuthWindow& from = backward ? reg : login;
    CWfAuthWindow& to = backward ? login : reg;
    if (backward)
    {
        SwitchDuiWindows(login, reg, kFlipForward, false, NULL);
        PumpMessages(kWfcSettleMs);
    }
    WfcOptions first = opts;
    first.m_dir = backward ? kFlipBackward : kFlipForward;
    const int differing = WfcRunVariant(name, from, to, first, outDir);
    if (secondFlip)
    {
        const std::string second = std::string(name) + "_second_flip";
        WfcOptions again = first;
        again.m_saveSequence = false;
        WfcRunVariant(second.c_str(), from, to, again, outDir);
    }
    login.ShowWindow(SW_HIDE);
    reg.ShowWindow(SW_HIDE);
    PumpMessages(kWfPumpMs);
    return differing;
}

} // namespace

TEST_CASE("WFC 实验：翻转结束前后逐帧截屏，比较现状与分别关掉各窗口 DWM 动画的做法", "[.][flipcapture]")
{
    TestDpiAwareScope dpiAware;
    wchar_t dir[MAX_PATH] = { 0 };
    ::GetEnvironmentVariableW(kWfcDirEnv, dir, MAX_PATH);
    const std::wstring outDir(dir);
    if (!outDir.empty())
        ::CreateDirectoryW(outDir.c_str(), NULL);
    Gdiplus::GdiplusStartupInput input;
    ULONG_PTR token = 0;
    REQUIRE(Gdiplus::GdiplusStartup(&token, &input, NULL) == Gdiplus::Ok);

    //第二轮（同日）：分别只关掉两个窗口的、只关掉过渡窗口的，区分动画来自哪个窗口
    //第三轮（同日）：每组翻转之前先把起始窗口切到前台。结果是只有第一次翻转会闪，同一对窗口再翻就不闪，
    //  而第二轮、第三轮的对照组用的都是已经显示过的窗口，不能说明问题
    //第四轮（同日）：每组都新建一对窗口。现状两次都闪、只关过渡窗口的照样闪、只关两个窗口的不闪，
    //  同一对窗口的第二次翻转不闪：闪烁来自目标窗口第一次显示时的 DWM 打开动画
    //第五轮（同日）：验证修法「过渡窗口盖上时关掉目标窗口的 DWM 动画、目标窗口一显示就恢复」，并测翻回登录窗的方向
    WfcOptions asIs;
    WfcOptions fix;
    fix.m_restoreTargetAfterShow = true;
    const int asIsCount = WfcRunFresh("A_as_is", asIs, false, false, outDir);
    const int fixCount = WfcRunFresh("F_restore_after_show", fix, false, true, outDir);
    const int fixAgain = WfcRunFresh("F2_restore_after_show_again", fix, false, false, outDir);
    const int backAsIs = WfcRunFresh("G_backward_as_is", asIs, true, false, outDir);
    WARN("frames differing after the target was shown (fresh windows each): as-is " << asIsCount
         << ", fix (disable on cover, restore after show) " << fixCount << " / " << fixAgain
         << ", backward as-is " << backAsIs);
    (void)token;
}
