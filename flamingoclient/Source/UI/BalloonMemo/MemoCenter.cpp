/**
 *  备忘中枢，见 MemoCenter.h。
 *
 *  balloonwj@qq.com   2026-10-03
 */
#include "stdafx.h"
#include "MemoCenter.h"

#include <stdio.h>
#include <algorithm>

#include "../../Common/Log/IULog.h"
#include "../../Common/Util/EncodeUtil.h"
#include "../../Feature/Memo/MemoClock.h"
#include "../../net/IUSocket.h"
#include "../../net/ContactsProtocolClient.h"
#include "../../Session/UserSessionData.h"   // FMG_MSG_MEMO_CHANGED
#include "BalloonMemoWindow.h"
#include "MemoEditDialog.h"
#include "MemoReminderPopup.h"
#include "MemoFullscreenWindow.h"
#include "../../Common/I18n/I18n.h"   // TR：界面文字的多语言查找
#include "../../Feature/Lock/LockHost.h"            // IsScreenLocked：锁屏期间提醒先记下来
#include "../../Feature/Lock/LockSuppressPolicy.h"  // DecideDuringLock

namespace balloonmemo {

namespace {

//只收消息的窗口上的三个定时器：提醒、拉取超时、保存超时
const UINT_PTR kScheduleTimerId = 1;
const UINT_PTR kListTimeoutTimerId = 2;
const UINT_PTR kSaveTimeoutTimerId = 3;

//拉取、保存发出之后等应答的最长时间（毫秒）：超过之后提示服务器没有应答（2026-10-03 与用户确认 10 秒）
const UINT kRequestTimeoutMs = 10000;

//最长多久检查一次（毫秒）：改了系统时间、休眠唤醒之后最多这么久就能补上
const int64_t kMaxCheckIntervalMs = 60000;

//定时器的最短间隔（毫秒）：下一次提醒恰好就在这一分钟时也不至于立刻反复触发
const int64_t kMinCheckIntervalMs = 500;

//已提醒记录的文件名，放在本人的个人文件夹里
const wchar_t* const kFiredFileName = L"memo_fired.txt";

//读已提醒记录文件时一次读入的字节数
const size_t kReadChunk = 4096;

} // anonymous namespace

/**
 *  只收消息的窗口（HWND_MESSAGE），承载备忘中枢的提醒定时器。不显示、不进 Alt-Tab。
 */
class CMemoTimerWindow : public CWindowImpl<CMemoTimerWindow, CWindow, CNullTraits>
{
public:
    DECLARE_WND_CLASS(_T("FlamingoMemoTimerWindow"))

