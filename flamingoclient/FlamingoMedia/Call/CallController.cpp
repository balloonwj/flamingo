/**
 *  一对一通话控制器的实现，见 CallController.h。
 *
 *  各处理函数与 exe 原通话协调器（MainPanelCallCoordinator.cpp，第 1c 步删除）一一对应，注释里的
 *  设计取舍沿用原实现；差异只在依赖的来源（宿主接口、自有消息窗口），见头文件说明。
 *
 *  balloonwj@qq.com   2026-10-02
 */
#include "stdafx.h"

#include "CallController.h"

#include "../../Source/UI/BalloonMain/NotificationSound.h"   //回铃 / 来电铃声（循环）与接通 / 结束 / 忙音（一次性）
#include "../../Source/Common/Log/IULog.h"                   //LOG_INFO / LOG_ERROR（DLL 内经日志桥接写进 exe 的日志）
#include "../../Source/net/Protocol.h"                       //cmd_call_* 与 error_code_call_*
#include "../../Source/Common/I18n/I18n.h"   // TR：界面文字的多语言查找
#include "../Host/MediaLockState.h"          // 锁屏期间来电静默（docs/锁屏方案.md）

#include <string.h>   // strcmp：读诊断开关

namespace call {

//中转意外断开后「等待状态推送」的定时器 id（本类消息窗口自己的定时器）
static const UINT_PTR kRelayLossTimerId = 1;

//提示框标题
static const TCHAR* kVoiceCallTitle = TR_NOOP(L"语音通话");
static const TCHAR* kAppTitle       = _T("Flamingo");

//客户端用户号是 32 位无符号数，通话窗的对端 userid 按它换算（与原协调器一致）
static const int64_t kMaxClientUserId = 0xFFFFFFFFLL;

//一对一通话登记的 P2P 用途：与服务端 ChatSession::onCallInvite 建会话时的 purpose（kP2PPurposeCall）一致
static const char* const kCallP2PPurpose = "call";

//诊断开关：默认登记通话的 P2P 业务方（通话优先走 P2P）；环境变量 FLAMINGO_CALL_P2P 为 0 时不登记，通话照旧全部走
//imgserver 中转，服务端也不会为它建 P2P 会话
static const char* const kCallP2PEnvName = "FLAMINGO_CALL_P2P";
static const char* const kCallP2PEnvOff = "0";

//读环境变量用的缓冲（字符数）：开关值只有一个字符
static const DWORD kCallP2PEnvValueChars = 8;

//是否按诊断开关关闭了通话走 P2P
static bool CallP2PDisabledByEnv()
{
    char value[kCallP2PEnvValueChars] = { 0 };
    const DWORD n = ::GetEnvironmentVariableA(kCallP2PEnvName, value, kCallP2PEnvValueChars);
    return n > 0 && n < kCallP2PEnvValueChars && strcmp(value, kCallP2PEnvOff) == 0;
}

CCallController::CCallController()
    : m_host(NULL)
    , m_callMediaType(net::call_media_audio)
    , m_shareLink(NULL)
    , m_p2pBroker(kCallP2PPurpose)
    , m_presencePeer(0)
    , m_presenceMedia(0)
    , m_hasLockedInvite(false)
    , m_lockedInviteTick(0)
{
}

CCallController::~CCallController()
{
    Shutdown();
}

void CCallController::OnScreenLockChanged(bool locked)
{
    //服务端的振铃超时（第 15 期为 45 秒）；来电推送里没带剩余振铃时间时按它估算
    const int64_t kDefaultRingMs = 45000;
    if (locked)
    {
        //锁屏开始：停掉正在响的铃声（来电窗若开着就留在锁屏之下，解锁后照旧可以接听）
        balloonmain::StopLoopSound();
        return;
    }
    if (!m_hasLockedInvite)
    {
        return;
    }
    m_hasLockedInvite = false;
    const int64_t ringMs = (m_lockedInvite.m_ringMs > 0) ? m_lockedInvite.m_ringMs : kDefaultRingMs;
    const int64_t elapsed = static_cast<int64_t>(::GetTickCount64() - m_lockedInviteTick);
    if (elapsed >= ringMs)
    {
        //已过了振铃期：服务端随后会推超时，不再补弹
        LOG_INFO("call: screen unlocked after the ring period, invite not shown, sessionId=%s.",
                 m_lockedInvite.m_sessionId.c_str());
        return;
    }
    //解锁时仍在响铃：补弹来电窗（剩余振铃时间按锁屏期间已过去的时间扣减）并响铃
    CString fromName(CA2W(m_lockedInvite.m_fromName.c_str(), CP_UTF8));
    m_callDlg.SetVideoCall(m_lockedInvite.m_media == net::call_media_video);
    m_callDlg.OpenIncoming(MainWindow(), fromName, m_lockedInvite.m_sessionId, m_lockedInvite.m_fromId,
                           ringMs - elapsed);
    balloonmain::StartLoopSound(balloonmain::kNotifySoundCallIncoming, IsMuted());
    LOG_INFO("call: screen unlocked while still ringing, invite shown, sessionId=%s, remaining %lld ms.",
             m_lockedInvite.m_sessionId.c_str(), static_cast<long long>(ringMs - elapsed));
}

bool CCallController::Init(flamingomedia::IMediaHost* host)
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
    //仅接收消息的窗口：中转事件与「关窗请停媒体」两条异步消息投到这里
    if (Create(HWND_MESSAGE) == NULL)
    {
        LOG_ERROR("CallController: create message window failed, err=%lu.", ::GetLastError());
        m_host = NULL;
        return false;
    }
    m_callDlg.SetActions(this);
    m_callDlg.SetNotifyWindow(m_hWnd);
    //来电窗与通话画面里的头像经宿主取得（docs/音视频通话优化方案.md 第 1、5 项）
    m_callDlg.SetHost(host);
    RegisterP2P();
    LOG_INFO("CallController: initialized, p2p=%d.", m_p2pBroker.IsRegistered() ? 1 : 0);
    return true;
}

