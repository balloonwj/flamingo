/**
 *  聊天窗录制按钮下拉菜单 ChatRecordMenu 的单元测试（标签 [recordmenu]），ChatRecordMenuTests.cpp
 *
 *  覆盖 docs/语音字幕方案.md 第 2 节第 10 条（录像列表的入口之一）：
 *    CRM1 —— 菜单内容：没在录制时第一项为「开始录制…」、正在录制时为「停止录制」；录制管理可用时多一项「录制管理」；
 *            重新搭建时先清空；菜单 id 与选择结果的换算，不认识的 id 当作没有选。
 *  框架 Catch2 v3。
 *
 *  balloonwj@qq.com   2026-10-06
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <vector>

#include "../UI/BalloonChatWnd/ChatRecordMenu.h"
#include "../../../third-party/balloonui/balloonui/Controls/List/DuiMenu.h"

TEST_CASE("CRM1 录制菜单：开始 / 停止录制随状态切换，录制管理可用时多一项；id 与选择的换算", "[recordmenu]")
{
    balloonwjui::DuiMenu menu;
    balloonchatwnd::BuildRecordMenu(menu, false, true);
    const std::vector<balloonwjui::DuiMenu::Item>& items = menu.GetItems();
    REQUIRE(items.size() == 2);
    CHECK(items[0].id == balloonchatwnd::kRecordMenuIdStart);
    CHECK(items[0].text == CString(L"开始录制…"));
    CHECK(items[1].id == balloonchatwnd::kRecordMenuIdManager);
    CHECK(items[1].text == CString(L"录制管理"));

    //正在录制、录制管理不可用：只有「停止录制」
    balloonchatwnd::BuildRecordMenu(menu, true, false);
    REQUIRE(menu.GetItems().size() == 1);
    CHECK(menu.GetItems()[0].id == balloonchatwnd::kRecordMenuIdStop);
    CHECK(menu.GetItems()[0].text == CString(L"停止录制"));

    CHECK(balloonchatwnd::RecordMenuChoiceFromId(balloonchatwnd::kRecordMenuIdStart) == balloonchatwnd::kRecordMenuStart);
    CHECK(balloonchatwnd::RecordMenuChoiceFromId(balloonchatwnd::kRecordMenuIdStop) == balloonchatwnd::kRecordMenuStop);
    CHECK(balloonchatwnd::RecordMenuChoiceFromId(balloonchatwnd::kRecordMenuIdManager) ==
          balloonchatwnd::kRecordMenuManager);
    CHECK(balloonchatwnd::RecordMenuChoiceFromId(0) == balloonchatwnd::kRecordMenuNone);
    CHECK(balloonchatwnd::RecordMenuChoiceFromId(99) == balloonchatwnd::kRecordMenuNone);
}
