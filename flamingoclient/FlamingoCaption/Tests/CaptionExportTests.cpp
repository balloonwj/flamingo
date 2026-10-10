/**
 *  CaptionExportTests.cpp
 *
 *  语音字幕导出（docs/语音字幕方案.md 第 8 节）的单元测试，标签 [captionexport]：
 *    CAPEX1  —— 显示区间：按开始时刻排序、重叠时前一条截到后一条开始、文字为空白或时长为 0 的丢弃、负的开始时刻按 0；
 *    CAPEX2  —— 文字样本：区间之间补空样本、末尾补到总时长、超出总时长的截掉；总时长不大于 0 时为空；
 *    CAPEX3  —— 按时刻找区间（含边界）；说话人前缀；
 *    CAPEX4  —— tx3g 字幕轨（moov 在文件末尾）：追加后顶层盒子为 ftyp、mdat、mdat、moov，原 mdat 不变，读回的样本与写入的一致；
 *    CAPEX5  —— tx3g 字幕轨（moov 在 mdat 之前）：原 moov 改成 free，末尾追加 mdat 与新 moov，读回一致；
 *    CAPEX6  —— 非法输入：样本为空、样本过长、不是 MP4 的文件都返回失败且不改文件；
 *    CAPEX7  —— 原样转封装：测试现场生成一段 H.264 + AAC 录像，转出的文件尺寸、时长、视频帧数与原文件相同；
 *    CAPEX8  —— 转封装后的真实文件追加字幕轨：读回一致，Source Reader 照样读出全部视频帧；
 *    CAPEX9  —— 硬字幕版：视频帧数不变；字幕时段内画面底部出现字幕的白色像素，字幕时段外没有；
 *    CAPEX10 —— 整次导出：SRT、字幕轨版、硬字幕版三项都成功，进度报到 1000，没有残留临时文件；
 *    CAPEX11 —— 取消：硬字幕版导出中途取消，结果标记为取消，目标文件与临时文件都不存在。
 *
 *  CAPEX7～CAPEX11 用 Media Foundation 现场编码一段 2 秒的 320×240 录像（深蓝色画面 + 正弦声），写在
 *  E:\flamingo\tmp（取不到时用系统临时目录）下，用完删除。
 *
 *  balloonwj@qq.com   2026-10-06
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <windows.h>
#include <atlbase.h>
#include <objidl.h>
#include <gdiplus.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <math.h>
#include <stdlib.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <vector>

#include "../Export/CaptionExportJob.h"
#include "../Export/CaptionMediaIo.h"
#include "../Export/CaptionTimedText.h"
#include "../Export/Mp4TextTrack.h"

using namespace caption;

namespace {

//测试录像的参数：320×240、15 帧/秒、2 秒；声音 48 kHz 单声道 440 Hz
const UINT32 kVidW = 320;
const UINT32 kVidH = 240;
const UINT32 kVidFps = 15;
const int kVidFrames = 30;
const UINT32 kAudRate = 48000;
const UINT32 kAudChannels = 1;
const double kAudHz = 440.0;
const double kAudAmp = 8000.0;
const double kTwoPi = 6.283185307179586;
const LONGLONG kHnsPerSecond = 10000000;
const UINT32 kVidBitrate = 400000;
const UINT32 kAacBytesPerSecond = 16000;
//画面颜色：深蓝（BGRA），亮度很低，字幕的白字一眼可分
const uint32_t kDarkBlue = 0xFF201008;
//判定「白色像素」的亮度门限（0～255）
const int kBrightThreshold = 200;

std::wstring TempDir()
{
    const wchar_t* const kRepoTmp = L"E:\\flamingo\\tmp\\";
    const DWORD attr = ::GetFileAttributesW(kRepoTmp);
    if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) != 0)
        return kRepoTmp;
    wchar_t buf[MAX_PATH] = { 0 };
    ::GetTempPathW(MAX_PATH, buf);
    return buf;
}

bool FileExists(const std::wstring& path)
{
    return ::GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

std::vector<uint8_t> ReadAll(const std::wstring& path)
{
    std::vector<uint8_t> out;
    HANDLE f = ::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE)
        return out;
    LARGE_INTEGER size;
    ::GetFileSizeEx(f, &size);
    out.resize(static_cast<size_t>(size.QuadPart));
    DWORD got = 0;
    if (!out.empty())
        ::ReadFile(f, &out[0], static_cast<DWORD>(out.size()), &got, NULL);
    ::CloseHandle(f);
    return out;
}

void WriteAll(const std::wstring& path, const std::vector<uint8_t>& bytes)
{
    HANDLE f = ::CreateFileW(path.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    if (f == INVALID_HANDLE_VALUE)
        return;
    DWORD put = 0;
    if (!bytes.empty())
        ::WriteFile(f, &bytes[0], static_cast<DWORD>(bytes.size()), &put, NULL);
    ::CloseHandle(f);
}

//按大端拼字节的小工具（构造人工 MP4 用）
struct Bytes
{
    std::vector<uint8_t> b;
    void U8(uint8_t v)
    {
        b.push_back(v);
    }
    void U16(uint16_t v)
    {
        U8(static_cast<uint8_t>(v >> 8));
        U8(static_cast<uint8_t>(v));
    }
    void U32(uint32_t v)
    {
        U8(static_cast<uint8_t>(v >> 24));
        U8(static_cast<uint8_t>(v >> 16));
        U8(static_cast<uint8_t>(v >> 8));
        U8(static_cast<uint8_t>(v));
    }
    void Type(const char* t)
    {
        b.insert(b.end(), t, t + 4);
    }
    size_t Begin(const char* t)
    {
        const size_t p = b.size();
        U32(0);
        Type(t);
        return p;
    }
    void End(size_t p)
    {
        const uint32_t size = static_cast<uint32_t>(b.size() - p);
        b[p] = static_cast<uint8_t>(size >> 24);
        b[p + 1] = static_cast<uint8_t>(size >> 16);
        b[p + 2] = static_cast<uint8_t>(size >> 8);
        b[p + 3] = static_cast<uint8_t>(size);
    }
};

//人工 moov：mvhd（第 0 版，刻度 1000、时长 5000、next_track_ID 2）+ 一条只有 tkhd（track_ID 1）的 trak
void AppendFakeMoov(Bytes& w)
{
    const size_t moov = w.Begin("moov");
    const size_t mvhd = w.Begin("mvhd");
    w.U32(0);          //版本与标志
    w.U32(0);          //创建时间
    w.U32(0);          //修改时间
    w.U32(1000);       //时间刻度
    w.U32(5000);       //时长
    w.U32(0x00010000); //rate
    w.U16(0x0100);     //volume
    w.U16(0);
    w.U32(0);
    w.U32(0);
    for (int i = 0; i < 9; ++i)
        w.U32(0);      //matrix
    for (int i = 0; i < 6; ++i)
        w.U32(0);      //pre_defined
    w.U32(2);          //next_track_ID
    w.End(mvhd);
    const size_t trak = w.Begin("trak");
    const size_t tkhd = w.Begin("tkhd");
    w.U32(3);          //版本 0、标志 3
    w.U32(0);
    w.U32(0);
    w.U32(1);          //track_ID
    for (int i = 0; i < 17; ++i)
        w.U32(0);
    w.End(tkhd);
    w.End(trak);
    w.End(moov);
}

std::vector<Mp4TextSample> SampleSet()
{
    std::vector<Mp4TextSample> s(3);
    s[0].m_durationMs = 1000;
    s[1].m_durationMs = 1500;
    s[1].m_utf8 = "\xE4\xBD\xA0\xE5\xA5\xBD SQLite";
    s[2].m_durationMs = 2500;
    return s;
}

//Media Foundation 的初始化范围
class MfScope
{
public:
    MfScope()
    {
        m_co = ::CoInitializeEx(NULL, COINIT_MULTITHREADED);
        m_mf = ::MFStartup(MF_VERSION);
        Gdiplus::GdiplusStartupInput input;
        m_gdiOk = (Gdiplus::GdiplusStartup(&m_gdiToken, &input, NULL) == Gdiplus::Ok);
    }
    ~MfScope()
    {
        if (m_gdiOk)
            Gdiplus::GdiplusShutdown(m_gdiToken);
        if (SUCCEEDED(m_mf))
            ::MFShutdown();
        if (SUCCEEDED(m_co))
            ::CoUninitialize();
    }
    bool Ok() const
    {
        return SUCCEEDED(m_mf) && m_gdiOk;
    }

private:
    HRESULT   m_co;
    HRESULT   m_mf;
    bool      m_gdiOk;
    ULONG_PTR m_gdiToken;
};

//现场编码测试录像：深蓝色画面 + 正弦声
bool MakeTestVideo(const std::wstring& path)
{
    ::DeleteFileW(path.c_str());
    CComPtr<IMFSinkWriter> writer;
    if (FAILED(MFCreateSinkWriterFromURL(path.c_str(), NULL, NULL, &writer)))
        return false;
    CComPtr<IMFMediaType> vOut;
    MFCreateMediaType(&vOut);
    vOut->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    vOut->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
    vOut->SetUINT32(MF_MT_AVG_BITRATE, kVidBitrate);
    vOut->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    MFSetAttributeSize(vOut, MF_MT_FRAME_SIZE, kVidW, kVidH);
    MFSetAttributeRatio(vOut, MF_MT_FRAME_RATE, kVidFps, 1);
    MFSetAttributeRatio(vOut, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    DWORD vs = 0;
    if (FAILED(writer->AddStream(vOut, &vs)))
        return false;
    CComPtr<IMFMediaType> vIn;
    MFCreateMediaType(&vIn);
    vIn->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    vIn->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
    vIn->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    MFSetAttributeSize(vIn, MF_MT_FRAME_SIZE, kVidW, kVidH);
    MFSetAttributeRatio(vIn, MF_MT_FRAME_RATE, kVidFps, 1);
    MFSetAttributeRatio(vIn, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    if (FAILED(writer->SetInputMediaType(vs, vIn, NULL)))
        return false;
    CComPtr<IMFMediaType> aOut;
    MFCreateMediaType(&aOut);
    aOut->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    aOut->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_AAC);
    aOut->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
    aOut->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, kAudRate);
    aOut->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, kAudChannels);
    aOut->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, kAacBytesPerSecond);
    DWORD as = 0;
    if (FAILED(writer->AddStream(aOut, &as)))
        return false;
    CComPtr<IMFMediaType> aIn;
    MFCreateMediaType(&aIn);
    aIn->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    aIn->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
    aIn->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
    aIn->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, kAudRate);
    aIn->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, kAudChannels);
    if (FAILED(writer->SetInputMediaType(as, aIn, NULL)))
        return false;
    if (FAILED(writer->BeginWriting()))
        return false;
    const LONGLONG frameHns = kHnsPerSecond / kVidFps;
    const UINT32 frameBytes = kVidW * kVidH * 4;
    const UINT32 audioPerFrame = kAudRate / kVidFps;
    UINT32 audioPos = 0;
    for (int i = 0; i < kVidFrames; ++i)
    {
        CComPtr<IMFMediaBuffer> vb;
        MFCreateMemoryBuffer(frameBytes, &vb);
        BYTE* p = NULL;
        vb->Lock(&p, NULL, NULL);
        uint32_t* px = reinterpret_cast<uint32_t*>(p);
        for (UINT32 k = 0; k < kVidW * kVidH; ++k)
            px[k] = kDarkBlue;
        vb->Unlock();
        vb->SetCurrentLength(frameBytes);
        CComPtr<IMFSample> vsmp;
        MFCreateSample(&vsmp);
        vsmp->AddBuffer(vb);
        vsmp->SetSampleTime(i * frameHns);
        vsmp->SetSampleDuration(frameHns);
        if (FAILED(writer->WriteSample(vs, vsmp)))
            return false;
        CComPtr<IMFMediaBuffer> ab;
        const DWORD audioBytes = audioPerFrame * sizeof(int16_t);
        MFCreateMemoryBuffer(audioBytes, &ab);
        ab->Lock(&p, NULL, NULL);
        int16_t* pcm = reinterpret_cast<int16_t*>(p);
        for (UINT32 k = 0; k < audioPerFrame; ++k, ++audioPos)
            pcm[k] = static_cast<int16_t>(kAudAmp * sin(kTwoPi * kAudHz * audioPos / kAudRate));
        ab->Unlock();
        ab->SetCurrentLength(audioBytes);
        CComPtr<IMFSample> asmp;
        MFCreateSample(&asmp);
        asmp->AddBuffer(ab);
        asmp->SetSampleTime(i * frameHns);
        asmp->SetSampleDuration(frameHns);
        if (FAILED(writer->WriteSample(as, asmp)))
            return false;
    }
    return SUCCEEDED(writer->Finalize());
}

//数视频帧数（读压缩样本，不解码）
int CountVideoFrames(const std::wstring& path)
{
    CComPtr<IMFSourceReader> reader;
    if (FAILED(MFCreateSourceReaderFromURL(path.c_str(), NULL, &reader)))
        return -1;
    reader->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS), FALSE);
    reader->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), TRUE);
    int frames = 0;
    for (;;)
    {
        DWORD idx = 0;
        DWORD flags = 0;
        LONGLONG ts = 0;
        CComPtr<IMFSample> s;
        if (FAILED(reader->ReadSample(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), 0, &idx, &flags, &ts, &s)))
            return -1;
        if ((flags & MF_SOURCE_READERF_ENDOFSTREAM) != 0)
            break;
        if (s != NULL)
            ++frames;
    }
    return frames;
}

//解码出 atMs 附近的一帧，数画面底部 40% 里亮度超过门限的像素个数
int CountBrightPixelsAt(const std::wstring& path, int64_t atMs)
{
    CComPtr<IMFAttributes> ra;
    MFCreateAttributes(&ra, 1);
    ra->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);   //单测的 stdafx 以 Win7 为目标，用 Win7 就有的这一项
    CComPtr<IMFSourceReader> reader;
    if (FAILED(MFCreateSourceReaderFromURL(path.c_str(), ra, &reader)))
        return -1;
    reader->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS), FALSE);
    reader->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), TRUE);
    CComPtr<IMFMediaType> rgb;
    MFCreateMediaType(&rgb);
    rgb->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    rgb->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
    if (FAILED(reader->SetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), NULL, rgb)))
        return -1;
    const LONGLONG target = atMs * 10000;
    for (;;)
    {
        DWORD idx = 0;
        DWORD flags = 0;
        LONGLONG ts = 0;
        CComPtr<IMFSample> s;
        if (FAILED(reader->ReadSample(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), 0, &idx, &flags, &ts, &s)))
            return -1;
        if ((flags & MF_SOURCE_READERF_ENDOFSTREAM) != 0)
            return -1;
        if (s == NULL || ts < target)
            continue;
        CComPtr<IMFMediaBuffer> buf;
        s->ConvertToContiguousBuffer(&buf);
        BYTE* p = NULL;
        DWORD len = 0;
        buf->Lock(&p, NULL, &len);
        int bright = 0;
        const uint32_t* px = reinterpret_cast<const uint32_t*>(p);
        const UINT32 total = len / 4;
        for (UINT32 k = 0; k < total; ++k)
        {
            const uint32_t v = px[k];
            const int lum = (static_cast<int>((v >> 16) & 0xFF) + static_cast<int>((v >> 8) & 0xFF) +
                             static_cast<int>(v & 0xFF)) / 3;
            if (lum > kBrightThreshold)
                ++bright;
        }
        buf->Unlock();
        return bright;
    }
}

CaptionCue Cue(int64_t start, int64_t end, const wchar_t* text, const wchar_t* speaker)
{
    CaptionCue c;
    c.m_startMs = start;
    c.m_endMs = end;
    c.m_text = text;
    c.m_speaker = speaker;
    return c;
}

//记录导出回报的替身
class RecordingListener : public ICaptionExportListener
{
public:
    RecordingListener()
        : m_done(false)
        , m_maxOverall(0)
    {
    }

    virtual void OnExportProgress(int kind, int permille, int overall) override
    {
        (void)kind;
        (void)permille;
        std::lock_guard<std::mutex> lock(m_mutex);
        if (overall > m_maxOverall)
            m_maxOverall = overall;
    }

    virtual void OnExportFinished(const CaptionExportResult& result) override
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_result = result;
        m_done = true;
        m_cv.notify_all();
    }

    bool WaitDone(int timeoutMs)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        const std::chrono::steady_clock::time_point deadline =
            std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
        while (!m_done)
        {
            if (m_cv.wait_until(lock, deadline) == std::cv_status::timeout)
                break;
        }
        return m_done;
    }

    std::mutex              m_mutex;
    std::condition_variable m_cv;
    bool                    m_done;
    int                     m_maxOverall;
    CaptionExportResult     m_result;
};

} // namespace

TEST_CASE("CAPEX1 显示区间：排序、重叠截短、丢弃空白与零时长、负的开始时刻按 0", "[captionexport]")
{
    std::vector<CaptionCue> cues;
    cues.push_back(Cue(3000, 5000, L"third", L"B"));
    cues.push_back(Cue(1000, 3500, L"second", L"A"));
    cues.push_back(Cue(-200, 800, L"first", L""));
    cues.push_back(Cue(6000, 7000, L"   ", L"A"));
    cues.push_back(Cue(8000, 8000, L"zero", L"A"));
    const std::vector<CaptionSpan> spans = BuildDisplaySpans(cues);
    REQUIRE(spans.size() == 3);
    CHECK(spans[0].m_startMs == 0);
    CHECK(spans[0].m_text == L"first");
    CHECK(spans[1].m_startMs == 1000);
    CHECK(spans[1].m_endMs == 3000);   //与第三条重叠，截到它开始
    CHECK(spans[1].m_speaker == L"A");
    CHECK(spans[2].m_text == L"third");
    CHECK(spans[2].m_endMs == 5000);
}

TEST_CASE("CAPEX2 文字样本：补空样本、末尾补齐、超出截掉；总时长不大于 0 时为空", "[captionexport]")
{
    std::vector<CaptionCue> cues;
    cues.push_back(Cue(1000, 2000, L"a", L""));
    cues.push_back(Cue(2000, 2500, L"b", L""));
    cues.push_back(Cue(4000, 9000, L"c", L""));
    const std::vector<CaptionSpan> spans = BuildDisplaySpans(cues);
    const std::vector<CaptionSpan> s = BuildTextSamples(spans, 6000);
    REQUIRE(s.size() == 5);
    CHECK(s[0].m_startMs == 0);
    CHECK(s[0].m_endMs == 1000);
    CHECK(s[0].m_text.empty());
    CHECK(s[1].m_text == L"a");
    CHECK(s[2].m_text == L"b");
    CHECK(s[3].m_startMs == 2500);
    CHECK(s[3].m_endMs == 4000);
    CHECK(s[3].m_text.empty());
    CHECK(s[4].m_text == L"c");
    CHECK(s[4].m_endMs == 6000);
    //最后一条早于总时长：末尾补一个空样本
    const std::vector<CaptionSpan> t = BuildTextSamples(spans, 12000);
    REQUIRE(t.size() == 6);
    CHECK(t[5].m_startMs == 9000);
    CHECK(t[5].m_endMs == 12000);
    CHECK(t[5].m_text.empty());
    CHECK(BuildTextSamples(spans, 0).empty());
    //没有字幕：一整段空样本
    const std::vector<CaptionSpan> empty = BuildTextSamples(std::vector<CaptionSpan>(), 3000);
    REQUIRE(empty.size() == 1);
    CHECK(empty[0].m_endMs == 3000);
}

TEST_CASE("CAPEX3 按时刻找区间（含边界）；说话人前缀", "[captionexport]")
{
    std::vector<CaptionCue> cues;
    cues.push_back(Cue(1000, 2000, L"a", L"\u5f20\u4e09"));
    cues.push_back(Cue(3000, 4000, L"b", L""));
    const std::vector<CaptionSpan> spans = BuildDisplaySpans(cues);
    CHECK(FindSpanAt(spans, 999) == -1);
    CHECK(FindSpanAt(spans, 1000) == 0);
    CHECK(FindSpanAt(spans, 1999) == 0);
    CHECK(FindSpanAt(spans, 2000) == -1);
    CHECK(FindSpanAt(spans, 3500) == 1);
    CHECK(FindSpanAt(spans, 4000) == -1);
    CHECK(FindSpanAt(std::vector<CaptionSpan>(), 0) == -1);
    CHECK(SpanDisplayText(spans[0], true) == L"\u5f20\u4e09\uff1aa");
    CHECK(SpanDisplayText(spans[0], false) == L"a");
    CHECK(SpanDisplayText(spans[1], true) == L"b");
}

TEST_CASE("CAPEX4 tx3g 字幕轨（moov 在文件末尾）：结构、原 mdat 不变、读回一致", "[captionexport]")
{
    const std::wstring path = TempDir() + L"capex4.mp4";
    Bytes w;
    const size_t ftyp = w.Begin("ftyp");
    w.Type("isom");
    w.U32(0);
    w.End(ftyp);
    const size_t mdat = w.Begin("mdat");
    for (int i = 0; i < 100; ++i)
        w.U8(static_cast<uint8_t>(i));
    w.End(mdat);
    const size_t mdatEnd = w.b.size();
    AppendFakeMoov(w);
    WriteAll(path, w.b);

    std::string error;
    REQUIRE(AppendTx3gTrack(path, SampleSet(), 1280, 720, "chi", &error));
    const std::vector<std::string> boxes = ListMp4TopLevelBoxes(path);
    REQUIRE(boxes.size() == 4);
    CHECK(boxes[0] == "ftyp");
    CHECK(boxes[1] == "mdat");
    CHECK(boxes[2] == "mdat");
    CHECK(boxes[3] == "moov");
    //原有的 ftyp 与 mdat 一个字节都没变
    const std::vector<uint8_t> after = ReadAll(path);
    REQUIRE(after.size() > mdatEnd);
    CHECK(std::equal(w.b.begin(), w.b.begin() + mdatEnd, after.begin()));

    std::vector<Mp4TextSample> back;
    REQUIRE(ReadTx3gTrack(path, back, &error));
    const std::vector<Mp4TextSample> want = SampleSet();
    REQUIRE(back.size() == want.size());
    for (size_t i = 0; i < want.size(); ++i)
    {
        CHECK(back[i].m_durationMs == want[i].m_durationMs);
        CHECK(back[i].m_utf8 == want[i].m_utf8);
    }
    ::DeleteFileW(path.c_str());
}

TEST_CASE("CAPEX5 tx3g 字幕轨（moov 在 mdat 之前）：原 moov 改成 free，读回一致", "[captionexport]")
{
    const std::wstring path = TempDir() + L"capex5.mp4";
    Bytes w;
    const size_t ftyp = w.Begin("ftyp");
    w.Type("isom");
    w.U32(0);
    w.End(ftyp);
    AppendFakeMoov(w);
    const size_t mdat = w.Begin("mdat");
    for (int i = 0; i < 64; ++i)
        w.U8(0xAB);
    w.End(mdat);
    WriteAll(path, w.b);

    std::string error;
    REQUIRE(AppendTx3gTrack(path, SampleSet(), 640, 360, NULL, &error));
    const std::vector<std::string> boxes = ListMp4TopLevelBoxes(path);
    REQUIRE(boxes.size() == 5);
    CHECK(boxes[0] == "ftyp");
    CHECK(boxes[1] == "free");
    CHECK(boxes[2] == "mdat");
    CHECK(boxes[3] == "mdat");
    CHECK(boxes[4] == "moov");
    std::vector<Mp4TextSample> back;
    REQUIRE(ReadTx3gTrack(path, back, &error));
    REQUIRE(back.size() == 3);
    CHECK(back[1].m_utf8 == SampleSet()[1].m_utf8);
    ::DeleteFileW(path.c_str());
}

TEST_CASE("CAPEX6 非法输入：样本为空、样本过长、不是 MP4 的文件都返回失败且不改文件", "[captionexport]")
{
    const std::wstring path = TempDir() + L"capex6.mp4";
    std::vector<uint8_t> junk(50, 0x11);
    WriteAll(path, junk);
    std::string error;
    CHECK_FALSE(AppendTx3gTrack(path, std::vector<Mp4TextSample>(), 1, 1, "chi", &error));
    std::vector<Mp4TextSample> tooLong(1);
    tooLong[0].m_durationMs = 1000;
    tooLong[0].m_utf8.assign(kMp4TextSampleMaxBytes + 1, 'x');
    CHECK_FALSE(AppendTx3gTrack(path, tooLong, 1, 1, "chi", &error));
    CHECK_FALSE(AppendTx3gTrack(path, SampleSet(), 1, 1, "chi", &error));
    CHECK(ReadAll(path) == junk);
    std::vector<Mp4TextSample> back;
    CHECK_FALSE(ReadTx3gTrack(path, back, &error));
    ::DeleteFileW(path.c_str());
}

TEST_CASE("CAPEX7 原样转封装：尺寸、时长、视频帧数与原文件相同", "[captionexport]")
{
    MfScope mf;
    REQUIRE(mf.Ok());
    const std::wstring src = TempDir() + L"capex7_src.mp4";
    const std::wstring dst = TempDir() + L"capex7_dst.mp4";
    REQUIRE(MakeTestVideo(src));
    std::string error;
    CaptionVideoInfo a;
    REQUIRE(ProbeVideo(src, a, &error));
    CHECK(a.m_width == kVidW);
    CHECK(a.m_height == kVidH);
    CHECK(a.m_hasAudio);
    REQUIRE(RemuxMp4(src, dst, NULL, &error));
    INFO(error);
    CaptionVideoInfo b;
    REQUIRE(ProbeVideo(dst, b, &error));
    CHECK(b.m_width == a.m_width);
    CHECK(b.m_height == a.m_height);
    CHECK(b.m_hasAudio);
    const int64_t kDurationToleranceMs = 100;
    CHECK(llabs(b.m_durationMs - a.m_durationMs) <= kDurationToleranceMs);
    CHECK(CountVideoFrames(dst) == CountVideoFrames(src));
    ::DeleteFileW(src.c_str());
    ::DeleteFileW(dst.c_str());
}

TEST_CASE("CAPEX8 转封装后的真实文件追加字幕轨：读回一致，视频帧照样读得出", "[captionexport]")
{
    MfScope mf;
    REQUIRE(mf.Ok());
    const std::wstring src = TempDir() + L"capex8_src.mp4";
    const std::wstring dst = TempDir() + L"capex8_dst.mp4";
    REQUIRE(MakeTestVideo(src));
    std::string error;
    REQUIRE(RemuxMp4(src, dst, NULL, &error));
    const int frames = CountVideoFrames(dst);
    REQUIRE(frames > 0);
    REQUIRE(AppendTx3gTrack(dst, SampleSet(), kVidW, kVidH, "chi", &error));
    std::vector<Mp4TextSample> back;
    REQUIRE(ReadTx3gTrack(dst, back, &error));
    CHECK(back.size() == SampleSet().size());
    CHECK(CountVideoFrames(dst) == frames);
    CaptionVideoInfo info;
    CHECK(ProbeVideo(dst, info, &error));
    ::DeleteFileW(src.c_str());
    ::DeleteFileW(dst.c_str());
}

TEST_CASE("CAPEX9 硬字幕版：帧数不变；字幕时段内底部有白色像素，时段外没有", "[captionexport]")
{
    MfScope mf;
    REQUIRE(mf.Ok());
    const std::wstring src = TempDir() + L"capex9_src.mp4";
    const std::wstring dst = TempDir() + L"capex9_dst.mp4";
    REQUIRE(MakeTestVideo(src));
    std::vector<CaptionCue> cues;
    cues.push_back(Cue(0, 1000, L"\u5b57\u5e55 Caption", L""));
    const std::vector<CaptionSpan> spans = BuildDisplaySpans(cues);
    CaptionStyle style;
    style.m_showSpeaker = false;
    CaptionPip pip;
    std::string error;
    REQUIRE(BurnHardSub(src, dst, spans, style, pip, NULL, &error));
    INFO(error);
    CHECK(CountVideoFrames(dst) == CountVideoFrames(src));
    const int kMinCaptionPixels = 50;
    CHECK(CountBrightPixelsAt(dst, 500) > kMinCaptionPixels);
    CHECK(CountBrightPixelsAt(dst, 1500) == 0);
    CHECK(CountBrightPixelsAt(src, 500) == 0);
    ::DeleteFileW(src.c_str());
    ::DeleteFileW(dst.c_str());
}

TEST_CASE("CAPEX10 整次导出：三项都成功，进度报到 1000，没有残留临时文件", "[captionexport]")
{
    MfScope mf;
    REQUIRE(mf.Ok());
    const std::wstring dir = TempDir();
    const std::wstring src = dir + L"capex10.mp4";
    REQUIRE(MakeTestVideo(src));
    CaptionExportRequest req;
    req.m_videoPath = src;
    req.m_cues.push_back(Cue(200, 1200, L"\u4f60\u597d", L"\u5f20\u4e09"));
    req.m_hard = true;
    req.m_track = true;
    req.m_srt = true;
    req.m_hardPath = dir + L"capex10-hard.mp4";
    req.m_trackPath = dir + L"capex10-track.mp4";
    req.m_srtPath = dir + L"capex10.srt";
    RecordingListener listener;
    CaptionExportJob job;
    REQUIRE(job.Start(req, &listener));
    CHECK_FALSE(job.Start(req, &listener));   //正在导出时不能再开始
    const int kTimeoutMs = 120000;
    REQUIRE(listener.WaitDone(kTimeoutMs));
    job.Wait();
    CHECK_FALSE(listener.m_result.m_cancelled);
    REQUIRE(listener.m_result.m_items.size() == 3);
    for (size_t i = 0; i < listener.m_result.m_items.size(); ++i)
    {
        INFO("kind " << listener.m_result.m_items[i].m_kind << " error " << listener.m_result.m_items[i].m_error);
        CHECK(listener.m_result.m_items[i].m_ok);
        CHECK(FileExists(listener.m_result.m_items[i].m_path));
        CHECK_FALSE(FileExists(listener.m_result.m_items[i].m_path + L".part"));
    }
    CHECK(listener.m_result.m_items[0].m_kind == caption_export_srt);
    CHECK(listener.m_result.m_items[1].m_kind == caption_export_track);
    CHECK(listener.m_result.m_items[2].m_kind == caption_export_hard);
    CHECK(listener.m_maxOverall == 1000);
    std::vector<Mp4TextSample> back;
    std::string error;
    REQUIRE(ReadTx3gTrack(req.m_trackPath, back, &error));
    bool found = false;
    for (size_t i = 0; i < back.size(); ++i)
        found = found || back[i].m_utf8 == "\xE5\xBC\xA0\xE4\xB8\x89\xEF\xBC\x9A\xE4\xBD\xA0\xE5\xA5\xBD";
    CHECK(found);
    ::DeleteFileW(src.c_str());
    ::DeleteFileW(req.m_hardPath.c_str());
    ::DeleteFileW(req.m_trackPath.c_str());
    ::DeleteFileW(req.m_srtPath.c_str());
}

TEST_CASE("CAPEX11 取消：硬字幕版中途取消，结果为取消，目标与临时文件都不存在", "[captionexport]")
{
    MfScope mf;
    REQUIRE(mf.Ok());
    const std::wstring dir = TempDir();
    const std::wstring src = dir + L"capex11.mp4";
    REQUIRE(MakeTestVideo(src));
    CaptionExportRequest req;
    req.m_videoPath = src;
    req.m_cues.push_back(Cue(0, 1500, L"cancel", L""));
    req.m_hard = true;
    req.m_hardPath = dir + L"capex11-hard.mp4";
    RecordingListener listener;
    CaptionExportJob job;
    job.Cancel();   //开始之前的取消不影响下一次
    REQUIRE(job.Start(req, &listener));
    job.Cancel();
    const int kTimeoutMs = 60000;
    REQUIRE(listener.WaitDone(kTimeoutMs));
    job.Wait();
    CHECK(listener.m_result.m_cancelled);
    CHECK_FALSE(FileExists(req.m_hardPath));
    CHECK_FALSE(FileExists(req.m_hardPath + L".part"));
    ::DeleteFileW(src.c_str());
}
