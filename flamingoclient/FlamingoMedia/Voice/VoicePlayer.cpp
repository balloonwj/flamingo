/**
 *  语音消息的播放器，说明见 VoicePlayer.h。
 *  balloonwj@qq.com   2026-10-04
 */
#include "VoicePlayer.h"

#include <windows.h>
#include <string.h>
#include "../../Source/Common/Log/IULog.h"   //须在 windows.h 之后

namespace voicemsg {

namespace {

//重采样质量（0~10），与通话一致
const int kResampleQuality = 5;
//48 kHz granule 与 16 kHz 采样之比：pre-skip 与有效采样数换算成 16 kHz 时用
const int kGranulePer16k = kOpusGranuleRate / kVoiceSampleRate;
//播放只用解码器，编码码率不起作用；传 0 表示使用 Opus 的默认值
const int kDecoderOnlyBitrate = 0;

} // namespace

bool VoiceWasapiRenderDevice::Start(call::WasapiRenderSource* source, int& outSampleRate)
{
    return m_render.Start(source, outSampleRate);
}

void VoiceWasapiRenderDevice::Stop()
{
    m_render.Stop();
}

VoicePlayer::VoicePlayer()
    : m_device(NULL)
    , m_listener(NULL)
    , m_playing(false)
    , m_finished(false)
    , m_resampling(false)
    , m_nextPacket(0)
    , m_skip16k(0)
    , m_remain16k(0)
    , m_totalMs(0)
    , m_deviceRate(0)
    , m_outputPos(0)
    , m_playedSamples(0)
    , m_lastProgressMs(0)
{
}

VoicePlayer::~VoicePlayer()
{
    //析构时还在播放：停止设备，避免渲染线程回调到已销毁的对象
    Stop();
}

bool VoicePlayer::Play(IVoiceRenderDevice* device, IVoicePlayerListener* listener, const std::wstring& path)
{
    if (device == NULL || path.empty())
        return false;
    //正在播放：先停止前一个
    Stop();

    //读文件、检查格式不需要持锁
    std::vector<uint8_t> bytes;
    if (!LoadBytesFromFile(path, bytes, kVoiceMaxFileBytes))
    {
        LOG_WARNING("voice play: cannot read file, bytes limit %u.", static_cast<unsigned>(kVoiceMaxFileBytes));
        return false;
    }
    VoiceOggReader reader;
    if (!reader.Parse(bytes))
    {
        LOG_WARNING("voice play: not a valid ogg opus file, %u bytes.", static_cast<unsigned>(bytes.size()));
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_codec.Init(kVoiceSampleRate, kVoiceSamplesPerFrame, kDecoderOnlyBitrate))
        {
            LOG_ERROR("voice play: opus decoder init failed.");
            resetLocked();
            return false;
        }
        m_packets = reader.Packets();
        m_nextPacket = 0;
        m_skip16k = reader.PreSkip48k() / kGranulePer16k;
        m_remain16k = reader.TotalSamples48k() / kGranulePer16k;
        m_totalMs = reader.DurationMs();
        m_device = device;
        m_listener = listener;
        m_finished = false;
        m_output.clear();
        m_outputPos = 0;
        m_playedSamples = 0;
        m_lastProgressMs = 0;
        //设备采样率未知之前不输出：m_playing 等设备打开之后再置位
        m_playing = false;
    }

    //打开设备时不持锁：设备可能在 Start 返回之前就开始回调，回调要取这把锁
    int deviceRate = 0;
    const bool opened = device->Start(this, deviceRate);
    std::lock_guard<std::mutex> lock(m_mutex);
    bool ok = opened && deviceRate > 0;
    if (ok)
    {
        m_deviceRate = deviceRate;
        m_resampling = (deviceRate != kVoiceSampleRate);
        if (m_resampling && !m_resampler.Init(kVoiceSampleRate, deviceRate, kResampleQuality))
            ok = false;
    }
    if (!ok)
    {
        LOG_ERROR("voice play: render device start failed, opened=%d, rate=%d.", opened ? 1 : 0, deviceRate);
        if (opened)
            device->Stop();
        resetLocked();
        return false;
    }
    m_playing = true;
    LOG_INFO("voice play: started, %lld ms, %u packets, device rate %d Hz.", static_cast<long long>(m_totalMs),
             static_cast<unsigned>(m_packets.size()), deviceRate);
    return true;
}

void VoicePlayer::Stop()
{
    if (!stopDevice())
        return;
    std::lock_guard<std::mutex> lock(m_mutex);
    LOG_INFO("voice play: stopped, finished %d.", m_finished ? 1 : 0);
    resetLocked();
}

bool VoicePlayer::IsPlaying() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_playing;
}

int64_t VoicePlayer::DurationMs() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_totalMs;
}

