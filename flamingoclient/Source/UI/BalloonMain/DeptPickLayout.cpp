/**
 *  DeptPickLayout.cpp
 *
 *  「选择部门」小窗客户区控件树的构造与状态注入，说明见 DeptPickLayout.h。本 .cpp 不 include stdafx.h，
 *  Flamingo.vcxproj 里标记 PrecompiledHeader=NotUsing，与 FlamingoUnit.vcxproj 共用。
 *
 *  balloonwj@qq.com   2026-10-08
 */

// ---- balloonui 头文件要求的 ATL/WTL 前置依赖（与 NotifyManageLayout.cpp 相同） ----
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

#include "DeptPickLayout.h"
#include "MemberPickerTreeView.h"   // 带复选框点击判定的树（与选人对话框相同）
#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiSeparator.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiEdit.h"
#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiScrollBar.h"   // DuiScrollView
#include "../../Common/I18n/I18n.h"   // TR / TRF：界面文字的多语言查找

using balloonwjui::DuiControl;
using balloonwjui::DuiVBox;
using balloonwjui::DuiHBox;
using balloonwjui::DuiButton;
using balloonwjui::DuiLabel;
using balloonwjui::DuiSeparator;
using balloonwjui::DuiEdit;
using balloonwjui::DuiScrollView;
using balloonwjui::DuiLayout;

