/**
 *  屏幕共享控制器的控制键鼠部分（第 18 期，docs/屏幕共享与录制方案.md 第 2 节第 24～26 条），ShareControllerControl.cpp
 *
 *  CShareController 的一部分实现，单独成文件只为不让 ShareController.cpp 继续变长；成员声明都在 ShareController.h。
 *    - 观看者：观看窗点「申请控制」发 1735 申请（多人共享时申请的是当前那一块画面的主人）；画面主人同意后（1725 里他的
 *      controlledby 是本人）进入控制，观看窗把那一块画面上的键鼠换成归一化坐标交给本类，组成键鼠输入控制帧只发给
 *      那块画面的座位；「结束控制」或画面主人收回后退出；
 *    - 画面主人（第 22 期起为任何正在共享的人：建房间的主持人，或另连一条中转连接推屏幕的共享者）：1736 收到申请时在
 *      右下角弹「某某申请控制你的屏幕」，同意即授予、拒绝即拒绝，主持人在名单里也可
 *      主动授予或收回；被控制期间核对每帧输入的发送者确实是控制者，换回本机屏幕坐标、过滤系统级按键后交给远程桌面的
 *      RdInputInjector 注入；装低级鼠标、键盘钩子，本机有动静（不是注入出来的输入）即收回；结束控制时补发还按着的
 *      按键与鼠标按钮的抬起。
 *  纯逻辑（帧格式、坐标换算、按键过滤、本机动静判定）在 ShareRemoteControl 里，单元测试见 ShareRemoteControlTests.cpp。
 *
 *  balloonwj@qq.com   2026-10-05
 */
#include "stdafx.h"

#include "ShareController.h"

#include <algorithm>
#include <chrono>
#include <memory>
#include "ShareSources.h"                        // ExcludeWindowFromCapture
#include "ShareRoomPolicy.h"                      // MakeShareControlStartNotice、DecideShareControlTarget
#include "../RemoteDesktop/RdInputDef.h"          // rd_input_mouse
#include "../RemoteDesktop/RdInputInjector.h"     // RdInputInjector::inject（注入复用远程桌面）
#include "../RemoteDesktop/RdInputEncoder.h"      // RdInputEncoder：结束控制时补发的松开编成命令字节
#include "../../Source/net/Protocol.h"
#include "../../Source/Common/Log/IULog.h"
#include "../../Source/Common/I18n/I18n.h"        // TR：界面文字的多语言查找

