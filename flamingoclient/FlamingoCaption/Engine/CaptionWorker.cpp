/**
 *  识别线程与识别流，实现见 CaptionWorker.h。
 *
 *  balloonwj@qq.com   2026-10-06
 */
#include "stdafx.h"

#include "CaptionWorker.h"

#include <stdlib.h>
#include <string.h>
#include <chrono>

#include "../../Source/Common/Log/IULog.h"
#include "../Model/CaptionText.h"
#include "CaptionEngine.h"

#ifdef CAPTION_HAS_ENGINE
#include "sherpa-onnx/c-api/c-api.h"
#endif

namespace caption {

namespace {

//识别线程没有唤醒时的轮询间隔（毫秒）：也是检查空闲卸载的间隔
const int kWorkerTickMs = 100;
//每路最多缓存的 48 kHz 采样数
const size_t kMaxQueuedSamples = static_cast<size_t>(kInputSampleRate) * kMaxQueuedSeconds;
//关闭时补的静音（16 kHz 采样数，0.8 秒）：让最后一句的尾字出来
const size_t kTailSilenceSamples = kModelSampleRate * 8 / 10;
//定稿时最后一个词片之后再算多长（16 kHz 采样数，0.3 秒）：词片时间戳是它被识别出来的那一帧，字还没念完
const int64_t kTokenTailSamples = kModelSampleRate * 3 / 10;
//没有词片时间戳时，按端点规则从结尾扣掉的静音（16 kHz 采样数，0.8 秒）
const int64_t kTrailingSilenceSamples = kModelSampleRate * 8 / 10;
//一句字幕至少多长（16 kHz 采样数，0.3 秒）
const int64_t kMinFinalSamples = kModelSampleRate * 3 / 10;
//时间换算表保留的最近时长（16 kHz 采样数，60 秒），更早的段丢弃
const int64_t kTimeMapKeepSamples = static_cast<int64_t>(kModelSampleRate) * 60;

int64_t NowMs()
{
    return static_cast<int64_t>(GetTickCount64());
}

#ifdef CAPTION_HAS_ENGINE
//从结果的 JSON 里取 "start_time"（秒）；没有时返回 0
double ParseStartTime(const char* json)
{
    if (json == NULL)
        return 0.0;
    const char* key = strstr(json, "\"start_time\"");
    if (key == NULL)
        return 0.0;
    const char* colon = strchr(key, ':');
    if (colon == NULL)
        return 0.0;
    return strtod(colon + 1, NULL);
}
#endif

} // namespace

CaptionStreamImpl::CaptionStreamImpl(CaptionWorker* worker, int purpose, ICaptionStreamListener* listener)
    : m_worker(worker)
    , m_purpose(purpose)
    , m_listener(listener)
    , m_queuedSamples(0)
    , m_closing(false)
    , m_silent(false)
    , m_flush(NULL)
    , m_sherpaStream(NULL)
    , m_fedSamples(0)
{
}

CaptionStreamImpl::~CaptionStreamImpl()
{
}

void CaptionStreamImpl::PushPcm(const int16_t* mono48k, uint32_t samples, int64_t ptsMs)
{
    if (mono48k == NULL || samples == 0 || m_closing.load())
        return;
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        Chunk c;
        c.m_pcm.assign(mono48k, mono48k + samples);
        c.m_ptsMs = ptsMs;
        m_queue.push_back(c);
        m_queuedSamples += samples;
        //模型加载期间积压太多：丢弃最早的
        while (m_queuedSamples > kMaxQueuedSamples && m_queue.size() > 1)
        {
            m_queuedSamples -= m_queue.front().m_pcm.size();
            m_queue.pop_front();
        }
    }
    m_worker->Wake();
}

size_t CaptionStreamImpl::QueuedSamples()
{
    std::lock_guard<std::mutex> lock(m_queueMutex);
    return m_queuedSamples;
}

void CaptionStreamImpl::Close()
{
    {
        //拿到回调锁才能保证：返回之后识别线程不会再回调
        std::lock_guard<std::mutex> lock(m_cbMutex);
        m_silent = true;
    }
    m_closing = true;
    m_worker->Wake();
}

void CaptionStreamImpl::CloseWithFlush(ICaptionFlushListener* flush)
{
    {
        std::lock_guard<std::mutex> lock(m_cbMutex);
        m_flush = flush;
    }
    m_closing = true;
    m_worker->Wake();
}

CaptionWorker& CaptionWorker::Instance()
{
    static CaptionWorker instance;
    return instance;
}

CaptionWorker::CaptionWorker()
    : m_started(false)
    , m_stop(false)
    , m_wakePending(false)
{
}

CaptionWorker::~CaptionWorker()
{
}

