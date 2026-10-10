/**
 *  屏幕共享控制器的白板部分（第 23 期，docs/屏幕共享与录制方案.md 第 2 节第 31、32、39、40 条），ShareControllerBoard.cpp
 *
 *  CShareController 的一部分实现，单独成文件只为不让 ShareController.cpp 继续变长；成员声明都在 ShareController.h。
 *    - 新建白板：观看窗或控制条的「新建白板」发 1737（kind 2）；主持人直接建，其他人经主持人同意（右下角「某某申请新建白板」）。
 *      建好后建立者打开白板窗口（房间模式）画，笔迹按白板的画面座位走房间标注；建立者每有背景或可见区域的变化就广播「白板视野」
 *      （标注轨操作 12，限频，另每 3 秒重发一次），其他人在观看窗里把白板当作一块画面，可以跟随建立者的可见区域；
 *    - 关闭白板：建立者关窗或点「关闭我的白板」，白板上有笔迹时先问要不要保存为图片；主持人也能关别人的；
 *    - 屏幕当白板（第 31 条）：只共享一个窗口时，先提示观看者将看到整块屏幕，确认后共享内容换成它所在的整块屏幕并进入标注，
 *      退出时恢复只共享原窗口；
 *    - 保存为图片（第 40 条）：标注工具条的「保存为图片」，白板导出整张画布，共享画面导出当前画面加笔迹；保存后右下角提示，
 *      可一键发到本会话。
 *  纯逻辑（白板视野、导出渲染）在 ShareWhiteboard、ShareImageExport 里，单元测试见 ShareBoardTests.cpp。
 *
 *  balloonwj@qq.com   2026-10-05
 */
#include "stdafx.h"

#include "ShareController.h"

#include <commdlg.h>      // GetSaveFileNameW
#include <shlobj.h>       // SHGetFolderPathW：默认存到「图片」文件夹
#include "ShareSources.h"                         // ExcludeWindowFromCapture
#include "../../Source/net/Protocol.h"
#include "../../Source/Common/Log/IULog.h"
#include "../../Source/Common/I18n/I18n.h"        // TR：界面文字的多语言查找
#include "../../Source/Common/Util/FileDialogFilter.h"   // filedlg::MakeFilter：另存为框的过滤串

