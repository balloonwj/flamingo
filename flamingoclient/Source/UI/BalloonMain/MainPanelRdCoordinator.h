/**
 *  远程桌面协调器：把一次远程桌面会话从发起到收尾的全过程集中在一个类里。
 *
 *  一次会话涉及的四个窗口 / 对象都归本类持有，于是登出与退出时的收尾能写在一处
 *  （见 ResetForLogout）。
 *
 *  典型用法：由 CBalloonMainPanel 按值持有，创建窗口后调一次 Attach 绑定宿主句柄，并在
 *  自己的消息映射里加一行 CHAIN_MSG_MAP_MEMBER(m_rdCoordinator)，六条消息即由本类接管。
 *
 *      // CBalloonMainPanel 的消息映射
 *      CHAIN_MSG_MAP_MEMBER(m_rdCoordinator)
 *
 *      // CBalloonMainPanel::EnsureCreated 里
 *      m_rdCoordinator.Attach(m_hWnd);
 *
 *  本类不是窗口，只是一个带消息映射的普通对象，靠 CHAIN_MSG_MAP_MEMBER 搭在宿主窗口的
 *  消息派发链上。之所以不让它自建窗口：网络应答的收件窗口全局只有一个（见
 *  CFlamingoClient::SetCallBackWnd），另开窗口就得多一次转发。
 *
 *  balloonwj@qq.com   2026-08-18
 */

#pragma once

#include <string>

#include "../../Feature/RemoteDesktop/RdControlledStreamer.h"
#include "../BalloonRemoteDesktop/BalloonRemoteDesktopDlg.h"
#include "../BalloonRemoteDesktop/RdViewerWindow.h"
#include "../BalloonRemoteDesktop/RdControlledBar.h"
#include "../../Session/UserSessionData.h"   // FMG_MSG_RD_* / FMG_MSG_V2_RD_* 消息号

namespace balloonmain {

/**
 *  远程桌面协调器。
 *
 *  一次会话涉及两端四个对象：发起端弹「呼叫中」对话框、被邀请端弹授权对话框（两者是
 *  同一个 CBalloonRemoteDesktopDlg 的两种形态）；双方谈妥后，被控端起推流器并在屏幕
 *  顶部挂受控提示条，控制端开观看窗收画面。本类持有这四个对象并按服务端推来的状态
 *  驱动它们。
 *
 *  出站信令直接走 CIUSocket 单例，本类不持有网络客户端。
 */
class CRdCoordinator
{
public:
    CRdCoordinator();
    ~CRdCoordinator();

    /**
     *  绑定宿主窗口。
     *
     *    hHost  宿主窗口句柄（主面板），本类不拥有它，只借用。两处用途：一是各提示框的
     *           属主窗口；二是受控提示条的通知目标 —— 用户点提示条上的「结束控制」时，
     *           提示条会往这个句柄投 FMG_MSG_RD_CONTROLLED_END_BY_USER。
     *
     *  须在宿主窗口创建之后、收到任何远程桌面消息之前调用一次。
     */
    void Attach(HWND hHost);

    /**
     *  把远程桌面收尾到「没有会话」的状态：停被控端推流、关控制端观看窗、隐藏受控提示条、
     *  关掉呼叫 / 授权对话框、清空会话 id。
     *
     *  幂等，没有会话时调用也安全。三条路径要调它：用户登出、被踢下线、以及本类析构。
     *  不调的后果是实打实的 —— 正在被控时登出，屏幕仍在往中转服务器推送，而用户看到的
     *  已经是登录窗，会以为早就断开了。
     */
    void ResetForLogout();

    // 六条远程桌面消息由本类接管。宿主窗口用 CHAIN_MSG_MAP_MEMBER 把消息派发链接过来，
    // 因此本映射里的处理器必须排在宿主自己的处理器之后 —— 由宿主决定 CHAIN 的位置。
    BEGIN_MSG_MAP_EX(CRdCoordinator)
        MESSAGE_HANDLER_EX(FMG_MSG_RD_START_INVITE, OnRdStartInvite)                //聊天窗显示器按钮发起远程桌面
        MESSAGE_HANDLER_EX(FMG_MSG_RD_CONTROLLED_END_BY_USER, OnRdControlledEndByUser)  //被控端点受控提示条的「结束控制」
        MESSAGE_HANDLER_EX(FMG_MSG_V2_RD_INVITE_ACK, OnV2RdInviteAck)               //v2 远程桌面邀请应答
        MESSAGE_HANDLER_EX(FMG_MSG_V2_RD_INVITE_NOTIFY, OnV2RdInviteNotify)         //v2 远程桌面邀请推送：被控端弹授权窗
        MESSAGE_HANDLER_EX(FMG_MSG_V2_RD_RELAY_INFO, OnV2RdRelayInfo)               //v2 远程桌面中转参数：连 imgserver 中转
        MESSAGE_HANDLER_EX(FMG_MSG_V2_RD_STATE_NOTIFY, OnV2RdStateNotify)           //v2 远程桌面状态推送
    END_MSG_MAP()

private:
    // 六条消息的处理器。wParam / lParam 的含义写在各自实现的
    // 头部注释里，凡 lParam 为堆上的 net:: 结构体的，处理完由处理器自己 delete。
    LRESULT OnRdStartInvite(UINT uMsg, WPARAM wParam, LPARAM lParam);
    LRESULT OnRdControlledEndByUser(UINT uMsg, WPARAM wParam, LPARAM lParam);
    LRESULT OnV2RdInviteAck(UINT uMsg, WPARAM wParam, LPARAM lParam);
    LRESULT OnV2RdInviteNotify(UINT uMsg, WPARAM wParam, LPARAM lParam);
    LRESULT OnV2RdRelayInfo(UINT uMsg, WPARAM wParam, LPARAM lParam);
    LRESULT OnV2RdStateNotify(UINT uMsg, WPARAM wParam, LPARAM lParam);

    HWND m_hHost;                                   // 宿主窗口句柄（主面板），借用不拥有；提示框属主与受控条的通知目标

    CBalloonRemoteDesktopDlg m_rdDlg;               // 会话对话框，呼叫中 / 授权两种形态共用一个对象
    remotedesktop::RdControlledStreamer m_rdStreamer;   // 被控端推流器：本端为被控端时连中转服务器抓屏并推送
    CRdViewerWindow m_rdViewer;                     // 控制端观看窗：本端为控制端时连中转服务器收屏幕帧并显示
    CRdControlledBar m_rdControlledBar;             // 被控端受控提示条：屏幕顶部置顶显示「正在被控制」并提供结束按钮

    std::wstring m_rdControlledPeerName;            // 控制方昵称，由邀请推送带来，供受控提示条显示；生命周期到本次会话结束
    std::string m_rdControlledSessionId;            // 本次会话 id，被控方主动结束时要用它发 cmd_rd_close；无会话时为空
};

} // namespace balloonmain
