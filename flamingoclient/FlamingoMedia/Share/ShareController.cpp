/**
 *  屏幕共享控制器，ShareController.cpp
 *  职责与线程约定见 ShareController.h。
 *
 *  balloonwj@qq.com   2026-10-02
 */
#include "stdafx.h"

#include "ShareController.h"

#include "ShareAnnotationRender.h"   // RenderToolCursor（工具光标，第 19 期）
#include "ShareAnnotModePolicy.h"    // DecideAnnotSurfaces：标注模式下的输入面与工具条锚点（2026-10-07）
#include "ShareRecordMutex.h"        // CheckScreenStartConflict：录制与共享互斥（2026-10-07）
#include "ShareLinkInvite.h"         // 分享链接：拼链接、邀请文字、凭链接加入的确认文字（第 25 期）
#include "../../Source/Common/Util/ClipboardText.h"   // CopyTextToClipboard：复制分享链接（第 25 期）
#include "ShareCallPolicy.h"
#include "ShareSources.h"
#include "ShareVideoPolicy.h"
#include "../Call/MfCameraCapture.h"
#include "../Common/OsVersionPolicy.h"
#include "../../Source/net/Protocol.h"
#include "../../Source/Common/Log/IULog.h"
#include "../../Source/Common/I18n/I18n.h"   // TR：界面文字的多语言查找
#include "../Host/MediaLockState.h"          // 锁屏期间共享开始通知静默处理（docs/锁屏方案.md）

#include <string.h>   // strcmp：读诊断开关

namespace screenshare {

namespace {

//边框跟随被共享窗口的定时器：每 200 毫秒一次；id 取较大值避开基类可能占用的小 id
const UINT_PTR kFollowTimerId = 401;
const UINT kFollowTimerMs = 200;
//说话人提示的刷新定时器：每 300 毫秒一次
const UINT_PTR kSpeakTimerId = 402;
const UINT kSpeakTimerMs = 300;
//共享时长的刷新定时器（第 13 期），每秒一次
const UINT_PTR kDurationTimerId = 403;
const UINT kDurationTimerMs = 1000;
//凭分享链接加入的确认小窗（2026-10-07）：整窗宽（像素）、一行大约放得下的中文字数、提问区至少与至多几行
const int kLinkJoinDialogW = 420;
const int kLinkJoinCharsPerLine = 22;
const int kLinkJoinMinLines = 3;
const int kLinkJoinMaxLines = 12;
//标注临时内容的刷新定时器（第 19 期）：激光笔淡出约 20 帧每秒，顺带清掉过期的画笔位置
const UINT_PTR kAnnotTickTimerId = 404;
const UINT kAnnotTickTimerMs = 50;
//GetAsyncKeyState 返回值里「此刻按着」的那一位
const SHORT kKeyDownBit = static_cast<SHORT>(0x8000);

#ifdef _DEBUG
//调试版「房间语音不用声音设备」开关的环境变量名（第 13 期）：值为 1 时房间语音不打开麦克风与扬声器，麦克风开关照常上报。
//用于在开发机上联调按人静音等功能，不碰真实的麦克风、不从扬声器放声音（与通话的 FLAMINGO_CALL_NO_AUDIO 同一做法）
const char* const kShareNoAudioEnv = "FLAMINGO_SHARE_NO_AUDIO";

//是否打开了「房间语音不用声音设备」开关
bool useShareNoAudioDevice()
{
    char value[8] = { 0 };
    size_t len = 0;
    if (getenv_s(&len, value, sizeof(value), kShareNoAudioEnv) != 0 || len == 0)
        return false;
    return value[0] == '1';
}
#endif
//临时提示（如「发起人已将全员静音」）的显示时长，单位毫秒
const int64_t kNoticeMs = 4000;
//显示名缓冲区的容量，单位字符
const uint32_t kNameChars = 128;
//两次补发笔迹的最小间隔，单位毫秒（慢观看者丢帧也会触发关键帧请求，补发不必每次都做）
const int64_t kSnapshotIntervalMs = 1000;

//中转接收线程交给 UI 线程的一帧标注
struct ShareAnnotationFrame
{
    int         m_seat;      //发送者座位号
    std::string m_payload;   //标注载荷
};
//消息框标题
const wchar_t* const kCaption = TR_NOOP(L"屏幕共享");

//通话中发起共享时选择窗顶部的提示
const wchar_t* const kInCallPickerHint = TR_NOOP(L"通话中共享屏幕，双方的声音继续经由通话传输。");

//诊断开关（P2P 方案第 6 期）：默认登记屏幕共享的 P2P 业务方（单聊共享优先走 P2P）；环境变量 FLAMINGO_SHARE_P2P 为 0 时
//不登记，单聊共享照旧全部走房间中转，服务端也不会为它建 P2P 会话
const char* const kShareP2PEnvName = "FLAMINGO_SHARE_P2P";
const char* const kShareP2PEnvOff = "0";

//读环境变量用的缓冲（字符数）：开关值只有一个字符
const DWORD kShareP2PEnvValueChars = 8;

} // namespace

CShareController::ToastInfo::ToastInfo()
    : m_ctype(0)
    , m_cid(0)
    , m_token(0)
    , m_forced(false)
{
}

CShareController::CShareController()
    : m_host(NULL)
    , m_hostPhase(host_idle)
    , m_hostCtype(0)
    , m_hostCid(0)
    , m_hostLinkNoPassword(false)
    , m_pendingLinkMenu(0)
    , m_hostForced(false)
    , m_hostRecordAllowed(true)
    , m_hostPaused(false)
    , m_mustRepick(false)
    , m_viewerPhase(viewer_idle)
    , m_viewerCtype(0)
    , m_viewerCid(0)
    , m_nextToastToken(1)
    , m_hostMicInitial(false)
    , m_hostSysAudioInitial(false)
    , m_removeTarget(0)
    , m_selfSeat(0)
    , m_noticeUntilMs(0)
    , m_annotOverlay(&m_annot)
    , m_annotMode(false)
    , m_annotPointer(false)
    , m_annotHoverTol(0)
    , m_annotHoverValid(false)
    , m_lastSnapshotMs(0)
    , m_recorder(NULL)
    , m_hostRecordSource(&m_streamer)
    , m_viewerRecordSource(&m_viewer)
    , m_speakerCamera(&m_cameraSender, &m_cameraReceiver)
    , m_recordMixer(NULL)
    , m_shareRecording(false)
    , m_recordAllowed(true)
    , m_pipSeat(-1)
    , m_callPeerId(0)
    , m_callObserver(NULL)
    , m_p2pBroker(kShareP2PPurpose)
    , m_controllerId(0)
    , m_revokingId(0)
    , m_injectedCount(0)
    , m_droppedCount(0)
    , m_inputQueueTotalUs(0)
    , m_inputQueueMaxUs(0)
    , m_inputQueueFrames(0)
    , m_viewerControl(share_viewer_control_none)
    , m_annotCursor(NULL)
    , m_annotCursorTool(0)
    , m_annotCursorColor(0)
    , m_publishPhase(share_publish_idle)
    , m_hostScreenStopped(false)
    , m_controlOwner(0)
    , m_publishOverlay(&m_annot)
    , m_annotOwnScreen(false)
    , m_textCanvas(kAnnotDefaultCanvas)
    , m_recordCanvas(kAnnotDefaultCanvas)
    , m_boardPhase(share_viewer_mine_none)
    , m_boardCloseOwner(0)
    , m_myBoardSeat(-1)
    , m_annotOnBoard(false)
    , m_annotOnScreens(false)
    , m_boardViewDirty(false)
    , m_boardViewSentMs(0)
    , m_boardRequests(share_kind_whiteboard)
    , m_screenBoard(false)
    , m_publishScreenBoard(false)
    , m_saveToastToken(0)
    , m_saveCtype(0)
    , m_saveCid(0)
{
}

CShareController::~CShareController()
{
    Shutdown();
}

CShareController::JoinTarget::JoinTarget()
    : m_ctype(0)
    , m_cid(0)
{
}

bool CShareController::Init(flamingomedia::IMediaHost* host)
{
    if (host == NULL)
        return false;
    m_host = host;
    if (!IsWindow() && Create(HWND_MESSAGE) == NULL)
    {
        LOG_ERROR("screenshare: create controller message window failed.");
        return false;
    }
    registerP2P();
    //实时字幕（语音字幕）：消息窗口与字幕可用状态的观察者
    m_caption.Init(this);
    LOG_INFO("ShareController: initialized, p2p=%d.", m_p2pBroker.IsRegistered() ? 1 : 0);
    return true;
}

void CShareController::registerP2P()
{
    //诊断开关：环境变量 FLAMINGO_SHARE_P2P 为 0 时不登记，单聊共享照旧全部走房间中转
    char value[kShareP2PEnvValueChars] = { 0 };
    const DWORD n = ::GetEnvironmentVariableA(kShareP2PEnvName, value, kShareP2PEnvValueChars);
    if (n > 0 && n < kShareP2PEnvValueChars && strcmp(value, kShareP2PEnvOff) == 0)
    {
        LOG_INFO("screenshare: p2p is off (%s is 0), single-chat shares use the room relay.", kShareP2PEnvName);
        return;
    }
    p2p::IP2PService* service = m_host->GetP2PService();
    if (service == NULL)
    {
        LOG_INFO("screenshare: p2p service not available, single-chat shares use the room relay.");
        return;
    }
    m_p2pBroker.Register(service);
}

void CShareController::syncHostP2PViewers(const ShareStateNotify& n)
{
    //发起人一侧：快照里带 p2psessionid 的观看者，认领链路槽交给推流管线；已离开房间的，放弃他的会话号。
    //房间里不止一名观看者（第 25 期凭分享链接加入的第三人）时一律不用 P2P：P2P 打开后发起人只经 P2P 发，
    //其他观看者经中转收不到任何帧，所以此时放弃全部 P2P 观看者、回到房间中转
    std::map<int, std::string> current;
    //云端录制中（第 27b 期）也不用 P2P：帧要经过房间中转才能被 imgserver 录下来
    const bool p2pAllowed = ShareHostP2PAllowed(n.m_members.size(), n.m_cloudRecordId != 0);
    for (size_t i = 0; i < n.m_members.size() && p2pAllowed; ++i)
    {
        const ShareMember& m = n.m_members[i];
        if (m.m_seat == 0 || m.m_p2pSessionId.empty())
            continue;
        current[m.m_seat] = ShareP2PFeatureId(n.m_roomId, m.m_seat);
    }
    if (!p2pAllowed && !m_hostP2PViewers.empty())
        LOG_INFO("screenshare: %u members in the room, cloud record %lld, p2p viewers go back to the room relay.",
                 static_cast<unsigned>(n.m_members.size()), static_cast<long long>(n.m_cloudRecordId));
    for (std::map<int, std::string>::iterator it = m_hostP2PViewers.begin(); it != m_hostP2PViewers.end();)
    {
        std::map<int, std::string>::iterator now = current.find(it->first);
        if (now != current.end() && now->second == it->second)
        {
            ++it;
            continue;
        }
        m_streamer.ClearP2PViewer(it->first);
        m_p2pBroker.Abandon(it->second);
        LOG_INFO("screenshare: p2p viewer %s left.", it->second.c_str());
        it = m_hostP2PViewers.erase(it);
    }
    if (!m_p2pBroker.IsRegistered())
        return;
    for (std::map<int, std::string>::iterator it = current.begin(); it != current.end(); ++it)
    {
        if (m_hostP2PViewers.count(it->first) != 0)
            continue;
        m_streamer.SetP2PViewer(it->first, m_p2pBroker.ClaimSlot(it->second));
        m_hostP2PViewers[it->first] = it->second;
        LOG_INFO("screenshare: p2p viewer %s registered with the streamer.", it->second.c_str());
    }
}

void CShareController::releaseHostP2PViewers()
{
    for (std::map<int, std::string>::iterator it = m_hostP2PViewers.begin(); it != m_hostP2PViewers.end(); ++it)
    {
        m_streamer.ClearP2PViewer(it->first);
        m_p2pBroker.Abandon(it->second);
    }
    m_hostP2PViewers.clear();
}

void CShareController::Shutdown()
{
    ResetForLogout();
    //实时字幕：会话已随语音结束，这里销毁浮层与消息窗口
    m_caption.Shutdown();
    //推流与观看都已收尾：注销用途（P2P 模块在本模块之后才卸载）
    m_p2pBroker.Unregister();
    //轻量提示（2026-10-07）：窗口过程在本 DLL 里，DLL 卸载之前必须销毁
    m_noticeToast.Destroy();
    if (IsWindow())
        DestroyWindow();
    m_host = NULL;
    //工具光标（第 19 期）
    if (m_annotCursor != NULL)
    {
        ::DestroyIcon(m_annotCursor);
        m_annotCursor = NULL;
    }
}

bool CShareController::IsMicrophoneInUse() const
{
    return m_audio.IsRunning() && m_audio.IsMicEnabled();
}

bool CShareController::IsShareSignal(uint16_t cmd)
{
    //1720 ~ 1739（第 22 期多人共享延到 1739），分享链接的 1752、1753（第 25 期，1740 ~ 1751 归群通话），
    //以及共享中云端录制的 1754（第 27b 期）；1755 ~ 1757 归云端录像中心（CCloudRecordCenter）
    return (cmd >= cmd_screenshare_start && cmd <= cmd_screenshare_transfer_host) ||
           (cmd >= cmd_screenshare_link && cmd <= cmd_screenshare_link_resolve) || cmd == cmd_cloudrecord_control;
}

bool CShareController::OnSignal(uint16_t cmd, const std::string& json)
{
    if (m_host == NULL || !IsShareSignal(cmd))
        return false;
    switch (cmd)
    {
    //发起共享的应答
    case cmd_screenshare_start:
        onStartResult(json);
        break;
    //加入共享的应答
    case cmd_screenshare_join:
        onJoinResult(json);
        break;
    //中转参数：发起人开始推流，观看者打开观看窗
    case cmd_screenshare_relay_info:
        onRelayInfo(json);
        break;
    //会话里有人开始共享
    case cmd_screenshare_started_notify:
        onStartedNotify(json);
        break;
    //房间状态变化
    case cmd_screenshare_state_notify:
        onStateNotify(json);
        break;
    //共享已结束
    case cmd_screenshare_ended_notify:
        onEndedNotify(json);
        break;
    //查询应答
    case cmd_screenshare_query:
        onQueryResult(json);
        break;
    //成员媒体（麦克风、摄像头、电脑声音）的应答：开摄像头超过上限时要关掉本地摄像头
    case cmd_screenshare_member_media:
        onMemberMediaResult(json);
        break;
    //开始 / 停止录制的应答：没有录制权限时停掉本地的录制
    case cmd_screenshare_record_state:
        onRecordStateResult(json);
        break;
    //移出观看者的应答（第 14 期）
    case cmd_screenshare_remove_member:
        onRemoveMemberResult(json);
        break;
    //修改标题与描述的应答（第 15 期）
    case cmd_screenshare_set_info:
        onSetInfoResult(json);
        break;
    //标题与描述变了的推送（第 15 期）
    case cmd_screenshare_info_notify:
        onInfoNotify(json);
        break;
    //控制键鼠的应答（第 18 期）
    case cmd_screenshare_control:
        onControlResult(json);
        break;
    //控制键鼠的推送（第 18 期）：申请、撤回、拒绝
    case cmd_screenshare_control_notify:
        onControlNotify(json);
        break;
    //多人共享（第 22 期）：申请、撤回、同意、拒绝、停止共享的应答
    case cmd_screenshare_sharing:
        onSharingResult(json);
        break;
    //多人共享的推送（第 22 期）：主持人收到申请或撤回，申请人收到同意或拒绝，共享者收到被停止
    case cmd_screenshare_sharing_notify:
        onSharingNotify(json);
        break;
    //移交主持人的应答（第 22 期）
    case cmd_screenshare_transfer_host:
        onTransferHostResult(json);
        break;
    //分享链接的应答（第 25 期）：取得、重置、停用、设置免输密码
    case cmd_screenshare_link:
        onLinkResult(json);
        break;
    //凭链接码查共享概要的应答（第 25 期）
    case cmd_screenshare_link_resolve:
        onLinkResolveResult(json);
        break;
    //共享中云端录制的应答（第 27b 期）
    case cmd_cloudrecord_control:
        onCloudRecordResult(json);
        break;
    //离开、结束、设置权限、录制状态的应答：只记日志（失败时房间状态以推送为准）
    default:
        {
            ShareResult r;
            if (ShareProtocolClient::parseResult(json, r) && r.m_code != 0)
                LOG_WARNING("screenshare: cmd %u failed, code=%d, msg=%s.", static_cast<unsigned>(cmd), r.m_code,
                            r.m_msg.c_str());
        }
        break;
    }
    return true;
}

bool CShareController::StartShare(int ctype, int64_t cid, const std::wstring& conversationName)
{
    uint32_t major = 0;
    uint32_t minor = 0;
    uint32_t build = 0;
    if (!flamingomedia::GetOsVersion(major, minor, build)
        || !flamingomedia::IsScreenCaptureSupportedBuild(major, minor, build))
    {
        showError(TR(L"当前系统版本不支持屏幕共享，需要 Windows 10 2004 或更高版本。"));
        return false;
    }
    //自己正在共享、正在观看别人的共享、独立录制进行中（含录制选择窗、框选区域）都不能发起共享。聊天窗、主面板、托盘、
    //通话窗等入口都走到这里。被拦下时用轻量提示（红色 toast），显示在发起入口所在的窗口里（2026-10-07 起，原先前两种弹消息框）
    const bool recordBusy = (m_recorder != NULL && m_recorder->IsStandaloneBusy());
    const int conflict = CheckShareStartConflict(m_hostPhase != host_idle, m_viewerPhase != viewer_idle, recordBusy);
    if (conflict != screen_start_ok)
    {
        LOG_INFO("screenshare: start rejected, conflict=%d (host phase %d, viewer phase %d, record busy %d).", conflict,
                 static_cast<int>(m_hostPhase), static_cast<int>(m_viewerPhase), recordBusy ? 1 : 0);
        m_noticeToast.ShowNearActiveWindow(ScreenStartConflictText(conflict), flamingomedia::media_toast_error);
        return false;
    }
    m_hostPhase = host_picking;
    m_hostCtype = ctype;
    m_hostCid = cid;
    m_hostConvName = conversationName;
    m_hostRoomId.clear();
    m_hostPaused = false;
    m_mustRepick = false;
    openPicker(false, inCallShare() ? std::wstring(TR(kInCallPickerHint)) : std::wstring());
    LOG_INFO("screenshare: start requested, ctype=%d, cid=%lld, in call=%d.", ctype, static_cast<long long>(cid),
             inCallShare() ? 1 : 0);
    notifyCallObserver();
    return true;
}

bool CShareController::JoinShare(int ctype, int64_t cid, const std::string& roomId, const std::wstring& hostName,
                                 bool forced)
{
    if (roomId.empty())
        return false;
    //强制模式（第 13 期）：已在看这一场时不再确认；否则先告知加入后要等发起人结束才能离开
    const bool watchingThis = (m_viewerPhase != viewer_idle && m_viewerRoomId == roomId);
    if (!watchingThis && ShouldConfirmForcedJoin(forced, false))
    {
        //第 15 期起写上共享标题与描述（不知道时只写原来的说明）
        std::wstring title;
        std::wstring desc;
        m_roomInfo.Find(roomId, title, desc);
        const int answer = ::MessageBoxW(mainWindow(), MakeForcedJoinConfirmText(title, desc).c_str(), TR(kCaption),
                                         MB_YESNO | MB_ICONQUESTION);
        if (answer != IDYES)
        {
            LOG_INFO("screenshare: forced join canceled by user, room=%s.", roomId.c_str());
            return false;
        }
    }
    //这一场正在等输入密码：把密码框提到前面，不重复发加入请求
    if (!m_pwdJoin.m_roomId.empty() && m_pwdJoin.m_roomId == roomId && m_pwdDialog.IsWindow())
    {
        ::SetForegroundWindow(m_pwdDialog.m_hWnd);
        return true;
    }
    //另一场的密码框：放弃那一次加入
    closePasswordDialog();
    //先不带密码加入：没设密码的共享、或本人免校验（如通话对方）时直接成功；需要密码时服务端回 717 再弹密码框
    return joinRoom(ctype, cid, roomId, hostName, std::string());
}

bool CShareController::joinRoom(int ctype, int64_t cid, const std::string& roomId, const std::wstring& hostName,
                                const std::string& password, const std::string& linkCode)
{
    if (m_viewerPhase != viewer_idle && m_viewerRoomId == roomId)
    {
        //已经在看这一场：把观看窗提到前面（最小化了就先还原）
        if (m_viewer.IsWindow())
        {
            m_viewer.ShowWindow(m_viewer.IsIconic() ? SW_RESTORE : SW_SHOW);
            ::SetForegroundWindow(m_viewer.m_hWnd);
        }
        return true;
    }
    if (m_hostPhase != host_idle || m_viewerPhase != viewer_idle)
    {
        showError(m_hostPhase != host_idle ? TR(L"你正在共享屏幕，不能同时观看别人的共享。")
                                           : TR(L"你正在观看另一个屏幕共享，请先离开后再加入。"));
        return false;
    }
    if (!send(cmd_screenshare_join, ShareProtocolClient::buildJoin(roomId, password, linkCode)))
        return false;
    m_viewerPhase = viewer_joining;
    m_lastState = ShareStateNotify();
    m_viewerCtype = ctype;
    m_viewerCid = cid;
    m_viewerRoomId = roomId;
    m_viewerHostName = hostName;
    m_viewerJoinPassword = password;
    m_viewerJoinLinkCode = linkCode;
    //结束提示的名单从这里开始记录（加入失败时这一场不会有结束推送发给本人，残留的房间号不影响）
    m_participantLog.Reset(roomId, inCallShare());
    if (m_toast.Token() != 0 && m_toastInfo.m_roomId == roomId)
        m_toast.Hide();
    //日志只记是否带了密码，不记密码本身
    LOG_INFO("screenshare: join requested, room=%s, with password %d.", roomId.c_str(), password.empty() ? 0 : 1);
    return true;
}

void CShareController::askJoinPassword(const JoinTarget& target, bool retry)
{
    m_pwdJoin = target;
    ShareTextInputSpec spec;
    spec.m_title = TR(L"输入共享密码");
    if (retry)
    {
        spec.m_hint = TR(L"密码不对，请重新输入");
        spec.m_hintIsError = true;
    }
    else
    {
        CString hint;
        hint.Format(TR(L"%s 的屏幕共享设了密码"), target.m_hostName.empty() ? TR(L"对方") : target.m_hostName.c_str());
        spec.m_hint = hint.GetString();
    }
    spec.m_placeholder = TR(L"请输入 6 位数字密码");
    spec.m_invalidHint = TR(L"密码是 6 位数字，请重新输入");
    spec.m_maxChars = static_cast<int>(kSharePasswordLength);
    spec.m_validator = IsValidSharePassword;
    spec.m_resultMsg = kMsgSharePasswordResult;
    //不置顶：发起人可能要切到别的程序里查看密码；在任务栏上显示按钮，被盖住后能找回
    spec.m_topmost = false;
    spec.m_inTaskbar = true;
    m_pwdDialog.OpenCentered(mainWindow(), m_hWnd, spec);
    LOG_INFO("screenshare: room %s needs a password, asking (retry %d).", target.m_roomId.c_str(), retry ? 1 : 0);
}

void CShareController::closePasswordDialog()
{
    m_pwdDialog.Close();
    m_pwdJoin = JoinTarget();
}

LRESULT CShareController::OnPasswordResultMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)wParam;
    (void)bHandled;
    std::unique_ptr<std::wstring> text(reinterpret_cast<std::wstring*>(lParam));
    const JoinTarget target = m_pwdJoin;
    m_pwdJoin = JoinTarget();
    if (!text || target.m_roomId.empty())
    {
        LOG_INFO("screenshare: join password input canceled.");
        return 0;
    }
    joinRoom(target.m_ctype, target.m_cid, target.m_roomId, target.m_hostName, SharePasswordToUtf8(*text),
             target.m_linkCode);
    return 0;
}

