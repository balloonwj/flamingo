/**
 *  语音消息的录音器，说明见 VoiceRecorder.h。
 *  balloonwj@qq.com   2026-10-04
 */
#include "VoiceRecorder.h"

#include <windows.h>
#include <math.h>
#include "../../Source/Common/Log/IULog.h"   //须在 windows.h 之后

namespace voicemsg {

namespace {

//重采样质量（0~10），与通话一致
const int kResampleQuality = 5;
//音量换算：kVoiceLevelFloorDb 及以下为 0，0 dB（满幅）为 kVoiceLevelMax
const double kVoiceLevelFloorDb = -50.0;
//16 位采样的满幅
const double kFullScale = 32768.0;
//幅度比换算成分贝的系数（20·log10）
const double kDbPerLog10 = 20.0;
//四舍五入用的半个单位
const double kHalf = 0.5;
//48 kHz granule 与 16 kHz 采样之比：lookahead 与采样数换算成 granule 时用
const int kGranulePer16k = kOpusGranuleRate / kVoiceSampleRate;
//最长录音对应的 16 kHz 采样数
const int64_t kMaxSamples16k = static_cast<int64_t>(kVoiceMaxSeconds) * kVoiceSampleRate;
//每次音量通知覆盖的 16 kHz 采样数
const int kLevelWindowSamples = kVoiceSampleRate * kVoiceLevelIntervalMs / kMillisPerSecond;

//一段采样的均方根
double rmsOf(const int16_t* pcm, int count)
{
    if (pcm == NULL || count <= 0)
        return 0.0;
    double sum = 0.0;
    for (int i = 0; i < count; ++i)
        sum += static_cast<double>(pcm[i]) * pcm[i];
    return sqrt(sum / count);
}

} // namespace

VoiceRecordResult::VoiceRecordResult()
    : m_durationMs(0)
    , m_fileBytes(0)
{
}

bool VoiceWasapiCaptureDevice::Start(call::WasapiCaptureHandler* handler, int& outSampleRate)
{
    return m_capture.Start(handler, outSampleRate);
}

void VoiceWasapiCaptureDevice::Stop()
{
    m_capture.Stop();
}

VoiceRecorder::VoiceRecorder()
    : m_device(NULL)
    , m_listener(NULL)
    , m_recording(false)
    , m_paused(false)
    , m_deviceRate(0)
    , m_limitReached(false)
    , m_resampling(false)
    , m_samples16k(0)
    , m_preSkip48k(0)
    , m_levelSquareSum(0.0)
    , m_levelSamples(0)
{
}

VoiceRecorder::~VoiceRecorder()
{
    //析构时还在录音：停止设备、丢弃内容，避免采集线程回调到已销毁的对象
    Cancel();
}

bool VoiceRecorder::Start(IVoiceCaptureDevice* device, IVoiceRecorderListener* listener, const std::wstring& outPath)
{
    if (device == NULL || outPath.empty())
        return false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_recording || m_device != NULL)
        {
            LOG_WARNING("voice record: start ignored, already recording.");
            return false;
        }
        if (!m_codec.Init(kVoiceSampleRate, kVoiceSamplesPerFrame, kVoiceBitrateBps))
        {
            LOG_ERROR("voice record: opus encoder init failed.");
            return false;
        }
        m_preSkip48k = m_codec.EncoderLookahead() * kGranulePer16k;
        //Ogg 逻辑流序列号取开机毫秒数与对象地址的组合，只要求同一文件内一致
        const uint32_t serial = static_cast<uint32_t>(::GetTickCount())
            ^ static_cast<uint32_t>(reinterpret_cast<uintptr_t>(this));
        m_writer.Begin(serial, kVoiceSampleRate, m_preSkip48k);
        m_device = device;
        m_listener = listener;
        m_path = outPath;
        m_limitReached = false;
        m_pending16k.clear();
        m_samples16k = 0;
        m_frameLevels.clear();
        m_levelSquareSum = 0.0;
        m_levelSamples = 0;
        //设备采样率未知之前不接收数据：m_recording 等设备打开之后再置位
        m_recording = false;
    }

    //打开设备时不持锁：设备可能在 Start 返回之前就开始回调，回调要取这把锁
    int deviceRate = 0;
    const bool opened = device->Start(this, deviceRate);
    std::lock_guard<std::mutex> lock(m_mutex);
    bool ok = opened && deviceRate > 0;
    if (ok)
    {
        m_resampling = (deviceRate != kVoiceSampleRate);
        if (m_resampling && !m_resampler.Init(deviceRate, kVoiceSampleRate, kResampleQuality))
            ok = false;
    }
    if (!ok)
    {
        LOG_ERROR("voice record: capture device start failed, opened=%d, rate=%d.", opened ? 1 : 0, deviceRate);
        if (opened)
            device->Stop();
        resetLocked();
        return false;
    }
    m_recording = true;
    m_deviceRate = deviceRate;
    LOG_INFO("voice record: started, device rate %d Hz, pre-skip %d.", deviceRate, m_preSkip48k);
    return true;
}

