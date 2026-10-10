/**
 *  群通话控制器，GroupCallController.cpp
 *  职责与线程约定见 GroupCallController.h；信令字段见 docs/协议设计-v2.md 第 21 节。
 *
 *  balloonwj@qq.com   2026-10-04
 */
#include "stdafx.h"

#include "GroupCallController.h"

#include <stdlib.h>
#include <algorithm>
#include "../../Source/net/Protocol.h"
#include "../../Source/Common/Log/IULog.h"
#include "../../Source/UI/BalloonMain/NotificationSound.h"   // StartLoopSound / StopLoopSound（来电铃声与回铃）
#include "../../Source/Common/I18n/I18n.h"                   // TR / TRF：界面文字的多语言查找
#include "../Host/MediaLockState.h"                          // 锁屏期间群通话邀请静默（docs/锁屏方案.md）

namespace groupcall {

namespace {

//定时器：画面刷新（约每秒 15 次）、慢刷新（说话人、时长、订阅、回铃，约每秒 3 次）、中转重连的延迟
const UINT_PTR kTimerFrames = 1;
const UINT_PTR kTimerSlow = 2;
const UINT_PTR kTimerRelayRetry = 3;
const UINT kFramesIntervalMs = 66;
const UINT kSlowIntervalMs = 300;
const UINT kRelayRetryDelayMs = 2000;
//中转断开后最多重新加入几次
const int kMaxRelayRetries = 3;
//临时提示的显示时长，单位毫秒
const int64_t kNoticeMs = 5000;
//一组人名最多列几个，超出时写「等 N 人」
const size_t kMaxNamesInNotice = 3;
//显示名缓冲的字符数
const uint32_t kNameBufChars = 128;
//角色还没查过的标记
const int kRoleUnknown = -2;
//群角色：管理员与群主（与协议 group_role 的取值一致）
const int kRoleAdmin = 1;
const int kRoleOwner = 2;
//Debug 版联调开关的环境变量名与取值缓冲
const wchar_t* const kNoDevicesEnv = L"FLAMINGO_GROUPCALL_NO_DEVICES";
const DWORD kEnvBufChars = 8;

int64_t nowTick()
{
    return static_cast<int64_t>(::GetTickCount64());
}

} // namespace

CGroupCallController::CGroupCallController()
    : m_host(NULL)
    , m_phase(phase_idle)
    , m_groupId(0)
    , m_media(gc_media_voice)
    , m_inviterId(0)
    , m_wantMic(true)
    , m_wantCamera(false)
    , m_mediaToken(0)
    , m_relayRetries(0)
    , m_callStartTick(0)
    , m_noticeUntil(0)
    , m_ringBack(false)
    , m_cachedRole(kRoleUnknown)
    , m_presenceGroup(0)
    , m_presenceMedia(0)
{
}

CGroupCallController::~CGroupCallController()
{
    Shutdown();
}

bool CGroupCallController::Init(flamingomedia::IMediaHost* host)
{
    if (host == NULL)
        return false;
    if (IsWindow())
        return true;
    m_host = host;
    //来电窗的群头像、通话窗格子里的成员头像经宿主取得（docs/音视频通话优化方案.md 第 1、5 项）
    m_incoming.SetHost(host);
    m_window.SetHost(host);
    //只收消息的窗口：媒体会话的事件、来电窗与通话窗的按钮都投到这里
    if (Create(HWND_MESSAGE) == NULL)
    {
        LOG_ERROR("GroupCallController: create message window failed, err=%lu.", ::GetLastError());
        m_host = NULL;
        return false;
    }
    LOG_INFO("GroupCallController: initialized.");
    return true;
}

void CGroupCallController::Shutdown()
{
    if (m_phase == phase_incoming)
        declineInvite();
    else if (m_phase != phase_idle)
        leaveCall(true);
    resetLocal();
    if (IsWindow())
        DestroyWindow();
    m_host = NULL;
}

bool CGroupCallController::IsGroupCallSignal(uint16_t cmd)
{
    return cmd >= cmd_groupcall_start && cmd <= cmd_groupcall_manage;
}

bool CGroupCallController::OnSignal(uint16_t cmd, const std::string& json)
{
    switch (cmd)
    {
    //发起的应答
    case cmd_groupcall_start:
        onStartResult(json);
        return true;

    //被邀请：弹来电窗
    case cmd_groupcall_invite_notify:
        onInvite(json);
        return true;

    //加入的应答
    case cmd_groupcall_join:
        onJoinResult(json);
        return true;

    //拒绝、离开的应答：本地已经收尾，只记日志
    case cmd_groupcall_decline:
    case cmd_groupcall_leave:
        LOG_INFO("groupcall: cmd %u answered.", static_cast<unsigned>(cmd));
        return true;

    //房间状态
    case cmd_groupcall_state_notify:
        onStateNotify(json);
        return true;

    //中转参数：启动媒体会话
    case cmd_groupcall_relay_info:
        onRelayInfo(json);
        return true;

    //追加邀请的应答
    case cmd_groupcall_invite_more:
        onInviteMoreResult(json);
        return true;

    //上报媒体的应答（摄像头满时要关掉本地摄像头）
    case cmd_groupcall_member_media:
        onMemberMediaResult(json);
        return true;

    //查询的应答
    case cmd_groupcall_query:
        onQueryResult(json);
        return true;

    //群内状态（开始、人数变化、结束）
    case cmd_groupcall_status_notify:
        onStatusNotify(json);
        return true;

    //管理操作的应答
    case cmd_groupcall_manage:
        onManageResult(json);
        return true;

    //不是群通话的命令
    default:
        return false;
    }
}

bool CGroupCallController::StartCall(int64_t groupId, const std::wstring& groupName, int media,
                                     const std::vector<int64_t>& invitees)
{
    if (m_host == NULL || groupId <= 0)
        return false;
    if (m_phase != phase_idle)
    {
        //已经在这个群的通话里：把通话窗提到前面即可
        if (m_groupId == groupId && m_phase == phase_in_call && m_window.IsShowing())
        {
            m_window.ShowWindow(SW_RESTORE);
            ::SetForegroundWindow(m_window.m_hWnd);
            return true;
        }
        showError(TR(L"你正在群通话中，请先结束当前的群通话。"));
        return false;
    }
    m_groupId = groupId;
    m_groupName = groupName;
    m_media = media;
    m_roomId.clear();
    m_state = GroupCallState();
    m_wantMic = true;
    m_wantCamera = (media == gc_media_video);
    m_cachedRole = kRoleUnknown;
    if (!send(cmd_groupcall_start, GroupCallProtocolClient::buildStart(groupId, media, invitees, m_wantMic,
                                                                         m_wantCamera)))
    {
        resetLocal();
        showError(TR(L"网络未连接，无法发起群通话。"));
        return false;
    }
    m_phase = phase_starting;
    LOG_INFO("groupcall: start requested, group=%lld, media=%d, invitees=%u.", static_cast<long long>(groupId), media,
             static_cast<unsigned>(invitees.size()));
    return true;
}

bool CGroupCallController::JoinCall(int64_t groupId, const std::wstring& groupName, const std::string& roomId)
{
    if (m_host == NULL || groupId <= 0 || roomId.empty())
        return false;
    if (m_phase != phase_idle)
    {
        //正在振铃的就是这一场：等同接听
        if (m_phase == phase_incoming && m_roomId == roomId)
        {
            acceptInvite();
            return true;
        }
        //已在这一场里：把通话窗提到前面
        if (m_roomId == roomId && m_window.IsShowing())
        {
            m_window.ShowWindow(SW_RESTORE);
            ::SetForegroundWindow(m_window.m_hWnd);
            return true;
        }
        showError(TR(L"你正在群通话中，请先结束当前的群通话。"));
        return false;
    }
    m_groupId = groupId;
    m_groupName = groupName;
    m_roomId = roomId;
    m_state = GroupCallState();
    m_media = gc_media_voice;
    m_wantMic = true;
    m_wantCamera = false;
    m_cachedRole = kRoleUnknown;
    if (!sendJoin(m_wantMic, m_wantCamera))
    {
        resetLocal();
        showError(TR(L"网络未连接，无法加入群通话。"));
        return false;
    }
    m_phase = phase_joining;
    LOG_INFO("groupcall: join requested from banner or card, group=%lld, room=%s.", static_cast<long long>(groupId),
             roomId.c_str());
    return true;
}

void CGroupCallController::Query(int64_t groupId)
{
    if (m_host == NULL || groupId <= 0)
        return;
    if (send(cmd_groupcall_query, GroupCallProtocolClient::buildQuery(groupId)))
        m_pendingQueries.push_back(groupId);
}

bool CGroupCallController::InviteMembers(int64_t groupId, const std::vector<int64_t>& userIds)
{
    if (m_phase != phase_in_call || groupId != m_groupId || userIds.empty())
        return false;
    return send(cmd_groupcall_invite_more, GroupCallProtocolClient::buildInviteMore(m_roomId, userIds));
}

void CGroupCallController::ResetForLogout()
{
    //登出之后主连接已断，信令发不出去；服务端按下线处理（振铃中移除、已加入的人进入宽限期后离开）
    resetLocal();
    m_pendingQueries.clear();
    m_nameCache.clear();
}

bool CGroupCallController::IsBusy() const
{
    return m_phase != phase_idle;
}

bool CGroupCallController::IsMicrophoneInUse() const
{
    return m_phase == phase_in_call && m_mediaSession.IsRunning() && m_mediaSession.IsMicEnabled();
}

bool CGroupCallController::GetTileFrame(int seat, bool self, std::vector<uint8_t>& bgra, int& width, int& height,
                                        uint64_t& serial)
{
    return m_mediaSession.GetCameraFrame(seat, self, bgra, width, height, serial);
}

//======================= 信令处理 =======================

void CGroupCallController::onStartResult(const std::string& json)
{
    if (m_phase != phase_starting)
        return;
    GroupCallResult r;
    if (!GroupCallProtocolClient::parseResult(json, r))
    {
        LOG_ERROR("groupcall: bad start result.");
        resetLocal();
        return;
    }
    //群里已有群通话：改为加入那一场
    if (r.m_code == error_code_groupcall_exists && !r.m_roomId.empty())
    {
        LOG_INFO("groupcall: group already in call, join it instead, room=%s.", r.m_roomId.c_str());
        m_roomId = r.m_roomId;
        if (sendJoin(m_wantMic, m_wantCamera))
            m_phase = phase_joining;
        else
            resetLocal();
        return;
    }
    if (r.m_code != error_code_ok)
    {
        const std::wstring text = errorText(r.m_code);
        LOG_WARNING("groupcall: start rejected, code=%d.", r.m_code);
        resetLocal();
        showError(text);
        return;
    }
    m_roomId = r.m_roomId;
    m_phase = phase_waiting;
    //没振铃的人：告诉发起人
    std::wstring notice;
    if (!r.m_busy.empty())
        notice += TRF(L"{0} 正在通话中，没有邀请到。", namesOf(r.m_busy));
    if (!r.m_offline.empty())
        notice += TRF(L"{0} 不在线，没有邀请到。", namesOf(r.m_offline));
    if (!notice.empty())
        showNotice(notice);
    LOG_INFO("groupcall: started, room=%s, busy=%u, offline=%u.", m_roomId.c_str(),
             static_cast<unsigned>(r.m_busy.size()), static_cast<unsigned>(r.m_offline.size()));
}

void CGroupCallController::onInvite(const std::string& json)
{
    GroupCallInvite inv;
    if (!GroupCallProtocolClient::parseInvite(json, inv))
    {
        LOG_ERROR("groupcall: bad invite notify.");
        return;
    }
    //同一时间只有一场：服务端不会给正忙的人振铃，这里兜底按拒绝处理
    if (m_phase != phase_idle)
    {
        LOG_WARNING("groupcall: invite while busy, decline it, room=%s.", inv.m_room.m_roomId.c_str());
        send(cmd_groupcall_decline, GroupCallProtocolClient::buildRoomRequest(inv.m_room.m_roomId));
        return;
    }
    //锁屏期间（docs/锁屏方案.md 第 4.9 节）：不弹邀请窗、不响铃，也不谢绝（谢绝会让本人其它设备上的邀请一起结束）；
    //群通话照常进行，只上报状态，解锁后可在群聊窗里加入
    if (flamingomedia::IsMediaScreenLocked())
    {
        reportStatus(inv.m_room.m_groupId, inv.m_room.m_roomId, true, inv.m_room.m_media, inv.m_room.joinedCount(),
                     inv.m_room.m_initiatorId, inv.m_room.m_startTime);
        LOG_INFO("groupcall: invite while the screen is locked, kept silent, room=%s.", inv.m_room.m_roomId.c_str());
        return;
    }
    m_phase = phase_incoming;
    m_groupId = inv.m_room.m_groupId;
    m_groupName = utf8ToWide(inv.m_groupName);
    m_media = inv.m_room.m_media;
    m_roomId = inv.m_room.m_roomId;
    m_inviterId = inv.m_inviterId;
    m_state = inv.m_room;
    m_cachedRole = kRoleUnknown;
    std::wstring inviter = utf8ToWide(inv.m_inviterName);
    if (inviter.empty())
        inviter = displayName(inv.m_inviterId);
    m_incoming.Open(m_groupId, MakeGroupCallInvitePrompt(inviter, m_media),
                    MakeGroupCallInviteDetail(m_groupName, inv.m_room.joinedCount()),
                    MakeGroupCallTitle(std::wstring(), m_media), inv.m_ringMs, m_hWnd);
    balloonmain::StartLoopSound(balloonmain::kNotifySoundCallIncoming, m_host->IsSoundMuted());
    reportStatus(m_groupId, m_roomId, true, m_media, inv.m_room.joinedCount(), inv.m_room.m_initiatorId,
                 inv.m_room.m_startTime);
    LOG_INFO("groupcall: invited, room=%s, group=%lld, inviter=%lld.", m_roomId.c_str(),
             static_cast<long long>(m_groupId), static_cast<long long>(m_inviterId));
}

void CGroupCallController::onJoinResult(const std::string& json)
{
    if (m_phase != phase_joining && m_phase != phase_in_call)
        return;
    GroupCallResult r;
    if (!GroupCallProtocolClient::parseResult(json, r))
        return;
    if (r.m_code == error_code_ok)
    {
        //首次加入：等中转参数；断线重连：已在通话中，等新的中转参数即可
        if (m_phase == phase_joining)
            m_phase = phase_waiting;
        return;
    }
    LOG_WARNING("groupcall: join rejected, code=%d, room=%s.", r.m_code, m_roomId.c_str());
    const std::wstring text = errorText(r.m_code);
    resetLocal();
    showError(text);
}

void CGroupCallController::onStateNotify(const std::string& json)
{
    GroupCallState n;
    if (!GroupCallProtocolClient::parseStateNotify(json, n))
    {
        LOG_ERROR("groupcall: bad state notify.");
        return;
    }
    if (m_phase == phase_idle || n.m_roomId != m_roomId)
        return;
    const int64_t selfId = m_host->GetSelfUserId();
    const GroupCallMember* self = n.findMember(selfId);

    //振铃中：别的设备接听或拒绝、振铃超时、被移出时收起来电窗；否则刷新正在通话的人数
    if (m_phase == phase_incoming)
    {
        m_state = n;
        if (self == NULL || self->m_state != gc_member_ringing)
        {
            LOG_INFO("groupcall: ringing ended by server, event=%d, room=%s.", n.m_event, m_roomId.c_str());
            resetLocal();
            return;
        }
        m_incoming.SetDetail(MakeGroupCallInviteDetail(m_groupName, n.joinedCount()));
        return;
    }

    //被移出：退出通话并提示
    if (n.m_event == gc_event_member_removed && n.m_target == selfId)
    {
        LOG_INFO("groupcall: removed from call by %lld, room=%s.", static_cast<long long>(n.m_actor),
                 m_roomId.c_str());
        resetLocal();
        showError(TR(L"你已被移出群通话。"));
        return;
    }
    //快照里没有本人了（例如另一台设备上离开了）：本地收尾
    if (m_phase == phase_in_call && self == NULL)
    {
        LOG_INFO("groupcall: self no longer in room, leave locally, room=%s.", m_roomId.c_str());
        resetLocal();
        return;
    }

    //离开了的座位：删掉声音路与解码器
    std::set<int> before;
    for (size_t i = 0; i < m_state.m_members.size(); ++i)
    {
        if (m_state.m_members[i].m_state == gc_member_joined)
            before.insert(m_state.m_members[i].m_seat);
    }
    for (size_t i = 0; i < n.m_members.size(); ++i)
    {
        if (n.m_members[i].m_state == gc_member_joined)
            before.erase(n.m_members[i].m_seat);
    }
    for (std::set<int>::const_iterator it = before.begin(); it != before.end(); ++it)
        m_mediaSession.RemoveSeat(*it);

    m_state = n;
    m_media = n.m_media;

    //被静音或处于全员静音：关掉本人麦克风并提示（被单独静音的之后可以自己重新打开，全员静音要等解除）
    const int micAction = DecideSelfMicOnState(n, selfId, canManage(), m_mediaSession.IsMicEnabled());
    if (micAction != gc_self_mic_keep)
    {
        m_wantMic = false;
        m_mediaSession.SetMicEnabled(false);
        showNotice(micAction == gc_self_mic_muted_by_all ? TR(L"已开启全员静音，你的麦克风已关闭。")
                                                         : TR(L"你已被静音，可以自己重新打开麦克风。"));
    }
    //解除全员静音：原本受限制的人可以自己打开麦克风了
    else if (n.m_event == gc_event_unmuted_all && n.m_actor != selfId && !canManage())
    {
        showNotice(TR(L"全员静音已解除，你可以打开麦克风了。"));
    }
    refreshView();
}

void CGroupCallController::onRelayInfo(const std::string& json)
{
    GroupCallRelayInfo relay;
    if (!GroupCallProtocolClient::parseRelayInfo(json, relay))
    {
        LOG_ERROR("groupcall: bad relay info.");
        return;
    }
    if (relay.m_roomId != m_roomId || (m_phase != phase_waiting && m_phase != phase_in_call))
        return;
    m_callStartTick = nowTick() - relay.m_elapsedMs;
    startMedia(relay);
}

void CGroupCallController::onInviteMoreResult(const std::string& json)
{
    GroupCallResult r;
    if (!GroupCallProtocolClient::parseResult(json, r))
        return;
    if (r.m_code != error_code_ok)
    {
        showNotice(errorText(r.m_code));
        return;
    }
    std::wstring notice;
    if (!r.m_busy.empty())
        notice += TRF(L"{0} 正在通话中，没有邀请到。", namesOf(r.m_busy));
    if (!r.m_offline.empty())
        notice += TRF(L"{0} 不在线，没有邀请到。", namesOf(r.m_offline));
    if (!notice.empty())
        showNotice(notice);
}

void CGroupCallController::onMemberMediaResult(const std::string& json)
{
    GroupCallResult r;
    if (!GroupCallProtocolClient::parseResult(json, r) || r.m_code == error_code_ok)
        return;
    //摄像头人数已满：关掉本地摄像头
    if (r.m_code == error_code_groupcall_camera_limit)
    {
        m_wantCamera = false;
        m_mediaSession.SetCameraEnabled(false);
        showNotice(TR(L"同时开摄像头的人数已满，暂时不能打开摄像头。"));
        refreshView();
        return;
    }
    //全员静音期间没有管理权的人开麦克风：关掉本地麦克风（服务端记录的仍是关）
    if (r.m_code == error_code_groupcall_no_permission)
    {
        m_wantMic = false;
        m_mediaSession.SetMicEnabled(false);
        showNotice(TR(L"已开启全员静音，只有发起人、群主和管理员可以打开麦克风。"));
        refreshView();
        return;
    }
    LOG_WARNING("groupcall: member media rejected, code=%d.", r.m_code);
}

void CGroupCallController::onQueryResult(const std::string& json)
{
    if (m_pendingQueries.empty())
        return;
    const int64_t groupId = m_pendingQueries.front();
    m_pendingQueries.pop_front();
    GroupCallQueryResult r;
    if (!GroupCallProtocolClient::parseQueryResult(json, r) || r.m_code != error_code_ok)
        return;
    if (r.m_exists)
        reportStatus(groupId, r.m_roomId, true, r.m_media, r.m_count, r.m_initiatorId, r.m_startTime);
    else
        reportStatus(groupId, std::string(), false, 0, 0, 0, 0);
}

void CGroupCallController::onStatusNotify(const std::string& json)
{
    GroupCallStatus s;
    if (!GroupCallProtocolClient::parseStatus(json, s))
    {
        LOG_ERROR("groupcall: bad status notify.");
        return;
    }
    if (s.m_active)
        reportStatus(s.m_groupId, s.m_roomId, true, s.m_media, s.m_count, s.m_initiatorId, s.m_startTime);
    else
        reportStatus(s.m_groupId, std::string(), false, 0, 0, 0, 0);
    if (!s.m_active && s.m_cardMsgId > 0 && m_host != NULL)
        m_host->OnGroupCallCardChanged(s.m_groupId, s.m_cardMsgId, s.m_cardContent.c_str());
    //本人所在的那一场结束了（振铃中最常见：发起人一个人等着、自己挂了）
    if (!s.m_active && s.m_roomId == m_roomId && m_phase != phase_idle)
    {
        LOG_INFO("groupcall: room ended, room=%s.", m_roomId.c_str());
        resetLocal();
    }
}

void CGroupCallController::onManageResult(const std::string& json)
{
    GroupCallResult r;
    if (!GroupCallProtocolClient::parseResult(json, r) || r.m_code == error_code_ok)
        return;
    showNotice(errorText(r.m_code));
}

//======================= 窗口与媒体事件 =======================

LRESULT CGroupCallController::OnMediaEventMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)bHandled;
    //旧会话的事件：丢掉
    if (lParam != m_mediaToken || m_phase != phase_in_call)
        return 0;
    switch (wParam)
    {
    //连上了：重连计数清零，按当前宫格订阅
    case gc_media_connected:
        m_relayRetries = 0;
        refreshView();
        break;

    //连不上或断开：过一会儿重新加入一次（换一份新令牌）再连；连续失败几次后退出
    case gc_media_relay_failed:
    case gc_media_relay_lost:
        if (m_relayRetries >= kMaxRelayRetries)
        {
            LOG_ERROR("groupcall: relay unavailable after %d retries, leave call, room=%s.", m_relayRetries,
                      m_roomId.c_str());
            leaveCall(true);
            showError(TR(L"与服务器的媒体连接已断开，已退出群通话。"));
            break;
        }
        ++m_relayRetries;
        showNotice(TR(L"网络连接不稳定，正在重新连接…"));
        SetTimer(kTimerRelayRetry, kRelayRetryDelayMs, NULL);
        break;

    //其它事件：忽略
    default:
        break;
    }
    return 0;
}