namespace screenshare {

namespace {

//提示框标题
const wchar_t* const kBoardCaption = TR_NOOP(L"屏幕共享");

//只取路径里的文件名部分（提示里显示）
std::wstring fileNameOf(const std::wstring& path)
{
    const size_t slash = path.find_last_of(L"\\/");
    return (slash == std::wstring::npos) ? path : path.substr(slash + 1);
}

} // namespace

//==================== 本人的白板 ====================

void CShareController::toggleMyBoard()
{
    if (m_hostPhase != host_streaming && m_viewerPhase != viewer_watching)
        return;
    switch (m_boardPhase)
    {
    //没有白板：申请新建；本人是主持人时服务端直接建好、经 1738 告诉本人座位
    case share_viewer_mine_none:
        m_boardPhase = share_viewer_mine_requested;
        sendSharing(share_sharing_request, 0, share_kind_whiteboard);
        if (!isRoomHost())
            showNotice(TR(L"已申请新建白板，等待主持人同意"));
        break;

    //申请中：撤回
    case share_viewer_mine_requested:
        sendSharing(share_sharing_cancel, 0, share_kind_whiteboard);
        m_boardPhase = share_viewer_mine_none;
        break;

    //白板开着：关闭（有笔迹时先问要不要保存）
    default:
        closeMyBoard(true);
        break;
    }
    m_viewer.SetBoardState(m_boardPhase);
    m_bar.SetBoardOpen(m_boardPhase == share_viewer_mine_sharing);
}

void CShareController::onBoardNotify(const ShareSharingNotify& n, const std::wstring& actorName)
{
    switch (n.m_action)
    {
    //主持人：有人申请新建白板
    case share_sharing_request:
        if (!isRoomHost())
            break;
        m_boardAnswered.erase(n.m_actor);
        if (m_boardRequests.Add(n.m_actor, actorName))
            LOG_INFO("screenshare board: request from user %lld queued, pending=%u.", static_cast<long long>(n.m_actor),
                     static_cast<unsigned>(m_boardRequests.Size()));
        showNextBoardRequest();
        break;

    //主持人：申请人撤回了申请
    case share_sharing_cancel:
        LOG_INFO("screenshare board: request from user %lld cancelled.", static_cast<long long>(n.m_actor));
        if (m_boardRequests.Remove(n.m_actor))
            showNextBoardRequest();
        break;

    //申请人（含主持人自己直接新建）：同意，带白板的画面座位，打开白板窗口
    case share_sharing_approve:
        if (n.m_screenSeat < 0)
            break;
        if (m_boardPhase == share_viewer_mine_sharing && m_myBoardSeat == n.m_screenSeat)
            break;
        openMyBoard(n.m_screenSeat);
        if (m_host != NULL && n.m_actor != m_host->GetSelfUserId())
            showNotice(TRF(L"{0} 已同意你新建白板", actorName).c_str());
        break;

    //申请人：主持人拒绝
    case share_sharing_reject:
        if (m_boardPhase == share_viewer_mine_requested)
        {
            m_boardPhase = share_viewer_mine_none;
            m_viewer.SetBoardState(m_boardPhase);
            m_bar.SetBoardOpen(false);
        }
        showNotice(TRF(L"{0} 拒绝了你新建白板的申请", actorName).c_str());
        break;

    //建立者：被主持人关闭
    case share_sharing_stop:
        if (m_boardPhase == share_viewer_mine_sharing)
        {
            LOG_INFO("screenshare board: closed by host %lld.", static_cast<long long>(n.m_actor));
            closeMyBoardLocal();
            showNotice(TRF(L"{0} 关闭了你的白板", actorName).c_str());
        }
        break;

    //未知动作：新服务端加了本客户端不认识的动作，记日志后忽略
    default:
        LOG_WARNING("screenshare board: unknown action %d ignored.", n.m_action);
        break;
    }
}

void CShareController::syncBoardOwners(const ShareStateNotify& n)
{
    //当前主持人的成员座位（2026-10-07，方案第 14.3 节第 1 条）：移交后标注的清空与整笔补发改认新主持人
    for (size_t j = 0; j < n.m_members.size(); ++j)
    {
        if (n.m_members[j].m_userId == n.m_hostId)
        {
            if (m_annot.Model().HostSeat() != n.m_members[j].m_seat)
                LOG_INFO("screenshare: annotation host seat %d -> %d.", m_annot.Model().HostSeat(), n.m_members[j].m_seat);
            m_annot.SetHostSeat(n.m_members[j].m_seat);
            break;
        }
    }
    //白板的建立者按 userid 给出，标注按成员座位区分发送者：在成员表里查出建立者的成员座位
    std::map<int, int> owners;
    for (size_t i = 0; i < n.m_whiteboards.size(); ++i)
    {
        for (size_t j = 0; j < n.m_members.size(); ++j)
        {
            if (n.m_members[j].m_userId == n.m_whiteboards[i].m_ownerId)
            {
                owners[n.m_whiteboards[i].m_seat] = n.m_members[j].m_seat;
                break;
            }
        }
    }
    m_annot.SetBoardOwners(owners);
}

void CShareController::syncBoards(const ShareStateNotify& n)
{
    if (m_host == NULL)
        return;
    const int64_t self = m_host->GetSelfUserId();
    //主持人「关闭白板 ▸」（2026-10-08）：别人的白板交给控制条「更多」与观看窗「主持人 ▾」；不是主持人时清空
    std::vector<ShareBoardCloseItem> closeItems;
    if (isRoomHost())
    {
        std::map<int64_t, std::wstring> names;
        for (size_t i = 0; i < n.m_whiteboards.size(); ++i)
            names[n.m_whiteboards[i].m_ownerId] = displayName(n.m_whiteboards[i].m_ownerId);
        BuildShareBoardCloseItems(n.m_whiteboards, self, names, closeItems);
    }
    m_bar.SetOtherBoards(closeItems);
    m_viewer.SetHostBoards(closeItems);
    int mySeat = -1;
    for (size_t i = 0; i < n.m_whiteboards.size(); ++i)
    {
        if (n.m_whiteboards[i].m_ownerId == self)
            mySeat = n.m_whiteboards[i].m_seat;
    }
    //本人的白板已不在快照里（被主持人关掉、本人离开后又进来）：收尾
    if (m_boardPhase == share_viewer_mine_sharing && mySeat < 0)
    {
        LOG_INFO("screenshare board: own board %d gone per state.", m_myBoardSeat);
        closeMyBoardLocal();
    }
    //申请中、快照里已经有了本人的白板（1738 的同意没收到）：打开
    else if (m_boardPhase == share_viewer_mine_requested && mySeat >= 0)
    {
        openMyBoard(mySeat);
    }
    //主持人一侧：快照里在申请、又没答复过的补进队列；已离开或不再申请的去掉
    if (!isRoomHost())
    {
        if (m_boardRequests.Size() > 0 || !m_boardAnswered.empty())
        {
            m_boardRequests.Clear();
            m_boardAnswered.clear();
            m_boardToast.Hide();
        }
        return;
    }
    std::set<int64_t> stillRequesting;
    for (size_t i = 0; i < n.m_members.size(); ++i)
    {
        const ShareMember& m = n.m_members[i];
        if (!m.m_boardRequesting)
            continue;
        stillRequesting.insert(m.m_userId);
        if (m_boardAnswered.count(m.m_userId) == 0)
            m_boardRequests.Add(m.m_userId, displayName(m.m_userId));
    }
    for (std::set<int64_t>::iterator it = m_boardAnswered.begin(); it != m_boardAnswered.end();)
    {
        if (stillRequesting.count(*it) == 0)
            m_boardAnswered.erase(it++);
        else
            ++it;
    }
    m_boardRequests.Prune(n);
    showNextBoardRequest();
}

LRESULT CShareController::OnBoardCloseResultMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)lParam;
    (void)bHandled;
    const int64_t owner = m_boardCloseOwner;
    m_boardCloseOwner = 0;
    //「取消」或关窗：不关
    if (wParam == 0 || owner == 0)
        return 0;
    //确认期间本人已不是主持人，或那块白板已经关了：不再发
    bool stillOpen = false;
    for (size_t i = 0; i < m_lastState.m_whiteboards.size(); ++i)
    {
        if (m_lastState.m_whiteboards[i].m_ownerId == owner)
            stillOpen = true;
    }
    if (!isRoomHost() || !stillOpen)
    {
        LOG_INFO("screenshare board: close board of user %lld skipped, host %d, still open %d.",
                 static_cast<long long>(owner), isRoomHost() ? 1 : 0, stillOpen ? 1 : 0);
        return 0;
    }
    sendSharing(share_sharing_stop, owner, share_kind_whiteboard);
    LOG_INFO("screenshare board: host closes the board of user %lld.", static_cast<long long>(owner));
    return 0;
}