bool VoiceRecorder::Stop(VoiceRecordResult& result)
{
    if (!stopDevice())
        return false;

    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_samples16k <= 0)
    {
        LOG_WARNING("voice record: stopped with no audio captured.");
        resetLocked();
        return false;
    }
    //最后不足一帧的部分补 0 编成一帧
    if (!m_pending16k.empty())
    {
        m_pending16k.resize(static_cast<size_t>(kVoiceSamplesPerFrame), 0);
        encodeFrameLocked(&m_pending16k[0], true);
        m_pending16k.clear();
    }
    //编码器的输出比输入延后 lookahead 个采样：再编静音帧，直到解码出的总采样数覆盖全部录音，
    //否则最后一小段声音解码不出来。这些静音帧不计入波形
    const int64_t needed48k = m_samples16k * kGranulePer16k + m_preSkip48k;
    const std::vector<int16_t> silence(static_cast<size_t>(kVoiceSamplesPerFrame), 0);
    while (static_cast<int64_t>(m_writer.PacketCount()) * kOpusSamplesPerFrame48k < needed48k)
    {
        const int before = m_writer.PacketCount();
        encodeFrameLocked(&silence[0], false);
        //编码失败时不再重试，避免死循环
        if (m_writer.PacketCount() == before)
            break;
    }
    //按实际录入的采样数裁剪，解码端丢掉补的静音
    m_writer.Finish(m_samples16k * kGranulePer16k);
    const bool saved = SaveBytesToFile(m_path, m_writer.Bytes());
    if (!saved)
    {
        LOG_ERROR("voice record: write file failed, bytes=%u.", static_cast<unsigned>(m_writer.Bytes().size()));
        resetLocked();
        return false;
    }
    result.m_path = m_path;
    result.m_durationMs = m_samples16k * kMillisPerSecond / kVoiceSampleRate;
    result.m_fileBytes = static_cast<int64_t>(m_writer.Bytes().size());
    result.m_wave = BuildVoiceWave(m_frameLevels, kVoiceWaveBars);
    LOG_INFO("voice record: stopped, duration %lld ms, %d packets, %lld bytes, limit reached %d.",
             static_cast<long long>(result.m_durationMs), m_writer.PacketCount(),
             static_cast<long long>(result.m_fileBytes), m_limitReached ? 1 : 0);
    resetLocked();
    return true;
}

void VoiceRecorder::Cancel()
{
    if (!stopDevice())
        return;
    std::lock_guard<std::mutex> lock(m_mutex);
    LOG_INFO("voice record: cancelled after %lld ms.",
             static_cast<long long>(m_samples16k * kMillisPerSecond / kVoiceSampleRate));
    resetLocked();
}

bool VoiceRecorder::Pause()
{
    IVoiceCaptureDevice* device = NULL;
    int64_t recordedMs = 0;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_recording || m_paused || m_device == NULL)
            return false;
        //先拒收之后的数据，再停止设备；编码器、已录的内容与当前音量区间都保留，继续时接着用
        m_recording = false;
        m_paused = true;
        device = m_device;
        recordedMs = m_samples16k * kMillisPerSecond / kVoiceSampleRate;
    }
    //停止设备时不持锁：设备的 Stop 会等采集线程结束，而采集线程回调时要取这把锁
    device->Stop();
    LOG_INFO("voice record: paused at %lld ms.", static_cast<long long>(recordedMs));
    return true;
}

