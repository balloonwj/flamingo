/**
 *  单声道音频重采样单元测试（Suite SPR），SpeexResamplerTests.cpp
 *  覆盖：
 *    - 初始化：合法参数 Init 成功、IsReady、速率回读；非法参数失败；Close 后不就绪；
 *    - 升采样 24000→48000：输出样本数约为输入 2 倍、非静音；
 *    - 降采样 48000→16000：输出样本数约为输入 1/3、非静音。
 *  框架 Catch2 v3。本组为纯逻辑（speexdsp），不触及设备/网络。
 *  balloonwj@qq.com   2026-06-12
 */
#include "catch_amalgamated.hpp"
#include "../Call/SpeexResampler.h"

#include <vector>
#include <cmath>
#include <stdint.h>

using namespace call;

namespace {

//生成一段 freqHz 正弦波(16 位 PCH)。
static std::vector<int16_t> makeSine(int freqHz, int sampleRate, int count, double amp)
{
    std::vector<int16_t> v(static_cast<size_t>(count));
    const double twoPi = 6.283185307179586;
    double step = twoPi * freqHz / sampleRate;
    double ph = 0.0;
    for (int i = 0; i < count; ++i)
    {
        v[static_cast<size_t>(i)] = static_cast<int16_t>(amp * std::sin(ph));
        ph += step;
        if (ph > twoPi)
            ph -= twoPi;
    }
    return v;
}

static double rms(const std::vector<int16_t>& v)
{
    if (v.empty())
        return 0.0;
    double s = 0.0;
    for (size_t i = 0; i < v.size(); ++i)
        s += static_cast<double>(v[i]) * static_cast<double>(v[i]);
    return std::sqrt(s / v.size());
}

const int kQuality = 5;

} // namespace

TEST_CASE("SPR1 初始化成功、速率回读、Close 后不就绪")
{
    SpeexResampler r;
    REQUIRE(r.Init(44100, 48000, kQuality));
    CHECK(r.IsReady());
    CHECK(r.InRate() == 44100);
    CHECK(r.OutRate() == 48000);

    //幂等：再次 Init 仍成功
    REQUIRE(r.Init(24000, 48000, kQuality));
    CHECK(r.OutRate() == 48000);

    r.Close();
    CHECK_FALSE(r.IsReady());
}

TEST_CASE("SPR2 非法参数初始化失败")
{
    SpeexResampler r;
    CHECK_FALSE(r.Init(0, 48000, kQuality));
    CHECK_FALSE(r.Init(48000, 0, kQuality));
    CHECK_FALSE(r.IsReady());
}

TEST_CASE("SPR3 升采样 24000->48000：输出约 2 倍、非静音")
{
    SpeexResampler r;
    REQUIRE(r.Init(24000, 48000, kQuality));

    const int inCount = 12000;   // 0.5 秒
    std::vector<int16_t> in = makeSine(440, 24000, inCount, 8000.0);

    std::vector<int16_t> out;
    r.Process(in.data(), inCount, out);

    //期望输出 ≈ 输入 × 48000/24000 = 2 倍；容差覆盖滤波器预热延迟
    double expected = static_cast<double>(inCount) * 48000.0 / 24000.0;
    CHECK(std::abs(static_cast<double>(out.size()) - expected) < expected * 0.05 + 64.0);
    //非静音：重采样后仍有能量
    CHECK(rms(out) > 1000.0);
}

TEST_CASE("SPR4 降采样 48000->16000：输出约 1/3、非静音")
{
    SpeexResampler r;
    REQUIRE(r.Init(48000, 16000, kQuality));

    const int inCount = 12000;
    std::vector<int16_t> in = makeSine(440, 48000, inCount, 8000.0);

    std::vector<int16_t> out;
    r.Process(in.data(), inCount, out);

    double expected = static_cast<double>(inCount) * 16000.0 / 48000.0;
    CHECK(std::abs(static_cast<double>(out.size()) - expected) < expected * 0.05 + 64.0);
    CHECK(rms(out) > 1000.0);
}