void CShareController::requestCloseBoardOf(int64_t ownerId)
{
    if (!isRoomHost() || ownerId == 0 || m_host == NULL || ownerId == m_host->GetSelfUserId())
        return;
    m_boardCloseOwner = ownerId;
    ShareConfirmSpec spec;
    spec.m_title = TR(L"关闭白板");
    spec.m_prompt = TRF(L"关闭 {0} 的白板？白板上的内容不会保留。", displayName(ownerId));
    spec.m_okText = TR(L"关闭");
    spec.m_resultMsg = kMsgShareBoardCloseResult;
    //原主持人共享屏幕时，确认框浮在被共享的内容上，排除在采集之外
    m_boardCloseDlg.OpenCentered(mainWindow(), m_hWnd, spec, m_hostPhase == host_streaming);
    LOG_INFO("screenshare board: asked to close the board of user %lld.", static_cast<long long>(ownerId));
}

void CShareController::openMyBoard(int seat)
{
    m_myBoardSeat = seat;
    m_boardPhase = share_viewer_mine_sharing;
    m_viewer.SetBoardState(m_boardPhase);
    m_bar.SetBoardOpen(true);
    if (!m_boardWindow.OpenShared(mainWindow(), &m_annot, this, seat, m_hWnd))
    {
        LOG_ERROR("screenshare board: open board window failed, seat %d.", seat);
        sendSharing(share_sharing_stop, 0, share_kind_whiteboard);
        closeMyBoardLocal();
        showNotice(TR(L"无法打开白板"));
        return;
    }
    //在白板上标注：标注工具条摆在白板窗口下方
    if (m_annotMode)
        setAnnotMode(false);
    m_annotOnBoard = true;
    setAnnotMode(true);
    m_boardViewDirty = true;
    m_boardViewSentMs = 0;
    SetTimer(kBoardViewTimerId, kBoardViewTimerMs, NULL);
    LOG_INFO("screenshare board: own board opened, seat %d.", seat);
}

