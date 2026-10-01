/**
 *  窗口位置的保存与恢复单元测试（HWP1～HWP7），WindowPlacementPrefsTests.cpp
 *
 *  覆盖：没有保存过时的默认位置、原样恢复、显示器拔掉后改为居中、比工作区大时缩小、
 *  比最小尺寸小时放大、一部分在屏幕外时平移回来，以及写进配置文件再读出（含负坐标）。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-01
 */
#include "catch_amalgamated.hpp"
#include <windows.h>
#include <string>
#include <vector>
#include "../Common/Util/IniFile.h"
#include "../Session/WindowPlacementPrefs.h"

using namespace winplace;

namespace {

//测试用的显示器布局：主屏 1920x1080，任务栏在底部占 40 像素
const RECT kPrimaryWork = { 0, 0, 1920, 1040 };
//副屏放在主屏左边，坐标为负；同样 1920x1080、任务栏 40 像素
const RECT kLeftWork = { -1920, 0, 0, 1040 };

//消息记录窗口在 100% 缩放下的默认大小与最小大小（与 BalloonHistoryWindow.cpp 的取值同量级）
const SIZE kDefaultSize = { 880, 690 };
const SIZE kMinSize = { 640, 480 };

RECT MakeRect(int left, int top, int width, int height)
{
    RECT rc = { left, top, left + width, top + height };
    return rc;
}

WindowPlacementPrefs Saved(const RECT& rc, bool maximized)
{
    WindowPlacementPrefs p;
    p.m_hasSaved = true;
    p.m_rect = rc;
    p.m_maximized = maximized;
    return p;
}

bool SameRect(const RECT& a, const RECT& b)
{
    return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom;
}

//每个测试进程一个临时配置文件，文件名带进程 id，两个测试进程同时跑也不会共用
std::wstring TempIniPath()
{
    wchar_t dir[MAX_PATH] = { 0 };
    ::GetTempPathW(MAX_PATH, dir);
    wchar_t name[MAX_PATH] = { 0 };
    swprintf_s(name, L"%sflamingo_winplace_%lu.ini", dir, ::GetCurrentProcessId());
    ::DeleteFileW(name);
    return name;
}

} // namespace

TEST_CASE("HWP1 没有保存过时按默认大小在工作区里居中", "[winplace]")
{
    std::vector<RECT> areas(1, kPrimaryWork);
    const RECT rc = ResolveRestoredWindowRect(WindowPlacementPrefs(), kDefaultSize, kMinSize,
                                              areas, kPrimaryWork);
    CHECK(SameRect(rc, MakeRect((1920 - 880) / 2, (1040 - 690) / 2, 880, 690)));
}

TEST_CASE("HWP2 保存的位置在屏幕内时原样恢复", "[winplace]")
{
    std::vector<RECT> areas;
    areas.push_back(kPrimaryWork);
    areas.push_back(kLeftWork);

    const RECT onPrimary = MakeRect(300, 200, 1000, 700);
    CHECK(SameRect(ResolveRestoredWindowRect(Saved(onPrimary, false), kDefaultSize, kMinSize,
                                             areas, kPrimaryWork), onPrimary));
    //副屏上的负坐标同样原样恢复，不会被挪回主屏
    const RECT onLeft = MakeRect(-1700, 100, 1000, 700);
    CHECK(SameRect(ResolveRestoredWindowRect(Saved(onLeft, true), kDefaultSize, kMinSize,
                                             areas, kPrimaryWork), onLeft));
}

TEST_CASE("HWP3 保存的位置已经不在任何屏幕上时改为默认居中", "[winplace]")
{
    //上次在左边的副屏上，这次副屏拔掉了，只剩主屏
    std::vector<RECT> areas(1, kPrimaryWork);
    const RECT expected = MakeRect((1920 - 880) / 2, (1040 - 690) / 2, 880, 690);
    CHECK(SameRect(ResolveRestoredWindowRect(Saved(MakeRect(-1700, 100, 1000, 700), false),
                                             kDefaultSize, kMinSize, areas, kPrimaryWork), expected));

    //只露出一条比标题栏还窄的边，同样抓不住，按不在屏幕上处理
    const RECT sliver = MakeRect(1920 - (kMinVisibleWidthPx - 1), 100, 1000, 700);
    CHECK(SameRect(ResolveRestoredWindowRect(Saved(sliver, false), kDefaultSize, kMinSize,
                                             areas, kPrimaryWork), expected));
}

TEST_CASE("HWP4 保存的尺寸超过工作区时缩到工作区以内", "[winplace]")
{
    //上次在 4K 屏上开得很大，这次换成了 1080p
    std::vector<RECT> areas(1, kPrimaryWork);
    const RECT rc = ResolveRestoredWindowRect(Saved(MakeRect(100, 50, 3000, 2000), false),
                                              kDefaultSize, kMinSize, areas, kPrimaryWork);
    CHECK(SameRect(rc, kPrimaryWork));
}

TEST_CASE("HWP5 保存的尺寸小于最小尺寸时按最小尺寸恢复", "[winplace]")
{
    std::vector<RECT> areas(1, kPrimaryWork);
    const RECT rc = ResolveRestoredWindowRect(Saved(MakeRect(300, 200, 200, 100), false),
                                              kDefaultSize, kMinSize, areas, kPrimaryWork);
    CHECK(SameRect(rc, MakeRect(300, 200, 640, 480)));
}

TEST_CASE("HWP6 写进配置文件再读出，值不变（含负坐标与最大化）", "[winplace]")
{
    const std::wstring path = TempIniPath();
    CIniFile ini;

    //配置里还没有这一节：读出来是"没有保存过"
    WindowPlacementPrefs empty;
    empty.m_hasSaved = true;
    LoadWindowPlacementPrefs(ini, path.c_str(), L"HistoryDlg", L"HistoryDlg", empty);
    CHECK_FALSE(empty.m_hasSaved);

    const WindowPlacementPrefs written = Saved(MakeRect(-1500, -20, 1100, 860), true);
    SaveWindowPlacementPrefs(ini, path.c_str(), L"HistoryDlg", L"HistoryDlg", written);

    WindowPlacementPrefs read;
    LoadWindowPlacementPrefs(ini, path.c_str(), L"HistoryDlg", L"HistoryDlg", read);
    CHECK(read.m_hasSaved);
    CHECK(SameRect(read.m_rect, written.m_rect));
    CHECK(read.m_maximized);

    ::DeleteFileW(path.c_str());
}

TEST_CASE("HWP7 一部分在屏幕外时整体平移回所在的工作区，尺寸不变", "[winplace]")
{
    std::vector<RECT> areas;
    areas.push_back(kPrimaryWork);
    areas.push_back(kLeftWork);

    //右下角超出主屏：平移到右下角贴边
    CHECK(SameRect(ResolveRestoredWindowRect(Saved(MakeRect(1500, 800, 1000, 700), false),
                                             kDefaultSize, kMinSize, areas, kPrimaryWork),
                   MakeRect(1920 - 1000, 1040 - 700, 1000, 700)));
    //大部分在左边的副屏上：以重叠更多的副屏为准，平移回副屏，而不是挪到主屏
    CHECK(SameRect(ResolveRestoredWindowRect(Saved(MakeRect(-1100, -50, 1000, 700), false),
                                             kDefaultSize, kMinSize, areas, kPrimaryWork),
                   MakeRect(-1100, 0, 1000, 700)));
}
