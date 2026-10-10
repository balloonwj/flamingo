/**
 *  ScreenshotActionsTests.cpp
 *
 *  截图浮层操作栏按钮表的测试（docs/截图快捷键与隐藏当前窗口方案.md 第 4 节，用例 SA1~SA4，
 *  2026-10-05 按用户要求自行确认）：从聊天窗发起与不绑定会话两套操作栏的按钮顺序、主按钮、
 *  选区内双击对应的动作。按钮的绘制与点击只能人工核对界面，不在本文件覆盖范围内。
 *
 *  用 Catch2 v3（amalgamated）。全局 _Module 由 BalloonChatLayoutTests.cpp 统一定义，本文件只引用。
 *
 *  balloonwj@qq.com   2026-10-05
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <vector>

#include "../../FlamingoScreenshot/Capture/ScreenshotActions.h"

using namespace balloonscreenshot;

namespace {

// 取一套操作栏的按钮 id（从左到右）
std::vector<UINT> ActionIds(bool hasSendTarget)
{
    int count = 0;
    const ActionItem* items = GetActionItems(hasSendTarget, count);
    std::vector<UINT> ids;
    for (int i = 0; i < count; ++i)
    {
        ids.push_back(items[i].m_id);
    }
    return ids;
}

// 取一套操作栏里主按钮的 id；主按钮不是恰好一个时返回 0
UINT PrimaryId(bool hasSendTarget)
{
    int count = 0;
    const ActionItem* items = GetActionItems(hasSendTarget, count);
    UINT found = 0;
    int primaries = 0;
    for (int i = 0; i < count; ++i)
    {
        if (items[i].m_primary)
        {
            found = items[i].m_id;
            ++primaries;
        }
    }
    return (primaries == 1) ? found : 0;
}

} // anonymous namespace

TEST_CASE("SA1 从聊天窗发起：重选 / 复制 / 保存 / 发送到对话框 / 发送 / 取消", "[screenshot][actions]")
{
    std::vector<UINT> expected;
    expected.push_back(kIdToolReselect);
    expected.push_back(kIdToolCopy);
    expected.push_back(kIdToolSave);
    expected.push_back(kIdToolStage);
    expected.push_back(kIdToolSend);
    expected.push_back(kIdToolCancel);
    CHECK(ActionIds(true) == expected);
}

TEST_CASE("SA2 不绑定会话：重选 / 保存 / 取消 / 完成，没有复制与两种发送", "[screenshot][actions]")
{
    std::vector<UINT> expected;
    expected.push_back(kIdToolReselect);
    expected.push_back(kIdToolSave);
    expected.push_back(kIdToolCancel);
    expected.push_back(kIdToolDone);
    CHECK(ActionIds(false) == expected);
}

TEST_CASE("SA3 两套操作栏各有且只有一个主按钮：发送到对话框 / 完成，且主按钮前留分组间距", "[screenshot][actions]")
{
    CHECK(PrimaryId(true) == (UINT)kIdToolStage);
    CHECK(PrimaryId(false) == (UINT)kIdToolDone);

    for (int mode = 0; mode < 2; ++mode)
    {
        bool hasSendTarget = (mode == 1);
        int count = 0;
        const ActionItem* items = GetActionItems(hasSendTarget, count);
        for (int i = 0; i < count; ++i)
        {
            INFO("mode=" << mode << " index=" << i);
            CHECK(items[i].m_groupGapBefore == items[i].m_primary);
            CHECK(items[i].m_width > 0);
            CHECK(items[i].m_label != NULL);
            CHECK(items[i].m_tip != NULL);
        }
    }
}

TEST_CASE("SA4 选区内双击：绑定会话时等于发送到对话框，不绑定会话时等于完成", "[screenshot][actions]")
{
    CHECK(DoubleClickActionId(true) == (UINT)kIdToolStage);
    CHECK(DoubleClickActionId(false) == (UINT)kIdToolDone);
}
