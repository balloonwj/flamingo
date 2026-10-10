/**
 *  MsgBoxLayout.cpp
 *
 *  「消息盒子」窗口客户区控件树的构造与状态注入。全部用 balloonui 现成控件组合：DuiVBox / DuiHBox /
 *  DuiButton / DuiLabel / DuiSeparator，列表复用主面板的 MainPanelScrollView 与 MainPanelTreeView
 *  （开发约定文档 UI 约定第 2 条）。本 .cpp 不 include stdafx.h，Flamingo.vcxproj 里标记
 *  PrecompiledHeader=NotUsing，与 FlamingoUnit.vcxproj 共用。
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

#include "MsgBoxLayout.h"
#include "../BalloonMain/MainPanelScrollView.h"   // 列表的悬浮式自动隐藏滚动视口
#include "../BalloonMain/MainPanelTreeView.h"     // 与主面板会话列表同一个列表控件
#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiSeparator.h"
#include "../../Common/I18n/I18n.h"   // TR：界面文字的多语言查找

using balloonwjui::DuiControl;
using balloonwjui::DuiVBox;
using balloonwjui::DuiHBox;
using balloonwjui::DuiButton;
using balloonwjui::DuiLabel;
using balloonwjui::DuiSeparator;
using balloonwjui::DuiLayout;

namespace balloonmsgbox {

const wchar_t* const kMsgBoxEmptyText = TR_NOOP(L"没有收进消息盒子的会话");

namespace {

// 颜色
const COLORREF kColorWhite    = RGB(0xFF, 0xFF, 0xFF);   // 窗口底色
const COLORREF kColorSepLine  = RGB(0xEE, 0xEE, 0xEE);   // 分隔线
const COLORREF kColorEmpty    = RGB(0x99, 0x99, 0x99);   // 空状态文字

// 顶部按钮行的几何（逻辑像素）
const int kToolbarH        = 48;    // 按钮行高
const int kToolbarPadH     = 12;    // 按钮行左右内边距
const int kToolbarPadV     = 10;    // 按钮行上下内边距
const int kMarkAllButtonW  = 104;   // 「全部标为已读」按钮宽
const int kManageButtonW   = 80;    // 「批量管理」按钮宽

// 列表内容的内边距（逻辑像素），与主面板会话列表的 <vbox padding="6,4,6,4"> 相同
const int kListPadH = 6;
const int kListPadV = 4;

// 头像边长（逻辑像素），与主面板会话列表相同；圆形头像的圆角半径取边长一半
const int kAvatarSizePx = 36;

// 造一个按钮
std::unique_ptr<DuiControl> MakeButton(LPCTSTR text, UINT ctrlId)
{
    std::unique_ptr<DuiButton> btn(new DuiButton());
    btn->SetButtonType(DuiButton::StylePushButton);
    btn->SetText(text);
    btn->SetCtrlId(ctrlId);
    return std::unique_ptr<DuiControl>(btn.release());
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
    if (ctrl == NULL)
    {
        return;
    }
    DuiControl* parent = ctrl->GetParent();
    if (parent == NULL)
    {
        return;
    }
    const RECT rc = parent->GetRect();
    if (rc.right > rc.left && rc.bottom > rc.top)
    {
        parent->ForceLayout(rc);
    }
}

} // anonymous namespace

std::unique_ptr<DuiControl> BuildMsgBoxBody()
{
    std::unique_ptr<DuiVBox> root(new DuiVBox());
    root->SetBgColor(kColorWhite);
    root->SetPadding(0);
    root->SetGap(0);

    // ---- 顶部按钮行：左「全部标为已读」、右「批量管理」 ----
    std::unique_ptr<DuiHBox> toolbar(new DuiHBox());
    toolbar->SetPadding(kToolbarPadH, kToolbarPadV, kToolbarPadH, kToolbarPadV);
    toolbar->SetGap(0);
    toolbar->AddChild(MakeButton(TR(L"全部标为已读"), kIdMsgBoxMarkAllRead),
                      DuiLayout::Hint().Fixed(kMarkAllButtonW));
    std::unique_ptr<DuiControl> spacer(new DuiControl());
    toolbar->AddChild(std::move(spacer), DuiLayout::Hint().Weight(1));
    toolbar->AddChild(MakeButton(TR(L"批量管理"), kIdMsgBoxManage),
                      DuiLayout::Hint().Fixed(kManageButtonW));
    root->AddChild(std::unique_ptr<DuiControl>(toolbar.release()), DuiLayout::Hint().Fixed(kToolbarH));
    root->AddChild(MakeSeparator(), DuiLayout::Hint().Fixed(1));

    // ---- 会话列表：与主面板会话列表同一套控件 ----
    std::unique_ptr<balloonmain::MainPanelTreeView> tree(new balloonmain::MainPanelTreeView());
    tree->SetCtrlId(kIdMsgBoxTree);
    tree->SetRowHeight(kMsgBoxRowHeight);
    tree->SetIconSize(kAvatarSizePx);
    tree->SetIconCornerRadius(kAvatarSizePx / 2);
    tree->SetIconUsesAlpha(true);   // 走 AlphaBlend 路径，圆形头像无锯齿

    std::unique_ptr<DuiVBox> listBox(new DuiVBox());
    listBox->SetPadding(kListPadH, kListPadV, kListPadH, kListPadV);
    listBox->SetGap(0);
    listBox->AddChild(std::unique_ptr<DuiControl>(tree.release()), DuiLayout::Hint().Weight(1));

    std::unique_ptr<balloonmain::MainPanelScrollView> scroll(new balloonmain::MainPanelScrollView());
    scroll->SetCtrlId(kIdMsgBoxScroll);
    scroll->SetContent(std::unique_ptr<DuiControl>(listBox.release()));
    scroll->SetVisible(false);   // 初始为空状态
    root->AddChild(std::unique_ptr<DuiControl>(scroll.release()), DuiLayout::Hint().Weight(1));

    // ---- 空状态：列表区居中一行灰字 ----
    std::unique_ptr<DuiLabel> empty(new DuiLabel());
    empty->SetCtrlId(kIdMsgBoxEmpty);
    empty->SetText(TR(kMsgBoxEmptyText));
    empty->SetTextColor(kColorEmpty);
    empty->SetTextAlign(DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    root->AddChild(std::unique_ptr<DuiControl>(empty.release()), DuiLayout::Hint().Weight(1));

    // 初始没有未读：「全部标为已读」禁用
    DuiControl* markAll = root->FindCtrlById(kIdMsgBoxMarkAllRead);
    if (markAll != NULL)
    {
        markAll->SetEnabled(false);
    }
    return std::unique_ptr<DuiControl>(root.release());
}

void ApplyMsgBoxState(DuiControl* root, int convCount, int unreadTotal)
{
    if (root == NULL)
    {
        return;
    }

    DuiControl* markAll = root->FindCtrlById(kIdMsgBoxMarkAllRead);
    if (markAll != NULL)
    {
        markAll->SetEnabled(unreadTotal > 0);
    }

    const bool hasConv = (convCount > 0);
    DuiControl* scroll = root->FindCtrlById(kIdMsgBoxScroll);
    DuiControl* empty = root->FindCtrlById(kIdMsgBoxEmpty);
    bool changed = false;
    if (scroll != NULL && scroll->IsVisible() != hasConv)
    {
        scroll->SetVisible(hasConv);
        changed = true;
    }
    if (empty != NULL && empty->IsVisible() == hasConv)
    {
        empty->SetVisible(!hasConv);
        changed = true;
    }
    if (changed)
    {
        RelayoutParent(scroll != NULL ? scroll : empty);
    }
}

void SyncMsgBoxContentHeight(DuiControl* root)
{
    if (root == NULL)
    {
        return;
    }
    balloonwjui::DuiScrollView* sv = dynamic_cast<balloonwjui::DuiScrollView*>(
        root->FindCtrlById(kIdMsgBoxScroll));
    balloonwjui::DuiTreeView* tv = dynamic_cast<balloonwjui::DuiTreeView*>(
        root->FindCtrlById(kIdMsgBoxTree));
    if (sv == NULL || tv == NULL)
    {
        return;
    }
    sv->SetContentHeight(tv->GetContentHeight() + kListPadV * 2);

    // SetContentHeight 不会重算内容的名义矩形，ForceLayout 一次让它跟上新高度（与主面板
    // SyncPaneContentHeight 同一做法）；视口还没排过版时矩形为空，此时不必做
    const RECT rc = sv->GetRect();
    if (rc.right > rc.left && rc.bottom > rc.top)
    {
        sv->ForceLayout(rc);
    }
}

} // namespace balloonmsgbox
