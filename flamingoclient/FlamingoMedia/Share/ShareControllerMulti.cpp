/**
 *  屏幕共享控制器的多人同时共享部分（第 22 期，docs/屏幕共享与录制方案.md 第 2 节第 27～30 条），ShareControllerMulti.cpp
 *
 *  CShareController 的一部分实现，单独成文件只为不让 ShareController.cpp 继续变长；成员声明都在 ShareController.h。
 *    - 观看者：观看窗的「共享我的屏幕」发 1737 申请；主持人同意后收到带「只推流」令牌的 1738，弹出选择窗选内容，另起一条
 *      推流管线（m_publisher）连上中转推自己的屏幕，被共享内容所在显示器的顶部显示共享者控制条；「停止共享」只停自己的
 *      画面，人还在房间里继续观看；
 *    - 建房间的主持人：1738 收到申请时在右下角弹「某某申请共享屏幕」，同意或拒绝；控制条「更多」菜单可只停自己的画面、
 *      再恢复（第 29 条）；其他共享者的画面经主持人自己的中转连接收到、按画面座位解码，显示在「其他人的屏幕」窗口里；
 *    - 主持人退出（第 30 条）：还有别人在共享时弹出选择框，移交给其中一位（1739，移交后本人离开房间）或结束整个共享；
 *    - 经移交成为主持人的观看者：仍用观看窗，额外负责答复共享申请，关窗时同样走上面的退出流程；他再共享自己的屏幕时
 *      服务端同样另分配画面座位、直接同意。
 *  纯逻辑（画面清单、布局、共享者阶段、申请队列）在 ShareScreens 里，单元测试见 ShareMultiTests.cpp。
 *
 *  balloonwj@qq.com   2026-10-05
 */
#include "stdafx.h"

#include "ShareController.h"

#include "ShareSources.h"                         // ExcludeWindowFromCapture、GetShareWindowBounds
#include "../../Source/net/Protocol.h"
#include "../../Source/Common/Log/IULog.h"
#include "../../Source/Common/I18n/I18n.h"        // TR：界面文字的多语言查找

