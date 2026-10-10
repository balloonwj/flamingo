/**
 *  MsgBoxLayoutTests.cpp
 *
 *  「消息盒子」窗口布局（UI/BalloonMsgBox/MsgBoxLayout）的单元测试，标签 [msgbox][layout]：
 *    MBL1  控件点名：两个按钮、滚动视口、列表、空状态文字，类型正确、id 互不重复
 *    MBL2  初始为空状态：列表隐藏、空状态文字可见、「全部标为已读」禁用
 *    MBL3  有会话 / 没有会话之间来回切换，「全部标为已读」随未读启用与禁用
 *    MBL4  按设计尺寸排得下：顶部按钮不被压扁，列表区占满按钮行以下的空间
 *    MBL5  先空状态排版、再切到有会话（"隐藏后再显示"的路径），列表区矩形正确
 *    MBL6  滚动视口的内容高度 = 行数 × 行高 + 上下内边距
 *
 *  用 Catch2 v3（amalgamated）。全局 _Module 由 BalloonChatLayoutTests.cpp 统一定义，本文件只引用。
 *
 *  balloonwj@qq.com   2026-10-03
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <set>
#include <vector>

#include "../UI/BalloonMsgBox/MsgBoxLayout.h"
#include "../UI/BalloonMain/MainPanelScrollView.h"
#include "../UI/BalloonMain/MainPanelTreeView.h"
#include "LayoutFitCheck.h"   // layoutfit::MeasureAt：按设计尺寸排得下的检查
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"

using namespace balloonmsgbox;
using balloonwjui::DuiControl;

namespace {

// 设计尺寸下的内容区（整窗扣掉标题栏）
const int kContentW = kMsgBoxWindowWidth;
const int kContentH = kMsgBoxWindowHeight - kMsgBoxTitleBarH;

// MBL3 / MBL5 / MBL6 用的会话数与未读数
const int kSomeConvs  = 3;
const int kSomeUnread = 5;

// 列表内容的上下内边距之和（逻辑像素），与 MsgBoxLayout.cpp 的 kListPadV * 2 一致
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

TEST_CASE("MBL1 控件点名：按钮、滚动视口、列表、空状态文字齐全且类型正确", "[msgbox][layout]")
{
    std::unique_ptr<DuiControl> root = BuildMsgBoxBody();
    REQUIRE(root != nullptr);

    balloonwjui::DuiButton* markAll = dynamic_cast<balloonwjui::DuiButton*>(root->FindCtrlById(kIdMsgBoxMarkAllRead));
    balloonwjui::DuiButton* manage = dynamic_cast<balloonwjui::DuiButton*>(root->FindCtrlById(kIdMsgBoxManage));
    REQUIRE(markAll != nullptr);
    REQUIRE(manage != nullptr);
    CHECK(markAll->GetText() == CString(L"全部标为已读"));
    CHECK(manage->GetText() == CString(L"批量管理"));
    CHECK(dynamic_cast<balloonmain::MainPanelScrollView*>(root->FindCtrlById(kIdMsgBoxScroll)) != nullptr);
    balloonmain::MainPanelTreeView* tree =
        dynamic_cast<balloonmain::MainPanelTreeView*>(root->FindCtrlById(kIdMsgBoxTree));
    REQUIRE(tree != nullptr);
    CHECK(tree->GetRowHeight() == kMsgBoxRowHeight);
    balloonwjui::DuiLabel* empty = dynamic_cast<balloonwjui::DuiLabel*>(root->FindCtrlById(kIdMsgBoxEmpty));
    REQUIRE(empty != nullptr);
    CHECK(empty->GetText() == CString(kMsgBoxEmptyText));

    std::vector<UINT> ids;
    CollectIds(root.get(), ids);
    std::set<UINT> uniq(ids.begin(), ids.end());
    CHECK(uniq.size() == ids.size());
}

TEST_CASE("MBL2 初始为空状态", "[msgbox][layout]")
{
    std::unique_ptr<DuiControl> root = BuildMsgBoxBody();
    REQUIRE(root != nullptr);
    CHECK_FALSE(root->FindCtrlById(kIdMsgBoxScroll)->IsVisible());
    CHECK(root->FindCtrlById(kIdMsgBoxEmpty)->IsVisible());
    CHECK_FALSE(root->FindCtrlById(kIdMsgBoxMarkAllRead)->IsEnabled());
    CHECK(root->FindCtrlById(kIdMsgBoxManage)->IsEnabled());
}

TEST_CASE("MBL3 有会话与没有会话来回切换，「全部标为已读」随未读启用", "[msgbox][layout]")
{
    std::unique_ptr<DuiControl> root = BuildMsgBoxBody();
    REQUIRE(root != nullptr);

    ApplyMsgBoxState(root.get(), kSomeConvs, kSomeUnread);
    CHECK(root->FindCtrlById(kIdMsgBoxScroll)->IsVisible());
    CHECK_FALSE(root->FindCtrlById(kIdMsgBoxEmpty)->IsVisible());
    CHECK(root->FindCtrlById(kIdMsgBoxMarkAllRead)->IsEnabled());

    //有会话但都已读
    ApplyMsgBoxState(root.get(), kSomeConvs, 0);
    CHECK(root->FindCtrlById(kIdMsgBoxScroll)->IsVisible());
    CHECK_FALSE(root->FindCtrlById(kIdMsgBoxMarkAllRead)->IsEnabled());

    //最后一个会话也移出了消息盒子
    ApplyMsgBoxState(root.get(), 0, 0);
    CHECK_FALSE(root->FindCtrlById(kIdMsgBoxScroll)->IsVisible());
    CHECK(root->FindCtrlById(kIdMsgBoxEmpty)->IsVisible());

    //传空指针不崩溃
    ApplyMsgBoxState(nullptr, kSomeConvs, kSomeUnread);
    SyncMsgBoxContentHeight(nullptr);
}

TEST_CASE("MBL4 按设计尺寸排得下：顶部按钮不被压扁，空状态文字占满按钮行以下", "[msgbox][layout]")
{
    std::unique_ptr<DuiControl> root = BuildMsgBoxBody();
    REQUIRE(root != nullptr);

    const layoutfit::FitResult markAll = layoutfit::MeasureAt(root.get(), kIdMsgBoxMarkAllRead, kContentW, kContentH);
    REQUIRE(markAll.m_found);
    CHECK(markAll.m_height == markAll.m_roomyHeight);
    CHECK(markAll.m_bottom <= kContentH);

    const layoutfit::FitResult empty = layoutfit::MeasureAt(root.get(), kIdMsgBoxEmpty, kContentW, kContentH);
    REQUIRE(empty.m_found);
    CHECK(empty.m_bottom == kContentH);
    CHECK(empty.m_height > 0);

    //两个按钮在同一行、不重叠，「批量管理」贴右
    const RECT rcMark = root->FindCtrlById(kIdMsgBoxMarkAllRead)->GetRect();
    const RECT rcManage = root->FindCtrlById(kIdMsgBoxManage)->GetRect();
    CHECK(rcMark.top == rcManage.top);
    CHECK(rcMark.right <= rcManage.left);
    CHECK(rcManage.right < kContentW);
    CHECK(HasArea(rcMark));
    CHECK(HasArea(rcManage));

    //最小尺寸下同样不被压扁
    const layoutfit::FitResult atMin = layoutfit::MeasureAt(root.get(), kIdMsgBoxManage,
                                                            kMsgBoxMinWidth, kMsgBoxMinHeight - kMsgBoxTitleBarH);
    REQUIRE(atMin.m_found);
    CHECK(atMin.m_height == atMin.m_roomyHeight);
}

TEST_CASE("MBL5 先按空状态排版、再切到有会话，列表区矩形正确", "[msgbox][layout]")
{
    std::unique_ptr<DuiControl> root = BuildMsgBoxBody();
    REQUIRE(root != nullptr);
    RECT rc = { 0, 0, kContentW, kContentH };
    root->ForceLayout(rc);
    const RECT rcEmpty = root->FindCtrlById(kIdMsgBoxEmpty)->GetRect();
    REQUIRE(HasArea(rcEmpty));

    //这一步不再整体排版：ApplyMsgBoxState 自己要让父容器重排，列表区才拿得到矩形
    ApplyMsgBoxState(root.get(), kSomeConvs, kSomeUnread);
    const RECT rcScroll = root->FindCtrlById(kIdMsgBoxScroll)->GetRect();
    CHECK(rcScroll.top == rcEmpty.top);
    CHECK(rcScroll.bottom == rcEmpty.bottom);
    CHECK(rcScroll.left == rcEmpty.left);
    CHECK(rcScroll.right == rcEmpty.right);
}

TEST_CASE("MBL6 滚动视口的内容高度 = 行数 × 行高 + 上下内边距", "[msgbox][layout]")
{
    std::unique_ptr<DuiControl> root = BuildMsgBoxBody();
    REQUIRE(root != nullptr);
    RECT rc = { 0, 0, kContentW, kContentH };
    root->ForceLayout(rc);

    balloonmain::MainPanelTreeView* tree =
        dynamic_cast<balloonmain::MainPanelTreeView*>(root->FindCtrlById(kIdMsgBoxTree));
    REQUIRE(tree != nullptr);
    for (int i = 0; i < kSomeConvs; ++i)
    {
        tree->AddRoot(_T("row"));
    }
    ApplyMsgBoxState(root.get(), kSomeConvs, 0);
    SyncMsgBoxContentHeight(root.get());

    balloonmain::MainPanelScrollView* sv =
        dynamic_cast<balloonmain::MainPanelScrollView*>(root->FindCtrlById(kIdMsgBoxScroll));
    REQUIRE(sv != nullptr);
    CHECK(sv->GetContentHeight() == kSomeConvs * kMsgBoxRowHeight + kListPadVTotal);
}
