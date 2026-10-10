/**
 *  屏幕共享房间语音的单元测试（Suite SSAU，标签 [shareaudio]），ShareAudioSessionTests.cpp
 *
 *  覆盖 FlamingoMedia/Share/ShareAudioSession（docs/屏幕共享与录制方案.md 第 4 期 4b / 4c），无设备模式、假发帧通道：
 *    - SSAU1 路号：不同座位、不同声音轨各自一路，互不重叠；
 *    - SSAU2 麦克风打开时采集编码成麦克风轨的包体发出（帧头的座位号是本人），关闭后不再发，再打开恢复；
 *    - SSAU3 两名成员的声音分别收进来，播放端两个频率都在；删除一个座位后只剩另一个；
 *    - SSAU4 发言判定：别人的声音响时判为在说话，本人麦克风响时本人座位也在列；安静一段时间后都不在列；
 *    - SSAU5 画面、控制等非声音轨，以及标着本人座位的帧都忽略；
 *  第 4 期 4d 补充「共享电脑声音」：
 *    - SSAU6 打开后电脑声音编成电脑声音轨发出（与麦克风轨分开），关闭后不再发；
 *    - SSAU7 收到别人的电脑声音照常混音播放，但不算「在说话」；
 *    - SSAU8 闪避：排除本进程树时不闪避；退回普通回环时有人说话把电脑声音压低 20 dB，没人说话不变。
 *    - TS3（2026-10-06，docs/音视频通话方案.md 第 9.9 节 B1）麦克风帧头时间戳取音频引擎给的采集帧序号 × 20 毫秒，
 *      电脑声音每处理一帧前进 20 毫秒，两条轨各自从 0 起。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-03
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <algorithm>
#include <math.h>
#include <mutex>
#include <string>
#include <vector>

#include "../../FlamingoMedia/Share/ShareAudioSession.h"
#include "../../FlamingoMedia/Call/OpusAudioCodec.h"
#include "../net/Protocol.h"

using namespace screenshare;
using call::OpusAudioCodec;

namespace {

const int kSsauRate = 48000;
const int kSsauFrame = 960;
const double kSsauPi = 3.14159265358979323846;
const double kSsauAmp = 8000.0;

std::vector<int16_t> ssauTone(int samples, double hz, int offset)
{
    std::vector<int16_t> v(static_cast<size_t>(samples));
    for (int i = 0; i < samples; ++i)
        v[i] = static_cast<int16_t>(kSsauAmp * sin(2.0 * kSsauPi * hz * (i + offset) / kSsauRate));
    return v;
}

double ssauGoertzel(const int16_t* p, int n, double hz)
{
    const double k = 2.0 * cos(2.0 * kSsauPi * hz / kSsauRate);
    double s1 = 0;
    double s2 = 0;
    for (int i = 0; i < n; ++i)
    {
        const double s0 = p[i] + k * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    return s1 * s1 + s2 * s2 - k * s1 * s2;
}

//把发出的包体收集起来的假通道
class SsauTransport : public ShareAudioTransport
{
public:
    virtual bool SendRoomBody(const std::string& body) override
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        m_bodies.push_back(body);
        return true;
    }

    size_t Count()
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        return m_bodies.size();
    }

    std::mutex               m_mutex;    //保护 m_bodies
    std::vector<std::string> m_bodies;   //收到的包体
};

//一帧声音的帧头时间戳步长（微秒，20 毫秒）
const uint64_t kSsauFrameUs = 20000;

//某个座位的一帧声音（用独立的编码器编出）；index 为发送端的采集帧序号，帧头时间戳 = index × 20 毫秒
//（2026-10-06 起播放端按它排队，见 docs/音视频通话方案.md 第 9.9 节）
ShareMediaFrame ssauFrame(int seat, int track, const std::vector<unsigned char>& opus, int index)
{
    ShareMediaFrame f;
    f.m_track = track;
    f.m_senderSeat = seat;
    f.m_keyframe = false;
    f.m_timestamp = static_cast<uint64_t>(index) * kSsauFrameUs;
    f.m_data.assign(reinterpret_cast<const char*>(&opus[0]), opus.size());
    return f;
}

//给某个座位编出 frames 帧单音
void ssauFeedRemote(ShareAudioSession& s, OpusAudioCodec& enc, int seat, double hz, int frames)
{
    for (int f = 0; f < frames; ++f)
    {
        const std::vector<int16_t> pcm = ssauTone(kSsauFrame, hz, f * kSsauFrame);
        std::vector<unsigned char> packet;
        REQUIRE(enc.Encode(&pcm[0], kSsauFrame, packet));
        s.OnRemoteFrame(ssauFrame(seat, screenshare_track_mic, packet, f));
    }
}

} // namespace

TEST_CASE("SSAU1 每个座位的每条声音轨各一路", "[shareaudio]")
{
    std::vector<int> ids;
    for (int seat = 0; seat <= 254; ++seat)
    {
        ids.push_back(ShareAudioSession::StreamIdOf(seat, screenshare_track_mic));
        ids.push_back(ShareAudioSession::StreamIdOf(seat, screenshare_track_system_audio));
    }
    std::sort(ids.begin(), ids.end());
    CHECK(std::unique(ids.begin(), ids.end()) == ids.end());
}

TEST_CASE("SSAU2 麦克风开关决定是否发出麦克风轨", "[shareaudio]")
{
    SsauTransport transport;
    ShareAudioSession s;
    REQUIRE(s.Start(&transport, 3, true, true));
    CHECK(s.IsRunning());
    CHECK(s.IsMicEnabled());
    const std::vector<int16_t> tone = ssauTone(kSsauFrame, 1000.0, 0);
    s.FeedMic(&tone[0], kSsauFrame);
    REQUIRE(transport.Count() == 1);
    ShareFrameHeader header;
    const char* payload = NULL;
    size_t payloadLen = 0;
    REQUIRE(ShareFraming::decodeBody(transport.m_bodies[0].data(), transport.m_bodies[0].size(), header, payload,
                                     payloadLen));
    CHECK(header.m_track == screenshare_track_mic);
    CHECK(header.m_senderSeat == 3);
    CHECK(payloadLen > 0);

    REQUIRE(s.SetMicEnabled(false));
    CHECK_FALSE(s.IsMicEnabled());
    s.FeedMic(&tone[0], kSsauFrame);
    CHECK(transport.Count() == 1);
    REQUIRE(s.SetMicEnabled(true));
    s.FeedMic(&tone[0], kSsauFrame);
    CHECK(transport.Count() == 2);
    s.Stop();
    CHECK_FALSE(s.IsRunning());
}

TEST_CASE("SSAU3 多名成员的声音混音播放，删除座位后只剩另一个", "[shareaudio]")
{
    SsauTransport transport;
    ShareAudioSession s;
    REQUIRE(s.Start(&transport, 0, false, true));
    OpusAudioCodec enc1;
    OpusAudioCodec enc2;
    REQUIRE(enc1.Init(kSsauRate, kSsauFrame, 32000));
    REQUIRE(enc2.Init(kSsauRate, kSsauFrame, 32000));
    const int kFrames = 10;
    ssauFeedRemote(s, enc1, 1, 1000.0, kFrames);
    ssauFeedRemote(s, enc2, 2, 2500.0, kFrames);
    std::vector<int16_t> out(static_cast<size_t>(kSsauFrame) * kFrames);
    s.PullPlayback(&out[0], static_cast<int>(out.size()));
    const int half = static_cast<int>(out.size()) / 2;
    const double noise = ssauGoertzel(&out[half], half, 4000.0);
    CHECK(ssauGoertzel(&out[half], half, 1000.0) > 10.0 * noise);
    CHECK(ssauGoertzel(&out[half], half, 2500.0) > 10.0 * noise);

    s.RemoveSeat(1);
    ssauFeedRemote(s, enc1, 1, 1000.0, 0);
    ssauFeedRemote(s, enc2, 2, 2500.0, kFrames);
    s.PullPlayback(&out[0], static_cast<int>(out.size()));
    CHECK(ssauGoertzel(&out[half], half, 2500.0) > 100.0 * ssauGoertzel(&out[half], half, 1000.0));
    s.Stop();
}

TEST_CASE("SSAU4 发言判定含别人与本人", "[shareaudio]")
{
    SsauTransport transport;
    ShareAudioSession s;
    REQUIRE(s.Start(&transport, 4, true, true));
    OpusAudioCodec enc;
    REQUIRE(enc.Init(kSsauRate, kSsauFrame, 32000));
    ssauFeedRemote(s, enc, 2, 1000.0, 5);
    std::vector<int16_t> out(static_cast<size_t>(kSsauFrame));
    for (int i = 0; i < 5; ++i)
        s.PullPlayback(&out[0], kSsauFrame);
    const std::vector<int16_t> tone = ssauTone(kSsauFrame, 700.0, 0);
    s.FeedMic(&tone[0], kSsauFrame);
    std::vector<int> talking = s.UpdateSpeaking(1000);
    REQUIRE(talking.size() == 2);
    CHECK(talking[0] == 2);
    CHECK(talking[1] == 4);

    //之后一直安静：别人那一路没有新数据、本人喂静音，过了保持时间都不在列
    const std::vector<int16_t> silent(static_cast<size_t>(kSsauFrame), 0);
    s.FeedMic(&silent[0], kSsauFrame);
    s.PullPlayback(&out[0], kSsauFrame);
    talking = s.UpdateSpeaking(1100);
    talking = s.UpdateSpeaking(5000);
    CHECK(talking.empty());
    s.Stop();
}

TEST_CASE("SSAU5 非声音轨与本人座位的帧忽略", "[shareaudio]")
{
    SsauTransport transport;
    ShareAudioSession s;
    REQUIRE(s.Start(&transport, 5, false, true));
    OpusAudioCodec enc;
    REQUIRE(enc.Init(kSsauRate, kSsauFrame, 32000));
    const std::vector<int16_t> pcm = ssauTone(kSsauFrame, 1000.0, 0);
    std::vector<unsigned char> packet;
    REQUIRE(enc.Encode(&pcm[0], kSsauFrame, packet));
    s.OnRemoteFrame(ssauFrame(1, screenshare_track_screen, packet, 0));
    s.OnRemoteFrame(ssauFrame(1, screenshare_track_control, packet, 0));
    s.OnRemoteFrame(ssauFrame(5, screenshare_track_mic, packet, 0));
    std::vector<int16_t> out(static_cast<size_t>(kSsauFrame), 3);
    s.PullPlayback(&out[0], kSsauFrame);
    for (int i = 0; i < kSsauFrame; ++i)
        CHECK(out[i] == 0);
    CHECK(s.UpdateSpeaking(100).empty());
    s.Stop();
}

namespace {

//用同一个解码器按顺序解码全部电脑声音轨的包体，返回每一包解出的均方根（Opus 解码器有状态，不能单独解某一包）
std::vector<double> ssauDecodedRmsList(const std::vector<std::string>& bodies)
{
    std::vector<double> result;
    OpusAudioCodec dec;
    if (!dec.Init(kSsauRate, kSsauFrame, 0))
        return result;
    for (size_t b = 0; b < bodies.size(); ++b)
    {
        ShareFrameHeader header;
        const char* payload = NULL;
        size_t len = 0;
        std::vector<int16_t> pcm;
        double rms = -1;
        if (ShareFraming::decodeBody(bodies[b].data(), bodies[b].size(), header, payload, len) &&
            dec.Decode(reinterpret_cast<const unsigned char*>(payload), len, pcm) && !pcm.empty())
        {
            double sum = 0;
            for (size_t i = 0; i < pcm.size(); ++i)
                sum += static_cast<double>(pcm[i]) * pcm[i];
            rms = sqrt(sum / pcm.size());
        }
        result.push_back(rms);
    }
    return result;
}

//包体的轨道
int ssauTrackOf(const std::string& body)
{
    ShareFrameHeader header;
    const char* payload = NULL;
    size_t len = 0;
    if (!ShareFraming::decodeBody(body.data(), body.size(), header, payload, len))
        return -1;
    return header.m_track;
}

} // namespace

TEST_CASE("SSAU6 电脑声音编成电脑声音轨发出", "[shareaudio]")
{
    SsauTransport transport;
    ShareAudioSession s;
    REQUIRE(s.Start(&transport, 0, false, true));
    CHECK_FALSE(s.IsSystemAudioEnabled());
    const std::vector<int16_t> tone = ssauTone(kSsauFrame, 440.0, 0);
    s.FeedSystemAudio(&tone[0], kSsauFrame);
    CHECK(transport.Count() == 0);

    REQUIRE(s.SetSystemAudioEnabled(true));
    CHECK(s.IsSystemAudioEnabled());
    s.FeedSystemAudio(&tone[0], kSsauFrame);
    REQUIRE(transport.Count() == 1);
    CHECK(ssauTrackOf(transport.m_bodies[0]) == screenshare_track_system_audio);
    //麦克风关着，电脑声音照发
    CHECK_FALSE(s.IsMicEnabled());

    REQUIRE(s.SetSystemAudioEnabled(false));
    s.FeedSystemAudio(&tone[0], kSsauFrame);
    CHECK(transport.Count() == 1);
    s.Stop();
}

TEST_CASE("SSAU7 别人的电脑声音照常播放但不算在说话", "[shareaudio]")
{
    SsauTransport transport;
    ShareAudioSession s;
    REQUIRE(s.Start(&transport, 2, false, true));
    OpusAudioCodec enc;
    REQUIRE(enc.Init(kSsauRate, kSsauFrame, 64000, true));
    const int kFrames = 10;
    for (int f = 0; f < kFrames; ++f)
    {
        const std::vector<int16_t> pcm = ssauTone(kSsauFrame, 1000.0, f * kSsauFrame);
        std::vector<unsigned char> packet;
        REQUIRE(enc.Encode(&pcm[0], kSsauFrame, packet));
        s.OnRemoteFrame(ssauFrame(0, screenshare_track_system_audio, packet, f));
    }
    std::vector<int16_t> out(static_cast<size_t>(kSsauFrame) * kFrames);
    s.PullPlayback(&out[0], static_cast<int>(out.size()));
    const int half = static_cast<int>(out.size()) / 2;
    CHECK(ssauGoertzel(&out[half], half, 1000.0) > 10.0 * ssauGoertzel(&out[half], half, 4000.0));
    CHECK(s.UpdateSpeaking(1000).empty());
    s.Stop();
}

TEST_CASE("SSAU8 退回普通回环时有人说话就闪避", "[shareaudio]")
{
    CHECK(ShareAudioSession::DuckingGainPercent(true, true) == 100);
    CHECK(ShareAudioSession::DuckingGainPercent(true, false) == 100);
    CHECK(ShareAudioSession::DuckingGainPercent(false, false) == 100);
    CHECK(ShareAudioSession::DuckingGainPercent(false, true) == 10);
    int16_t pcm[3] = { 1000, -2000, 32767 };
    ShareAudioSession::ApplyGainPercent(pcm, 3, 10);
    CHECK(pcm[0] == 100);
    CHECK(pcm[1] == -200);
    CHECK(pcm[2] == 3276);

    //会话层面：退回普通回环、别人正在说话时，发出去的电脑声音明显变小
    SsauTransport transport;
    ShareAudioSession s;
    REQUIRE(s.Start(&transport, 0, false, true));
    REQUIRE(s.SetSystemAudioEnabled(true));
    s.SetSystemAudioModeForTest(false);
    //Opus 编码器要十几帧才稳定下来，预热与闪避各喂 15 帧，比较各自最后一帧
    const int kWarm = 15;
    std::vector<int16_t> tone;
    for (int f = 0; f < kWarm; ++f)
    {
        tone = ssauTone(kSsauFrame, 440.0, f * kSsauFrame);
        s.FeedSystemAudio(&tone[0], kSsauFrame);
    }
    REQUIRE(transport.Count() == static_cast<size_t>(kWarm));

    OpusAudioCodec enc;
    REQUIRE(enc.Init(kSsauRate, kSsauFrame, 32000));
    ssauFeedRemote(s, enc, 3, 1000.0, kWarm);
    std::vector<int16_t> out(static_cast<size_t>(kSsauFrame));
    for (int i = 0; i < kWarm; ++i)
        s.PullPlayback(&out[0], kSsauFrame);
    for (int f = 0; f < kWarm; ++f)
    {
        tone = ssauTone(kSsauFrame, 440.0, (kWarm + f) * kSsauFrame);
        s.FeedSystemAudio(&tone[0], kSsauFrame);
    }
    REQUIRE(transport.Count() == static_cast<size_t>(kWarm) * 2);
    const std::vector<double> rms = ssauDecodedRmsList(transport.m_bodies);
    REQUIRE(rms.size() == static_cast<size_t>(kWarm) * 2);
    const double loud = rms[kWarm - 1];
    const double ducked = rms.back();
    INFO("loud rms " << loud << ", ducked rms " << ducked);
    CHECK(loud > 1000.0);
    CHECK(ducked < loud * 0.3);
    s.Stop();
}

TEST_CASE("TS3 屏幕共享的声音时间戳：麦克风按采集帧序号，电脑声音每处理一帧前进 20 毫秒", "[shareaudio][jitterbuf]")
{
    //帧头时间戳的单位（微秒）与相邻两帧之差
    const uint64_t kFrameUs = 20000;
    SsauTransport transport;
    ShareAudioSession s;
    REQUIRE(s.Start(&transport, 0, true, true));
    REQUIRE(s.SetSystemAudioEnabled(true));
    const std::vector<int16_t> tone = ssauTone(kSsauFrame, 1000.0, 0);
    s.FeedMic(&tone[0], kSsauFrame);
    s.FeedMic(&tone[0], kSsauFrame);
    s.FeedSystemAudio(&tone[0], kSsauFrame);
    s.FeedSystemAudio(&tone[0], kSsauFrame);
    s.Stop();

    std::vector<uint64_t> mic;
    std::vector<uint64_t> sys;
    for (size_t i = 0; i < transport.m_bodies.size(); ++i)
    {
        ShareFrameHeader header;
        const char* payload = NULL;
        size_t len = 0;
        REQUIRE(ShareFraming::decodeBody(transport.m_bodies[i].data(), transport.m_bodies[i].size(), header, payload,
                                         len));
        if (header.m_track == screenshare_track_mic)
            mic.push_back(header.m_timestamp);
        else if (header.m_track == screenshare_track_system_audio)
            sys.push_back(header.m_timestamp);
    }
    REQUIRE(mic.size() == 2);
    REQUIRE(sys.size() == 2);
    CHECK(mic[0] == 0);
    CHECK(mic[1] == kFrameUs);
    CHECK(sys[0] == 0);
    CHECK(sys[1] == kFrameUs);
}
