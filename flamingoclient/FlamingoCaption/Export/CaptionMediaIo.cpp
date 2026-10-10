/**
 *  导出与预览用到的 Media Foundation 处理，见 CaptionMediaIo.h。
 *  做法来自第 0 期的试验程序 tmp/caption0c/burnprobe.cpp（14.4）。
 *  balloonwj@qq.com   2026-10-06
 */
#include "CaptionMediaIo.h"

#include <windows.h>
#include <atlbase.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <string.h>

#include "../Render/CaptionRenderer.h"

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")

namespace caption {

namespace {

//Media Foundation 的时间单位：100 纳秒；每毫秒的 100 纳秒数
const LONGLONG kHnsPerMs = 10000;
//硬字幕版的码率相对原码率的倍数：补偿二次编码的画质损失（14.4）
const double kBurnBitrateFactor = 1.2;
//原文件没有记录码率时，按每像素每帧多少比特估算（约等于 1080p 30 帧 4 Mbps）
const double kFallbackBitsPerPixelFrame = 0.064;
//重新编码声音时的码率（字节/秒）：AAC 128 kbps
const UINT32 kAacBytesPerSecond = 16000;
//重新编码声音时的位深
const UINT32 kPcmBitsPerSample = 16;
//进度的满分（千分比）
const int kPermilleFull = 1000;
//每像素的字节数（RGB32 / BGRA）
const int kBytesPerPixel = 4;
//预乘透明度混合时的满值
const uint32_t kAlphaFull = 255;
//四舍五入用的半值
const uint32_t kRoundHalf = 127;
//原文件没有记录声道数、采样率、帧率时的默认值
const UINT32 kDefaultAudioChannels = 2;
const UINT32 kDefaultAudioRate = 48000;
const UINT32 kDefaultFps = 30;

void SetError(std::string* error, const char* what, HRESULT hr)
{
    if (error == NULL)
        return;
    char buf[160];
    _snprintf_s(buf, sizeof(buf), _TRUNCATE, "%s failed, hr=0x%08lX", what, static_cast<unsigned long>(hr));
    *error = buf;
}

//进度汇报：只在千分比变化时转给调用方
class ProgressReporter
{
public:
    ProgressReporter(ICaptionMediaProgress* progress, int64_t durationMs)
        : m_progress(progress)
        , m_durationMs(durationMs)
        , m_last(-1)
    {
    }

    void Report(LONGLONG ptsHns)
    {
        if (m_progress == NULL || m_durationMs <= 0)
            return;
        int permille = static_cast<int>(ptsHns / kHnsPerMs * kPermilleFull / m_durationMs);
        if (permille > kPermilleFull)
            permille = kPermilleFull;
        if (permille < 0)
            permille = 0;
        if (permille != m_last)
        {
            m_last = permille;
            m_progress->OnMediaProgress(permille);
        }
    }

    bool Cancelled() const
    {
        return m_progress != NULL && m_progress->IsMediaCancelled();
    }

private:
    ICaptionMediaProgress* m_progress;     //调用方的进度接口（不拥有），可为 NULL
    int64_t                m_durationMs;   //总时长（毫秒）
    int                    m_last;         //上次报出的千分比
};

/**
 *  读录像的基本信息；时长取自媒体源的 MF_PD_DURATION。
 *  @param allowAudioOnly 为 true 时没有视频流也算成功（须有声音流，只录声音的录音，第 24 期）；为 false 时必须有视频流
 */
bool ReadInfo(IMFSourceReader* reader, CaptionVideoInfo& info, std::string* error, bool allowAudioOnly = false)
{
    CComPtr<IMFMediaType> video;
    HRESULT hr = reader->GetNativeMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), 0, &video);
    info.m_hasVideo = SUCCEEDED(hr);
    if (!info.m_hasVideo && !allowAudioOnly)
    {
        SetError(error, "GetNativeMediaType(video)", hr);
        return false;
    }
    if (info.m_hasVideo)
    {
        MFGetAttributeSize(video, MF_MT_FRAME_SIZE, &info.m_width, &info.m_height);
        MFGetAttributeRatio(video, MF_MT_FRAME_RATE, &info.m_fpsNum, &info.m_fpsDen);
        info.m_bitrate = MFGetAttributeUINT32(video, MF_MT_AVG_BITRATE, 0);
    }
    CComPtr<IMFMediaType> audio;
    info.m_hasAudio =
        SUCCEEDED(reader->GetNativeMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM), 0, &audio));
    PROPVARIANT var;
    PropVariantInit(&var);
    if (SUCCEEDED(reader->GetPresentationAttribute(static_cast<DWORD>(MF_SOURCE_READER_MEDIASOURCE), MF_PD_DURATION,
                                                   &var)) &&
        var.vt == VT_UI8)
    {
        info.m_durationMs = static_cast<int64_t>(var.uhVal.QuadPart / kHnsPerMs);
    }
    PropVariantClear(&var);
    //只有声音的录音：有声音流即可
    if (!info.m_hasVideo)
        return info.m_hasAudio;
    return info.m_width > 0 && info.m_height > 0;
}

