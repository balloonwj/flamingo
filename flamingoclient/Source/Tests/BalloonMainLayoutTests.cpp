/**
 *  BalloonMainLayoutTests.cpp
 *
 *  P1a 测试：验证 BalloonMainLayout 的种子函数 BuildMainPanelBody() 产出
 *  的 balloonui 控件树符合"控件点名契约" —— 主面板所有需要被对话框代码
 *  引用的控件都能按既定数字 id 找到、类型正确。
 *
 *  覆盖用例（与需求确认一致）：
 *    M1-M6  主面板布局契约
 *    C1     CtrlId enum 自洽
 *
 *  用 Catch2 v3（amalgamated 形态）作测试框架。本工程不创建任何窗口，
 *  纯粹在内存里 build 控件树 + 断言，无需消息循环 / HWND。
 *
 *  全局 _Module 由 BalloonChatLayoutTests.cpp 统一定义，本文件只引用。
 *
 *  balloonwj@qq.com   2026-05-21
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include "../BalloonMain/BalloonMainLayout.h"
#include "../BalloonMain/MainPanelScrollView.h"
#include "../BalloonMain/MainPanelTreeView.h"
#include "../../balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../balloonui/balloonui/Controls/Input/DuiEditHost.h"
#include "../../balloonui/balloonui/Controls/Layout/DuiLayout.h"      // DuiVBox
#include "../../balloonui/balloonui/Controls/Window/DuiScrollBar.h"   // DuiScrollView

using namespace balloonwjui;
using namespace balloonmain;

namespace {

// 递归收集子树里所有<u>非 0</u> 的 ctrlId。0 = 未命名（Header 占位 /
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

} // anonymous namespace

// =====================================================================
// M. 主面板布局契约
// =====================================================================

TEST_CASE("M1 BuildMainPanelBody 返回非空控件树", "[p1a][main]")
{
    auto root = BuildMainPanelBody();
    REQUIRE(root != nullptr);
}

TEST_CASE("M2 主面板含搜索框", "[p1a][main]")
{
    auto root = BuildMainPanelBody();
    REQUIRE(root != nullptr);

    DuiControl* search = root->FindCtrlById(kIdSearchBox);
    REQUIRE(search != nullptr);
    CHECK(dynamic_cast<DuiEditHost*>(search) != nullptr);
}

TEST_CASE("M3 主面板含消息 / 联系人 / 群三个 Tab 按钮", "[p1a][main]")
{
    auto root = BuildMainPanelBody();
    REQUIRE(root != nullptr);

    const UINT tabIds[] = { kIdTabMessages, kIdTabContacts, kIdTabGroups };
    for (UINT id : tabIds)
    {
        INFO("tab button id = " << id);
        auto* tab = dynamic_cast<DuiButton*>(root->FindCtrlById(id));
        REQUIRE(tab != nullptr);
        CHECK_FALSE(tab->GetText().IsEmpty());
    }
}

TEST_CASE("M4 主面板含三个 Tab 内容面板容器", "[p1a][main]")
{
    auto root = BuildMainPanelBody();
    REQUIRE(root != nullptr);

    CHECK(root->FindCtrlById(kIdPaneMessages) != nullptr);
    CHECK(root->FindCtrlById(kIdPaneContacts) != nullptr);
    CHECK(root->FindCtrlById(kIdPaneGroups)   != nullptr);
}

TEST_CASE("M5 主面板含底部主操作按钮", "[p1a][main]")
{
    auto root = BuildMainPanelBody();
    REQUIRE(root != nullptr);

    auto* action = dynamic_cast<DuiButton*>(root->FindCtrlById(kIdBottomAction));
    REQUIRE(action != nullptr);
    CHECK_FALSE(action->GetText().IsEmpty());
}

TEST_CASE("M6 主面板布局所有控件 id 互不重复", "[p1a][main]")
{
    auto root = BuildMainPanelBody();
    REQUIRE(root != nullptr);

    std::vector<UINT> ids;
    CollectIds(root.get(), ids);
    CHECK_FALSE(ids.empty());
    CHECK_FALSE(HasDuplicateId(ids));
}

// 用例 M7 复现的根因 ──
//   单列 DuiTreeView 自己不创建滚动条:DuiTreeView::EnsureScrollRanges 第一行
//   就 if (!IsMultiCol()) return;DuiTreeView::OnPaint 单列路径按
//   m_rcItem.top + r * m_rowH 直接铺行,既不裁剪也不应用滚动偏移。所以行数
//   溢出可视区时,内容会被画到容器(treeview m_rcItem)之外,跑到底部栏覆盖
//   范围内,造成"花屏 + 无滚动"。
//
//   修复(方案 1a):BalloonMainLayout 的 <main-scroll> 标签须映射成 DuiScrollView
//   子类(MainPanelScrollView 或裸 DuiScrollView),由外层提供滚动 + 用
//   IntersectClipRect 裁剪。当前实现把它映射成普通 DuiVBox,所以本用例
//   现阶段会 fail,提醒回归。
TEST_CASE("M7 三个 Tab 内容面板都是 DuiScrollView 子类(单列 DuiTreeView 需外层滚)",
          "[p1a][main][scroll]")
{
    std::unique_ptr<DuiControl> root = BuildMainPanelBody();
    REQUIRE(root != nullptr);

    const UINT paneIds[] = { kIdPaneMessages, kIdPaneContacts, kIdPaneGroups };
    for (size_t i = 0; i < sizeof(paneIds) / sizeof(paneIds[0]); ++i)
    {
        INFO("pane id = " << paneIds[i]);
        DuiControl* pane = root->FindCtrlById(paneIds[i]);
        REQUIRE(pane != nullptr);
        // 单列 DuiTreeView 自身不创建滚动条(EnsureScrollRanges 在
        // !IsMultiCol 时直接 return)且 OnPaint 不裁剪,行数溢出会
        // 画到容器外造成花屏。故 Tab 内容面板必须是 DuiScrollView
        // 子类,提供外层滚动 + IntersectClipRect 硬裁剪。
        CHECK(dynamic_cast<DuiScrollView*>(pane) != nullptr);
    }
}

// 用例 M8 / M9 复现的根因 ──
//   DuiHost::OnMouseWheel 只把滚轮投给"最深命中控件",<u>不冒泡</u>
//   (见 DuiHost.cpp OnMouseWheel 注释);而单列 DuiTreeView::OnMouseWheel
//   直接 return false(见 DuiTreeView.cpp:3439-3456,!IsMultiCol 时
//   直接 return)。三个 Tab 面板里 treeview 又铺满了 scrollview 视口,
//   滚轮命中点几乎总落在 treeview 身上 —— 于是滚轮永远到不了外层
//   MainPanelScrollView,overlay 滚动条没机会显示,treeview 也滚不动。
//
//   修复:业务侧子类化 DuiTreeView -> MainPanelTreeView,覆写 OnMouseWheel
//   让基类未消化时调 ForwardWheelToScrollView 把滚轮交给祖先 scrollview。
//   BalloonMainLayout 把 <treeview> 改成自定义 tag <main-treeview>(内置
//   <treeview> 工厂会先截住,不走 CustomFactory)由 CreateMainCustomControl
//   建子类实例。

TEST_CASE("M8 三个 Tab 面板下的 treeview 都是 MainPanelTreeView 子类(单列上抛滚轮)",
          "[p1a][main][scroll][wheel]")
{
    std::unique_ptr<DuiControl> root = BuildMainPanelBody();
    REQUIRE(root != nullptr);

    struct TreeUnderPane
    {
        UINT treeId;
        UINT paneId;
    };
    const TreeUnderPane kPairs[] =
    {
        { kIdMessagesTree, kIdPaneMessages },
        { kIdContactsTree, kIdPaneContacts },
        { kIdGroupsTree,   kIdPaneGroups   },
    };
    for (size_t i = 0; i < sizeof(kPairs) / sizeof(kPairs[0]); ++i)
    {
        INFO("treeview id = " << kPairs[i].treeId
             << ", under pane = " << kPairs[i].paneId);
        DuiControl* tv = root->FindCtrlById(kPairs[i].treeId);
        REQUIRE(tv != nullptr);
        // 单列 DuiTreeView 基类不上抛滚轮 —— 三个 pane 下必须是
        // MainPanelTreeView 子类(其 OnMouseWheel 会把基类未消化的滚轮
        // 转发给祖先 scrollview)。
        CHECK(dynamic_cast<balloonmain::MainPanelTreeView*>(tv) != nullptr);
    }
}

TEST_CASE("M9 MainPanelTreeView 在父链 scrollview 上时,滚轮触发祖先滚动",
          "[p1a][main][scroll][wheel]")
{
    // 直接搭一棵最小控件树验证行为(不依赖 BuildMainPanelBody):
    //   MainPanelScrollView
    //     └─ DuiVBox(内容)
    //         └─ MainPanelTreeView(单列)
    // 给 scrollview 一个 200x200 的视口 + 1000 的内容高度(必溢出),
    // 然后直接调 treeview->OnMouseWheel 下滚一格;若上抛链路正常,
    // scrollview->GetScrollPos() 必 > 0。
    std::unique_ptr<balloonmain::MainPanelScrollView> sv(
        new balloonmain::MainPanelScrollView());
    std::unique_ptr<balloonwjui::DuiVBox> vbox(new balloonwjui::DuiVBox());

    std::unique_ptr<balloonmain::MainPanelTreeView> tvOwned(
        new balloonmain::MainPanelTreeView());
    balloonmain::MainPanelTreeView* tv = tvOwned.get();
    vbox->AddChild(std::unique_ptr<DuiControl>(tvOwned.release()));
    sv->SetContent(std::unique_ptr<DuiControl>(vbox.release()));

    RECT rcView = { 0, 0, 200, 200 };
    sv->ForceLayout(rcView);
    // 内容高 1000 > 视口 200,scrollbar range 必非零。
    sv->SetContentHeight(1000);

    REQUIRE(sv->GetScrollPos() == 0);
    POINT pt = { 100, 100 };
    // zDelta = -WHEEL_DELTA 表示向下滚一格。
    tv->OnMouseWheel(pt, -WHEEL_DELTA, 0);
    CHECK(sv->GetScrollPos() > 0);
}

// =====================================================================
// C. CtrlId enum 自洽
// =====================================================================

TEST_CASE("C1 主面板 CtrlId 各枚举值互不重复", "[p1a][main][ids]")
{
    const std::vector<UINT> allIds =
    {
        kIdSearchBox,
        kIdTabMessages, kIdTabContacts, kIdTabGroups,
        kIdPaneMessages, kIdPaneContacts, kIdPaneGroups,
        kIdBottomMenu, kIdBottomAction,
    };
    CHECK_FALSE(HasDuplicateId(allIds));
}