void CCallController::RegisterP2P()
{
    if (CallP2PDisabledByEnv())
    {
        LOG_INFO("CallController: call p2p is off (%s is 0), calls use the imgserver relay.", kCallP2PEnvName);
        return;
    }
    p2p::IP2PService* service = m_host->GetP2PService();
    if (service == NULL)
    {
        LOG_INFO("CallController: p2p service not available, calls use the imgserver relay.");
        return;
    }
    m_p2pBroker.Register(service);
}

void CCallController::ClaimCallSlot(const std::string& sessionId, const std::string& p2pSessionId)
{
    ReleaseCallSlot();
    if (p2pSessionId.empty() || sessionId.empty())
        return;
    if (!m_p2pBroker.IsRegistered())
    {
        LOG_WARNING("CallController: call %s carries p2p session %s but p2p is not registered, using relay.",
                    sessionId.c_str(), p2pSessionId.c_str());
        return;
    }
    m_p2pSlot = m_p2pBroker.ClaimSlot(sessionId);
    m_p2pSlotSessionId = sessionId;
    LOG_INFO("CallController: claimed p2p link slot for call %s, p2p session %s.", sessionId.c_str(),
             p2pSessionId.c_str());
}

void CCallController::ReleaseCallSlot()
{
    if (m_p2pSlotSessionId.empty())
        return;
    //放弃会话号：已就绪的链路立即关闭；还没建好的，之后建好时由业务方直接关闭
    m_p2pBroker.Abandon(m_p2pSlotSessionId);
    m_p2pSlot.reset();
    m_p2pSlotSessionId.clear();
}

void CCallController::Shutdown()
{
    ResetForLogout();
    //媒体会话与链路槽都已收尾：注销用途（P2P 模块在本模块之后才卸载）
    m_p2pBroker.Unregister();
    if (IsWindow())
    {
        DestroyWindow();
    }
    m_callDlg.SetActions(NULL);
    m_callDlg.SetNotifyWindow(NULL);
    m_callDlg.SetHost(NULL);
    m_host = NULL;
}

void CCallController::ResetForLogout()
{
    CancelRelayLossTimer();
    //顺序：先停铃声（循环播放的回铃 / 来电铃声不停就会一直响），再停中转会话释放
    //摄像头与麦克风，最后关窗。
    balloonmain::StopLoopSound();
    m_callMedia.Stop();
    ReleaseCallSlot();
    m_callDlg.CloseDlg();
    NotifyShareCallEnded();
    SyncPresence();
}

void CCallController::OnAvatarsUpdated()
{
    m_callDlg.RefreshAvatars();
}

void CCallController::SyncPresence()
{
    if (m_host == NULL)
        return;
    //通话中态才算「与对方通话中」；呼叫中、来电振铃都不算
    const bool inCall = m_callDlg.IsWindow() && m_callDlg.GetMode() == CBalloonCallDlg::kModeConnected
                        && m_callDlg.GetPeerUserId() > 0;
    const int64_t peer = inCall ? m_callDlg.GetPeerUserId() : 0;
    const int media = m_callDlg.IsVideoCall() ? flamingomedia::kMediaPresenceVideo : flamingomedia::kMediaPresenceVoice;
    if (peer == m_presencePeer && (peer == 0 || media == m_presenceMedia))
        return;
    //先结束上一次回报的，再回报新的
    if (m_presencePeer != 0 && peer != m_presencePeer)
    {
        LOG_INFO("CallController: presence ended, peer=%lld.", static_cast<long long>(m_presencePeer));
        m_host->OnCallPresence(flamingomedia::media_presence_single, m_presencePeer, 0, false);
    }
    if (peer != 0)
    {
        LOG_INFO("CallController: presence in call, peer=%lld, media=%d.", static_cast<long long>(peer), media);
        m_host->OnCallPresence(flamingomedia::media_presence_single, peer, media, true);
    }
    m_presencePeer = peer;
    m_presenceMedia = media;
}

void CCallController::SetShareLink(ICallShareLink* link)
{
    m_shareLink = link;
}