LRESULT CGroupCallController::OnIncomingCommandMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)lParam;
    (void)bHandled;
    if (m_phase != phase_incoming)
        return 0;
    if (wParam == gc_incoming_accept)
        acceptInvite();
    else
        declineInvite();
    return 0;
}

LRESULT CGroupCallController::OnWindowCommandMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)lParam;
    (void)bHandled;
    if (m_phase != phase_in_call && wParam != gc_window_hangup)
        return 0;
    switch (wParam)
    {
    //麦克风开关
    case gc_window_toggle_mic:
        //全员静音期间没有管理权的人不能打开（按钮已置灰，这里再拦一次，免得白发一次请求）
        if (!m_mediaSession.IsMicEnabled() && IsGroupCallMicLocked(m_state.m_allMuted, canManage()))
        {
            showNotice(TR(L"已开启全员静音，只有发起人、群主和管理员可以打开麦克风。"));
            break;
        }
        setMic(!m_mediaSession.IsMicEnabled());
        break;

    //摄像头开关
    case gc_window_toggle_camera:
        setCamera(!m_mediaSession.IsCameraEnabled());
        break;

    //邀请成员：请 exe 弹选人框，已在房间里的人不列出
    case gc_window_invite:
    {
        std::vector<int64_t> exclude;
        for (size_t i = 0; i < m_state.m_members.size(); ++i)
            exclude.push_back(m_state.m_members[i].m_userId);
        m_host->RequestGroupCallInvitees(m_groupId, exclude.empty() ? NULL : &exclude[0],
                                         static_cast<uint32_t>(exclude.size()));
        break;
    }

    //挂断或关闭通话窗
    case gc_window_hangup:
        if (m_phase != phase_idle)
            leaveCall(true);
        break;

    //静音某人
    case gc_window_mute_member:
    {
        const int64_t target = m_window.TakePendingTarget();
        if (target > 0)
            send(cmd_groupcall_manage, GroupCallProtocolClient::buildManage(m_roomId, gc_manage_mute, target));
        break;
    }

    //移出某人
    case gc_window_remove_member:
    {
        const int64_t target = m_window.TakePendingTarget();
        if (target > 0)
            send(cmd_groupcall_manage, GroupCallProtocolClient::buildManage(m_roomId, gc_manage_remove, target));
        break;
    }

    //勾选「全员静音」：开启
    case gc_window_mute_all:
        send(cmd_groupcall_manage, GroupCallProtocolClient::buildManage(m_roomId, gc_manage_mute_all, 0));
        break;

    //取消勾选「全员静音」：解除（不替任何人打开麦克风）
    case gc_window_unmute_all:
        send(cmd_groupcall_manage, GroupCallProtocolClient::buildManage(m_roomId, gc_manage_unmute_all, 0));
        break;

    //未知命令
    default:
        break;
    }
    return 0;
}

