/**
 *  字幕编辑器的事后识别，见 CaptionPostRecognizer.h。
 *  balloonwj@qq.com   2026-10-06
 */
#include "CaptionPostRecognizer.h"

#include <atlbase.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>

#include "../Engine/CaptionAudio.h"   // kInputSampleRate
#include "../Engine/CaptionEngine.h"  // State：引擎是否加载失败
#include "../../Source/Common/Log/IULog.h"

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")

namespace caption {

namespace {

//时间换算
const LONGLONG kHnsPerMs = 10000;
const int64_t kMsPerSecond = 1000;
//进度的满分（千分比）
const int kPermilleFull = 1000;
//送完之后等识别流处理完的轮询间隔、排队超限时的等待（毫秒）
const DWORD kPollMs = 20;
//16 位 PCM 的位深与每采样字节数
const UINT32 kPcmBits = 16;
const UINT32 kPcmBytes = 2;

} // namespace

CaptionPostRecognizer::CaptionPostRecognizer()
    : m_notifyWnd(NULL)
    , m_msg(0)
    , m_running(false)
    , m_cancel(false)
    , m_flushed(false)
    , m_durationMs(0)
    , m_assembler(CaptionSegmentRules(), &m_replace)
    , m_ok(false)
    , m_cancelled(false)
{
}

CaptionPostRecognizer::~CaptionPostRecognizer()
{
    Cancel();
    Wait();
}

bool CaptionPostRecognizer::Start(const std::wstring& videoPath, const CaptionReplaceTable& replace, HWND notifyWnd,
                                  UINT msg)
{
    if (m_running.load())
        return false;
    Wait();
    m_videoPath = videoPath;
    m_replace = replace;
    m_notifyWnd = notifyWnd;
    m_msg = msg;
    m_cancel.store(false);
    m_flushed.store(false);
    m_durationMs = 0;
    {
        std::lock_guard<std::mutex> lock(m_resultMutex);
        m_track.Clear();
        m_assembler.Reset();
        m_ok = false;
        m_cancelled = false;
        m_error.clear();
    }
    m_running.store(true);
    m_thread.reset(new std::thread(&CaptionPostRecognizer::threadProc, this));
    return true;
}

void CaptionPostRecognizer::Cancel()
{
    m_cancel.store(true);
}

bool CaptionPostRecognizer::IsRunning() const
{
    return m_running.load();
}

void CaptionPostRecognizer::Wait()
{
    if (m_thread)
    {
        if (m_thread->joinable())
            m_thread->join();
        m_thread.reset();
    }
}

bool CaptionPostRecognizer::TakeResult(std::vector<CaptionCue>& cues, bool& cancelled, std::string& error)
{
    std::lock_guard<std::mutex> lock(m_resultMutex);
    cues = m_track.Cues();
    cancelled = m_cancelled;
    error = m_error;
    return m_ok;
}

void CaptionPostRecognizer::post(int event, LPARAM param)
{
    if (m_notifyWnd != NULL && ::IsWindow(m_notifyWnd))
        ::PostMessage(m_notifyWnd, m_msg, static_cast<WPARAM>(event), param);
}

void CaptionPostRecognizer::threadProc()
{
    ::SetThreadPriority(::GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
    const HRESULT co = ::CoInitializeEx(NULL, COINIT_MULTITHREADED);
    const HRESULT mf = ::MFStartup(MF_VERSION);
    LOG_INFO("caption postrec: started.");
    bool ok = false;
    CaptionStreamImpl* stream = CaptionWorker::Instance().CreateStream(caption_purpose_postrec, this);
    if (stream == NULL)
    {
        std::lock_guard<std::mutex> lock(m_resultMutex);
        m_error = "recognition engine unavailable";
    }
    else if (!feed(stream))
    {
        //失败或取消：立即关闭，不再回调
        stream->Close();
    }
    else
    {
        //送完了：让识别流把剩余的声音识别完
        //即使中途取消也要等它处理完：处理完之前它还会回调本对象，提前退出后本对象可能已被销毁。
        //排队上限只有 kPostRecMaxQueuedSec 秒的声音，处理完只要一两秒
        stream->CloseWithFlush(this);
        while (!m_flushed.load())
            ::Sleep(kPollMs);
        //引擎加载失败时识别流会丢弃全部声音：报失败，不当作「没有识别出字幕」
        if (CaptionEngine::Instance().State() == caption_engine_failed)
        {
            std::lock_guard<std::mutex> lock(m_resultMutex);
            m_error = "recognition engine failed to load";
        }
        else
        {
            ok = true;
        }
    }
    {
        std::lock_guard<std::mutex> lock(m_resultMutex);
        if (m_cancel.load())
            m_cancelled = true;
        m_ok = ok && !m_cancelled;
        LOG_INFO("caption postrec: finished, ok=%d, cancelled=%d, %u cues, error=%s.", m_ok ? 1 : 0,
                 m_cancelled ? 1 : 0, static_cast<unsigned>(m_track.Count()), m_error.c_str());
    }
    if (SUCCEEDED(mf))
        ::MFShutdown();
    if (SUCCEEDED(co))
        ::CoUninitialize();
    m_running.store(false);
    post(caption_postrec_done, 0);
}

bool CaptionPostRecognizer::feed(CaptionStreamImpl* stream)
{
    CComPtr<IMFSourceReader> reader;
    HRESULT hr = MFCreateSourceReaderFromURL(m_videoPath.c_str(), NULL, &reader);
    if (FAILED(hr))
    {
        std::lock_guard<std::mutex> lock(m_resultMutex);
        m_error = "open video failed";
        return false;
    }
    const DWORD audioIndex = static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM);
    reader->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS), FALSE);
    if (FAILED(reader->SetStreamSelection(audioIndex, TRUE)))
    {
        std::lock_guard<std::mutex> lock(m_resultMutex);
        m_error = "no audio stream";
        return false;
    }
    PROPVARIANT var;
    PropVariantInit(&var);
    if (SUCCEEDED(reader->GetPresentationAttribute(static_cast<DWORD>(MF_SOURCE_READER_MEDIASOURCE), MF_PD_DURATION,
                                                   &var)) && var.vt == VT_UI8)
        m_durationMs = static_cast<int64_t>(var.uhVal.QuadPart / kHnsPerMs);
    PropVariantClear(&var);

    //先请求 48 kHz 单声道 16 位；不行就取原声道数与采样率的 16 位 PCM，自己转换
    CComPtr<IMFMediaType> pcm;
    MFCreateMediaType(&pcm);
    pcm->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    pcm->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
    pcm->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, kPcmBits);
    pcm->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 1);
    pcm->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, kInputSampleRate);
    pcm->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, kPcmBytes);
    pcm->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, kInputSampleRate * kPcmBytes);
    hr = reader->SetCurrentMediaType(audioIndex, NULL, pcm);
    if (FAILED(hr))
    {
        CComPtr<IMFMediaType> plain;
        MFCreateMediaType(&plain);
        plain->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
        plain->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
        plain->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, kPcmBits);
        hr = reader->SetCurrentMediaType(audioIndex, NULL, plain);
    }
    if (FAILED(hr))
    {
        std::lock_guard<std::mutex> lock(m_resultMutex);
        m_error = "audio cannot be decoded to pcm";
        return false;
    }
    CComPtr<IMFMediaType> cur;
    reader->GetCurrentMediaType(audioIndex, &cur);
    const UINT32 channels = MFGetAttributeUINT32(cur, MF_MT_AUDIO_NUM_CHANNELS, 1);
    const UINT32 rate = MFGetAttributeUINT32(cur, MF_MT_AUDIO_SAMPLES_PER_SECOND, kInputSampleRate);
    if (channels == 0 || rate == 0)
    {
        std::lock_guard<std::mutex> lock(m_resultMutex);
        m_error = "invalid audio format";
        return false;
    }
    LOG_INFO("caption postrec: audio %u ch %u Hz, duration %lld ms.", channels, rate,
             static_cast<long long>(m_durationMs));

    const size_t maxQueued = static_cast<size_t>(kInputSampleRate) * kPostRecMaxQueuedSec;
    std::vector<int16_t> mono;
    std::vector<int16_t> out;
    int lastPermille = -1;
    for (;;)
    {
        if (m_cancel.load())
            return false;
        //排队太多：等识别线程消化
        while (stream->QueuedSamples() > maxQueued)
        {
            if (m_cancel.load())
                return false;
            ::Sleep(kPollMs);
        }
        DWORD idx = 0;
        DWORD flags = 0;
        LONGLONG ts = 0;
        CComPtr<IMFSample> sample;
        hr = reader->ReadSample(audioIndex, 0, &idx, &flags, &ts, &sample);
        if (FAILED(hr))
        {
            std::lock_guard<std::mutex> lock(m_resultMutex);
            m_error = "read audio failed";
            return false;
        }
        if ((flags & MF_SOURCE_READERF_ENDOFSTREAM) != 0)
            break;
        if (sample == NULL)
            continue;
        CComPtr<IMFMediaBuffer> buffer;
        if (FAILED(sample->ConvertToContiguousBuffer(&buffer)))
            continue;
        BYTE* data = NULL;
        DWORD len = 0;
        if (FAILED(buffer->Lock(&data, NULL, &len)))
            continue;
        const int16_t* pcmData = reinterpret_cast<const int16_t*>(data);
        const size_t frames = len / (kPcmBytes * channels);
        //混成单声道
        mono.resize(frames);
        for (size_t f = 0; f < frames; ++f)
        {
            int sum = 0;
            for (UINT32 c = 0; c < channels; ++c)
                sum += pcmData[f * channels + c];
            mono[f] = static_cast<int16_t>(sum / static_cast<int>(channels));
        }
        buffer->Unlock();
        //采样率不是 48 kHz 时线性插值
        const std::vector<int16_t>* toPush = &mono;
        if (rate != static_cast<UINT32>(kInputSampleRate) && frames > 0)
        {
            const size_t outFrames = static_cast<size_t>(static_cast<uint64_t>(frames) * kInputSampleRate / rate);
            out.resize(outFrames);
            for (size_t i = 0; i < outFrames; ++i)
            {
                const double pos = static_cast<double>(i) * rate / kInputSampleRate;
                const size_t i0 = static_cast<size_t>(pos);
                const size_t i1 = (i0 + 1 < frames) ? i0 + 1 : i0;
                const double frac = pos - static_cast<double>(i0);
                out[i] = static_cast<int16_t>(mono[i0] * (1.0 - frac) + mono[i1] * frac);
            }
            toPush = &out;
        }
        const int64_t ptsMs = ts / kHnsPerMs;
        if (!toPush->empty())
            stream->PushPcm(&(*toPush)[0], static_cast<uint32_t>(toPush->size()), ptsMs);
        //进度：已送入的时刻减去还在排队的部分
        if (m_durationMs > 0)
        {
            const int64_t queuedMs = static_cast<int64_t>(stream->QueuedSamples()) * kMsPerSecond / kInputSampleRate;
            int permille = static_cast<int>((ptsMs - queuedMs) * kPermilleFull / m_durationMs);
            if (permille < 0)
                permille = 0;
            if (permille > kPermilleFull)
                permille = kPermilleFull;
            if (permille != lastPermille)
            {
                lastPermille = permille;
                post(caption_postrec_progress, static_cast<LPARAM>(permille));
            }
        }
    }
    return true;
}

void CaptionPostRecognizer::OnCaptionPartial(const wchar_t* text)
{
    (void)text;
}

void CaptionPostRecognizer::OnCaptionFinal(const wchar_t* text, int64_t startMs, int64_t endMs)
{
    if (text == NULL)
        return;
    std::lock_guard<std::mutex> lock(m_resultMutex);
    m_assembler.OnFinal(m_track, text, startMs, endMs, std::wstring(), caption_source_postrec);
}

void CaptionPostRecognizer::OnStreamFlushed()
{
    m_flushed.store(true);
}

} // namespace caption
