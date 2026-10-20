/**
 *  远程桌面控制端的控制权限状态（纯逻辑），RdViewerControl.h
 *
 *  2026-10-12 起远程桌面接通后默认只能查看（docs/远程桌面控制权限与观看工具条方案.md 第 4.5 节）：控制端在观看窗的浮动工具条上
 *  点「申请控制」，被控端同意后才发送键鼠；被控端拒绝、收回，或控制端自己点「结束控制」后回到只能查看。
 *  本类只根据输入事件算出状态、要发的 1718 动作与要给用户的提示，不碰窗口与网络，由观看窗执行。
 *
 *  三种状态与浮动工具条上「申请控制」按钮的样子：
 *    - 未授权：灰底「申请控制」，状态文字「仅查看」，点了发 request；
 *    - 申请中：橙底「取消申请」，状态文字「申请中」，点了发 cancel；
 *    - 已授权：蓝底「结束控制」，状态文字「控制中」，点了发 release，本地立即停止发送键鼠。
 *  只有已授权时才发送键鼠（CanSendInput）。
 *  单元测试见 Source/Tests/RdViewerControlTests.cpp（RDC9、RDC10）。
 *
 *  典型用法（观看窗里）：
 *      RdViewerEffects e = m_control.OnControlButton();
 *      if (e.m_sendAction != 0) m_actions->SendControl(m_sessionId, e.m_sendAction);
 *
 *  balloonwj@qq.com   2026-10-12
 */
#pragma once

#include <string>
#include "../Share/ViewerFloatBar.h"   // ViewerBarButtonSpec：「申请控制」按钮的样子

namespace remotedesktop {

//要给控制端用户的提示（RdViewerEffects::m_notice）
enum rd_viewer_notice
{
    rd_viewer_notice_none    = 0,   //不提示
    rd_viewer_notice_denied  = 1,   //被控端拒绝了申请
    rd_viewer_notice_revoked = 2,   //被控端收回了控制
    rd_viewer_notice_granted = 3,   //被控端同意了申请（或主动允许），可以操作了
    rd_viewer_notice_failed  = 4,   //控制动作失败（会话已结束、状态不对等），状态已按服务端纠正
};

//一个事件之后观看窗要执行的动作；事件之后的本地控制状态取 RdViewerControlState::State()
struct RdViewerEffects
{
    int m_sendAction;   //要发的 1718 动作，取值见 net::rd_control_action；0 表示不发
    int m_notice;       //要给用户的提示，取值见 rd_viewer_notice

    RdViewerEffects();
};

//控制端的控制权限状态
class RdViewerControlState
{
public:
    RdViewerControlState();

    //当前的本地控制状态，取值见 net::rd_control_state
    int State() const;

    //是否可以发送键鼠：只有已授权时才可以
    bool CanSendInput() const;

    //用户点了浮动工具条上的「申请控制 / 取消申请 / 结束控制」
    RdViewerEffects OnControlButton();

    /**
     *  收到 1719：被控端做了一个控制动作。
     *  @param action  动作，取值见 net::rd_control_action
     *  @param control 动作之后服务端记录的控制状态，取值见 net::rd_control_state
     */
    RdViewerEffects OnNotify(int action, int control);

    /**
     *  收到本端 1718 的应答。
     *  @param action  请求里的动作，取值见 net::rd_control_action
     *  @param code    结果码，0 成功
     *  @param control 动作之后（失败时为当前）服务端记录的控制状态
     */
    RdViewerEffects OnResponse(int action, int code, int control);

    //会话结束或重新开始：回到未授权
    void Reset();

private:
    //按两个成员组装结果
    static RdViewerEffects make(int sendAction, int notice);

    int m_state;   //本地控制状态，取值见 net::rd_control_state；新会话从未授权开始
};

/**
 *  浮动工具条上「申请控制」按钮的样子（纯函数）。
 *  @param id    按钮编号
 *  @param state 控制状态，取值见 net::rd_control_state；越界按未授权处理
 *  @return 未授权灰底「申请控制」、申请中橙底「取消申请」、已授权蓝底「结束控制」；提示文字留空，
 *          由工具条按 screenshare::ShareBarTooltipForTriState 取，与屏幕共享观看窗的同一按钮一致
 */
screenshare::ViewerBarButtonSpec RdViewerControlButton(UINT id, int state);

/**
 *  浮动工具条左侧的状态文字（纯函数）。
 *  @param state 控制状态，取值见 net::rd_control_state；越界按未授权处理
 *  @return 「仅查看」「申请中」「控制中」（已翻译）
 */
std::wstring RdViewerStatusText(int state);

/**
 *  提示的文字（纯函数）。
 *  @param notice 取值见 rd_viewer_notice
 *  @return 已翻译的文字；rd_viewer_notice_none 返回空
 */
std::wstring RdViewerNoticeText(int notice);

} // namespace remotedesktop
