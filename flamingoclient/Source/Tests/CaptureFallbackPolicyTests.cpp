/**
 *  远程桌面抓屏降级状态机单元测试（Suite RDFB），CaptureFallbackPolicyTests.cpp
 *  覆盖：Ok/Timeout 继续、AccessLost 先重建、连续重建尝试达阈值仍未出帧则降级 GDI、
 *       Fatal 直接降级、GDI 后端无可降级、出 Ok 帧清零计数、Reset 清零。框架 Catch2 v3。
 *  balloonwj@qq.com   2026-06-11
 */
#include "catch_amalgamated.hpp"
#include "../RemoteDesktop/CaptureFallbackPolicy.h"

using namespace remotedesktop;

TEST_CASE("RDFB1 Ok → 继续，重建尝试计数归零")
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

TEST_CASE("RDFB2 Timeout → 继续，不算失败")
{
    CCaptureFallbackPolicy p;
    p.Reset(kBackendDxgi);
    CHECK(p.OnResult(kCaptureTimeout) == kActionContinue);
    CHECK(p.RebuildAttempts() == 0);
}

TEST_CASE("RDFB3 DXGI + AccessLost 首次 → 重建 DXGI")
{
    CCaptureFallbackPolicy p;
    p.Reset(kBackendDxgi);
    CHECK(p.OnResult(kCaptureAccessLost) == kActionRebuildDxgi);
}

TEST_CASE("RDFB4 连续重建尝试达阈值仍未出帧 → 降级 GDI")
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

TEST_CASE("RDFB5 DXGI + Fatal → 直接降级 GDI")
{
    CCaptureFallbackPolicy p;
    p.Reset(kBackendDxgi);
    CHECK(p.OnResult(kCaptureFatal) == kActionFallbackGdi);
}

TEST_CASE("RDFB6 GDI 后端 + AccessLost/Fatal → 继续（无可降级）")
{
    CCaptureFallbackPolicy p;
    p.Reset(kBackendGdi);
    CHECK(p.OnResult(kCaptureAccessLost) == kActionContinue);
    CHECK(p.OnResult(kCaptureFatal) == kActionContinue);
}

TEST_CASE("RDFB7 出 Ok 帧后计数归零，后续 AccessLost 仍重建（不直接降级）")
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

TEST_CASE("RDFB8 Reset 清零状态")
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