void CCallController::OnInCallShareChanged(bool hostingWithPeer)
{
    m_callDlg.SetScreenSharing(hostingWithPeer);
}

void CCallController::ToggleScreenShare()
{
    if (m_shareLink == NULL)
    {
        LOG_WARNING("CallController: share button clicked but screen share is unavailable.");
        return;
    }
    m_shareLink->ToggleShareInCall();
}

bool CCallController::SetLocalCamera(bool on)
{
    //打开时先关一次：采集中途断掉（摄像头被别的程序抢走）后会话仍记着「开着」，不先关就不会真正重开
    if (on)
        m_callMedia.SetCameraEnabled(false);
    const bool cameraOn = m_callMedia.SetCameraEnabled(on) && on;
    LOG_INFO("CallController: local camera %s by user, camera on=%d.", on ? "on" : "off", cameraOn ? 1 : 0);
    return cameraOn;
}

bool CCallController::SetLocalMic(bool on)
{
    m_callMedia.SetMicEnabled(on);
    const bool micOn = m_callMedia.IsMicEnabled();
    LOG_INFO("CallController: local microphone %s by user, mic on=%d.", on ? "on" : "off", micOn ? 1 : 0);
    return micOn;
}

void CCallController::SetPlaybackVolume(int percent)
{
    //拖动滑块时连续调用，不记日志（通话窗在浮层关闭时记最终值）
    m_callMedia.SetPlaybackVolume(percent);
}

int CCallController::GetPlaybackVolume() const
{
    return m_callMedia.PlaybackVolume();
}

CallNetSnapshot CCallController::GetNetSnapshot() const
{
    return m_callMedia.GetNetSnapshot();
}

void CCallController::NotifyShareCallEnded()
{
    //通话结束时媒体会话已停（电脑声音共享随之关闭）：撤掉互斥登记，按钮状态复位
    SystemAudioExclusive::Instance().Release(this);
    m_callDlg.SetSystemAudioOn(false);
    if (m_shareLink != NULL)
    {
        m_shareLink->OnCallEnded();
    }
}

int CCallController::SetSystemAudio(bool on)
{
    const int result = m_callMedia.SetSystemAudioEnabled(on);
    const bool nowOn = m_callMedia.IsSystemAudioEnabled();
    //先打开、成功之后再登记：打不开时不打扰屏幕共享那一路。登记时屏幕共享若也在共享电脑声音，会被要求停下
    SystemAudioHolder* preempted = NULL;
    if (nowOn)
        preempted = SystemAudioExclusive::Instance().Acquire(this);
    else
        SystemAudioExclusive::Instance().Release(this);
    m_callDlg.SetSystemAudioOn(nowOn);
    LOG_INFO("CallController: system audio %s by user, result=%d, on=%d, took over from screen share=%d.",
             on ? "on" : "off", result, nowOn ? 1 : 0, preempted != NULL ? 1 : 0);
    return result;
}

void CCallController::OnSystemAudioPreempted()
{
    //屏幕共享开始共享电脑声音：通话这一路停下，免得对方听到两遍
    if (!m_callMedia.IsSystemAudioEnabled())
        return;
    m_callMedia.SetSystemAudioEnabled(false);
    m_callDlg.SetSystemAudioOn(false);
    m_callDlg.ShowNotice(TR(L"电脑声音已改由屏幕共享发送"));
    LOG_INFO("CallController: system audio stopped, screen share took over.");
}

bool CCallController::IsCallSignal(uint16_t cmd)
{
    return cmd >= cmd_call_invite && cmd <= cmd_call_media_changed_notify;
}

bool CCallController::IsMicrophoneInUse() const
{
    return m_callMedia.IsRunning();
}

bool CCallController::IsMuted() const
{
    if (m_host == NULL)
    {
        return false;
    }
    return m_host->IsSoundMuted();
}

HWND CCallController::MainWindow() const
{
    if (m_host == NULL)
    {
        return NULL;
    }
    return m_host->GetMainWindow();
}

bool CCallController::SendCallSignal(uint16_t cmd, const std::string& body, const char* what)
{
    if (m_host == NULL || body.empty())
    {
        LOG_ERROR("CallController: skip %s, host=%p, body empty=%d.", what, m_host, body.empty() ? 1 : 0);
        return false;
    }
    if (!m_host->SendSignal(cmd, body.data(), static_cast<uint32_t>(body.size())))
    {
        LOG_ERROR("CallController: send %s failed, cmd=%u.", what, static_cast<unsigned>(cmd));
        return false;
    }
    return true;
}

