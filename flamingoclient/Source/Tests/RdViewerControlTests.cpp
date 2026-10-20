/**
 *  远程桌面控制端的控制权限与观看窗浮动工具条单元测试（标签 [rdctl]），RdViewerControlTests.cpp
 *  背景：2026-10-12 起远程桌面接通后默认只能查看，控制端申请、被控端同意后才发送键鼠；观看窗的按钮从标题栏图标与全屏工具条
 *  改为一条浮动工具条（docs/远程桌面控制权限与观看工具条方案.md 第 4.5 节）。与用户确认的用例：
 *    - RDC9 RdViewerControlState 状态转换：未授权 → 申请中 → 已授权；拒绝、收回、会话结束回到未授权；各状态下按钮的外观与提示文字；
 *    - RDC10 未授权或申请中时观看窗不发送任何键鼠，已授权后才发送（观看窗按 RdViewerControlState::CanSendInput 判断）；
 *    - RDC11 远程桌面观看窗的浮动条按钮集合：申请控制、全屏、断开、隐藏、收起；标题栏上没有自定义图标。
 *  RDC10、RDC11 会建出真实的观看窗与浮动工具条（不连传输），须在隐藏桌面上运行。框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-12
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <stdio.h>
#include <string>
#include <vector>
#include "../../FlamingoMedia/RemoteDesktop/RdViewerWindow.h"
#include "../../FlamingoMedia/RemoteDesktop/RdViewerControl.h"
#include "../../FlamingoMedia/RemoteDesktop/RdSessionActions.h"
#include "../../FlamingoMedia/RemoteDesktop/RemoteDesktopProtocolClient.h"
#include "../../FlamingoMedia/Share/ShareBarButtonArt.h"
#include "../Common/I18n/I18n.h"

using namespace remotedesktop;
using namespace screenshare;

namespace {

//用例里的会话 id
const char* const kRvcSession = "rvc-session";
//读源码的块大小（字节）与本文件到仓库 flamingoclient 目录的层数（Source/Tests/本文件）
const size_t kRvcReadChunk = 4096;
const int kRvcDepth = 3;

//记下观看窗交出来的控制动作，代替远程桌面控制器
class RvcFakeActions : public IRdSessionActions
{
public:
    RvcFakeActions()
        : m_lastAction(0)
        , m_closed(0)
    {
    }
    virtual void CancelInvite(const std::string& sessionId) override
    {
        (void)sessionId;
    }
    virtual void RespondInvite(const std::string& sessionId, bool accept) override
    {
        (void)sessionId;
        (void)accept;
    }
    virtual void CloseSession(const std::string& sessionId) override
    {
        (void)sessionId;
        ++m_closed;
    }
    virtual void SendControl(const std::string& sessionId, int action) override
    {
        m_lastSession = sessionId;
        m_lastAction = action;
    }

    std::string m_lastSession;   //最近一次 SendControl 的会话 id
    int         m_lastAction;    //最近一次 SendControl 的动作
    int         m_closed;        //CloseSession 被调用的次数
};

//1719 推送
net::RDControlNotify RvcNotify(int action, int control)
{
    net::RDControlNotify n;
    n.m_sessionId = kRvcSession;
    n.m_action = action;
    n.m_control = control;
    return n;
}

//读 flamingoclient 下的一个源文件
std::string RvcReadSource(const std::string& rel)
{
    std::string self(__FILE__);
    for (int i = 0; i < kRvcDepth; ++i)
    {
        const size_t pos = self.find_last_of("\\/");
        if (pos == std::string::npos)
            return std::string();
        self = self.substr(0, pos);
    }
    std::string data;
    FILE* f = NULL;
    if (fopen_s(&f, (self + rel).c_str(), "rb") != 0 || f == NULL)
        return data;
    char buf[kRvcReadChunk];
    size_t n = 0;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
        data.append(buf, n);
    fclose(f);
    return data;
}

} // namespace

TEST_CASE("RDC9 viewer control state transitions and the control button look", "[rdctl]")
{
    RdViewerControlState s;
    CHECK(s.State() == net::rd_control_none);
    //申请：发 request，变为申请中
    RdViewerEffects e = s.OnControlButton();
    CHECK(e.m_sendAction == net::rd_control_request);
    CHECK(s.State() ==net::rd_control_requested);
    //申请中再点：撤回
    e = s.OnControlButton();
    CHECK(e.m_sendAction == net::rd_control_cancel);
    CHECK(s.State() ==net::rd_control_none);
    //再申请，被控端同意：已授权并提示
    s.OnControlButton();
    e = s.OnNotify(net::rd_control_grant, net::rd_control_granted);
    CHECK(s.State() ==net::rd_control_granted);
    CHECK(e.m_notice == rd_viewer_notice_granted);
    //被控端收回：回到未授权并提示
    e = s.OnNotify(net::rd_control_revoke, net::rd_control_none);
    CHECK(s.State() ==net::rd_control_none);
    CHECK(e.m_notice == rd_viewer_notice_revoked);
    //申请后被拒绝
    s.OnControlButton();
    e = s.OnNotify(net::rd_control_deny, net::rd_control_none);
    CHECK(s.State() ==net::rd_control_none);
    CHECK(e.m_notice == rd_viewer_notice_denied);
    //已授权时自己结束：发 release
    s.OnControlButton();
    s.OnNotify(net::rd_control_grant, net::rd_control_granted);
    e = s.OnControlButton();
    CHECK(e.m_sendAction == net::rd_control_release);
    CHECK(s.State() ==net::rd_control_none);
    //申请失败（例如服务端回 742）：按服务端状态纠正并提示
    s.OnControlButton();
    e = s.OnResponse(net::rd_control_request, 742, net::rd_control_none);
    CHECK(s.State() ==net::rd_control_none);
    CHECK(e.m_notice == rd_viewer_notice_failed);
    //会话结束
    s.OnControlButton();
    s.Reset();
    CHECK(s.State() == net::rd_control_none);

    //按钮外观与提示、状态文字：提示留空，由工具条按三态取默认文字
    const ViewerBarButtonSpec off = RdViewerControlButton(1, net::rd_control_none);
    CHECK(off.m_action == share_bar_control);
    CHECK(off.m_state == share_bar_tri_off);
    CHECK(off.m_tooltip.empty());
    CHECK(ShareBarTooltipForTriState(off.m_action, off.m_state) == TR(L"申请控制"));
    const ViewerBarButtonSpec pending = RdViewerControlButton(1, net::rd_control_requested);
    CHECK(pending.m_state == share_bar_tri_pending);
    CHECK(ShareBarTooltipForTriState(pending.m_action, pending.m_state) == TR(L"取消申请"));
    const ViewerBarButtonSpec on = RdViewerControlButton(1, net::rd_control_granted);
    CHECK(on.m_state == share_bar_tri_on);
    CHECK(ShareBarTooltipForTriState(on.m_action, on.m_state) == TR(L"结束控制"));
    CHECK(RdViewerStatusText(net::rd_control_none) == TR(L"仅查看"));
    CHECK(RdViewerStatusText(net::rd_control_requested) == TR(L"申请中"));
    CHECK(RdViewerStatusText(net::rd_control_granted) == TR(L"控制中"));
    CHECK_FALSE(RdViewerNoticeText(rd_viewer_notice_denied).empty());
    CHECK(RdViewerNoticeText(rd_viewer_notice_none).empty());
}

TEST_CASE("RDC10 the viewer sends input only after control is granted", "[rdctl]")
{
    //纯逻辑：只有已授权时可以发送
    RdViewerControlState s;
    CHECK_FALSE(s.CanSendInput());
    s.OnControlButton();
    CHECK_FALSE(s.CanSendInput());
    s.OnNotify(net::rd_control_grant, net::rd_control_granted);
    CHECK(s.CanSendInput());
    s.OnNotify(net::rd_control_revoke, net::rd_control_none);
    CHECK_FALSE(s.CanSendInput());

    //观看窗：按钮交出控制动作，推送改变能否发送
    RvcFakeActions actions;
    CRdViewerWindow wnd;
    wnd.SetActions(&actions);
    REQUIRE(wnd.OpenForTest(kRvcSession));
    CHECK_FALSE(wnd.IsInputAllowedForTest());
    CHECK(wnd.BarStatusForTest() == TR(L"仅查看"));
    ::SendMessageW(wnd.m_hWnd, CRdViewerWindow::WM_RD_VIEWER_BAR_CMD, static_cast<WPARAM>(kRdViewBarControl), 0);
    CHECK(actions.m_lastAction == net::rd_control_request);
    CHECK(actions.m_lastSession == kRvcSession);
    CHECK_FALSE(wnd.IsInputAllowedForTest());
    CHECK(wnd.BarStatusForTest() == TR(L"申请中"));
    //别的会话的推送不影响本窗口
    net::RDControlNotify other = RvcNotify(net::rd_control_grant, net::rd_control_granted);
    other.m_sessionId = "other";
    wnd.OnControlNotify(other);
    CHECK_FALSE(wnd.IsInputAllowedForTest());
    //被控端同意：可以发送，状态区先显示提示
    wnd.OnControlNotify(RvcNotify(net::rd_control_grant, net::rd_control_granted));
    CHECK(wnd.IsInputAllowedForTest());
    CHECK(wnd.BarStatusForTest() == RdViewerNoticeText(rd_viewer_notice_granted));
    //自己结束控制：发 release，本地立即停止发送
    ::SendMessageW(wnd.m_hWnd, CRdViewerWindow::WM_RD_VIEWER_BAR_CMD, static_cast<WPARAM>(kRdViewBarControl), 0);
    CHECK(actions.m_lastAction == net::rd_control_release);
    CHECK_FALSE(wnd.IsInputAllowedForTest());
    wnd.SetActions(NULL);
    wnd.CloseViewer();
}

TEST_CASE("RDC11 remote desktop viewer bar buttons and no caption icons", "[rdctl]")
{
    CRdViewerWindow wnd;
    REQUIRE(wnd.OpenForTest(kRvcSession));
    const HWND bar = wnd.BarWindowForTest();
    REQUIRE(bar != NULL);
    RECT barRect;
    ::GetWindowRect(bar, &barRect);
    const UINT ids[] = { static_cast<UINT>(kRdViewBarControl), static_cast<UINT>(kRdViewBarFullscreen),
                         static_cast<UINT>(kRdViewBarDisconnect), static_cast<UINT>(CViewerFloatBar::kIdHide),
                         static_cast<UINT>(CViewerFloatBar::kIdToggle) };
    const size_t count = sizeof(ids) / sizeof(ids[0]);
    std::vector<RECT> rects;
    for (size_t i = 0; i < count; ++i)
    {
        RECT rc;
        REQUIRE(wnd.BarButtonRectForTest(ids[i], rc));
        CHECK(rc.right - rc.left == kShareBarBtnPx);
        CHECK(rc.left >= barRect.left);
        CHECK(rc.right <= barRect.right);
        rects.push_back(rc);
    }
    //从左到右依次排列、互不重叠
    for (size_t i = 1; i < count; ++i)
        CHECK(rects[i - 1].right <= rects[i].left);
    //点「全屏」：进入全屏，工具条贴在显示器顶部
    ::SendMessageW(wnd.m_hWnd, CRdViewerWindow::WM_RD_VIEWER_BAR_CMD, static_cast<WPARAM>(kRdViewBarFullscreen), 0);
    CHECK(wnd.IsFullscreen());
    ::SendMessageW(wnd.m_hWnd, CRdViewerWindow::WM_RD_VIEWER_BAR_CMD, static_cast<WPARAM>(kRdViewBarFullscreen), 0);
    CHECK_FALSE(wnd.IsFullscreen());
    wnd.CloseViewer();
    CHECK(wnd.BarWindowForTest() == NULL);

    //源码守卫：标题栏不再放自定义图标，也不再有全屏工具条
    const std::string src = RvcReadSource("\\FlamingoMedia\\RemoteDesktop\\RdViewerWindow.cpp");
    REQUIRE_FALSE(src.empty());
    CHECK(src.find("AddCaptionIcon(") == std::string::npos);
    CHECK(src.find("DUIFW_CAPTION_ICON_CLICK") == std::string::npos);
    CHECK(src.find("CRdViewerToolbar") == std::string::npos);
}
