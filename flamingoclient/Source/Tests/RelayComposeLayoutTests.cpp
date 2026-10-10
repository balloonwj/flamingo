/**
 *  「发起接龙」窗口的布局单测（UI/BalloonCard/RelayComposeLayout），RelayComposeLayoutTests.cpp
 *
 *  窗口不可缩放，按开发约定文档 UI 约定第 7 条配一条「按设计尺寸排得下」的检查：整窗尺寸扣掉标题栏
 *  剩下的内容区，要完整容下底部两个按钮，按钮上方弹性区里最下面的提示行与各输入框也不能被压扁。
 *    RCL1 按设计尺寸排得下（layoutfit::MeasureAt，与 RegisterTests 的 R25 同一做法）；
 *    RCL2 各控件按 id 都能取到、类型正确，窗口代码据此取回控件指针（截止时间是六个微调框）。
 *    RCL3 客户区任何控件的 id 都不与标题栏的最小化 / 最大化 / 关闭按钮相同。
 *    RCL4 截止时间的六个微调框宽高一致，年月日与时分秒两行按列对齐。
 *
 *  标签 [grouprelay][layoutfit]。框架 Catch2 v3。全局 _Module 由 BalloonChatLayoutTests.cpp 统一定义。
 *  balloonwj@qq.com   2026-10-03
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <memory>
#include <vector>

#include "LayoutFitCheck.h"
#include "../UI/BalloonCard/RelayComposeLayout.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiEdit.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiSpinBox.h"

using namespace balloonwjui;
using namespace ballooncard;

namespace {

//DuiFrameWindow 标题栏三个按钮固定使用的控件 id：1 最小化、2 最大化、3 关闭（见 balloonui
//DuiFrameWindow.cpp 的 OnDuiChildNotify）。客户区控件的点击通知与它们走同一条 WM_DUI_NOTIFY，
//同号时窗口自己的处理函数会先把标题栏按钮的点击当成客户区控件处理，点 X 就关不掉窗口
const UINT kCaptionIdFirst = 1;
const UINT kCaptionIdLast  = 3;

//递归收集控件树里所有 id 落在标题栏按钮范围内的控件 id（0 表示没设 id，不算）
void CollectCaptionIdClashes(const DuiControl* ctrl, std::vector<UINT>& clashes)
{
    if (ctrl == nullptr)
        return;
    const UINT id = ctrl->GetCtrlId();
    if (id >= kCaptionIdFirst && id <= kCaptionIdLast)
        clashes.push_back(id);
    for (size_t i = 0; i < ctrl->Children().size(); ++i)
        CollectCaptionIdClashes(ctrl->Children()[i].get(), clashes);
}

} // namespace

TEST_CASE("RCL1 「发起接龙」窗口按设计尺寸扣掉标题栏后排得下，按钮与输入框都没有被压扁", "[grouprelay][layoutfit]")
{
    const int contentH = kRelayComposeWindowHeight - kRelayComposeTitleBarH;
    std::unique_ptr<DuiControl> root = BuildRelayComposeBody();
    REQUIRE(root != nullptr);

    //底部两个按钮：在内容区之内、高度与空间充裕时相同、下方留白（容器的下内边距）没有被占用
    const UINT buttons[] = { (UINT)kRelayComposeIdSubmit, (UINT)kRelayComposeIdCancel };
    for (size_t i = 0; i < sizeof(buttons) / sizeof(buttons[0]); ++i)
    {
        INFO("button id = " << buttons[i]);
        const layoutfit::FitResult r =
            layoutfit::MeasureAt(root.get(), buttons[i], kRelayComposeWindowWidth, contentH);
        REQUIRE(r.m_found);
        INFO("bottom = " << r.m_bottom << ", content height = " << contentH
             << ", height = " << r.m_height << ", roomy height = " << r.m_roomyHeight
             << ", bottom gap = " << r.m_bottomGap << ", roomy bottom gap = " << r.m_roomyBottomGap);
        CHECK(r.m_bottom <= contentH);
        CHECK(r.m_height == r.m_roomyHeight);
        CHECK(r.m_bottomGap == r.m_roomyBottomGap);
    }

    //按钮固定贴底时，空间不够先被挤压的是按钮上方的那一段：提示行与各输入框保持原高，提示行不压到按钮
    const UINT stacked[] = { (UINT)kRelayComposeIdTitle, (UINT)kRelayComposeIdNote,
                             (UINT)kRelayComposeIdDeadlineYear, (UINT)kRelayComposeIdDeadlineMonth,
                             (UINT)kRelayComposeIdDeadlineDay, (UINT)kRelayComposeIdDeadlineHour,
                             (UINT)kRelayComposeIdDeadlineMinute, (UINT)kRelayComposeIdDeadlineSecond,
                             (UINT)kRelayComposeIdEntry, (UINT)kRelayComposeIdError };
    for (size_t i = 0; i < sizeof(stacked) / sizeof(stacked[0]); ++i)
    {
        INFO("control id = " << stacked[i]);
        const layoutfit::FitResult r =
            layoutfit::MeasureAt(root.get(), stacked[i], kRelayComposeWindowWidth, contentH);
        REQUIRE(r.m_found);
        INFO("height = " << r.m_height << ", roomy height = " << r.m_roomyHeight);
        CHECK(r.m_height == r.m_roomyHeight);
    }
    const RECT rcError = root->FindCtrlById(kRelayComposeIdError)->GetRect();
    const RECT rcSubmit = root->FindCtrlById(kRelayComposeIdSubmit)->GetRect();
    CHECK(rcError.bottom <= rcSubmit.top);
}

TEST_CASE("RCL2 「发起接龙」窗口的控件按 id 都能取到，类型与窗口代码取回时一致", "[grouprelay][layoutfit]")
{
    std::unique_ptr<DuiControl> root = BuildRelayComposeBody();
    REQUIRE(root != nullptr);
    CHECK(dynamic_cast<DuiEdit*>(root->FindCtrlById(kRelayComposeIdTitle)) != nullptr);
    CHECK(dynamic_cast<DuiEdit*>(root->FindCtrlById(kRelayComposeIdNote)) != nullptr);
    //截止时间的年月日时分秒各一个微调框（2026-10-03 起替换原先的档位下拉框）
    const UINT spins[] = { (UINT)kRelayComposeIdDeadlineYear, (UINT)kRelayComposeIdDeadlineMonth,
                           (UINT)kRelayComposeIdDeadlineDay, (UINT)kRelayComposeIdDeadlineHour,
                           (UINT)kRelayComposeIdDeadlineMinute, (UINT)kRelayComposeIdDeadlineSecond };
    for (size_t i = 0; i < sizeof(spins) / sizeof(spins[0]); ++i)
    {
        INFO("spin id = " << spins[i]);
        CHECK(dynamic_cast<DuiSpinBox*>(root->FindCtrlById(spins[i])) != nullptr);
    }
    CHECK(dynamic_cast<DuiEdit*>(root->FindCtrlById(kRelayComposeIdEntry)) != nullptr);
    CHECK(dynamic_cast<DuiLabel*>(root->FindCtrlById(kRelayComposeIdError)) != nullptr);
    CHECK(dynamic_cast<DuiButton*>(root->FindCtrlById(kRelayComposeIdCancel)) != nullptr);
    CHECK(dynamic_cast<DuiButton*>(root->FindCtrlById(kRelayComposeIdSubmit)) != nullptr);
}

TEST_CASE("RCL3 「发起接龙」窗口客户区的控件 id 都不与标题栏按钮相同", "[grouprelay][layoutfit]")
{
    std::unique_ptr<DuiControl> root = BuildRelayComposeBody();
    REQUIRE(root != nullptr);
    std::vector<UINT> clashes;
    CollectCaptionIdClashes(root.get(), clashes);
    INFO("controls using caption button ids: " << clashes.size());
    CHECK(clashes.empty());
}

TEST_CASE("RCL4 截止时间的六个微调框宽高一致，年月日与时分秒两行按列对齐", "[grouprelay][layoutfit]")
{
    std::unique_ptr<DuiControl> root = BuildRelayComposeBody();
    REQUIRE(root != nullptr);
    RECT rc = { 0, 0, kRelayComposeWindowWidth, kRelayComposeWindowHeight - kRelayComposeTitleBarH };
    root->ForceLayout(rc);

    //上一行年、月、日，下一行时、分、秒，同一列的两个框上下对齐
    const UINT topRow[]    = { (UINT)kRelayComposeIdDeadlineYear, (UINT)kRelayComposeIdDeadlineMonth,
                               (UINT)kRelayComposeIdDeadlineDay };
    const UINT bottomRow[] = { (UINT)kRelayComposeIdDeadlineHour, (UINT)kRelayComposeIdDeadlineMinute,
                               (UINT)kRelayComposeIdDeadlineSecond };
    const DuiControl* first = root->FindCtrlById(topRow[0]);
    REQUIRE(first != nullptr);
    const RECT rcFirst = first->GetRect();
    for (size_t i = 0; i < sizeof(topRow) / sizeof(topRow[0]); ++i)
    {
        const DuiControl* top = root->FindCtrlById(topRow[i]);
        const DuiControl* bottom = root->FindCtrlById(bottomRow[i]);
        REQUIRE(top != nullptr);
        REQUIRE(bottom != nullptr);
        const RECT rcTop = top->GetRect();
        const RECT rcBottom = bottom->GetRect();
        INFO("column " << i << ": top [" << rcTop.left << "," << rcTop.right << "] x " << (rcTop.bottom - rcTop.top)
             << ", bottom [" << rcBottom.left << "," << rcBottom.right << "] x " << (rcBottom.bottom - rcBottom.top));
        //宽高与第一个框（年）相同
        CHECK(rcTop.right - rcTop.left == rcFirst.right - rcFirst.left);
        CHECK(rcTop.bottom - rcTop.top == rcFirst.bottom - rcFirst.top);
        CHECK(rcBottom.right - rcBottom.left == rcFirst.right - rcFirst.left);
        CHECK(rcBottom.bottom - rcBottom.top == rcFirst.bottom - rcFirst.top);
        //上下两行同一列左右边缘对齐，下一行在上一行下方
        CHECK(rcTop.left == rcBottom.left);
        CHECK(rcTop.right == rcBottom.right);
        CHECK(rcBottom.top >= rcTop.bottom);
    }
}