void VoicePlayer::OnAudioNeeded(int16_t* mono, int frames)
{
    if (mono == NULL || frames <= 0)
        return;
    //进度与结束的通知在释放锁之后发出
    std::vector<int64_t> progress;
    bool notifyFinished = false;
    int64_t totalMs = 0;
    IVoicePlayerListener* listener = NULL;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        int filled = 0;
        if (m_playing && !m_finished)
        {
            while (filled < frames)
            {
                if (m_outputPos >= m_output.size())
                {
                    m_output.clear();
                    m_outputPos = 0;
                    if (!decodeNextLocked())
                        break;
                    continue;
                }
                const size_t avail = m_output.size() - m_outputPos;
                const size_t want = static_cast<size_t>(frames - filled);
                const size_t take = (avail < want) ? avail : want;
                memcpy(mono + filled, &m_output[m_outputPos], take * sizeof(int16_t));
                m_outputPos += take;
                filled += static_cast<int>(take);
                m_playedSamples += static_cast<int64_t>(take);
            }
            //进度：每满 kVoicePlayProgressMs 毫秒报告一次，不超过总时长
            int64_t playedMs = m_playedSamples * kMillisPerSecond / m_deviceRate;
            if (playedMs > m_totalMs)
                playedMs = m_totalMs;
            while (playedMs - m_lastProgressMs >= kVoicePlayProgressMs)
            {
                m_lastProgressMs += kVoicePlayProgressMs;
                progress.push_back(m_lastProgressMs);
            }
            //包已解完、缓冲已交完：播完
            if (filled < frames && m_outputPos >= m_output.size() && m_nextPacket >= m_packets.size())
            {
                m_finished = true;
                notifyFinished = true;
                LOG_INFO("voice play: finished, %lld ms played.", static_cast<long long>(playedMs));
            }
        }
        //剩下的部分（没有在播放、已播完、或这一次数据不够）补静音
        if (filled < frames)
            memset(mono + filled, 0, static_cast<size_t>(frames - filled) * sizeof(int16_t));
        totalMs = m_totalMs;
        listener = m_listener;
    }
    if (listener == NULL)
        return;
    for (size_t i = 0; i < progress.size(); ++i)
        listener->OnVoicePlayProgress(progress[i], totalMs);
    if (notifyFinished)
        listener->OnVoicePlayFinished();
}

bool VoicePlayer::decodeNextLocked()
{
    while (m_nextPacket < m_packets.size())
    {
        const std::vector<uint8_t>& packet = m_packets[m_nextPacket];
        ++m_nextPacket;
        std::vector<int16_t> pcm;
        if (!m_codec.Decode(packet.empty() ? NULL : &packet[0], packet.size(), pcm) || pcm.empty())
        {
            LOG_WARNING("voice play: opus decode failed at packet %u.", static_cast<unsigned>(m_nextPacket - 1));
            continue;
        }
        //丢掉开头的 pre-skip
        size_t begin = 0;
        if (m_skip16k > 0)
        {
            const int64_t drop = (m_skip16k < static_cast<int64_t>(pcm.size())) ? m_skip16k
                                                                                : static_cast<int64_t>(pcm.size());
            begin = static_cast<size_t>(drop);
            m_skip16k -= drop;
        }
        //按有效采样数裁掉结尾补的静音
        size_t end = pcm.size();
        if (static_cast<int64_t>(end - begin) > m_remain16k)
            end = begin + static_cast<size_t>(m_remain16k);
        m_remain16k -= static_cast<int64_t>(end - begin);
        if (end <= begin)
        {
            //有效采样已全部输出：剩下的包都是补的静音，不再解码
            if (m_remain16k <= 0 && m_skip16k <= 0)
            {
                m_nextPacket = m_packets.size();
                return false;
            }
            continue;
        }
        if (m_resampling)
            m_resampler.Process(&pcm[begin], static_cast<int>(end - begin), m_output);
        else
            m_output.insert(m_output.end(), pcm.begin() + static_cast<ptrdiff_t>(begin),
                            pcm.begin() + static_cast<ptrdiff_t>(end));
        if (!m_output.empty())
            return true;
    }
    return false;
}

bool VoicePlayer::stopDevice()
{
    IVoiceRenderDevice* device = NULL;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_device == NULL)
            return false;
        device = m_device;
        //先停止输出，再停止设备
        m_playing = false;
    }
    device->Stop();
    return true;
}

void VoicePlayer::resetLocked()
{
    m_device = NULL;
    m_listener = NULL;
    m_playing = false;
    m_finished = false;
    m_resampling = false;
    m_resampler.Close();
    m_codec.Close();
    m_packets.clear();
    m_nextPacket = 0;
    m_skip16k = 0;
    m_remain16k = 0;
    m_totalMs = 0;
    m_deviceRate = 0;
    m_output.clear();
    m_outputPos = 0;
    m_playedSamples = 0;
    m_lastProgressMs = 0;
}

} // namespace voicemsg
