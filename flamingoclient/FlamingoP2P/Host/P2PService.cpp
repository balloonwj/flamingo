/**
 *  P2P 服务的实现，见 P2PService.h。
 *  balloonwj@qq.com   2026-10-03
 */
#include "P2PService.h"

#include <string.h>

#include "../../Source/Common/Log/IULog.h"

namespace p2p {

CP2PService::CP2PService()
    : m_enabled(false)
    , m_observer(NULL)
{
}

void CP2PService::SetEnabled(bool enabled)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    m_enabled = enabled;
    if (!enabled)
        m_handlers.clear();
}

IP2PLinkHandler* CP2PService::FindHandler(const char* purpose)
{
    if (!IsValidPurpose(purpose))
        return NULL;

    std::lock_guard<std::mutex> guard(m_mutex);
    if (!m_enabled)
        return NULL;
    std::map<std::string, IP2PLinkHandler*>::const_iterator it = m_handlers.find(purpose);
    return it == m_handlers.end() ? NULL : it->second;
}

void CP2PService::ListPurposes(std::vector<std::string>& purposes)
{
    purposes.clear();
    std::lock_guard<std::mutex> guard(m_mutex);
    if (!m_enabled)
        return;
    for (std::map<std::string, IP2PLinkHandler*>::const_iterator it = m_handlers.begin(); it != m_handlers.end(); ++it)
        purposes.push_back(it->first);
}

void CP2PService::SetObserver(IP2PServiceObserver* observer)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    m_observer = observer;
}

bool CP2PService::RegisterHandler(const char* purpose, IP2PLinkHandler* handler)
{
    if (!IsValidPurpose(purpose) || handler == NULL)
        return false;

    IP2PServiceObserver* observer = NULL;
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        if (!m_enabled)
            return false;
        if (m_handlers.find(purpose) != m_handlers.end())
        {
            LOG_WARNING("p2p handler for purpose '%s' is already registered, the new one is rejected.", purpose);
            return false;
        }
        m_handlers[purpose] = handler;
        observer = m_observer;
    }

    LOG_INFO("p2p handler registered, purpose: %s.", purpose);
    //释放互斥量之后再通知：观察者会经引擎回头调用 ListPurposes
    if (observer != NULL)
        observer->OnHandlersChanged();
    return true;
}

bool CP2PService::UnregisterHandler(const char* purpose)
{
    if (!IsValidPurpose(purpose))
        return false;

    IP2PServiceObserver* observer = NULL;
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        std::map<std::string, IP2PLinkHandler*>::iterator it = m_handlers.find(purpose);
        if (it == m_handlers.end())
            return false;
        m_handlers.erase(it);
        observer = m_observer;
    }

    LOG_INFO("p2p handler unregistered, purpose: %s.", purpose);
    if (observer != NULL)
        observer->OnHandlersChanged();
    return true;
}

bool CP2PService::IsValidPurpose(const char* purpose)
{
    if (purpose == NULL)
        return false;
    size_t len = strlen(purpose);
    return len > 0 && len <= kMaxPurposeChars;
}

} // namespace p2p
