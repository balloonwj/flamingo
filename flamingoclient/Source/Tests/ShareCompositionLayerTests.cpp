/**
 *  DirectComposition 标注显示层 CShareCompositionLayer 的单元测试（标签 [shareaa]），ShareCompositionLayerTests.cpp
 *  背景（2026-10-05 联调反馈「绘制的线条有锯齿」）：发起人本机的标注层原先是颜色键窗口，笔迹边缘只能全透明或
 *  全不透明。改用 DirectComposition 之前先实测两件不确定的事：逐像素透明的边缘在屏幕上确实与下层混合；这种窗口
 *  设置「不被采集」（WDA_EXCLUDEFROMCAPTURE）能成功且真的不进采集。
 *  覆盖：
 *    - SSAA0 未 Attach 时 Present 返回 false；Attach 无效窗口返回 false；
 *    - SSAA1（隐藏用例，在桌面右下角显示约一秒的小窗口，须单独运行 "[shareaa]"）：洋红底窗口上盖显示层、画一条
 *      抗锯齿红线，屏幕上读到纯红与红洋红之间的过渡像素；设置排除采集返回成功，之后读到的只剩洋红。
 *    - SSAA2（隐藏用例，同上）：发起人的标注层 CShareAnnotationOverlay 用的是逐像素透明的显示层；在洋红底上用标注会话
 *      画一道直线，屏幕上读到笔迹颜色与洋红之间的过渡像素（原先的颜色键窗口只有全透明与全不透明，读不到过渡）。
 *      用 FLAMINGO_SHARE_NO_EXCLUDE=1 让显示层不排除采集，屏幕读取才看得到它。
 *    - SSAA3 绘制用的线宽保留小数：790 像素宽的画面上中档钢笔约 1.58 像素，不再截成 1 像素；最细不小于 1 像素。
 *  屏幕像素只在内存里统计，读取范围只有测试窗口自己那一块。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-05
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <string.h>
#include <vector>
#include <objidl.h>    //gdiplus.h 依赖 IStream 等 COM 声明
#include <gdiplus.h>   //在图层上画抗锯齿红线
#include <dwmapi.h>    //DwmFlush：等 DWM 把新内容合成到屏幕上

#include "../../FlamingoMedia/Share/ShareCompositionLayer.h"
#include "../../FlamingoMedia/Share/ShareAnnotation.h"
#include "../../FlamingoMedia/Share/ShareAnnotationOverlay.h"
#include "../../FlamingoMedia/Share/ShareAnnotationRender.h"
#include "../../FlamingoMedia/Share/ShareAnnotationSession.h"
#include "../../FlamingoMedia/Share/ShareAudioTransport.h"
#include <stdlib.h>   // _putenv_s

#pragma comment(lib, "dwmapi.lib")

namespace {

//测试窗口尺寸（像素）与离工作区右下角的距离
const int kSsaaW = 200;
const int kSsaaH = 120;
const int kSsaaMargin = 40;
//每像素字节数
const int kSsaaBpp = 4;
//红线宽（像素）与端点
const float kSsaaLineW = 3.0f;
//等 DWM 合成的轮数与每轮毫秒数
const int kSsaaWaitRounds = 8;
const DWORD kSsaaWaitMs = 50;
//排除采集的取值（Windows 10 2004 起；旧 SDK 宏下没有定义）
const DWORD kSsaaExcludeFromCapture = 0x00000011;
//不要重定向位图的扩展样式 WS_EX_NOREDIRECTIONBITMAP（Windows 8 起；本工程按 Windows 7 编译，WinUser.h 不定义它）
const DWORD kSsaaNoRedirectionBitmap = 0x00200000L;
//判定颜色的阈值：纯红 B < 60；过渡像素 60 <= B <= 200；洋红 B > 230（红与洋红只差蓝通道）
const int kSsaaRedMaxB = 60;
const int kSsaaBlendMaxB = 200;
const int kSsaaHighChannel = 200;
const int kSsaaLowChannel = 60;
//按显示器感知 DPI（DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2）
const LONG_PTR kSsaaPerMonitorV2 = -4;

typedef HANDLE (WINAPI* SetThreadDpiCtxFn)(HANDLE ctx);

//洋红底窗口与显示层窗口的类名
const wchar_t* const kSsaaBgClass = L"FlamingoSsaaBackground";
const wchar_t* const kSsaaLayerClass = L"FlamingoSsaaLayer";

//统计结果
struct SsaaCounts
{
    int m_red;     //纯红像素数
    int m_blend;   //红与洋红之间的过渡像素数
    int m_other;   //其它颜色的像素数（既不是红、过渡，也不是洋红）
};

void SsaaPump()
{
    for (int i = 0; i < kSsaaWaitRounds; ++i)
    {
        MSG msg;
        while (::PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
        {
            ::TranslateMessage(&msg);
            ::DispatchMessageW(&msg);
        }
        ::DwmFlush();
        ::Sleep(kSsaaWaitMs);
    }
}

//读屏幕上 rc 那一块的像素（只在内存里），统计颜色
SsaaCounts SsaaReadScreen(const RECT& rc)
{
    SsaaCounts counts = { 0, 0, 0 };
    const int w = rc.right - rc.left;
    const int h = rc.bottom - rc.top;
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
    HGDIOBJ old = ::SelectObject(mem, dib);
    ::BitBlt(mem, 0, 0, w, h, screen, rc.left, rc.top, SRCCOPY | CAPTUREBLT);
    const uint8_t* p = static_cast<const uint8_t*>(bits);
    for (int i = 0; i < w * h; ++i)
    {
        const int b = p[i * kSsaaBpp];
        const int g = p[i * kSsaaBpp + 1];
        const int r = p[i * kSsaaBpp + 2];
        if (r < kSsaaHighChannel || g > kSsaaLowChannel)
            ++counts.m_other;
        else if (b < kSsaaRedMaxB)
            ++counts.m_red;
        else if (b <= kSsaaBlendMaxB)
            ++counts.m_blend;
    }
    ::SelectObject(mem, old);
    ::DeleteObject(dib);
    ::DeleteDC(mem);
    ::ReleaseDC(NULL, screen);
    return counts;
}

void SsaaRegisterClasses()
{
    WNDCLASSW wc;
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = ::DefWindowProcW;
    wc.hInstance = ::GetModuleHandleW(NULL);
    wc.lpszClassName = kSsaaBgClass;
    wc.hbrBackground = ::CreateSolidBrush(RGB(255, 0, 255));
    ::RegisterClassW(&wc);
    wc.lpszClassName = kSsaaLayerClass;
    wc.hbrBackground = NULL;
    ::RegisterClassW(&wc);
}

//在全透明的预乘 BGRA 图层上画一条抗锯齿红线
void SsaaDrawLine(std::vector<uint8_t>& bgra)
{
    ULONG_PTR token = 0;
    Gdiplus::GdiplusStartupInput input;
    Gdiplus::GdiplusStartup(&token, &input, NULL);
    {
        Gdiplus::Bitmap bmp(kSsaaW, kSsaaH, kSsaaW * kSsaaBpp, PixelFormat32bppPARGB, &bgra[0]);
        Gdiplus::Graphics g(&bmp);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        Gdiplus::Pen pen(Gdiplus::Color(255, 255, 0, 0), kSsaaLineW);
        g.DrawLine(&pen, 10.0f, 10.0f, static_cast<float>(kSsaaW - 10), static_cast<float>(kSsaaH - 20));
    }
    Gdiplus::GdiplusShutdown(token);
}

} // namespace

TEST_CASE("SSAA0 composition layer rejects use before attach", "[shareaa]")
{
    screenshare::CShareCompositionLayer layer;
    std::vector<uint8_t> bgra(static_cast<size_t>(kSsaaW) * kSsaaH * kSsaaBpp, 0);
    CHECK_FALSE(layer.IsAttached());
    CHECK_FALSE(layer.Present(&bgra[0], kSsaaW, kSsaaH));
    CHECK_FALSE(layer.Attach(NULL));
    CHECK_FALSE(layer.IsAttached());
}

TEST_CASE("SSAA1 composition layer blends edges on screen and can be excluded from capture", "[.][shareaa]")
{
    //本线程临时改为按显示器感知 DPI，窗口坐标与屏幕读取都是物理像素，系统不做缩放
    SetThreadDpiCtxFn setCtx = reinterpret_cast<SetThreadDpiCtxFn>(
        ::GetProcAddress(::GetModuleHandleW(L"user32.dll"), "SetThreadDpiAwarenessContext"));
    HANDLE oldCtx = (setCtx != NULL) ? setCtx(reinterpret_cast<HANDLE>(kSsaaPerMonitorV2)) : NULL;

    SsaaRegisterClasses();
    RECT work;
    ::SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    RECT rc = { work.right - kSsaaMargin - kSsaaW, work.bottom - kSsaaMargin - kSsaaH,
                work.right - kSsaaMargin, work.bottom - kSsaaMargin };
    //两个窗口都鼠标穿透、不抢焦点
    const DWORD bgEx = WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED | WS_EX_TRANSPARENT;
    HWND bg = ::CreateWindowExW(bgEx, kSsaaBgClass, L"SSAA-BG", WS_POPUP, rc.left, rc.top, kSsaaW, kSsaaH,
                                NULL, NULL, ::GetModuleHandleW(NULL), NULL);
    REQUIRE(bg != NULL);
    ::SetLayeredWindowAttributes(bg, 0, 255, LWA_ALPHA);
    ::ShowWindow(bg, SW_SHOWNOACTIVATE);
    const DWORD layerEx = kSsaaNoRedirectionBitmap | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST |
                          WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE;
    HWND top = ::CreateWindowExW(layerEx, kSsaaLayerClass, L"SSAA-LAYER", WS_POPUP, rc.left, rc.top, kSsaaW,
                                 kSsaaH, NULL, NULL, ::GetModuleHandleW(NULL), NULL);
    REQUIRE(top != NULL);
    ::SetLayeredWindowAttributes(top, 0, 255, LWA_ALPHA);

    screenshare::CShareCompositionLayer layer;
    const bool attached = layer.Attach(top);
    CHECK(attached);
    std::vector<uint8_t> bgra(static_cast<size_t>(kSsaaW) * kSsaaH * kSsaaBpp, 0);
    SsaaDrawLine(bgra);
    CHECK(layer.Present(&bgra[0], kSsaaW, kSsaaH));
    ::SetWindowPos(top, HWND_TOPMOST, rc.left, rc.top, kSsaaW, kSsaaH, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    SsaaPump();

    //未排除时：屏幕上既有纯红，也有红与洋红之间的过渡（抗锯齿边缘与下层混合）
    const SsaaCounts shown = SsaaReadScreen(rc);
    INFO("shown: red=" << shown.m_red << " blend=" << shown.m_blend << " other=" << shown.m_other);
    CHECK(shown.m_red > 0);
    CHECK(shown.m_blend > 0);
    CHECK(shown.m_other == 0);

    //设置排除采集：成功，之后屏幕采集里只剩洋红
    const BOOL excluded = ::SetWindowDisplayAffinity(top, kSsaaExcludeFromCapture);
    INFO("SetWindowDisplayAffinity error=" << ::GetLastError());
    CHECK(excluded != FALSE);
    SsaaPump();
    const SsaaCounts hidden = SsaaReadScreen(rc);
    INFO("hidden: red=" << hidden.m_red << " blend=" << hidden.m_blend << " other=" << hidden.m_other);
    CHECK(hidden.m_red == 0);
    CHECK(hidden.m_blend == 0);

    layer.Detach();
    ::DestroyWindow(top);
    ::DestroyWindow(bg);
    if (setCtx != NULL && oldCtx != NULL)
        setCtx(oldCtx);
}

namespace {

//标注会话的假传输：发出的包体丢弃
class SsaaTransport : public screenshare::ShareAudioTransport
{
public:
    virtual bool SendRoomBody(const std::string& body) override
    {
        (void)body;
        return true;
    }
};

//会话坐标：图标方框按比例换成归一化坐标
screenshare::AnnotationPoint SsaaPt(double fx, double fy)
{
    return screenshare::AnnotationPoint(static_cast<uint16_t>(fx * screenshare::kAnnotCoordMax),
                                        static_cast<uint16_t>(fy * screenshare::kAnnotCoordMax));
}

//会话的座位号、命中容差与起始时刻
const int kSsaaSeat = 0;
const int kSsaaTolerance = 100;
const int64_t kSsaaNow = 100;
//SSAA3 的画面宽（像素）与期望的中档钢笔宽（像素）及误差
const int kSsaaViewW = 790;
const int kSsaaViewH = 499;
const float kSsaaPenPx = 1.58f;
const float kSsaaPxTolerance = 0.01f;

} // namespace

TEST_CASE("SSAA2 host annotation overlay blends stroke edges", "[.][shareaa]")
{
    SetThreadDpiCtxFn setCtx = reinterpret_cast<SetThreadDpiCtxFn>(
        ::GetProcAddress(::GetModuleHandleW(L"user32.dll"), "SetThreadDpiAwarenessContext"));
    HANDLE oldCtx = (setCtx != NULL) ? setCtx(reinterpret_cast<HANDLE>(kSsaaPerMonitorV2)) : NULL;
    //调试开关：标注层不排除采集，屏幕读取才看得到
    _putenv_s("FLAMINGO_SHARE_NO_EXCLUDE", "1");

    SsaaRegisterClasses();
    RECT work;
    ::SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    RECT rc = { work.right - kSsaaMargin - kSsaaW, work.bottom - kSsaaMargin - kSsaaH,
                work.right - kSsaaMargin, work.bottom - kSsaaMargin };
    const DWORD bgEx = WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED | WS_EX_TRANSPARENT;
    HWND bg = ::CreateWindowExW(bgEx, kSsaaBgClass, L"SSAA-BG", WS_POPUP, rc.left, rc.top, kSsaaW, kSsaaH,
                                NULL, NULL, ::GetModuleHandleW(NULL), NULL);
    REQUIRE(bg != NULL);
    ::SetLayeredWindowAttributes(bg, 0, 255, LWA_ALPHA);
    ::ShowWindow(bg, SW_SHOWNOACTIVATE);

    SsaaTransport transport;
    screenshare::ShareAnnotationSession session;
    session.Start(&transport, kSsaaSeat, kSsaaNow);
    session.SetTool(screenshare::annot_tool_line);
    session.SetWidthLevel(screenshare::annot_width_thick);
    session.PointerDown(SsaaPt(0.08, 0.12), kSsaaTolerance, kSsaaNow);
    session.PointerMove(SsaaPt(0.92, 0.80), kSsaaTolerance, kSsaaNow + 1);
    session.PointerUp(SsaaPt(0.92, 0.80), kSsaaNow + 2);
    REQUIRE(session.Model().Strokes().size() == 1);

    screenshare::CShareAnnotationOverlay overlay(&session);
    overlay.Place(rc, true);
    overlay.Redraw();
    SsaaPump();
    CHECK(overlay.IsAlphaLayer());
    const SsaaCounts shown = SsaaReadScreen(rc);
    INFO("overlay: red=" << shown.m_red << " blend=" << shown.m_blend << " other=" << shown.m_other);
    //测试窗口只有 200 像素宽，粗线换算后约 1 像素，斜线上没有完全覆盖的像素，所以不要求有纯色像素；
    //颜色键窗口时过渡像素恒为 0（改动前实测 red=163 blend=0）
    CHECK(shown.m_blend > 0);
    CHECK(shown.m_other == 0);

    overlay.Close();
    ::DestroyWindow(bg);
    _putenv_s("FLAMINGO_SHARE_NO_EXCLUDE", "");
    if (setCtx != NULL && oldCtx != NULL)
        setCtx(oldCtx);
}

TEST_CASE("SSAA3 stroke width keeps its fraction", "[shareaa]")
{
    const RECT view = { 0, 0, kSsaaViewW, kSsaaViewH };
    CHECK(screenshare::AnnotStrokeWidthPx(view, screenshare::kAnnotPenWidth) ==
          Catch::Approx(kSsaaPenPx).margin(kSsaaPxTolerance));
    //极细的线不小于 1 像素
    CHECK(screenshare::AnnotStrokeWidthPx(view, 1) == Catch::Approx(1.0f));
}