LRESULT CGroupCallController::OnDeferredMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)wParam;
    (void)lParam;
    (void)bHandled;
    return 0;
}

LRESULT CGroupCallController::OnTimer(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)lParam;
    switch (wParam)
    {
    //画面刷新
    case kTimerFrames:
        m_window.RefreshFrames();
        break;

    //说话人、时长、订阅、回铃
    case kTimerSlow:
        onSlowTick();
        break;

    //中转断开后重新加入一次，服务端回新的 1746 后重启媒体会话
    case kTimerRelayRetry:
        KillTimer(kTimerRelayRetry);
        if (m_phase == phase_in_call)
        {
            LOG_INFO("groupcall: rejoin to refresh relay token, attempt=%d, room=%s.", m_relayRetries,
                     m_roomId.c_str());
            sendJoin(m_mediaSession.IsMicEnabled(), m_mediaSession.IsCameraEnabled());
        }
        break;

    //别的定时器：交给默认处理
    default:
        bHandled = FALSE;
        break;
    }
    return 0;
}

//======================= 内部流程 =======================

bool CGroupCallController::send(uint16_t cmd, const std::string& body)
{
    if (m_host == NULL)
        return false;
    const bool ok = m_host->SendSignal(cmd, body.data(), static_cast<uint32_t>(body.size()));
    if (!ok)
        LOG_WARNING("groupcall: send cmd %u failed.", static_cast<unsigned>(cmd));
    return ok;
}

