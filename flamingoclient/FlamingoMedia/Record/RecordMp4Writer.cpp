/**
 *  录制文件的写入，见 RecordMp4Writer.h。
 *  做法与第 0 期 0d 的验证程序（tmp/share_spike/0d_mp4/mp4spike.cpp）一致。
 *  balloonwj@qq.com   2026-10-03
 */
#include "RecordMp4Writer.h"

#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <string.h>

#include "RecordPolicy.h"
#include "../../Source/Common/Log/IULog.h"   //须在 windows.h 之后

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")

namespace screenrecord {

namespace {

//每像素字节数、16 位采样的字节数
const int kBgraBytes = 4;
const int kSampleBytes = 2;
//每秒的 100 纳秒数
const int64_t kHnsPerSecond = 10000000;

IMFMediaType* makeType(const GUID& major, const GUID& sub)
{
    IMFMediaType* t = NULL;
    if (FAILED(::MFCreateMediaType(&t)))
        return NULL;
    t->SetGUID(MF_MT_MAJOR_TYPE, major);
    t->SetGUID(MF_MT_SUBTYPE, sub);
    return t;
}

//把一块数据包成样本写进指定的流
bool writeBuffer(IMFSinkWriter* w, DWORD stream, const void* data, DWORD bytes, int64_t pts, int64_t dur)
{
    IMFMediaBuffer* buf = NULL;
    if (FAILED(::MFCreateMemoryBuffer(bytes, &buf)))
        return false;
    BYTE* p = NULL;
    if (FAILED(buf->Lock(&p, NULL, NULL)))
    {
        buf->Release();
        return false;
    }
    memcpy(p, data, bytes);
    buf->Unlock();
    buf->SetCurrentLength(bytes);
    IMFSample* s = NULL;
    if (FAILED(::MFCreateSample(&s)))
    {
        buf->Release();
        return false;
    }
    s->AddBuffer(buf);
    s->SetSampleTime(pts);
    s->SetSampleDuration(dur);
    const HRESULT hr = w->WriteSample(stream, s);
    s->Release();
    buf->Release();
    return SUCCEEDED(hr);
}

} // namespace

RecordMp4Writer::RecordMp4Writer()
    : m_writer(NULL)
    , m_videoStream(0)
    , m_audioStream(0)
    , m_width(0)
    , m_height(0)
    , m_hasVideo(false)
{
}

RecordMp4Writer::~RecordMp4Writer()
{
    Close();
}

bool RecordMp4Writer::Open(const std::wstring& path, int width, int height, int fps, uint32_t bitrate)
{
    Close();
    if (path.empty() || width <= 0 || height <= 0 || fps <= 0)
        return false;
    return openInternal(path, true, width, height, fps, bitrate);
}

bool RecordMp4Writer::OpenAudioOnly(const std::wstring& path)
{
    Close();
    if (path.empty())
        return false;
    return openInternal(path, false, 0, 0, 0, 0);
}

bool RecordMp4Writer::HasVideo() const
{
    return m_writer != NULL && m_hasVideo;
}

bool RecordMp4Writer::openInternal(const std::wstring& path, bool withVideo, int width, int height, int fps,
                                   uint32_t bitrate)
{
    IMFAttributes* attrs = NULL;
    if (FAILED(::MFCreateAttributes(&attrs, 2)))
        return false;
    //允许硬件编码器；分段式 MP4（中途崩溃时已写的部分仍可播放）
    attrs->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);
    attrs->SetGUID(MF_TRANSCODE_CONTAINERTYPE, MFTranscodeContainerType_FMPEG4);
    IMFSinkWriter* w = NULL;
    HRESULT hr = ::MFCreateSinkWriterFromURL(path.c_str(), NULL, attrs, &w);
    attrs->Release();
    if (FAILED(hr) || w == NULL)
    {
        LOG_ERROR("record: create sink writer failed, hr=0x%08X.", static_cast<unsigned>(hr));
        return false;
    }