namespace screenshare {

namespace {

//中转接收线程交给 UI 线程的一帧键鼠输入
struct ShareInputFrameMsg
{
    int             m_senderSeat;   //发送者座位号
    int             m_targetSeat;   //帧里写的目标座位号
    ShareInputEvent m_event;        //输入
    int64_t         m_postedUs;     //接收线程投递的时刻（inputQueueNowUs，微秒）；界面线程据此统计排队时长（2026-10-17）
};

//微秒与毫秒的换算（排队时长写日志时用）
const double kInputQueueUsPerMs = 1000.0;

//排队时长统计用的单调时钟，单位微秒（steady_clock 在 Windows 上基于 QueryPerformanceCounter，精度远好于 GetTickCount）
int64_t inputQueueNowUs()
{
    return static_cast<int64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

//在「座位号 → userid」表里找某人的座位号；找不到返回 -1
int seatOfUser(const std::map<int, int64_t>& seatUsers, int64_t userId)
{
    for (std::map<int, int64_t>::const_iterator it = seatUsers.begin(); it != seatUsers.end(); ++it)
    {
        if (it->second == userId)
            return it->first;
    }
    return -1;
}

//窗口所在的顶层窗口；NULL 时返回 NULL
HWND rootOf(HWND hwnd)
{
    return hwnd != NULL ? ::GetAncestor(hwnd, GA_ROOT) : NULL;
}

#ifdef _DEBUG
//调试版「控制键鼠只记日志」开关的环境变量名：值为 1 时画面主人收到输入只写日志、不注入，也不装检测本机动静的钩子。
//用于在共用桌面上联调控制键鼠的整条链路，不碰真实的鼠标与键盘（与 FLAMINGO_SHARE_NO_AUDIO 同一做法）
const char* const kControlDryRunEnv = "FLAMINGO_SHARE_CONTROL_DRYRUN";

//是否打开了「控制键鼠只记日志」开关
bool controlDryRun()
{
    char value[8] = { 0 };
    size_t len = 0;
    if (getenv_s(&len, value, sizeof(value), kControlDryRunEnv) != 0 || len == 0)
        return false;
    return value[0] == '1';
}
#else
//发布版没有这个开关
bool controlDryRun()
{
    return false;
}
#endif

/**
 *  把一条键鼠输入换成远程桌面的输入命令（RdControlGate 判定与记录用）。
 *  @param ev 输入
 *  @param pt 鼠标输入换回画面主人屏幕上的位置；键盘输入不用
 */
remotedesktop::RdInputCommand toRdInputCommand(const ShareInputEvent& ev, POINT pt)
{
    remotedesktop::RdInputCommand cmd;
    cmd.type = ev.m_type;
    cmd.message = ev.m_message;
    cmd.x = pt.x;
    cmd.y = pt.y;
    cmd.wheel = ev.m_wheel;
    cmd.vkey = ev.m_vkey;
    return cmd;
}

} // namespace

CShareController::ControlRequest::ControlRequest()
    : m_userId(0)
{
}

//==================== 信令 ====================

void CShareController::sendControl(int action, int64_t owner, int64_t target)
{
    const std::string& roomId = currentRoomId();
    if (roomId.empty() || owner <= 0)
        return;
    send(cmd_screenshare_control, ShareProtocolClient::buildControl(roomId, action, owner, target));
    LOG_INFO("screenshare control: sent action=%d, owner=%lld, target=%lld, room=%s.", action,
             static_cast<long long>(owner), static_cast<long long>(target), roomId.c_str());
}

void CShareController::onControlResult(const std::string& json)
{
    ShareResult r;
    if (!ShareProtocolClient::parseResult(json, r) || r.m_code == 0)
        return;
    LOG_WARNING("screenshare control: request failed, code=%d, msg=%s.", r.m_code, r.m_msg.c_str());
    //观看者的申请没有成功：回到「申请控制」
    if (m_viewerControl == share_viewer_control_requested)
        setViewerControlState(share_viewer_control_none);
    if (r.m_code == error_code_screenshare_not_sharing)
        showNotice(TR(L"对方暂时不在共享屏幕，无法控制"));
    else if (r.m_code == error_code_screenshare_not_owner)
        showNotice(TR(L"只有画面的主人才能这样操作"));
    else
        showNotice(TRF(L"控制操作失败（错误码 {0}）", r.m_code).c_str());
}

void CShareController::onControlNotify(const std::string& json)
{
    ShareControlNotify n;
    if (!ShareProtocolClient::parseControlNotify(json, n))
        return;
    std::wstring actorName = utf8ToWide(n.m_actorName);
    if (actorName.empty())
        actorName = displayName(n.m_actor);
    //画面主人（本人正有一块画面在共享）：收到申请或撤回
    if ((n.m_action == share_control_request || n.m_action == share_control_cancel) && isScreenOwner()
        && n.m_roomId == currentRoomId())
    {
        if (n.m_action == share_control_request)
        {
            bool queued = false;
            for (size_t i = 0; i < m_controlRequests.size(); ++i)
                queued = queued || (m_controlRequests[i].m_userId == n.m_actor);
            if (!queued)
            {
                ControlRequest req;
                req.m_userId = n.m_actor;
                req.m_name = actorName;
                m_controlRequests.push_back(req);
            }
            LOG_INFO("screenshare control: request from user %lld queued, pending=%u.",
                     static_cast<long long>(n.m_actor), static_cast<unsigned>(m_controlRequests.size()));
            showNextControlRequest();
        }
        else if (n.m_action == share_control_cancel)
        {
            LOG_INFO("screenshare control: request from user %lld cancelled.", static_cast<long long>(n.m_actor));
            dropControlRequest(n.m_actor);
        }
        return;
    }
    //观看者：申请被拒绝
    if (m_viewerPhase == viewer_watching && n.m_roomId == m_viewerRoomId && n.m_action == share_control_deny)
    {
        LOG_INFO("screenshare control: request denied by user %lld.", static_cast<long long>(n.m_actor));
        if (m_viewerControl == share_viewer_control_requested)
            setViewerControlState(share_viewer_control_none);
        showNotice(TRF(L"{0} 拒绝了你的控制申请", actorName).c_str());
    }
}

void CShareController::applyControlState(const ShareStateNotify& n, bool asHost)
{
    //2026-10-07 起主持人与观看者走同一套逻辑（主持人在「其他人的屏幕」窗口里也能控制别人），不再按身份提前返回
    (void)asHost;
    const int64_t self = m_host->GetSelfUserId();
    //画面主人一侧（第 22 期起任何正在共享的人）：看本人成员项的 controlledby；本人这块画面不在共享时一律按没人控制
    const ShareMember* selfMember = n.findMember(self);
    int64_t controller = (isScreenOwner() && selfMember != NULL) ? selfMember->m_controlledBy : 0;
    //本地已收回、服务端还没确认：仍带着这位控制者的快照不让控制恢复；快照里换了人或已清零时确认结束
    if (m_revokingId != 0)
    {
        if (controller == m_revokingId)
            controller = 0;
        else
            m_revokingId = 0;
    }
    const int64_t prev = m_controllerId;
    setControlledBy(controller);
    if (prev == 0 && controller != 0)
    {
        showNotice(MakeShareControlStartNotice(displayName(controller)).c_str());
    }
    else if (prev != 0 && controller == 0)
    {
        //控制者自己结束时说明是他结束的，其余（画面主人收回、控制者离开）只说控制已结束
        if (n.m_event == share_event_control_revoked && n.m_actor == prev)
            showNotice(TRF(L"{0} 已结束控制", displayName(prev)).c_str());
        else
            showNotice(TR(L"控制已结束"));
    }
    else if (prev != 0 && controller != 0 && prev != controller)
    {
        showNotice(TRF(L"改由 {0} 控制你的屏幕", displayName(controller)).c_str());
    }
    //排队的申请人已离开房间：从队列里去掉
    std::vector<int64_t> gone;
    for (size_t i = 0; i < m_controlRequests.size(); ++i)
    {
        if (n.findMember(m_controlRequests[i].m_userId) == NULL)
            gone.push_back(m_controlRequests[i].m_userId);
    }
    for (size_t i = 0; i < gone.size(); ++i)
        dropControlRequest(gone[i]);

    //控制别人的一侧：观看者在观看窗里，主持人（2026-10-07，方案第 14.3 节第 4 条）在「其他人的屏幕」窗口里。
    //本人正在控制谁的画面（服务端保证一个人同时只控制一块画面）
    int64_t controlling = 0;
    int controlSeat = -1;
    for (size_t i = 0; i < n.m_members.size(); ++i)
    {
        const ShareMember& m = n.m_members[i];
        if (m.m_userId != self && m.m_sharing && m.m_controlledBy == self && self != 0)
        {
            controlling = m.m_userId;
            controlSeat = m.m_screenSeat;
        }
    }
    if (controlling != 0)
    {
        const bool changed = (m_viewerControl != share_viewer_control_active || m_controlOwner != controlling);
        m_controlOwner = controlling;
        //按这一块画面的矩形换算坐标（主人在名单里主动授予时本人可能没有先申请）
        setControlScreen(controlSeat);
        if (changed)
        {
            setViewerControlState(share_viewer_control_active);
            showNotice(TR(L"你已获得控制权，可以直接操作对方的屏幕"));
        }
        return;
    }
    if (m_viewerControl == share_viewer_control_active)
    {
        const ShareMember* owner = n.findMember(m_controlOwner);
        setViewerControlState(share_viewer_control_none);
        if (n.m_event == share_event_control_revoked && n.m_actor == m_controlOwner)
            showNotice(TR(L"对方已收回控制"));
        else if (owner != NULL && owner->m_controlledBy != 0)
            showNotice(TR(L"对方已把控制权交给别人"));
        else
            showNotice(TR(L"控制已结束"));
        m_controlOwner = 0;
        setControlScreen(-1);
        return;
    }
    //申请中：画面主人掉线、离开或不再共享时，还没答复的申请随之作废
    if (m_viewerControl == share_viewer_control_requested)
    {
        const ShareMember* owner = n.findMember(m_controlOwner);
        const bool ownerGone = (owner == NULL || !owner->m_sharing
                                || (m_controlOwner == n.m_hostId && !n.m_hostOnline));
        if (ownerGone)
        {
            setViewerControlState(share_viewer_control_none);
            m_controlOwner = 0;
        }
    }
}

//==================== 画面主人一侧 ====================

void CShareController::setControlledBy(int64_t controllerId)
{
    if (controllerId == m_controllerId)
        return;
    //结束或换人：先把上一位控制者按着的键与按钮补发抬起
    if (m_controllerId != 0)
    {
        releaseInjectedInput();
        //键鼠帧在界面线程队列里的排队时长（2026-10-17）：平均与最长，单位毫秒
        const double avgMs = (m_inputQueueFrames > 0)
            ? static_cast<double>(m_inputQueueTotalUs) / m_inputQueueFrames / kInputQueueUsPerMs : 0.0;
        const double maxMs = static_cast<double>(m_inputQueueMaxUs) / kInputQueueUsPerMs;
        LOG_INFO("screenshare control: control by user %lld ended, injected=%u, dropped=%u, ui queue avg=%.2f ms, "
                 "max=%.2f ms over %u frames.", static_cast<long long>(m_controllerId), m_injectedCount, m_droppedCount,
                 avgMs, maxMs, m_inputQueueFrames);
    }
    m_controllerId = controllerId;
    m_injectedCount = 0;
    m_droppedCount = 0;
    m_inputQueueTotalUs = 0;
    m_inputQueueMaxUs = 0;
    m_inputQueueFrames = 0;
    //被控制期间推流帧率提到 kShareControlledFps，结束后恢复（2026-10-17）。本人这块画面由主持人推流管线或共享者推流管线
    //之一推出，两条都设：没在推流的那条只记下状态，不产生任何效果
    m_streamer.SetControlled(controllerId != 0);
    m_publisher.SetControlled(controllerId != 0);
    //主持人的控制条与非主持人共享者的共享者条都显示控制者与「收回控制」（2026-10-12）；没显示的那条只记下状态
    if (controllerId == 0)
    {
        m_bar.SetControlledBy(std::wstring());
        m_sharerBar.SetControlledBy(std::wstring());
        return;
    }
    dropControlRequest(controllerId);
    //开始被控制：打开控制把关，此后这位控制者的输入按规则注入并记下按着的键与按钮
    const POINT notUsed = { 0, 0 };
    std::vector<remotedesktop::RdInputCommand> noReleases;
    m_controlGate.SetAllowed(true, notUsed, noReleases);
    m_bar.SetControlledBy(displayName(controllerId));
    m_sharerBar.SetControlledBy(displayName(controllerId));
    //只共享一个窗口：控制前把它切到前台，键盘输入才落得到它上面（系统可能拒绝，点一下窗口同样会切过去）
    const ShareSourceSpec& source = ownSource();
    if (source.m_kind == share_source_window && ::IsWindow(source.m_hwnd))
    {
        const BOOL fg = ::SetForegroundWindow(source.m_hwnd);
        LOG_INFO("screenshare control: bring shared window to foreground, ok=%d.", fg ? 1 : 0);
    }
    LOG_INFO("screenshare control: controlled by user %lld, source kind=%d.", static_cast<long long>(controllerId),
             source.m_kind);
}

void CShareController::releaseInjectedInput()
{
    //关闭控制把关，取回仍按着的键与按钮：按键按按下的逆序抬起（修饰键最后抬起），再在光标当前位置松开鼠标按钮
    POINT cursor = { 0, 0 };
    ::GetCursorPos(&cursor);
    std::vector<remotedesktop::RdInputCommand> releases;
    m_controlGate.SetAllowed(false, cursor, releases);
    m_injectKeys.Reset();
    //调试开关：没有真正注入过，也就不用补发
    if (controlDryRun() || releases.empty())
        return;
    unsigned keys = 0;
    for (size_t i = 0; i < releases.size(); ++i)
    {
        const remotedesktop::RdInputCommand& up = releases[i];
        std::vector<BYTE> bytes;
        if (up.type == remotedesktop::rd_input_key)
        {
            remotedesktop::RdInputEncoder::encodeKey(bytes, up.message, up.vkey);
            ++keys;
        }
        else
        {
            remotedesktop::RdInputEncoder::encodeMouse(bytes, up.message, up.x, up.y, 0);
        }
        remotedesktop::RdInputInjector::inject(bytes.data(), bytes.size());
    }
    LOG_INFO("screenshare control: released %u keys and %u buttons.", keys,
             static_cast<unsigned>(releases.size()) - keys);
}

void CShareController::OnInputFrame(int senderSeat, int targetSeat, const ShareInputEvent& ev)
{
    //中转接收线程上：拷贝一份转投 UI 线程；消息窗口已销毁时直接丢弃
    if (!IsWindow())
        return;
    ShareInputFrameMsg* msg = new ShareInputFrameMsg();
    msg->m_senderSeat = senderSeat;
    msg->m_targetSeat = targetSeat;
    msg->m_event = ev;
    msg->m_postedUs = inputQueueNowUs();
    if (!PostMessage(kMsgShareInputFrame, 0, reinterpret_cast<LPARAM>(msg)))
        delete msg;
}

LRESULT CShareController::OnInputFrameMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)wParam;
    (void)bHandled;
    std::unique_ptr<ShareInputFrameMsg> frame(reinterpret_cast<ShareInputFrameMsg*>(lParam));
    //只接收发给本人这块画面（建房间的主持人为自己的座位，共享者为画面座位）的输入
    if (!frame || !isScreenOwner() || m_controllerId == 0 || frame->m_targetSeat != ownScreenSeat())
        return 0;
    //排队时长统计：从接收线程投递到这里开始处理（不论之后注入还是丢弃）
    const int64_t queuedUs = inputQueueNowUs() - frame->m_postedUs;
    if (queuedUs >= 0)
    {
        m_inputQueueTotalUs += static_cast<uint64_t>(queuedUs);
        if (queuedUs > m_inputQueueMaxUs)
            m_inputQueueMaxUs = queuedUs;
        ++m_inputQueueFrames;
    }
    //暂停期间观看者看不到画面，不注入
    if (ownScreenPaused())
    {
        ++m_droppedCount;
        return 0;
    }
    const ShareInputEvent& ev = frame->m_event;
    const ShareSourceSpec& source = ownSource();
    const bool windowShare = (source.m_kind == share_source_window);
    const HWND sharedRoot = windowShare ? rootOf(source.m_hwnd) : NULL;
    POINT pt = { 0, 0 };
    bool pointOnWindow = true;
    if (ev.m_type == remotedesktop::rd_input_mouse)
    {
        pt = ShareRemoteInput::NormalizedToSource(ownSourceRect(), ev.m_x, ev.m_y);
        const HWND hit = ::WindowFromPoint(pt);
        pointOnWindow = !windowShare || (hit != NULL && rootOf(hit) == sharedRoot);
    }
    const HWND fg = ::GetForegroundWindow();
    const bool windowForeground = !windowShare || (fg != NULL && rootOf(fg) == sharedRoot);
    //落在本进程窗口上的输入不注入（Flamingo 自己的窗口里有聊天内容等私人信息）：鼠标按坐标处的窗口、键盘按前台窗口判定，
    //判定方法与远程桌面相同。鼠标松开、按键抬起不受这一限制，按下时已经注入的按钮与键必须能松开。
    //这一项须在系统级按键过滤之前：被丢掉的按键不能改变过滤器里修饰键的按下状态
    const remotedesktop::RdInputCommand rdCmd = toRdInputCommand(ev, pt);
    const bool isButtonUp = (ev.m_message == WM_LBUTTONUP || ev.m_message == WM_RBUTTONUP ||
                             ev.m_message == WM_MBUTTONUP);
    const bool isKeyUp = (ev.m_message == WM_KEYUP || ev.m_message == WM_SYSKEYUP);
    if (!isButtonUp && !isKeyUp && remotedesktop::RdControlGate::IsOnOwnProcessWindow(rdCmd))
    {
        ++m_droppedCount;
        return 0;
    }
    const int controllerSeat = seatOfUser(m_seatUsers, m_controllerId);
    const int decision = DecideShareInput(ev, frame->m_senderSeat, controllerSeat, windowShare, pointOnWindow,
                                          windowForeground, m_injectKeys);
    if (decision != share_input_inject)
    {
        ++m_droppedCount;
        //冒充或过时的输入值得一条日志；窗口外、系统级按键属于正常操作，只计数
        if (decision == share_input_not_controller)
            LOG_WARNING("screenshare control: input from seat %d dropped, controller seat %d.", frame->m_senderSeat,
                        controllerSeat);
        return 0;
    }
    //控制把关：确认仍在被控制，并记下注入后按着的键与鼠标按钮，结束控制时补发松开（本进程窗口已在前面判定过）
    if (!m_controlGate.Filter(rdCmd))
    {
        ++m_droppedCount;
        return 0;
    }
    //滚轮只滚光标所在处：先把光标挪过去（调试开关下不挪）
    if (ev.m_type == remotedesktop::rd_input_mouse && ev.m_message == WM_MOUSEWHEEL && !controlDryRun())
    {
        const std::string move = ShareRemoteInput::ToRdCommand(ShareInputEvent::Mouse(WM_MOUSEMOVE, 0, 0, 0), pt);
        remotedesktop::RdInputInjector::inject(reinterpret_cast<const BYTE*>(move.data()), move.size());
    }
    const std::string cmd = ShareRemoteInput::ToRdCommand(ev, pt);
    ++m_injectedCount;
    //调试开关：只记日志，不注入
    if (controlDryRun())
    {
        LOG_INFO("screenshare control: dry run, would inject type=%d, message=0x%x, x=%d, y=%d, vkey=%u.", ev.m_type,
                 ev.m_message, static_cast<int>(pt.x), static_cast<int>(pt.y), ev.m_vkey);
        return 0;
    }
    remotedesktop::RdInputInjector::inject(reinterpret_cast<const BYTE*>(cmd.data()), cmd.size());
    return 0;
}

