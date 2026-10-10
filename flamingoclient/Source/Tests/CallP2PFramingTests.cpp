/**
 *  一对一通话 P2P 数据报消息格式与丢包处理的单元测试（Suite CPF，标签 [callp2p]），CallP2PFramingTests.cpp
 *
 *  覆盖 FlamingoMedia/Call/CallP2PFraming.h（docs/P2P通信方案.md 第 6 期）：
 *    - CPF1 组包与解析：音频、带关键帧标志的视频、没有负载的关键帧请求都能原样解析回来；头部 14 字节，序号与时间戳是
 *           网络字节序；长度不足、类型不认识时解析失败；
 *    - CPF2 音频序号：按序到达不补偿；中间丢了 2 个补 2 次；丢得太多只补 kCallMaxConcealFrames 次；迟到、重复的包丢弃；
 *           序号回绕时照常判断；Reset 之后从头开始；
 *    - CPF3 视频丢帧：第一帧是关键帧时直接解码，之后按序的非关键帧照常解码；序号断档后丢弃非关键帧并请求关键帧，
 *           间隔内不重复请求、过了间隔再请求；收到关键帧恢复解码；迟到的帧丢弃且不请求；会话开始收到的第一帧不是
 *           关键帧时同样丢弃并请求；
 *    - CPF4 定时重发请求：等关键帧期间每过一个间隔返回一次 true；不在等时、还没收到过任何帧时返回 false；
 *    - CPF5 发送端强制关键帧限频：第一次允许，间隔内拒绝，过了间隔允许；
 *    - CPF6 质量报告消息（2026-10-05，网络状况标识第 2 期）：组包后解析出类型、序号、时间戳与负载。
 *
 *  框架 Catch2 v3。
 *
 *  典型用法：FlamingoUnit.exe "[callp2p]"
 *
 *  balloonwj@qq.com   2026-10-04
 */
#include "catch_amalgamated.hpp"

#include <stdint.h>
#include <string>
#include <vector>

#include "../../FlamingoMedia/Call/CallP2PFraming.h"

using namespace call;

namespace {

//用例里的时间起点（单调时钟毫秒）与步长
const int64_t kT0 = 100000;
const int64_t kFrameMs = 33;

//CPF1 的序号与时间戳：每个字节都不同，便于核对字节序
const uint32_t kSeq = 0x01020304u;
const uint64_t kTs = 0x0A0B0C0D0E0F1011ULL;

//CPF2：中间丢的包数，与超过补偿上限的丢包数
const int kSmallGap = 2;
const int kBigGap = 10;

} // namespace

TEST_CASE("CPF1 组包与解析：音频、视频关键帧标志、关键帧请求", "[callp2p]")
{
    const std::string opus = "opus-bytes";
    std::vector<uint8_t> msg;
    BuildCallP2PMessage(call_p2p_kind_audio, 0, kSeq, kTs, reinterpret_cast<const uint8_t*>(opus.data()), opus.size(),
                        msg);
    REQUIRE(msg.size() == kCallP2PHeaderBytes + opus.size());
    CHECK(msg[0] == call_p2p_kind_audio);
    CHECK(msg[2] == 0x01);
    CHECK(msg[5] == 0x04);
    CHECK(msg[6] == 0x0A);
    CHECK(msg[13] == 0x11);
    CallP2PMessage m;
    REQUIRE(ParseCallP2PMessage(msg.data(), msg.size(), m));
    CHECK(m.m_kind == call_p2p_kind_audio);
    CHECK(m.m_seq == kSeq);
    CHECK(m.m_tsMs == kTs);
    REQUIRE(m.m_payloadLen == opus.size());
    CHECK(std::string(reinterpret_cast<const char*>(m.m_payload), m.m_payloadLen) == opus);

    const std::string h264 = "h264-keyframe";
    BuildCallP2PMessage(call_p2p_kind_video, kCallP2PFlagKeyframe, 7, kTs, reinterpret_cast<const uint8_t*>(h264.data()),
                        h264.size(), msg);
    REQUIRE(ParseCallP2PMessage(msg.data(), msg.size(), m));
    CHECK(m.m_kind == call_p2p_kind_video);
    CHECK((m.m_flags & kCallP2PFlagKeyframe) != 0);
    CHECK(m.m_seq == 7);

    BuildCallP2PMessage(call_p2p_kind_keyframe_request, 0, 0, 0, NULL, 0, msg);
    REQUIRE(msg.size() == kCallP2PHeaderBytes);
    REQUIRE(ParseCallP2PMessage(msg.data(), msg.size(), m));
    CHECK(m.m_kind == call_p2p_kind_keyframe_request);
    CHECK(m.m_payload == NULL);
    CHECK(m.m_payloadLen == 0);

    //长度不足、类型不认识
    CHECK_FALSE(ParseCallP2PMessage(msg.data(), kCallP2PHeaderBytes - 1, m));
    msg[0] = 9;
    CHECK_FALSE(ParseCallP2PMessage(msg.data(), msg.size(), m));
    CHECK_FALSE(ParseCallP2PMessage(NULL, 0, m));
}

