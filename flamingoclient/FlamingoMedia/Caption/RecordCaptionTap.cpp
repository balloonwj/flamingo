/**
 *  录屏的语音字幕旁路，见 RecordCaptionTap.h。
 *  balloonwj@qq.com   2026-10-06
 */
#include "RecordCaptionTap.h"

namespace flamingomedia {

CRecordCaptionTap::CRecordCaptionTap()
    : m_recording(NULL)
    , m_mic(false)
    , m_system(false)
    , m_suspended(false)
{
}

void CRecordCaptionTap::Attach(caption::ICaptionRecording* recording, bool mic, bool system)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_recording = recording;
    m_mic = mic;
    m_system = system;
}

caption::ICaptionRecording* CRecordCaptionTap::Detach()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    caption::ICaptionRecording* recording = m_recording;
    m_recording = NULL;
    return recording;
}

bool CRecordCaptionTap::IsAttached() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_recording != NULL;
}

caption::ICaptionRecording* CRecordCaptionTap::Current() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_recording;
}

void CRecordCaptionTap::SetSources(bool mic, bool system)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_mic = mic;
    m_system = system;
}

void CRecordCaptionTap::SetSuspended(bool suspended)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_suspended = suspended;
}

bool CRecordCaptionTap::IsSuspended() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_suspended;
}

void CRecordCaptionTap::OnRecordAudio(int source, const int16_t* pcm, size_t samples, int64_t ptsMs)
{
    if (pcm == NULL || samples == 0)
        return;
    //持锁调用：草稿会话的 Push 只把声音放进队列，很快返回；持锁保证 Detach 之后不会再有调用
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_recording == NULL)
        return;
    //关闭了字幕：换成同样长度的静音，时刻不变
    if (m_suspended)
    {
        if (m_silence.size() < samples)
            m_silence.resize(samples, 0);
        pcm = &m_silence[0];
    }
    const uint32_t count = static_cast<uint32_t>(samples);
    switch (source)
    {
    //本人麦克风：说话人记为本人
    case screenrecord::record_audio_mic:
        if (m_mic)
            m_recording->PushMic(pcm, count, ptsMs);
        break;
    //电脑声音：说话人记为「电脑声音」
    case screenrecord::record_audio_system:
        if (m_system)
            m_recording->PushSystem(pcm, count, ptsMs);
        break;
    //房间里其他人的声音：不识别，他们的字幕取自字幕控制帧
    default:
        break;
    }
}

} // namespace flamingomedia
