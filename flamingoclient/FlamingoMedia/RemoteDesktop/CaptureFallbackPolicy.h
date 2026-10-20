/**
 *  远程桌面抓屏降级决策状态机，CaptureFallbackPolicy.h
 *
 *  把"抓帧出错后该继续 / 重建 DXGI / 降级 GDI"的决策抽成纯逻辑状态机，不依赖真实
 *  DXGI / GDI，便于确定性单元测试（见 Tests/CaptureFallbackPolicyTests.cpp）。真正的
 *  执行（销毁重建 duplication、换 GDI 后端）由 RdScreenCapturer 据本状态机的决定去做。
 *
 *  策略：单次 AccessLost 先尝试重建 DXGI（锁屏 / UAC / 分辨率变这类抖动多可救回）；
 *  以"自上次成功出帧（Ok）以来的重建尝试次数"为准——连续重建若干次仍出不了 Ok 帧
 *  （达阈值）才降级 GDI，**只有真正出一帧 Ok 才算恢复、计数清零**（避免"重建成功但帧
 *  立刻又 AccessLost"陷入无限重建）。Fatal 不可恢复直接降级 GDI；已在 GDI 后端则无处
 *  可退、一律继续。Timeout（无新帧）是正常态，不算失败、不降级。
 *
 *  2026-10-17 补两条（联调发现分辨率变化后远程桌面一直用 GDI）：
 *    · AcquireNextFrame 的返回值经 ClassifyAcquireResult 归类，除等待超时以外的失败都按 AccessLost（先重建）处理——
 *      分辨率变化时实测返回 DXGI_ERROR_INVALID_CALL，原先按 Fatal 直接降级，本次会话从此只用 GDI；
 *    · 因故障降级到 GDI 之后定期重试 DXGI（OnFellBackToGdi / ShouldRetryDxgi / OnDxgiRetryFailed）：第一次在
 *      kDxgiRetryFirstMs 之后，每失败一次间隔加倍，最长 kDxgiRetryMaxMs；成功由调用方 Reset(kBackendDxgi)。
 *      锁屏、UAC 提示期间重建一直失败，解锁后就能回到 DXGI。调试时强制用 GDI 的不重试。
 *
 *  balloonwj@qq.com   2026-06-11
 */
#pragma once

#include <stdint.h>
#include "IScreenCapturer.h"   // CaptureResult

namespace remotedesktop {

//当前抓取后端类型。
enum CaptureBackend
{
    kBackendNone = 0,   //无可用后端（两者都初始化失败）
    kBackendDxgi = 1,   //DXGI Desktop Duplication（主）
    kBackendGdi  = 2,   //GDI BitBlt（降级）
};

//据抓帧结果决定的恢复动作。
enum FallbackAction
{
    kActionContinue    = 0,   //继续用当前后端（正常出帧 / 无新帧 / 已是 GDI 无可降级）
    kActionRebuildDxgi = 1,   //销毁并重建 DXGI duplication
    kActionFallbackGdi = 2,   //降级到 GDI 后端
};

//抓屏降级决策状态机（纯逻辑）。
class CCaptureFallbackPolicy
{
public:
    //自上次成功出帧以来允许的最多重建尝试次数；再出错则降级 GDI。
    enum { kMaxRebuildAttempts = 3 };
    //因故障降级到 GDI 之后，第一次尝试重建 DXGI 的等待时间（毫秒）
    enum { kDxgiRetryFirstMs = 3000 };
    //重建 DXGI 的尝试每失败一次间隔加倍，最长这么久（毫秒）
    enum { kDxgiRetryMaxMs = 60000 };

    CCaptureFallbackPolicy();

    /**
     *  把 DXGI AcquireNextFrame 的返回值归类（2026-10-17）。
     *  @param hr AcquireNextFrame 的返回值
     *  @return 成功为 kCaptureOk，等待超时为 kCaptureTimeout，其余失败一律为 kCaptureAccessLost（先重建 DXGI，
     *          重建次数仍受 kMaxRebuildAttempts 限制，重建失败照常降级 GDI）
     */
    static CaptureResult ClassifyAcquireResult(HRESULT hr);

    //重置为指定后端（抓取器初始化成功 / 切换后端后调用）。同时清零重建尝试计数，并取消重试 DXGI 的安排。
    void Reset(CaptureBackend backend);

    /**
     *  因故障降级到 GDI 之后调用（在 Reset(kBackendGdi) 之后，2026-10-17）：安排在 kDxgiRetryFirstMs 之后第一次重试 DXGI。
     *  调试时强制用 GDI 的抓取器不调用它，因而不会重试。
     *  @param nowMs 当前时刻（毫秒，单调时钟）
     */
    void OnFellBackToGdi(int64_t nowMs);

    /**
     *  GDI 后端下是否到了重试 DXGI 的时刻。
     *  @param nowMs 当前时刻（毫秒，单调时钟）
     *  @return 因故障降级到 GDI、且到了安排的时刻时返回 true；DXGI 后端、强制 GDI、没到时刻时返回 false
     */
    bool ShouldRetryDxgi(int64_t nowMs) const;

    /**
     *  重试 DXGI 失败：间隔加倍（不超过 kDxgiRetryMaxMs），从 nowMs 起安排下一次。重试成功时调用方改调 Reset(kBackendDxgi)。
     *  @param nowMs 当前时刻（毫秒，单调时钟）
     */
    void OnDxgiRetryFailed(int64_t nowMs);

    /**
     *  据本次抓帧结果决定动作并更新内部状态（Ok 会清零重建尝试计数）。
     *  @param r 本次 CaptureFrame 的结果
     *  @return 见 FallbackAction
     */
    FallbackAction OnResult(CaptureResult r);

    //记一次 DXGI 重建尝试（无论成功失败都计入；执行重建后由调用方调一次）。
    void NotifyRebuildAttempt();

    //当前后端。
    CaptureBackend Backend() const;
    //自上次成功出帧以来的重建尝试次数。
    int RebuildAttempts() const;

private:
    CaptureBackend m_backend;          //当前后端类型
    int            m_rebuildAttempts;  //自上次 Ok 帧以来的重建尝试次数（出 Ok 帧清零）
    bool           m_retryArmed;       //是否安排了重试 DXGI（因故障降级到 GDI 时为 true，Reset 时清除）
    int64_t        m_retryAtMs;        //下一次重试 DXGI 的时刻（毫秒，单调时钟）
    int64_t        m_retryIntervalMs;  //当前的重试间隔（毫秒），每失败一次加倍
};

} // namespace remotedesktop
