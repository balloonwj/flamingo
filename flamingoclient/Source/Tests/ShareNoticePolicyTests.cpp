/**
 *  文件共享弹窗通知在主程序一侧的判定（Suite SNPOL / SXT，标签 [sharenoticepolicy]），ShareNoticePolicyTests.cpp
 *  覆盖 docs/文件共享方案.md 第 14.5 节：
 *    - SNPOL1 勿扰时段 22:00 - 08:00：开着时段内丢弃（含边界 22 点，不含 8 点），关着时不管时间；锁屏推迟、锁屏判定为
 *             丢弃时丢弃、其余照常；勿扰优先于锁屏；锁屏策略里「文件共享通知」这一类推迟；
 *    - SXT1 退出与登出确认框里的提示：没有传输时为空，有传输时写明个数，退出与登出两种说法。
 *  纯函数，框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-07
 */
#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include "../Feature/FileShare/ShareNoticePolicy.h"
#include "../Feature/Lock/LockSuppressPolicy.h"
#include "../../FlamingoFileShare/FileShareApi.h"

using namespace balloonfileshare;

TEST_CASE("SNPOL1 do-not-disturb and lock decide the file share notices", "[sharenoticepolicy]")
{
    //时段：22 点起到次日 8 点前
    CHECK(IsInDoNotDisturbPeriod(22));
    CHECK(IsInDoNotDisturbPeriod(23));
    CHECK(IsInDoNotDisturbPeriod(0));
    CHECK(IsInDoNotDisturbPeriod(7));
    CHECK_FALSE(IsInDoNotDisturbPeriod(8));
    CHECK_FALSE(IsInDoNotDisturbPeriod(12));
    CHECK_FALSE(IsInDoNotDisturbPeriod(21));

    const int normal = balloonlock::kLockActNormal;
    const int defer = balloonlock::kLockActDefer;
    const int drop = balloonlock::kLockActDrop;
    //勿扰开着且在时段内：丢弃（锁不锁屏都一样）
    CHECK(DecideShareNoticePolicy(true, 23, normal) == fileshare::fileshare_notice_drop);
    CHECK(DecideShareNoticePolicy(true, 3, defer) == fileshare::fileshare_notice_drop);
    //勿扰开着但不在时段内、勿扰关着：按锁屏
    CHECK(DecideShareNoticePolicy(true, 9, normal) == fileshare::fileshare_notice_show);
    CHECK(DecideShareNoticePolicy(false, 23, normal) == fileshare::fileshare_notice_show);
    CHECK(DecideShareNoticePolicy(false, 23, defer) == fileshare::fileshare_notice_defer);
    CHECK(DecideShareNoticePolicy(false, 10, drop) == fileshare::fileshare_notice_drop);

    //锁屏策略：文件共享通知锁屏时推迟，未锁屏时照常
    CHECK(balloonlock::DecideDuringLock(balloonlock::kLockEvtFileShareNotice, true) == balloonlock::kLockActDefer);
    CHECK(balloonlock::DecideDuringLock(balloonlock::kLockEvtFileShareNotice, false) == balloonlock::kLockActNormal);
}

TEST_CASE("SXT1 exit and logout warnings for file share transfers", "[sharenoticepolicy]")
{
    CHECK(FileShareTransferExitWarning(0, false).empty());
    CHECK(FileShareTransferExitWarning(0, true).empty());
    CHECK(FileShareTransferExitWarning(3, false)
          == L"还有 3 个文件共享传输没有完成，退出会中断。下载下次可以接着传，上传需要重新传。");
    CHECK(FileShareTransferExitWarning(1, true)
          == L"还有 1 个文件共享传输没有完成，登出会中断。下载下次可以接着传，上传需要重新传。");
}
