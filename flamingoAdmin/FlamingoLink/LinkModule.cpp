/**
 *  FlamingoLink.dll —— ILinkModule 实现，说明见 LinkModule.h。
 *
 *  balloonwj@qq.com   2026-10-04
 */
#include "stdafx.h"
#include "FlamingoLink/LinkModule.h"

#include "FlamingoLink/LinkLog.h"
#include "../../flamingoclient/Source/net/FrameCrypto.h"

namespace flamingolink {

namespace {

//Winsock 版本：2.2
const BYTE kWinsockMajor = 2;
const BYTE kWinsockMinor = 2;

} // anonymous namespace

// ==================== CLinkMessageWnd ====================

CLinkMessageWnd::CLinkMessageWnd(CLinkModule* owner)
    : m_owner(owner)
{
}

LRESULT CLinkMessageWnd::OnLinkEvent(UINT, WPARAM, LPARAM, BOOL& bHandled)
{
    bHandled = TRUE;
    if (m_owner != NULL)
        m_owner->OnLinkEvents();
    return 0;
}

// ==================== CLinkModule ====================

CLinkModule& CLinkModule::Instance()
{
    static CLinkModule instance;
    return instance;
}

CLinkModule::CLinkModule()
    : m_host(NULL)
    , m_inited(false)
    , m_enterpriseId(0)
    , m_pageActive(false)
    , m_pageCovered(false)
    , m_desired(0)
    , m_state(link_state_stopped)
    , m_attempt(0)
    , m_delayMs(0)
    , m_waitStartTick(0)
    , m_subscribed(0)
    , m_lastError(0)
    , m_received(false)
{
}

uint32_t CLinkModule::GetApiVersion() const
{
    return kApiVersion;
}

bool CLinkModule::Init(ILinkHost* host)
{
    if (m_inited)
        return true;
    if (host == NULL)
        return false;

    m_host = host;
    SetLogHost(host);

    WSADATA wsa;
    if (::WSAStartup(MAKEWORD(kWinsockMajor, kWinsockMinor), &wsa) != 0)
    {
        LINK_LOG_ERROR("adminlink WSAStartup failed: %d", ::WSAGetLastError());
        SetLogHost(NULL);
        m_host = NULL;
        return false;
    }

    //libsodium 必须在用到任何加密功能之前初始化一次（本 DLL 静态链接自己的一份）
    if (!net::cryptoInit())
    {
        LINK_LOG_ERROR("adminlink crypto init failed");
        ::WSACleanup();
        SetLogHost(NULL);
        m_host = NULL;
        return false;
    }

    m_msgWnd.reset(new CLinkMessageWnd(this));
    if (m_msgWnd->Create(HWND_MESSAGE) == NULL)
    {
        LINK_LOG_ERROR("adminlink create message window failed: %u", (unsigned)::GetLastError());
        m_msgWnd.reset();
        ::WSACleanup();
        SetLogHost(NULL);
        m_host = NULL;
        return false;
    }
    m_client.SetNotifyTarget(m_msgWnd->m_hWnd, CLinkMessageWnd::kMsgLinkEvent);

    m_inited = true;
    LINK_LOG_INFO("FlamingoLink initialized, api version %u", (unsigned)kApiVersion);
    return true;
}

void CLinkModule::Shutdown()
{
    if (!m_inited)
        return;

    Stop();
    DestroyPage();

    if (m_msgWnd && m_msgWnd->IsWindow())
        m_msgWnd->DestroyWindow();
    m_msgWnd.reset();

    LINK_LOG_INFO("FlamingoLink shut down");
    ::WSACleanup();
    SetLogHost(NULL);
    m_host = NULL;
    m_inited = false;
}

void CLinkModule::Start(const LinkStartParams* params)
{
    if (!m_inited || params == NULL)
        return;

    m_client.Stop();
    m_table.Clear();
    m_received = false;
    m_subscribed = 0;
    m_lastError = 0;
    m_attempt = 0;
    m_delayMs = 0;

    if (params->m_port <= 0 || params->m_host == NULL || params->m_host[0] == L'\0')
    {
        LINK_LOG_INFO("adminlink disabled: server did not provide a link port");
        m_state = link_state_disabled;
        RefreshPage();
        return;
    }

    LinkConfig cfg;
    cfg.m_host = params->m_host;
    cfg.m_port = params->m_port;
    cfg.m_token = (params->m_token != NULL) ? params->m_token : "";
    cfg.m_clientVersion = (params->m_clientVersion != NULL) ? params->m_clientVersion : "";

    const std::string pubkey = (params->m_pubkeyBase64 != NULL) ? params->m_pubkeyBase64 : "";
    if (!pubkey.empty())
    {
        //服务端下发了公钥却解析不出来：不能退回明文，按服务器不可用处理
        if (!net::parseServerPublicKey(pubkey, cfg.m_pubkey))
        {
            LINK_LOG_ERROR("adminlink disabled: invalid server public key from login.do");
            m_state = link_state_disabled;
            RefreshPage();
            return;
        }
        cfg.m_encrypt = true;
    }

    m_client.SetDesiredEnterprise(m_desired);
    m_client.Start(cfg);
    m_state = link_state_connecting;
    RefreshPage();
}

void CLinkModule::Stop()
{
    m_client.Stop();
    m_table.Clear();
    m_received = false;
    m_subscribed = 0;
    m_lastError = 0;
    m_attempt = 0;
    m_delayMs = 0;
    m_state = link_state_stopped;
    RefreshPage();
}

HWND CLinkModule::CreatePage(HWND parent)
{
    if (!m_inited || !::IsWindow(parent))
        return NULL;

    if (m_page && m_page->IsWindow())
        return m_page->m_hWnd;

    m_page.reset(new COnlinePage());
    if (!m_page->CreatePage(parent, this))
    {
        LINK_LOG_ERROR("adminlink create page window failed: %u", (unsigned)::GetLastError());
        m_page.reset();
        return NULL;
    }

    RefreshPage();
    UpdatePageVisibility();
    return m_page->m_hWnd;
}

void CLinkModule::DestroyPage()
{
    if (m_page && m_page->IsWindow())
        m_page->DestroyWindow();
    m_page.reset();
}

void CLinkModule::SetPageBounds(const RECT* rc)
{
    if (rc == NULL || !m_page || !m_page->IsWindow())
        return;

    m_page->SetWindowPos(HWND_TOP, rc->left, rc->top, rc->right - rc->left, rc->bottom - rc->top,
                         SWP_NOACTIVATE);
}

void CLinkModule::SetEnterprise(const wchar_t* enterpriseId, const wchar_t* enterpriseName)
{
    int64_t id = 0;
    ParseEnterpriseIdText(enterpriseId, id);

    m_enterpriseName = (enterpriseName != NULL) ? enterpriseName : L"";
    if (id != m_enterpriseId)
    {
        m_enterpriseId = id;
        m_table.Clear();
        m_received = false;
        m_lastError = 0;
        m_subscribed = 0;
        //名单已清空：要求连接线程重新订阅一次，即使企业换来换去之后又回到了它已订阅的那一个
        m_client.RequestResubscribe();
    }

    UpdateSubscription();
    RefreshPage();
}

void CLinkModule::SetPageActive(bool active)
{
    m_pageActive = active;
    UpdateSubscription();
    UpdatePageVisibility();
    RefreshPage();
}

void CLinkModule::SetPageCovered(bool covered)
{
    m_pageCovered = covered;
    UpdatePageVisibility();
}

void CLinkModule::GetStatus(LinkStatus* out)
{
    if (out == NULL)
        return;

    out->m_state = m_state;
    out->m_retryAttempt = (m_state == link_state_waiting) ? m_attempt : 0;
    out->m_retryDelayMs = (m_state == link_state_waiting) ? m_delayMs : 0;
    out->m_subscribedEnterprise = m_subscribed;
    out->m_onlineUsers = m_table.UserCount();
    out->m_onlineDevices = static_cast<int>(m_table.Rows().size());
    out->m_lastError = m_lastError;
}

void CLinkModule::GetViewState(OnlineViewState& out)
{
    out.m_table = &m_table;
    out.m_hasEnterprise = (m_enterpriseId > 0);
    out.m_enterpriseName = m_enterpriseName;
    out.m_state = m_state;
    out.m_attempt = m_attempt;
    out.m_delayMs = m_delayMs;
    out.m_waitStartTick = m_waitStartTick;
    out.m_lastError = m_lastError;
    out.m_received = m_received;
}

void CLinkModule::OnLinkEvents()
{
    std::vector<LinkEvent> events;
    m_client.DrainEvents(events);
    if (events.empty())
        return;

    bool sessionExpired = false;
    for (size_t i = 0; i < events.size(); ++i)
        HandleEvent(events[i], sessionExpired);

    RefreshPage();

    //最后才通知宿主：宿主会在回调里调用 Stop，那时本函数已不再访问事件
    if (sessionExpired && m_host != NULL)
        m_host->OnSessionExpired();
}

void CLinkModule::HandleEvent(const LinkEvent& ev, bool& sessionExpired)
{
    switch (ev.m_kind)
    {
    //连接状态变化。连接不正常时服务端那边的订阅已随连接消失，重连成功后连接线程会自动重新订阅
    case link_event_state:
        m_state = ev.m_state;
        m_attempt = ev.m_attempt;
        m_delayMs = ev.m_delayMs;
        if (ev.m_state == link_state_waiting)
            m_waitStartTick = ::GetTickCount64();
        if (ev.m_state != link_state_connected)
            m_subscribed = 0;
        break;

    //订阅应答：只认当前想订阅的那个企业的应答（换企业后旧应答可能才到）
    case link_event_subscribe:
        if (ev.m_enterpriseId != m_desired)
            break;
        if (ev.m_code == 0)
        {
            m_subscribed = ev.m_enterpriseId;
            m_lastError = 0;
        }
        else
        {
            m_subscribed = 0;
            m_lastError = ev.m_code;
            m_table.Clear();
            m_received = false;
        }
        break;

    //名单推送：只应用当前企业的；完整名单的首帧到达即视为已收到名单
    case link_event_online:
        if (m_table.Apply(ev.m_notify, m_enterpriseId) && ev.m_notify.m_reset)
            m_received = true;
        break;

    //鉴权时 token 已失效
    case link_event_expired:
        m_state = link_state_expired;
        sessionExpired = true;
        break;

    //连接线程发出了退订：服务端那边已不再订阅
    case link_event_unsubscribed:
        m_subscribed = 0;
        break;

    //不认识的事件类型（不会出现）
    default:
        break;
    }
}

void CLinkModule::UpdateSubscription()
{
    const int64_t desired = DesiredEnterprise(m_pageActive, m_enterpriseId);
    if (desired == m_desired)
        return;

    //「已订阅企业」不在这里改：要等连接线程真的发出退订或收到订阅应答（见 HandleEvent）
    m_desired = desired;
    m_client.SetDesiredEnterprise(desired);
}

void CLinkModule::UpdatePageVisibility()
{
    if (!m_page || !m_page->IsWindow())
        return;

    const bool show = m_pageActive && !m_pageCovered;
    m_page->ShowWindow(show ? SW_SHOWNA : SW_HIDE);
}

void CLinkModule::RefreshPage()
{
    if (m_page && m_page->IsWindow())
        m_page->Refresh();
}

} // namespace flamingolink
