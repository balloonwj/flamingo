/**
 *  TrayFlashControllerTests.cpp
 *
 *  托盘"新消息闪头像"状态机 CTrayFlashController 的测试。覆盖与用户确认过的
 *  五条用例：
 *    1) 收到一条单聊未读 → 处于闪烁中，单击目标 = 这条会话。
 *    2) 收到多条不同会话 → 目标 = 最近一条；集合含全部且最近在前。
 *    3) 同一会话连来多条 → 只一条记录、未读累加，且预览更新成最新这条。
 *    4) 打开 / 读完某会话 → 它被移除；若还有别的未读仍在闪、目标切到次新；全清 → 停闪。
 *    5) 单聊（ctype==1）与群聊（ctype==2）用 (ctype,cid) 区分，互不串。
 *  另含边界：非法会话 ID（<=0）被忽略、Clear 全清。
 *
 *  用 Catch2 v3（amalgamated）作框架，纯内存断言，不创建窗口 / 不依赖消息循环。
 *  全局 _Module 由 BalloonChatLayoutTests.cpp 统一定义，本文件只引用。
 *
 *  balloonwj@qq.com   2026-06-04
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <string>

#include "../BalloonMain/TrayFlashController.h"

using namespace balloonmain;

namespace {

// 测试里造一条 push 的小工具：底色不参与逻辑断言，统一给个固定值即可。
const COLORREF kAnyColor = RGB(0xFF, 0x7A, 0x45);

} // anonymous namespace

// =====================================================================
// FLASH1 —— 一条单聊未读：处于闪烁中，单击目标就是它
// =====================================================================

TEST_CASE("FLASH1 一条单聊未读 → 闪烁中且单击目标 = 该会话", "[trayflash]")
{
    CTrayFlashController ctrl;
    CHECK_FALSE(ctrl.IsFlashing());          // 初始不闪
    CHECK(ctrl.GetConversationCount() == 0);

    ctrl.PushUnread(1, 1001, L"张三", L"在吗", L"张", kAnyColor);

    CHECK(ctrl.IsFlashing());
    CHECK(ctrl.GetConversationCount() == 1);

    TrayFlashEntry target;
    REQUIRE(ctrl.GetTarget(target));
    CHECK(target.m_ctype == 1);
    CHECK(target.m_cid == 1001);
    CHECK(target.m_unreadCount == 1);
    CHECK(target.m_displayName == L"张三");
    CHECK(target.m_preview == L"在吗");
    CHECK(target.m_avatarText == L"张");
}

// =====================================================================
// FLASH2 —— 多条不同会话：目标 = 最近一条；集合含全部、最近在前
// =====================================================================

TEST_CASE("FLASH2 多条不同会话 → 目标=最近一条，集合最近在前含全部", "[trayflash]")
{
    CTrayFlashController ctrl;
    ctrl.PushUnread(1, 1001, L"张三", L"a", L"张", kAnyColor);
    ctrl.PushUnread(1, 1002, L"李四", L"b", L"李", kAnyColor);
    ctrl.PushUnread(2, 5000, L"产品群", L"c", L"产", kAnyColor);   // 最后到达 = 最近

    CHECK(ctrl.GetConversationCount() == 3);

    TrayFlashEntry target;
    REQUIRE(ctrl.GetTarget(target));
    CHECK(target.m_ctype == 2);
    CHECK(target.m_cid == 5000);

    // 集合最近在前：下标 0 = 群、1 = 李四、2 = 张三。
    const std::vector<TrayFlashEntry>& entries = ctrl.GetEntries();
    REQUIRE(entries.size() == 3);
    CHECK(entries[0].m_cid == 5000);
    CHECK(entries[1].m_cid == 1002);
    CHECK(entries[2].m_cid == 1001);
}

// =====================================================================
// FLASH3 —— 同一会话连来多条：只一条记录、未读累加、预览更新成最新、并冒到最前
// =====================================================================

TEST_CASE("FLASH3 同一会话连来多条 → 一条记录未读累加、预览取最新、冒到最前", "[trayflash]")
{
    CTrayFlashController ctrl;
    ctrl.PushUnread(1, 1001, L"张三", L"第一条", L"张", kAnyColor);
    ctrl.PushUnread(1, 1002, L"李四", L"x", L"李", kAnyColor);

    // 张三再来两条：仍只占一条记录，未读累计到 3，预览取最后这条，并冒泡成最近一条。
    ctrl.PushUnread(1, 1001, L"张三", L"第二条", L"张", kAnyColor);
    ctrl.PushUnread(1, 1001, L"张三", L"第三条", L"张", kAnyColor);

    CHECK(ctrl.GetConversationCount() == 2);   // 仍是两个会话，不是四条

    TrayFlashEntry target;
    REQUIRE(ctrl.GetTarget(target));
    CHECK(target.m_ctype == 1);
    CHECK(target.m_cid == 1001);
    CHECK(target.m_unreadCount == 3);          // 1 + 1 + 1
    CHECK(target.m_preview == L"第三条");       // 预览取最新这条

    const std::vector<TrayFlashEntry>& entries = ctrl.GetEntries();
    REQUIRE(entries.size() == 2);
    CHECK(entries[0].m_cid == 1001);           // 冒到最前
    CHECK(entries[1].m_cid == 1002);
}

// =====================================================================
// FLASH4 —— 打开 / 读完：移除该会话；剩余仍闪、目标切到次新；全清 → 停闪
// =====================================================================

TEST_CASE("FLASH4 读完最近一条 → 移除、目标切到次新；全清后停闪", "[trayflash]")
{
    CTrayFlashController ctrl;
    ctrl.PushUnread(1, 1001, L"张三", L"a", L"张", kAnyColor);
    ctrl.PushUnread(1, 1002, L"李四", L"b", L"李", kAnyColor);   // 最近 = 李四

    // 读完最近一条（李四）：仍在闪，目标切到次新（张三）。
    bool stillFlashing = ctrl.ClearConversation(1, 1002);
    CHECK(stillFlashing);
    CHECK(ctrl.IsFlashing());
    CHECK(ctrl.GetConversationCount() == 1);

    TrayFlashEntry target;
    REQUIRE(ctrl.GetTarget(target));
    CHECK(target.m_cid == 1001);

    // 读完最后一条：不再闪，目标取不到。
    stillFlashing = ctrl.ClearConversation(1, 1001);
    CHECK_FALSE(stillFlashing);
    CHECK_FALSE(ctrl.IsFlashing());
    CHECK(ctrl.GetConversationCount() == 0);
    CHECK_FALSE(ctrl.GetTarget(target));
}

TEST_CASE("FLASH4b 清除不存在的会话 → 无操作，闪烁状态不变", "[trayflash]")
{
    CTrayFlashController ctrl;
    ctrl.PushUnread(1, 1001, L"张三", L"a", L"张", kAnyColor);

    bool stillFlashing = ctrl.ClearConversation(1, 9999);   // 不存在
    CHECK(stillFlashing);
    CHECK(ctrl.GetConversationCount() == 1);
}

// =====================================================================
// FLASH5 —— 单聊与群聊用 (ctype,cid) 区分：cid 相同但 ctype 不同算两条
// =====================================================================

TEST_CASE("FLASH5 (ctype,cid) 联合区分：cid 相同 ctype 不同算两条会话", "[trayflash]")
{
    CTrayFlashController ctrl;
    ctrl.PushUnread(1, 2000, L"某好友", L"a", L"某", kAnyColor);   // 单聊 cid=2000
    ctrl.PushUnread(2, 2000, L"某群",   L"b", L"某", kAnyColor);   // 群聊 cid=2000

    CHECK(ctrl.GetConversationCount() == 2);

    // 清掉群聊那条，单聊那条应仍在。
    ctrl.ClearConversation(2, 2000);
    CHECK(ctrl.GetConversationCount() == 1);

    TrayFlashEntry target;
    REQUIRE(ctrl.GetTarget(target));
    CHECK(target.m_ctype == 1);
    CHECK(target.m_cid == 2000);
}

// =====================================================================
// FLASH6 —— 边界：非法会话 ID（<=0）被忽略；Clear 全清
// =====================================================================

TEST_CASE("FLASH6 非法会话 ID 被忽略；Clear 全清", "[trayflash]")
{
    CTrayFlashController ctrl;
    ctrl.PushUnread(1, 0,  L"零", L"a", L"零", kAnyColor);    // cid==0 忽略
    ctrl.PushUnread(1, -5, L"负", L"b", L"负", kAnyColor);    // cid<0 忽略
    CHECK_FALSE(ctrl.IsFlashing());
    CHECK(ctrl.GetConversationCount() == 0);

    ctrl.PushUnread(1, 1001, L"张三", L"a", L"张", kAnyColor);
    ctrl.PushUnread(2, 5000, L"产品群", L"b", L"产", kAnyColor);
    CHECK(ctrl.GetConversationCount() == 2);

    ctrl.Clear();
    CHECK_FALSE(ctrl.IsFlashing());
    CHECK(ctrl.GetConversationCount() == 0);
}
