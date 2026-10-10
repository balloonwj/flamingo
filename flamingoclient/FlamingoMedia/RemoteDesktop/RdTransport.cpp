/**
 *  远程桌面一端的传输的实现，见 RdTransport.h。
 *  balloonwj@qq.com   2026-10-04
 */
#include "RdTransport.h"

#include <string.h>

#include "../../Source/Common/Log/IULog.h"   //LOG_INFO / LOG_WARNING（DLL 内经日志桥接写进 exe 的日志）

namespace remotedesktop {

// ======================== RdTransportParams ========================

RdTransportParams::RdTransportParams()
    : m_relayPort(0)
    , m_hasServerPubKey(false)
{
    memset(m_serverPubKey, 0, sizeof(m_serverPubKey));
}

void RdTransportParams::SetRelay(const std::string& sessionId, const std::string& relayIp, int relayPort,
                                 const std::string& token, const unsigned char* serverPubKey)
{
    m_sessionId = sessionId;
    m_relayIp = relayIp;
    m_relayPort = relayPort;
    m_token = token;
    m_hasServerPubKey = (serverPubKey != NULL);
    if (m_hasServerPubKey)
        memcpy(m_serverPubKey, serverPubKey, sizeof(m_serverPubKey));
    else
        memset(m_serverPubKey, 0, sizeof(m_serverPubKey));
}

// ======================== RdTransport ========================

RdTransport::RdTransport()
    : m_handler(NULL)
    , m_path(rd_path_none)
{
}

RdTransport::~RdTransport()
{
    Close();
    m_channel.reset();
}

bool RdTransport::Connect(const RdTransportParams& params, RdRelayClientHandler* handler,
                          const std::atomic<bool>* cancel)
{
    if (handler == NULL)
        return false;
    //上一次连接已经由调用方 Close，且没有线程还在用它：此时可以重建帧通道
    Close();
    m_channel.reset();
    m_slot = params.m_slot;
    m_handler = handler;

    if (m_slot)
    {
        bool cancelled = false;
        if (ConnectP2P(params, cancel, &cancelled))
            return true;
        if (cancelled)
        {
            LOG_INFO("RdTransport: connect cancelled while waiting for the p2p link, session=%s.",
                     params.m_sessionId.c_str());
            return false;
        }
    }

    //没有 P2P，或 P2P 没成：连 imgserver 中转（与第 6 期之前相同）
    if (!m_relay.Connect(params.m_relayIp, params.m_relayPort, params.m_token,
                         params.m_hasServerPubKey ? params.m_serverPubKey : NULL, handler, kRdRelayConnectTimeoutSec))
    {
        LOG_WARNING("RdTransport: connect relay %s:%d failed, session=%s.", params.m_relayIp.c_str(),
                    params.m_relayPort, params.m_sessionId.c_str());
        return false;
    }
    m_path = rd_path_relay;
    LOG_INFO("RdTransport: path decided, session=%s, path=relay, p2p offered=%d.", params.m_sessionId.c_str(),
             m_slot ? 1 : 0);
    return true;
}

bool RdTransport::ConnectP2P(const RdTransportParams& params, const std::atomic<bool>* cancel, bool* cancelled)
{
    //分段等待链路结果，每段之间检查取消标志：用户在等待期间结束会话时不必等满 kRdP2PLinkWaitMs
    const int64_t start = balloonp2p::P2PSteadyNowMs();
    int state = balloonp2p::P2PLinkSlot::slot_waiting;
    while (true)
    {
        if (cancel != NULL && cancel->load())
        {
            *cancelled = true;
            return false;
        }
        state = m_slot->Wait(kRdP2PWaitSliceMs);
        if (state != balloonp2p::P2PLinkSlot::slot_waiting)
            break;
        if (balloonp2p::P2PSteadyNowMs() - start >= kRdP2PLinkWaitMs)
            break;
    }
    const long long waited = (long long)(balloonp2p::P2PSteadyNowMs() - start);
    if (state != balloonp2p::P2PLinkSlot::slot_ready)
    {
        LOG_INFO("RdTransport: p2p link not available, session=%s, slot state=%d, waited=%lld ms, using relay.",
                 params.m_sessionId.c_str(), state, waited);
        return false;
    }
    m_channel.reset(new balloonp2p::P2PStreamChannel(m_slot, kRdP2PMaxFrameBytes, kRdP2PSendWaitMs));
    if (!m_channel->Open(kRdP2PChannelId, this))
    {
        LOG_WARNING("RdTransport: open p2p channel failed, session=%s, using relay.", params.m_sessionId.c_str());
        m_channel->Close();
        return false;
    }
    m_path = rd_path_p2p;
    LOG_INFO("RdTransport: path decided, session=%s, path=p2p, waited=%lld ms.", params.m_sessionId.c_str(), waited);
    return true;
}

bool RdTransport::SendEngineFrame(const unsigned char* data, size_t len)
{
    const int path = m_path;
    if (path == rd_path_p2p)
        return m_channel->SendFrame(data, len);
    if (path == rd_path_relay)
        return m_relay.SendEngineFrame(data, len);
    return false;
}

void RdTransport::Close()
{
    m_path = rd_path_none;
    //帧通道只关闭不销毁：别的线程可能正停在 SendFrame 里，对象要留到下次 Connect 或析构
    if (m_channel)
        m_channel->Close();
    //会话结束时立即关闭 P2P 链路，服务端随之结束 P2P 会话；槽对象（及链路的引用）留到下次 Connect 或析构
    if (m_slot)
        m_slot->CloseLink();
    m_relay.Close();
}

bool RdTransport::IsConnected() const
{
    const int path = m_path;
    if (path == rd_path_p2p)
        return m_channel->IsOpen();
    if (path == rd_path_relay)
        return m_relay.IsConnected();
    return false;
}

bool RdTransport::GetP2PStats(p2p::P2PLinkStats& out) const
{
    memset(&out, 0, sizeof(out));
    out.m_cbSize = sizeof(out);
    if (m_path != rd_path_p2p || !m_slot)
        return false;
    p2p::IP2PLink* link = m_slot->Link();
    if (link == NULL)
        return false;
    link->GetStats(&out);
    return true;
}

int RdTransport::LinkPath() const
{
    return balloonp2p::LinkPathWhenUsing(m_path == rd_path_p2p, m_slot);
}

bool RdTransport::NeedsPeriodicKeyframes(int path)
{
    return path != rd_path_p2p;
}

void RdTransport::OnStreamFrame(const unsigned char* data, size_t len)
{
    m_handler->OnRelayEngineFrame(data, len);
}

void RdTransport::OnStreamClosed()
{
    m_handler->OnRelayClosed();
}

} // namespace remotedesktop
