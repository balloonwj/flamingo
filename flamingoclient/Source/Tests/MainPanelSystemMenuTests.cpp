/**
 *  MainPanelSystemMenuTests.cpp
 *
 *  主面板系统菜单（左下角汉堡按钮弹出菜单）的测试：
 *    · 验证纯函数 BuildSystemMenuModel() 产出的行模型符合设计稿 —— 条目数、
 *      顺序、文本、分隔线位置、命令 id 的唯一性与非 0 约束、图标 / 可用态。
 *    · 验证 balloonui 新增的 DuiMenu::MeasureSize() —— 弹出前取尺寸用于"向上
 *      弹"定位，高度须随行数线性、分隔条比文本行更矮、空菜单为 {0,0}。
 *
 *  用 Catch2 v3（amalgamated）作框架，纯内存断言，不创建窗口 / 不依赖消息循环。
 *  全局 _Module 由 BalloonChatLayoutTests.cpp 统一定义，本文件只引用。
 *
 *  balloonwj@qq.com   2026-06-03
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <algorithm>
#include <vector>
#include <cwchar>          // wcscmp —— 比对菜单项中文文本

#include "../BalloonMain/MainPanelSystemMenu.h"
#include "../BalloonMain/MainPanelIcons.h"   // IconKind（断言各项图标）
#include "../../../third-party/balloonui/balloonui/Controls/List/DuiMenu.h"

using namespace balloonmain;

// =====================================================================
// SYS. 系统菜单行模型契约（对照 design/主面板系统菜单.png）
// =====================================================================

TEST_CASE("SYS1 系统菜单共 7 个功能项 + 4 条分隔线 = 11 行", "[sysmenu]")
{
    // 项数的来历：设计稿原有 7 项，之后追加了"登出"（末尾单独一组，前置一条分隔线）
    // 变成 8 项，再之后「群发助手」下线又回到 7 项。功能项与总行数是一起变的，改动
    // 时两个数都要跟上 —— 此前就出现过总行数已改到 11、功能项却还写着 8 的自相矛盾。
    std::vector<SystemMenuEntry> items = BuildSystemMenuModel();
    REQUIRE(items.size() == 11);

    int seps = 0;
    int funcs = 0;
    for (size_t i = 0; i < items.size(); ++i)
    {
        if (items[i].isSeparator)
        {
            ++seps;
        }
        else
        {
            ++funcs;
        }
    }
    CHECK(seps == 4);
    CHECK(funcs == 7);
    // 分隔线 + 功能项必须正好铺满全部行，不允许出现第三种行类型悄悄混进来。
    CHECK(seps + funcs == (int)items.size());
}

TEST_CASE("SYS2 行顺序、文本、分隔线位置严格对照设计稿", "[sysmenu]")
{
    std::vector<SystemMenuEntry> items = BuildSystemMenuModel();
    // 「群发助手」功能已下线，该项连同其命令 id 一并移除，故这里比原先少一项
    REQUIRE(items.size() == 11);

    CHECK(items[0].id == kSysMenuProfile);
    CHECK(::wcscmp(items[0].label, L"个人信息") == 0);
    CHECK(items[1].id == kSysMenuChangePwd);
    CHECK(::wcscmp(items[1].label, L"修改密码") == 0);

    CHECK(items[2].isSeparator);

    CHECK(items[3].id == kSysMenuAnnounce);
    CHECK(::wcscmp(items[3].label, L"公告与通知") == 0);

    CHECK(items[4].isSeparator);

    CHECK(items[5].id == kSysMenuSettings);
    CHECK(::wcscmp(items[5].label, L"系统设置") == 0);
    CHECK(items[6].id == kSysMenuNetSettings);
    CHECK(::wcscmp(items[6].label, L"网络设置") == 0);

    CHECK(items[7].isSeparator);

    CHECK(items[8].id == kSysMenuLogout);
    CHECK(::wcscmp(items[8].label, L"登出") == 0);

    CHECK(items[9].isSeparator);

    CHECK(items[10].id == kSysMenuAbout);
    CHECK(::wcscmp(items[10].label, L"关于 FlamingoIM") == 0);
}

TEST_CASE("SYS3 功能项命令 id 非 0 且互不重复", "[sysmenu]")
{
    std::vector<SystemMenuEntry> items = BuildSystemMenuModel();

    std::vector<UINT> ids;
    for (size_t i = 0; i < items.size(); ++i)
    {
        if (items[i].isSeparator)
        {
            continue;
        }
        INFO("功能项 index = " << i);
        CHECK(items[i].id != 0);   // 0 是 TrackPopup 的"未选中"返回值，不能占用
        ids.push_back(items[i].id);
    }
    //「群发助手」下线后功能项少一个
    REQUIRE(ids.size() == 7);

    std::sort(ids.begin(), ids.end());
    CHECK(std::adjacent_find(ids.begin(), ids.end()) == ids.end());
}

TEST_CASE("SYS5 登出项排在关于之上，且两侧都被分隔线隔开单独成组", "[sysmenu][logout]")
{
    // 登出是"结束当前会话"类操作，必须与相邻的设置 / 关于等查看类操作用分隔线隔开
    // 单独成组，避免误点。本用例锁定这一位置契约：一旦有人挪动它、把它并进相邻分组，
    // 或删掉任一侧的分隔线，都会失败。
    std::vector<SystemMenuEntry> items = BuildSystemMenuModel();

    int logoutIdx = -1;
    int aboutIdx  = -1;
    for (size_t i = 0; i < items.size(); ++i)
    {
        if (items[i].isSeparator)
        {
            continue;
        }
        if (items[i].id == kSysMenuLogout)
        {
            logoutIdx = (int)i;
        }
        if (items[i].id == kSysMenuAbout)
        {
            aboutIdx = (int)i;
        }
    }
    REQUIRE(logoutIdx > 0);
    REQUIRE(aboutIdx > 0);

    CHECK(::wcscmp(items[logoutIdx].label, L"登出") == 0);
    CHECK(items[logoutIdx].enabled);

    // 登出在关于之上，两者之间恰好隔着一条分隔线。
    CHECK(logoutIdx < aboutIdx);
    CHECK(aboutIdx == logoutIdx + 2);
    CHECK(items[logoutIdx + 1].isSeparator);

    // 登出上方也必须有一条分隔线（与"系统设置 / 网络设置"那组隔开）。
    CHECK(items[logoutIdx - 1].isSeparator);
}

TEST_CASE("SYS6 登出项的图标与其它各项都不相同", "[sysmenu][logout]")
{
    // 登出用独立图标(kIconLogout)：与托盘菜单里紧挨着的"退出"(kIconExit)在视觉上必须
    // 能区分，也不能和菜单内其它项撞图标。
    std::vector<SystemMenuEntry> items = BuildSystemMenuModel();

    int logoutIcon = -1;
    for (size_t i = 0; i < items.size(); ++i)
    {
        if (!items[i].isSeparator && items[i].id == kSysMenuLogout)
        {
            logoutIcon = items[i].iconKind;
        }
    }
    REQUIRE(logoutIcon == kIconLogout);
    CHECK(logoutIcon != kIconExit);

    for (size_t i = 0; i < items.size(); ++i)
    {
        if (items[i].isSeparator || items[i].id == kSysMenuLogout)
        {
            continue;
        }
        INFO("功能项 index = " << i);
        CHECK(items[i].iconKind != logoutIcon);
    }
}

TEST_CASE("SYS4 功能项都带文本 + 图标 + 可用；分隔条无文本无图标", "[sysmenu]")
{
    std::vector<SystemMenuEntry> items = BuildSystemMenuModel();

    for (size_t i = 0; i < items.size(); ++i)
    {
        INFO("行 index = " << i);
        if (items[i].isSeparator)
        {
            CHECK(items[i].label == NULL);
            CHECK(items[i].iconKind < 0);
        }
        else
        {
            CHECK(items[i].label != NULL);
            CHECK(items[i].iconKind >= 0);
            CHECK(items[i].enabled);   // 当前 7 项均可用（无灰显项）
        }
    }
}

// =====================================================================
// MS. DuiMenu::MeasureSize() —— "弹出前取尺寸"契约
// =====================================================================

TEST_CASE("MS1 空菜单 MeasureSize 返回 {0,0}", "[sysmenu][measuresize]")
{
    balloonwjui::DuiMenu menu;
    SIZE sz = menu.MeasureSize();
    CHECK(sz.cx == 0);
    CHECK(sz.cy == 0);
}

TEST_CASE("MS2 菜单高度随行数线性，分隔条比文本行矮", "[sysmenu][measuresize]")
{
    // 用"增量法"反推单行高 / 单分隔高，避免在测试里硬编码 balloonui 内部常量。
    balloonwjui::DuiMenu m1;
    m1.AppendItem(1, L"甲");

    balloonwjui::DuiMenu m2;
    m2.AppendItem(1, L"甲");
    m2.AppendItem(2, L"乙");

    balloonwjui::DuiMenu m3;
    m3.AppendItem(1, L"甲");
    m3.AppendSeparator();
    m3.AppendItem(2, L"乙");

    long rowH = m2.MeasureSize().cy - m1.MeasureSize().cy;   // 一个文本行的高度
    long sepH = m3.MeasureSize().cy - m2.MeasureSize().cy;   // 一条分隔线的高度

    CHECK(rowH > 0);
    CHECK(sepH > 0);
    CHECK(sepH < rowH);                              // 分隔条比文本行矮
    CHECK(m1.MeasureSize().cy == rowH);              // 单项菜单高 = 一个文本行
    CHECK(m3.MeasureSize().cy == 2 * rowH + sepH);   // 2 文本 + 1 分隔
    CHECK(m1.MeasureSize().cx > 0);                  // 宽度恒为正
}
