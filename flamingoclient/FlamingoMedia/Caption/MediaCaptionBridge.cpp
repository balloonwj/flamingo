/**
 *  FlamingoMedia.dll 与 FlamingoCaption.dll 的衔接，见 MediaCaptionBridge.h。
 *  balloonwj@qq.com   2026-10-06
 */
#include "stdafx.h"

#include "MediaCaptionBridge.h"

#include <algorithm>

#include "../../Source/Common/Log/IULog.h"   //LOG_INFO 等宏；DLL 内由 IULogBridge.cpp 实现，经宿主写日志

namespace flamingomedia {

CMediaCaptionBridge& CMediaCaptionBridge::Instance()
{
    static CMediaCaptionBridge s_instance;
    return s_instance;
}

CMediaCaptionBridge::CMediaCaptionBridge()
    : m_host(NULL)
{
}

void CMediaCaptionBridge::Attach(IMediaHost* host)
{
    m_host = host;
}

void CMediaCaptionBridge::Detach()
{
    m_host = NULL;
    m_observers.clear();
}

bool CMediaCaptionBridge::IsAvailable() const
{
    return m_host != NULL && m_host->IsCaptionAvailable();
}

caption::ICaptionModule* CMediaCaptionBridge::Acquire()
{
    if (!IsAvailable())
        return NULL;
    caption::ICaptionModule* module = m_host->GetCaptionService();
    if (module == NULL)
        LOG_WARNING("caption bridge: caption service unavailable (dll load failed).");
    return module;
}

void CMediaCaptionBridge::SetAvailable(bool available)
{
    LOG_INFO("caption bridge: availability changed to %d, %u observer(s).", available ? 1 : 0,
             static_cast<unsigned>(m_observers.size()));
    //先拷贝一份：观察者在回调里注销自己时不影响本次遍历
    const std::vector<ICaptionAvailabilityObserver*> observers = m_observers;
    for (size_t i = 0; i < observers.size(); ++i)
    {
        if (std::find(m_observers.begin(), m_observers.end(), observers[i]) != m_observers.end())
            observers[i]->OnCaptionAvailabilityChanged(available);
    }
}

void CMediaCaptionBridge::AddObserver(ICaptionAvailabilityObserver* observer)
{
    if (observer == NULL)
        return;
    if (std::find(m_observers.begin(), m_observers.end(), observer) == m_observers.end())
        m_observers.push_back(observer);
}

void CMediaCaptionBridge::RemoveObserver(ICaptionAvailabilityObserver* observer)
{
    std::vector<ICaptionAvailabilityObserver*>::iterator it =
        std::find(m_observers.begin(), m_observers.end(), observer);
    if (it != m_observers.end())
        m_observers.erase(it);
}

} // namespace flamingomedia