//只选中第一个视频流与第一个声音流（其余流不读）；video 为 false 时不选视频流（只录声音的录音，第 24 期）
void SelectStreams(IMFSourceReader* reader, bool audio, bool video = true)
{
    reader->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS), FALSE);
    if (video)
        reader->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), TRUE);
    if (audio)
        reader->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM), TRUE);
}

//声音流加进写入器：优先原样拷贝；不支持时让读取器解码成 PCM、写入器重新编码为 AAC
bool AddAudioStream(IMFSourceReader* reader, IMFSinkWriter* writer, DWORD& streamIndex, std::string* error)
{
    const DWORD audioIndex = static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM);
    CComPtr<IMFMediaType> native;
    HRESULT hr = reader->GetNativeMediaType(audioIndex, 0, &native);
    if (FAILED(hr))
    {
        SetError(error, "GetNativeMediaType(audio)", hr);
        return false;
    }
    if (SUCCEEDED(reader->SetCurrentMediaType(audioIndex, NULL, native)) &&
        SUCCEEDED(writer->AddStream(native, &streamIndex)) &&
        SUCCEEDED(writer->SetInputMediaType(streamIndex, native, NULL)))
    {
        return true;
    }
    //重新编码：读取器输出 16 位 PCM，写入器输出 AAC（声道数与采样率沿用原值）
    const UINT32 channels = MFGetAttributeUINT32(native, MF_MT_AUDIO_NUM_CHANNELS, kDefaultAudioChannels);
    const UINT32 rate = MFGetAttributeUINT32(native, MF_MT_AUDIO_SAMPLES_PER_SECOND, kDefaultAudioRate);
    CComPtr<IMFMediaType> pcm;
    MFCreateMediaType(&pcm);
    pcm->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    pcm->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
    pcm->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, kPcmBitsPerSample);
    pcm->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, channels);
    pcm->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, rate);
    hr = reader->SetCurrentMediaType(audioIndex, NULL, pcm);
    if (FAILED(hr))
    {
        SetError(error, "SetCurrentMediaType(audio pcm)", hr);
        return false;
    }
    CComPtr<IMFMediaType> pcmCur;
    reader->GetCurrentMediaType(audioIndex, &pcmCur);
    CComPtr<IMFMediaType> aac;
    MFCreateMediaType(&aac);
    aac->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    aac->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_AAC);
    aac->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, kPcmBitsPerSample);
    aac->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, channels);
    aac->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, rate);
    aac->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, kAacBytesPerSecond);
    hr = writer->AddStream(aac, &streamIndex);
    if (SUCCEEDED(hr))
        hr = writer->SetInputMediaType(streamIndex, pcmCur, NULL);
    if (FAILED(hr))
    {
        SetError(error, "AddStream(audio aac)", hr);
        return false;
    }
    return true;
}