bool CCallController::OnSignal(uint16_t cmd, const std::string& body)
{
    if (m_host == NULL || !IsCallSignal(cmd))
    {
        return false;
    }
    switch (cmd)
    {
    //邀请应答：成功带 sessionid，失败（对方离线 / 忙）带 code / msg
    case cmd_call_invite:
        {
            net::CallInviteResponse resp;
            if (net::CallProtocolClient::parseInviteResponse(body, resp))
            {
                LOG_INFO("Recv v2 call invite-ack, code=%d, sessionid=%s.", resp.m_code, resp.m_sessionId.c_str());
                HandleInviteAck(resp);
            }
            else
            {
                LOG_ERROR("Parse v2 call invite-ack failed, body=%s.", body.c_str());
            }
            break;
        }
    //来电推送：被叫弹来电窗
    case cmd_call_invite_notify:
        {
            net::CallInviteNotify notify;
            if (net::CallProtocolClient::parseInviteNotify(body, notify))
            {
                LOG_INFO("Recv v2 call invite-notify, from=%lld, session=%s.", static_cast<long long>(notify.m_fromId),
                         notify.m_sessionId.c_str());
                HandleInviteNotify(notify);
            }
            else
            {
                LOG_ERROR("Parse v2 call invite-notify failed, body=%s.", body.c_str());
            }
            break;
        }
    //中转参数下发：双方接通，连 imgserver 中转
    case cmd_call_signaling:
        {
            net::CallSignalingInfo info;
            if (net::CallProtocolClient::parseSignaling(body, info))
            {
                LOG_INFO("Recv v2 call signaling, session=%s, role=%d, %s:%d.", info.m_sessionId.c_str(), info.m_role,
                         info.m_relayIp.c_str(), info.m_relayPort);
                HandleSignaling(info);
            }
            else
            {
                LOG_ERROR("Parse v2 call signaling failed, body=%s.", body.c_str());
            }
            break;
        }
    //状态推送：响铃 / 拒绝 / 取消 / 离线 / 超时 / 接通 / 结束 / 忙
    case cmd_call_state_notify:
        {
            net::CallStateNotify state;
            if (net::CallProtocolClient::parseStateNotify(body, state))
            {
                LOG_INFO("Recv v2 call state-notify, session=%s, state=%d.", state.m_sessionId.c_str(), state.m_state);
                HandleStateNotify(state);
            }
            else
            {
                LOG_ERROR("Parse v2 call state-notify failed, body=%s.", body.c_str());
            }
            break;
        }
    //切换媒体的应答：只用于失败时提示原因，成功的改动等媒体已变更推送再做
    case cmd_call_switch_media:
        {
            net::CallSwitchMediaResponse resp;
            if (net::CallProtocolClient::parseSwitchMediaResponse(body, resp))
            {
                LOG_INFO("Recv v2 call switch-media ack, code=%d, msg=%s.", resp.m_code, resp.m_msg.c_str());
                HandleSwitchMediaResult(resp);
            }
            else
            {
                LOG_ERROR("Parse v2 call switch-media ack failed, body=%s.", body.c_str());
            }
            break;
        }
    //媒体已变更推送：双方（含发起者本人）都会收到，本端据此切换界面与引擎
    case cmd_call_media_changed_notify:
        {
            net::CallMediaChangedNotify notify;
            if (net::CallProtocolClient::parseMediaChangedNotify(body, notify))
            {
                LOG_INFO("Recv v2 call media-changed notify, session=%s, media=%d, operator=%lld.",
                         notify.m_sessionId.c_str(), notify.m_media, static_cast<long long>(notify.m_operator));
                HandleMediaChanged(notify);
            }
            else
            {
                LOG_ERROR("Parse v2 call media-changed notify failed, body=%s.", body.c_str());
            }
            break;
        }
    //接听 / 拒绝的应答：后续结果由状态推送驱动，这里只记日志（迁入 DLL 之前 exe 同样不处理）
    case cmd_call_response:
    //取消呼叫的应答：同上，只记日志
    case cmd_call_cancel:
    //挂断的应答：同上，只记日志
    case cmd_call_hangup:
        LOG_INFO("Recv v2 call ack, cmd=%u, body=%s.", static_cast<unsigned>(cmd), body.c_str());
        break;
    //号段内其余命令号目前没有定义，记一条日志后忽略
    default:
        LOG_INFO("Recv v2 call signal with unexpected cmd=%u, ignored.", static_cast<unsigned>(cmd));
        break;
    }
    return true;
}

bool CCallController::StartCall(int64_t peerUserId, const CString& peerName, bool video)
{
    if (m_host == NULL || peerUserId <= 0 || peerUserId > kMaxClientUserId)
    {
        return false;
    }
    int media = video ? net::call_media_video : net::call_media_audio;
    if (!SendCallSignal(cmd_call_invite, net::CallProtocolClient::buildInviteRequest(peerUserId, media), "call invite"))
    {
        return false;
    }
    LOG_INFO("Send v2 call invite, to=%lld, media=%d.", static_cast<long long>(peerUserId), media);
    //记下本次通话媒体类型，供接通后 signaling 决定中转会话是否起视频
    m_callMediaType = media;
    //告知对话框本次是视频还是纯语音：呼叫中的标题栏与提示文字据此措辞（也清掉上一通的标志）
    m_callDlg.SetVideoCall(video);
    m_callDlg.OpenCalling(MainWindow(), peerName, peerUserId);
    return true;
}