void CShareController::revokeControlNow()
{
    if (m_controllerId == 0)
        return;
    //先停止注入，等服务端的 1725 再正式结束；期间到达的输入一律丢弃
    const int64_t controller = m_controllerId;
    m_revokingId = controller;
    setControlledBy(0);
    sendControl(share_control_revoke, m_host->GetSelfUserId(), 0);
    LOG_INFO("screenshare control: revoked by the screen owner, controller %lld.", static_cast<long long>(controller));
    showNotice(TRF(L"已收回 {0} 的控制", displayName(controller)).c_str());
}

void CShareController::showNextControlRequest()
{
    if (m_controlRequests.empty())
    {
        m_controlToast.Hide();
        return;
    }
    const ControlRequest& front = m_controlRequests.front();
    //队首已经显示着：不重复弹
    if (m_controlToast.IsWindow() && m_controlToast.Token() == static_cast<LPARAM>(front.m_userId))
        return;
    ShareToastTexts texts;
    texts.m_title = TR(L"控制申请");
    texts.m_accept = TR(L"同意");
    texts.m_ignore = TR(L"拒绝");
    texts.m_message = kMsgShareControlToast;
    m_controlToast.Show(TRF(L"{0} 申请控制你的屏幕", front.m_name), static_cast<LPARAM>(front.m_userId), m_hWnd, 1,
                        texts);
    //提示排除在屏幕采集之外：观看者看不到谁在申请
    if (m_controlToast.IsWindow())
        ExcludeWindowFromCapture(m_controlToast.m_hWnd);
}

