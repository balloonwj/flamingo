/**
 *  文件共享 DLL 窗口位置保存与恢复的单测（Suite SWP，标签 [shareplacement]），ShareWindowPlacementTests.cpp
 *  覆盖（2026-10-07，「文件共享」窗口默认居中并记住位置与大小）：
 *    - SWP1 配置文件路径由个人目录拼出，个人目录带不带结尾反斜杠结果相同，为空时返回空串
 *    - SWP2 保存后读回，坐标（含负数）、宽高与是否最大化都与保存时相同
 *    - SWP3 文件不存在、宽高缺失或不为正数时按没有保存过处理
 *    - SWP4 没有保存过（m_hasSaved 为假）或路径为空时不写文件
 *    - SWP5 没有保存过时在主显示器的工作区里按默认大小居中；保存的位置在屏幕内时原样恢复
 *  2026-10-07 补（共享浏览窗口也居中并记住位置，多个浏览窗口错开摆放）：
 *    - SWP11 错开摆放 CascadeSharePlacementRect：没有上一个窗口用起点；有时右下错开；超出工作区回到起点；
 *            窗口比工作区大时对齐工作区左上角；错开距离不为正数时与上一个窗口重合；起点超出工作区时夹回
 *    - SWP12 LatestOpenShare：取最近打开、仍开着、不是正在打开的那一个；都不符合时返回 0
 *    - SWP13 浏览窗口的 [ShareBrowser] 与「文件共享」窗口的 [ShareCenter] 互不影响
 *  框架 Catch2 v3。临时文件写在 %TEMP%（按约定运行前已把 TEMP 设到仓库的 tmp\ 下）。
 *  balloonwj@qq.com   2026-10-07
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <windows.h>
#include <stdlib.h>   // abs
#include <set>
#include <string>
#include <vector>

#include "../UI/ShareWindowPlacement.h"

using fileshare::CascadeSharePlacementRect;
using fileshare::kShareBrowserCascadeStep;
using fileshare::kShareBrowserPlacementSection;
using fileshare::kShareCenterPlacementSection;
using fileshare::LatestOpenShare;
using fileshare::SaveSharePlacement;
using fileshare::SharePlacementIniPath;
using fileshare::LoadSharePlacement;
using fileshare::ResolveSharePlacementRect;

namespace {

//临时目录路径缓冲区长度（字符数）
const DWORD kTempPathChars = MAX_PATH;

//本次运行里已经建过的个人目录个数，拼进目录名。GetTickCount 的精度约 15 毫秒，只用进程号与它拼名时，
//同一个时钟间隔里先后运行的两个用例会得到同一个目录，后一个用例看到前一个用例写下的 fileshare_ui.ini
//（2026-10-07 实测：按声明顺序运行时 SWP3 与 SWP4 共用一个目录，SWP4 的「不写文件」断言 20 次里失败 10 次）
unsigned long g_placementDirCount = 0;

//在 %TEMP% 下建一个本次运行独有的个人目录（带结尾反斜杠），并在其下建 ChatLog 子目录
std::wstring FreshPlacementDir()
{
    wchar_t temp[kTempPathChars] = { 0 };
    ::GetTempPathW(kTempPathChars, temp);
    wchar_t name[64] = { 0 };
    ++g_placementDirCount;
    swprintf_s(name, L"swp_%lu_%lu_%lu\\", ::GetCurrentProcessId(), ::GetTickCount(), g_placementDirCount);
    std::wstring dir = std::wstring(temp) + name;
    ::CreateDirectoryW(dir.c_str(), NULL);
    ::CreateDirectoryW((dir + L"ChatLog").c_str(), NULL);
    return dir;
}

bool FileExists(const std::wstring& path)
{
    return ::GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

RECT MakeRect(int left, int top, int width, int height)
{
    RECT rc;
    rc.left = left;
    rc.top = top;
    rc.right = left + width;
    rc.bottom = top + height;
    return rc;
}

} // namespace

TEST_CASE("SWP1 配置文件路径由个人目录拼出", "[shareplacement]")
{
    CHECK(fileshare::SharePlacementIniPath(L"C:\\Users\\a\\") == L"C:\\Users\\a\\ChatLog\\fileshare_ui.ini");
    CHECK(fileshare::SharePlacementIniPath(L"C:\\Users\\a") == L"C:\\Users\\a\\ChatLog\\fileshare_ui.ini");
    CHECK(fileshare::SharePlacementIniPath(L"").empty());
    CHECK(fileshare::SharePlacementIniPath(NULL).empty());
}

TEST_CASE("SWP2 保存后读回与保存时相同，负数坐标也能还原", "[shareplacement]")
{
    const std::wstring ini = fileshare::SharePlacementIniPath(FreshPlacementDir().c_str());
    winplace::WindowPlacementPrefs prefs;
    prefs.m_hasSaved = true;
    //副屏在主屏左边时窗口坐标是负数
    prefs.m_rect = MakeRect(-1500, 120, 1080, 652);
    prefs.m_maximized = true;
    REQUIRE(fileshare::SaveSharePlacement(ini.c_str(), kShareCenterPlacementSection, prefs));
    REQUIRE(FileExists(ini));

    winplace::WindowPlacementPrefs loaded;
    LoadSharePlacement(ini.c_str(), kShareCenterPlacementSection, loaded);
    REQUIRE(loaded.m_hasSaved);
    CHECK(loaded.m_rect.left == -1500);
    CHECK(loaded.m_rect.top == 120);
    CHECK(loaded.m_rect.right - loaded.m_rect.left == 1080);
    CHECK(loaded.m_rect.bottom - loaded.m_rect.top == 652);
    CHECK(loaded.m_maximized);

    //再存一次还原态，覆盖上一次的值
    prefs.m_rect = MakeRect(200, 100, 900, 500);
    prefs.m_maximized = false;
    REQUIRE(fileshare::SaveSharePlacement(ini.c_str(), kShareCenterPlacementSection, prefs));
    LoadSharePlacement(ini.c_str(), kShareCenterPlacementSection, loaded);
    REQUIRE(loaded.m_hasSaved);
    CHECK(loaded.m_rect.left == 200);
    CHECK(loaded.m_rect.right - loaded.m_rect.left == 900);
    CHECK_FALSE(loaded.m_maximized);
}

TEST_CASE("SWP3 文件不存在或宽高无效时按没有保存过处理", "[shareplacement]")
{
    const std::wstring dir = FreshPlacementDir();
    const std::wstring ini = fileshare::SharePlacementIniPath(dir.c_str());
    winplace::WindowPlacementPrefs loaded;
    LoadSharePlacement(ini.c_str(), kShareCenterPlacementSection, loaded);
    CHECK_FALSE(loaded.m_hasSaved);

    //宽度被手工改成 0：同样按没有保存过处理
    ::WritePrivateProfileStringW(kShareCenterPlacementSection, L"X", L"10", ini.c_str());
    ::WritePrivateProfileStringW(kShareCenterPlacementSection, L"Y", L"10", ini.c_str());
    ::WritePrivateProfileStringW(kShareCenterPlacementSection, L"Width", L"0", ini.c_str());
    ::WritePrivateProfileStringW(kShareCenterPlacementSection, L"Height", L"600", ini.c_str());
    LoadSharePlacement(ini.c_str(), kShareCenterPlacementSection, loaded);
    CHECK_FALSE(loaded.m_hasSaved);

    //路径为空
    LoadSharePlacement(L"", kShareCenterPlacementSection, loaded);
    CHECK_FALSE(loaded.m_hasSaved);
}

TEST_CASE("SWP4 没有保存过或路径为空时不写文件", "[shareplacement]")
{
    const std::wstring ini = fileshare::SharePlacementIniPath(FreshPlacementDir().c_str());
    winplace::WindowPlacementPrefs prefs;
    prefs.m_rect = MakeRect(0, 0, 800, 600);
    CHECK_FALSE(fileshare::SaveSharePlacement(ini.c_str(), kShareCenterPlacementSection, prefs));
    CHECK_FALSE(FileExists(ini));

    prefs.m_hasSaved = true;
    CHECK_FALSE(fileshare::SaveSharePlacement(L"", kShareCenterPlacementSection, prefs));
    CHECK_FALSE(fileshare::SaveSharePlacement(NULL, kShareCenterPlacementSection, prefs));
}

TEST_CASE("SWP5 没有保存过时在主显示器工作区里居中；保存的位置在屏幕内时原样恢复", "[shareplacement]")
{
    RECT work;
    REQUIRE(::SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0));
    const int workW = work.right - work.left;
    const int workH = work.bottom - work.top;

    //默认大小比工作区小：居中、大小不变
    SIZE defaultSize;
    defaultSize.cx = workW / 2;
    defaultSize.cy = workH / 2;
    SIZE minSize;
    minSize.cx = workW / 4;
    minSize.cy = workH / 4;
    winplace::WindowPlacementPrefs none;
    const RECT centered = ResolveSharePlacementRect(none, defaultSize, minSize, NULL);
    CHECK(centered.right - centered.left == defaultSize.cx);
    CHECK(centered.bottom - centered.top == defaultSize.cy);
    //左右、上下留白相等（整数除法最多差 1 像素）
    const int leftGap = centered.left - work.left;
    const int rightGap = work.right - centered.right;
    const int topGap = centered.top - work.top;
    const int bottomGap = work.bottom - centered.bottom;
    CHECK(abs(leftGap - rightGap) <= 1);
    CHECK(abs(topGap - bottomGap) <= 1);

    //保存过且完全在工作区里：原样恢复
    winplace::WindowPlacementPrefs saved;
    saved.m_hasSaved = true;
    saved.m_rect = MakeRect(work.left + 10, work.top + 20, defaultSize.cx, defaultSize.cy);
    const RECT restored = ResolveSharePlacementRect(saved, defaultSize, minSize, NULL);
    CHECK(restored.left == saved.m_rect.left);
    CHECK(restored.top == saved.m_rect.top);
    CHECK(restored.right == saved.m_rect.right);
    CHECK(restored.bottom == saved.m_rect.bottom);
}

namespace {

//SWP11 的工作区与窗口尺寸（像素）：工作区 1000×800，窗口 400×300，起点在工作区里居中
const int kSwpWorkW = 1000;
const int kSwpWorkH = 800;
const int kSwpWinW = 400;
const int kSwpWinH = 300;
//SWP11：工作区左上角放在负坐标上（副屏在主屏左边），检查负数坐标照常处理
const int kSwpWorkLeft = -1200;
const int kSwpWorkTop = 100;
//SWP12 用的共享 id
const int64_t kSwpShareA = 101;
const int64_t kSwpShareB = 102;
const int64_t kSwpShareC = 103;
//SWP13 两个小节各自保存的矩形
const int kSwpBrowserX = 30;
const int kSwpBrowserY = 40;
const int kSwpCenterX = 300;
const int kSwpCenterY = 400;

POINT MakePoint(int x, int y)
{
    POINT pt;
    pt.x = x;
    pt.y = y;
    return pt;
}

} // namespace

TEST_CASE("SWP11 浏览窗口错开摆放：右下错开，超出工作区回到起点并夹进工作区", "[shareplacement]")
{
    const RECT work = MakeRect(kSwpWorkLeft, kSwpWorkTop, kSwpWorkW, kSwpWorkH);
    const RECT start = MakeRect(kSwpWorkLeft + (kSwpWorkW - kSwpWinW) / 2, kSwpWorkTop + (kSwpWorkH - kSwpWinH) / 2,
                                kSwpWinW, kSwpWinH);

    //没有上一个窗口：起点
    RECT rc = CascadeSharePlacementRect(start, work, false, MakePoint(0, 0), kShareBrowserCascadeStep);
    CHECK(rc.left == start.left);
    CHECK(rc.top == start.top);
    CHECK(rc.right - rc.left == kSwpWinW);
    CHECK(rc.bottom - rc.top == kSwpWinH);

    //有上一个窗口：相对它右下各错开 kShareBrowserCascadeStep，大小不变
    rc = CascadeSharePlacementRect(start, work, true, MakePoint(start.left, start.top), kShareBrowserCascadeStep);
    CHECK(rc.left == start.left + kShareBrowserCascadeStep);
    CHECK(rc.top == start.top + kShareBrowserCascadeStep);
    CHECK(rc.right - rc.left == kSwpWinW);
    CHECK(rc.bottom - rc.top == kSwpWinH);

    //上一个窗口贴着工作区右下角：错开会超出，回到起点
    const POINT nearCorner = MakePoint(work.right - kSwpWinW, work.bottom - kSwpWinH);
    rc = CascadeSharePlacementRect(start, work, true, nearCorner, kShareBrowserCascadeStep);
    CHECK(rc.left == start.left);
    CHECK(rc.top == start.top);

    //错开距离不为正数：与上一个窗口重合
    rc = CascadeSharePlacementRect(start, work, true, MakePoint(start.left, start.top), 0);
    CHECK(rc.left == start.left);
    CHECK(rc.top == start.top);

    //起点超出工作区右下：夹回工作区
    const RECT outside = MakeRect(work.right - kSwpWinW / 2, work.bottom - kSwpWinH / 2, kSwpWinW, kSwpWinH);
    rc = CascadeSharePlacementRect(outside, work, false, MakePoint(0, 0), kShareBrowserCascadeStep);
    CHECK(rc.right == work.right);
    CHECK(rc.bottom == work.bottom);

    //窗口比工作区还大：左上角对齐工作区左上角
    const RECT huge = MakeRect(work.left + 1, work.top + 1, kSwpWorkW * 2, kSwpWorkH * 2);
    rc = CascadeSharePlacementRect(huge, work, false, MakePoint(0, 0), kShareBrowserCascadeStep);
    CHECK(rc.left == work.left);
    CHECK(rc.top == work.top);
}

TEST_CASE("SWP12 找出最近打开、仍开着的另一个浏览窗口", "[shareplacement]")
{
    std::vector<int64_t> order;
    std::set<int64_t> open;
    //没有打开过
    CHECK(LatestOpenShare(order, open, kSwpShareA) == 0);

    order.push_back(kSwpShareA);
    order.push_back(kSwpShareB);
    order.push_back(kSwpShareA);
    open.insert(kSwpShareA);
    open.insert(kSwpShareB);
    //A 最后一次打开得最晚
    CHECK(LatestOpenShare(order, open, kSwpShareC) == kSwpShareA);
    //正在打开的就是 A：取 B
    CHECK(LatestOpenShare(order, open, kSwpShareA) == kSwpShareB);
    //A 已关闭：取 B
    open.erase(kSwpShareA);
    CHECK(LatestOpenShare(order, open, kSwpShareC) == kSwpShareB);
    //只剩正在打开的那一个
    CHECK(LatestOpenShare(order, open, kSwpShareB) == 0);
}

TEST_CASE("SWP13 浏览窗口与文件共享窗口各存各的位置", "[shareplacement]")
{
    const std::wstring ini = SharePlacementIniPath(FreshPlacementDir().c_str());

    winplace::WindowPlacementPrefs browser;
    browser.m_hasSaved = true;
    browser.m_rect = MakeRect(kSwpBrowserX, kSwpBrowserY, kSwpWinW, kSwpWinH);
    REQUIRE(SaveSharePlacement(ini.c_str(), kShareBrowserPlacementSection, browser));

    //只存过浏览窗口：文件共享窗口仍按没有保存过处理
    winplace::WindowPlacementPrefs center;
    LoadSharePlacement(ini.c_str(), kShareCenterPlacementSection, center);
    CHECK_FALSE(center.m_hasSaved);

    winplace::WindowPlacementPrefs centerSaved;
    centerSaved.m_hasSaved = true;
    centerSaved.m_maximized = true;
    centerSaved.m_rect = MakeRect(kSwpCenterX, kSwpCenterY, kSwpWinW, kSwpWinH);
    REQUIRE(SaveSharePlacement(ini.c_str(), kShareCenterPlacementSection, centerSaved));

    //两个小节各自读回自己的值
    winplace::WindowPlacementPrefs browserBack;
    LoadSharePlacement(ini.c_str(), kShareBrowserPlacementSection, browserBack);
    REQUIRE(browserBack.m_hasSaved);
    CHECK(browserBack.m_rect.left == kSwpBrowserX);
    CHECK(browserBack.m_rect.top == kSwpBrowserY);
    CHECK_FALSE(browserBack.m_maximized);
    winplace::WindowPlacementPrefs centerBack;
    LoadSharePlacement(ini.c_str(), kShareCenterPlacementSection, centerBack);
    REQUIRE(centerBack.m_hasSaved);
    CHECK(centerBack.m_rect.left == kSwpCenterX);
    CHECK(centerBack.m_rect.top == kSwpCenterY);
    CHECK(centerBack.m_maximized);
}