namespace screenshare {

//==================== 共享者推流管线的监听者 ====================

CShareController::PublisherListener::PublisherListener()
    : m_hwnd(NULL)
{
}

void CShareController::PublisherListener::SetWindow(HWND hwnd)
{
    m_hwnd = hwnd;
}

void CShareController::PublisherListener::OnHostStreamEvent(int event, int value)
{
    //推流线程上：转投 UI 线程；消息窗口已销毁时丢弃
    if (m_hwnd != NULL && ::IsWindow(m_hwnd))
        ::PostMessage(m_hwnd, kMsgSharePublisherEvent, static_cast<WPARAM>(event), static_cast<LPARAM>(value));
}

//==================== 本人的画面 ====================

void CShareController::OnAnnotCanvas(int canvas)
{
    if (!m_annot.IsRunning())
        return;
    //在本人的白板上落笔、而标注模式在别处（第 23 期）：切到白板上标注
    if (canvas == m_myBoardSeat && m_boardWindow.IsOpen() && !(m_annotMode && m_annotOnBoard))
    {
        if (m_annotMode)
            setAnnotMode(false);
        m_annotOnBoard = true;
        setAnnotMode(true);
    }
    //白板的世界是可见区域的两倍，线宽与字号减半后与屏幕上的标注一样粗（第 23 期）
    m_annot.SetWidthScale(isBoardCanvas(canvas) ? kWhiteboardWidthScale : 1.0);
    if (canvas == m_annot.Canvas())
        return;
    m_annot.SetCanvas(canvas);
    //正在画的预览、选中框随之换到新的画面：重画
    annotationChanged();
}

void CShareController::setPublisherAnnotMode(bool on)
{
    //与观看窗里的标注模式互斥：先退出当前的标注模式
    if (m_annotMode)
        setAnnotMode(false);
    if (!on || m_publishPhase != share_publish_streaming)
        return;
    m_annotOwnScreen = true;
    setAnnotMode(true);
}

int CShareController::recordCanvas() const
{
    if (m_hostPhase == host_streaming)
        return m_selfSeat;
    const int seat = m_viewer.ActiveScreenSeat();
    return seat >= 0 ? seat : kAnnotDefaultCanvas;
}

void CShareController::pushRecordAnnotations()
{
    if (m_recorder == NULL)
        return;
    m_recordCanvas = recordCanvas();
    m_recorder->SetShareAnnotations(AnnotStrokesOnCanvas(m_annot.Model().Strokes(), m_recordCanvas));
}

bool CShareController::isScreenOwner() const
{
    return (m_hostPhase == host_streaming && !m_hostScreenStopped) || m_publishPhase == share_publish_streaming;
}

int CShareController::ownScreenSeat() const
{
    //建房间的主持人用自己的座位推流；共享者用服务端分配的画面座位
    if (m_hostPhase == host_streaming)
        return m_selfSeat;
    return m_publishRelay.m_seat;
}

const ShareSourceSpec& CShareController::ownSource() const
{
    return (m_hostPhase == host_streaming) ? m_hostSource : m_publishSource;
}

RECT CShareController::ownSourceRect() const
{
    const ShareSourceSpec& source = ownSource();
    if (source.m_kind == share_source_monitor)
        return source.m_monitorRect;
    return GetShareWindowBounds(source.m_hwnd);
}

bool CShareController::ownScreenPaused() const
{
    return m_hostPhase == host_streaming && (m_hostPaused || m_hostScreenStopped);
}

bool CShareController::isRoomHost() const
{
    if (m_host == NULL)
        return false;
    if (m_hostPhase == host_streaming)
        return true;
    //经移交成为主持人的观看者：以最近一次快照为准
    return m_viewerPhase == viewer_watching && !m_viewerRoomId.empty() && m_lastState.m_roomId == m_viewerRoomId
           && m_lastState.m_hostId == m_host->GetSelfUserId();
}

//==================== 信令 ====================

CShareController::PendingSharing::PendingSharing()
    : m_action(0)
    , m_target(0)
    , m_kind(share_kind_screen)
{
}

void CShareController::sendSharing(int action, int64_t target, int kind)
{
    const std::string& roomId = currentRoomId();
    if (roomId.empty())
        return;
    if (!send(cmd_screenshare_sharing, ShareProtocolClient::buildSharing(roomId, action, target, kind)))
        return;
    PendingSharing pending;
    pending.m_action = action;
    pending.m_target = target;
    pending.m_kind = kind;
    m_pendingSharing.push_back(pending);
    LOG_INFO("screenshare multi: sent sharing action=%d, target=%lld, kind=%d, room=%s.", action,
             static_cast<long long>(target), kind, roomId.c_str());
}

void CShareController::onSharingResult(const std::string& json)
{
    //应答与请求按序对应
    PendingSharing request;
    if (!m_pendingSharing.empty())
    {
        request = m_pendingSharing.front();
        m_pendingSharing.pop_front();
    }
    ShareResult r;
    if (!ShareProtocolClient::parseResult(json, r) || r.m_code == 0)
        return;
    LOG_WARNING("screenshare multi: sharing action %d (kind %d) failed, code=%d, msg=%s.", request.m_action,
                request.m_kind, r.m_code, r.m_msg.c_str());
    const bool board = (request.m_kind == share_kind_whiteboard);
    //本人的申请没有成功：回到「共享我的屏幕」或「新建白板」（第 23 期）
    if (request.m_action == share_sharing_request && !board && m_publishPhase == share_publish_requested)
    {
        m_publishPhase = share_publish_idle;
        m_viewer.SetShareMineState(ShareMineStateForPhase(m_publishPhase));
    }
    if (request.m_action == share_sharing_request && board && m_boardPhase == share_viewer_mine_requested)
    {
        m_boardPhase = share_viewer_mine_none;
        m_viewer.SetBoardState(m_boardPhase);
        m_bar.SetBoardOpen(false);
    }
    //同意别人时人数已满：服务端保留了他的申请，放回队列，等有人停下后再提示
    if (request.m_action == share_sharing_approve && r.m_code == error_code_screenshare_sharers_full &&
        request.m_target > 0)
    {
        if (board)
        {
            m_boardAnswered.erase(request.m_target);
            m_boardRequests.Add(request.m_target, displayName(request.m_target));
        }
        else
        {
            m_shareAnswered.erase(request.m_target);
            m_shareRequests.Add(request.m_target, displayName(request.m_target));
        }
    }
    if (r.m_code == error_code_screenshare_sharers_full)
        showNotice(TR(L"同时共享屏幕的人数已满，等有人停止共享后再试"));
    else if (r.m_code == error_code_screenshare_no_request)
        showNotice(TR(L"对方已撤回共享申请"));
    else if (r.m_code != error_code_screenshare_not_sharing)
        showNotice(TRF(L"共享操作失败（错误码 {0}）", r.m_code).c_str());
}

void CShareController::onSharingNotify(const std::string& json)
{
    ShareSharingNotify n;
    if (!ShareProtocolClient::parseSharingNotify(json, n))
        return;
    if (n.m_roomId.empty() || n.m_roomId != currentRoomId())
    {
        LOG_WARNING("screenshare multi: sharing notify for room %s ignored.", n.m_roomId.c_str());
        return;
    }
    std::wstring actorName = utf8ToWide(n.m_actorName);
    if (actorName.empty())
        actorName = displayName(n.m_actor);
    //白板（第 23 期）
    if (n.m_kind == share_kind_whiteboard)
    {
        onBoardNotify(n, actorName);
        return;
    }
    switch (n.m_action)
    {
    //主持人：有人申请共享屏幕
    case share_sharing_request:
        if (!isRoomHost())
            break;
        m_shareAnswered.erase(n.m_actor);
        if (m_shareRequests.Add(n.m_actor, actorName))
            LOG_INFO("screenshare multi: share request from user %lld queued, pending=%u.",
                     static_cast<long long>(n.m_actor), static_cast<unsigned>(m_shareRequests.Size()));
        showNextShareRequest();
        break;

    //主持人：申请人撤回了申请
    case share_sharing_cancel:
        LOG_INFO("screenshare multi: share request from user %lld cancelled.", static_cast<long long>(n.m_actor));
        if (m_shareRequests.Remove(n.m_actor))
            showNextShareRequest();
        break;

    //申请人：主持人同意（经移交成为主持人的人自己申请时也是这条），带着「只推流」中转参数，弹出选择窗选共享内容
    case share_sharing_approve:
        if (m_viewerPhase != viewer_watching || !n.HasPublishInfo())
            break;
        if (m_publishPhase == share_publish_picking || m_publishPhase == share_publish_streaming)
        {
            LOG_WARNING("screenshare multi: duplicate approve ignored, phase=%d.", m_publishPhase);
            break;
        }
        m_publishRelay = ShareRelayInfo();
        m_publishRelay.m_roomId = n.m_roomId;
        m_publishRelay.m_relayIp = n.m_relayIp;
        m_publishRelay.m_relayPort = n.m_relayPort;
        m_publishRelay.m_token = n.m_token;
        m_publishRelay.m_seat = n.m_screenSeat;
        m_publishPhase = share_publish_picking;
        m_viewer.SetShareMineState(ShareMineStateForPhase(m_publishPhase));
        LOG_INFO("screenshare multi: share approved by user %lld, screen seat %d.", static_cast<long long>(n.m_actor),
                 n.m_screenSeat);
        //申请之后开始了独立录制（2026-10-07，录制与共享互斥）：不打开选择窗，告诉服务端不共享了
        if (rejectIfRecording())
        {
            stopPublisherLocal(true);
            m_viewer.SetShareMineState(ShareMineStateForPhase(m_publishPhase));
            break;
        }
        if (m_host != NULL && n.m_actor == m_host->GetSelfUserId())
            openPicker(true, TR(L"请选择要共享的内容；取消则不共享。"));
        else
            openPicker(true, TRF(L"{0} 已同意你共享屏幕，请选择要共享的内容；取消则不共享。", actorName));
        break;

    //申请人：主持人拒绝
    case share_sharing_reject:
        if (m_publishPhase == share_publish_requested)
        {
            m_publishPhase = share_publish_idle;
            m_viewer.SetShareMineState(ShareMineStateForPhase(m_publishPhase));
        }
        showNotice(TRF(L"{0} 拒绝了你的共享申请", actorName).c_str());
        break;

    //共享者：被主持人停止
    case share_sharing_stop:
        if (m_publishPhase != share_publish_idle)
        {
            LOG_INFO("screenshare multi: sharing stopped by host %lld.", static_cast<long long>(n.m_actor));
            stopPublisherLocal(false);
            showNotice(TRF(L"{0} 停止了你的屏幕共享", actorName).c_str());
        }
        break;

    //未知动作：新服务端加了本客户端不认识的动作，记日志后忽略
    default:
        LOG_WARNING("screenshare multi: unknown sharing action %d ignored.", n.m_action);
        break;
    }
}

void CShareController::onTransferHostResult(const std::string& json)
{
    ShareResult r;
    if (!ShareProtocolClient::parseResult(json, r))
        return;
    if (r.m_code != 0)
    {
        LOG_WARNING("screenshare multi: transfer host failed, code=%d, msg=%s.", r.m_code, r.m_msg.c_str());
        if (r.m_code == error_code_screenshare_not_sharing)
            showNotice(TR(L"对方已不在共享屏幕，无法移交主持人"));
        else
            showNotice(TRF(L"移交主持人失败（错误码 {0}）", r.m_code).c_str());
        return;
    }
    LOG_INFO("screenshare multi: host transferred, leaving room %s.", currentRoomId().c_str());
    //本人移交后离开了房间：共享由新主持人继续，之后结束时本人不弹结束提示
    m_participantLog.Reset(std::string(), false);
    //本人已离开房间；共享换了主持人继续进行，重新查询一次，卡片与横幅继续显示「进行中」
    if (m_hostPhase != host_idle)
    {
        const int ctype = m_hostCtype;
        const int64_t cid = m_hostCid;
        stopHostLocal();
        QueryShare(ctype, cid);
        return;
    }
    if (m_viewerPhase != viewer_idle)
    {
        const int ctype = m_viewerCtype;
        const int64_t cid = m_viewerCid;
        stopViewerLocal();
        QueryShare(ctype, cid);
    }
}

//==================== 房间快照 ====================

void CShareController::applyMultiState(const ShareStateNotify& n, bool asHost)
{
    if (m_host == NULL)
        return;
    const int64_t self = m_host->GetSelfUserId();
    const std::vector<ShareScreenInfo> before = m_screens;
    m_screens = CollectOtherScreens(n, self);
    //已不在共享的画面：本地清掉它的标注（第 22 期 22e），那个座位以后可能分给别人的画面
    for (size_t i = 0; i < before.size(); ++i)
    {
        if (FindScreenBySeat(m_screens, before[i].m_screenSeat) < 0 && m_annot.IsRunning()
            && m_annot.ClearCanvasLocal(before[i].m_screenSeat))
            annotationChanged();
    }
    const ShareMember* selfMember = n.findMember(self);
    if (asHost)
    {
        //主持人停掉自己的画面后又申请恢复：服务端已记为在共享
        if (m_hostScreenStopped && selfMember != NULL && selfMember->m_sharing)
            setHostScreenStopped(false);
        refreshScreensWindow(before, m_screens);
    }
    else
    {
        std::vector<int> seats;
        std::vector<std::wstring> names;
        std::vector<int> boardSeats;
        std::vector<int> boardOwners;
        for (size_t i = 0; i < m_screens.size(); ++i)
        {
            seats.push_back(m_screens[i].m_screenSeat);
            //白板（第 23 期）的名字写成「某某 的白板」
            if (m_screens[i].m_isWhiteboard)
            {
                names.push_back(TRF(L"{0} 的白板", displayName(m_screens[i].m_ownerId)));
                boardSeats.push_back(m_screens[i].m_screenSeat);
                boardOwners.push_back(m_screens[i].m_ownerSeat);
            }
            else
            {
                names.push_back(displayName(m_screens[i].m_ownerId));
            }
        }
        m_viewer.SetScreens(seats, names);
        m_viewer.SetBoards(boardSeats, boardOwners);
        //本人作为共享者：快照里已不在共享（被主持人停掉）时收尾；1738 的停止推送通常先到，这里兜底
        const int phase = SyncPublishPhase(m_publishPhase, n, self);
        if (phase == share_publish_idle && m_publishPhase != share_publish_idle)
        {
            LOG_INFO("screenshare multi: no longer sharing per state, phase %d -> idle.", m_publishPhase);
            stopPublisherLocal(false);
        }
        m_viewer.SetShareMineState(ShareMineStateForPhase(m_publishPhase));
    }
    //白板（第 23 期）：本人的白板、主持人一侧的新建白板申请
    syncBoards(n);
    //经移交成为主持人后有人加入（2026-10-07，方案第 14.3 节第 1 条）：原主持人在收到关键帧请求时补发已有的标注，新主持人停在
    //观看窗里收不到这个信号，改为等对方连上中转之后由新主持人补发
    if (n.m_event == share_event_member_joined && n.m_actor != self && isRoomHost() && m_hostPhase != host_streaming)
        SetTimer(kTransferredSnapshotTimerId, kTransferredSnapshotDelayMs, NULL);
    //换了主持人（事件 14）
    if (n.m_event == share_event_host_changed && n.m_target != 0)
    {
        LOG_INFO("screenshare multi: host changed from %lld to %lld.", static_cast<long long>(n.m_actor),
                 static_cast<long long>(n.m_target));
        if (n.m_target == self)
            showNotice(TR(L"你已成为这场屏幕共享的主持人"));
        else
            showNotice(TRF(L"{0} 成为了主持人", displayName(n.m_target)).c_str());
        //聊天窗卡片与横幅上的发起人：1725 不带「是否需要密码」，不能直接上报，改为重新查询一次这个会话，
        //查询应答带着新的主持人、密码与强制模式，经 onQueryResult 上报给 exe
        if (m_viewerPhase != viewer_idle && m_viewerCid != 0)
            QueryShare(m_viewerCtype, m_viewerCid);
    }
    //排队的共享申请：本人是主持人时，快照里在申请、又没答复过的补进队列；已离开或不再申请的去掉
    if (!isRoomHost())
    {
        if (m_shareRequests.Size() > 0 || !m_shareAnswered.empty())
        {
            m_shareRequests.Clear();
            m_shareAnswered.clear();
            m_sharingToast.Hide();
        }
        return;
    }
    std::set<int64_t> stillRequesting;
    for (size_t i = 0; i < n.m_members.size(); ++i)
    {
        const ShareMember& m = n.m_members[i];
        if (!m.m_requesting)
            continue;
        stillRequesting.insert(m.m_userId);
        if (m_shareAnswered.count(m.m_userId) == 0)
            m_shareRequests.Add(m.m_userId, displayName(m.m_userId));
    }
    //已答复的人在快照里不再申请（同意后在共享、或已被拒绝）：不用再记
    for (std::set<int64_t>::iterator it = m_shareAnswered.begin(); it != m_shareAnswered.end();)
    {
        if (stillRequesting.count(*it) == 0)
            m_shareAnswered.erase(it++);
        else
            ++it;
    }
    m_shareRequests.Prune(n);
    showNextShareRequest();
}

//==================== 主持人一侧 ====================

void CShareController::showNextShareRequest()
{
    const int64_t front = m_shareRequests.FrontUser();
    if (front == 0)
    {
        m_sharingToast.Hide();
        return;
    }
    //共享名额已满（第 23 期起白板也占名额）：申请先留在队列里，等有人停下后（下一次快照）再提示
    if (m_lastState.m_maxSharers > 0 && ShareSlotsUsed(m_lastState) >= m_lastState.m_maxSharers)
    {
        if (m_sharingToast.IsWindow())
            m_sharingToast.Hide();
        return;
    }
    //队首已经显示着：不重复弹
    if (m_sharingToast.IsWindow() && m_sharingToast.Token() == static_cast<LPARAM>(front))
        return;
    ShareToastTexts texts;
    texts.m_title = TR(L"共享申请");
    texts.m_accept = TR(L"同意");
    texts.m_ignore = TR(L"拒绝");
    texts.m_message = kMsgShareSharingToast;
    m_sharingToast.Show(TRF(L"{0} 申请共享屏幕", m_shareRequests.FrontName()), static_cast<LPARAM>(front), m_hWnd, 1,
                        texts);
    //提示排除在屏幕采集之外：观看者看不到谁在申请
    if (m_sharingToast.IsWindow())
        ExcludeWindowFromCapture(m_sharingToast.m_hWnd);
}

LRESULT CShareController::OnSharingToastMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)bHandled;
    const int64_t userId = static_cast<int64_t>(lParam);
    if (!isRoomHost() || userId <= 0)
        return 0;
    //提示已经自己关掉：从队列里去掉，记下已答复，等快照里他不再申请
    m_shareRequests.Remove(userId);
    m_shareAnswered.insert(userId);
    if (static_cast<int>(wParam) == CShareCornerToast::kCmdJoin)
        sendSharing(share_sharing_approve, userId);
    else
        sendSharing(share_sharing_reject, userId);
    showNextShareRequest();
    return 0;
}