    BEGIN_MSG_MAP(CMemoTimerWindow)
        MESSAGE_HANDLER(WM_TIMER, OnTimer)
    END_MSG_MAP()

private:
    LRESULT OnTimer(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& bHandled)
    {
        //三个都是一次性定时器：到点先停掉，需要时由中枢重新设定
        switch (wParam)
        {
        //提醒：检查此刻该弹的提醒，再按下一次提醒时间重设
        case kScheduleTimerId:
            KillTimer(kScheduleTimerId);
            CMemoCenter::Instance().OnScheduleTimer();
            return 0;
        //拉取备忘超时
        case kListTimeoutTimerId:
            KillTimer(kListTimeoutTimerId);
            CMemoCenter::Instance().OnListTimeout();
            return 0;
        //保存备忘超时
        case kSaveTimeoutTimerId:
            KillTimer(kSaveTimeoutTimerId);
            CMemoCenter::Instance().OnSaveTimeout();
            return 0;
        //不是本窗口设的定时器
        default:
            bHandled = FALSE;
            return 0;
        }
    }
};

CMemoCenter& CMemoCenter::Instance()
{
    static CMemoCenter instance;
    return instance;
}

CMemoCenter::CMemoCenter()
    : m_panel(NULL)
    , m_selfId(0)
    , m_host(NULL)
    , m_saving(false)
    , m_firedLoaded(false)
    , m_listTimedOut(false)
    , m_deleteOwner(NULL)
    , m_popupDeferredByLock(false)
{
}

CMemoCenter::~CMemoCenter()
{
    if (m_timerWnd && m_timerWnd->IsWindow())
    {
        m_timerWnd->DestroyWindow();
    }
}

// ---------------- 会话生命周期 ----------------

void CMemoCenter::OnLoginSucceeded(HWND panel, int64_t selfId, const std::wstring& personalFolder, IMemoHost* host)
{
    //换了账号：先按注销收尾，上一个账号的备忘与提醒一律不留
    if (m_selfId != 0 && m_selfId != selfId)
    {
        OnLogout();
    }
    m_panel = panel;
    m_selfId = selfId;
    m_book.SetSelfId(selfId);
    m_personalFolder = personalFolder;
    m_host = host;
    if (!m_firedLoaded)
    {
        LoadFiredRecords();
        m_firedLoaded = true;
    }
    //重连时也重拉一次：断线期间的推送都收不到了
    const bool sent = CIUSocket::GetInstance().SendMemoList();
    m_listTimedOut = false;
    if (EnsureTimerWindow())
    {
        m_timerWnd->SetTimer(kListTimeoutTimerId, kRequestTimeoutMs);
    }
    LOG_INFO("memo center: login succeeded, self=%lld, list requested=%d, fired records=%u.", (long long)selfId,
             (int)sent, (uint32_t)m_scheduler.FiredRecords().size());
}

void CMemoCenter::OnLogout()
{
    if (m_selfId == 0)
    {
        return;
    }
    if (m_editDialog)
    {
        m_editDialog->CloseDialog();
    }
    if (m_memoWindow)
    {
        m_memoWindow->CloseWindow();
    }
    if (m_popup)
    {
        m_popup->CloseWindow();
    }
    if (m_fullscreen)
    {
        m_fullscreen->CloseWindow();
    }
    if (m_timerWnd && m_timerWnd->IsWindow())
    {
        m_timerWnd->KillTimer(kScheduleTimerId);
        m_timerWnd->KillTimer(kListTimeoutTimerId);
        m_timerWnd->KillTimer(kSaveTimeoutTimerId);
    }
    SaveFiredRecords();
    LOG_INFO("memo center: logout, self=%lld, memos dropped=%u.", (long long)m_selfId,
             (uint32_t)m_book.Items().size());
    m_book.Clear();
    m_scheduler.Reset();
    m_queue.Clear();
    m_observers.clear();
    m_panel = NULL;
    m_selfId = 0;
    m_host = NULL;
    m_personalFolder.clear();
    m_saving = false;
    m_firedLoaded = false;
    m_listTimedOut = false;
    m_deleteOwner = NULL;
    m_lockedFires.clear();
    m_popupDeferredByLock = false;
}

void CMemoCenter::Shutdown()
{
    OnLogout();
    if (m_timerWnd && m_timerWnd->IsWindow())
    {
        m_timerWnd->DestroyWindow();
    }
    m_memoWindow.reset();
    m_editDialog.reset();
    m_popup.reset();
    m_fullscreen.reset();
    m_timerWnd.reset();
}

// ---------------- 网络应答与推送 ----------------

void CMemoCenter::OnListResult(const net::MemoListResult& result)
{
    if (m_selfId == 0)
    {
        return;
    }
    if (result.m_code != 0)
    {
        LOG_ERROR("memo center: list failed, code=%d, msg=%s.", result.m_code, result.m_msg.c_str());
        return;
    }
    if (m_timerWnd && m_timerWnd->IsWindow())
    {
        m_timerWnd->KillTimer(kListTimeoutTimerId);
    }
    m_listTimedOut = false;
    m_book.ReplaceAll(result.m_memos);
    m_book.SetSelfId(m_selfId);
    std::vector<memo::MemoScheduleInput> inputs;
    m_book.BuildScheduleInputs(memo::LocalMinuteFromUnixMs, inputs);
    if (m_scheduler.Prune(inputs))
    {
        SaveFiredRecords();
    }
    LOG_INFO("memo center: list arrived, memos=%u.", (uint32_t)result.m_memos.size());
    AfterBookChanged(0);
}

void CMemoCenter::OnSaveResult(const net::MemoSaveResult& result)
{
    if (m_timerWnd && m_timerWnd->IsWindow())
    {
        m_timerWnd->KillTimer(kSaveTimeoutTimerId);
    }
    m_saving = false;
    int64_t groupid = 0;
    if (result.m_code == 0 && result.m_hasMemo && m_selfId != 0)
    {
        const bool existed = m_book.ApplyUpsert(result.m_memo, m_selfId);
        if (existed)
        {
            //修改之后按新规则重新计算，旧的稍后提醒不再有意义
            m_scheduler.CancelSnooze(result.m_memo.m_memoid);
        }
        groupid = result.m_memo.m_groupid;
        LOG_INFO("memo center: saved, memoid=%lld, update=%d.", (long long)result.m_memo.m_memoid, (int)existed);
    }
    else
    {
        LOG_ERROR("memo center: save failed, code=%d, msg=%s.", result.m_code, result.m_msg.c_str());
    }
    if (m_editDialog)
    {
        m_editDialog->OnSaveResult(result.m_code);
    }
    if (result.m_code == 0)
    {
        AfterBookChanged(groupid);
    }
}

void CMemoCenter::OnDeleteResult(const net::MemoDeleteResult& result)
{
    if (result.m_code == 0)
    {
        LOG_INFO("memo center: deleted, memoid=%lld.", (long long)result.m_memoid);
        HandleDeleted(result.m_memoid);
        return;
    }
    LOG_ERROR("memo center: delete failed, memoid=%lld, code=%d.", (long long)result.m_memoid, result.m_code);
    //失败时提示原因；确认框的 owner 已经关掉时改用主面板
    HWND owner = ::IsWindow(m_deleteOwner) ? m_deleteOwner : m_panel;
    ::MessageBoxW(owner, MemoFailureText(result.m_code, true).c_str(), TR(L"删除备忘"), MB_OK | MB_ICONWARNING);
}

void CMemoCenter::OnNotify(const net::MemoNotify& notify)
{
    if (m_selfId == 0)
    {
        return;
    }
    //memo_change_type：1 新增或修改、2 删除或从此看不到
    const int kChangeUpsert = 1;
    if (notify.m_changeType == kChangeUpsert)
    {
        const bool existed = m_book.ApplyUpsert(notify.m_memo, m_selfId);
        if (existed)
        {
            m_scheduler.CancelSnooze(notify.m_memoid);
        }
        LOG_INFO("memo center: notify upsert, memoid=%lld, existed=%d, unseen in group=%d.", (long long)notify.m_memoid,
                 (int)existed, m_book.UnseenCount(notify.m_memo.m_groupid));
        AfterBookChanged(notify.m_memo.m_groupid);
        return;
    }
    LOG_INFO("memo center: notify delete, memoid=%lld.", (long long)notify.m_memoid);
    HandleDeleted(notify.m_memoid);
}

void CMemoCenter::HandleDeleted(int64_t memoid)
{
    const net::MemoItem* item = m_book.Find(memoid);
    const int64_t groupid = (item != NULL) ? item->m_groupid : 0;
    if (!m_book.ApplyDelete(memoid))
    {
        return;
    }
    m_scheduler.CancelSnooze(memoid);
    m_queue.RemoveMemo(memoid);
    if (m_fullscreen)
    {
        m_fullscreen->RemoveMemo(memoid);
    }
    if (m_editDialog)
    {
        m_editDialog->OnMemoDeleted(memoid);
    }
    std::vector<memo::MemoScheduleInput> inputs;
    m_book.BuildScheduleInputs(memo::LocalMinuteFromUnixMs, inputs);
    if (m_scheduler.Prune(inputs))
    {
        SaveFiredRecords();
    }
    AfterBookChanged(groupid);
}

// ---------------- 入口 ----------------

void CMemoCenter::OpenMemoWindow(HWND owner)
{
    if (m_selfId == 0)
    {
        LOG_ERROR("memo center: open memo window ignored, not logged in.");
        return;
    }
    if (!m_memoWindow)
    {
        m_memoWindow.reset(new CBalloonMemoWindow());
    }
    m_memoWindow->Open(owner);
}

void CMemoCenter::OpenEditor(HWND owner, const net::MemoItem& item, MemoEditMode mode,
                             const std::vector<net::ContactItem>& members)
{
    if (m_selfId == 0)
    {
        return;
    }
    if (!m_editDialog)
    {
        m_editDialog.reset(new CMemoEditDialog());
    }
    m_saving = false;
    m_editDialog->Open(owner, item, mode, members);
}

bool CMemoCenter::Save(const net::MemoItem& item)
{
    if (m_selfId == 0)
    {
        return false;
    }
    if (!CIUSocket::GetInstance().SendMemoSave(item))
    {
        return false;
    }
    m_saving = true;
    if (EnsureTimerWindow())
    {
        m_timerWnd->SetTimer(kSaveTimeoutTimerId, kRequestTimeoutMs);
    }
    return true;
}

void CMemoCenter::ConfirmAndDelete(HWND owner, int64_t memoid)
{
    const net::MemoItem* item = m_book.Find(memoid);
    if (item == NULL)
    {
        return;
    }
    CString prompt;
    if (item->m_ctype == net::kMemoCtypeGroup)
    {
        prompt.Format(TR(L"确定删除群备忘「%s」吗？\n删除后被提醒的成员都不会再收到这条提醒。"),
                      EncodeUtil::Utf8ToUnicode(item->m_title).c_str());
    }
    else
    {
        prompt.Format(TR(L"确定删除备忘「%s」吗？"), EncodeUtil::Utf8ToUnicode(item->m_title).c_str());
    }
    if (::MessageBoxW(owner, prompt, TR(L"删除备忘"), MB_YESNO | MB_ICONWARNING) != IDYES)
    {
        return;
    }
    m_deleteOwner = owner;
    if (!CIUSocket::GetInstance().SendMemoDelete(memoid))
    {
        ::MessageBoxW(owner, TR(L"网络未连接，请稍后重试。"), TR(L"删除备忘"), MB_OK | MB_ICONWARNING);
    }
}

// ---------------- 数据 ----------------

const memo::CMemoBook& CMemoCenter::Book() const
{
    return m_book;
}

void CMemoCenter::MarkGroupSeen(int64_t groupid)
{
    if (m_book.UnseenCount(groupid) == 0)
    {
        return;
    }
    m_book.MarkGroupSeen(groupid);
    NotifyObservers(groupid);
}

int64_t CMemoCenter::SelfId() const
{
    return m_selfId;
}

std::wstring CMemoCenter::GroupNameOf(int64_t groupid) const
{
    std::wstring name;
    if (m_host != NULL)
    {
        name = m_host->MemoGroupName(groupid);
    }
    return name.empty() ? std::wstring(TR(L"群聊")) : name;
}

int CMemoCenter::GroupRoleOf(int64_t groupid) const
{
    return (m_host != NULL) ? m_host->MemoGroupRole(groupid) : -1;
}

std::wstring CMemoCenter::UserNameOf(int64_t groupid, int64_t userid) const
{
    std::wstring name;
    if (m_host != NULL)
    {
        name = m_host->MemoUserName(groupid, userid);
    }
    return name.empty() ? TRF(L"用户{0}", (long long)userid) : name;
}

memo::LocalMinute CMemoCenter::CreatedAtOf(const net::MemoItem& item)
{
    return (item.m_createTime > 0) ? memo::LocalMinuteFromUnixMs(item.m_createTime) : 0;
}

std::wstring CMemoCenter::CreatorLineOf(const net::MemoItem& item, memo::LocalMinute now) const
{
    if (item.m_ctype != net::kMemoCtypeGroup)
    {
        return std::wstring();
    }
    return CreatorText(UserNameOf(item.m_groupid, item.m_creator), CreatedAtOf(item), now);
}

void CMemoCenter::RegisterObserver(HWND hwnd)
{
    if (hwnd == NULL || std::find(m_observers.begin(), m_observers.end(), hwnd) != m_observers.end())
    {
        return;
    }
    m_observers.push_back(hwnd);
}

void CMemoCenter::UnregisterObserver(HWND hwnd)
{
    m_observers.erase(std::remove(m_observers.begin(), m_observers.end(), hwnd), m_observers.end());
}

void CMemoCenter::NotifyObservers(int64_t groupid)
{
    std::vector<HWND> alive;
    for (size_t i = 0; i < m_observers.size(); ++i)
    {
        if (::IsWindow(m_observers[i]))
        {
            ::PostMessage(m_observers[i], FMG_MSG_MEMO_CHANGED, (WPARAM)groupid, 0);
            alive.push_back(m_observers[i]);
        }
    }
    m_observers.swap(alive);
}

// ---------------- 提醒 ----------------

void CMemoCenter::AfterBookChanged(int64_t groupid)
{
    NotifyObservers(groupid);
    RefreshPopup();
    CheckNow();
    Reschedule();
}

void CMemoCenter::CheckNow()
{
    if (m_selfId == 0 || !m_book.IsLoaded())
    {
        return;
    }
    std::vector<memo::MemoScheduleInput> inputs;
    m_book.BuildScheduleInputs(memo::LocalMinuteFromUnixMs, inputs);
    std::vector<memo::MemoFire> fires;
    if (m_scheduler.Check(inputs, memo::NowLocalMinute(), fires))
    {
        SaveFiredRecords();
    }
    if (!fires.empty())
    {
        DispatchFires(fires);
    }
}

RECT CMemoCenter::FullscreenMonitorRect() const
{
    RECT panelRc = { 0, 0, 0, 0 };
    RECT primaryRc = { 0, 0, 0, 0 };
    MONITORINFO mi;
    mi.cbSize = sizeof(mi);
    const POINT origin = { 0, 0 };
    if (::GetMonitorInfo(::MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY), &mi))
    {
        primaryRc = mi.rcMonitor;
    }
    panelRc = primaryRc;
    if (::IsWindow(m_panel))
    {
        mi.cbSize = sizeof(mi);
        if (::GetMonitorInfo(::MonitorFromWindow(m_panel, MONITOR_DEFAULTTONEAREST), &mi))
        {
            panelRc = mi.rcMonitor;
        }
    }
    const bool panelVisible = (m_host != NULL) && m_host->MemoPanelVisible();
    return ChooseFullscreenMonitor(panelVisible, panelRc, primaryRc);
}