void CShareController::dropControlRequest(int64_t userId)
{
    bool removed = false;
    for (std::deque<ControlRequest>::iterator it = m_controlRequests.begin(); it != m_controlRequests.end(); ++it)
    {
        if (it->m_userId == userId)
        {
            m_controlRequests.erase(it);
            removed = true;
            break;
        }
    }
    if (!removed)
        return;
    //正显示着他的申请：关掉，换下一个
    if (m_controlToast.IsWindow() && m_controlToast.Token() == static_cast<LPARAM>(userId))
    {
        m_controlToast.Hide();
        showNextControlRequest();
    }
}

LRESULT CShareController::OnControlToastMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)bHandled;
    const int64_t userId = static_cast<int64_t>(lParam);
    if (!isScreenOwner() || userId <= 0)
        return 0;
    //从队列里去掉这个申请（提示已经自己关掉）
    for (std::deque<ControlRequest>::iterator it = m_controlRequests.begin(); it != m_controlRequests.end(); ++it)
    {
        if (it->m_userId == userId)
        {
            m_controlRequests.erase(it);
            break;
        }
    }
    const int64_t self = m_host->GetSelfUserId();
    if (static_cast<int>(wParam) == CShareCornerToast::kCmdJoin)
        sendControl(share_control_grant, self, userId);
    else
        sendControl(share_control_deny, self, userId);
    showNextControlRequest();
    return 0;
}