bool CGroupCallController::sendJoin(bool mic, bool camera)
{
    return send(cmd_groupcall_join, GroupCallProtocolClient::buildJoin(m_roomId, mic, camera));
}

void CGroupCallController::acceptInvite()
{
    balloonmain::StopLoopSound();
    m_incoming.Hide();
    m_wantMic = true;
    m_wantCamera = (m_media == gc_media_video);
    if (!sendJoin(m_wantMic, m_wantCamera))
    {
        resetLocal();
        showError(TR(L"网络未连接，无法加入群通话。"));
        return;
    }
    m_phase = phase_joining;
    LOG_INFO("groupcall: invite accepted, room=%s.", m_roomId.c_str());
}

void CGroupCallController::declineInvite()
{
    send(cmd_groupcall_decline, GroupCallProtocolClient::buildRoomRequest(m_roomId));
    LOG_INFO("groupcall: invite declined, room=%s.", m_roomId.c_str());
    resetLocal();
}

void CGroupCallController::leaveCall(bool notifyServer)
{
    if (notifyServer && !m_roomId.empty())
        send(cmd_groupcall_leave, GroupCallProtocolClient::buildRoomRequest(m_roomId));
    LOG_INFO("groupcall: left, room=%s, notify=%d.", m_roomId.c_str(), notifyServer ? 1 : 0);
    resetLocal();
}

