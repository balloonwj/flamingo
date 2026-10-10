/**
 *  一次录制，见 RecordSession.h。
 *  balloonwj@qq.com   2026-10-03
 */
#include "RecordSession.h"

#include <windows.h>
#include <objbase.h>
#include <mfapi.h>

#include "RecordFrameComposer.h"
#include "RecordMp4Writer.h"
#include "RecordPolicy.h"
#include "../Call/AudioPcmUtils.h"
#include "../../Source/Common/Log/IULog.h"   //须在 windows.h 之后

namespace screenrecord {

namespace {

//每秒的 100 纳秒数、每毫秒的 100 纳秒数
const int64_t kHnsPerSecond = 10000000;
const int64_t kHnsPerMs = 10000;
//声音每次写 20 毫秒（48 kHz 下 960 个采样帧）
const int kAudioChunkFrames = 960;
const int64_t kAudioChunkHns = 200000;
//写入线程空闲时的等待（毫秒）
const DWORD kIdleSleepMs = 4;
//暂停时的等待（毫秒）
const DWORD kPausedSleepMs = 20;

//默认的磁盘剩余空间查询：文件所在目录所在的卷
uint64_t defaultFreeSpace(const std::wstring& dir)
{
    ULARGE_INTEGER avail;
    avail.QuadPart = 0;
    if (!::GetDiskFreeSpaceExW(dir.c_str(), &avail, NULL, NULL))
        return 0;
    return avail.QuadPart;
}

std::wstring dirOf(const std::wstring& path)
{
    const size_t pos = path.find_last_of(L"\\/");
    if (pos == std::wstring::npos)
        return std::wstring(L".");
    return path.substr(0, pos + 1);
}

int64_t nowHns()
{
    LARGE_INTEGER freq;
    LARGE_INTEGER now;
    ::QueryPerformanceFrequency(&freq);
    ::QueryPerformanceCounter(&now);
    return static_cast<int64_t>(now.QuadPart / freq.QuadPart) * kHnsPerSecond +
           static_cast<int64_t>(now.QuadPart % freq.QuadPart) * kHnsPerSecond / freq.QuadPart;
}

} // namespace

const char* RecordWriterStageName(int stage)
{
    switch (stage)
    {
    //写入线程还没有启动
    case record_stage_not_started:
        return "not_started";
    //创建文件、初始化合成器与编码器
    case record_stage_opening:
        return "opening";
    //两轮之间或暂停期间的等待
    case record_stage_idle:
        return "idle";
    //向画面来源取最新一帧
    case record_stage_get_frame:
        return "get_frame";
    //装饰与合成画面
    case record_stage_compose:
        return "compose";
    //编码器写一帧画面
    case record_stage_write_video:
        return "write_video";
    //从混音器取声音（含语音字幕的分路旁路）
    case record_stage_pull_audio:
        return "pull_audio";
    //编码器写一段声音
    case record_stage_write_audio:
        return "write_audio";
    //查询磁盘剩余空间
    case record_stage_disk_check:
        return "disk_check";
    //关闭文件
    case record_stage_closing:
        return "closing";
    //回报录制结束
    case record_stage_notifying:
        return "notifying";
    //写入线程即将退出
    case record_stage_done:
        return "done";
    //不认识的取值
    default:
        return "unknown";
    }
}

bool RecordStopStallReportDue(int64_t waitedMs, int64_t lastReportMs, int firstMs, int intervalMs)
{
    if (lastReportMs < 0)
        return waitedMs >= firstMs;
    return waitedMs - lastReportMs >= intervalMs;
}

RecordOptions::RecordOptions()
    : m_sourceWidth(0)
    , m_sourceHeight(0)
    , m_fps(kRecordDefaultFps)
    , m_quality(record_quality_standard)
    , m_audioOnly(false)
{
}

RecordSession::RecordSession()
    : m_video(NULL)
    , m_pip(NULL)
    , m_listener(NULL)
    , m_stop(false)
    , m_running(false)
    , m_paused(false)
    , m_pipEnabled(false)
    , m_recordedHns(0)
    , m_failReason(record_fail_none)
    , m_freeSpaceFn(&defaultFreeSpace)
    , m_diskCheckMs(kRecordDiskCheckMs)
    , m_annotationsSet(false)
    , m_decorator(NULL)
    , m_writerStage(record_stage_not_started)
    , m_writerStageSinceMs(0)
    , m_writerExited(true)
    , m_stopStallFirstMs(kRecordStopStallFirstMs)
    , m_lastStopStallStage(-1)
{
}

void RecordSession::SetDecorator(IRecordFrameDecorator* decorator)
{
    m_decorator.store(decorator);
}

RecordSession::~RecordSession()
{
    Stop();
}

bool RecordSession::Start(const RecordOptions& options, IRecordVideoSource* video, IRecordVideoSource* pip,
                          RecordSessionListener* listener)
{
    m_failReason = record_fail_none;
    if (m_running.load() || m_thread)
    {
        m_failReason = record_fail_running;
        return false;
    }
    //只录声音（第 24 期）不要画面来源；录像要有画面来源与合法的尺寸
    const bool videoOk = options.m_audioOnly ||
                         (video != NULL && options.m_sourceWidth > 0 && options.m_sourceHeight > 0);
    if (options.m_path.empty() || !videoOk)
    {
        m_failReason = record_fail_argument;
        return false;
    }
    m_options = options;
    m_options.m_fps = ClampRecordFps(options.m_fps);
    if (!RecordCanStart(freeSpace()))
    {
        m_failReason = record_fail_disk_full;
        LOG_WARNING("record: not enough disk space to start.");
        return false;
    }
    m_video = options.m_audioOnly ? NULL : video;
    m_pip = pip;
    m_listener = listener;
    //上一段录制交来的笔迹不带进这一段：控制器的录制会话是成员对象、多次录制共用；画笔与共享中的录制
    //都在开始之后才交笔迹，这里清空不会丢掉这一段的笔迹
    {
        std::lock_guard<std::mutex> guard(m_annotMutex);
        m_annotations.clear();
        m_annotationsSet = false;
    }
    m_stop.store(false);
    m_paused.store(false);
    m_recordedHns.store(0);
    m_running.store(true);
    m_writerExited.store(false);
    //在起线程之前就记为「打开文件」：Start 返回后读到的步骤不会是「还没启动」
    enterStage(record_stage_opening);
    m_thread.reset(new std::thread(&RecordSession::threadProc, this));
    return true;
}

void RecordSession::Stop()
{
    m_stop.store(true);
    m_lastStopStallStage = -1;
    if (m_thread)
    {
        if (m_thread->joinable())
        {
            //写入线程当前在哪一步，先记一行：卡死时能分清是卡在 Stop 里，还是 Stop 根本没有被调用（BUG-120）
            LOG_INFO("record: stop requested, writer stage=%s for %lld ms.", RecordWriterStageName(m_writerStage.load()),
                     static_cast<long long>(WriterStageElapsedMs()));
            waitWriterExit();
            m_thread->join();
        }
        m_thread.reset();
    }
    m_running.store(false);
}

void RecordSession::waitWriterExit()
{
    const ULONGLONG begin = ::GetTickCount64();
    int64_t lastReport = -1;
    while (!m_writerExited.load())
    {
        const int64_t waited = static_cast<int64_t>(::GetTickCount64() - begin);
        if (RecordStopStallReportDue(waited, lastReport, m_stopStallFirstMs, kRecordStopStallIntervalMs))
        {
            lastReport = waited;
            const int stage = m_writerStage.load();
            m_lastStopStallStage = stage;
            LOG_WARNING("record: writer thread still running %lld ms after stop, stage=%s (%d) for %lld ms.",
                        static_cast<long long>(waited), RecordWriterStageName(stage), stage,
                        static_cast<long long>(WriterStageElapsedMs()));
        }
        ::Sleep(kRecordStopPollMs);
    }
    if (lastReport >= 0)
    {
        LOG_INFO("record: writer thread exited %lld ms after stop.",
                 static_cast<long long>(::GetTickCount64() - begin));
    }
}

void RecordSession::enterStage(int stage)
{
    m_writerStageSinceMs.store(static_cast<int64_t>(::GetTickCount64()));
    m_writerStage.store(stage);
}

int RecordSession::WriterStage() const
{
    return m_writerStage.load();
}

int64_t RecordSession::WriterStageElapsedMs() const
{
    if (m_writerStage.load() == record_stage_not_started)
        return 0;
    return static_cast<int64_t>(::GetTickCount64()) - m_writerStageSinceMs.load();
}

int RecordSession::LastStopStallStage() const
{
    return m_lastStopStallStage;
}

void RecordSession::SetStopStallWarnMs(int ms)
{
    m_stopStallFirstMs = (ms > 0) ? ms : kRecordStopStallFirstMs;
}

bool RecordSession::IsRunning() const
{
    return m_running.load();
}

void RecordSession::SetPaused(bool paused)
{
    m_paused.store(paused);
}

bool RecordSession::IsPaused() const
{
    return m_paused.load();
}

int64_t RecordSession::RecordedMs() const
{
    return m_recordedHns.load() / kHnsPerMs;
}

void RecordSession::SetPipPlacement(const RecordPipPlacement& placement)
{
    std::lock_guard<std::mutex> guard(m_pipMutex);
    m_pipPlacement = placement;
}

RecordPipPlacement RecordSession::PipPlacement() const
{
    std::lock_guard<std::mutex> guard(m_pipMutex);
    return m_pipPlacement;
}

RecordAudioMixer& RecordSession::Audio()
{
    return m_audio;
}

void RecordSession::SetAnnotations(const std::vector<screenshare::AnnotationStroke>& strokes)
{
    std::lock_guard<std::mutex> guard(m_annotMutex);
    m_annotations = strokes;
    m_annotationsSet = true;
}

void RecordSession::SetPipEnabled(bool on)
{
    m_pipEnabled.store(on);
}

int RecordSession::FailReason() const
{
    return m_failReason;
}

void RecordSession::SetFreeSpaceFn(FreeSpaceFn fn)
{
    m_freeSpaceFn = (fn != NULL) ? fn : &defaultFreeSpace;
}

void RecordSession::SetDiskCheckIntervalMs(int ms)
{
    m_diskCheckMs = ms > 0 ? ms : kRecordDiskCheckMs;
}

uint64_t RecordSession::freeSpace() const
{
    return m_freeSpaceFn(dirOf(m_options.m_path));
}

int64_t RecordSession::elapsedHns() const
{
    return m_recordedHns.load();
}

void RecordSession::threadProc()
{
    ::CoInitializeEx(NULL, COINIT_MULTITHREADED);
    ::MFStartup(MF_VERSION);
    int reason = record_stop_user;
    const bool audioOnly = m_options.m_audioOnly;
    int outW = 0;
    int outH = 0;
    //输出尺寸按画质（2026-10-07）：「高清」长边最高 2560，「标准」最高 1920
    if (!audioOnly)
        ComputeRecordSize(m_options.m_sourceWidth, m_options.m_sourceHeight, m_options.m_quality, outW, outH);
    RecordMp4Writer writer;
    RecordFrameComposer composer;
    const int fps = m_options.m_fps;
    const int64_t frameHns = kHnsPerSecond / fps;
    //只录声音（第 24 期）：不建合成器，文件里只有音频流
    const bool opened = audioOnly
        ? writer.OpenAudioOnly(m_options.m_path)
        : (composer.Init(outW, outH) &&
           writer.Open(m_options.m_path, outW, outH, fps, RecordVideoBitrate(m_options.m_quality, outW, outH)));
    if (!opened)
    {
        reason = record_stop_error;
    }
    else
    {
        if (audioOnly)
            LOG_INFO("record: audio-only recording started.");
        else
            LOG_INFO("record: started, %dx%d @%d fps.", outW, outH, fps);
        std::vector<uint8_t> frame;
        //交给画面装饰的副本（第 16 期）：原始画面留着，下一帧画面没变时还要从它重新装饰
        std::vector<uint8_t> decorated;
        int frameW = 0;
        int frameH = 0;
        uint64_t frameSerial = 0;
        std::vector<uint8_t> pipFrame;
        int pipW = 0;
        int pipH = 0;
        uint64_t pipSerial = 0;
        std::vector<screenshare::AnnotationStroke> strokes;
        std::vector<int16_t> mono(kAudioChunkFrames);
        std::vector<int16_t> stereo(static_cast<size_t>(kAudioChunkFrames) * kRecordAudioChannels);
        int64_t nextVideoHns = 0;
        int64_t audioHns = 0;
        int64_t lastTick = nowHns();
        int64_t recorded = 0;
        int64_t lastDiskCheck = recorded;
        bool wasPaused = false;
        while (!m_stop.load())
        {
            const int64_t tick = nowHns();
            const int64_t delta = tick - lastTick;
            lastTick = tick;
            //暂停：时长不前进；继续时丢掉暂停期间攒下的声音
            if (m_paused.load())
            {
                wasPaused = true;
                enterStage(record_stage_idle);
                ::Sleep(kPausedSleepMs);
                continue;
            }
            if (wasPaused)
            {
                wasPaused = false;
                m_audio.Clear();
                continue;
            }
            recorded += delta;
            m_recordedHns.store(recorded);
            //画面：到时刻就写一帧（落后多帧时只补最新的一帧）；只录声音时没有画面
            if (!audioOnly && recorded >= nextVideoHns)
            {
                enterStage(record_stage_get_frame);
                uint64_t serial = 0;
                int w = 0;
                int h = 0;
                std::vector<uint8_t> latest;
                if (m_video->GetRecordFrame(latest, w, h, serial) && serial != frameSerial && w > 0 && h > 0)
                {
                    frame.swap(latest);
                    frameW = w;
                    frameH = h;
                    frameSerial = serial;
                }
                const bool pipOn = m_pipEnabled.load() && m_pip != NULL;
                if (pipOn)
                {
                    std::vector<uint8_t> p;
                    uint64_t ps = 0;
                    if (m_pip->GetRecordFrame(p, w, h, ps) && ps != pipSerial && w > 0 && h > 0)
                    {
                        pipFrame.swap(p);
                        pipW = w;
                        pipH = h;
                        pipSerial = ps;
                    }
                }
                enterStage(record_stage_compose);
                bool drawAnnotations = false;
                {
                    std::lock_guard<std::mutex> guard(m_annotMutex);
                    if (m_annotationsSet)
                    {
                        strokes = m_annotations;
                        drawAnnotations = true;
                    }
                }
                //独立录屏的鼠标效果（第 16 期）：拷一份原始画面交给装饰，画完再合成
                const uint8_t* source = frame.empty() ? NULL : &frame[0];
                IRecordFrameDecorator* decorator = m_decorator.load();
                if (decorator != NULL && !frame.empty())
                {
                    decorated.assign(frame.begin(), frame.end());
                    decorator->Decorate(&decorated[0], frameW, frameH, tick / kHnsPerMs);
                    source = &decorated[0];
                }
                //画中画的摆放随屏幕上摄像头小窗的位置变化（2026-10-05）
                composer.SetPipPlacement(PipPlacement());
                composer.Compose(source, frameW, frameH, frameW * 4,
                                 drawAnnotations ? &strokes : NULL, (pipOn && !pipFrame.empty()) ? &pipFrame[0] : NULL,
                                 pipW, pipH);
                enterStage(record_stage_write_video);
                if (!writer.WriteVideo(composer.Pixels(), composer.Stride(), nextVideoHns, frameHns))
                {
                    reason = record_stop_error;
                    break;
                }
                nextVideoHns += frameHns;
                if (nextVideoHns + frameHns <= recorded)
                    nextVideoHns = (recorded / frameHns) * frameHns;
            }
            //声音：每 20 毫秒写一段
            while (audioHns + kAudioChunkHns <= recorded)
            {
                //带上这一段在录制时间轴上的时刻：语音字幕的分路旁路据此给字幕定时（docs/语音字幕方案.md 6.1）
                enterStage(record_stage_pull_audio);
                m_audio.Pull(&mono[0], mono.size(), audioHns / kHnsPerMs);
                call::MonoToStereoI16(&mono[0], &stereo[0], mono.size());
                enterStage(record_stage_write_audio);
                if (!writer.WriteAudio(&stereo[0], kAudioChunkFrames, audioHns))
                {
                    reason = record_stop_error;
                    break;
                }
                audioHns += kAudioChunkHns;
            }
            if (reason == record_stop_error)
                break;
            //磁盘空间
            if ((recorded - lastDiskCheck) / kHnsPerMs >= m_diskCheckMs)
            {
                lastDiskCheck = recorded;
                enterStage(record_stage_disk_check);
                if (!RecordCanContinue(freeSpace()))
                {
                    LOG_WARNING("record: disk space low, stopping.");
                    reason = record_stop_disk_full;
                    break;
                }
            }
            enterStage(record_stage_idle);
            ::Sleep(kIdleSleepMs);
        }
        enterStage(record_stage_closing);
        if (!writer.Close() && reason == record_stop_user)
            reason = record_stop_error;
    }
    const int64_t durationMs = m_recordedHns.load() / kHnsPerMs;
    LOG_INFO("record: stopped, reason=%d, duration=%lld ms.", reason, static_cast<long long>(durationMs));
    m_running.store(false);
    enterStage(record_stage_notifying);
    if (m_listener != NULL)
        m_listener->OnRecordStopped(reason, m_options.m_path, durationMs);
    ::MFShutdown();
    ::CoUninitialize();
    enterStage(record_stage_done);
    //须是最后一句：Stop 看到它之后就去 join，本线程已不再访问任何成员
    m_writerExited.store(true);
}

} // namespace screenrecord