void CShareController::setHostScreenStopped(bool stopped)
{
    if (m_hostPhase != host_streaming || stopped == m_hostScreenStopped)
        return;
    m_hostScreenStopped = stopped;
    //停掉期间不推新画面；恢复时回到停掉之前的暂停状态
    m_streamer.SetPaused(stopped || m_hostPaused);
    m_bar.SetMineStopped(stopped);
    if (stopped)
    {
        m_border.Hide();
        if (m_annotMode)
            setAnnotMode(false);
        m_annotOverlay.Place(m_annotOverlay.Rect(), false);
        //自己的画面已不在共享：它的标注随之清掉（别人那边同样按快照清掉）
        if (m_annot.IsRunning() && m_annot.ClearCanvasLocal(m_selfSeat))
            annotationChanged();
        //自己的画面已不在共享，正在进行的控制与排队的控制申请随之结束（服务端同样会清掉）
        if (m_controllerId != 0)
            setControlledBy(0);
        m_revokingId = 0;
        m_controlRequests.clear();
        m_controlToast.Hide();
    }
    else
    {
        const bool monitor = (m_hostSource.m_kind == share_source_monitor);
        const RECT rect = hostSourceRect();
        if (!m_hostPaused)
            m_border.Show(rect, monitor);
        m_annotOverlay.Place(rect, true);
    }
    LOG_INFO("screenshare multi: host screen %s, room=%s.", stopped ? "stopped" : "resumed", m_hostRoomId.c_str());
}

