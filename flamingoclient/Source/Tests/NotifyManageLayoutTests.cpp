/**
 *  NotifyManageLayoutTests.cpp
 *
 *  批量管理窗口「消息提醒管理」布局（UI/BalloonNotifyMode/NotifyManageLayout）的单元测试，
 *  标签 [notifymanage][layout]：
 *    NML1  控件点名：页签、搜索框、全选、计数、提示、列表、空状态、下拉框、应用，类型正确、id 不重复
 *    NML2  初始状态：群聊页、下拉框五项并选中允许提醒、空状态可见、应用禁用、截断提示隐藏
 *    NML3  换成单聊页的选项：四项、没有屏蔽；读回目标模式
 *    NML4  列表状态切换：有行 / 没有行、截断提示、已选计数、全选框、应用按钮
 *    NML5  按设计尺寸与最小尺寸排得下：底部「应用」与下拉框不被压扁、不越界
 *    NML6  先按空状态排版、再切到有行，列表区矩形正确，内容高度 = 行数 × 行高 + 上下内边距
 *
 *  用 Catch2 v3（amalgamated）。全局 _Module 由 BalloonChatLayoutTests.cpp 统一定义，本文件只引用。
 *
 *  balloonwj@qq.com   2026-10-03
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <set>
#include <vector>

#include "../UI/BalloonNotifyMode/NotifyManageLayout.h"
#include "../UI/BalloonNotifyMode/NotifyManageModel.h"   // kNotifyManageMaxRows
#include "../UI/BalloonMain/MainPanelScrollView.h"
#include "../UI/BalloonMain/MemberPickerTreeView.h"
#include "../net/Protocol.h"                             // enum conversation_notify_mode
#include "../UI/BalloonMain/NotifyModeMenu.h"            // GetNotifyModeChoices：群聊页下拉框的选项数
#include "LayoutFitCheck.h"                              // layoutfit::MeasureAt
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiEdit.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiComboBox.h"
#include "../../../third-party/balloonui/balloonui/Controls/List/DuiTab.h"

using namespace balloonnotifymode;
using balloonwjui::DuiControl;

namespace {

// 设计尺寸与最小尺寸下的内容区（整窗扣掉标题栏）
const int kContentW = kManageWindowWidth;
const int kContentH = kManageWindowHeight - kManageTitleBarH;
const int kMinContentW = kManageMinWidth;
const int kMinContentH = kManageMinHeight - kManageTitleBarH;

// NML4 / NML6 用的行数与勾选数
const int kSomeRows = 6;
const int kSomeChecked = 2;

// 列表内容的上下内边距之和（逻辑像素），与 NotifyManageLayout.cpp 的 kListPadV * 2 一致
const int kListPadVTotal = 8;

// 收集控件树里全部非 0 的 ctrlId
void CollectIds(DuiControl* c, std::vector<UINT>& ids)
{
    if (c == NULL)
    {
        return;
    }
    if (c->GetCtrlId() != 0)
    {
        ids.push_back(c->GetCtrlId());
    }
    const std::vector<std::unique_ptr<DuiControl>>& children = c->Children();
    for (size_t i = 0; i < children.size(); ++i)
    {
        CollectIds(children[i].get(), ids);
    }
}

// 矩形是否非空
bool HasArea(const RECT& rc)
{
    return rc.right > rc.left && rc.bottom > rc.top;
}

} // anonymous namespace

TEST_CASE("NML1 控件点名齐全且类型正确、id 不重复", "[notifymanage][layout]")
{
    std::unique_ptr<DuiControl> root = BuildNotifyManageBody();
    REQUIRE(root != nullptr);

    balloonwjui::DuiTab* tabs = dynamic_cast<balloonwjui::DuiTab*>(root->FindCtrlById(kIdManageTabs));
    REQUIRE(tabs != nullptr);
    REQUIRE(tabs->GetTabCount() == 2);
    CHECK(tabs->GetTabText(kManageTabGroup) == CString(L"群聊"));
    CHECK(tabs->GetTabText(kManageTabSingle) == CString(L"单聊"));
    CHECK(dynamic_cast<balloonwjui::DuiEdit*>(root->FindCtrlById(kIdManageSearch)) != nullptr);
    CHECK(dynamic_cast<balloonwjui::DuiButton*>(root->FindCtrlById(kIdManageSelectAll)) != nullptr);
    CHECK(dynamic_cast<balloonwjui::DuiLabel*>(root->FindCtrlById(kIdManageCount)) != nullptr);
    CHECK(dynamic_cast<balloonwjui::DuiLabel*>(root->FindCtrlById(kIdManageHint)) != nullptr);
    CHECK(dynamic_cast<balloonmain::MainPanelScrollView*>(root->FindCtrlById(kIdManageScroll)) != nullptr);
    balloonmain::MemberPickerTreeView* tree =
        dynamic_cast<balloonmain::MemberPickerTreeView*>(root->FindCtrlById(kIdManageTree));
    REQUIRE(tree != nullptr);
    CHECK(tree->GetRowHeight() == kManageRowHeight);
    balloonwjui::DuiLabel* empty = dynamic_cast<balloonwjui::DuiLabel*>(root->FindCtrlById(kIdManageEmpty));
    REQUIRE(empty != nullptr);
    CHECK(empty->GetText() == CString(kManageEmptyText));
    CHECK(dynamic_cast<balloonwjui::DuiComboBox*>(root->FindCtrlById(kIdManageTarget)) != nullptr);
    balloonwjui::DuiButton* apply = dynamic_cast<balloonwjui::DuiButton*>(root->FindCtrlById(kIdManageApply));
    REQUIRE(apply != nullptr);
    CHECK(apply->GetText() == CString(L"应用"));

    std::vector<UINT> ids;
    CollectIds(root.get(), ids);
    std::set<UINT> uniq(ids.begin(), ids.end());
    CHECK(uniq.size() == ids.size());
}

TEST_CASE("NML2 初始状态", "[notifymanage][layout]")
{
    std::unique_ptr<DuiControl> root = BuildNotifyManageBody();
    REQUIRE(root != nullptr);

    balloonwjui::DuiTab* tabs = dynamic_cast<balloonwjui::DuiTab*>(root->FindCtrlById(kIdManageTabs));
    REQUIRE(tabs != nullptr);
    CHECK(tabs->GetCurSel() == kManageTabGroup);

    balloonwjui::DuiComboBox* combo = dynamic_cast<balloonwjui::DuiComboBox*>(root->FindCtrlById(kIdManageTarget));
    REQUIRE(combo != nullptr);
    //群聊页六项：2026-10-06 起多了「只提醒 @我和关注的人」（docs/消息提醒模式与消息盒子方案.md §12）
    CHECK(combo->GetCount() == (int)balloonmain::GetNotifyModeChoices(true).size());
    CHECK(combo->GetText() == CString(L"允许消息提醒"));
    CHECK(GetManageTargetMode(root.get(), true) == notify_mode_allow);

    CHECK_FALSE(root->FindCtrlById(kIdManageScroll)->IsVisible());
    CHECK(root->FindCtrlById(kIdManageEmpty)->IsVisible());
    CHECK_FALSE(root->FindCtrlById(kIdManageHint)->IsVisible());
    CHECK_FALSE(root->FindCtrlById(kIdManageApply)->IsEnabled());
}

TEST_CASE("NML3 单聊页的下拉框没有屏蔽，读回的目标模式与选中项一致", "[notifymanage][layout]")
{
    std::unique_ptr<DuiControl> root = BuildNotifyManageBody();
    REQUIRE(root != nullptr);
    SetManageTargetChoices(root.get(), false);

    balloonwjui::DuiComboBox* combo = dynamic_cast<balloonwjui::DuiComboBox*>(root->FindCtrlById(kIdManageTarget));
    REQUIRE(combo != nullptr);
    REQUIRE(combo->GetCount() == 4);
    for (int i = 0; i < combo->GetCount(); ++i)
    {
        CHECK(combo->GetItemText(i) != CString(L"屏蔽消息"));
    }
    CHECK(GetManageTargetMode(root.get(), false) == notify_mode_allow);

    //选第一项：特别关注
    combo->SetCurSel(0, false);
    CHECK(GetManageTargetMode(root.get(), false) == notify_mode_starred);

    //换回群聊页：六项（含屏蔽与只提醒 @我和关注的人），又回到允许提醒
    SetManageTargetChoices(root.get(), true);
    CHECK(combo->GetCount() == (int)balloonmain::GetNotifyModeChoices(true).size());
    CHECK(GetManageTargetMode(root.get(), true) == notify_mode_allow);

    //空指针不崩溃
    SetManageTargetChoices(nullptr, true);
    CHECK(GetManageTargetMode(nullptr, true) == notify_mode_allow);
    ApplyManageListState(nullptr, kSomeRows, false, 0, false);
}

TEST_CASE("NML4 列表状态切换", "[notifymanage][layout]")
{
    std::unique_ptr<DuiControl> root = BuildNotifyManageBody();
    REQUIRE(root != nullptr);

    ApplyManageListState(root.get(), kSomeRows, true, kSomeChecked, false);
    CHECK(root->FindCtrlById(kIdManageScroll)->IsVisible());
    CHECK_FALSE(root->FindCtrlById(kIdManageEmpty)->IsVisible());
    CHECK(root->FindCtrlById(kIdManageHint)->IsVisible());
    CHECK(root->FindCtrlById(kIdManageApply)->IsEnabled());
    balloonwjui::DuiLabel* count = dynamic_cast<balloonwjui::DuiLabel*>(root->FindCtrlById(kIdManageCount));
    REQUIRE(count != nullptr);
    CHECK(count->GetText() == CString(L"已选 2 个"));
    balloonwjui::DuiButton* selectAll = dynamic_cast<balloonwjui::DuiButton*>(root->FindCtrlById(kIdManageSelectAll));
    REQUIRE(selectAll != nullptr);
    CHECK_FALSE(selectAll->IsChecked());

    ApplyManageListState(root.get(), kSomeRows, false, kSomeRows, true);
    CHECK(selectAll->IsChecked());
    CHECK_FALSE(root->FindCtrlById(kIdManageHint)->IsVisible());

    //没有行：空状态，全选框禁用；另一页签还有勾选时「应用」仍可用
    ApplyManageListState(root.get(), 0, false, kSomeChecked, false);
    CHECK_FALSE(root->FindCtrlById(kIdManageScroll)->IsVisible());
    CHECK(root->FindCtrlById(kIdManageEmpty)->IsVisible());
    CHECK_FALSE(selectAll->IsEnabled());
    CHECK_FALSE(selectAll->IsChecked());
    CHECK(root->FindCtrlById(kIdManageApply)->IsEnabled());

    ApplyManageListState(root.get(), 0, false, 0, false);
    CHECK_FALSE(root->FindCtrlById(kIdManageApply)->IsEnabled());
}

TEST_CASE("NML5 按设计尺寸与最小尺寸排得下", "[notifymanage][layout]")
{
    std::unique_ptr<DuiControl> root = BuildNotifyManageBody();
    REQUIRE(root != nullptr);

    const int sizes[2][2] = { { kContentW, kContentH }, { kMinContentW, kMinContentH } };
    for (int k = 0; k < 2; ++k)
    {
        INFO("size index = " << k);
        const int w = sizes[k][0];
        const int h = sizes[k][1];
        const layoutfit::FitResult apply = layoutfit::MeasureAt(root.get(), kIdManageApply, w, h);
        REQUIRE(apply.m_found);
        CHECK(apply.m_height == apply.m_roomyHeight);
        CHECK(apply.m_bottom <= h);
        const RECT rcApply = root->FindCtrlById(kIdManageApply)->GetRect();
        const RECT rcCombo = root->FindCtrlById(kIdManageTarget)->GetRect();
        CHECK(HasArea(rcApply));
        CHECK(HasArea(rcCombo));
        CHECK(rcCombo.right <= rcApply.left);
        CHECK(rcApply.right <= w);

        const layoutfit::FitResult search = layoutfit::MeasureAt(root.get(), kIdManageSearch, w, h);
        REQUIRE(search.m_found);
        CHECK(search.m_height == search.m_roomyHeight);
        const layoutfit::FitResult empty = layoutfit::MeasureAt(root.get(), kIdManageEmpty, w, h);
        REQUIRE(empty.m_found);
        CHECK(empty.m_height > 0);
    }
}

TEST_CASE("NML6 先按空状态排版、再切到有行，列表区矩形与内容高度正确", "[notifymanage][layout]")
{
    std::unique_ptr<DuiControl> root = BuildNotifyManageBody();
    REQUIRE(root != nullptr);
    RECT rc = { 0, 0, kContentW, kContentH };
    root->ForceLayout(rc);
    const RECT rcEmpty = root->FindCtrlById(kIdManageEmpty)->GetRect();
    REQUIRE(HasArea(rcEmpty));

    ApplyManageListState(root.get(), kSomeRows, false, 0, false);
    const RECT rcScroll = root->FindCtrlById(kIdManageScroll)->GetRect();
    CHECK(rcScroll.top == rcEmpty.top);
    CHECK(rcScroll.bottom == rcEmpty.bottom);
    CHECK(rcScroll.left == rcEmpty.left);
    CHECK(rcScroll.right == rcEmpty.right);

    balloonmain::MainPanelScrollView* sv =
        dynamic_cast<balloonmain::MainPanelScrollView*>(root->FindCtrlById(kIdManageScroll));
    REQUIRE(sv != nullptr);
    CHECK(sv->GetContentHeight() == kSomeRows * kManageRowHeight + kListPadVTotal);
}
