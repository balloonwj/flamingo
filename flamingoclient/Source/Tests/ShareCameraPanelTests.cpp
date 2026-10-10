/**
 *  摄像头小窗排布的单元测试（Suite SSCAMUI，标签 [sharecamera]），ShareCameraPanelTests.cpp
 *
 *  覆盖 FlamingoMedia/Share/ShareCameraPanel 的 ComputeCameraTileRects（docs/屏幕共享与录制方案.md 第 5 期 5a）：
 *    - SSCAMUI1 放得下时宽度用满（左右各留 8）、16:9、竖向间隔 8；
 *    - SSCAMUI2 放不下时按高度均分、宽度随之缩小并水平居中；底部留给「另有 N 人」那一行的高度不被占用；
 *    - SSCAMUI3 个数为 0 或区域太小时没有小窗；
 *  以及 BuildCameraTiles、ComputeCameraWindowRect（第 5 期 5b）：
 *    - SSCAMUI4 本人在最前，其他人按座位号升序，名字查不到时显示「成员」；
 *    - SSCAMUI5 超过同屏上限时只排前面的、其余计入人数；本人关着摄像头时不占位；
 *    - SSCAMUI6 发起人的摄像头浮动窗口贴在工作区右边缘、竖向居中，高度随人数变化且不超出工作区。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-03
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <map>
#include <set>
#include <vector>

#include "../../FlamingoMedia/Share/ShareCameraPanel.h"

using namespace screenshare;

TEST_CASE("SSCAMUI1 放得下时宽度用满、16:9、间隔 8", "[sharecamera]")
{
    const RECT area = { 100, 50, 340, 1050 };
    std::vector<RECT> rects;
    ComputeCameraTileRects(area, 2, 0, rects);
    REQUIRE(rects.size() == 2);
    CHECK(rects[0].left == 108);
    CHECK(rects[0].right == 332);
    CHECK(rects[0].top == 58);
    CHECK(rects[0].bottom - rects[0].top == 126);
    CHECK(rects[1].top == rects[0].bottom + 8);
    CHECK(rects[1].left == 108);
}

TEST_CASE("SSCAMUI2 放不下时按高度均分、水平居中，底部预留不占用", "[sharecamera]")
{
    const RECT area = { 0, 0, 240, 1000 };
    std::vector<RECT> rects;
    ComputeCameraTileRects(area, 9, 24, rects);
    REQUIRE(rects.size() == 9);
    //可用高度 1000 - 24，9 个小窗加 10 道间隔：每个高 (976 - 80) / 9 = 99，宽 99 * 16 / 9 = 176
    CHECK(rects[0].bottom - rects[0].top == 99);
    CHECK(rects[0].right - rects[0].left == 176);
    CHECK(rects[0].left == (240 - 176) / 2);
    CHECK(rects[8].bottom <= 1000 - 24);
    for (size_t i = 1; i < rects.size(); ++i)
        CHECK(rects[i].top == rects[i - 1].bottom + 8);
}

TEST_CASE("SSCAMUI3 个数为 0 或区域太小时没有小窗", "[sharecamera]")
{
    std::vector<RECT> rects;
    const RECT area = { 0, 0, 240, 1000 };
    ComputeCameraTileRects(area, 0, 0, rects);
    CHECK(rects.empty());
    const RECT tiny = { 0, 0, 10, 10 };
    ComputeCameraTileRects(tiny, 3, 0, rects);
    CHECK(rects.empty());
}

TEST_CASE("SSCAMUI4 本人在最前，其他人按座位号升序", "[sharecamera]")
{
    std::set<int> others;
    others.insert(3);
    others.insert(0);
    others.insert(1);
    std::map<int, std::wstring> names;
    names[0] = L"发起人";
    names[3] = L"王五";
    std::vector<ShareCameraTile> tiles;
    const int overflow = BuildCameraTiles(true, 2, others, names, 9, tiles);
    CHECK(overflow == 0);
    REQUIRE(tiles.size() == 4);
    CHECK(tiles[0].m_self);
    CHECK(tiles[0].m_seat == 2);
    CHECK(tiles[0].m_name == L"我");
    CHECK(tiles[1].m_seat == 0);
    CHECK(tiles[1].m_name == L"发起人");
    CHECK_FALSE(tiles[1].m_self);
    CHECK(tiles[2].m_seat == 1);
    CHECK(tiles[2].m_name == L"成员");
    CHECK(tiles[3].m_seat == 3);
    CHECK(tiles[3].m_name == L"王五");
}

TEST_CASE("SSCAMUI5 超过同屏上限时其余计入人数，本人关着摄像头时不占位", "[sharecamera]")
{
    std::set<int> others;
    for (int seat = 1; seat <= 10; ++seat)
        others.insert(seat);
    std::map<int, std::wstring> names;
    std::vector<ShareCameraTile> tiles;
    CHECK(BuildCameraTiles(true, 0, others, names, 9, tiles) == 2);
    REQUIRE(tiles.size() == 9);
    CHECK(tiles[0].m_self);
    CHECK(tiles[8].m_seat == 8);
    CHECK(BuildCameraTiles(false, 0, others, names, 9, tiles) == 1);
    REQUIRE(tiles.size() == 9);
    CHECK_FALSE(tiles[0].m_self);
    CHECK(tiles[0].m_seat == 1);
    CHECK(BuildCameraTiles(false, 0, std::set<int>(), names, 9, tiles) == 0);
    CHECK(tiles.empty());
}

TEST_CASE("SSCAMUI6 摄像头浮动窗口贴右边缘、竖向居中、不超出工作区", "[sharecamera]")
{
    const RECT work = { 0, 0, 1920, 1040 };
    //两个小窗：高 = 标题栏 28 + 顶部间隔 8 + 2 × (137 + 8)
    RECT rc = ComputeCameraWindowRect(work, 2, 0);
    CHECK(rc.right == 1920 - 12);
    CHECK(rc.right - rc.left == 260);
    CHECK(rc.bottom - rc.top == 326);
    CHECK(rc.top == 12 + (1040 - 24 - 326) / 2);
    //多出的人数占一行 24
    rc = ComputeCameraWindowRect(work, 2, 1);
    CHECK(rc.bottom - rc.top == 350);
    //九个小窗放不下：高度限制在工作区内（上下各留 12）
    const RECT shortWork = { 0, 0, 1280, 800 };
    rc = ComputeCameraWindowRect(shortWork, 9, 0);
    CHECK(rc.top == 12);
    CHECK(rc.bottom == 800 - 12);
    //副显示器：工作区不从 0 开始
    const RECT second = { 1920, 100, 3840, 1180 };
    rc = ComputeCameraWindowRect(second, 1, 0);
    CHECK(rc.right == 3840 - 12);
    CHECK(rc.top >= 100 + 12);
}