bool CShareController::closeMyBoard(bool prompt)
{
    if (m_boardPhase != share_viewer_mine_sharing)
        return true;
    const int seat = m_myBoardSeat;
    //白板上有笔迹：关闭后不保留，先问要不要保存为图片
    if (prompt && m_annot.IsRunning() && !AnnotStrokesOnCanvas(m_annot.Model().Strokes(), seat).empty())
    {
        const HWND owner = m_boardWindow.IsOpen() ? m_boardWindow.m_hWnd : mainWindow();
        const int answer = ::MessageBoxW(owner, TR(L"白板上的内容关闭后不会保留，要先保存为图片吗？"), TR(kBoardCaption),
                                         MB_YESNOCANCEL | MB_ICONQUESTION);
        if (answer == IDCANCEL)
            return false;
        //选了保存但在另存为框里取消：白板不关
        if (answer == IDYES && !saveBoardImage(seat))
            return false;
    }
    sendSharing(share_sharing_stop, 0, share_kind_whiteboard);
    LOG_INFO("screenshare board: own board %d closed.", seat);
    closeMyBoardLocal();
    return true;
}

void CShareController::closeMyBoardLocal()
{
    if (IsWindow())
        KillTimer(kBoardViewTimerId);
    if (m_annotMode && m_annotOnBoard)
        setAnnotMode(false);
    m_annotOnBoard = false;
    m_boardWindow.Close();
    if (m_myBoardSeat >= 0 && m_annot.IsRunning() && m_annot.ClearCanvasLocal(m_myBoardSeat))
        annotationChanged();
    m_myBoardSeat = -1;
    m_boardPhase = share_viewer_mine_none;
    m_boardViewDirty = false;
    m_viewer.SetBoardState(m_boardPhase);
    m_bar.SetBoardOpen(false);
}

void CShareController::sendBoardView(bool force)
{
    if (m_boardPhase != share_viewer_mine_sharing || m_myBoardSeat < 0 || !m_boardWindow.IsOpen())
        return;
    if (!force)
    {
        m_boardViewDirty = true;
        return;
    }
    AnnotationBoardView v;
    if (!m_boardWindow.VisibleRegion(v.m_left, v.m_top, v.m_width))
        return;
    v.m_background = m_boardWindow.Background();
    if (m_annot.SendBoardView(m_myBoardSeat, v))
    {
        m_boardViewDirty = false;
        m_boardViewSentMs = static_cast<int64_t>(::GetTickCount64());
    }
}

void CShareController::onBoardViewTimer()
{
    const int64_t now = static_cast<int64_t>(::GetTickCount64());
    //有变化时至少隔 kBoardViewTimerMs 发一次；没有变化也每 kBoardViewKeepaliveMs 重发一次，后加入的人据此画背景与跟随
    if ((m_boardViewDirty && now - m_boardViewSentMs >= kBoardViewTimerMs) ||
        now - m_boardViewSentMs >= kBoardViewKeepaliveMs)
        sendBoardView(true);
}

bool CShareController::isBoardCanvas(int canvas) const
{
    if (canvas >= 0 && canvas == m_myBoardSeat)
        return true;
    for (size_t i = 0; i < m_screens.size(); ++i)
    {
        if (m_screens[i].m_isWhiteboard && m_screens[i].m_screenSeat == canvas)
            return true;
    }
    return false;
}

void CShareController::showNextBoardRequest()
{
    const int64_t front = m_boardRequests.FrontUser();
    if (front == 0)
    {
        m_boardToast.Hide();
        return;
    }
    //共享名额已满：申请先留在队列里，等有人停下后（下一次快照）再提示
    if (m_lastState.m_maxSharers > 0 && ShareSlotsUsed(m_lastState) >= m_lastState.m_maxSharers)
    {
        if (m_boardToast.IsWindow())
            m_boardToast.Hide();
        return;
    }
    if (m_boardToast.IsWindow() && m_boardToast.Token() == static_cast<LPARAM>(front))
        return;
    ShareToastTexts texts;
    texts.m_title = TR(L"白板申请");
    texts.m_accept = TR(L"同意");
    texts.m_ignore = TR(L"拒绝");
    texts.m_message = kMsgShareBoardToast;
    m_boardToast.Show(TRF(L"{0} 申请新建白板", m_boardRequests.FrontName()), static_cast<LPARAM>(front), m_hWnd, 1, texts);
    if (m_boardToast.IsWindow())
        ExcludeWindowFromCapture(m_boardToast.m_hWnd);
}