TEST_CASE("CPF2 音频序号：按序不补偿、丢包补偿有上限、迟到与重复丢弃、回绕", "[callp2p]")
{
    CallAudioSeqTracker t;
    int lost = -1;
    CHECK(t.OnPacket(100, lost));
    CHECK(lost == 0);
    CHECK(t.OnPacket(101, lost));
    CHECK(lost == 0);

    //丢了 2 个：补 2 次
    CHECK(t.OnPacket(101 + kSmallGap + 1, lost));
    CHECK(lost == kSmallGap);

    //丢得太多：只补上限次
    const uint32_t afterBig = 101 + kSmallGap + 1 + kBigGap + 1;
    CHECK(t.OnPacket(afterBig, lost));
    CHECK(lost == kCallMaxConcealFrames);

    //迟到、重复：丢弃
    CHECK_FALSE(t.OnPacket(afterBig - 1, lost));
    CHECK(lost == 0);
    CHECK_FALSE(t.OnPacket(afterBig, lost));
    CHECK(t.OnPacket(afterBig + 1, lost));
    CHECK(lost == 0);

    //序号回绕：0xFFFFFFFF 之后是 0
    CallAudioSeqTracker w;
    CHECK(w.OnPacket(0xFFFFFFFFu, lost));
    CHECK(w.OnPacket(0, lost));
    CHECK(lost == 0);
    CHECK_FALSE(w.OnPacket(0xFFFFFFFFu, lost));

    //Reset 之后从头开始：第一个包不算丢
    t.Reset();
    CHECK(t.OnPacket(5, lost));
    CHECK(lost == 0);
}

