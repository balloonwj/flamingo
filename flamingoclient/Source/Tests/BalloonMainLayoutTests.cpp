/**
 *  BalloonMainLayoutTests.cpp
 *
 *  P1a 测试：验证 BalloonMainLayout 的种子函数 BuildMainPanelBody() 产出
 *  的 balloonui 控件树符合"控件点名契约" —— 主面板所有需要被对话框代码
 *  引用的控件都能按既定数字 id 找到、类型正确。
 *
 *  覆盖用例（与需求确认一致）：
 *    M1-M6  主面板布局契约
 *    M10    搜索框是带放大镜与清除叉号的 DuiSearchBox，文字非空时才出现叉号
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

#include "../UI/BalloonMain/BalloonMainLayout.h"
#include "../UI/BalloonMain/MainPanelScrollView.h"
#include "../UI/BalloonMain/MainPanelTreeView.h"
#include "../../../third-party/balloonui/balloonui/DuiHost.h"                    // DispatchMouseWheel（滚轮派发入口）
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiEdit.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiSearchBox.h"   // M10
#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"      // DuiVBox
#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiScrollBar.h"   // DuiScrollView

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
    CHECK(dynamic_cast<DuiEdit*>(search) != nullptr);
}

TEST_CASE("M10 搜索框带放大镜，文字非空时才出现清除叉号", "[p1a][main][globalsearch]")
{
    auto root = BuildMainPanelBody();
    REQUIRE(root != nullptr);
    DuiSearchBox* sb = dynamic_cast<DuiSearchBox*>(root->FindCtrlById(kIdSearchBox));
    REQUIRE(sb != nullptr);
    //左侧放大镜
    CHECK(sb->GetGlyphStripWidth() > 0);
    //空时没有叉号，有字时出现，清空后消失
    CHECK_FALSE(sb->IsClearShowing());
    sb->SetText(_T("设计稿"));
    CHECK(sb->IsClearShowing());
    sb->SetText(_T(""));
    CHECK_FALSE(sb->IsClearShowing());
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

// 用例 M8 / M9 覆盖的链路 ──
//   单列 DuiTreeView::OnMouseWheel 直接 return false(见 DuiTreeView.cpp
//   的 !IsMultiCol 短路),它自己不滚,靠外层 scrollview 滚。三个 Tab 面板里
//   treeview 又铺满了 scrollview 视口,滚轮命中点几乎总落在 treeview 身上,
//   因此滚轮能不能到达外层 MainPanelScrollView 决定了这三个列表滚不滚得动
//   (滚不动的同时,overlay 滚动条也没机会显示)。
//
//   2026-08-14 之前库里的滚轮不冒泡,业务侧靠 MainPanelTreeView 覆写
//   OnMouseWheel 手工转发;之后 DuiHost::DispatchMouseWheel 会沿父链上冒,
//   那段转发代码已删除,改由库承担。M9 走的正是库的派发入口。
//
//   M8 仍然要求这三处是 MainPanelTreeView:该子类如今的职责是行右侧列的
//   绘制与会话行装饰(见 MainPanelTreeView.h),与滚轮无关。

TEST_CASE("M8 三个 Tab 面板下的 treeview 都是 MainPanelTreeView 子类",
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
        // 三个 pane 下必须是 MainPanelTreeView 子类:会话树的时间 / 未读红点 /
        // 置顶与免打扰标记、群树的成员数,都由该子类的 OnPaint 画。
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
    // 然后按产品里滚轮真正的派发路径(DuiHost::DispatchMouseWheel,命中控件
    // 为 treeview)下滚一格;若上冒链路正常,scrollview->GetScrollPos() 必 > 0。
    //
    // 必须走 DispatchMouseWheel 而不是直接调 tv->OnMouseWheel:单列 treeview
    // 自己一律返回 false,直接调它测不出滚轮到底有没有到达外层滚动视口。
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
    CHECK(balloonwjui::DuiHost::DispatchMouseWheel(tv, pt, -WHEEL_DELTA, 0) == true);
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
