/**
 *  音视频通话协调器的实现。本类不是窗口，几处与窗口相关的约定：
 *    · 提示框的属主窗口用宿主句柄 m_hHost，也就是主面板；
 *    · 静音开关与本人 userid 改从绑定进来的 CFlamingoClient 读，通话结果提示改经主面板写；
 *    · 通话窗的 owner 由 NULL 换成 m_hHost —— 原先传 NULL 使得通话窗记下的 owner 恒为空，
 *      「关窗即请求停媒体会话」那条兜底路径从未生效。
 *
 *  balloonwj@qq.com   2026-08-18
 */

#include "stdafx.h"
#include "MainPanelCallCoordinator.h"

#include "BalloonMainPanel.h"                       // CBalloonMainPanel::AppendCallTipToChat
#include "NotificationSound.h"                      // 回铃 / 来电铃声（循环）与接通 / 结束 / 忙音（一次性）
#include "../../App/FlamingoClient.h"               // CFlamingoClient::m_UserConfig / m_UserMgr
#include "../../Common/Log/IULog.h"                 // LOG_INFO / LOG_ERROR
#include "../../net/IUSocket.h"                     // CIUSocket::SendCallInvite
#include "../../net/CallProtocolClient.h"           // net::CallInviteResponse / CallInviteNotify / CallSignalingInfo / CallStateNotify 等
#include "../../net/Protocol.h"                     // error_code_call_* —— 切换媒体失败时按错误码给具体原因
#include "../../Feature/Call/CallMediaSession.h"    // call::call_relay_* 事件码

