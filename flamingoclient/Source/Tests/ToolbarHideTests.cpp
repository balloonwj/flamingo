/**
 *  录制条与共享控制条的「收起 / 隐藏」（标签 [barhide]），ToolbarHideTests.cpp
 *
 *  背景：docs/屏幕共享与录制方案.md 第 14.2 节（2026-10-07 用户确认）。共享控制条、标注工具条、录制条都要能「完全隐藏」：
 *  点「隐藏」后屏幕上不留任何东西，鼠标移到工具条原来所在的屏幕边缘时临时出现，离开约 2 秒后再隐藏，临时出现时点「固定」
 *  恢复常显；原有的「收起」（留小把手）保留。录制条原先没有「收起」，这次补上，小把手上显示闪烁的红点与录制时长。
 *  本文件覆盖：
 *    - BHID1 录制条有「隐藏」「收起」两个图标按钮；点「收起」缩成顶边正中的小把手（只剩标题栏），把手的标题写录制时长、
 *            标题栏图标是红点并随闪烁切换；鼠标移到把手上临时展开，点「固定」回到一直展开；
 *    - BHID2 录制条点「隐藏」后窗口不可见；鼠标在屏幕中间时一直隐藏；鼠标到了顶边的感应区时整条出现，离开满 2 秒再隐藏；
 *    - BHID3 共享控制条点「隐藏」后不可见，鼠标到顶边感应区时出现，点「固定」回到一直展开；
 *    - BHID4 两条工具条的「隐藏」按钮都有提示文字，说明隐藏后怎样重新出现；
 *    - BHID5 标注工具条（整屏共享时在屏幕底部）点「隐藏」后不可见，重新摆位也不出现；鼠标推到顶边不出现、推到底边才出现，
 *            离开满 2 秒再隐藏；退出标注后再进入（重新 Show）时重新显示。
 *  用例里的光标位置由测试接口 TickCollapseAt 直接给出，不移动真实鼠标；窗口建在屏幕外的参照矩形所在显示器上，测完即销毁。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-07
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <string>
#include "../../FlamingoMedia/Record/RecordBar.h"
#include "../../FlamingoMedia/Share/ShareAnnotationBar.h"
#include "../../FlamingoMedia/Share/ShareControlBar.h"
#include "../../FlamingoMedia/Share/ShareTopBarCollapse.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/Controls/Feedback/DuiToolTip.h"
#include "../../../third-party/balloonui/balloonui/DuiNotify.h"

using namespace screenshare;

namespace {

//参照矩形（屏幕坐标）：工具条摆在它所在显示器的顶部
const RECT kBhidAnchor = { 0, 0, 1600, 900 };
//起始时刻（毫秒），各步按它往后推
const int64_t kBhidT0 = 100000;

//收命令的通知窗口
HWND BhidNotifyWindow()
{
    return ::CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, ::GetModuleHandleW(NULL), NULL);
}

//按 id 找控件树里的按钮
balloonwjui::DuiButton* BhidButton(balloonwjui::DuiFrameWindow& wnd, UINT id)
{
    balloonwjui::DuiControl* content = wnd.GetClientContent();
    return content != NULL ? dynamic_cast<balloonwjui::DuiButton*>(content->FindCtrlById(id)) : NULL;
}

//模拟点一下按钮
void BhidClick(HWND wnd, UINT id)
{
    balloonwjui::DuiNotify n;
    n.code = static_cast<UINT>(balloonwjui::DUIN_CLICK);
    n.ctrlId = id;
    n.pCtrl = NULL;
    n.extra = 0;
    ::SendMessageW(wnd, WM_DUI_NOTIFY, static_cast<WPARAM>(id), reinterpret_cast<LPARAM>(&n));
}

//参照矩形所在显示器的整块矩形
RECT BhidMonitorRect()
{
    HMONITOR mon = ::MonitorFromRect(&kBhidAnchor, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi;
    memset(&mi, 0, sizeof(mi));
    mi.cbSize = sizeof(mi);
    ::GetMonitorInfo(mon, &mi);
    return mi.rcMonitor;
}

//窗口矩形
RECT BhidWindowRect(HWND wnd)
{
    RECT rc = { 0, 0, 0, 0 };
    ::GetWindowRect(wnd, &rc);
    return rc;
}

//矩形的中心点
POINT BhidCenter(const RECT& rc)
{
    POINT pt = { (rc.left + rc.right) / 2, (rc.top + rc.bottom) / 2 };
    return pt;
}

bool BhidSameRect(const RECT& a, const RECT& b)
{
    return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom;
}

//处理窗口自己投给自己的消息（标注工具条的收起 / 隐藏等点击处理完后才执行）
void BhidPump(HWND wnd)
{
    MSG msg;
    while (::PeekMessageW(&msg, wnd, 0, 0, PM_REMOVE))
    {
        ::TranslateMessage(&msg);
        ::DispatchMessageW(&msg);
    }
}

} // namespace

TEST_CASE("BHID1 record bar collapses to a handle with a blinking dot and the duration", "[barhide]")
{
    const HWND notify = BhidNotifyWindow();
    screenrecord::CRecordBar bar;
    bar.Show(kBhidAnchor, notify, 0);
    REQUIRE(bar.IsWindow());
    REQUIRE(BhidButton(bar, screenrecord::CRecordBar::kIdToggle) != NULL);
    REQUIRE(BhidButton(bar, screenrecord::CRecordBar::kIdHide) != NULL);
    bar.SetState(65000, false);
    const RECT full = BhidWindowRect(bar.m_hWnd);
    CHECK(bar.CollapseState() == top_bar_pinned);
    CHECK(bar.GetIcon() == NULL);

    //收起：只剩标题栏的小把手，在整条顶边正中
    BhidClick(bar.m_hWnd, screenrecord::CRecordBar::kIdToggle);
    CHECK(bar.CollapseState() == top_bar_collapsed);
    const RECT handle = BhidWindowRect(bar.m_hWnd);
    CHECK(::IsWindowVisible(bar.m_hWnd));
    CHECK(handle.top == full.top);
    CHECK(handle.bottom - handle.top == bar.GetTitleBarHeight());
    CHECK(handle.right - handle.left == kTopBarHandleW);
    CHECK((handle.left + handle.right) / 2 - (full.left + full.right) / 2 <= 1);
    //把手标题写录制时长，标题栏图标是红点，随闪烁切换
    CHECK(std::wstring(bar.GetTitle().GetString()).find(L"01:05") != std::wstring::npos);
    bar.TickBlink(0);
    const HBITMAP bright = bar.GetIcon();
    CHECK(bright != NULL);
    bar.TickBlink(kShareRecordBlinkMs);
    const HBITMAP dim = bar.GetIcon();
    CHECK(dim != NULL);
    CHECK(dim != bright);
    //时长变化时把手标题跟着变
    bar.SetState(125000, false);
    CHECK(std::wstring(bar.GetTitle().GetString()).find(L"02:05") != std::wstring::npos);

    //鼠标移到把手上：临时展开（整条），点「固定」回到一直展开
    bar.TickCollapseAt(kBhidT0, BhidCenter(handle));
    CHECK(bar.CollapseState() == top_bar_peek);
    CHECK(BhidSameRect(BhidWindowRect(bar.m_hWnd), full));
    CHECK(bar.GetIcon() == NULL);
    BhidClick(bar.m_hWnd, screenrecord::CRecordBar::kIdToggle);
    CHECK(bar.CollapseState() == top_bar_pinned);
    CHECK(BhidSameRect(BhidWindowRect(bar.m_hWnd), full));
    bar.Hide();
    ::DestroyWindow(notify);
}

TEST_CASE("BHID2 record bar hides and comes back at the screen edge", "[barhide]")
{
    const HWND notify = BhidNotifyWindow();
    screenrecord::CRecordBar bar;
    bar.Show(kBhidAnchor, notify, 0);
    REQUIRE(bar.IsWindow());
    const RECT full = BhidWindowRect(bar.m_hWnd);
    const RECT monitor = BhidMonitorRect();

    BhidClick(bar.m_hWnd, screenrecord::CRecordBar::kIdHide);
    CHECK(bar.CollapseState() == top_bar_hidden);
    CHECK_FALSE(::IsWindowVisible(bar.m_hWnd));
    //鼠标在屏幕中间：一直隐藏
    bar.TickCollapseAt(kBhidT0, BhidCenter(monitor));
    CHECK(bar.CollapseState() == top_bar_hidden);
    CHECK_FALSE(::IsWindowVisible(bar.m_hWnd));
    //鼠标推到工具条原位置上方的屏幕顶边：整条出现
    POINT edge = { (full.left + full.right) / 2, monitor.top };
    bar.TickCollapseAt(kBhidT0 + 1000, edge);
    CHECK(bar.CollapseState() == top_bar_hidden_peek);
    CHECK(::IsWindowVisible(bar.m_hWnd));
    CHECK(BhidSameRect(BhidWindowRect(bar.m_hWnd), full));
    //鼠标离开：从最后一次检查到鼠标在感应区里的时刻（kBhidT0 + 1000）算起，不足 2 秒仍在，满 2 秒再隐藏
    bar.TickCollapseAt(kBhidT0 + 1500, BhidCenter(monitor));
    bar.TickCollapseAt(kBhidT0 + 1000 + kTopBarPeekHideMs - 1, BhidCenter(monitor));
    CHECK(::IsWindowVisible(bar.m_hWnd));
    bar.TickCollapseAt(kBhidT0 + 1000 + kTopBarPeekHideMs, BhidCenter(monitor));
    CHECK(bar.CollapseState() == top_bar_hidden);
    CHECK_FALSE(::IsWindowVisible(bar.m_hWnd));
    //下一次录制重新显示时从一直展开开始
    bar.Hide();
    bar.Show(kBhidAnchor, notify, 0);
    CHECK(bar.CollapseState() == top_bar_pinned);
    CHECK(::IsWindowVisible(bar.m_hWnd));
    bar.Hide();
    ::DestroyWindow(notify);
}

TEST_CASE("BHID3 share control bar hides and comes back at the screen edge", "[barhide]")
{
    const HWND notify = BhidNotifyWindow();
    CShareControlBar bar;
    bar.Show(kBhidAnchor, notify);
    REQUIRE(bar.IsWindow());
    REQUIRE(BhidButton(bar, CShareControlBar::kIdHide) != NULL);
    const RECT full = BhidWindowRect(bar.m_hWnd);
    const RECT monitor = BhidMonitorRect();

    BhidClick(bar.m_hWnd, CShareControlBar::kIdHide);
    CHECK(bar.CollapseState() == top_bar_hidden);
    CHECK_FALSE(::IsWindowVisible(bar.m_hWnd));
    bar.TickCollapseAt(kBhidT0, BhidCenter(monitor));
    CHECK_FALSE(::IsWindowVisible(bar.m_hWnd));
    POINT edge = { (full.left + full.right) / 2, monitor.top };
    bar.TickCollapseAt(kBhidT0 + 500, edge);
    CHECK(bar.CollapseState() == top_bar_hidden_peek);
    CHECK(::IsWindowVisible(bar.m_hWnd));
    CHECK(BhidSameRect(BhidWindowRect(bar.m_hWnd), full));
    //临时出现时点「固定」：回到一直展开，鼠标离开也不再隐藏
    BhidClick(bar.m_hWnd, CShareControlBar::kIdToggle);
    CHECK(bar.CollapseState() == top_bar_pinned);
    bar.TickCollapseAt(kBhidT0 + 500 + kTopBarPeekHideMs * 3, BhidCenter(monitor));
    CHECK(::IsWindowVisible(bar.m_hWnd));
    //收起与隐藏并存：收起后仍是小把手
    BhidClick(bar.m_hWnd, CShareControlBar::kIdToggle);
    CHECK(bar.CollapseState() == top_bar_collapsed);
    CHECK(::IsWindowVisible(bar.m_hWnd));
    bar.Hide();
    ::DestroyWindow(notify);
}

TEST_CASE("BHID4 hide buttons explain how to bring the bar back", "[barhide]")
{
    const HWND notify = BhidNotifyWindow();
    screenrecord::CRecordBar rec;
    rec.Show(kBhidAnchor, notify, 0);
    CShareControlBar share;
    share.Show(kBhidAnchor, notify);
    balloonwjui::DuiButton* a = BhidButton(rec, screenrecord::CRecordBar::kIdHide);
    balloonwjui::DuiButton* b = BhidButton(share, CShareControlBar::kIdHide);
    REQUIRE(a != NULL);
    REQUIRE(b != NULL);
    CHECK(balloonwjui::DuiToolTipMgr::Inst().GetText(a) == CString(TopBarHideText()));
    CHECK(balloonwjui::DuiToolTipMgr::Inst().GetText(b) == CString(TopBarHideText()));
    CHECK(a->GetText().IsEmpty());
    CHECK(b->GetText().IsEmpty());
    rec.Hide();
    share.Hide();
    ::DestroyWindow(notify);
}

TEST_CASE("BHID5 annotation bar hides and comes back at the bottom edge", "[barhide]")
{
    const RECT monitor = BhidMonitorRect();
    CShareAnnotationBar bar;
    //整屏共享：参照矩形就是整块显示器，工具条放在底部
    bar.Show(monitor, NULL, false);
    REQUIRE(bar.IsWindow());
    balloonwjui::DuiButton* hideBtn = BhidButton(bar, CShareAnnotationBar::kIdHide);
    REQUIRE(hideBtn != NULL);
    CHECK(balloonwjui::DuiToolTipMgr::Inst().GetText(hideBtn) == CString(TopBarHideText()));
    const RECT full = BhidWindowRect(bar.m_hWnd);
    CHECK(monitor.bottom - full.bottom < full.top - monitor.top);

    BhidClick(bar.m_hWnd, CShareAnnotationBar::kIdHide);
    BhidPump(bar.m_hWnd);
    CHECK(bar.CollapseState() == top_bar_hidden);
    CHECK_FALSE(::IsWindowVisible(bar.m_hWnd));
    //共享的窗口移动等原因重新摆位：仍不显示
    bar.MoveTo(monitor);
    CHECK_FALSE(::IsWindowVisible(bar.m_hWnd));
    //鼠标推到顶边：工具条在底部，不出现
    POINT top = { (full.left + full.right) / 2, monitor.top };
    bar.TickCollapseAt(kBhidT0, top);
    CHECK(bar.CollapseState() == top_bar_hidden);
    //鼠标推到底边：出现
    POINT bottom = { (full.left + full.right) / 2, monitor.bottom - 1 };
    bar.TickCollapseAt(kBhidT0 + 500, bottom);
    CHECK(bar.CollapseState() == top_bar_hidden_peek);
    CHECK(::IsWindowVisible(bar.m_hWnd));
    CHECK(BhidSameRect(BhidWindowRect(bar.m_hWnd), full));
    //离开满 2 秒：再隐藏
    bar.TickCollapseAt(kBhidT0 + 1000, BhidCenter(monitor));
    bar.TickCollapseAt(kBhidT0 + 1000 + kTopBarPeekHideMs, BhidCenter(monitor));
    CHECK(bar.CollapseState() == top_bar_hidden);
    CHECK_FALSE(::IsWindowVisible(bar.m_hWnd));
    //退出标注后再进入：重新显示
    bar.Hide();
    bar.Show(monitor, NULL, false);
    CHECK(bar.CollapseState() == top_bar_pinned);
    CHECK(::IsWindowVisible(bar.m_hWnd));
    bar.Hide();
}