void CShareController::refreshScreensWindow(const std::vector<ShareScreenInfo>& before,
                                            const std::vector<ShareScreenInfo>& others)
{
    std::vector<int> seats;
    std::vector<std::wstring> names;
    std::vector<int> boardSeats;
    std::vector<int> boardOwners;
    bool added = false;
    for (size_t i = 0; i < others.size(); ++i)
    {
        seats.push_back(others[i].m_screenSeat);
        //白板（第 23 期）的名字写成「某某 的白板」
        if (others[i].m_isWhiteboard)
        {
            names.push_back(TRF(L"{0} 的白板", displayName(others[i].m_ownerId)));
            boardSeats.push_back(others[i].m_screenSeat);
            boardOwners.push_back(others[i].m_ownerSeat);
        }
        else
        {
            names.push_back(displayName(others[i].m_ownerId));
        }
        if (FindScreenBySeat(before, others[i].m_screenSeat) < 0)
            added = true;
    }
    //已不在共享的画面：推流管线丢掉它的解码器与最新一帧
    for (size_t i = 0; i < before.size(); ++i)
    {
        if (FindScreenBySeat(others, before[i].m_screenSeat) < 0)
            m_streamer.RemoveRemoteScreen(before[i].m_screenSeat);
    }
    m_bar.SetOtherScreenCount(static_cast<int>(others.size()));
    if (others.empty())
    {
        //正在那个窗口里标注（2026-10-07）：窗口要关了，先退出标注
        if (m_annotMode && m_annotOnScreens)
            setAnnotMode(false);
        m_screensWindow.Close();
        return;
    }
    m_screensWindow.SetScreens(seats, names);
    m_screensWindow.SetBoards(boardSeats, boardOwners);
    m_screensWindow.SetAnnotation(&m_annot);
    //有人新开始共享：打开（或重新显示被关掉的）窗口；顶部按钮的命令发回本控制器，控制中的键鼠交给本控制器
    if (added)
    {
        m_screensWindow.Open(mainWindow(), &m_streamer, m_hWnd);
        m_screensWindow.SetControlInput(this);
    }
}

