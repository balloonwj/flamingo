/**
 *  通话音频引擎的单元测试（Suite CAE，标签 [audiomix]），CallAudioEngineTests.cpp
 *
 *  第 4 期 4a 把引擎的播放端改为多路混音（AudioStreamMixer），这里用「无设备模式」验证引擎本体，不打开麦克风与扬声器：
 *    - CAE1 一对一通话的路径不变：喂入采集的 1 kHz 正弦 → sink 收到 Opus 包 → 包喂回 OnEncodedPacket → 播放端取出的是同一个音；
 *    - CAE2 多路：两路分别喂入 1 kHz 与 2.5 kHz，播放端两个频率都在；按路查询电平；
 *    - CAE3 关闭采集后不再编码送出，再打开后恢复；采集电平随输入变化；
 *    - CAE4 未启动时喂入与取出都安全：取出静音、sink 收不到包。
 *  2026-10-05 补充真实通话的采集处理路径（CAE1~CAE4 都关着回声消除，而一对一通话一定打开回声消除与降噪）：
 *    - CAE5 两个引擎都打开回声消除与降噪，A 说话、B 不说话：A 的声音完整到达 B 的播放端；
 *    - CAE6 两个引擎都打开回声消除与降噪，双方同时说话：两个方向都完整到达，不被对方的声音当作回声消掉；
 *    - CAE7 播放音量的增益曲线：0~100% 取平方、100%~150% 线性，超出范围按边界；
 *    - CAE8 引擎播放音量：输出按增益缩放，0% 为静音；录制旁路收到的是调音量之前的声音。
 *  2026-10-06 补充（docs/音视频通话方案.md 第 9.9 节方案 B 第 B1 步，标签另加 [jitterbuf]）：
 *    - TS1 采集帧序号每处理一帧加 1、从 0 起，重新启动后清零；
 *    - TS2 打开 DTX 后静音期间没有送出的帧照样计入序号，恢复说话后的序号越过这段静音。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-03
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <math.h>
#include <vector>

#include "../../FlamingoMedia/Call/CallAudioEngine.h"

using namespace call;
using Catch::Approx;

namespace {

const int kCaeRate = 48000;
const int kCaeFrame = 960;
const double kCaePi = 3.14159265358979323846;
const double kCaeAmp = 8000.0;

std::vector<int16_t> caeTone(int samples, double hz, int offset)
{
    std::vector<int16_t> v(static_cast<size_t>(samples));
    for (int i = 0; i < samples; ++i)
        v[i] = static_cast<int16_t>(kCaeAmp * sin(2.0 * kCaePi * hz * (i + offset) / kCaeRate));
    return v;
}

double caeGoertzel(const int16_t* p, int n, double hz)
{
    const double k = 2.0 * cos(2.0 * kCaePi * hz / kCaeRate);
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

//把编码出的包收集起来
class CaeSink : public CallAudioEngineSink
{
public:
    virtual void OnEncodedAudio(const unsigned char* data, size_t len, uint64_t frameIndex) override
    {
        m_packets.push_back(std::vector<unsigned char>(data, data + len));
        m_frameIndexes.push_back(frameIndex);
    }

    std::vector<std::vector<unsigned char> > m_packets;   //收到的编码包，按到达顺序
    std::vector<uint64_t> m_frameIndexes;                 //每个包的采集帧序号，与 m_packets 一一对应
};

// ---- CAE5 / CAE6：模拟说话的测试信号 ----------------------------------------------
//一个音节发声的时长与音节之间停顿的时长（毫秒）：接近正常语速，让降噪器能分辨出「有人说话」与「背景」
const int kCaeSyllableOnMs = 200;
const int kCaeSyllableOffMs = 100;
//每秒的毫秒数
const int kCaeMsPerSecond = 1000;
//每个音节的基频在这两个值之间循环（Hz）：成年人说话的基频范围
const double kCaeVoiceLowHz = 140.0;
const double kCaeVoiceHighHz = 220.0;
//相邻音节基频的步进（Hz）
const double kCaeVoiceStepHz = 20.0;
//谐波最高到这个频率（Hz）：语音能量主要分布在 4 kHz 以下
const double kCaeHarmonicTopHz = 3500.0;
//信号峰值幅度：约 -12 dBFS，与正常说话时的麦克风电平相当
const double kCaeVoiceAmp = 8000.0;
//CAE5 / CAE6 的总时长（帧，20 ms 一帧）：3 秒，留给降噪器的噪声估计足够的收敛时间
const int kCaeVoiceFrames = 150;
//只比较后 2 秒（帧）：前 1 秒是回声消除与降噪的收敛期
const int kCaeMeasureFrames = 100;
//到达播放端的均方根至少是输入的这个比例才算「完整到达」（约 -10 dB；完全听不到时接近 0）
const double kCaeMinPassRatio = 0.3;
//CAE6 里 B 说话的基频比 A 高出的量（Hz）：两路声音频率错开，便于分辨谁是谁
const double kCaeOtherVoiceShiftHz = 60.0;

/**
 *  生成一段模拟说话的信号：按音节发声与停顿，每个音节的基频不同，带 1/k 衰减的谐波，音节首尾按升余弦渐入渐出。
 *  @param frames   帧数（每帧 kCaeFrame 个采样，48 kHz）
 *  @param shiftHz  基频整体上移的量（Hz），用来生成另一个人的声音
 *  @return 采样序列
 */
