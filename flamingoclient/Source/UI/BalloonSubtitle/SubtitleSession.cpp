/**
 *  SubtitleSession.cpp
 *
 *  一个会话的字幕模式的实现，说明见 SubtitleSession.h。
 *
 *  balloonwj@qq.com   2026-10-05
 */

#include "stdafx.h"

#include "SubtitleSession.h"

#include <algorithm>

#include "../../Common/Log/IULog.h"
#include "SubtitleWindow.h"   // CreateSubtitleWindowView：默认的视图工厂

namespace subtitle {

namespace {

//会话类型：群聊（与协议 conversation_type 的 conversation_group 一致）。群聊字幕在行首标出群名
const int kGroupConversation = 2;

} // namespace

CSubtitleSession::CSubtitleSession()
    : m_ctype(0)
    , m_cid(0)
    , m_prefs(NULL)
    , m_save(NULL)
    , m_saveCtx(NULL)
    , m_hostCb(NULL)
    , m_hostCtx(NULL)
    , m_viewFactory(NULL)
    , m_viewCtx(NULL)
    , m_view(NULL)
    , m_hidden(false)
{
}

CSubtitleSession::~CSubtitleSession()
{
    Exit();
}

void CSubtitleSession::Init(int ctype, int64_t cid, SubtitlePrefs* prefs, SaveSubtitlePrefsFn save, void* saveCtx,
                            SubtitleHostCallback hostCb, void* hostCtx)
{
    m_ctype = ctype;
    m_cid = cid;
    m_prefs = prefs;
    m_save = save;
    m_saveCtx = saveCtx;
    m_hostCb = hostCb;
    m_hostCtx = hostCtx;
}

void CSubtitleSession::SetViewFactory(CreateSubtitleViewFn fn, void* ctx)
{
    m_viewFactory = fn;
    m_viewCtx = ctx;
}

bool CSubtitleSession::Enter(const std::wstring& title)
{
    m_title = title;
    if (m_view != NULL)
        return true;

    //群聊字幕在行首标出群名（会话名就是群名）；单聊不标
    m_model.SetOrigin((m_ctype == kGroupConversation) ? title : std::wstring());
    //从进入的这一刻起来一条显示一条，之前的消息不显示
    m_model.Reset();
    ISubtitleView* view = (m_viewFactory != NULL) ? m_viewFactory(m_viewCtx) : CreateSubtitleWindowView(NULL);
    if (view == NULL)
        return false;

    SubtitleViewInit init;
    init.m_host = this;
    init.m_title = title;
    init.m_appearance = (m_prefs != NULL) ? m_prefs->GetAppearance() : SubtitleAppearance();
    init.m_hasSaved = (m_prefs != NULL) && m_prefs->GetPlacement(m_ctype, m_cid, init.m_saved);
    //其它会话的字幕此刻所在的位置：没有保存过位置的新字幕避开它们
    const std::vector<CSubtitleSession*>& active = activeSessions();
    for (size_t i = 0; i < active.size(); ++i)
    {
        if (active[i] != this && active[i]->m_view != NULL)
            init.m_occupied.push_back(active[i]->m_view->GetRect());
    }
    init.m_line = m_model.Current();
    if (!view->Create(init))
    {
        delete view;
        LOG_WARNING("subtitle: cannot create the subtitle window, ctype=%d, cid=%lld.", m_ctype, (long long)m_cid);
        return false;
    }
    m_view = view;
    activeSessions().push_back(this);
    //临时隐藏期间（客户端锁屏中）进入的，窗口建好后同样隐藏
    if (m_hidden)
        m_view->SetHidden(true);
    LOG_INFO("subtitle: entered, ctype=%d, cid=%lld, saved placement=%d, other subtitles=%u, hidden=%d.", m_ctype,
             (long long)m_cid, init.m_hasSaved ? 1 : 0, (unsigned)init.m_occupied.size(), m_hidden ? 1 : 0);
    return true;
}

void CSubtitleSession::SetHidden(bool hidden)
{
    m_hidden = hidden;
    if (m_view != NULL)
        m_view->SetHidden(hidden);
}

bool CSubtitleSession::IsHidden() const
{
    return m_hidden;
}

void CSubtitleSession::Exit()
{
    if (m_view == NULL)
        return;
    std::vector<CSubtitleSession*>& active = activeSessions();
    active.erase(std::remove(active.begin(), active.end(), this), active.end());
    //先置空再销毁：销毁窗口期间若有回调进来，看到的是已经退出的状态
    ISubtitleView* view = m_view;
    m_view = NULL;
    view->Destroy();
    delete view;
    LOG_INFO("subtitle: exited, ctype=%d, cid=%lld.", m_ctype, (long long)m_cid);
}

bool CSubtitleSession::Toggle(const std::wstring& title)
{
    if (IsActive())
    {
        Exit();
        return false;
    }
    return Enter(title);
}

bool CSubtitleSession::IsActive() const
{
    return m_view != NULL;
}

void CSubtitleSession::OnIncoming(const SubtitleIncoming& in)
{
    if (m_view == NULL)
        return;
    if (m_model.OnIncoming(in))
        m_view->SetContent(m_model.Current());
}

void CSubtitleSession::OnRecalled(long long msgid, const std::wstring& noticeText)
{
    if (m_view == NULL)
        return;
    if (m_model.OnRecalled(msgid, noticeText))
        m_view->SetContent(m_model.Current());
}

void CSubtitleSession::SetTitle(const std::wstring& title)
{
    m_title = title;
    //群改名后行首的群名标注跟着换
    if (m_ctype == kGroupConversation)
        m_model.SetOrigin(title);
    if (m_view != NULL)
    {
        m_view->SetTitle(title);
        if (m_ctype == kGroupConversation)
            m_view->SetContent(m_model.Current());
    }
}

const CSubtitleModel& CSubtitleSession::Model() const
{
    return m_model;
}

ISubtitleView* CSubtitleSession::View() const
{
    return m_view;
}

int CSubtitleSession::ActiveCount()
{
    return (int)activeSessions().size();
}

void CSubtitleSession::OnSubtitleBack()
{
    LOG_INFO("subtitle: back to chat, ctype=%d, cid=%lld.", m_ctype, (long long)m_cid);
    notifyHost(kSubtitleEventBack);
}

void CSubtitleSession::OnSubtitleClose()
{
    Exit();
    notifyHost(kSubtitleEventExited);
}

void CSubtitleSession::OnSubtitleLockChanged(bool locked)
{
    if (m_view != NULL)
        rememberPlacement(m_view->GetRect(), locked);
    save();
    LOG_INFO("subtitle: %s, ctype=%d, cid=%lld.", locked ? "locked" : "unlocked", m_ctype, (long long)m_cid);
}

void CSubtitleSession::OnSubtitleMoved(const RECT& rc)
{
    rememberPlacement(rc, (m_view != NULL) ? m_view->IsLocked() : false);
    save();
}

void CSubtitleSession::OnSubtitleAppearanceChanged(const SubtitleAppearance& ap)
{
    if (m_prefs != NULL)
        m_prefs->SetAppearance(ap);
    const SubtitleAppearance applied = (m_prefs != NULL) ? m_prefs->GetAppearance() : NormalizeAppearance(ap);
    //外观所有会话共用：每条正在显示的字幕都换新外观。高度变了窗口位置随之变化，各自的新位置一并记下
    const std::vector<CSubtitleSession*> active = activeSessions();
    for (size_t i = 0; i < active.size(); ++i)
    {
        CSubtitleSession* s = active[i];
        if (s->m_view == NULL)
            continue;
        s->m_view->SetAppearance(applied);
        s->rememberPlacement(s->m_view->GetRect(), s->m_view->IsLocked());
    }
    save();
    LOG_INFO("subtitle: appearance changed, font=%d, lines=%d, hide from capture=%d, subtitles=%u.",
             applied.m_fontSize, applied.m_maxLines, applied.m_hideFromCapture ? 1 : 0, (unsigned)active.size());
}

void CSubtitleSession::rememberPlacement(const RECT& rc, bool locked)
{
    if (m_prefs == NULL || ::IsRectEmpty(&rc))
        return;
    SubtitlePlacement pl;
    pl.m_left = rc.left;
    pl.m_top = rc.top;
    pl.m_width = rc.right - rc.left;
    pl.m_locked = locked;
    m_prefs->SetPlacement(m_ctype, m_cid, pl);
}

void CSubtitleSession::save()
{
    if (m_save != NULL)
        m_save(m_saveCtx);
}

void CSubtitleSession::notifyHost(int event)
{
    if (m_hostCb != NULL)
        m_hostCb(m_hostCtx, event);
}

std::vector<CSubtitleSession*>& CSubtitleSession::activeSessions()
{
    static std::vector<CSubtitleSession*> s_active;
    return s_active;
}

//======================================================================================================================
// CSubtitleRegistry
//======================================================================================================================

CSubtitleRegistry::CSubtitleRegistry()
    : m_prefs(NULL)
    , m_save(NULL)
    , m_saveCtx(NULL)
    , m_cb(NULL)
    , m_cbCtx(NULL)
    , m_viewFactory(NULL)
    , m_viewCtx(NULL)
    , m_hidden(false)
{
}

CSubtitleRegistry::~CSubtitleRegistry()
{
    ExitAll();
    for (std::map<Key, Entry*>::iterator it = m_entries.begin(); it != m_entries.end(); ++it)
        delete it->second;
    m_entries.clear();
}

void CSubtitleRegistry::Init(SubtitlePrefs* prefs, SaveSubtitlePrefsFn save, void* saveCtx, SubtitleRegistryCallback cb,
                             void* cbCtx)
{
    //重新初始化（换了账号）：旧账号的字幕全部关闭，会话对象按新依赖重建
    ExitAll();
    for (std::map<Key, Entry*>::iterator it = m_entries.begin(); it != m_entries.end(); ++it)
        delete it->second;
    m_entries.clear();
    m_prefs = prefs;
    m_save = save;
    m_saveCtx = saveCtx;
    m_cb = cb;
    m_cbCtx = cbCtx;
}

void CSubtitleRegistry::SetViewFactory(CreateSubtitleViewFn fn, void* ctx)
{
    m_viewFactory = fn;
    m_viewCtx = ctx;
    for (std::map<Key, Entry*>::iterator it = m_entries.begin(); it != m_entries.end(); ++it)
        it->second->m_session.SetViewFactory(fn, ctx);
}

bool CSubtitleRegistry::IsActive(int ctype, int64_t cid) const
{
    const Entry* e = find(ctype, cid);
    return e != NULL && e->m_session.IsActive();
}

bool CSubtitleRegistry::Enter(int ctype, int64_t cid, const std::wstring& title)
{
    Entry* e = findOrCreate(ctype, cid);
    if (e->m_session.IsActive())
        return true;
    if (!e->m_session.Enter(title))
        return false;
    notify(kRegistryEventStateChanged, ctype, cid, true);
    return true;
}

void CSubtitleRegistry::Exit(int ctype, int64_t cid)
{
    Entry* e = find(ctype, cid);
    if (e == NULL || !e->m_session.IsActive())
        return;
    e->m_session.Exit();
    notify(kRegistryEventStateChanged, ctype, cid, false);
}

bool CSubtitleRegistry::Toggle(int ctype, int64_t cid, const std::wstring& title)
{
    if (IsActive(ctype, cid))
    {
        Exit(ctype, cid);
        return false;
    }
    return Enter(ctype, cid, title);
}

void CSubtitleRegistry::OnIncoming(int ctype, int64_t cid, const SubtitleIncoming& in)
{
    Entry* e = find(ctype, cid);
    if (e != NULL)
        e->m_session.OnIncoming(in);
}

void CSubtitleRegistry::OnRecalled(int ctype, int64_t cid, long long msgid, const std::wstring& noticeText)
{
    Entry* e = find(ctype, cid);
    if (e != NULL)
        e->m_session.OnRecalled(msgid, noticeText);
}

void CSubtitleRegistry::SetTitle(int ctype, int64_t cid, const std::wstring& title)
{
    Entry* e = find(ctype, cid);
    if (e != NULL)
        e->m_session.SetTitle(title);
}

void CSubtitleRegistry::ExitAll()
{
    int closed = 0;
    for (std::map<Key, Entry*>::iterator it = m_entries.begin(); it != m_entries.end(); ++it)
    {
        if (!it->second->m_session.IsActive())
            continue;
        it->second->m_session.Exit();
        ++closed;
    }
    if (closed > 0)
        LOG_INFO("subtitle registry: closed all subtitles, count=%d.", closed);
}

void CSubtitleRegistry::SetAllHidden(bool hidden)
{
    m_hidden = hidden;
    for (std::map<Key, Entry*>::iterator it = m_entries.begin(); it != m_entries.end(); ++it)
        it->second->m_session.SetHidden(hidden);
    LOG_INFO("subtitle registry: all subtitles %s, active=%d.", hidden ? "hidden" : "shown", ActiveCount());
}

int CSubtitleRegistry::ActiveCount() const
{
    int n = 0;
    for (std::map<Key, Entry*>::const_iterator it = m_entries.begin(); it != m_entries.end(); ++it)
    {
        if (it->second->m_session.IsActive())
            ++n;
    }
    return n;
}

void CSubtitleRegistry::ActiveConversations(std::vector<std::pair<int, int64_t> >& out) const
{
    out.clear();
    for (std::map<Key, Entry*>::const_iterator it = m_entries.begin(); it != m_entries.end(); ++it)
    {
        if (it->second->m_session.IsActive())
            out.push_back(it->first);
    }
}

CSubtitleRegistry::Entry* CSubtitleRegistry::find(int ctype, int64_t cid) const
{
    std::map<Key, Entry*>::const_iterator it = m_entries.find(Key(ctype, cid));
    return (it != m_entries.end()) ? it->second : NULL;
}

CSubtitleRegistry::Entry* CSubtitleRegistry::findOrCreate(int ctype, int64_t cid)
{
    Entry* e = find(ctype, cid);
    if (e != NULL)
        return e;
    e = new Entry();
    e->m_owner = this;
    e->m_ctype = ctype;
    e->m_cid = cid;
    e->m_session.Init(ctype, cid, m_prefs, m_save, m_saveCtx, &CSubtitleRegistry::sessionEventThunk, e);
    e->m_session.SetViewFactory(m_viewFactory, m_viewCtx);
    e->m_session.SetHidden(m_hidden);
    m_entries[Key(ctype, cid)] = e;
    return e;
}

void CSubtitleRegistry::sessionEventThunk(void* ctx, int event)
{
    Entry* e = static_cast<Entry*>(ctx);
    if (e == NULL || e->m_owner == NULL)
        return;
    switch (event)
    {
    //用户点了「回到聊天窗」：字幕保留，请主面板打开聊天窗
    case kSubtitleEventBack:
        e->m_owner->notify(kRegistryEventBack, e->m_ctype, e->m_cid, e->m_session.IsActive());
        break;

    //用户点了「退出字幕模式」：会话对象已经退出，转告主面板刷新徽章与按钮高亮
    case kSubtitleEventExited:
        e->m_owner->notify(kRegistryEventStateChanged, e->m_ctype, e->m_cid, false);
        break;

    //其它取值目前没有，忽略
    default:
        break;
    }
}

void CSubtitleRegistry::notify(int event, int ctype, int64_t cid, bool active)
{
    if (m_cb != NULL)
        m_cb(m_cbCtx, event, ctype, cid, active);
}

} // namespace subtitle