LRESULT CShareController::OnScreensCommandMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)lParam;
    (void)bHandled;
    if (m_hostPhase != host_streaming)
        return 0;
    switch (static_cast<int>(wParam))
    {
    //「标注 / 退出标注」：已在这个窗口里标注时退出；否则先退出别处的标注，再进入在这个窗口里标注
    case CShareScreensWindow::kCmdAnnotate:
        if (m_annotMode && m_annotOnScreens)
        {
            setAnnotMode(false);
            break;
        }
        if (m_annotMode)
            setAnnotMode(false);
        m_annotOnScreens = true;
        setAnnotMode(true);
        LOG_INFO("screenshare multi: host annotating in the screens window, room=%s.", m_hostRoomId.c_str());
        break;
    //「申请控制 / 取消申请 / 结束控制」：与观看窗同一套流程，画面取这个窗口当前那一块
    case CShareScreensWindow::kCmdControl:
        toggleViewerControl();
        break;
    //未知命令：忽略
    default:
        break;
    }
    return 0;
}

void CShareController::beginHostExit()
{
    if (m_host == NULL || !isRoomHost())
        return;
    const int64_t self = m_host->GetSelfUserId();
    std::vector<int64_t> ids;
    if (m_lastState.m_roomId == currentRoomId())
        ids = HostTransferCandidates(m_lastState, self);
    //没有别人在共享：直接结束整个共享
    if (ids.empty())
    {
        endAllSharing();
        return;
    }
    std::vector<std::pair<int64_t, std::wstring> > candidates;
    for (size_t i = 0; i < ids.size(); ++i)
        candidates.push_back(std::make_pair(ids[i], displayName(ids[i])));
    LOG_INFO("screenshare multi: host exit with %u other sharer(s), asking to transfer or end.",
             static_cast<unsigned>(ids.size()));
    m_hostExitDialog.Open(mainWindow(), m_hWnd, candidates);
}

