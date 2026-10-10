/**
 *  NotifyManageLayout.cpp
 *
 *  批量管理窗口「消息提醒管理」客户区控件树的构造与状态注入。本 .cpp 不 include stdafx.h，
 *  Flamingo.vcxproj 里标记 PrecompiledHeader=NotUsing，与 FlamingoUnit.vcxproj 共用。
 *
 *  balloonwj@qq.com   2026-10-03
 */

// ---- balloonui 头文件要求的 ATL/WTL 前置依赖（与 BalloonGroupInfoLayout.cpp 相同） ----
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#define WINVER          0x0601      // 目标系统下限 = Windows 7
#define _WIN32_WINNT    0x0601
#define _WIN32_IE       0x0601
#define _RICHEDIT_VER   0x0300      // RichEdit 3.0，WTL 10 要求的下限
#define _WTL_NO_CSTRING
#define _WTL_NO_WTYPES

#include <atlbase.h>
#include <atlstr.h>
#include <atltypes.h>
#include <atlapp.h>

extern CAppModule _Module;

#include <atlwin.h>
#include <atlcrack.h>
#include <atlmisc.h>

#include <vector>

#include "NotifyManageLayout.h"
#include "NotifyManageModel.h"                        // kNotifyManageMaxRows（截断提示里的数字）
#include "../BalloonMain/NotifyModeMenu.h"            // 下拉框选项：与右键二级菜单同一份顺序和文字
#include "../BalloonMain/MainPanelScrollView.h"       // 列表的悬浮式自动隐藏滚动视口
#include "../BalloonMain/MemberPickerTreeView.h"      // 带复选框点击判定的列表（与选人对话框相同）
#include "../../net/Protocol.h"                       // notify_mode_allow
#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiSeparator.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiEdit.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiComboBox.h"
#include "../../../third-party/balloonui/balloonui/Controls/List/DuiTab.h"
#include "../../Common/I18n/I18n.h"   // TR：界面文字的多语言查找

using balloonwjui::DuiControl;
using balloonwjui::DuiVBox;
using balloonwjui::DuiHBox;
using balloonwjui::DuiButton;
using balloonwjui::DuiLabel;
using balloonwjui::DuiSeparator;
using balloonwjui::DuiEdit;
using balloonwjui::DuiComboBox;
using balloonwjui::DuiTab;
using balloonwjui::DuiLayout;