    //视频：BGRA（RGB32，自上而下）→ H.264；只录声音时不建（第 24 期）
    DWORD vIdx = 0;
    IMFMediaType* vOut = withVideo ? makeType(MFMediaType_Video, MFVideoFormat_H264) : NULL;
    IMFMediaType* vIn = withVideo ? makeType(MFMediaType_Video, MFVideoFormat_RGB32) : NULL;
    bool ok = !withVideo || (vOut != NULL && vIn != NULL);
    if (ok && withVideo)
    {
        vOut->SetUINT32(MF_MT_AVG_BITRATE, bitrate);
        vOut->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
        ::MFSetAttributeSize(vOut, MF_MT_FRAME_SIZE, static_cast<UINT32>(width), static_cast<UINT32>(height));
        ::MFSetAttributeRatio(vOut, MF_MT_FRAME_RATE, static_cast<UINT32>(fps), 1);
        ::MFSetAttributeRatio(vOut, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
        hr = w->AddStream(vOut, &vIdx);
        ok = SUCCEEDED(hr);
    }
    if (ok && withVideo)
    {
        vIn->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
        //行跨度为正表示自上而下
        vIn->SetUINT32(MF_MT_DEFAULT_STRIDE, static_cast<UINT32>(width * kBgraBytes));
        ::MFSetAttributeSize(vIn, MF_MT_FRAME_SIZE, static_cast<UINT32>(width), static_cast<UINT32>(height));
        ::MFSetAttributeRatio(vIn, MF_MT_FRAME_RATE, static_cast<UINT32>(fps), 1);
        ::MFSetAttributeRatio(vIn, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
        hr = w->SetInputMediaType(vIdx, vIn, NULL);
        ok = SUCCEEDED(hr);
    }
    if (vOut != NULL)
        vOut->Release();
    if (vIn != NULL)
        vIn->Release();

    //声音：48 kHz 双声道 16 位 PCM → AAC
    DWORD aIdx = 0;
    if (ok)
    {
        IMFMediaType* aOut = makeType(MFMediaType_Audio, MFAudioFormat_AAC);
        IMFMediaType* aIn = makeType(MFMediaType_Audio, MFAudioFormat_PCM);
        ok = (aOut != NULL && aIn != NULL);
        if (ok)
        {
            aOut->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, kSampleBytes * 8);
            aOut->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, kRecordAudioRate);
            aOut->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, kRecordAudioChannels);
            aOut->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, kRecordAacBytesPerSecond);
            hr = w->AddStream(aOut, &aIdx);
            ok = SUCCEEDED(hr);
        }
        if (ok)
        {
            aIn->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, kSampleBytes * 8);
            aIn->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, kRecordAudioRate);
            aIn->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, kRecordAudioChannels);
            aIn->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, kRecordAudioChannels * kSampleBytes);
            aIn->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, kRecordAudioRate * kRecordAudioChannels * kSampleBytes);
            hr = w->SetInputMediaType(aIdx, aIn, NULL);
            ok = SUCCEEDED(hr);
        }
        if (aOut != NULL)
            aOut->Release();
        if (aIn != NULL)
            aIn->Release();
    }
    if (ok)
    {
        hr = w->BeginWriting();
        ok = SUCCEEDED(hr);
    }
    if (!ok)
    {
        LOG_ERROR("record: set up sink writer failed (video=%d, %dx%d @%d), hr=0x%08X.", withVideo ? 1 : 0, width,
                  height, fps, static_cast<unsigned>(hr));
        w->Release();
        return false;
    }
    m_writer = w;
    m_videoStream = vIdx;
    m_audioStream = aIdx;
    m_width = width;
    m_height = height;
    m_hasVideo = withVideo;
    if (withVideo)
        LOG_INFO("record: mp4 opened, %dx%d @%d fps, %u bps.", width, height, fps, static_cast<unsigned>(bitrate));
    else
        LOG_INFO("record: audio-only file opened.");
    return true;
}

bool RecordMp4Writer::WriteVideo(const uint8_t* bgra, int stride, int64_t ptsHns, int64_t durHns)
{
    //只录声音时文件里没有视频流
    if (m_writer == NULL || bgra == NULL || !m_hasVideo)
        return false;
    const int rowBytes = m_width * kBgraBytes;
    if (stride == rowBytes)
        return writeBuffer(m_writer, m_videoStream, bgra, static_cast<DWORD>(rowBytes * m_height), ptsHns, durHns);
    //行跨度不同：逐行拷进连续的缓冲
    std::string packed(static_cast<size_t>(rowBytes) * m_height, '\0');
    for (int y = 0; y < m_height; ++y)
        memcpy(&packed[static_cast<size_t>(y) * rowBytes], bgra + static_cast<size_t>(y) * stride, rowBytes);
    return writeBuffer(m_writer, m_videoStream, packed.data(), static_cast<DWORD>(packed.size()), ptsHns, durHns);
}

bool RecordMp4Writer::WriteAudio(const int16_t* stereo, int frames, int64_t ptsHns)
{
    if (m_writer == NULL || stereo == NULL || frames <= 0)
        return false;
    const int64_t dur = static_cast<int64_t>(frames) * kHnsPerSecond / kRecordAudioRate;
    return writeBuffer(m_writer, m_audioStream, stereo,
                       static_cast<DWORD>(frames * kRecordAudioChannels * kSampleBytes), ptsHns, dur);
}

bool RecordMp4Writer::Close()
{
    if (m_writer == NULL)
        return true;
    const HRESULT hr = m_writer->Finalize();
    m_writer->Release();
    m_writer = NULL;
    m_hasVideo = false;
    if (FAILED(hr))
    {
        LOG_ERROR("record: finalize failed, hr=0x%08X.", static_cast<unsigned>(hr));
        return false;
    }
    LOG_INFO("record: mp4 closed.");
    return true;
}

bool RecordMp4Writer::IsOpen() const
{
    return m_writer != NULL;
}

} // namespace screenrecord
