/**
 *  访问者一端中转通道 CShareVisitorChannel 的实现，见 ShareVisitorChannel.h。
 *  balloonwj@qq.com   2026-10-02
 */
#include "stdafx.h"

#include "ShareVisitorChannel.h"

#include <string.h>   //memcpy / memset

#include "../../Source/Common/Log/IULog.h"

namespace fileshare {

CShareVisitorChannel::CShareVisitorChannel(const ShareRelayParams& relay, const unsigned char* serverPubKey,
                                           IShareSessionEventSink* sink)
    : m_relay(relay)
    , m_hasServerPubKey(serverPubKey != NULL)
    , m_sink(sink)
    , m_closing(false)
    , m_connectDone(false)
{
    memset(m_serverPubKey, 0, sizeof(m_serverPubKey));
    if (serverPubKey != NULL)
        memcpy(m_serverPubKey, serverPubKey, sizeof(m_serverPubKey));
}

CShareVisitorChannel::~CShareVisitorChannel()
{
    if (m_connectThread.joinable())
        m_connectThread.join();
    m_client.Close();
}

void CShareVisitorChannel::StartConnect()
{
    m_connectThread = std::thread(&CShareVisitorChannel::ConnectProc, this);
}

bool CShareVisitorChannel::SendFrame(const std::string& frame)
{
    return m_client.SendAppFrame(reinterpret_cast<const unsigned char*>(frame.data()), frame.size());
}

bool CShareVisitorChannel::Close()
{
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        m_closing = true;
        if (!m_connectDone)
            return false;
    }
    m_client.Close();
    if (m_connectThread.joinable())
        m_connectThread.join();
    return true;
}

void CShareVisitorChannel::WaitConnectThread()
{
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        m_closing = true;
    }
    if (m_connectThread.joinable())
        m_connectThread.join();
}

const std::string& CShareVisitorChannel::GetAccessId() const
{
    return m_relay.m_accessId;
}

void CShareVisitorChannel::ConnectProc()
{
    const bool ok = m_client.Connect(m_relay.m_relayIp, m_relay.m_relayPort, m_relay.m_token,
                                     foldertransfer::kFolderControlLane,
                                     m_hasServerPubKey ? m_serverPubKey : NULL, this);
    LOG_INFO("share visitor relay %s, accessid: %s, relay: %s:%d.", ok ? "connected" : "connect failed",
             m_relay.m_accessId.c_str(), m_relay.m_relayIp.c_str(), m_relay.m_relayPort);
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        m_connectDone = true;
    }
    Post(ok ? share_visitor_event_connected : share_visitor_event_connect_failed, NULL, 0);
}

void CShareVisitorChannel::OnFolderRelayAppFrame(const unsigned char* data, size_t len)
{
    Post(share_visitor_event_frame, reinterpret_cast<const char*>(data), len);
}

void CShareVisitorChannel::OnFolderRelayClosed()
{
    LOG_INFO("share visitor relay closed by peer or network, accessid: %s.", m_relay.m_accessId.c_str());
    Post(share_visitor_event_channel_closed, NULL, 0);
}

void CShareVisitorChannel::Post(int type, const char* data, size_t len)
{
    ShareSessionEvent ev;
    ev.m_type = type;
    ev.m_accessId = m_relay.m_accessId;
    if (data != NULL && len > 0)
        ev.m_data.assign(data, len);
    m_sink->PostSessionEvent(ev);
}

} // namespace fileshare
