/**
 *  群「仅通知模式」（原称全员禁言）客户端的单测（docs/群管理接入方案.md §14.5），NotifyOnlyTests.cpp
 *
 *  [notifyonly]
 *    NM-C1 群列表（1320）解析出 muteall；老服务端不带该字段时为 0
 *    NM-C2 两种系统提示按模板生成文字：本人、他人、取不到昵称三种写法
 *    NM-C3 能否发言：开关关时谁都能发；开关开时群主、管理员能发、普通成员不能；角色未知时按能发处理
 *    NM-C5 群聊窗输入区：仅通知模式与正常状态之间切换，切回后输入框的矩形与切换前相同
 *    NM-C6 发送被拒的提示：文字与文件被拒时，仅通知模式与单人禁言的提示各不相同
 *  NM-C4（群管理窗那一行的布局）在 GroupManageLayoutTests.cpp。
 *
 *  框架 Catch2 v3。全局 _Module 由 BalloonChatLayoutTests.cpp 统一定义。
 *  balloonwj@qq.com   2026-10-06
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <memory>
#include <string>

#include "../net/GroupProtocolClient.h"
#include "../net/SystemTipText.h"
#include "../net/Protocol.h"
#include "../UI/BalloonChatWnd/GroupNotifyOnly.h"
#include "../UI/BalloonChatWnd/GroupChatWindowLayout.h"
#include "../UI/BalloonChatWnd/ChatSendLimit.h"
#include "../third-party/jsoncpp-1.9.0/json.h"
#include "../../../third-party/balloonui/balloonui/DuiControl.h"

using namespace balloonchatwnd;

namespace {

//本人 userid（NM-C2 用来判断「你」）
const int64_t kSelfId = 100001;

//群聊窗排版用的整窗客户区尺寸（像素）：不小于群聊窗的最小宽度
const int kGroupBodyWidth = 900;
const int kGroupBodyHeight = 700;

//两个矩形是否完全相同
bool SameRect(const RECT& a, const RECT& b)
{
    return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom;
}

} // namespace

TEST_CASE("NM-C1 user group list carries muteall", "[notifyonly]")
{
    const std::string json =
        "{\"code\":0,\"msg\":\"ok\",\"groups\":["
        "{\"groupid\":\"10000000021\",\"name\":\"a\",\"muteall\":1},"
        "{\"groupid\":\"10000000022\",\"name\":\"b\",\"muteall\":0},"
        "{\"groupid\":\"10000000023\",\"name\":\"c\"}]}";
    net::UserGroupsResult out;
    REQUIRE(net::GroupProtocolClient::parseUserGroupsResponse(json, out));
    REQUIRE(out.m_groups.size() == 3);
    CHECK(out.m_groups[0].m_muteall == 1);
    CHECK(out.m_groups[1].m_muteall == 0);
    //老服务端不带这个字段：按关闭处理
    CHECK(out.m_groups[2].m_muteall == 0);
}

TEST_CASE("NM-C2 notify-only system tips are generated from templates", "[notifyonly]")
{
    net::SetSystemTipSelfUserId(kSelfId);

    Json::Value byOther(Json::objectValue);
    byOther["operator"] = "张三";
    byOther["operatorid"] = "100002";
    std::string text;
    REQUIRE(net::FormatSystemTip(kSystemTipMuteAllOn, byOther, text));
    CHECK(text == std::string("「张三」开启了仅通知模式，仅群主和管理员可以发言"));
    REQUIRE(net::FormatSystemTip(kSystemTipMuteAllOff, byOther, text));
    CHECK(text == std::string("「张三」关闭了仅通知模式"));

    //操作人是本人：写「你」
    Json::Value bySelf(Json::objectValue);
    bySelf["operator"] = "我自己";
    bySelf["operatorid"] = "100001";
    REQUIRE(net::FormatSystemTip(kSystemTipMuteAllOn, bySelf, text));
    CHECK(text == std::string("你开启了仅通知模式，仅群主和管理员可以发言"));
    REQUIRE(net::FormatSystemTip(kSystemTipMuteAllOff, bySelf, text));
    CHECK(text == std::string("你关闭了仅通知模式"));

    //取不到昵称与 userid：不带名字的写法
    Json::Value anonymous(Json::objectValue);
    REQUIRE(net::FormatSystemTip(kSystemTipMuteAllOn, anonymous, text));
    CHECK(text == std::string("已开启仅通知模式，仅群主和管理员可以发言"));
    REQUIRE(net::FormatSystemTip(kSystemTipMuteAllOff, anonymous, text));
    CHECK(text == std::string("已关闭仅通知模式"));

    net::SetSystemTipSelfUserId(0);
}

TEST_CASE("NM-C3 who can speak in a notify-only group", "[notifyonly]")
{
    //开关关闭：谁都能发言
    CHECK(CanSpeakInGroup(0, group_role_member));
    CHECK(CanSpeakInGroup(0, group_role_admin));
    CHECK(CanSpeakInGroup(0, kGroupRoleUnknown));
    //开关开启：群主、管理员能发，普通成员不能
    CHECK(CanSpeakInGroup(1, group_role_owner));
    CHECK(CanSpeakInGroup(1, group_role_admin));
    CHECK_FALSE(CanSpeakInGroup(1, group_role_member));
    //角色未知：不在客户端拦截，以服务端判断为准
    CHECK(CanSpeakInGroup(1, kGroupRoleUnknown));
}

TEST_CASE("NM-C5 group chat input area switches between input box and notify-only bar", "[notifyonly]")
{
    std::unique_ptr<balloonwjui::DuiControl> root = BuildGroupChatWindowBody(false);
    REQUIRE(root != nullptr);
    root->Layout(RECT{ 0, 0, kGroupBodyWidth, kGroupBodyHeight });

    balloonwjui::DuiControl* input = root->FindCtrlById(kGIdInputEdit);
    balloonwjui::DuiControl* bar = root->FindCtrlById(kGIdNotifyOnlyBar);
    balloonwjui::DuiControl* toolbar = root->FindCtrlById(kGIdToolbarRow);
    balloonwjui::DuiControl* bottom = root->FindCtrlById(kGIdInputBottomRow);
    REQUIRE(input != nullptr);
    REQUIRE(bar != nullptr);
    REQUIRE(toolbar != nullptr);
    REQUIRE(bottom != nullptr);
    //建好时是正常状态：提示行隐藏
    CHECK_FALSE(bar->IsVisible());
    CHECK(input->IsVisible());
    const RECT inputBefore = input->GetRect();
    const RECT bottomBefore = bottom->GetRect();
    CHECK(inputBefore.bottom > inputBefore.top);

    //进入仅通知模式：输入框、工具栏、底部行隐藏，提示行显示并有面积
    REQUIRE(ApplyNotifyOnlyInput(root.get(), true));
    CHECK_FALSE(input->IsVisible());
    CHECK_FALSE(toolbar->IsVisible());
    CHECK_FALSE(bottom->IsVisible());
    CHECK(bar->IsVisible());
    const RECT rb = bar->GetRect();
    CHECK(rb.bottom - rb.top > 0);
    CHECK(rb.right - rb.left > 0);

    //恢复：输入框的矩形与切换前相同（覆盖 UI 约定 5 的「隐藏后再显示」）
    REQUIRE(ApplyNotifyOnlyInput(root.get(), false));
    CHECK(input->IsVisible());
    CHECK(toolbar->IsVisible());
    CHECK(bottom->IsVisible());
    CHECK_FALSE(bar->IsVisible());
    CHECK(SameRect(input->GetRect(), inputBefore));
    CHECK(SameRect(bottom->GetRect(), bottomBefore));

    //没有根控件时安全返回
    CHECK_FALSE(ApplyNotifyOnlyInput(NULL, true));
}

TEST_CASE("NM-C6 send failure texts distinguish notify-only from a personal mute", "[notifyonly]")
{
    const std::wstring notifyOnly = NotifyOnlyInputHintText();
    //文字消息
    CHECK(SendFailureText(error_code_member_muted, true) == notifyOnly);
    CHECK(SendFailureText(error_code_member_muted, false) != notifyOnly);
    CHECK(SendFailureText(error_code_member_muted) == SendFailureText(error_code_member_muted, false));
    //文件消息：不再只显示错误码
    CHECK(FileSendFailureText(error_code_member_muted, true) == notifyOnly);
    const std::wstring muted = FileSendFailureText(error_code_member_muted, false);
    CHECK(muted != notifyOnly);
    CHECK(muted.find(L"306") == std::wstring::npos);
    //其他错误码不受影响
    CHECK(SendFailureText(error_code_sensitive_word, true) == SendFailureText(error_code_sensitive_word, false));
}
