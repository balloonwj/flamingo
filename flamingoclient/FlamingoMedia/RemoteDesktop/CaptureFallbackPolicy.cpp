/**
 *  远程桌面抓屏降级决策状态机实现，CaptureFallbackPolicy.cpp
 *  balloonwj@qq.com   2026-06-11
 */
#include "CaptureFallbackPolicy.h"

namespace remotedesktop {

CCaptureFallbackPolicy::CCaptureFallbackPolicy()
    : m_backend(kBackendNone)
    , m_rebuildAttempts(0)
    , m_retryArmed(false)
    , m_retryAtMs(0)
    , m_retryIntervalMs(kDxgiRetryFirstMs)
{
}

CaptureResult CCaptureFallbackPolicy::ClassifyAcquireResult(HRESULT hr)
{
    if (hr == DXGI_ERROR_WAIT_TIMEOUT)
        return kCaptureTimeout;
    //除超时以外的失败都先重建：DXGI_ERROR_ACCESS_LOST 之外，分辨率变化时实测还会返回 DXGI_ERROR_INVALID_CALL，
    //复制对象此时已不可用，重建即可恢复；重建失败由降级策略照常切到 GDI
    if (FAILED(hr))
        return kCaptureAccessLost;
    return kCaptureOk;
}

void CCaptureFallbackPolicy::Reset(CaptureBackend backend)
{
    m_backend = backend;
    m_rebuildAttempts = 0;
    m_retryArmed = false;
    m_retryIntervalMs = kDxgiRetryFirstMs;
}

void CCaptureFallbackPolicy::OnFellBackToGdi(int64_t nowMs)
{
    m_retryArmed = true;
    m_retryIntervalMs = kDxgiRetryFirstMs;
    m_retryAtMs = nowMs + m_retryIntervalMs;
}

bool CCaptureFallbackPolicy::ShouldRetryDxgi(int64_t nowMs) const
{
    return m_backend == kBackendGdi && m_retryArmed && nowMs >= m_retryAtMs;
}

void CCaptureFallbackPolicy::OnDxgiRetryFailed(int64_t nowMs)
{
    //间隔加倍，不超过上限；锁屏、UAC 提示可能持续很久，避免每隔几秒就建一次 D3D 设备
    m_retryIntervalMs *= 2;
    if (m_retryIntervalMs > kDxgiRetryMaxMs)
        m_retryIntervalMs = kDxgiRetryMaxMs;
    m_retryAtMs = nowMs + m_retryIntervalMs;
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
