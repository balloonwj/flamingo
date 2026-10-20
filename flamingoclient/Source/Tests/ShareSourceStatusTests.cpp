/**
 *  屏幕共享「共享内容状态」的计算（标签 [sharestate]），ShareSourceStatusTests.cpp
 *  背景见 FlamingoMedia/Share/ShareSourceStatus.h 与 docs/远程桌面控制权限与观看工具条方案.md 8.2 第 7 条、8.4。
 *  与用户确认的用例：
 *    - SST1 纯函数 ComputeShareSourceState 的真值表：已关闭优先；其次用户暂停或采集挂起为画面暂停；其次窗口最小化为已最小化；
 *      否则为正在共享；
 *    - SST2 窗口关闭之后换了共享内容，状态回到正在共享；暂停与继续不会让「已关闭」变回正在共享。
 *      2026-10-14 先把推流循环原来的写法原样搬进 ShareSourceStatus 跑本用例，确认失败（复现缺陷），再改成按条件统一计算。
 *  框架 Catch2 v3，纯逻辑。
 *  balloonwj@qq.com   2026-10-14
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include "../../FlamingoMedia/Share/ShareSourceStatus.h"
#include "../../FlamingoMedia/Share/ShareCaptureRetry.h"
#include "../net/Protocol.h"

using namespace screenshare;

namespace {

//真值表的一行：四个条件与期望的状态
struct SourceStateRow
{
    bool m_closed;      //共享的窗口已关闭
    bool m_paused;      //用户暂停
    bool m_suspended;   //采集挂起
    bool m_minimized;   //窗口已最小化
    int  m_expected;    //期望的 screenshare_source_state
};

} // namespace

TEST_CASE("SST1 source state truth table: closed, then paused or suspended, then minimized, else sharing", "[sharestate]")
{
    const SourceStateRow rows[] =
    {
        //已关闭：其余条件怎样都是已关闭
        { true,  false, false, false, screenshare_source_closed },
        { true,  true,  false, false, screenshare_source_closed },
        { true,  false, true,  false, screenshare_source_closed },
        { true,  false, false, true,  screenshare_source_closed },
        { true,  true,  true,  true,  screenshare_source_closed },
        //没有关闭：用户暂停或采集挂起时为画面暂停，最小化不影响
        { false, true,  false, false, screenshare_source_paused },
        { false, false, true,  false, screenshare_source_paused },
        { false, true,  true,  false, screenshare_source_paused },
        { false, true,  false, true,  screenshare_source_paused },
        { false, false, true,  true,  screenshare_source_paused },
        { false, true,  true,  true,  screenshare_source_paused },
        //没有关闭也没有暂停：最小化时为已最小化，否则正在共享
        { false, false, false, true,  screenshare_source_minimized },
        { false, false, false, false, screenshare_source_sharing },
    };
    for (size_t i = 0; i < sizeof(rows) / sizeof(rows[0]); ++i)
    {
        const SourceStateRow& row = rows[i];
        INFO("row " << i);
        CHECK(ComputeShareSourceState(row.m_closed, row.m_paused, row.m_suspended, row.m_minimized) == row.m_expected);
    }
}

TEST_CASE("SST2 switching content clears closed, and pausing or resuming does not", "[sharestate]")
{
    SECTION("window closed, then content switched: back to sharing")
    {
        ShareSourceStatus status;
        status.OnCaptureClosed();
        CHECK(status.State() == screenshare_source_closed);
        CHECK(status.IsClosed());
        status.OnSourceSwitched(true);
        CHECK(status.State() == screenshare_source_sharing);
        CHECK_FALSE(status.IsClosed());
    }

    SECTION("window closed, then paused and resumed: still closed until content is switched")
    {
        ShareSourceStatus status;
        status.OnCaptureClosed();
        status.OnPauseChanged(true);
        CHECK(status.State() == screenshare_source_closed);
        status.OnPauseChanged(false);
        CHECK(status.State() == screenshare_source_closed);
        CHECK(status.IsClosed());
        status.OnSourceSwitched(true);
        CHECK(status.State() == screenshare_source_sharing);
    }

    SECTION("paused by the user, window closed, content switched: still paused")
    {
        ShareSourceStatus status;
        status.OnPauseChanged(true);
        status.OnCaptureClosed();
        status.OnSourceSwitched(true);
        CHECK(status.State() == screenshare_source_paused);
        status.OnPauseChanged(false);
        CHECK(status.State() == screenshare_source_sharing);
    }

    SECTION("capture suspended after a failed retry, recovered after a later retry")
    {
        ShareSourceStatus status;
        status.OnRetryReport(share_retry_report_paused);
        CHECK(status.State() == screenshare_source_paused);
        status.OnRetryReport(share_retry_report_none);
        CHECK(status.State() == screenshare_source_paused);
        status.OnRetryReport(share_retry_report_resumed);
        CHECK(status.State() == screenshare_source_sharing);
    }
}
