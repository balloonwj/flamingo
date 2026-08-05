/**
 *  SettingsPageModelTests.cpp
 *
 *  "系统设置"对话框导航纯数据模型的测试：
 *    · 验证 BuildSettingsNavModel() 产出的行模型符合设计稿 —— 条目数、顺序、
 *      文本、页序号与下标一致、本期 implemented 标记。
 *    · 验证 ClampNavSelection() / ComputePageVisibility() 的边界与"同一时刻只
 *      显示一页"契约。
 *
 *  用 Catch2 v3（amalgamated）作框架，纯内存断言，不创建窗口 / 不依赖消息循环。
 *  全局 _Module 由 BalloonChatLayoutTests.cpp 统一定义，本文件只引用。
 *
 *  balloonwj@qq.com   2026-06-03
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <vector>
#include <cwchar>          // wcscmp —— 比对导航项中文文本

#include "../BalloonSettings/SettingsPageModel.h"

using namespace balloonsettings;

// =====================================================================
// NAV. 导航行模型契约（对照 design/系统设置1~7.png）
// =====================================================================

TEST_CASE("NAV1 导航共 7 页", "[settings][nav]")
{
    std::vector<SettingsNavEntry> items = BuildSettingsNavModel();
    REQUIRE(items.size() == (size_t)kPageCount);
    CHECK(kPageCount == 7);
}

TEST_CASE("NAV2 页顺序与文本严格对照设计稿", "[settings][nav]")
{
    std::vector<SettingsNavEntry> items = BuildSettingsNavModel();
    REQUIRE(items.size() == 7);

    CHECK(items[0].id == kPageGeneral);
    CHECK(::wcscmp(items[0].title, L"通用") == 0);
    CHECK(items[1].id == kPageNotify);
    CHECK(::wcscmp(items[1].title, L"消息通知") == 0);
    CHECK(items[2].id == kPageChat);
    CHECK(::wcscmp(items[2].title, L"聊天") == 0);
    CHECK(items[3].id == kPageAudioVideo);
    CHECK(::wcscmp(items[3].title, L"音视频") == 0);
    CHECK(items[4].id == kPageShortcut);
    CHECK(::wcscmp(items[4].title, L"快捷键") == 0);
    CHECK(items[5].id == kPageStorage);
    CHECK(::wcscmp(items[5].title, L"存储") == 0);
    CHECK(items[6].id == kPageAccount);
    CHECK(::wcscmp(items[6].title, L"账号安全") == 0);
}

TEST_CASE("NAV3 页序号等于下标（导航选中索引可直接定位分页）", "[settings][nav]")
{
    std::vector<SettingsNavEntry> items = BuildSettingsNavModel();
    for (int i = 0; i < (int)items.size(); ++i)
    {
        INFO("行 index = " << i);
        CHECK(items[(size_t)i].id == i);
        CHECK(items[(size_t)i].title != NULL);
    }
}

TEST_CASE("NAV4 本期前三页已落地、后四页占位", "[settings][nav]")
{
    std::vector<SettingsNavEntry> items = BuildSettingsNavModel();
    REQUIRE(items.size() == 7);

    CHECK(items[0].implemented);   // 通用
    CHECK(items[1].implemented);   // 消息通知
    CHECK(items[2].implemented);   // 聊天
    CHECK_FALSE(items[3].implemented);   // 音视频
    CHECK_FALSE(items[4].implemented);   // 快捷键
    CHECK_FALSE(items[5].implemented);   // 存储
    CHECK_FALSE(items[6].implemented);   // 账号安全
}

// =====================================================================
// CLAMP. 选中索引夹取
// =====================================================================

TEST_CASE("CLAMP1 合法索引原样返回", "[settings][clamp]")
{
    CHECK(ClampNavSelection(0, 7) == 0);
    CHECK(ClampNavSelection(3, 7) == 3);
    CHECK(ClampNavSelection(6, 7) == 6);
}

TEST_CASE("CLAMP2 越界 / 负数夹到端点", "[settings][clamp]")
{
    CHECK(ClampNavSelection(-1, 7) == 0);
    CHECK(ClampNavSelection(-100, 7) == 0);
    CHECK(ClampNavSelection(7, 7) == 6);    // 等于 count 也算越界
    CHECK(ClampNavSelection(999, 7) == 6);
}

TEST_CASE("CLAMP3 无合法页时返回 -1", "[settings][clamp]")
{
    CHECK(ClampNavSelection(0, 0) == -1);
    CHECK(ClampNavSelection(3, -5) == -1);
}

// =====================================================================
// VIS. 分页可见性（同一时刻只显示一页）
// =====================================================================

TEST_CASE("VIS1 仅选中页可见，其余隐藏", "[settings][visibility]")
{
    std::vector<bool> vis = ComputePageVisibility(2, 7);
    REQUIRE(vis.size() == 7);

    int visibleCount = 0;
    for (int i = 0; i < 7; ++i)
    {
        if (vis[(size_t)i])
        {
            ++visibleCount;
            CHECK(i == 2);   // 唯一可见页必须是选中页
        }
    }
    CHECK(visibleCount == 1);
}

TEST_CASE("VIS2 越界选中先夹取再算可见性", "[settings][visibility]")
{
    std::vector<bool> vis = ComputePageVisibility(999, 7);
    REQUIRE(vis.size() == 7);
    CHECK(vis[6]);   // 夹到末页
    for (int i = 0; i < 6; ++i)
    {
        CHECK_FALSE(vis[(size_t)i]);
    }
}

TEST_CASE("VIS3 页数非正返回空表", "[settings][visibility]")
{
    CHECK(ComputePageVisibility(0, 0).empty());
    CHECK(ComputePageVisibility(0, -3).empty());
}
