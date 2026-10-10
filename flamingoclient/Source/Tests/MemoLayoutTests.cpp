/**
 *  MemoLayoutTests.cpp
 *
 *  「备忘与提醒」各窗口控件树的布局单测（开发约定文档 UI 约定第 7 条）：
 *    MEM-UL1 备忘窗口按设计尺寸扣掉标题栏后排得下：42 个日期格都在、宽高一致，卡片视口与「＋ 添加备忘」没有被压扁；
 *            月历上方是「‹ 年下拉框 月下拉框 ›」与「今天」，翻月按钮不留焦点框；
 *    MEM-UL2 编辑对话框（个人、群两种）按设计尺寸排得下：底部按钮与各输入控件保持原高，群模式才有「提醒对象」一行；
 *            时间的五个微调框同宽、「时」「分」与「年」「月」按列对齐；左下角有「删除」，「每周」一行有 id；
 *    MEM-UL3 两个窗口的客户区控件 id 都不与标题栏的最小化 / 最大化 / 关闭按钮（1～3）相同；
 *    MEM-UL4 群聊窗右侧边栏有「备忘」页签与面板（空状态提示在卡片视口上方，不再有顶部条数），
 *            新增的 id 与群聊窗其它控件不重复；
 *    MEM-UL5 编辑对话框「方式」一行在「全屏动画」右侧有「预览效果」文字按钮（2026-10-04 起），按设计尺寸排得下。
 *
 *  标签 [memo][layoutfit]。框架 Catch2 v3。全局 _Module 由 BalloonChatLayoutTests.cpp 统一定义。
 *  balloonwj@qq.com   2026-10-03
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <memory>
#include <set>
#include <vector>

#include "LayoutFitCheck.h"
#include "../UI/BalloonMemo/MemoWindowLayout.h"
#include "../UI/BalloonMemo/MemoEditLayout.h"
#include "../UI/BalloonMemo/MemoCalendarControls.h"
#include "../UI/BalloonMemo/MemoCardList.h"
#include "../UI/BalloonChatWnd/GroupChatWindowLayout.h"
#include "../UI/BalloonChatWnd/GroupSideTab.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiEdit.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiSpinBox.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiComboBox.h"

using namespace balloonwjui;
using namespace balloonmemo;

namespace {

//DuiFrameWindow 标题栏三个按钮固定使用的控件 id：1 最小化、2 最大化、3 关闭
const UINT kCaptionIdFirst = 1;
const UINT kCaptionIdLast  = 3;

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

//按设计尺寸排版后，ctrlId 那个控件在内容区之内、高度与空间充裕时相同
void CheckFits(DuiControl* root, UINT ctrlId, int width, int contentH)
{
    INFO("control id = " << ctrlId);
    const layoutfit::FitResult r = layoutfit::MeasureAt(root, ctrlId, width, contentH);
    REQUIRE(r.m_found);
    INFO("bottom = " << r.m_bottom << ", content height = " << contentH << ", height = " << r.m_height
         << ", roomy height = " << r.m_roomyHeight);
    CHECK(r.m_bottom <= contentH);
    CHECK(r.m_height == r.m_roomyHeight);
}

} // namespace

TEST_CASE("MEM-UL1 备忘窗口按设计尺寸排得下", "[memo][layoutfit]")
{
    std::unique_ptr<DuiControl> root = BuildMemoWindowBody();
    REQUIRE(root != nullptr);
    const int contentH = kMemoWindowHeight - kMemoWindowTitleBarH;

    CheckFits(root.get(), kMemoWinIdAdd, kMemoWindowWidth, contentH);
    CheckFits(root.get(), kMemoWinIdDayFirst + kCalendarCells - 1, kMemoWindowWidth, contentH);
    CheckFits(root.get(), kMemoWinIdDayTitle, kMemoWindowWidth, contentH);

    //42 个日期格都是 CMemoDayButton，宽高一致
    root->ForceLayout(RECT{ 0, 0, kMemoWindowWidth, contentH });
    const RECT first = root->FindCtrlById(kMemoWinIdDayFirst)->GetRect();
    for (int i = 0; i < kCalendarCells; ++i)
    {
        INFO("cell " << i);
        DuiControl* cell = root->FindCtrlById(kMemoWinIdDayFirst + i);
        REQUIRE(dynamic_cast<CMemoDayButton*>(cell) != nullptr);
        const RECT rc = cell->GetRect();
        CHECK(rc.right - rc.left == first.right - first.left);
        CHECK(rc.bottom - rc.top == first.bottom - first.top);
    }
    //卡片视口要有实际高度可用
    const RECT list = root->FindCtrlById(kMemoWinIdList)->GetRect();
    CHECK(dynamic_cast<DuiScrollView*>(root->FindCtrlById(kMemoWinIdList)) != nullptr);
    CHECK(list.bottom - list.top > 0);
    //年、月两个下拉框取代了原来的月份标题（2026-10-04 用户反馈切换年月不方便）；104 作废
    const UINT kRetiredMonthTitleId = 104;
    CHECK(root->FindCtrlById(kRetiredMonthTitleId) == nullptr);
    DuiComboBox* year = dynamic_cast<DuiComboBox*>(root->FindCtrlById(kMemoWinIdYear));
    DuiComboBox* month = dynamic_cast<DuiComboBox*>(root->FindCtrlById(kMemoWinIdMonth));
    REQUIRE(year != nullptr);
    REQUIRE(month != nullptr);
    const int kMonthItems = 12;
    CHECK(month->GetCount() == kMonthItems);
    CheckFits(root.get(), kMemoWinIdYear, kMemoWindowWidth, contentH);
    CheckFits(root.get(), kMemoWinIdMonth, kMemoWindowWidth, contentH);
    CheckFits(root.get(), kMemoWinIdToday, kMemoWindowWidth, contentH);
    //「‹ 年 月 › …… 今天」从左到右排开、互不重叠，「今天」右缘不超出月历栏（日期格最右一列）
    root->ForceLayout(RECT{ 0, 0, kMemoWindowWidth, contentH });
    const RECT rcPrev = root->FindCtrlById(kMemoWinIdPrev)->GetRect();
    const RECT rcYear = year->GetRect();
    const RECT rcMonth = month->GetRect();
    const RECT rcNext = root->FindCtrlById(kMemoWinIdNext)->GetRect();
    const RECT rcToday = root->FindCtrlById(kMemoWinIdToday)->GetRect();
    const RECT rcLastCell = root->FindCtrlById(kMemoWinIdDayFirst + kCalendarCols - 1)->GetRect();
    CHECK(rcPrev.right <= rcYear.left);
    CHECK(rcYear.right <= rcMonth.left);
    CHECK(rcMonth.right <= rcNext.left);
    CHECK(rcNext.right <= rcToday.left);
    CHECK(rcToday.right <= rcLastCell.right);
    CHECK(rcYear.right - rcYear.left > 0);
    CHECK(rcMonth.right - rcMonth.left > 0);
    //翻月一行的按钮不参与 Tab 键焦点，点过之后不留虚线焦点框
    CHECK_FALSE(root->FindCtrlById(kMemoWinIdPrev)->IsTabStop());
    CHECK_FALSE(root->FindCtrlById(kMemoWinIdNext)->IsTabStop());
    CHECK_FALSE(root->FindCtrlById(kMemoWinIdToday)->IsTabStop());
}

TEST_CASE("MEM-UL2 编辑对话框按设计尺寸排得下", "[memo][layoutfit]")
{
    const bool modes[] = { false, true };
    for (size_t m = 0; m < sizeof(modes) / sizeof(modes[0]); ++m)
    {
        const bool groupMode = modes[m];
        INFO("group mode = " << groupMode);
        std::unique_ptr<DuiControl> root = BuildMemoEditBody(groupMode);
        REQUIRE(root != nullptr);
        const int height = groupMode ? kMemoEditWindowHeightGroup : kMemoEditWindowHeightPersonal;
        const int contentH = height - kMemoEditTitleBarH;

        const UINT fits[] = { (UINT)kMemoEditIdSave, (UINT)kMemoEditIdCancel, (UINT)kMemoEditIdDelete, (UINT)kMemoEditIdTitle,
                              (UINT)kMemoEditIdYear, (UINT)kMemoEditIdMinute, (UINT)kMemoEditIdAdvance,
                              (UINT)kMemoEditIdRepeat, (UINT)kMemoEditIdWeekdayFirst, (UINT)kMemoEditIdCount,
                              (UINT)kMemoEditIdStyleFull, (UINT)kMemoEditIdNote, (UINT)kMemoEditIdError };
        for (size_t i = 0; i < sizeof(fits) / sizeof(fits[0]); ++i)
            CheckFits(root.get(), fits[i], kMemoEditWindowWidth, contentH);

        CHECK(dynamic_cast<DuiEdit*>(root->FindCtrlById(kMemoEditIdTitle)) != nullptr);
        CHECK(dynamic_cast<DuiEdit*>(root->FindCtrlById(kMemoEditIdNote)) != nullptr);
        CHECK(dynamic_cast<DuiSpinBox*>(root->FindCtrlById(kMemoEditIdDay)) != nullptr);
        CHECK(dynamic_cast<DuiComboBox*>(root->FindCtrlById(kMemoEditIdRepeat)) != nullptr);
        for (int d = 0; d < memo::kDaysPerWeek; ++d)
            CHECK(dynamic_cast<DuiButton*>(root->FindCtrlById(kMemoEditIdWeekdayFirst + d)) != nullptr);

        //提醒对象一行只在群模式下有
        if (groupMode)
        {
            CheckFits(root.get(), kMemoEditIdTargetPick, kMemoEditWindowWidth, contentH);
            CHECK(dynamic_cast<DuiButton*>(root->FindCtrlById(kMemoEditIdPickButton)) != nullptr);
        }
        else
        {
            CHECK(root->FindCtrlById(kMemoEditIdTargetAll) == nullptr);
            CHECK(root->FindCtrlById(kMemoEditIdPickButton) == nullptr);
        }

        //提示行不压到按钮行
        root->ForceLayout(RECT{ 0, 0, kMemoEditWindowWidth, contentH });
        const RECT rcError = root->FindCtrlById(kMemoEditIdError)->GetRect();
        const RECT rcSave = root->FindCtrlById(kMemoEditIdSave)->GetRect();
        CHECK(rcError.bottom <= rcSave.top);

        //时间的五个微调框同宽，「时」与「年」、「分」与「月」左边对齐（2026-10-03 用户反馈原先错位）
        const RECT rcYear = root->FindCtrlById(kMemoEditIdYear)->GetRect();
        const RECT rcMonth = root->FindCtrlById(kMemoEditIdMonth)->GetRect();
        const RECT rcDay = root->FindCtrlById(kMemoEditIdDay)->GetRect();
        const RECT rcHour = root->FindCtrlById(kMemoEditIdHour)->GetRect();
        const RECT rcMinute = root->FindCtrlById(kMemoEditIdMinute)->GetRect();
        const int spinW = rcYear.right - rcYear.left;
        CHECK(spinW > 0);
        CHECK(rcMonth.right - rcMonth.left == spinW);
        CHECK(rcDay.right - rcDay.left == spinW);
        CHECK(rcHour.right - rcHour.left == spinW);
        CHECK(rcMinute.right - rcMinute.left == spinW);
        CHECK(rcHour.left == rcYear.left);
        CHECK(rcMinute.left == rcMonth.left);

        //左下角「删除」在「取消」「保存」左边，三个按钮同一行；「每周」一行与按钮行都有 id，对话框据此重排
        DuiButton* del = dynamic_cast<DuiButton*>(root->FindCtrlById(kMemoEditIdDelete));
        REQUIRE(del != nullptr);
        const RECT rcDelete = del->GetRect();
        const RECT rcCancel = root->FindCtrlById(kMemoEditIdCancel)->GetRect();
        CHECK(rcDelete.right <= rcCancel.left);
        CHECK(rcDelete.top == rcSave.top);
        CHECK(root->FindCtrlById(kMemoEditIdWeekdayRow) != nullptr);
        CHECK(root->FindCtrlById(kMemoEditIdButtonRow) != nullptr);
        CHECK(root->FindCtrlById(kMemoEditIdWeekdayFirst)->GetParent() == root->FindCtrlById(kMemoEditIdWeekdayRow));
    }
    //「提醒」下拉的八档提前量
    CHECK(kMemoAdvanceChoices[0] == 0);
    CHECK(kMemoAdvanceChoices[kMemoAdvanceChoiceCount - 1] == 1440);
}

TEST_CASE("MEM-UL3 备忘窗口与编辑对话框的控件 id 不占用标题栏按钮的 id", "[memo][layoutfit]")
{
    std::vector<UINT> clashes;
    std::unique_ptr<DuiControl> window = BuildMemoWindowBody();
    CollectCaptionIdClashes(window.get(), clashes);
    std::unique_ptr<DuiControl> personal = BuildMemoEditBody(false);
    CollectCaptionIdClashes(personal.get(), clashes);
    std::unique_ptr<DuiControl> group = BuildMemoEditBody(true);
    CollectCaptionIdClashes(group.get(), clashes);
    CHECK(clashes.empty());
}

TEST_CASE("MEM-UL4 群聊窗右侧边栏有「备忘」页签与面板", "[memo][groupchat]")
{
    std::unique_ptr<DuiControl> root = balloonchatwnd::BuildGroupChatWindowBody();
    REQUIRE(root != nullptr);
    CHECK(dynamic_cast<balloonchatwnd::GroupSideTab*>(root->FindCtrlById(balloonchatwnd::kGIdTabMemo)) != nullptr);
    CHECK(root->FindCtrlById(balloonchatwnd::kGIdMemoPanel) != nullptr);
    CHECK(dynamic_cast<DuiScrollView*>(root->FindCtrlById(balloonchatwnd::kGIdMemoList)) != nullptr);
    CHECK(dynamic_cast<DuiButton*>(root->FindCtrlById(balloonchatwnd::kGIdMemoAdd)) != nullptr);
    //顶部的「群备忘 · N 条」已去掉，条数写在页签标题上（591 作废）
    const UINT kRetiredMemoCountId = 591;
    CHECK(root->FindCtrlById(kRetiredMemoCountId) == nullptr);
    //空状态提示排在卡片视口上方：没有备忘时提示在面板最上面，「＋ 新建群备忘」仍在底部
    DuiControl* memoPanel = root->FindCtrlById(balloonchatwnd::kGIdMemoPanel);
    REQUIRE(memoPanel != nullptr);
    const std::vector<std::unique_ptr<DuiControl> >& memoKids = memoPanel->Children();
    REQUIRE(memoKids.size() == 3);
    CHECK(memoKids[0]->GetCtrlId() == (UINT)balloonchatwnd::kGIdMemoEmpty);
    CHECK(memoKids[1]->GetCtrlId() == (UINT)balloonchatwnd::kGIdMemoList);
    CHECK(memoKids[2]->GetCtrlId() == (UINT)balloonchatwnd::kGIdMemoAdd);
    //起始隐藏：成员 Tab 默认选中
    CHECK_FALSE(root->FindCtrlById(balloonchatwnd::kGIdMemoPanel)->IsVisible());

    //角标：大于 0 时记下，负数按 0
    balloonchatwnd::GroupSideTab tab;
    tab.SetBadgeCount(3);
    CHECK(tab.GetBadgeCount() == 3);
    tab.SetBadgeCount(-1);
    CHECK(tab.GetBadgeCount() == 0);

    //新增的 id 与边栏其它控件不重复
    const UINT ids[] = { balloonchatwnd::kGIdTabMembers, balloonchatwnd::kGIdTabFiles, balloonchatwnd::kGIdTabNotice,
                         balloonchatwnd::kGIdTabMemo, balloonchatwnd::kGIdMemberList, balloonchatwnd::kGIdMemberSearch,
                         balloonchatwnd::kGIdNoticePanel, balloonchatwnd::kGIdNoticeEmpty, balloonchatwnd::kGIdHeaderInfo,
                         balloonchatwnd::kGIdMemoPanel, balloonchatwnd::kGIdMemoList,
                         balloonchatwnd::kGIdMemoEmpty, balloonchatwnd::kGIdMemoAdd };
    std::set<UINT> unique(ids, ids + sizeof(ids) / sizeof(ids[0]));
    CHECK(unique.size() == sizeof(ids) / sizeof(ids[0]));
}

TEST_CASE("MEM-UL5 「方式」一行在「全屏动画」右侧有「预览效果」文字按钮，按设计尺寸排得下", "[memo][layoutfit][memo-preview]")
{
    const bool modes[] = { false, true };
    for (size_t m = 0; m < sizeof(modes) / sizeof(modes[0]); ++m)
    {
        const bool groupMode = modes[m];
        INFO("group mode = " << groupMode);
        std::unique_ptr<DuiControl> root = BuildMemoEditBody(groupMode);
        REQUIRE(root != nullptr);
        const int height = groupMode ? kMemoEditWindowHeightGroup : kMemoEditWindowHeightPersonal;
        const int contentH = height - kMemoEditTitleBarH;
        CheckFits(root.get(), kMemoEditIdPreviewFull, kMemoEditWindowWidth, contentH);

        DuiButton* preview = dynamic_cast<DuiButton*>(root->FindCtrlById(kMemoEditIdPreviewFull));
        REQUIRE(preview != nullptr);
        CHECK(preview->GetText() == CString(L"预览效果"));
        //与两个单选同一行、在「全屏动画」右侧，不越出这一行
        DuiControl* full = root->FindCtrlById(kMemoEditIdStyleFull);
        REQUIRE(full != nullptr);
        CHECK(preview->GetParent() == full->GetParent());
        root->ForceLayout(RECT{ 0, 0, kMemoEditWindowWidth, contentH });
        const RECT rcPreview = preview->GetRect();
        const RECT rcFull = full->GetRect();
        const RECT rcRow = full->GetParent()->GetRect();
        CHECK(rcPreview.left >= rcFull.right);
        CHECK(rcPreview.top == rcFull.top);
        CHECK(rcPreview.right <= rcRow.right);
        CHECK(rcPreview.right > rcPreview.left);
        //默认可用：只读模式也能预览，对话框不会把它列进置灰的输入控件
        CHECK(preview->IsEnabled());
    }
}
