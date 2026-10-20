/**
 *  屏幕录制管线的单元测试（Suite REC，标签 [record]），RecordPipelineTests.cpp
 *
 *  覆盖 FlamingoMedia/Record（docs/屏幕共享与录制方案.md 第 6 节、第 7 期 7a）：
 *    - REC1 输出尺寸：长边不超过 1920、取偶数、不小于下限；帧率夹到 1~30；
 *    - REC2 码率：两档画质的 1080p 基准，按像素数换算，不低于下限；
 *    - REC3 文件名：「FlamingoRecord_年月日_时分秒.mp4」，重名加序号（第 13 期由「Flamingo录屏_」改为英文）；
 *    - SSREC1 默认录像目录：「视频」文件夹下的 FlamingoRecords，末尾有没有反斜杠都只隔一个，取不到「视频」文件夹时为空；
 *    - SSREC2 录制完成提示（第 14 期）：从路径取出文件名，时长与大小一行的写法；
 *    - REC4 画中画：右下角、宽为画面的五分之一、16:9、偶数；
 *    - REC5 磁盘空间门限与时长文字；
 *    - REC6 混音：各路相加、限幅、不足补静音、关闭的一路丢弃、清空；
 *    - REC7 画面合成：等比缩放留黑边、画中画、标注叠在画面所占的矩形里；
 *    - REC8 录制一段：写出的 MP4 能读回，时长不含暂停的时段（也不含打开文件、建编码器的时间），有画面与声音两路；
 *    - REC9 磁盘空间不够时不能开始；录制中空间不够时自动停止并保存；
 *    - REC10 框选区域：与拖动方向无关、限定在显示器内、太小的区域不可用；
 *    - REC16 「高清」画质（2026-10-07）：长边上限 2560，2K 屏不缩小、4K 屏缩到 2560，「标准」仍是 1920；码率按像素数提高，
 *            同一尺寸下「高清」是「标准」的 2 倍；
 *    - REC17 「高清」录 2K 画面：经真实的录制管线与本机 H.264 编码器（允许硬件编码器）写出 2560×1440 的 MP4，读回有画面。
 *    - REC18 同一个录制会话连录两段：第一段用画笔画过的笔迹，不带进第二段（第二段没有交过笔迹）；
 *    - REC19（2026-10-15，A7）画面合成缩小时用 BgraDownscaler（与它逐字节一致）、原尺寸时逐行复制（含行跨度大于宽度），
 *            画中画照常叠加；放大仍走 HALFTONE（REC7）。
 *  框架 Catch2 v3。REC8、REC9 写临时文件（系统临时目录），用完删除。
 *  balloonwj@qq.com   2026-10-03
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
#include "../../FlamingoMedia/Record/RecordFrameComposer.h"
#include "../../FlamingoMedia/Record/RecordPolicy.h"
#include "../../FlamingoMedia/Record/RecordSession.h"
#include "../../FlamingoMedia/Share/ShareFrameDrawer.h"   // BgraDownscaler：REC19 的对照结果

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")

using namespace screenrecord;

namespace {

//纯色画面来源：每次取帧序号加一
class SolidSource : public IRecordVideoSource
{
public:
    SolidSource(int w, int h, uint8_t r, uint8_t g, uint8_t b)
        : m_w(w)
        , m_h(h)
        , m_serial(0)
    {
        m_frame.resize(static_cast<size_t>(w) * h * 4);
        for (size_t i = 0; i < m_frame.size(); i += 4)
        {
            m_frame[i] = b;
            m_frame[i + 1] = g;
            m_frame[i + 2] = r;
            m_frame[i + 3] = 255;
        }
    }

    virtual bool GetRecordFrame(std::vector<uint8_t>& bgra, int& width, int& height, uint64_t& serial) override
    {
        bgra = m_frame;
        width = m_w;
        height = m_h;
        serial = ++m_serial;
        return true;
    }

private:
    int                   m_w;
    int                   m_h;
    std::atomic<uint64_t> m_serial;
    std::vector<uint8_t>  m_frame;
};

//记下结束回报
class StopListener : public RecordSessionListener
{
public:
    StopListener()
        : m_reason(0)
        , m_durationMs(-1)
    {
    }

    virtual void OnRecordStopped(int reason, const std::wstring& path, int64_t durationMs) override
    {
        m_path = path;
        m_durationMs = durationMs;
        m_reason.store(reason);
    }

    std::atomic<int> m_reason;
    std::wstring     m_path;
    int64_t          m_durationMs;
};

//读回 MP4：总时长（毫秒）、视频与声音的样本数
struct Mp4Info
{
    bool    m_opened;
    int64_t m_durationMs;
    int     m_videoSamples;
    int     m_audioSamples;
};

Mp4Info readMp4(const std::wstring& path)
{
    Mp4Info info = { false, 0, 0, 0 };
    ::CoInitializeEx(NULL, COINIT_MULTITHREADED);
    ::MFStartup(MF_VERSION);
    IMFSourceReader* reader = NULL;
    if (SUCCEEDED(::MFCreateSourceReaderFromURL(path.c_str(), NULL, &reader)))
    {
        info.m_opened = true;
        PROPVARIANT var;
        PropVariantInit(&var);
        if (SUCCEEDED(reader->GetPresentationAttribute(static_cast<DWORD>(MF_SOURCE_READER_MEDIASOURCE), MF_PD_DURATION, &var)))
            info.m_durationMs = static_cast<int64_t>(var.uhVal.QuadPart / 10000);
        PropVariantClear(&var);
        for (;;)
        {
            DWORD stream = 0;
            DWORD flags = 0;
            LONGLONG ts = 0;
            IMFSample* sample = NULL;
            if (FAILED(reader->ReadSample(static_cast<DWORD>(MF_SOURCE_READER_ANY_STREAM), 0, &stream, &flags, &ts, &sample)))
                break;
            if (sample != NULL)
            {
                IMFMediaType* type = NULL;
                GUID major = GUID_NULL;
                if (SUCCEEDED(reader->GetCurrentMediaType(stream, &type)))
                {
                    type->GetGUID(MF_MT_MAJOR_TYPE, &major);
                    type->Release();
                }
                if (major == MFMediaType_Video)
                    ++info.m_videoSamples;
                else if (major == MFMediaType_Audio)
                    ++info.m_audioSamples;
                sample->Release();
            }
            if ((flags & MF_SOURCE_READERF_ENDOFSTREAM) != 0)
            {
                //两路都读完才算结束：取下一路，直到读不出
                bool more = false;
                for (DWORD s = 0; s < 2; ++s)
                {
                    BOOL selected = FALSE;
                    if (SUCCEEDED(reader->GetStreamSelection(s, &selected)) && selected && s != stream)
                        more = true;
                }
                reader->SetStreamSelection(stream, FALSE);
                if (!more)
                    break;
            }
        }
        reader->Release();
    }
    ::MFShutdown();
    ::CoUninitialize();
    return info;
}

std::wstring tempMp4(const wchar_t* name)
{
    wchar_t dir[MAX_PATH] = { 0 };
    ::GetTempPathW(MAX_PATH, dir);
    return std::wstring(dir) + name;
}

uint64_t freeLow(const std::wstring& dir)
{
    (void)dir;
    return 100ULL * 1024 * 1024;
}

//第一次取时够、之后不够
std::atomic<int> g_freeCalls(0);
uint64_t freeThenLow(const std::wstring& dir)
{
    (void)dir;
    return (g_freeCalls++ == 0) ? 600ULL * 1024 * 1024 : 100ULL * 1024 * 1024;
}

//按实时节奏推入 1 kHz 声音的线程
struct ToneFeeder
{
    RecordSession*     m_session;   //推给哪个录制
    std::atomic<bool>* m_stop;      //停止标志
};

void toneProc(ToneFeeder* f)
{
    std::vector<int16_t> pcm(480);
    int64_t n = 0;
    while (!f->m_stop->load())
    {
        for (size_t i = 0; i < pcm.size(); ++i, ++n)
            pcm[i] = static_cast<int16_t>(8000 * sin(6.283185307179586 * 1000.0 * n / 48000.0));
        f->m_session->Audio().Push(record_audio_mic, &pcm[0], pcm.size());
        ::Sleep(10);
    }
}

//读合成结果里一个像素
void pixelAt(const RecordFrameComposer& c, int x, int y, int& r, int& g, int& b)
{
    const uint8_t* p = c.Pixels() + static_cast<size_t>(y) * c.Stride() + static_cast<size_t>(x) * 4;
    b = p[0];
    g = p[1];
    r = p[2];
}

} // namespace

TEST_CASE("REC1 输出尺寸与帧率", "[record]")
{
    int w = 0;
    int h = 0;
    ComputeRecordSize(2560, 1440, w, h);
    CHECK(w == 1920);
    CHECK(h == 1080);
    ComputeRecordSize(1920, 1200, w, h);
    CHECK(w == 1920);
    CHECK(h == 1200);
    ComputeRecordSize(1365, 767, w, h);
    CHECK(w == 1364);
    CHECK(h == 766);
    ComputeRecordSize(3840, 1600, w, h);
    CHECK(w == 1920);
    CHECK(h == 800);
    ComputeRecordSize(1000, 4000, w, h);
    CHECK(w == 480);
    CHECK(h == 1920);
    ComputeRecordSize(10, 10, w, h);
    CHECK(w == kRecordMinSide);
    CHECK(h == kRecordMinSide);
    CHECK(ClampRecordFps(0) == 1);
    CHECK(ClampRecordFps(60) == 30);
    CHECK(ClampRecordFps(15) == 15);
}

TEST_CASE("REC2 码率", "[record]")
{
    CHECK(RecordVideoBitrate(record_quality_standard, 1920, 1080) == kRecordStandardBitrate1080p);
    CHECK(RecordVideoBitrate(record_quality_high, 1920, 1080) == kRecordHighBitrate1080p);
    CHECK(RecordVideoBitrate(record_quality_standard, 960, 540) == kRecordStandardBitrate1080p / 4);
    CHECK(RecordVideoBitrate(record_quality_standard, 64, 64) == kRecordMinBitrate);
}

TEST_CASE("REC3 文件名", "[record]")
{
    SYSTEMTIME t;
    memset(&t, 0, sizeof(t));
    t.wYear = 2026;
    t.wMonth = 10;
    t.wDay = 3;
    t.wHour = 8;
    t.wMinute = 30;
    t.wSecond = 12;
    CHECK(MakeRecordFileName(t, 0) == L"FlamingoRecord_20261003_083012.mp4");
    CHECK(MakeRecordFileName(t, 2) == L"FlamingoRecord_20261003_083012_2.mp4");
}

TEST_CASE("SSREC1 默认录像目录", "[record]")
{
    CHECK(MakeDefaultRecordFolder(L"C:\\Users\\a\\Videos") == L"C:\\Users\\a\\Videos\\FlamingoRecords");
    //末尾已带反斜杠时不再多加一个
    CHECK(MakeDefaultRecordFolder(L"D:\\Media\\Videos\\") == L"D:\\Media\\Videos\\FlamingoRecords");
    //取不到「视频」文件夹：返回空，录制控制器据此提示「无法创建录制文件，请检查保存目录」
    CHECK(MakeDefaultRecordFolder(L"") == L"");
}

TEST_CASE("SSREC2 录制完成提示的文件名与时长、大小", "[record]")
{
    CHECK(RecordFileNameOf(L"C:\\Users\\a\\Videos\\FlamingoRecords\\FlamingoRecord_20261003_083012.mp4") ==
          L"FlamingoRecord_20261003_083012.mp4");
    //正斜杠、没有目录、以分隔符结尾、空串
    CHECK(RecordFileNameOf(L"D:/rec/FlamingoRecord_20261003_083012_2.mp4") == L"FlamingoRecord_20261003_083012_2.mp4");
    CHECK(RecordFileNameOf(L"FlamingoRecord_20261003_083012.mp4") == L"FlamingoRecord_20261003_083012.mp4");
    CHECK(RecordFileNameOf(L"C:\\rec\\") == L"");
    CHECK(RecordFileNameOf(L"") == L"");

    //3355443 字节约为 3.19999 MB，保留一位小数为 3.2
    CHECK(FormatRecordDoneDetail(65000, 3355443) == L"时长 01:05 · 3.2 MB");
    CHECK(FormatRecordDoneDetail(3725000, 1048576) == L"时长 1:02:05 · 1.0 MB");
    CHECK(FormatRecordDoneDetail(0, 0) == L"时长 00:00 · 0.0 MB");
}

TEST_CASE("REC4 画中画的位置", "[record]")
{
    const RECT rc = ComputePipRect(1920, 1080);
    CHECK(rc.right - rc.left == 384);
    CHECK(rc.bottom - rc.top == 216);
    CHECK(rc.right == 1920 - 32);
    CHECK(rc.bottom == 1080 - 32);
    const RECT little = ComputePipRect(640, 360);
    CHECK((little.right - little.left) % 2 == 0);
    CHECK((little.bottom - little.top) % 2 == 0);
    CHECK(little.right <= 640);
    CHECK(little.bottom <= 360);
}

TEST_CASE("REC5 磁盘空间门限与时长文字", "[record]")
{
    CHECK(RecordCanStart(600ULL * 1024 * 1024));
    CHECK_FALSE(RecordCanStart(400ULL * 1024 * 1024));
    CHECK(RecordCanContinue(250ULL * 1024 * 1024));
    CHECK_FALSE(RecordCanContinue(150ULL * 1024 * 1024));
    CHECK(FormatRecordDuration(0) == L"00:00");
    CHECK(FormatRecordDuration(65000) == L"01:05");
    CHECK(FormatRecordDuration(3725000) == L"1:02:05");
}

TEST_CASE("REC6 混音", "[record]")
{
    RecordAudioMixer mixer;
    mixer.SetEnabled(record_audio_mic, true);
    mixer.SetEnabled(record_audio_system, true);
    std::vector<int16_t> mic(960, 1000);
    std::vector<int16_t> sys(480, 2000);
    mixer.Push(record_audio_mic, &mic[0], mic.size());
    mixer.Push(record_audio_system, &sys[0], sys.size());
    //关闭的一路推入的丢弃
    std::vector<int16_t> others(960, 5000);
    mixer.Push(record_audio_others, &others[0], others.size());
    CHECK(mixer.Available(record_audio_others) == 0);
    std::vector<int16_t> out(960, -1);
    mixer.Pull(&out[0], out.size());
    CHECK(out[0] == 3000);
    CHECK(out[479] == 3000);
    CHECK(out[480] == 1000);
    CHECK(out[959] == 1000);
    //什么都没有时全是静音
    mixer.Pull(&out[0], out.size());
    CHECK(out[0] == 0);
    CHECK(out[959] == 0);
    //限幅
    std::vector<int16_t> loud(10, 30000);
    mixer.Push(record_audio_mic, &loud[0], loud.size());
    mixer.Push(record_audio_system, &loud[0], loud.size());
    mixer.Pull(&out[0], 10);
    CHECK(out[0] == 32767);
    //清空
    mixer.Push(record_audio_mic, &mic[0], mic.size());
    mixer.Clear();
    CHECK(mixer.Available(record_audio_mic) == 0);
}

TEST_CASE("REC7 画面合成", "[record]")
{
    RecordFrameComposer c;
    REQUIRE(c.Init(320, 180));
    //同宽高比：铺满
    std::vector<uint8_t> red(static_cast<size_t>(160) * 90 * 4);
    for (size_t i = 0; i < red.size(); i += 4)
    {
        red[i] = 0;
        red[i + 1] = 0;
        red[i + 2] = 255;
        red[i + 3] = 255;
    }
    c.Compose(&red[0], 160, 90, 160 * 4, NULL, NULL, 0, 0);
    int r = 0;
    int g = 0;
    int b = 0;
    pixelAt(c, 5, 5, r, g, b);
    CHECK(r > 200);
    CHECK(g < 50);
    //方形来源：左右留黑边
    std::vector<uint8_t> square(static_cast<size_t>(100) * 100 * 4);
    for (size_t i = 0; i < square.size(); i += 4)
    {
        square[i] = 0;
        square[i + 1] = 0;
        square[i + 2] = 255;
        square[i + 3] = 255;
    }
    c.Compose(&square[0], 100, 100, 100 * 4, NULL, NULL, 0, 0);
    CHECK(c.ContentRect().left == 70);
    CHECK(c.ContentRect().right == 250);
    pixelAt(c, 10, 90, r, g, b);
    CHECK(r < 20);
    pixelAt(c, 160, 90, r, g, b);
    CHECK(r > 200);
    //画中画：绿色，画在右下角
    std::vector<uint8_t> green(static_cast<size_t>(64) * 36 * 4);
    for (size_t i = 0; i < green.size(); i += 4)
    {
        green[i] = 0;
        green[i + 1] = 255;
        green[i + 2] = 0;
        green[i + 3] = 255;
    }
    //标注：从内容左上角到右下角的一笔蓝色粗线
    std::vector<screenshare::AnnotationStroke> strokes(1);
    strokes[0].m_tool = screenshare::annot_tool_pen;
    strokes[0].m_color = 0x0000FF;
    strokes[0].m_width = 200;
    strokes[0].m_points.push_back(screenshare::AnnotationPoint(0, 0));
    strokes[0].m_points.push_back(screenshare::AnnotationPoint(65535, 65535));
    c.Compose(&square[0], 100, 100, 100 * 4, &strokes, &green[0], 64, 36);
    const RECT pip = ComputePipRect(320, 180);
    pixelAt(c, (pip.left + pip.right) / 2, (pip.top + pip.bottom) / 2, r, g, b);
    CHECK(g > 200);
    CHECK(r < 50);
    //内容矩形的中心落在蓝线上
    pixelAt(c, 160, 90, r, g, b);
    CHECK(b > 200);
    CHECK(r < 50);
}

TEST_CASE("REC8 录制一段，读回时长不含暂停", "[record]")
{
    const std::wstring path = tempMp4(L"flamingo_rec8_test.mp4");
    ::DeleteFileW(path.c_str());
    SolidSource source(320, 180, 200, 40, 40);
    StopListener listener;
    RecordSession session;
    RecordOptions opt;
    opt.m_path = path;
    opt.m_sourceWidth = 320;
    opt.m_sourceHeight = 180;
    opt.m_fps = 15;
    opt.m_quality = record_quality_standard;
    session.Audio().SetEnabled(record_audio_mic, true);
    REQUIRE(session.Start(opt, &source, NULL, &listener));
    CHECK(session.IsRunning());
    //一边录一边按实时节奏推入 1 kHz 的声音
    std::atomic<bool> stopTone(false);
    ToneFeeder feeder = { &session, &stopTone };
    std::thread tone(toneProc, &feeder);
    //写入线程打开文件（建编码器）要几百毫秒，开始计时之后再按墙钟安排暂停
    for (int i = 0; i < 100 && session.RecordedMs() == 0; ++i)
        ::Sleep(20);
    CHECK(session.RecordedMs() > 0);
    ::Sleep(600);
    session.SetPaused(true);
    CHECK(session.IsPaused());
    ::Sleep(500);
    session.SetPaused(false);
    ::Sleep(600);
    session.Stop();
    stopTone.store(true);
    tone.join();
    CHECK_FALSE(session.IsRunning());
    CHECK(listener.m_reason.load() == record_stop_user);
    CHECK(listener.m_path == path);
    CHECK(listener.m_durationMs >= 1000);
    CHECK(listener.m_durationMs <= 1500);
    const Mp4Info info = readMp4(path);
    REQUIRE(info.m_opened);
    CHECK(info.m_durationMs >= 900);
    CHECK(info.m_durationMs <= 1600);
    CHECK(info.m_videoSamples >= 12);
    CHECK(info.m_audioSamples > 0);
    ::DeleteFileW(path.c_str());
}

TEST_CASE("REC9 磁盘空间不够", "[record]")
{
    SolidSource source(160, 90, 10, 10, 10);
    RecordOptions opt;
    opt.m_path = tempMp4(L"flamingo_rec9_test.mp4");
    opt.m_sourceWidth = 160;
    opt.m_sourceHeight = 90;
    opt.m_fps = 10;
    ::DeleteFileW(opt.m_path.c_str());
    //开始前就不够
    {
        RecordSession session;
        session.SetFreeSpaceFn(&freeLow);
        CHECK_FALSE(session.Start(opt, &source, NULL, NULL));
        CHECK(session.FailReason() == record_fail_disk_full);
    }
    //录制中变得不够：自动停止并保存
    {
        g_freeCalls.store(0);
        StopListener listener;
        RecordSession session;
        session.SetFreeSpaceFn(&freeThenLow);
        session.SetDiskCheckIntervalMs(200);
        REQUIRE(session.Start(opt, &source, NULL, &listener));
        for (int i = 0; i < 50 && listener.m_reason.load() == 0; ++i)
            ::Sleep(50);
        CHECK(listener.m_reason.load() == record_stop_disk_full);
        session.Stop();
        CHECK_FALSE(session.IsRunning());
        const Mp4Info info = readMp4(opt.m_path);
        CHECK(info.m_opened);
    }
    ::DeleteFileW(opt.m_path.c_str());
}

TEST_CASE("REC10 框选区域", "[record]")
{
    const RECT bounds = { 1920, 0, 3840, 1080 };
    POINT a = { 2500, 600 };
    POINT b = { 2100, 200 };
    RECT rc = NormalizeSelection(a, b, bounds);
    CHECK(rc.left == 2100);
    CHECK(rc.top == 200);
    CHECK(rc.right == 2500);
    CHECK(rc.bottom == 600);
    CHECK(IsSelectionUsable(rc));
    //拖出显示器：夹到边缘
    POINT c = { 1800, -50 };
    rc = NormalizeSelection(a, c, bounds);
    CHECK(rc.left == 1920);
    CHECK(rc.top == 0);
    //太小
    POINT d = { 2530, 640 };
    rc = NormalizeSelection(a, d, bounds);
    CHECK_FALSE(IsSelectionUsable(rc));
    //完全在显示器以外
    POINT e = { 100, 100 };
    POINT f = { 200, 200 };
    rc = NormalizeSelection(e, f, bounds);
    CHECK(rc.right - rc.left == 0);
}

namespace {

//SSME13：数一数录制线程调了几次装饰，记下画面尺寸
class CountingDecorator : public IRecordFrameDecorator
{
public:
    CountingDecorator()
        : m_calls(0)
        , m_width(0)
        , m_height(0)
    {
    }

    virtual void Decorate(uint8_t* bgra, int width, int height, int64_t nowMs) override
    {
        (void)nowMs;
        if (bgra != NULL)
            bgra[0] = 0;
        m_width.store(width);
        m_height.store(height);
        m_calls.fetch_add(1);
    }

    std::atomic<int> m_calls;    //调用次数
    std::atomic<int> m_width;    //最近一次的画面宽
    std::atomic<int> m_height;   //最近一次的画面高
};

} // namespace

TEST_CASE("SSME13 独立录屏：录制线程写每一帧之前都交给画面装饰，尺寸为原始画面的尺寸", "[record][mouseeffect]")
{
    const std::wstring path = tempMp4(L"flamingo_ssme13_test.mp4");
    ::DeleteFileW(path.c_str());
    SolidSource source(320, 180, 40, 160, 40);
    CountingDecorator deco;
    RecordSession session;
    session.SetDecorator(&deco);
    RecordOptions opt;
    opt.m_path = path;
    opt.m_sourceWidth = 320;
    opt.m_sourceHeight = 180;
    opt.m_fps = 15;
    opt.m_quality = record_quality_standard;
    REQUIRE(session.Start(opt, &source, NULL, NULL));
    for (int i = 0; i < 100 && session.RecordedMs() == 0; ++i)
        ::Sleep(20);
    ::Sleep(500);
    session.Stop();
    CHECK(deco.m_calls.load() >= 3);
    CHECK(deco.m_width.load() == 320);
    CHECK(deco.m_height.load() == 180);
    ::DeleteFileW(path.c_str());
}

TEST_CASE("REC16 high quality keeps up to 2560 on the long side", "[record]")
{
    int w = 0;
    int h = 0;
    CHECK(RecordMaxLongSide(record_quality_standard) == kRecordMaxLongSide);
    CHECK(RecordMaxLongSide(record_quality_high) == kRecordHighMaxLongSide);
    //2K 屏：「高清」不缩小，「标准」缩到 1920
    ComputeRecordSize(2560, 1440, record_quality_high, w, h);
    CHECK(w == 2560);
    CHECK(h == 1440);
    ComputeRecordSize(2560, 1440, record_quality_standard, w, h);
    CHECK(w == 1920);
    CHECK(h == 1080);
    //4K 屏：「高清」缩到长边 2560
    ComputeRecordSize(3840, 2160, record_quality_high, w, h);
    CHECK(w == 2560);
    CHECK(h == 1440);
    //竖屏
    ComputeRecordSize(2160, 3840, record_quality_high, w, h);
    CHECK(w == 1440);
    CHECK(h == 2560);
    //1080p 不受影响
    ComputeRecordSize(1920, 1080, record_quality_high, w, h);
    CHECK(w == 1920);
    CHECK(h == 1080);
    //码率按像素数：2560×1440 的「高清」高于 1080p 的「高清」，同一尺寸下是「标准」的 2 倍
    CHECK(RecordVideoBitrate(record_quality_high, 2560, 1440) > kRecordHighBitrate1080p);
    CHECK(RecordVideoBitrate(record_quality_high, 2560, 1440) == 2 * RecordVideoBitrate(record_quality_standard, 2560, 1440));
}

namespace {

//读出 MP4 第一路视频的画面尺寸；读不到时返回 false
bool readMp4FrameSize(const std::wstring& path, UINT32& width, UINT32& height)
{
    width = 0;
    height = 0;
    ::CoInitializeEx(NULL, COINIT_MULTITHREADED);
    ::MFStartup(MF_VERSION);
    bool ok = false;
    IMFSourceReader* reader = NULL;
    if (SUCCEEDED(::MFCreateSourceReaderFromURL(path.c_str(), NULL, &reader)))
    {
        IMFMediaType* type = NULL;
        if (SUCCEEDED(reader->GetNativeMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), 0, &type)))
        {
            ok = SUCCEEDED(::MFGetAttributeSize(type, MF_MT_FRAME_SIZE, &width, &height));
            type->Release();
        }
        reader->Release();
    }
    ::MFShutdown();
    ::CoUninitialize();
    return ok;
}

} // namespace

TEST_CASE("REC17 high quality records a 2K source at 2560x1440 through the real encoder", "[record]")
{
    const std::wstring path = tempMp4(L"flamingo_rec12_test.mp4");
    ::DeleteFileW(path.c_str());
    //2K 屏的源画面、录制时长（毫秒）、等编码器开始计时的最多次数与间隔（毫秒）
    const int srcW = 2560;
    const int srcH = 1440;
    const DWORD recordMs = 800;
    const int waitTries = 100;
    const DWORD waitStepMs = 20;
    SolidSource source(srcW, srcH, 40, 120, 200);
    StopListener listener;
    RecordSession session;
    RecordOptions opt;
    opt.m_path = path;
    opt.m_sourceWidth = srcW;
    opt.m_sourceHeight = srcH;
    opt.m_fps = kRecordDefaultFps;
    opt.m_quality = record_quality_high;
    REQUIRE(session.Start(opt, &source, NULL, &listener));
    for (int i = 0; i < waitTries && session.RecordedMs() == 0; ++i)
        ::Sleep(waitStepMs);
    CHECK(session.RecordedMs() > 0);
    ::Sleep(recordMs);
    session.Stop();
    CHECK(listener.m_reason.load() == record_stop_user);
    const Mp4Info info = readMp4(path);
    REQUIRE(info.m_opened);
    CHECK(info.m_videoSamples > 0);
    UINT32 w = 0;
    UINT32 h = 0;
    REQUIRE(readMp4FrameSize(path, w, h));
    CHECK(w == static_cast<UINT32>(srcW));
    CHECK(h == static_cast<UINT32>(srcH));
    ::DeleteFileW(path.c_str());
}

namespace {

//读出 MP4 第 skip + 1 个视频帧正中的像素（按 RGB32 解码）；读不到时返回 false
bool readMp4CenterPixel(const std::wstring& path, int skip, int& r, int& g, int& b)
{
    r = -1;
    g = -1;
    b = -1;
    ::CoInitializeEx(NULL, COINIT_MULTITHREADED);
    ::MFStartup(MF_VERSION);
    bool ok = false;
    IMFAttributes* attrs = NULL;
    IMFSourceReader* reader = NULL;
    if (SUCCEEDED(::MFCreateAttributes(&attrs, 1)))
    {
        attrs->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);
        if (FAILED(::MFCreateSourceReaderFromURL(path.c_str(), attrs, &reader)))
            reader = NULL;
        attrs->Release();
    }
    IMFMediaType* want = NULL;
    if (reader != NULL && SUCCEEDED(::MFCreateMediaType(&want)))
    {
        want->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        want->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
        UINT32 fw = 0;
        UINT32 fh = 0;
        IMFMediaType* actual = NULL;
        if (SUCCEEDED(reader->SetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), NULL, want)) &&
            SUCCEEDED(reader->GetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), &actual)))
        {
            ::MFGetAttributeSize(actual, MF_MT_FRAME_SIZE, &fw, &fh);
            actual->Release();
        }
        for (int n = 0; n <= skip && fw > 0 && fh > 0; ++n)
        {
            DWORD flags = 0;
            LONGLONG ts = 0;
            IMFSample* sample = NULL;
            if (FAILED(reader->ReadSample(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), 0, NULL, &flags, &ts,
                                          &sample)) || (flags & MF_SOURCE_READERF_ENDOFSTREAM) != 0)
            {
                if (sample != NULL)
                    sample->Release();
                break;
            }
            if (sample == NULL)
                continue;
            IMFMediaBuffer* buf = NULL;
            if (n == skip && SUCCEEDED(sample->ConvertToContiguousBuffer(&buf)))
            {
                BYTE* data = NULL;
                DWORD len = 0;
                const DWORD bytesPerPixel = 4;
                if (SUCCEEDED(buf->Lock(&data, NULL, &len)))
                {
                    //正中那一点：上下翻转与否都落在同一行附近，不影响判断
                    const size_t offset = (static_cast<size_t>(fh / 2) * fw + fw / 2) * bytesPerPixel;
                    if (offset + bytesPerPixel <= len)
                    {
                        b = data[offset];
                        g = data[offset + 1];
                        r = data[offset + 2];
                        ok = true;
                    }
                    buf->Unlock();
                }
                buf->Release();
            }
            sample->Release();
        }
        want->Release();
    }
    if (reader != NULL)
        reader->Release();
    ::MFShutdown();
    ::CoUninitialize();
    return ok;
}

//等到已录时长（不含打开文件、建编码器的时间）达到 recordMs 毫秒；机器繁忙时建编码器可能要好几秒，最多等 15 秒
void waitRecorded(RecordSession& session, int64_t recordMs)
{
    const int waitTries = 750;
    const DWORD waitStepMs = 20;
    for (int i = 0; i < waitTries && session.RecordedMs() < recordMs; ++i)
        ::Sleep(waitStepMs);
}

//录一段：开始，录满 recordMs 毫秒后停止
bool recordOnce(RecordSession& session, SolidSource& source, const std::wstring& path, int64_t recordMs)
{
    RecordOptions opt;
    opt.m_path = path;
    opt.m_sourceWidth = 320;
    opt.m_sourceHeight = 180;
    opt.m_fps = 15;
    opt.m_quality = record_quality_standard;
    if (!session.Start(opt, &source, NULL, NULL))
        return false;
    waitRecorded(session, recordMs);
    session.Stop();
    return true;
}

} // namespace

TEST_CASE("REC18 同一个录制会话连录两段，第一段的笔迹不带进第二段", "[record]")
{
    const std::wstring first = tempMp4(L"flamingo_rec18_first.mp4");
    const std::wstring second = tempMp4(L"flamingo_rec18_second.mp4");
    ::DeleteFileW(first.c_str());
    ::DeleteFileW(second.c_str());
    //每段录制时长（毫秒）、读第几帧（跳过开头的几帧）
    const int64_t recordMs = 500;
    const int skipFrames = 3;
    //绿色画面上一笔从左上到右下的蓝色粗线（与 REC7 相同），正中的点落在线上
    SolidSource source(320, 180, 40, 160, 40);
    std::vector<screenshare::AnnotationStroke> strokes(1);
    strokes[0].m_tool = screenshare::annot_tool_pen;
    strokes[0].m_color = 0x0000FF;
    strokes[0].m_width = 200;
    strokes[0].m_points.push_back(screenshare::AnnotationPoint(0, 0));
    strokes[0].m_points.push_back(screenshare::AnnotationPoint(65535, 65535));
    RecordSession session;
    //第一段：录制开始之后交笔迹（与画笔模式相同的时机）
    RecordOptions opt;
    opt.m_path = first;
    opt.m_sourceWidth = 320;
    opt.m_sourceHeight = 180;
    opt.m_fps = 15;
    opt.m_quality = record_quality_standard;
    REQUIRE(session.Start(opt, &source, NULL, NULL));
    session.SetAnnotations(strokes);
    waitRecorded(session, recordMs);
    session.Stop();
    int r = 0;
    int g = 0;
    int b = 0;
    REQUIRE(readMp4CenterPixel(first, skipFrames, r, g, b));
    CHECK(b > 150);
    CHECK(g < 120);
    //第二段：没有交过笔迹，画面应是原来的绿色
    REQUIRE(recordOnce(session, source, second, recordMs));
    REQUIRE(readMp4CenterPixel(second, skipFrames, r, g, b));
    CHECK(g > 120);
    CHECK(b < 100);
    ::DeleteFileW(first.c_str());
    ::DeleteFileW(second.c_str());
}

namespace {

//REC19 的源画面：每个字节按位置取不同的值（相邻像素有差别，缩放错位能被看出来），alpha 为 255
std::vector<uint8_t> rec19Source(int w, int h, int stride)
{
    const uint8_t kOpaque = 255;
    std::vector<uint8_t> src(static_cast<size_t>(stride) * h, 0);
    for (int y = 0; y < h; ++y)
    {
        for (int x = 0; x < w; ++x)
        {
            uint8_t* p = &src[static_cast<size_t>(y) * stride + static_cast<size_t>(x) * 4];
            p[0] = static_cast<uint8_t>((x * 7 + y * 3) & 0xFF);
            p[1] = static_cast<uint8_t>((x * 5 + y * 11) & 0xFF);
            p[2] = static_cast<uint8_t>((x * 13 + y) & 0xFF);
            p[3] = kOpaque;
        }
    }
    return src;
}

//合成结果在 rc 里的 B、G、R 是否与 expect（行跨度 expectStride）逐字节相同
bool rec19RegionSame(const RecordFrameComposer& c, const RECT& rc, const uint8_t* expect, int expectStride)
{
    const int kColorBytes = 3;
    for (int y = rc.top; y < rc.bottom; ++y)
    {
        for (int x = rc.left; x < rc.right; ++x)
        {
            const uint8_t* got = c.Pixels() + static_cast<size_t>(y) * c.Stride() + static_cast<size_t>(x) * 4;
            const uint8_t* want = expect + static_cast<size_t>(y - rc.top) * expectStride
                                  + static_cast<size_t>(x - rc.left) * 4;
            for (int b = 0; b < kColorBytes; ++b)
            {
                if (got[b] != want[b])
                    return false;
            }
        }
    }
    return true;
}

} // namespace

TEST_CASE("REC19 画面合成缩小时用 BgraDownscaler、原尺寸时逐行复制，不再一律走 HALFTONE", "[record]")
{
    //docs/远程桌面H264与共享画质提升方案.md A7（2026-10-15）：2560×1440 合成进 1920×1080 的画布（「标准」画质录 2K 屏，
    //或观看者录制 2K 的共享），结果与 BgraDownscaler 直接缩放逐字节一致（只比较 B、G、R）
    const int srcW = 2560;
    const int srcH = 1440;
    const int outW = 1920;
    const int outH = 1080;
    const std::vector<uint8_t> src = rec19Source(srcW, srcH, srcW * 4);
    RecordFrameComposer c;
    REQUIRE(c.Init(outW, outH));
    c.Compose(&src[0], srcW, srcH, srcW * 4, NULL, NULL, 0, 0);
    const RECT full = { 0, 0, outW, outH };
    CHECK(c.ContentRect().left == 0);
    CHECK(c.ContentRect().right == outW);
    std::vector<uint8_t> expect(static_cast<size_t>(outW) * outH * 4, 0);
    screenshare::BgraDownscaler down;
    down.Scale(&src[0], srcW * 4, srcW, srcH, &expect[0], outW * 4, outW, outH);
    CHECK(rec19RegionSame(c, full, &expect[0], outW * 4));

    //原尺寸、源行跨度大于宽度（采集到的画面常有对齐的行跨度）：逐字节复制
    const int padStride = (outW + 16) * 4;
    const std::vector<uint8_t> padded = rec19Source(outW, outH, padStride);
    c.Compose(&padded[0], outW, outH, padStride, NULL, NULL, 0, 0);
    CHECK(rec19RegionSame(c, full, &padded[0], padStride));

    //缩小时画中画与标注照常叠加：画中画（绿色）出现在右下角
    std::vector<uint8_t> green(static_cast<size_t>(64) * 36 * 4, 0);
    for (size_t i = 0; i < green.size(); i += 4)
    {
        green[i + 1] = 255;
        green[i + 3] = 255;
    }
    c.Compose(&src[0], srcW, srcH, srcW * 4, NULL, &green[0], 64, 36);
    const RECT pip = ComputePipRect(outW, outH);
    int r = 0;
    int g = 0;
    int b = 0;
    pixelAt(c, (pip.left + pip.right) / 2, (pip.top + pip.bottom) / 2, r, g, b);
    CHECK(g > 200);
    CHECK(r < 50);
}