//==================== 分享链接（第 25 期） ====================

bool CShareController::JoinShareByLink(const std::wstring& url)
{
    std::string code;
    if (!ParseShareLinkUrl(url, code))
    {
        LOG_WARNING("screenshare: share link rejected, bad format.");
        showError(TR(L"这个分享链接无效。"));
        return false;
    }
    if (m_hostPhase != host_idle)
    {
        showError(TR(L"你正在共享屏幕，不能同时观看别人的共享。"));
        return false;
    }
    if (!send(cmd_screenshare_link_resolve, ShareProtocolClient::buildLinkResolve(code)))
        return false;
    m_pendingLinkCode = code;
    //日志不记链接码本身：持有链接码即可加入
    LOG_INFO("screenshare: resolving a share link.");
    return true;
}

void CShareController::onLinkResolveResult(const std::string& json)
{
    const std::string code = m_pendingLinkCode;
    m_pendingLinkCode.clear();
    ShareLinkSummary s;
    if (code.empty() || !ShareProtocolClient::parseLinkSummary(json, s))
        return;
    LOG_INFO("screenshare: share link resolved, code=%d, status=%d, member=%d.", s.m_code, s.m_status,
             s.m_member ? 1 : 0);
    switch (s.m_code)
    {
    //查到了：下面接着处理
    case 0:
        break;
    //链接无效、已被重置或停用、共享结束已超过保留期
    case error_code_screenshare_link_invalid:
        showError(TR(L"这个分享链接无效或已经过期。"));
        return;
    //与发起人不在同一企业
    case error_code_screenshare_link_enterprise:
        showError(TR(L"这个屏幕共享只对发起人所在企业的同事开放。"));
        return;
    //解析太频繁
    case error_code_screenshare_link_too_frequent:
        showError(TR(L"操作太频繁，请稍后再试。"));
        return;
    //其余错误
    default:
        showError(TR(L"无法打开这个分享链接，请稍后再试。"));
        return;
    }
    if (s.m_status != share_link_status_active || s.m_roomId.empty())
    {
        showError(TR(L"这个屏幕共享已经结束了。"));
        return;
    }
    const std::wstring hostName = utf8ToWide(s.m_hostName);
    //已经在看这一场：不再确认，直接把观看窗提到前面（joinRoom 里处理）
    const bool watchingThis = (m_viewerPhase != viewer_idle && m_viewerRoomId == s.m_roomId);
    if (watchingThis)
    {
        closePasswordDialog();
        joinRoom(s.m_ctype, s.m_cid, s.m_roomId, hostName, std::string(), code);
        return;
    }
    //确认小窗（2026-10-07，方案第 14.5 节，替换原来的系统消息框）：标题栏程序图标，写发起人、标题、描述与密码、强制模式的说明，
    //按钮「取消」「加入」；结果经 kMsgShareLinkJoinResult 回来，在 OnLinkJoinResultMsg 里加入
    m_linkJoin = JoinTarget();
    //会话成员按原来的会话加入；链接访客没有对应的会话，ctype / cid 为 0
    m_linkJoin.m_ctype = s.m_ctype;
    m_linkJoin.m_cid = s.m_cid;
    m_linkJoin.m_roomId = s.m_roomId;
    m_linkJoin.m_hostName = hostName;
    m_linkJoin.m_linkCode = code;
    m_linkJoinTitle = utf8ToWide(s.m_title);
    m_linkJoinDesc = utf8ToWide(s.m_desc);
    ShareConfirmSpec spec;
    spec.m_title = TR(L"加入屏幕共享");
    spec.m_prompt = MakeShareLinkJoinConfirmText(hostName, m_linkJoinTitle, m_linkJoinDesc, s.m_locked, s.m_forced);
    spec.m_promptLines = EstimateShareTextLines(spec.m_prompt, kLinkJoinCharsPerLine, kLinkJoinMinLines, kLinkJoinMaxLines);
    spec.m_width = kLinkJoinDialogW;
    spec.m_okText = TR(L"加入");
    spec.m_resultMsg = kMsgShareLinkJoinResult;
    m_linkJoinDlg.OpenCentered(mainWindow(), m_hWnd, spec, false);
    LOG_INFO("screenshare: join by link confirm opened, locked=%d, forced=%d.", s.m_locked ? 1 : 0, s.m_forced ? 1 : 0);
}

LRESULT CShareController::OnLinkJoinResultMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)lParam;
    (void)bHandled;
    const JoinTarget target = m_linkJoin;
    m_linkJoin = JoinTarget();
    if (target.m_roomId.empty())
        return 0;
    if (wParam == 0)
    {
        LOG_INFO("screenshare: join by link canceled by user.");
        return 0;
    }
    //观看窗标题与名单要用到标题与描述
    m_roomInfo.Remember(target.m_roomId, m_linkJoinTitle, m_linkJoinDesc);
    closePasswordDialog();
    joinRoom(target.m_ctype, target.m_cid, target.m_roomId, target.m_hostName, std::string(), target.m_linkCode);
    return 0;
}

std::wstring CShareController::hostInviteText() const
{
    const std::wstring url = MakeShareLinkUrl(m_hostLinkCode);
    //房间有密码、且没有打开「点链接加入免输密码」时，邀请文字里附上密码
    std::wstring password;
    if (!m_hostPassword.empty() && !m_hostLinkNoPassword)
        password = utf8ToWide(m_hostPassword);
    return MakeShareInviteText(m_hostTitle, url, password);
}

void CShareController::onLinkMenu(int action)
{
    if (m_hostPhase == host_idle || m_hostRoomId.empty())
        return;
    //复制、发送邀请：已有链接时直接完成
    if ((action == CShareControlBar::kLinkCopy || action == CShareControlBar::kLinkSendInvite) &&
        !m_hostLinkCode.empty())
    {
        deliverHostLink(action);
        return;
    }
    int request = share_link_get;
    bool noPassword = false;
    switch (action)
    {
    //重置：旧链接作废，生成新链接
    case CShareControlBar::kLinkReset:
        request = share_link_reset;
        break;
    //停用
    case CShareControlBar::kLinkDisable:
        request = share_link_disable;
        break;
    //切换免输密码：设成当前的反面
    case CShareControlBar::kLinkNoPassword:
        request = share_link_set_nopassword;
        noPassword = !m_hostLinkNoPassword;
        break;
    //复制、发送邀请：还没有链接，先取得
    default:
        request = share_link_get;
        break;
    }
    if (!send(cmd_screenshare_link, ShareProtocolClient::buildLink(m_hostRoomId, request, noPassword)))
    {
        //没发出去（网络断开等）：用轻量提示告诉主持人，不弹模态框（2026-10-07）
        showBarToast(TR(L"分享链接的操作没有成功，请稍后再试。"), true);
        return;
    }
    m_pendingLinkMenu = action;
    LOG_INFO("screenshare: share link request sent, action=%d.", action);
}

void CShareController::onCloudRecordResult(const std::string& json)
{
    ShareCloudRecordResult r;
    if (!ShareProtocolClient::parseCloudRecordResult(json, r))
        return;
    LOG_INFO("screenshare: cloud record result code %d, action %d, record %lld, room=%s.", r.m_code, r.m_action,
             static_cast<long long>(r.m_recordId), r.m_roomId.c_str());
    if (r.m_code != 0 && m_hostPhase == host_streaming && r.m_roomId == m_hostRoomId)
        showError(MakeCloudRecordErrorText(r.m_code, r.m_action).c_str());
}

void CShareController::applyCloudRecordState(const ShareStateNotify& n, bool isHost)
{
    const bool recording = n.m_cloudRecordId != 0;
    if (isHost)
    {
        m_bar.SetCloudRecordState(m_host != NULL && m_host->IsCloudRecordEnabled(), recording, n.m_cloudRecordPaused);
        //imgserver 出错等被动结束：告诉主持人原因，已录部分保留
        if (n.m_event == share_event_cloudrecord_changed && !recording && n.m_cloudRecordCode != 0)
            showError(MakeCloudRecordStoppedText(n.m_cloudRecordCode).c_str());
        return;
    }
    m_viewer.SetCloudRecordState(recording, n.m_cloudRecordPaused);
}

void CShareController::onLinkResult(const std::string& json)
{
    const int action = m_pendingLinkMenu;
    m_pendingLinkMenu = 0;
    ShareLinkResult r;
    if (!ShareProtocolClient::parseLinkResult(json, r))
        return;
    if (r.m_code != 0)
    {
        LOG_WARNING("screenshare: share link request failed, code=%d.", r.m_code);
        //失败也用轻量提示（2026-10-07 起不再弹模态框），红色底比成功提示醒目
        showBarToast(TR(L"分享链接的操作没有成功，请稍后再试。"), true);
        return;
    }
    //应答来时共享已经结束（或换了一场）：不再处理
    if (m_hostPhase == host_idle || r.m_roomId != m_hostRoomId)
        return;
    m_hostLinkCode = r.m_linkCode;
    m_hostLinkNoPassword = r.m_noPassword;
    m_bar.SetLinkState(!m_hostLinkCode.empty(), m_hostLinkNoPassword);
    LOG_INFO("screenshare: share link updated, active=%d, no password=%d.", m_hostLinkCode.empty() ? 0 : 1,
             m_hostLinkNoPassword ? 1 : 0);
    deliverHostLink(action);
}

void CShareController::deliverHostLink(int action)
{
    switch (action)
    {
    //复制分享链接：邀请文字放进剪贴板；结果用控制条下方的轻量提示告诉主持人（2026-10-07 起不再弹模态框）
    case CShareControlBar::kLinkCopy:
        if (m_hostLinkCode.empty())
            break;
        if (flamingo::CopyTextToClipboard(hostInviteText().c_str()))
            showBarToast(TR(L"分享链接已复制，可以粘贴到聊天里发给同事。"), false);
        else
            showBarToast(TR(L"复制分享链接失败，请稍后再试。"), true);
        break;
    //发送邀请：请宿主弹出会话选择窗，选好后以文字消息发出
    case CShareControlBar::kLinkSendInvite:
        if (!m_hostLinkCode.empty() && m_host != NULL)
            m_host->PickConversationsAndSendText(hostInviteText().c_str());
        break;
    //重置：新链接放进剪贴板，并说明旧链接已失效
    case CShareControlBar::kLinkReset:
        if (!m_hostLinkCode.empty())
        {
            const bool copied = flamingo::CopyTextToClipboard(hostInviteText().c_str());
            showBarToast(copied ? TR(L"分享链接已重置，之前发出的链接都已失效。新的链接已复制。")
                                : TR(L"分享链接已重置，之前发出的链接都已失效。"),
                         false);
        }
        break;
    //停用：说明之前发出的链接不能再用
    case CShareControlBar::kLinkDisable:
        showBarToast(TR(L"分享链接已停用，之前发出的链接都不能再用来加入。"), false);
        break;
    //切换免输密码：状态已更新到控制条，不再提示
    default:
        break;
    }
}

void CShareController::QueryShare(int ctype, int64_t cid)
{
    if (send(cmd_screenshare_query, ShareProtocolClient::buildQuery(ctype, cid)))
        m_pendingQueries.push_back(std::make_pair(ctype, cid));
}

void CShareController::SetMouseEffects(const ShareMouseEffectSettings& settings)
{
    m_mouseSettings = settings;
    m_streamer.SetMouseEffects(settings);
    m_bar.SetMouseEffects(settings);
}

void CShareController::SetQuality(int quality)
{
    //推流器记住档位、跨推流保持：两条管线不论是否正在推流都设上，下一次开始推流时直接生效
    m_streamer.SetQuality(quality);
    m_publisher.SetQuality(quality);
    LOG_INFO("screenshare: quality set to %d.", quality);
}

void CShareController::OnScreenLockChanged(bool locked)
{
    if (locked)
    {
        //本人主持的共享：结束（与登出时主持方的收尾相同）；角落提示收起
        if (m_hostPhase == host_waiting || m_hostPhase == host_streaming)
        {
            LOG_INFO("screenshare: screen locked, ending the share hosted by self, room %s.", m_hostRoomId.c_str());
            send(cmd_screenshare_stop, ShareProtocolClient::buildRoomRequest(m_hostRoomId));
            stopHostLocal();
        }
        m_toast.Hide();
        return;
    }
    //解锁：锁屏期间记下的强制模式重新加入，若此刻既没在观看也没在主持，就加入
    if (!m_lockedRejoin.m_roomId.empty())
    {
        if (m_viewerPhase == viewer_idle && m_hostPhase == host_idle)
        {
            LOG_INFO("screenshare: screen unlocked, back to forced room %s.", m_lockedRejoin.m_roomId.c_str());
            joinRoom(m_lockedRejoin.m_ctype, m_lockedRejoin.m_cid, m_lockedRejoin.m_roomId, m_lockedRejoin.m_hostName,
                     std::string());
        }
        m_lockedRejoin.m_roomId.clear();
    }
}

void CShareController::ResetForLogout()
{
    if (m_hostPhase == host_waiting || m_hostPhase == host_streaming)
        send(cmd_screenshare_stop, ShareProtocolClient::buildRoomRequest(m_hostRoomId));
    stopHostLocal();
    if (m_viewerPhase != viewer_idle)
        send(cmd_screenshare_leave, ShareProtocolClient::buildRoomRequest(m_viewerRoomId));
    stopViewerLocal();
    m_toast.Hide();
    m_endedToast.Hide();
    m_participantLog.Reset(std::string(), false);
    closePasswordDialog();
    //凭链接加入的确认小窗（2026-10-07）：登出后不再加入
    m_linkJoinDlg.Close();
    m_linkJoin = JoinTarget();
    m_pendingQueries.clear();
    m_nameCache.clear();
    m_roomInfo.Clear();
}

//==================== 信令 ====================

void CShareController::onStartResult(const std::string& json)
{
    if (m_hostPhase != host_starting)
        return;
    ShareResult r;
    if (!ShareProtocolClient::parseResult(json, r))
        return;
    if (r.m_code != 0)
    {
        LOG_WARNING("screenshare: start rejected, code=%d.", r.m_code);
        stopHostLocal();
        switch (r.m_code)
        {
        //该会话已有人在共享
        case error_code_screenshare_exists:
            showError(TR(L"这个会话里已经有人在共享屏幕了。"));
            break;
        //正在一对一通话中：只能在与通话对方的单聊里共享（第 8 期）
        case error_code_screenshare_in_call:
            showError(TR(L"正在通话中，只能在与通话对方的聊天里共享屏幕。"));
            break;
        //已在另一个共享中
        case error_code_screenshare_in_other_room:
            showError(TR(L"你已经在另一个屏幕共享中了。"));
            break;
        //不是群成员
        case error_code_screenshare_not_member:
            showError(TR(L"你不在这个群里，不能共享屏幕。"));
            break;
        //共享标题或描述包含敏感词（第 15 期）
        case error_code_screenshare_sensitive:
            showError(TR(L"共享标题或描述包含敏感词，请修改后重新发起。"));
            break;
        //其余错误
        default:
            showError(TR(L"发起屏幕共享失败，请稍后再试。"));
            break;
        }
        return;
    }
    m_hostRoomId = r.m_roomId;
    m_hostPhase = host_waiting;
    //结束提示的名单从这里开始记录。服务端发起成功时不给发起人推 1721 与 1725，没人加入的共享一条推送都收不到：
    //本人在这里记为发起人，本人填的标题与描述也在这里记进房间信息，否则结束提示的分享人一行为空、也不写标题
    //（2026-10-15 实机检查发现，用例 [shareended] SSET7、SSET8）。之后收到的 1725 与修改标题的结果照常覆盖房间信息
    m_participantLog.Reset(m_hostRoomId, inCallShare());
    m_participantLog.SetInitiator(m_host->GetSelfUserId());
    m_roomInfo.Remember(m_hostRoomId, m_hostTitle, m_hostDesc);
    LOG_INFO("screenshare: started, room=%s, waiting for relay info.", m_hostRoomId.c_str());
    reportStatus(m_hostCtype, m_hostCid, m_hostRoomId, m_host->GetSelfUserId(), std::wstring(), true,
                 !m_hostPassword.empty(), m_hostForced, m_hostTitle);
}

void CShareController::onJoinResult(const std::string& json)
{
    if (m_viewerPhase != viewer_joining)
        return;
    ShareResult r;
    if (!ShareProtocolClient::parseResult(json, r) || r.m_code == 0)
        return;
    LOG_WARNING("screenshare: join rejected, room=%s, code=%d.", m_viewerRoomId.c_str(), r.m_code);
    JoinTarget target;
    target.m_roomId = m_viewerRoomId;
    target.m_ctype = m_viewerCtype;
    target.m_cid = m_viewerCid;
    target.m_hostName = m_viewerHostName;
    target.m_linkCode = m_viewerJoinLinkCode;
    const bool triedPassword = !m_viewerJoinPassword.empty();
    stopViewerLocal();
    switch (r.m_code)
    {
    //凭分享链接加入（第 25 期）：链接在查概要之后失效（被重置、停用或共享结束）
    case error_code_screenshare_link_invalid:
        showError(TR(L"这个分享链接已经失效。"));
        break;
    //凭分享链接加入（第 25 期）：与发起人不在同一企业
    case error_code_screenshare_link_enterprise:
        showError(TR(L"这个屏幕共享只对发起人所在企业的同事开放。"));
        break;
    //房间已不存在：共享已经结束
    case error_code_screenshare_not_exist:
        reportStatus(target.m_ctype, target.m_cid, std::string(), 0, std::wstring(), false, false);
        showError(TR(L"屏幕共享已经结束了。"));
        break;
    //共享设了密码（第 12 期）：弹出密码框，不限次数重试；带了密码仍被拒说明密码不对
    case error_code_screenshare_bad_password:
        askJoinPassword(target, triedPassword);
        break;
    //人数已满
    case error_code_screenshare_room_full:
        showError(TR(L"观看人数已满，暂时无法加入。"));
        break;
    //已被发起人移出且不允许再次加入（第 14 期）
    case error_code_screenshare_removed:
        showError(TR(L"你已被发起人移出这场共享，不能再加入。"));
        break;
    //正在通话中：只能观看通话对方的共享（第 8 期）
    case error_code_screenshare_in_call:
        showError(TR(L"正在通话中，只能观看通话对方的屏幕共享。"));
        break;
    //已在另一个共享中
    case error_code_screenshare_in_other_room:
        showError(TR(L"你已经在另一个屏幕共享中了。"));
        break;
    //没有资格
    case error_code_screenshare_not_member:
        showError(TR(L"你不能加入这个屏幕共享。"));
        break;
    //其余错误
    default:
        showError(TR(L"加入屏幕共享失败，请稍后再试。"));
        break;
    }
}

