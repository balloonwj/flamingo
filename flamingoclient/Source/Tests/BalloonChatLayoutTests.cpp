/**
 *  BalloonChatLayoutTests.cpp
 *
 *  P1a 测试：验证 BalloonChatLayout 的两个种子函数
 *  BuildBuddyChatBody() / BuildGroupChatBody() 产出的 balloonui 控件树
 *  符合"控件点名契约" —— 所有需要被对话框代码引用的控件都能按既定
 *  数字 id 找到、类型正确、关键属性（只读 / 占位文字）正确。
 *
 *  覆盖用例（与需求确认一致）：
 *    A1-A6  单聊布局契约
 *    B1-B5  群聊布局契约
 *    C1     CtrlId enum 自洽
 *
 *  用 Catch2 v3（amalgamated 形态）作测试框架。本工程不创建任何窗口，
 *  纯粹在内存里 build 控件树 + 断言，无需消息循环 / HWND。
 *
 *  balloonwj@qq.com   2026.05.21
 */

#include "stdafx.h"

// balloonui 的 ATL 窗口类引用一个全局 _Module。FlamingoUnit 不开窗口，
// 但链接 balloonui_static 时该符号必须有定义 —— 在此提供唯一一份。
CAppModule _Module;

#include "catch_amalgamated.hpp"

#include "../BalloonChat/BalloonChatLayout.h"
#include "../../balloonui/balloonui/Controls/Input/DuiRichEditHost.h"
#include "../../balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../balloonui/balloonui/Controls/List/DuiTreeView.h"

using namespace balloonwjui;
using namespace balloonchat;

namespace {

// 递归收集子树里所有<u>非 0</u> 的 ctrlId。0 = 未命名（分隔线 / 占位
// spacer），不属于"控件点名契约"，跳过。
void CollectIds(const DuiControl* node, std::vector<UINT>& out)
{
    if (node == nullptr)
    {
        return;
    }
    UINT id = node->GetCtrlId();
    if (id != 0)
    {
        out.push_back(id);
    }
    for (const auto& child : node->Children())
    {
        CollectIds(child.get(), out);
    }
}

// 判断一组 id 里是否存在重复值。
bool HasDuplicateId(std::vector<UINT> ids)
{
    std::sort(ids.begin(), ids.end());
    return std::adjacent_find(ids.begin(), ids.end()) != ids.end();
}

// 单聊 / 群聊左半区共有的、必须存在且为按钮的工具栏 + 底部栏 id 全集。
const UINT kCommonButtonIds[] =
{
    kIdTbFile, kIdTbRemoteDesktop,                       // 顶部工具栏
    kIdTbFont, kIdTbEmoji, kIdTbShake, kIdTbImage, kIdTbScreenshot,  // 中部工具栏
    kIdBtnClose, kIdBtnSend,                             // 底部按钮栏
};

} // anonymous namespace

// =====================================================================
// A. 单聊布局契约
// =====================================================================

TEST_CASE("A1 BuildBuddyChatBody 返回非空控件树", "[p1a][buddy]")
{
    auto root = BuildBuddyChatBody();
    REQUIRE(root != nullptr);
}

TEST_CASE("A2 单聊含只读接收 richedit", "[p1a][buddy]")
{
    auto root = BuildBuddyChatBody();
    REQUIRE(root != nullptr);

    DuiControl* recv = root->FindCtrlById(kIdRecvEdit);
    REQUIRE(recv != nullptr);

    auto* re = dynamic_cast<DuiRichEditHost*>(recv);
    REQUIRE(re != nullptr);
    CHECK(re->IsReadOnly());
}

TEST_CASE("A3 单聊含可编辑发送 richedit 且有占位文字", "[p1a][buddy]")
{
    auto root = BuildBuddyChatBody();
    REQUIRE(root != nullptr);

    DuiControl* send = root->FindCtrlById(kIdSendEdit);
    REQUIRE(send != nullptr);

    auto* re = dynamic_cast<DuiRichEditHost*>(send);
    REQUIRE(re != nullptr);
    CHECK_FALSE(re->IsReadOnly());
    CHECK_FALSE(re->GetPlaceholder().IsEmpty());
}

TEST_CASE("A4 单聊含关闭 / 发送按钮", "[p1a][buddy]")
{
    auto root = BuildBuddyChatBody();
    REQUIRE(root != nullptr);

    auto* btnClose = dynamic_cast<DuiButton*>(root->FindCtrlById(kIdBtnClose));
    REQUIRE(btnClose != nullptr);
    CHECK_FALSE(btnClose->GetText().IsEmpty());

    auto* btnSend = dynamic_cast<DuiButton*>(root->FindCtrlById(kIdBtnSend));
    REQUIRE(btnSend != nullptr);
    CHECK_FALSE(btnSend->GetText().IsEmpty());
}