void CMemoCenter::DispatchFires(const std::vector<memo::MemoFire>& fires)
{
    //锁屏期间先记下来，解锁后由 OnScreenUnlocked 补弹（调度器已把它们记为已提醒，不会重复触发）
    if (balloonlock::DecideDuringLock(balloonlock::kLockEvtMemoReminder, balloonlock::IsScreenLocked())
        == balloonlock::kLockActDefer)
    {
        m_lockedFires.insert(m_lockedFires.end(), fires.begin(), fires.end());
        LOG_INFO("memo reminder deferred until unlock: fires=%u, queued=%u.", (uint32_t)fires.size(),
                 (uint32_t)m_lockedFires.size());
        return;
    }
    //全屏样式的备忘值 memo_style_fullscreen
    const int kStyleFullscreen = 2;
    bool popupChanged = false;
    for (size_t i = 0; i < fires.size(); ++i)
    {
        const memo::MemoFire& fire = fires[i];
        const net::MemoItem* item = m_book.Find(fire.m_memoid);
        if (item == NULL)
        {
            continue;
        }
        LOG_INFO("memo reminder fired: memoid=%lld, event=%lld, kind=%d, snoozed=%d, style=%d.", (long long)fire.m_memoid,
                 (long long)memo::StartFromLocalMinute(fire.m_event), fire.m_kind, (int)fire.m_snoozed, item->m_style);
        if (fire.m_kind == memo::kMemoFireDue && item->m_style == kStyleFullscreen)
        {
            if (!m_fullscreen)
            {
                m_fullscreen.reset(new CMemoFullscreenWindow());
            }
            ReminderEntry entry;
            entry.m_memoid = fire.m_memoid;
            entry.m_event = fire.m_event;
            m_fullscreen->Enqueue(entry, FullscreenMonitorRect());
            continue;
        }
        if (m_queue.AddOne(fire.m_memoid, fire.m_event, fire.m_kind == memo::kMemoFireMissed))
        {
            popupChanged = true;
        }
    }
    if (popupChanged)
    {
        RefreshPopup();
    }
}

