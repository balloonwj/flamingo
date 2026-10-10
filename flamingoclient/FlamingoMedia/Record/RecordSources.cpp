/**
 *  独立录屏的画面与声音来源，见 RecordSources.h。
 *  balloonwj@qq.com   2026-10-03
 */
#include "RecordSources.h"

#include <objbase.h>
#include <mfapi.h>
#include <string.h>

#include "../Share/ShareCapturer.h"
#include "../Share/ShareSources.h"
#include "../../Source/Common/Log/IULog.h"   //须在 windows.h 之后

namespace screenrecord {

namespace {

//每次取帧最多等待的时间（毫秒）
const UINT kCaptureWaitMs = 100;
//采集器失效后重建前的等待（毫秒）
const DWORD kRestartWaitMs = 200;
//BGRA 每像素字节数
const int kBgraBytes = 4;
//录制的内部采样率
const int kRate = 48000;
//重采样质量（0~10）
const int kResampleQuality = 5;

} // namespace

//==================== RecordCaptureSource ====================

RecordCaptureSource::RecordCaptureSource()
    : m_width(0)
    , m_height(0)
    , m_stop(false)
    , m_failed(false)
    , m_latestW(0)
    , m_latestH(0)
    , m_serial(0)
{
    memset(&m_windowRect, 0, sizeof(m_windowRect));
    memset(&m_crop, 0, sizeof(m_crop));
}

RecordCaptureSource::~RecordCaptureSource()
{
    Stop();
}

bool RecordCaptureSource::Start(const screenshare::ShareSourceSpec& spec, const RECT& region)
{
    Stop();
    m_spec = spec;
    memset(&m_crop, 0, sizeof(m_crop));
    if (spec.m_kind == screenshare::share_source_monitor)
    {
        const RECT& mon = spec.m_monitorRect;
        if (region.right > region.left && region.bottom > region.top)
        {
            //区域限定在显示器内，换成相对显示器左上角的坐标
            RECT r;
            if (!::IntersectRect(&r, &region, &mon))
                return false;
            m_crop.left = r.left - mon.left;
            m_crop.top = r.top - mon.top;
            m_crop.right = r.right - mon.left;
            m_crop.bottom = r.bottom - mon.top;
            m_width = m_crop.right - m_crop.left;
            m_height = m_crop.bottom - m_crop.top;
        }
        else
        {
            m_width = mon.right - mon.left;
            m_height = mon.bottom - mon.top;
        }
    }
    else
    {
        const RECT bounds = screenshare::GetShareWindowBounds(spec.m_hwnd);
        m_width = bounds.right - bounds.left;
        m_height = bounds.bottom - bounds.top;
    }
    if (m_width <= 0 || m_height <= 0)
        return false;
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        m_latest.clear();
        m_latestW = 0;
        m_latestH = 0;
        m_serial = 0;
    }
    m_failed.store(false);
    m_stop.store(false);
    m_thread.reset(new std::thread(&RecordCaptureSource::threadProc, this));
    return true;
}

void RecordCaptureSource::Stop()
{
    m_stop.store(true);
    if (m_thread)
    {
        if (m_thread->joinable())
            m_thread->join();
        m_thread.reset();
    }
}

bool RecordCaptureSource::IsFailed() const
{
    return m_failed.load();
}

int RecordCaptureSource::Width() const
{
    return m_width;
}

int RecordCaptureSource::Height() const
{
    return m_height;
}

bool RecordCaptureSource::GetRecordFrame(std::vector<uint8_t>& bgra, int& width, int& height, uint64_t& serial)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    if (m_latest.empty())
        return false;
    bgra = m_latest;
    width = m_latestW;
    height = m_latestH;
    serial = m_serial;
    return true;
}

RECT RecordCaptureSource::SourceScreenRect() const
{
    if (m_spec.m_kind == screenshare::share_source_monitor)
        return RecordSourceScreenRect(m_spec.m_monitorRect, m_crop);
    std::lock_guard<std::mutex> guard(m_mutex);
    return m_windowRect;
}