void CShareController::onRelayInfo(const std::string& json)
{
    ShareRelayInfo info;
    if (!ShareProtocolClient::parseRelayInfo(json, info))
        return;
    const int action = DecideShareRelayInfoAction(info.m_roomId, m_hostPhase != host_idle, m_hostRoomId,
                                                  m_hostPhase == host_waiting, m_streamer.IsRunning(),
                                                  m_viewerPhase == viewer_joining, m_viewerPhase == viewer_watching,
                                                  m_viewerRoomId);
    //发起人：开始推流（宽限期内重新登录时服务端会再下发一次，推流还在跑就不用管）
    if (action == share_relay_info_start_streaming)
    {
        startStreaming(info);
        return;
    }
    if (action == share_relay_info_host_running)
        return;
    //观看中又收到 1726（2026-10-07，方案第 14.3 节第 3 条）：经移交成为主持人的人掉线后在宽限期内重新登录，服务端按
    //主持人回来处理并签了新令牌。观看窗用新的中转参数重连，语音、标注与摄像头仍经同一个收流管线收发，不用重建
    if (action == share_relay_info_reconnect_viewer)
    {
        LOG_INFO("screenshare: relay info again for watching room %s (seat %d), reconnecting the viewer.",
                 info.m_roomId.c_str(), info.m_seat);
        m_durationClock.Start(info.m_elapsedMs, ::GetTickCount64());
        if (!m_viewer.Reconnect(info, m_host->GetServerPublicKey()))
            LOG_WARNING("screenshare: viewer reconnect failed, room=%s.", info.m_roomId.c_str());
        return;
    }
    //观看者：打开观看窗
    if (action == share_relay_info_open_viewer)
    {
        m_viewerPhase = viewer_watching;
        //共享时长（第 13 期）：从 1726 的 elapsedms 开始计时
        m_durationClock.Start(info.m_elapsedMs, ::GetTickCount64());
        SetTimer(kDurationTimerId, kDurationTimerMs, NULL);
        //观看者的麦克风、摄像头默认关闭，需要时自己打开
        m_viewer.SetMicOn(false);
        m_viewer.SetCameraOn(false);
        m_viewer.SetSpeakingText(std::wstring());
        //摄像头接收先于收流管线启动，房间里已开着的摄像头一到就能解码
        m_cameraSeats.clear();
        m_cameraReceiver.Start();
        m_viewer.SetCameraEndpoints(&m_cameraSender, &m_cameraReceiver);
        m_viewer.SetCameraSource(this);
        //标注会话先于收流管线开始，发起人补发的笔迹一到就能接住
        m_annot.Start(m_viewer.AudioTransport(), info.m_seat, static_cast<uint32_t>(::GetTickCount()));
        //沿用工具条上次的选择（工具、颜色与各项样式）
        ApplyAnnotBarState(m_annotState, m_annot);
        SetTimer(kAnnotTickTimerId, kAnnotTickTimerMs, NULL);
        m_annotMode = false;
        m_viewer.SetAnnotation(&m_annot, this, this);
        //控制键鼠（第 18 期）：观看窗在控制中把键鼠交给本类
        m_viewerControl = share_viewer_control_none;
        m_viewer.SetControlInput(this);
        //共享标题与描述（第 15 期）：从 1721 / 1727 / 1725 记下的缓存里取，不知道时为空，标题栏显示「某某 的屏幕共享」
        std::wstring shareTitle;
        std::wstring shareDesc;
        m_roomInfo.Find(info.m_roomId, shareTitle, shareDesc);
        m_viewer.SetShareInfo(shareTitle, shareDesc);
        //单聊共享里服务端为本人与发起人建了 P2P 会话：认领链路槽交给收流管线，由它先等链路、不成再连房间中转
        std::shared_ptr<balloonp2p::P2PLinkSlot> p2pSlot;
        if (!info.m_p2pSessionId.empty() && m_p2pBroker.IsRegistered())
        {
            m_viewerP2PFeature = ShareP2PFeatureId(info.m_roomId, info.m_seat);
            p2pSlot = m_p2pBroker.ClaimSlot(m_viewerP2PFeature);
        }
        m_viewer.Open(info, m_host->GetServerPublicKey(), m_viewerHostName, m_hWnd, mainWindow(), p2pSlot);
        m_viewer.SetAudioSession(&m_audio);
        startAudio(m_viewer.AudioTransport(), info.m_seat, false);
        refreshCameraTiles();
        //加入中先到的 1725（第 13 期）：人数、强制模式、成员状态现在补上
        if (m_lastState.m_roomId == info.m_roomId)
        {
            const int viewers = static_cast<int>(m_lastState.m_members.size()) - 1;
            m_viewer.SetHostOnline(m_lastState.m_hostOnline);
            m_viewer.SetViewerCount(viewers > 0 ? viewers : 0);
            m_viewer.SetForced(m_lastState.m_forced);
            applyMembers(m_lastState, false);
            applyMultiState(m_lastState, false);
            applyControlState(m_lastState, false);
            LOG_INFO("screenshare: applied early state of room %s, forced %d.", info.m_roomId.c_str(),
                     m_lastState.m_forced ? 1 : 0);
        }
        LOG_INFO("screenshare: watching room %s, seat %d.", info.m_roomId.c_str(), info.m_seat);
        return;
    }
    LOG_WARNING("screenshare: relay info for unknown room %s ignored.", info.m_roomId.c_str());
}

void CShareController::onStartedNotify(const std::string& json)
{
    ShareStartedNotify n;
    if (!ShareProtocolClient::parseStartedNotify(json, n))
        return;
    const std::wstring hostName = utf8ToWide(n.m_hostName);
    //记下这场共享的标题与描述（第 15 期），加入前确认、观看窗据此显示
    m_roomInfo.Remember(n.m_roomId, utf8ToWide(n.m_title), utf8ToWide(n.m_desc));
    reportStatus(n.m_ctype, n.m_cid, n.m_roomId, n.m_hostId, hostName, true, n.m_locked, n.m_forced,
                 utf8ToWide(n.m_title));
    //锁屏期间（docs/锁屏方案.md 第 4.9 节）：状态照常上报（聊天窗里的共享卡片照常更新），但不弹提示、不自动加入；
    //强制模式下的重新加入记下来，解锁时再加入
    if (flamingomedia::IsMediaScreenLocked())
    {
        if (n.m_rejoin)
        {
            m_lockedRejoin.m_roomId = n.m_roomId;
            m_lockedRejoin.m_ctype = n.m_ctype;
            m_lockedRejoin.m_cid = n.m_cid;
            m_lockedRejoin.m_hostName = hostName.empty() ? displayName(n.m_hostId) : hostName;
        }
        LOG_INFO("screenshare: started notify while the screen is locked, room %s, rejoin %d, kept silent.",
                 n.m_roomId.c_str(), n.m_rejoin ? 1 : 0);
        return;
    }
    //强制模式下掉线后重新登录（第 13 期）：服务端让本人回到观看，直接加入，不弹提示、不确认（服务端免校验密码）
    if (n.m_rejoin)
    {
        if (m_viewerPhase == viewer_idle && m_hostPhase == host_idle)
        {
            LOG_INFO("screenshare: back to forced room %s after re-login.", n.m_roomId.c_str());
            joinRoom(n.m_ctype, n.m_cid, n.m_roomId, hostName.empty() ? displayName(n.m_hostId) : hostName,
                     std::string());
        }
        return;
    }
    //通话中：通话对方发起的共享不弹提示，直接加入观看（第 8 期）
    if (ShouldAutoJoinInCall(n.m_ctype, n.m_hostId, m_callPeerId,
                             m_viewerPhase == viewer_idle && m_hostPhase == host_idle))
    {
        LOG_INFO("screenshare: call peer %lld started sharing, join room %s directly.",
                 static_cast<long long>(n.m_hostId), n.m_roomId.c_str());
        JoinShare(n.m_ctype, n.m_cid, n.m_roomId, hostName);
        return;
    }
    //单聊：屏幕右下角弹可点击的提示，不振铃
    if (n.m_ctype == conversation_single && m_viewerPhase == viewer_idle && m_hostPhase == host_idle)
    {
        m_toastInfo.m_roomId = n.m_roomId;
        m_toastInfo.m_ctype = n.m_ctype;
        m_toastInfo.m_cid = n.m_cid;
        m_toastInfo.m_hostName = hostName;
        m_toastInfo.m_forced = n.m_forced;
        m_toastInfo.m_token = m_nextToastToken++;
        //设了加入密码时标出「需要密码」，强制模式写明效果（第 12、14 期）；第 15 期起写共享标题，有描述时另起一行写描述，
        //提示窗按 ShareToastTextLines 算出的行数加高
        const std::wstring desc = utf8ToWide(n.m_desc);
        std::wstring text = MakeShareStartedToastText(hostName, utf8ToWide(n.m_title), n.m_locked, n.m_forced);
        if (!desc.empty())
            text += L"\n" + desc;
        m_toast.Show(text, m_toastInfo.m_token, m_hWnd, ShareToastTextLines(n.m_forced, !desc.empty()));
    }
}

void CShareController::onStateNotify(const std::string& json)
{
    ShareStateNotify n;
    if (!ShareProtocolClient::parseStateNotify(json, n))
        return;
    //当前的共享标题与描述（第 15 期）
    m_roomInfo.Remember(n.m_roomId, utf8ToWide(n.m_title), utf8ToWide(n.m_desc));
    //结束提示的名单（房间号不是正在记录的那一场时内部忽略）
    m_participantLog.Observe(n);
    if (m_hostPhase == host_streaming && n.m_roomId == m_hostRoomId)
    {
        const int viewers = static_cast<int>(n.m_members.size()) - 1;
        m_bar.SetViewerCount(viewers > 0 ? viewers : 0);
        m_lastState = n;
        syncHostP2PViewers(n);
        applyCloudRecordState(n, true);
        applyMembers(n, true);
        applyMultiState(n, true);
        applyControlState(n, true);
        refreshViewerList();
        return;
    }
    //加入中（还没收到 1726、观看窗还没打开）：服务端广播的 1725 可能先于本人的应答到达，先记下，打开观看窗后再套用；
    //否则人数、强制模式要等到下一次 1725 才生效（第 13 期联调发现）
    if (m_viewerPhase == viewer_joining && n.m_roomId == m_viewerRoomId)
    {
        m_lastState = n;
        return;
    }
    if (m_viewerPhase == viewer_watching && n.m_roomId == m_viewerRoomId)
    {
        m_viewer.SetHostOnline(n.m_hostOnline);
        //观看人数与强制模式（第 13 期）
        const int viewers = static_cast<int>(n.m_members.size()) - 1;
        m_viewer.SetViewerCount(viewers > 0 ? viewers : 0);
        //共享标题与描述（第 15 期）：取缓存而不直接用 n 里的值，老服务端的 1725 不带标题时保留先前记下的
        std::wstring shareTitle;
        std::wstring shareDesc;
        m_roomInfo.Find(n.m_roomId, shareTitle, shareDesc);
        m_viewer.SetShareInfo(shareTitle, shareDesc);
        m_lastState = n;
        //强制模式只约束观看者：经移交成为主持人的人不再被锁在全屏里（2026-10-07，方案第 14.3 节第 2 条）
        m_viewer.SetForced(n.m_forced && !isRoomHost());
        applyCloudRecordState(n, false);
        applyMembers(n, false);
        applyMultiState(n, false);
        applyControlState(n, false);
        refreshHostTools();
        refreshViewerList();
    }
}

void CShareController::refreshHostTools()
{
    if (m_viewerPhase != viewer_watching || m_host == NULL)
        return;
    const bool transferred = IsTransferredShareHost(m_hostPhase == host_streaming, true, m_viewerRoomId,
                                                    m_lastState.m_roomId, m_lastState.m_hostId,
                                                    m_host->GetSelfUserId());
    const bool changed = (transferred != m_viewer.HasHostTools());
    m_viewer.SetHostTools(transferred, m_lastState.m_recordAllowed, m_lastState.m_annotateAllowed);
    if (!changed)
        return;
    LOG_INFO("screenshare: viewer-side host tools %s, room=%s.", transferred ? "on" : "off", m_viewerRoomId.c_str());
    //标注工具条开着：按新的身份重建，主持人多出「清空」与权限按钮
    if (m_annotMode && m_annotBar.IsWindow())
        m_annotBar.Show(annotAnchorRect(), m_hWnd, transferred || m_hostPhase == host_streaming);
    //名单开着：按新的身份重开，主持人多出静音与移出
    if (m_viewerList.IsShown())
    {
        m_viewerList.Hide();
        toggleViewerList();
    }
}

void CShareController::setBarCapture(bool allowed)
{
    SetBarCaptureAllowed(allowed);
    //当前显示着、默认不允许采集的各条：控制条、共享者控制条，以及主持人模式或浮在本人共享内容上的标注工具条
    if (m_bar.IsWindow())
        ApplyBarCaptureState(m_bar.m_hWnd);
    if (m_sharerBar.IsWindow())
        ApplyBarCaptureState(m_sharerBar.m_hWnd);
    if (m_annotBar.IsWindow() && (m_annotBar.IsHostMode() || m_annotOwnScreen))
        ApplyBarCaptureState(m_annotBar.m_hWnd);
    //看画面的窗口也跟随这个开关（2026-10-12）
    applyViewCaptureState();
}

void CShareController::applyViewCaptureState()
{
    const bool allowed = IsBarCaptureAllowed();
    //观看窗：本人经共享者推流管线共享自己的屏幕时（原主持人没有观看窗）
    m_viewer.SetCaptureExcluded(ShouldExcludeShareView(m_publishPhase == share_publish_streaming, allowed));
    //「共享画面」窗口：原主持人推流中、且没有停掉自己的画面时
    m_screensWindow.SetCaptureExcluded(
        ShouldExcludeShareView(m_hostPhase == host_streaming && !m_hostScreenStopped, allowed));
}

void CShareController::onHostMenu(int item)
{
    if (!isRoomHost())
        return;
    const std::string& roomId = currentRoomId();
    switch (item)
    {
    //修改标题与描述：打开修改小窗，填好当前的标题与描述（经移交成为主持人的人取房间缓存里的）
    case share_host_menu_edit_info:
    {
        std::wstring title = m_hostTitle;
        std::wstring desc = m_hostDesc;
        if (m_hostPhase != host_streaming)
            m_roomInfo.Find(roomId, title, desc);
        m_infoEdit.Open(hostToolsWindow(), m_hWnd, title, desc, std::wstring());
        break;
    }
    //全员静音
    case share_host_menu_mute_all:
        muteAllViewers();
        break;
    //允许 / 禁止他人录制：设成当前的反面，结果以 1725 的广播为准
    case share_host_menu_record_perm:
        send(cmd_screenshare_set_permission,
             ShareProtocolClient::buildSetPermission(roomId, share_perm_unchanged,
                                                     m_lastState.m_recordAllowed ? share_perm_revoke : share_perm_allow,
                                                     false));
        LOG_INFO("screenshare: record permission -> %d by host menu, room=%s.", m_lastState.m_recordAllowed ? 0 : 1,
                 roomId.c_str());
        break;
    //允许 / 禁止他人标注：同上
    case share_host_menu_annot_perm:
        send(cmd_screenshare_set_permission,
             ShareProtocolClient::buildSetPermission(roomId,
                                                     m_lastState.m_annotateAllowed ? share_perm_revoke : share_perm_allow,
                                                     share_perm_unchanged, false));
        LOG_INFO("screenshare: annotate permission -> %d by host menu, room=%s.", m_lastState.m_annotateAllowed ? 0 : 1,
                 roomId.c_str());
        break;
    //允许截图（2026-10-08）：本机的设置，设成当前的反面
    case share_host_menu_allow_capture:
        setBarCapture(!IsBarCaptureAllowed());
        break;
    //未知菜单项：忽略
    default:
        break;
    }
}

HWND CShareController::hostToolsWindow()
{
    if (m_hostPhase == host_streaming && m_bar.IsWindow())
        return m_bar.m_hWnd;
    if (m_viewerPhase == viewer_watching && m_viewer.IsWindow())
        return m_viewer.m_hWnd;
    return mainWindow();
}

void CShareController::onEndedNotify(const std::string& json)
{
    ShareEndedNotify n;
    if (!ShareProtocolClient::parseEndedNotify(json, n))
        return;
    //本人被发起人移出（第 14 期，原因 3 / 4）：共享仍在进行，只关掉本人的观看窗并弹出提示；不把会话标成「没有共享」，
    //卡片与横幅照常显示，允许再次加入时还能点
    if (IsShareRemovedReason(n.m_reason))
    {
        if (m_viewerPhase != viewer_idle && n.m_roomId == m_viewerRoomId)
        {
            //stopViewerLocal 会清空发起人名，先记下
            const std::wstring hostName = m_viewerHostName;
            LOG_INFO("screenshare: removed from room %s by host (reason %d).", n.m_roomId.c_str(), n.m_reason);
            stopViewerLocal();
            showError(MakeShareRemovedNotice(hostName, n.m_reason == share_end_removed).c_str());
        }
        return;
    }
    reportStatus(n.m_ctype, n.m_cid, std::string(), 0, std::wstring(), false, false);
    //结束提示要写共享标题：先取出来再忘掉
    std::wstring endedTitle;
    std::wstring endedDesc;
    m_roomInfo.Find(n.m_roomId, endedTitle, endedDesc);
    m_roomInfo.Forget(n.m_roomId);
    if (m_toast.Token() != 0 && m_toastInfo.m_roomId == n.m_roomId)
        m_toast.Hide();
    //正在为这一场输入密码：共享已经结束，关掉密码框
    if (!m_pwdJoin.m_roomId.empty() && m_pwdJoin.m_roomId == n.m_roomId)
        closePasswordDialog();
    //是不是本人参与过、还在记录名单的那一场（主动离开、移交、出错后已清空，不再弹提示）
    const bool logged = !n.m_roomId.empty() && n.m_roomId == m_participantLog.RoomId();
    if (m_hostPhase != host_idle && n.m_roomId == m_hostRoomId)
    {
        LOG_INFO("screenshare: room %s ended (reason %d) while hosting.", n.m_roomId.c_str(), n.m_reason);
        stopHostLocal();
        //通话里的共享不弹（通话窗还在）
        if (logged && !m_participantLog.InCall())
            showEndedToast(n, endedTitle);
        return;
    }
    if (m_viewerPhase != viewer_idle && n.m_roomId == m_viewerRoomId)
    {
        LOG_INFO("screenshare: room %s ended (reason %d) while watching.", n.m_roomId.c_str(), n.m_reason);
        //通话中的共享是自动加入的，对方正常结束时只关观看窗、不弹提示（通话窗还在，看得出共享已结束）
        const bool prompt = ShouldPromptShareEnded(inCallShare(), n.m_reason == share_end_host_timeout);
        stopViewerLocal();
        if (prompt)
            showEndedToast(n, endedTitle);
        else
            m_participantLog.Reset(std::string(), false);
        return;
    }
    //发起人本人点了结束：本地已先回到空闲，服务端随后推来的结束推送在这里弹提示
    if (logged && !m_participantLog.InCall())
        showEndedToast(n, endedTitle);
}

void CShareController::showEndedToast(const ShareEndedNotify& n, const std::wstring& shareTitle)
{
    //参与者最多写几个名字，再多写「等 N 人」
    const int kEndedMaxNames = 8;
    const std::vector<int64_t> sharerIds = m_participantLog.Sharers();
    const std::vector<int64_t> participantIds = m_participantLog.Participants();
    //名单里的本人写「你」（2026-10-05 用户要求）
    const int64_t selfId = (m_host != NULL) ? m_host->GetSelfUserId() : 0;
    std::vector<std::wstring> sharers;
    for (size_t i = 0; i < sharerIds.size(); ++i)
        sharers.push_back(ShareEndedNameOf(sharerIds[i], selfId, displayName(sharerIds[i])));
    std::vector<std::wstring> participants;
    for (size_t i = 0; i < participantIds.size(); ++i)
        participants.push_back(ShareEndedNameOf(participantIds[i], selfId, displayName(participantIds[i])));
    const ShareEndedTexts texts = FormatShareEndedTexts(n.m_durationSeconds, sharers, participants, kEndedMaxNames);
    const std::wstring headline = (n.m_reason == share_end_host_timeout) ? TR(L"对方网络中断，屏幕共享已结束。")
                                                                          : TR(L"屏幕共享已结束。");
    m_endedToast.Show(headline, shareTitle, texts);
    LOG_INFO("screenshare: ended toast for room %s, %lld sec, %u sharer(s), %u participant(s).", n.m_roomId.c_str(),
             static_cast<long long>(n.m_durationSeconds), static_cast<unsigned>(sharerIds.size()),
             static_cast<unsigned>(participantIds.size()));
    m_participantLog.Reset(std::string(), false);
}