void CMemoCenter::RefreshPopup()
{
    //提醒队列里的备忘可能已被删除（全量替换之后），先拿掉
    std::vector<ReminderEntry> all = m_queue.DueEntries();
    const std::vector<ReminderEntry> missed = m_queue.MissedEntries();
    all.insert(all.end(), missed.begin(), missed.end());
    for (size_t i = 0; i < all.size(); ++i)
    {
        if (m_book.Find(all[i].m_memoid) == NULL)
        {
            m_queue.RemoveMemo(all[i].m_memoid);
        }
    }
    if (m_queue.IsEmpty())
    {
        if (m_popup)
        {
            m_popup->ShowEntries(std::vector<ReminderEntry>(), std::vector<ReminderEntry>());
        }
        return;
    }
    //锁屏期间不显示（例如锁屏前已打开的全屏提醒 60 秒无人操作，转到右下角弹窗），解锁后再刷新
    if (balloonlock::DecideDuringLock(balloonlock::kLockEvtMemoReminder, balloonlock::IsScreenLocked())
        == balloonlock::kLockActDefer)
    {
        m_popupDeferredByLock = true;
        return;
    }
    if (!m_popup)
    {
        m_popup.reset(new CMemoReminderPopup());
    }
    m_popup->ShowEntries(m_queue.DueEntries(), m_queue.MissedEntries());
}