std::vector<int16_t> caeVoice(int frames, double shiftHz)
{
    const int total = frames * kCaeFrame;
    const int onSamples = kCaeRate * kCaeSyllableOnMs / kCaeMsPerSecond;
    const int periodSamples = kCaeRate * (kCaeSyllableOnMs + kCaeSyllableOffMs) / kCaeMsPerSecond;
    const int pitchSteps = static_cast<int>((kCaeVoiceHighHz - kCaeVoiceLowHz) / kCaeVoiceStepHz) + 1;
    std::vector<int16_t> v(static_cast<size_t>(total), 0);
    for (int i = 0; i < total; ++i)
    {
        const int syllable = i / periodSamples;
        const int pos = i % periodSamples;
        //音节之间的停顿：静音
        if (pos >= onSamples)
            continue;
        const double f0 = kCaeVoiceLowHz + kCaeVoiceStepHz * (syllable % pitchSteps) + shiftHz;
        //升余弦包络：音节首尾渐入渐出，避免突变带来的宽带噪声
        const double env = 0.5 - 0.5 * cos(2.0 * kCaePi * pos / onSamples);
        double s = 0;
        for (int k = 1; k * f0 < kCaeHarmonicTopHz; ++k)
            s += sin(2.0 * kCaePi * k * f0 * i / kCaeRate) / k;
        v[static_cast<size_t>(i)] = static_cast<int16_t>(kCaeVoiceAmp * 0.5 * env * s);
    }
    return v;
}

//一段采样的均方根
double caeRms(const int16_t* p, size_t n)
{
    if (n == 0)
        return 0;
    double energy = 0;
    for (size_t i = 0; i < n; ++i)
        energy += static_cast<double>(p[i]) * p[i];
    return sqrt(energy / n);
}

/**
 *  模拟一通真实通话：两个引擎都按 CallMediaSession 的方式打开回声消除与降噪（无设备模式），逐帧推进——
 *  各自喂入一帧采集、把对方新编出的包交给自己、再取出一帧播放（播放的这一帧同时成为自己的回声参考）。
 *  @param aIn  A 的「麦克风」输入（kCaeVoiceFrames 帧）
 *  @param bIn  B 的「麦克风」输入（同长）
 *  @param aOut [出参] A 播放出来的声音（即 B 传过来的）
 *  @param bOut [出参] B 播放出来的声音（即 A 传过来的）
 */
void caeRunCall(const std::vector<int16_t>& aIn, const std::vector<int16_t>& bIn,
                std::vector<int16_t>& aOut, std::vector<int16_t>& bOut)
{
    CallAudioEngine a;
    CallAudioEngine b;
    CaeSink sinkA;
    CaeSink sinkB;
    REQUIRE(a.StartWithoutDevices(&sinkA, true, true));
    REQUIRE(b.StartWithoutDevices(&sinkB, true, true));
    aOut.assign(aIn.size(), 0);
    bOut.assign(bIn.size(), 0);
    for (int f = 0; f < kCaeVoiceFrames; ++f)
    {
        const size_t off = static_cast<size_t>(f) * kCaeFrame;
        a.FeedCapture(&aIn[off], kCaeFrame);
        b.FeedCapture(&bIn[off], kCaeFrame);
        //这一帧双方各编出的包交给对方（网络无延迟、不丢包）；媒体时间戳 = 采集帧序号 × 每帧采样数
        for (size_t i = 0; i < sinkA.m_packets.size(); ++i)
            b.OnEncodedPacket(&sinkA.m_packets[i][0], sinkA.m_packets[i].size(),
                              static_cast<uint32_t>(sinkA.m_frameIndexes[i] * kCaeFrame));
        for (size_t i = 0; i < sinkB.m_packets.size(); ++i)
            a.OnEncodedPacket(&sinkB.m_packets[i][0], sinkB.m_packets[i].size(),
                              static_cast<uint32_t>(sinkB.m_frameIndexes[i] * kCaeFrame));
        sinkA.m_packets.clear();
        sinkB.m_packets.clear();
        sinkA.m_frameIndexes.clear();
        sinkB.m_frameIndexes.clear();
        a.PullRender(&aOut[off], kCaeFrame);
        b.PullRender(&bOut[off], kCaeFrame);
    }
    a.Stop();
    b.Stop();
}

} // namespace

