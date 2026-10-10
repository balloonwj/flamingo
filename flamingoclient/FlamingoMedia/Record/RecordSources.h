/**
 *  独立录屏的画面与声音来源，RecordSources.h
 *
 *  独立录屏（docs/屏幕共享与录制方案.md 6.1、第 7 期 7b）不经过共享的推流管线，画面与声音由这里的几个类直接采集：
 *    - RecordCaptureSource：自己的采集线程，整屏用 DXGI、单个窗口用 Windows.Graphics.Capture（与共享相同的采集器），
 *      自选区域从所在显示器的画面中裁出；保留最新一帧供录制取用；
 *    - RecordMicSource：默认麦克风，重采样到 48 kHz 后推进录制的混音器；
 *    - RecordSystemAudioSource：电脑声音（排除 Flamingo 进程树的回环采集），推进混音器；
 *    - RecordCameraSource：本机摄像头的预览（不编码、不发送），作画中画。
 *
 *  典型用法：
 *      m_capture.Start(spec, regionRect);   m_session.Start(opt, &m_capture, &m_camera, this);
 *      m_mic.Start(&m_session.Audio());      m_system.Start(&m_session.Audio());
 *
 *  balloonwj@qq.com   2026-10-03
 */
#pragma once

#include <stdint.h>
#include <atomic>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>
#include <winsock2.h>   //须在 windows.h 之前：下面的共享头文件用到 winsock2
#include <windows.h>

#include "RecordAudioMixer.h"
#include "RecordSession.h"
#include "RecordMouseEffects.h"   // IRecordSourceGeometry（独立录屏的鼠标效果，第 16 期）
#include "../Call/SpeexResampler.h"
#include "../Call/WasapiAudioIo.h"
#include "../Share/ShareCameraSender.h"
#include "../Share/ShareHostStreamer.h"
#include "../Share/ShareSystemAudioCapture.h"

namespace screenrecord {

//屏幕、窗口或区域的采集（自己的采集线程）
class RecordCaptureSource : public IRecordVideoSource, public IRecordSourceGeometry
{
public:
    RecordCaptureSource();
    ~RecordCaptureSource();

    RecordCaptureSource(const RecordCaptureSource&) = delete;
    RecordCaptureSource& operator=(const RecordCaptureSource&) = delete;

    /**
     *  开始采集。
     *  @param spec   采集对象：整块显示器或单个窗口（与共享的 ShareSourceSpec 相同）
     *  @param region 自选区域（屏幕坐标，限定在 spec 的显示器内）；为空矩形时不裁剪
     *  @return 采集线程已起返回 true（采集器建不起来时线程会退出，IsFailed 返回 true）
     */
    bool Start(const screenshare::ShareSourceSpec& spec, const RECT& region);

    //停止采集；可重复调用
    void Stop();

    //采集器是否已失败（建不起来、被采集的窗口已关闭）
    bool IsFailed() const;

    //输出画面的尺寸（区域为区域大小，整屏为显示器大小，窗口为开始时的窗口大小）
    int Width() const;
    int Height() const;

    virtual bool GetRecordFrame(std::vector<uint8_t>& bgra, int& width, int& height, uint64_t& serial) override;

    /**
     *  见 IRecordSourceGeometry（第 16 期）：录制画面在屏幕上的矩形。整屏为显示器矩形，框选区域为显示器矩形加上裁剪矩形，
     *  单个窗口为采集器最近一次报告的窗口矩形。任意线程可调用。
     */
    virtual RECT SourceScreenRect() const override;

private:
    void threadProc();

    screenshare::ShareSourceSpec m_spec;     //采集对象
    RECT                         m_crop;     //相对显示器左上角的裁剪矩形；为空时不裁
    int                          m_width;    //输出宽
    int                          m_height;   //输出高
    std::unique_ptr<std::thread> m_thread;   //采集线程
    std::atomic<bool>            m_stop;     //通知采集线程退出
    std::atomic<bool>            m_failed;   //采集是否已失败
    mutable std::mutex           m_mutex;    //保护以下三项
    std::vector<uint8_t>         m_latest;   //最新一帧（自上而下的 BGRA，行跨度 = 宽 × 4）
    int                          m_latestW;  //最新一帧的宽
    int                          m_latestH;  //最新一帧的高
    uint64_t                     m_serial;   //帧序号
    RECT                         m_windowRect;   //单个窗口：采集器最近一次报告的窗口矩形（屏幕坐标，受 m_mutex 保护）
};

//默认麦克风 → 48 kHz → 录制混音器
class RecordMicSource : private call::WasapiCaptureHandler
{
public:
    RecordMicSource();
    ~RecordMicSource();

    /**
     *  打开默认麦克风。
     *  @param mixer 推入哪个混音器（record_audio_mic 一路），不拥有，Stop 之前须一直有效
     *  @return 麦克风打开返回 true
     */
    bool Start(RecordAudioMixer* mixer);
    void Stop();

private:
    virtual void OnAudioCaptured(const int16_t* mono, int frames) override;

    call::WasapiCapture    m_capture;     //麦克风采集
    call::SpeexResampler   m_resampler;   //设备采样率 → 48 kHz（设备本就是 48 kHz 时不用）
    bool                   m_resampling;  //是否需要重采样
    std::atomic<bool>      m_ready;       //重采样配好之前采集线程交来的声音丢弃
    std::vector<int16_t>   m_out;         //重采样的输出（只在采集线程使用）
    RecordAudioMixer*      m_mixer;       //混音器（不拥有）
};

//电脑声音（排除本进程树的回环）→ 录制混音器
class RecordSystemAudioSource : private screenshare::ShareSystemAudioHandler
{
public:
    RecordSystemAudioSource();
    ~RecordSystemAudioSource();

    //开始采集，推入 mixer 的 record_audio_system 一路；mixer 不拥有
    bool Start(RecordAudioMixer* mixer);
    void Stop();

private:
    virtual void OnSystemAudio(const int16_t* mono, int samples) override;

    screenshare::ShareSystemAudioCapture m_capture;   //回环采集
    RecordAudioMixer*                    m_mixer;     //混音器（不拥有）
};

//本机摄像头的预览（画中画）
class RecordCameraSource : public IRecordVideoSource
{
public:
    RecordCameraSource();
    ~RecordCameraSource();

    //打开摄像头（只取预览，不编码、不发送）；失败时 FailReason 说明原因
    bool Start();
    void Stop();
    int FailReason() const;

    virtual bool GetRecordFrame(std::vector<uint8_t>& bgra, int& width, int& height, uint64_t& serial) override;

private:
    screenshare::ShareCameraSender m_camera;   //摄像头（本地预览模式）
};

} // namespace screenrecord