void CMemoCenter::OnScreenUnlocked()
{
    std::vector<memo::MemoFire> fires;
    fires.swap(m_lockedFires);
    const bool refresh = m_popupDeferredByLock;
    m_popupDeferredByLock = false;
    if (m_selfId == 0)
    {
        return;
    }
    LOG_INFO("memo center: screen unlocked, deferred fires=%u, popup refresh=%d.", (uint32_t)fires.size(),
             refresh ? 1 : 0);
    //DispatchFires 逐条到备忘簿里查，锁屏期间被删除的会被跳过
    if (!fires.empty())
    {
        DispatchFires(fires);
    }
    if (refresh)
    {
        RefreshPopup();
    }
}

void CMemoCenter::Reschedule()
{
    if (m_selfId == 0 || !m_book.IsLoaded())
    {
        return;
    }
    if (!EnsureTimerWindow())
    {
        return;
    }
    std::vector<memo::MemoScheduleInput> inputs;
    m_book.BuildScheduleInputs(memo::LocalMinuteFromUnixMs, inputs);
    memo::LocalMinute wake = 0;
    int64_t delay = kMaxCheckIntervalMs;
    if (m_scheduler.NextWake(inputs, memo::NowLocalMinute(), wake))
    {
        delay = memo::MillisUntilLocalMinute(wake);
    }
    if (delay > kMaxCheckIntervalMs)
    {
        delay = kMaxCheckIntervalMs;
    }
    if (delay < kMinCheckIntervalMs)
    {
        delay = kMinCheckIntervalMs;
    }
    m_timerWnd->SetTimer(kScheduleTimerId, (UINT)delay);
}