TEST_CASE("CAE1 一对一通话路径：采集编码后喂回，播放端取出同一个音", "[audiomix]")
{
    CallAudioEngine engine;
    CaeSink sink;
    REQUIRE(engine.StartWithoutDevices(&sink, false, true));
    const int kFrames = 10;
    for (int f = 0; f < kFrames; ++f)
    {
        const std::vector<int16_t> pcm = caeTone(kCaeFrame, 1000.0, f * kCaeFrame);
        engine.FeedCapture(&pcm[0], kCaeFrame);
    }
    REQUIRE(sink.m_packets.size() == static_cast<size_t>(kFrames));
    //统计日志用的计数（2026-10-05）：送出的包数与 sink 收到的一致；无设备模式可查询
    CHECK(engine.EncodedPacketCount() == static_cast<uint64_t>(kFrames));
    CHECK(engine.IsWithoutDevices());
    for (size_t i = 0; i < sink.m_packets.size(); ++i)
        engine.OnEncodedPacket(&sink.m_packets[i][0], sink.m_packets[i].size(),
                               static_cast<uint32_t>(sink.m_frameIndexes[i] * kCaeFrame));
    std::vector<int16_t> out(static_cast<size_t>(kCaeFrame) * kFrames);
    engine.PullRender(&out[0], static_cast<int>(out.size()));
    const int half = static_cast<int>(out.size()) / 2;
    CHECK(caeGoertzel(&out[half], half, 1000.0) > 10.0 * caeGoertzel(&out[half], half, 2500.0));
    uint64_t requested = 0;
    uint64_t concealed = 0;
    CHECK(engine.GetPlayoutCounters(CallAudioEngine::kDefaultStreamId, requested, concealed));
    CHECK(requested == out.size());
    engine.Stop();
}

TEST_CASE("CAE2 多路混音与按路电平", "[audiomix]")
{
    CallAudioEngine engine;
    CaeSink sink;
    REQUIRE(engine.StartWithoutDevices(&sink, false, true));
    const int kFrames = 10;
    for (int f = 0; f < kFrames; ++f)
    {
        const std::vector<int16_t> a = caeTone(kCaeFrame, 1000.0, f * kCaeFrame);
        engine.FeedCapture(&a[0], kCaeFrame);
    }
    std::vector<std::vector<unsigned char> > first = sink.m_packets;
    sink.m_packets.clear();
    for (int f = 0; f < kFrames; ++f)
    {
        const std::vector<int16_t> b = caeTone(kCaeFrame, 2500.0, f * kCaeFrame);
        engine.FeedCapture(&b[0], kCaeFrame);
    }
    REQUIRE(first.size() == static_cast<size_t>(kFrames));
    REQUIRE(sink.m_packets.size() == static_cast<size_t>(kFrames));
    for (int f = 0; f < kFrames; ++f)
    {
        const uint32_t ts = static_cast<uint32_t>(f * kCaeFrame);
        engine.OnEncodedPacketFrom(11, &first[f][0], first[f].size(), ts);
        engine.OnEncodedPacketFrom(22, &sink.m_packets[f][0], sink.m_packets[f].size(), ts);
    }
    std::vector<int16_t> out(static_cast<size_t>(kCaeFrame) * kFrames);
    engine.PullRender(&out[0], static_cast<int>(out.size()));
    const int half = static_cast<int>(out.size()) / 2;
    const double noise = caeGoertzel(&out[half], half, 4000.0);
    CHECK(caeGoertzel(&out[half], half, 1000.0) > 10.0 * noise);
    CHECK(caeGoertzel(&out[half], half, 2500.0) > 10.0 * noise);
    int level = 0;
    CHECK(engine.GetStreamLevel(11, level));
    CHECK(level > 1000);
    CHECK_FALSE(engine.GetStreamLevel(33, level));
    engine.RemoveStream(11);
    CHECK_FALSE(engine.GetStreamLevel(11, level));
    engine.Stop();
}