LRESULT CShareController::OnBoardToastMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)bHandled;
    const int64_t userId = static_cast<int64_t>(lParam);
    if (!isRoomHost() || userId <= 0)
        return 0;
    m_boardRequests.Remove(userId);
    m_boardAnswered.insert(userId);
    if (static_cast<int>(wParam) == CShareCornerToast::kCmdJoin)
        sendSharing(share_sharing_approve, userId, share_kind_whiteboard);
    else
        sendSharing(share_sharing_reject, userId, share_kind_whiteboard);
    showNextBoardRequest();
    return 0;
}

LRESULT CShareController::OnBoardCommandMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)lParam;
    (void)bHandled;
    switch (static_cast<int>(wParam))
    {
    //换了背景：马上广播
    case share_board_cmd_background:
        sendBoardView(true);
        break;

    //「工具条」：在白板上进入标注（工具条随之显示）
    case share_board_cmd_toolbar:
        if (m_annotMode)
            setAnnotMode(false);
        m_annotOnBoard = true;
        setAnnotMode(true);
        break;

    //可见区域变了（滚动、缩放、窗口大小）：交给定时器限频广播
    case share_board_cmd_view:
        sendBoardView(false);
        break;

    //关窗：有笔迹时先问要不要保存，再关闭这块白板
    case share_board_cmd_close:
        closeMyBoard(true);
        break;

    //白板窗口移动或改变大小：标注工具条跟着走
    case share_board_cmd_moved:
        if (m_annotMode && m_annotOnBoard)
            m_annotBar.MoveTo(m_boardWindow.WindowRect());
        break;

    //未知命令：忽略
    default:
        break;
    }
    return 0;
}

//==================== 屏幕当白板 ====================

void CShareController::toggleScreenBoard()
{
    if (m_hostPhase != host_streaming)
        return;
    //退出：有笔迹时先问要不要保存，再恢复只共享原窗口
    if (m_screenBoard)
    {
        if (m_annot.IsRunning() && !AnnotStrokesOnCanvas(m_annot.Model().Strokes(), m_selfSeat).empty())
        {
            const int answer = ::MessageBoxW(mainWindow(), TR(L"屏幕白板上的内容退出后不会保留，要先保存为图片吗？"),
                                             TR(kBoardCaption), MB_YESNOCANCEL | MB_ICONQUESTION);
            if (answer == IDCANCEL)
                return;
            if (answer == IDYES)
            {
                m_annot.SetCanvas(m_selfSeat);
                if (!saveAnnotImage())
                    return;
            }
        }
        m_screenBoard = false;
        if (m_annotMode)
            setAnnotMode(false);
        LOG_INFO("screenshare board: screen board off, back to window.");
        switchHostSource(m_screenBoardSaved);
        return;
    }
    if (m_hostSource.m_kind != share_source_window)
    {
        showNotice(TR(L"只共享一个窗口时才能把它所在的屏幕当白板"));
        return;
    }
    if (::MessageBoxW(mainWindow(), TR(L"观看者将看到整块屏幕。确定把共享的窗口所在的屏幕当白板吗？"), TR(kBoardCaption),
                      MB_OKCANCEL | MB_ICONQUESTION) != IDOK)
        return;
    ShareSourceSpec monitor;
    if (!MonitorSourceForWindow(m_hostSource.m_hwnd, monitor))
    {
        showNotice(TR(L"找不到共享的窗口所在的屏幕"));
        return;
    }
    m_screenBoardSaved = m_hostSource;
    switchHostSource(monitor);
    m_screenBoard = true;
    m_bar.SetScreenBoard(true, true);
    LOG_INFO("screenshare board: screen board on.");
    setAnnotMode(true);
}