bool CMemoCenter::EnsureTimerWindow()
{
    if (!m_timerWnd)
    {
        m_timerWnd.reset(new CMemoTimerWindow());
    }
    if (!m_timerWnd->IsWindow() && m_timerWnd->Create(HWND_MESSAGE) == NULL)
    {
        LOG_ERROR("memo center: create timer window failed, error=%lu.", ::GetLastError());
        return false;
    }
    return true;
}

bool CMemoCenter::ListTimedOut() const
{
    return m_listTimedOut && !m_book.IsLoaded();
}

void CMemoCenter::OnListTimeout()
{
    if (m_selfId == 0 || m_book.IsLoaded())
    {
        return;
    }
    m_listTimedOut = true;
    LOG_ERROR("memo center: memo list got no response within %u ms, the server may not support memos.",
              kRequestTimeoutMs);
    NotifyObservers(0);
}

void CMemoCenter::OnSaveTimeout()
{
    if (!m_saving)
    {
        return;
    }
    m_saving = false;
    LOG_ERROR("memo center: memo save got no response within %u ms.", kRequestTimeoutMs);
    if (m_editDialog)
    {
        m_editDialog->OnSaveResult(kMemoCodeTimeout);
    }
}

void CMemoCenter::OnScheduleTimer()
{
    CheckNow();
    Reschedule();
}