TEST_CASE("CAE3 关闭采集后不再编码送出，采集电平随输入变化", "[audiomix]")
{
    CallAudioEngine engine;
    CaeSink sink;
    REQUIRE(engine.StartWithoutDevices(&sink, false, true));
    const std::vector<int16_t> tone = caeTone(kCaeFrame, 1000.0, 0);
    engine.FeedCapture(&tone[0], kCaeFrame);
    CHECK(sink.m_packets.size() == 1);
    CHECK(engine.CaptureLevel() > 1000);

    engine.SetCaptureEnabled(false);
    CHECK_FALSE(engine.IsCaptureEnabled());
    engine.FeedCapture(&tone[0], kCaeFrame);
    CHECK(sink.m_packets.size() == 1);
    CHECK(engine.CaptureLevel() == 0);

    engine.SetCaptureEnabled(true);
    engine.FeedCapture(&tone[0], kCaeFrame);
    CHECK(sink.m_packets.size() == 2);
    const std::vector<int16_t> silent(static_cast<size_t>(kCaeFrame), 0);
    engine.FeedCapture(&silent[0], kCaeFrame);
    CHECK(engine.CaptureLevel() == 0);
    engine.Stop();
}

TEST_CASE("CAE4 未启动时喂入与取出都安全", "[audiomix]")
{
    CallAudioEngine engine;
    const std::vector<int16_t> tone = caeTone(kCaeFrame, 1000.0, 0);
    engine.FeedCapture(&tone[0], kCaeFrame);
    engine.OnEncodedPacket(NULL, 0, 0);
    std::vector<int16_t> out(static_cast<size_t>(kCaeFrame), 5);
    engine.PullRender(&out[0], kCaeFrame);
    for (int i = 0; i < kCaeFrame; ++i)
        CHECK(out[i] == 0);
    CHECK_FALSE(engine.IsRunning());
}

TEST_CASE("CAE5 打开回声消除与降噪，A 说话 B 不说话：A 的声音完整到达 B", "[audiomix]")
{
    const std::vector<int16_t> aIn = caeVoice(kCaeVoiceFrames, 0.0);
    const std::vector<int16_t> bIn(aIn.size(), 0);
    std::vector<int16_t> aOut;
    std::vector<int16_t> bOut;
    caeRunCall(aIn, bIn, aOut, bOut);

    const size_t from = static_cast<size_t>(kCaeVoiceFrames - kCaeMeasureFrames) * kCaeFrame;
    const size_t n = static_cast<size_t>(kCaeMeasureFrames) * kCaeFrame;
    const double inRms = caeRms(&aIn[from], n);
    const double outRms = caeRms(&bOut[from], n);
    INFO("A input rms=" << inRms << ", B output rms=" << outRms);
    CHECK(outRms >= kCaeMinPassRatio * inRms);
    //B 不说话：A 那边播放出来的应接近静音
    CHECK(caeRms(&aOut[from], n) < kCaeMinPassRatio * inRms);
}

TEST_CASE("CAE6 打开回声消除与降噪，双方同时说话：两个方向都完整到达", "[audiomix]")
{
    const std::vector<int16_t> aIn = caeVoice(kCaeVoiceFrames, 0.0);
    const std::vector<int16_t> bIn = caeVoice(kCaeVoiceFrames, kCaeOtherVoiceShiftHz);
    std::vector<int16_t> aOut;
    std::vector<int16_t> bOut;
    caeRunCall(aIn, bIn, aOut, bOut);

    const size_t from = static_cast<size_t>(kCaeVoiceFrames - kCaeMeasureFrames) * kCaeFrame;
    const size_t n = static_cast<size_t>(kCaeMeasureFrames) * kCaeFrame;
    const double aInRms = caeRms(&aIn[from], n);
    const double bInRms = caeRms(&bIn[from], n);
    const double aOutRms = caeRms(&aOut[from], n);
    const double bOutRms = caeRms(&bOut[from], n);
    INFO("A in=" << aInRms << " B out=" << bOutRms << "; B in=" << bInRms << " A out=" << aOutRms);
    CHECK(bOutRms >= kCaeMinPassRatio * aInRms);
    CHECK(aOutRms >= kCaeMinPassRatio * bInRms);
}