namespace balloonmain {

namespace {

// 颜色
const COLORREF kColorWhite   = RGB(0xFF, 0xFF, 0xFF);   // 窗口底色
const COLORREF kColorSepLine = RGB(0xEE, 0xEE, 0xEE);   // 分隔线
const COLORREF kColorMeta    = RGB(0x99, 0x99, 0x99);   // 次要文字：计数、空状态

// 几何（像素）
const int kPadH         = 12;   // 各行左右内边距
const int kSearchRowH   = 48;   // 搜索行高
const int kSearchEditH  = 30;   // 搜索框高
const int kBottomRowH   = 52;   // 底部操作行高
const int kButtonW      = 72;   // 「取消」「确定」宽
const int kButtonH      = 30;   // 「取消」「确定」高
const int kRowGap       = 8;    // 行内控件间距

// 造一个文字标签
std::unique_ptr<DuiControl> MakeLabel(LPCTSTR text, COLORREF color, DWORD align, UINT ctrlId)
{
    std::unique_ptr<DuiLabel> lbl(new DuiLabel());
    lbl->SetText(text);
    lbl->SetTextColor(color);
    lbl->SetTextAlign(align);
    lbl->SetCtrlId(ctrlId);
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

// 造一个按钮
std::unique_ptr<DuiControl> MakeButton(LPCTSTR text, UINT ctrlId, DuiButton::Variant variant)
{
    std::unique_ptr<DuiButton> btn(new DuiButton());
    btn->SetButtonType(DuiButton::StylePushButton);
    btn->SetVariant(variant);
    btn->SetText(text);
    btn->SetCtrlId(ctrlId);
    return std::unique_ptr<DuiControl>(btn.release());
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

// 按控件当前的矩形重新排版（矩形没变时 SetRect 不往下排，所以用 ForceLayout）
void ForceRelayout(DuiControl* ctrl)
{
    if (ctrl == NULL)
    {
        return;
    }
    const RECT rc = ctrl->GetRect();
    if (rc.right > rc.left && rc.bottom > rc.top)
    {
        ctrl->ForceLayout(rc);
    }
}

} // anonymous namespace

std::unique_ptr<DuiControl> BuildDeptPickBody()
{
    std::unique_ptr<DuiVBox> root(new DuiVBox());
    root->SetBgColor(kColorWhite);
    root->SetPadding(0);
    root->SetGap(0);

    // ---- 搜索框 ----
    std::unique_ptr<DuiHBox> searchRow(new DuiHBox());
    searchRow->SetPadding(kPadH, 0, kPadH, 0);
    std::unique_ptr<DuiEdit> search(new DuiEdit());
    search->SetCtrlId(kIdDeptPickSearch);
    search->SetShowBorder(true);
    search->SetPlaceholder(TR(L"搜索部门"));
    searchRow->AddChild(std::unique_ptr<DuiControl>(search.release()),
                        //主轴（横向）按权重占满，只固定交叉轴（高度）
                        DuiLayout::Hint().Fixed(-1, kSearchEditH).Weight(1).AlignC(DuiLayout::AlignCenter));
    root->AddChild(std::unique_ptr<DuiControl>(searchRow.release()), DuiLayout::Hint().Fixed(kSearchRowH));
    root->AddChild(MakeSeparator(), DuiLayout::Hint().Fixed(1));

    // ---- 部门树：与选人对话框同一套控件；树自己不滚、也报不出期望高度，滚动范围由 SyncDeptPickTreeHeight 显式设置 ----
    std::unique_ptr<MemberPickerTreeView> tree(new MemberPickerTreeView());
    tree->SetCtrlId(kIdDeptPickTree);
    tree->SetRowHeight(kDeptPickRowHeight);
    //点在部门行任何位置都切换勾选（复选框之外也行），比只能点小方框好用；展开箭头仍是展开 / 收起
    tree->SetToggleOnRowClick(true);
    std::unique_ptr<DuiScrollView> scroll(new DuiScrollView());
    scroll->SetCtrlId(kIdDeptPickScroll);
    scroll->SetAutoContentHeight(false);
    scroll->SetContent(std::unique_ptr<DuiControl>(tree.release()));
    scroll->SetVisible(false);   // 初始树为空
    root->AddChild(std::unique_ptr<DuiControl>(scroll.release()), DuiLayout::Hint().Weight(1));
    root->AddChild(MakeLabel(TR(L"没有找到部门"), kColorMeta, DT_CENTER | DT_VCENTER | DT_SINGLELINE, kIdDeptPickEmpty),
                   DuiLayout::Hint().Weight(1));
    root->AddChild(MakeSeparator(), DuiLayout::Hint().Fixed(1));

    // ---- 底部：已选计数  取消  确定 ----
    std::unique_ptr<DuiHBox> bottom(new DuiHBox());
    bottom->SetCtrlId(kIdDeptPickBottom);
    bottom->SetPadding(kPadH, 0, kPadH, 0);
    bottom->SetGap(kRowGap);
    bottom->AddChild(MakeLabel(std::wstring(TRF(L"已选 {0} 个部门（含下级部门）", 0)).c_str(), kColorMeta,
                               DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, kIdDeptPickCount),
                     DuiLayout::Hint().Weight(1));
    bottom->AddChild(MakeButton(TR(L"取消"), kIdDeptPickCancel, DuiButton::Variant::Default),
                     DuiLayout::Hint().Fixed(kButtonW, kButtonH).AlignC(DuiLayout::AlignCenter));
    std::unique_ptr<DuiControl> ok = MakeButton(TR(L"确定"), kIdDeptPickOk, DuiButton::Variant::Primary);
    ok->SetEnabled(false);
    bottom->AddChild(std::move(ok), DuiLayout::Hint().Fixed(kButtonW, kButtonH).AlignC(DuiLayout::AlignCenter));
    root->AddChild(std::unique_ptr<DuiControl>(bottom.release()), DuiLayout::Hint().Fixed(kBottomRowH));
    return std::unique_ptr<DuiControl>(root.release());
}

void ApplyDeptPickState(DuiControl* root, int rowCount, int checkedCount)
{
    if (root == NULL)
    {
        return;
    }
    DuiLabel* count = dynamic_cast<DuiLabel*>(root->FindCtrlById(kIdDeptPickCount));
    if (count != NULL)
    {
        count->SetText(TRF(L"已选 {0} 个部门（含下级部门）", checkedCount).c_str());
    }
    DuiControl* ok = root->FindCtrlById(kIdDeptPickOk);
    if (ok != NULL)
    {
        ok->SetEnabled(checkedCount > 0);
    }

    //树与空提示二选一；显隐变了要对父容器重排（开发约定文档 UI 约定第 5 条）
    const bool hasRows = (rowCount > 0);
    DuiControl* scroll = root->FindCtrlById(kIdDeptPickScroll);
    DuiControl* empty = root->FindCtrlById(kIdDeptPickEmpty);
    const bool changedList = SetVisibleIfChanged(scroll, hasRows);
    const bool changedEmpty = SetVisibleIfChanged(empty, !hasRows);
    if ((changedList || changedEmpty) && scroll != NULL)
    {
        ForceRelayout(scroll->GetParent());
    }
}

void SyncDeptPickTreeHeight(DuiControl* root)
{
    if (root == NULL)
    {
        return;
    }
    DuiScrollView* scroll = dynamic_cast<DuiScrollView*>(root->FindCtrlById(kIdDeptPickScroll));
    MemberPickerTreeView* tree = dynamic_cast<MemberPickerTreeView*>(root->FindCtrlById(kIdDeptPickTree));
    if (scroll == NULL || tree == NULL)
    {
        return;
    }
    const int h = tree->GetContentHeight();
    scroll->SetContentHeight((h > 0) ? h : 0);
    ForceRelayout(scroll);
}

} // namespace balloonmain
