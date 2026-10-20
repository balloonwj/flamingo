/**
 *  远程桌面被控端的控制权限状态（纯逻辑），说明见 RdControlledControl.h。
 *  balloonwj@qq.com   2026-10-12
 */
#include "RdControlledControl.h"

#include "RemoteDesktopProtocolClient.h"   // net::rd_control_action、net::rd_control_state

namespace remotedesktop {

RdControlledEffects::RdControlledEffects()
    : m_gate(rd_gate_keep)
    , m_sendAction(0)
    , m_toast(rd_toast_keep)
{
}

RdControlledControlState::RdControlledControlState()
    : m_state(net::rd_control_none)
{
}

int RdControlledControlState::State() const
{
    return m_state;
}

RdControlledEffects RdControlledControlState::make(int gate, int sendAction, int toast)
{
    RdControlledEffects e;
    e.m_gate = gate;
    e.m_sendAction = sendAction;
    e.m_toast = toast;
    return e;
}

RdControlledEffects RdControlledControlState::OnControlButton()
{
    //已授权：收回。先在本地关闭把关，不等应答，控制端此后的输入立即失效
    if (m_state == net::rd_control_granted)
    {
        m_state = net::rd_control_none;
        return make(rd_gate_close, net::rd_control_revoke, rd_toast_keep);
    }
    //未授权或有人申请：授予；应答成功后才打开把关。有申请提示时一并关闭
    const int toast = (m_state == net::rd_control_requested) ? rd_toast_hide : rd_toast_keep;
    return make(rd_gate_keep, net::rd_control_grant, toast);
}

RdControlledEffects RdControlledControlState::OnToastAnswer(bool accept)
{
    //提示已过时（对方已撤回、本人已从顶部条授予）：只关闭提示
    if (m_state != net::rd_control_requested)
        return make(rd_gate_keep, 0, rd_toast_hide);
    if (accept)
        return make(rd_gate_keep, net::rd_control_grant, rd_toast_hide);
    //拒绝：本地立即回到未授权，顶部条不再显示申请中
    m_state = net::rd_control_none;
    return make(rd_gate_keep, net::rd_control_deny, rd_toast_hide);
}

RdControlledEffects RdControlledControlState::OnNotify(int action, int control)
{
    switch (action)
    {
    //控制端申请：显示申请提示
    case net::rd_control_request:
        m_state = net::rd_control_requested;
        return make(rd_gate_keep, 0, rd_toast_show);
    //控制端撤回申请：关闭申请提示
    case net::rd_control_cancel:
        m_state = net::rd_control_none;
        return make(rd_gate_keep, 0, rd_toast_hide);
    //控制端放弃控制：关闭把关
    case net::rd_control_release:
        m_state = net::rd_control_none;
        return make(rd_gate_close, 0, rd_toast_hide);
    //其余动作不应推给被控端：按推送里的状态纠正，不是已授权就关闭把关
    default:
        if (!net::IsRdControlState(control))
            return make(rd_gate_keep, 0, rd_toast_keep);
        m_state = control;
        return make(control == net::rd_control_granted ? rd_gate_keep : rd_gate_close, 0,
                    control == net::rd_control_requested ? rd_toast_keep : rd_toast_hide);
    }
}

RdControlledEffects RdControlledControlState::OnResponse(int action, int code, int control)
{
    if (code == 0)
    {
        //授予成功：打开把关
        if (action == net::rd_control_grant)
        {
            m_state = net::rd_control_granted;
            return make(rd_gate_open, 0, rd_toast_hide);
        }
        //拒绝、收回成功：本地已在发送时改好，按服务端的状态确认一次
        m_state = net::rd_control_none;
        return make(rd_gate_keep, 0, rd_toast_keep);
    }
    //失败：按服务端当前的状态纠正；不是已授权就关闭把关（例如授予失败时控制端已撤回）
    if (!net::IsRdControlState(control))
        control = net::rd_control_none;
    m_state = control;
    const int toast = (control == net::rd_control_requested) ? rd_toast_keep : rd_toast_hide;
    return make(control == net::rd_control_granted ? rd_gate_keep : rd_gate_close, 0, toast);
}

RdControlledEffects RdControlledControlState::Reset()
{
    m_state = net::rd_control_none;
    return make(rd_gate_close, 0, rd_toast_hide);
}

} // namespace remotedesktop
