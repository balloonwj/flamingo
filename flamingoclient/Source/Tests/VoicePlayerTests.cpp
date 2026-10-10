/**
 *  语音消息播放器的单元测试（前缀 VM，标签 [voicemsg]），用例清单见 docs/语音消息方案.md 第 9 节：
 *    - VM6 播放器用模拟输出设备把文件完整解码一遍：进度通知逐次递增、结束通知只来一次，有声部分的长度与文件时长
 *          一致、开头没有 pre-skip 留下的延迟，播完之后只填静音；文件不存在、格式不对、设备打不开时播放失败
 *          （第 2c 步）。
 *  不碰真实扬声器：用模拟设备，由测试直接调用播放器的 OnAudioNeeded 取数据。测试用的语音文件在测试里直接用
 *  Opus 编码器与 VoiceOggWriter 生成，不依赖录音器。框架 Catch2 v3。
 *
 *  典型用法：FlamingoUnit.exe "[voicemsg]"
 *
 *  balloonwj@qq.com   2026-10-04
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <math.h>
#include <string>
#include <vector>

#include "../../FlamingoMedia/Call/OpusAudioCodec.h"
#include "../../FlamingoMedia/Voice/VoiceOggFile.h"
#include "../../FlamingoMedia/Voice/VoicePlayer.h"
#include "../../FlamingoMedia/Voice/VoiceRecorder.h"   // kVoiceBitrateBps

using namespace voicemsg;

namespace {

//模拟的扬声器：不出声，记下开始与停止，由测试直接调用播放器的 OnAudioNeeded 取数据
class FakeRenderDevice : public IVoiceRenderDevice
{
public:
    explicit FakeRenderDevice(int sampleRate)
        : m_sampleRate(sampleRate)
        , m_failStart(false)
        , m_started(false)
    {
    }

    virtual bool Start(call::WasapiRenderSource* source, int& outSampleRate) override
    {
        (void)source;
        if (m_failStart)
            return false;
        m_started = true;
        outSampleRate = m_sampleRate;
        return true;
    }

    virtual void Stop() override
    {
        m_started = false;
    }

    int  m_sampleRate;   //报告的设备采样率（Hz）
    bool m_failStart;    //为 true 时 Start 返回失败
    bool m_started;      //是否处于播放中
};

//记录播放器发来的通知
class FakePlayerListener : public IVoicePlayerListener
{
public:
    FakePlayerListener()
        : m_finishedCount(0)
    {
    }

    virtual void OnVoicePlayProgress(int64_t playedMs, int64_t totalMs) override
    {
        m_progress.push_back(playedMs);
        m_totals.push_back(totalMs);
    }

    virtual void OnVoicePlayFinished() override
    {
        ++m_finishedCount;
    }

    std::vector<int64_t> m_progress;       //每次进度通知的已播放毫秒数
    std::vector<int64_t> m_totals;         //每次进度通知的总毫秒数
    int                  m_finishedCount;  //结束通知次数
};

//测试音：440 Hz、幅度 12000、1 秒
const double kVpToneHz = 440.0;
const double kVpTwoPi = 6.283185307179586;
const double kVpAmplitude = 12000.0;
const int kVpSeconds = 1;
//设备采样率与每次取数据的采样数（10 毫秒）
const int kVpDeviceRate = 48000;
const int kVpChunk = kVpDeviceRate / 100;
//判定「有声」的采样绝对值门限
const int kVpLoudThreshold = 100;

//生成测试用的语音文件：16 kHz 正弦音编成 Opus，补齐编码器延迟后按实际采样数裁剪
std::vector<uint8_t> buildToneFile()
{
    call::OpusAudioCodec codec;
    REQUIRE(codec.Init(kVoiceSampleRate, kVoiceSamplesPerFrame, kVoiceBitrateBps));
    const int preSkip48k = codec.EncoderLookahead() * (kOpusGranuleRate / kVoiceSampleRate);
    VoiceOggWriter writer;
    writer.Begin(1u, kVoiceSampleRate, preSkip48k);
    const int64_t samples = static_cast<int64_t>(kVpSeconds) * kVoiceSampleRate;
    std::vector<int16_t> frame(static_cast<size_t>(kVoiceSamplesPerFrame));
    std::vector<unsigned char> packet;
    int64_t pos = 0;
    //有效采样编完后再补静音帧，直到解码出的总采样数覆盖 pre-skip
    while (static_cast<int64_t>(writer.PacketCount()) * kOpusSamplesPerFrame48k
           < samples * (kOpusGranuleRate / kVoiceSampleRate) + preSkip48k)
    {
        for (int i = 0; i < kVoiceSamplesPerFrame; ++i, ++pos)
        {
            const double t = static_cast<double>(pos) / kVoiceSampleRate;
            frame[static_cast<size_t>(i)] = (pos < samples)
                ? static_cast<int16_t>(kVpAmplitude * sin(kVpTwoPi * kVpToneHz * t)) : 0;
        }
        REQUIRE(codec.Encode(&frame[0], kVoiceSamplesPerFrame, packet));
        writer.AddPacket(&packet[0], packet.size(), kOpusSamplesPerFrame48k);
    }
    writer.Finish(samples * (kOpusGranuleRate / kVoiceSampleRate));
    return writer.Bytes();
}

//测试用的文件路径（系统临时目录）
std::wstring playerTempPath(const wchar_t* name)
{
    wchar_t tempDir[MAX_PATH] = { 0 };
    ::GetTempPathW(MAX_PATH, tempDir);
    return std::wstring(tempDir) + name;
}

} // namespace

TEST_CASE("VM6 播放器用模拟输出设备完整解码一遍：进度递增、结束只通知一次、播完只填静音", "[voicemsg]")
{
    const std::wstring path = playerTempPath(L"FlamingoUnit_VM6.ogg");
    REQUIRE(SaveBytesToFile(path, buildToneFile()));

    FakeRenderDevice device(kVpDeviceRate);
    FakePlayerListener listener;
    VoicePlayer player;
    REQUIRE(player.Play(&device, &listener, path));
    CHECK(device.m_started);
    CHECK(player.IsPlaying());
    CHECK(player.DurationMs() == kVpSeconds * kMillisPerSecond);

    //每次取 10 毫秒，最多取 3 秒；收到结束通知后再多取 10 次
    std::vector<int16_t> all;
    std::vector<int16_t> buf(static_cast<size_t>(kVpChunk));
    int extra = 0;
    for (int n = 0; n < 300 && extra < 10; ++n)
    {
        player.OnAudioNeeded(&buf[0], kVpChunk);
        all.insert(all.end(), buf.begin(), buf.end());
        if (listener.m_finishedCount > 0)
            ++extra;
    }
    CHECK(listener.m_finishedCount == 1);

    //进度：约每 100 毫秒一次，逐次递增，不超过总时长，总时长都是 1000
    CHECK(listener.m_progress.size() >= 8);
    CHECK(listener.m_progress.size() <= 11);
    for (size_t i = 1; i < listener.m_progress.size(); ++i)
        CHECK(listener.m_progress[i] > listener.m_progress[i - 1]);
    if (!listener.m_progress.empty())
        CHECK(listener.m_progress.back() <= kVpSeconds * kMillisPerSecond);
    for (size_t i = 0; i < listener.m_totals.size(); ++i)
        CHECK(listener.m_totals[i] == kVpSeconds * kMillisPerSecond);

    //有声部分：从开头（pre-skip 已丢弃，延迟不超过一帧）到约 1 秒处；中段音量明显
    long long first = -1;
    long long last = -1;
    for (size_t i = 0; i < all.size(); ++i)
    {
        if (all[i] > kVpLoudThreshold || all[i] < -kVpLoudThreshold)
        {
            if (first < 0)
                first = static_cast<long long>(i);
            last = static_cast<long long>(i);
        }
    }
    const long long kFrame48k = kOpusSamplesPerFrame48k;
    REQUIRE(first >= 0);
    CHECK(first < kFrame48k);
    CHECK(last > kVpDeviceRate - 2 * kFrame48k);
    CHECK(last < kVpDeviceRate + 2 * kFrame48k);
    double sum = 0.0;
    const size_t midBegin = static_cast<size_t>(kVpDeviceRate / 10);
    const size_t midEnd = static_cast<size_t>(kVpDeviceRate * 9 / 10);
    for (size_t i = midBegin; i < midEnd && i < all.size(); ++i)
        sum += static_cast<double>(all[i]) * all[i];
    CHECK(sqrt(sum / static_cast<double>(midEnd - midBegin)) > 4000.0);

    //播完之后只填静音
    for (size_t i = 0; i < buf.size(); ++i)
        buf[i] = 1;
    player.OnAudioNeeded(&buf[0], kVpChunk);
    bool silent = true;
    for (size_t i = 0; i < buf.size(); ++i)
        silent = silent && (buf[i] == 0);
    CHECK(silent);

    player.Stop();
    CHECK_FALSE(player.IsPlaying());
    CHECK_FALSE(device.m_started);
    ::DeleteFileW(path.c_str());
}

TEST_CASE("VM6b 播放失败的情况：文件不存在、格式不对、设备打不开", "[voicemsg]")
{
    FakeRenderDevice device(kVpDeviceRate);
    VoicePlayer player;
    CHECK_FALSE(player.Play(&device, NULL, playerTempPath(L"FlamingoUnit_VM6_missing.ogg")));
    CHECK_FALSE(player.IsPlaying());

    const std::wstring junk = playerTempPath(L"FlamingoUnit_VM6_junk.ogg");
    std::vector<uint8_t> bytes(200, 0x41);
    REQUIRE(SaveBytesToFile(junk, bytes));
    CHECK_FALSE(player.Play(&device, NULL, junk));
    CHECK_FALSE(device.m_started);
    ::DeleteFileW(junk.c_str());

    const std::wstring good = playerTempPath(L"FlamingoUnit_VM6_good.ogg");
    REQUIRE(SaveBytesToFile(good, buildToneFile()));
    device.m_failStart = true;
    CHECK_FALSE(player.Play(&device, NULL, good));
    CHECK_FALSE(player.IsPlaying());
    ::DeleteFileW(good.c_str());
}
