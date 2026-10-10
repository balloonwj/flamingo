/**
 *  群通话的 Opus DTX（不连续发送）单元测试（Suite GCD，标签 [groupcall]），GroupCallDtxTests.cpp
 *  群通话最多 16 人，大多数人大多数时候不说话。打开 DTX 后，静音时编码器只产出 1~2 字节的「不必发送」包，引擎不再
 *  把这些包交给上层，服务端也就不必把它们转发给其余 15 人（docs/群音视频通话方案.md 第 5 期）。覆盖：
 *    - GCD1 编码器：初始化之前打不开 DTX；打开后连续编码静音，大部分包是 DTX 包；有声音时不是 DTX 包；
 *    - GCD2 音频引擎：打开 DTX 时静音帧大部分不送出、有声音的帧逐帧送出；没打开时每帧都送出（一对一通话、屏幕共享不变）；
 *    - GCD3 共享音频会话：SetDtx 转给引擎，静音时发出的麦克风轨帧明显少于帧数，有声音时逐帧发出。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-04
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <math.h>
#include <mutex>
#include <string>
#include <vector>

#include "../../FlamingoMedia/Call/CallAudioEngine.h"
#include "../../FlamingoMedia/Call/OpusAudioCodec.h"
#include "../../FlamingoMedia/Share/ShareAudioSession.h"

namespace {

const int kGcdRate = 48000;                      //内部采样率（Hz）
const int kGcdFrame = 960;                       //20 毫秒一帧的采样数
const int kGcdBitrate = 24000;                   //与通话引擎相同的目标码率（bit/s）
const int kGcdSilentFrames = 100;                //连续编码的静音帧数（2 秒）
const int kGcdToneFrames = 50;                   //连续编码的有声帧数（1 秒）
const double kGcdPi = 3.14159265358979323846;
const double kGcdAmp = 8000.0;                   //测试音的幅度
const double kGcdToneHz = 1000.0;                //测试音的频率（Hz）

std::vector<int16_t> gcdTone(int samples, int offset)
{
    std::vector<int16_t> v(static_cast<size_t>(samples));
    for (int i = 0; i < samples; ++i)
        v[i] = static_cast<int16_t>(kGcdAmp * sin(2.0 * kGcdPi * kGcdToneHz * (i + offset) / kGcdRate));
    return v;
}

//把引擎编码出的包收集起来
class GcdSink : public call::CallAudioEngineSink
{
public:
    virtual void OnEncodedAudio(const unsigned char* data, size_t len, uint64_t frameIndex) override
    {
        (void)frameIndex;
        m_sizes.push_back(len);
    }

    std::vector<size_t> m_sizes;   //收到的每个包的字节数，按到达顺序
};

//把共享音频会话发出的包体收集起来的假通道
class GcdTransport : public screenshare::ShareAudioTransport
{
public:
    virtual bool SendRoomBody(const std::string& body) override
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        m_count++;
        return true;
    }

    size_t Count()
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        return m_count;
    }

private:
    std::mutex m_mutex;      //保护 m_count
    size_t     m_count = 0;  //收到的包体个数
};

} // namespace

TEST_CASE("GCD1 编码器的 DTX 开关与 DTX 包判定", "[groupcall]")
{
    call::OpusAudioCodec codec;
    //还没初始化：打不开
    CHECK_FALSE(codec.SetDtx(true));
    REQUIRE(codec.Init(kGcdRate, kGcdFrame, kGcdBitrate));
    REQUIRE(codec.SetDtx(true));

    const std::vector<int16_t> silent(static_cast<size_t>(kGcdFrame), 0);
    int dtxPackets = 0;
    for (int f = 0; f < kGcdSilentFrames; ++f)
    {
        std::vector<unsigned char> packet;
        REQUIRE(codec.Encode(&silent[0], kGcdFrame, packet));
        if (call::OpusAudioCodec::IsDtxPacket(packet.size()))
            ++dtxPackets;
    }
    INFO("dtx packets=" << dtxPackets);
    //Opus 在静音约 200 毫秒后进入 DTX，之后每 400 毫秒才补一个舒适噪声包：2 秒里一半以上应是 DTX 包
    CHECK(dtxPackets > kGcdSilentFrames / 2);

    int toneDtx = 0;
    for (int f = 0; f < kGcdToneFrames; ++f)
    {
        const std::vector<int16_t> pcm = gcdTone(kGcdFrame, f * kGcdFrame);
        std::vector<unsigned char> packet;
        REQUIRE(codec.Encode(&pcm[0], kGcdFrame, packet));
        if (call::OpusAudioCodec::IsDtxPacket(packet.size()))
            ++toneDtx;
    }
    CHECK(toneDtx == 0);

    CHECK(call::OpusAudioCodec::IsDtxPacket(0));
    CHECK(call::OpusAudioCodec::IsDtxPacket(1));
    CHECK(call::OpusAudioCodec::IsDtxPacket(2));
    CHECK_FALSE(call::OpusAudioCodec::IsDtxPacket(3));
}

TEST_CASE("GCD2 音频引擎打开 DTX 后静音不送出，没打开时逐帧送出", "[groupcall]")
{
    const std::vector<int16_t> silent(static_cast<size_t>(kGcdFrame), 0);

    GcdSink dtxSink;
    call::CallAudioEngine dtxEngine;
    dtxEngine.SetDtx(true);
    REQUIRE(dtxEngine.StartWithoutDevices(&dtxSink, false, true));
    for (int f = 0; f < kGcdSilentFrames; ++f)
        dtxEngine.FeedCapture(&silent[0], kGcdFrame);
    const size_t silentSent = dtxSink.m_sizes.size();
    INFO("silent frames sent=" << silentSent);
    CHECK(silentSent < static_cast<size_t>(kGcdSilentFrames / 2));
    //送出去的都不是 DTX 包
    for (size_t i = 0; i < dtxSink.m_sizes.size(); ++i)
        CHECK_FALSE(call::OpusAudioCodec::IsDtxPacket(dtxSink.m_sizes[i]));
    for (int f = 0; f < kGcdToneFrames; ++f)
    {
        const std::vector<int16_t> pcm = gcdTone(kGcdFrame, f * kGcdFrame);
        dtxEngine.FeedCapture(&pcm[0], kGcdFrame);
    }
    CHECK(dtxSink.m_sizes.size() == silentSent + kGcdToneFrames);
    dtxEngine.Stop();

    //没打开 DTX：每一帧都送出
    GcdSink plainSink;
    call::CallAudioEngine plainEngine;
    REQUIRE(plainEngine.StartWithoutDevices(&plainSink, false, true));
    for (int f = 0; f < kGcdSilentFrames; ++f)
        plainEngine.FeedCapture(&silent[0], kGcdFrame);
    CHECK(plainSink.m_sizes.size() == static_cast<size_t>(kGcdSilentFrames));
    plainEngine.Stop();
}

TEST_CASE("GCD3 共享音频会话把 DTX 开关转给引擎", "[groupcall]")
{
    const std::vector<int16_t> silent(static_cast<size_t>(kGcdFrame), 0);
    GcdTransport transport;
    screenshare::ShareAudioSession s;
    s.SetDtx(true);
    REQUIRE(s.Start(&transport, 2, true, true));
    for (int f = 0; f < kGcdSilentFrames; ++f)
        s.FeedMic(&silent[0], kGcdFrame);
    const size_t silentSent = transport.Count();
    INFO("silent frames sent=" << silentSent);
    CHECK(silentSent < static_cast<size_t>(kGcdSilentFrames / 2));
    for (int f = 0; f < kGcdToneFrames; ++f)
    {
        const std::vector<int16_t> pcm = gcdTone(kGcdFrame, f * kGcdFrame);
        s.FeedMic(&pcm[0], kGcdFrame);
    }
    CHECK(transport.Count() == silentSent + kGcdToneFrames);
    s.Stop();
}