void CShareController::endAllSharing()
{
    m_hostExitDialog.Close();
    const std::string roomId = currentRoomId();
    if (roomId.empty())
        return;
    send(cmd_screenshare_stop, ShareProtocolClient::buildRoomRequest(roomId));
    LOG_INFO("screenshare: stopped by host, room=%s.", roomId.c_str());
    //不等 1729，本地立即收尾
    if (m_hostPhase != host_idle)
    {
        stopHostLocal();
        return;
    }
    if (m_viewerPhase != viewer_idle)
        stopViewerLocal();
}

void CShareController::transferHostTo(int64_t userId)
{
    const std::string& roomId = currentRoomId();
    if (roomId.empty() || userId <= 0 || !isRoomHost())
        return;
    send(cmd_screenshare_transfer_host, ShareProtocolClient::buildTransferHost(roomId, userId));
    LOG_INFO("screenshare multi: transfer host to user %lld, room=%s.", static_cast<long long>(userId), roomId.c_str());
}

//==================== 共享者一侧 ====================

void CShareController::toggleShareMine()
{
    if (m_viewerPhase != viewer_watching)
        return;
    switch (m_publishPhase)
    {
    //没有申请也没在共享：向主持人申请；本人就是主持人时服务端直接同意。独立录制进行中时不申请（2026-10-07，录制与共享互斥）
    case share_publish_idle:
        if (rejectIfRecording())
            break;
        m_publishPhase = share_publish_requested;
        sendSharing(share_sharing_request, 0);
        if (!isRoomHost())
            showNotice(TR(L"已申请共享屏幕，等待主持人同意"));
        break;

    //申请中：撤回
    case share_publish_requested:
        sendSharing(share_sharing_cancel, 0);
        m_publishPhase = share_publish_idle;
        break;

    //选择内容中或推流中：停止本人的共享
    default:
        stopPublisherLocal(true);
        showNotice(TR(L"你已停止共享屏幕"));
        break;
    }
    m_viewer.SetShareMineState(ShareMineStateForPhase(m_publishPhase));
}

