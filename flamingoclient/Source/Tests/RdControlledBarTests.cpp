/**
 *  远程桌面被控端顶部条的单元测试（标签 [rdctl]），RdControlledBarTests.cpp
 *  背景：2026-10-12 起远程桌面接通后默认只能查看，控制要经被控端同意（docs/远程桌面控制权限与观看工具条方案.md 第 4.2 节），
 *  被控端顶部条改成屏幕共享控制条的样式，状态文字分三种，「允许控制 / 收回控制」随状态切换。与用户确认的用例：
 *    - RDC5 顶部条布局：按钮都在窗口内、互不重叠、宽 30、都有悬停提示；三种状态文字正确；「允许控制 / 收回控制」
 *      随状态切换外观与提示；点按钮时通知窗口收到 kMsgRdControlledBarCommand；
 *    - RDC6 顶部条置顶、不抢焦点、不进任务栏，并排除在屏幕采集之外（读 GetWindowDisplayAffinity）；
 *    - RDC7 申请提示：文字为「某某 申请控制你的电脑」，按钮「同意 / 拒绝」，点按钮通知控制器；收到申请时显示，控制端撤回时关闭，
 *      同意发 grant、拒绝发 deny；
 *    - RDC8 被控端点「收回控制」时，本地把关先关闭，再发 revoke；授予要等应答成功才打开把关。
 *  RDC5 的窗口部分与 RDC6 会建出真实的窗口（显示在主显示器顶部），须在隐藏桌面上运行。框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-12
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <string>
#include <vector>
#include "../../FlamingoMedia/RemoteDesktop/RdControlledBar.h"
#include "../../FlamingoMedia/RemoteDesktop/RdControlledControl.h"
#include "../../FlamingoMedia/RemoteDesktop/RdMessages.h"
#include "../../FlamingoMedia/RemoteDesktop/RemoteDesktopProtocolClient.h"
#include "../../FlamingoMedia/Share/ShareBarButtonArt.h"
#include "../../../third-party/balloonui/balloonui/DuiNotify.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../Common/I18n/I18n.h"

using namespace screenshare;
using namespace remotedesktop;

namespace {

//用例里控制端的显示名
const wchar_t* const kRdcPeer = L"Alice";

//收命令的通知窗口
HWND RdcNotifyWindow()
{
    return ::CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, ::GetModuleHandleW(NULL), NULL);
}

//模拟点一下顶部条上的按钮（与 balloonui 按钮发出的点击通知相同）
void RdcClick(HWND bar, UINT id)
{
    balloonwjui::DuiNotify n;
    n.code = static_cast<UINT>(balloonwjui::DUIN_CLICK);
    n.ctrlId = id;
    n.pCtrl = NULL;
    n.extra = 0;
    ::SendMessageW(bar, WM_DUI_NOTIFY, static_cast<WPARAM>(id), reinterpret_cast<LPARAM>(&n));
}

//两个矩形是否有重叠
bool RdcOverlap(const RECT& a, const RECT& b)
{
    RECT r;
    return ::IntersectRect(&r, &a, &b) != FALSE;
}

} // namespace

TEST_CASE("RDC5 controlled bar texts, buttons and layout follow the control state", "[rdctl]")
{
    //状态文字：三种控制状态各一种；没有名字时写「对方」
    CHECK(RdControlledStatusText(net::rd_control_none, kRdcPeer) == TRF(L"{0} 正在查看你的桌面", kRdcPeer));
    CHECK(RdControlledStatusText(net::rd_control_requested, kRdcPeer) == TRF(L"{0} 申请控制你的电脑", kRdcPeer));
    CHECK(RdControlledStatusText(net::rd_control_granted, kRdcPeer) == TRF(L"{0} 正在控制你的电脑", kRdcPeer));
    CHECK(RdControlledStatusText(net::rd_control_none, L"") == TRF(L"{0} 正在查看你的桌面", TRC(L"远程桌面被控端顶部条里的称呼", L"对方")));

    //按钮：「允许控制 / 收回控制」随状态换外观与提示，「结束远程桌面」不变
    const int states[] = { net::rd_control_none, net::rd_control_requested, net::rd_control_granted };
    const int looks[] = { share_bar_tri_off, share_bar_tri_pending, share_bar_tri_on };
    for (size_t i = 0; i < sizeof(states) / sizeof(states[0]); ++i)
    {
        const std::vector<ViewerBarButtonSpec> b = RdControlledBarButtons(states[i]);
        REQUIRE(b.size() == 2);
        CHECK(b[0].m_id == static_cast<UINT>(kRdBarControl));
        CHECK(b[0].m_action == share_bar_control);
        CHECK(b[0].m_state == looks[i]);
        CHECK(b[0].m_tooltip == (states[i] == net::rd_control_granted ? TR(L"收回控制") : TR(L"允许控制")));
        CHECK(b[1].m_id == static_cast<UINT>(kRdBarEnd));
        CHECK(b[1].m_action == share_bar_leave);
        CHECK(b[1].m_tooltip == TR(L"结束远程桌面"));
    }

    const HWND notify = RdcNotifyWindow();
    REQUIRE(notify != NULL);
    {
        CRdControlledBar bar;
        bar.Open(kRdcPeer, notify);
        REQUIRE(bar.IsWindow());
        CHECK(bar.StatusTextForTest() == RdControlledStatusText(net::rd_control_none, kRdcPeer));

        //四个按钮都在窗口内、宽 30、互不重叠
        RECT wnd;
        ::GetWindowRect(bar.m_hWnd, &wnd);
        const UINT ids[] = { static_cast<UINT>(kRdBarControl), static_cast<UINT>(kRdBarEnd),
                             static_cast<UINT>(CViewerFloatBar::kIdHide), static_cast<UINT>(CViewerFloatBar::kIdToggle) };
        const size_t count = sizeof(ids) / sizeof(ids[0]);
        std::vector<RECT> rects;
        for (size_t i = 0; i < count; ++i)
        {
            RECT rc;
            REQUIRE(bar.GetButtonScreenRect(ids[i], rc));
            CHECK(rc.right - rc.left == kShareBarBtnPx);
            CHECK(rc.left >= wnd.left);
            CHECK(rc.right <= wnd.right);
            CHECK(rc.top >= wnd.top);
            CHECK(rc.bottom <= wnd.bottom);
            rects.push_back(rc);
        }
        for (size_t i = 0; i < count; ++i)
        {
            for (size_t k = i + 1; k < count; ++k)
                CHECK_FALSE(RdcOverlap(rects[i], rects[k]));
        }

        //状态变化：文字随之更新
        bar.SetControlState(net::rd_control_requested);
        CHECK(bar.StatusTextForTest() == RdControlledStatusText(net::rd_control_requested, kRdcPeer));
        bar.SetControlState(net::rd_control_granted);
        CHECK(bar.StatusTextForTest() == RdControlledStatusText(net::rd_control_granted, kRdcPeer));
        CHECK(bar.ControlState() == net::rd_control_granted);

        //点按钮：通知窗口收到命令，wParam 为按钮编号（按钮的控件通知发回控制条自身，再由控制条把命令投给
        //通知窗口；开发约定文档「UI 约定」第 11 条）
        RdcClick(bar.m_hWnd, static_cast<UINT>(kRdBarEnd));
        MSG msg;
        REQUIRE(::PeekMessageW(&msg, notify, remotedesktop::kMsgRdControlledBarCommand,
                               remotedesktop::kMsgRdControlledBarCommand, PM_REMOVE));
        CHECK(msg.wParam == static_cast<WPARAM>(kRdBarEnd));

        //重新打开：控制状态从未授权开始
        bar.Open(kRdcPeer, notify);
        CHECK(bar.ControlState() == net::rd_control_none);
        bar.Close();
        CHECK_FALSE(bar.IsWindow());
    }
    ::DestroyWindow(notify);
}

TEST_CASE("RDC6 controlled bar is topmost, never activates and is excluded from capture", "[rdctl]")
{
    const HWND notify = RdcNotifyWindow();
    REQUIRE(notify != NULL);
    {
        CRdControlledBar bar;
        bar.Open(kRdcPeer, notify);
        REQUIRE(bar.IsWindow());
        const LONG_PTR ex = ::GetWindowLongPtrW(bar.m_hWnd, GWL_EXSTYLE);
        CHECK((ex & WS_EX_TOPMOST) != 0);
        CHECK((ex & WS_EX_NOACTIVATE) != 0);
        CHECK((ex & WS_EX_TOOLWINDOW) != 0);
        //没有所有者，理由见 RdControlledBar.cpp 构造函数的注释；有无所有者，按钮通知都发回控制条自身
        //（开发约定文档「UI 约定」第 11 条）
        CHECK(::GetWindow(bar.m_hWnd, GW_OWNER) == NULL);
        DWORD affinity = WDA_NONE;
        REQUIRE(::GetWindowDisplayAffinity(bar.m_hWnd, &affinity));
        CHECK(affinity != WDA_NONE);
        bar.Close();
    }
    ::DestroyWindow(notify);
}

TEST_CASE("RDC7 control request toast: texts, buttons, shown on request and closed on cancel", "[rdctl]")
{
    //文字与按钮
    const ShareToastTexts texts = MakeRdControlRequestToastTexts();
    CHECK(texts.m_title == TR(L"控制申请"));
    CHECK(texts.m_accept == TR(L"同意"));
    CHECK(texts.m_ignore == TR(L"拒绝"));
    CHECK(texts.m_message == kMsgRdControlRequestAnswer);
    CHECK(RdControlRequestToastText(kRdcPeer) == TRF(L"{0} 申请控制你的电脑", kRdcPeer));

    //收到申请：显示提示，顶部条为申请中；控制端撤回：关闭提示，回到未授权
    RdControlledControlState s;
    RdControlledEffects e = s.OnNotify(net::rd_control_request, net::rd_control_requested);
    CHECK(e.m_toast == rd_toast_show);
    CHECK(s.State() == net::rd_control_requested);
    CHECK(e.m_sendAction == 0);
    e = s.OnNotify(net::rd_control_cancel, net::rd_control_none);
    CHECK(e.m_toast == rd_toast_hide);
    CHECK(s.State() == net::rd_control_none);
    //撤回之后才点提示上的按钮：提示已过时，只关闭，不发送
    e = s.OnToastAnswer(true);
    CHECK(e.m_sendAction == 0);
    CHECK(e.m_toast == rd_toast_hide);
    //同意：发 grant、关闭提示；把关等应答成功才打开
    s.OnNotify(net::rd_control_request, net::rd_control_requested);
    e = s.OnToastAnswer(true);
    CHECK(e.m_sendAction == net::rd_control_grant);
    CHECK(e.m_toast == rd_toast_hide);
    CHECK(e.m_gate == rd_gate_keep);
    //拒绝（含 60 秒无操作）：发 deny，回到未授权
    RdControlledControlState d;
    d.OnNotify(net::rd_control_request, net::rd_control_requested);
    e = d.OnToastAnswer(false);
    CHECK(e.m_sendAction == net::rd_control_deny);
    CHECK(d.State() == net::rd_control_none);

    //真实的提示窗口：两个按钮的文字，点「拒绝」时控制器的消息窗口收到答复
    const HWND notify = RdcNotifyWindow();
    REQUIRE(notify != NULL);
    {
        CShareCornerToast toast;
        toast.Show(RdControlRequestToastText(kRdcPeer), 0, notify, 1, texts);
        REQUIRE(toast.IsWindow());
        balloonwjui::DuiControl* content = toast.GetClientContent();
        REQUIRE(content != NULL);
        balloonwjui::DuiButton* accept =
            dynamic_cast<balloonwjui::DuiButton*>(content->FindCtrlById(CShareCornerToast::kIdJoin));
        balloonwjui::DuiButton* deny =
            dynamic_cast<balloonwjui::DuiButton*>(content->FindCtrlById(CShareCornerToast::kIdIgnore));
        REQUIRE(accept != NULL);
        REQUIRE(deny != NULL);
        CHECK(std::wstring(accept->GetText()) == TR(L"同意"));
        CHECK(std::wstring(deny->GetText()) == TR(L"拒绝"));
        RdcClick(toast.m_hWnd, static_cast<UINT>(CShareCornerToast::kIdIgnore));
        MSG msg;
        REQUIRE(::PeekMessageW(&msg, notify, kMsgRdControlRequestAnswer, kMsgRdControlRequestAnswer, PM_REMOVE));
        CHECK(msg.wParam == static_cast<WPARAM>(CShareCornerToast::kCmdIgnore));
        toast.Hide();
    }
    ::DestroyWindow(notify);
}

TEST_CASE("RDC8 revoking closes the gate before sending, granting opens it only after the response", "[rdctl]")
{
    RdControlledControlState s;
    //没人申请时直接允许：发 grant，把关不变
    RdControlledEffects e = s.OnControlButton();
    CHECK(e.m_sendAction == net::rd_control_grant);
    CHECK(e.m_gate == rd_gate_keep);
    CHECK(s.State() == net::rd_control_none);
    //应答成功：打开把关，顶部条为已授权
    e = s.OnResponse(net::rd_control_grant, 0, net::rd_control_granted);
    CHECK(e.m_gate == rd_gate_open);
    CHECK(s.State() == net::rd_control_granted);
    //收回：同一个结果里先关闭把关、再发 revoke（控制器按成员顺序执行），本地立即回到未授权
    e = s.OnControlButton();
    CHECK(e.m_gate == rd_gate_close);
    CHECK(e.m_sendAction == net::rd_control_revoke);
    CHECK(s.State() == net::rd_control_none);
    //revoke 的应答不再改动把关
    e = s.OnResponse(net::rd_control_revoke, 0, net::rd_control_none);
    CHECK(e.m_gate == rd_gate_keep);

    //授予失败（例如控制端已撤回、服务端回 742）：按服务端状态纠正，不打开把关
    RdControlledControlState f;
    f.OnNotify(net::rd_control_request, net::rd_control_requested);
    f.OnToastAnswer(true);
    e = f.OnResponse(net::rd_control_grant, 742, net::rd_control_none);
    CHECK(e.m_gate == rd_gate_close);
    CHECK(f.State() == net::rd_control_none);

    //控制端放弃控制：关闭把关
    RdControlledControlState r;
    r.OnControlButton();
    r.OnResponse(net::rd_control_grant, 0, net::rd_control_granted);
    e = r.OnNotify(net::rd_control_release, net::rd_control_none);
    CHECK(e.m_gate == rd_gate_close);
    CHECK(r.State() == net::rd_control_none);

    //会话结束、锁屏：关闭把关与申请提示
    r.OnControlButton();
    r.OnResponse(net::rd_control_grant, 0, net::rd_control_granted);
    e = r.Reset();
    CHECK(e.m_gate == rd_gate_close);
    CHECK(e.m_toast == rd_toast_hide);
    CHECK(r.State() == net::rd_control_none);
}
