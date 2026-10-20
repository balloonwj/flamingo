/**
 *  远程桌面被控端的控制权限状态（纯逻辑），RdControlledControl.h
 *
 *  2026-10-12 起远程桌面接通后默认只能查看，控制端申请、被控端同意后才注入键鼠（docs/远程桌面控制权限与观看工具条方案.md
 *  第 4.2 节）。被控端要协调四样东西：把关开关（RdControlGate，经推流器 SetControlAllowed）、要发的 1718 动作、右下角的
 *  申请提示、顶部条显示的控制状态。本类只根据输入事件算出前三样要怎么变（RdControlledEffects），顶部条显示 State()，
 *  不碰窗口与网络，由远程桌面控制器按固定顺序执行：先改把关开关，再发 1718，再处理申请提示，最后按 State() 刷新顶部条。
 *
 *  规则：
 *    - 点「允许控制」或在申请提示上点「同意」：发 grant，应答成功后才打开把关；
 *    - 点「收回控制」：先在本地关闭把关，再发 revoke，不等应答；
 *    - 在申请提示上点「拒绝」（含 60 秒无操作）：发 deny；
 *    - 收到控制端的申请（1719 request）：显示申请提示；控制端撤回（cancel）：关闭申请提示；控制端放弃（release）：关闭把关；
 *    - 1718 失败应答：按应答里服务端当前的状态纠正本地状态，不是已授权就关闭把关；
 *    - 会话结束、锁屏、登出（Reset）：关闭把关与申请提示，回到未授权。
 *  单元测试见 Source/Tests/RdControlledBarTests.cpp（RDC7、RDC8）。
 *
 *  典型用法（远程桌面控制器里）：
 *      RdControlledEffects e = m_controlFlow.OnNotify(notify.m_action, notify.m_control);
 *      applyControlledEffects(e);
 *
 *  balloonwj@qq.com   2026-10-12
 */
#pragma once

namespace remotedesktop {

//把关开关的变化（RdControlledEffects::m_gate）
enum rd_gate_change
{
    rd_gate_keep  = 0,   //不变
    rd_gate_open  = 1,   //打开：此后注入控制端发来的键鼠（只在 grant 应答成功后出现）
    rd_gate_close = 2,   //关闭：此后丢弃键鼠，并补发仍按下的键与按钮的松开；与发送同时出现时先关闭再发送
};

//申请提示的变化（RdControlledEffects::m_toast）
enum rd_toast_change
{
    rd_toast_keep = 0,   //不变
    rd_toast_show = 1,   //显示「某某 申请控制你的电脑」与「同意 / 拒绝」
    rd_toast_hide = 2,   //关闭（已处理、对方撤回、会话结束）
};

//一个事件之后控制器要执行的动作，按成员顺序执行；事件之后的本地状态（顶部条显示的控制状态）取 State()
struct RdControlledEffects
{
    int m_gate;         //把关开关的变化，取值见 rd_gate_change
    int m_sendAction;   //要发的 1718 动作，取值见 net::rd_control_action；0 表示不发
    int m_toast;        //申请提示的变化，取值见 rd_toast_change

    RdControlledEffects();
};

//被控端的控制权限状态
class RdControlledControlState
{
public:
    RdControlledControlState();

    //当前的本地控制状态，取值见 net::rd_control_state
    int State() const;

    //用户点了顶部条上的「允许控制 / 收回控制」：未授权或有人申请时发 grant，已授权时先关闭把关再发 revoke
    RdControlledEffects OnControlButton();

    /**
     *  用户在申请提示上做了选择（超时按拒绝）。不在申请中时（提示已过时）只关闭提示，不发送。
     *  @param accept true 同意（发 grant），false 拒绝（发 deny）
     */
    RdControlledEffects OnToastAnswer(bool accept);

    /**
     *  收到 1719：控制端做了一个控制动作。
     *  @param action  动作，取值见 net::rd_control_action
     *  @param control 动作之后服务端记录的控制状态，取值见 net::rd_control_state
     */
    RdControlledEffects OnNotify(int action, int control);

    /**
     *  收到本端 1718 的应答。
     *  @param action  请求里的动作，取值见 net::rd_control_action
     *  @param code    结果码，0 成功
     *  @param control 动作之后（失败时为当前）服务端记录的控制状态
     */
    RdControlledEffects OnResponse(int action, int code, int control);

    //会话结束、锁屏、登出：关闭把关与申请提示，回到未授权
    RdControlledEffects Reset();

private:
    //按三个成员组装结果
    static RdControlledEffects make(int gate, int sendAction, int toast);

    int m_state;   //本地控制状态，取值见 net::rd_control_state；新会话从未授权开始
};

} // namespace remotedesktop