namespace balloonnotifymode {

const wchar_t* const kManageEmptyText = TR_NOOP(L"没有可设置的会话");

namespace {

// 颜色
const COLORREF kColorWhite   = RGB(0xFF, 0xFF, 0xFF);   // 窗口底色
const COLORREF kColorSepLine = RGB(0xEE, 0xEE, 0xEE);   // 分隔线
const COLORREF kColorText    = RGB(0x33, 0x33, 0x33);   // 正文文字
const COLORREF kColorMeta    = RGB(0x99, 0x99, 0x99);   // 次要文字：计数、提示、空状态

// 几何（逻辑像素）
const int kPadH            = 12;    // 各行左右内边距
const int kTabRowH         = 44;    // 页签行高（含上内边距）
const int kTabH            = 34;    // 页签高
const int kTabMinW         = 72;    // 单个页签最小宽
const int kSearchRowH      = 44;    // 搜索行高
const int kSearchEditH     = 30;    // 搜索框高
const int kSelectRowH      = 32;    // 「全选」行高
const int kSelectAllW      = 64;    // 「全选」勾选框宽
const int kCountW          = 96;    // 已选计数宽
const int kListPadV        = 4;     // 列表内容上下内边距
const int kBottomRowH      = 52;    // 底部操作行高
const int kTargetLabelW    = 88;    // 「将选中的设为」文字宽
const int kTargetComboW    = 208;   // 下拉框宽：放得下「收进消息盒子且不提醒」连同下拉箭头（176 时在 125% 缩放下会被截断）
const int kTargetComboH    = 28;    // 下拉框高
const int kApplyButtonW    = 72;    // 「应用」按钮宽
const int kApplyButtonH    = 30;    // 「应用」按钮高
const int kRowGap          = 8;     // 行内控件间距

// 造一个文字标签
std::unique_ptr<DuiControl> MakeLabel(LPCTSTR text, COLORREF color, DWORD align, UINT ctrlId)
{
    std::unique_ptr<DuiLabel> lbl(new DuiLabel());
    lbl->SetText(text);
    lbl->SetTextColor(color);
    lbl->SetTextAlign(align);
    if (ctrlId != 0)
    {
        lbl->SetCtrlId(ctrlId);
    }
    return std::unique_ptr<DuiControl>(lbl.release());
}

// 造一条横向分隔线
std::unique_ptr<DuiControl> MakeSeparator()
{
    std::unique_ptr<DuiSeparator> sep(new DuiSeparator());
    sep->SetOrientation(DuiSeparator::Horizontal);
    sep->SetColor(kColorSepLine);
    return std::unique_ptr<DuiControl>(sep.release());
}

// 对显隐变了的控件的父容器按当前矩形重排一次（SetVisible 本身不重排，开发约定文档 UI 约定第 5 条）
void RelayoutParent(DuiControl* ctrl)
{
    if (ctrl == NULL || ctrl->GetParent() == NULL)
    {
        return;
    }
    DuiControl* parent = ctrl->GetParent();
    const RECT rc = parent->GetRect();
    if (rc.right > rc.left && rc.bottom > rc.top)
    {
        parent->ForceLayout(rc);
    }
}

// 改控件的可见性，有变化时返回 true
bool SetVisibleIfChanged(DuiControl* ctrl, bool visible)
{
    if (ctrl == NULL || ctrl->IsVisible() == visible)
    {
        return false;
    }
    ctrl->SetVisible(visible);
    return true;
}

} // anonymous namespace

std::unique_ptr<DuiControl> BuildNotifyManageBody()
{
    std::unique_ptr<DuiVBox> root(new DuiVBox());
    root->SetBgColor(kColorWhite);
    root->SetPadding(0);
    root->SetGap(0);

    // ---- 页签条：群聊 / 单聊 ----
    std::unique_ptr<DuiHBox> tabRow(new DuiHBox());
    tabRow->SetPadding(kPadH, kTabRowH - kTabH, kPadH, 0);
    std::unique_ptr<DuiTab> tabs(new DuiTab());
    tabs->SetCtrlId(kIdManageTabs);
    tabs->SetTabHeight(kTabH);
    tabs->SetMinTabWidth(kTabMinW);
    tabs->AddTab(TR(L"群聊"));
    tabs->AddTab(TR(L"单聊"));
    tabs->SetCurSel(kManageTabGroup, /*notify*/false);
    tabRow->AddChild(std::unique_ptr<DuiControl>(tabs.release()), DuiLayout::Hint().Weight(1));
    root->AddChild(std::unique_ptr<DuiControl>(tabRow.release()), DuiLayout::Hint().Fixed(kTabRowH));
    root->AddChild(MakeSeparator(), DuiLayout::Hint().Fixed(1));

    // ---- 搜索框 ----
    std::unique_ptr<DuiHBox> searchRow(new DuiHBox());
    searchRow->SetPadding(kPadH, 0, kPadH, 0);
    std::unique_ptr<DuiEdit> search(new DuiEdit());
    search->SetCtrlId(kIdManageSearch);
    search->SetShowBorder(true);
    search->SetPlaceholder(TR(L"搜索名字或账号"));
    searchRow->AddChild(std::unique_ptr<DuiControl>(search.release()),
                       //主轴（横向）按权重占满，只固定交叉轴（高度）
                       DuiLayout::Hint().Fixed(-1, kSearchEditH).Weight(1).AlignC(DuiLayout::AlignCenter));
    root->AddChild(std::unique_ptr<DuiControl>(searchRow.release()), DuiLayout::Hint().Fixed(kSearchRowH));

    // ---- 「全选」+ 已选计数 + 截断提示 ----
    std::unique_ptr<DuiHBox> selectRow(new DuiHBox());
    selectRow->SetPadding(kPadH, 0, kPadH, 0);
    selectRow->SetGap(kRowGap);
    std::unique_ptr<DuiButton> selectAll(new DuiButton());
    selectAll->SetButtonType(DuiButton::StyleCheckbox);
    selectAll->SetText(TR(L"全选"));
    selectAll->SetCtrlId(kIdManageSelectAll);
    selectRow->AddChild(std::unique_ptr<DuiControl>(selectAll.release()), DuiLayout::Hint().Fixed(kSelectAllW));
    selectRow->AddChild(MakeLabel(TR(L"已选 0 个"), kColorMeta, DT_LEFT | DT_VCENTER | DT_SINGLELINE, kIdManageCount),
                        DuiLayout::Hint().Fixed(kCountW));
    CString hint;
    hint.Format(TR(L"只列出前 %u 个，请输入更精确的搜索词"), (unsigned)kNotifyManageMaxRows);
    std::unique_ptr<DuiControl> hintLbl = MakeLabel(hint, kColorMeta,
                                                    DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS,
                                                    kIdManageHint);
    hintLbl->SetVisible(false);
    selectRow->AddChild(std::move(hintLbl), DuiLayout::Hint().Weight(1));
    root->AddChild(std::unique_ptr<DuiControl>(selectRow.release()), DuiLayout::Hint().Fixed(kSelectRowH));
    root->AddChild(MakeSeparator(), DuiLayout::Hint().Fixed(1));

    // ---- 会话列表：与选人对话框同一套控件 ----
    std::unique_ptr<balloonmain::MemberPickerTreeView> tree(new balloonmain::MemberPickerTreeView());
    tree->SetCtrlId(kIdManageTree);
    tree->SetRowHeight(kManageRowHeight);
    std::unique_ptr<DuiVBox> listBox(new DuiVBox());
    listBox->SetPadding(0, kListPadV, 0, kListPadV);
    listBox->AddChild(std::unique_ptr<DuiControl>(tree.release()), DuiLayout::Hint().Weight(1));
    std::unique_ptr<balloonmain::MainPanelScrollView> scroll(new balloonmain::MainPanelScrollView());
    scroll->SetCtrlId(kIdManageScroll);
    scroll->SetContent(std::unique_ptr<DuiControl>(listBox.release()));
    scroll->SetVisible(false);   // 初始列表为空
    root->AddChild(std::unique_ptr<DuiControl>(scroll.release()), DuiLayout::Hint().Weight(1));
    root->AddChild(MakeLabel(TR(kManageEmptyText), kColorMeta, DT_CENTER | DT_VCENTER | DT_SINGLELINE, kIdManageEmpty),
                   DuiLayout::Hint().Weight(1));
    root->AddChild(MakeSeparator(), DuiLayout::Hint().Fixed(1));

    // ---- 底部：将选中的设为 [下拉框]  应用 ----
    std::unique_ptr<DuiHBox> bottom(new DuiHBox());
    bottom->SetPadding(kPadH, 0, kPadH, 0);
    bottom->SetGap(kRowGap);
    bottom->AddChild(MakeLabel(TR(L"将选中的设为"), kColorText, DT_LEFT | DT_VCENTER | DT_SINGLELINE, 0),
                     DuiLayout::Hint().Fixed(kTargetLabelW));
    std::unique_ptr<DuiComboBox> target(new DuiComboBox());
    target->SetCtrlId(kIdManageTarget);
    target->SetEditable(false);
    bottom->AddChild(std::unique_ptr<DuiControl>(target.release()),
                     DuiLayout::Hint().Fixed(kTargetComboW, kTargetComboH).AlignC(DuiLayout::AlignCenter));
    std::unique_ptr<DuiControl> spacer(new DuiControl());
    bottom->AddChild(std::move(spacer), DuiLayout::Hint().Weight(1));
    std::unique_ptr<DuiButton> apply(new DuiButton());
    apply->SetButtonType(DuiButton::StylePushButton);
    apply->SetText(TR(L"应用"));
    apply->SetCtrlId(kIdManageApply);
    apply->SetEnabled(false);
    bottom->AddChild(std::unique_ptr<DuiControl>(apply.release()),
                     DuiLayout::Hint().Fixed(kApplyButtonW, kApplyButtonH).AlignC(DuiLayout::AlignCenter));
    root->AddChild(std::unique_ptr<DuiControl>(bottom.release()), DuiLayout::Hint().Fixed(kBottomRowH));

    SetManageTargetChoices(root.get(), /*isGroup*/true);
    return std::unique_ptr<DuiControl>(root.release());
}

void SetManageTargetChoices(DuiControl* root, bool isGroup)
{
    if (root == NULL)
    {
        return;
    }
    DuiComboBox* combo = dynamic_cast<DuiComboBox*>(root->FindCtrlById(kIdManageTarget));
    if (combo == NULL)
    {
        return;
    }
    combo->ResetContent();
    const std::vector<int> modes = balloonmain::GetNotifyModeChoices(isGroup);
    for (size_t i = 0; i < modes.size(); ++i)
    {
        combo->AddString(balloonmain::GetNotifyModeLabel(modes[i]));
    }
    combo->SetCurSel(balloonmain::NotifyModeChoiceIndex(isGroup, notify_mode_allow), /*notify*/false);
}

int GetManageTargetMode(DuiControl* root, bool isGroup)
{
    if (root == NULL)
    {
        return notify_mode_allow;
    }
    DuiComboBox* combo = dynamic_cast<DuiComboBox*>(root->FindCtrlById(kIdManageTarget));
    if (combo == NULL)
    {
        return notify_mode_allow;
    }
    return balloonmain::NotifyModeOfChoice(isGroup, combo->GetCurSel());
}

void ApplyManageListState(DuiControl* root, int rowCount, bool truncated,
                          int checkedCount, bool allChecked)
{
    if (root == NULL)
    {
        return;
    }

    CString count;
    count.Format(TR(L"已选 %d 个"), checkedCount);
    DuiLabel* countLbl = dynamic_cast<DuiLabel*>(root->FindCtrlById(kIdManageCount));
    if (countLbl != NULL)
    {
        countLbl->SetText(count);
    }
    DuiButton* selectAll = dynamic_cast<DuiButton*>(root->FindCtrlById(kIdManageSelectAll));
    if (selectAll != NULL)
    {
        selectAll->SetCheck(rowCount > 0 && allChecked, /*notify*/false);
        selectAll->SetEnabled(rowCount > 0);
    }
    DuiControl* apply = root->FindCtrlById(kIdManageApply);
    if (apply != NULL)
    {
        apply->SetEnabled(checkedCount > 0);
    }

    DuiControl* hint = root->FindCtrlById(kIdManageHint);
    if (SetVisibleIfChanged(hint, truncated))
    {
        RelayoutParent(hint);
    }

    const bool hasRows = (rowCount > 0);
    DuiControl* scroll = root->FindCtrlById(kIdManageScroll);
    DuiControl* empty = root->FindCtrlById(kIdManageEmpty);
    const bool changedList = SetVisibleIfChanged(scroll, hasRows);
    const bool changedEmpty = SetVisibleIfChanged(empty, !hasRows);
    if (changedList || changedEmpty)
    {
        RelayoutParent(scroll != NULL ? scroll : empty);
    }

    // 滚动视口的内容高度 = 行数 × 行高 + 上下内边距（树控件问不出自己的总高度）
    balloonwjui::DuiScrollView* sv = dynamic_cast<balloonwjui::DuiScrollView*>(scroll);
    if (sv != NULL)
    {
        sv->SetContentHeight(rowCount * kManageRowHeight + kListPadV * 2);
        const RECT rc = sv->GetRect();
        if (rc.right > rc.left && rc.bottom > rc.top)
        {
            sv->ForceLayout(rc);
        }
    }
}

} // namespace balloonnotifymode
