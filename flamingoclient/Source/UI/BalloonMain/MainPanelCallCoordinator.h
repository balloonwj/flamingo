/**
 *  音视频通话协调器：把一通电话从拨出 / 来电到挂断的全过程集中在一个类里。
 *
 *  一通电话涉及的两个对象（通话窗、中转会话）都归本类持有，于是登出与退出时的收尾能
 *  写在一处（见 ResetForLogout）。
 *
 *  典型用法：由 CBalloonMainPanel 按值持有，创建窗口后调一次 Attach，并在自己的消息映射
 *  里加一行 CHAIN_MSG_MAP_MEMBER(m_callCoordinator)，十条消息即由本类接管。
 *
 *      // CBalloonMainPanel 的消息映射
 *      CHAIN_MSG_MAP_MEMBER(m_callCoordinator)
 *
 *      // CBalloonMainPanel::EnsureCreated 里
 *      m_callCoordinator.Attach(m_hWnd, this, m_lpFMGClient);
 *
 *  本类不是窗口，只是一个带消息映射的普通对象，靠 CHAIN_MSG_MAP_MEMBER 搭在宿主窗口的
 *  消息派发链上。理由与远程桌面协调器相同，见 MainPanelRdCoordinator.h。
 *
 *  balloonwj@qq.com   2026-08-18
 */

#pragma once

#include "../../Feature/Call/BalloonCallDlg.h"
#include "../../Feature/Call/CallMediaSession.h"
#include "../../Session/UserSessionData.h"   // FMG_MSG_CALL_* / FMG_MSG_V2_CALL_* 消息号

class CBalloonMainPanel;
class CFlamingoClient;

namespace balloonmain {

/**
 *  通话协调器。
 *
 *  一通电话涉及两个对象：会话对话框（呼叫中 / 来电 / 通话中三种形态共用一个
 *  CBalloonCallDlg），以及接通后连中转服务器收发音视频的 call::CallMediaSession。
 *  本类持有这两个对象，按服务端推来的状态驱动它们，并负责铃声的起停。
 *
 *  出站信令的分工：发起呼叫由本类发（cmd_call_invite），取消 / 接听 / 拒绝 / 挂断 /
 *  切换媒体都由通话窗自己发 —— 那几个动作的触发点都在窗口上，绕一圈没有意义。
 */
class CCallCoordinator
{
public:
    CCallCoordinator();
    ~CCallCoordinator();

    /**
     *  绑定宿主与协作对象。须在宿主窗口创建之后、收到任何通话消息之前调用一次。
     *
     *    hHost    宿主窗口句柄（主面板），本类不拥有。三处用途：提示框的属主窗口；
     *             中转会话把事件回投到这个句柄（FMG_MSG_CALL_RELAY_EVENT）；作为通话窗
     *             记录的 owner，让通话窗关闭时能请求本类停掉媒体会话。
     *    pPanel   主面板，本类不拥有。用来把通话结果（对方拒绝 / 无应答 / 通话结束等）
     *             写成聊天窗里的一条居中系统提示。可为 NULL，此时不写提示。
     *    pClient  网络客户端，本类不拥有。只读两项：静音开关与本人 userid。可为 NULL。
     */
    void Attach(HWND hHost, CBalloonMainPanel* pPanel, CFlamingoClient* pClient);

    /**
     *  把通话收尾到「没有会话」的状态：停铃声、停中转会话（释放摄像头与麦克风）、
     *  关掉通话窗。
     *
     *  幂等，没有通话时调用也安全。三条路径要调它：用户登出、被踢下线、以及本类析构。
     *  不调的后果是采集线程与摄像头跨越登出边界继续运行。
     */
    void ResetForLogout();

