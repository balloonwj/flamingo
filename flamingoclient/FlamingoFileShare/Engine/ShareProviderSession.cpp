/**
 *  分享者一端访问会话 CShareProviderSession 的实现，见 ShareProviderSession.h。
 *  balloonwj@qq.com   2026-10-02
 */
#include "stdafx.h"

#include "ShareProviderSession.h"

#include <string.h>   //memcpy
#include <windows.h>  //GetTickCount64
#include <chrono>

#include "../../Source/Common/Log/IULog.h"

namespace fileshare {

CShareProviderSession::CShareProviderSession(const ShareProviderContext& ctx, const ShareRelayParams& relay,
                                             const unsigned char* serverPubKey, IShareSessionEventSink* sink)
    : m_relay(relay)
    , m_hasServerPubKey(serverPubKey != NULL)
    , m_sink(sink)
    , m_provider(ctx, this, this)
    , m_closing(false)
    , m_connectDone(false)
    , m_connected(false)
{
    memset(m_serverPubKey, 0, sizeof(m_serverPubKey));
    if (serverPubKey != NULL)
        memcpy(m_serverPubKey, serverPubKey, sizeof(m_serverPubKey));
}

CShareProviderSession::~CShareProviderSession()
{
    //兜底：正常流程下此时连接线程已经结束、连接已经关闭，这里不会阻塞
    if (m_connectThread.joinable())
        m_connectThread.join();
    m_client.Close();
}

void CShareProviderSession::StartConnect()
{
    m_connectThread = std::thread(&CShareProviderSession::ConnectProc, this);
}

bool CShareProviderSession::Close(int byeErrCode)
{
    bool sendBye = false;
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        m_closing = true;
        //唤醒正在重发 sf_hello 的连接线程，让它立即退出
        m_cv.notify_all();
        if (!m_connectDone)
            return false;
        sendBye = m_connected && byeErrCode != kShareNoBye;
    }

    if (sendBye)
        m_provider.SendBye(byeErrCode);
    m_client.Close();
    if (m_connectThread.joinable())
        m_connectThread.join();
    return true;
}

void CShareProviderSession::WaitConnectThread()
{
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        m_closing = true;
        m_cv.notify_all();
    }
    if (m_connectThread.joinable())
        m_connectThread.join();
}

void CShareProviderSession::SetPerms(int perms)
{
    bool connected = false;
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        connected = m_connected && !m_closing;
    }
    //未连接时只更新权限位，sf_hello 发出时会带上新值
    if (connected)
        m_provider.SetPerms(perms);
}

const std::string& CShareProviderSession::GetAccessId() const
{
    return m_relay.m_accessId;
}

int64_t CShareProviderSession::GetShareId() const
{
    return m_relay.m_shareId;
}

int64_t CShareProviderSession::GetVisitorId() const
{
    return m_relay.m_visitorId;
}

const std::string& CShareProviderSession::GetVisitorName() const
{
    return m_relay.m_visitorName;
}

void CShareProviderSession::ConnectProc()
{
    const bool ok = m_client.Connect(m_relay.m_relayIp, m_relay.m_relayPort, m_relay.m_token,
                                     foldertransfer::kFolderControlLane,
                                     m_hasServerPubKey ? m_serverPubKey : NULL, this);
    bool startProvider = false;
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        m_connected = ok;
        startProvider = ok && !m_closing;
    }

    //sf_hello 必须是这条通道上分享者发出的第一帧，放在投递事件之前发
    if (startProvider)
        m_provider.Start();
    LOG_INFO("share provider relay %s, accessid: %s, relay: %s:%d.", ok ? "connected" : "connect failed",
             m_relay.m_accessId.c_str(), m_relay.m_relayIp.c_str(), m_relay.m_relayPort);

    {
        std::lock_guard<std::mutex> guard(m_mutex);
        m_connectDone = true;
    }
    Post(ok ? share_provider_event_connected : share_provider_event_connect_failed, 0);

    if (startProvider)
        ResendHelloUntilHeard();
}

void CShareProviderSession::ResendHelloUntilHeard()
{
    const int timeoutMs = (m_relay.m_openTimeoutMs > 0) ? m_relay.m_openTimeoutMs : kShareDefaultOpenTimeoutMs;
    const ULONGLONG deadline = ::GetTickCount64() + static_cast<ULONGLONG>(timeoutMs);
    int resent = 0;
    std::unique_lock<std::mutex> lock(m_mutex);
    while (!m_closing && !m_provider.HasHeardFromVisitor() && ::GetTickCount64() < deadline)
    {
        m_cv.wait_for(lock, std::chrono::milliseconds(kShareHelloResendMs));
        if (m_closing || m_provider.HasHeardFromVisitor())
            break;
        //发送不持锁：Close 在 UI 线程上要拿这把锁
        lock.unlock();
        m_provider.ResendHello();
        ++resent;
        lock.lock();
    }
    LOG_INFO("share provider hello resend loop ended, accessid: %s, resent: %d, heard visitor: %d, closing: %d.",
             m_relay.m_accessId.c_str(), resent, m_provider.HasHeardFromVisitor() ? 1 : 0, m_closing ? 1 : 0);
}

void CShareProviderSession::OnFolderRelayAppFrame(const unsigned char* data, size_t len)
{
    m_provider.OnFrame(reinterpret_cast<const char*>(data), len);
    //收到了访问者的帧：让连接线程停止重发 sf_hello
    m_cv.notify_all();
}

void CShareProviderSession::OnFolderRelayClosed()
{
    LOG_INFO("share provider relay closed by peer or network, accessid: %s.", m_relay.m_accessId.c_str());
    Post(share_provider_event_channel_closed, 0);
}

bool CShareProviderSession::SendFrame(const std::string& frame)
{
    return m_client.SendAppFrame(reinterpret_cast<const unsigned char*>(frame.data()), frame.size());
}

void CShareProviderSession::OnVisitorBye(const std::string& accessId, int errCode)
{
    (void)accessId;
    Post(share_provider_event_visitor_bye, errCode);
}

void CShareProviderSession::OnWriteOpDone(const std::string& accessId, const std::vector<ShareAuditEntry>& audits)
{
    (void)accessId;
    ShareSessionEvent ev;
    ev.m_type = share_provider_event_write_done;
    ev.m_accessId = m_relay.m_accessId;
    ev.m_audits = audits;
    m_sink->PostSessionEvent(ev);
}

void CShareProviderSession::Post(int type, int errCode)
{
    ShareSessionEvent ev;
    ev.m_type = type;
    ev.m_accessId = m_relay.m_accessId;
    ev.m_errCode = errCode;
    m_sink->PostSessionEvent(ev);
}

} // namespace fileshare
