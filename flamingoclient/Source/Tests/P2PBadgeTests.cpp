/**
 *  P2PBadgeTests.cpp
 *
 *  「P2P」徽章（UI/BalloonMain/P2PBadge）的测试，标签 [p2pbadge]，方案见 docs/单聊文件P2P直传与P2P徽章方案.md 第 4 节：
 *    - P2PB1：只有局域网直连（1）与打洞直连（2）显示徽章，中转（3）、没有链路（0、负数）与未知取值都不显示；
 *    - P2PB2：外框尺寸等于文字宽高加上左右、上下的空白，宽大于高；
 *    - P2PB3：balloonui 窗口用的 DuiBadge 控件按徽章样式配置（文字、颜色、两端半圆、不截断、不因文字为空而隐藏）；
 *    - P2PB4：画到白底上，徽章外框以外的像素不变，左端半圆内是绿色、中部有白色文字像素。
 *
 *  balloonwj@qq.com   2026-10-04
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include "../UI/BalloonMain/P2PBadge.h"
#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"   //P2PB5：徽章放在横向布局里

namespace {

//测试画布的尺寸（像素）与徽章在画布上的左上角
const int kCanvasW = 120;
const int kCanvasH = 40;
const int kBadgeLeft = 10;
const int kBadgeTop = 8;

//测试字体的字号（像素高，负值表示字符高度）
const int kFontHeight = -12;

//判定两个颜色每个分量相差都不超过 tol
bool Near(COLORREF a, COLORREF b, int tol)
{
    return abs((int)GetRValue(a) - (int)GetRValue(b)) <= tol && abs((int)GetGValue(a) - (int)GetGValue(b)) <= tol &&
           abs((int)GetBValue(a) - (int)GetBValue(b)) <= tol;
}

//允许抗锯齿与 ClearType 带来的颜色偏差
const int kColorTol = 40;

HFONT MakeFont()
{
    return ::CreateFontW(kFontHeight, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                         CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, L"Segoe UI");
}

} // namespace

TEST_CASE("P2PB1 只有局域网直连与打洞直连显示徽章", "[p2pbadge]")
{
    CHECK(balloonmain::ShouldShowP2PBadge(1));
    CHECK(balloonmain::ShouldShowP2PBadge(2));
    //经 imgserver 一对一中转：数据经过服务器，不显示（方案决定 4）
    CHECK_FALSE(balloonmain::ShouldShowP2PBadge(3));
    CHECK_FALSE(balloonmain::ShouldShowP2PBadge(0));
    CHECK_FALSE(balloonmain::ShouldShowP2PBadge(-1));
    CHECK_FALSE(balloonmain::ShouldShowP2PBadge(4));
}

TEST_CASE("P2PB2 外框等于文字尺寸加上左右、上下的空白", "[p2pbadge]")
{
    HDC mem = ::CreateCompatibleDC(NULL);
    REQUIRE(mem != NULL);
    HFONT font = MakeFont();
    REQUIRE(font != NULL);
    HGDIOBJ old = ::SelectObject(mem, font);
    SIZE text = { 0, 0 };
    ::GetTextExtentPoint32W(mem, balloonmain::kP2PBadgeText, (int)wcslen(balloonmain::kP2PBadgeText), &text);
    ::SelectObject(mem, old);

    SIZE sz = balloonmain::MeasureP2PBadge(mem, font);
    CHECK(sz.cx == text.cx + 2 * balloonmain::kP2PBadgePadX);
    CHECK(sz.cy == text.cy + 2 * balloonmain::kP2PBadgePadY);
    CHECK(sz.cx > sz.cy);
    //没有设备上下文时返回 0×0
    SIZE none = balloonmain::MeasureP2PBadge(NULL, font);
    CHECK(none.cx == 0);
    CHECK(none.cy == 0);

    ::DeleteObject(font);
    ::DeleteDC(mem);
}

TEST_CASE("P2PB3 balloonui 窗口用的徽章控件：文字、颜色、两端半圆、不截断、不隐藏", "[p2pbadge]")
{
    std::unique_ptr<balloonwjui::DuiBadge> badge = balloonmain::MakeP2PBadgeControl();
    REQUIRE(badge);
    CHECK(badge->GetText() == CString(balloonmain::kP2PBadgeText));
    CHECK(badge->GetBgColor() == balloonmain::kP2PBadgeBgColor);
    CHECK(badge->GetTextColor() == balloonmain::kP2PBadgeTextColor);
    //-1 表示圆角半径取高度的一半，即两端半圆的胶囊
    CHECK(badge->GetCornerRadius() == -1);
    //0 表示不截断
    CHECK(badge->GetMaxDisplayChars() == 0);
    CHECK_FALSE(badge->GetHideWhenEmpty());
    CHECK_FALSE(badge->HasLeadingDot());
    CHECK(balloonmain::kP2PBadgeControlWidth > balloonmain::kP2PBadgeControlHeight);
}

TEST_CASE("P2PB4 画到白底上：外框以外不变，半圆内为绿色，中部有白色文字", "[p2pbadge]")
{
    BITMAPINFO bi;
    ::ZeroMemory(&bi, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = kCanvasW;
    bi.bmiHeader.biHeight = -kCanvasH;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = NULL;
    HBITMAP canvas = ::CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    REQUIRE(canvas != NULL);
    HDC mem = ::CreateCompatibleDC(NULL);
    HGDIOBJ oldBmp = ::SelectObject(mem, canvas);
    RECT all = { 0, 0, kCanvasW, kCanvasH };
    HBRUSH white = ::CreateSolidBrush(RGB(255, 255, 255));
    ::FillRect(mem, &all, white);
    ::DeleteObject(white);

    HFONT font = MakeFont();
    SIZE sz = balloonmain::MeasureP2PBadge(mem, font);
    REQUIRE(sz.cx > 0);
    RECT rc = { kBadgeLeft, kBadgeTop, kBadgeLeft + sz.cx, kBadgeTop + sz.cy };
    balloonmain::DrawP2PBadge(mem, rc, font);

    //外框以外不变
    CHECK(::GetPixel(mem, kBadgeLeft - 2, kBadgeTop + sz.cy / 2) == RGB(255, 255, 255));
    CHECK(::GetPixel(mem, rc.right + 2, kBadgeTop + sz.cy / 2) == RGB(255, 255, 255));
    //外框左上角在半圆之外，仍是白底
    CHECK(::GetPixel(mem, kBadgeLeft, kBadgeTop) == RGB(255, 255, 255));
    //左端半圆内：绿色
    CHECK(Near(::GetPixel(mem, kBadgeLeft + 3, kBadgeTop + sz.cy / 2), balloonmain::kP2PBadgeBgColor, kColorTol));
    //中部一行里至少有一个接近白色的文字像素
    int whiteText = 0;
    for (int x = kBadgeLeft + balloonmain::kP2PBadgePadX; x < rc.right - balloonmain::kP2PBadgePadX; ++x)
    {
        if (Near(::GetPixel(mem, x, kBadgeTop + sz.cy / 2), balloonmain::kP2PBadgeTextColor, kColorTol))
            ++whiteText;
    }
    CHECK(whiteText > 0);

    ::DeleteObject(font);
    ::SelectObject(mem, oldBmp);
    ::DeleteDC(mem);
    ::DeleteObject(canvas);
}

namespace {

//P2PB5：放徽章的横向布局的宽高（像素）
const int kRowW = 200;
const int kRowH = 20;

} // namespace

TEST_CASE("P2PB5 按链路路径显示或隐藏徽章控件：显隐变化时返回 true 并重排父容器，不变时返回 false", "[p2pbadge]")
{
    std::unique_ptr<balloonwjui::DuiHBox> row(new balloonwjui::DuiHBox());
    std::unique_ptr<balloonwjui::DuiBadge> badge = balloonmain::MakeP2PBadgeControl();
    balloonwjui::DuiBadge* b = badge.get();
    row->AddChild(std::move(badge), balloonwjui::DuiLayout::Hint().Fixed(balloonmain::kP2PBadgeControlWidth));
    b->SetVisible(false);
    const RECT rowRc = { 0, 0, kRowW, kRowH };
    row->ForceLayout(rowRc);

    //隐藏 → 局域网直连：显示，并已按固定宽度排好
    CHECK(balloonmain::ApplyP2PBadgeVisibility(b, 1));
    CHECK(b->IsVisible());
    CHECK(b->GetRect().right - b->GetRect().left == balloonmain::kP2PBadgeControlWidth);
    //仍是直连（打洞）：不变
    CHECK_FALSE(balloonmain::ApplyP2PBadgeVisibility(b, 2));
    CHECK(b->IsVisible());
    //改走中转：隐藏
    CHECK(balloonmain::ApplyP2PBadgeVisibility(b, 3));
    CHECK_FALSE(b->IsVisible());
    //没有链路：仍隐藏，不变
    CHECK_FALSE(balloonmain::ApplyP2PBadgeVisibility(b, 0));
    //空指针：什么也不做
    CHECK_FALSE(balloonmain::ApplyP2PBadgeVisibility(NULL, 1));
}

TEST_CASE("P2PB6 画面左上角的叠加徽章：留出边距画在画面里，画面以外不动；画面太小时不画", "[p2pbadge]")
{
    BITMAPINFO bi;
    ::ZeroMemory(&bi, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = kCanvasW;
    bi.bmiHeader.biHeight = -kCanvasH;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = NULL;
    HBITMAP canvas = ::CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    REQUIRE(canvas != NULL);
    HDC mem = ::CreateCompatibleDC(NULL);
    HGDIOBJ oldBmp = ::SelectObject(mem, canvas);
    RECT all = { 0, 0, kCanvasW, kCanvasH };
    HBRUSH white = ::CreateSolidBrush(RGB(255, 255, 255));
    ::FillRect(mem, &all, white);

    //画面区域从 (2, 2) 起：徽章在其左上角留出 kP2PBadgeOverlayMargin 的边距
    const RECT area = { 2, 2, kCanvasW, kCanvasH };
    balloonmain::DrawP2PBadgeOverlay(mem, area);
    const int x0 = area.left + balloonmain::kP2PBadgeOverlayMargin;
    const int y0 = area.top + balloonmain::kP2PBadgeOverlayMargin;
    //边距里仍是白底
    CHECK(::GetPixel(mem, area.left + 1, area.top + 1) == RGB(255, 255, 255));
    //徽章左端半圆内：绿色（取徽章高度的一半处）
    bool green = false;
    for (int dy = 0; dy < kCanvasH - y0 && !green; ++dy)
        green = Near(::GetPixel(mem, x0 + 3, y0 + dy), balloonmain::kP2PBadgeBgColor, kColorTol);
    CHECK(green);

    //画面太小（放不下徽章）：不画
    ::FillRect(mem, &all, white);
    const RECT tiny = { 0, 0, 10, 10 };
    balloonmain::DrawP2PBadgeOverlay(mem, tiny);
    CHECK(::GetPixel(mem, 5, 5) == RGB(255, 255, 255));

    ::DeleteObject(white);
    ::SelectObject(mem, oldBmp);
    ::DeleteDC(mem);
    ::DeleteObject(canvas);
}