void CCallController::HandleInviteAck(const net::CallInviteResponse& resp)
{
    //邀请应答：成功把会话 id 回填给「呼叫中」对话框（取消时要用）；失败则关对话框并提示。
    bool bMuted = IsMuted();
    if (resp.m_code == 0)
    {
        m_callDlg.SetSessionId(resp.m_sessionId);
        //服务端在响铃阶段就为这通电话建了 P2P 会话：先认领链路槽，接通时交给媒体会话。呼叫窗已关说明用户已经取消，
        //直接放弃这个会话号，链路建好时随即关闭
        if (m_callDlg.IsWindow())
            ClaimCallSlot(resp.m_sessionId, resp.m_p2pSessionId);
        else if (!resp.m_p2pSessionId.empty() && m_p2pBroker.IsRegistered())
            m_p2pBroker.Abandon(resp.m_sessionId);
        //服务端已受理呼叫、对方开始响铃：本端起回铃音，循环到接通 / 被拒 / 超时 / 取消。
        //只在呼叫窗还在时才起：铃声的停止统一挂在通话窗销毁上，窗口已经没了还起铃声，
        //就再没有任何路径会去停它。
        if (m_callDlg.IsWindow())
        {
            balloonmain::StartLoopSound(balloonmain::kNotifySoundCallOutgoing, bMuted);
        }
        else
        {
            LOG_INFO("v2 call invite ack arrived after dialog closed, skip ringback.");
        }
    }
    else
    {
        //呼叫没起来：停掉可能已经起的回铃，播一声忙音。
        balloonmain::StopLoopSound();
        balloonmain::PlayNotifySound(balloonmain::kNotifySoundCallBusy, bMuted);
        //对端：邀请应答对应最近一次 OpenCalling 的对方（呼叫窗关了也记着）。2026-10-07 起提示写进与对方的单聊窗（灰色提示行，
        //聊天窗没开时由 exe 打开并前置），不再弹消息框；取不到对端时（不应发生）仍弹消息框，免得提示丢掉
        const int64_t peerId = m_callDlg.GetPeerUserId();
        m_callDlg.CloseDlg();
        const wchar_t* tip = TR(L"对方当前无法接听（不在线或正在通话中）。");
        LOG_INFO("CallController: invite rejected by server, code=%d, peer=%lld.", resp.m_code,
                 static_cast<long long>(peerId));
        if (peerId != 0 && m_host != NULL)
            m_host->AppendCallTip(peerId, tip);
        else
            ::MessageBox(MainWindow(), tip, TR(kVoiceCallTitle), MB_OK | MB_ICONINFORMATION);
    }
}

void CCallController::HandleInviteNotify(const net::CallInviteNotify& notify)
{
    //被叫收到来电：弹来电对话框（接听 / 拒绝经 ICallDialogActions 回到本类发 cmd_call_response）。
    //记下来电的媒体类型：被叫接通后 signaling 据此决定中转会话是否起视频
    m_callMediaType = notify.m_media;
    //锁屏期间（docs/锁屏方案.md 第 4.9 节）：不弹来电窗、不响铃，也不回绝（回绝会让本人其它设备上的响铃一起结束）。
    //照常认领链路槽，记下这通来电，解锁时若仍在响铃期内再弹来电窗
    if (flamingomedia::IsMediaScreenLocked())
    {
        ClaimCallSlot(notify.m_sessionId, notify.m_p2pSessionId);
        m_lockedInvite = notify;
        m_hasLockedInvite = true;
        m_lockedInviteTick = ::GetTickCount64();
        LOG_INFO("call: invite while the screen is locked, kept silent, sessionId=%s.", notify.m_sessionId.c_str());
        return;
    }
    CString fromName(CA2W(notify.m_fromName.c_str(), CP_UTF8));
    //被叫在接听前就能从提示文字看出对方约的是视频通话（视频通话接听后会打开摄像头）
    m_callDlg.SetVideoCall(notify.m_media == net::call_media_video);
    m_callDlg.OpenIncoming(MainWindow(), fromName, notify.m_sessionId, notify.m_fromId, notify.m_ringMs);
    //服务端在响铃阶段就为这通电话建了 P2P 会话：先认领链路槽，接听后交给媒体会话
    ClaimCallSlot(notify.m_sessionId, notify.m_p2pSessionId);
    //来电铃声：循环到接听 / 拒绝 / 对方取消 / 超时。
    balloonmain::StartLoopSound(balloonmain::kNotifySoundCallIncoming, IsMuted());
}