void CShareController::onQueryResult(const std::string& json)
{
    if (m_pendingQueries.empty())
        return;
    const std::pair<int, int64_t> query = m_pendingQueries.front();
    m_pendingQueries.pop_front();
    ShareQueryResult r;
    if (!ShareProtocolClient::parseQueryResult(json, r) || r.m_code != 0)
        return;
    if (r.m_exists)
        m_roomInfo.Remember(r.m_roomId, utf8ToWide(r.m_title), utf8ToWide(r.m_desc));
    reportStatus(query.first, query.second, r.m_roomId, r.m_hostId, std::wstring(), r.m_exists, r.m_locked,
                 r.m_forced, utf8ToWide(r.m_title));
}

void CShareController::onMemberMediaResult(const std::string& json)
{
    ShareResult r;
    if (!ShareProtocolClient::parseResult(json, r) || r.m_code == 0)
        return;
    LOG_WARNING("screenshare: member media rejected, code=%d, msg=%s.", r.m_code, r.m_msg.c_str());
    //同时开摄像头的人数已满：服务端没有接受这次变更，关掉本地摄像头后把麦克风等状态重新报一次
    if (r.m_code == error_code_screenshare_camera_limit && m_cameraSender.IsRunning())
    {
        m_cameraSender.Stop();
        applyCameraButton();
        reportMedia();
        refreshCameraTiles();
        //上限是服务端配置项（screensharemaxcameras），客户端不知道具体人数，提示里不写数字
        showError(TR(L"房间里同时开摄像头的人数已达上限，暂时不能打开摄像头。"));
    }
}

//==================== 窗口与推流事件 ====================

void CShareController::OnHostStreamEvent(int event, int value)
{
    //推流线程上：转投 UI 线程
    if (IsWindow())
        PostMessage(kMsgShareHostEvent, static_cast<WPARAM>(event), static_cast<LPARAM>(value));
}

LRESULT CShareController::OnHostEventMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)bHandled;
    if (m_hostPhase != host_streaming)
        return 0;
    switch (static_cast<int>(wParam))
    {
    //连不上房间中转、或推流途中断开：结束共享
    case share_host_relay_failed:
    case share_host_relay_lost:
        send(cmd_screenshare_stop, ShareProtocolClient::buildRoomRequest(m_hostRoomId));
        stopHostLocal();
        //已经弹出连接断开的提示，随后（如果还能收到）的结束推送不再弹结束提示
        m_participantLog.Reset(std::string(), false);
        showError(TR(L"与屏幕共享服务的连接已断开，共享已结束。"));
        break;
    //采集器建不起来：请发起人改选共享内容
    case share_host_capture_failed:
        m_mustRepick = true;
        openPicker(true, TR(L"无法采集所选的内容，请重新选择；取消则结束共享。"));
        break;
    //共享的窗口被关闭：请发起人改选共享内容
    case share_host_source_state:
        if (static_cast<int>(lParam) == screenshare_source_closed)
        {
            m_mustRepick = true;
            openPicker(true, TR(L"共享的窗口已关闭，请重新选择要共享的内容；取消则结束共享。"));
        }
        break;
    //解出了其他共享者画面的新一帧（第 22 期）：刷新「其他人的屏幕」窗口
    case share_host_remote_screen:
        m_screensWindow.OnFeedFrame();
        break;
    //服务端请求画面关键帧（有新成员连上中转）：把已有的笔迹补发一次，限频每秒一次
    case share_host_key_requested:
        {
            const int64_t now = static_cast<int64_t>(::GetTickCount64());
            if (now - m_lastSnapshotMs >= kSnapshotIntervalMs && !m_annot.Model().Strokes().empty())
            {
                m_lastSnapshotMs = now;
                m_annot.SendSnapshot();
                LOG_INFO("screenshare: annotation snapshot sent, %u stroke(s).",
                         static_cast<unsigned>(m_annot.Model().Strokes().size()));
            }
        }
        break;
    //连上了中转等其余事件：不需要处理
    default:
        break;
    }
    return 0;
}

LRESULT CShareController::OnViewerEventMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)lParam;
    (void)bHandled;
    //观看窗连不上或断开了中转：观看窗已显示提示，这里只记日志，由用户关窗离开
    LOG_WARNING("screenshare: viewer relay event %d, room=%s.", static_cast<int>(wParam), m_viewerRoomId.c_str());
    return 0;
}

LRESULT CShareController::OnPickerResultMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)bHandled;
    std::unique_ptr<SharePickerResult> spec(reinterpret_cast<SharePickerResult*>(lParam));
    const bool chosen = (wParam != 0 && spec);
    //多人共享（第 22 期）：主持人同意后本人选共享内容；取消则告诉服务端不共享了
    if (m_publishPhase == share_publish_picking)
    {
        if (!chosen)
        {
            LOG_INFO("screenshare multi: picker cancelled after approval, room=%s.", m_publishRelay.m_roomId.c_str());
            stopPublisherLocal(true);
            return 0;
        }
        m_publishSource = spec->m_source;
        startPublishing();
        return 0;
    }
    //发起前的选择
    if (m_hostPhase == host_picking)
    {
        if (!chosen)
        {
            stopHostLocal();
            return 0;
        }
        m_hostSource = spec->m_source;
        //通话中共享不开房间麦克风（选择窗也不显示这一项）
        m_hostMicInitial = spec->m_mic && !inCallShare();
        m_hostSysAudioInitial = spec->m_systemAudio;
        //加入密码（第 12 期）：通话中共享不提供密码（选择窗也不显示这一项）
        m_hostPassword = inCallShare() ? std::string() : spec->m_password;
        //强制模式与禁止观看者录制（第 13 期）：通话中共享不提供（选择窗也不显示这两项）
        m_hostForced = !inCallShare() && spec->m_forced;
        m_hostRecordAllowed = inCallShare() || !spec->m_recordBlocked;
        //共享标题与描述（第 15 期）：选择窗已规整过
        m_hostTitle = utf8ToWide(spec->m_title);
        m_hostDesc = utf8ToWide(spec->m_desc);
        //为我的发言生成字幕（2026-10-07）：记成偏好；共享开始、房间语音建好时字幕会话按它自动打开，控制条的按钮与「更多」
        //菜单里的勾选项随之同步（applyCaptionState）
        if (spec->m_captionOffered)
        {
            //先直接写偏好：字幕控制器只在开关真正变化时才写，它记着的值可能与偏好不一致（还没开过会话时为默认值）
            caption::ICaptionModule* captionModule = flamingomedia::CMediaCaptionBridge::Instance().Acquire();
            if (captionModule != NULL)
                captionModule->SetPreference(caption::caption_pref_live_self, spec->m_caption);
            m_caption.SetSelfEnabled(spec->m_caption);
            LOG_INFO("screenshare: self captions chosen in picker: %d.", spec->m_caption ? 1 : 0);
        }
        LOG_INFO("screenshare: start with password %d, forced %d, record allowed %d.", m_hostPassword.empty() ? 0 : 1,
                 m_hostForced ? 1 : 0, m_hostRecordAllowed ? 1 : 0);
        if (!send(cmd_screenshare_start,
                  ShareProtocolClient::buildStart(m_hostCtype, m_hostCid, m_hostMicInitial, false,
                                                  m_hostSysAudioInitial, m_hostPassword, m_hostForced,
                                                  m_hostRecordAllowed, spec->m_title, spec->m_desc)))
        {
            stopHostLocal();
            showError(TR(L"网络未连接，无法发起屏幕共享。"));
            return 0;
        }
        m_hostPhase = host_starting;
        return 0;
    }
    //共享过程中的切换
    if (m_hostPhase == host_streaming)
    {
        if (!chosen)
        {
            //共享的窗口已经没了又不重新选：只能结束共享
            if (m_mustRepick)
            {
                send(cmd_screenshare_stop, ShareProtocolClient::buildRoomRequest(m_hostRoomId));
                stopHostLocal();
            }
            return 0;
        }
        m_mustRepick = false;
        //自己改选了共享内容：「屏幕当白板」随之结束（不再恢复原窗口，第 23 期）
        m_screenBoard = false;
        switchHostSource(spec->m_source);
    }
    return 0;
}

LRESULT CShareController::OnBarCommandMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)lParam;
    (void)bHandled;
    if (m_hostPhase != host_streaming)
        return 0;
    switch (static_cast<int>(wParam))
    {
    //收回控制（第 18 期）：控制条上的「收回控制」，有人在控制时才出现
    case CShareControlBar::kCmdRevokeControl:
        revokeControlNow();
        break;
    //结束共享：还有别人在共享时先问移交还是结束（第 22 期第 30 条），没有时直接结束、本地立即收尾
    case CShareControlBar::kCmdStop:
        beginHostExit();
        break;
    //「更多」菜单的「停止我的共享」（第 22 期第 29 条）：只停自己的画面，仍是主持人
    case CShareControlBar::kCmdStopMine:
        if (!m_hostScreenStopped)
        {
            sendSharing(share_sharing_stop, 0);
            setHostScreenStopped(true);
            showNotice(TR(L"已停止你的屏幕共享，你仍是主持人"));
        }
        break;
    //「更多」菜单的「恢复我的共享」：服务端在快照里记为在共享后再恢复推流（人数已满时 1737 应答失败并提示）
    case CShareControlBar::kCmdResumeMine:
        if (m_hostScreenStopped)
            sendSharing(share_sharing_request, 0);
        break;
    //「更多」菜单的「共享画面」（2026-10-12 前叫「其他人的屏幕」）：有别人在共享时重新打开看全部画面的窗口
    case CShareControlBar::kCmdOtherScreens:
        if (m_host != NULL && CountOtherScreens(m_screens, m_host->GetSelfUserId()) > 0)
        {
            m_screensWindow.Open(mainWindow(), &m_streamer, m_hWnd);
            m_screensWindow.SetControlInput(this);
        }
        break;
    //「更多」菜单的「新建白板 / 关闭我的白板」（第 23 期）
    case CShareControlBar::kCmdBoard:
        toggleMyBoard();
        break;
    //「更多」菜单的「屏幕当白板 / 退出屏幕白板」（第 23 期）
    case CShareControlBar::kCmdScreenBoard:
        toggleScreenBoard();
        break;
    //暂停
    case CShareControlBar::kCmdPause:
        LOG_INFO("screenshare: paused by host, room=%s.", m_hostRoomId.c_str());
        m_hostPaused = true;
        m_streamer.SetPaused(true);
        m_bar.SetPaused(true);
        break;
    //继续
    case CShareControlBar::kCmdResume:
        LOG_INFO("screenshare: resumed by host, room=%s.", m_hostRoomId.c_str());
        m_hostPaused = false;
        m_streamer.SetPaused(false);
        m_bar.SetPaused(false);
        break;
    //切换共享内容
    case CShareControlBar::kCmdSwitch:
        openPicker(true, std::wstring());
        break;
    //打开 / 关闭麦克风
    case CShareControlBar::kCmdMicOn:
    case CShareControlBar::kCmdMicOff:
        setMic(static_cast<int>(wParam) == CShareControlBar::kCmdMicOn);
        break;
    //共享 / 停止电脑声音
    case CShareControlBar::kCmdSysAudioOn:
    case CShareControlBar::kCmdSysAudioOff:
        setSystemAudio(static_cast<int>(wParam) == CShareControlBar::kCmdSysAudioOn);
        break;
    //打开 / 关闭摄像头
    case CShareControlBar::kCmdCameraOn:
    case CShareControlBar::kCmdCameraOff:
        setCamera(static_cast<int>(wParam) == CShareControlBar::kCmdCameraOn);
        break;
    //进入或退出标注模式
    case CShareControlBar::kCmdAnnotate:
        setAnnotMode(!m_annotMode);
        break;
    //开始录制
    case CShareControlBar::kCmdRecordStart:
        startShareRecording();
        break;
    //停止录制
    case CShareControlBar::kCmdRecordStop:
        stopShareRecording();
        break;
    //切换他人的录制权限：经 1730 告诉服务端，结果以 1725 的广播为准
    case CShareControlBar::kCmdRecordPermission:
        send(cmd_screenshare_set_permission,
             ShareProtocolClient::buildSetPermission(m_hostRoomId, share_perm_unchanged,
                                                     lParam != 0 ? share_perm_allow : share_perm_revoke, false));
        LOG_INFO("screenshare: record permission -> %d, room=%s.", static_cast<int>(lParam), m_hostRoomId.c_str());
        break;
    //「N 人观看 ▾」：打开或关闭观看者名单（第 13 期）
    case CShareControlBar::kCmdViewers:
        toggleViewerList();
        break;
    //全员静音：服务端关掉发起人以外所有人的麦克风状态并广播，各人的客户端据此关麦
    case CShareControlBar::kCmdMuteAll:
        muteAllViewers();
        break;
    //鼠标效果的开关或颜色（第 16 期）：改设置、立即生效，并交给 exe 按账号保存（exe 保存后会把同一份设置推回来）
    case CShareControlBar::kCmdMouseEffect:
    {
        ShareMouseEffectSettings settings = m_mouseSettings;
        if (ApplyMouseEffectMenuCommand(settings, static_cast<UINT>(lParam), CShareControlBar::kMenuMouseBase))
        {
            SetMouseEffects(settings);
            m_host->SaveMouseEffectSettings(SerializeMouseEffectSettings(settings).c_str());
        }
        break;
    }
    //「更多」菜单：为我的发言生成字幕（语音字幕），lParam 为 1 打开、0 关闭
    case CShareControlBar::kCmdCaptionSelf:
        m_caption.SetSelfEnabled(lParam != 0);
        break;
    //「更多」菜单：显示字幕（语音字幕），lParam 为 1 打开、0 关闭
    case CShareControlBar::kCmdCaptionShow:
        m_caption.SetShowEnabled(lParam != 0);
        break;
    //分享链接一组（第 25 期）：复制、发送邀请、免输密码、重置、停用
    case CShareControlBar::kCmdLink:
        onLinkMenu(static_cast<int>(lParam));
        break;
    //关闭别人的白板（2026-10-08）：lParam 为建立者，先弹确认框，确认后才关闭
    case CShareControlBar::kCmdCloseBoard:
        requestCloseBoardOf(static_cast<int64_t>(lParam));
        break;
    //允许截图（2026-10-08）：本机的工具条与控制条能否被截图
    case CShareControlBar::kCmdAllowCapture:
        setBarCapture(lParam != 0);
        break;
    //云端录制一组（第 27b 期）：开始、暂停、继续、停止，lParam 为 cloudrecord_control_action；结果以 1754 应答与 1725 为准
    case CShareControlBar::kCmdCloudRecord:
        if (m_hostPhase == host_streaming)
        {
            send(cmd_cloudrecord_control, ShareProtocolClient::buildCloudRecordControl(m_hostRoomId, static_cast<int>(lParam)));
            LOG_INFO("screenshare: cloud record action %d, room=%s.", static_cast<int>(lParam), m_hostRoomId.c_str());
        }
        break;
    //修改标题与描述（第 15 期）：打开修改小窗，填好当前的标题与描述
    case CShareControlBar::kCmdEditInfo:
        if (m_hostPhase == host_streaming)
            m_infoEdit.Open(m_bar.m_hWnd, m_hWnd, m_hostTitle, m_hostDesc, std::wstring());
        break;
    //未知命令：忽略
    default:
        break;
    }
    return 0;
}

LRESULT CShareController::OnViewerCommandMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)lParam;
    (void)bHandled;
    //强制模式（第 13 期）：观看窗本身已不提供离开，这里再拦一道，不发 1723；经移交成为主持人的人不受此限
    if (static_cast<int>(wParam) == CShareViewerWindow::kCmdLeave && m_viewer.IsForced() && !isRoomHost())
    {
        LOG_INFO("screenshare: leave ignored in forced mode, room=%s.", m_viewerRoomId.c_str());
        return 0;
    }
    //「主持人 ▾」菜单选了一项（2026-10-07，经移交成为主持人的人）
    if (static_cast<int>(wParam) == CShareViewerWindow::kCmdHostMenu && m_viewerPhase == viewer_watching)
    {
        onHostMenu(static_cast<int>(lParam));
        return 0;
    }
    //「主持人 ▾」菜单「关闭白板 ▸」里选了一块（2026-10-08）：lParam 为建立者，先弹确认框，确认后才关闭
    if (static_cast<int>(wParam) == CShareViewerWindow::kCmdCloseBoard && m_viewerPhase == viewer_watching)
    {
        requestCloseBoardOf(static_cast<int64_t>(lParam));
        return 0;
    }
    //「N 人观看 ▾」或标题栏「观看者」图标：打开或关闭观看者名单（第 13 期）
    if (static_cast<int>(wParam) == CShareViewerWindow::kCmdViewers && m_viewerPhase == viewer_watching)
    {
        toggleViewerList();
        return 0;
    }
    //「申请控制 / 取消申请 / 结束控制」（第 18 期）
    if (static_cast<int>(wParam) == CShareViewerWindow::kCmdControl && m_viewerPhase == viewer_watching)
    {
        toggleViewerControl();
        return 0;
    }
    //「共享我的屏幕 / 取消共享申请 / 停止我的共享」（第 22 期）
    if (static_cast<int>(wParam) == CShareViewerWindow::kCmdShareMine && m_viewerPhase == viewer_watching)
    {
        toggleShareMine();
        return 0;
    }
    //「新建白板 / 取消白板申请 / 关闭我的白板」（第 23 期）
    if (static_cast<int>(wParam) == CShareViewerWindow::kCmdBoard && m_viewerPhase == viewer_watching)
    {
        toggleMyBoard();
        return 0;
    }
    //标题栏「字幕」菜单（语音字幕）：为我的发言生成字幕、显示字幕，lParam 为 1 打开、0 关闭
    if (static_cast<int>(wParam) == CShareViewerWindow::kCmdCaptionSelf && m_viewerPhase == viewer_watching)
    {
        m_caption.SetSelfEnabled(lParam != 0);
        return 0;
    }
    if (static_cast<int>(wParam) == CShareViewerWindow::kCmdCaptionShow && m_viewerPhase == viewer_watching)
    {
        m_caption.SetShowEnabled(lParam != 0);
        return 0;
    }
    //经移交成为主持人的人关窗：与发起人结束共享一样，还有别人在共享时先问移交还是结束（第 22 期第 30 条）
    if (static_cast<int>(wParam) == CShareViewerWindow::kCmdLeave && m_viewerPhase == viewer_watching && isRoomHost())
    {
        beginHostExit();
        return 0;
    }
    //离开：发 1723 后本地立即关窗
    if (static_cast<int>(wParam) == CShareViewerWindow::kCmdLeave && m_viewerPhase != viewer_idle)
    {
        send(cmd_screenshare_leave, ShareProtocolClient::buildRoomRequest(m_viewerRoomId));
        LOG_INFO("screenshare: left room %s.", m_viewerRoomId.c_str());
        //本人主动离开：这场共享之后结束时不弹结束提示
        m_participantLog.Reset(std::string(), false);
        stopViewerLocal();
        return 0;
    }
    //打开 / 关闭麦克风
    if ((static_cast<int>(wParam) == CShareViewerWindow::kCmdMicOn
         || static_cast<int>(wParam) == CShareViewerWindow::kCmdMicOff)
        && m_viewerPhase == viewer_watching)
    {
        setMic(static_cast<int>(wParam) == CShareViewerWindow::kCmdMicOn);
    }
    //打开 / 关闭摄像头
    if ((static_cast<int>(wParam) == CShareViewerWindow::kCmdCameraOn
         || static_cast<int>(wParam) == CShareViewerWindow::kCmdCameraOff)
        && m_viewerPhase == viewer_watching)
    {
        setCamera(static_cast<int>(wParam) == CShareViewerWindow::kCmdCameraOn);
    }
    //开始 / 停止录制
    if (static_cast<int>(wParam) == CShareViewerWindow::kCmdRecordStart && m_viewerPhase == viewer_watching)
        startShareRecording();
    if (static_cast<int>(wParam) == CShareViewerWindow::kCmdRecordStop && m_viewerPhase == viewer_watching)
        stopShareRecording();
    //进入或退出标注模式：没有标注权限时只提示
    if (static_cast<int>(wParam) == CShareViewerWindow::kCmdAnnotate && m_viewerPhase == viewer_watching)
    {
        if (!m_annotMode && !m_annot.CanDraw())
        {
            showNotice(TR(L"发起人已关闭标注"));
        }
        else if (m_annotMode && m_annotOwnScreen)
        {
            //正在自己共享的内容上标注（第 22 期 22e）：改为在观看窗里标注
            setAnnotMode(false);
            setAnnotMode(true);
        }
        else
        {
            setAnnotMode(!m_annotMode);
        }
    }
    return 0;
}

LRESULT CShareController::OnToastCommandMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)bHandled;
    if (lParam != m_toastInfo.m_token)
        return 0;
    //加入
    if (static_cast<int>(wParam) == CShareCornerToast::kCmdJoin)
        JoinShare(m_toastInfo.m_ctype, m_toastInfo.m_cid, m_toastInfo.m_roomId, m_toastInfo.m_hostName,
                  m_toastInfo.m_forced);
    m_toastInfo = ToastInfo();
    return 0;
}

