/**
 *  CaptionRecordTapTests.cpp
 *
 *  录屏接入语音字幕的单元测试（docs/语音字幕方案.md 6.1），标签 [captionrec]：
 *    CAPRT1 —— 混音器的分路旁路：每取出一段，打开的每一路各交一次，带上录制时间轴上的时刻；数据不够的部分补静音；
 *              不带时刻（ptsMs 为负）时不旁路；关闭的一路不交；取消登记后不再交；
 *    CAPRT2 —— 旁路到草稿会话：本人麦克风交给 PushMic、电脑声音交给 PushSystem，其他人的声音不交；按勾选只交选中的路；
 *    CAPRT3 —— 解除草稿会话后不再交；SetSources 改变要识别的路；
 *    CAPRT4 —— 录制线程真实运行一段：草稿会话收到的时刻从 0 起连续递增、每段 20 毫秒，与录像时间轴一致；
 *    CAPRT5 —— 录制中关闭字幕：关闭期间交给草稿会话的是同样长度、同样时刻的静音，重新打开后恢复原声。
 *
 *  草稿会话用记录调用的替身，不加载字幕 DLL。
 *
 *  balloonwj@qq.com   2026-10-06
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <mutex>
#include <string>
#include <vector>

#include "../../FlamingoMedia/Caption/RecordCaptionTap.h"
#include "../../FlamingoMedia/Record/RecordAudioMixer.h"
#include "../../FlamingoMedia/Record/RecordSession.h"

using namespace screenrecord;

namespace {

//每段的采样数（48 kHz 下 20 毫秒）
const size_t kChunk = 960;

//一次旁路调用的记录
struct TapCall
{
    int                  m_source;
    std::vector<int16_t> m_pcm;
    int64_t              m_ptsMs;
};

//记录旁路调用的替身
class RecordingTap : public IRecordAudioTap
{
public:
    virtual void OnRecordAudio(int source, const int16_t* pcm, size_t samples, int64_t ptsMs) override
    {
        TapCall c;
        c.m_source = source;
        c.m_pcm.assign(pcm, pcm + samples);
        c.m_ptsMs = ptsMs;
        m_calls.push_back(c);
    }

    std::vector<TapCall> m_calls;
};

//记录调用的字幕草稿会话替身（写入线程与测试线程都会访问，加锁）
class FakeCaptionRecording : public caption::ICaptionRecording
{
public:
    FakeCaptionRecording()
        : m_finished(false)
    {
    }

    virtual void PushMic(const int16_t* mono48k, uint32_t samples, int64_t ptsMs) override
    {
        bool silent = true;
        for (uint32_t i = 0; i < samples && silent; ++i)
            silent = (mono48k[i] == 0);
        std::lock_guard<std::mutex> lock(m_mutex);
        m_micPts.push_back(ptsMs);
        m_micSamples.push_back(samples);
        m_micSilent.push_back(silent);
    }

    virtual void PushSystem(const int16_t* mono48k, uint32_t samples, int64_t ptsMs) override
    {
        (void)mono48k;
        (void)samples;
        std::lock_guard<std::mutex> lock(m_mutex);
        m_systemPts.push_back(ptsMs);
    }

    virtual void AddRemoteCaption(const wchar_t* speaker, const wchar_t* text, int64_t startMs, int64_t endMs) override
    {
        (void)speaker;
        (void)text;
        (void)startMs;
        (void)endMs;
    }

    virtual void SetPip(bool present, double left, double top, double right, double bottom) override
    {
        (void)present;
        (void)left;
        (void)top;
        (void)right;
        (void)bottom;
    }

    virtual void Finish(int64_t durationMs) override
    {
        (void)durationMs;
        m_finished = true;
    }

    virtual void Cancel() override
    {
    }

    virtual void SetObserver(caption::ICaptionRecordingObserver* observer) override
    {
        (void)observer;
    }

    std::vector<int64_t> MicPts()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_micPts;
    }

    std::mutex            m_mutex;
    std::vector<int64_t>  m_micPts;       //PushMic 收到的时刻
    std::vector<uint32_t> m_micSamples;   //PushMic 收到的采样数
    std::vector<bool>     m_micSilent;    //PushMic 收到的这一段是否全为 0
    std::vector<int64_t>  m_systemPts;    //PushSystem 收到的时刻
    bool                  m_finished;     //是否调过 Finish
};

//纯色画面来源（CAPRT4 用）
class SolidSource : public IRecordVideoSource
{
public:
    virtual bool GetRecordFrame(std::vector<uint8_t>& bgra, int& width, int& height, uint64_t& serial) override
    {
        //与录制流水线用例 REC8 相同的 320×180：过小的画面可能建不起 H.264 编码器
        const int kW = 320;
        const int kH = 180;
        bgra.assign(static_cast<size_t>(kW) * kH * 4, 0x80);
        width = kW;
        height = kH;
        serial = 1;
        return true;
    }
};

//磁盘空间总是够用（CAPRT4 用）
uint64_t plentyOfSpace(const std::wstring& dir)
{
    (void)dir;
    return 100ULL * 1024 * 1024 * 1024;
}

} // namespace

TEST_CASE("CAPRT1 混音器的分路旁路：按路交出、带时刻、补静音；不带时刻、关闭的路、取消登记后都不交", "[captionrec]")
{
    RecordAudioMixer mixer;
    RecordingTap tap;
    mixer.SetTap(&tap);
    mixer.SetEnabled(record_audio_mic, true);
    mixer.SetEnabled(record_audio_system, true);
    std::vector<int16_t> mic(kChunk, 1000);
    std::vector<int16_t> sys(kChunk / 2, 2000);
    mixer.Push(record_audio_mic, &mic[0], mic.size());
    mixer.Push(record_audio_system, &sys[0], sys.size());
    std::vector<int16_t> out(kChunk);
    mixer.Pull(&out[0], out.size(), 40);
    REQUIRE(tap.m_calls.size() == 2);
    CHECK(tap.m_calls[0].m_source == record_audio_mic);
    CHECK(tap.m_calls[0].m_ptsMs == 40);
    REQUIRE(tap.m_calls[0].m_pcm.size() == kChunk);
    CHECK(tap.m_calls[0].m_pcm[kChunk - 1] == 1000);
    CHECK(tap.m_calls[1].m_source == record_audio_system);
    REQUIRE(tap.m_calls[1].m_pcm.size() == kChunk);
    CHECK(tap.m_calls[1].m_pcm[0] == 2000);
    //电脑声音只有半段：后半段补成静音
    CHECK(tap.m_calls[1].m_pcm[kChunk / 2] == 0);
    CHECK(tap.m_calls[1].m_pcm[kChunk - 1] == 0);
    //混音本身不受旁路影响
    CHECK(out[0] == 3000);
    CHECK(out[kChunk - 1] == 1000);

    //不带时刻：不旁路
    tap.m_calls.clear();
    mixer.Pull(&out[0], out.size());
    CHECK(tap.m_calls.empty());
    //关闭电脑声音：只交麦克风（没有数据时整段静音）
    mixer.SetEnabled(record_audio_system, false);
    mixer.Pull(&out[0], out.size(), 60);
    REQUIRE(tap.m_calls.size() == 1);
    CHECK(tap.m_calls[0].m_source == record_audio_mic);
    CHECK(tap.m_calls[0].m_pcm[0] == 0);
    //取消登记
    tap.m_calls.clear();
    mixer.SetTap(NULL);
    mixer.Pull(&out[0], out.size(), 80);
    CHECK(tap.m_calls.empty());
}

TEST_CASE("CAPRT2 旁路到草稿会话：麦克风交 PushMic、电脑声音交 PushSystem，其他人的声音不交；按勾选只交选中的路", "[captionrec]")
{
    FakeCaptionRecording rec;
    flamingomedia::CRecordCaptionTap tap;
    CHECK_FALSE(tap.IsAttached());
    tap.Attach(&rec, true, false);
    CHECK(tap.IsAttached());
    std::vector<int16_t> pcm(kChunk, 7);
    tap.OnRecordAudio(record_audio_mic, &pcm[0], pcm.size(), 0);
    tap.OnRecordAudio(record_audio_others, &pcm[0], pcm.size(), 0);
    tap.OnRecordAudio(record_audio_system, &pcm[0], pcm.size(), 0);
    REQUIRE(rec.m_micPts.size() == 1);
    CHECK(rec.m_micSamples[0] == kChunk);
    CHECK(rec.m_systemPts.empty());

    //两路都勾：电脑声音也交
    tap.Attach(&rec, true, true);
    tap.OnRecordAudio(record_audio_system, &pcm[0], pcm.size(), 20);
    REQUIRE(rec.m_systemPts.size() == 1);
    CHECK(rec.m_systemPts[0] == 20);
}

TEST_CASE("CAPRT3 解除草稿会话后不再交；SetSources 改变要识别的路", "[captionrec]")
{
    FakeCaptionRecording rec;
    flamingomedia::CRecordCaptionTap tap;
    tap.Attach(&rec, true, true);
    std::vector<int16_t> pcm(kChunk, 7);
    tap.SetSources(false, true);
    tap.OnRecordAudio(record_audio_mic, &pcm[0], pcm.size(), 0);
    tap.OnRecordAudio(record_audio_system, &pcm[0], pcm.size(), 0);
    CHECK(rec.m_micPts.empty());
    CHECK(rec.m_systemPts.size() == 1);
    CHECK(tap.Detach() == &rec);
    CHECK_FALSE(tap.IsAttached());
    tap.OnRecordAudio(record_audio_system, &pcm[0], pcm.size(), 20);
    CHECK(rec.m_systemPts.size() == 1);
    CHECK(tap.Detach() == NULL);
}

TEST_CASE("CAPRT4 录制线程真实运行一段：草稿会话收到的时刻从 0 起连续递增、每段 20 毫秒", "[captionrec]")
{
    wchar_t tempDir[MAX_PATH] = { 0 };
    ::GetTempPathW(MAX_PATH, tempDir);
    const std::wstring path = std::wstring(tempDir) + L"caprt4_record.mp4";
    ::DeleteFileW(path.c_str());

    FakeCaptionRecording rec;
    flamingomedia::CRecordCaptionTap tap;
    tap.Attach(&rec, true, false);
    SolidSource video;
    RecordSession session;
    session.SetFreeSpaceFn(&plentyOfSpace);
    session.Audio().SetEnabled(record_audio_mic, true);
    session.Audio().SetTap(&tap);
    RecordOptions opt;
    opt.m_path = path;
    opt.m_sourceWidth = 320;
    opt.m_sourceHeight = 180;
    REQUIRE(session.Start(opt, &video, NULL, NULL));
    //写入线程打开文件（建编码器）要几百毫秒，等它开始计时之后再录一段（与 REC8 相同）
    const int kWaitSteps = 100;
    const DWORD kWaitStepMs = 20;
    for (int i = 0; i < kWaitSteps && session.RecordedMs() == 0; ++i)
        ::Sleep(kWaitStepMs);
    REQUIRE(session.RecordedMs() > 0);
    const DWORD kRunMs = 600;
    ::Sleep(kRunMs);
    session.Stop();
    session.Audio().SetTap(NULL);
    CHECK(tap.Detach() == &rec);

    const std::vector<int64_t> pts = rec.MicPts();
    //600 毫秒至少写出二十多段（机器繁忙时放宽到 10 段）
    REQUIRE(pts.size() >= 10);
    CHECK(pts[0] == 0);
    const int64_t kChunkMs = 20;
    for (size_t i = 1; i < pts.size(); ++i)
        CHECK(pts[i] - pts[i - 1] == kChunkMs);
    ::DeleteFileW(path.c_str());
}

TEST_CASE("CAPRT5 录制中关闭字幕：关闭期间交给草稿会话的是同样长度、同样时刻的静音，重新打开后恢复原声", "[captionrec]")
{
    //一段非静音的声音
    const int16_t kTone = 1000;
    std::vector<int16_t> pcm(kChunk, kTone);
    FakeCaptionRecording rec;
    flamingomedia::CRecordCaptionTap tap;
    tap.Attach(&rec, true, false);
    CHECK_FALSE(tap.IsSuspended());
    const int64_t kFirstPts = 0;
    const int64_t kSecondPts = 20;
    const int64_t kThirdPts = 40;
    tap.OnRecordAudio(record_audio_mic, &pcm[0], pcm.size(), kFirstPts);
    tap.SetSuspended(true);
    CHECK(tap.IsSuspended());
    tap.OnRecordAudio(record_audio_mic, &pcm[0], pcm.size(), kSecondPts);
    tap.SetSuspended(false);
    tap.OnRecordAudio(record_audio_mic, &pcm[0], pcm.size(), kThirdPts);
    CHECK(tap.Detach() == &rec);

    const size_t kCalls = 3;
    REQUIRE(rec.m_micPts.size() == kCalls);
    CHECK(rec.m_micPts[0] == kFirstPts);
    CHECK(rec.m_micPts[1] == kSecondPts);
    CHECK(rec.m_micPts[2] == kThirdPts);
    for (size_t i = 0; i < kCalls; ++i)
        CHECK(rec.m_micSamples[i] == kChunk);
    CHECK_FALSE(rec.m_micSilent[0]);
    CHECK(rec.m_micSilent[1]);
    CHECK_FALSE(rec.m_micSilent[2]);
    //关闭期间原声缓冲本身不被改动
    CHECK(pcm[0] == kTone);
}
