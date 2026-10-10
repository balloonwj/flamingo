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

#include "../UI/BalloonMain/TrayFlashController.h"

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

// =====================================================================
// FLASH7 —— 闪烁间隔换算 ClampTrayFlashInterval
//
// 这条是为"本机好好的、换台机器托盘就一次都不闪"那个问题补的。根因在于闪烁
// 定时器的间隔直接取 ::GetCaretBlinkTime()，而用户把系统的"文本光标闪烁"关掉
// 时它返回 INFINITE，原样交给 SetTimer 会被夹成约 24.8 天 —— 定时器永不触发，
// 托盘完全不闪且没有任何报错。该设置每台机器每个用户独立，故症状与机器绑定。
// =====================================================================

TEST_CASE("FLASH7 闪烁间隔换算：异常取值回退、正常取值夹进上下界", "[trayflash]")
{
    SECTION("系统默认的 530ms 落在合理区间内，原样采用")
    {
        CHECK(ClampTrayFlashInterval(530) == 530);
    }

    SECTION("关闭光标闪烁返回 INFINITE —— 必须回退到兜底值，绝不能原样外传")
    {
        CHECK(ClampTrayFlashInterval(INFINITE) == kTrayFlashIntervalDefaultMs);
    }

    SECTION("系统给 0 视为无效值，同样回退到兜底值")
    {
        CHECK(ClampTrayFlashInterval(0) == kTrayFlashIntervalDefaultMs);
    }

    SECTION("偏小的取值抬到下界，避免一秒切十几次图标")
    {
        CHECK(ClampTrayFlashInterval(1) == kTrayFlashIntervalMinMs);
        CHECK(ClampTrayFlashInterval(kTrayFlashIntervalMinMs - 1) == kTrayFlashIntervalMinMs);
    }

    SECTION("偏大的取值压到上界，保证还看得出是在闪")
    {
        CHECK(ClampTrayFlashInterval(10000) == kTrayFlashIntervalMaxMs);
        CHECK(ClampTrayFlashInterval(kTrayFlashIntervalMaxMs + 1) == kTrayFlashIntervalMaxMs);
    }

    SECTION("上下界本身是合法取值，不被改动")
    {
        CHECK(ClampTrayFlashInterval(kTrayFlashIntervalMinMs) == kTrayFlashIntervalMinMs);
        CHECK(ClampTrayFlashInterval(kTrayFlashIntervalMaxMs) == kTrayFlashIntervalMaxMs);
    }

    SECTION("任何输入的结果都落在上下界之内")
    {
        const unsigned int inputs[] = { 0, 1, 99, 100, 530, 2000, 2001, 60000, INFINITE };
        for (size_t i = 0; i < sizeof(inputs) / sizeof(inputs[0]); ++i)
        {
            const unsigned int out = ClampTrayFlashInterval(inputs[i]);
            CHECK(out >= kTrayFlashIntervalMinMs);
            CHECK(out <= kTrayFlashIntervalMaxMs);
        }
    }
}

// =====================================================================
// FLASH8 —— 托盘悬停：有未读时系统提示为空，悬停交给未读消息浮窗（2026-10-03）
// =====================================================================

TEST_CASE("FLASH8 有未读且未读提醒放行时不显示系统提示，读完后恢复", "[trayflash][traytip]")
{
    //悬停时弹不弹未读浮窗看的是「有未读、且未读提醒放行」，系统提示按同一条件取反，两者不会同时出现
    CTrayFlashController ctrl;
    CHECK(ShouldShowTraySystemTip(ctrl, true));

    ctrl.PushUnread(1, 1001, L"张三", L"在吗", L"张", kAnyColor);
    ctrl.PushUnread(2, 2001, L"项目群", L"开会", L"项", kAnyColor);
    CHECK_FALSE(ShouldShowTraySystemTip(ctrl, true));

    //还剩一个未读会话：仍不显示
    ctrl.ClearConversation(1, 1001);
    CHECK_FALSE(ShouldShowTraySystemTip(ctrl, true));

    //全部读完：恢复显示昵称 / 账号 / 状态
    ctrl.ClearConversation(2, 2001);
    CHECK(ShouldShowTraySystemTip(ctrl, true));

    //注销等场合一次清空：同样恢复
    ctrl.PushUnread(1, 1002, L"李四", L"b", L"李", kAnyColor);
    CHECK_FALSE(ShouldShowTraySystemTip(ctrl, true));
    ctrl.Clear();
    CHECK(ShouldShowTraySystemTip(ctrl, true));
}

TEST_CASE("FLASH9 重连期间未读提醒暂停：有未读也显示系统提示", "[trayflash][traytip]")
{
    //重连期间与连上后的等待期里悬停不弹未读浮窗（2026-10-03 用户要求），此时显示系统提示，
    //让用户看得到「正在连接」；未读本身保留，提醒恢复后系统提示再次让位
    CTrayFlashController ctrl;
    ctrl.PushUnread(1, 1001, L"张三", L"在吗", L"张", kAnyColor);
    CHECK(ShouldShowTraySystemTip(ctrl, false));
    CHECK_FALSE(ShouldShowTraySystemTip(ctrl, true));

    //没有未读时放不放行都显示系统提示
    CTrayFlashController empty;
    CHECK(ShouldShowTraySystemTip(empty, false));
    CHECK(ShouldShowTraySystemTip(empty, true));
}