//字幕图层缓存：字幕变化时才重新画，每帧只把画到的区域叠加到画面上
class OverlayCache
{
public:
    OverlayCache(const std::vector<CaptionSpan>& spans, const CaptionStyle& style, const CaptionPip& pip,
                 uint32_t width, uint32_t height)
        : m_spans(spans)
        , m_width(width)
        , m_height(height)
        , m_current(-2)
    {
        m_layer.assign(static_cast<size_t>(width) * height, 0);
        m_params.m_style = style;
        m_params.m_width = static_cast<int>(width);
        m_params.m_height = static_cast<int>(height);
        m_params.m_refHeight = static_cast<int>(height);
        m_params.m_hasAvoid = pip.m_present;
        m_params.m_avoid.left = static_cast<LONG>(pip.m_left * width);
        m_params.m_avoid.top = static_cast<LONG>(pip.m_top * height);
        m_params.m_avoid.right = static_cast<LONG>(pip.m_right * width);
        m_params.m_avoid.bottom = static_cast<LONG>(pip.m_bottom * height);
        memset(&m_bounds, 0, sizeof(m_bounds));
    }

    //把 ptsMs 时刻的字幕叠加到一帧 RGB32 画面上（scan0 为第一行，pitch 可为负）
    void Blend(int64_t ptsMs, BYTE* scan0, LONG pitch)
    {
        const int idx = FindSpanAt(m_spans, ptsMs);
        if (idx != m_current)
            render(idx);
        if (m_bounds.right <= m_bounds.left || m_bounds.bottom <= m_bounds.top)
            return;
        for (LONG y = m_bounds.top; y < m_bounds.bottom; ++y)
        {
            uint32_t* dst = reinterpret_cast<uint32_t*>(scan0 + static_cast<LONG_PTR>(y) * pitch);
            const uint32_t* src = &m_layer[static_cast<size_t>(y) * m_width];
            for (LONG x = m_bounds.left; x < m_bounds.right; ++x)
            {
                const uint32_t s = src[x];
                const uint32_t a = s >> 24;
                if (a == 0)
                    continue;
                const uint32_t d = dst[x];
                const uint32_t inv = kAlphaFull - a;
                const uint32_t r = ((s >> 16) & 0xFF) + (((d >> 16) & 0xFF) * inv + kRoundHalf) / kAlphaFull;
                const uint32_t g = ((s >> 8) & 0xFF) + (((d >> 8) & 0xFF) * inv + kRoundHalf) / kAlphaFull;
                const uint32_t b = (s & 0xFF) + ((d & 0xFF) * inv + kRoundHalf) / kAlphaFull;
                dst[x] = 0xFF000000 | (r << 16) | (g << 8) | b;
            }
        }
    }

private:
    void render(int idx)
    {
        m_current = idx;
        //只清上次画到的区域
        for (LONG y = m_bounds.top; y < m_bounds.bottom; ++y)
            memset(&m_layer[static_cast<size_t>(y) * m_width + m_bounds.left], 0,
                   static_cast<size_t>(m_bounds.right - m_bounds.left) * sizeof(uint32_t));
        memset(&m_bounds, 0, sizeof(m_bounds));
        if (idx < 0)
            return;
        std::vector<CaptionPaintLine> lines(1);
        lines[0].m_speaker = m_spans[idx].m_speaker;
        lines[0].m_text = m_spans[idx].m_text;
        lines[0].m_partial = false;
        RECT bounds;
        if (!CaptionRenderer::Paint(lines, m_params, &m_layer[0], &bounds))
            return;
        //夹到画面以内，避免叠加时越界
        if (bounds.left < 0)
            bounds.left = 0;
        if (bounds.top < 0)
            bounds.top = 0;
        if (bounds.right > static_cast<LONG>(m_width))
            bounds.right = static_cast<LONG>(m_width);
        if (bounds.bottom > static_cast<LONG>(m_height))
            bounds.bottom = static_cast<LONG>(m_height);
        m_bounds = bounds;
    }

    const std::vector<CaptionSpan>& m_spans;   //全部字幕（调用方持有）
    uint32_t              m_width;             //画面宽
    uint32_t              m_height;            //画面高
    CaptionPaintParams    m_params;            //绘制参数
    std::vector<uint32_t> m_layer;             //字幕图层（预乘透明度的 BGRA，与画面同大）
    RECT                  m_bounds;            //图层里当前画到的区域；空矩形表示没有字幕
    int                   m_current;           //图层里当前画的是哪条字幕；-1 表示没有，-2 表示尚未画过
};

} // namespace

