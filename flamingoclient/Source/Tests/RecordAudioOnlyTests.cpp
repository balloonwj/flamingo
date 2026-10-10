/**
 *  只录声音的单元测试（标签 [record]），RecordAudioOnlyTests.cpp
 *
 *  覆盖第 24 期（docs/屏幕共享与录制方案.md 第 12.5 节，第 53 条）录制管线里与只录声音有关的部分：
 *    - REC11 只录声音的录制会话：不要画面来源也能开始；写出的文件读回没有视频流、音频样本数大于 0、时长与录制时长相符；
 *            不是只录声音时仍要求画面来源（参数不对）；
 *    - REC12 文件名：录音用 .m4a，重名序号照旧；按扩展名判断是不是录音（不区分大小写）；
 *    - REC13 电平：16 位采样的峰值换算成 0~100 的电平，-60 dB 及以下为 0，满幅为 100，按分贝线性；
 *    - REC14 混音器的峰值：推入时记下各路的峰值，取出后清零；关闭的一路推入的数据不计；
 *    - REC15 写入器只建音频流时：可以写声音，拒绝写画面；收尾后文件里只有一路音频。
 *  REC11、REC15 写临时文件（系统临时目录），用完删除。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-06
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <math.h>
#include <atomic>
#include <string>
#include <thread>
#include <vector>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>

#include "../../FlamingoMedia/Record/RecordAudioMixer.h"
#include "../../FlamingoMedia/Record/RecordMp4Writer.h"
#include "../../FlamingoMedia/Record/RecordPolicy.h"
#include "../../FlamingoMedia/Record/RecordSession.h"

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")

using namespace screenrecord;

namespace {

//读回的结果
struct AudioOnlyInfo
{
    bool    m_opened;         //能否打开
    bool    m_hasVideo;       //有没有视频流
    int64_t m_durationMs;     //文件里记的时长（毫秒）
    int     m_audioSamples;   //音频样本数
};

AudioOnlyInfo readAudioOnly(const std::wstring& path)
{
    AudioOnlyInfo info = { false, false, 0, 0 };
    ::CoInitializeEx(NULL, COINIT_MULTITHREADED);
    ::MFStartup(MF_VERSION);
    IMFSourceReader* reader = NULL;
    if (SUCCEEDED(::MFCreateSourceReaderFromURL(path.c_str(), NULL, &reader)))
    {
        info.m_opened = true;
        PROPVARIANT var;
        PropVariantInit(&var);
        if (SUCCEEDED(reader->GetPresentationAttribute(static_cast<DWORD>(MF_SOURCE_READER_MEDIASOURCE), MF_PD_DURATION,
                                                       &var)))
            info.m_durationMs = static_cast<int64_t>(var.uhVal.QuadPart / 10000);
        PropVariantClear(&var);
        IMFMediaType* videoType = NULL;
        if (SUCCEEDED(reader->GetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), &videoType)))
        {
            info.m_hasVideo = true;
            videoType->Release();
        }
        for (;;)
        {
            DWORD flags = 0;
            LONGLONG ts = 0;
            IMFSample* sample = NULL;
            if (FAILED(reader->ReadSample(static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM), 0, NULL, &flags, &ts,
                                          &sample)))
                break;
            if (sample != NULL)
            {
                ++info.m_audioSamples;
                sample->Release();
            }
            if ((flags & MF_SOURCE_READERF_ENDOFSTREAM) != 0)
                break;
        }
        reader->Release();
    }
    ::MFShutdown();
    ::CoUninitialize();
    return info;
}

std::wstring tempPath(const wchar_t* name)
{
    wchar_t dir[MAX_PATH] = { 0 };
    ::GetTempPathW(MAX_PATH, dir);
    return std::wstring(dir) + name;
}

//录制结束的回报
class AudioStopListener : public RecordSessionListener
{
public:
    AudioStopListener()
        : m_reason(0)
        , m_durationMs(0)
    {
    }

    virtual void OnRecordStopped(int reason, const std::wstring& path, int64_t durationMs) override
    {
        m_reason.store(reason);
        m_path = path;
        m_durationMs = durationMs;
    }

    std::atomic<int> m_reason;       //结束原因
    std::wstring     m_path;         //文件路径
    int64_t          m_durationMs;   //录到的时长
};

//按实时节奏往麦克风一路推 1 kHz 正弦波，直到 stop 为 true
void pushTone(RecordSession* session, std::atomic<bool>* stop)
{
    const int kChunk = 480;
    const double kStep = 2 * 3.14159265358979 * 1000.0 / kRecordAudioRate;
    std::vector<int16_t> pcm(kChunk);
    double phase = 0;
    while (!stop->load())
    {
        for (int i = 0; i < kChunk; ++i)
        {
            pcm[i] = static_cast<int16_t>(sin(phase) * 8000);
            phase += kStep;
        }
        session->Audio().Push(record_audio_mic, &pcm[0], pcm.size());
        ::Sleep(10);
    }
}

} // namespace

TEST_CASE("REC11 audio-only session writes an m4a without video", "[record]")
{
    const std::wstring path = tempPath(L"flamingo_rec11_test.m4a");
    ::DeleteFileW(path.c_str());
    //不是只录声音时，没有画面来源仍是参数不对
    {
        RecordSession session;
        RecordOptions opt;
        opt.m_path = path;
        CHECK_FALSE(session.Start(opt, NULL, NULL, NULL));
        CHECK(session.FailReason() == record_fail_argument);
    }
    AudioStopListener listener;
    RecordSession session;
    RecordOptions opt;
    opt.m_path = path;
    opt.m_audioOnly = true;
    session.Audio().SetEnabled(record_audio_mic, true);
    REQUIRE(session.Start(opt, NULL, NULL, &listener));
    std::atomic<bool> stop(false);
    std::thread tone(pushTone, &session, &stop);
    //写入线程打开文件之后才开始计时
    for (int i = 0; i < 100 && session.RecordedMs() == 0; ++i)
        ::Sleep(20);
    ::Sleep(1200);
    session.Stop();
    stop.store(true);
    tone.join();
    CHECK(listener.m_reason.load() == record_stop_user);
    CHECK(listener.m_path == path);
    CHECK(listener.m_durationMs >= 1000);
    CHECK(listener.m_durationMs <= 1700);
    const AudioOnlyInfo info = readAudioOnly(path);
    REQUIRE(info.m_opened);
    CHECK_FALSE(info.m_hasVideo);
    CHECK(info.m_audioSamples > 0);
    CHECK(info.m_durationMs >= 900);
    CHECK(info.m_durationMs <= 1800);
    ::DeleteFileW(path.c_str());
}

TEST_CASE("REC12 audio recording file names", "[record]")
{
    SYSTEMTIME t;
    memset(&t, 0, sizeof(t));
    t.wYear = 2026;
    t.wMonth = 10;
    t.wDay = 6;
    t.wHour = 9;
    t.wMinute = 5;
    t.wSecond = 7;
    CHECK(MakeRecordFileName(t, 0) == L"FlamingoRecord_20261006_090507.mp4");
    CHECK(MakeRecordFileName(t, 0, true) == L"FlamingoRecord_20261006_090507.m4a");
    CHECK(MakeRecordFileName(t, 2, true) == L"FlamingoRecord_20261006_090507_2.m4a");
    CHECK(IsAudioRecordingPath(L"C:\\v\\FlamingoRecord_20261006_090507.m4a"));
    CHECK(IsAudioRecordingPath(L"C:\\v\\会议.M4A"));
    CHECK_FALSE(IsAudioRecordingPath(L"C:\\v\\FlamingoRecord_20261006_090507.mp4"));
    CHECK_FALSE(IsAudioRecordingPath(L"C:\\v.m4a\\video.mp4"));
    CHECK_FALSE(IsAudioRecordingPath(L"m4a"));
    CHECK_FALSE(IsAudioRecordingPath(L""));
}

TEST_CASE("REC13 peak to level percent", "[record]")
{
    CHECK(RecordLevelPercent(0) == 0);
    CHECK(RecordLevelPercent(32) == 0);           //约 -60 dB
    CHECK(RecordLevelPercent(32767) == 100);
    CHECK(RecordLevelPercent(-32768) == 100);     //负的峰值按绝对值
    //-30 dB 约为满幅的 1/31.6，落在中间
    const int mid = RecordLevelPercent(1036);
    CHECK(mid >= 48);
    CHECK(mid <= 52);
    //单调不减
    int last = 0;
    for (int v = 0; v <= 32767; v += 97)
    {
        const int p = RecordLevelPercent(v);
        CHECK(p >= last);
        last = p;
    }
}

TEST_CASE("REC14 mixer peak levels", "[record]")
{
    RecordAudioMixer mixer;
    mixer.SetEnabled(record_audio_mic, true);
    mixer.SetEnabled(record_audio_system, true);
    CHECK(mixer.TakePeak(record_audio_mic) == 0);
    std::vector<int16_t> quiet(100, 120);
    std::vector<int16_t> loud(100, 0);
    loud[50] = -9000;
    mixer.Push(record_audio_mic, &quiet[0], quiet.size());
    mixer.Push(record_audio_mic, &loud[0], loud.size());
    mixer.Push(record_audio_system, &quiet[0], quiet.size());
    CHECK(mixer.TakePeak(record_audio_mic) == 9000);
    //取出后清零
    CHECK(mixer.TakePeak(record_audio_mic) == 0);
    CHECK(mixer.TakePeak(record_audio_system) == 120);
    //关闭的一路推入的不计
    std::vector<int16_t> others(10, 20000);
    mixer.Push(record_audio_others, &others[0], others.size());
    CHECK(mixer.TakePeak(record_audio_others) == 0);
    //来源编号越界时返回 0
    CHECK(mixer.TakePeak(-1) == 0);
    CHECK(mixer.TakePeak(record_audio_source_count) == 0);
}

TEST_CASE("REC15 audio-only writer rejects video", "[record]")
{
    const std::wstring path = tempPath(L"flamingo_rec15_test.m4a");
    ::DeleteFileW(path.c_str());
    ::CoInitializeEx(NULL, COINIT_MULTITHREADED);
    ::MFStartup(MF_VERSION);
    {
        RecordMp4Writer writer;
        REQUIRE(writer.OpenAudioOnly(path));
        CHECK(writer.IsOpen());
        CHECK_FALSE(writer.HasVideo());
        std::vector<uint8_t> frame(64 * 64 * 4, 0);
        CHECK_FALSE(writer.WriteVideo(&frame[0], 64 * 4, 0, 333333));
        std::vector<int16_t> stereo(960 * kRecordAudioChannels, 100);
        for (int i = 0; i < 50; ++i)
            CHECK(writer.WriteAudio(&stereo[0], 960, static_cast<int64_t>(i) * 200000));
        CHECK(writer.Close());
    }
    ::MFShutdown();
    ::CoUninitialize();
    const AudioOnlyInfo info = readAudioOnly(path);
    REQUIRE(info.m_opened);
    CHECK_FALSE(info.m_hasVideo);
    CHECK(info.m_audioSamples > 0);
    CHECK(info.m_durationMs >= 900);
    CHECK(info.m_durationMs <= 1100);
    ::DeleteFileW(path.c_str());
}