LRESULT CShareController::OnTimer(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)lParam;
    //共享时长（第 13 期）
    if (wParam == kDurationTimerId)
    {
        refreshDuration();
        return 0;
    }
    //说话人提示；顺带检查摄像头是否中途断开（被别的程序抢走或被拔出）
    if (wParam == kSpeakTimerId)
    {
        if (m_cameraSender.IsRunning() && m_cameraSender.FailReason() == call::kCameraFailStream)
        {
            LOG_WARNING("screenshare: camera stream lost, room=%s.", currentRoomId().c_str());
            setCamera(false);
            m_notice = TR(L"摄像头已断开");
            m_noticeUntilMs = static_cast<int64_t>(::GetTickCount64()) + kNoticeMs;
        }
        //观看窗可能改变了大小或切换了全屏：标注工具条跟着挪
        if (m_annotMode && m_viewerPhase == viewer_watching)
            m_annotBar.MoveTo(annotAnchorRect());
        //观看者换了当前那一块画面（双击放大或还原）：录制里叠的标注跟着换（第 22 期 22e）
        if (m_shareRecording && m_recorder != NULL && recordCanvas() != m_recordCanvas)
            pushRecordAnnotations();
        refreshSpeaking();
        return 0;
    }
    //标注的临时内容（第 19 期）：清掉淡完的激光笔与过期的画笔位置；激光笔淡出期间每次都重画
    if (wParam == kAnnotTickTimerId)
    {
        if (!m_annot.IsRunning())
        {
            KillTimer(kAnnotTickTimerId);
            return 0;
        }
        bool fading = false;
        const std::vector<AnnotationStroke>& strokes = m_annot.Model().Strokes();
        for (size_t i = 0; i < strokes.size() && !fading; ++i)
            fading = (strokes[i].m_tool == annot_tool_laser && strokes[i].m_finished);
        //拖动形状中按下或松开 Shift、鼠标不动（2026-10-07）：预览也要跟着变成正形或恢复。键盘焦点通常在被共享的程序上，
        //本线程收不到按键消息，取异步的按键状态
        const bool shiftChanged =
            m_annotMode && m_annot.SetShiftDown((::GetAsyncKeyState(VK_SHIFT) & kKeyDownBit) != 0);
        if (m_annot.Tick(static_cast<int64_t>(::GetTickCount64())) || fading || shiftChanged)
            annotationChanged();
        return 0;
    }
    //共享者的边框、标注层跟随被共享的内容（第 22 期）
    if (wParam == kPublishFollowTimerId)
    {
        if (m_publishPhase == share_publish_streaming)
            followStreamSource(m_publisher, m_publishSource, m_publishBorder, m_publishOverlay, false,
                               m_annotMode && m_annotOwnScreen, "screenshare multi");
        return 0;
    }
    //本人的白板广播视野（第 23 期）
    if (wParam == kBoardViewTimerId)
    {
        onBoardViewTimer();
        return 0;
    }
    //经移交成为主持人后有人加入：补发已有的标注，只触发一次（2026-10-07）
    if (wParam == kTransferredSnapshotTimerId)
    {
        KillTimer(kTransferredSnapshotTimerId);
        if (isRoomHost() && m_hostPhase != host_streaming && m_annot.IsRunning() && !m_annot.Model().Strokes().empty())
        {
            m_annot.SendSnapshot();
            LOG_INFO("screenshare: annotation snapshot sent by the transferred host, %u stroke(s).",
                     static_cast<unsigned>(m_annot.Model().Strokes().size()));
        }
        return 0;
    }
    if (wParam != kFollowTimerId)
    {
        bHandled = FALSE;
        return 0;
    }
    //发起人的边框、标注层跟随共享的内容；暂停或停止共享屏幕时不显示边框
    if (m_hostPhase == host_streaming)
        followStreamSource(m_streamer, m_hostSource, m_border, m_annotOverlay, m_hostPaused || m_hostScreenStopped,
                           m_annotMode, "screenshare");
    return 0;
}

void CShareController::followStreamSource(const ShareHostStreamer& streamer, ShareSourceSpec& source,
                                          CShareBorderWindow& border, CShareAnnotationOverlay& overlay, bool suppressed,
                                          bool moveAnnotBar, const char* logTag)
{
    //共享整块显示器时显示器矩形可能变了（分辨率变化后采集器重建，2026-10-12）：边框、标注层、控制坐标跟着换
    if (source.m_kind == share_source_monitor)
    {
        const RECT now = streamer.CurrentSourceRect();
        if (::IsRectEmpty(&now) || ::EqualRect(&now, &source.m_monitorRect))
            return;
        LOG_INFO("%s: shared monitor rect changed to (%ld,%ld)-(%ld,%ld).", logTag, now.left, now.top, now.right,
                 now.bottom);
        source.m_monitorRect = now;
        if (!suppressed)
            border.Show(now, true);
        overlay.Place(now, true);
        //工具条按当前的锚点挪（锚点由 source 算出，须在更新矩形之后取）
        if (moveAnnotBar)
            m_annotBar.MoveTo(annotAnchorRect());
        return;
    }
    if (source.m_kind != share_source_window)
        return;
    //边框跟随被共享的窗口：移动、缩放时重画，最小化、已关闭或共享暂停时隐藏
    const HWND hwnd = source.m_hwnd;
    const bool hidden = !::IsWindow(hwnd) || ::IsIconic(hwnd) || suppressed;
    const RECT bounds = hidden ? RECT() : GetShareWindowBounds(hwnd);
    border.Follow(bounds, hidden);
    //标注层与工具条跟着被共享的窗口移动、缩放
    const RECT& old = overlay.Rect();
    if (hidden || !::EqualRect(&bounds, &old))
    {
        overlay.Place(hidden ? old : bounds, !hidden);
        //工具条按当前的锚点挪：在本人的白板上标注时锚在白板上，不跟着共享的窗口走
        if (moveAnnotBar && !hidden)
            m_annotBar.MoveTo(annotAnchorRect());
    }
}

//==================== 内部 ====================

bool CShareController::send(uint16_t cmd, const std::string& body)
{
    if (m_host == NULL)
        return false;
    const bool ok = m_host->SendSignal(cmd, body.data(), static_cast<uint32_t>(body.size()));
    if (!ok)
        LOG_WARNING("screenshare: send cmd %u failed.", static_cast<unsigned>(cmd));
    return ok;
}

void CShareController::reportStatus(int ctype, int64_t cid, const std::string& roomId, int64_t hostId,
                                    const std::wstring& hostName, bool active, bool locked, bool forced,
                                    const std::wstring& title)
{
    if (m_host != NULL)
        m_host->OnScreenShareStatus(ctype, cid, roomId.c_str(), hostId, hostName.c_str(), active, locked, forced,
                                    title.c_str());
}

void CShareController::startStreaming(const ShareRelayInfo& relay)
{
    //宽限期内重新登录会再收到 1726：推流管线、语音与摄像头都重建（麦克风、摄像头保持之前的开关）
    const bool micOn = m_audio.IsRunning() ? m_audio.IsMicEnabled() : m_hostMicInitial;
    const bool cameraOn = m_cameraSender.IsRunning();
    m_cameraSender.Stop();
    m_streamer.Stop();
    m_streamer.SetAudioSession(NULL);
    m_streamer.SetCameraEndpoints(NULL, NULL);
    m_cameraReceiver.Stop();
    m_cameraSeats.clear();
    stopAudio();
    //摄像头接收先于推流管线启动，观看者已开着的摄像头一到就能解码
    m_cameraReceiver.Start();
    m_streamer.SetCameraEndpoints(&m_cameraSender, &m_cameraReceiver);
    //鼠标效果按当前设置（第 16 期）
    m_streamer.SetMouseEffects(m_mouseSettings);
    if (!m_streamer.Start(relay, m_host->GetServerPublicKey(), m_hostSource, NULL, this))
    {
        LOG_ERROR("screenshare: start streamer failed, room=%s.", relay.m_roomId.c_str());
        m_streamer.SetCameraEndpoints(NULL, NULL);
        m_cameraReceiver.Stop();
        return;
    }
    m_streamer.SetAudioSession(&m_audio);
    m_cameraWindow.SetSource(this);
    //标注：宽限期内重连时保留已有的笔迹（发送通道仍是同一个推流管线）；临时内容的刷新定时器随之开始（第 19 期）
    if (!m_annot.IsRunning())
        m_annot.Start(&m_streamer, relay.m_seat, static_cast<uint32_t>(::GetTickCount()));
    //发起人的标注层盖在自己的画面上，标注画在这块画面上（第 22 期 22e）
    m_annotOverlay.SetCanvas(relay.m_seat);
    m_annot.SetCanvas(relay.m_seat);
        //沿用工具条上次的选择（工具、颜色与各项样式）
        ApplyAnnotBarState(m_annotState, m_annot);
    SetTimer(kAnnotTickTimerId, kAnnotTickTimerMs, NULL);
    m_streamer.SetAnnotationSink(this);
    //控制键鼠（第 18 期）：控制者的输入帧交给本类核对后注入
    m_streamer.SetInputSink(this);
    m_hostPhase = host_streaming;
    const bool monitor = (m_hostSource.m_kind == share_source_monitor);
    const RECT rect = monitor ? m_hostSource.m_monitorRect : GetShareWindowBounds(m_hostSource.m_hwnd);
    //控制条显示加入密码（第 12 期）与强制模式（第 13 期）
    m_bar.SetPassword(utf8ToWide(m_hostPassword));
    m_bar.SetForced(m_hostForced);
    //「屏幕当白板」只在共享一个窗口时可用（第 23 期）
    m_bar.SetScreenBoard(m_hostSource.m_kind == share_source_window, m_screenBoard);
    m_bar.SetShareTitle(m_hostTitle);
    m_bar.Show(rect, m_hWnd);
    //「更多」菜单里的云端录制一组：开始共享时只收到 1720、1726，要等第一条 1725 才会设置，
    //主持人独自一人时就看不到「开始云端录制」；这里按企业开关先设好，只改「是否可用」，不动正在录制的状态（宽限期内重连时可能在录）
    m_bar.SetCloudRecordAvailable(m_host != NULL && m_host->IsCloudRecordEnabled());
    m_bar.SetPaused(false);
    //共享时长（第 13 期）：以 1726 的 elapsedms 为起点计时，宽限期内重连后以新的 1726 为准
    m_durationClock.Start(relay.m_elapsedMs, ::GetTickCount64());
    refreshDuration();
    SetTimer(kDurationTimerId, kDurationTimerMs, NULL);
    m_border.Show(rect, monitor);
    m_annotOverlay.Place(rect, true);
    m_bar.SetAnnotating(m_annotMode);
    SetTimer(kFollowTimerId, kFollowTimerMs, NULL);
    const bool sysAudioOn = m_audio.IsRunning() ? m_audio.IsSystemAudioEnabled() : m_hostSysAudioInitial;
    startAudio(&m_streamer, relay.m_seat, micOn);
    m_bar.SetMicOn(m_audio.IsMicEnabled());
    m_bar.SetSystemAudioOn(false);
    m_bar.SetCameraOn(false);
    if (sysAudioOn)
        setSystemAudio(true);
    if (cameraOn)
        setCamera(true);
    LOG_INFO("screenshare: streaming room %s, source kind=%d.", relay.m_roomId.c_str(), m_hostSource.m_kind);
}

void CShareController::stopHostLocal()
{
    if (IsWindow())
        KillTimer(kFollowTimerId);
    //共享结束：本人的录制随之停止并保存（房间已没有了，不再发 1731）
    if (m_shareRecording && m_viewerPhase == viewer_idle)
    {
        if (m_recorder != NULL)
            m_recorder->StopRecording();
        finishShareRecording(false);
    }
    //本人摄像头经推流管线发帧，先于推流停止
    if (m_viewerPhase == viewer_idle)
        m_cameraSender.Stop();
    //先停推流（中转接收线程随之结束，不再有声音帧、摄像头帧交出来），再停语音与摄像头接收
    m_streamer.Stop();
    releaseHostP2PViewers();
    m_streamer.SetAudioSession(NULL);
    m_streamer.SetCameraEndpoints(NULL, NULL);
    m_streamer.SetAnnotationSink(NULL);
    m_streamer.SetInputSink(NULL);
    //控制键鼠（第 18 期）：卸下钩子、补发抬起、关掉申请提示
    resetControlLocal();
    //多人共享（第 22 期）：其他人的屏幕、排队的共享申请、退出选择框都属于这一场
    m_screensWindow.Close();
    m_screens.clear();
    m_shareRequests.Clear();
    m_shareAnswered.clear();
    m_pendingSharing.clear();
    m_sharingToast.Hide();
    m_hostExitDialog.Close();
    m_hostScreenStopped = false;
    m_bar.SetMineStopped(false);
    m_bar.SetOtherScreenCount(0);
    //白板（第 23 期）：本人的白板、排队的新建白板申请、屏幕当白板都属于这一场
    if (m_viewerPhase == viewer_idle)
        closeMyBoardLocal();
    m_boardRequests.Clear();
    m_boardAnswered.clear();
    m_boardToast.Hide();
    m_screenBoard = false;
    m_bar.SetScreenBoard(false, false);
    if (m_viewerPhase == viewer_idle)
    {
        stopAudio();
        m_cameraReceiver.Stop();
        m_cameraSeats.clear();
        m_annotMode = false;
        m_annotBar.Hide();
        m_textDialog.Close();
        m_annot.Stop();
    }
    m_annotOverlay.Close();
    //在「其他人的屏幕」窗口里标注的记录属于这一场（2026-10-07），窗口已在上面关闭
    m_annotOnScreens = false;
    m_cameraWindow.Close();
    m_bar.Hide();
    m_border.Hide();
    //「允许截图」只对这一场共享有效（2026-10-08）：结束时恢复不允许
    SetBarCaptureAllowed(false);
    //关闭别人白板的确认框（2026-10-08）属于这一场
    m_boardCloseDlg.Close();
    m_boardCloseOwner = 0;
    m_picker.CloseDialog();
    //共享结束：还没确认的移出不再需要，没保存的标题与描述修改也不再需要
    m_removeDlg.Close();
    m_removeTarget = 0;
    setRemoveConfirmModal(false);
    m_infoEdit.Close();
    m_pendingTitle.clear();
    m_pendingDesc.clear();
    if (m_hostPhase == host_waiting || m_hostPhase == host_streaming)
        reportStatus(m_hostCtype, m_hostCid, std::string(), 0, std::wstring(), false, false);
    m_hostPhase = host_idle;
    m_hostRoomId.clear();
    m_hostPassword.clear();
    //分享链接（第 25 期）属于这一场：服务端在共享结束时已把它标为结束
    m_hostLinkCode.clear();
    m_hostLinkNoPassword = false;
    m_pendingLinkMenu = 0;
    m_bar.SetLinkState(false, false);
    m_bar.SetPassword(std::wstring());
    m_bar.SetShareTitle(std::wstring());
    m_hostForced = false;
    m_hostRecordAllowed = true;
    m_hostTitle.clear();
    m_hostDesc.clear();
    stopDurationIfIdle();
    m_hostPaused = false;
    m_mustRepick = false;
    notifyCallObserver();
}

void CShareController::stopViewerLocal()
{
    //离开观看：本人的录制随之停止并保存；录制在取观看窗的画面，必须先于观看窗关闭
    if (m_shareRecording && m_hostPhase == host_idle)
    {
        if (m_recorder != NULL)
            m_recorder->StopRecording();
        finishShareRecording(false);
    }
    //本人摄像头经收流管线发帧，先于观看窗关闭
    if (m_hostPhase == host_idle)
        m_cameraSender.Stop();
    //多人共享（第 22 期）：本人作为共享者的推流随之停止（离开房间时服务端同样结束他的画面，不另发 1737）；
    //经移交成为主持人时排队的共享申请、退出选择框也属于这一场
    stopPublisherLocal(false);
    if (m_hostPhase == host_idle)
    {
        m_shareRequests.Clear();
        m_shareAnswered.clear();
        m_pendingSharing.clear();
        m_sharingToast.Hide();
        m_hostExitDialog.Close();
        m_screens.clear();
        //白板（第 23 期）：本人的白板与排队的新建白板申请属于这一场
        closeMyBoardLocal();
        m_boardRequests.Clear();
        m_boardAnswered.clear();
        m_boardToast.Hide();
    }
    //控制键鼠（第 18 期）：观看窗关闭前清掉控制状态
    resetControlLocal();
    m_viewer.SetControlInput(NULL);
    //「允许截图」只对这一场共享有效（2026-10-08）：经移交的主持人与共享者离开时恢复不允许
    SetBarCaptureAllowed(false);
    //关闭别人白板的确认框（2026-10-08）属于这一场
    m_boardCloseDlg.Close();
    m_boardCloseOwner = 0;
    //先关观看窗（收流管线随之停止，不再有声音帧、摄像头帧交出来），再停语音与摄像头接收
    m_viewer.CloseViewer();
    //收流管线已关闭链路；链路还没建好的，之后建好时由业务方直接关闭
    if (!m_viewerP2PFeature.empty())
    {
        m_p2pBroker.Abandon(m_viewerP2PFeature);
        m_viewerP2PFeature.clear();
    }
    m_viewer.SetAudioSession(NULL);
    m_viewer.SetCameraEndpoints(NULL, NULL);
    m_viewer.SetCameraSource(NULL);
    m_viewer.SetAnnotation(NULL, NULL, NULL);
    if (m_hostPhase == host_idle)
    {
        stopAudio();
        m_cameraReceiver.Stop();
        m_cameraSeats.clear();
        m_annotMode = false;
        m_annotBar.Hide();
        m_textDialog.Close();
        m_annot.Stop();
    }
    m_viewerPhase = viewer_idle;
    m_viewerRoomId.clear();
    m_viewerHostName.clear();
    m_viewerJoinPassword.clear();
    stopDurationIfIdle();
}

void CShareController::refreshDuration()
{
    if (!m_durationClock.IsRunning())
        return;
    const std::wstring text = FormatShareDuration(m_durationClock.ElapsedSeconds(::GetTickCount64()));
    //录下这场共享时的已录时长（2026-10-07，方案第 14.2 节）：共享录制不再弹出录制条，时长显示在主工具条上
    std::wstring recordText;
    if (m_shareRecording && m_recorder != NULL)
        recordText = screenrecord::FormatRecordDuration(m_recorder->RecordedMs());
    if (m_hostPhase == host_streaming)
    {
        m_bar.SetDurationText(text);
        m_bar.SetRecordDuration(recordText);
        //单聊共享里画面经直连链路发给观看者时，控制条显示「P2P」徽章
        m_bar.SetP2PLinkPath(m_streamer.P2PViewerLinkPath());
    }
    if (m_viewerPhase == viewer_watching)
    {
        m_viewer.SetDurationText(text);
        m_viewer.SetRecordDuration(recordText);
    }
}

void CShareController::muteAllViewers()
{
    //发起人与经移交成为主持人的人都可以（2026-10-07），房间号取当前这一场
    if (!isRoomHost())
        return;
    const std::string& roomId = currentRoomId();
    send(cmd_screenshare_set_permission,
         ShareProtocolClient::buildSetPermission(roomId, share_perm_unchanged, share_perm_unchanged, true));
    LOG_INFO("screenshare: mute all requested, room=%s.", roomId.c_str());
    m_notice = TR(L"已请所有人关闭麦克风");
    m_noticeUntilMs = static_cast<int64_t>(::GetTickCount64()) + kNoticeMs;
    refreshSpeaking();
}

void CShareController::toggleViewerList()
{
    if (m_viewerList.IsShown())
    {
        m_viewerList.Hide();
        return;
    }
    RECT anchor;
    const bool hosting = (m_hostPhase == host_streaming);
    //经移交成为主持人的人在观看窗里同样可以静音与移出别人（2026-10-07，方案第 14.3 节第 2 条）
    const bool asHost = hosting || isRoomHost();
    if (hosting)
    {
        if (!m_bar.GetViewersButtonScreenRect(anchor))
            return;
    }
    else
    {
        //观看端：全屏时锚在工具条的「N 人观看 ▾」下，窗口模式时锚在标题栏右侧
        if (m_viewerPhase != viewer_watching || !m_viewer.GetViewersAnchor(anchor))
            return;
    }
    //发起人一侧排除在屏幕采集之外，不会被共享出去
    m_viewerList.Show(anchor, m_hWnd, asHost, hosting);
    refreshViewerList();
    LOG_INFO("screenshare: viewer list opened, host=%d, room=%s.", asHost ? 1 : 0, currentRoomId().c_str());
}

void CShareController::refreshViewerList()
{
    if (!m_viewerList.IsShown())
        return;
    std::map<int64_t, std::wstring> names;
    for (size_t i = 0; i < m_lastState.m_members.size(); ++i)
    {
        const int64_t uid = m_lastState.m_members[i].m_userId;
        if (uid != m_lastState.m_hostId)
            names[uid] = displayName(uid);
    }
    std::vector<ShareViewerRow> rows;
    const bool hosting = (m_hostPhase == host_streaming);
    BuildShareViewerRows(m_lastState, names, m_speakingSeats, hosting || isRoomHost(), rows);
    //「允许控制」把本人正在推的屏幕交给观看者控制（2026-10-08）：原主持人推流中，或经移交成为主持人、
    //经共享者管线推流中才给；没在推流（含原主持人停掉了自己的画面）时不给
    if (!ShareViewerListCanGrantControl(isRoomHost(), hosting, m_hostScreenStopped,
                                        m_publishPhase == share_publish_streaming))
    {
        for (size_t i = 0; i < rows.size(); ++i)
            rows[i].m_canControl = false;
    }
    //名单顶部的共享描述（第 15 期）：发起人用自己填写的，观看者用缓存里记下的
    std::wstring shareTitle;
    std::wstring shareDesc;
    if (m_hostPhase == host_streaming)
        shareDesc = m_hostDesc;
    else
        m_roomInfo.Find(currentRoomId(), shareTitle, shareDesc);
    m_viewerList.SetDescription(shareDesc);
    m_viewerList.SetRows(rows);
}

