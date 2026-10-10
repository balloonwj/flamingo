/**
 *  语音消息控制器的单元测试（前缀 VM，标签 [voicemsg]），用例清单见 docs/语音消息方案.md 第 9 节：
 *    - VM7 通话中或屏幕共享开着麦克风时开始录音被拒绝、不打开麦克风；占用期间播放不受影响；不占用时照常录音
 *          （第 2d 步）；
 *    - VM7b 采集 / 渲染线程上的事件经消息窗口转到 UI 线程：消息循环处理之前不回调宿主；播完时先停止设备再通知、
 *          带回 Play 时的编号；停止播放后排着的旧进度事件被丢弃；关闭后不再回调（第 2d 步）；
 *    - VM39 暂停与继续经控制器转到录音器：暂停期间没有音量通知、再开始一段返回「已经在录」；继续前像开始录音一样
 *          检查麦克风占用，被占用或麦克风打不开时保持暂停；暂停中停止写出暂停前后的两段（第 6d 步）。
 *  不碰真实麦克风与扬声器：用模拟设备，测试直接调用录音器的 OnAudioCaptured、播放器的 OnAudioNeeded。
 *  框架 Catch2 v3。
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

#include "../../FlamingoMedia/Voice/VoiceController.h"

using namespace voicemsg;

namespace {

//模拟的麦克风占用查询
class FakeMicBusy : public IVoiceMicBusyQuery
{
public:
    FakeMicBusy()
        : m_busy(false)
    {
    }

    virtual bool IsMicrophoneBusy() override
    {
        return m_busy;
    }

    bool m_busy;   //为 true 时表示麦克风正被通话或屏幕共享使用
};

//记录控制器转来的事件
class FakeVoiceSink : public IVoiceControllerSink
{
public:
    FakeVoiceSink()
        : m_levels(0)
        , m_limits(0)
    {
    }

    virtual void OnVoiceRecordLevel(int level) override
    {
        (void)level;
        ++m_levels;
    }

    virtual void OnVoiceRecordLimitReached() override
    {
        ++m_limits;
    }

    virtual void OnVoicePlayProgress(uint32_t token, int64_t playedMs, int64_t totalMs) override
    {
        (void)playedMs;
        (void)totalMs;
        m_progressTokens.push_back(token);
    }

    virtual void OnVoicePlayFinished(uint32_t token) override
    {
        m_finishedTokens.push_back(token);
    }

    int                   m_levels;           //音量事件次数
    int                   m_limits;           //录满事件次数
    std::vector<uint32_t> m_progressTokens;   //每次进度事件带的编号
    std::vector<uint32_t> m_finishedTokens;   //每次播完事件带的编号
};

//模拟的麦克风：记下数据接收方，由测试直接喂数据
class FakeCtlCapture : public IVoiceCaptureDevice
{
public:
    FakeCtlCapture()
        : m_handler(NULL)
        , m_started(false)
        , m_failStart(false)
    {
    }

    virtual bool Start(call::WasapiCaptureHandler* handler, int& outSampleRate) override
    {
        if (m_failStart)
            return false;
        m_handler = handler;
        m_started = true;
        outSampleRate = kVoiceSampleRate;
        return true;
    }

    virtual void Stop() override
    {
        m_started = false;
    }

    call::WasapiCaptureHandler* m_handler;     //录音器（Start 时记下）
    bool                        m_started;     //是否处于采集中
    bool                        m_failStart;   //为 true 时 Start 返回失败（模拟麦克风打不开）
};

//模拟的扬声器：记下数据来源，由测试直接取数据
class FakeCtlRender : public IVoiceRenderDevice
{
public:
    FakeCtlRender()
        : m_source(NULL)
        , m_started(false)
    {
    }

    virtual bool Start(call::WasapiRenderSource* source, int& outSampleRate) override
    {
        m_source = source;
        m_started = true;
        outSampleRate = kVoiceSampleRate;
        return true;
    }

    virtual void Stop() override
    {
        m_started = false;
    }

    call::WasapiRenderSource* m_source;    //播放器（Start 时记下）
    bool                      m_started;   //是否处于播放中
};

//每次喂 / 取 10 毫秒（16 kHz 下 160 个采样）
const int kCtlChunk = kVoiceSampleRate / 100;
const double kCtlTwoPi = 6.283185307179586;
const double kCtlToneHz = 440.0;
const double kCtlAmplitude = 10000.0;

//向录音器喂 ms 毫秒的正弦音
void feedCtl(FakeCtlCapture& capture, int ms)
{
    REQUIRE(capture.m_handler != NULL);
    std::vector<int16_t> buf(static_cast<size_t>(kCtlChunk));
    for (int done = 0, pos = 0; done < ms; done += 10)
    {
        for (int i = 0; i < kCtlChunk; ++i, ++pos)
            buf[static_cast<size_t>(i)] = static_cast<int16_t>(
                kCtlAmplitude * sin(kCtlTwoPi * kCtlToneHz * pos / kVoiceSampleRate));
        capture.m_handler->OnAudioCaptured(&buf[0], kCtlChunk);
    }
}

//从播放器取 ms 毫秒的数据
void pullCtl(FakeCtlRender& render, int ms)
{
    REQUIRE(render.m_source != NULL);
    std::vector<int16_t> buf(static_cast<size_t>(kCtlChunk));
    for (int done = 0; done < ms; done += 10)
        render.m_source->OnAudioNeeded(&buf[0], kCtlChunk);
}

//处理本线程排着的窗口消息（控制器的消息窗口在本线程上）
void pumpMessages()
{
    MSG msg;
    while (::PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
    {
        ::TranslateMessage(&msg);
        ::DispatchMessageW(&msg);
    }
}

//测试用的文件路径（系统临时目录）
std::wstring ctlTempPath(const wchar_t* name)
{
    wchar_t tempDir[MAX_PATH] = { 0 };
    ::GetTempPathW(MAX_PATH, tempDir);
    const std::wstring path = std::wstring(tempDir) + name;
    ::DeleteFileW(path.c_str());
    return path;
}

//用控制器录一段 ms 毫秒的语音文件
void recordFile(CVoiceController& controller, FakeCtlCapture& capture, const std::wstring& path, int ms)
{
    REQUIRE(controller.StartRecord(path) == voice_record_ok);
    feedCtl(capture, ms);
    VoiceRecordResult result;
    REQUIRE(controller.StopRecord(result));
    pumpMessages();
}

} // namespace

TEST_CASE("VM7 麦克风被通话或屏幕共享占用时不录音、不打开麦克风，播放照常", "[voicemsg]")
{
    FakeMicBusy busy;
    FakeVoiceSink sink;
    FakeCtlCapture capture;
    FakeCtlRender render;
    CVoiceController controller(&busy, &sink, &capture, &render);
    REQUIRE(controller.Init());
    const std::wstring path = ctlTempPath(L"FlamingoUnit_VM7.ogg");

    //不占用时录一段，供后面播放
    recordFile(controller, capture, path, 1000);

    //占用：开始录音被拒绝，麦克风没有打开
    busy.m_busy = true;
    CHECK(controller.StartRecord(ctlTempPath(L"FlamingoUnit_VM7_busy.ogg")) == voice_record_mic_busy);
    CHECK_FALSE(capture.m_started);
    CHECK_FALSE(controller.IsRecording());
    //占用期间播放照常
    CHECK(controller.Play(path, 7));
    CHECK(render.m_started);
    CHECK(controller.PlayingToken() == 7);
    controller.StopPlay();
    CHECK_FALSE(render.m_started);
    CHECK(controller.PlayingToken() == 0);

    //不再占用：照常录音；已在录音时再开始返回 voice_record_already
    busy.m_busy = false;
    CHECK(controller.StartRecord(ctlTempPath(L"FlamingoUnit_VM7_again.ogg")) == voice_record_ok);
    CHECK(capture.m_started);
    CHECK(controller.StartRecord(ctlTempPath(L"FlamingoUnit_VM7_again2.ogg")) == voice_record_already);
    controller.CancelRecord();
    CHECK_FALSE(capture.m_started);

    controller.Shutdown();
    //关闭之后开始录音返回 voice_record_not_ready
    CHECK(controller.StartRecord(ctlTempPath(L"FlamingoUnit_VM7_closed.ogg")) == voice_record_not_ready);
    ::DeleteFileW(path.c_str());
}

TEST_CASE("VM39 控制器暂停与继续：转到录音器，暂停期间没有音量通知，继续前检查麦克风占用", "[voicemsg]")
{
    const int kSegmentMs = 500;   //暂停前、继续后各录的时长（毫秒）；设备就是 16 kHz，不重采样，时长精确
    FakeMicBusy busy;
    FakeVoiceSink sink;
    FakeCtlCapture capture;
    FakeCtlRender render;
    CVoiceController controller(&busy, &sink, &capture, &render);
    REQUIRE(controller.Init());

    //没有录音：暂停失败，继续返回 voice_record_not_paused
    CHECK_FALSE(controller.PauseRecord());
    CHECK(controller.ResumeRecord() == voice_record_not_paused);

    const std::wstring path = ctlTempPath(L"FlamingoUnit_VM39.ogg");
    REQUIRE(controller.StartRecord(path) == voice_record_ok);
    feedCtl(capture, kSegmentMs);
    pumpMessages();
    const int levelsAtPause = sink.m_levels;
    CHECK(levelsAtPause > 0);

    //暂停：麦克风关闭；录音会话仍在，再开始一段返回 voice_record_already
    REQUIRE(controller.PauseRecord());
    CHECK(controller.IsRecordPaused());
    CHECK(controller.IsRecording());
    CHECK_FALSE(capture.m_started);
    CHECK_FALSE(controller.PauseRecord());
    CHECK(controller.StartRecord(ctlTempPath(L"FlamingoUnit_VM39_b.ogg")) == voice_record_already);
    //暂停期间设备即使还有回调，也不产生音量通知
    feedCtl(capture, kSegmentMs);
    pumpMessages();
    CHECK(sink.m_levels == levelsAtPause);

    //暂停期间开始了通话：继续被拒绝，保持暂停，不打开麦克风
    busy.m_busy = true;
    CHECK(controller.ResumeRecord() == voice_record_mic_busy);
    CHECK(controller.IsRecordPaused());
    CHECK_FALSE(capture.m_started);

    //通话结束：继续成功，接着有音量通知
    busy.m_busy = false;
    CHECK(controller.ResumeRecord() == voice_record_ok);
    CHECK_FALSE(controller.IsRecordPaused());
    CHECK(capture.m_started);
    feedCtl(capture, kSegmentMs);
    pumpMessages();
    CHECK(sink.m_levels > levelsAtPause);

    //再次暂停后麦克风打不开：继续返回 voice_record_device_error，保持暂停
    REQUIRE(controller.PauseRecord());
    capture.m_failStart = true;
    CHECK(controller.ResumeRecord() == voice_record_device_error);
    CHECK(controller.IsRecordPaused());
    capture.m_failStart = false;

    //暂停中停止：写出暂停前后录到的两段
    VoiceRecordResult result;
    REQUIRE(controller.StopRecord(result));
    CHECK(result.m_durationMs == kSegmentMs * 2);
    CHECK_FALSE(controller.IsRecording());
    CHECK_FALSE(controller.IsRecordPaused());
    ::DeleteFileW(path.c_str());

    //关闭之后：继续返回 voice_record_not_ready
    controller.Shutdown();
    CHECK(controller.ResumeRecord() == voice_record_not_ready);
}

TEST_CASE("VM7b 事件经消息窗口转到 UI 线程：播完先停设备再通知，旧事件丢弃，关闭后不再回调", "[voicemsg]")
{
    FakeMicBusy busy;
    FakeVoiceSink sink;
    FakeCtlCapture capture;
    FakeCtlRender render;
    CVoiceController controller(&busy, &sink, &capture, &render);
    REQUIRE(controller.Init());
    const std::wstring path = ctlTempPath(L"FlamingoUnit_VM7b.ogg");

    SECTION("录音音量：消息循环处理之前不回调，处理之后约每 100 毫秒一次")
    {
        REQUIRE(controller.StartRecord(path) == voice_record_ok);
        feedCtl(capture, 1000);
        CHECK(sink.m_levels == 0);
        pumpMessages();
        CHECK(sink.m_levels >= 8);
        CHECK(sink.m_levels <= 11);
        VoiceRecordResult result;
        CHECK(controller.StopRecord(result));
    }
    SECTION("播放到结尾：设备已停止、编号原样带回，之后不在播放")
    {
        recordFile(controller, capture, path, 1000);
        REQUIRE(controller.Play(path, 42));
        pullCtl(render, 1500);
        pumpMessages();
        REQUIRE(sink.m_finishedTokens.size() == 1);
        CHECK(sink.m_finishedTokens[0] == 42u);
        CHECK_FALSE(render.m_started);
        CHECK(controller.PlayingToken() == 0);
        CHECK_FALSE(sink.m_progressTokens.empty());
        for (size_t i = 0; i < sink.m_progressTokens.size(); ++i)
            CHECK(sink.m_progressTokens[i] == 42u);
    }
    SECTION("停止播放后，排着的旧进度事件被丢弃，也不通知播完")
    {
        recordFile(controller, capture, path, 1000);
        REQUIRE(controller.Play(path, 43));
        pullCtl(render, 300);
        controller.StopPlay();
        pumpMessages();
        CHECK(sink.m_progressTokens.empty());
        CHECK(sink.m_finishedTokens.empty());
    }
    SECTION("换播另一个文件：前一个的旧事件被丢弃，新编号的事件照常")
    {
        recordFile(controller, capture, path, 1000);
        REQUIRE(controller.Play(path, 44));
        pullCtl(render, 300);
        REQUIRE(controller.Play(path, 45));
        pullCtl(render, 300);
        pumpMessages();
        for (size_t i = 0; i < sink.m_progressTokens.size(); ++i)
            CHECK(sink.m_progressTokens[i] == 45u);
        CHECK_FALSE(sink.m_progressTokens.empty());
        controller.StopPlay();
    }
    SECTION("关闭控制器：停止录音与播放，排着的事件不再回调")
    {
        REQUIRE(controller.StartRecord(path) == voice_record_ok);
        feedCtl(capture, 500);
        controller.Shutdown();
        CHECK_FALSE(capture.m_started);
        pumpMessages();
        CHECK(sink.m_levels == 0);
        CHECK_FALSE(controller.IsRecording());
    }
    controller.Shutdown();
    ::DeleteFileW(path.c_str());
}
