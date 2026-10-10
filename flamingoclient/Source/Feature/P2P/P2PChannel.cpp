/**
 *  P2P 信令转发通道（放行规则与帧路由）的实现，见 P2PChannel.h。
 *  balloonwj@qq.com   2026-10-03
 */
#include "stdafx.h"

#include "P2PChannel.h"

namespace balloonp2p {

bool P2PChannelPolicy::CanSendP2PCommand(uint16_t cmd, uint32_t bodyLen, bool connected)
{
    if (!p2p::IsP2PCommand(cmd))
        return false;
    if (bodyLen == 0)
        return false;
    return connected;
}

CP2PFrameRouter::CP2PFrameRouter()
    : m_module(NULL)
    , m_dropped(0)
{
}

void CP2PFrameRouter::Attach(p2p::IP2PModule* module)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    m_module = module;
}

void CP2PFrameRouter::Detach()
{
    //与 Route 共用一把锁：拿到锁时，网络线程上正在进行的转交一定已经结束
    std::lock_guard<std::mutex> guard(m_mutex);
    m_module = NULL;
}

bool CP2PFrameRouter::Route(uint16_t cmd, uint32_t seq, const char* body, uint32_t len)
{
    if (!p2p::IsP2PCommand(cmd))
        return false;

    std::lock_guard<std::mutex> guard(m_mutex);
    if (m_module == NULL)
    {
        ++m_dropped;
        return false;
    }
    return m_module->OnServerFrame(cmd, seq, body, len);
}

uint32_t CP2PFrameRouter::GetDroppedCount() const
{
    std::lock_guard<std::mutex> guard(m_mutex);
    return m_dropped;
}

} // namespace balloonp2p
