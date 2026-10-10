/**
 *  文件共享信令转发通道（放行规则与帧路由）的实现，见 ShareChannel.h。
 *  balloonwj@qq.com   2026-10-02
 */
#include "stdafx.h"

#include "ShareChannel.h"

namespace balloonfileshare {

bool ShareChannelPolicy::CanSendShareCommand(uint16_t cmd, uint32_t bodyLen, bool connected)
{
    if (!fileshare::IsShareCommand(cmd))
        return false;
    if (bodyLen == 0)
        return false;
    return connected;
}

CShareFrameRouter::CShareFrameRouter()
    : m_module(NULL)
    , m_dropped(0)
{
}

void CShareFrameRouter::Attach(fileshare::IFileShareModule* module)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    m_module = module;
}

void CShareFrameRouter::Detach()
{
    std::lock_guard<std::mutex> guard(m_mutex);
    m_module = NULL;
}

bool CShareFrameRouter::Route(uint16_t cmd, uint32_t seq, const char* body, uint32_t len)
{
    if (!fileshare::IsShareCommand(cmd))
        return false;

    std::lock_guard<std::mutex> guard(m_mutex);
    if (m_module == NULL)
    {
        ++m_dropped;
        return false;
    }
    return m_module->OnServerFrame(cmd, seq, body, len);
}

uint32_t CShareFrameRouter::GetDroppedCount() const
{
    std::lock_guard<std::mutex> guard(m_mutex);
    return m_dropped;
}

} // namespace balloonfileshare