TEST_CASE("CPF3 视频丢帧：断档后等关键帧并请求，间隔内不重复请求", "[callp2p]")
{
    CallVideoLossGuard g;
    bool request = false;
    int64_t now = kT0;
    CHECK(g.WaitingKeyframe());
    CHECK(g.OnFrame(0, true, now, request) == CallVideoLossGuard::video_decode);
    CHECK_FALSE(request);
    CHECK_FALSE(g.WaitingKeyframe());
    CHECK(g.OnFrame(1, false, now += kFrameMs, request) == CallVideoLossGuard::video_decode);
    CHECK(g.OnFrame(2, false, now += kFrameMs, request) == CallVideoLossGuard::video_decode);

    //帧 3 丢了：帧 4 丢弃并请求关键帧
    CHECK(g.OnFrame(4, false, now += kFrameMs, request) == CallVideoLossGuard::video_drop);
    CHECK(request);
    CHECK(g.WaitingKeyframe());
    //间隔内：继续丢弃，不重复请求
    CHECK(g.OnFrame(5, false, now += kFrameMs, request) == CallVideoLossGuard::video_drop);
    CHECK_FALSE(request);
    //过了间隔：再请求一次
    now += kCallKeyframeRequestIntervalMs;
    CHECK(g.OnFrame(6, false, now, request) == CallVideoLossGuard::video_drop);
    CHECK(request);
    //关键帧到达：恢复解码
    CHECK(g.OnFrame(7, true, now += kFrameMs, request) == CallVideoLossGuard::video_decode);
    CHECK_FALSE(request);
    CHECK_FALSE(g.WaitingKeyframe());
    CHECK(g.OnFrame(8, false, now += kFrameMs, request) == CallVideoLossGuard::video_decode);
    CHECK(g.DroppedFrames() == 3);

    //迟到的帧：丢弃，不请求，也不进入等关键帧
    CHECK(g.OnFrame(6, false, now += kFrameMs, request) == CallVideoLossGuard::video_drop);
    CHECK_FALSE(request);
    CHECK_FALSE(g.WaitingKeyframe());
    CHECK(g.OnFrame(9, false, now += kFrameMs, request) == CallVideoLossGuard::video_decode);

    //会话开始收到的第一帧不是关键帧（关键帧丢了）：丢弃并请求
    CallVideoLossGuard fresh;
    CHECK(fresh.OnFrame(1, false, kT0, request) == CallVideoLossGuard::video_drop);
    CHECK(request);
    CHECK(fresh.OnFrame(2, true, kT0 + kFrameMs, request) == CallVideoLossGuard::video_decode);
}

TEST_CASE("CPF4 等关键帧期间按间隔重发请求", "[callp2p]")
{
    CallVideoLossGuard g;
    //还没收到过任何帧：不请求（对方可能还没开摄像头）
    CHECK_FALSE(g.PollRequest(kT0));

    bool request = false;
    CHECK(g.OnFrame(1, false, kT0, request) == CallVideoLossGuard::video_drop);
    CHECK(request);
    CHECK_FALSE(g.PollRequest(kT0 + kCallKeyframeRequestIntervalMs - 1));
    CHECK(g.PollRequest(kT0 + kCallKeyframeRequestIntervalMs));
    CHECK_FALSE(g.PollRequest(kT0 + kCallKeyframeRequestIntervalMs + 1));
    CHECK(g.PollRequest(kT0 + 2 * kCallKeyframeRequestIntervalMs));

    //收到关键帧之后不再请求
    CHECK(g.OnFrame(2, true, kT0 + 3 * kCallKeyframeRequestIntervalMs, request) == CallVideoLossGuard::video_decode);
    CHECK_FALSE(g.PollRequest(kT0 + 10 * kCallKeyframeRequestIntervalMs));
}

TEST_CASE("CPF5 发送端强制关键帧限频", "[callp2p]")
{
    CallKeyframeForceLimiter l;
    CHECK(l.Allow(kT0));
    CHECK_FALSE(l.Allow(kT0 + kCallKeyframeForceMinIntervalMs - 1));
    CHECK(l.Allow(kT0 + kCallKeyframeForceMinIntervalMs));
    l.Reset();
    CHECK(l.Allow(kT0));
}

TEST_CASE("CPF6 质量报告消息：组包后解析出类型、序号、时间戳与负载（网络状况标识第 2 期）", "[callp2p]")
{
    const std::string report(24, '\x5A');
    std::vector<uint8_t> msg;
    BuildCallP2PMessage(call_p2p_kind_stats, 0, kSeq, kTs, reinterpret_cast<const uint8_t*>(report.data()),
                        report.size(), msg);
    REQUIRE(msg.size() == kCallP2PHeaderBytes + report.size());
    CallP2PMessage m;
    REQUIRE(ParseCallP2PMessage(msg.data(), msg.size(), m));
    CHECK(m.m_kind == call_p2p_kind_stats);
    CHECK(m.m_seq == kSeq);
    CHECK(m.m_tsMs == kTs);
    REQUIRE(m.m_payloadLen == report.size());
    CHECK(std::string(reinterpret_cast<const char*>(m.m_payload), m.m_payloadLen) == report);
}
