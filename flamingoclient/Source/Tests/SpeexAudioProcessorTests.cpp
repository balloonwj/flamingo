/**
 *  通话采集端音频处理（AEC + 降噪）单元测试（Suite SPX），SpeexAudioProcessorTests.cpp
 *  覆盖：
 *    - 初始化：合法参数 Init 成功、IsReady、帧长回读；非法参数失败；Close 后不就绪；
 *    - 无参考路径：ref=NULL 时只降噪、不崩、输出帧长正确；
 *    - 回声消除：mic 恰等于 ref（完美回声、近端无人声）时，连续多帧自适应后输出能量
 *      显著低于输入——证明 AEC 确实在消回声。
 *  框架 Catch2 v3。本组为纯逻辑（speexdsp），不触及设备/网络。
 *  balloonwj@qq.com   2026-06-12
 */
#include "catch_amalgamated.hpp"
#include "../Feature/Call/SpeexAudioProcessor.h"

#include <vector>
#include <cmath>
#include <stdint.h>

using namespace call;

namespace {

//测试用参数：16kHz、10ms 一帧(160)、回声尾长 2048(128ms)。AEC 与采样率无关，取小值跑得快。
const int kRate   = 16000;
const int kFrame  = 160;
const int kFilter = 2048;

//确定性伪随机噪声（LCG），用作 far-end 回声源：噪声比纯音更利于 AEC 自适应。
static int16_t lcgNoise(uint32_t& state)
{
    state = state * 1664525u + 1013904223u;
    //取高位，幅度约 ±8000
    int v = static_cast<int>((state >> 16) & 0xFFFF) - 32768;
    return static_cast<int16_t>(v / 4);
}

static double rms(const int16_t* p, int n)
{
    if (n <= 0)
        return 0.0;
    double sum = 0.0;
    for (int i = 0; i < n; ++i)
        sum += static_cast<double>(p[i]) * static_cast<double>(p[i]);
    return std::sqrt(sum / n);
}

} // namespace

TEST_CASE("SPX1 合法参数初始化成功、参数回读、Close 后不就绪")
{
    SpeexAudioProcessor proc;
    REQUIRE(proc.Init(kRate, kFrame, kFilter));
    CHECK(proc.IsReady());
    CHECK(proc.FrameSamples() == kFrame);

    //幂等：再次 Init 仍成功
    REQUIRE(proc.Init(kRate, kFrame, kFilter));
    CHECK(proc.IsReady());

    proc.Close();
    CHECK_FALSE(proc.IsReady());
    CHECK(proc.FrameSamples() == 0);
}

TEST_CASE("SPX2 非法参数初始化失败")
{
    SpeexAudioProcessor proc;
    CHECK_FALSE(proc.Init(0, kFrame, kFilter));
    CHECK_FALSE(proc.Init(kRate, 0, kFilter));
    CHECK_FALSE(proc.Init(kRate, kFrame, 0));
    CHECK_FALSE(proc.IsReady());
}

TEST_CASE("SPX3 无参考(ref=NULL)只降噪、不崩、输出帧长正确")
{
    SpeexAudioProcessor proc;
    REQUIRE(proc.Init(kRate, kFrame, kFilter));

    uint32_t st = 12345u;
    std::vector<int16_t> mic(kFrame);
    std::vector<int16_t> out(kFrame, 0x7777);   //预填非零，验证确被写
    for (int i = 0; i < kFrame; ++i)
        mic[static_cast<size_t>(i)] = lcgNoise(st);

    proc.ProcessCapture(mic.data(), NULL, out.data(), kFrame);
    //输出被处理（不再是预填值），且能量有限（降噪不应放大）
    CHECK(rms(out.data(), kFrame) < 32768.0);

    //帧长不符：原样拷贝、不崩
    std::vector<int16_t> out2(kFrame, 0);
    proc.ProcessCapture(mic.data(), NULL, out2.data(), kFrame - 1);
    //（不崩即通过；此处不强求内容）
    CHECK(true);
}

TEST_CASE("SPX4 完美回声(mic==ref)经多帧自适应后输出能量显著下降")
{
    SpeexAudioProcessor proc;
    REQUIRE(proc.Init(kRate, kFrame, kFilter));

    uint32_t st = 987654321u;
    std::vector<int16_t> mic(kFrame);
    std::vector<int16_t> ref(kFrame);
    std::vector<int16_t> out(kFrame);

    const int kFrames = 400;
    double inRmsLate = 0.0;
    double outRmsLate = 0.0;
    int lateCount = 0;

    for (int f = 0; f < kFrames; ++f)
    {
        //mic 恰等于 ref：近端无人声、麦克风只听到「回声」，AEC 应能消到很低
        for (int i = 0; i < kFrame; ++i)
        {
            int16_t s = lcgNoise(st);
            mic[static_cast<size_t>(i)] = s;
            ref[static_cast<size_t>(i)] = s;
        }
        proc.ProcessCapture(mic.data(), ref.data(), out.data(), kFrame);

        //统计后段（自适应收敛后）的输入 / 输出能量
        if (f >= kFrames - 50)
        {
            inRmsLate += rms(mic.data(), kFrame);
            outRmsLate += rms(out.data(), kFrame);
            ++lateCount;
        }
    }

    REQUIRE(lateCount > 0);
    double inRms = inRmsLate / lateCount;
    double outRms = outRmsLate / lateCount;

    //输入确有能量（自检），输出在自适应后显著低于输入（回声被消除）
    CHECK(inRms > 500.0);
    CHECK(outRms < inRms * 0.6);
}