    // 十条通话消息由本类接管。宿主窗口用 CHAIN_MSG_MAP_MEMBER 把消息派发链接过来。
    BEGIN_MSG_MAP_EX(CCallCoordinator)
        MESSAGE_HANDLER_EX(FMG_MSG_CALL_START_INVITE, OnCallStartInvite)                //单聊窗「语音通话」按钮发起呼叫
        MESSAGE_HANDLER_EX(FMG_MSG_CALL_START_VIDEO_INVITE, OnCallStartVideoInvite)     //单聊窗「视频通话」按钮发起呼叫
        MESSAGE_HANDLER_EX(FMG_MSG_CALL_STOP_MEDIA, OnCallStopMedia)                    //通话窗关窗：停媒体会话释放摄像头 / 麦克风
        MESSAGE_HANDLER_EX(FMG_MSG_CALL_RELAY_EVENT, OnCallRelayEvent)                  //中转会话事件（连通 / 透传校验 / 失败 / 断开）
        MESSAGE_HANDLER_EX(FMG_MSG_V2_CALL_INVITE_ACK, OnV2CallInviteAck)               //v2 通话邀请应答
        MESSAGE_HANDLER_EX(FMG_MSG_V2_CALL_INVITE_NOTIFY, OnV2CallInviteNotify)         //v2 通话邀请推送：被叫弹来电窗
        MESSAGE_HANDLER_EX(FMG_MSG_V2_CALL_SIGNALING, OnV2CallSignaling)                //v2 通话中转参数：连 imgserver 中转
        MESSAGE_HANDLER_EX(FMG_MSG_V2_CALL_STATE_NOTIFY, OnV2CallStateNotify)           //v2 通话状态推送
        MESSAGE_HANDLER_EX(FMG_MSG_V2_CALL_SWITCH_MEDIA_RESULT, OnV2CallSwitchMediaResult)  //v2 切换通话媒体应答：失败时提示原因
        MESSAGE_HANDLER_EX(FMG_MSG_V2_CALL_MEDIA_CHANGED, OnV2CallMediaChanged)         //v2 通话媒体已变更推送
    END_MSG_MAP()

private:
    // 用户是否在系统设置里关掉了提示音。铃声与提示音的每次播放都要带这个开关。
    // 网络客户端未绑定时按「不静音」处理。
    bool IsMuted() const;

    // 十条消息的处理器。wParam / lParam 的含义写在各自实现的头部注释里，凡 lParam 为
    // 堆上的 net:: 结构体的，处理完由处理器自己 delete。
    LRESULT OnCallStartInvite(UINT uMsg, WPARAM wParam, LPARAM lParam);
    LRESULT OnCallStartVideoInvite(UINT uMsg, WPARAM wParam, LPARAM lParam);
    LRESULT OnCallStopMedia(UINT uMsg, WPARAM wParam, LPARAM lParam);
    LRESULT OnCallRelayEvent(UINT uMsg, WPARAM wParam, LPARAM lParam);
    LRESULT OnV2CallInviteAck(UINT uMsg, WPARAM wParam, LPARAM lParam);
    LRESULT OnV2CallInviteNotify(UINT uMsg, WPARAM wParam, LPARAM lParam);
    LRESULT OnV2CallSignaling(UINT uMsg, WPARAM wParam, LPARAM lParam);
    LRESULT OnV2CallStateNotify(UINT uMsg, WPARAM wParam, LPARAM lParam);
    LRESULT OnV2CallSwitchMediaResult(UINT uMsg, WPARAM wParam, LPARAM lParam);
    LRESULT OnV2CallMediaChanged(UINT uMsg, WPARAM wParam, LPARAM lParam);

    HWND m_hHost;                       // 宿主窗口句柄（主面板），借用不拥有；提示框属主、中转事件回投目标、通话窗的 owner
    CBalloonMainPanel* m_pPanel;        // 主面板，借用不拥有；只用来把通话结果写进聊天窗
    CFlamingoClient* m_lpFMGClient;     // 网络客户端，借用不拥有；只读静音开关与本人 userid

    CBalloonCallDlg m_callDlg;          // 通话会话对话框，呼叫中 / 来电 / 通话中三种形态共用一个对象
    call::CallMediaSession m_callMedia; // 中转会话：接通后连 imgserver 中转通道收发音视频，持有摄像头与麦克风
    int m_callMediaType;                // 本次通话的媒体类型（net::call_media_audio / call_media_video）。发起或接听时记下，接通后据它决定中转会话是否启用视频
};

} // namespace balloonmain