TEST_CASE("CAE7 播放音量的增益曲线：0~100% 取平方、100%~150% 线性，超出范围按边界", "[audiomix][volume]")
{
    CHECK(CallAudioEngine::GainForVolume(0) == Approx(0.0));
    CHECK(CallAudioEngine::GainForVolume(50) == Approx(0.25));
    CHECK(CallAudioEngine::GainForVolume(100) == Approx(1.0));
    CHECK(CallAudioEngine::GainForVolume(125) == Approx(1.25));
    CHECK(CallAudioEngine::GainForVolume(150) == Approx(1.5));
    CHECK(CallAudioEngine::GainForVolume(-10) == Approx(0.0));
    CHECK(CallAudioEngine::GainForVolume(400) == Approx(1.5));
    CHECK(CallAudioEngine::kVolumeDefault == 100);
    CHECK(CallAudioEngine::kVolumeMax == 150);
}

namespace {

//CAE8：录制旁路，记下渲染线程交来的声音
class CaeTap : public CallAudioPcmTap
{
public:
    virtual void OnCapturedPcm(const int16_t* /*mono*/, int /*samples*/) override
    {
    }

    virtual void OnRenderedPcm(const int16_t* mono, int samples) override
    {
        m_rendered.insert(m_rendered.end(), mono, mono + samples);
    }

    std::vector<int16_t> m_rendered;   //交来的全部渲染声音，按顺序
};

//CAE8 每次取样的帧数，以及其中跳过的开头帧数（Opus 解码起始的过渡段）
const int kCaeVolumeFrames = 12;
const int kCaeVolumeSkipFrames = 4;
//CAE8 比例的允许误差
const double kCaeVolumeTolerance = 0.05;

/**
 *  把一段 1 kHz 正弦经引擎编码、解码后按指定音量取出，返回后段的均方根；同时返回旁路收到的后段均方根。
 *  @param volume  播放音量（%）
 *  @param tapRms  [出参] 录制旁路收到的声音的均方根
 */
double caePlayAtVolume(int volume, double& tapRms)
{
    CallAudioEngine engine;
    CaeSink sink;
    CaeTap tap;
    REQUIRE(engine.StartWithoutDevices(&sink, false, true));
    engine.SetPcmTap(&tap);
    engine.SetPlaybackVolume(volume);
    for (int f = 0; f < kCaeVolumeFrames; ++f)
    {
        const std::vector<int16_t> pcm = caeTone(kCaeFrame, 1000.0, f * kCaeFrame);
        engine.FeedCapture(&pcm[0], kCaeFrame);
    }
    for (size_t i = 0; i < sink.m_packets.size(); ++i)
        engine.OnEncodedPacket(&sink.m_packets[i][0], sink.m_packets[i].size(),
                               static_cast<uint32_t>(sink.m_frameIndexes[i] * kCaeFrame));
    std::vector<int16_t> out(static_cast<size_t>(kCaeFrame) * kCaeVolumeFrames);
    engine.PullRender(&out[0], static_cast<int>(out.size()));
    engine.SetPcmTap(NULL);
    engine.Stop();
    const size_t from = static_cast<size_t>(kCaeVolumeSkipFrames) * kCaeFrame;
    const size_t n = out.size() - from;
    REQUIRE(tap.m_rendered.size() == out.size());
    tapRms = caeRms(&tap.m_rendered[from], n);
    return caeRms(&out[from], n);
}

} // namespace

