/**
 *  屏幕共享第 17 期（主面板与托盘菜单的「发起屏幕共享」「发起录制」）的单元测试，前缀 SSMN，标签 [sharemenu]。
 *  方案见 docs/屏幕共享与录制方案.md 第 2 节第 42 条、第 8 节第 17 期：
 *    - SSMN1 系统菜单：能共享屏幕时「发起屏幕共享」紧挨在「发起录制」之前（同一组）；不能共享时没有；
 *            录屏不可用时「发起屏幕共享」仍在原组的末尾；
 *    - SSMN2 托盘菜单：能共享屏幕时「打开主面板」之后依次是「发起屏幕共享」「发起录制」；不能共享时没有前者；
 *            未登录与锁定形态都没有；
 *    - SSMN3 选会话窗口的「发起屏幕共享」用途：标题、确认按钮、说明标题与说明文字，只能选一个会话。
 *  纯逻辑，不建窗口。框架 Catch2 v3。
 *
 *  典型用法：FlamingoUnit.exe "[sharemenu]"
 *
 *  balloonwj@qq.com   2026-10-05
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <string>
#include <vector>

#include "../UI/BalloonMain/MainPanelTrayMenu.h"
#include "../UI/BalloonMain/MainPanelSystemMenu.h"
#include "../UI/BalloonMain/MainPanelIcons.h"
#include "../UI/BalloonMain/OnlineCapability.h"
#include "../UI/BalloonMain/ForwardRequest.h"
#include "../UI/BalloonMain/ForwardTargetModel.h"
#include "../net/Protocol.h"

using namespace balloonmain;

namespace {

//托盘菜单测试用的在线状态与连接态：在线、连接就绪
const int kSsmnPresence = presence_online;
const int kSsmnConn = (int)kConnUiOnline;

//在行模型里找某个 id 的下标；找不到返回 items.size()
size_t indexOfTray(const std::vector<TrayMenuEntry>& items, UINT id)
{
    for (size_t i = 0; i < items.size(); ++i)
    {
        if (!items[i].isSeparator && items[i].id == id)
            return i;
    }
    return items.size();
}

//在系统菜单行模型里找某个 id 的下标；找不到返回 items.size()
size_t indexOfSys(const std::vector<SystemMenuEntry>& items, UINT id)
{
    for (size_t i = 0; i < items.size(); ++i)
    {
        if (!items[i].isSeparator && items[i].id == id)
            return i;
    }
    return items.size();
}

} // namespace

TEST_CASE("SSMN1 系统菜单：能共享时「发起屏幕共享」紧挨在「发起录制」之前", "[sharemenu][sysmenu]")
{
    const std::vector<SystemMenuEntry> both =
        BuildSystemMenuModel(true, kRecordMenuStart, NULL, true, true);
    const size_t shareAt = indexOfSys(both, kSysMenuScreenShare);
    const size_t recordAt = indexOfSys(both, kSysMenuRecord);
    REQUIRE(shareAt < both.size());
    REQUIRE(recordAt < both.size());
    CHECK(shareAt + 1 == recordAt);
    CHECK(std::wstring(both[shareAt].label) == L"发起屏幕共享");
    CHECK(both[shareAt].iconKind == kIconScreenShare);
    CHECK(both[shareAt].enabled);
    CHECK(std::wstring(both[recordAt].label) == L"发起录制");
    //同属「发起」组（2026-10-05 起，F8）：前面紧挨「发起视频通话」，「发起录制」之后是分隔线
    CHECK(both[shareAt - 1].id == kSysMenuStartVideoCall);
    REQUIRE(recordAt + 1 < both.size());
    CHECK(both[recordAt + 1].isSeparator);

    //录制中：「停止录制」前面照样是「发起屏幕共享」
    const std::vector<SystemMenuEntry> recording =
        BuildSystemMenuModel(false, kRecordMenuStop, NULL, false, true);
    const size_t stopAt = indexOfSys(recording, kSysMenuRecord);
    REQUIRE(stopAt < recording.size());
    CHECK(std::wstring(recording[stopAt].label) == L"停止录制");
    CHECK(recording[stopAt - 1].id == kSysMenuScreenShare);

    //录屏与通话都不可用、能共享：「发起」组只有这一项，紧跟在「发起」标题之后，之后是分隔线
    const std::vector<SystemMenuEntry> shareOnly =
        BuildSystemMenuModel(false, kRecordMenuHidden, NULL, false, true);
    const size_t onlyAt = indexOfSys(shareOnly, kSysMenuScreenShare);
    REQUIRE(onlyAt < shareOnly.size());
    REQUIRE(shareOnly[onlyAt - 1].isHeader);
    CHECK(std::wstring(shareOnly[onlyAt - 1].label) == L"发起");
    REQUIRE(onlyAt + 1 < shareOnly.size());
    CHECK(shareOnly[onlyAt + 1].isSeparator);

    //不能共享：没有这一项，其余各项不变
    const std::vector<SystemMenuEntry> noShare =
        BuildSystemMenuModel(true, kRecordMenuStart, NULL, true, false);
    CHECK(indexOfSys(noShare, kSysMenuScreenShare) == noShare.size());
    CHECK(noShare.size() + 1 == both.size());

    //命令 id 不与已有各项重复
    CHECK((UINT)kSysMenuScreenShare == 16u);
}

TEST_CASE("SSMN2 托盘菜单：能共享时「打开主面板」之后依次是「发起屏幕共享」「发起录制」", "[sharemenu]")
{
    const std::vector<TrayMenuEntry> both =
        BuildTrayMenuModel(kTrayNormal, false, kSsmnPresence, kSsmnConn, kRecordMenuStart, true);
    const size_t panelAt = indexOfTray(both, kTrayMenuOpenPanel);
    REQUIRE(panelAt + 2 < both.size());
    CHECK(both[panelAt + 1].id == kTrayMenuScreenShare);
    CHECK(std::wstring(both[panelAt + 1].label) == L"发起屏幕共享");
    CHECK(both[panelAt + 1].iconKind == kIconScreenShare);
    CHECK(both[panelAt + 1].enabled);
    CHECK(both[panelAt + 2].id == kTrayMenuRecord);
    CHECK(std::wstring(both[panelAt + 2].label) == L"发起录制");

    //录制中：第二项为「停止录制」，「发起屏幕共享」照常出现
    const std::vector<TrayMenuEntry> recording =
        BuildTrayMenuModel(kTrayNormal, false, kSsmnPresence, kSsmnConn, kRecordMenuStop, true);
    CHECK(std::wstring(recording[indexOfTray(recording, kTrayMenuRecord)].label) == L"停止录制");
    CHECK(indexOfTray(recording, kTrayMenuScreenShare) < recording.size());

    //不能共享（模块没有加载或系统不支持）：没有「发起屏幕共享」，「发起录制」紧跟「打开主面板」
    const std::vector<TrayMenuEntry> noShare =
        BuildTrayMenuModel(kTrayNormal, false, kSsmnPresence, kSsmnConn, kRecordMenuStart, false);
    CHECK(indexOfTray(noShare, kTrayMenuScreenShare) == noShare.size());
    CHECK(noShare[indexOfTray(noShare, kTrayMenuOpenPanel) + 1].id == kTrayMenuRecord);
    CHECK(noShare.size() + 1 == both.size());

    //未登录与锁定形态：都没有
    const std::vector<TrayMenuEntry> before =
        BuildTrayMenuModel(kTrayBeforeLogin, false, kSsmnPresence, kSsmnConn, kRecordMenuStart, true);
    const std::vector<TrayMenuEntry> locked =
        BuildTrayMenuModel(kTrayLocked, false, kSsmnPresence, kSsmnConn, kRecordMenuStart, true);
    CHECK(indexOfTray(before, kTrayMenuScreenShare) == before.size());
    CHECK(indexOfTray(locked, kTrayMenuScreenShare) == locked.size());

    //命令 id 不与已有各项重复
    CHECK((UINT)kTrayMenuScreenShare == 11u);
}

TEST_CASE("SSMN3 选会话窗口的「发起屏幕共享」用途：标题、按钮、说明，只能选一个", "[sharemenu]")
{
    ForwardRequest share;
    share.m_screenShare = true;
    CHECK(ForwardPickerTitle(share) == L"发起屏幕共享");
    CHECK(ForwardPickerConfirmText(share) == L"共享");
    CHECK(ForwardPickerContentTitle(share) == L"说明");
    CHECK(ForwardPickerSingleSelect(share));
    CHECK(BuildForwardPreviewText(share) == L"选择一个联系人或群，向对方共享你的屏幕。");

    //普通转发不受影响
    ForwardRequest forward;
    CHECK(ForwardPickerTitle(forward) == L"转发");
    CHECK(ForwardPickerConfirmText(forward) == L"发送");
    CHECK_FALSE(ForwardPickerSingleSelect(forward));
}