//==================== 观看者一侧 ====================

void CShareController::toggleViewerControl()
{
    switch (m_viewerControl)
    {
    //还没申请：向当前那一块画面（放大的那块，没有放大时为第一块别人的画面）的主人申请，等他答复；没有多人共享信息时为发起人
    case share_viewer_control_none:
        {
            //主持人（2026-10-07，方案第 14.3 节第 4 条）在「其他人的屏幕」窗口里申请，画面取那个窗口当前那一块；
            //观看者在观看窗里申请
            const bool hosting = (m_hostPhase == host_streaming);
            const bool activeIsBoard = hosting ? m_screensWindow.ActiveIsBoard() : m_viewer.ActiveIsBoard();
            const int activeSeat = hosting ? m_screensWindow.ActiveScreenSeat() : m_viewer.ActiveScreenSeat();
            //查不到画面时观看者按单人共享向发起人申请，主持人不发
            int64_t owner = hosting ? 0 : m_lastState.m_hostId;
            const int index = FindScreenBySeat(m_screens, activeSeat);
            if (index >= 0)
                owner = m_screens[index].m_ownerId;
            //白板（第 23 期）没有电脑可控制；2026-10-12 起清单里有本人自己的画面，它只能看、不能申请控制
            const int64_t self = (m_host != NULL) ? m_host->GetSelfUserId() : 0;
            const int target = DecideShareControlTarget(activeIsBoard, owner, self);
            if (target == share_control_target_board)
            {
                showNotice(TR(L"白板不能申请控制"));
                return;
            }
            if (target == share_control_target_self)
            {
                showNotice(TR(L"不能控制自己的屏幕"));
                return;
            }
            if (target != share_control_target_ok)
                return;
            m_controlOwner = owner;
            sendControl(share_control_request, owner, 0);
            setViewerControlState(share_viewer_control_requested);
            showNotice(TRF(L"已申请控制 {0} 的屏幕，等待对方同意", displayName(owner)).c_str());
        }
        break;
    //申请中：撤回
    case share_viewer_control_requested:
        sendControl(share_control_cancel, m_controlOwner, 0);
        setViewerControlState(share_viewer_control_none);
        m_controlOwner = 0;
        break;
    //控制中：结束控制；本地立即停止发送输入，1725 到达后再确认
    case share_viewer_control_active:
        setViewerControlState(share_viewer_control_none);
        sendControl(share_control_revoke, m_controlOwner, 0);
        showNotice(TR(L"已结束控制"));
        m_controlOwner = 0;
        setControlScreen(-1);
        break;
    //状态取值不对：回到初始状态
    default:
        setViewerControlState(share_viewer_control_none);
        m_controlOwner = 0;
        break;
    }
}

