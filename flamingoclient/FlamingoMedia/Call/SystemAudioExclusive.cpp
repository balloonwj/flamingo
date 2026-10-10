/**
 *  共享电脑声音的进程内互斥，说明见 SystemAudioExclusive.h。
 *  balloonwj@qq.com   2026-10-06
 */
#include "SystemAudioExclusive.h"

#include <stddef.h>   //NULL

namespace call {

SystemAudioExclusive& SystemAudioExclusive::Instance()
{
    static SystemAudioExclusive s_instance;
    return s_instance;
}

SystemAudioExclusive::SystemAudioExclusive()
    : m_current(NULL)
{
}

SystemAudioHolder* SystemAudioExclusive::Acquire(SystemAudioHolder* holder)
{
    if (holder == NULL)
        return NULL;
    SystemAudioHolder* previous = m_current;
    //先改登记再回调：被抢占的一方在回调里调用 Release 时，登记已经不是它，不会把新登记清掉
    m_current = holder;
    if (previous == NULL || previous == holder)
        return NULL;
    previous->OnSystemAudioPreempted();
    return previous;
}

void SystemAudioExclusive::Release(SystemAudioHolder* holder)
{
    if (holder != NULL && m_current == holder)
        m_current = NULL;
}

SystemAudioHolder* SystemAudioExclusive::Current() const
{
    return m_current;
}

} // namespace call