void CCallController::HandleSignaling(const net::CallSignalingInfo& info)
{
    //中转参数下发：双方接通。切到「通话中」形态，并据下发的中转地址 / 令牌 / 角色启动中转会话。
    bool enableVideo = (m_callMediaType == net::call_media_video);
    //视频通话：先告知通话窗用视频布局（须在 OpenConnected 前），再把通话窗作为视频帧显示去向注入中转会话
    m_callDlg.SetVideoCall(enableVideo);
    //接通：停掉回铃 / 来电铃声，播一声接通提示。
    balloonmain::StopLoopSound();
    balloonmain::PlayNotifySound(balloonmain::kNotifySoundCallConnected, IsMuted());
    m_callDlg.OpenConnected(MainWindow());
    //语音通话也先把通话窗设为画面的显示去向：中途切到视频时不再经过这里，不设的话双方都看不到画面。
    //通话窗是本类成员，指针在本类存活期间一直有效；语音期间没有视频帧，设了也不会有回调
    m_callMedia.SetVideoDisplaySink(m_callDlg.GetVideoDisplaySink());
    //告诉屏幕共享：此后与对方的单聊共享走通话的声音，对方发起的共享直接加入观看
    if (m_shareLink != NULL)
    {
        m_shareLink->OnCallConnected(m_callDlg.GetPeerUserId(), std::wstring(m_callDlg.GetPeerName().GetString()));
    }
    //响铃阶段认领的链路槽交给媒体会话（由它在 Stop 时关闭链路），本类不再持有
    std::shared_ptr<balloonp2p::P2PLinkSlot> slot;
    if (!m_p2pSlotSessionId.empty() && m_p2pSlotSessionId == info.m_sessionId)
    {
        slot = m_p2pSlot;
        m_p2pSlot.reset();
        m_p2pSlotSessionId.clear();
    }
    //中转握手用的服务端公钥由宿主提供（取自 exe 的主连接）；事件回投到本类的消息窗口
    m_callMedia.Start(info.m_relayIp, info.m_relayPort, info.m_token, m_host->GetServerPublicKey(),
                      info.m_role, info.m_sessionId, m_hWnd, kMsgCallRelayEvent, enableVideo, slot);
    //接通：主面板对方那一行显示「通话中」
    SyncPresence();
}

void CCallController::HandleStateNotify(const net::CallStateNotify& state)
{
    //锁屏期间记下的来电有了结果（对方取消、超时、在本人其它设备上接听或拒绝）：不再补弹
    if (m_hasLockedInvite && state.m_sessionId == m_lockedInvite.m_sessionId)
    {
        m_hasLockedInvite = false;
        LOG_INFO("call: invite kept during screen lock ended with state %d, sessionId=%s.", state.m_state,
                 state.m_sessionId.c_str());
    }
    //收到了服务端的状态推送：按推送收尾，不再由本端自行结束
    CancelRelayLossTimer();
    //接通态在下面的分支里不做收尾，但铃声必须停：接通信令并非每次都会到，这里无条件先停一次。
    balloonmain::StopLoopSound();
    if (state.m_state == net::call_state_connected)
    {
        return;
    }

    //通话非接通态（结束 / 拒绝 / 取消 / 离线 / 超时 / 忙）：先停中转会话再关对话框。
    //关窗前先把对端 userid 取出来：CloseDlg 会销毁窗口，之后就取不到了。
    int64_t peerId = m_callDlg.GetPeerUserId();
    m_callMedia.Stop();
    ReleaseCallSlot();
    m_callDlg.CloseDlg();
    NotifyShareCallEnded();
    SyncPresence();

    //没有建成会话的结果写成与对端聊天窗里的一条居中提示，可回溯，也不必点一下「确定」。拒绝、取消、超时与结束由服务端改写
    //单聊里的通话卡片显示（docs/音视频通话优化方案.md 第 6 项），不再另写提示
    CString tip;
    switch (state.m_state)
    {
    //对方不在线
    case net::call_state_peer_offline:
        tip = TR(L"对方不在线");
        break;
    //对方正在别的通话中
    case net::call_state_busy:
        tip = TR(L"对方正在通话中");
        break;
    //拒绝、取消、超时、结束：通话卡片会显示；其余状态（响铃等）不留提示
    default:
        break;
    }

    //一次性音效：正常结束播挂断音，其余异常结束播忙音。
    bool bMutedNow = IsMuted();
    if (state.m_state == net::call_state_ended)
    {
        balloonmain::PlayNotifySound(balloonmain::kNotifySoundCallEnded, bMutedNow);
    }
    else
    {
        balloonmain::PlayNotifySound(balloonmain::kNotifySoundCallBusy, bMutedNow);
    }

    if (!tip.IsEmpty())
    {
        LOG_INFO("CallController: append call result tip to chat, peer=%lld, state=%d.", static_cast<long long>(peerId),
                 state.m_state);
        //与 HandleInviteAck 相同：取不到对端时（不应发生）弹消息框兜底，免得提示丢掉
        if (peerId != 0 && m_host != NULL)
            m_host->AppendCallTip(peerId, tip);
        else
            ::MessageBox(MainWindow(), tip, TR(kVoiceCallTitle), MB_OK | MB_ICONINFORMATION);
    }
}