LRESULT CShareController::OnViewerListCommandMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)bHandled;
    //发起人与经移交成为主持人的人（2026-10-07）都可以管理名单；房间号取当前这一场
    if (!isRoomHost())
        return 0;
    const std::string roomId = currentRoomId();
    switch (static_cast<int>(wParam))
    {
    //静音某个观看者：按座位号找到 userid，经 1730 的 muteuser 请服务端关掉他的麦克风
    case CShareViewerListPanel::kCmdMute:
    {
        std::map<int, int64_t>::const_iterator it = m_seatUsers.find(static_cast<int>(lParam));
        if (it == m_seatUsers.end())
            break;
        send(cmd_screenshare_set_permission,
             ShareProtocolClient::buildSetPermission(roomId, share_perm_unchanged, share_perm_unchanged, false,
                                                     it->second));
        LOG_INFO("screenshare: mute member requested, room=%s, seat=%d, user=%lld.", roomId.c_str(),
                 static_cast<int>(lParam), static_cast<long long>(it->second));
        m_notice = TRF(L"已请 {0} 关闭麦克风", displayName(it->second));
        m_noticeUntilMs = static_cast<int64_t>(::GetTickCount64()) + kNoticeMs;
        refreshSpeaking();
        break;
    }
    //名单顶部的「全员静音」
    case CShareViewerListPanel::kCmdMuteAll:
        muteAllViewers();
        break;
    //允许控制 / 收回控制（第 18 期）：按座位号找到 userid；他正在控制就收回，否则授予（原控制者由服务端替换下来）
    case CShareViewerListPanel::kCmdControl:
    {
        //控制的是主持人自己正在推的屏幕（2026-10-08：经移交成为主持人、经共享者管线推流中同样可以）；没在推流时不处理
        if (!ShareViewerListCanGrantControl(isRoomHost(), m_hostPhase == host_streaming, m_hostScreenStopped,
                                            m_publishPhase == share_publish_streaming))
            break;
        std::map<int, int64_t>::const_iterator it = m_seatUsers.find(static_cast<int>(lParam));
        if (it == m_seatUsers.end())
            break;
        if (it->second == m_controllerId)
            revokeControlNow();
        else
            sendControl(share_control_grant, m_host->GetSelfUserId(), it->second);
        //他还在排队等答复：从队列里去掉，免得授予之后再弹一次
        dropControlRequest(it->second);
        break;
    }
    //移出某个观看者（第 14 期）：按座位号找到 userid，先弹确认框，确认后再发 1732
    case CShareViewerListPanel::kCmdRemove:
    {
        std::map<int, int64_t>::const_iterator it = m_seatUsers.find(static_cast<int>(lParam));
        if (it == m_seatUsers.end())
            break;
        m_removeTarget = it->second;
        ShareConfirmSpec spec;
        spec.m_title = TR(L"移出观看者");
        spec.m_prompt = MakeShareRemovePrompt(displayName(it->second));
        spec.m_checkText = TR(L"允许他再次加入");
        spec.m_checked = true;
        spec.m_okText = TR(L"移出");
        spec.m_resultMsg = kMsgShareRemoveConfirmResult;
        //确认框排除在屏幕采集之外，观看者看不到；居中在控制条（经移交成为主持人的人为观看窗）所在的显示器上（第 15 期），
        //对控制条与名单模态
        m_removeDlg.OpenCentered(hostToolsWindow(), m_hWnd, spec, true);
        setRemoveConfirmModal(true);
        LOG_INFO("screenshare: remove member confirm opened, room=%s, seat=%d, user=%lld.", roomId.c_str(),
                 static_cast<int>(lParam), static_cast<long long>(it->second));
        break;
    }
    //未知命令：忽略
    default:
        break;
    }
    return 0;
}

LRESULT CShareController::OnRemoveConfirmMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)bHandled;
    //确认框已关闭（确认或取消）：控制条与名单恢复可用
    setRemoveConfirmModal(false);
    const int64_t target = m_removeTarget;
    m_removeTarget = 0;
    if (wParam == 0 || target == 0 || !isRoomHost())
        return 0;
    const std::string roomId = currentRoomId();
    //确认框开着的时候对方可能已经自己离开了：不在房间里就不再发
    bool stillIn = false;
    for (std::map<int, int64_t>::const_iterator it = m_seatUsers.begin(); it != m_seatUsers.end(); ++it)
        stillIn = stillIn || (it->second == target);
    if (!stillIn)
    {
        LOG_INFO("screenshare: remove member skipped, user=%lld already left.", static_cast<long long>(target));
        return 0;
    }
    const bool allowRejoin = (lParam != 0);
    send(cmd_screenshare_remove_member, ShareProtocolClient::buildRemoveMember(roomId, target, allowRejoin));
    LOG_INFO("screenshare: remove member requested, room=%s, user=%lld, allow rejoin=%d.", roomId.c_str(),
             static_cast<long long>(target), allowRejoin ? 1 : 0);
    m_notice = TRF(L"已将 {0} 移出", displayName(target));
    m_noticeUntilMs = static_cast<int64_t>(::GetTickCount64()) + kNoticeMs;
    refreshSpeaking();
    return 0;
}

void CShareController::onRemoveMemberResult(const std::string& json)
{
    ShareResult r;
    if (!ShareProtocolClient::parseResult(json, r) || r.m_code == 0)
        return;
    LOG_WARNING("screenshare: remove member rejected, code=%d, msg=%s.", r.m_code, r.m_msg.c_str());
    //对方已经不在房间里（在服务端处理之前自己离开或掉线）：结果与移出相同，不提示
    if (r.m_code == error_code_screenshare_not_member)
        return;
    showError(TR(L"移出观看者失败，请稍后再试。"));
}

LRESULT CShareController::OnInfoEditResultMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)wParam;
    (void)bHandled;
    std::unique_ptr<ShareInfoEditResult> result(reinterpret_cast<ShareInfoEditResult*>(lParam));
    //发起人与经移交成为主持人的人（2026-10-07）都可以修改
    if (!result || !isRoomHost())
        return 0;
    const std::string roomId = currentRoomId();
    //当前的标题与描述：发起人用自己记着的，经移交成为主持人的人用房间缓存里的
    std::wstring curTitle = m_hostTitle;
    std::wstring curDesc = m_hostDesc;
    if (m_hostPhase != host_streaming)
        m_roomInfo.Find(roomId, curTitle, curDesc);
    //没有改动：不发请求
    if (result->m_title == curTitle && result->m_desc == curDesc)
    {
        LOG_INFO("screenshare: share info unchanged, room=%s.", roomId.c_str());
        return 0;
    }
    m_pendingTitle = result->m_title;
    m_pendingDesc = result->m_desc;
    send(cmd_screenshare_set_info, ShareProtocolClient::buildSetInfo(roomId, ShareTextToUtf8(m_pendingTitle),
                                                                   ShareTextToUtf8(m_pendingDesc)));
    LOG_INFO("screenshare: set share info requested, title chars %u, desc chars %u, room=%s.",
             static_cast<unsigned>(CountShareInfoChars(m_pendingTitle)),
             static_cast<unsigned>(CountShareInfoChars(m_pendingDesc)), roomId.c_str());
    return 0;
}

void CShareController::onSetInfoResult(const std::string& json)
{
    ShareResult r;
    if (!ShareProtocolClient::parseResult(json, r))
        return;
    if (r.m_code == 0)
    {
        //成功：1734 推送随后也会到，这里先按本次提交的内容刷新，两者内容相同
        if (m_hostPhase == host_streaming && r.m_roomId == m_hostRoomId)
            applyHostInfo(m_pendingTitle, m_pendingDesc);
        return;
    }
    LOG_WARNING("screenshare: set share info rejected, code=%d, msg=%s.", r.m_code, r.m_msg.c_str());
    if (!isRoomHost())
        return;
    //命中敏感词：带着刚才的输入重新打开修改窗，说明行写明原因
    if (r.m_code == error_code_screenshare_sensitive)
    {
        m_infoEdit.Open(hostToolsWindow(), m_hWnd, m_pendingTitle, m_pendingDesc,
                        TR(L"标题或描述包含敏感词，请修改后再保存"));
        return;
    }
    showError(TR(L"修改共享标题与描述失败，请稍后再试。"));
}

void CShareController::onInfoNotify(const std::string& json)
{
    ShareInfoNotify n;
    if (!ShareProtocolClient::parseInfoNotify(json, n))
        return;
    const std::wstring title = utf8ToWide(n.m_title);
    const std::wstring desc = utf8ToWide(n.m_desc);
    m_roomInfo.Remember(n.m_roomId, title, desc);
    LOG_INFO("screenshare: share info changed, room=%s, card msgid=%lld.", n.m_roomId.c_str(),
             static_cast<long long>(n.m_cardMsgId));
    //本人发起的这一场
    if (m_hostPhase == host_streaming && n.m_roomId == m_hostRoomId)
    {
        applyHostInfo(title, desc);
        return;
    }
    //正在观看的这一场：观看窗标题、共享说明与名单顶部的描述
    if (m_viewerPhase == viewer_watching && n.m_roomId == m_viewerRoomId)
    {
        m_viewer.SetShareInfo(title, desc);
        refreshViewerList();
    }
}

void CShareController::setRemoveConfirmModal(bool modal)
{
    //EnableWindow 只改输入状态，不改窗口的显隐与位置；窗口不存在（名单没打开）时跳过
    if (m_bar.IsWindow())
        m_bar.EnableWindow(modal ? FALSE : TRUE);
    if (m_viewerList.IsWindow())
        m_viewerList.EnableWindow(modal ? FALSE : TRUE);
    LOG_INFO("screenshare: remove confirm modal=%d.", modal ? 1 : 0);
}

void CShareController::applyHostInfo(const std::wstring& title, const std::wstring& desc)
{
    if (title == m_hostTitle && desc == m_hostDesc)
        return;
    m_hostTitle = title;
    m_hostDesc = desc;
    m_bar.SetShareTitle(m_hostTitle);
    refreshViewerList();
    //主程序的群聊横幅与状态缓存按新标题显示
    reportStatus(m_hostCtype, m_hostCid, m_hostRoomId, m_host->GetSelfUserId(), std::wstring(), true,
                 !m_hostPassword.empty(), m_hostForced, m_hostTitle);
}

void CShareController::stopDurationIfIdle()
{
    if (m_hostPhase != host_idle || m_viewerPhase != viewer_idle)
        return;
    m_viewerList.Hide();
    m_lastState = ShareStateNotify();
    m_speakingSeats.clear();
    m_durationClock.Stop();
    if (IsWindow())
        KillTimer(kDurationTimerId);
}

void CShareController::showError(const wchar_t* text)
{
    ::MessageBoxW(mainWindow(), text, TR(kCaption), MB_OK | MB_ICONINFORMATION);
}

void CShareController::openPicker(bool switchMode, const std::wstring& hint, bool startsShare)
{
    int mode = share_picker_start;
    //切换模式：本人已在共享时换内容；多人共享里经主持人同意、还没开始共享时界面相同，但按钮为「开始共享」
    if (switchMode)
        mode = startsShare ? share_picker_switch_to_start : share_picker_switch;
    else if (inCallShare())
        mode = share_picker_start_in_call;
    //为我的发言生成字幕（2026-10-07）：发起共享时字幕可用的话在选择窗里提供，默认值为上次的选择（字幕模块记住的偏好）
    bool offerCaption = false;
    bool captionOn = false;
    if (SharePickerOffersCaption(mode))
    {
        caption::ICaptionModule* captionModule = flamingomedia::CMediaCaptionBridge::Instance().Acquire();
        if (captionModule != NULL)
        {
            offerCaption = true;
            captionOn = captionModule->GetPreference(caption::caption_pref_live_self);
        }
    }
    m_picker.SetCaptionOptions(offerCaption, captionOn);
    //共享标题的默认值写成「本人显示名的屏幕共享」（第 15 期）
    m_picker.Open(mainWindow(), m_hWnd, mode, hint, displayName(m_host != NULL ? m_host->GetSelfUserId() : 0));
}

HWND CShareController::mainWindow() const
{
    return m_host != NULL ? m_host->GetMainWindow() : NULL;
}

void CShareController::startAudio(ShareAudioTransport* transport, int seat, bool micOn)
{
    m_selfSeat = seat;
    m_seatUsers.clear();
    m_notice.clear();
    m_noticeUntilMs = 0;
    //说话人提示的定时器同时负责摄像头断开检查与临时提示，声音设备打不开时也要跑
    if (IsWindow())
        SetTimer(kSpeakTimerId, kSpeakTimerMs, NULL);
    //声音设备打不开：画面照常共享，只是没有声音（已记日志）
    bool withoutDevices = false;
#ifdef _DEBUG
    withoutDevices = useShareNoAudioDevice();
    if (withoutDevices)
        LOG_INFO("screenshare: FLAMINGO_SHARE_NO_AUDIO=1, room audio runs without devices.");
#endif
    m_audio.Start(transport, seat, micOn, withoutDevices);
    //实时字幕（语音字幕）：两条收帧管线都登记字幕接收方（同一时间只有正在用的那条会收到字幕帧），
    //字幕会话登记为语音会话的字幕旁路，按记住的选择决定是否识别本人的发言
    m_streamer.SetCaptionSink(&m_caption);
    m_viewer.SetCaptionSink(&m_caption);
    m_caption.BeginSession(transport, seat, &m_audio);
    applyCaptionState();
}

void CShareController::stopAudio()
{
    if (IsWindow())
        KillTimer(kSpeakTimerId);
    //实时字幕：先结束字幕会话（取消声音旁路、关闭识别流、清掉浮层），再停语音
    m_caption.EndSession();
    m_streamer.SetCaptionSink(NULL);
    m_viewer.SetCaptionSink(NULL);
    //语音会话停掉之后电脑声音也随之停止：撤掉共享电脑声音的互斥登记（2026-10-06）
    call::SystemAudioExclusive::Instance().Release(this);
    m_audio.Stop();
    m_seatUsers.clear();
}

//==================== 实时字幕（语音字幕） ====================

std::wstring CShareController::CaptionSeatName(int seat)
{
    std::map<int, int64_t>::const_iterator it = m_seatUsers.find(seat);
    if (it != m_seatUsers.end())
        return displayName(it->second);
    //刚进房间、成员快照还没到时，本人的座位按本人显示名
    if (seat == m_selfSeat && m_host != NULL)
        return displayName(m_host->GetSelfUserId());
    return std::wstring();
}

bool CShareController::CaptionOverlayArea(RECT& area, bool& excludeFromCapture, HWND& owner)
{
    //观看者：盖在观看窗的画面上，以观看窗为所有者窗口
    if (m_viewerPhase == viewer_watching)
    {
        excludeFromCapture = false;
        owner = m_viewer.m_hWnd;
        return m_viewer.CaptionArea(area);
    }
    //共享者本人：盖在被共享的显示器或窗口上，排除在采集之外；共享的窗口最小化或不可见时不显示
    if (m_hostPhase == host_streaming)
    {
        if (m_hostSource.m_kind != share_source_monitor
            && (m_hostSource.m_hwnd == NULL || !::IsWindowVisible(m_hostSource.m_hwnd) || ::IsIconic(m_hostSource.m_hwnd)))
            return false;
        area = hostSourceRect();
        excludeFromCapture = true;
        owner = NULL;
        return area.right > area.left && area.bottom > area.top;
    }
    return false;
}

caption::ICaptionRecording* CShareController::CaptionRecording(int64_t& recordedMs)
{
    recordedMs = 0;
    if (!m_shareRecording || m_recorder == NULL)
        return NULL;
    return m_recorder->ShareCaptionRecording(recordedMs);
}

void CShareController::OnCaptionStateChanged()
{
    applyCaptionState();
}

void CShareController::applyCaptionState()
{
    const bool available = m_caption.IsAvailable();
    const bool selfOn = m_caption.IsSelfEnabled();
    const bool showOn = m_caption.IsShowEnabled();
    m_bar.SetCaptionState(available, selfOn, showOn);
    m_viewer.SetCaptionState(available, selfOn, showOn);
}

void CShareController::setMic(bool on)
{
    const std::string& roomId = currentRoomId();
    if (roomId.empty() || !m_audio.IsRunning())
        return;
    //通话中共享：声音已经经由通话传输，房间里再开麦克风对方会听到两遍
    if (on && inCallShare())
    {
        if (m_hostPhase == host_streaming)
            m_bar.SetMicOn(m_audio.IsMicEnabled());
        if (m_viewerPhase == viewer_watching)
            m_viewer.SetMicOn(m_audio.IsMicEnabled());
        showNotice(TR(L"通话中，双方的声音经由通话传输"));
        return;
    }
    const bool ok = m_audio.SetMicEnabled(on);
    const bool actual = m_audio.IsMicEnabled();
    if (m_hostPhase == host_streaming)
        m_bar.SetMicOn(actual);
    if (m_viewerPhase == viewer_watching)
        m_viewer.SetMicOn(actual);
    reportMedia();
    if (on && !ok)
        showError(TR(L"无法打开麦克风，请检查麦克风是否接好、是否被其他程序占用。"));
}

void CShareController::setSystemAudio(bool on)
{
    if (m_hostPhase != host_streaming || !m_audio.IsRunning())
        return;
    const bool ok = m_audio.SetSystemAudioEnabled(on);
    //共享电脑声音的进程内互斥（2026-10-06）：打开后登记，通话那一路若也在共享电脑声音会被要求停下，免得对方听到两遍；
    //关闭后撤掉登记
    if (m_audio.IsSystemAudioEnabled())
        call::SystemAudioExclusive::Instance().Acquire(this);
    else
        call::SystemAudioExclusive::Instance().Release(this);
    m_bar.SetSystemAudioOn(m_audio.IsSystemAudioEnabled());
    reportMedia();
    if (on && !ok)
        showError(TR(L"无法采集电脑声音，请检查播放设备。"));
}

void CShareController::OnSystemAudioPreempted()
{
    //通话开始共享电脑声音：屏幕共享这一路停下（2026-10-06）
    if (!m_audio.IsRunning() || !m_audio.IsSystemAudioEnabled())
        return;
    m_audio.SetSystemAudioEnabled(false);
    if (m_hostPhase == host_streaming)
        m_bar.SetSystemAudioOn(false);
    reportMedia();
    showNotice(TR(L"电脑声音已改由通话发送"));
    LOG_INFO("ShareController: system audio stopped, the call took over.");
}

void CShareController::reportMedia()
{
    const std::string& roomId = currentRoomId();
    if (roomId.empty())
        return;
    send(cmd_screenshare_member_media,
         ShareProtocolClient::buildMemberMedia(roomId, m_audio.IsMicEnabled(), m_cameraSender.IsRunning(),
                                               m_audio.IsSystemAudioEnabled()));
}