CaptionStreamImpl* CaptionWorker::CreateStream(int purpose, ICaptionStreamListener* listener)
{
    CaptionEngine& engine = CaptionEngine::Instance();
    if (engine.State() == caption_engine_failed)
        return NULL;
    engine.Preload();
    CaptionStreamImpl* s = new CaptionStreamImpl(this, purpose, listener);
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_streams.push_back(s);
        if (!m_started)
        {
            m_stop = false;
            m_thread = std::thread(&CaptionWorker::Run, this);
            m_started = true;
        }
    }
    LOG_INFO("caption: stream created, purpose %d.", purpose);
    return s;
}

void CaptionWorker::SetReplaceTable(const CaptionReplaceTable& table)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_replace = table;
}

void CaptionWorker::Wake()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_wakePending = true;
    }
    m_cv.notify_one();
}

void CaptionWorker::Shutdown()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_started)
            return;
        m_stop = true;
        m_wakePending = true;
    }
    m_cv.notify_one();
    m_thread.join();
    std::lock_guard<std::mutex> lock(m_mutex);
    m_started = false;
}

std::wstring CaptionWorker::PostProcess(const std::wstring& text)
{
    const std::wstring t = NormalizeText(text);
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_replace.Apply(t, NULL);
}

void CaptionWorker::EmitPartial(CaptionStreamImpl* s, const std::wstring& text)
{
    std::lock_guard<std::mutex> lock(s->m_cbMutex);
    if (!s->m_silent && s->m_listener != NULL)
        s->m_listener->OnCaptionPartial(text.c_str());
}

void CaptionWorker::EmitFinal(CaptionStreamImpl* s, const std::wstring& text, int64_t startMs, int64_t endMs)
{
    std::lock_guard<std::mutex> lock(s->m_cbMutex);
    if (!s->m_silent && s->m_listener != NULL)
        s->m_listener->OnCaptionFinal(text.c_str(), startMs, endMs);
}

void CaptionWorker::FinishStream(CaptionStreamImpl* s)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (size_t i = 0; i < m_streams.size(); ++i)
        {
            if (m_streams[i] == s)
            {
                m_streams.erase(m_streams.begin() + static_cast<ptrdiff_t>(i));
                break;
            }
        }
    }
    ICaptionFlushListener* flush = NULL;
    {
        std::lock_guard<std::mutex> lock(s->m_cbMutex);
        flush = s->m_silent ? NULL : s->m_flush;
    }
    LOG_INFO("caption: stream finished, purpose %d, %lld samples recognized.", s->m_purpose, s->m_fedSamples);
    delete s;
    if (flush != NULL)
        flush->OnStreamFlushed();
}

void CaptionWorker::CheckResult(CaptionStreamImpl* s, bool force)
{
#ifdef CAPTION_HAS_ENGINE
    const SherpaOnnxOnlineRecognizer* rec =
        static_cast<const SherpaOnnxOnlineRecognizer*>(CaptionEngine::Instance().Recognizer());
    const SherpaOnnxOnlineStream* stream = static_cast<const SherpaOnnxOnlineStream*>(s->m_sherpaStream);
    const SherpaOnnxOnlineRecognizerResult* r = SherpaOnnxGetOnlineStreamResult(rec, stream);
    if (r == NULL)
        return;
    const std::wstring text = r->text != NULL ? PostProcess(Utf8ToWide(r->text)) : std::wstring();
    const bool endpoint = force || SherpaOnnxOnlineStreamIsEndpoint(rec, stream) != 0;
    if (endpoint)
    {
        if (!text.empty())
        {
            //段起点 + 词片时间戳 → 识别流里的采样位置 → 推入时的时刻
            const double segStart = ParseStartTime(r->json);
            int64_t startSample = static_cast<int64_t>(segStart * kModelSampleRate);
            int64_t endSample = s->m_fedSamples - kTrailingSilenceSamples;
            if (r->timestamps != NULL && r->count > 0)
            {
                startSample = static_cast<int64_t>((segStart + r->timestamps[0]) * kModelSampleRate);
                endSample = static_cast<int64_t>((segStart + r->timestamps[r->count - 1]) * kModelSampleRate)
                    + kTokenTailSamples;
            }
            if (endSample > s->m_fedSamples)
                endSample = s->m_fedSamples;
            if (endSample - startSample < kMinFinalSamples)
                endSample = startSample + kMinFinalSamples;
            const int64_t startMs = s->m_timeMap.ToPts(startSample);
            const int64_t endMs = s->m_timeMap.ToPts(endSample);
            EmitFinal(s, text, startMs, endMs);
        }
        if (!force)
            SherpaOnnxOnlineStreamReset(rec, stream);
        s->m_lastPartial.clear();
        s->m_timeMap.DropBefore(s->m_fedSamples - kTimeMapKeepSamples);
    }
    else if (text != s->m_lastPartial)
    {
        s->m_lastPartial = text;
        EmitPartial(s, text);
    }
    SherpaOnnxDestroyOnlineRecognizerResult(r);
#else
    (void)s;
    (void)force;
#endif
}