bool ProbeVideo(const std::wstring& path, CaptionVideoInfo& info, std::string* error)
{
    info = CaptionVideoInfo();
    CComPtr<IMFSourceReader> reader;
    HRESULT hr = MFCreateSourceReaderFromURL(path.c_str(), NULL, &reader);
    if (FAILED(hr))
    {
        SetError(error, "MFCreateSourceReaderFromURL", hr);
        return false;
    }
    return ReadInfo(reader, info, error);
}

bool RemuxMp4(const std::wstring& src, const std::wstring& dst, ICaptionMediaProgress* progress, std::string* error)
{
    CComPtr<IMFSourceReader> reader;
    HRESULT hr = MFCreateSourceReaderFromURL(src.c_str(), NULL, &reader);
    if (FAILED(hr))
    {
        SetError(error, "MFCreateSourceReaderFromURL", hr);
        return false;
    }
    CaptionVideoInfo info;
    //只有声音的录音（第 24 期）同样可以转封装
    if (!ReadInfo(reader, info, error, true))
        return false;
    SelectStreams(reader, info.m_hasAudio, info.m_hasVideo);

    CComPtr<IMFAttributes> wa;
    MFCreateAttributes(&wa, 2);
    wa->SetUINT32(MF_SINK_WRITER_DISABLE_THROTTLING, TRUE);
    //明确指定 MP4 容器：目标可能是「.part」临时文件，写入器按扩展名认不出容器格式
    wa->SetGUID(MF_TRANSCODE_CONTAINERTYPE, MFTranscodeContainerType_MPEG4);
    ::DeleteFileW(dst.c_str());
    CComPtr<IMFSinkWriter> writer;
    hr = MFCreateSinkWriterFromURL(dst.c_str(), NULL, wa, &writer);
    if (FAILED(hr))
    {
        SetError(error, "MFCreateSinkWriterFromURL", hr);
        return false;
    }
    //原样拷贝：读取器输出原始压缩格式，写入器的输入、输出类型都是它。槽 0 为视频、槽 1 为声音，文件里没有的那一路跳过
    const DWORD readerStreams[] = { static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM),
                                    static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM) };
    const bool present[2] = { info.m_hasVideo, info.m_hasAudio };
    DWORD writerStreams[2] = { 0, 0 };
    for (int i = 0; i < 2; ++i)
    {
        if (!present[i])
            continue;
        CComPtr<IMFMediaType> native;
        hr = reader->GetNativeMediaType(readerStreams[i], 0, &native);
        if (SUCCEEDED(hr))
            hr = reader->SetCurrentMediaType(readerStreams[i], NULL, native);
        if (SUCCEEDED(hr))
            hr = writer->AddStream(native, &writerStreams[i]);
        if (SUCCEEDED(hr))
            hr = writer->SetInputMediaType(writerStreams[i], native, NULL);
        if (FAILED(hr))
        {
            SetError(error, i == 0 ? "passthrough video stream" : "passthrough audio stream", hr);
            return false;
        }
    }
    hr = writer->BeginWriting();
    if (FAILED(hr))
    {
        SetError(error, "BeginWriting", hr);
        return false;
    }
    ProgressReporter reporter(progress, info.m_durationMs);
    bool ended[2] = { !info.m_hasVideo, !info.m_hasAudio };
    //按哪一路的时间汇报进度：有视频时按视频，只有声音时按声音（第 24 期）
    const int progressSlot = info.m_hasVideo ? 0 : 1;
    while (!(ended[0] && ended[1]))
    {
        if (reporter.Cancelled())
        {
            if (error != NULL)
                *error = "cancelled";
            return false;
        }
        DWORD actualIndex = 0;
        DWORD flags = 0;
        LONGLONG ts = 0;
        CComPtr<IMFSample> sample;
        hr = reader->ReadSample(static_cast<DWORD>(MF_SOURCE_READER_ANY_STREAM), 0, &actualIndex, &flags, &ts, &sample);
        if (FAILED(hr))
        {
            SetError(error, "ReadSample", hr);
            return false;
        }
        //读取器的流号：第一个视频流与第一个声音流在文件里的实际序号未必是 0 和 1，按媒体类型区分
        CComPtr<IMFMediaType> type;
        reader->GetCurrentMediaType(actualIndex, &type);
        GUID major = GUID_NULL;
        if (type != NULL)
            type->GetGUID(MF_MT_MAJOR_TYPE, &major);
        const int slot = (major == MFMediaType_Audio) ? 1 : 0;
        if ((flags & MF_SOURCE_READERF_ENDOFSTREAM) != 0)
        {
            ended[slot] = true;
            continue;
        }
        if (sample == NULL)
            continue;
        hr = writer->WriteSample(writerStreams[slot], sample);
        if (FAILED(hr))
        {
            SetError(error, "WriteSample", hr);
            return false;
        }
        if (slot == progressSlot)
            reporter.Report(ts);
    }
    hr = writer->Finalize();
    if (FAILED(hr))
    {
        SetError(error, "Finalize", hr);
        return false;
    }
    return true;
}

