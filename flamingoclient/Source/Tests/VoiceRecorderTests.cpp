/**
 *  语音消息录音器的单元测试（前缀 VM，标签 [voicemsg]），用例清单见 docs/语音消息方案.md 第 9 节：
 *    - VM3 录音器用模拟采集数据录 1 秒静音加 2 秒有声：时长、文件格式、40 个波形值（静音段接近 0、有声段
 *          明显更大）、音量通知的次数与大小（第 2b 步）；
 *    - VM4 录满 120 秒后不再录入，只通知一次，停止后照常产出 120 秒的文件（第 2b 步）；
 *    - VM5 取消录音不留文件；没有录到音频时停止失败也不留文件；设备打不开、重复开始都返回失败（第 2b 步）；
 *    - VM38 暂停与继续：暂停时停设备、之后送来的数据不计入、没有音量通知；继续后重新打开设备接着录；暂停中停止
 *          写出已录的部分，暂停中取消不留文件；没在录音时暂停或继续失败；继续时设备打不开返回失败并保持暂停；
 *          继续时设备采样率变了照常累计（第 6d 步）；
 *    - VM47 暂停中另存已录的部分（供暂停时试听，2026-10-05）：另存的文件能解析、时长比已录时长少不到 30 毫秒，
 *          录音仍是暂停、原文件没有写出；继续录完后停止得到完整内容；没在录音、没暂停、一帧都没编出时另存失败。
 *  不碰真实麦克风：用模拟设备，由测试直接调用录音器的 OnAudioCaptured 喂数据。框架 Catch2 v3。
 *  Ogg Opus 文件读写的用例在 VoiceMessageTests.cpp。
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

#include "../../FlamingoMedia/Voice/VoiceOggFile.h"
#include "../../FlamingoMedia/Voice/VoiceRecorder.h"

using namespace voicemsg;

namespace {

//模拟的麦克风：不碰真实设备，记下开始与停止，由测试直接调用录音器的 OnAudioCaptured 喂数据
class FakeCaptureDevice : public IVoiceCaptureDevice
{
public:
    explicit FakeCaptureDevice(int sampleRate)
        : m_sampleRate(sampleRate)
        , m_failStart(false)
        , m_started(false)
        , m_stopCount(0)
        , m_startCount(0)
    {
    }

    virtual bool Start(call::WasapiCaptureHandler* handler, int& outSampleRate) override
    {
        (void)handler;
        if (m_failStart)
            return false;
        m_started = true;
        ++m_startCount;
        outSampleRate = m_sampleRate;
        return true;
    }

    virtual void Stop() override
    {
        m_started = false;
        ++m_stopCount;
    }

    int  m_sampleRate;   //报告的设备采样率（Hz）
    bool m_failStart;    //为 true 时 Start 返回失败
    bool m_started;      //是否处于采集中
    int  m_stopCount;    //Stop 被调用的次数
    int  m_startCount;   //Start 成功的次数
};

//记录录音器发来的通知
class FakeRecorderListener : public IVoiceRecorderListener
{
public:
    FakeRecorderListener()
        : m_levelCount(0)
        , m_maxLevel(0)
        , m_limitCount(0)
    {
    }

    virtual void OnVoiceLevel(int level) override
    {
        ++m_levelCount;
        if (level > m_maxLevel)
            m_maxLevel = level;
    }

    virtual void OnVoiceLimitReached() override
    {
        ++m_limitCount;
    }

    int m_levelCount;   //音量通知次数
    int m_maxLevel;     //收到的最大音量
    int m_limitCount;   //录满通知次数
};

//喂数据的块长（毫秒），与 WASAPI 共享模式每次回调的量级相近
const int kVmChunkMs = 10;
//测试音的频率（Hz）
const double kVmToneHz = 440.0;
const double kVmTwoPi = 6.283185307179586;
//测试文件读回时允许的最大字节数
const size_t kVmMaxFileBytes = 1024 * 1024;

//按 sampleRate 生成 ms 毫秒的正弦音（amplitude 为 0 时是静音），按 kVmChunkMs 分块喂给录音器；
//phase 跨调用累计，使相邻两段衔接连续
void feedTone(VoiceRecorder& recorder, int sampleRate, int ms, double amplitude, int64_t& phase)
{
    const int chunk = sampleRate * kVmChunkMs / kMillisPerSecond;
    std::vector<int16_t> buf(static_cast<size_t>(chunk));
    for (int done = 0; done < ms; done += kVmChunkMs)
    {
        for (int i = 0; i < chunk; ++i)
        {
            const double t = static_cast<double>(phase + i) / sampleRate;
            buf[static_cast<size_t>(i)] = static_cast<int16_t>(amplitude * sin(kVmTwoPi * kVmToneHz * t));
        }
        phase += chunk;
        recorder.OnAudioCaptured(&buf[0], chunk);
    }
}

//测试用的录音文件路径（系统临时目录），先删掉旧文件
std::wstring tempVoicePath(const wchar_t* name)
{
    wchar_t tempDir[MAX_PATH] = { 0 };
    ::GetTempPathW(MAX_PATH, tempDir);
    const std::wstring path = std::wstring(tempDir) + name;
    ::DeleteFileW(path.c_str());
    ::DeleteFileW((path + L".part").c_str());
    return path;
}

bool fileExists(const std::wstring& path)
{
    return ::GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

int64_t absDiff(int64_t a, int64_t b)
{
    return (a > b) ? (a - b) : (b - a);
}

} // namespace

TEST_CASE("VM3 录音器用模拟采集数据录音：时长、文件格式、波形与音量通知", "[voicemsg]")
{
    //设备采样率 48 kHz，录音器要重采样到 16 kHz
    FakeCaptureDevice device(48000);
    FakeRecorderListener listener;
    VoiceRecorder recorder;
    const std::wstring path = tempVoicePath(L"FlamingoUnit_VM3.ogg");
    REQUIRE(recorder.Start(&device, &listener, path));
    CHECK(device.m_started);
    CHECK(recorder.IsRecording());

    //1 秒静音，再 2 秒响亮的正弦音（幅度 16000，约 -9 dB）
    int64_t phase = 0;
    feedTone(recorder, 48000, 1000, 0.0, phase);
    feedTone(recorder, 48000, 2000, 16000.0, phase);
    //重采样器有少量延迟，允许 40 毫秒（两帧）误差
    const int64_t kToleranceMs = 40;
    CHECK(absDiff(recorder.RecordedMs(), 3000) <= kToleranceMs);

    VoiceRecordResult result;
    REQUIRE(recorder.Stop(result));
    CHECK_FALSE(recorder.IsRecording());
    CHECK_FALSE(device.m_started);
    CHECK(result.m_path == path);
    CHECK(absDiff(result.m_durationMs, 3000) <= kToleranceMs);

    //文件：能解析，时长与结果一致，原始采样率 16 kHz，有 pre-skip，至少 150 帧，没有留下临时文件
    std::vector<uint8_t> bytes;
    REQUIRE(LoadBytesFromFile(path, bytes, kVmMaxFileBytes));
    CHECK(result.m_fileBytes == static_cast<int64_t>(bytes.size()));
    VoiceOggReader reader;
    REQUIRE(reader.Parse(bytes));
    CHECK(reader.DurationMs() == result.m_durationMs);
    CHECK(reader.InputSampleRate() == static_cast<uint32_t>(kVoiceSampleRate));
    CHECK(reader.PreSkip48k() > 0);
    CHECK(reader.Packets().size() >= 150);
    CHECK_FALSE(fileExists(path + L".part"));

    //波形：40 个值；前 12 个落在静音段，接近 0；后 20 个落在有声段，明显更大
    REQUIRE(result.m_wave.size() == static_cast<size_t>(kVoiceWaveBars));
    for (int i = 0; i < 12; ++i)
        CHECK(result.m_wave[static_cast<size_t>(i)] < 20);
    for (int i = kVoiceWaveBars - 20; i < kVoiceWaveBars; ++i)
        CHECK(result.m_wave[static_cast<size_t>(i)] > 150);

    //音量通知：每 100 毫秒一次，3 秒约 30 次；有声段的音量明显大于 0；没有录满
    CHECK(listener.m_levelCount >= 25);
    CHECK(listener.m_levelCount <= 31);
    CHECK(listener.m_maxLevel > 150);
    CHECK(listener.m_limitCount == 0);
    ::DeleteFileW(path.c_str());
}

TEST_CASE("VM4 录满 120 秒后不再录入，只通知一次，停止后产出 120 秒的文件", "[voicemsg]")
{
    //设备采样率就是 16 kHz，不重采样，时长可以精确比较
    FakeCaptureDevice device(kVoiceSampleRate);
    FakeRecorderListener listener;
    VoiceRecorder recorder;
    const std::wstring path = tempVoicePath(L"FlamingoUnit_VM4.ogg");
    REQUIRE(recorder.Start(&device, &listener, path));
    int64_t phase = 0;
    feedTone(recorder, kVoiceSampleRate, (kVoiceMaxSeconds + 1) * kMillisPerSecond, 8000.0, phase);
    CHECK(listener.m_limitCount == 1);
    CHECK(recorder.RecordedMs() == static_cast<int64_t>(kVoiceMaxSeconds) * kMillisPerSecond);
    //录满之后仍在录音状态，等用户确认
    CHECK(recorder.IsRecording());

    VoiceRecordResult result;
    REQUIRE(recorder.Stop(result));
    CHECK(result.m_durationMs == static_cast<int64_t>(kVoiceMaxSeconds) * kMillisPerSecond);
    //24 kbps 录满 2 分钟约 360 KB，在服务端的 1 MiB 上限之内
    CHECK(result.m_fileBytes > 0);
    CHECK(result.m_fileBytes < static_cast<int64_t>(kVmMaxFileBytes));
    CHECK(result.m_wave.size() == static_cast<size_t>(kVoiceWaveBars));
    VoiceOggReader reader;
    std::vector<uint8_t> bytes;
    REQUIRE(LoadBytesFromFile(path, bytes, kVmMaxFileBytes));
    REQUIRE(reader.Parse(bytes));
    CHECK(reader.DurationMs() == static_cast<int64_t>(kVoiceMaxSeconds) * kMillisPerSecond);
    ::DeleteFileW(path.c_str());
}

TEST_CASE("VM5 取消录音不留文件；没录到音频时停止失败；设备打不开、重复开始都返回失败", "[voicemsg]")
{
    FakeCaptureDevice device(kVoiceSampleRate);
    VoiceRecorder recorder;
    const std::wstring path = tempVoicePath(L"FlamingoUnit_VM5.ogg");

    SECTION("录了半秒后取消：停止设备，不写文件，之后再停止返回失败")
    {
        REQUIRE(recorder.Start(&device, NULL, path));
        int64_t phase = 0;
        feedTone(recorder, kVoiceSampleRate, 500, 8000.0, phase);
        recorder.Cancel();
        CHECK_FALSE(recorder.IsRecording());
        CHECK_FALSE(device.m_started);
        CHECK_FALSE(fileExists(path));
        CHECK_FALSE(fileExists(path + L".part"));
        VoiceRecordResult result;
        CHECK_FALSE(recorder.Stop(result));
        CHECK_FALSE(fileExists(path));
    }
    SECTION("一个采样都没录到就停止：返回失败，不留文件")
    {
        REQUIRE(recorder.Start(&device, NULL, path));
        VoiceRecordResult result;
        CHECK_FALSE(recorder.Stop(result));
        CHECK_FALSE(recorder.IsRecording());
        CHECK_FALSE(fileExists(path));
    }
    SECTION("正在录音时再次开始返回失败，原来的录音不受影响")
    {
        REQUIRE(recorder.Start(&device, NULL, path));
        CHECK_FALSE(recorder.Start(&device, NULL, path));
        CHECK(recorder.IsRecording());
        recorder.Cancel();
    }
    SECTION("设备打不开：开始失败，不处于录音状态")
    {
        device.m_failStart = true;
        CHECK_FALSE(recorder.Start(&device, NULL, path));
        CHECK_FALSE(recorder.IsRecording());
    }
}

TEST_CASE("VM38 录音器暂停与继续：暂停时停设备、不计入数据，继续后接着录，暂停中能停止或取消", "[voicemsg]")
{
    const int kDeviceRate = 48000;         //设备采样率，录音器要重采样到 16 kHz
    const double kAmplitude = 12000.0;     //有声段的幅度
    const int64_t kToleranceMs = 40;       //重采样器有少量延迟，与 VM3 相同允许两帧误差
    //暂停一次会丢掉重采样器里积压的少量采样，经过一次暂停、换一次采样率时允许的误差加倍
    const int64_t kRateChangeToleranceMs = kToleranceMs * 2;
    FakeCaptureDevice device(kDeviceRate);
    FakeRecorderListener listener;
    VoiceRecorder recorder;
    const std::wstring path = tempVoicePath(L"FlamingoUnit_VM38.ogg");
    int64_t phase = 0;

    SECTION("没有在录音：暂停、继续都返回失败")
    {
        CHECK_FALSE(recorder.Pause());
        CHECK_FALSE(recorder.Resume());
        CHECK_FALSE(recorder.IsPaused());
    }
    SECTION("录 1 秒、暂停、暂停中送来的 1 秒不计入、继续再录 1 秒：文件时长约 2 秒")
    {
        REQUIRE(recorder.Start(&device, &listener, path));
        feedTone(recorder, kDeviceRate, 1000, kAmplitude, phase);
        REQUIRE(recorder.Pause());
        //设备已停止；录音会话仍在，暂停不算结束
        CHECK_FALSE(device.m_started);
        CHECK(recorder.IsPaused());
        CHECK(recorder.IsRecording());
        CHECK_FALSE(recorder.Pause());
        const int64_t pausedMs = recorder.RecordedMs();
        const int levelsAtPause = listener.m_levelCount;
        //暂停中设备若仍有数据回调，一律不计入，也没有音量通知
        feedTone(recorder, kDeviceRate, 1000, kAmplitude, phase);
        CHECK(recorder.RecordedMs() == pausedMs);
        CHECK(listener.m_levelCount == levelsAtPause);

        REQUIRE(recorder.Resume());
        CHECK(device.m_started);
        CHECK(device.m_startCount == 2);
        CHECK_FALSE(recorder.IsPaused());
        CHECK_FALSE(recorder.Resume());
        feedTone(recorder, kDeviceRate, 1000, kAmplitude, phase);
        CHECK(listener.m_levelCount > levelsAtPause);

        VoiceRecordResult result;
        REQUIRE(recorder.Stop(result));
        CHECK(absDiff(result.m_durationMs, 2000) <= kToleranceMs);
        CHECK_FALSE(recorder.IsRecording());
        CHECK_FALSE(recorder.IsPaused());
        std::vector<uint8_t> bytes;
        REQUIRE(LoadBytesFromFile(path, bytes, kVmMaxFileBytes));
        VoiceOggReader reader;
        REQUIRE(reader.Parse(bytes));
        CHECK(reader.DurationMs() == result.m_durationMs);
    }
    SECTION("暂停中停止：写出暂停前录到的部分")
    {
        REQUIRE(recorder.Start(&device, &listener, path));
        feedTone(recorder, kDeviceRate, 1500, kAmplitude, phase);
        REQUIRE(recorder.Pause());
        VoiceRecordResult result;
        REQUIRE(recorder.Stop(result));
        CHECK(absDiff(result.m_durationMs, 1500) <= kToleranceMs);
        CHECK(fileExists(path));
        CHECK_FALSE(device.m_started);
        CHECK_FALSE(recorder.IsRecording());
        CHECK_FALSE(recorder.IsPaused());
    }
    SECTION("暂停中取消：不留文件，录音结束")
    {
        REQUIRE(recorder.Start(&device, &listener, path));
        feedTone(recorder, kDeviceRate, 500, kAmplitude, phase);
        REQUIRE(recorder.Pause());
        recorder.Cancel();
        CHECK_FALSE(fileExists(path));
        CHECK_FALSE(fileExists(path + L".part"));
        CHECK_FALSE(recorder.IsRecording());
        CHECK_FALSE(recorder.IsPaused());
        CHECK_FALSE(recorder.Resume());
    }
    SECTION("继续时设备打不开：返回失败并保持暂停，之后设备恢复可以继续，也照常能停止")
    {
        REQUIRE(recorder.Start(&device, &listener, path));
        feedTone(recorder, kDeviceRate, 1000, kAmplitude, phase);
        REQUIRE(recorder.Pause());
        device.m_failStart = true;
        CHECK_FALSE(recorder.Resume());
        CHECK(recorder.IsPaused());
        CHECK(recorder.IsRecording());
        CHECK_FALSE(device.m_started);
        device.m_failStart = false;
        REQUIRE(recorder.Resume());
        feedTone(recorder, kDeviceRate, 500, kAmplitude, phase);
        VoiceRecordResult result;
        REQUIRE(recorder.Stop(result));
        CHECK(absDiff(result.m_durationMs, 1500) <= kToleranceMs);
    }
    SECTION("继续时设备采样率变了（换了默认麦克风）：按新采样率接着录，时长照常累计")
    {
        const int kNewDeviceRate = 44100;
        REQUIRE(recorder.Start(&device, &listener, path));
        feedTone(recorder, kDeviceRate, 1000, kAmplitude, phase);
        REQUIRE(recorder.Pause());
        device.m_sampleRate = kNewDeviceRate;
        REQUIRE(recorder.Resume());
        int64_t newPhase = 0;
        feedTone(recorder, kNewDeviceRate, 1000, kAmplitude, newPhase);
        VoiceRecordResult result;
        REQUIRE(recorder.Stop(result));
        CHECK(absDiff(result.m_durationMs, 2000) <= kRateChangeToleranceMs);
    }
    ::DeleteFileW(path.c_str());
}

TEST_CASE("VM47 录音器暂停中另存已录的部分：文件可解析、时长接近已录时长，原录音不受影响，继续录完内容完整", "[voicemsg]")
{
    const int kDeviceRate = 48000;         //设备采样率，录音器要重采样到 16 kHz
    const double kAmplitude = 12000.0;     //有声段的幅度
    const int64_t kToleranceMs = 40;       //重采样器有少量延迟，与 VM38 相同允许两帧误差
    //另存的文件里没有最后不足一帧的采样与编码器 lookahead 里的一小段：最多比已录时长少这么多（毫秒）
    const int64_t kSnapshotShortfallMs = 30;
    FakeCaptureDevice device(kDeviceRate);
    FakeRecorderListener listener;
    VoiceRecorder recorder;
    const std::wstring path = tempVoicePath(L"FlamingoUnit_VM47.ogg");
    const std::wstring snapPath = tempVoicePath(L"FlamingoUnit_VM47_snap.ogg");
    int64_t phase = 0;
    int64_t snapMs = 0;

    SECTION("没在录音、录音中没有暂停、暂停时一帧都没编出：另存失败，不留文件")
    {
        CHECK_FALSE(recorder.SnapshotPaused(snapPath, snapMs));
        REQUIRE(recorder.Start(&device, &listener, path));
        REQUIRE(recorder.Pause());
        CHECK_FALSE(recorder.SnapshotPaused(snapPath, snapMs));
        REQUIRE(recorder.Resume());
        feedTone(recorder, kDeviceRate, 500, kAmplitude, phase);
        CHECK_FALSE(recorder.SnapshotPaused(snapPath, snapMs));
        CHECK_FALSE(fileExists(snapPath));
        recorder.Cancel();
    }
    SECTION("录 1.5 秒、暂停、另存、继续再录 1 秒、停止")
    {
        REQUIRE(recorder.Start(&device, &listener, path));
        feedTone(recorder, kDeviceRate, 1500, kAmplitude, phase);
        REQUIRE(recorder.Pause());
        const int64_t pausedMs = recorder.RecordedMs();
        REQUIRE(recorder.SnapshotPaused(snapPath, snapMs));
        CHECK(snapMs <= pausedMs);
        CHECK(pausedMs - snapMs < kSnapshotShortfallMs);
        std::vector<uint8_t> bytes;
        REQUIRE(LoadBytesFromFile(snapPath, bytes, kVmMaxFileBytes));
        VoiceOggReader reader;
        REQUIRE(reader.Parse(bytes));
        CHECK(reader.DurationMs() == snapMs);
        CHECK(reader.InputSampleRate() == static_cast<uint32_t>(kVoiceSampleRate));
        CHECK_FALSE(fileExists(snapPath + L".part"));
        //录音仍是暂停，已录时长不变，原文件还没有写出
        CHECK(recorder.IsPaused());
        CHECK(recorder.RecordedMs() == pausedMs);
        CHECK_FALSE(fileExists(path));
        //再另存一次（覆盖）结果相同
        int64_t again = 0;
        REQUIRE(recorder.SnapshotPaused(snapPath, again));
        CHECK(again == snapMs);

        //继续再录 1 秒后停止：完整内容约 2.5 秒，另存没有往原录音里加任何内容
        REQUIRE(recorder.Resume());
        feedTone(recorder, kDeviceRate, 1000, kAmplitude, phase);
        VoiceRecordResult result;
        REQUIRE(recorder.Stop(result));
        CHECK(absDiff(result.m_durationMs, 2500) <= kToleranceMs);
        bytes.clear();
        REQUIRE(LoadBytesFromFile(path, bytes, kVmMaxFileBytes));
        VoiceOggReader full;
        REQUIRE(full.Parse(bytes));
        CHECK(full.DurationMs() == result.m_durationMs);
        //停止之后不能再另存
        CHECK_FALSE(recorder.SnapshotPaused(snapPath, snapMs));
    }
    ::DeleteFileW(path.c_str());
    ::DeleteFileW(snapPath.c_str());
}
