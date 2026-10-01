/**
 *  ReadReportDedupeTests.cpp
 *
 *  会话已读位点上报去重 balloonmain::ReadReportDedupe 的测试（标签 [readreport]），问题背景见 UI/BalloonMain/ReadReportDedupe.h。
 *
 *  覆盖：
 *    RR1 同一会话、同一位点连续两次（单击会话列表打开时，点击处理一次、窗口激活一次）：
 *        第一次上报，第二次不上报
 *    RR2 位点往前推进（窗口开着时又收到新消息）：上报；之后同一个新位点再来时不上报
 *    RR3 比已上报的位点小（乱序到达、或会话列表记录落后）：不上报
 *    RR4 位点小于等于 0（会话还没有已知消息）：不上报、不留记录；之后来了有效位点照常上报
 *    RR5 不同会话互不影响，包括单聊与群聊 cid 数值相同的情况
 *    RR6 Reset 之后（重新登录、断线重连成功、登出）：同一位点重新上报
 *
 *  balloonwj@qq.com   2026-10-01
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include "../UI/BalloonMain/ReadReportDedupe.h"

using namespace balloonmain;

namespace {

//会话类型：单聊与群聊
const int kCtypeSingle = 1;
const int kCtypeGroup = 2;
//测试用的会话 id 与已读位点
const int64_t kCid = 5308;
const int64_t kOtherCid = 268435464;
const int64_t kReadMsgid = 1147;
const int64_t kNewerMsgid = 1150;
const int64_t kOlderMsgid = 1100;

} // namespace

TEST_CASE("RR1 同一会话同一位点连续两次：第一次上报、第二次不上报", "[readreport]")
{
    ReadReportDedupe dedupe;
    CHECK(dedupe.ShouldReport(kCtypeGroup, kOtherCid, kReadMsgid));
    CHECK_FALSE(dedupe.ShouldReport(kCtypeGroup, kOtherCid, kReadMsgid));
}

TEST_CASE("RR2 位点往前推进时上报，之后同一新位点不再上报", "[readreport]")
{
    ReadReportDedupe dedupe;
    CHECK(dedupe.ShouldReport(kCtypeGroup, kOtherCid, kReadMsgid));
    CHECK(dedupe.ShouldReport(kCtypeGroup, kOtherCid, kNewerMsgid));
    CHECK_FALSE(dedupe.ShouldReport(kCtypeGroup, kOtherCid, kNewerMsgid));
}

TEST_CASE("RR3 比已上报的位点小时不上报", "[readreport]")
{
    ReadReportDedupe dedupe;
    CHECK(dedupe.ShouldReport(kCtypeSingle, kCid, kReadMsgid));
    CHECK_FALSE(dedupe.ShouldReport(kCtypeSingle, kCid, kOlderMsgid));
    //较小的位点也不会把记录拉低：之后原来的位点仍不重复上报
    CHECK_FALSE(dedupe.ShouldReport(kCtypeSingle, kCid, kReadMsgid));
}

TEST_CASE("RR4 位点小于等于 0 时不上报、不留记录", "[readreport]")
{
    ReadReportDedupe dedupe;
    CHECK_FALSE(dedupe.ShouldReport(kCtypeSingle, kCid, 0));
    CHECK_FALSE(dedupe.ShouldReport(kCtypeSingle, kCid, -1));
    CHECK(dedupe.ShouldReport(kCtypeSingle, kCid, kReadMsgid));
}

TEST_CASE("RR5 不同会话互不影响，单聊与群聊 cid 数值相同也分开记录", "[readreport]")
{
    ReadReportDedupe dedupe;
    CHECK(dedupe.ShouldReport(kCtypeSingle, kCid, kReadMsgid));
    CHECK(dedupe.ShouldReport(kCtypeGroup, kCid, kReadMsgid));
    CHECK(dedupe.ShouldReport(kCtypeGroup, kOtherCid, kReadMsgid));
    CHECK_FALSE(dedupe.ShouldReport(kCtypeSingle, kCid, kReadMsgid));
    CHECK_FALSE(dedupe.ShouldReport(kCtypeGroup, kCid, kReadMsgid));
    CHECK_FALSE(dedupe.ShouldReport(kCtypeGroup, kOtherCid, kReadMsgid));
}

TEST_CASE("RR6 Reset 之后同一位点重新上报", "[readreport]")
{
    ReadReportDedupe dedupe;
    CHECK(dedupe.ShouldReport(kCtypeGroup, kOtherCid, kReadMsgid));
    CHECK(dedupe.ShouldReport(kCtypeSingle, kCid, kReadMsgid));
    dedupe.Reset();
    CHECK(dedupe.ShouldReport(kCtypeGroup, kOtherCid, kReadMsgid));
    CHECK(dedupe.ShouldReport(kCtypeSingle, kCid, kReadMsgid));
}