void CShareController::startPublishing()
{
    if (m_host == NULL)
        return;
    //选内容期间开始了独立录制（2026-10-07，录制与共享互斥）：不推流，告诉服务端不共享了
    if (rejectIfRecording())
    {
        stopPublisherLocal(true);
        m_viewer.SetShareMineState(ShareMineStateForPhase(m_publishPhase));
        return;
    }
    m_publisherListener.SetWindow(m_hWnd);
    m_publisher.SetMouseEffects(m_mouseSettings);
    if (!m_publisher.Start(m_publishRelay, m_host->GetServerPublicKey(), m_publishSource, NULL, &m_publisherListener))
    {
        LOG_ERROR("screenshare multi: start publisher failed, room=%s.", m_publishRelay.m_roomId.c_str());
        stopPublisherLocal(true);
        showNotice(TR(L"无法开始共享屏幕"));
        return;
    }
    //控制键鼠（第 18 期）：控制者发给本人这块画面的输入经这条中转连接收到
    m_publisher.SetInputSink(this);
    m_publishPhase = share_publish_streaming;
    const bool monitor = (m_publishSource.m_kind == share_source_monitor);
    const RECT rect = ownSourceRect();
    m_publishBorder.Show(rect, monitor);
    //本机标注层盖在共享的内容上，显示别人画在这块画面上的标注（第 22 期 22e）
    m_publishOverlay.SetCanvas(m_publishRelay.m_seat);
    m_publishOverlay.Place(rect, true);
    //「屏幕当白板」（2026-10-07）：只共享一个窗口时提供
    m_publishScreenBoard = false;
    m_sharerBar.SetScreenBoard(!monitor, false);
    m_sharerBar.Show(rect, m_hWnd);
    SetTimer(kPublishFollowTimerId, kPublishFollowTimerMs, NULL);
    m_viewer.SetShareMineState(ShareMineStateForPhase(m_publishPhase));
    LOG_INFO("screenshare multi: publishing to room %s, screen seat %d, source kind=%d.",
             m_publishRelay.m_roomId.c_str(), m_publishRelay.m_seat, m_publishSource.m_kind);
}

void CShareController::stopPublisherLocal(bool notifyServer)
{
    if (m_publishPhase == share_publish_idle)
        return;
    if (notifyServer)
        sendSharing(m_publishPhase == share_publish_requested ? share_sharing_cancel : share_sharing_stop, 0);
    //本人这块画面的控制随之结束（建房间的主持人不会走到这里，他的控制状态由 stopHostLocal 清理）
    if (m_hostPhase == host_idle)
    {
        if (m_controllerId != 0)
            setControlledBy(0);
        m_revokingId = 0;
        m_controlRequests.clear();
        m_controlToast.Hide();
    }
    if (IsWindow())
        KillTimer(kPublishFollowTimerId);
    //正在自己共享的内容上标注：退出标注模式；这块画面的标注随之清掉（第 22 期 22e）
    if (m_annotMode && m_annotOwnScreen)
        setAnnotMode(false);
    m_publishOverlay.Close();
    if (m_publishPhase == share_publish_streaming && m_annot.IsRunning() && m_annot.ClearCanvasLocal(m_publishRelay.m_seat))
        annotationChanged();
    m_publisher.Stop();
    m_publisher.SetInputSink(NULL);
    m_publishScreenBoard = false;
    m_sharerBar.Hide();
    m_publishBorder.Hide();
    //同意之后还没选好内容：关掉选择窗（不投递结果）
    if (m_publishPhase == share_publish_picking)
        m_picker.CloseDialog();
    LOG_INFO("screenshare multi: publisher stopped, phase %d, notify server %d.", m_publishPhase, notifyServer ? 1 : 0);
    m_publishPhase = share_publish_idle;
    m_publishRelay = ShareRelayInfo();
    m_publishSource = ShareSourceSpec();
    m_viewer.SetShareMineState(ShareMineStateForPhase(m_publishPhase));
}

