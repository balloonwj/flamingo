/**
 *  屏幕共享房间中转帧的封装、分片与重组的单元测试（Suite SSFR，标签 [screenshare]），ShareFramingTests.cpp
 *
 *  覆盖 FlamingoMedia/Share/ShareFraming（docs/屏幕共享与录制方案.md 第 11.4 节 2c-1）：
 *    - SSFR1 帧头封装与拆解：11 字节布局与服务端 RoomRelayFrame 一致（大端时间戳），长度不足或轨道越界拒绝；
 *    - SSFR2 分片：视频帧按上限切片，首分片带首分片位与整帧长度前缀，尾分片带尾分片位，关键帧的每个分片都带关键帧位；
 *            不足一片的视频帧也带长度前缀；非视频轨整帧一片、不带前缀；
 *    - SSFR3 重组：分片按序还原出逐字节相同的整帧；不同发送者、不同轨道的分片交错到达各自还原；
 *    - SSFR4 异常丢弃：没有首分片的后续分片、累积中途又来新的首分片、时间戳不一致、中间缺片（长度不符）都整帧丢弃，
 *            丢弃之后下一帧照常还原；成员离开后其未完成分片清空；
 *    - SSFR5 识别服务端的关键帧请求，普通控制帧与别人发的控制帧不算；
 *    - SSFR6 视频帧载荷带真实宽高（[宽 2B][高 2B] + H.264），观看端据此裁掉解码补齐的部分；
 *    - SSFR7 共享内容状态控制帧的构造与识别。
 *
 *  典型用法：FlamingoUnit.exe "SSFR*"
 *
 *  balloonwj@qq.com   2026-10-02
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <string>
#include <vector>
#include "../../FlamingoMedia/Share/ShareFraming.h"
#include "../net/Protocol.h"

using namespace screenshare;

namespace {

//测试用的时间戳：八个字节各不相同，便于发现字节序错误
const uint64_t kSsfrTimestamp = 0x0102030405060708ULL;

//测试用的分片上限（小一些，便于构造多片）
const size_t kSsfrMaxPayload = 100;

//造一段内容随下标变化的数据
std::string ssfrData(size_t len, int salt)
{
    std::string s(len, '\0');
    for (size_t i = 0; i < len; ++i)
        s[i] = static_cast<char>((i * 7 + salt) & 0xFF);
    return s;
}

//把包体逐个喂给重组器，返回还原出的帧
std::vector<ShareMediaFrame> ssfrFeed(ShareFrameReassembler& r, const std::vector<std::string>& bodies)
{
    std::vector<ShareMediaFrame> frames;
    for (size_t i = 0; i < bodies.size(); ++i)
    {
        ShareMediaFrame f;
        if (r.push(bodies[i].data(), bodies[i].size(), f))
            frames.push_back(f);
    }
    return frames;
}

uint32_t ssfrReadBe32(const char* p)
{
    const unsigned char* b = reinterpret_cast<const unsigned char*>(p);
    return (uint32_t(b[0]) << 24) | (uint32_t(b[1]) << 16) | (uint32_t(b[2]) << 8) | uint32_t(b[3]);
}

} // namespace

TEST_CASE("SSFR1 帧头封装与拆解与服务端布局一致", "[screenshare]")
{
    ShareFrameHeader h;
    h.m_track = screenshare_track_camera;
    h.m_senderSeat = 3;
    h.m_flags = screenshare_flag_keyframe | screenshare_flag_first_frag;
    h.m_timestamp = kSsfrTimestamp;
    const std::string body = ShareFraming::encodeBody(h, "abc", 3);
    REQUIRE(body.size() == kShareFrameHeaderSize + 3);
    //逐字节核对布局：[轨道][座位][标志][时间戳 8 字节大端][载荷]
    CHECK(static_cast<unsigned char>(body[0]) == screenshare_track_camera);
    CHECK(static_cast<unsigned char>(body[1]) == 3);
    CHECK(static_cast<unsigned char>(body[2]) == (screenshare_flag_keyframe | screenshare_flag_first_frag));
    CHECK(static_cast<unsigned char>(body[3]) == 0x01);
    CHECK(static_cast<unsigned char>(body[10]) == 0x08);
    CHECK(body.substr(kShareFrameHeaderSize) == "abc");

    ShareFrameHeader d;
    const char* payload = NULL;
    size_t payloadLen = 0;
    REQUIRE(ShareFraming::decodeBody(body.data(), body.size(), d, payload, payloadLen));
    CHECK(d.m_track == screenshare_track_camera);
    CHECK(d.m_senderSeat == 3);
    CHECK(d.m_flags == h.m_flags);
    CHECK(d.m_timestamp == kSsfrTimestamp);
    REQUIRE(payloadLen == 3);
    CHECK(std::string(payload, payloadLen) == "abc");

    CHECK_FALSE(ShareFraming::decodeBody(body.data(), kShareFrameHeaderSize - 1, d, payload, payloadLen));
    std::string badTrack = body;
    badTrack[0] = static_cast<char>(screenshare_track_control + 1);
    CHECK_FALSE(ShareFraming::decodeBody(badTrack.data(), badTrack.size(), d, payload, payloadLen));
    badTrack[0] = 0;
    CHECK_FALSE(ShareFraming::decodeBody(badTrack.data(), badTrack.size(), d, payload, payloadLen));

    CHECK(ShareFraming::isVideoTrack(screenshare_track_screen));
    CHECK(ShareFraming::isVideoTrack(screenshare_track_camera));
    CHECK_FALSE(ShareFraming::isVideoTrack(screenshare_track_mic));
    CHECK_FALSE(ShareFraming::isVideoTrack(screenshare_track_control));
}

TEST_CASE("SSFR2 分片：首尾分片位、关键帧位与整帧长度前缀", "[screenshare]")
{
    //250 字节的关键帧，每片载荷上限 100：首片 4 字节前缀 + 96 字节数据，之后 100、54
    const std::string frame = ssfrData(250, 1);
    std::vector<std::string> bodies;
    ShareFraming::fragmentFrame(screenshare_track_screen, 0, true, kSsfrTimestamp, frame.data(), frame.size(),
                                kSsfrMaxPayload, bodies);
    REQUIRE(bodies.size() == 3);
    std::string rebuilt;
    for (size_t i = 0; i < bodies.size(); ++i)
    {
        ShareFrameHeader h;
        const char* payload = NULL;
        size_t payloadLen = 0;
        REQUIRE(ShareFraming::decodeBody(bodies[i].data(), bodies[i].size(), h, payload, payloadLen));
        CHECK(payloadLen <= kSsfrMaxPayload);
        CHECK(h.m_track == screenshare_track_screen);
        CHECK(h.m_timestamp == kSsfrTimestamp);
        CHECK((h.m_flags & screenshare_flag_keyframe) != 0);
        CHECK(((h.m_flags & screenshare_flag_first_frag) != 0) == (i == 0));
        CHECK(((h.m_flags & screenshare_flag_last_frag) != 0) == (i + 1 == bodies.size()));
        if (i == 0)
        {
            REQUIRE(payloadLen >= kShareFrameLengthPrefixSize);
            CHECK(ssfrReadBe32(payload) == frame.size());
            rebuilt.append(payload + kShareFrameLengthPrefixSize, payloadLen - kShareFrameLengthPrefixSize);
        }
        else
        {
            rebuilt.append(payload, payloadLen);
        }
    }
    CHECK(rebuilt == frame);

    //非关键帧：不带关键帧位
    ShareFraming::fragmentFrame(screenshare_track_camera, 2, false, 1, frame.data(), frame.size(), kSsfrMaxPayload,
                                bodies);
    REQUIRE(bodies.size() == 3);
    CHECK((static_cast<unsigned char>(bodies[1][2]) & screenshare_flag_keyframe) == 0);

    //不足一片的视频帧：一个包体、首尾位都有、仍带长度前缀
    const std::string tiny = ssfrData(10, 2);
    ShareFraming::fragmentFrame(screenshare_track_screen, 0, false, 2, tiny.data(), tiny.size(), kSsfrMaxPayload,
                                bodies);
    REQUIRE(bodies.size() == 1);
    CHECK(bodies[0].size() == kShareFrameHeaderSize + kShareFrameLengthPrefixSize + tiny.size());
    CHECK((static_cast<unsigned char>(bodies[0][2]) & (screenshare_flag_first_frag | screenshare_flag_last_frag)) ==
          (screenshare_flag_first_frag | screenshare_flag_last_frag));

    //非视频轨：即使超过上限也整帧一片，不带前缀
    const std::string voice = ssfrData(300, 3);
    ShareFraming::fragmentFrame(screenshare_track_mic, 1, false, 3, voice.data(), voice.size(), kSsfrMaxPayload,
                                bodies);
    REQUIRE(bodies.size() == 1);
    CHECK(bodies[0].substr(kShareFrameHeaderSize) == voice);

    //上限传 0 时按默认值：64 KiB 以内的帧一片
    const std::string mid = ssfrData(1000, 4);
    ShareFraming::fragmentFrame(screenshare_track_screen, 0, true, 4, mid.data(), mid.size(), 0, bodies);
    CHECK(bodies.size() == 1);
}

TEST_CASE("SSFR3 重组：按序还原整帧，不同发送者与轨道交错到达各自还原", "[screenshare]")
{
    ShareFrameReassembler r;
    const std::string screenFrame = ssfrData(450, 5);
    const std::string cameraFrame = ssfrData(220, 6);
    std::vector<std::string> a;
    std::vector<std::string> b;
    ShareFraming::fragmentFrame(screenshare_track_screen, 0, true, 10, screenFrame.data(), screenFrame.size(),
                                kSsfrMaxPayload, a);
    ShareFraming::fragmentFrame(screenshare_track_camera, 2, false, 20, cameraFrame.data(), cameraFrame.size(),
                                kSsfrMaxPayload, b);
    REQUIRE(a.size() > 2);
    REQUIRE(b.size() > 2);

    //两路交错喂入
    std::vector<std::string> mixed;
    for (size_t i = 0; i < a.size() || i < b.size(); ++i)
    {
        if (i < a.size())
            mixed.push_back(a[i]);
        if (i < b.size())
            mixed.push_back(b[i]);
    }
    std::vector<ShareMediaFrame> frames = ssfrFeed(r, mixed);
    REQUIRE(frames.size() == 2);
    const ShareMediaFrame& f0 = (frames[0].m_track == screenshare_track_screen) ? frames[0] : frames[1];
    const ShareMediaFrame& f1 = (frames[0].m_track == screenshare_track_screen) ? frames[1] : frames[0];
    CHECK(f0.m_data == screenFrame);
    CHECK(f0.m_senderSeat == 0);
    CHECK(f0.m_keyframe);
    CHECK(f0.m_timestamp == 10);
    CHECK(f1.m_data == cameraFrame);
    CHECK(f1.m_senderSeat == 2);
    CHECK_FALSE(f1.m_keyframe);
    CHECK(r.droppedFrames() == 0);

    //非视频轨整帧到达即交出，载荷原样
    std::vector<std::string> voice;
    ShareFraming::fragmentFrame(screenshare_track_mic, 1, false, 30, "pcm", 3, kSsfrMaxPayload, voice);
    frames = ssfrFeed(r, voice);
    REQUIRE(frames.size() == 1);
    CHECK(frames[0].m_track == screenshare_track_mic);
    CHECK(frames[0].m_data == "pcm");
}

TEST_CASE("SSFR4 缺首分片、首分片重复、时间戳不一致、中间缺片都整帧丢弃，之后照常还原", "[screenshare]")
{
    const std::string frame = ssfrData(350, 7);
    std::vector<std::string> bodies;
    ShareFraming::fragmentFrame(screenshare_track_screen, 0, false, 100, frame.data(), frame.size(), kSsfrMaxPayload,
                                bodies);
    REQUIRE(bodies.size() == 4);

    SECTION("没有首分片的后续分片")
    {
        ShareFrameReassembler r;
        std::vector<std::string> noFirst(bodies.begin() + 1, bodies.end());
        CHECK(ssfrFeed(r, noFirst).empty());
        CHECK(ssfrFeed(r, bodies).size() == 1);
    }
    SECTION("累积中途又来了新的首分片：旧的丢弃，新的照常还原")
    {
        ShareFrameReassembler r;
        std::vector<std::string> seq;
        seq.push_back(bodies[0]);
        seq.push_back(bodies[1]);
        seq.insert(seq.end(), bodies.begin(), bodies.end());
        std::vector<ShareMediaFrame> frames = ssfrFeed(r, seq);
        REQUIRE(frames.size() == 1);
        CHECK(frames[0].m_data == frame);
        CHECK(r.droppedFrames() == 1);
    }
    SECTION("时间戳与首分片不一致")
    {
        ShareFrameReassembler r;
        std::vector<std::string> other;
        ShareFraming::fragmentFrame(screenshare_track_screen, 0, false, 101, frame.data(), frame.size(),
                                    kSsfrMaxPayload, other);
        std::vector<std::string> seq;
        seq.push_back(bodies[0]);
        seq.push_back(other[1]);
        seq.push_back(bodies[2]);
        seq.push_back(bodies[3]);
        CHECK(ssfrFeed(r, seq).empty());
        CHECK(r.droppedFrames() >= 1);
    }
    SECTION("中间缺片：尾分片到达时长度不符")
    {
        ShareFrameReassembler r;
        std::vector<std::string> seq;
        seq.push_back(bodies[0]);
        seq.push_back(bodies[1]);
        seq.push_back(bodies[3]);
        CHECK(ssfrFeed(r, seq).empty());
        CHECK(r.droppedFrames() == 1);
        CHECK(ssfrFeed(r, bodies).size() == 1);
    }
    SECTION("成员离开后其未完成分片清空")
    {
        ShareFrameReassembler r;
        std::vector<std::string> head(bodies.begin(), bodies.begin() + 2);
        CHECK(ssfrFeed(r, head).empty());
        r.resetSender(0);
        std::vector<std::string> tail(bodies.begin() + 2, bodies.end());
        CHECK(ssfrFeed(r, tail).empty());
    }
    SECTION("非法包体")
    {
        ShareFrameReassembler r;
        ShareMediaFrame f;
        CHECK_FALSE(r.push("abc", 3, f));
        CHECK_FALSE(r.push(NULL, 0, f));
    }
}

TEST_CASE("SSFR5 识别服务端发来的关键帧请求", "[screenshare]")
{
    ShareMediaFrame f;
    f.m_track = screenshare_track_control;
    f.m_senderSeat = screenshare_seat_server;
    f.m_data.push_back(static_cast<char>(screenshare_control_keyframe_request));
    f.m_data.push_back(static_cast<char>(screenshare_track_camera));
    int track = 0;
    REQUIRE(ShareFraming::parseKeyFrameRequest(f, track));
    CHECK(track == screenshare_track_camera);

    //成员发来的控制帧不算
    ShareMediaFrame fromMember = f;
    fromMember.m_senderSeat = 1;
    CHECK_FALSE(ShareFraming::parseKeyFrameRequest(fromMember, track));
    //别的控制类型不算
    ShareMediaFrame other = f;
    other.m_data[0] = static_cast<char>(screenshare_control_keyframe_request + 1);
    CHECK_FALSE(ShareFraming::parseKeyFrameRequest(other, track));
    //载荷长度不对不算
    ShareMediaFrame shortFrame = f;
    shortFrame.m_data.resize(1);
    CHECK_FALSE(ShareFraming::parseKeyFrameRequest(shortFrame, track));
    //不是控制帧不算
    ShareMediaFrame video = f;
    video.m_track = screenshare_track_screen;
    CHECK_FALSE(ShareFraming::parseKeyFrameRequest(video, track));

    //经重组器还原服务端发来的整帧（不带长度前缀的控制帧）
    ShareFrameHeader h;
    h.m_track = screenshare_track_control;
    h.m_senderSeat = screenshare_seat_server;
    h.m_flags = screenshare_flag_first_frag | screenshare_flag_last_frag;
    const std::string body = ShareFraming::encodeBody(h, f.m_data.data(), f.m_data.size());
    ShareFrameReassembler r;
    ShareMediaFrame got;
    REQUIRE(r.push(body.data(), body.size(), got));
    REQUIRE(ShareFraming::parseKeyFrameRequest(got, track));
    CHECK(track == screenshare_track_camera);
}

TEST_CASE("SSFR6 视频帧载荷带真实宽高，供观看端裁掉解码补齐的部分", "[screenshare]")
{
    const std::string h264 = ssfrData(500, 11);
    const std::string payload = ShareFraming::buildVideoPayload(1920, 1080, h264.data(), h264.size());
    REQUIRE(payload.size() == 4 + h264.size());
    //宽高各 2 字节大端：1920 = 0x0780，1080 = 0x0438
    CHECK(static_cast<unsigned char>(payload[0]) == 0x07);
    CHECK(static_cast<unsigned char>(payload[1]) == 0x80);
    CHECK(static_cast<unsigned char>(payload[2]) == 0x04);
    CHECK(static_cast<unsigned char>(payload[3]) == 0x38);

    int w = 0;
    int h = 0;
    const char* data = NULL;
    size_t len = 0;
    REQUIRE(ShareFraming::parseVideoPayload(payload, w, h, data, len));
    CHECK(w == 1920);
    CHECK(h == 1080);
    REQUIRE(len == h264.size());
    CHECK(std::string(data, len) == h264);

    //尺寸越界组不出来；太短、宽或高为 0 的载荷拆不开
    CHECK(ShareFraming::buildVideoPayload(0, 1080, h264.data(), h264.size()).empty());
    CHECK(ShareFraming::buildVideoPayload(70000, 1080, h264.data(), h264.size()).empty());
    CHECK_FALSE(ShareFraming::parseVideoPayload(std::string("\x07\x80\x04", 3), w, h, data, len));
    CHECK_FALSE(ShareFraming::parseVideoPayload(std::string(4, '\0'), w, h, data, len));
}

TEST_CASE("SSFR7 共享内容状态控制帧：构造后经重组器还原并识别，四种状态都能往返", "[screenshare]")
{
    const int states[] = { screenshare_source_sharing, screenshare_source_minimized, screenshare_source_paused,
                           screenshare_source_closed };
    for (size_t i = 0; i < sizeof(states) / sizeof(states[0]); ++i)
    {
        const std::string body = ShareFraming::buildSourceStateBody(0, states[i]);
        REQUIRE(body.size() == kShareFrameHeaderSize + 2);
        ShareFrameReassembler r;
        ShareMediaFrame f;
        REQUIRE(r.push(body.data(), body.size(), f));
        CHECK(f.m_track == screenshare_track_control);
        int state = -1;
        REQUIRE(ShareFraming::parseSourceState(f, state));
        CHECK(state == states[i]);
        //不是关键帧请求
        int track = 0;
        CHECK_FALSE(ShareFraming::parseKeyFrameRequest(f, track));
    }

    //关键帧请求不是状态帧；状态取值越界不认
    ShareMediaFrame keyReq;
    keyReq.m_track = screenshare_track_control;
    keyReq.m_senderSeat = screenshare_seat_server;
    keyReq.m_data.push_back(static_cast<char>(screenshare_control_keyframe_request));
    keyReq.m_data.push_back(static_cast<char>(screenshare_track_screen));
    int state = -1;
    CHECK_FALSE(ShareFraming::parseSourceState(keyReq, state));
    ShareMediaFrame bad = keyReq;
    bad.m_data[0] = static_cast<char>(screenshare_control_source_state);
    bad.m_data[1] = static_cast<char>(screenshare_source_closed + 1);
    CHECK_FALSE(ShareFraming::parseSourceState(bad, state));
}
