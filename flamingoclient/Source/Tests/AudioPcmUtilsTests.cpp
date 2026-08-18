/**
 *  音频 PCM 工具单元测试（Suite APU），AudioPcmUtilsTests.cpp
 *  覆盖：
 *    - 浮点↔int16 转换：往返近似还原；超幅浮点夹取到 int16 边界不回绕；
 *    - 立体声降单声道：取左右平均；接近满幅时用 int32 求和不溢出；
 *    - 单声道升立体声：左右同值复制；
 *    - PcmRingBuffer：FIFO 顺序、环回绕、写满只写部分(overrun)、读空只读部分(underrun)、
 *      Available/Capacity/Clear。
 *  框架 Catch2 v3。本组为纯逻辑，不触及设备/网络。
 *  balloonwj@qq.com   2026-06-12
 */
#include "catch_amalgamated.hpp"
#include "../Feature/Call/AudioPcmUtils.h"

#include <vector>
#include <stdint.h>

using namespace call;

TEST_CASE("APU1 浮点↔int16 转换：往返近似还原、超幅夹取")
{
    //往返：int16 → float → int16 应基本还原（仅舍入级误差）
    int16_t src[] = { 0, 1, -1, 1000, -1000, 30000, -30000, 32767, -32768 };
    const size_t n = sizeof(src) / sizeof(src[0]);
    std::vector<float> f(n);
    std::vector<int16_t> back(n);
    Int16ToFloat(src, f.data(), n);
    FloatToInt16(f.data(), back.data(), n);
    for (size_t i = 0; i < n; ++i)
        CHECK(std::abs(static_cast<int>(back[i]) - static_cast<int>(src[i])) <= 1);

    //超幅浮点：> 1.0 夹到 32767，< -1.0 夹到 -32768（不回绕）
    float over[] = { 2.0f, -2.0f, 1.5f, -1.5f, 1.0f, -1.0f };
    int16_t out[6];
    FloatToInt16(over, out, 6);
    CHECK(out[0] == 32767);
    CHECK(out[1] == -32768);
    CHECK(out[2] == 32767);
    CHECK(out[3] == -32768);
    CHECK(out[4] == 32767);   // 1.0*32768=32768 → 夹到 32767
    CHECK(out[5] == -32768);  // -1.0*32768=-32768 → 边界
}

TEST_CASE("APU2 立体声降单声道：取平均、接近满幅不溢出")
{
    //普通平均
    int16_t stereo[] = { 100, 200, -100, 100, 1000, -1000 };  // 3 帧
    int16_t mono[3];
    DownmixStereoToMonoI16(stereo, mono, 3);
    CHECK(mono[0] == 150);    // (100+200)/2
    CHECK(mono[1] == 0);      // (-100+100)/2
    CHECK(mono[2] == 0);      // (1000-1000)/2

    //两路都接近满幅：(32767+32767)/2 ≈ 32767，不能因 int16 加法溢出变负
    int16_t hot[] = { 32767, 32767, -32768, -32768 };  // 2 帧
    int16_t hotMono[2];
    DownmixStereoToMonoI16(hot, hotMono, 2);
    CHECK(hotMono[0] == 32767);
    CHECK(hotMono[1] == -32768);

    //float 版平均
    float fstereo[] = { 0.5f, 0.5f, 0.2f, -0.2f };  // 2 帧
    float fmono[2];
    DownmixStereoToMonoF32(fstereo, fmono, 2);
    CHECK(fmono[0] == Catch::Approx(0.5f));
    CHECK(fmono[1] == Catch::Approx(0.0f));
}

TEST_CASE("APU3 单声道升立体声：左右同值复制")
{
    int16_t mono[] = { 10, -20, 30 };
    int16_t stereo[6];
    MonoToStereoI16(mono, stereo, 3);
    CHECK(stereo[0] == 10);
    CHECK(stereo[1] == 10);
    CHECK(stereo[2] == -20);
    CHECK(stereo[3] == -20);
    CHECK(stereo[4] == 30);
    CHECK(stereo[5] == 30);
}

TEST_CASE("APU4 环形缓冲：FIFO 顺序")
{
    PcmRingBuffer rb(16);
    CHECK(rb.Capacity() == 16);
    CHECK(rb.Available() == 0);

    int16_t in[] = { 1, 2, 3, 4, 5 };
    CHECK(rb.Push(in, 5) == 5);
    CHECK(rb.Available() == 5);

    int16_t out[5] = { 0 };
    CHECK(rb.Pop(out, 5) == 5);
    CHECK(rb.Available() == 0);
    for (int i = 0; i < 5; ++i)
        CHECK(out[i] == in[i]);
}

TEST_CASE("APU5 环形缓冲：环回绕")
{
    PcmRingBuffer rb(8);
    int16_t a[] = { 1, 2, 3, 4, 5, 6 };
    rb.Push(a, 6);
    int16_t tmp[4];
    rb.Pop(tmp, 4);                 // 读走 1..4，head 前移，留 5,6
    int16_t b[] = { 7, 8, 9, 10 };
    CHECK(rb.Push(b, 4) == 4);      // 写 7..10，绕过缓冲尾回到头
    CHECK(rb.Available() == 6);     // 剩 5,6,7,8,9,10

    int16_t out[6];
    CHECK(rb.Pop(out, 6) == 6);
    int16_t expect[] = { 5, 6, 7, 8, 9, 10 };
    for (int i = 0; i < 6; ++i)
        CHECK(out[i] == expect[i]);
}

TEST_CASE("APU6 环形缓冲：写满只写部分、读空只读部分")
{
    PcmRingBuffer rb(4);
    int16_t in[] = { 1, 2, 3, 4, 5, 6 };
    //容量 4，推 6 个只写下 4 个
    CHECK(rb.Push(in, 6) == 4);
    CHECK(rb.Available() == 4);
    //满了再推写入 0 个
    CHECK(rb.Push(in, 2) == 0);

    int16_t out[10] = { 0 };
    //只有 4 个，请求读 10 个只读出 4 个
    CHECK(rb.Pop(out, 10) == 4);
    CHECK(rb.Available() == 0);
    for (int i = 0; i < 4; ++i)
        CHECK(out[i] == in[i]);
    //空了再读读出 0 个
    CHECK(rb.Pop(out, 4) == 0);
}

TEST_CASE("APU7 环形缓冲：Clear 丢弃未读")
{
    PcmRingBuffer rb(8);
    int16_t in[] = { 1, 2, 3 };
    rb.Push(in, 3);
    CHECK(rb.Available() == 3);
    rb.Clear();
    CHECK(rb.Available() == 0);
    //Clear 后仍可正常写读
    CHECK(rb.Push(in, 3) == 3);
    CHECK(rb.Available() == 3);
}