void CGroupCallController::OnAvatarsUpdated()
{
    m_incoming.RefreshAvatar();
    m_window.RefreshAvatars();
}

void CGroupCallController::syncPresence()
{
    if (m_host == NULL)
        return;
    const int64_t group = (m_phase == phase_in_call && m_groupId > 0) ? m_groupId : 0;
    //媒体类型以房间快照为准（从横幅加入时本地记的是语音，快照到了才知道是群视频）
    const int media = (!m_state.m_roomId.empty() ? m_state.m_media : m_media) == gc_media_video
                      ? flamingomedia::kMediaPresenceVideo : flamingomedia::kMediaPresenceVoice;
    if (group == m_presenceGroup && (group == 0 || media == m_presenceMedia))
        return;
    if (m_presenceGroup != 0 && group != m_presenceGroup)
    {
        LOG_INFO("groupcall: presence ended, group=%lld.", static_cast<long long>(m_presenceGroup));
        m_host->OnCallPresence(flamingomedia::media_presence_group, m_presenceGroup, 0, false);
    }
    if (group != 0)
    {
        LOG_INFO("groupcall: presence in call, group=%lld, media=%d.", static_cast<long long>(group), media);
        m_host->OnCallPresence(flamingomedia::media_presence_group, group, media, true);
    }
    m_presenceGroup = group;
    m_presenceMedia = media;
}