void CShareController::applyMembers(const ShareStateNotify& n, bool asHost)
{
    //白板的建立者：须在下面处理标注权限之前交给标注会话
    syncBoardOwners(n);
    std::map<int, int64_t> seats;
    for (size_t i = 0; i < n.m_members.size(); ++i)
        seats[n.m_members[i].m_seat] = n.m_members[i].m_userId;
    //离开了的座位：删掉这个人的声音路，免得他的最后几帧一直留在混音器里
    for (std::map<int, int64_t>::const_iterator it = m_seatUsers.begin(); it != m_seatUsers.end(); ++it)
    {
        if (seats.find(it->first) == seats.end())
        {
            m_audio.RemoveSeat(it->first);
            //实时字幕：清掉离开的人还显示着的字幕
            m_caption.OnSeatLeft(it->first);
            //别人画笔位置旁的名字（第 19 期）：离开的人删掉
            m_annot.SetSeatName(it->first, std::wstring());
        }
    }
    m_seatUsers = seats;
    for (std::map<int, int64_t>::const_iterator it = m_seatUsers.begin(); it != m_seatUsers.end(); ++it)
        m_annot.SetSeatName(it->first, displayName(it->second));
    //摄像头：关掉了摄像头的人、已经离开房间的人，删掉他的解码器与最后一帧。
    //只按「上一次是开着的」判断关掉，不按「这一次没开」删：刚打开摄像头的人，画面可能比服务端广播的 1725 先到，
    //其间若插进一条别的 1725，按后者删会把他刚建好的解码器删掉，要等下一个关键帧才能重新出画面
    std::set<int> cameraSeats;
    for (size_t i = 0; i < n.m_members.size(); ++i)
    {
        if (n.m_members[i].m_camera && n.m_members[i].m_seat != m_selfSeat)
            cameraSeats.insert(n.m_members[i].m_seat);
    }
    for (std::set<int>::const_iterator it = m_cameraSeats.begin(); it != m_cameraSeats.end(); ++it)
    {
        if (cameraSeats.find(*it) == cameraSeats.end())
            m_cameraReceiver.RemoveSeat(*it);
    }
    //开摄像头被服务端拒绝（超过上限）的人不会出现在 m_cameraSeats 里，他发过的几帧建起的解码器在他离开时删掉
    const std::vector<int> decoding = m_cameraReceiver.Seats();
    for (size_t i = 0; i < decoding.size(); ++i)
    {
        if (seats.find(decoding[i]) == seats.end())
            m_cameraReceiver.RemoveSeat(decoding[i]);
    }
    m_cameraSeats = cameraSeats;
    refreshCameraTiles();
    //录制：谁在录（不含本人）；观看者被收回录制权限时停止本人的录制（服务端已把本人的录制状态置为停止）
    std::wstring recordingText;
    for (size_t i = 0; i < n.m_members.size(); ++i)
    {
        if (!n.m_members[i].m_recording || n.m_members[i].m_seat == m_selfSeat)
            continue;
        if (!recordingText.empty())
            recordingText += L"、";
        recordingText += displayName(n.m_members[i].m_userId);
    }
    if (!recordingText.empty())
        recordingText = TRF(L"{0} 正在录制", recordingText);   //{0} 为上面拼好的名字列表
    if (recordingText != m_recordingText && n.m_event == share_event_record_changed && !recordingText.empty())
        showNotice(recordingText.c_str());
    m_recordingText = recordingText;
    //经移交成为主持人的人（2026-10-07）：权限是自己改的，不受「他人录制 / 标注」权限约束，也不提示
    const bool roomHost = asHost || isRoomHost();
    if (asHost)
    {
        m_bar.SetRecordAllowed(n.m_recordAllowed);
    }
    else if (roomHost)
    {
        m_recordAllowed = n.m_recordAllowed;
    }
    else
    {
        const bool wasRecordAllowed = m_recordAllowed;
        m_recordAllowed = n.m_recordAllowed;
        if (wasRecordAllowed && !m_recordAllowed && m_shareRecording)
        {
            LOG_INFO("screenshare: record permission revoked, stopping own recording, room=%s.", n.m_roomId.c_str());
            if (m_recorder != NULL)
                m_recorder->StopRecording();
            showNotice(TR(L"发起人已收回录制权限，录制已停止并保存"));
        }
    }
    //标注权限：观看者被收回时退出标注模式（已有笔迹保留）；发起人的工具条刷新权限按钮
    const bool wasAllowed = m_annot.Model().ViewersAllowed();
    m_annot.SetViewersAllowed(n.m_annotateAllowed);
    if (roomHost)
    {
        m_annotBar.SetViewersAllowed(n.m_annotateAllowed);
    }
    else if (wasAllowed && !n.m_annotateAllowed)
    {
        //正在自己建的白板上标注时不退出（2026-10-07，方案第 14.1 节 ④-1：建立者在自己的白板上仍能画）
        const bool keepOnOwnBoard = m_annotOnBoard && m_myBoardSeat >= 0 && m_annot.CanDrawOn(m_myBoardSeat);
        if (m_annotMode && !keepOnOwnBoard)
            setAnnotMode(false);
        showNotice(keepOnOwnBoard ? TR(L"发起人已收回标注权限，你仍可以在自己的白板上标注") : TR(L"发起人已收回标注权限"));
        annotationChanged();
    }
    else if (!wasAllowed && n.m_annotateAllowed)
    {
        showNotice(TR(L"发起人已允许标注"));
    }
    //发起人单独静音了本人（第 13 期）：服务端已把本人的麦克风状态置为关闭，本地照做，之后可以自己重新打开
    if (!roomHost && ShouldMuteSelfByHost(n.m_event, n.m_target, m_host->GetSelfUserId(), m_audio.IsMicEnabled()))
    {
        m_audio.SetMicEnabled(false);
        m_viewer.SetMicOn(false);
        LOG_INFO("screenshare: muted individually by host, room=%s.", n.m_roomId.c_str());
        m_notice = TR(L"发起人已将你静音");
        m_noticeUntilMs = static_cast<int64_t>(::GetTickCount64()) + kNoticeMs;
        refreshSpeaking();
    }
    //发起人执行了全员静音：服务端已把本人的麦克风状态置为关闭，本地照做（被静音的人之后可以自己重新打开）
    if (!roomHost && n.m_event == share_event_muted_all && m_audio.IsMicEnabled())
    {
        const ShareMember* self = n.findMember(m_host->GetSelfUserId());
        if (self != NULL && !self->m_mic)
        {
            m_audio.SetMicEnabled(false);
            m_viewer.SetMicOn(false);
            LOG_INFO("screenshare: muted by host, room=%s.", n.m_roomId.c_str());
            m_notice = TR(L"发起人已将全员静音");
            m_noticeUntilMs = static_cast<int64_t>(::GetTickCount64()) + kNoticeMs;
            refreshSpeaking();
        }
    }
}

void CShareController::refreshSpeaking()
{
    const int64_t now = static_cast<int64_t>(::GetTickCount64());
    std::wstring text;
    if (!m_notice.empty() && now < m_noticeUntilMs)
    {
        text = m_notice;
    }
    else
    {
        m_notice.clear();
        const std::vector<int> seats = m_audio.UpdateSpeaking(now);
        updateRecordPip(seats);
        m_speakingSeats = std::set<int>(seats.begin(), seats.end());
        refreshViewerList();
        for (size_t i = 0; i < seats.size(); ++i)
        {
            std::wstring name;
            if (seats[i] == m_selfSeat)
            {
                name = TR(L"我");
            }
            else
            {
                std::map<int, int64_t>::const_iterator it = m_seatUsers.find(seats[i]);
                name = (it != m_seatUsers.end()) ? displayName(it->second) : std::wstring(TR(L"有人"));
            }
            if (!text.empty())
                text += L"、";
            text += name;
        }
        if (!text.empty())
            text = TRF(L"{0} 正在说话", text);   //{0} 为上面拼好的名字列表
        //有人在录制时一直提示
        if (!m_recordingText.empty())
        {
            if (!text.empty())
                text += L" · ";
            text += m_recordingText;
        }
    }
    if (m_hostPhase == host_streaming)
        m_bar.SetSpeakingText(text);
    if (m_viewerPhase == viewer_watching)
        m_viewer.SetSpeakingText(text);
}

const std::string& CShareController::currentRoomId() const
{
    if (m_hostPhase == host_streaming || m_hostPhase == host_waiting)
        return m_hostRoomId;
    return m_viewerRoomId;
}

std::wstring CShareController::displayName(int64_t userId)
{
    std::map<int64_t, std::wstring>::const_iterator it = m_nameCache.find(userId);
    if (it != m_nameCache.end())
        return it->second;
    wchar_t buf[kNameChars] = { 0 };
    std::wstring name;
    if (m_host != NULL && m_host->GetUserDisplayName(userId, buf, kNameChars) && buf[0] != L'\0')
        name = buf;
    else
        name = TRF(L"用户{0}", static_cast<long long>(userId));
    m_nameCache[userId] = name;
    return name;
}

bool CShareController::GetCameraFrame(int seat, bool self, std::vector<uint8_t>& bgra, int& width, int& height,
                                      uint64_t& serial)
{
    if (self)
        return m_cameraSender.GetPreview(bgra, width, height, serial);
    return m_cameraReceiver.GetFrame(seat, bgra, width, height, serial);
}

void CShareController::setCamera(bool on)
{
    ShareAudioTransport* transport = NULL;
    if (m_hostPhase == host_streaming)
        transport = &m_streamer;
    else if (m_viewerPhase == viewer_watching)
        transport = m_viewer.AudioTransport();
    if (transport == NULL)
        return;
    //通话中共享：摄像头归通话使用，房间里不另开（两边同时打开同一个摄像头会失败）
    if (on && inCallShare())
    {
        applyCameraButton();
        showNotice(TR(L"通话中请在通话窗口里开关摄像头"));
        return;
    }
    int failReason = call::kCameraFailNone;
    if (on && !m_cameraSender.IsRunning())
    {
        if (!m_cameraSender.Start(transport, m_selfSeat))
            failReason = m_cameraSender.FailReason();
    }
    else if (!on)
    {
        m_cameraSender.Stop();
    }
    LOG_INFO("screenshare: camera %s, room=%s.", m_cameraSender.IsRunning() ? "on" : "off", currentRoomId().c_str());
    applyCameraButton();
    reportMedia();
    refreshCameraTiles();
    if (on && !m_cameraSender.IsRunning())
    {
        showError(failReason == call::kCameraFailNoDevice
                      ? TR(L"没有找到摄像头，请检查摄像头是否接好、系统隐私设置是否允许使用摄像头。")
                      : TR(L"无法打开摄像头，它可能正被其他程序使用。"));
    }
}

void CShareController::applyCameraButton()
{
    const bool on = m_cameraSender.IsRunning();
    if (m_hostPhase == host_streaming)
        m_bar.SetCameraOn(on);
    if (m_viewerPhase == viewer_watching)
        m_viewer.SetCameraOn(on);
}

void CShareController::refreshCameraTiles()
{
    //本人在最前，其他人按座位号（发起人座位 0 排在观看者前面）；同屏最多 kShareMaxCameraTiles 个，多出的只计人数
    std::map<int, std::wstring> names;
    for (std::set<int>::const_iterator it = m_cameraSeats.begin(); it != m_cameraSeats.end(); ++it)
    {
        std::map<int, int64_t>::const_iterator user = m_seatUsers.find(*it);
        if (user != m_seatUsers.end())
            names[*it] = displayName(user->second);
    }
    std::vector<ShareCameraTile> tiles;
    const int overflow = BuildCameraTiles(m_cameraSender.IsRunning(), m_selfSeat, m_cameraSeats, names,
                                          kShareMaxCameraTiles, tiles);
    if (m_hostPhase == host_streaming)
        m_cameraWindow.Update(tiles, overflow, hostSourceRect());
    if (m_viewerPhase == viewer_watching)
        m_viewer.SetCameraTiles(tiles, overflow);
}

void CShareController::switchHostSource(const ShareSourceSpec& spec)
{
    m_hostSource = spec;
    m_streamer.SwitchSource(m_hostSource);
    const bool monitor = (m_hostSource.m_kind == share_source_monitor);
    const RECT rect = monitor ? m_hostSource.m_monitorRect : GetShareWindowBounds(m_hostSource.m_hwnd);
    m_border.Show(rect, monitor);
    m_bar.MoveToSource(rect);
    //「屏幕当白板」只在共享一个窗口时可用（第 23 期）
    m_bar.SetScreenBoard(!monitor || m_screenBoard, m_screenBoard);
    refreshCameraTiles();
    //切换共享内容时清空标注（docs/屏幕共享与录制方案.md 5.9），标注层与工具条挪到新的内容上
    m_annot.ClearAll();
    m_annotOverlay.Place(rect, true);
    //工具条按当前的锚点挪：在本人的白板上标注时锚在白板上
    if (m_annotMode)
        m_annotBar.MoveTo(annotAnchorRect());
    annotationChanged();
    LOG_INFO("screenshare: switched source, kind=%d.", m_hostSource.m_kind);
}

RECT CShareController::hostSourceRect() const
{
    if (m_hostSource.m_kind == share_source_monitor)
        return m_hostSource.m_monitorRect;
    return GetShareWindowBounds(m_hostSource.m_hwnd);
}

//==================== 标注 ====================

void CShareController::OnAnnotationFrame(int senderSeat, const std::string& payload)
{
    //中转接收线程上：拷贝一份转投 UI 线程；消息窗口已销毁时直接丢弃
    if (!IsWindow())
        return;
    ShareAnnotationFrame* frame = new ShareAnnotationFrame();
    frame->m_seat = senderSeat;
    frame->m_payload = payload;
    if (!PostMessage(kMsgShareAnnotFrame, 0, reinterpret_cast<LPARAM>(frame)))
        delete frame;
}

LRESULT CShareController::OnAnnotFrameMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)wParam;
    (void)bHandled;
    std::unique_ptr<ShareAnnotationFrame> frame(reinterpret_cast<ShareAnnotationFrame*>(lParam));
    //带本机时刻：激光笔据此淡出、画笔位置据此过期（第 19 期）
    if (frame && m_annot.OnRemoteFrame(frame->m_seat, frame->m_payload, static_cast<int64_t>(::GetTickCount64())))
        annotationChanged();
    return 0;
}

void CShareController::OnAnnotPointer(int phase, const AnnotationPoint& pt, int tolerance, double aspect, POINT screenPt)
{
    if (!m_annotMode || !m_annot.IsRunning())
        return;
    //共享内容显示的宽高比（第 20 期）：选择、连接线裁边按它换算
    m_annot.SetContentAspect(aspect);
    const int64_t now = static_cast<int64_t>(::GetTickCount64());
    //按住 Shift：形状画成正形、直线与箭头吸附 45 度（第 19 期）。键盘焦点通常在被共享的程序上，取异步的按键状态（2026-10-07）
    m_annot.SetShiftDown((::GetAsyncKeyState(VK_SHIFT) & kKeyDownBit) != 0);
    //光标离开标注区域：告诉房间里的人不再显示本人的画笔
    if (phase == annot_pointer_leave)
    {
        m_annotHoverValid = false;
        m_annot.PointerLeave();
        return;
    }
    //记下光标位置：选择工具按它决定光标是四向移动还是十字（2026-10-07）
    m_annotHoverPt = pt;
    m_annotHoverTol = tolerance;
    m_annotHoverValid = true;
    //「鼠标」状态（2026-10-07）：不画。白板画布等仍会交来鼠标动作，这里一律不处理
    if (m_annotPointer)
        return;
    //画笔位置：悬停、按下、拖动、松开都广播（会话内部限频）
    m_annot.PointerHover(pt, now);
    //文字工具：按下处弹出输入框，确定后才添加
    if (m_annot.Tool() == annot_tool_text)
    {
        if (phase == annot_pointer_down && m_annot.CanDraw())
        {
            m_textPoint = pt;
            m_textCanvas = m_annot.Canvas();
            m_editRef = AnnotationRef();
            m_textDialog.Open(screenPt, m_hWnd, m_hostPhase == host_streaming);
        }
        return;
    }
    AnnotationStroke pending;
    switch (phase)
    {
    //悬停：只有折线、曲线在添加顶点时要重画预览
    case annot_pointer_hover:
        if (!m_annot.PendingShape(pending))
            return;
        break;
    //按下
    case annot_pointer_down:
        m_annot.PointerDown(pt, tolerance, now);
        break;
    //拖动
    case annot_pointer_move:
        m_annot.PointerMove(pt, tolerance, now);
        break;
    //松开
    case annot_pointer_up:
        m_annot.PointerUp(pt, now);
        break;
    //双击或右键：完成折线、曲线；选择工具下修改点中对象的文字（第 20 期）
    case annot_pointer_finish:
        if (m_annot.Tool() == kAnnotSelect)
        {
            openObjectTextEdit(pt, tolerance, screenPt);
            return;
        }
        m_annot.FinishPending(now);
        break;
    //未知动作：忽略
    default:
        return;
    }
    annotationChanged();
}

HCURSOR CShareController::AnnotCursor()
{
    const int tool = m_annot.Tool();
    const uint32_t color = m_annot.Color();
    //「鼠标」状态（2026-10-07）：系统箭头（共享光标，不缓存、不释放）
    if (m_annotPointer)
        return ::LoadCursor(NULL, IDC_ARROW);
    //选择工具（第 20 期）：2026-10-07 起悬停在对象上为四向移动光标，空白处为十字（可以框选）；系统光标不缓存、不释放
    if (tool == kAnnotSelect)
        return ::LoadCursor(NULL, AnnotSelectCursorName(m_annotHoverValid &&
                                                         m_annot.ObjectAt(m_annotHoverPt, m_annotHoverTol)));
    if (m_annotCursor != NULL && tool == m_annotCursorTool && color == m_annotCursorColor)
        return m_annotCursor;
    //工具或颜色变了：按新的样子重建（光标边长随系统光标大小）
    if (m_annotCursor != NULL)
    {
        ::DestroyIcon(m_annotCursor);
        m_annotCursor = NULL;
    }
    m_annotCursor = CreateAnnotationCursor(tool, color);
    m_annotCursorTool = tool;
    m_annotCursorColor = color;
    if (m_annotCursor == NULL)
        LOG_WARNING("screenshare: create annotation cursor failed, tool=%d.", tool);
    return m_annotCursor;
}

LRESULT CShareController::OnAnnotTextMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)wParam;
    (void)bHandled;
    std::unique_ptr<std::wstring> text(reinterpret_cast<std::wstring*>(lParam));
    //双击修改已有对象的文字（第 20 期）：取消时 text 为空，不改
    const AnnotationRef editRef = m_editRef;
    m_editRef = AnnotationRef();
    if (editRef.IsSet())
    {
        if (text && m_annot.IsRunning() && m_annot.SetObjectText(editRef, wideToUtf8(*text)))
            annotationChanged();
        return 0;
    }
    //文字加到按下时所在的那块画面上（第 22 期 22e）：输入期间鼠标可能已移到别的画面
    if (text && !text->empty() && m_annot.IsRunning())
    {
        m_annot.SetCanvas(m_textCanvas);
        if (m_annot.AddText(m_textPoint, wideToUtf8(*text)))
            annotationChanged();
    }
    return 0;
}

void CShareController::openObjectTextEdit(const AnnotationPoint& pt, int tolerance, POINT screenPt)
{
    AnnotationRef ref;
    std::string current;
    if (!m_annot.CanDraw() || !m_annot.EditableAt(pt, tolerance, ref, current))
        return;
    const AnnotationStroke* s = m_annot.Model().Find(ref);
    if (s == NULL)
        return;
    m_editRef = ref;
    m_textDialog.Open(screenPt, m_hWnd, m_hostPhase == host_streaming,
                      MakeAnnotTextEditSpec(s->m_tool, utf8ToWide(current)));
}

bool CShareController::OnAnnotKey(UINT vk)
{
    if (!m_annotMode || !m_annot.IsRunning() || m_annot.Selection().empty())
        return false;
    //Delete：删除选中的对象
    if (vk == VK_DELETE)
    {
        if (m_annot.DeleteSelection())
            annotationChanged();
        return true;
    }
    //Esc：取消选择
    if (vk == VK_ESCAPE)
    {
        m_annot.ClearSelection();
        annotationChanged();
        return true;
    }
    return false;
}

LRESULT CShareController::OnAnnotCommandMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)bHandled;
    if (!m_annot.IsRunning())
        return 0;
    //工具、颜色与各项样式：记进状态后整体交给标注会话，再同步回工具条
    AnnotMenuChoice choice;
    choice.m_command = static_cast<int>(wParam);
    choice.m_value = static_cast<int>(lParam);
    //选择工具下有选中对象（第 20 期）：样式类命令改到选中的对象上，工具保持为选择工具
    int restyleAttr = 0;
    int restyleValue = 0;
    if (m_annot.Tool() == kAnnotSelect && !m_annot.Selection().empty() &&
        ApplyAnnotChoiceInSelectMode(choice, m_annotState, restyleAttr, restyleValue))
    {
        m_annot.RestyleSelection(restyleAttr, restyleValue);
        m_annotBar.SetState(m_annotState);
        annotationChanged();
        return 0;
    }
    //选了工具（或会切到某个工具的样式）：从「鼠标」状态回到画图（2026-10-07）
    if (m_annotPointer && AnnotChoiceLeavesPointer(choice))
        setAnnotPointer(false);
    if (ApplyAnnotChoice(choice, m_annotState))
    {
        const int oldTool = m_annot.Tool();
        ApplyAnnotBarState(m_annotState, m_annot);
        m_annotBar.SetState(m_annotState);
        //换了工具：重画标注层（光标在鼠标回到标注层时由 AnnotCursor 按新工具重建）
        if (m_annot.Tool() != oldTool)
            annotationChanged();
        return 0;
    }
    switch (choice.m_command)
    {
    //数字标号从头编号
    case annot_bar_badge_reset:
        m_annot.ResetBadgeNumber();
        break;
    //删除选中的对象（第 20 期）
    case annot_bar_delete:
        if (m_annot.DeleteSelection())
            annotationChanged();
        break;
    //撤销本人最近的一笔
    case CShareAnnotationBar::kCmdUndo:
        if (m_annot.UndoOwn())
            annotationChanged();
        break;
    //清空所有人的笔迹（发起人）
    case CShareAnnotationBar::kCmdClear:
        if (m_annot.ClearAll())
        {
            LOG_INFO("screenshare: annotations cleared by host, room=%s.", currentRoomId().c_str());
            annotationChanged();
        }
        break;
    //发起人切换他人的标注权限：经 1730 告诉服务端，结果以 1725 的广播为准
    case CShareAnnotationBar::kCmdPermission:
        if (isRoomHost())
        {
            send(cmd_screenshare_set_permission,
                 ShareProtocolClient::buildSetPermission(currentRoomId(), lParam != 0 ? share_perm_allow : share_perm_revoke,
                                                         share_perm_unchanged, false));
            LOG_INFO("screenshare: annotate permission -> %d, room=%s.", static_cast<int>(lParam),
                     currentRoomId().c_str());
        }
        break;
    //退出标注模式
    case CShareAnnotationBar::kCmdExit:
        setAnnotMode(false);
        break;
    //「鼠标」（2026-10-07）：1 进入「鼠标」状态，0 回到画图
    case annot_bar_pointer:
        setAnnotPointer(lParam != 0);
        break;
    //保存为图片（第 23 期）：白板导出整张画布，共享画面导出当前画面加笔迹
    case CShareAnnotationBar::kCmdSave:
        saveAnnotImage();
        break;
    //未知命令：忽略
    default:
        break;
    }
    return 0;
}