void CCallController::HandleSwitchMediaResult(const net::CallSwitchMediaResponse& resp)
{
    //只处理失败分支：成功时界面与引擎改动统一等服务端随后推来的媒体已变更通知再做。
    if (resp.m_code == 0)
    {
        return;
    }
    //按服务端返回的 error_code 给出具体原因，别让用户面对一句笼统的「切换失败」。
    LPCTSTR pszTip = TR(L"切换失败，请稍后重试。");
    if (resp.m_code == error_code_call_not_exist)
    {
        pszTip = TR(L"通话已结束，无法切换。");
    }
    else if (resp.m_code == error_code_call_not_member)
    {
        pszTip = TR(L"你不是本次通话的参与方。");
    }
    else if (resp.m_code == error_code_call_state_invalid)
    {
        pszTip = TR(L"通话尚未接通，接通后才能切换。");
    }
    LOG_ERROR("v2 call switch media failed, code=%d, msg=%s.", resp.m_code, resp.m_msg.c_str());
    HWND hOwner = m_callDlg.IsWindow() ? m_callDlg.m_hWnd : MainWindow();
    ::MessageBox(hOwner, pszTip, kAppTitle, MB_OK | MB_ICONINFORMATION);
}

void CCallController::HandleMediaChanged(const net::CallMediaChangedNotify& notify)
{
    //推送可能晚于通话结束到达（如切换在途时对端挂断）：此时通话窗已关，直接丢弃。挂断优先于切换。
    if (!m_callDlg.IsWindow() || m_callDlg.GetSessionId() != notify.m_sessionId)
    {
        LOG_INFO("v2 call media changed ignored (dialog closed or session mismatch), session=%s.",
                 notify.m_sessionId.c_str());
        return;
    }
    //升级到视频时只有发起方自动开摄像头；降级回语音时两端都不开（见 docs/音视频通话-媒体切换方案.md 3.1 节）。
    bool bVideo  = (notify.m_media == net::call_media_video);
    int64_t selfId = m_host->GetSelfUserId();
    bool bIsSelf = (selfId != 0 && notify.m_operator == selfId);
    bool bOpenCamera = bVideo && bIsSelf;
    LOG_INFO("v2 call media changed: session=%s, media=%d, operator=%lld, isSelf=%d, openCamera=%d.",
             notify.m_sessionId.c_str(), notify.m_media, static_cast<long long>(notify.m_operator), bIsSelf ? 1 : 0,
             bOpenCamera ? 1 : 0);

    //先切引擎再切界面：进入视频态时双方都开始接收对方画面，只有发起方打开摄像头（原先只有发起方
    //起了解码，另一方根本收不到画面）；回到语音时连同摄像头一起停。摄像头可能打不开（被占用），据其结果决定界面呈现
    m_callMedia.EnableVideo(bVideo);
    if (bVideo && bOpenCamera && !m_callMedia.SetCameraEnabled(true))
    {
        //摄像头拿不到：仍进入视频界面（对端可能已在推流），但本端摄像头保持关闭
        LOG_ERROR("v2 call switch to video: local camera unavailable, keep camera off.");
        bOpenCamera = false;
    }
    m_callDlg.ApplyMediaMode(bVideo, bOpenCamera);
    //媒体类型变了：徽章的图标随之改为电话或摄像机
    SyncPresence();
}

void CCallController::CancelCall(const std::string& sessionId)
{
    if (sessionId.empty())
    {
        return;
    }
    if (SendCallSignal(cmd_call_cancel, net::CallProtocolClient::buildCancel(sessionId), "call cancel"))
    {
        LOG_INFO("Send v2 call cancel, session=%s.", sessionId.c_str());
    }
    //主叫取消：服务端只通知被叫，本端在这里放弃响铃阶段认领的链路槽
    ReleaseCallSlot();
}

void CCallController::RespondCall(const std::string& sessionId, bool accept)
{
    if (sessionId.empty())
    {
        return;
    }
    if (SendCallSignal(cmd_call_response, net::CallProtocolClient::buildResponse(sessionId, accept), "call response"))
    {
        LOG_INFO("Send v2 call response, session=%s, accept=%d.", sessionId.c_str(), accept ? 1 : 0);
    }
    //被叫拒绝：服务端只通知主叫，本端在这里放弃响铃阶段认领的链路槽
    if (!accept)
        ReleaseCallSlot();
}

void CCallController::HangupCall(const std::string& sessionId)
{
    if (sessionId.empty())
    {
        return;
    }
    if (SendCallSignal(cmd_call_hangup, net::CallProtocolClient::buildHangup(sessionId), "call hangup"))
    {
        LOG_INFO("Send v2 call hangup, session=%s.", sessionId.c_str());
    }
}

void CCallController::SwitchCallMedia(const std::string& sessionId, int media)
{
    //sessionId 为空或 media 非法时 buildSwitchMedia 返回空串（服务端必判非法），直接放弃，省一次必然失败的往返
    std::string body = net::CallProtocolClient::buildSwitchMedia(sessionId, media);
    if (body.empty())
    {
        LOG_ERROR("Skip v2 call switch-media: invalid session=%s, media=%d.", sessionId.c_str(), media);
        return;
    }
    if (SendCallSignal(cmd_call_switch_media, body, "call switch-media"))
    {
        LOG_INFO("Send v2 call switch-media, session=%s, media=%d.", sessionId.c_str(), media);
    }
}