void CGroupCallController::resetLocal()
{
    if (IsWindow())
    {
        KillTimer(kTimerFrames);
        KillTimer(kTimerSlow);
        KillTimer(kTimerRelayRetry);
    }
    if (m_ringBack || m_phase == phase_incoming)
        balloonmain::StopLoopSound();
    m_ringBack = false;
    m_mediaSession.Stop();
    m_incoming.Hide();
    m_window.Close();
    m_phase = phase_idle;
    m_groupId = 0;
    m_groupName.clear();
    m_roomId.clear();
    m_inviterId = 0;
    m_state = GroupCallState();
    m_relayRetries = 0;
    m_speakingSeats.clear();
    //本人已离开：主面板这个群的「通话中」徽章随之去掉
    syncPresence();
    m_notice.clear();
    m_noticeUntil = 0;
    m_cachedRole = kRoleUnknown;
}

void CGroupCallController::startMedia(const GroupCallRelayInfo& relay)
{
    //服务端在摄像头满时会把本人按关闭加入：以快照为准，免得打开后再被拒
    const GroupCallMember* self = selfMember();
    if (self != NULL && m_wantCamera && !self->m_camera && m_phase == phase_waiting)
    {
        m_wantCamera = false;
        showNotice(TR(L"同时开摄像头的人数已满，暂时不能打开摄像头。"));
    }
    //在全员静音期间加入（来电的快照里已带 allmuted）：服务端已按关麦克风记下，本地也不打开，免得先发出一段声音再被关掉。
    //从横幅直接加入时此刻还没有快照，由随后的 1748 应答（728）或 1745 快照关掉
    if (m_wantMic && m_phase == phase_waiting && IsGroupCallMicLocked(m_state.m_allMuted, canManage()))
    {
        m_wantMic = false;
        showNotice(TR(L"已开启全员静音，你的麦克风已关闭。"));
    }
    const bool firstStart = (m_phase == phase_waiting);
    m_mediaSession.Start(relay, m_host->GetServerPublicKey(), m_wantMic, m_wantCamera, m_hWnd, ++m_mediaToken,
                         deviceMode());
    //设备打不开时按实际状态改
    if (m_wantCamera && !m_mediaSession.IsCameraEnabled())
    {
        m_wantCamera = false;
        showNotice(TR(L"摄像头打不开，可能被别的程序占用。"));
    }
    m_phase = phase_in_call;
    //进入通话：主面板这个群显示「通话中」
    syncPresence();
    if (firstStart)
    {
        GroupCallWindowView view;
        view.m_title = MakeGroupCallTitle(m_groupName, m_media);
        m_window.Open(mainWindow(), m_hWnd, this, view);
        SetTimer(kTimerFrames, kFramesIntervalMs, NULL);
        SetTimer(kTimerSlow, kSlowIntervalMs, NULL);
    }
    reportMedia();
    refreshView();
    LOG_INFO("groupcall: media started, room=%s, seat=%d, first=%d.", m_roomId.c_str(), relay.m_seat,
             firstStart ? 1 : 0);
}

