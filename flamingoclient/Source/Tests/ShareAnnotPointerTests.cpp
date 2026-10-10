/**
 *  标注工具条的「鼠标」按钮与小把手的单元测试（标签 [annotpointer]），ShareAnnotPointerTests.cpp
 *  背景：2026-10-07 用户确认：标注工具条最左边加「鼠标」按钮，点了以后撤掉输入层、点击穿透到桌面程序，工具条与已画的标注保留，
 *  再选任意工具回到画图；「选择对象」工具悬停在对象上显示四向移动光标、空白处显示十字；「收起」改成与顶部共享控制条一样的
 *  小把手：显示当前工具的图标，鼠标移上去自动展开，移开后再收起（docs/屏幕共享与录制方案.md 第 13 节）。
 *  覆盖：
 *    - SAPB1 「鼠标」按钮在整条最左边；点一下向通知窗口发 annot_bar_pointer（1 进入），按钮变主色、「绘图工具」不再高亮；
 *      再点一下发 0 回到画图；
 *    - SAPB2 哪些命令让标注从「鼠标」状态回到画图：选工具、会切到某个工具的样式；颜色、粗细、撤销等不会；
 *    - SAPB3 收起成小把手：小把手贴在整条的底边正中、只有一个显示当前工具图标的按钮；鼠标移上去临时展开（整条），
 *      离开不满 2 秒不收，满 2 秒收回；临时展开时「收起」按钮的提示是「固定工具条」，点它固定；点小把手展开并固定；
 *    - SAPB4 选择工具的光标：有对象时为四向移动、空白处为十字。
 *  框架 Catch2 v3。会建工具条窗口，放在不可见桌面上运行。
 *  balloonwj@qq.com   2026-10-07
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include "../../FlamingoMedia/Share/ShareAnnotation.h"
#include "../../FlamingoMedia/Share/ShareAnnotationBar.h"
#include "../../FlamingoMedia/Share/ShareAnnotationMenus.h"
#include "../../FlamingoMedia/Share/ShareAnnotModePolicy.h"
#include "../../FlamingoMedia/Share/ShareMessages.h"
#include "../Common/I18n/I18n.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/Controls/Feedback/DuiToolTip.h"
#include "../../../third-party/balloonui/balloonui/DuiNotify.h"

using namespace screenshare;

namespace {

//参照矩形（屏幕坐标）：工具条摆在它下方或底部
const RECT kSapbAnchor = { 100, 100, 1500, 800 };
//小把手的宽（与 ShareAnnotationBar.cpp 的 kHandleBtnW + 2 × kHandlePadH 一致）
const int kSapbHandleW = 52;
//临时展开后鼠标离开的时长（毫秒）：不满与超过 kTopBarPeekHideMs
const int64_t kSapbShortLeaveMs = 500;
const int64_t kSapbStartMs = 100000;

HWND SapbNotifyWindow()
{
    return ::CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, ::GetModuleHandleW(NULL), NULL);
}

balloonwjui::DuiButton* SapbButton(CShareAnnotationBar& bar, UINT id)
{
    balloonwjui::DuiControl* content = bar.GetClientContent();
    return content != NULL ? dynamic_cast<balloonwjui::DuiButton*>(content->FindCtrlById(id)) : NULL;
}

RECT SapbWindowRect(CShareAnnotationBar& bar)
{
    RECT rc;
    ::GetWindowRect(bar.m_hWnd, &rc);
    return rc;
}

void SapbClick(HWND wnd, UINT id)
{
    balloonwjui::DuiNotify n;
    n.code = static_cast<UINT>(balloonwjui::DUIN_CLICK);
    n.ctrlId = id;
    n.pCtrl = NULL;
    n.extra = 0;
    ::SendMessageW(wnd, WM_DUI_NOTIFY, static_cast<WPARAM>(id), reinterpret_cast<LPARAM>(&n));
}

//取走通知窗口里最后一条标注命令；没有时返回 false
bool SapbTakeCommand(HWND notify, WPARAM& wParam, LPARAM& lParam)
{
    MSG msg;
    bool got = false;
    while (::PeekMessageW(&msg, notify, kMsgShareAnnotCommand, kMsgShareAnnotCommand, PM_REMOVE))
    {
        got = true;
        wParam = msg.wParam;
        lParam = msg.lParam;
    }
    return got;
}

//处理投给自己的消息（收起 / 展开在点击后投递）
void SapbPump()
{
    MSG msg;
    while (::PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
    {
        ::TranslateMessage(&msg);
        ::DispatchMessageW(&msg);
    }
}

} // namespace

TEST_CASE("SAPB1 the pointer button sits at the left end and toggles pointer mode", "[annotpointer]")
{
    const HWND notify = SapbNotifyWindow();
    CShareAnnotationBar bar;
    bar.Show(kSapbAnchor, notify, true);
    REQUIRE(bar.IsWindow());
    balloonwjui::DuiButton* pointer = SapbButton(bar, CShareAnnotationBar::kIdPointer);
    balloonwjui::DuiButton* collapse = SapbButton(bar, CShareAnnotationBar::kIdCollapse);
    balloonwjui::DuiButton* tools = SapbButton(bar, CShareAnnotationBar::kIdTools);
    REQUIRE(pointer != NULL);
    REQUIRE(collapse != NULL);
    REQUIRE(tools != NULL);
    //最左边
    CHECK(pointer->GetRect().left < collapse->GetRect().left);
    CHECK(pointer->GetRect().left < tools->GetRect().left);
    CHECK(pointer->GetVariant() == balloonwjui::DuiButton::Variant::Default);
    CHECK(tools->GetVariant() == balloonwjui::DuiButton::Variant::Primary);
    CHECK_FALSE(balloonwjui::DuiToolTipMgr::Inst().GetText(pointer).IsEmpty());
    //点一下：进入「鼠标」状态
    SapbClick(bar.m_hWnd, CShareAnnotationBar::kIdPointer);
    WPARAM w = 0;
    LPARAM l = 0;
    REQUIRE(SapbTakeCommand(notify, w, l));
    CHECK(w == static_cast<WPARAM>(annot_bar_pointer));
    CHECK(l == 1);
    CHECK(bar.IsPointerMode());
    CHECK(pointer->GetVariant() == balloonwjui::DuiButton::Variant::Primary);
    CHECK(tools->GetVariant() == balloonwjui::DuiButton::Variant::Default);
    //再点：回到画图
    SapbClick(bar.m_hWnd, CShareAnnotationBar::kIdPointer);
    REQUIRE(SapbTakeCommand(notify, w, l));
    CHECK(l == 0);
    CHECK_FALSE(bar.IsPointerMode());
    //控制器同步
    bar.SetPointerMode(true);
    CHECK(pointer->GetVariant() == balloonwjui::DuiButton::Variant::Primary);
    bar.Hide();
    ::DestroyWindow(notify);
}

TEST_CASE("SAPB2 which commands bring annotation back from pointer mode", "[annotpointer]")
{
    AnnotMenuChoice c;
    c.m_command = annot_bar_tool;
    c.m_value = annot_tool_rect;
    CHECK(AnnotChoiceLeavesPointer(c));
    c.m_command = annot_bar_routing;
    CHECK(AnnotChoiceLeavesPointer(c));
    c.m_command = annot_bar_text_bold;
    CHECK(AnnotChoiceLeavesPointer(c));
    c.m_command = annot_bar_color;
    CHECK_FALSE(AnnotChoiceLeavesPointer(c));
    c.m_command = annot_bar_width;
    CHECK_FALSE(AnnotChoiceLeavesPointer(c));
    c.m_command = annot_bar_undo;
    CHECK_FALSE(AnnotChoiceLeavesPointer(c));
    c.m_command = annot_bar_pointer;
    CHECK_FALSE(AnnotChoiceLeavesPointer(c));
}

TEST_CASE("SAPB3 the bar collapses into a handle that expands on hover", "[annotpointer]")
{
    const HWND notify = SapbNotifyWindow();
    CShareAnnotationBar bar;
    bar.Show(kSapbAnchor, notify, true);
    REQUIRE(bar.IsWindow());
    const RECT full = SapbWindowRect(bar);
    bar.SetCollapsed(true);
    CHECK(bar.IsCollapsed());
    CHECK(bar.CollapseState() == top_bar_collapsed);
    RECT rc = SapbWindowRect(bar);
    CHECK(rc.right - rc.left == kSapbHandleW);
    CHECK(rc.bottom == full.bottom);
    CHECK((rc.left + rc.right) / 2 == (full.left + full.right) / 2);
    //小把手上只有一个显示当前工具图标的按钮
    balloonwjui::DuiButton* handle = SapbButton(bar, CShareAnnotationBar::kIdExpand);
    REQUIRE(handle != NULL);
    CHECK(handle->GetLeadingIcon() != NULL);
    CHECK(SapbButton(bar, CShareAnnotationBar::kIdTools) == NULL);
    CHECK(SapbButton(bar, CShareAnnotationBar::kIdPointer) == NULL);
    //鼠标移上去：临时展开成整条
    bar.TickCollapse(kSapbStartMs, true);
    CHECK(bar.CollapseState() == top_bar_peek);
    rc = SapbWindowRect(bar);
    CHECK(rc.right - rc.left == full.right - full.left);
    REQUIRE(SapbButton(bar, CShareAnnotationBar::kIdTools) != NULL);
    balloonwjui::DuiButton* collapse = SapbButton(bar, CShareAnnotationBar::kIdCollapse);
    REQUIRE(collapse != NULL);
    CHECK(balloonwjui::DuiToolTipMgr::Inst().GetText(collapse) == CString(TR(L"固定工具条")));
    //离开不满 2 秒不收，满了收回
    bar.TickCollapse(kSapbStartMs + kSapbShortLeaveMs, false);
    CHECK(bar.CollapseState() == top_bar_peek);
    bar.TickCollapse(kSapbStartMs + kSapbShortLeaveMs + kTopBarPeekHideMs, false);
    CHECK(bar.CollapseState() == top_bar_collapsed);
    //再临时展开、点「固定」：一直展开
    bar.TickCollapse(kSapbStartMs * 2, true);
    REQUIRE(bar.CollapseState() == top_bar_peek);
    SapbClick(bar.m_hWnd, CShareAnnotationBar::kIdCollapse);
    SapbPump();
    CHECK(bar.CollapseState() == top_bar_pinned);
    collapse = SapbButton(bar, CShareAnnotationBar::kIdCollapse);
    REQUIRE(collapse != NULL);
    CHECK(balloonwjui::DuiToolTipMgr::Inst().GetText(collapse) == CString(TR(L"收起工具条")));
    //收起后点小把手：展开并固定
    SapbClick(bar.m_hWnd, CShareAnnotationBar::kIdCollapse);
    SapbPump();
    REQUIRE(bar.CollapseState() == top_bar_collapsed);
    SapbClick(bar.m_hWnd, CShareAnnotationBar::kIdExpand);
    SapbPump();
    CHECK(bar.CollapseState() == top_bar_pinned);
    rc = SapbWindowRect(bar);
    CHECK(rc.right - rc.left == full.right - full.left);
    bar.Hide();
    ::DestroyWindow(notify);
}

TEST_CASE("SAPB4 select tool cursor shows move over objects and a cross elsewhere", "[annotpointer]")
{
    CHECK(AnnotSelectCursorName(true) == IDC_SIZEALL);
    CHECK(AnnotSelectCursorName(false) == IDC_CROSS);
}
