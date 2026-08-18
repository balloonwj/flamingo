/**
 *  远程桌面抓屏降级决策状态机实现，CaptureFallbackPolicy.cpp
 *  balloonwj@qq.com   2026-06-11
 */
#include "CaptureFallbackPolicy.h"

namespace remotedesktop {

CCaptureFallbackPolicy::CCaptureFallbackPolicy()
    : m_backend(kBackendNone)
    , m_rebuildAttempts(0)
{
}

void CCaptureFallbackPolicy::Reset(CaptureBackend backend)
{
    m_backend = backend;
    m_rebuildAttempts = 0;
}

FallbackAction CCaptureFallbackPolicy::OnResult(CaptureResult r)
{
    if (r == kCaptureOk)
    {
        //真正出帧才算恢复：清零重建尝试计数，继续
        m_rebuildAttempts = 0;
        return kActionContinue;
    }
    if (r == kCaptureTimeout)
    {
        //无新帧是正常态（屏幕没变）：不算失败、不降级
        return kActionContinue;
    }

    //AccessLost / Fatal：只有 DXGI 后端才有降级动作；其它后端无处可退，继续
    if (m_backend != kBackendDxgi)
        return kActionContinue;

    if (r == kCaptureFatal)
    {
        //不可恢复：直接降级 GDI，不浪费重建
        return kActionFallbackGdi;
    }

    //AccessLost：自上次出帧以来已重建够多次仍救不回，降级 GDI；否则再试一次重建
    if (m_rebuildAttempts >= kMaxRebuildAttempts)
        return kActionFallbackGdi;
    return kActionRebuildDxgi;
}

void CCaptureFallbackPolicy::NotifyRebuildAttempt()
{
    ++m_rebuildAttempts;
}

CaptureBackend CCaptureFallbackPolicy::Backend() const
{
    return m_backend;
}

int CCaptureFallbackPolicy::RebuildAttempts() const
{
    return m_rebuildAttempts;
}

} // namespace remotedesktop