namespace balloonmain {

CCallCoordinator::CCallCoordinator()
    : m_hHost(NULL)
    , m_pPanel(NULL)
    , m_lpFMGClient(NULL)
    , m_callMediaType(net::call_media_audio)
{
    // BEGIN_MSG_MAP_EX 声明的这个标志位没有初始化器，而它的 ProcessWindowMessage 一进来
    // 就会读它一次（保存旧值以支持重入），不置初值就是读未初始化内存。
    m_bMsgHandled = FALSE;
}

CCallCoordinator::~CCallCoordinator()
{
    // 通话窗是 CWindowImpl 派生的窗口。若析构时它仍持有有效 HWND，会触发 ATL
    // CWindowImplRoot 的析构断言（atlwin.h "Object deleted before window was
    // destroyed"），表现为退出程序时崩溃。ResetForLogout 会把它销毁掉。
    ResetForLogout();
}

void CCallCoordinator::Attach(HWND hHost, CBalloonMainPanel* pPanel, CFlamingoClient* pClient)
{
    m_hHost = hHost;
    m_pPanel = pPanel;
    m_lpFMGClient = pClient;
}

void CCallCoordinator::ResetForLogout()
{
    // 顺序：先停铃声（循环播放的回铃 / 来电铃声不停就会一直响），再停中转会话释放
    // 摄像头与麦克风，最后关窗。
    StopLoopSound();
    m_callMedia.Stop();
    m_callDlg.CloseDlg();
}

bool CCallCoordinator::IsMuted() const
{
    if (m_lpFMGClient == NULL)
        return false;
    return m_lpFMGClient->m_UserConfig.IsEnableMute() ? true : false;
}

LRESULT CCallCoordinator::OnCallStartInvite(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam)
{
    //单聊窗"语音通话"按钮发起：wParam=对端 userid，lParam=堆 std::wstring* peerName。
    //发起纯语音呼叫并弹"呼叫中"对话框；会话 id 待邀请应答回来后回填。
    int64_t peerId = (int64_t)(UINT)wParam;
    std::wstring* pName = (std::wstring*)lParam;
    CString peerName = (pName != NULL) ? pName->c_str() : _T("");
    if (pName != NULL)
        delete pName;

    if (peerId == 0)
        return 0;

    //本端发起纯语音呼叫：记下本次通话媒体类型，供接通后 signaling 决定中转会话是否起视频
    m_callMediaType = net::call_media_audio;
    CIUSocket::GetInstance().SendCallInvite(peerId, net::call_media_audio);
    //告知对话框本次是纯语音（标题栏与提示文字据此措辞；也清掉上一通视频通话留下的标志）
    m_callDlg.SetVideoCall(false);
    m_callDlg.OpenCalling(m_hHost, peerName, peerId);
    return 0;
}

LRESULT CCallCoordinator::OnCallStartVideoInvite(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam)
{
    //单聊窗"视频通话"按钮发起：wParam=对端 userid，lParam=堆 std::wstring* peerName。
    //发起音视频呼叫并弹"呼叫中"对话框；接通后 signaling 据 m_callMediaType 起视频引擎。
    int64_t peerId = (int64_t)(UINT)wParam;
    std::wstring* pName = (std::wstring*)lParam;
    CString peerName = (pName != NULL) ? pName->c_str() : _T("");
    if (pName != NULL)
        delete pName;

    if (peerId == 0)
        return 0;

    //本端发起视频呼叫：记下媒体类型为视频，发 cmd_call_invite(media=video)
    m_callMediaType = net::call_media_video;
    CIUSocket::GetInstance().SendCallInvite(peerId, net::call_media_video);
    //告知对话框本次是视频通话：呼叫中的标题栏与提示文字据此写成"视频通话"
    m_callDlg.SetVideoCall(true);
    m_callDlg.OpenCalling(m_hHost, peerName, peerId);
    return 0;
}

LRESULT CCallCoordinator::OnV2CallInviteAck(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //邀请应答：成功把会话 id 回填给"呼叫中"对话框（取消时要用）；失败则关对话框并提示。
    net::CallInviteResponse* pResp = (net::CallInviteResponse*)lParam;
    if (pResp == NULL)
        return 0;

    bool bMuted = IsMuted();

    if (pResp->m_code == 0)
    {
        m_callDlg.SetSessionId(pResp->m_sessionId);
        //服务端已受理呼叫、对方开始响铃：本端起回铃音，循环到接通 / 被拒 / 超时 / 取消。
        //
        //只在呼叫窗还在时才起：铃声的停止统一挂在通话窗销毁上（见 BalloonCallDlg::OnDestroyMsg），
        //窗口已经没了还起铃声，就再没有任何路径会去停它。用户在 ack 回来之前就关掉呼叫窗时
        //会走到这里 —— 那时 sessionId 还没回填，关窗连取消信令都发不出去。
        if (m_callDlg.IsWindow())
            StartLoopSound(kNotifySoundCallOutgoing, bMuted);
        else
            LOG_INFO("v2 call invite ack arrived after dialog closed, skip ringback.");
    }
    else
    {
        //呼叫没起来：停掉可能已经起的回铃，播一声忙音。
        StopLoopSound();
        PlayNotifySound(kNotifySoundCallBusy, bMuted);
        m_callDlg.CloseDlg();
        ::MessageBox(m_hHost, _T("对方当前无法接听（不在线或正在通话中）。"),
                     _T("语音通话"), MB_OK | MB_ICONINFORMATION);
    }
    delete pResp;
    return 0;
}

LRESULT CCallCoordinator::OnV2CallInviteNotify(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //被叫收到来电：弹来电对话框（接听 / 拒绝由对话框直接发 cmd_call_response）。
    //昵称以 UTF-8 传来，转成宽字符再显示。
    net::CallInviteNotify* pNotify = (net::CallInviteNotify*)lParam;
    if (pNotify == NULL)
        return 0;

    //记下来电的媒体类型：被叫接通后 signaling 据此决定中转会话是否起视频
    m_callMediaType = pNotify->m_media;
    CString fromName(CA2W(pNotify->m_fromName.c_str(), CP_UTF8));
    //告知对话框本次来电是视频还是纯语音：被叫在接听前就能从提示文字看出对方约的是视频通话
    //（视频通话接听后会打开摄像头）
    m_callDlg.SetVideoCall(pNotify->m_media == net::call_media_video);
    m_callDlg.OpenIncoming(m_hHost, fromName, pNotify->m_sessionId, pNotify->m_fromId);
    //来电铃声：循环到接听 / 拒绝 / 对方取消 / 超时。
    StartLoopSound(kNotifySoundCallIncoming,
                                IsMuted());
    delete pNotify;
    return 0;
}

LRESULT CCallCoordinator::OnV2CallSignaling(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //中转参数下发：双方接通。切到"通话中"形态，并据下发的中转地址 / 令牌 / 角色启动
    //中转会话连上 imgserver 中转通道。第一期中转会话在通道上收发测试帧验证字节透传，
    //连通 / 透传校验 / 失败 / 断开等结果经 FMG_MSG_CALL_RELAY_EVENT 回报；第二期此处
    //承载真实音频采集 / 编码 / 播放管线。
    net::CallSignalingInfo* pInfo = (net::CallSignalingInfo*)lParam;
    if (pInfo == NULL)
        return 0;

    //据本次通话媒体类型决定中转会话是否起视频（视频通话才开摄像头采集 + H.264 编解码 + 视频解码线程）
    bool enableVideo = (m_callMediaType == net::call_media_video);

    //视频通话：先告知通话窗用视频布局（须在 OpenConnected 前），再把通话窗作为视频帧显示去向
    //注入中转会话（远端 / 本端帧解出后经它画到通话窗）。
    m_callDlg.SetVideoCall(enableVideo);
    //接通：停掉回铃 / 来电铃声，播一声接通提示。
    StopLoopSound();
    PlayNotifySound(kNotifySoundCallConnected,
                                 IsMuted());
    m_callDlg.OpenConnected(m_hHost);
    if (enableVideo)
        m_callMedia.SetVideoDisplaySink(m_callDlg.GetVideoDisplaySink());

    m_callMedia.Start(pInfo->m_relayIp, pInfo->m_relayPort, pInfo->m_token,
                      pInfo->m_role, pInfo->m_sessionId, m_hHost, FMG_MSG_CALL_RELAY_EVENT,
                      enableVideo);

    delete pInfo;
    return 0;
}

LRESULT CCallCoordinator::OnV2CallStateNotify(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //通话状态推送：接通态由 signaling 驱动；其余状态关对话框并据 state 提示。
    net::CallStateNotify* pState = (net::CallStateNotify*)lParam;
    if (pState == NULL)
        return 0;

    // 接通态在下面的分支里不做收尾，但铃声必须停 —— 原先只在 OnV2CallSignaling 里停，
    // 而那条路径并非每次接通都会走到（实测日志里收到 state=connected 却没有 signaling），
    // 于是回铃 / 来电铃声在接通后仍在响。这里无条件先停一次，最可靠。
    StopLoopSound();

    if (pState->m_state != net::call_state_connected)
    {
        //通话非接通态（结束 / 拒绝 / 取消 / 离线 / 超时 / 忙）：先停中转会话再关对话框。
        //本端挂断也走这里——挂断按钮发 cmd_call_hangup，服务端回推 state(已结束)。
        //
        //关窗前先把对端 userid 与通话时长取出来：CloseDlg 会销毁窗口，之后这两个值就没了。

        UINT    peerId       = (UINT)m_callDlg.GetPeerUserId();
        CString durationText = m_callDlg.GetCallDurationText();

        m_callMedia.Stop();
        m_callDlg.CloseDlg();

        //提示不再用 MessageBox 打断用户，改为在与对端的聊天窗里留一条居中系统提示 ——
        //通话结果本就属于两人之间的会话记录，留在聊天里可回溯，也不必点一下"确定"。
        CString tip;
        switch (pState->m_state)
        {
        case net::call_state_rejected:
            tip = _T("对方拒绝了通话");
            break;
        case net::call_state_canceled:
            tip = _T("对方已取消通话");
            break;
        case net::call_state_peer_offline:
            tip = _T("对方不在线");
            break;
        case net::call_state_timeout:
            tip = _T("对方无应答");
            break;
        case net::call_state_busy:
            tip = _T("对方正在通话中");
            break;
        case net::call_state_ended:
            //正常结束：接通过就带上时长（"通话结束 05:23"），没接通过只写"通话结束"。
            tip = _T("通话结束");
            if (!durationText.IsEmpty())
            {
                tip += _T(" ");
                tip += durationText;
            }
            break;
        default:
            break;
        }

        //一次性音效：正常结束播挂断音，其余异常结束（被拒 / 无应答 / 忙 / 离线 / 取消）播忙音。
        bool bMutedNow = IsMuted();
        if (pState->m_state == net::call_state_ended)
            PlayNotifySound(kNotifySoundCallEnded, bMutedNow);
        else
            PlayNotifySound(kNotifySoundCallBusy, bMutedNow);

        if (!tip.IsEmpty())
        {
            LOG_INFO("CallCoordinator: append call result tip to chat, peer=%u, state=%d.", peerId, pState->m_state);
            if (m_pPanel != NULL)
                m_pPanel->AppendCallTipToChat(peerId, tip);
        }
    }
    delete pState;
    return 0;
}

// v2 切换通话媒体类型的应答：只处理失败分支。成功时界面与引擎改动统一等服务端随后
// 推来的 cmd_call_media_changed_notify 再做（见 OnV2CallMediaChanged），这样本端与对端
// 走同一条路径，媒体类型必然一致。
LRESULT CCallCoordinator::OnV2CallSwitchMediaResult(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //lParam 为 IUSocket 接收线程堆分配的 net::CallSwitchMediaResponse，处理后须 delete。
    net::CallSwitchMediaResponse* pResp = (net::CallSwitchMediaResponse*)lParam;
    if (pResp == NULL)
        return 0;

    if (pResp->m_code != 0)
    {
        //按服务端返回的 error_code 给出具体原因，别让用户面对一句笼统的"切换失败"。
        LPCTSTR pszTip = _T("切换失败，请稍后重试。");
        if (pResp->m_code == error_code_call_not_exist)
            pszTip = _T("通话已结束，无法切换。");
        else if (pResp->m_code == error_code_call_not_member)
            pszTip = _T("你不是本次通话的参与方。");
        else if (pResp->m_code == error_code_call_state_invalid)
            pszTip = _T("通话尚未接通，接通后才能切换。");

        LOG_ERROR("v2 call switch media failed, code=%d, msg=%s.",
                  pResp->m_code, pResp->m_msg.c_str());

        HWND hOwner = m_callDlg.IsWindow() ? m_callDlg.m_hWnd : m_hHost;
        ::MessageBox(hOwner, pszTip, g_strAppTitle.c_str(), MB_OK | MB_ICONINFORMATION);
    }

    delete pResp;
    return 0;
}

// v2 通话媒体类型已变更的推送：双方（含发起切换者本人）都会收到，本端据此落地。
//
// 按已定交互模型（见 docs/音视频通话-媒体切换方案.md 第 3.1 节）：升级到视频时，
// <u>只有发起方自动打开摄像头</u>，另一方进入视频界面但摄像头保持关闭，由其自己决定
// 何时开启；降级回语音时双方摄像头都关。
LRESULT CCallCoordinator::OnV2CallMediaChanged(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
{
    //lParam 为 IUSocket 接收线程堆分配的 net::CallMediaChangedNotify，处理后须 delete。
    net::CallMediaChangedNotify* pNotify = (net::CallMediaChangedNotify*)lParam;
    if (pNotify == NULL)
        return 0;

    //推送可能晚于通话结束到达（如切换在途时对端挂断）：此时通话窗已关，直接丢弃，
    //不能去改一个已经不存在的窗口。挂断优先于切换。
    if (!m_callDlg.IsWindow() || m_callDlg.GetSessionId() != pNotify->m_sessionId)
    {
        LOG_INFO("v2 call media changed ignored (dialog closed or session mismatch), session=%s.",
                 pNotify->m_sessionId.c_str());
        delete pNotify;
        return 0;
    }

    bool bVideo  = (pNotify->m_media == net::call_media_video);
    UINT selfId  = (m_lpFMGClient != NULL) ? m_lpFMGClient->m_UserMgr.m_UserInfo.m_uUserID : 0;
    bool bIsSelf = (selfId != 0 && (UINT)pNotify->m_operator == selfId);
    //升级到视频时只有发起方自动开摄像头；降级回语音时两端都不开。
    bool bOpenCamera = bVideo && bIsSelf;

    LOG_INFO("v2 call media changed: session=%s, media=%d, operator=%lld, isSelf=%d, openCamera=%d.",
             pNotify->m_sessionId.c_str(), pNotify->m_media,
             (long long)pNotify->m_operator, bIsSelf ? 1 : 0, bOpenCamera ? 1 : 0);

    //先切引擎再切界面：引擎启停可能失败（摄像头被占用），据其结果决定界面呈现，
    //避免出现"界面已是视频态、实际没有画面且无提示"的不一致。
    bool bEngineOk = m_callMedia.EnableVideo(bVideo && bOpenCamera);
    if (bVideo && bOpenCamera && !bEngineOk)
    {
        //摄像头拿不到：仍进入视频界面（对端可能已在推流），但本端摄像头保持关闭并提示。
        LOG_ERROR("v2 call switch to video: local camera unavailable, keep camera off.");
        bOpenCamera = false;
    }

    m_callDlg.ApplyMediaMode(bVideo, bOpenCamera);

    delete pNotify;
    return 0;
}

// 通话窗关窗时请求停掉媒体会话。
//
// 为什么需要这条消息：通话窗自己不持有 CallMediaSession（持有者是本类），用户点
// 通话窗标题栏关闭按钮时它只发挂断信令 + 销毁自身，媒体会话仍在跑 —— 采集线程不停，
// 摄像头灯就一直亮着。正常挂断另有一条路径（服务端回推 ended，OnV2CallStateNotify 里
// 停会话），但那依赖推送按时到达；网络异常或推送延迟时就漏掉了。本消息把"关窗即停采集"
// 变成不依赖网络的确定行为。
//
// CallMediaSession::Stop 自身幂等（m_running.exchange 保证），故与 ended 推送那条路径
// 重复调用无害。
LRESULT CCallCoordinator::OnCallStopMedia(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/)
{
    LOG_INFO("CallCoordinator::OnCallStopMedia: call window closed, stop media session to release camera and microphone.");
    //关窗兜底：铃声停不下来比多调一次糟得多，这里再停一次（无声在播时是空操作）。
    StopLoopSound();
    m_callMedia.Stop();
    return 0;
}

LRESULT CCallCoordinator::OnCallRelayEvent(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam)
{
    //通话中转会话事件（来自 call::CallMediaSession 接收线程，已 PostMessage 转投本 UI 线程）。
    //第一期主要用于日志与端到端联调核对：接通态 UI 由 signaling 驱动，这里不改 UI、也不弹窗
    //打扰；中转失败 / 断开仅记日志（信令侧仍由挂断 / 状态推送收尾）。第二期可据此驱动通话窗
    //的媒体状态指示。
    int eventCode = (int)wParam;
    switch (eventCode)
    {
    case call::call_relay_connected:
        //本端已连上中转并握手成功、发出首帧测试帧
        LOG_INFO("Call relay connected: local channel ready.");
        break;
    case call::call_relay_test_ok:
        //收到对端测试帧且逐字节校验一致：字节透传端到端验证通过
        LOG_INFO("Call relay transport verified: test frame seq=%u.", (unsigned)lParam);
        break;
    case call::call_relay_failed:
        //中转连接 / 握手失败：通道未建立
        LOG_ERROR("Call relay connect/handshake failed.");
        break;
    case call::call_relay_closed:
        //中转异常断开：通话期间通道中断
        LOG_INFO("Call relay channel closed unexpectedly.");
        break;
    case call::call_relay_camera_unavailable:
        //本端摄像头不可用（没有设备 / 打不开 / 采集中途被别的程序抢走）：通话不中断，
        //只在通话窗本端那一路画面上给一句提示，免得用户对着黑屏猜。lParam 是失败原因。
        LOG_ERROR("Call local camera unavailable, reason=%d.", (int)lParam);
        m_callDlg.SetCameraUnavailable((int)lParam);
        break;
    default:
        break;
    }
    return 0;
}

} // namespace balloonmain