LRESULT CCallController::OnStopMediaMsg(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    //通话窗关窗时请求停掉媒体会话：通话窗不持有 CallMediaSession，只销毁窗口的话采集线程会继续跑、
    //摄像头灯不灭。正常挂断时状态推送那条路径会再停一次，CallMediaSession::Stop 幂等，重复调用无害。
    LOG_INFO("CallController: call window closed, stop media session to release camera and microphone.");
    CancelRelayLossTimer();
    balloonmain::StopLoopSound();
    m_callMedia.Stop();
    ReleaseCallSlot();
    NotifyShareCallEnded();
    //本端主动挂断（或点标题栏关闭）时服务端只把结束推送发给对方，本端的「通话中」标记只能在这里撤掉
    //（通话窗此时已销毁，SyncPresence 据此回报通话结束；2026-10-05 界面联调查出挂断的一方徽章残留）
    SyncPresence();
    return 0;
}

void CCallController::CancelRelayLossTimer()
{
    if (IsWindow())
        KillTimer(kRelayLossTimerId);
}

LRESULT CCallController::OnTimerMsg(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& bHandled)
{
    if (wParam != kRelayLossTimerId)
    {
        bHandled = FALSE;
        return 0;
    }
    KillTimer(kRelayLossTimerId);
    if (!m_callDlg.IsWindow() || !ShouldEndCallAfterRelayLoss(m_callDlg.GetMode(), m_callDlg.GetSessionId()))
        return 0;

    //等了一段时间仍没有状态推送：对方掉线或退出了程序。发挂断让服务端立刻清掉会话（不必等它的掉线宽限期），
    //再按挂断收尾：停媒体、关窗、在与对方的聊天窗里留一条「通话已中断」
    const std::string sessionId = m_callDlg.GetSessionId();
    const int64_t peerId = m_callDlg.GetPeerUserId();
    const CString tip = MakeCallInterruptedTip(m_callDlg.GetCallDurationText());
    LOG_INFO("CallController: no call state notify within %u ms after the relay closed, end the call locally, session=%s.",
             kRelayLossGraceMs, sessionId.c_str());
    HangupCall(sessionId);
    m_callMedia.Stop();
    m_callDlg.CloseDlg();
    NotifyShareCallEnded();
    SyncPresence();
    balloonmain::PlayNotifySound(balloonmain::kNotifySoundCallBusy, IsMuted());
    if (m_host != NULL)
        m_host->AppendCallTip(peerId, tip);
    return 0;
}

LRESULT CCallController::OnRelayEventMsg(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& /*bHandled*/)
{
    //通话中转会话事件（来自 CallMediaSession 的接收线程 / 采集线程，已投回 UI 线程）。接通态界面由
    //signaling 驱动，这里只记日志；中转失败 / 断开由信令侧的挂断 / 状态推送收尾。
    int eventCode = static_cast<int>(wParam);
    switch (eventCode)
    {
    //本端已连上中转并握手成功、音视频引擎已启动
    case call_relay_connected:
        LOG_INFO("Call relay connected: local channel ready, link path %d.", m_callMedia.LinkPath());
        //链路实际路径在接通时就定了：直连时通话窗底部栏显示「P2P」徽章
        m_callDlg.SetLinkPath(m_callMedia.LinkPath());
        break;
    //第一期测试帧遗留的事件，真实音视频不再投递
    case call_relay_test_ok:
        LOG_INFO("Call relay transport verified: test frame seq=%u.", static_cast<unsigned>(lParam));
        break;
    //中转连接 / 握手失败：通道未建立
    case call_relay_failed:
        LOG_ERROR("Call relay connect/handshake failed.");
        break;
    //中转异常断开：通话期间通道中断。声音与画面已经不通，且没有中转重连：等一小段时间，
    //期间收到状态推送（正常挂断）按推送收尾，否则由本端自行结束
    case call_relay_closed:
        LOG_INFO("Call relay channel closed unexpectedly.");
        if (m_callDlg.IsWindow() && ShouldEndCallAfterRelayLoss(m_callDlg.GetMode(), m_callDlg.GetSessionId()))
        {
            LOG_INFO("CallController: wait %u ms for a call state notify before ending the call locally, session=%s.",
                     kRelayLossGraceMs, m_callDlg.GetSessionId().c_str());
            SetTimer(kRelayLossTimerId, kRelayLossGraceMs, NULL);
        }
        break;
    //本端摄像头不可用：通话不中断，只在通话窗本端画面上给一句提示，lParam 是失败原因
    case call_relay_camera_unavailable:
        LOG_ERROR("Call local camera unavailable, reason=%d.", static_cast<int>(lParam));
        m_callDlg.SetCameraUnavailable(static_cast<int>(lParam));
        break;
    //Debug 版无设备模式：音频引擎没打开麦克风与扬声器，在通话窗画面顶部提示（2026-10-05 起）
    case call_relay_no_audio_devices:
        LOG_WARNING("Call audio runs without devices (debug switch), show a banner in the call window.");
        m_callDlg.SetNoAudioDevices(true);
        break;
    //未知事件码
    default:
        break;
    }
    return 0;
}

} // namespace call
