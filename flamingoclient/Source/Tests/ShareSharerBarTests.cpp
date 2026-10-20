/**
 *  共享者控制条迁到顶部浮动条公共基类之后的外观与接线（标签 [sharerbar]），ShareSharerBarTests.cpp
 *
 *  背景：docs/远程桌面控制权限与观看工具条方案.md 8.2 第 6 条、8.4（2026-10-14 用户确认）。多人共享里主持人以外的共享者的
 *  控制条（CShareSharerBar）改为派生自 CShareTopBar，外观与主持人控制条一致：方形图标按钮加悬停提示、最右端「隐藏」
 *  「收起 / 固定」，「收回控制」仍是红色文字按钮，「更多」菜单仍由它自己弹。用例：
 *    - SSB1 按钮集合：停止共享、标注、更多、隐藏、收起 / 固定总有；屏幕当白板只在提供时出现；被控制时多出「收回控制」，
 *           状态文字以「某某 正在控制你的屏幕」开头；按钮都在窗口里、互不重叠；
 *    - SSB2 各按钮都有悬停提示，点击投出对应的 ShareSharerCommand；「更多」投给自己（等点击处理完再弹菜单），
 *           菜单的弹出点是「更多」按钮的左下角；
 *    - SSB3 收起、隐藏后鼠标推到屏幕顶边时临时出现（复用 TopBarCollapse，只测接线）。
 *  建真实窗口（不显示到前台），用例里的光标位置由 TickCollapseAt 直接给出，不移动真实鼠标。框架 Catch2 v3。
 *
 *  balloonwj@qq.com   2026-10-14
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <string>
#include "../../FlamingoMedia/Share/ShareMessages.h"
#include "../../FlamingoMedia/Share/ShareSharerBar.h"
#include "../Common/I18n/I18n.h"
#include "../Common/I18n/I18nLayout.h"   // MeasureUiTextWidth：SSB1 检查状态文字放得下
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/Controls/Feedback/DuiToolTip.h"
#include "../../../third-party/balloonui/balloonui/DuiNotify.h"

using namespace screenshare;

namespace {

//参照矩形（屏幕坐标）：落在主显示器上
const RECT kSsbAnchor = { 0, 0, 1600, 900 };
//SSB3 的起始时刻（毫秒），数值本身无关紧要
const int64_t kSsbT0 = 1000000;

HWND SsbNotifyWindow()
{
    return ::CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, ::GetModuleHandleW(NULL), NULL);
}

balloonwjui::DuiButton* SsbButton(CShareSharerBar& bar, UINT id)
{
    balloonwjui::DuiControl* content = bar.GetClientContent();
    return content != NULL ? dynamic_cast<balloonwjui::DuiButton*>(content->FindCtrlById(id)) : NULL;
}

void SsbClick(HWND wnd, UINT id)
{
    balloonwjui::DuiNotify n;
    n.code = static_cast<UINT>(balloonwjui::DUIN_CLICK);
    n.ctrlId = id;
    n.pCtrl = NULL;
    n.extra = 0;
    ::SendMessageW(wnd, WM_DUI_NOTIFY, static_cast<WPARAM>(id), reinterpret_cast<LPARAM>(&n));
}

//清空通知窗口（HWND_MESSAGE 窗口）消息队列里的消息。不要用在控制条窗口上：可见窗口有待重画区域时，PeekMessage
//取走 WM_PAINT 并不会使区域变为有效，会一直返回 WM_PAINT，循环不会结束
void SsbDrain(HWND wnd)
{
    MSG msg;
    while (::PeekMessageW(&msg, wnd, 0, 0, PM_REMOVE))
    {
    }
}

//取出投给控制条自己的「弹出更多菜单」（只取这一条消息，不处理，免得真的弹出菜单）；取到返回 true
bool SsbTakeMoreMenuMsg(HWND bar)
{
    MSG msg;
    bool got = false;
    while (::PeekMessageW(&msg, bar, CShareSharerBar::kMsgShowMoreMenu, CShareSharerBar::kMsgShowMoreMenu, PM_REMOVE))
        got = true;
    return got;
}

//取出发给通知窗口的一条控制条命令；没有时返回 -1
int SsbTakeCommand(HWND notify)
{
    MSG msg;
    int command = -1;
    while (::PeekMessageW(&msg, notify, kMsgShareSharerCommand, kMsgShareSharerCommand, PM_REMOVE))
        command = static_cast<int>(msg.wParam);
    return command;
}

//矩形中心
POINT SsbCenter(const RECT& rc)
{
    POINT pt = { (rc.left + rc.right) / 2, (rc.top + rc.bottom) / 2 };
    return pt;
}

} // namespace

TEST_CASE("SSB1 sharer bar buttons: stop, annotate, more, hide, collapse; board and revoke only when needed", "[sharerbar]")
{
    const HWND notify = SsbNotifyWindow();
    CShareSharerBar bar;
    bar.Show(kSsbAnchor, notify);
    REQUIRE(bar.IsWindow());
    CHECK(bar.TipTextForTest() == std::wstring(TR(L"你正在共享屏幕")));
    const UINT always[] = { CShareSharerBar::kIdStop, CShareSharerBar::kIdAnnotate, CShareSharerBar::kIdMore,
                            CShareSharerBar::kIdHide, CShareSharerBar::kIdToggle };
    for (size_t i = 0; i < sizeof(always) / sizeof(always[0]); ++i)
        CHECK(SsbButton(bar, always[i]) != NULL);
    //共享整块屏幕、没人控制：没有「屏幕当白板」与「收回控制」
    CHECK(SsbButton(bar, CShareSharerBar::kIdScreenBoard) == NULL);
    CHECK(SsbButton(bar, CShareSharerBar::kIdRevokeControl) == NULL);

    //只共享一个窗口：多出「屏幕当白板」；被控制：多出「收回控制」，文字以「某某 正在控制你的屏幕」开头
    bar.SetScreenBoard(true, false);
    bar.SetControlledBy(L"tom");
    CHECK(SsbButton(bar, CShareSharerBar::kIdScreenBoard) != NULL);
    balloonwjui::DuiButton* revoke = SsbButton(bar, CShareSharerBar::kIdRevokeControl);
    REQUIRE(revoke != NULL);
    //「收回控制」仍是红色文字按钮
    CHECK(revoke->GetText() == CString(TR(L"收回控制")));
    const std::wstring prefix = TRF(L"{0} 正在控制你的屏幕", std::wstring(L"tom"));
    CHECK(bar.TipTextForTest().compare(0, prefix.size(), prefix) == 0);
    //状态文字含控制者的名字，按实际文字宽度留位置，不被截成省略号（简体中文界面也要量）
    const RECT tipRc = bar.TipRectForTest();
    const int tipNeed = i18n::MeasureUiTextWidth(bar.TipTextForTest().c_str(), i18n::DefaultFontPixelHeight(), false);
    CHECK(tipRc.right - tipRc.left >= tipNeed);

    //全部按钮都在窗口里、互不重叠
    const UINT all[] = { CShareSharerBar::kIdRevokeControl, CShareSharerBar::kIdAnnotate,
                         CShareSharerBar::kIdScreenBoard, CShareSharerBar::kIdMore, CShareSharerBar::kIdStop,
                         CShareSharerBar::kIdHide, CShareSharerBar::kIdToggle };
    const size_t count = sizeof(all) / sizeof(all[0]);
    RECT client;
    ::GetClientRect(bar.m_hWnd, &client);
    for (size_t i = 0; i < count; ++i)
    {
        balloonwjui::DuiButton* a = SsbButton(bar, all[i]);
        REQUIRE(a != NULL);
        const RECT ra = a->GetRect();
        CHECK(ra.left >= client.left);
        CHECK(ra.right <= client.right);
        CHECK(ra.right > ra.left);
        for (size_t j = i + 1; j < count; ++j)
        {
            balloonwjui::DuiButton* b = SsbButton(bar, all[j]);
            REQUIRE(b != NULL);
            RECT rb = b->GetRect();
            RECT both;
            CHECK_FALSE(::IntersectRect(&both, &ra, &rb));
        }
    }
    bar.Hide();
    ::DestroyWindow(notify);
}

TEST_CASE("SSB2 sharer bar buttons have tooltips and post their commands; the more menu opens under its button", "[sharerbar]")
{
    const HWND notify = SsbNotifyWindow();
    CShareSharerBar bar;
    bar.SetScreenBoard(true, false);
    bar.Show(kSsbAnchor, notify);
    REQUIRE(bar.IsWindow());
    balloonwjui::DuiToolTipMgr& tips = balloonwjui::DuiToolTipMgr::Inst();

    //图标按钮都有悬停提示；按钮上不写文字
    const UINT iconIds[] = { CShareSharerBar::kIdStop, CShareSharerBar::kIdAnnotate, CShareSharerBar::kIdScreenBoard,
                             CShareSharerBar::kIdMore, CShareSharerBar::kIdHide, CShareSharerBar::kIdToggle };
    for (size_t i = 0; i < sizeof(iconIds) / sizeof(iconIds[0]); ++i)
    {
        balloonwjui::DuiButton* b = SsbButton(bar, iconIds[i]);
        REQUIRE(b != NULL);
        CHECK_FALSE(tips.GetText(b).IsEmpty());
        CHECK(b->GetText().IsEmpty());
    }
    //「屏幕当白板」进入前后提示不同
    CHECK(tips.GetText(SsbButton(bar, CShareSharerBar::kIdScreenBoard)) == CString(TR(L"屏幕当白板")));
    bar.SetScreenBoard(true, true);
    CHECK(tips.GetText(SsbButton(bar, CShareSharerBar::kIdScreenBoard)) == CString(TR(L"退出屏幕白板")));

    //点击投出对应的命令
    SsbDrain(notify);
    SsbClick(bar.m_hWnd, CShareSharerBar::kIdStop);
    CHECK(SsbTakeCommand(notify) == static_cast<int>(share_sharer_stop));
    SsbClick(bar.m_hWnd, CShareSharerBar::kIdAnnotate);
    CHECK(SsbTakeCommand(notify) == static_cast<int>(share_sharer_annotate));
    SsbClick(bar.m_hWnd, CShareSharerBar::kIdScreenBoard);
    CHECK(SsbTakeCommand(notify) == static_cast<int>(share_sharer_screen_board));

    //「更多」：投给自己（kMsgShowMoreMenu），不发给控制器；取出不处理，免得真的弹出菜单
    SsbTakeMoreMenuMsg(bar.m_hWnd);
    SsbClick(bar.m_hWnd, CShareSharerBar::kIdMore);
    CHECK(SsbTakeCommand(notify) == -1);
    CHECK(SsbTakeMoreMenuMsg(bar.m_hWnd));
    //菜单弹在「更多」按钮下方：弹出点是按钮的左下角（屏幕坐标）
    POINT anchor = { 0, 0 };
    REQUIRE(bar.GetMoreMenuAnchor(anchor));
    const RECT more = SsbButton(bar, CShareSharerBar::kIdMore)->GetRect();
    POINT expected = { more.left, more.bottom };
    ::ClientToScreen(bar.m_hWnd, &expected);
    CHECK(anchor.x == expected.x);
    CHECK(anchor.y == expected.y);

    //「收起 / 固定」由条自己处理（收起），不发给控制器
    SsbClick(bar.m_hWnd, CShareSharerBar::kIdToggle);
    CHECK(SsbTakeCommand(notify) == -1);
    CHECK(bar.CollapseState() == top_bar_collapsed);
    bar.Hide();
    ::DestroyWindow(notify);
}

TEST_CASE("SSB3 sharer bar collapses, hides, and peeks when the mouse reaches the top edge of the screen", "[sharerbar]")
{
    const HWND notify = SsbNotifyWindow();
    CShareSharerBar bar;
    bar.Show(kSsbAnchor, notify);
    REQUIRE(bar.IsWindow());
    CHECK(bar.CollapseState() == top_bar_pinned);
    RECT expanded;
    ::GetWindowRect(bar.m_hWnd, &expanded);

    //收起：缩成顶边正中的小把手
    SsbClick(bar.m_hWnd, CShareSharerBar::kIdToggle);
    CHECK(bar.CollapseState() == top_bar_collapsed);
    RECT handle;
    ::GetWindowRect(bar.m_hWnd, &handle);
    CHECK(handle.right - handle.left < expanded.right - expanded.left);
    //鼠标移到小把手上：临时展开成整条；此时按钮是「固定」，点了回到一直展开
    bar.TickCollapseAt(kSsbT0, SsbCenter(handle));
    CHECK(bar.CollapseState() == top_bar_peek);
    SsbClick(bar.m_hWnd, CShareSharerBar::kIdToggle);
    CHECK(bar.CollapseState() == top_bar_pinned);

    //隐藏：窗口不可见；鼠标推到所在显示器的顶边（条的正上方）时临时出现
    SsbClick(bar.m_hWnd, CShareSharerBar::kIdHide);
    CHECK(bar.CollapseState() == top_bar_hidden);
    CHECK_FALSE(::IsWindowVisible(bar.m_hWnd));
    MONITORINFO mi;
    memset(&mi, 0, sizeof(mi));
    mi.cbSize = sizeof(mi);
    REQUIRE(::GetMonitorInfo(::MonitorFromRect(&kSsbAnchor, MONITOR_DEFAULTTOPRIMARY), &mi));
    POINT top = SsbCenter(expanded);
    top.y = mi.rcMonitor.top;
    bar.TickCollapseAt(kSsbT0, top);
    CHECK(bar.CollapseState() == top_bar_hidden_peek);
    CHECK(::IsWindowVisible(bar.m_hWnd) != FALSE);
    bar.Hide();
    ::DestroyWindow(notify);
}
