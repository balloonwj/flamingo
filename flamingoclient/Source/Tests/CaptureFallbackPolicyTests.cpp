/**
 *  远程桌面抓屏降级状态机单元测试（Suite RDFB），CaptureFallbackPolicyTests.cpp
 *  覆盖：Ok/Timeout 继续、AccessLost 先重建、连续重建尝试达阈值仍未出帧则降级 GDI、
 *       Fatal 直接降级、GDI 后端无可降级、出 Ok 帧清零计数、Reset 清零。框架 Catch2 v3。
 *  2026-10-17（联调发现分辨率变化后远程桌面一直用 GDI，docs/远程桌面H264与共享画质提升方案.md 8.6）补充：
 *    - RDFB9  AcquireNextFrame 的返回值归类：除等待超时以外的失败都按 AccessLost 先重建；分辨率变化时实测的
 *             DXGI_ERROR_INVALID_CALL 在 DXGI 后端下得到「重建」而不是「降级」；
 *    - RDFB10 因故障降级到 GDI 之后重试 DXGI 的时间安排：3 秒后第一次，失败后间隔加倍、最长 60 秒；
 *             切回 DXGI 后取消；强制 GDI（没有调用 OnFellBackToGdi）时从不重试。
 *  balloonwj@qq.com   2026-06-11
 */
#include "catch_amalgamated.hpp"
#include "../../FlamingoMedia/RemoteDesktop/CaptureFallbackPolicy.h"

using namespace remotedesktop;

TEST_CASE("RDFB1 Ok → 继续，重建尝试计数归零", "[rdfb]")
{
    CCaptureFallbackPolicy p;
    p.Reset(kBackendDxgi);
    //先制造一些重建尝试计数
    p.NotifyRebuildAttempt();
    p.NotifyRebuildAttempt();
    CHECK(p.RebuildAttempts() == 2);

    CHECK(p.OnResult(kCaptureOk) == kActionContinue);
    CHECK(p.RebuildAttempts() == 0);
}

TEST_CASE("RDFB2 Timeout → 继续，不算失败", "[rdfb]")
{
    CCaptureFallbackPolicy p;
    p.Reset(kBackendDxgi);
    CHECK(p.OnResult(kCaptureTimeout) == kActionContinue);
    CHECK(p.RebuildAttempts() == 0);
}

TEST_CASE("RDFB3 DXGI + AccessLost 首次 → 重建 DXGI", "[rdfb]")
{
    CCaptureFallbackPolicy p;
    p.Reset(kBackendDxgi);
    CHECK(p.OnResult(kCaptureAccessLost) == kActionRebuildDxgi);
}

TEST_CASE("RDFB4 连续重建尝试达阈值仍未出帧 → 降级 GDI", "[rdfb]")
{
    CCaptureFallbackPolicy p;
    p.Reset(kBackendDxgi);
    //模拟：每次 AccessLost 触发重建、记一次重建尝试，但始终出不了 Ok 帧
    for (int i = 0; i < (int)CCaptureFallbackPolicy::kMaxRebuildAttempts; ++i)
    {
        CHECK(p.OnResult(kCaptureAccessLost) == kActionRebuildDxgi);
        p.NotifyRebuildAttempt();
    }
    //尝试次数已达阈值，再 AccessLost → 降级 GDI
    CHECK(p.RebuildAttempts() == (int)CCaptureFallbackPolicy::kMaxRebuildAttempts);
    CHECK(p.OnResult(kCaptureAccessLost) == kActionFallbackGdi);
}

TEST_CASE("RDFB5 DXGI + Fatal → 直接降级 GDI", "[rdfb]")
{
    CCaptureFallbackPolicy p;
    p.Reset(kBackendDxgi);
    CHECK(p.OnResult(kCaptureFatal) == kActionFallbackGdi);
}

TEST_CASE("RDFB6 GDI 后端 + AccessLost/Fatal → 继续（无可降级）", "[rdfb]")
{
    CCaptureFallbackPolicy p;
    p.Reset(kBackendGdi);
    CHECK(p.OnResult(kCaptureAccessLost) == kActionContinue);
    CHECK(p.OnResult(kCaptureFatal) == kActionContinue);
}

TEST_CASE("RDFB7 出 Ok 帧后计数归零，后续 AccessLost 仍重建（不直接降级）", "[rdfb]")
{
    CCaptureFallbackPolicy p;
    p.Reset(kBackendDxgi);

    //重建两次仍没出帧
    CHECK(p.OnResult(kCaptureAccessLost) == kActionRebuildDxgi);
    p.NotifyRebuildAttempt();
    CHECK(p.OnResult(kCaptureAccessLost) == kActionRebuildDxgi);
    p.NotifyRebuildAttempt();
    CHECK(p.RebuildAttempts() == 2);

    //终于出了一帧 Ok：算恢复，计数清零
    CHECK(p.OnResult(kCaptureOk) == kActionContinue);
    CHECK(p.RebuildAttempts() == 0);

    //此后再 AccessLost：仍是重建（而非直接降级）
    CHECK(p.OnResult(kCaptureAccessLost) == kActionRebuildDxgi);
}