bool VoiceRecorder::SnapshotPaused(const std::wstring& path, int64_t& outDurationMs)
{
    outDurationMs = 0;
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_paused || m_writer.PacketCount() <= 0)
        return false;
    //已编出的包能解码出的有效采样数（48 kHz 计，扣掉 pre-skip）；录入的采样比它多出的部分还留在编码器里，
    //要等之后的帧才编得出来，另存的文件里没有这一小段
    const int64_t encoded48k =
        static_cast<int64_t>(m_writer.PacketCount()) * kOpusSamplesPerFrame48k - m_preSkip48k;
    int64_t valid48k = m_samples16k * kGranulePer16k;
    if (encoded48k < valid48k)
        valid48k = encoded48k;
    if (valid48k <= 0)
        return false;
    //复制一份写入器另行结束：原写入器不动，继续录音时接着往里写
    VoiceOggWriter snapshot = m_writer;
    snapshot.Finish(valid48k);
    if (!SaveBytesToFile(path, snapshot.Bytes()))
    {
        LOG_ERROR("voice record: write paused snapshot failed, bytes=%u.",
                  static_cast<unsigned>(snapshot.Bytes().size()));
        return false;
    }
    outDurationMs = valid48k * kMillisPerSecond / kOpusGranuleRate;
    LOG_INFO("voice record: paused snapshot written, %lld ms of %lld ms recorded, %d packets.",
             static_cast<long long>(outDurationMs),
             static_cast<long long>(m_samples16k * kMillisPerSecond / kVoiceSampleRate), m_writer.PacketCount());
    return true;
}

bool VoiceRecorder::Resume()
{
    IVoiceCaptureDevice* device = NULL;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_paused || m_device == NULL)
            return false;
        device = m_device;
    }
    //打开设备时不持锁：设备可能在 Start 返回之前就开始回调，回调要取这把锁（此时仍在暂停，回调直接返回）
    int deviceRate = 0;
    const bool opened = device->Start(this, deviceRate);
    bool ok = opened && deviceRate > 0;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (ok && deviceRate != m_deviceRate)
        {
            //采样率与暂停前不同（如换了默认麦克风）：按新的采样率重建重采样器，旧重采样器里积压的少量采样丢弃
            m_resampler.Close();
            m_resampling = (deviceRate != kVoiceSampleRate);
            if (m_resampling && !m_resampler.Init(deviceRate, kVoiceSampleRate, kResampleQuality))
                ok = false;
            else
                m_deviceRate = deviceRate;
        }
        if (ok)
        {
            m_paused = false;
            m_recording = true;
            LOG_INFO("voice record: resumed at %lld ms, device rate %d Hz.",
                     static_cast<long long>(m_samples16k * kMillisPerSecond / kVoiceSampleRate), deviceRate);
            return true;
        }
    }
    //失败时保持暂停；已经打开的设备在锁外关闭，理由同 Pause
    LOG_ERROR("voice record: resume failed, opened=%d, rate=%d.", opened ? 1 : 0, deviceRate);
    if (opened)
        device->Stop();
    return false;
}

bool VoiceRecorder::IsPaused() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_paused;
}

bool VoiceRecorder::IsRecording() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_recording || m_paused;
}

int64_t VoiceRecorder::RecordedMs() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_samples16k * kMillisPerSecond / kVoiceSampleRate;
}