bool BurnHardSub(const std::wstring& src, const std::wstring& dst, const std::vector<CaptionSpan>& spans,
                 const CaptionStyle& style, const CaptionPip& pip, ICaptionMediaProgress* progress, std::string* error)
{
    CComPtr<IMFAttributes> ra;
    MFCreateAttributes(&ra, 2);
    ra->SetUINT32(MF_SOURCE_READER_ENABLE_ADVANCED_VIDEO_PROCESSING, TRUE);
    ra->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);
    CComPtr<IMFSourceReader> reader;
    HRESULT hr = MFCreateSourceReaderFromURL(src.c_str(), ra, &reader);
    if (FAILED(hr))
    {
        SetError(error, "MFCreateSourceReaderFromURL", hr);
        return false;
    }
    CaptionVideoInfo info;
    if (!ReadInfo(reader, info, error))
        return false;
    SelectStreams(reader, info.m_hasAudio);
    const DWORD videoIndex = static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM);

    //视频：读取器解码为 RGB32
    CComPtr<IMFMediaType> rgb;
    MFCreateMediaType(&rgb);
    rgb->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    rgb->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
    hr = reader->SetCurrentMediaType(videoIndex, NULL, rgb);
    if (FAILED(hr))
    {
        SetError(error, "SetCurrentMediaType(video RGB32)", hr);
        return false;
    }
    CComPtr<IMFMediaType> rgbCur;
    reader->GetCurrentMediaType(videoIndex, &rgbCur);
    const LONG defaultStride =
        static_cast<LONG>(static_cast<INT32>(MFGetAttributeUINT32(rgbCur, MF_MT_DEFAULT_STRIDE,
                                                                  info.m_width * kBytesPerPixel)));

    CComPtr<IMFAttributes> wa;
    MFCreateAttributes(&wa, 3);
    wa->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);
    wa->SetUINT32(MF_SINK_WRITER_DISABLE_THROTTLING, TRUE);
    //明确指定 MP4 容器，理由同 RemuxMp4
    wa->SetGUID(MF_TRANSCODE_CONTAINERTYPE, MFTranscodeContainerType_MPEG4);
    ::DeleteFileW(dst.c_str());
    CComPtr<IMFSinkWriter> writer;
    hr = MFCreateSinkWriterFromURL(dst.c_str(), NULL, wa, &writer);
    if (FAILED(hr))
    {
        SetError(error, "MFCreateSinkWriterFromURL", hr);
        return false;
    }
    UINT32 bitrate = info.m_bitrate;
    if (bitrate == 0)
    {
        const double fps = info.m_fpsDen != 0 ? static_cast<double>(info.m_fpsNum) / info.m_fpsDen : kDefaultFps;
        bitrate = static_cast<UINT32>(kFallbackBitsPerPixelFrame * info.m_width * info.m_height * fps);
    }
    CComPtr<IMFMediaType> h264;
    MFCreateMediaType(&h264);
    h264->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    h264->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
    h264->SetUINT32(MF_MT_AVG_BITRATE, static_cast<UINT32>(bitrate * kBurnBitrateFactor));
    h264->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    MFSetAttributeSize(h264, MF_MT_FRAME_SIZE, info.m_width, info.m_height);
    MFSetAttributeRatio(h264, MF_MT_FRAME_RATE, info.m_fpsNum != 0 ? info.m_fpsNum : kDefaultFps,
                        info.m_fpsDen != 0 ? info.m_fpsDen : 1);
    MFSetAttributeRatio(h264, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    DWORD videoOut = 0;
    hr = writer->AddStream(h264, &videoOut);
    if (SUCCEEDED(hr))
        hr = writer->SetInputMediaType(videoOut, rgbCur, NULL);
    if (FAILED(hr))
    {
        SetError(error, "AddStream(video h264)", hr);
        return false;
    }
    DWORD audioOut = 0;
    if (info.m_hasAudio && !AddAudioStream(reader, writer, audioOut, error))
        return false;
    hr = writer->BeginWriting();
    if (FAILED(hr))
    {
        SetError(error, "BeginWriting", hr);
        return false;
    }

    OverlayCache overlay(spans, style, pip, info.m_width, info.m_height);
    ProgressReporter reporter(progress, info.m_durationMs);
    bool ended[2] = { false, !info.m_hasAudio };
    while (!(ended[0] && ended[1]))
    {
        if (reporter.Cancelled())
        {
            if (error != NULL)
                *error = "cancelled";
            return false;
        }
        DWORD actualIndex = 0;
        DWORD flags = 0;
        LONGLONG ts = 0;
        CComPtr<IMFSample> sample;
        hr = reader->ReadSample(static_cast<DWORD>(MF_SOURCE_READER_ANY_STREAM), 0, &actualIndex, &flags, &ts, &sample);
        if (FAILED(hr))
        {
            SetError(error, "ReadSample", hr);
            return false;
        }
        CComPtr<IMFMediaType> type;
        reader->GetCurrentMediaType(actualIndex, &type);
        GUID major = GUID_NULL;
        if (type != NULL)
            type->GetGUID(MF_MT_MAJOR_TYPE, &major);
        const bool isAudio = (major == MFMediaType_Audio);
        if ((flags & MF_SOURCE_READERF_ENDOFSTREAM) != 0)
        {
            ended[isAudio ? 1 : 0] = true;
            continue;
        }
        if (sample == NULL)
            continue;
        if (isAudio)
        {
            hr = writer->WriteSample(audioOut, sample);
            if (FAILED(hr))
            {
                SetError(error, "WriteSample(audio)", hr);
                return false;
            }
            continue;
        }
        //视频：把这一帧时刻的字幕叠加上去
        CComPtr<IMFMediaBuffer> buffer;
        hr = sample->ConvertToContiguousBuffer(&buffer);
        if (FAILED(hr))
        {
            SetError(error, "ConvertToContiguousBuffer", hr);
            return false;
        }
        CComPtr<IMF2DBuffer> buffer2d;
        BYTE* scan0 = NULL;
        LONG pitch = 0;
        if (SUCCEEDED(buffer->QueryInterface(IID_PPV_ARGS(&buffer2d))) && SUCCEEDED(buffer2d->Lock2D(&scan0, &pitch)))
        {
            overlay.Blend(ts / kHnsPerMs, scan0, pitch);
            buffer2d->Unlock2D();
        }
        else
        {
            BYTE* p = NULL;
            DWORD len = 0;
            if (SUCCEEDED(buffer->Lock(&p, NULL, &len)))
            {
                //自下而上的画面（跨度为负）第一行在缓冲区末尾
                pitch = defaultStride;
                scan0 = pitch > 0 ? p : p + static_cast<size_t>(-pitch) * (info.m_height - 1);
                overlay.Blend(ts / kHnsPerMs, scan0, pitch);
                buffer->Unlock();
            }
        }
        hr = writer->WriteSample(videoOut, sample);
        if (FAILED(hr))
        {
            SetError(error, "WriteSample(video)", hr);
            return false;
        }
        reporter.Report(ts);
    }
    hr = writer->Finalize();
    if (FAILED(hr))
    {
        SetError(error, "Finalize", hr);
        return false;
    }
    return true;
}

} // namespace caption