void CShareController::switchPublishSource(const ShareSourceSpec& spec)
{
    if (m_publishPhase != share_publish_streaming)
        return;
    m_publishSource = spec;
    m_publisher.SwitchSource(m_publishSource);
    const bool monitor = (m_publishSource.m_kind == share_source_monitor);
    const RECT rect = ownSourceRect();
    m_publishBorder.Show(rect, monitor);
    //「屏幕当白板」：只共享一个窗口、或已在屏幕白板里时提供（2026-10-07，方案第 14.1 节 ③）
    m_sharerBar.SetScreenBoard(!monitor || m_publishScreenBoard, m_publishScreenBoard);
    m_sharerBar.Show(rect, m_hWnd);
    //这块画面上的标注清掉（与主持人切换内容相同），标注层与工具条挪到新的内容上
    if (m_annot.IsRunning() && m_annot.ClearCanvasLocal(m_publishRelay.m_seat))
        annotationChanged();
    m_publishOverlay.Place(rect, true);
    if (m_annotMode && m_annotOwnScreen)
        m_annotBar.MoveTo(annotAnchorRect());
    LOG_INFO("screenshare multi: publisher switched source, kind=%d, screen board %d.", m_publishSource.m_kind,
             m_publishScreenBoard ? 1 : 0);
}

void CShareController::followPublishedSource()
{
    if (m_publishPhase != share_publish_streaming || m_publishSource.m_kind != share_source_window)
        return;
    const HWND hwnd = m_publishSource.m_hwnd;
    const bool hidden = !::IsWindow(hwnd) || ::IsIconic(hwnd);
    m_publishBorder.Follow(hidden ? RECT() : GetShareWindowBounds(hwnd), hidden);
    //标注层与工具条跟着被共享的窗口移动、缩放（第 22 期 22e）
    const RECT bounds = hidden ? RECT() : GetShareWindowBounds(hwnd);
    const RECT& old = m_publishOverlay.Rect();
    if (hidden || !::EqualRect(&bounds, &old))
    {
        m_publishOverlay.Place(hidden ? old : bounds, !hidden);
        if (m_annotMode && m_annotOwnScreen && !hidden)
            m_annotBar.MoveTo(bounds);
    }
}

LRESULT CShareController::OnPublisherEventMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)bHandled;
    if (m_publishPhase != share_publish_streaming)
        return 0;
    switch (static_cast<int>(wParam))
    {
    //连不上中转、或推流途中断开：停止本人的共享，人还在房间里
    case share_host_relay_failed:
    case share_host_relay_lost:
        LOG_WARNING("screenshare multi: publisher relay event %d, room=%s.", static_cast<int>(wParam),
                    m_publishRelay.m_roomId.c_str());
        stopPublisherLocal(true);
        showNotice(TR(L"与屏幕共享服务的连接已断开，你的共享已停止"));
        break;

    //采集器建不起来：停止本人的共享
    case share_host_capture_failed:
        stopPublisherLocal(true);
        showNotice(TR(L"无法采集所选的内容，你的共享已停止"));
        break;

    //共享的窗口被关闭：停止本人的共享
    case share_host_source_state:
        if (static_cast<int>(lParam) == screenshare_source_closed)
        {
            stopPublisherLocal(true);
            showNotice(TR(L"共享的窗口已关闭，你的共享已停止"));
        }
        break;

    //连上了中转、关键帧请求等其余事件：不需要处理
    default:
        break;
    }
    return 0;
}

LRESULT CShareController::OnSharerCommandMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)bHandled;
    switch (static_cast<int>(wParam))
    {
    //共享者控制条的「停止共享」：只停自己的画面，留在房间里继续观看
    case share_sharer_stop:
        if (m_publishPhase != share_publish_idle)
        {
            stopPublisherLocal(true);
            showNotice(TR(L"你已停止共享屏幕"));
        }
        break;

    //主持人退出选择框的「移交给某人并离开」：lParam 为新主持人
    case share_sharer_transfer:
        transferHostTo(static_cast<int64_t>(lParam));
        break;

    //共享者控制条「更多」菜单的「允许截图」（2026-10-08）
    case share_sharer_allow_capture:
        setBarCapture(lParam != 0);
        break;

    //主持人退出选择框的「结束共享」
    case share_sharer_end_all:
        if (isRoomHost())
            endAllSharing();
        break;

    //共享者控制条的「标注 / 退出标注」（第 22 期 22e）：在自己共享的内容上标注
    case share_sharer_annotate:
        if (m_publishPhase == share_publish_streaming)
        {
            if (!(m_annotMode && m_annotOwnScreen) && !m_annot.CanDraw())
                showNotice(TR(L"发起人已关闭标注"));
            else
                setPublisherAnnotMode(!(m_annotMode && m_annotOwnScreen));
        }
        break;

    //共享者控制条的「屏幕当白板 / 退出屏幕白板」（2026-10-07，方案第 14.1 节 ③）：只切换本人共享的那一块
    case share_sharer_screen_board:
        toggleSharerScreenBoard();
        break;

    //主持人退出选择框的「取消」或关窗：什么也不做
    default:
        break;
    }
    return 0;
}

} // namespace screenshare
