/**
 *  远程桌面控制端的控制权限状态（纯逻辑），说明见 RdViewerControl.h。
 *  balloonwj@qq.com   2026-10-12
 */
#include "stdafx.h"

#include "RdViewerControl.h"

#include "RemoteDesktopProtocolClient.h"   // net::rd_control_action、net::rd_control_state
#include "../Share/ShareBarButtonArt.h"    // share_bar_control、ShareBarTriState
#include "../../Source/Common/I18n/I18n.h"   // TR：界面文字的多语言查找

namespace remotedesktop {

RdViewerEffects::RdViewerEffects()
    : m_sendAction(0)
    , m_notice(rd_viewer_notice_none)
{
}

RdViewerControlState::RdViewerControlState()
    : m_state(net::rd_control_none)
{
}

int RdViewerControlState::State() const
{
    return m_state;
}

bool RdViewerControlState::CanSendInput() const
{
    return m_state == net::rd_control_granted;
}

RdViewerEffects RdViewerControlState::make(int sendAction, int notice)
{
    RdViewerEffects e;
    e.m_sendAction = sendAction;
    e.m_notice = notice;
    return e;
}

RdViewerEffects RdViewerControlState::OnControlButton()
{
    switch (m_state)
    {
    //申请中：撤回申请
    case net::rd_control_requested:
        m_state = net::rd_control_none;
        return make(net::rd_control_cancel, rd_viewer_notice_none);
    //已授权：结束控制，本地立即停止发送键鼠
    case net::rd_control_granted:
        m_state = net::rd_control_none;
        return make(net::rd_control_release, rd_viewer_notice_none);
    //未授权：申请，按钮马上变成「取消申请」，等被控端答复
    default:
        m_state = net::rd_control_requested;
        return make(net::rd_control_request, rd_viewer_notice_none);
    }
}

RdViewerEffects RdViewerControlState::OnNotify(int action, int control)
{
    switch (action)
    {
    //被控端同意申请或主动允许：开始发送键鼠
    case net::rd_control_grant:
        m_state = net::rd_control_granted;
        return make(0, rd_viewer_notice_granted);
    //被控端拒绝申请
    case net::rd_control_deny:
        m_state = net::rd_control_none;
        return make(0, rd_viewer_notice_denied);
    //被控端收回控制：立即停止发送
    case net::rd_control_revoke:
        m_state = net::rd_control_none;
        return make(0, rd_viewer_notice_revoked);
    //其余动作不应推给控制端：按推送里的状态纠正
    default:
        if (net::IsRdControlState(control))
            m_state = control;
        return make(0, rd_viewer_notice_none);
    }
}

RdViewerEffects RdViewerControlState::OnResponse(int action, int code, int control)
{
    (void)action;
    if (code == 0)
        return make(0, rd_viewer_notice_none);
    //失败：按服务端当前的状态纠正（例如会话还没接通、对方已收回）
    if (!net::IsRdControlState(control))
        control = net::rd_control_none;
    m_state = control;
    return make(0, rd_viewer_notice_failed);
}

void RdViewerControlState::Reset()
{
    m_state = net::rd_control_none;
}

screenshare::ViewerBarButtonSpec RdViewerControlButton(UINT id, int state)
{
    //提示文字留空：工具条按三态取与屏幕共享观看窗相同的默认提示（ShareBarTooltipForTriState）
    int triState = screenshare::share_bar_tri_off;
    //申请中：橙底「取消申请」
    if (state == net::rd_control_requested)
        triState = screenshare::share_bar_tri_pending;
    //已授权：蓝底「结束控制」
    else if (state == net::rd_control_granted)
        triState = screenshare::share_bar_tri_on;
    return screenshare::ViewerBarButtonSpec(id, screenshare::share_bar_control, triState, std::wstring());
}

std::wstring RdViewerStatusText(int state)
{
    if (state == net::rd_control_requested)
        return TR(L"申请中");
    if (state == net::rd_control_granted)
        return TR(L"控制中");
    return TR(L"仅查看");
}

std::wstring RdViewerNoticeText(int notice)
{
    switch (notice)
    {
    //被控端拒绝了申请
    case rd_viewer_notice_denied:
        return TR(L"对方拒绝了你的控制申请");
    //被控端收回了控制
    case rd_viewer_notice_revoked:
        return TR(L"对方收回了控制");
    //被控端同意了申请
    case rd_viewer_notice_granted:
        return TR(L"对方已允许控制，现在可以操作对方的电脑");
    //控制动作失败
    case rd_viewer_notice_failed:
        return TR(L"控制操作没有成功，已按对方的当前状态更新");
    //不提示
    default:
        return std::wstring();
    }
}

} // namespace remotedesktop
