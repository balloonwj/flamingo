/**
 *  多路语音混音器与发言判定的单元测试（Suite ASM，标签 [audiomix]），AudioStreamMixerTests.cpp
 *
 *  覆盖 FlamingoMedia/Call/AudioStreamMixer（docs/屏幕共享与录制方案.md 第 4 期 4a、4c）：
 *    - ASM1 一路 PCM 原样混出，不足的部分补静音；没有任何一路时输出静音；
 *    - ASM2 两路叠加，超出 16 位范围时限幅；
 *    - ASM3 推入 Opus 包：用另一个编码器编出 1 kHz 正弦，解码混出后能量与频率都对；中间少一个包时补出来，不出错；
 *    - ASM4 删除一路后它不再参与混音，路号列表随之变化；
 *    - ASM5 电平：响的一路电平高于轻的一路，静音的一路电平为 0，不存在的路查询失败；
 *    - ASD1 发言判定：超过门限即判为在说话，降下去后保持一段时间才判为停止；
 *    - ASM6 播放计数（2026-10-05，网络状况标识第 1 期）：一路只推入半帧时，混音一帧后该路请求一帧、补静音半帧；
 *           不存在的路查询失败。
 *  2026-10-06 起编码包走抖动缓冲（docs/音视频通话方案.md 第 9.9 节方案 B 第 B3 步，标签另加 [jitterbuf]）：
 *    - AMB1 编码包均匀到达：输出连续、不补帧；PushEncoded 不接受空包；
 *    - AMB2 缺包：连续缺失 kMaxConcealFrames 帧以内用丢包补偿（有声音），再多补静音，补帧计数等于缺失的采样数；
 *    - AMB3 时间线重置：缓冲已空时发送端的时间戳从 0 重新开始，新一段照常播出，开头从上一个采样平滑过渡；
 *    - AMB4 多路互不影响：一路缺包时另一路不补帧。
 *    - AMB6（B4）每一路的播放统计：放入与取出的包数、缺失次数、丢包补偿与补静音的帧数、缓冲深度的测量次数；
 *    - AMB5 解码挪到播放线程后的耗时：4 路同时说话，混音一帧（含解码）的平均耗时远小于声卡周期，实测值用 WARN 输出。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-03
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <math.h>
#include <stdlib.h>
#include <vector>

#include "../../FlamingoMedia/Call/AudioStreamMixer.h"
#include "../../FlamingoMedia/Call/OpusAudioCodec.h"

using namespace call;

namespace {

//内部采样率与 20 ms 一帧的采样数
const int kAsmRate = 48000;
const int kAsmFrame = 960;
//测试用正弦的频率（Hz）与振幅
const double kAsmToneHz = 1000.0;
const double kAsmToneAmp = 8000.0;
const double kAsmPi = 3.14159265358979323846;

std::vector<int16_t> asmTone(int samples, double amp, int offset)
{
    std::vector<int16_t> v(static_cast<size_t>(samples));
    for (int i = 0; i < samples; ++i)
        v[i] = static_cast<int16_t>(amp * sin(2.0 * kAsmPi * kAsmToneHz * (i + offset) / kAsmRate));
    return v;
}

double asmRms(const int16_t* p, int n)
{
    double sum = 0;
    for (int i = 0; i < n; ++i)
        sum += static_cast<double>(p[i]) * p[i];
    return n > 0 ? sqrt(sum / n) : 0;
}

//Goertzel：某个频率上的能量（相对值）
double asmGoertzel(const int16_t* p, int n, double hz)
{
    const double k = 2.0 * cos(2.0 * kAsmPi * hz / kAsmRate);
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

} // namespace

TEST_CASE("ASM1 一路 PCM 原样混出，不足补静音", "[audiomix]")
{
    AudioStreamMixer mixer(kAsmRate, kAsmFrame);
    std::vector<int16_t> out(100, 7);
    mixer.Mix(&out[0], 100);
    for (size_t i = 0; i < out.size(); ++i)
        CHECK(out[i] == 0);

    std::vector<int16_t> ramp(60);
    for (int i = 0; i < 60; ++i)
        ramp[i] = static_cast<int16_t>(i * 10 - 300);
    mixer.PushPcm(3, &ramp[0], ramp.size());
    mixer.Mix(&out[0], 100);
    for (int i = 0; i < 60; ++i)
        CHECK(out[i] == ramp[i]);
    for (int i = 60; i < 100; ++i)
        CHECK(out[i] == 0);
}

TEST_CASE("ASM2 两路叠加，超出范围时限幅", "[audiomix]")
{
    AudioStreamMixer mixer(kAsmRate, kAsmFrame);
    const int16_t a[4] = { 1000, 30000, -30000, 5 };
    const int16_t b[4] = { -400, 10000, -10000, 7 };
    mixer.PushPcm(1, a, 4);
    mixer.PushPcm(2, b, 4);
    int16_t out[4] = { 0 };
    mixer.Mix(out, 4);
    CHECK(out[0] == 600);
    CHECK(out[1] == 32767);
    CHECK(out[2] == -32768);
    CHECK(out[3] == 12);
}

TEST_CASE("ASM3 推入 Opus 包解码混音", "[audiomix]")
{
    OpusAudioCodec enc;
    REQUIRE(enc.Init(kAsmRate, kAsmFrame, 32000));
    AudioStreamMixer mixer(kAsmRate, kAsmFrame);
    const int kFrames = 10;
    for (int f = 0; f < kFrames; ++f)
    {
        const std::vector<int16_t> pcm = asmTone(kAsmFrame, kAsmToneAmp, f * kAsmFrame);
        std::vector<unsigned char> packet;
        REQUIRE(enc.Encode(&pcm[0], kAsmFrame, packet));
        //中间少一个包（第 2 帧，放在下面统计电平的后半段之外），播放到那里时由丢包补偿补上
        if (f != 2)
            REQUIRE(mixer.PushEncoded(9, &packet[0], packet.size(), static_cast<uint32_t>(f * kAsmFrame)));
    }
    std::vector<int16_t> out(static_cast<size_t>(kAsmFrame) * kFrames);
    mixer.Mix(&out[0], static_cast<int>(out.size()));
    //跳过编解码器起始的过渡段，看后半段
    const int half = static_cast<int>(out.size()) / 2;
    const double rms = asmRms(&out[half], half);
    CHECK(rms > kAsmToneAmp * 0.5);
    CHECK(asmGoertzel(&out[half], half, kAsmToneHz) > 10.0 * asmGoertzel(&out[half], half, kAsmToneHz * 3.3));
}

TEST_CASE("ASM4 删除一路后不再参与混音", "[audiomix]")
{
    AudioStreamMixer mixer(kAsmRate, kAsmFrame);
    const int16_t a[2] = { 100, 100 };
    const int16_t b[2] = { 20, 20 };
    mixer.PushPcm(5, a, 2);
    mixer.PushPcm(2, b, 2);
    std::vector<int> ids = mixer.StreamIds();
    REQUIRE(ids.size() == 2);
    CHECK(ids[0] == 2);
    CHECK(ids[1] == 5);
    mixer.RemoveStream(5);
    int16_t out[2] = { 0 };
    mixer.Mix(out, 2);
    CHECK(out[0] == 20);
    ids = mixer.StreamIds();
    REQUIRE(ids.size() == 1);
    CHECK(ids[0] == 2);
    mixer.Clear();
    CHECK(mixer.StreamIds().empty());
}

TEST_CASE("ASM5 各路电平", "[audiomix]")
{
    AudioStreamMixer mixer(kAsmRate, kAsmFrame);
    const std::vector<int16_t> loud = asmTone(kAsmFrame, 12000.0, 0);
    const std::vector<int16_t> quiet = asmTone(kAsmFrame, 300.0, 0);
    const std::vector<int16_t> silent(static_cast<size_t>(kAsmFrame), 0);
    mixer.PushPcm(1, &loud[0], loud.size());
    mixer.PushPcm(2, &quiet[0], quiet.size());
    mixer.PushPcm(3, &silent[0], silent.size());
    std::vector<int16_t> out(static_cast<size_t>(kAsmFrame));
    mixer.Mix(&out[0], kAsmFrame);
    int l1 = -1;
    int l2 = -1;
    int l3 = -1;
    REQUIRE(mixer.GetLevel(1, l1));
    REQUIRE(mixer.GetLevel(2, l2));
    REQUIRE(mixer.GetLevel(3, l3));
    CHECK(l1 > 8000);
    CHECK(l2 > 100);
    CHECK(l2 < 400);
    CHECK(l3 == 0);
    int none = -1;
    CHECK_FALSE(mixer.GetLevel(42, none));
    //该路这一次没有数据：电平按静音算
    mixer.Mix(&out[0], kAsmFrame);
    REQUIRE(mixer.GetLevel(1, l1));
    CHECK(l1 == 0);
}

TEST_CASE("ASD1 发言判定带保持时间", "[audiomix]")
{
    SpeakingDetector d(500, 300);
    CHECK_FALSE(d.IsSpeaking());
    CHECK_FALSE(d.Update(100, 0));
    CHECK(d.Update(2000, 20));
    CHECK(d.IsSpeaking());
    //降到门限以下，保持 300 毫秒
    CHECK(d.Update(50, 100));
    CHECK(d.Update(50, 319));
    CHECK_FALSE(d.Update(50, 321));
    CHECK_FALSE(d.IsSpeaking());
    //再次超过门限
    CHECK(d.Update(600, 400));
}

TEST_CASE("ASM6 播放计数：一路只推入半帧时，混音一帧后请求一帧、补静音半帧", "[audiomix][callnet]")
{
    AudioStreamMixer mixer(kAsmRate, kAsmFrame);
    const int half = kAsmFrame / 2;
    const std::vector<int16_t> pcm = asmTone(half, kAsmToneAmp, 0);
    mixer.PushPcm(1, &pcm[0], pcm.size());
    std::vector<int16_t> out(static_cast<size_t>(kAsmFrame));
    mixer.Mix(&out[0], kAsmFrame);

    uint64_t requested = 0;
    uint64_t concealed = 0;
    REQUIRE(mixer.GetPlayoutCounters(1, requested, concealed));
    CHECK(requested == static_cast<uint64_t>(kAsmFrame));
    CHECK(concealed == static_cast<uint64_t>(kAsmFrame - half));

    //再混一帧：缓冲已空，整帧补静音，计数累加
    mixer.Mix(&out[0], kAsmFrame);
    REQUIRE(mixer.GetPlayoutCounters(1, requested, concealed));
    CHECK(requested == static_cast<uint64_t>(2 * kAsmFrame));
    CHECK(concealed == static_cast<uint64_t>(2 * kAsmFrame - half));

    //不存在的路
    CHECK_FALSE(mixer.GetPlayoutCounters(2, requested, concealed));
}

namespace {

//AMB：编一段连续的 1 kHz 正弦，每帧一个包
std::vector<std::vector<unsigned char> > ambEncode(int frames)
{
    OpusAudioCodec enc;
    std::vector<std::vector<unsigned char> > packets;
    if (!enc.Init(kAsmRate, kAsmFrame, 32000))
        return packets;
    for (int f = 0; f < frames; ++f)
    {
        const std::vector<int16_t> pcm = asmTone(kAsmFrame, kAsmToneAmp, f * kAsmFrame);
        std::vector<unsigned char> packet;
        if (!enc.Encode(&pcm[0], kAsmFrame, packet))
            break;
        packets.push_back(packet);
    }
    return packets;
}

//AMB：第 f 帧的媒体时间戳（采样）
uint32_t ambTs(int f)
{
    return static_cast<uint32_t>(f * kAsmFrame);
}

//AMB：混出 frames 帧，每帧单独调用一次 Mix（与渲染线程按声卡周期取数一致）
std::vector<int16_t> ambMixFrames(AudioStreamMixer& mixer, int frames)
{
    std::vector<int16_t> out(static_cast<size_t>(frames) * kAsmFrame);
    for (int f = 0; f < frames; ++f)
        mixer.Mix(&out[static_cast<size_t>(f) * kAsmFrame], kAsmFrame);
    return out;
}

//AMB2：连续缺失的帧数（多于丢包补偿的上限 kMaxConcealFrames）与缺失的起点
const int kAmbLostFrom = 10;
const int kAmbLostCount = 5;
//AMB2：丢包补偿那几帧的电平下限（相对正弦幅度的比例）：补偿音会逐渐减弱，但第一帧应明显有声音
const double kAmbPlcMinRatio = 0.05;
//AMB3：平滑过渡后，新一段第一个采样与上一段最后一个采样之差的上限（正弦幅度的比例）
const double kAmbSmoothMaxStepRatio = 0.1;

} // namespace

TEST_CASE("AMB1 编码包均匀到达：输出连续、不补帧", "[audiomix][jitterbuf]")
{
    const int kFrames = 50;
    const std::vector<std::vector<unsigned char> > packets = ambEncode(kFrames);
    REQUIRE(packets.size() == static_cast<size_t>(kFrames));
    AudioStreamMixer mixer(kAsmRate, kAsmFrame);
    std::vector<int16_t> out(static_cast<size_t>(kFrames) * kAsmFrame);
    for (int f = 0; f < kFrames; ++f)
    {
        REQUIRE(mixer.PushEncoded(1, &packets[f][0], packets[f].size(), ambTs(f)));
        mixer.Mix(&out[static_cast<size_t>(f) * kAsmFrame], kAsmFrame);
    }
    uint64_t requested = 0;
    uint64_t concealed = 0;
    REQUIRE(mixer.GetPlayoutCounters(1, requested, concealed));
    CHECK(requested == static_cast<uint64_t>(kFrames) * kAsmFrame);
    CHECK(concealed == 0);
    const int half = static_cast<int>(out.size()) / 2;
    CHECK(asmRms(&out[half], half) > kAsmToneAmp * 0.5);
    //PushEncoded 不接受空包
    CHECK_FALSE(mixer.PushEncoded(1, NULL, 0, ambTs(kFrames)));
}

TEST_CASE("AMB2 缺包：连续缺失 3 帧以内用丢包补偿，再多补静音，补帧计数准确", "[audiomix][jitterbuf]")
{
    const int kFrames = 20;
    const std::vector<std::vector<unsigned char> > packets = ambEncode(kFrames);
    REQUIRE(packets.size() == static_cast<size_t>(kFrames));
    AudioStreamMixer mixer(kAsmRate, kAsmFrame);
    for (int f = 0; f < kFrames; ++f)
    {
        if (f >= kAmbLostFrom && f < kAmbLostFrom + kAmbLostCount)
            continue;
        REQUIRE(mixer.PushEncoded(1, &packets[f][0], packets[f].size(), ambTs(f)));
    }
    const std::vector<int16_t> out = ambMixFrames(mixer, kFrames);
    uint64_t requested = 0;
    uint64_t concealed = 0;
    REQUIRE(mixer.GetPlayoutCounters(1, requested, concealed));
    CHECK(concealed == static_cast<uint64_t>(kAmbLostCount) * kAsmFrame);
    //缺失的第 1 帧是丢包补偿：有声音
    const size_t plcStart = static_cast<size_t>(kAmbLostFrom) * kAsmFrame;
    CHECK(asmRms(&out[plcStart], kAsmFrame) > kAsmToneAmp * kAmbPlcMinRatio);
    //超过 kMaxConcealFrames 之后的帧是静音
    const size_t silentStart = static_cast<size_t>(kAmbLostFrom + kMaxConcealFrames) * kAsmFrame;
    for (int i = 0; i < kAsmFrame; ++i)
        CHECK(out[silentStart + static_cast<size_t>(i)] == 0);
    //缺失之后恢复：最后一帧有声音
    const size_t lastStart = static_cast<size_t>(kFrames - 1) * kAsmFrame;
    CHECK(asmRms(&out[lastStart], kAsmFrame) > kAsmToneAmp * 0.5);
}

TEST_CASE("AMB3 时间线重置：新一段开头从上一个采样平滑过渡", "[audiomix][jitterbuf]")
{
    const int kFrames = 10;
    const std::vector<std::vector<unsigned char> > packets = ambEncode(kFrames);
    REQUIRE(packets.size() == static_cast<size_t>(kFrames));
    AudioStreamMixer mixer(kAsmRate, kAsmFrame);
    for (int f = 0; f < kFrames; ++f)
        REQUIRE(mixer.PushEncoded(1, &packets[f][0], packets[f].size(), ambTs(f)));
    const std::vector<int16_t> first = ambMixFrames(mixer, kFrames);
    //缓冲已空，发送端的时间线从 0 重新开始（如对方重新打开麦克风）
    for (int f = 0; f < kFrames; ++f)
        REQUIRE(mixer.PushEncoded(1, &packets[f][0], packets[f].size(), ambTs(f)));
    const std::vector<int16_t> second = ambMixFrames(mixer, 1);
    const int step = abs(static_cast<int>(second[0]) - static_cast<int>(first.back()));
    INFO("last sample " << first.back() << ", next sample " << second[0]);
    CHECK(step < static_cast<int>(kAsmToneAmp * kAmbSmoothMaxStepRatio));
    //新一段确实播出来了（不是被当作迟到包丢掉、补了静音）
    uint64_t requested = 0;
    uint64_t concealed = 0;
    REQUIRE(mixer.GetPlayoutCounters(1, requested, concealed));
    CHECK(concealed == 0);
}

TEST_CASE("AMB4 多路互不影响：一路缺包不影响另一路", "[audiomix][jitterbuf]")
{
    const int kFrames = 20;
    const std::vector<std::vector<unsigned char> > packets = ambEncode(kFrames);
    REQUIRE(packets.size() == static_cast<size_t>(kFrames));
    AudioStreamMixer mixer(kAsmRate, kAsmFrame);
    for (int f = 0; f < kFrames; ++f)
    {
        REQUIRE(mixer.PushEncoded(1, &packets[f][0], packets[f].size(), ambTs(f)));
        if (f % 4 != 0)
            REQUIRE(mixer.PushEncoded(2, &packets[f][0], packets[f].size(), ambTs(f)));
    }
    ambMixFrames(mixer, kFrames);
    uint64_t requested = 0;
    uint64_t concealed1 = 0;
    uint64_t concealed2 = 0;
    REQUIRE(mixer.GetPlayoutCounters(1, requested, concealed1));
    REQUIRE(mixer.GetPlayoutCounters(2, requested, concealed2));
    CHECK(concealed1 == 0);
    CHECK(concealed2 > 0);
}

namespace {

//AMB5：测量的帧数（10 秒）与路数（群通话里同时说话的人数取 4）
const int kAmbTimingFrames = 500;
const int kAmbTimingStreams = 4;
//AMB5：每帧平均耗时的上限（毫秒）。声卡周期在 10 毫秒级，播放线程上混音一帧的开销应远小于它。2026-10-06 在
//Debug（未优化）版本上实测平均约 1.0 毫秒、最坏约 2.4 毫秒；上限取 5 毫秒，避免机器繁忙时偶然失败
const double kAmbMaxMsPerFrame = 5.0;
//每秒的毫秒数
const double kAmbMsPerSecond = 1000.0;

} // namespace

TEST_CASE("AMB5 解码挪到播放线程后的耗时：4 路同时说话，混音一帧的平均耗时远小于声卡周期", "[audiomix][jitterbuf]")
{
    const std::vector<std::vector<unsigned char> > packets = ambEncode(kAmbTimingFrames);
    REQUIRE(packets.size() == static_cast<size_t>(kAmbTimingFrames));
    AudioStreamMixer mixer(kAsmRate, kAsmFrame);
    std::vector<int16_t> out(static_cast<size_t>(kAsmFrame));
    LARGE_INTEGER freq;
    ::QueryPerformanceFrequency(&freq);
    double totalMs = 0;
    double worstMs = 0;
    for (int f = 0; f < kAmbTimingFrames; ++f)
    {
        for (int s = 0; s < kAmbTimingStreams; ++s)
            mixer.PushEncoded(s, &packets[f][0], packets[f].size(), ambTs(f));
        LARGE_INTEGER t0;
        LARGE_INTEGER t1;
        ::QueryPerformanceCounter(&t0);
        mixer.Mix(&out[0], kAsmFrame);
        ::QueryPerformanceCounter(&t1);
        const double ms = static_cast<double>(t1.QuadPart - t0.QuadPart) * kAmbMsPerSecond / freq.QuadPart;
        totalMs += ms;
        worstMs = (ms > worstMs) ? ms : worstMs;
    }
    const double avgMs = totalMs / kAmbTimingFrames;
    WARN("mix with decode, " << kAmbTimingStreams << " streams: avg " << avgMs << " ms/frame, worst " << worstMs
                             << " ms/frame");
    CHECK(avgMs < kAmbMaxMsPerFrame);
}

TEST_CASE("AMB6 每一路的播放统计：缺失、丢包补偿与补静音的帧数（2026-10-06 B4）", "[audiomix][jitterbuf]")
{
    const int kFrames = 20;
    const std::vector<std::vector<unsigned char> > packets = ambEncode(kFrames);
    REQUIRE(packets.size() == static_cast<size_t>(kFrames));
    AudioStreamMixer mixer(kAsmRate, kAsmFrame);
    for (int f = 0; f < kFrames; ++f)
    {
        if (f >= kAmbLostFrom && f < kAmbLostFrom + kAmbLostCount)
            continue;
        REQUIRE(mixer.PushEncoded(1, &packets[f][0], packets[f].size(), ambTs(f)));
    }
    ambMixFrames(mixer, kFrames);
    AudioStreamStats st;
    REQUIRE(mixer.GetStreamStats(1, st));
    CHECK(st.m_jb.m_puts == static_cast<uint64_t>(kFrames - kAmbLostCount));
    CHECK(st.m_jb.m_packets == static_cast<uint64_t>(kFrames - kAmbLostCount));
    CHECK(st.m_jb.m_missing == static_cast<uint64_t>(kAmbLostCount));
    CHECK(st.m_plcFrames == static_cast<uint64_t>(kMaxConcealFrames));
    CHECK(st.m_silenceFrames == static_cast<uint64_t>(kAmbLostCount - kMaxConcealFrames));
    CHECK(st.m_jb.m_bufferedCount == static_cast<uint64_t>(kFrames - kAmbLostCount));
    //不存在的路
    CHECK_FALSE(mixer.GetStreamStats(99, st));
}
