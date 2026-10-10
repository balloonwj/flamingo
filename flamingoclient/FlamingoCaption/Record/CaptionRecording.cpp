/**
 *  一次录制的字幕草稿，实现见 CaptionRecording.h。
 *
 *  balloonwj@qq.com   2026-10-06
 */
#include "stdafx.h"

#include "CaptionRecording.h"

#include "../../Source/Common/Log/IULog.h"
#include "../Model/CaptionFiles.h"

namespace caption {

namespace {

int64_t NowMs()
{
    return static_cast<int64_t>(GetTickCount64());
}

int64_t FileBytes(const std::wstring& path)
{
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data))
        return 0;
    return (static_cast<int64_t>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
}

} // namespace

CaptionRecordingChannel::CaptionRecordingChannel()
    : m_owner(NULL)
    , m_stream(NULL)
    , m_source(caption_source_mic)
{
}

void CaptionRecordingChannel::OnCaptionPartial(const wchar_t* text)
{
    m_owner->OnChannelPartial(this, text);
}

void CaptionRecordingChannel::OnCaptionFinal(const wchar_t* text, int64_t startMs, int64_t endMs)
{
    m_owner->OnChannelFinal(this, text, startMs, endMs);
}

void CaptionRecordingChannel::OnStreamFlushed()
{
    m_owner->OnChannelFlushed(this);
}

CaptionRecording* CaptionRecording::Create(const CaptionRecordingParams* params, const CaptionReplaceTable& replace)
{
    if (params == NULL || params->m_cbSize != sizeof(CaptionRecordingParams) || params->m_videoPath == NULL
        || params->m_videoPath[0] == 0)
        return NULL;
    //两路都不识别：录制屏幕共享时字幕全部来自房间里的实时字幕（AddRemoteCaption），不建识别流
    const bool wantStreams = params->m_mic || params->m_system;
    CaptionRecording* r = new CaptionRecording(params->m_videoPath, replace);
    if (params->m_mic)
    {
        r->m_mic.m_speaker = params->m_selfName != NULL ? params->m_selfName : L"";
        r->m_mic.m_stream = CaptionWorker::Instance().CreateStream(caption_purpose_live, &r->m_mic);
    }
    if (params->m_system)
        r->m_system.m_stream = CaptionWorker::Instance().CreateStream(caption_purpose_live, &r->m_system);
    if (wantStreams && r->m_mic.m_stream == NULL && r->m_system.m_stream == NULL)
    {
        delete r;
        return NULL;
    }
    {
        std::lock_guard<std::mutex> lock(r->m_mutex);
        r->SaveDraftLocked(true);
    }
    LOG_INFO("caption: recording draft started, mic %d, system %d.", params->m_mic ? 1 : 0, params->m_system ? 1 : 0);
    return r;
}

CaptionRecording::CaptionRecording(const std::wstring& videoPath, const CaptionReplaceTable& replace)
    : m_videoPath(videoPath)
    , m_draftPath(CaptionDraftIo::DraftPathFor(videoPath))
    , m_replace(replace)
    , m_assembler(CaptionSegmentRules(), &m_replace)
    , m_pendingFlush(0)
    , m_finishing(false)
    , m_lastSaveMs(0)
    , m_dirty(false)
    , m_observer(NULL)
{
    m_draft.m_videoName = FileNameOf(videoPath);
    m_mic.m_owner = this;
    m_mic.m_source = caption_source_mic;
    m_system.m_owner = this;
    m_system.m_source = caption_source_system;
}

CaptionRecording::~CaptionRecording()
{
}

void CaptionRecording::PushMic(const int16_t* mono48k, uint32_t samples, int64_t ptsMs)
{
    if (m_mic.m_stream != NULL)
        m_mic.m_stream->PushPcm(mono48k, samples, ptsMs);
}

void CaptionRecording::PushSystem(const int16_t* mono48k, uint32_t samples, int64_t ptsMs)
{
    if (m_system.m_stream != NULL)
        m_system.m_stream->PushPcm(mono48k, samples, ptsMs);
}

void CaptionRecording::AddRemoteCaption(const wchar_t* speaker, const wchar_t* text, int64_t startMs, int64_t endMs)
{
    if (text == NULL)
        return;
    std::lock_guard<std::mutex> lock(m_mutex);
    m_assembler.OnFinal(m_track, text, startMs, endMs, speaker != NULL ? speaker : L"", caption_source_others);
    m_dirty = true;
    SaveDraftLocked(false);
}