TEST_CASE("CAE8 引擎播放音量：输出按增益缩放，0% 为静音；录制旁路收到的是调音量之前的声音", "[audiomix][volume]")
{
    double tapFull = 0;
    double tapHalf = 0;
    double tapBoost = 0;
    double tapMute = 0;
    const double full = caePlayAtVolume(CallAudioEngine::kVolumeDefault, tapFull);
    const double half = caePlayAtVolume(50, tapHalf);
    const double boost = caePlayAtVolume(CallAudioEngine::kVolumeMax, tapBoost);
    const double mute = caePlayAtVolume(0, tapMute);
    INFO("full=" << full << " half=" << half << " boost=" << boost << " mute=" << mute);
    REQUIRE(full > 1000.0);
    CHECK(half / full == Approx(0.25).margin(kCaeVolumeTolerance));
    CHECK(boost / full == Approx(1.5).margin(kCaeVolumeTolerance));
    CHECK(mute == Approx(0.0));
    //旁路不受音量影响：四次都与 100% 时的输出相同
    CHECK(tapFull == Approx(full).epsilon(kCaeVolumeTolerance));
    CHECK(tapHalf == Approx(full).epsilon(kCaeVolumeTolerance));
    CHECK(tapBoost == Approx(full).epsilon(kCaeVolumeTolerance));
    CHECK(tapMute == Approx(full).epsilon(kCaeVolumeTolerance));
    //默认音量为 100%
    CallAudioEngine fresh;
    CHECK(fresh.PlaybackVolume() == CallAudioEngine::kVolumeDefault);
}

namespace {

//TS1：连续喂入的帧数
const int kTsFrames = 5;
//TS2：说话、静音、再说话三段的帧数。静音段 2 秒，足够让 Opus 进入 DTX（约 200 毫秒后进入）
const int kTsToneFrames = 5;
const int kTsSilentFrames = 100;

} // namespace

TEST_CASE("TS1 采集帧序号：每处理一帧加 1，从 0 起，重新启动后清零", "[audiomix][jitterbuf]")
{
    CallAudioEngine engine;
    CaeSink sink;
    REQUIRE(engine.StartWithoutDevices(&sink, false, true));
    for (int f = 0; f < kTsFrames; ++f)
    {
        const std::vector<int16_t> pcm = caeTone(kCaeFrame, 1000.0, f * kCaeFrame);
        engine.FeedCapture(&pcm[0], kCaeFrame);
    }
    REQUIRE(sink.m_frameIndexes.size() == static_cast<size_t>(kTsFrames));
    for (int f = 0; f < kTsFrames; ++f)
        CHECK(sink.m_frameIndexes[static_cast<size_t>(f)] == static_cast<uint64_t>(f));
    engine.Stop();

    CaeSink sink2;
    REQUIRE(engine.StartWithoutDevices(&sink2, false, true));
    const std::vector<int16_t> pcm = caeTone(kCaeFrame, 1000.0, 0);
    engine.FeedCapture(&pcm[0], kCaeFrame);
    REQUIRE(sink2.m_frameIndexes.size() == 1);
    CHECK(sink2.m_frameIndexes[0] == 0);
    engine.Stop();
}

TEST_CASE("TS2 打开 DTX：静音期间没有送出的帧照样计入序号，恢复说话后的序号越过这段静音", "[audiomix][jitterbuf]")
{
    CallAudioEngine engine;
    CaeSink sink;
    engine.SetDtx(true);
    REQUIRE(engine.StartWithoutDevices(&sink, false, true));
    int fed = 0;
    for (int f = 0; f < kTsToneFrames; ++f, ++fed)
    {
        const std::vector<int16_t> pcm = caeTone(kCaeFrame, 1000.0, fed * kCaeFrame);
        engine.FeedCapture(&pcm[0], kCaeFrame);
    }
    const std::vector<int16_t> silent(static_cast<size_t>(kCaeFrame), 0);
    for (int f = 0; f < kTsSilentFrames; ++f, ++fed)
        engine.FeedCapture(&silent[0], kCaeFrame);
    const size_t beforeTalk = sink.m_frameIndexes.size();
    const std::vector<int16_t> pcm = caeTone(kCaeFrame, 1000.0, fed * kCaeFrame);
    engine.FeedCapture(&pcm[0], kCaeFrame);
    engine.Stop();

    //静音段里大部分帧没有送出
    INFO("packets sent=" << sink.m_frameIndexes.size() << " frames fed=" << (fed + 1));
    CHECK(beforeTalk < static_cast<size_t>(kTsToneFrames + kTsSilentFrames / 2));
    //恢复说话的那一帧序号等于此前喂入的总帧数，没有因 DTX 少算
    REQUIRE(sink.m_frameIndexes.size() == beforeTalk + 1);
    CHECK(sink.m_frameIndexes.back() == static_cast<uint64_t>(fed));
    //序号严格递增
    for (size_t i = 1; i < sink.m_frameIndexes.size(); ++i)
        CHECK(sink.m_frameIndexes[i] > sink.m_frameIndexes[i - 1]);
}