TEST_CASE("RDFB8 Reset 清零状态", "[rdfb]")
{
    CCaptureFallbackPolicy p;
    p.Reset(kBackendDxgi);
    p.NotifyRebuildAttempt();
    p.NotifyRebuildAttempt();
    CHECK(p.RebuildAttempts() == 2);

    p.Reset(kBackendGdi);
    CHECK(p.Backend() == kBackendGdi);
    CHECK(p.RebuildAttempts() == 0);
}

TEST_CASE("RDFB9 AcquireNextFrame 的返回值归类：除超时以外的失败都先重建，分辨率变化时的 INVALID_CALL 不再直接降级", "[rdfb]")
{
    CHECK(CCaptureFallbackPolicy::ClassifyAcquireResult(S_OK) == kCaptureOk);
    CHECK(CCaptureFallbackPolicy::ClassifyAcquireResult(DXGI_ERROR_WAIT_TIMEOUT) == kCaptureTimeout);
    CHECK(CCaptureFallbackPolicy::ClassifyAcquireResult(DXGI_ERROR_ACCESS_LOST) == kCaptureAccessLost);
    CHECK(CCaptureFallbackPolicy::ClassifyAcquireResult(DXGI_ERROR_INVALID_CALL) == kCaptureAccessLost);
    CHECK(CCaptureFallbackPolicy::ClassifyAcquireResult(E_FAIL) == kCaptureAccessLost);

    //DXGI 后端遇到分辨率变化时的 INVALID_CALL：重建 DXGI，而不是直接降级 GDI
    CCaptureFallbackPolicy p;
    p.Reset(kBackendDxgi);
    CHECK(p.OnResult(CCaptureFallbackPolicy::ClassifyAcquireResult(DXGI_ERROR_INVALID_CALL)) == kActionRebuildDxgi);
}

TEST_CASE("RDFB10 降级到 GDI 后重试 DXGI：3 秒后第一次，失败后间隔加倍、最长 60 秒，切回 DXGI 后取消", "[rdfb]")
{
    const int64_t t0 = 100000;
    const int64_t first = CCaptureFallbackPolicy::kDxgiRetryFirstMs;
    const int64_t maxGap = CCaptureFallbackPolicy::kDxgiRetryMaxMs;
    //「一直失败」模拟的次数，足以让间隔加倍到上限
    const int kManyFailures = 10;

    //强制 GDI（只 Reset、没有 OnFellBackToGdi）：从不重试
    CCaptureFallbackPolicy forced;
    forced.Reset(kBackendGdi);
    CHECK_FALSE(forced.ShouldRetryDxgi(t0 + kManyFailures * maxGap));

    //因故障降级：kDxgiRetryFirstMs 之后第一次
    CCaptureFallbackPolicy p;
    p.Reset(kBackendGdi);
    p.OnFellBackToGdi(t0);
    CHECK_FALSE(p.ShouldRetryDxgi(t0 + first - 1));
    CHECK(p.ShouldRetryDxgi(t0 + first));

    //失败一次：间隔加倍
    int64_t now = t0 + first;
    p.OnDxgiRetryFailed(now);
    CHECK_FALSE(p.ShouldRetryDxgi(now + 2 * first - 1));
    CHECK(p.ShouldRetryDxgi(now + 2 * first));

    //一直失败：间隔不超过 kDxgiRetryMaxMs
    for (int i = 0; i < kManyFailures; ++i)
    {
        now += maxGap;
        p.OnDxgiRetryFailed(now);
    }
    CHECK_FALSE(p.ShouldRetryDxgi(now + maxGap - 1));
    CHECK(p.ShouldRetryDxgi(now + maxGap));

    //重试成功、切回 DXGI：取消安排
    p.Reset(kBackendDxgi);
    CHECK_FALSE(p.ShouldRetryDxgi(now + kManyFailures * maxGap));

    //之后再次因故障降级：间隔从 kDxgiRetryFirstMs 重新开始
    now += kManyFailures * maxGap;
    p.Reset(kBackendGdi);
    p.OnFellBackToGdi(now);
    CHECK_FALSE(p.ShouldRetryDxgi(now + first - 1));
    CHECK(p.ShouldRetryDxgi(now + first));
}
