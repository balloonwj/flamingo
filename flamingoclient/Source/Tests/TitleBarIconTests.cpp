/**
 *  TitleBarIconTests.cpp
 *
 *  程序图标（UI/BalloonMain/TitleBarIcon 的 ProgramWindowIcons）的测试，标签 [titlebaricon]：
 *    - PWI1：标题栏位图按要求的边长生成、画上了图标，重复调用返回同一块位图；底色铺满图标的透明处
 *      （16 像素那一帧没有透明像素，这一项在 48 像素上检查）；
 *    - PWI2：任务栏 / Alt-Tab 的大、小两枚图标设置到窗口上，重复设置用的是同一组图标；不是有效窗口时不做任何事。
 *
 *  测试程序带了与主程序相同编号的程序图标资源（Tests/FlamingoUnitIcon.rc），所以能在真实图标上验证。
 *
 *  balloonwj@qq.com   2026-10-03
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include "../UI/BalloonMain/TitleBarIcon.h"

namespace {

// 测试用的标题栏底色：一个不会出现在程序图标里的纯色，便于判断哪些像素是底色
const COLORREF kProbeBg = RGB(0x12, 0x34, 0x56);

// 测试窗口的窗口类名
const wchar_t* const kProbeClass = L"FlamingoTitleBarIconProbe";

// 测试窗口的边长（像素），窗口不显示，取值无关紧要
const int kProbeWindowSize = 100;

// 检查底色用的位图边长（像素）。flamingo.ico 的 16、24 两帧没有完全透明的像素（实测），
// 48 那一帧四角有 16 个，所以底色是否铺到透明处只能在这个边长上检查
const int kTransparentProbeSize = 48;

// 统计位图里等于底色与不等于底色的像素数
void CountPixels(HBITMAP bmp, int size, COLORREF bg, int* bgPixels, int* iconPixels)
{
    *bgPixels = 0;
    *iconPixels = 0;
    HDC mem = ::CreateCompatibleDC(NULL);
    HGDIOBJ old = ::SelectObject(mem, bmp);
    for (int y = 0; y < size; ++y)
    {
        for (int x = 0; x < size; ++x)
        {
            if (::GetPixel(mem, x, y) == bg)
                ++*bgPixels;
            else
                ++*iconPixels;
        }
    }
    ::SelectObject(mem, old);
    ::DeleteDC(mem);
}

// 建一个隐藏的普通顶层窗口，用来接收 WM_SETICON
HWND CreateProbeWindow()
{
    WNDCLASSEXW wc;
    ::ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = ::DefWindowProcW;
    wc.hInstance = ::GetModuleHandle(NULL);
    wc.lpszClassName = kProbeClass;
    ::RegisterClassExW(&wc);
    return ::CreateWindowExW(0, kProbeClass, L"probe", WS_OVERLAPPEDWINDOW, 0, 0, kProbeWindowSize, kProbeWindowSize, NULL, NULL,
                             ::GetModuleHandle(NULL), NULL);
}

} // namespace

TEST_CASE("PWI1 标题栏位图的边长、底色与复用", "[titlebaricon]")
{
    balloonmain::ProgramWindowIcons icons;
    HBITMAP bmp = icons.TitleBitmap(balloonmain::kProgramTitleIconSize, kProbeBg);
    REQUIRE(bmp != NULL);
    BITMAP info;
    ::ZeroMemory(&info, sizeof(info));
    REQUIRE(::GetObject(bmp, sizeof(info), &info) == sizeof(info));
    CHECK(info.bmWidth == balloonmain::kProgramTitleIconSize);
    CHECK(info.bmHeight == balloonmain::kProgramTitleIconSize);

    //画上了图标，不是一整块底色
    int bgPixels = 0;
    int iconPixels = 0;
    CountPixels(bmp, balloonmain::kProgramTitleIconSize, kProbeBg, &bgPixels, &iconPixels);
    CHECK(iconPixels > 0);

    //第二次调用返回同一块位图，参数不再起作用
    CHECK(icons.TitleBitmap(balloonmain::kProgramTitleIconSize * 2, RGB(0, 0, 0)) == bmp);

    //图标的透明处显示底色（在有透明像素的边长上检查）
    balloonmain::ProgramWindowIcons large;
    HBITMAP largeBmp = large.TitleBitmap(kTransparentProbeSize, kProbeBg);
    REQUIRE(largeBmp != NULL);
    CountPixels(largeBmp, kTransparentProbeSize, kProbeBg, &bgPixels, &iconPixels);
    CHECK(bgPixels > 0);
    CHECK(iconPixels > 0);
}

TEST_CASE("PWI2 任务栏图标设置到窗口上", "[titlebaricon]")
{
    balloonmain::ProgramWindowIcons icons;
    //不是有效窗口：不做任何事
    icons.ApplyTaskIcons(NULL);

    HWND hwnd = CreateProbeWindow();
    REQUIRE(hwnd != NULL);
    CHECK(::SendMessage(hwnd, WM_GETICON, ICON_BIG, 0) == 0);
    icons.ApplyTaskIcons(hwnd);
    const LRESULT bigIcon = ::SendMessage(hwnd, WM_GETICON, ICON_BIG, 0);
    const LRESULT smallIcon = ::SendMessage(hwnd, WM_GETICON, ICON_SMALL, 0);
    CHECK(bigIcon != 0);
    CHECK(smallIcon != 0);
    CHECK(bigIcon != smallIcon);

    //同一个对象给第二个窗口设置：用的是同一组图标，不重复加载
    HWND second = CreateProbeWindow();
    REQUIRE(second != NULL);
    icons.ApplyTaskIcons(second);
    CHECK(::SendMessage(second, WM_GETICON, ICON_BIG, 0) == bigIcon);
    CHECK(::SendMessage(second, WM_GETICON, ICON_SMALL, 0) == smallIcon);
    ::DestroyWindow(second);
    ::DestroyWindow(hwnd);
}