void CMemoCenter::OnReminderDismissed(int64_t memoid)
{
    if (memoid == 0)
    {
        m_queue.DismissMissed();
    }
    else
    {
        m_queue.Dismiss(memoid);
    }
    RefreshPopup();
}

void CMemoCenter::OnReminderSnoozed(int64_t memoid, memo::LocalMinute event)
{
    memo::LocalMinute queued = 0;
    if (m_queue.TakeForSnooze(memoid, queued))
    {
        event = queued;
    }
    if (event == 0 || m_book.Find(memoid) == NULL)
    {
        RefreshPopup();
        return;
    }
    const memo::LocalMinute until = memo::NowLocalMinute() + memo::kMemoSnoozeMinutes;
    m_scheduler.Snooze(memoid, event, until);
    LOG_INFO("memo reminder snoozed: memoid=%lld, until=%lld.", (long long)memoid,
             (long long)memo::StartFromLocalMinute(until));
    RefreshPopup();
    Reschedule();
}

void CMemoCenter::OnFullscreenTimedOut(const std::vector<ReminderEntry>& entries)
{
    for (size_t i = 0; i < entries.size(); ++i)
    {
        m_queue.AddOne(entries[i].m_memoid, entries[i].m_event, false);
    }
    RefreshPopup();
}

void CMemoCenter::OpenFromReminder(int64_t memoid)
{
    const net::MemoItem* item = m_book.Find(memoid);
    if (item == NULL)
    {
        return;
    }
    //个人备忘可以直接修改；群备忘这里没有本群成员可选，只能查看
    const MemoEditMode mode = (item->m_ctype == net::kMemoCtypePersonal) ? kMemoEditExisting : kMemoEditReadOnly;
    OpenEditor(NULL, *item, mode, std::vector<net::ContactItem>());
}

// ---------------- 已提醒记录 ----------------

std::wstring CMemoCenter::FiredFilePath() const
{
    if (m_personalFolder.empty())
    {
        return std::wstring();
    }
    return m_personalFolder + kFiredFileName;
}

void CMemoCenter::LoadFiredRecords()
{
    const std::wstring path = FiredFilePath();
    if (path.empty())
    {
        return;
    }
    FILE* fp = NULL;
    if (_wfopen_s(&fp, path.c_str(), L"rb") != 0 || fp == NULL)
    {
        return;   //第一次使用：还没有记录
    }
    std::string text;
    char buf[kReadChunk];
    while (true)
    {
        const size_t n = fread(buf, 1, sizeof(buf), fp);
        if (n == 0)
        {
            break;
        }
        text.append(buf, n);
    }
    fclose(fp);
    std::map<int64_t, memo::LocalMinute> records;
    memo::DecodeFiredRecords(text, records);
    for (std::map<int64_t, memo::LocalMinute>::const_iterator it = records.begin(); it != records.end(); ++it)
    {
        m_scheduler.LoadFiredRecord(it->first, it->second);
    }
}

void CMemoCenter::SaveFiredRecords()
{
    const std::wstring path = FiredFilePath();
    if (path.empty())
    {
        return;
    }
    const std::string text = memo::EncodeFiredRecords(m_scheduler.FiredRecords());
    FILE* fp = NULL;
    if (_wfopen_s(&fp, path.c_str(), L"wb") != 0 || fp == NULL)
    {
        LOG_ERROR("memo center: cannot write fired records, records=%u.", (uint32_t)m_scheduler.FiredRecords().size());
        return;
    }
    if (!text.empty())
    {
        fwrite(text.data(), 1, text.size(), fp);
    }
    fclose(fp);
}

} // namespace balloonmemo