void CShareController::setAnnotMode(bool on)
{
    const bool hosting = (m_hostPhase == host_streaming);
    const bool watching = (m_viewerPhase == viewer_watching);
    //共享者在自己共享的内容上标注（第 22 期 22e），而不是在观看窗里
    const bool ownScreen = m_annotOwnScreen && m_publishPhase == share_publish_streaming;
    //在本人的白板窗口里标注（第 23 期）
    const bool onBoard = m_annotOnBoard && m_boardWindow.IsOpen();
    //主持人在「其他人的屏幕」窗口里对别人的画面标注（2026-10-07，方案第 14.3 节第 4 条）
    const bool onScreens = m_annotOnScreens && hosting && m_screensWindow.IsOpen();
    if (!hosting && !watching)
        on = false;
    //在本人的白板上标注时按白板判断（2026-10-07：权限关闭后建立者在自己的白板上仍能画）；画面要到落笔时才切过去
    if (on && !(onBoard ? m_annot.CanDrawOn(m_myBoardSeat) : m_annot.CanDraw()))
        on = false;
    m_annotMode = on;
    //进入或退出标注模式都从画图开始，不沿用「鼠标」状态（2026-10-07）
    m_annotPointer = false;
    m_annotBar.SetPointerMode(false);
    if (!on)
    {
        m_annot.CancelPointer();
        m_annot.ClearSelection();
        m_editRef = AnnotationRef();
        m_textDialog.Close();
    }
    //各输入面开不开、工具条锚在哪里（2026-10-07）：在本人的白板上标注时不打开任何屏幕输入层，否则这层几乎透明的置顶窗口
    //盖住整块屏幕，白板窗口的标题栏被压在下面拖不动
    const ShareAnnotSurfacePlan plan = DecideAnnotSurfaces(on, hosting, watching, ownScreen, onBoard, false, onScreens);
    if (hosting)
    {
        m_annotOverlay.SetInputMode(plan.m_hostOverlayInput, this);
        m_screensWindow.SetAnnotating(on && plan.m_anchor == annot_anchor_screens, this);
        m_bar.SetAnnotating(on);
        //输入层刚显示在最上面：把控制条、摄像头浮动窗口提到它上面，否则点不到（标注工具条随后显示，自然在最上面）
        if (plan.m_hostOverlayInput)
        {
            m_bar.MoveToSource(hostSourceRect());
            refreshCameraTiles();
        }
    }
    if (watching && !ownScreen && !onBoard)
        m_viewer.SetAnnotating(on);
    if (ownScreen)
    {
        m_publishOverlay.SetInputMode(plan.m_publishOverlayInput, this);
        m_sharerBar.SetAnnotating(on);
        //输入层刚显示在最上面：把共享者控制条提到它上面，否则点不到
        if (plan.m_publishOverlayInput)
            m_sharerBar.Show(ownSourceRect(), m_hWnd);
    }
    if (on)
    {
        m_annotBar.SetState(m_annotState);
        m_annotBar.SetViewersAllowed(m_annot.Model().ViewersAllowed());
        m_annotBar.SetDrawEnabled(true);
        //经移交成为主持人的人（2026-10-07）同样是主持人工具条：多出「清空」与权限按钮
        m_annotBar.Show(annotAnchorRect(), m_hWnd, hosting || isRoomHost());
        //工具条浮在共享者自己共享的内容上，默认不能被采进画面；「允许截图」打开时（2026-10-08）允许
        if (ownScreen && m_annotBar.IsWindow())
            ApplyBarCaptureState(m_annotBar.m_hWnd);
    }
    else
    {
        m_annotBar.Hide();
        m_annotOwnScreen = false;
        m_annotOnBoard = false;
        m_annotOnScreens = false;
    }
    //在白板上：标注会话切到这块白板，线宽按白板的世界缩小（第 23 期）
    if (on && onBoard)
        OnAnnotCanvas(m_myBoardSeat);
    annotationChanged();
    LOG_INFO("screenshare: annotation mode %s, room=%s.", on ? "on" : "off", currentRoomId().c_str());
}

void CShareController::setAnnotPointer(bool on)
{
    if (!m_annotMode)
        on = false;
    m_annotBar.SetPointerMode(on);
    if (on == m_annotPointer)
        return;
    m_annotPointer = on;
    if (on)
    {
        //正在画的一笔、正在拖的形状收尾，免得停在「按着」的状态
        m_annot.CancelPointer();
    }
    const bool hosting = (m_hostPhase == host_streaming);
    const bool watching = (m_viewerPhase == viewer_watching);
    const bool ownScreen = m_annotOwnScreen && m_publishPhase == share_publish_streaming;
    const bool onBoard = m_annotOnBoard && m_boardWindow.IsOpen();
    const bool onScreens = m_annotOnScreens && hosting && m_screensWindow.IsOpen();
    const ShareAnnotSurfacePlan plan = DecideAnnotSurfaces(m_annotMode, hosting, watching, ownScreen, onBoard, on,
                                                           onScreens);
    //主持人在「其他人的屏幕」窗口里标注（2026-10-07）：那个窗口的画面不再拦截标注输入
    if (onScreens && !onBoard)
        m_screensWindow.SetAnnotPointer(on);
    if (hosting && !onBoard)
    {
        m_annotOverlay.SetInputMode(plan.m_hostOverlayInput, this);
        //输入层重新盖上来、到了置顶窗口的最上面：把控制条与摄像头浮动窗口提到它上面
        if (plan.m_hostOverlayInput)
        {
            m_bar.MoveToSource(hostSourceRect());
            refreshCameraTiles();
        }
    }
    //观看者：画面控件不再拦截标注输入（标注模式、按钮与图标不变）
    if (watching && !ownScreen && !onBoard)
        m_viewer.SetAnnotPointer(on);
    if (ownScreen)
    {
        m_publishOverlay.SetInputMode(plan.m_publishOverlayInput, this);
        if (plan.m_publishOverlayInput)
            m_sharerBar.Show(ownSourceRect(), m_hWnd);
    }
    //白板画布总把鼠标动作交来，「鼠标」状态下由 OnAnnotPointer 不处理；工具条提到最上面，免得被输入层压住
    if (m_annotBar.IsWindow())
        ::SetWindowPos(m_annotBar.m_hWnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    annotationChanged();
    LOG_INFO("screenshare: annotation pointer mode %s, room=%s.", on ? "on" : "off", currentRoomId().c_str());
}

RECT CShareController::annotAnchorRect() const
{
    //锚点与 setAnnotMode 用同一个策略（2026-10-07）：在本人的白板上标注时锚在白板窗口上，主持人也不例外
    const ShareAnnotSurfacePlan plan = DecideAnnotSurfaces(true, m_hostPhase == host_streaming,
                                                           m_viewerPhase == viewer_watching,
                                                           m_annotOwnScreen && m_publishPhase == share_publish_streaming,
                                                           m_annotOnBoard && m_boardWindow.IsOpen(), false,
                                                           m_annotOnScreens && m_hostPhase == host_streaming
                                                               && m_screensWindow.IsOpen());
    RECT rc = { 0, 0, 0, 0 };
    switch (plan.m_anchor)
    {
    //主持人在「其他人的屏幕」窗口里标注（2026-10-07）
    case annot_anchor_screens:
        return m_screensWindow.WindowRect();
    //在本人的白板窗口里标注（第 23 期）
    case annot_anchor_board:
        return m_boardWindow.WindowRect();
    //主持人在共享的内容上标注
    case annot_anchor_host_source:
        return hostSourceRect();
    //共享者在自己共享的内容上标注（第 22 期 22e）
    case annot_anchor_own_source:
        return ownSourceRect();
    //观看者在观看窗里标注
    case annot_anchor_viewer:
        if (m_viewer.IsWindow())
            ::GetWindowRect(m_viewer.m_hWnd, &rc);
        return rc;
    //不在推流也不在观看：没有锚点
    default:
        return rc;
    }
}

void CShareController::annotationChanged()
{
    if (m_shareRecording && m_recorder != NULL)
        pushRecordAnnotations();
    if (m_hostPhase == host_streaming)
        m_annotOverlay.Redraw();
    //共享者本机的标注层（第 22 期 22e）
    if (m_publishPhase == share_publish_streaming)
        m_publishOverlay.Redraw();
    //本人的白板窗口与主持人的「其他人的屏幕」窗口（第 23 期）
    if (m_boardWindow.IsOpen())
        m_boardWindow.Redraw();
    m_screensWindow.AnnotationChanged();
    if (m_viewerPhase == viewer_watching)
        m_viewer.AnnotationChanged();
}

void CShareController::showNotice(const wchar_t* text)
{
    m_notice = text;
    m_noticeUntilMs = static_cast<int64_t>(::GetTickCount64()) + kNoticeMs;
    refreshSpeaking();
}

bool CShareController::IsLocalShareBusy() const
{
    return m_hostPhase != host_idle || m_publishPhase == share_publish_picking ||
           m_publishPhase == share_publish_streaming;
}

bool CShareController::rejectIfRecording()
{
    const bool recordBusy = (m_recorder != NULL && m_recorder->IsStandaloneBusy());
    const int conflict = CheckScreenStartConflict(screen_start_share, recordBusy, false);
    if (conflict == screen_start_ok)
        return false;
    LOG_INFO("screenshare: start rejected, a standalone recording is in progress.");
    m_noticeToast.ShowNearActiveWindow(ScreenStartConflictText(conflict), flamingomedia::media_toast_error);
    return true;
}

void CShareController::showBarToast(const wchar_t* text, bool error)
{
    const int style = error ? flamingomedia::media_toast_error : flamingomedia::media_toast_info;
    //控制条显示着（含收起成小把手）：贴在它当前的窗口矩形下方
    if (m_bar.IsWindow() && m_bar.IsWindowVisible())
    {
        RECT rc;
        m_bar.GetWindowRect(&rc);
        m_noticeToast.Show(rc, flamingomedia::media_toast_below, text, style);
        return;
    }
    m_noticeToast.ShowNearActiveWindow(text, style);
}

//==================== 共享录制 ====================

CShareController::HostRecordSource::HostRecordSource(ShareHostStreamer* streamer)
    : m_streamer(streamer)
{
}

bool CShareController::HostRecordSource::GetRecordFrame(std::vector<uint8_t>& bgra, int& width, int& height,
                                                          uint64_t& serial)
{
    return m_streamer->GetRecordFrame(bgra, width, height, serial);
}

CShareController::ViewerRecordSource::ViewerRecordSource(CShareViewerWindow* viewer)
    : m_viewer(viewer)
{
}

bool CShareController::ViewerRecordSource::GetRecordFrame(std::vector<uint8_t>& bgra, int& width, int& height,
                                                            uint64_t& serial)
{
    return m_viewer->GetRecordFrame(bgra, width, height, serial);
}

CShareController::SpeakerCameraSource::SpeakerCameraSource(ShareCameraSender* sender, ShareCameraReceiver* receiver)
    : m_sender(sender)
    , m_receiver(receiver)
    , m_seat(-1)
    , m_self(false)
{
}

void CShareController::SpeakerCameraSource::SetSpeaker(int seat, bool self)
{
    m_seat.store(seat);
    m_self.store(self);
}

bool CShareController::SpeakerCameraSource::GetRecordFrame(std::vector<uint8_t>& bgra, int& width, int& height,
                                                             uint64_t& serial)
{
    if (m_self.load())
        return m_sender->GetPreview(bgra, width, height, serial);
    const int seat = m_seat.load();
    if (seat < 0)
        return false;
    return m_receiver->GetFrame(seat, bgra, width, height, serial);
}

void CShareController::SetRecorder(screenrecord::CRecordController* recorder)
{
    m_recorder = recorder;
}

//==================== 通话中共享（第 8 期）====================

void CShareController::SetCallObserver(call::ICallShareObserver* observer)
{
    m_callObserver = observer;
}

void CShareController::OnCallConnected(int64_t peerId, const std::wstring& peerName)
{
    m_callPeerId = peerId;
    m_callPeerName = peerName;
    LOG_INFO("screenshare: call connected with %lld, in-call share available.", static_cast<long long>(peerId));
    notifyCallObserver();
}

void CShareController::OnCallEnded()
{
    if (m_callPeerId == 0)
        return;
    LOG_INFO("screenshare: call with %lld ended, hosting=%d, viewing=%d.", static_cast<long long>(m_callPeerId),
             m_hostPhase != host_idle ? 1 : 0, m_viewerPhase != viewer_idle ? 1 : 0);
    m_callPeerId = 0;
    m_callPeerName.clear();
}

void CShareController::ToggleShareInCall()
{
    if (m_callPeerId == 0)
    {
        LOG_WARNING("screenshare: share button clicked without a connected call, ignored.");
        return;
    }
    const bool hostingWithPeer = (m_hostPhase != host_idle) && IsInCallShare(m_callPeerId, m_hostCtype, m_hostCid);
    switch (DecideInCallShareClick(hostingWithPeer, m_hostPhase != host_idle, m_viewerPhase != viewer_idle))
    {
    //没有在共享：发起与通话对方的单聊共享，先选内容
    case in_call_share_start:
        StartShare(conversation_single, m_callPeerId, m_callPeerName);
        break;
    //正在与通话对方共享：结束它。还在选内容时只关选择窗；1720 还没应答时房间号未知，等应答后再点
    case in_call_share_stop:
        if (m_hostPhase == host_picking)
        {
            stopHostLocal();
        }
        else if (m_hostPhase == host_waiting || m_hostPhase == host_streaming)
        {
            send(cmd_screenshare_stop, ShareProtocolClient::buildRoomRequest(m_hostRoomId));
            LOG_INFO("screenshare: stopped from call window, room=%s.", m_hostRoomId.c_str());
            stopHostLocal();
        }
        else
        {
            LOG_INFO("screenshare: stop from call window ignored while waiting for start result.");
        }
        break;
    //正在别的会话里共享
    case in_call_share_busy_hosting:
        showError(TR(L"你正在别的会话里共享屏幕，请先结束那一场共享。"));
        break;
    //正在观看共享
    case in_call_share_busy_viewing:
        showError(TR(L"你正在观看屏幕共享，请先离开后再发起。"));
        break;
    //未知动作：忽略
    default:
        break;
    }
}

bool CShareController::inCallShare() const
{
    if (m_hostPhase != host_idle)
        return IsInCallShare(m_callPeerId, m_hostCtype, m_hostCid);
    //观看单聊共享时 m_viewerCid 是从本人角度看的会话 id，即发起人
    if (m_viewerPhase != viewer_idle)
        return IsInCallShare(m_callPeerId, m_viewerCtype, m_viewerCid);
    return false;
}

void CShareController::notifyCallObserver()
{
    if (m_callObserver == NULL)
        return;
    m_callObserver->OnInCallShareChanged((m_hostPhase != host_idle) &&
                                         IsInCallShare(m_callPeerId, m_hostCtype, m_hostCid));
}

void CShareController::startShareRecording()
{
    if (m_shareRecording)
        return;
    if (m_recorder == NULL)
    {
        showError(TR(L"屏幕录制组件不可用。"));
        return;
    }
    const bool hosting = (m_hostPhase == host_streaming);
    const bool watching = (m_viewerPhase == viewer_watching);
    if (!hosting && !watching)
        return;
    if (watching && !m_recordAllowed && !isRoomHost())
    {
        showNotice(TR(L"发起人已关闭录制"));
        return;
    }
    screenrecord::IRecordVideoSource* video = NULL;
    int srcW = 0;
    int srcH = 0;
    RECT anchor = { 0, 0, 0, 0 };
    int ctype = 0;
    int64_t cid = 0;
    std::wstring convName;
    if (hosting)
    {
        m_streamer.SetRecordTapEnabled(true);
        video = &m_hostRecordSource;
        anchor = hostSourceRect();
        srcW = anchor.right - anchor.left;
        srcH = anchor.bottom - anchor.top;
        ctype = m_hostCtype;
        cid = m_hostCid;
        convName = m_hostConvName;
    }
    else
    {
        video = &m_viewerRecordSource;
        //当前那块可能是白板（2026-10-07）：先画一张白板图，下面的探测才取得到画面
        m_viewer.RefreshRecordBoardFrame();
        std::vector<uint8_t> probe;
        uint64_t serial = 0;
        if (!m_viewer.GetRecordFrame(probe, srcW, srcH, serial))
        {
            showNotice(TR(L"还没有收到画面，请稍后再开始录制"));
            return;
        }
        ::GetWindowRect(m_viewer.m_hWnd, &anchor);
        ctype = m_viewerCtype;
        cid = m_viewerCid;
        convName = m_viewerHostName;
    }
    if (!m_recorder->StartShareRecording(video, &m_speakerCamera, srcW, srcH, anchor, ctype, cid, convName, this))
    {
        if (hosting)
            m_streamer.SetRecordTapEnabled(false);
        return;
    }
    m_shareRecording = true;
    m_recordMixer.store(m_recorder->ShareAudio());
    m_audio.SetRecordTap(this);
    pushRecordAnnotations();
    m_pipSeat = -1;
    send(cmd_screenshare_record_state, ShareProtocolClient::buildRecordState(currentRoomId(), true));
    if (hosting)
        m_bar.SetRecording(true);
    if (watching)
        m_viewer.SetRecording(true);
    //录制时长立即显示在主工具条上（之后随共享时长每秒刷新）
    refreshDuration();
    LOG_INFO("screenshare: share recording started, room=%s, %dx%d.", currentRoomId().c_str(), srcW, srcH);
}

void CShareController::stopShareRecording()
{
    if (!m_shareRecording || m_recorder == NULL)
        return;
    //停止后录屏控制器回调 OnShareRecordingEnded，在那里收尾
    m_recorder->StopRecording();
}

void CShareController::OnShareRecordingEnded()
{
    finishShareRecording(true);
}

void CShareController::finishShareRecording(bool notifyServer)
{
    if (!m_shareRecording)
        return;
    m_shareRecording = false;
    m_audio.SetRecordTap(NULL);
    m_recordMixer.store(NULL);
    m_streamer.SetRecordTapEnabled(false);
    m_speakerCamera.SetSpeaker(-1, false);
    m_pipSeat = -1;
    const std::string& roomId = currentRoomId();
    if (notifyServer && !roomId.empty())
        send(cmd_screenshare_record_state, ShareProtocolClient::buildRecordState(roomId, false));
    if (m_hostPhase == host_streaming)
        m_bar.SetRecording(false);
    if (m_viewerPhase == viewer_watching)
        m_viewer.SetRecording(false);
    LOG_INFO("screenshare: share recording ended, room=%s.", roomId.c_str());
}

void CShareController::OnRoomAudio(int kind, const int16_t* mono, int samples)
{
    //采集、渲染或回环采集线程上：推进录制的混音器
    screenrecord::RecordAudioMixer* mixer = m_recordMixer.load();
    if (mixer == NULL || mono == NULL || samples <= 0)
        return;
    int source = screenrecord::record_audio_others;
    if (kind == share_audio_tap_mic)
        source = screenrecord::record_audio_mic;
    else if (kind == share_audio_tap_system)
        source = screenrecord::record_audio_system;
    mixer->Push(source, mono, static_cast<size_t>(samples));
}

void CShareController::updateRecordPip(const std::vector<int>& speakingSeats)
{
    if (!m_shareRecording || m_recorder == NULL)
        return;
    //当前发言人里第一个开着摄像头的人；没有人说话时保持上一个（他的摄像头还开着的话）
    int chosen = -1;
    for (size_t i = 0; i < speakingSeats.size() && chosen < 0; ++i)
    {
        const int seat = speakingSeats[i];
        if (seat == m_selfSeat ? m_cameraSender.IsRunning() : (m_cameraSeats.count(seat) > 0))
            chosen = seat;
    }
    if (chosen < 0 && m_pipSeat >= 0)
    {
        const bool stillOn = (m_pipSeat == m_selfSeat) ? m_cameraSender.IsRunning() : (m_cameraSeats.count(m_pipSeat) > 0);
        if (stillOn)
            chosen = m_pipSeat;
    }
    if (chosen == m_pipSeat)
        return;
    m_pipSeat = chosen;
    m_speakerCamera.SetSpeaker(chosen, chosen >= 0 && chosen == m_selfSeat);
    m_recorder->SetSharePipEnabled(chosen >= 0);
}

void CShareController::onRecordStateResult(const std::string& json)
{
    ShareResult r;
    if (!ShareProtocolClient::parseResult(json, r) || r.m_code == 0)
        return;
    LOG_WARNING("screenshare: record state rejected, code=%d, msg=%s.", r.m_code, r.m_msg.c_str());
    //发起人已收回录制权限：停止本人的录制（已录的部分照常保存）
    if (r.m_code == error_code_screenshare_no_record_perm && m_shareRecording)
    {
        m_recordAllowed = false;
        if (m_recorder != NULL)
            m_recorder->StopRecording();
        showNotice(TR(L"发起人已关闭录制，录制已停止并保存"));
    }
}

std::string CShareController::wideToUtf8(const std::wstring& w)
{
    if (w.empty())
        return std::string();
    const int n = ::WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), NULL, 0, NULL, NULL);
    if (n <= 0)
        return std::string();
    std::string s(static_cast<size_t>(n), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), &s[0], n, NULL, NULL);
    return s;
}

std::wstring CShareController::utf8ToWide(const std::string& s)
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

} // namespace screenshare
