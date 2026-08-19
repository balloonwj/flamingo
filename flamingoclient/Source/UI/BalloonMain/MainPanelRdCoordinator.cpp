/**
 *  远程桌面协调器的实现。本类不是窗口：提示框的属主、受控提示条的回投目标都用宿主
 *  句柄 m_hHost，也就是主面板。
 *
 *  balloonwj@qq.com   2026-08-18
 */

#include "stdafx.h"
#include "MainPanelRdCoordinator.h"

#include "../../Common/Log/IULog.h"                     // LOG_INFO
#include "../../net/IUSocket.h"                         // CIUSocket::SendRemoteDesktopInvite / SendRemoteDesktopClose
#include "../../net/RemoteDesktopProtocolClient.h"      // net::RDInviteResponse / RDInviteNotify / RDRelayInfo / RDStateNotify

namespace balloonmain {

CRdCoordinator::CRdCoordinator()
    : m_hHost(NULL)
{
    // BEGIN_MSG_MAP_EX 声明的这个标志位没有初始化器，而它的 ProcessWindowMessage 一进来
    // 就会读它一次（保存旧值以支持重入），不置初值就是读未初始化内存。
    m_bMsgHandled = FALSE;
}

CRdCoordinator::~CRdCoordinator()
{
    // 四个会话对象里有三个是 CWindowImpl 派生的窗口。若析构时它们仍持有有效 HWND，
    // 会触发 ATL CWindowImplRoot 的析构断言（atlwin.h "Object deleted before window
    // was destroyed"），表现为退出程序时崩溃。ResetForLogout 会把三个窗口都销毁掉。
    ResetForLogout();
}

void CRdCoordinator::Attach(HWND hHost)
{
    m_hHost = hHost;
}

void CRdCoordinator::ResetForLogout()
{
    m_rdDlg.CloseDlg();
    m_rdStreamer.Stop();
    m_rdControlledBar.Hide();
    m_rdViewer.CloseViewer();
    m_rdControlledSessionId.clear();
    m_rdControlledPeerName.clear();
}

LRESULT CRdCoordinator::OnRdStartInvite(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam)
{
    //聊天窗显示器按钮发起：wParam=对端 userid，lParam=堆 std::wstring* peerName。
    //协调器统一发 cmd_rd_invite 并弹"呼叫中"对话框；会话 id 待邀请应答回来后回填。
    int64_t peerId = (int64_t)(UINT)wParam;
    std::wstring* pName = (std::wstring*)lParam;
    CString peerName = (pName != NULL) ? pName->c_str() : _T("");
    if (pName != NULL)
        delete pName;

    if (peerId == 0)
        return 0;

    CIUSocket::GetInstance().SendRemoteDesktopInvite(peerId);
    m_rdDlg.OpenCalling(m_hHost, peerName, peerId);
    return 0;
}

LRESULT CRdCoordinator::OnRdControlledEndByUser(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/)
{
    //被控端用户点受控提示条"结束控制"：主动发 cmd_rd_close 结束会话（服务端会广播
    //cmd_rd_state_notify 给控制端收尾），随后停本端推流、关受控条、清会话 id。与服务端
    //推来的 state_notify 收尾走同一组动作，重复调用幂等。
    //远程桌面会话收尾是关键路径，留一条日志便于排查
    LOG_INFO("RdCoordinator: controlled side ended the session by user, sessionId=%s.",
             m_rdControlledSessionId.c_str());
    if (!m_rdControlledSessionId.empty())
        CIUSocket::GetInstance().SendRemoteDesktopClose(m_rdControlledSessionId);
    m_rdStreamer.Stop();
    m_rdControlledBar.Hide();
    m_rdControlledSessionId.clear();
    return 0;
}

LRESULT CRdCoordinator::OnV2RdInviteAck(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //邀请应答：成功把会话 id 回填给"呼叫中"对话框（取消时要用）；失败则关对话框并提示。
    net::RDInviteResponse* pResp = (net::RDInviteResponse*)lParam;
    if (pResp == NULL)
        return 0;

    if (pResp->m_code == 0)
    {
        m_rdDlg.SetSessionId(pResp->m_sessionId);
    }
    else
    {
        m_rdDlg.CloseDlg();
        ::MessageBox(m_hHost, _T("对方当前无法接受远程桌面请求。"),
                     _T("远程桌面"), MB_OK | MB_ICONINFORMATION);
    }
    delete pResp;
    return 0;
}

LRESULT CRdCoordinator::OnV2RdInviteNotify(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //被控端收到邀请：弹授权对话框（接受 / 拒绝由对话框直接发 cmd_rd_response）。
    //昵称以 UTF-8 传来，转成宽字符再显示。
    net::RDInviteNotify* pNotify = (net::RDInviteNotify*)lParam;
    if (pNotify == NULL)
        return 0;

    CString fromName(CA2W(pNotify->m_fromName.c_str(), CP_UTF8));
    //记下控制方昵称：若本端接受授权、随后成为被控端，受控提示条用它显示"正在被 XXX 控制"
    m_rdControlledPeerName = (LPCTSTR)fromName;
    m_rdDlg.OpenAuth(m_hHost, fromName, pNotify->m_sessionId, pNotify->m_fromId);
    delete pNotify;
    return 0;
}

LRESULT CRdCoordinator::OnV2RdRelayInfo(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //中转参数下发：双方建立成功。关呼叫 / 授权对话框，按本端角色启动：被控端连中转
    //抓屏推流，控制端开观看窗连中转收画面显示。
    net::RDRelayInfo* pInfo = (net::RDRelayInfo*)lParam;
    if (pInfo == NULL)
        return 0;

    m_rdDlg.CloseDlg();

    if (pInfo->m_role == net::rd_role_controlled)
    {
        //本端=被控端：先停上次残留，再连中转抓屏推流
        m_rdStreamer.Stop();
        if (!m_rdStreamer.Start(pInfo->m_relayIp, pInfo->m_relayPort, pInfo->m_token))
        {
            ::MessageBox(m_hHost, _T("启动屏幕推流失败。"), _T("远程桌面"),
                         MB_OK | MB_ICONWARNING);
        }
        else
        {
            //推流已起：记下会话 id（被控方主动结束时发 cmd_rd_close 用），并在屏幕顶部
            //弹出受控提示条，让被控方随时知情、可随手终止远程控制
            m_rdControlledSessionId = pInfo->m_sessionId;
            m_rdControlledBar.Show(m_rdControlledPeerName, m_hHost);
        }
    }
    else if (pInfo->m_role == net::rd_role_controller)
    {
        //本端=控制端：先关上次残留，再开观看窗连中转收画面
        m_rdViewer.CloseViewer();
        m_rdViewer.Start(pInfo->m_relayIp, pInfo->m_relayPort, pInfo->m_token, pInfo->m_sessionId);
    }

    delete pInfo;
    return 0;
}

LRESULT CRdCoordinator::OnV2RdStateNotify(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //状态推送：关对话框并据 state 提示（拒绝 / 取消 / 对方离线 / 超时 / 断开）。
    net::RDStateNotify* pState = (net::RDStateNotify*)lParam;
    if (pState == NULL)
        return 0;

    //会话结束：关呼叫 / 授权对话框，停被控端推流并关受控提示条、关控制端观看窗
    //（远程桌面会话状态收尾是关键路径，留一条日志）
    LOG_INFO("RdCoordinator: session teardown, state=%d.", pState->m_state);
    m_rdDlg.CloseDlg();
    m_rdStreamer.Stop();
    m_rdControlledBar.Hide();
    m_rdControlledSessionId.clear();
    m_rdViewer.CloseViewer();

    //据状态提示；disconnected（正常断开）静默，避免与观看窗 / 推流器自身的断开提示重复
    LPCTSTR tip = NULL;
    switch (pState->m_state)
    {
    //对方在授权窗上点了拒绝
    case net::rd_state_rejected:
        tip = _T("对方拒绝了远程桌面请求。");
        break;
    //发起方在呼叫中对话框上点了取消
    case net::rd_state_canceled:
        tip = _T("对方取消了远程桌面请求。");
        break;
    //服务端查到对端没有在线会话
    case net::rd_state_peer_offline:
        tip = _T("对方当前不在线。");
        break;
    //超过服务端设定的等待时限仍未应答
    case net::rd_state_timeout:
        tip = _T("远程桌面请求超时。");
        break;
    //正常断开等其余状态：不弹提示，避免与观看窗 / 推流器自身的断开提示重复
    default:
        break;
    }
    if (tip != NULL)
        ::MessageBox(m_hHost, tip, _T("远程桌面"), MB_OK | MB_ICONINFORMATION);

    delete pState;
    return 0;
}

} // namespace balloonmain
