/**
 *  通话抖动缓冲封装 CallJitterBuffer 的单元测试（标签 [jitterbuf]，docs/音视频通话方案.md 第 9.9 节方案 B 第 B2 步），
 *  CallJitterBufferTests.cpp
 *
 *  speexdsp 的抖动缓冲没有时钟，时间由取包的节拍推进：这里每调用一次 GetFrame 视为过了 20 毫秒，按事先排好的
 *  「到达时刻」放包，不真正等待。包的负载写入它的帧序号，取出后据此核对顺序。
 *    - JBB1 顺序到达：按顺序取出、不缺失不插入；还没收到包时取到的是一帧缺失（跨度不为 0）；
 *    - JBB2 乱序：在缓冲范围内乱序到达的包按时间戳顺序取出；
 *    - JBB3 丢包：中间少一个包，到点返回缺失，之后的包照常取出；
 *    - JBB4 卡顿后回落：稳定收发中网络卡住 200 / 500 毫秒、积压的包随后一次到达，测缓冲延迟能否在若干秒内回落；
 *    - JBB5 时间线重置：长时间没有包之后恢复；以及缓冲已空时收到时间戳明显落后（发送端序号没有前进）的包，
 *           不当作迟到包丢掉，而是重置时间线后正常取出；
 *    - JBB6 线程安全：一个线程放、一个线程取，取出的包时间戳严格递增、不重复；
 *    - JBB7（B4）正常播放时的缓冲深度累计：每取到一个包量一次，缺失时不计。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-06
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <string.h>
#include <atomic>
#include <sstream>
#include <thread>
#include <vector>

#include "../../FlamingoMedia/Call/CallJitterBuffer.h"

using namespace call;

namespace {

//一帧的采样数（20 毫秒，48 kHz）
const uint32_t kJbbFrame = static_cast<uint32_t>(kCallJbFrameSamples);
//每帧的毫秒数
const int kJbbFrameMs = 20;
//负载长度：写入一个 32 位帧序号
const size_t kJbbPayload = 4;
//JBB4：卡顿前稳定收发的帧数、卡顿之后观察的帧数（20 秒）
const int kJbbSteadyFrames = 100;
const int kJbbAfterFrames = 1000;
//JBB4：期望卡顿之后缓冲延迟最终回落到的上限（毫秒）
const int kJbbRecoveredMs = 120;
//JBB5：长时间没有包的帧数（1 秒，超过 speexdsp 连续缺失 20 帧自动重置的门槛）
const int kJbbLongGapFrames = 50;
//JBB6：放取的包数
const int kJbbThreadPackets = 500;

//把帧序号编成负载
void JbbPut(CallJitterBuffer& jb, uint32_t index)
{
    unsigned char payload[kJbbPayload];
    memcpy(payload, &index, sizeof(index));
    jb.Put(payload, sizeof(payload), index * kJbbFrame);
}

//从取到的包里读出帧序号；不是包时返回 -1
long long JbbIndexOf(const CallJbFrame& f)
{
    if (f.m_result != call_jb_packet || f.m_data.size() < kJbbPayload)
        return -1;
    uint32_t index = 0;
    memcpy(&index, &f.m_data[0], sizeof(index));
    return static_cast<long long>(index);
}

//一次「稳定收发 → 卡住 freezeFrames 帧 → 积压的包一次到达 → 继续稳定收发」的模拟，返回卡顿之后每一帧的缓冲延迟（毫秒）
std::vector<int> JbbFreezeRun(int freezeFrames)
{
    CallJitterBuffer jb;
    std::vector<int> delays;
    uint32_t next = 0;   //下一个要发出的帧序号（发送端每 20 毫秒发一个）
    CallJbFrame f;
    for (int t = 0; t < kJbbSteadyFrames; ++t)
    {
        JbbPut(jb, next++);
        jb.GetFrame(f);
    }
    //卡顿期间发送端照常发，但包都没到
    const uint32_t frozenFrom = next;
    for (int t = 0; t < freezeFrames; ++t)
    {
        ++next;
        jb.GetFrame(f);
    }
    //卡顿结束：积压的包一次到达
    for (uint32_t i = frozenFrom; i < next; ++i)
        JbbPut(jb, i);
    for (int t = 0; t < kJbbAfterFrames; ++t)
    {
        JbbPut(jb, next++);
        jb.GetFrame(f);
        delays.push_back(jb.BufferedSamples() * kJbbFrameMs / static_cast<int>(kJbbFrame));
    }
    return delays;
}

} // namespace

TEST_CASE("JBB1 顺序到达：按顺序取出；还没收到包时取到一帧缺失", "[jitterbuf]")
{
    CallJitterBuffer jb;
    CallJbFrame f;
    jb.GetFrame(f);
    CHECK(f.m_result == call_jb_missing);
    CHECK(f.m_span == kCallJbFrameSamples);

    const uint32_t count = 10;
    for (uint32_t i = 0; i < count; ++i)
        JbbPut(jb, i);
    for (uint32_t i = 0; i < count; ++i)
    {
        jb.GetFrame(f);
        INFO("frame " << i);
        REQUIRE(f.m_result == call_jb_packet);
        CHECK(JbbIndexOf(f) == static_cast<long long>(i));
        CHECK(f.m_span == kCallJbFrameSamples);
        CHECK(f.m_timestamp == i * kJbbFrame);
    }
    jb.GetFrame(f);
    CHECK(f.m_result == call_jb_missing);
    const CallJbStats st = jb.Stats();
    CHECK(st.m_puts == count);
    CHECK(st.m_packets == count);
}

TEST_CASE("JBB2 乱序到达：按时间戳顺序取出", "[jitterbuf]")
{
    CallJitterBuffer jb;
    const uint32_t order[] = { 0, 1, 3, 2, 5, 4 };
    for (size_t i = 0; i < sizeof(order) / sizeof(order[0]); ++i)
        JbbPut(jb, order[i]);
    CallJbFrame f;
    for (long long expect = 0; expect < 6; ++expect)
    {
        jb.GetFrame(f);
        INFO("expect " << expect);
        CHECK(JbbIndexOf(f) == expect);
    }
}

TEST_CASE("JBB3 丢包：到点返回缺失，之后的包照常取出", "[jitterbuf]")
{
    CallJitterBuffer jb;
    const uint32_t arrived[] = { 0, 1, 3, 4 };
    for (size_t i = 0; i < sizeof(arrived) / sizeof(arrived[0]); ++i)
        JbbPut(jb, arrived[i]);
    CallJbFrame f;
    jb.GetFrame(f);
    CHECK(JbbIndexOf(f) == 0);
    jb.GetFrame(f);
    CHECK(JbbIndexOf(f) == 1);
    jb.GetFrame(f);
    CHECK(f.m_result == call_jb_missing);
    CHECK(f.m_span == kCallJbFrameSamples);
    jb.GetFrame(f);
    CHECK(JbbIndexOf(f) == 3);
    jb.GetFrame(f);
    CHECK(JbbIndexOf(f) == 4);
    CHECK(jb.Stats().m_missing >= 1);
}

TEST_CASE("JBB4 卡顿后回落：网络卡住后积压的包一次到达，若干秒内缓冲延迟回落", "[jitterbuf]")
{
    const int freezes[] = { 10, 25 };   //卡 200 毫秒、500 毫秒
    for (size_t k = 0; k < sizeof(freezes) / sizeof(freezes[0]); ++k)
    {
        const std::vector<int> delays = JbbFreezeRun(freezes[k]);
        REQUIRE(!delays.empty());
        //输出每秒一个采样点，便于看清回落的过程
        std::ostringstream curve;
        for (size_t i = 0; i < delays.size(); i += 50)
            curve << delays[i] << ' ';
        WARN("freeze " << freezes[k] * kJbbFrameMs << " ms: buffered delay per second (ms): " << curve.str());
        //最后一秒的缓冲延迟不超过上限
        int lastMax = 0;
        for (size_t i = delays.size() - 50; i < delays.size(); ++i)
            lastMax = (delays[i] > lastMax) ? delays[i] : lastMax;
        INFO("freeze " << freezes[k] * kJbbFrameMs << " ms, last-second max delay " << lastMax << " ms");
        CHECK(lastMax <= kJbbRecoveredMs);
    }
}

TEST_CASE("JBB5 时间线重置：长时间没有包后恢复；缓冲已空时时间戳落后的包不被丢掉", "[jitterbuf]")
{
    SECTION("长时间没有包，之后时间戳照常前进的包恢复播放")
    {
        CallJitterBuffer jb;
        CallJbFrame f;
        uint32_t next = 0;
        for (int t = 0; t < 10; ++t)
        {
            JbbPut(jb, next++);
            jb.GetFrame(f);
        }
        for (int t = 0; t < kJbbLongGapFrames; ++t)
        {
            ++next;
            jb.GetFrame(f);
            CHECK(f.m_result != call_jb_packet);
        }
        JbbPut(jb, next);
        jb.GetFrame(f);
        CHECK(JbbIndexOf(f) == static_cast<long long>(next));
    }

    SECTION("缓冲已空、发送端序号没有前进（麦克风开关、电脑声音间歇、老版本）：重置后取出，不当作迟到")
    {
        CallJitterBuffer jb;
        CallJbFrame f;
        for (uint32_t i = 0; i < 10; ++i)
        {
            JbbPut(jb, i);
            jb.GetFrame(f);
        }
        //停 100 毫秒，期间取空
        for (int t = 0; t < 5; ++t)
            jb.GetFrame(f);
        //发送端的序号没有前进，接着发 10、11
        JbbPut(jb, 10);
        JbbPut(jb, 11);
        jb.GetFrame(f);
        CHECK(JbbIndexOf(f) == 10);
        jb.GetFrame(f);
        CHECK(JbbIndexOf(f) == 11);
        CHECK(jb.Stats().m_resets >= 1);
    }
}

namespace {

//JBB6 放包线程的参数
struct JbbProducerArgs
{
    CallJitterBuffer*  m_jb;     //抖动缓冲（借用）
    std::atomic<bool>* m_done;   //放完后置 true
};

//JBB6 放包线程体：按序放入 kJbbThreadPackets 个包，每放 5 个让出一次
void JbbProducer(JbbProducerArgs* args)
{
    for (uint32_t i = 0; i < static_cast<uint32_t>(kJbbThreadPackets); ++i)
    {
        JbbPut(*args->m_jb, i);
        if (i % 5 == 0)
            std::this_thread::yield();
    }
    args->m_done->store(true);
}

} // namespace

TEST_CASE("JBB6 线程安全：一个线程放、一个线程取，取出的时间戳严格递增", "[jitterbuf]")
{
    CallJitterBuffer jb;
    std::atomic<bool> done(false);
    JbbProducerArgs args;
    args.m_jb = &jb;
    args.m_done = &done;
    std::thread producer(&JbbProducer, &args);
    long long last = -1;
    int got = 0;
    bool ordered = true;
    CallJbFrame f;
    //放完之后再多取一些，把缓冲里剩下的取干净
    for (int spins = 0; spins < kJbbThreadPackets * 4; ++spins)
    {
        jb.GetFrame(f);
        const long long idx = JbbIndexOf(f);
        if (idx >= 0)
        {
            if (idx <= last)
                ordered = false;
            last = idx;
            ++got;
        }
        if (done.load() && spins > kJbbThreadPackets * 2)
            break;
    }
    producer.join();
    INFO("got " << got << " packets, last index " << last);
    CHECK(ordered);
    CHECK(got > 0);
}

TEST_CASE("JBB7 正常播放时的缓冲深度累计：只在取到包时量，缺失时不计（2026-10-06 B4）", "[jitterbuf]")
{
    CallJitterBuffer jb;
    const uint32_t count = 3;
    for (uint32_t i = 0; i < count; ++i)
        JbbPut(jb, i);
    CallJbFrame f;
    for (uint32_t i = 0; i < count; ++i)
        jb.GetFrame(f);
    CallJbStats st = jb.Stats();
    CHECK(st.m_bufferedCount == count);
    //三个包一次到齐：取第 1、2、3 个之后分别还剩 2、1、0 帧没播放
    INFO("buffered sum " << st.m_bufferedSum);
    CHECK(st.m_bufferedSum == static_cast<uint64_t>(3) * kJbbFrame);
    //缺失时不量
    jb.GetFrame(f);
    REQUIRE(f.m_result != call_jb_packet);
    st = jb.Stats();
    CHECK(st.m_bufferedCount == count);
}
