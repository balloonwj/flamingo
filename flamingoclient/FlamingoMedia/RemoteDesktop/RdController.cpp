/**
 *  远程桌面控制器的实现，见 RdController.h。
 *
 *  各处理函数与 exe 原远程桌面协调器（MainPanelRdCoordinator.cpp，第 1d 步删除）一一对应，注释里的
 *  设计取舍沿用原实现；差异只在依赖的来源（宿主接口、自有消息窗口），见头文件说明。
 *
 *  balloonwj@qq.com   2026-10-02
 */
#include "stdafx.h"

#include "RdController.h"

#include "../../Source/Common/Log/IULog.h"   //LOG_INFO / LOG_ERROR（DLL 内经日志桥接写进 exe 的日志）
#include "../../Source/net/Protocol.h"       //cmd_rd_*
#include "../../Source/Common/I18n/I18n.h"   // TR：界面文字的多语言查找
#include "../Host/MediaLockState.h"          // 锁屏期间远程桌面请求自动拒绝（docs/锁屏方案.md）

#include <string.h>   // strcmp：读诊断开关

namespace remotedesktop {

//提示框标题
static const TCHAR* kRdTitle = TR_NOOP(L"远程桌面");

//客户端用户号是 32 位无符号数，发起远程桌面的对端 userid 按它校验（与通话一致）
static const int64_t kMaxClientUserId = 0xFFFFFFFFLL;

//诊断开关：默认登记远程桌面的 P2P 业务方（远程桌面优先走 P2P）；环境变量 FLAMINGO_RD_P2P 为 0 时不登记，
//远程桌面照旧全部走 imgserver 中转，服务端也不会为它建 P2P 会话
static const char* const kRdP2PEnvName = "FLAMINGO_RD_P2P";
static const char* const kRdP2PEnvOff = "0";

//读环境变量用的缓冲（字符数）：开关值只有一个字符
static const DWORD kRdP2PEnvValueChars = 8;

//是否按诊断开关关闭了远程桌面走 P2P
static bool RdP2PDisabledByEnv()
{
    char value[kRdP2PEnvValueChars] = { 0 };
    const DWORD n = ::GetEnvironmentVariableA(kRdP2PEnvName, value, kRdP2PEnvValueChars);
    return n > 0 && n < kRdP2PEnvValueChars && strcmp(value, kRdP2PEnvOff) == 0;
}

CRdController::CRdController()
    : m_host(NULL)
    , m_p2pBroker(kRdP2PPurpose)
{
}

CRdController::~CRdController()
{
    Shutdown();
}

bool CRdController::Init(flamingomedia::IMediaHost* host)
{
    if (host == NULL)
    {
        return false;
    }
    if (IsWindow())
    {
        return true;
    }
    m_host = host;
    //仅接收消息的窗口：受控提示条的「结束控制」投到这里
    if (Create(HWND_MESSAGE) == NULL)
    {
        LOG_ERROR("RdController: create message window failed, err=%lu.", ::GetLastError());
        m_host = NULL;
        return false;
    }
    m_rdDlg.SetActions(this);
    m_rdViewer.SetActions(this);
    RegisterP2P();
    LOG_INFO("RdController: initialized, p2p=%d.", m_p2pBroker.IsRegistered() ? 1 : 0);
    return true;
}

void CRdController::RegisterP2P()
{
    if (RdP2PDisabledByEnv())
    {
        LOG_INFO("RdController: remote desktop p2p is off (%s is 0), sessions use the imgserver relay.", kRdP2PEnvName);
        return;
    }
    p2p::IP2PService* service = m_host->GetP2PService();
    if (service == NULL)
    {
        LOG_INFO("RdController: p2p service not available, sessions use the imgserver relay.");
        return;
    }
    m_p2pBroker.Register(service);
}

void CRdController::Shutdown()
{
    ResetForLogout();
    //推流器与观看窗都已收尾，没有会话再认领链路槽：注销用途（P2P 模块在本模块之后才卸载）
    m_p2pBroker.Unregister();
    if (IsWindow())
    {
        DestroyWindow();
    }
    m_rdDlg.SetActions(NULL);
    m_rdViewer.SetActions(NULL);
    m_host = NULL;
}

void CRdController::ResetForLogout()
{
    m_rdDlg.CloseDlg();
    m_rdStreamer.Stop();
    m_rdControlledBar.Hide();
    m_rdViewer.CloseViewer();
    m_rdControlledSessionId.clear();
    m_rdControlledPeerName.clear();
}

bool CRdController::IsRdSignal(uint16_t cmd)
{
    return cmd >= cmd_rd_invite && cmd <= cmd_rd_relay_info;
}

HWND CRdController::MainWindow() const
{
    if (m_host == NULL)
    {
        return NULL;
    }
    return m_host->GetMainWindow();
}

bool CRdController::SendRdSignal(uint16_t cmd, const std::string& body, const char* what)
{
    if (m_host == NULL || body.empty())
    {
        LOG_ERROR("RdController: skip %s, host=%p, body empty=%d.", what, m_host, body.empty() ? 1 : 0);
        return false;
    }
    if (!m_host->SendSignal(cmd, body.data(), static_cast<uint32_t>(body.size())))
    {
        LOG_ERROR("RdController: send %s failed, cmd=%u.", what, static_cast<unsigned>(cmd));
        return false;
    }
    return true;
}

bool CRdController::OnSignal(uint16_t cmd, const std::string& body)
{
    if (m_host == NULL || !IsRdSignal(cmd))
    {
        return false;
    }
    switch (cmd)
    {
    //邀请应答：成功带 sessionid，失败（对方离线 / 忙）带 code / msg
    case cmd_rd_invite:
        {
            net::RDInviteResponse resp;
            if (net::RemoteDesktopProtocolClient::parseInviteResponse(body, resp))
            {
                LOG_INFO("Recv v2 rd invite-ack, code=%d, sessionid=%s.", resp.m_code, resp.m_sessionId.c_str());
                HandleInviteAck(resp);
            }
            else
            {
                LOG_ERROR("Parse v2 rd invite-ack failed, body=%s.", body.c_str());
            }
            break;
        }
    //邀请推送：被控端弹授权对话框
    case cmd_rd_invite_notify:
        {
            net::RDInviteNotify notify;
            if (net::RemoteDesktopProtocolClient::parseInviteNotify(body, notify))
            {
                LOG_INFO("Recv v2 rd invite-notify, from=%lld, session=%s.", static_cast<long long>(notify.m_fromId),
                         notify.m_sessionId.c_str());
                HandleInviteNotify(notify);
            }
            else
            {
                LOG_ERROR("Parse v2 rd invite-notify failed, body=%s.", body.c_str());
            }
            break;
        }
    //中转参数下发：双方建立成功，按角色起推流或观看窗
    case cmd_rd_relay_info:
        {
            net::RDRelayInfo info;
            if (net::RemoteDesktopProtocolClient::parseRelayInfo(body, info))
            {
                LOG_INFO("Recv v2 rd relay-info, session=%s, role=%d, %s:%d, p2p session=%s.", info.m_sessionId.c_str(),
                         info.m_role, info.m_relayIp.c_str(), info.m_relayPort, info.m_p2pSessionId.c_str());
                HandleRelayInfo(info);
            }
            else
            {
                LOG_ERROR("Parse v2 rd relay-info failed, body=%s.", body.c_str());
            }
            break;
        }
    //状态推送：拒绝 / 取消 / 对方离线 / 超时 / 断开，收尾并提示
    case cmd_rd_state_notify:
        {
            net::RDStateNotify state;
            if (net::RemoteDesktopProtocolClient::parseStateNotify(body, state))
            {
                LOG_INFO("Recv v2 rd state-notify, session=%s, state=%d.", state.m_sessionId.c_str(), state.m_state);
                HandleStateNotify(state);
            }
            else
            {
                LOG_ERROR("Parse v2 rd state-notify failed, body=%s.", body.c_str());
            }
            break;
        }
    //应答 / 取消 / 结束的回包：后续结果由状态推送驱动，这里只记日志（迁入 DLL 之前 exe 同样不处理）
    case cmd_rd_response:
    //取消邀请的回包：同上，只记日志
    case cmd_rd_cancel:
    //结束会话的回包：同上，只记日志
    case cmd_rd_close:
        LOG_INFO("Recv v2 rd ack, cmd=%u, body=%s.", static_cast<unsigned>(cmd), body.c_str());
        break;
    //号段内其余命令号目前没有定义，记一条日志后忽略
    default:
        LOG_INFO("Recv v2 rd signal with unexpected cmd=%u, ignored.", static_cast<unsigned>(cmd));
        break;
    }
    return true;
}

bool CRdController::StartRemoteDesktop(int64_t peerUserId, const CString& peerName)
{
    if (m_host == NULL || peerUserId <= 0 || peerUserId > kMaxClientUserId)
    {
        return false;
    }
    if (!SendRdSignal(cmd_rd_invite, net::RemoteDesktopProtocolClient::buildInviteRequest(peerUserId), "rd invite"))
    {
        return false;
    }
    LOG_INFO("Send v2 rd invite, to=%lld.", static_cast<long long>(peerUserId));
    //会话 id 待邀请应答回来后回填（取消时要用）
    m_rdDlg.OpenCalling(MainWindow(), peerName, peerUserId);
    return true;
}

void CRdController::HandleInviteAck(const net::RDInviteResponse& resp)
{
    //邀请应答：成功把会话 id 回填给「呼叫中」对话框（取消时要用）；失败则关对话框并提示。
    if (resp.m_code == 0)
    {
        m_rdDlg.SetSessionId(resp.m_sessionId);
        return;
    }
    //对端：邀请应答对应最近一次 OpenCalling 的对方（对话框关了也记着）
    const int64_t peerId = m_rdDlg.GetPeerUserId();
    m_rdDlg.CloseDlg();
    LOG_INFO("RdController: invite rejected by server, code=%d, peer=%lld.", resp.m_code, static_cast<long long>(peerId));
    ShowResultTip(peerId, TR(L"对方当前无法接受远程桌面请求。"));
}

void CRdController::ShowResultTip(int64_t peerUserId, const wchar_t* tip)
{
    if (tip == NULL)
        return;
    if (peerUserId > 0 && m_host != NULL)
    {
        m_host->AppendCallTip(peerUserId, tip);
        return;
    }
    ::MessageBox(MainWindow(), tip, TR(kRdTitle), MB_OK | MB_ICONINFORMATION);
}

void CRdController::HandleInviteNotify(const net::RDInviteNotify& notify)
{
    //锁屏期间（docs/锁屏方案.md 第 4.9 节）：受控需要本人在场确认，直接拒绝，不弹授权对话框
    if (flamingomedia::IsMediaScreenLocked())
    {
        LOG_INFO("RdController: invite while the screen is locked, rejected, sessionId=%s.", notify.m_sessionId.c_str());
        RespondInvite(notify.m_sessionId, false);
        return;
    }
    //被控端收到邀请：弹授权对话框（接受 / 拒绝经 IRdSessionActions 回到本类发 cmd_rd_response）。
    //昵称以 UTF-8 传来，转成宽字符再显示。
    CString fromName(CA2W(notify.m_fromName.c_str(), CP_UTF8));
    //记下控制方昵称：若本端接受授权、随后成为被控端，受控提示条用它显示「正在被 XXX 控制」
    m_rdControlledPeerName = (LPCTSTR)fromName;
    m_rdDlg.OpenAuth(MainWindow(), fromName, notify.m_sessionId, notify.m_fromId);
}

void CRdController::BuildTransportParams(const net::RDRelayInfo& info, RdTransportParams& out)
{
    //中转握手用的服务端公钥由宿主提供（取自 exe 的主连接），参数里存副本供工作线程使用
    out.SetRelay(info.m_sessionId, info.m_relayIp, info.m_relayPort, info.m_token, m_host->GetServerPublicKey());
    //服务端为本次会话建了 P2P 会话、本端也登记了用途：认领链路槽（2302 可能已经先到，链路已在槽里等着）。
    //P2P 会话的功能会话号就是远程桌面会话号
    if (!info.m_p2pSessionId.empty() && m_p2pBroker.IsRegistered())
        out.m_slot = m_p2pBroker.ClaimSlot(info.m_sessionId);
    else if (!info.m_p2pSessionId.empty())
        LOG_WARNING("RdController: relay-info carries p2p session %s but p2p is not registered, using relay.",
                    info.m_p2pSessionId.c_str());
}

void CRdController::HandleRelayInfo(const net::RDRelayInfo& info)
{
    //中转参数下发：双方建立成功。关呼叫 / 授权对话框，按本端角色启动：被控端建立传输后抓屏推流，
    //控制端开观看窗、建立传输后收画面显示。传输优先走 P2P 链路（服务端建了 P2P 会话时），否则连中转。
    m_rdDlg.CloseDlg();

    RdTransportParams params;
    BuildTransportParams(info, params);

    if (info.m_role == net::rd_role_controlled)
    {
        //本端=被控端：先停上次残留，再起推流线程（连接在推流线程上进行，失败时投 kMsgRdStreamerConnectFailed 回来）
        m_rdStreamer.Stop();
        if (!m_rdStreamer.Start(params, m_hWnd))
        {
            ::MessageBox(MainWindow(), TR(L"启动屏幕推流失败。"), TR(kRdTitle), MB_OK | MB_ICONWARNING);
            return;
        }
        //推流已起：记下会话 id（被控方主动结束时发 cmd_rd_close 用），并在屏幕顶部弹出受控提示条，
        //让被控方随时知情、可随手终止远程控制。提示条的「结束控制」投到本类的消息窗口。
        m_rdControlledSessionId = info.m_sessionId;
        //提示条每秒问推流器一次链路路径：画面经直连链路发出时显示「P2P」徽章
        m_rdControlledBar.SetLinkPathSource(&m_rdStreamer);
        m_rdControlledBar.Show(m_rdControlledPeerName, m_hWnd);
    }
    else if (info.m_role == net::rd_role_controller)
    {
        //本端=控制端：先关上次残留，再开观看窗（连接在观看窗的连接线程上进行）
        m_rdViewer.CloseViewer();
        m_rdViewer.Start(params);
    }
}

void CRdController::HandleStateNotify(const net::RDStateNotify& state)
{
    //会话结束：关呼叫 / 授权对话框，停被控端推流并关受控提示条、关控制端观看窗
    //（远程桌面会话状态收尾是关键路径，留一条日志）
    LOG_INFO("RdController: session teardown, state=%d.", state.m_state);
    //对端：呼叫中或授权中的对话框对应的对方（关对话框之前取，对话框关了也记着）
    const int64_t peerId = m_rdDlg.GetPeerUserId();
    m_rdDlg.CloseDlg();
    m_rdStreamer.Stop();
    m_rdControlledBar.Hide();
    m_rdControlledSessionId.clear();
    m_rdViewer.CloseViewer();

    //据状态提示；disconnected（正常断开）静默，避免与观看窗 / 推流器自身的断开提示重复
    LPCTSTR tip = NULL;
    switch (state.m_state)
    {
    //对方在授权窗上点了拒绝
    case net::rd_state_rejected:
        tip = TR(L"对方拒绝了远程桌面请求。");
        break;
    //发起方在呼叫中对话框上点了取消
    case net::rd_state_canceled:
        tip = TR(L"对方取消了远程桌面请求。");
        break;
    //服务端查到对端没有在线会话
    case net::rd_state_peer_offline:
        tip = TR(L"对方当前不在线。");
        break;
    //超过服务端设定的等待时限仍未应答
    case net::rd_state_timeout:
        tip = TR(L"远程桌面请求超时。");
        break;
    //正常断开等其余状态：不弹提示，避免与观看窗 / 推流器自身的断开提示重复
    default:
        break;
    }
    //没有建成的结果（拒绝、取消、不在线、超时）写进与对方的单聊窗（2026-10-07 起不再弹消息框）
    if (tip != NULL)
    {
        ShowResultTip(peerId, tip);
    }
}

void CRdController::CancelInvite(const std::string& sessionId)
{
    if (sessionId.empty())
    {
        return;
    }
    if (SendRdSignal(cmd_rd_cancel, net::RemoteDesktopProtocolClient::buildCancel(sessionId), "rd cancel"))
    {
        LOG_INFO("Send v2 rd cancel, session=%s.", sessionId.c_str());
    }
}

void CRdController::RespondInvite(const std::string& sessionId, bool accept)
{
    if (sessionId.empty())
    {
        return;
    }
    if (SendRdSignal(cmd_rd_response, net::RemoteDesktopProtocolClient::buildResponse(sessionId, accept), "rd response"))
    {
        LOG_INFO("Send v2 rd response, session=%s, accept=%d.", sessionId.c_str(), accept ? 1 : 0);
    }
}

void CRdController::CloseSession(const std::string& sessionId)
{
    if (sessionId.empty())
    {
        return;
    }
    if (SendRdSignal(cmd_rd_close, net::RemoteDesktopProtocolClient::buildClose(sessionId), "rd close"))
    {
        LOG_INFO("Send v2 rd close, session=%s.", sessionId.c_str());
    }
}

void CRdController::OnScreenLockChanged(bool locked)
{
    //锁屏开始时本机若正被远程控制，立即结束控制：控制方用模拟键鼠操作本机，能直接在锁屏界面上打字
    if (!locked || m_rdControlledSessionId.empty())
    {
        return;
    }
    LOG_INFO("RdController: screen locked while being controlled, ending the session, sessionId=%s.",
             m_rdControlledSessionId.c_str());
    CloseSession(m_rdControlledSessionId);
    m_rdStreamer.Stop();
    m_rdControlledBar.Hide();
    m_rdControlledSessionId.clear();
}

LRESULT CRdController::OnControlledEndByUserMsg(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    //被控端用户点受控提示条「结束控制」：主动发 cmd_rd_close 结束会话（服务端会广播 cmd_rd_state_notify 给
    //控制端收尾），随后停本端推流、关受控条、清会话 id。与服务端推来的 state_notify 收尾走同一组动作，重复调用幂等。
    LOG_INFO("RdController: controlled side ended the session by user, sessionId=%s.", m_rdControlledSessionId.c_str());
    CloseSession(m_rdControlledSessionId);
    m_rdStreamer.Stop();
    m_rdControlledBar.Hide();
    m_rdControlledSessionId.clear();
    return 0;
}

LRESULT CRdController::OnStreamerConnectFailedMsg(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    //上一次会话迟到的通知（推流器已经为新会话重新启动过）：丢弃
    if ((unsigned int)wParam != m_rdStreamer.StartSeq())
        return 0;
    //P2P 与中转都没连上：结束会话（服务端广播状态推送，控制端随之关观看窗），停推流、关提示条，再提示用户
    LOG_ERROR("RdController: streamer connect failed, ending session %s.", m_rdControlledSessionId.c_str());
    CloseSession(m_rdControlledSessionId);
    m_rdStreamer.Stop();
    m_rdControlledBar.Hide();
    m_rdControlledSessionId.clear();
    ::MessageBox(MainWindow(), TR(L"启动屏幕推流失败。"), TR(kRdTitle), MB_OK | MB_ICONWARNING);
    return 0;
}

} // namespace remotedesktop