void VoiceRecorder::OnAudioCaptured(const int16_t* mono, int frames)
{
    if (mono == NULL || frames <= 0)
        return;
    //音量与录满的通知在释放锁之后发出，避免通知对象回调录音器时与这把锁交叉
    std::vector<int> levels;
    bool notifyLimit = false;
    IVoiceRecorderListener* listener = NULL;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_recording || m_limitReached)
            return;
        const int16_t* src = mono;
        int count = frames;
        if (m_resampling)
        {
            m_resampled.clear();
            m_resampler.Process(mono, frames, m_resampled);
            if (m_resampled.empty())
                return;
            src = &m_resampled[0];
            count = static_cast<int>(m_resampled.size());
        }
        for (int i = 0; i < count; ++i)
        {
            //已录满：之后的音频丢弃
            if (m_samples16k >= kMaxSamples16k)
                break;
            const int16_t v = src[i];
            m_pending16k.push_back(v);
            ++m_samples16k;
            m_levelSquareSum += static_cast<double>(v) * v;
            ++m_levelSamples;
            if (m_levelSamples >= kLevelWindowSamples)
            {
                levels.push_back(VoiceLevelFromRms(sqrt(m_levelSquareSum / m_levelSamples)));
                m_levelSquareSum = 0.0;
                m_levelSamples = 0;
            }
            if (m_pending16k.size() >= static_cast<size_t>(kVoiceSamplesPerFrame))
            {
                encodeFrameLocked(&m_pending16k[0], true);
                m_pending16k.clear();
            }
        }
        if (m_samples16k >= kMaxSamples16k)
        {
            m_limitReached = true;
            notifyLimit = true;
            LOG_INFO("voice record: reached the %d second limit, waiting for confirmation.", kVoiceMaxSeconds);
        }
        listener = m_listener;
    }
    if (listener == NULL)
        return;
    for (size_t i = 0; i < levels.size(); ++i)
        listener->OnVoiceLevel(levels[i]);
    if (notifyLimit)
        listener->OnVoiceLimitReached();
}

void VoiceRecorder::encodeFrameLocked(const int16_t* pcm, bool countLevel)
{
    std::vector<unsigned char> packet;
    if (m_codec.Encode(pcm, kVoiceSamplesPerFrame, packet) && !packet.empty())
        m_writer.AddPacket(&packet[0], packet.size(), kOpusSamplesPerFrame48k);
    else
        LOG_WARNING("voice record: opus encode failed at %lld samples.", static_cast<long long>(m_samples16k));
    if (countLevel)
        m_frameLevels.push_back(VoiceLevelFromRms(rmsOf(pcm, kVoiceSamplesPerFrame)));
}

bool VoiceRecorder::stopDevice()
{
    IVoiceCaptureDevice* device = NULL;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_device == NULL)
            return false;
        device = m_device;
        //先拒收之后的数据，再停止设备
        m_recording = false;
    }
    device->Stop();
    return true;
}

void VoiceRecorder::resetLocked()
{
    m_device = NULL;
    m_listener = NULL;
    m_path.clear();
    m_recording = false;
    m_paused = false;
    m_deviceRate = 0;
    m_limitReached = false;
    m_resampling = false;
    m_resampler.Close();
    m_codec.Close();
    m_pending16k.clear();
    m_resampled.clear();
    m_samples16k = 0;
    m_preSkip48k = 0;
    m_frameLevels.clear();
    m_levelSquareSum = 0.0;
    m_levelSamples = 0;
}

uint8_t VoiceLevelFromRms(double rms)
{
    if (rms <= 0.0)
        return 0;
    const double db = kDbPerLog10 * log10(rms / kFullScale);
    if (db <= kVoiceLevelFloorDb)
        return 0;
    const double level = (db - kVoiceLevelFloorDb) / (-kVoiceLevelFloorDb) * kVoiceLevelMax + kHalf;
    if (level >= kVoiceLevelMax)
        return static_cast<uint8_t>(kVoiceLevelMax);
    return static_cast<uint8_t>(level);
}

std::vector<uint8_t> BuildVoiceWave(const std::vector<uint8_t>& frameLevels, int count)
{
    if (count <= 0)
        return std::vector<uint8_t>();
    std::vector<uint8_t> wave(static_cast<size_t>(count), 0);
    const size_t n = frameLevels.size();
    if (n == 0)
        return wave;
    for (int b = 0; b < count; ++b)
    {
        //第 b 个区间对应的帧 [begin, end)；帧数少于 count 时区间为空，取 begin 那一帧
        size_t begin = static_cast<size_t>(b) * n / static_cast<size_t>(count);
        size_t end = static_cast<size_t>(b + 1) * n / static_cast<size_t>(count);
        if (begin >= n)
            begin = n - 1;
        if (end <= begin)
            end = begin + 1;
        uint8_t peak = 0;
        for (size_t i = begin; i < end && i < n; ++i)
        {
            if (frameLevels[i] > peak)
                peak = frameLevels[i];
        }
        wave[static_cast<size_t>(b)] = peak;
    }
    return wave;
}

} // namespace voicemsg
