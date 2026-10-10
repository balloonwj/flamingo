/**
 *  屏幕共享「共享内容」选择规则的单元测试（Suite SSSP，标签 [screenshare]），ShareSourcePolicyTests.cpp
 *
 *  覆盖 FlamingoMedia/Share/ShareSourcePolicy（docs/屏幕共享与录制方案.md 第 11.4 节 2d）：
 *    - SSSP1 哪些顶层窗口列入选择窗：普通应用窗口列入；不可见、被 DWM 隐藏、标题空白、本进程、桌面与任务栏、
 *            工具窗口、有属主的窗口、不可激活的窗口、太小的窗口不列；带 WS_EX_APPWINDOW 的工具窗口与有属主窗口照列；
 *            最小化的窗口不看尺寸照列；
 *    - SSSP2 显示器找对应的 DXGI 输出：跳过没有输出的显卡，显示器在第二块显卡上也能找到，找不到返回 false；
 *    - SSSP3 缩略图尺寸：保持宽高比缩进框内，只缩不放，结果至少 1×1，非法尺寸为 0×0；
 *    - SSSP4 （2h 联调发现）GDI 画进 32 位 DIB 的像素透明通道为 0，按钮按预乘透明度绘制时整块透明：
 *            MakeDibOpaque 把透明通道全部置为 255、颜色不变；不是 DIB 或为 NULL 时返回 false；
 *    - SSSP5 （2h 联调发现）边框窗口原先用逐像素透明的分层窗口，设置排除采集失败（错误 8），共享整块显示器时会被采进画面；
 *            改为颜色键分层窗口后，显示亲和性读回为 WDA_EXCLUDEFROMCAPTURE，窗口位置为目标矩形外扩 4 像素，点击穿透。
 *
 *  典型用法：FlamingoUnit.exe "SSSP*"
 *
 *  balloonwj@qq.com   2026-10-02
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <vector>
#include "../../FlamingoMedia/Share/ShareSourcePolicy.h"
#include "../../FlamingoMedia/Share/ShareSources.h"
#include "../../FlamingoMedia/Share/ShareBorderWindow.h"
#include "../../FlamingoMedia/Common/OsVersionPolicy.h"

using namespace screenshare;

namespace {

//一个正常的应用主窗口
ShareWindowFacts ssspNormal()
{
    ShareWindowFacts f;
    f.m_visible = true;
    f.m_width = 800;
    f.m_height = 600;
    f.m_title = L"Untitled - Notepad";
    f.m_className = L"Notepad";
    return f;
}

HMONITOR ssspMonitor(int id)
{
    return reinterpret_cast<HMONITOR>(static_cast<INT_PTR>(id));
}

} // namespace

TEST_CASE("SSSP1 哪些顶层窗口列入选择窗", "[screenshare]")
{
    CHECK(ShouldListShareWindow(ssspNormal()));

    ShareWindowFacts f = ssspNormal();
    f.m_visible = false;
    CHECK_FALSE(ShouldListShareWindow(f));

    f = ssspNormal();
    f.m_cloaked = true;
    CHECK_FALSE(ShouldListShareWindow(f));

    f = ssspNormal();
    f.m_title = L"   ";
    CHECK_FALSE(ShouldListShareWindow(f));
    f.m_title = L"";
    CHECK_FALSE(ShouldListShareWindow(f));

    f = ssspNormal();
    f.m_ownProcess = true;
    CHECK_FALSE(ShouldListShareWindow(f));

    //桌面与任务栏
    const wchar_t* shellClasses[] = { L"Progman", L"WorkerW", L"Shell_TrayWnd", L"Shell_SecondaryTrayWnd" };
    for (size_t i = 0; i < sizeof(shellClasses) / sizeof(shellClasses[0]); ++i)
    {
        f = ssspNormal();
        f.m_className = shellClasses[i];
        CHECK_FALSE(ShouldListShareWindow(f));
    }

    f = ssspNormal();
    f.m_toolWindow = true;
    CHECK_FALSE(ShouldListShareWindow(f));
    f.m_appWindow = true;
    CHECK(ShouldListShareWindow(f));

    f = ssspNormal();
    f.m_hasOwner = true;
    CHECK_FALSE(ShouldListShareWindow(f));
    f.m_appWindow = true;
    CHECK(ShouldListShareWindow(f));

    f = ssspNormal();
    f.m_noActivate = true;
    CHECK_FALSE(ShouldListShareWindow(f));

    f = ssspNormal();
    f.m_width = kShareWindowMinSize - 1;
    CHECK_FALSE(ShouldListShareWindow(f));
    f.m_width = kShareWindowMinSize;
    f.m_height = kShareWindowMinSize;
    CHECK(ShouldListShareWindow(f));

    //最小化的窗口外框很小，但照列
    f = ssspNormal();
    f.m_minimized = true;
    f.m_width = 0;
    f.m_height = 0;
    CHECK(ShouldListShareWindow(f));
}

TEST_CASE("SSSP2 显示器找对应的 DXGI 输出", "[screenshare]")
{
    //显卡 0 有两个输出（显示器 1、2），显卡 1 没有输出，显卡 2 有一个输出（显示器 3）
    std::vector<ShareDxgiOutputRef> outputs;
    outputs.push_back(ShareDxgiOutputRef(0, 0, ssspMonitor(1)));
    outputs.push_back(ShareDxgiOutputRef(0, 1, ssspMonitor(2)));
    outputs.push_back(ShareDxgiOutputRef(2, 0, ssspMonitor(3)));
    //同一显示器在后面再次出现（如核显与独显都报告了它）：取第一个
    outputs.push_back(ShareDxgiOutputRef(3, 0, ssspMonitor(1)));

    int adapter = -1;
    int output = -1;
    REQUIRE(FindDxgiOutputForMonitor(outputs, ssspMonitor(2), adapter, output));
    CHECK(adapter == 0);
    CHECK(output == 1);
    REQUIRE(FindDxgiOutputForMonitor(outputs, ssspMonitor(3), adapter, output));
    CHECK(adapter == 2);
    CHECK(output == 0);
    REQUIRE(FindDxgiOutputForMonitor(outputs, ssspMonitor(1), adapter, output));
    CHECK(adapter == 0);
    CHECK(output == 0);
    CHECK_FALSE(FindDxgiOutputForMonitor(outputs, ssspMonitor(4), adapter, output));
    CHECK_FALSE(FindDxgiOutputForMonitor(std::vector<ShareDxgiOutputRef>(), ssspMonitor(1), adapter, output));
}

TEST_CASE("SSSP3 缩略图尺寸保持宽高比缩进框内，只缩不放", "[screenshare]")
{
    int w = 0;
    int h = 0;
    FitShareSize(1920, 1080, 320, 180, w, h);
    CHECK(w == 320);
    CHECK(h == 180);
    //竖长的源：高度先到框边
    FitShareSize(1080, 1920, 320, 180, w, h);
    CHECK(h == 180);
    CHECK(w == 101);
    //比框小：不放大
    FitShareSize(200, 100, 320, 180, w, h);
    CHECK(w == 200);
    CHECK(h == 100);
    //极细长：短边至少 1
    FitShareSize(10000, 1, 320, 180, w, h);
    CHECK(w == 320);
    CHECK(h == 1);
    //非法尺寸
    FitShareSize(0, 100, 320, 180, w, h);
    CHECK(w == 0);
    CHECK(h == 0);
    FitShareSize(100, 100, 0, 180, w, h);
    CHECK(w == 0);
    CHECK(h == 0);
}

TEST_CASE("SSSP4 GDI 画的 32 位 DIB 置为不透明后颜色不变", "[screenshare]")
{
    const int kSide = 4;
    BITMAPINFO bi;
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = kSide;
    bi.bmiHeader.biHeight = -kSide;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = NULL;
    HBITMAP dib = ::CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    REQUIRE(dib != NULL);
    HDC dc = ::CreateCompatibleDC(NULL);
    HGDIOBJ old = ::SelectObject(dc, dib);
    RECT rc = { 0, 0, kSide, kSide };
    HBRUSH brush = ::CreateSolidBrush(RGB(10, 20, 30));
    ::FillRect(dc, &rc, brush);
    ::DeleteObject(brush);
    ::GdiFlush();
    const BYTE* px = static_cast<const BYTE*>(bits);
    //GDI 不写透明通道：这就是选择窗格子被画成透明的原因
    CHECK(px[3] == 0);

    REQUIRE(MakeDibOpaque(dib));
    for (int i = 0; i < kSide * kSide; ++i)
    {
        INFO("pixel " << i);
        CHECK(px[i * 4 + 0] == 30);
        CHECK(px[i * 4 + 1] == 20);
        CHECK(px[i * 4 + 2] == 10);
        CHECK(px[i * 4 + 3] == 255);
    }
    ::SelectObject(dc, old);
    ::DeleteDC(dc);
    ::DeleteObject(dib);

    //不是 DIB（设备相关位图）或为 NULL：返回 false
    CHECK_FALSE(MakeDibOpaque(NULL));
    HDC screen = ::GetDC(NULL);
    HBITMAP ddb = ::CreateCompatibleBitmap(screen, kSide, kSide);
    ::ReleaseDC(NULL, screen);
    REQUIRE(ddb != NULL);
    CHECK_FALSE(MakeDibOpaque(ddb));
    ::DeleteObject(ddb);
}

namespace {

//WDA_EXCLUDEFROMCAPTURE（Win10 2004 起；旧 SDK 头文件里没有这个宏）
const DWORD kSsspWdaExclude = 0x00000011;
//测试用分层窗口的边长
const int kSsspLayeredSide = 16;

//建一个隐藏的分层弹出窗口（与边框窗口同样的扩展样式）
HWND ssspCreateLayered()
{
    const DWORD exStyle = WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE;
    return ::CreateWindowExW(exStyle, L"STATIC", L"sssp5", WS_POPUP, 0, 0, kSsspLayeredSide, kSsspLayeredSide, NULL,
                             NULL, ::GetModuleHandleW(NULL), NULL);
}

//用 UpdateLayeredWindow 给分层窗口一份半透明的内容，不显示窗口
bool ssspPaintLayered(HWND hwnd)
{
    BITMAPINFO bi;
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = kSsspLayeredSide;
    bi.bmiHeader.biHeight = -kSsspLayeredSide;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = NULL;
    HDC screen = ::GetDC(NULL);
    HBITMAP dib = ::CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (dib == NULL)
    {
        ::ReleaseDC(NULL, screen);
        return false;
    }
    memset(bits, 0, static_cast<size_t>(kSsspLayeredSide) * kSsspLayeredSide * 4);
    HDC mem = ::CreateCompatibleDC(screen);
    HGDIOBJ old = ::SelectObject(mem, dib);
    POINT dst = { 0, 0 };
    SIZE size = { kSsspLayeredSide, kSsspLayeredSide };
    POINT src = { 0, 0 };
    BLENDFUNCTION blend = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    const BOOL ok = ::UpdateLayeredWindow(hwnd, screen, &dst, &size, mem, &src, 0, &blend, ULW_ALPHA);
    ::SelectObject(mem, old);
    ::DeleteDC(mem);
    ::DeleteObject(dib);
    ::ReleaseDC(NULL, screen);
    return ok != FALSE;
}

} // namespace

TEST_CASE("SSSP5 边框窗口被排除在屏幕采集之外", "[screenshare]")
{
    uint32_t major = 0;
    uint32_t minor = 0;
    uint32_t build = 0;
    if (!flamingomedia::GetOsVersion(major, minor, build) || !flamingomedia::IsScreenCaptureSupportedBuild(major, minor, build))
    {
        SKIP("WDA_EXCLUDEFROMCAPTURE needs Windows 10 2004 or later");
    }
    //系统行为的对照：逐像素透明（UpdateLayeredWindow）的分层窗口设置排除采集失败，联调时边框窗口就是这样报错 8 的；
    //颜色键分层窗口可以设置。前者只记录、不断言（不同系统版本可能不同），后者是边框窗口改用的做法，必须成功
    HWND ulw = ssspCreateLayered();
    REQUIRE(ulw != NULL);
    ssspPaintLayered(ulw);
    const BOOL ulwOk = ::SetWindowDisplayAffinity(ulw, kSsspWdaExclude);
    WARN("per-pixel layered window: exclude ok=" << (ulwOk ? 1 : 0) << ", error=" << (ulwOk ? 0 : ::GetLastError()));
    ::DestroyWindow(ulw);
    HWND keyed = ssspCreateLayered();
    REQUIRE(keyed != NULL);
    ::SetLayeredWindowAttributes(keyed, RGB(1, 0, 1), 0, LWA_COLORKEY);
    CHECK(ExcludeWindowFromCapture(keyed));
    ::DestroyWindow(keyed);

    //真实的边框窗口：共享窗口时画在外侧（四边各外扩 4 像素），点击穿透，显示亲和性为 WDA_EXCLUDEFROMCAPTURE
    const int kBorderOut = 4;
    const RECT target = { 100, 120, 500, 420 };
    CShareBorderWindow border;
    border.Show(target, false);
    REQUIRE(border.IsWindow());
    DWORD affinity = 0;
    CHECK(::GetWindowDisplayAffinity(border.m_hWnd, &affinity));
    CHECK(affinity == kSsspWdaExclude);
    CHECK((border.GetExStyle() & WS_EX_TRANSPARENT) != 0);
    RECT wr;
    border.GetWindowRect(&wr);
    CHECK(wr.left == target.left - kBorderOut);
    CHECK(wr.top == target.top - kBorderOut);
    CHECK(wr.right == target.right + kBorderOut);
    CHECK(wr.bottom == target.bottom + kBorderOut);
    border.Hide();
    CHECK_FALSE(border.IsWindow());
}