TEST_CASE("A5 单聊含 7 个工具栏按钮", "[p1a][buddy]")
{
    auto root = BuildBuddyChatBody();
    REQUIRE(root != nullptr);

    const UINT toolbarIds[] =
    {
        kIdTbFile, kIdTbRemoteDesktop,
        kIdTbFont, kIdTbEmoji, kIdTbShake, kIdTbImage, kIdTbScreenshot,
    };
    for (UINT id : toolbarIds)
    {
        INFO("toolbar button id = " << id);
        auto* btn = dynamic_cast<DuiButton*>(root->FindCtrlById(id));
        CHECK(btn != nullptr);
    }
}

TEST_CASE("A6 单聊布局所有控件 id 互不重复", "[p1a][buddy]")
{
    auto root = BuildBuddyChatBody();
    REQUIRE(root != nullptr);

    std::vector<UINT> ids;
    CollectIds(root.get(), ids);
    CHECK_FALSE(ids.empty());
    CHECK_FALSE(HasDuplicateId(ids));
}

// =====================================================================
// B. 群聊布局契约
// =====================================================================

TEST_CASE("B1 BuildGroupChatBody 返回非空控件树", "[p1a][group]")
{
    auto root = BuildGroupChatBody();
    REQUIRE(root != nullptr);
}

TEST_CASE("B2 群聊左半区含收 / 发 richedit 与关闭 / 发送按钮", "[p1a][group]")
{
    auto root = BuildGroupChatBody();
    REQUIRE(root != nullptr);

    auto* recv = dynamic_cast<DuiRichEditHost*>(root->FindCtrlById(kIdRecvEdit));
    REQUIRE(recv != nullptr);
    CHECK(recv->IsReadOnly());

    auto* send = dynamic_cast<DuiRichEditHost*>(root->FindCtrlById(kIdSendEdit));
    REQUIRE(send != nullptr);
    CHECK_FALSE(send->IsReadOnly());

    CHECK(dynamic_cast<DuiButton*>(root->FindCtrlById(kIdBtnClose)) != nullptr);
    CHECK(dynamic_cast<DuiButton*>(root->FindCtrlById(kIdBtnSend))  != nullptr);

    // 左半区工具栏按钮也应原样存在。
    for (UINT id : kCommonButtonIds)
    {
        INFO("group left-pane button id = " << id);
        CHECK(dynamic_cast<DuiButton*>(root->FindCtrlById(id)) != nullptr);
    }
}

TEST_CASE("B3 群聊含只读群公告 richedit", "[p1a][group]")
{
    auto root = BuildGroupChatBody();
    REQUIRE(root != nullptr);

    DuiControl* announce = root->FindCtrlById(kIdGroupAnnounce);
    REQUIRE(announce != nullptr);

    auto* re = dynamic_cast<DuiRichEditHost*>(announce);
    REQUIRE(re != nullptr);
    CHECK(re->IsReadOnly());
}

TEST_CASE("B4 群聊含群成员 treeview", "[p1a][group]")
{
    auto root = BuildGroupChatBody();
    REQUIRE(root != nullptr);

    DuiControl* members = root->FindCtrlById(kIdMemberList);
    REQUIRE(members != nullptr);

    auto* tv = dynamic_cast<DuiTreeView*>(members);
    REQUIRE(tv != nullptr);
}

TEST_CASE("B5 群聊布局所有控件 id 互不重复", "[p1a][group]")
{
    auto root = BuildGroupChatBody();
    REQUIRE(root != nullptr);

    std::vector<UINT> ids;
    CollectIds(root.get(), ids);
    CHECK_FALSE(ids.empty());
    CHECK_FALSE(HasDuplicateId(ids));
}

// =====================================================================
// C. CtrlId enum 自洽
// =====================================================================

TEST_CASE("C1 CtrlId 各枚举值互不重复", "[p1a][ids]")
{
    const std::vector<UINT> allIds =
    {
        kIdRecvEdit, kIdSendEdit,
        kIdGroupAnnounce, kIdMemberList, kIdMemberCountLabel,
        kIdHistoryEdit, kIdHistoryPanel, kIdHistorySep,
        kIdTbFile, kIdTbRemoteDesktop,
        kIdTbFont, kIdTbEmoji, kIdTbShake, kIdTbImage, kIdTbScreenshot,
        kIdTbMsgHistory,
        kIdBtnClose, kIdBtnSend, kIdBtnSendArrow,
    };
    CHECK_FALSE(HasDuplicateId(allIds));
}
