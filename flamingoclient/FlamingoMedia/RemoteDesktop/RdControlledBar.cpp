/**
 *  远程桌面被控端的顶部条，说明见 RdControlledBar.h。
 *  balloonwj@qq.com   2026-06-11
 */
#include "stdafx.h"
#include "RdControlledBar.h"

#include "RdMessages.h"             // remotedesktop::kMsgRdControlledBarCommand
#include "RdControlledStreamer.h"   // LinkPath：「P2P」徽章按推流器的链路路径显隐
#include "RemoteDesktopProtocolClient.h"   // net::rd_control_state
#include "../Share/ShareBarButtonArt.h"    // share_bar_control、share_bar_leave 等按钮种类
#include "../../Source/Common/Log/IULog.h"
#include "../../Source/Common/I18n/I18n.h"   // TR / TRF：界面文字的多语言查找
#include "../../Source/UI/BalloonMain/P2PBadge.h"   // ShouldShowP2PBadge

namespace {

//刷新「P2P」徽章的定时器 id 与周期（毫秒）：推流器在连接线程上等 P2P 链路（最长 20 秒），顶部条打开时多半还没连上。
//id 取较大值，避开基类收起 / 隐藏检查用的定时器
const UINT_PTR kP2PBadgeTimerId = 0x5250;
const UINT kP2PBadgeTimerMs = 1000;

//控制端显示名为空时状态文字里的称呼
std::wstring peerOrDefault(const std::wstring& peerName)
{
    return peerName.empty() ? std::wstring(TRC(L"远程桌面被控端顶部条里的称呼", L"对方")) : peerName;
}

//取主显示器的工作区：被控端只采集主显示器，顶部条放在它的顶部居中
RECT primaryWorkArea()
{
    RECT work = { 0, 0, 0, 0 };
    const POINT origin = { 0, 0 };
    HMONITOR mon = ::MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi;
    memset(&mi, 0, sizeof(mi));
    mi.cbSize = sizeof(mi);
    if (::GetMonitorInfo(mon, &mi))
        work = mi.rcWork;
    else
        ::SystemParametersInfo(SPI_GETWORKAREA, 0, &work, 0);
    return work;
}

} // namespace

std::wstring RdControlledStatusText(int controlState, const std::wstring& peerName)
{
    const std::wstring name = peerOrDefault(peerName);
    switch (controlState)
    {
    //控制端已申请、等本人同意
    case net::rd_control_requested:
        return TRF(L"{0} 申请控制你的电脑", name);
    //已授权：控制端的键鼠输入会注入本机
    case net::rd_control_granted:
        return TRF(L"{0} 正在控制你的电脑", name);
    //未授权（会话接通后的初始状态）与越界值：对方只能查看
    default:
        return TRF(L"{0} 正在查看你的桌面", name);
    }
}

std::vector<screenshare::ViewerBarButtonSpec> RdControlledBarButtons(int controlState)
{
    std::vector<screenshare::ViewerBarButtonSpec> out;
    //「允许控制 / 收回控制」：已授权时蓝底，有人申请时橙底提醒本人处理，其余灰底
    int state = screenshare::share_bar_tri_off;
    if (controlState == net::rd_control_granted)
        state = screenshare::share_bar_tri_on;
    else if (controlState == net::rd_control_requested)
        state = screenshare::share_bar_tri_pending;
    const std::wstring controlTip = (state == screenshare::share_bar_tri_on) ? TR(L"收回控制") : TR(L"允许控制");
    out.push_back(screenshare::ViewerBarButtonSpec(kRdBarControl, screenshare::share_bar_control, state, controlTip));
    out.push_back(screenshare::ViewerBarButtonSpec(kRdBarEnd, screenshare::share_bar_leave, screenshare::share_bar_tri_off,
                                                   TR(L"结束远程桌面")));
    return out;
}

screenshare::ShareToastTexts MakeRdControlRequestToastTexts()
{
    screenshare::ShareToastTexts texts;
    texts.m_title = TR(L"控制申请");
    texts.m_accept = TR(L"同意");
    texts.m_ignore = TR(L"拒绝");
    texts.m_message = remotedesktop::kMsgRdControlRequestAnswer;
    return texts;
}

std::wstring RdControlRequestToastText(const std::wstring& peerName)
{
    return RdControlledStatusText(net::rd_control_requested, peerName);
}

CRdControlledBar::CRdControlledBar()
    : m_notifyWnd(NULL)
    , m_controlState(net::rd_control_none)
    , m_linkSource(NULL)
{
    //没有所有者：置顶才不会被其他窗口挡住；被控方的桌面正被对方看着，本条不进入画面
    SetTopmost(true);
    SetCaptureExcluded(true);
}

CRdControlledBar::~CRdControlledBar()
{
    Close();
}

void CRdControlledBar::SetLinkPathSource(const remotedesktop::RdControlledStreamer* streamer)
{
    m_linkSource = streamer;
}

void CRdControlledBar::Open(const std::wstring& peerName, HWND notifyWnd)
{
    m_notifyWnd = notifyWnd;
    m_peerName = peerName;
    m_controlState = net::rd_control_none;
    SetLinkBadgeVisible(false);
    SetCaptionText(TR(L"远程桌面"));
    refresh();
    Show(NULL, notifyWnd, remotedesktop::kMsgRdControlledBarCommand, primaryWorkArea());
    if (!IsWindow())
    {
        LOG_ERROR("RdControlledBar: create window failed.");
        return;
    }
    if (m_linkSource != NULL)
        SetTimer(kP2PBadgeTimerId, kP2PBadgeTimerMs, NULL);
    LOG_INFO("RdControlledBar: opened.");
}

void CRdControlledBar::Close()
{
    if (IsWindow())
        KillTimer(kP2PBadgeTimerId);
    m_notifyWnd = NULL;
    Hide();
}

void CRdControlledBar::SetControlState(int controlState)
{
    if (controlState == m_controlState)
        return;
    LOG_INFO("RdControlledBar: control state %d -> %d.", m_controlState, controlState);
    m_controlState = controlState;
    refresh();
}

int CRdControlledBar::ControlState() const
{
    return m_controlState;
}

void CRdControlledBar::refresh()
{
    SetStatusText(RdControlledStatusText(m_controlState, m_peerName));
    SetBarButtons(RdControlledBarButtons(m_controlState));
}

LRESULT CRdControlledBar::OnTimerMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)lParam;
    if (wParam != kP2PBadgeTimerId)
    {
        bHandled = FALSE;
        return 0;
    }
    if (m_linkSource != NULL)
        SetLinkBadgeVisible(balloonmain::ShouldShowP2PBadge(m_linkSource->LinkPath()));
    return 0;
}