void CShareController::toggleSharerScreenBoard()
{
    if (m_publishPhase != share_publish_streaming)
        return;
    const int seat = m_publishRelay.m_seat;
    //退出：有笔迹时先问要不要保存，再恢复只共享原窗口
    if (m_publishScreenBoard)
    {
        if (m_annot.IsRunning() && !AnnotStrokesOnCanvas(m_annot.Model().Strokes(), seat).empty())
        {
            const int answer = ::MessageBoxW(mainWindow(), TR(L"屏幕白板上的内容退出后不会保留，要先保存为图片吗？"),
                                             TR(kBoardCaption), MB_YESNOCANCEL | MB_ICONQUESTION);
            if (answer == IDCANCEL)
                return;
            if (answer == IDYES)
            {
                m_annot.SetCanvas(seat);
                if (!saveAnnotImage())
                    return;
            }
        }
        m_publishScreenBoard = false;
        if (m_annotMode && m_annotOwnScreen)
            setPublisherAnnotMode(false);
        LOG_INFO("screenshare board: sharer screen board off, back to window, seat=%d.", seat);
        switchPublishSource(m_publishScreenBoardSaved);
        return;
    }
    if (m_publishSource.m_kind != share_source_window)
    {
        showNotice(TR(L"只共享一个窗口时才能把它所在的屏幕当白板"));
        return;
    }
    if (::MessageBoxW(mainWindow(), TR(L"观看者将看到整块屏幕。确定把共享的窗口所在的屏幕当白板吗？"), TR(kBoardCaption),
                      MB_OKCANCEL | MB_ICONQUESTION) != IDOK)
        return;
    //确认框开着时共享可能已经停了
    if (m_publishPhase != share_publish_streaming)
        return;
    ShareSourceSpec monitor;
    if (!MonitorSourceForWindow(m_publishSource.m_hwnd, monitor))
    {
        showNotice(TR(L"找不到共享的窗口所在的屏幕"));
        return;
    }
    m_publishScreenBoardSaved = m_publishSource;
    //先记下在屏幕白板里，switchPublishSource 据此把共享者控制条的按钮改成「退出屏幕白板」
    m_publishScreenBoard = true;
    switchPublishSource(monitor);
    LOG_INFO("screenshare board: sharer screen board on, seat=%d.", seat);
    //进入在自己共享的内容上标注（没有标注权限时只提示）
    if (!(m_annotMode && m_annotOwnScreen))
    {
        if (m_annot.CanDrawOn(seat))
            setPublisherAnnotMode(true);
        else
            showNotice(TR(L"发起人已关闭标注"));
    }
}

//==================== 保存为图片 ====================

bool CShareController::saveAnnotImage()
{
    if (!m_annot.IsRunning())
        return false;
    const int canvas = m_annot.Canvas();
    if (isBoardCanvas(canvas))
        return saveBoardImage(canvas);
    //共享画面：取这块画面当前的一帧（本人共享的取采到的原始画面，别人的取解码出的画面）
    std::vector<uint8_t> frame;
    int width = 0;
    int height = 0;
    uint64_t serial = 0;
    bool ok = false;
    if (m_hostPhase == host_streaming && canvas == m_selfSeat)
        ok = m_streamer.GetRecordFrame(frame, width, height, serial);
    else if (m_publishPhase == share_publish_streaming && canvas == m_publishRelay.m_seat)
        ok = m_publisher.GetRecordFrame(frame, width, height, serial);
    else if (m_viewerPhase == viewer_watching)
        ok = m_viewer.GetScreenFrame(canvas, frame, width, height);
    if (!ok || frame.empty())
    {
        showNotice(TR(L"还没有画面，无法保存为图片"));
        return false;
    }
    std::vector<uint8_t> image;
    RenderScreenImage(frame, width, height, AnnotStrokesOnCanvas(m_annot.Model().Strokes(), canvas), image);
    if (image.empty())
    {
        showNotice(TR(L"还没有画面，无法保存为图片"));
        return false;
    }
    return savePngWithDialog(image, width, height, false);
}

bool CShareController::saveBoardImage(int seat)
{
    //背景：本人的白板取白板窗口当前的，别人的取最近收到的白板视野
    int background = wb_bg_grid;
    AnnotationBoardView view;
    if (seat == m_myBoardSeat && m_boardWindow.IsOpen())
        background = m_boardWindow.Background();
    else if (m_annot.Model().BoardView(seat, view))
        background = view.m_background;
    std::vector<uint8_t> image;
    RenderBoardImage(kBoardExportWidth, kBoardExportHeight, background,
                     AnnotStrokesOnCanvas(m_annot.Model().Strokes(), seat), image);
    return savePngWithDialog(image, kBoardExportWidth, kBoardExportHeight, true);
}