void CaptionRecording::SetPip(bool present, double left, double top, double right, double bottom)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_draft.m_pip.m_present = present;
    m_draft.m_pip.m_left = left;
    m_draft.m_pip.m_top = top;
    m_draft.m_pip.m_right = right;
    m_draft.m_pip.m_bottom = bottom;
    m_dirty = true;
}

void CaptionRecording::SetObserver(ICaptionRecordingObserver* observer)
{
    std::lock_guard<std::mutex> lock(m_observerMutex);
    m_observer = observer;
}

void CaptionRecording::OnChannelPartial(CaptionRecordingChannel* ch, const wchar_t* text)
{
    if (text == NULL)
        return;
    std::lock_guard<std::mutex> lock(m_observerMutex);
    if (m_observer != NULL)
        m_observer->OnRecordingPartial(ch->m_source, text);
}

void CaptionRecording::OnChannelFinal(CaptionRecordingChannel* ch, const wchar_t* text, int64_t startMs, int64_t endMs)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        //识别流给出的文字已整理并应用过替换表，装配时再做一遍不影响结果
        m_assembler.OnFinal(m_track, text, startMs, endMs, ch->m_speaker, ch->m_source);
        m_dirty = true;
        SaveDraftLocked(false);
    }
    //先写进草稿再通知观察者；两把锁不同时持有
    if (text == NULL)
        return;
    std::lock_guard<std::mutex> lock(m_observerMutex);
    if (m_observer != NULL)
        m_observer->OnRecordingFinal(ch->m_source, text);
}

void CaptionRecording::OnChannelFlushed(CaptionRecordingChannel* ch)
{
    bool done = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        ch->m_stream = NULL;
        --m_pendingFlush;
        done = m_finishing && m_pendingFlush <= 0;
        if (done)
        {
            m_draft.m_videoBytes = FileBytes(m_videoPath);
            SaveDraftLocked(true);
        }
    }
    if (done)
    {
        LOG_INFO("caption: recording draft finished, %u cue(s).", static_cast<unsigned>(m_track.Count()));
        delete this;
    }
}

void CaptionRecording::Finish(int64_t durationMs)
{
    //结束之后剩余声音的识别结果只写进草稿，不再通知观察者（调用方此后可能销毁它）
    SetObserver(NULL);
    CaptionStreamImpl* mic = NULL;
    CaptionStreamImpl* system = NULL;
    bool nothingPending = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_finishing = true;
        m_draft.m_videoDurationMs = durationMs;
        mic = m_mic.m_stream;
        system = m_system.m_stream;
        m_pendingFlush = (mic != NULL ? 1 : 0) + (system != NULL ? 1 : 0);
        nothingPending = m_pendingFlush == 0;
        if (nothingPending)
        {
            m_draft.m_videoBytes = FileBytes(m_videoPath);
            SaveDraftLocked(true);
        }
    }
    if (nothingPending)
    {
        delete this;
        return;
    }
    //关闭之后识别线程可能马上回调 OnChannelFlushed 并释放本对象，所以之后不再访问成员
    if (mic != NULL)
        mic->CloseWithFlush(&m_mic);
    if (system != NULL)
        system->CloseWithFlush(&m_system);
}

void CaptionRecording::Cancel()
{
    SetObserver(NULL);
    if (m_mic.m_stream != NULL)
        m_mic.m_stream->Close();
    if (m_system.m_stream != NULL)
        m_system.m_stream->Close();
    DeleteFileW(m_draftPath.c_str());
    LOG_INFO("caption: recording draft cancelled.");
    delete this;
}

size_t CaptionRecording::CueCount()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_track.Count();
}

void CaptionRecording::SaveDraftLocked(bool force)
{
    const int64_t now = NowMs();
    if (!force && (!m_dirty || now - m_lastSaveMs < kDraftSaveIntervalMs))
        return;
    m_draft.m_cues = m_track.Cues();
    m_draft.m_nextId = m_track.NextId();
    if (!CaptionDraftIo::SaveFile(m_draftPath, m_draft))
        LOG_WARNING("caption: failed to save the recording draft.");
    m_lastSaveMs = now;
    m_dirty = false;
}

} // namespace caption