void CGroupCallController::refreshView()
{
    if (m_phase != phase_in_call)
        return;
    //快照到了媒体类型才准：徽章的图标随之改为电话或摄像机
    syncPresence();
    const int64_t selfId = m_host->GetSelfUserId();
    GroupCallWindowView view;
    view.m_title = MakeGroupCallTitle(m_groupName, m_media);
    view.m_micOn = m_mediaSession.IsMicEnabled();
    view.m_cameraOn = m_mediaSession.IsCameraEnabled();
    BuildGroupCallTiles(m_state, selfId, view.m_cameraOn, view.m_micOn, kGroupCallMaxGridTiles, view.m_grid,
                        view.m_strip);
    for (size_t i = 0; i < m_state.m_members.size(); ++i)
        view.m_names[m_state.m_members[i].m_userId] = displayName(m_state.m_members[i].m_userId);
    view.m_speakingSeats = m_speakingSeats;
    view.m_canManage = canManage();
    view.m_allMuted = m_state.m_allMuted;
    view.m_micLocked = IsGroupCallMicLocked(m_state.m_allMuted, view.m_canManage);
    view.m_joinedCount = m_state.joinedCount();

    //状态行：临时提示优先；其次全员静音锁住本人麦克风时写「已开启全员静音」，后面接正在说话的其他人
    const bool noticeActive = !m_notice.empty() && nowTick() < m_noticeUntil;
    std::vector<int64_t> talkers;
    for (size_t i = 0; i < m_state.m_members.size(); ++i)
    {
        const GroupCallMember& m = m_state.m_members[i];
        if (m.m_userId != selfId && m.m_state == gc_member_joined &&
            m_speakingSeats.find(m.m_seat) != m_speakingSeats.end())
            talkers.push_back(m.m_userId);
    }
    view.m_status = MakeGroupCallStatusLine(noticeActive ? m_notice : std::wstring(), view.m_micLocked,
                                            talkers.empty() ? std::wstring() : namesOf(talkers));
    m_window.Update(view);

    //订阅（第 5 期）：只收宫格里看得见的画面；窗口最小化时一路都不收
    std::vector<int> seats;
    if (!m_window.IsMinimizedNow())
        seats = SubscribedCameraSeats(view.m_grid);
    m_mediaSession.Subscribe(seats);
    //摄像头码率（第 5 期）：按房间里开着摄像头的人数降低本人这一路的码率，控制每个人收到的画面总码率
    m_mediaSession.SetCameraBitrate(GroupCallCameraBitrateFor(CountGroupCallCameras(view.m_grid, view.m_strip)));
    syncRingBack();
}

void CGroupCallController::onSlowTick()
{
    if (m_phase != phase_in_call)
        return;
    const std::vector<int> speaking = m_mediaSession.UpdateSpeaking(nowTick());
    m_speakingSeats = std::set<int>(speaking.begin(), speaking.end());
    if (!m_notice.empty() && nowTick() >= m_noticeUntil)
        m_notice.clear();
    m_window.SetDuration(FormatGroupCallDuration(nowTick() - m_callStartTick));
    refreshView();
}

void CGroupCallController::reportMedia()
{
    if (m_phase != phase_in_call || m_roomId.empty())
        return;
    const bool mic = m_mediaSession.IsMicEnabled();
    const bool camera = m_mediaSession.IsCameraEnabled();
    const GroupCallMember* self = selfMember();
    //快照里已经一致时不报
    if (self != NULL && self->m_mic == mic && self->m_camera == camera)
        return;
    send(cmd_groupcall_member_media, GroupCallProtocolClient::buildMemberMedia(m_roomId, mic, camera));
}

void CGroupCallController::setMic(bool on)
{
    if (!m_mediaSession.SetMicEnabled(on) && on)
        showNotice(TR(L"麦克风打不开，请检查设备。"));
    m_wantMic = m_mediaSession.IsMicEnabled();
    reportMedia();
    refreshView();
}