void RecordCaptureSource::threadProc()
{
    //采集器要求在采集线程上初始化 COM（Windows.Graphics.Capture 用 WinRT）
    ::CoInitializeEx(NULL, COINIT_MULTITHREADED);
    std::unique_ptr<screenshare::IShareCapturer> cap;
    const bool monitor = (m_spec.m_kind == screenshare::share_source_monitor);
    std::vector<uint8_t> buf;
    while (!m_stop.load())
    {
        if (!cap)
        {
            cap.reset(monitor ? screenshare::CreateMonitorCapturer(m_spec.m_adapter, m_spec.m_output, m_spec.m_monitorRect)
                              : screenshare::CreateWindowCapturer(m_spec.m_hwnd));
            if (!cap || !cap->Start())
            {
                LOG_ERROR("record: capturer start failed, kind=%d.", m_spec.m_kind);
                m_failed.store(true);
                break;
            }
        }
        screenshare::ShareCaptureFrame f;
        const int r = cap->Capture(kCaptureWaitMs, f);
        if (r == screenshare::share_capture_ok && f.m_data != NULL && f.m_width > 0 && f.m_height > 0)
        {
            //整帧或区域：裁好后存成连续的 BGRA
            int x0 = 0;
            int y0 = 0;
            int w = f.m_width;
            int h = f.m_height;
            if (m_crop.right > m_crop.left)
            {
                x0 = m_crop.left;
                y0 = m_crop.top;
                w = m_crop.right - m_crop.left;
                h = m_crop.bottom - m_crop.top;
                if (x0 + w > f.m_width)
                    w = f.m_width - x0;
                if (y0 + h > f.m_height)
                    h = f.m_height - y0;
            }
            if (w > 0 && h > 0)
            {
                buf.resize(static_cast<size_t>(w) * h * kBgraBytes);
                for (int y = 0; y < h; ++y)
                {
                    memcpy(&buf[static_cast<size_t>(y) * w * kBgraBytes],
                           f.m_data + static_cast<size_t>(y0 + y) * f.m_stride + static_cast<size_t>(x0) * kBgraBytes,
                           static_cast<size_t>(w) * kBgraBytes);
                }
                std::lock_guard<std::mutex> guard(m_mutex);
                m_latest.swap(buf);
                m_latestW = w;
                m_latestH = h;
                ++m_serial;
                //单个窗口：记下窗口在屏幕上的位置，鼠标效果按它换算光标坐标（第 16 期）
                if (!monitor)
                    m_windowRect = cap->SourceRect();
            }
        }
        else if (r == screenshare::share_capture_lost)
        {
            //锁屏、分辨率变化等：重建采集器
            cap->Stop();
            cap.reset();
            ::Sleep(kRestartWaitMs);
        }
        else if (r == screenshare::share_capture_closed || r == screenshare::share_capture_fatal)
        {
            LOG_WARNING("record: capture ended, result=%d.", r);
            m_failed.store(true);
            break;
        }
    }
    if (cap)
        cap->Stop();
    cap.reset();
    ::CoUninitialize();
}

//==================== RecordMicSource ====================

RecordMicSource::RecordMicSource()
    : m_resampling(false)
    , m_ready(false)
    , m_mixer(NULL)
{
}

RecordMicSource::~RecordMicSource()
{
    Stop();
}

bool RecordMicSource::Start(RecordAudioMixer* mixer)
{
    Stop();
    if (mixer == NULL)
        return false;
    m_mixer = mixer;
    int rate = 0;
    //设备打开后才知道采样率；配好重采样之前采集线程交来的声音都丢弃（m_ready）
    m_resampling = false;
    m_ready.store(false);
    if (!m_capture.Start(this, rate))
    {
        LOG_WARNING("record: open microphone failed.");
        m_mixer = NULL;
        return false;
    }
    if (rate != kRate)
    {
        m_resampling = m_resampler.Init(rate, kRate, kResampleQuality);
        if (!m_resampling)
        {
            //采样率不对又没法重采样：不录麦克风，避免音调错误
            LOG_WARNING("record: microphone resampler %d -> %d failed.", rate, kRate);
            m_capture.Stop();
            m_mixer = NULL;
            return false;
        }
    }
    m_ready.store(true);
    LOG_INFO("record: microphone started, %d Hz.", rate);
    return true;
}

void RecordMicSource::Stop()
{
    m_ready.store(false);
    m_capture.Stop();
    m_resampler.Close();
    m_resampling = false;
    m_mixer = NULL;
}

void RecordMicSource::OnAudioCaptured(const int16_t* mono, int frames)
{
    RecordAudioMixer* mixer = m_mixer;
    if (!m_ready.load() || mixer == NULL || mono == NULL || frames <= 0)
        return;
    if (!m_resampling)
    {
        mixer->Push(record_audio_mic, mono, static_cast<size_t>(frames));
        return;
    }
    m_out.clear();
    m_resampler.Process(mono, frames, m_out);
    if (!m_out.empty())
        mixer->Push(record_audio_mic, &m_out[0], m_out.size());
}

//==================== RecordSystemAudioSource ====================

RecordSystemAudioSource::RecordSystemAudioSource()
    : m_mixer(NULL)
{
}

RecordSystemAudioSource::~RecordSystemAudioSource()
{
    Stop();
}

bool RecordSystemAudioSource::Start(RecordAudioMixer* mixer)
{
    Stop();
    if (mixer == NULL)
        return false;
    m_mixer = mixer;
    if (!m_capture.Start(this))
    {
        LOG_WARNING("record: system audio capture failed.");
        m_mixer = NULL;
        return false;
    }
    LOG_INFO("record: system audio started, excluding self=%d.", m_capture.IsExcludingSelf() ? 1 : 0);
    return true;
}

void RecordSystemAudioSource::Stop()
{
    m_capture.Stop();
    m_mixer = NULL;
}

void RecordSystemAudioSource::OnSystemAudio(const int16_t* mono, int samples)
{
    RecordAudioMixer* mixer = m_mixer;
    if (mixer != NULL && mono != NULL && samples > 0)
        mixer->Push(record_audio_system, mono, static_cast<size_t>(samples));
}

//==================== RecordCameraSource ====================

RecordCameraSource::RecordCameraSource()
{
}

RecordCameraSource::~RecordCameraSource()
{
    Stop();
}

bool RecordCameraSource::Start()
{
    return m_camera.StartLocal();
}

void RecordCameraSource::Stop()
{
    m_camera.Stop();
}

int RecordCameraSource::FailReason() const
{
    return m_camera.FailReason();
}

bool RecordCameraSource::GetRecordFrame(std::vector<uint8_t>& bgra, int& width, int& height, uint64_t& serial)
{
    return m_camera.GetPreview(bgra, width, height, serial);
}

} // namespace screenrecord
