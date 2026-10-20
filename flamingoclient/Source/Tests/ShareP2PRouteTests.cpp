/**
 *  单聊屏幕共享走 P2P 时发起人一侧接管的服务端职责的单元测试（Suite SHP2P，标签 [sharep2p]），ShareP2PRouteTests.cpp
 *
 *  覆盖 FlamingoMedia/Share/ShareP2PRoute.h（docs/P2P通信方案.md 第 6 期）：
 *    - SHP2P1 视频丢帧规则：积压不超过上限时照发；超过上限进入等关键帧，非关键帧丢弃并请求关键帧，间隔内不重复请求、
 *             过了间隔再请求；关键帧一律照发并结束等关键帧；结束后积压回落时照常发送；Reset 回到初始状态；
 *    - SHP2P2 改写发送者座位号：只改第 2 个字节，其余不变；不足一个帧头、座位号越界时不改并返回 false；
 *    - SHP2P3 积压上限为 4 MiB，与服务端 screenshareviewerbufferbytes 的默认值相同（2026-10-15 一起由 1 MiB 调大）。
 *
 *  框架 Catch2 v3。
 *
 *  典型用法：FlamingoUnit.exe "[sharep2p]"
 *
 *  balloonwj@qq.com   2026-10-04
 */
#include "catch_amalgamated.hpp"

#include <stdint.h>
#include <string>

#include "../../FlamingoMedia/Share/ShareP2PRoute.h"
#include "../../FlamingoMedia/Share/ShareFraming.h"

using namespace screenshare;

namespace {

//用例里的时间起点（单调时钟毫秒）与帧间隔
const int64_t kT0 = 100000;
const int64_t kFrameMs = 66;

//座位号：观看者 1，越界 256
const int kViewerSeat = 1;
const int kBadSeat = 256;

} // namespace

TEST_CASE("SHP2P1 发给 P2P 观看者的视频：积压超过上限时只发关键帧并限频请求关键帧", "[sharep2p]")
{
    ShareP2PVideoGate gate;
    bool requestKey = false;
    int64_t now = kT0;
    CHECK(gate.OnVideoFrame(false, 0, now, requestKey));
    CHECK_FALSE(requestKey);
    CHECK(gate.OnVideoFrame(false, kShareP2PViewerBacklogBytes, now += kFrameMs, requestKey));
    CHECK_FALSE(gate.WaitingKeyframe());

    //积压超过上限：丢弃并请求关键帧
    CHECK_FALSE(gate.OnVideoFrame(false, kShareP2PViewerBacklogBytes + 1, now += kFrameMs, requestKey));
    CHECK(requestKey);
    CHECK(gate.WaitingKeyframe());
    //积压回落也继续等关键帧；间隔内不重复请求
    CHECK_FALSE(gate.OnVideoFrame(false, 0, now += kFrameMs, requestKey));
    CHECK_FALSE(requestKey);
    //过了间隔：再请求一次
    now += kShareP2PKeyRequestIntervalMs;
    CHECK_FALSE(gate.OnVideoFrame(false, 0, now, requestKey));
    CHECK(requestKey);
    CHECK(gate.DroppedFrames() == 3);

    //关键帧一律照发，结束等关键帧；之后积压不大时照常发送
    CHECK(gate.OnVideoFrame(true, kShareP2PViewerBacklogBytes * 4, now += kFrameMs, requestKey));
    CHECK_FALSE(requestKey);
    CHECK_FALSE(gate.WaitingKeyframe());
    CHECK(gate.OnVideoFrame(false, 0, now += kFrameMs, requestKey));

    gate.Reset();
    CHECK(gate.DroppedFrames() == 0);
    CHECK_FALSE(gate.WaitingKeyframe());
}

TEST_CASE("SHP2P3 P2P 观看者的积压上限与服务端默认值同为 4 MiB", "[sharep2p]")
{
    //2026-10-15 与 imgserver 的 screenshareviewerbufferbytes 默认值一起由 1 MiB 调到 4 MiB（用户定的 Q3）
    const uint64_t kFourMiB = 4ULL * 1024 * 1024;
    CHECK(kShareP2PViewerBacklogBytes == kFourMiB);
}

TEST_CASE("SHP2P2 改写发送者座位号：只改第 2 个字节", "[sharep2p]")
{
    std::string body(kShareFrameHeaderSize + 3, 'x');
    body[0] = 5;
    body[1] = 9;
    const std::string before = body;
    REQUIRE(ShareRewriteSenderSeat(body, kViewerSeat));
    CHECK(body[1] == (char)kViewerSeat);
    CHECK(body[0] == before[0]);
    CHECK(body.substr(2) == before.substr(2));

    std::string shortBody(kShareFrameHeaderSize - 1, 'x');
    CHECK_FALSE(ShareRewriteSenderSeat(shortBody, kViewerSeat));
    CHECK_FALSE(ShareRewriteSenderSeat(body, kBadSeat));
    CHECK_FALSE(ShareRewriteSenderSeat(body, -1));
}