void CGroupCallController::setCamera(bool on)
{
    if (!m_mediaSession.SetCameraEnabled(on) && on)
        showNotice(TR(L"摄像头打不开，可能被别的程序占用。"));
    m_wantCamera = m_mediaSession.IsCameraEnabled();
    reportMedia();
    refreshView();
}

void CGroupCallController::reportStatus(int64_t groupId, const std::string& roomId, bool active, int media, int count,
                                        int64_t initiatorId, int64_t startTime)
{
    if (m_host == NULL || groupId <= 0)
        return;
    m_host->OnGroupCallStatus(groupId, roomId.c_str(), active, media, count, initiatorId, startTime);
}

void CGroupCallController::showNotice(const std::wstring& text)
{
    m_notice = text;
    m_noticeUntil = nowTick() + kNoticeMs;
    refreshView();
}

void CGroupCallController::showError(const std::wstring& text)
{
    ::MessageBoxW(mainWindow(), text.c_str(), TR(L"群通话"), MB_OK | MB_ICONINFORMATION);
}

std::wstring CGroupCallController::errorText(int code) const
{
    switch (code)
    {
    //通话已结束
    case error_code_groupcall_not_exist:
        return TR(L"群通话已经结束。");
    //本人正忙
    case error_code_groupcall_busy:
        return TR(L"你正在通话或屏幕共享中，请先结束后再试。");
    //人数已满
    case error_code_groupcall_room_full:
        return TR(L"群通话的人数已满。");
    //不是群成员或不在通话里
    case error_code_groupcall_not_member:
        return TR(L"你不是这个群的成员，或已不在这场群通话里。");
    //摄像头已满
    case error_code_groupcall_camera_limit:
        return TR(L"同时开摄像头的人数已满，暂时不能打开摄像头。");
    //企业关闭了群通话
    case error_code_groupcall_disabled:
        return TR(L"管理员已关闭群通话功能。");
    //没有管理权限
    case error_code_groupcall_no_permission:
        return TR(L"只有发起人、群主和管理员可以这样操作。");
    //其余错误
    default:
        return TRF(L"操作失败（错误码 {0}）。", code);
    }
}

std::wstring CGroupCallController::displayName(int64_t userId)
{
    std::map<int64_t, std::wstring>::const_iterator it = m_nameCache.find(userId);
    if (it != m_nameCache.end())
        return it->second;
    wchar_t buf[kNameBufChars] = { 0 };
    std::wstring name;
    if (m_host != NULL && m_host->GetUserDisplayName(userId, buf, kNameBufChars))
        name = buf;
    if (name.empty())
        return TR(L"成员");
    m_nameCache[userId] = name;
    return name;
}

std::wstring CGroupCallController::namesOf(const std::vector<int64_t>& userIds)
{
    std::wstring text;
    for (size_t i = 0; i < userIds.size() && i < kMaxNamesInNotice; ++i)
    {
        if (i > 0)
            text += TR(L"、");
        text += displayName(userIds[i]);
    }
    if (userIds.size() > kMaxNamesInNotice)
        text = TRF(L"{0} 等 {1} 人", text, static_cast<int>(userIds.size()));
    return text;
}

const GroupCallMember* CGroupCallController::selfMember() const
{
    if (m_host == NULL)
        return NULL;
    return m_state.findMember(m_host->GetSelfUserId());
}

bool CGroupCallController::canManage()
{
    const int64_t selfId = m_host->GetSelfUserId();
    if (selfId == m_state.m_initiatorId)
        return true;
    if (m_cachedRole == kRoleUnknown)
        m_cachedRole = m_host->GetGroupMemberRole(m_groupId, selfId);
    return m_cachedRole == kRoleAdmin || m_cachedRole == kRoleOwner;
}

bool CGroupCallController::shouldRingBack() const
{
    //只有自己一个人已加入、还有人在振铃：放回铃，让发起人知道正在呼叫
    if (m_phase != phase_in_call || m_state.joinedCount() != 1)
        return false;
    for (size_t i = 0; i < m_state.m_members.size(); ++i)
    {
        if (m_state.m_members[i].m_state == gc_member_ringing)
            return true;
    }
    return false;
}

void CGroupCallController::syncRingBack()
{
    const bool want = shouldRingBack();
    if (want == m_ringBack)
        return;
    m_ringBack = want;
    if (want)
        balloonmain::StartLoopSound(balloonmain::kNotifySoundCallOutgoing, m_host->IsSoundMuted());
    else
        balloonmain::StopLoopSound();
}

HWND CGroupCallController::mainWindow() const
{
    return m_host != NULL ? m_host->GetMainWindow() : NULL;
}

int CGroupCallController::deviceMode()
{
#ifdef _DEBUG
    wchar_t buf[kEnvBufChars] = { 0 };
    const DWORD n = ::GetEnvironmentVariableW(kNoDevicesEnv, buf, kEnvBufChars);
    if (n == 0 || n >= kEnvBufChars)
        return gc_devices_all;
    if (buf[0] == L'1')
        return gc_devices_none;
    if (buf[0] == L'2')
        return gc_devices_no_audio;
    return gc_devices_all;
#else
    return gc_devices_all;
#endif
}

std::wstring CGroupCallController::utf8ToWide(const std::string& s)
{
    if (s.empty())
        return std::wstring();
    const int n = ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), NULL, 0);
    if (n <= 0)
        return std::wstring();
    std::wstring w(static_cast<size_t>(n), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), &w[0], n);
    return w;
}

} // namespace groupcall