bool CShareController::savePngWithDialog(const std::vector<uint8_t>& bgra, int width, int height, bool whiteboard)
{
    if (bgra.empty())
        return false;
    SYSTEMTIME now;
    ::GetLocalTime(&now);
    wchar_t file[MAX_PATH] = { 0 };
    wcsncpy_s(file, MAX_PATH, MakeAnnotExportFileName(whiteboard, now).c_str(), _TRUNCATE);
    //默认放到「图片」文件夹；取不到时由对话框自己决定
    wchar_t dir[MAX_PATH] = { 0 };
    ::SHGetFolderPathW(NULL, CSIDL_MYPICTURES, NULL, 0, dir);
    const std::wstring filter = filedlg::MakeFilter(TR(L"PNG 图片"), L"*.png");
    OPENFILENAMEW ofn;
    memset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = (whiteboard && m_boardWindow.IsOpen()) ? m_boardWindow.m_hWnd : mainWindow();
    ofn.lpstrFilter = filter.c_str();
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrInitialDir = (dir[0] != 0) ? dir : NULL;
    ofn.lpstrDefExt = L"png";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER;
    if (!::GetSaveFileNameW(&ofn))
        return false;
    if (!SaveBgraAsPng(file, bgra, width, height))
    {
        LOG_ERROR("screenshare: save annotation image failed, %dx%d.", width, height);
        showError(TR(L"保存图片失败，请换个位置再试。"));
        return false;
    }
    m_savedPath = file;
    //发到哪个会话：保存时所在的这场共享的会话
    m_saveCtype = (m_hostPhase != host_idle) ? m_hostCtype : m_viewerCtype;
    m_saveCid = (m_hostPhase != host_idle) ? m_hostCid : m_viewerCid;
    LOG_INFO("screenshare: annotation image saved, %dx%d, whiteboard %d.", width, height, whiteboard ? 1 : 0);
    //右下角提示：可一键发到本会话
    ShareToastTexts texts;
    texts.m_title = TR(L"已保存为图片");
    texts.m_accept = TR(L"发送到本会话");
    texts.m_ignore = TR(L"关闭");
    texts.m_message = kMsgShareSaveToast;
    ++m_saveToastToken;
    m_saveToast.Show(TRF(L"已保存为 {0}", fileNameOf(m_savedPath)), m_saveToastToken, m_hWnd, 1, texts);
    if (m_saveToast.IsWindow())
        ExcludeWindowFromCapture(m_saveToast.m_hWnd);
    return true;
}

LRESULT CShareController::OnSaveToastMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)bHandled;
    if (lParam != m_saveToastToken || static_cast<int>(wParam) != CShareCornerToast::kCmdJoin)
        return 0;
    if (m_host == NULL || m_savedPath.empty() || m_saveCid == 0)
        return 0;
    //以图片消息发送（2026-10-07，方案第 14.1 节 ⑤），对方在聊天里直接看到图片；超过图片消息的大小上限等情况退回文件消息
    if (m_host->SendImageToConversation(m_saveCtype, m_saveCid, m_savedPath.c_str()))
    {
        LOG_INFO("screenshare: annotation image sent as an image message, ctype=%d, cid=%lld.", m_saveCtype,
                 static_cast<long long>(m_saveCid));
        showNotice(TR(L"图片已发到本会话"));
    }
    else if (m_host->SendFileToConversation(m_saveCtype, m_saveCid, m_savedPath.c_str()))
    {
        LOG_INFO("screenshare: annotation image sent as a file, ctype=%d, cid=%lld.", m_saveCtype,
                 static_cast<long long>(m_saveCid));
        showNotice(TR(L"图片已发到本会话"));
    }
    else
    {
        LOG_WARNING("screenshare: send annotation image failed, ctype=%d, cid=%lld.", m_saveCtype,
                    static_cast<long long>(m_saveCid));
        showNotice(TR(L"图片发送失败"));
    }
    return 0;
}

} // namespace screenshare
