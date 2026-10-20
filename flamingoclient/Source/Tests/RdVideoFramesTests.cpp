/**
 *  远程桌面 H.264 引擎帧的编解码与协商用的纯逻辑（标签 [rdvideo]），RdVideoFramesTests.cpp
 *
 *  背景：docs/远程桌面H264与共享画质提升方案.md 4.3、6.2（2026-10-15）。远程桌面改用 H.264 后新增的引擎帧：
 *  被控端 → 控制端的 4 号视频帧、5 号指针帧、6 号能力帧；控制端 → 被控端带能力位的 3 号关键帧请求与 4 号回执。
 *    - RDV1  4 号视频帧往返：源尺寸、编码尺寸、关键帧标志、发送时刻、H.264 负载逐字节一致；长度不足、尺寸为 0 或超出上限
 *            时解析失败（覆盖控制端在多分片引擎帧中途连上、收到缺头的帧）；
 *    - RDV2  5 号指针帧、6 号能力帧、控制端 → 被控端的 4 号回执往返；负载不足时解析失败；
 *    - RDV3  带能力位的 3 号帧：解析出能力与能解的最大长边；只有 1 字节时视为「不能解 H.264」；有能力位而没有最大长边时按 1920；
 *    - RDV9  回执延迟：发送时刻回绕（32 位毫秒溢出）时差值仍正确；回送的时刻早于本会话第一帧（上一会话的残留）时忽略；
 *    - RDV10 被控端定编码长边：取远程桌面参数的上限与控制端能解的最大长边中较小的一个；
 *    - RDV11 控制端回执回送的时刻 = 最近收到的视频帧的发送时刻 + 该帧在控制端停留的时长（实施时修正方案 4.5 的「原样回送」：
 *            画面静止时被控端每 4 秒才发一帧，原样回送会把停留时长算成延迟、超过拥塞门限）。
 *  框架 Catch2 v3。
 *
 *  balloonwj@qq.com   2026-10-15
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <stdint.h>
#include <string.h>
#include <vector>

#include "../../FlamingoMedia/RemoteDesktop/RdVideoFrames.h"
#include "../../FlamingoMedia/RemoteDesktop/RdEngineDef.h"
#include "../../FlamingoMedia/Share/ShareBitrateController.h"   // kShareFeedbackCongestedDelayMs：回执延迟的拥塞门限

using namespace remotedesktop;

namespace {

//一段假的 H.264 负载（内容不解析，只比较字节）
std::vector<unsigned char> rdvPayload()
{
    std::vector<unsigned char> p;
    const unsigned char bytes[] = { 0, 0, 0, 1, 0x67, 0x42, 0x00, 0x33, 0xAB, 0xCD, 0, 0, 1, 0x65, 0x88, 0x99 };
    p.assign(bytes, bytes + sizeof(bytes));
    return p;
}

RdVideoFrameHeader rdvHeader()
{
    RdVideoFrameHeader h;
    h.m_srcW = 3840;
    h.m_srcH = 2160;
    h.m_encW = 2560;
    h.m_encH = 1440;
    h.m_keyframe = true;
    h.m_sendMs = 0xFFFFFFF0u;
    return h;
}

} // namespace

TEST_CASE("RDV1 4 号视频帧往返一致，长度不足、尺寸为 0 或超出上限时解析失败", "[rdvideo]")
{
    const std::vector<unsigned char> payload = rdvPayload();
    const RdVideoFrameHeader h = rdvHeader();
    std::vector<unsigned char> frame;
    RdVideoFrames::BuildVideo(h, payload.data(), payload.size(), frame);
    REQUIRE(frame.size() == kRdVideoHeaderLen + payload.size());
    CHECK(frame[0] == static_cast<unsigned char>(rd_frame_video));

    RdVideoFrameHeader got;
    const unsigned char* h264 = NULL;
    size_t h264Len = 0;
    REQUIRE(RdVideoFrames::ParseVideo(frame.data(), frame.size(), got, h264, h264Len));
    CHECK(got.m_srcW == h.m_srcW);
    CHECK(got.m_srcH == h.m_srcH);
    CHECK(got.m_encW == h.m_encW);
    CHECK(got.m_encH == h.m_encH);
    CHECK(got.m_keyframe);
    CHECK(got.m_sendMs == h.m_sendMs);
    REQUIRE(h264Len == payload.size());
    CHECK(memcmp(h264, payload.data(), payload.size()) == 0);

    //非关键帧
    RdVideoFrameHeader delta = h;
    delta.m_keyframe = false;
    RdVideoFrames::BuildVideo(delta, payload.data(), payload.size(), frame);
    REQUIRE(RdVideoFrames::ParseVideo(frame.data(), frame.size(), got, h264, h264Len));
    CHECK_FALSE(got.m_keyframe);

    //只有帧头、没有负载：不是一个有效的视频帧
    std::vector<unsigned char> headerOnly(frame.begin(), frame.begin() + kRdVideoHeaderLen);
    CHECK_FALSE(RdVideoFrames::ParseVideo(headerOnly.data(), headerOnly.size(), got, h264, h264Len));
    //截断的帧头
    CHECK_FALSE(RdVideoFrames::ParseVideo(frame.data(), kRdVideoHeaderLen - 1, got, h264, h264Len));
    //类型不对（例如缺头的帧，首字节是任意值）
    std::vector<unsigned char> wrongType = frame;
    wrongType[0] = static_cast<unsigned char>(rd_frame_next);
    CHECK_FALSE(RdVideoFrames::ParseVideo(wrongType.data(), wrongType.size(), got, h264, h264Len));
    //尺寸为 0、超出上限
    RdVideoFrameHeader zero = h;
    zero.m_encW = 0;
    RdVideoFrames::BuildVideo(zero, payload.data(), payload.size(), frame);
    CHECK_FALSE(RdVideoFrames::ParseVideo(frame.data(), frame.size(), got, h264, h264Len));
    RdVideoFrameHeader huge = h;
    huge.m_srcW = kRdVideoMaxDimension + 1;
    RdVideoFrames::BuildVideo(huge, payload.data(), payload.size(), frame);
    CHECK_FALSE(RdVideoFrames::ParseVideo(frame.data(), frame.size(), got, h264, h264Len));
    CHECK_FALSE(RdVideoFrames::ParseVideo(NULL, 0, got, h264, h264Len));
}

TEST_CASE("RDV2 5 号指针帧、6 号能力帧、4 号回执往返一致，负载不足时解析失败", "[rdvideo]")
{
    std::vector<unsigned char> frame;
    //指针帧：坐标可以为负（被控端主屏左上角之外的副屏）
    RdVideoFrames::BuildCursor(-120, 2159, 7, frame);
    REQUIRE(frame.size() == kRdCursorFrameLen);
    CHECK(frame[0] == static_cast<unsigned char>(rd_frame_cursor));
    int32_t x = 0;
    int32_t y = 0;
    uint8_t type = 0;
    REQUIRE(RdVideoFrames::ParseCursor(frame.data(), frame.size(), x, y, type));
    CHECK(x == -120);
    CHECK(y == 2159);
    CHECK(type == 7);
    CHECK_FALSE(RdVideoFrames::ParseCursor(frame.data(), frame.size() - 1, x, y, type));

    //能力帧
    RdVideoFrames::BuildCaps(kRdCapH264, frame);
    REQUIRE(frame.size() == kRdCapsFrameLen);
    CHECK(frame[0] == static_cast<unsigned char>(rd_frame_caps));
    uint8_t caps = 0;
    REQUIRE(RdVideoFrames::ParseCaps(frame.data(), frame.size(), caps));
    CHECK(caps == kRdCapH264);
    CHECK_FALSE(RdVideoFrames::ParseCaps(frame.data(), 1, caps));

    //回执：控制端原样回送最近收到的视频帧的发送时刻
    RdVideoFrames::BuildVideoAck(0x12345678u, frame);
    REQUIRE(frame.size() == kRdVideoAckLen);
    CHECK(frame[0] == static_cast<unsigned char>(rd_ctl_video_ack));
    uint32_t sendMs = 0;
    REQUIRE(RdVideoFrames::ParseVideoAck(frame.data(), frame.size(), sendMs));
    CHECK(sendMs == 0x12345678u);
    CHECK_FALSE(RdVideoFrames::ParseVideoAck(frame.data(), frame.size() - 1, sendMs));
}

TEST_CASE("RDV3 带能力位的 3 号帧：解析能力与最大长边，1 字节为不能解 H.264，缺最大长边按 1920", "[rdvideo]")
{
    std::vector<unsigned char> frame;
    RdVideoFrames::BuildKeyframeRequest(true, 2560, frame);
    REQUIRE(frame.size() == kRdKeyRequestFullLen);
    CHECK(frame[0] == static_cast<unsigned char>(rd_frame_request_keyframe));
    RdKeyframeRequestInfo info;
    RdVideoFrames::ParseKeyframeRequest(frame.data(), frame.size(), info);
    CHECK(info.m_hasCaps);
    CHECK(info.m_canDecodeH264);
    CHECK(info.m_maxLongSide == 2560);

    //不能解 H.264 的新控制端
    RdVideoFrames::BuildKeyframeRequest(false, 0, frame);
    RdVideoFrames::ParseKeyframeRequest(frame.data(), frame.size(), info);
    CHECK(info.m_hasCaps);
    CHECK_FALSE(info.m_canDecodeH264);

    //旧控制端只发 1 字节
    const unsigned char legacy[] = { static_cast<unsigned char>(rd_frame_request_keyframe) };
    RdVideoFrames::ParseKeyframeRequest(legacy, sizeof(legacy), info);
    CHECK_FALSE(info.m_hasCaps);
    CHECK_FALSE(info.m_canDecodeH264);

    //有能力位而没有最大长边：按 1920
    const unsigned char capsOnly[] = { static_cast<unsigned char>(rd_frame_request_keyframe), kRdCapH264 };
    RdVideoFrames::ParseKeyframeRequest(capsOnly, sizeof(capsOnly), info);
    CHECK(info.m_hasCaps);
    CHECK(info.m_canDecodeH264);
    CHECK(info.m_maxLongSide == kRdDefaultControllerMaxLongSide);
}

TEST_CASE("RDV9 回执延迟：32 位毫秒回绕时差值正确，上一会话残留的回执忽略", "[rdvideo]")
{
    int64_t delay = -1;
    //普通情况
    REQUIRE(RdAckDelayMs(10500u, 10200u, 10000u, delay));
    CHECK(delay == 300);
    //发送时刻在回绕之前、当前时刻在回绕之后
    REQUIRE(RdAckDelayMs(0x00000064u, 0xFFFFFFCEu, 0xFFFFFF00u, delay));
    CHECK(delay == 150);
    //回送的时刻早于本会话第一帧：上一会话的残留，忽略
    CHECK_FALSE(RdAckDelayMs(20000u, 9000u, 10000u, delay));
    //回送的时刻在当前时刻之后（不可能，防御）：忽略
    CHECK_FALSE(RdAckDelayMs(10000u, 10500u, 9000u, delay));
}

TEST_CASE("RDV11 回执回送「发送时刻 + 在控制端停留的时长」：画面静止、几秒才来一帧时延迟不被算大", "[rdvideo]")
{
    //两端的单调时钟起点不同：被控端在 1000 毫秒发出最后一帧，控制端在自己的 50000 毫秒收到；单程 40 毫秒
    const uint32_t kSendMs = 1000u;
    const int64_t kRecvAt = 50000;
    const int64_t kOneWayMs = 40;
    //画面静止：被控端每 4 秒才发一个兜底关键帧，控制端在收到之后 3500 毫秒照常回执
    const int64_t kHeldMs = 3500;
    const uint32_t senderNow = kSendMs + static_cast<uint32_t>(kOneWayMs + kHeldMs + kOneWayMs);
    int64_t delay = -1;

    //原样回送（方案 4.5 的原写法）：停留的时长被算进延迟，超过拥塞门限，静止画面下码率会被反复误降
    REQUIRE(RdAckDelayMs(senderNow, kSendMs, kSendMs, delay));
    CHECK(delay > screenshare::kShareFeedbackCongestedDelayMs);

    //加上停留时长再回送：被控端算出的只有往返时间
    const uint32_t echo = RdAckEchoMs(kSendMs, kRecvAt, kRecvAt + kHeldMs);
    CHECK(echo == kSendMs + static_cast<uint32_t>(kHeldMs));
    REQUIRE(RdAckDelayMs(senderNow, echo, kSendMs, delay));
    CHECK(delay == 2 * kOneWayMs);

    //刚收到就回执：停留 0，等于原样回送
    CHECK(RdAckEchoMs(kSendMs, kRecvAt, kRecvAt) == kSendMs);
    //发送时刻接近 32 位上限：加上停留时长后回绕，被控端照样算得对
    const uint32_t nearWrap = 0xFFFFFF00u;
    const uint32_t wrapped = RdAckEchoMs(nearWrap, kRecvAt, kRecvAt + kHeldMs);
    REQUIRE(RdAckDelayMs(nearWrap + static_cast<uint32_t>(kHeldMs + 2 * kOneWayMs), wrapped, nearWrap, delay));
    CHECK(delay == 2 * kOneWayMs);
    //当前时刻早于收到的时刻（不应出现，防御）：按停留 0
    CHECK(RdAckEchoMs(kSendMs, kRecvAt, kRecvAt - 10) == kSendMs);
}

TEST_CASE("RDV10 被控端的编码长边取参数上限与控制端能解的最大长边中较小的一个", "[rdvideo]")
{
    CHECK(RdChooseEncodeLongSide(2560, 2560) == 2560);
    CHECK(RdChooseEncodeLongSide(2560, 1920) == 1920);
    CHECK(RdChooseEncodeLongSide(2560, 4096) == 2560);
    //控制端没报（旧的带能力位格式）或报了非法值：按 1920
    CHECK(RdChooseEncodeLongSide(2560, 0) == kRdDefaultControllerMaxLongSide);
    CHECK(RdChooseEncodeLongSide(2560, -5) == kRdDefaultControllerMaxLongSide);
}