void CShareController::setViewerControlState(int state)
{
    if (state == m_viewerControl)
        return;
    LOG_INFO("screenshare control: viewer control state %d -> %d.", m_viewerControl, state);
    //主持人（2026-10-07）在「其他人的屏幕」窗口里控制别人，观看者在观看窗里
    const bool hosting = (m_hostPhase == host_streaming);
    //先改状态再通知窗口：窗口离开控制时补发的抬起要能发出去，所以控制中 → 其他时先通知窗口
    if (m_viewerControl == share_viewer_control_active)
    {
        if (hosting)
            m_screensWindow.SetControlState(state);
        else
            m_viewer.SetControlState(state);
        m_viewerControl = state;
        return;
    }
    m_viewerControl = state;
    if (hosting)
        m_screensWindow.SetControlState(state);
    else
        m_viewer.SetControlState(state);
}

void CShareController::setControlScreen(int seat)
{
    if (m_hostPhase == host_streaming)
        m_screensWindow.SetControlScreen(seat);
    else
        m_viewer.SetControlScreen(seat);
}

void CShareController::OnControlInput(const ShareInputEvent& ev)
{
    //观看者经观看窗的收流管线发出；主持人（2026-10-07，在「其他人的屏幕」窗口里控制别人）经自己的推流管线发出
    const bool hosting = (m_hostPhase == host_streaming);
    if (m_viewerControl != share_viewer_control_active || (!hosting && m_viewerPhase != viewer_watching))
        return;
    //画面主人那块画面的座位（第 22 期起按画面座位，共享者与成员座位不同）；查不到时按发起人的座位，再查不到按 0
    int ownerSeat = -1;
    const ShareMember* owner = m_lastState.findMember(m_controlOwner);
    if (owner != NULL && owner->m_sharing && owner->m_screenSeat >= 0)
        ownerSeat = owner->m_screenSeat;
    if (ownerSeat < 0)
        ownerSeat = seatOfUser(m_seatUsers, m_lastState.m_hostId);
    if (ownerSeat < 0)
        ownerSeat = 0;
    const std::string body = ShareRemoteInput::BuildInputBody(m_selfSeat, ownerSeat, ev);
    ShareAudioTransport* transport = hosting ? static_cast<ShareAudioTransport*>(&m_streamer) : m_viewer.AudioTransport();
    if (!body.empty() && transport != NULL)
        transport->SendRoomBody(body);
}

void CShareController::resetControlLocal()
{
    //画面主人一侧
    if (m_controllerId != 0)
        setControlledBy(0);
    m_revokingId = 0;
    m_controlRequests.clear();
    m_controlToast.Hide();
    //观看者一侧
    if (m_viewerControl != share_viewer_control_none)
        setViewerControlState(share_viewer_control_none);
    m_controlOwner = 0;
    m_viewer.SetControlScreen(-1);
    m_screensWindow.SetControlScreen(-1);
}

} // namespace screenshare