bool CaptionWorker::ProcessStream(CaptionStreamImpl* s, int engineState)
{
    const bool closing = s->m_closing.load();
    //模型还没加载好：声音留在队列里等（PushPcm 会丢弃超出上限的部分）
    if (engineState == caption_engine_loading || engineState == caption_engine_idle)
    {
        if (engineState == caption_engine_idle)
            CaptionEngine::Instance().Preload();
        return false;
    }
    //引擎不可用：丢弃声音；已关闭的流直接收尾
    if (engineState != caption_engine_ready)
    {
        {
            std::lock_guard<std::mutex> lock(s->m_queueMutex);
            s->m_queue.clear();
            s->m_queuedSamples = 0;
        }
        if (closing)
        {
            FinishStream(s);
            return true;
        }
        return false;
    }
#ifdef CAPTION_HAS_ENGINE
    const SherpaOnnxOnlineRecognizer* rec =
        static_cast<const SherpaOnnxOnlineRecognizer*>(CaptionEngine::Instance().Recognizer());
    if (s->m_sherpaStream == NULL)
    {
        s->m_sherpaStream = SherpaOnnxCreateOnlineStream(rec);
        if (s->m_sherpaStream == NULL)
        {
            LOG_ERROR("caption: failed to create a recognition stream, purpose %d.", s->m_purpose);
            {
                std::lock_guard<std::mutex> lock(s->m_queueMutex);
                s->m_queue.clear();
                s->m_queuedSamples = 0;
            }
            if (closing)
            {
                FinishStream(s);
                return true;
            }
            return false;
        }
    }
    const SherpaOnnxOnlineStream* stream = static_cast<const SherpaOnnxOnlineStream*>(s->m_sherpaStream);
    std::deque<CaptionStreamImpl::Chunk> chunks;
    {
        std::lock_guard<std::mutex> lock(s->m_queueMutex);
        chunks.swap(s->m_queue);
        s->m_queuedSamples = 0;
    }
    std::vector<float> f;
    for (size_t i = 0; i < chunks.size(); ++i)
    {
        f.clear();
        s->m_resampler.Process(chunks[i].m_pcm.data(), chunks[i].m_pcm.size(), f);
        if (f.empty())
            continue;
        s->m_timeMap.Append(s->m_fedSamples, chunks[i].m_ptsMs, f.size());
        SherpaOnnxOnlineStreamAcceptWaveform(stream, kModelSampleRate, f.data(), static_cast<int32_t>(f.size()));
        s->m_fedSamples += static_cast<int64_t>(f.size());
        while (SherpaOnnxIsOnlineStreamReady(rec, stream))
            SherpaOnnxDecodeOnlineStream(rec, stream);
        CheckResult(s, false);
    }
    if (closing)
    {
        bool silent = false;
        {
            std::lock_guard<std::mutex> lock(s->m_cbMutex);
            silent = s->m_silent;
        }
        if (!silent)
        {
            const std::vector<float> tail(kTailSilenceSamples, 0.0f);
            SherpaOnnxOnlineStreamAcceptWaveform(stream, kModelSampleRate, tail.data(),
                                                 static_cast<int32_t>(tail.size()));
            s->m_fedSamples += static_cast<int64_t>(tail.size());
            SherpaOnnxOnlineStreamInputFinished(stream);
            while (SherpaOnnxIsOnlineStreamReady(rec, stream))
                SherpaOnnxDecodeOnlineStream(rec, stream);
            CheckResult(s, true);
        }
        SherpaOnnxDestroyOnlineStream(stream);
        s->m_sherpaStream = NULL;
        FinishStream(s);
        return true;
    }
#endif
    return false;
}

void CaptionWorker::Run()
{
    LOG_INFO("caption: recognition thread started.");
    int64_t idleSince = NowMs();
    for (;;)
    {
        std::vector<CaptionStreamImpl*> snapshot;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            if (!m_wakePending && !m_stop.load())
                m_cv.wait_for(lock, std::chrono::milliseconds(kWorkerTickMs));
            m_wakePending = false;
            if (m_stop.load())
                break;
            snapshot = m_streams;
        }
        const int state = CaptionEngine::Instance().State();
        for (size_t i = 0; i < snapshot.size(); ++i)
            ProcessStream(snapshot[i], state);

        bool empty = false;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            empty = m_streams.empty();
        }
        if (!empty)
            idleSince = NowMs();
        else if (state == caption_engine_ready && NowMs() - idleSince > kCaptionIdleUnloadMs)
            CaptionEngine::Instance().Unload();
    }

    //退出：仍未处理完的流不再回调，直接释放
    std::vector<CaptionStreamImpl*> rest;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        rest.swap(m_streams);
    }
    for (size_t i = 0; i < rest.size(); ++i)
    {
#ifdef CAPTION_HAS_ENGINE
        if (rest[i]->m_sherpaStream != NULL)
            SherpaOnnxDestroyOnlineStream(static_cast<const SherpaOnnxOnlineStream*>(rest[i]->m_sherpaStream));
#endif
        delete rest[i];
    }
    LOG_INFO("caption: recognition thread stopped, %u stream(s) dropped.", static_cast<unsigned>(rest.size()));
}

} // namespace caption
