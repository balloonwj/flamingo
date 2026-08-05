/**
 *  Opus 语音编解码封装单元测试（Suite OAC），OpusAudioCodecTests.cpp
 *  覆盖：
 *    - 初始化：合法参数 Init 成功、IsReady、采样率/帧长回读正确；非法参数 Init 失败；
 *    - 编码：正弦波一帧能编出非空 Opus 包，包长在合理范围；帧长不符的输入安全失败；
 *    - 解码：包能解回一帧 PCM，长度为 frameSamples；
 *    - 端到端能量：连续多帧正弦波编码→解码后，稳态帧的能量(RMS)与输入同量级、非静音、
 *      不爆量——证明编解码链路真的在传递音频而非吐垃圾/静音（Opus 有损 + 算法延迟，
 *      故不逐采样比对，只校验能量量级）；
 *    - 丢包隐藏：传 NULL 包让 Opus 走 PLC，能填出一帧、不崩。
 *  框架 Catch2 v3。本组为纯逻辑，不触及设备/网络。
 *  balloonwj@qq.com   2026-06-12
 */
#include "catch_amalgamated.hpp"
#include "../Call/OpusAudioCodec.h"

#include <vector>
#include <cmath>
#include <stdint.h>

using namespace call;

namespace {

//通话音频管线的标准参数：48kHz 单声道、20ms 一帧(=960 采样)、24kbps。
const int kSampleRate   = 48000;
const int kFrameSamples = 960;     // 48000 * 20 / 1000
const int kBitrate      = 24000;

//生成一帧 freqHz 正弦波(16 位 PCM)，phase 为跨帧连续相位(按引用累进，避免帧间断裂)。
static std::vector<int16_t> makeSineFrame(int freqHz, int sampleRate, int frameSamples,
                                          double amplitude, double& phase)
{
    std::vector<int16_t> pcm(static_cast<size_t>(frameSamples));
    const double twoPi = 6.283185307179586;
    double step = twoPi * static_cast<double>(freqHz) / static_cast<double>(sampleRate);
    for (int i = 0; i < frameSamples; ++i)
    {
        double v = amplitude * std::sin(phase);
        pcm[static_cast<size_t>(i)] = static_cast<int16_t>(v);
        phase += step;
        if (phase > twoPi)
            phase -= twoPi;
    }
    return pcm;
}

//计算一帧 PCM 的 RMS(均方根能量)。
static double rms(const std::vector<int16_t>& pcm)
{
    if (pcm.empty())
        return 0.0;
    double sum = 0.0;
    for (size_t i = 0; i < pcm.size(); ++i)
    {
        double s = static_cast<double>(pcm[i]);
        sum += s * s;
    }
    return std::sqrt(sum / static_cast<double>(pcm.size()));
}

} // namespace

TEST_CASE("OAC1 合法参数初始化成功、参数回读正确")
{
    OpusAudioCodec codec;
    REQUIRE(codec.Init(kSampleRate, kFrameSamples, kBitrate));
    CHECK(codec.IsReady());
    CHECK(codec.SampleRate() == kSampleRate);
    CHECK(codec.FrameSamples() == kFrameSamples);

    //幂等：再次 Init 仍成功
    REQUIRE(codec.Init(kSampleRate, kFrameSamples, kBitrate));
    CHECK(codec.IsReady());

    //Close 后不再就绪
    codec.Close();
    CHECK_FALSE(codec.IsReady());
    CHECK(codec.SampleRate() == 0);
}

TEST_CASE("OAC2 非法参数初始化失败")
{
    OpusAudioCodec codec;
    CHECK_FALSE(codec.Init(0, kFrameSamples, kBitrate));     //采样率非法
    CHECK_FALSE(codec.Init(kSampleRate, 0, kBitrate));       //帧长非法
    CHECK_FALSE(codec.Init(44100, kFrameSamples, kBitrate)); //44100 非 Opus 支持采样率
    CHECK_FALSE(codec.IsReady());
}

TEST_CASE("OAC3 单帧编码产出非空包、解码还原一帧；帧长不符安全失败")
{
    OpusAudioCodec codec;
    REQUIRE(codec.Init(kSampleRate, kFrameSamples, kBitrate));

    double phase = 0.0;
    std::vector<int16_t> frame = makeSineFrame(440, kSampleRate, kFrameSamples, 8000.0, phase);

    std::vector<unsigned char> packet;
    REQUIRE(codec.Encode(frame.data(), kFrameSamples, packet));
    //Opus 包非空且在合理上限内
    CHECK(packet.size() > 0);
    CHECK(packet.size() < 4000);

    std::vector<int16_t> decoded;
    REQUIRE(codec.Decode(packet.data(), packet.size(), decoded));
    CHECK(static_cast<int>(decoded.size()) == kFrameSamples);

    //帧长不符：Encode 安全失败
    std::vector<unsigned char> bad;
    CHECK_FALSE(codec.Encode(frame.data(), kFrameSamples - 1, bad));
}

TEST_CASE("OAC4 连续多帧端到端：稳态帧能量与输入同量级、非静音")
{
    OpusAudioCodec codec;
    REQUIRE(codec.Init(kSampleRate, kFrameSamples, kBitrate));

    const int kNumFrames = 12;
    const double kAmp = 8000.0;
    double phase = 0.0;

    double inRmsAccum = 0.0;
    double outRmsAccum = 0.0;
    int counted = 0;

    for (int f = 0; f < kNumFrames; ++f)
    {
        std::vector<int16_t> in = makeSineFrame(440, kSampleRate, kFrameSamples, kAmp, phase);

        std::vector<unsigned char> pkt;
        REQUIRE(codec.Encode(in.data(), kFrameSamples, pkt));
        REQUIRE(pkt.size() > 0);

        std::vector<int16_t> out;
        REQUIRE(codec.Decode(pkt.data(), pkt.size(), out));
        REQUIRE(static_cast<int>(out.size()) == kFrameSamples);

        //跳过前几帧的编解码器预热 / 算法延迟，只统计稳态帧
        if (f >= 4)
        {
            inRmsAccum += rms(in);
            outRmsAccum += rms(out);
            ++counted;
        }
    }

    REQUIRE(counted > 0);
    double inRms = inRmsAccum / counted;
    double outRms = outRmsAccum / counted;

    //输入正弦 RMS ≈ amplitude/√2 ≈ 5657；解码输出应同量级：非静音、且不爆量
    CHECK(inRms > 1000.0);                 //输入确实有能量(自检)
    CHECK(outRms > inRms * 0.2);           //输出非静音、能量与输入同量级(下界)
    CHECK(outRms < inRms * 3.0);           //输出未异常放大(上界)
}

TEST_CASE("OAC5 丢包隐藏：传 NULL 包走 PLC 能填一帧、不崩")
{
    OpusAudioCodec codec;
    REQUIRE(codec.Init(kSampleRate, kFrameSamples, kBitrate));

    //先正常喂一帧，建立解码器状态(PLC 据历史外推)
    double phase = 0.0;
    std::vector<int16_t> in = makeSineFrame(440, kSampleRate, kFrameSamples, 8000.0, phase);
    std::vector<unsigned char> pkt;
    REQUIRE(codec.Encode(in.data(), kFrameSamples, pkt));
    std::vector<int16_t> out;
    REQUIRE(codec.Decode(pkt.data(), pkt.size(), out));

    //丢包：data=NULL,len=0 → Opus PLC 填一帧
    std::vector<int16_t> plc;
    REQUIRE(codec.Decode(NULL, 0, plc));
    CHECK(static_cast<int>(plc.size()) == kFrameSamples);
}
